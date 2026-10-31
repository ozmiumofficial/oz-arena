"""Rewrites one of game/patches from the edited game tree (thirdparty/ioq3).

python engine/tools/regen_game_patch.py 0015 [code/cgame/cg_event.c ...]

A game patch can't change lines an earlier patch added, or their context: the build checks each
patch alone (git apply --reverse --check). So code a patch created, like 0015's cg_oz_fx.c, is
changed by editing the tree and rewriting that patch:
  base   = ioq3 at its pinned commit + the patches before it, for the patch's files;
  target = the tree as it is (every patch applied, plus the edits) less the patches after it.
Its description (the text before the first diff) stays; files named on the command line join it.
The tree's working copy has CRLF (autocrlf), as the patches do. Run build_engine.py after: every
patch must still check as applied.
"""
import os, re, sys, glob, shutil, tempfile, subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TREE = os.path.join(ROOT, "thirdparty", "ioq3")
PATCHES = os.path.join(ROOT, "game", "patches")
CR, LF = b"\x0d", b"\x0a"


def run(args, cwd):
    r = subprocess.run(args, cwd=cwd, capture_output=True, text=True)
    if r.returncode:
        sys.exit("%s failed:\n%s%s" % (" ".join(args), r.stdout, r.stderr))
    return r.stdout


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    number = int(sys.argv[1])
    out = next((p for p in glob.glob(os.path.join(PATCHES, "%04d-*.patch" % number))), None)
    if not out:
        sys.exit("no patch %04d" % number)
    old = open(out, "rb").read().decode("utf-8")
    header = old[:old.index("diff --git")]
    files = re.findall(r"^diff --git a/(\S+) b/", old, re.M)
    files += [f for f in sys.argv[2:] if f not in files]
    s = tempfile.mkdtemp(prefix="regen_patch_")
    run(["git", "init", "-q"], s)
    run(["git", "config", "core.autocrlf", "false"], s)
    for f in files:
        if subprocess.run(["git", "cat-file", "-e", "HEAD:" + f], cwd=TREE, capture_output=True).returncode:
            continue	# made by this patch or an earlier one
        data = subprocess.run(["git", "show", "HEAD:" + f], cwd=TREE, capture_output=True).stdout
        os.makedirs(os.path.dirname(os.path.join(s, f)), exist_ok=True)
        open(os.path.join(s, f), "wb").write(data.replace(CR + LF, LF).replace(LF, CR + LF))
    inc = sum([["--include", f] for f in files], [])
    patches = sorted(glob.glob(os.path.join(PATCHES, "*.patch")))
    for p in patches:
        if int(os.path.basename(p)[:4]) < number:
            run(["git", "apply", "--whitespace=nowarn"] + inc + [p], s)
    run(["git", "add", "-A"], s)
    run(["git", "commit", "-q", "-m", "base", "--allow-empty"], s)
    for f in files:
        os.makedirs(os.path.dirname(os.path.join(s, f)), exist_ok=True)
        shutil.copyfile(os.path.join(TREE, f), os.path.join(s, f))
    for p in reversed(patches):
        if int(os.path.basename(p)[:4]) > number:
            run(["git", "apply", "-R", "--whitespace=nowarn"] + inc + [p], s)
    run(["git", "add", "-A", "-N", "."], s)
    body = subprocess.run(["git", "diff", "--no-color", "--src-prefix=a/", "--dst-prefix=b/"] + files, cwd=s,
                          capture_output=True).stdout.decode("utf-8")
    chunks = re.split(r"(?=^diff --git )", body, flags=re.M)
    by = {re.match(r"diff --git a/(\S+)", c).group(1): c for c in chunks if c.startswith("diff --git")}
    text = header + "".join(by[f] for f in files if f in by)
    text = re.sub(r"^index [0-9a-f]+\.\.[0-9a-f]+( \d+)?\r?\n", "", text, flags=re.M)
    data = text.encode("utf-8").replace(CR + LF, LF).replace(LF, CR + LF)
    open(out, "wb").write(data)
    shutil.rmtree(s, ignore_errors=True)
    print("%s: %d files%s" % (os.path.relpath(out, ROOT), len(by),
                              "; unchanged: " + ", ".join(f for f in files if f not in by) if len(by) < len(files) else ""))


if __name__ == "__main__":
    main()
