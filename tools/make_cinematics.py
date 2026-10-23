"""Cinematics: Quake 3's RoQ movies -> Ogg Theora for Godot, plus thumbnails and a catalog.

python make_cinematics.py <extracted baseq3> <work/_cinematics> [--q3 <baseq3>] [--ffmpeg <exe>] [--jobs 2]

video/*.roq are 512x256 VQ frames at 30 fps that the engine stretches to a 4:3 screen, with
22 kHz DPCM stereo audio. Each becomes <id>.ogv at 1280x960 (4:3): a light denoise against the
VQ blocking, a Lanczos upscale, a little sharpening; audio as 44.1 kHz Vorbis. thumbs/<id>.jpg
is a frame for the menu. cinematics.json lists them in q3_ui's Cinematics menu order
(ui_cinematics.c), with what unlocks each: tier movies once that campaign tier is won, the
ending after the final. ffmpeg comes from PATH or the imageio-ffmpeg Python package.
"""
import os, sys, json, glob, shutil, argparse, subprocess
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import q3common as q3

ap = argparse.ArgumentParser()
ap.add_argument("extracted")
ap.add_argument("out")
ap.add_argument("--q3")
ap.add_argument("--ffmpeg")
ap.add_argument("--jobs", type=int, default=2)
ap.add_argument("--force", action="store_true", help="re-encode even when the .ogv is newer than its .roq")
a = ap.parse_args()

# id, title, unlock (q3_ui ui_cinematics.c order; demoend is the shareware demo's and is left out)
CINEMATICS = [
    ("idlogo", "id Software", "always"),
    ("intro", "Intro", "always"),
    ("tier1", "Tier 1", "tier:1"), ("tier2", "Tier 2", "tier:2"), ("tier3", "Tier 3", "tier:3"),
    ("tier4", "Tier 4", "tier:4"), ("tier5", "Tier 5", "tier:5"), ("tier6", "Tier 6", "tier:6"),
    ("tier7", "Tier 7", "tier:7"),
    ("end", "Ending", "final"),
]


def find_ffmpeg():
    if a.ffmpeg:
        return a.ffmpeg
    exe = shutil.which("ffmpeg")
    if exe:
        return exe
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except ImportError:
        return None


ff = find_ffmpeg()
if not ff:
    sys.exit("make_cinematics: ffmpeg not found (install it, or pip install imageio-ffmpeg)")
vdir = os.path.join(a.extracted, "video")
if not glob.glob(os.path.join(vdir, "*.roq")):
    q3dir = next((c for c in [a.q3] + q3.DEFAULT_Q3 if c and os.path.isfile(os.path.join(c, "pak0.pk3"))), None)
    if not q3dir:
        sys.exit("make_cinematics: no video/*.roq and no Quake 3 install found; pass --q3 <baseq3>")
    subprocess.check_call([sys.executable, os.path.join(HERE, "extract_pk3.py"), q3dir, a.extracted, "--prefix", "video/"])
os.makedirs(os.path.join(a.out, "thumbs"), exist_ok=True)


def seconds(roq):
    # RoQ has no duration header; count frames
    p = subprocess.run([ff, "-hide_banner", "-i", roq, "-map", "0:v", "-f", "null", "-"], capture_output=True, text=True, errors="replace")
    for line in reversed(p.stderr.splitlines()):
        if "frame=" in line:
            try:
                return round(int(line.split("frame=")[1].split()[0]) / 30.0, 2)
            except (ValueError, IndexError):
                break
    return 0.0


def convert(item):
    cid, title, unlock = item
    roq = os.path.join(vdir, cid + ".roq")
    if not os.path.isfile(roq):
        return None
    ogv = os.path.join(a.out, cid + ".ogv")
    if a.force or not os.path.isfile(ogv) or os.path.getmtime(ogv) < os.path.getmtime(roq):
        subprocess.run([ff, "-hide_banner", "-loglevel", "error", "-y", "-i", roq,
                        "-vf", "hqdn3d=1.5:1.5:3:3,scale=1280:960:flags=lanczos,unsharp=5:5:0.5:5:5:0,setsar=1,format=yuv420p",
                        "-c:v", "libtheora", "-q:v", "6", "-c:a", "libvorbis", "-q:a", "6", "-ar", "44100", ogv], check=True)
    dur = seconds(roq)
    thumb = os.path.join(a.out, "thumbs", cid + ".jpg")
    subprocess.run([ff, "-hide_banner", "-loglevel", "error", "-y", "-ss", str(max(dur * 0.35, 0.5)), "-i", ogv,
                    "-frames:v", "1", "-vf", "scale=480:360", "-q:v", "3", thumb], check=True)
    print("%-8s %6.1fs  %5.1f MB" % (cid, dur, os.path.getsize(ogv) / 1e6), flush=True)
    return dict(id=cid, title=title, unlock=unlock, seconds=dur, file=cid + ".ogv", thumb="thumbs/%s.jpg" % cid)


with ThreadPoolExecutor(max_workers=max(1, a.jobs)) as pool:
    done = [c for c in pool.map(convert, CINEMATICS) if c]
json.dump(dict(cinematics=done), open(os.path.join(a.out, "cinematics.json"), "w"), indent=1)
print("cinematics: %d movies" % len(done))
