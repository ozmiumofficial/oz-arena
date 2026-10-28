"""Q3Updater.exe against a fake install (UPDATES_PLAN.md U5). Build both first:
    python -m SCons -f SConstruct.tools bin/tools/Q3Updater.exe bin/tools/test/fake_game.exe
    python test_updater.py

Each case lays out <tmp>/game with "old" files and a staged update, runs the updater the way
Q3UpdateInstall.apply does (copied into .update, pid of a process that has exited), and checks the
files, the backup and the log:
  relaunch ok     new files in place, the new build started, backup and stage gone
  relaunch crash  the new build exits before its menu: old files back, the old build started again
  at quit         new files in place, nothing started, backup kept for the game's next start
  missing stage   a staged file is missing: nothing changes
  rollback        --rollback puts an at-quit update's backup back
  refused path    "../x" in apply.txt: nothing changes
Prints "UPDATER TEST ok" or the failures.
"""
import os, shutil, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
UPDATER = os.path.join(HERE, "bin", "tools", "Q3Updater.exe")
FAKE = os.path.join(HERE, "bin", "tools", "test", "fake_game.exe")
fails = []


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="\n") as f:
        f.write(text)


def read(path):
    return open(path).read() if os.path.isfile(path) else None


def setup(root, new_mode, paths=("Q3Remaster.pck", "q3core/baseq3/game.dll", "fake_game.txt", "new_file.txt")):
    game = os.path.join(root, "game")
    shutil.rmtree(game, ignore_errors=True)
    os.makedirs(game)
    shutil.copy2(FAKE, os.path.join(game, "Q3Remaster.exe"))
    write(os.path.join(game, "Q3Remaster.pck"), "old pck")
    write(os.path.join(game, "q3core", "baseq3", "game.dll"), "old dll")
    write(os.path.join(game, "fake_game.txt"), "ok old")
    stage = os.path.join(game, ".update", "stage")
    write(os.path.join(stage, "Q3Remaster.pck"), "new pck")
    write(os.path.join(stage, "q3core", "baseq3", "game.dll"), "new dll")
    write(os.path.join(stage, "fake_game.txt"), new_mode + " new")
    write(os.path.join(stage, "new_file.txt"), "added")
    write(os.path.join(game, ".update", "apply.txt"), "v9.9.9\n" + "\n".join(paths) + "\n")
    run = os.path.join(game, ".update", "Q3Updater.run.exe")
    shutil.copy2(UPDATER, run)
    return game, run


def updater(run, game, relaunch, timeout=60):
    # a pid that's gone: the updater goes straight on, as when the game has already quit
    p = subprocess.Popen(["cmd", "/c", "exit 0"])
    p.wait()
    r = subprocess.run([run, game, str(p.pid), "1" if relaunch else "0", "Q3Remaster.exe"], timeout=timeout)
    time.sleep(0.5)
    return r.returncode


def check(name, cond, detail=""):
    print("  [%s] %s%s" % ("ok" if cond else "FAIL", name, "" if cond else ": " + detail))
    if not cond:
        fails.append(name)


def state(game):
    return {p: read(os.path.join(game, p)) for p in ("Q3Remaster.pck", "q3core/baseq3/game.dll", "fake_game.txt", "new_file.txt")}


def main():
    for f in (UPDATER, FAKE):
        if not os.path.isfile(f):
            sys.exit("missing %s (build it first)" % f)
    root = tempfile.mkdtemp(prefix="q3upd_")
    try:
        print("relaunch ok")
        game, run = setup(root, "ok")
        rc = updater(run, game, True)
        s = state(game)
        check("exit 0", rc == 0, str(rc))
        check("new files in place", s == {"Q3Remaster.pck": "new pck", "q3core/baseq3/game.dll": "new dll", "fake_game.txt": "ok new", "new_file.txt": "added"}, str(s))
        check("the new build ran", (read(os.path.join(game, "fake_game.log")) or "").split() == ["ok"], str(read(os.path.join(game, "fake_game.log"))))
        check("backup and stage cleared", not os.path.exists(os.path.join(game, ".update", "backup")) and not os.path.exists(os.path.join(game, ".update", "stage"))
              and not os.path.exists(os.path.join(game, ".update", "apply.txt")))

        print("relaunch crash")
        game, run = setup(root, "crash")
        rc = updater(run, game, True)
        s = state(game)
        check("exit 1", rc == 1, str(rc))
        check("old files back, the added file gone", s == {"Q3Remaster.pck": "old pck", "q3core/baseq3/game.dll": "old dll", "fake_game.txt": "ok old", "new_file.txt": None}, str(s))
        check("new build tried, then the old one", (read(os.path.join(game, "fake_game.log")) or "").split() == ["crash", "ok"], str(read(os.path.join(game, "fake_game.log"))))
        check("a failure note for the menu", read(os.path.join(game, ".update", "failed")) is not None)

        print("at quit")
        game, run = setup(root, "ok")
        rc = updater(run, game, False)
        s = state(game)
        check("exit 0", rc == 0, str(rc))
        check("new files in place", s["Q3Remaster.pck"] == "new pck" and s["new_file.txt"] == "added", str(s))
        check("nothing started", read(os.path.join(game, "fake_game.log")) is None)
        check("backup kept, applied says quit", read(os.path.join(game, ".update", "backup", "Q3Remaster.pck")) == "old pck"
              and read(os.path.join(game, ".update", "applied")) == "quit")

        print("rollback")
        r = subprocess.run([UPDATER, "--rollback", game], timeout=30)
        s = state(game)
        check("old files back", s == {"Q3Remaster.pck": "old pck", "q3core/baseq3/game.dll": "old dll", "fake_game.txt": "ok old", "new_file.txt": None}, str(s))

        print("missing stage")
        game, run = setup(root, "ok")
        os.remove(os.path.join(game, ".update", "stage", "q3core", "baseq3", "game.dll"))
        rc = updater(run, game, False)
        s = state(game)
        check("exit 1", rc == 1, str(rc))
        check("nothing changed", s == {"Q3Remaster.pck": "old pck", "q3core/baseq3/game.dll": "old dll", "fake_game.txt": "ok old", "new_file.txt": None}, str(s))

        print("refused path")
        game, run = setup(root, "ok", paths=("Q3Remaster.pck", "../escape.txt"))
        write(os.path.join(game, ".update", "stage", "..", "escape.txt"), "x")
        rc = updater(run, game, False)
        check("exit 1", rc == 1, str(rc))
        check("nothing changed", read(os.path.join(game, "Q3Remaster.pck")) == "old pck")
    finally:
        shutil.rmtree(root, ignore_errors=True)
    print("UPDATER TEST %s" % ("ok" if not fails else "FAIL: " + ", ".join(fails)))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
