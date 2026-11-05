"""What the Linux game's cross build needs on Windows (build_engine.py --linux): zig as the C and C++
compiler for Linux, and a sysroot of Debian's headers and libraries for SDL2's and OpenAL Soft's back
ends, into build/cross/.

python engine/tools/linux_cross.py [--force]

zig (thirdparty.json "cross" "zig", checked by its SHA-256) brings glibc for Linux x86_64 at the version
named there (2.31: Ubuntu 20.04, Debian 11 and later), libc++, and clang. The sysroot is Debian 12's
packages pinned in "cross" "debian" (each file and its SHA-256; --pin makes the list from Debian's
index), unpacked without running anything.
Symbolic links in them become copies. Nothing of either ships but SDL2 and OpenAL Soft's own builds.
Prints the folders build_engine.py uses.
"""
import argparse, hashlib, io, json, lzma, os, posixpath, re, shutil, sys, tarfile, urllib.request, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
PINS = json.load(open(os.path.join(ROOT, "engine", "thirdparty.json"), encoding="utf-8"))["cross"]
OUT = os.path.join(ROOT, "build", "cross")
DL = os.path.join(OUT, "dl")
SYSROOT = os.path.join(OUT, "sysroot")
UA = "OzArenaBuild/1.0 (+https://github.com/ozmiumofficial/oz-arena)"


def get(url, dest, sha256=None):
    if os.path.exists(dest) and (not sha256 or digest(dest) == sha256):
        return dest
    print("> fetch", url, file=sys.stderr, flush=True)
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=120) as r, open(dest + ".part", "wb") as f:
        shutil.copyfileobj(r, f, 1 << 20)
    if sha256 and digest(dest + ".part") != sha256:
        os.remove(dest + ".part")
        sys.exit("%s: its SHA-256 isn't the pinned one" % url)
    os.replace(dest + ".part", dest)
    return dest


def digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def zig():
    z = PINS["zig"]
    home = os.path.join(OUT, "zig-" + z["version"])
    exe = os.path.join(home, "zig.exe")
    if not os.path.exists(exe):
        archive = get(z["url"], os.path.join(DL, os.path.basename(z["url"])), z["sha256"])
        tmp = home + ".tmp"
        shutil.rmtree(tmp, ignore_errors=True)
        with zipfile.ZipFile(archive) as zf:
            for m in zf.namelist():   # never outside the folder
                if m.startswith("/") or ".." in m.split("/"):
                    sys.exit("%s: a path outside its folder" % m)
            zf.extractall(tmp)
        (top,) = os.listdir(tmp)
        os.replace(os.path.join(tmp, top), home)
        os.rmdir(tmp)
    return exe


def ar_members(data):
    """A .deb is an ar archive: its members' names and bytes."""
    if data[:8] != b"!<arch>\n":
        raise ValueError("not a .deb")
    p, out = 8, {}
    while p + 60 <= len(data):
        name = data[p:p + 16].decode().strip().rstrip("/")
        size = int(data[p + 48:p + 58].decode().strip())
        out[name] = data[p + 60:p + 60 + size]
        p += 60 + size + (size & 1)
    return out


def unpack_deb(path, root):
    members = ar_members(open(path, "rb").read())
    name = next(n for n in members if n.startswith("data.tar"))
    blob = members[name]
    mode = "r:xz" if name.endswith(".xz") else "r:gz" if name.endswith(".gz") else "r:zst" if name.endswith(".zst") else "r:"
    if mode == "r:zst":
        sys.exit("%s is zstd: not handled" % path)
    links = []
    with tarfile.open(fileobj=io.BytesIO(blob), mode=mode) as t:
        for m in t.getmembers():
            rel = posixpath.normpath(m.name.lstrip("./"))
            if rel.startswith("..") or rel.startswith("/") or rel == ".":
                continue
            # headers, libraries and pkg-config files only
            if not (rel.startswith("usr/include/") or rel.startswith("usr/lib/") or rel.startswith("lib/") or rel.startswith("usr/share/pkgconfig/")):
                continue
            dest = os.path.join(root, *rel.split("/"))
            if m.isdir():
                os.makedirs(dest, exist_ok=True)
            elif m.isfile():
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                with t.extractfile(m) as src, open(dest, "wb") as f:
                    shutil.copyfileobj(src, f)
            elif m.issym():
                links.append((rel, m.linkname))
    return links


def resolve_links(links, root):
    """Each link a copy of what it points at (a link to a link: as many rounds as it takes)."""
    for _ in range(4):
        left = []
        for rel, target in links:
            to = posixpath.normpath(posixpath.join(posixpath.dirname(rel), target)) if not target.startswith("/") else target.lstrip("/")
            src = os.path.join(root, *to.split("/"))
            dest = os.path.join(root, *rel.split("/"))
            if os.path.isfile(src):
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                shutil.copy2(src, dest)
            elif os.path.isdir(src):
                shutil.copytree(src, dest, dirs_exist_ok=True)
            else:
                left.append((rel, target))
        links = left
    for rel, target in links:   # pointing outside what was unpacked (docs, /etc): not needed
        print("  (left out link %s -> %s)" % (rel, target))


def index(suite):
    """Debian's current Packages index for suite: {package: its fields}."""
    idx = get("https://deb.debian.org/debian/dists/%s/main/binary-amd64/Packages.xz" % suite, os.path.join(DL, "Packages-%s.xz" % suite))
    pkgs = {}
    for block in lzma.decompress(open(idx, "rb").read()).decode("utf-8", "replace").split("\n\n"):
        f = dict(re.findall(r"^([A-Za-z0-9-]+): (.*)$", block, re.M))
        if "Package" in f:
            pkgs[f["Package"]] = f
    return pkgs


def pin(names):
    """thirdparty.json's "cross" "debian" "packages" for these packages, from Debian's index today."""
    d = PINS["debian"]
    if os.path.exists(os.path.join(DL, "Packages-%s.xz" % d["suite"])):
        os.remove(os.path.join(DL, "Packages-%s.xz" % d["suite"]))
    pkgs = index(d["suite"])
    out = []
    for n in names:
        if n not in pkgs:
            sys.exit("no %s in Debian %s" % (n, d["suite"]))
        out.append({"package": n, "version": pkgs[n]["Version"], "file": pkgs[n]["Filename"], "sha256": pkgs[n]["SHA256"]})
    print(json.dumps(out, indent=1))


def sysroot(force):
    """Each pinned .deb from deb.debian.org (or, once a point release drops it there, snapshot.debian.org),
    checked by its SHA-256, unpacked into build/cross/sysroot."""
    d = PINS["debian"]
    stamp = os.path.join(SYSROOT, ".oz_packages")
    want = "\n".join(p["sha256"] for p in d["packages"])
    if not force and os.path.exists(stamp) and open(stamp).read() == want:
        return SYSROOT
    shutil.rmtree(SYSROOT, ignore_errors=True)
    os.makedirs(SYSROOT)
    links = []
    for p in d["packages"]:
        dest = os.path.join(DL, os.path.basename(p["file"]))
        try:
            get("https://deb.debian.org/debian/" + p["file"], dest, p["sha256"])
        except Exception as e:   # gone from the mirror: the archive keeps every version
            print("  deb.debian.org: %s; trying snapshot.debian.org" % e)
            get("https://snapshot.debian.org/archive/debian/%s/%s" % (d["snapshot"], p["file"]), dest, p["sha256"])
        links += unpack_deb(dest, SYSROOT)
    resolve_links(links, SYSROOT)
    open(stamp, "w").write(want)
    return SYSROOT


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--force", action="store_true", help="unpack the sysroot again")
    ap.add_argument("--pin", nargs="+", metavar="PACKAGE", help="print pins for these Debian packages, from today's index")
    a = ap.parse_args()
    os.makedirs(DL, exist_ok=True)
    if a.pin:
        return pin(a.pin)
    print("zig:", zig())
    print("sysroot:", sysroot(a.force))


if __name__ == "__main__":
    main()
