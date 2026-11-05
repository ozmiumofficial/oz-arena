"""The map editor's compile tools for the Linux game: q3map2 and mbspc from NetRadiant-custom's Linux
release, made to run on any x86_64 Linux, into build/tools/netradiant-custom-<date>/linux-app.

python engine/tools/linux_editor_tools.py [--force] [--pin-ubuntu <package> ...]

NetRadiant-custom (GPL-2.0) ships Linux as an AppImage built on Ubuntu 24.04: its q3map2 needs glibc
2.38 and the system's libstdc++, newer than many players have (the game itself needs 2.30). So the
tools go with everything they load: the libraries the AppImage bundles that q3map2 and mbspc need
(found by reading their ELF files), and Ubuntu 24.04's own glibc, libgcc, libstdc++ and zlib
(thirdparty.json "cross" "editor_tools", each package pinned by SHA-256). A small sh wrapper per tool
starts it through that glibc's loader (ld-linux-x86-64.so.2 --library-path), which runs on any x86_64
Linux kernel whatever glibc the system has; they're command-line programs, with no graphics drivers
to share with the system. The editor runs tools/q3map2 and tools/mbspc (ed_compile.cpp, OZ_ED_EXE).

The .7z, the AppImage's squashfs and Ubuntu's zstd packages are read with py7zr, PySquashfsImage and
zstandard, installed into a venv of their own (build/cross/py7z) at pinned versions; nothing is run
from what's downloaded. build_engine.py --linux runs this and stages the result as tools/.
"""
import argparse, io, json, lzma, os, posixpath, shutil, stat, struct, subprocess, sys, tarfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
PINS = json.load(open(os.path.join(ROOT, "engine", "thirdparty.json"), encoding="utf-8"))["cross"]["editor_tools"]
VENV = os.path.join(ROOT, "build", "cross", "py7z")
VENV_PY = os.path.join(VENV, "Scripts" if os.name == "nt" else "bin", "python" + (".exe" if os.name == "nt" else ""))
TOOLS = ["q3map2", "mbspc"]

sys.path.insert(0, HERE)
import linux_cross   # get(), ar_members()

WRAPPER = """#!/bin/sh
# Oz: NetRadiant-custom's {tool} with the libraries it loads and Ubuntu 24.04's glibc, started through
# that glibc's own loader so it runs whatever glibc this system has (engine/tools/linux_editor_tools.py)
here="$(dirname "$(readlink -f "$0")")"
LC_ALL=C GCONV_PATH="$here/lib/gconv" exec "$here/lib/ld-linux-x86-64.so.2" --library-path "$here/lib" \\
	--argv0 "$here/{tool}" "$here/bin/{tool}.x86_64" "$@"
"""


def in_venv():
    """Back into this script under the venv's Python, with the readers installed."""
    if os.path.abspath(sys.prefix) == os.path.abspath(VENV):
        return
    if not os.path.exists(VENV_PY):
        subprocess.run([sys.executable, "-m", "venv", VENV], check=True)
    have = subprocess.run([VENV_PY, "-m", "pip", "freeze"], capture_output=True, text=True).stdout.lower()
    want = PINS["readers"]
    if any(r.lower() not in have for r in want):
        subprocess.run([VENV_PY, "-m", "pip", "install", "-q", "--disable-pip-version-check"] + want, check=True)
    sys.exit(subprocess.run([VENV_PY, "-I", os.path.abspath(__file__)] + sys.argv[1:]).returncode)


def elf_needed(data):
    """An ELF file's DT_NEEDED names."""
    shoff, = struct.unpack_from("<Q", data, 0x28)
    shentsize, shnum = struct.unpack_from("<HH", data, 0x3A)
    secs = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shentsize) for i in range(shnum)]
    out = []
    for s in secs:
        if s[1] != 6:   # SHT_DYNAMIC
            continue
        strtab = secs[s[6]][4]
        for o in range(s[4], s[4] + s[5], 16):
            tag, val = struct.unpack_from("<qQ", data, o)
            if tag == 0:
                break
            if tag == 1:
                out.append(data[strtab + val:data.index(b"\0", strtab + val)].decode())
    return out


def appimage_files(appimage):
    """{name: bytes} of the AppImage's q3map2, mbspc and every bundled library they need."""
    from PySquashfsImage import SquashFsImage
    d = open(appimage, "rb").read()
    shoff, = struct.unpack_from("<Q", d, 0x28)
    shentsize, shnum = struct.unpack_from("<HH", d, 0x3A)
    sq = appimage + ".squashfs"
    open(sq, "wb").write(d[shoff + shentsize * shnum:])   # an AppImage: its loader's ELF, then the squashfs
    files = {}
    im = SquashFsImage.from_file(sq)
    try:
        for f in im:
            p = f.path
            if f.is_file and (p in ("/usr/bin/%s.x86_64" % t for t in TOOLS) or (p.startswith("/usr/lib/") and p.count("/") == 3 and ".so" in p)):
                files[p.split("/")[-1]] = f.read_bytes()
    finally:
        im.close()
        os.remove(sq)
    keep, todo = {}, ["%s.x86_64" % t for t in TOOLS]
    while todo:
        n = todo.pop()
        if n in keep:
            continue
        keep[n] = files[n]
        todo += [m for m in elf_needed(files[n]) if m in files]
    return keep


def ubuntu_files(wanted):
    """{name: bytes} of the named libraries from the pinned Ubuntu packages (links followed)."""
    import zstandard
    out = {}
    for p in PINS["ubuntu"]["packages"]:
        deb = linux_cross.get(PINS["ubuntu"]["mirror"] + p["file"], os.path.join(linux_cross.DL, os.path.basename(p["file"])), p["sha256"])
        members = linux_cross.ar_members(open(deb, "rb").read())
        name = next(n for n in members if n.startswith("data.tar"))
        blob = members[name]
        if name.endswith(".zst"):
            blob = zstandard.ZstdDecompressor().stream_reader(io.BytesIO(blob)).read()
        elif name.endswith(".xz"):
            blob = lzma.decompress(blob)
        with tarfile.open(fileobj=io.BytesIO(blob), mode="r:") as t:
            members = {posixpath.normpath(m.name.lstrip("./")): m for m in t.getmembers()}
            for rel, m in members.items():
                base = posixpath.basename(rel)
                if base not in wanted or base in out:
                    continue
                for _ in range(8):   # a link: what it points at, in the same package
                    if not m.issym():
                        break
                    to = posixpath.normpath(posixpath.join(posixpath.dirname(rel), m.linkname)) if not m.linkname.startswith("/") else m.linkname.lstrip("/")
                    if to not in members:   # usr-merged: lib64/ld-linux... -> /lib/x86_64-linux-gnu/... is usr/lib/...
                        to = "usr/" + to if ("usr/" + to) in members else to
                    rel, m = to, members.get(to)
                    if m is None:
                        break
                if m is not None and m.isfile():
                    out[base] = t.extractfile(m).read()
    missing = sorted(set(wanted) - set(out))
    if missing:
        sys.exit("not in the pinned Ubuntu packages: " + ", ".join(missing))
    return out


def pin_ubuntu(names):
    """thirdparty.json's "cross" "editor_tools" "ubuntu" "packages" from Ubuntu's indexes today."""
    u = PINS["ubuntu"]
    pkgs = {}
    for suite in u["suites"]:   # noble, then noble-updates over it
        idx = os.path.join(linux_cross.DL, "Packages-ubuntu-%s.xz" % suite)
        if os.path.exists(idx):
            os.remove(idx)
        linux_cross.get(u["mirror"] + "dists/%s/main/binary-amd64/Packages.xz" % suite, idx)
        for block in lzma.decompress(open(idx, "rb").read()).decode("utf-8", "replace").split("\n\n"):
            f = dict(l.split(": ", 1) for l in block.splitlines() if ": " in l and not l.startswith(" "))
            if "Package" in f:
                pkgs[f["Package"]] = f
    print(json.dumps([{"package": n, "version": pkgs[n]["Version"], "file": pkgs[n]["Filename"], "sha256": pkgs[n]["SHA256"]} for n in names], indent=1))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--force", action="store_true", help="make linux-app again")
    ap.add_argument("--pin-ubuntu", nargs="+", metavar="PACKAGE", help="print pins for these Ubuntu packages, from today's indexes")
    a = ap.parse_args()
    in_venv()
    os.makedirs(linux_cross.DL, exist_ok=True)
    if a.pin_ubuntu:
        return pin_ubuntu(a.pin_ubuntu)
    nr = PINS["netradiant"]
    home = os.path.join(ROOT, "build", "tools", "netradiant-custom-" + nr["release"])
    out = os.path.join(home, "linux-app")
    stamp = os.path.join(out, ".oz_pins")
    want = json.dumps(PINS, sort_keys=True)
    if not a.force and os.path.exists(stamp) and open(stamp).read() == want:
        print("editor tools:", out)
        return
    dl = os.path.join(home, "dl")
    os.makedirs(dl, exist_ok=True)
    archive = linux_cross.get(nr["url"], os.path.join(dl, os.path.basename(nr["url"])), nr["sha256"])
    import py7zr
    with py7zr.SevenZipFile(archive) as z:
        names = z.getnames()
        if names != [nr["appimage"]]:
            sys.exit("%s holds %s, not %s alone" % (archive, names, nr["appimage"]))
        z.extractall(dl)
    files = appimage_files(os.path.join(dl, nr["appimage"]))
    os.remove(os.path.join(dl, nr["appimage"]))
    system = set()
    for n, data in files.items():
        system |= {m for m in elf_needed(data) if m not in files}
    system |= {"ld-linux-x86-64.so.2"}
    files.update(ubuntu_files(system))

    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(os.path.join(out, "bin"))
    os.makedirs(os.path.join(out, "lib"))
    for n, data in files.items():
        dest = os.path.join(out, "bin" if n.endswith(".x86_64") else "lib", n)
        open(dest, "wb").write(data)
    for t in TOOLS:
        open(os.path.join(out, t), "w", newline="\n").write(WRAPPER.format(tool=t))
    open(os.path.join(out, "README-oz.txt"), "w", newline="\n").write(
        "The map editor's compile tools (q3map2, mbspc) from NetRadiant-custom %s's Linux release\n"
        "(GPL-2.0, https://github.com/Garux/netradiant-custom), with the libraries they load from it and\n"
        "Ubuntu 24.04's glibc, libgcc, libstdc++ and zlib (LGPL / GPL with the runtime exception / zlib;\n"
        "their sources: https://launchpad.net/ubuntu/+source/glibc, gcc-14, zlib). Each tool starts\n"
        "through that glibc's loader, so it runs on any x86_64 Linux. Made by\n"
        "engine/tools/linux_editor_tools.py.\n" % nr["release"])
    open(stamp, "w").write(want)
    size = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(out) for f in fs)
    print("editor tools: %s (%d files, %.1f MB): %s" % (out, len(files) + 2, size / 1e6, ", ".join(sorted(files))))


if __name__ == "__main__":
    main()
