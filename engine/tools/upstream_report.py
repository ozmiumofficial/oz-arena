"""What changed upstream since the last sync (ENGINE_PLAN.md, "Upstream tracking").

python engine/tools/upstream_report.py [--fetch] [--mark <upstream> [<commit>]]

For each engine in engine/thirdparty.json "upstream" it lists the commits on that project's
branch since the recorded sync point, limited to the paths Oz takes from it (all paths when none
are listed): Quake3e (merged in with `git subtree pull`), ioquake3's renderer and OpenAL code,
oDFe's Defrag changes and CNQ3's CPMA extensions (both cherry-picked or reimplemented).

--fetch     git fetch each upstream remote first (the remotes are added if missing)
--mark X    after syncing upstream X, record its new sync point (default: the remote branch's head)
"""
import os, sys, json, argparse, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.dirname(HERE)
ROOT = os.path.dirname(ENGINE)
PINS_PATH = os.path.join(ENGINE, "thirdparty.json")

ap = argparse.ArgumentParser()
ap.add_argument("--fetch", action="store_true")
ap.add_argument("--mark", nargs="+", metavar=("UPSTREAM", "COMMIT"))
a = ap.parse_args()


def git(*args, check=True):
    r = subprocess.run(["git"] + list(args), cwd=ROOT, capture_output=True, text=True)
    if check and r.returncode != 0:
        sys.exit("git %s failed:\n%s" % (" ".join(args), r.stderr))
    return r.stdout.strip()


pins = json.load(open(PINS_PATH))
ups = pins["upstream"]
remotes = git("remote").split()

if a.mark:
    name = a.mark[0]
    if name not in ups:
        sys.exit("no upstream %r (have %s)" % (name, ", ".join(ups)))
    u = ups[name]
    commit = git("rev-parse", a.mark[1] if len(a.mark) > 1 else "%s/%s" % (u["remote"], u["branch"]))
    u["commit"] = commit
    text = json.dumps(pins, indent=1, ensure_ascii=False)
    open(PINS_PATH, "w", newline="\n").write(text + "\n")
    print("%s synced to %s" % (name, commit))
    sys.exit(0)

total = 0
for name, u in ups.items():
    ref = "%s/%s" % (u["remote"], u["branch"])
    if u["remote"] not in remotes:
        git("remote", "add", u["remote"], u["url"])
    if a.fetch or not git("rev-parse", "--verify", "-q", ref, check=False):
        print("fetching %s ..." % name, flush=True)
        git("fetch", "-q", "--no-tags", u["remote"], u["branch"])
    paths = u.get("paths", [])
    log = git("log", "--no-merges", "--format=%h %ad %s", "--date=short", "%s..%s" % (u["commit"], ref), "--", *paths)
    lines = [l for l in log.splitlines() if l]
    total += len(lines)
    where = (" in " + ", ".join(paths)) if paths else ""
    print("\n%s: %d new commit%s%s since %s (%s)" % (name, len(lines), "" if len(lines) == 1 else "s", where, u["commit"][:10], u["how"]))
    for l in lines:
        print("  " + l)
print("\n%d new upstream commit%s in all" % (total, "" if total == 1 else "s"))
