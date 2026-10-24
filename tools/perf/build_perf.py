"""Measures a release build's frame times and stalls without rebuilding it (CLAUDE.md "Performance facts").

python tools/perf/build_perf.py <build dir> <name> [--patch res://a.gd,res://b.gd] [-- game args]

  Hard-links the build into <build dir>_perf (same drive, so no extra space; the build itself is
  untouched) with an override.cfg that gives it:
    its own user folder, %APPDATA%/Q3RemasterPerf, which the stand-in Diagnostics autoload
      (patch_diag.gd) fills each run with your settings, offline, and your remembered Quake 3 folder:
      it never logs in as you, and your profile, replays and settings stay untouched
    with --patch, the source tree's versions of those scripts mounted over the build's (also
      patch_diag.gd): measure a change before exporting it
    the probe autoload, perf_probe.gd (its header lists the perf_* args)
  Game args default to `map=q3dm6 bots=5 window=1920x1080 perf=40`; any given replace them.
  Writes work/perf/<name>.out (stdout), .log, .json (every measured frame) and prints the PERF lines.
"""
import os, sys, argparse, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(ROOT, "work", "perf")
PERF_DIR_NAME = "Q3RemasterPerf"


def link_tree(src, dst):
    for base, _dirs, files in os.walk(src):
        rel = os.path.relpath(base, src)
        os.makedirs(os.path.join(dst, rel), exist_ok=True)
        for f in files:
            t = os.path.join(dst, rel, f)
            s = os.path.join(base, f)
            if os.path.exists(t):
                if os.path.samefile(s, t):
                    continue
                os.remove(t)   # a newer build: link the new file
            os.link(s, t)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build")
    ap.add_argument("name")
    ap.add_argument("--patch", default="", help="res:// scripts to take from godot/ instead of the build")
    ap.add_argument("--timeout", type=int, default=600)
    argv = sys.argv[1:]
    cut = argv.index("--") if "--" in argv else len(argv)
    game_args = argv[cut + 1:]
    a = ap.parse_args(argv[:cut])
    build = os.path.abspath(a.build)
    copy = build.rstrip("\\/") + "_perf"
    link_tree(build, copy)
    fwd = lambda p: p.replace("\\", "/")
    lines = ["[application]", "", "config/use_custom_user_dir=true", 'config/custom_user_dir_name="%s"' % PERF_DIR_NAME,
             "", "[autoload]", ""]
    env = dict(os.environ)
    lines.append('Diagnostics="*%s"' % fwd(os.path.join(HERE, "patch_diag.gd")))
    if a.patch:
        tmp = os.path.join(OUT, "patch")
        os.makedirs(tmp, exist_ok=True)
        env.update(PATCH_SRC=fwd(os.path.join(ROOT, "godot")), PATCH_TMP=fwd(tmp), PATCH_FILES=a.patch)
    lines.append('PerfProbe="*%s"' % fwd(os.path.join(HERE, "perf_probe.gd")))
    open(os.path.join(copy, "override.cfg"), "w", newline="\n").write("\n".join(lines) + "\n")
    os.makedirs(OUT, exist_ok=True)
    stem = os.path.join(OUT, a.name)
    args = game_args or ["map=q3dm6", "bots=5", "window=1920x1080", "perf=40"]
    if not any(x.startswith("perf_out=") for x in args):
        args.append("perf_out=" + fwd(stem + ".json"))
    cmd = [os.path.join(copy, "Q3Remaster.console.exe"), "--log-file", fwd(stem + ".log")] + args
    with open(stem + ".out", "w", encoding="utf-8", errors="replace") as out:
        try:
            r = subprocess.run(cmd, cwd=copy, env=env, stdout=out, stderr=subprocess.STDOUT, timeout=a.timeout).returncode
        except subprocess.TimeoutExpired:
            r = "timeout"
    print("exit", r)
    for line in open(stem + ".out", encoding="utf-8", errors="replace"):
        if line.startswith(("PERF", "PATCH", "SCRIPT ERROR")):
            print(line.rstrip())


if __name__ == "__main__":
    main()
