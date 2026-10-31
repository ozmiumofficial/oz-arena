"""The arenas' level shots in the Oz look -> build/engine/oz/pak_oz_levelshots.pk3.

python tools/make_levelshots.py [--maps q3dm17 ...] [--size 1280x960] [--supersample 2] [--keep]

id's level shots are 128x128 (levelshots/<map>.jpg, q3tourney6_ctf's a .tga): id's levelshot command
squeezed a 4:3 view into a square, and the menus (the Campaign's arenas, Skirmish's and Custom Match's
maps, Training, the cinematics and replays, the Profile's arena banners) and the game's loading screen
stretch them many times over. This takes them again as id took them, in the engine with the Oz look:
one client run on the oz renderer with build/engine's paks (the remastered world and its relit
lightmaps), Oz's default settings with MSAA 4x and 16x anisotropy. For each map it loads the map
(devmap, free for all, no bots), runs the game's levelshot command (the player at the intermission
spot, 90 degrees across at 4:3, nothing 2D drawn: g_cmds.c Cmd_LevelShot_f, cg.levelShot), lets the
picture settle, and shoots it at --supersample times the size in a borderless window (larger than the
screen is fine: SDL fixes a window's size), scaled down with Lanczos. Game time steps a fixed amount a
frame (fixedtime) from a fixed seed (sv_randomSeed), so a run frames the same pictures every time.

The pak keeps id's paths (levelshots/<map> with id's extension): an Oz asset pak loads above id's paks
(FS_AddOzAssets under other mods), so the menus and the cgame's loading screen, which ask for those
names, get these on every renderer; a pure server leaves the pak out and shows id's, and map packs keep
their own. It's made from id's maps, so it's build output, never committed.

Writes build/levelshots/<map>.jpg (the scaled shots), build/levelshots/sheet.jpg (all of them) and the
pak. Needs the GPU and the desktop: it opens a window at the top left. --keep keeps the scratch user
folder (build/levelshots/home) with the run's qconsole.log.
"""
import os, re, sys, glob, time, shutil, struct, zipfile, argparse, subprocess
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from q3paks import find_baseq3, Paks

ENGINE = os.path.join(ROOT, "build", "engine")
CLIENT = os.path.join(ENGINE, "OzArena.exe")
OUT = os.path.join(ROOT, "build", "levelshots")
PAK = os.path.join(ENGINE, "oz", "pak_oz_levelshots.pk3")
SKIP = ("test_bigbox",)   # id's test box: no level shot, no arena


def maps_of(paks):
    """id's maps that have id's level shot: map -> the shot's path in the paks."""
    out = {}
    for n in sorted(paks.files):
        m = re.match(r"maps/([^/]+)\.bsp$", n)
        if m and m.group(1) not in SKIP:
            shot = paks.image("levelshots/" + m.group(1))
            if shot:
                out[m.group(1)] = shot
    return out


def has_intermission(paks, m):
    data = paks.read("maps/%s.bsp" % m)
    off, ln = struct.unpack_from("<ii", data, 8)
    return b'"info_player_intermission"' in data[off:off + ln]


def run(maps, w, h, home):
    """One client run that shoots every map; returns the log's text."""
    shots = os.path.join(home, "oz", "screenshots")
    if os.path.isdir(home):
        shutil.rmtree(home)
    os.makedirs(shots)
    open(os.path.join(home, "oz", "q3config.cfg"), "w").close()
    log = os.path.join(home, "oz", "qconsole.log")
    cmd = [CLIENT, "+set", "fs_homepath", home, "+set", "logfile", "2", "+set", "r_fullscreen", "0",
           "+set", "r_mode", "-1", "+set", "r_customwidth", str(w), "+set", "r_customheight", str(h),
           "+set", "r_noborder", "1", "+set", "vid_xpos", "0", "+set", "vid_ypos", "0",
           "+set", "cl_renderer", "oz", "+set", "r_swapInterval", "0", "+set", "com_maxfpsUnfocused", "0",
           "+set", "cg_draw2D", "0", "+set", "cl_ozHud", "0", "+set", "con_notifytime", "0", "+set", "in_mouse", "0",
           "+set", "s_volume", "0", "+set", "s_musicVolume", "0", "+set", "sv_pure", "0", "+set", "bot_enable", "0",
           "+set", "g_gametype", "0", "+set", "sv_randomSeed", "1",
           "+set", "r_ext_multisample", "4", "+set", "r_picmip", "0", "+set", "r_ext_max_anisotropy", "16"]
    # the maps run from a script: the command line has a length limit, and its +set values are startup
    # values the engine applies again. Waits count frames, and fixedtime makes game time count them too
    script = []
    for m in maps:
        script += ["devmap %s" % m, "fixedtime 8", "com_maxfps 125", "wait 400", "levelshot", "wait 250",
                   "screenshot %s" % m, "wait 10"]
    script += ["quit"]
    open(os.path.join(home, "oz", "levelshots.cfg"), "w").write("\n".join(script) + "\n")
    cmd += ["+exec", "levelshots.cfg"]
    started = time.time()
    proc = subprocess.Popen(cmd, cwd=ENGINE)
    while proc.poll() is None:
        if time.time() - started > 60 + 30 * len(maps):
            proc.kill()
            print("  timed out")
            break
        time.sleep(2)
    return open(log, encoding="latin-1").read() if os.path.exists(log) else ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--maps", nargs="*", help="only these maps (the pak keeps the others' shots)")
    ap.add_argument("--size", default="1280x960", help="the shots' size (4:3)")
    ap.add_argument("--supersample", type=int, default=2, help="rendered at this many times the size")
    ap.add_argument("--baseq3")
    ap.add_argument("--keep", action="store_true", help="keep the scratch user folder")
    a = ap.parse_args()
    if not os.path.isfile(CLIENT) or not glob.glob(os.path.join(ENGINE, "oz", "pak_oz_world*.pk3")):
        sys.exit("make_levelshots: needs build/engine with the world paks (engine/build_engine.py, tools/make_pk3s.py)")
    w, h = (int(x) for x in a.size.split("x"))
    paks = Paks(a.baseq3 or find_baseq3())
    ids = maps_of(paks)
    maps = [m.lower() for m in a.maps] if a.maps else sorted(ids)
    unknown = [m for m in maps if m not in ids]
    if unknown:
        sys.exit("make_levelshots: not id's maps with a level shot: " + ", ".join(unknown))
    for m in maps:
        if not has_intermission(paks, m):
            print("  %s has no intermission spot: the game shoots from a spawn point" % m)

    home = os.path.join(OUT, "home")
    text = run(maps, w * a.supersample, h * a.supersample, home)
    got, missing = [], []
    for m in maps:
        src = os.path.join(home, "oz", "screenshots", m + ".tga")
        if not os.path.isfile(src):
            missing.append(m)
            continue
        im = Image.open(src).convert("RGB")
        if im.size != (w * a.supersample, h * a.supersample):
            print("  %s: shot at %dx%d (the window was clamped?)" % (m, im.size[0], im.size[1]))
        im.resize((w, h), Image.LANCZOS).save(os.path.join(OUT, m + ".jpg"), quality=92)
        got.append(m)
    errors = re.findall(r"(?m)^.*(?:ERROR|Must not be|must be executed).*$", text)
    for e in errors[:10]:
        print("  log: " + e)
    if missing:
        print("  no shot: " + ", ".join(missing))
    if not a.keep:
        shutil.rmtree(home, ignore_errors=True)

    # the pak: every map with a shot in build/levelshots, at id's path and extension
    made = sorted(m for m in ids if os.path.isfile(os.path.join(OUT, m + ".jpg")))
    tmp = PAK + ".tmp"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_STORED) as z:   # JPG is compressed already
        for m in made:
            ext = os.path.splitext(ids[m])[1]
            src = os.path.join(OUT, m + ".jpg")
            if ext == ".jpg":
                z.write(src, "levelshots/%s.jpg" % m)
            else:   # a .tga of id's would win over a .jpg of ours (the renderers try tga first)
                tga = os.path.join(OUT, m + ".tga")
                Image.open(src).save(tga, compression="tga_rle")
                z.write(tga, "levelshots/%s%s" % (m, ext))
                os.remove(tga)
    os.replace(tmp, PAK)
    print("%s: %d level shots, %.1f MB (%d shot this run)" % (PAK, len(made), os.path.getsize(PAK) / 1e6, len(got)))

    cols, tw, th = 6, 320, 240
    rows = (len(made) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * tw, rows * th))
    for i, m in enumerate(made):
        sheet.paste(Image.open(os.path.join(OUT, m + ".jpg")).resize((tw, th), Image.LANCZOS), ((i % cols) * tw, (i // cols) * th))
    sheet.save(os.path.join(OUT, "sheet.jpg"), quality=90)
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
