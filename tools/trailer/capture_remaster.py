"""Records camera paths in the remaster (TRAILER_PLAN.md, "Footage"): the same .cam paths that
capture_q3.py records in retail Quake 3, so every frame lines up with the original's.

python tools/trailer/capture_remaster.py <path> [<path> ...] | --all  [--showcase [<name> ...]]  [--godot <exe>] [--preroll 120]

--showcase records tools/trailer/showcase.json's shots as well: fighters taunting in the main menu's
red mist (godot/tests/trailer_showcase.tscn), the same way.

For each path this runs godot/tests/trailer_shot.tscn with --fixed-fps 60 (the game steps 1/60 s a
frame however long a frame takes, like cl_avidemo): it renders offscreen at exactly 1920x1080 with
the Ultra preset (that run only) and pipes each frame straight into ffmpeg. Writes
work/trailer/remaster/<name>.mp4 plus <name>_first.png, <name>_last.png and <name>_sheet.jpg.
Windowed: it needs the GPU. Movie Maker isn't used: it takes its size from the project's window,
and saving every frame as PNG was the slow part.
"""
import os, sys, json, glob, time, shutil, argparse, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import q3common as q3
import cam_path, footage

GODOT_PROJECT = os.path.join(ROOT, "godot")

ap = argparse.ArgumentParser()
ap.add_argument("paths", nargs="*")
ap.add_argument("--all", action="store_true")
ap.add_argument("--showcase", nargs="*", help="fighters in the menu's mist from showcase.json (all, or these names)")
ap.add_argument("--matches", nargs="*", help="bot matches from matches.json (all, or these names)")
ap.add_argument("--action", nargs="*", help="directed action from bot-match replays, action.json (all, or these names)")
ap.add_argument("--refilm", action="store_true", help="--action: film the replay the last run recorded (its _shots.json) again")
ap.add_argument("--godot")
ap.add_argument("--out", default=os.path.join(ROOT, "work", "trailer", "remaster"))
ap.add_argument("--preroll", type=int, default=120, help="frames held on the first key before flying (dropped)")
ap.add_argument("--timeout", type=float, default=900)
a = ap.parse_args()
a.out = os.path.abspath(a.out)   # Godot runs with its own working folder, so paths it gets must be absolute


def capture(godot, ff, name, scene, frames, shot_args, window="960x540"):
    picks = sorted({round(k * (frames - 1) / 5) for k in range(6)})   # the contact sheet's frames
    stills = os.path.join(a.out, name + "_stills")
    if os.path.isdir(stills):
        shutil.rmtree(stills)
    os.makedirs(stills)
    mp4 = os.path.join(a.out, name + ".mp4")
    log = os.path.abspath(os.path.join(a.out, name + ".log"))   # Godot needs an absolute --log-file
    args = [godot, "--path", GODOT_PROJECT, "--log-file", log, "--fixed-fps", str(footage.FPS),
            scene, "--"] + shot_args + [f"name={name}", f"out={mp4}", f"ffmpeg={ff}",
            f"stills={','.join(map(str, picks))}", f"stills_dir={stills}", f"preroll={a.preroll}", f"window={window}"]
    t0 = time.time()
    try:
        subprocess.run(args, timeout=a.timeout, capture_output=True)
    except subprocess.TimeoutExpired:
        sys.exit(f"capture_remaster: {name}: Godot still running after {a.timeout:.0f} s (see {log})")
    text = open(log, errors="replace").read() if os.path.isfile(log) else ""
    if f"TRAILER SHOT done {frames}" not in text or not os.path.isfile(mp4):
        errs = [l for l in text.splitlines() if "ERROR" in l][:10]
        sys.exit(f"capture_remaster: {name}: no 'TRAILER SHOT done {frames}' in {log}" + "".join("\n" + e for e in errs))
    shots = [os.path.join(stills, f"{name}_f{k:05d}.png") for k in picks]
    footage.sheet(shots, os.path.join(a.out, name + "_sheet.jpg"))
    shutil.copy(shots[0], os.path.join(a.out, name + "_first.png"))
    shutil.copy(shots[-1], os.path.join(a.out, name + "_last.png"))
    shutil.rmtree(stills)
    print(f"capture_remaster: {name}: {frames} frames in {time.time() - t0:.0f} s -> {mp4}")


def capture_action(godot, ff, m):
    """Records a bot match as a replay (headless, fast), then films its best frags with the director
    (tests/trailer_action.tscn). Writes <name>.mp4, <name>_shots.json (each shot's first frame, length,
    kill frame, weapon and camera) and <name>_sheet.jpg (the middle of the first six shots). With
    --refilm it films the replay named in <name>_shots.json again: the same match, so the same shots,
    for footage that needs a fix in the game rather than a new match."""
    name = m["name"]
    t0 = time.time()
    old = os.path.join(a.out, name + "_shots.json")
    if a.refilm and os.path.isfile(old) and os.path.isfile(json.load(open(old))["replay"]):
        replay = json.load(open(old))["replay"]
    else:
        replays = os.path.join(os.path.dirname(a.out), "replays")
        os.makedirs(replays, exist_ok=True)
        log = os.path.join(a.out, name + "_replay.log")
        subprocess.run([godot, "--headless", "--path", GODOT_PROJECT, "--log-file", log, "res://tests/trailer_replay_rec.tscn", "--",
                        f"map={m['map']}", f"bots={m['bots']}", f"skill={m.get('skill', 5)}", f"seconds={m['seconds']}",
                        f"warmup={m.get('warmup', 45)}", f"dir={replays}"], timeout=a.timeout, capture_output=True)
        text = open(log, errors="replace").read() if os.path.isfile(log) else ""
        found = [l.split("TRAILER REPLAY ", 1)[1].strip() for l in text.splitlines() if l.startswith("TRAILER REPLAY ")]
        if not found or "FAILED" in found[0]:
            sys.exit(f"capture_remaster: {name}: no replay recorded (see {log})")
        replay = found[0]
    mp4 = os.path.join(a.out, name + ".mp4")
    log = os.path.join(a.out, name + ".log")
    try:
        subprocess.run([godot, "--path", GODOT_PROJECT, "--log-file", log, "--fixed-fps", str(footage.FPS),
                        "res://tests/trailer_action.tscn", "--", f"replay={replay}", f"max={m.get('max', 8)}", f"name={name}",
                        f"out={mp4}", f"ffmpeg={ff}", f"preroll={a.preroll}", "window=1920x1080"],
                       timeout=a.timeout * 4, capture_output=True)
    except subprocess.TimeoutExpired:
        sys.exit(f"capture_remaster: {name}: Godot still running (see {log})")
    text = open(log, errors="replace").read() if os.path.isfile(log) else ""
    shots = []
    for l in text.splitlines():
        if l.startswith("TRAILER ACTION shot "):
            w = l.split()
            shots.append({"shot": int(w[3]), "frame": int(w[5]), "frames": int(w[7]), "weapon": w[8], "camera": w[9],
                          "kill": int(w[11]) if len(w) > 11 else 182})
    total = sum(x["frames"] for x in shots)
    if not shots or f"TRAILER SHOT done {total}" not in text or not os.path.isfile(mp4):
        errs = [l for l in text.splitlines() if "ERROR" in l][:10]
        sys.exit(f"capture_remaster: {name}: the director didn't finish (see {log})" + "".join("\n" + e for e in errs))
    json.dump({"replay": replay, "fps": footage.FPS, "shots": shots}, open(os.path.join(a.out, name + "_shots.json"), "w"), indent=1)
    stills = []
    for x in shots[:6]:
        png = os.path.join(a.out, f"{name}_shot{x['shot']}.png")
        mid = (x["frame"] + x["frames"] * 0.45) / footage.FPS
        subprocess.run([ff, "-y", "-loglevel", "error", "-ss", f"{mid:.3f}", "-i", mp4, "-frames:v", "1", "-update", "1", png], check=True)
        stills.append(png)
    footage.sheet(stills, os.path.join(a.out, name + "_sheet.jpg"))
    for p in stills:
        os.remove(p)
    print(f"capture_remaster: {name}: {len(shots)} shots, {total} frames in {time.time() - t0:.0f} s -> {mp4}")


def main():
    names = sorted(os.path.splitext(os.path.basename(f))[0] for f in glob.glob(os.path.join(cam_path.PATHS, "*.cam"))) \
        if a.all else a.paths
    shots = []
    if a.showcase is not None:
        shots = json.load(open(os.path.join(HERE, "showcase.json")))["shots"]
        shots = [s for s in shots if not a.showcase or s["name"] in a.showcase]
    matches = []
    if a.matches is not None:
        matches = json.load(open(os.path.join(HERE, "matches.json")))["matches"]
        matches = [m for m in matches if not a.matches or m["name"] in a.matches]
    actions = []
    if a.action is not None:
        actions = json.load(open(os.path.join(HERE, "action.json")))["matches"]
        actions = [m for m in actions if not a.action or m["name"] in a.action]
    if not names and not shots and not matches and not actions:
        sys.exit("capture_remaster: name a path, or --all, --showcase, --matches or --action")
    godot = next((c for c in [a.godot] + q3.default_godot() if c and os.path.isfile(c)), None)
    if not godot:
        sys.exit("capture_remaster: Godot not found (--godot)")
    os.makedirs(a.out, exist_ok=True)
    ff = footage.find_ffmpeg()
    for n in names:
        _, keys = cam_path.load(n)
        capture(godot, ff, n, "res://tests/trailer_shot.tscn", len(cam_path.frames(keys)), [f"path={n}"])
    for s in shots:
        args = [f"model={s['model']}", f"framing={s.get('framing', 0)}", f"seconds={s['seconds']}",
                "gesture=" + ",".join(str(g) for g in s.get("gesture", [])), f"yaw={s.get('yaw', 0)}",
                f"closer={s.get('closer', 1.0)}", f"seed={s.get('seed', 1)}"] + ([f"shell={s['shell']}"] if s.get("shell") else [])
        capture(godot, ff, s["name"], "res://tests/trailer_showcase.tscn", round(s["seconds"] * footage.FPS), args)
    for m in matches:
        args = [f"map={m['map']}", f"bots={m['bots']}", f"skill={m.get('skill', 5)}", f"seconds={m['seconds']}",
                f"warmup={m.get('warmup', 60)}", f"view={m.get('view', 'chase')}", f"follow={m.get('follow', 0)}"]
        capture(godot, ff, m["name"], "res://tests/trailer_match.tscn", round(m["seconds"] * footage.FPS), args, window="1920x1080")
    for m in actions:
        capture_action(godot, ff, m)


main()
