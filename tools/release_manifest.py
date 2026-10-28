"""The signed manifest of a release (UPDATES_PLAN.md, phase U2).

python release_manifest.py keygen [--out <folder>]       make the release signing key (once; keep it off GitHub)
python release_manifest.py build <folder> <tag> [--sign]  manifest.json of a game folder (what the release zip holds)
python release_manifest.py sign <manifest.json>           manifest.sig beside it (RSA-3072, PKCS#1 v1.5, SHA-256)
python release_manifest.py verify <manifest.json> [--pub <update_key.json>]
python release_manifest.py make-fixture <folder>          a throwaway key + signed sample, for tests/update_manifest_test.tscn

The game verifies with Godot's Crypto (it has no Ed25519; RSA with SHA-256 is what it can check), against
the public key shipped as godot/game/update_key.json. The signature covers manifest.json's exact bytes.

Each file of the manifest has a "source": "release" (attached to the game release by its flat name, "/" -> "__"),
"textures" (the permanent Textures release) or "retail" (id's own 1.32 paks, which only a first install brings).
"""
import os, sys, json, hashlib, base64, argparse, re, datetime, pathlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FORMAT = 1
KEY_BITS = 3072
ALGORITHM = "RSA-3072 PKCS1v15 SHA-256"
PUBLIC_KEY = os.path.join(ROOT, "godot", "game", "update_key.json")
SKIP_DIRS = {"Maps", ".update"}  # made by running the game from the export folder (published maps, a staged update), not part of it
TAG = re.compile(r"^v?\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$")


def private_key_path():
    return os.environ.get("Q3_RELEASE_KEY") or os.path.join(pathlib.Path.home(), ".q3remaster", "release_key.pem")


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def source_of(rel):
    if re.fullmatch(r"textures-\d+\.pck", rel):
        return "textures"
    if re.fullmatch(r"q3core/baseq3/pak\d\.pk3", rel):
        return "retail"
    return "release"


def list_files(folder):
    """The game folder's files as relative paths with "/": what the release zip holds, plus the texture packs."""
    out = []
    for d, dirs, files in os.walk(folder):
        if os.path.abspath(d) == os.path.abspath(folder):
            dirs[:] = [x for x in dirs if x not in SKIP_DIRS]
        for f in files:
            if f.endswith(".log") or f in ("manifest.json", "manifest.sig"):
                continue
            out.append(os.path.relpath(os.path.join(d, f), folder).replace(os.sep, "/"))
    return sorted(out)


def build(folder, tag, files=None, tags=None):
    """The manifest as bytes (indented JSON, sorted by path) for the files of `folder`. `tags` maps a path
    to the release that holds its asset when that isn't `tag` (the base pack stays on its base release)."""
    if not TAG.match(tag):
        sys.exit("release_manifest: %r isn't a version tag like v0.4.6-beta" % tag)
    entries, assets = [], set()
    for rel in files if files is not None else list_files(folder):
        full = os.path.join(folder, rel)
        e = {"path": rel, "size": os.path.getsize(full), "sha256": sha256_file(full), "source": source_of(rel)}
        if e["source"] == "release":
            e["asset"] = rel.replace("/", "__")
            if e["asset"] in assets:
                sys.exit("release_manifest: two files make the asset name " + e["asset"])
            assets.add(e["asset"])
            if tags and rel in tags:
                if not TAG.match(tags[rel]):
                    sys.exit("release_manifest: %r isn't a version tag" % tags[rel])
                e["tag"] = tags[rel]
        entries.append(e)
    m = {"format": FORMAT, "game": "Q3Remaster", "version": tag,
         "created": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"), "files": entries}
    return (json.dumps(m, indent=1) + "\n").encode("utf-8")


def _crypto():
    try:
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import padding, rsa
    except ImportError:
        sys.exit("release_manifest: pip install cryptography")
    return hashes, serialization, padding, rsa


def keygen(out=None):
    hashes, serialization, padding, rsa = _crypto()
    path = os.path.join(out, "release_key.pem") if out else private_key_path()
    if os.path.exists(path):
        sys.exit("release_manifest: %s exists; a second key would orphan every build signed with it" % path)
    key = rsa.generate_private_key(public_exponent=65537, key_size=KEY_BITS)
    pw = os.environ.get("Q3_RELEASE_KEY_PASSPHRASE")
    enc = serialization.BestAvailableEncryption(pw.encode()) if pw else serialization.NoEncryption()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, enc))
    pem = key.public_key().public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo).decode()
    return path, {"algorithm": ALGORITHM, "public_key": pem}


def sign(data, key_path=None):
    hashes, serialization, padding, rsa = _crypto()
    path = key_path or private_key_path()
    if not os.path.isfile(path):
        return None
    pw = os.environ.get("Q3_RELEASE_KEY_PASSPHRASE")
    key = serialization.load_pem_private_key(open(path, "rb").read(), pw.encode() if pw else None)
    return key.sign(data, padding.PKCS1v15(), hashes.SHA256())


def verify(data, sig, pub_pem):
    hashes, serialization, padding, rsa = _crypto()
    try:
        serialization.load_pem_public_key(pub_pem.encode()).verify(sig, data, padding.PKCS1v15(), hashes.SHA256())
        return True
    except Exception:
        return False


def write_release(folder, tag, outdir, tags=None):
    """manifest.json (+ manifest.sig when the key exists) in `outdir`; returns whether it was signed."""
    data = build(folder, tag, tags=tags)
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, "manifest.json"), "wb") as f:
        f.write(data)
    sig = sign(data)
    sig_path = os.path.join(outdir, "manifest.sig")
    if sig is None:
        if os.path.exists(sig_path):
            os.remove(sig_path)
        return False
    with open(sig_path, "wb") as f:
        f.write(sig)
    return True


def main():
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest="cmd", required=True)
    p = sp.add_parser("keygen"); p.add_argument("--out")
    p = sp.add_parser("build"); p.add_argument("folder"); p.add_argument("tag"); p.add_argument("--out"); p.add_argument("--sign", action="store_true")
    p = sp.add_parser("sign"); p.add_argument("manifest"); p.add_argument("--key")
    p = sp.add_parser("verify"); p.add_argument("manifest"); p.add_argument("--pub", default=PUBLIC_KEY)
    p = sp.add_parser("make-fixture"); p.add_argument("folder")
    a = ap.parse_args()
    if a.cmd == "keygen":
        path, pub = keygen(a.out)
        os.makedirs(os.path.dirname(PUBLIC_KEY), exist_ok=True)
        dest = os.path.join(a.out, "update_key.json") if a.out else PUBLIC_KEY
        with open(dest, "w", encoding="utf-8", newline="\n") as f:
            json.dump(pub, f, indent=1)
            f.write("\n")
        print("private key:", path, "(back it up somewhere safe, never commit it)")
        print("public key: ", dest, "(this is what the game trusts; commit it)")
    elif a.cmd == "build":
        out = a.out or a.folder
        print("signed" if write_release(a.folder, a.tag, out) else "UNSIGNED (no key at %s)" % private_key_path(), "->", out)
    elif a.cmd == "sign":
        data = open(a.manifest, "rb").read()
        sig = sign(data, a.key)
        if sig is None:
            sys.exit("release_manifest: no key at " + (a.key or private_key_path()))
        open(os.path.splitext(a.manifest)[0] + ".sig", "wb").write(sig)
        print("signed", a.manifest)
    elif a.cmd == "verify":
        pub = json.load(open(a.pub, encoding="utf-8"))["public_key"]
        ok = verify(open(a.manifest, "rb").read(), open(os.path.splitext(a.manifest)[0] + ".sig", "rb").read(), pub)
        print("signature", "ok" if ok else "BAD")
        sys.exit(0 if ok else 1)
    elif a.cmd == "make-fixture":
        os.makedirs(a.folder, exist_ok=True)
        key_dir = os.path.join(a.folder, "_key")
        path, pub = keygen(key_dir)
        os.environ["Q3_RELEASE_KEY"] = path
        sample = os.path.join(a.folder, "_game")
        os.makedirs(os.path.join(sample, "q3core", "baseq3"), exist_ok=True)
        for rel, body in [("Q3Remaster.exe", b"exe"), ("Q3Remaster.pck", b"pck" * 100), ("q3core/baseq3/qagamex86_64.dll", b"dll"),
                          ("q3core/baseq3/pak1.pk3", b"retail"), ("textures-1.pck", b"tex" * 50)]:
            with open(os.path.join(sample, rel), "wb") as f:
                f.write(body)
        write_release(sample, "v9.9.9-beta", a.folder)
        with open(os.path.join(a.folder, "update_key.json"), "w", encoding="utf-8", newline="\n") as f:
            json.dump(pub, f, indent=1)
            f.write("\n")
        import shutil
        shutil.rmtree(key_dir)       # nothing signs with this key again
        shutil.rmtree(sample)
        print("fixture in", a.folder)


if __name__ == "__main__":
    main()
