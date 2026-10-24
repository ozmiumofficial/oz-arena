"""Records camera paths in retail Quake 3 through the q3cam mod (TRAILER_PLAN.md, "Footage").

python tools/trailer/capture_q3.py <path> [<path> ...] | --all  [--q3 <Quake 3 folder>] [--keep]

A path is tools/trailer/paths/<name>.cam (see tools/trailer/q3cam/src/cg_q3cam.c), whose first
line names its map: "// map q3dm17". For each path this installs the mod (q3cam/pak0.pk3 and
q3cam/cams/ in the Quake 3 folder; baseq3 is never touched), runs quake3.exe fullscreen at
1920x1080 with the original renderer at its best settings, lets cgame join the spectators, fly the
path and record it with cl_avidemo, and quit. The TGAs become work/trailer/retail/<name>.mp4
(60 fps, H.264), plus <name>_first.png, <name>_last.png and <name>_sheet.jpg (six frames) for checking.
Paths are checked against the map's walls first (cam_path.py).

Fullscreen, because Quake 3 turns overbright lighting off in a window (R_SetColorMappings), and
the original look needs it; screenshots are gamma-corrected the same way (RB_TakeScreenshot).
Build the mod first: python tools/trailer/q3cam/build.py
"""
import os, re, sys, glob, time, shutil, argparse, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import q3common as q3
import cam_path, footage

PATHS = os.path.join(HERE, "paths")
PK3 = os.path.join(ROOT, "work", "trailer", "q3cam", "pak0.pk3")
TAIL = 2            # CAM_TAIL in cg_q3cam.c: frames held after the last key
LEAD = 1            # cl_avidemo saves each frame at the start of the next, so the first is from before the path
FPS = footage.FPS

ap = argparse.ArgumentParser()
ap.add_argument("paths", nargs="*")
ap.add_argument("--all", action="store_true")
ap.add_argument("--q3", help="the Quake 3 folder (the one holding quake3.exe)")
ap.add_argument("--out", default=os.path.join(ROOT, "work", "trailer", "retail"))
ap.add_argument("--keep", action="store_true", help="keep the TGA frames")
ap.add_argument("--timeout", type=float, default=300)
ap.add_argument("--force", action="store_true", help="capture even when cam_path finds the camera in a wall")
a = ap.parse_args()

# the original renderer at its best: full-size textures, trilinear filtering, 32-bit color
SETTINGS = {
    "fs_game": "q3cam", "sv_pure": "0", "com_introplayed": "1", "logfile": "2",
    "r_mode": "-1", "r_customwidth": "1920", "r_customheight": "1080", "r_fullscreen": "1",
    "r_picmip": "0", "r_texturebits": "32", "r_colorbits": "32", "r_depthbits": "24",
    "r_textureMode": "GL_LINEAR_MIPMAP_LINEAR", "r_subdivisions": "4", "r_lodbias": "0",
    "r_vertexlight": "0", "r_overBrightBits": "1", "r_mapOverBrightBits": "2", "r_gamma": "1",
    "cg_draw2D": "0", "cg_drawGun": "0", "con_notifytime": "-1",  # -1: 0 still shows lines printed this frame
    "s_initsound": "0",
    "g_doWarmup": "0", "cam_broadcast": "1", "cam_wait": "60", "cam_quit": "1", "cam_fps": str(FPS),
}


def find_q3():
    for c in [a.q3] + [os.path.dirname(p) for p in q3.DEFAULT_Q3]:
        if c and os.path.isfile(os.path.join(c, "quake3.exe")):
            return c
    sys.exit("capture_q3: quake3.exe not found (--q3 <folder>)")


def path_map(name):
    with open(os.path.join(PATHS, name + ".cam")) as fh:
        m = re.match(r"\s*//\s*map\s+(\S+)", fh.readline())
    if not m:
        sys.exit(f"capture_q3: {name}.cam: the first line must be // map <name>")
    return m.group(1)


def install(game):
    if not os.path.isfile(PK3):
        sys.exit(f"capture_q3: {PK3} missing (python tools/trailer/q3cam/build.py)")
    mod = os.path.join(game, "q3cam")
    os.makedirs(os.path.join(mod, "cams"), exist_ok=True)
    shutil.copy(PK3, os.path.join(mod, "pak0.pk3"))
    for f in glob.glob(os.path.join(mod, "cams", "*.cam")):
        os.remove(f)
    for f in glob.glob(os.path.join(PATHS, "*.cam")):
        shutil.copy(f, os.path.join(mod, "cams"))
    return mod


def capture(game, mod, ff, name):
    shots = os.path.join(mod, "screenshots")
    for f in glob.glob(os.path.join(shots, "shot*.tga")):
        os.remove(f)
    log = os.path.join(mod, "qconsole.log")
    if os.path.isfile(log):
        os.remove(log)
    args = [os.path.join(game, "quake3.exe")]
    for k, v in dict(SETTINGS, cam_auto=name).items():
        args += ["+set", k, v]
    args += ["+map", path_map(name)]
    t0 = time.time()
    p = subprocess.Popen(args, cwd=game)   # quake3 takes its base path from the working folder
    try:
        p.wait(timeout=a.timeout)
    except subprocess.TimeoutExpired:
        p.kill()
        sys.exit(f"capture_q3: {name}: quake3 still running after {a.timeout:.0f} s, killed (see {log})")
    text = open(log, errors="replace").read() if os.path.isfile(log) else ""
    m = re.search(r"q3cam: done (\S+) (\d+)", text)
    if not m:
        err = re.findall(r"(?:q3cam: .*|ERROR: .*)", text)
        sys.exit(f"capture_q3: {name}: no 'q3cam: done' in {log}" + ("\n" + "\n".join(err) if err else ""))
    frames = int(m.group(2))
    tgas = sorted(glob.glob(os.path.join(shots, "shot*.tga")))
    print(f"capture_q3: {name}: {len(tgas)} screenshots for {frames} path frames in {time.time() - t0:.0f} s")
    if len(tgas) < frames + LEAD:
        sys.exit(f"capture_q3: {name}: only {len(tgas)} screenshots, the path has {frames} frames")

    os.makedirs(a.out, exist_ok=True)
    for f in tgas[:LEAD] + tgas[LEAD + frames:]:
        os.remove(f)
    print(f"capture_q3: {footage.finish(ff, tgas[LEAD:LEAD + frames], a.out, name, a.keep)}")


def main():
    names = sorted(os.path.splitext(os.path.basename(f))[0] for f in glob.glob(os.path.join(PATHS, "*.cam"))) \
        if a.all else a.paths
    if not names:
        sys.exit("capture_q3: name a path, or --all")
    if cam_path.check(names, 8.0) and not a.force:
        sys.exit("capture_q3: a path goes through a wall (cam_path.py check); fix it or pass --force")
    game = find_q3()
    ff = footage.find_ffmpeg()
    mod = install(game)
    for n in names:
        capture(game, mod, ff, n)


main()
