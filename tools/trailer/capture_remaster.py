"""Records camera paths in the remaster (TRAILER_PLAN.md, "Footage"): the same .cam paths that
capture_q3.py records in retail Quake 3, so every frame lines up with the original's.

python tools/trailer/capture_remaster.py <path> [<path> ...] | --all  [--godot <exe>] [--preroll 120]

For each path this runs godot/tests/trailer_shot.tscn with --fixed-fps 60 (the game steps 1/60 s a
frame however long a frame takes, like cl_avidemo): it renders offscreen at exactly 1920x1080 with
the Ultra preset (that run only) and pipes each frame straight into ffmpeg. Writes
work/trailer/remaster/<name>.mp4 plus <name>_first.png, <name>_last.png and <name>_sheet.jpg.
Windowed: it needs the GPU. Movie Maker isn't used: it takes its size from the project's window,
and saving every frame as PNG was the slow part.
"""
import os, sys, glob, time, shutil, argparse, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import q3common as q3
import cam_path, footage

GODOT_PROJECT = os.path.join(ROOT, "godot")

ap = argparse.ArgumentParser()
ap.add_argument("paths", nargs="*")
ap.add_argument("--all", action="store_true")
ap.add_argument("--godot")
ap.add_argument("--out", default=os.path.join(ROOT, "work", "trailer", "remaster"))
ap.add_argument("--preroll", type=int, default=120, help="frames held on the first key before flying (dropped)")
ap.add_argument("--timeout", type=float, default=900)
a = ap.parse_args()


def capture(godot, ff, name):
    _, keys = cam_path.load(name)
    frames = len(cam_path.frames(keys))
    picks = sorted({round(k * (frames - 1) / 5) for k in range(6)})   # the contact sheet's frames
    stills = os.path.join(a.out, name + "_stills")
    if os.path.isdir(stills):
        shutil.rmtree(stills)
    os.makedirs(stills)
    mp4 = os.path.join(a.out, name + ".mp4")
    log = os.path.abspath(os.path.join(a.out, name + ".log"))   # Godot needs an absolute --log-file
    args = [godot, "--path", GODOT_PROJECT, "--log-file", log, "--fixed-fps", str(footage.FPS),
            "res://tests/trailer_shot.tscn", "--", f"path={name}", f"out={mp4}", f"ffmpeg={ff}",
            f"stills={','.join(map(str, picks))}", f"stills_dir={stills}", f"preroll={a.preroll}", "window=960x540"]
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


def main():
    names = sorted(os.path.splitext(os.path.basename(f))[0] for f in glob.glob(os.path.join(cam_path.PATHS, "*.cam"))) \
        if a.all else a.paths
    if not names:
        sys.exit("capture_remaster: name a path, or --all")
    godot = next((c for c in [a.godot] + q3.default_godot() if c and os.path.isfile(c)), None)
    if not godot:
        sys.exit("capture_remaster: Godot not found (--godot)")
    os.makedirs(a.out, exist_ok=True)
    ff = footage.find_ffmpeg()
    for n in names:
        capture(godot, ff, n)


main()
