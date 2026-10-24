"""Puts each path's retail and remaster captures together (TRAILER_PLAN.md, "Footage").

python tools/trailer/compare.py <path> [<path> ...] | --all

For every path captured by both capture_q3.py and capture_remaster.py, writes
work/trailer/compare/<name>.jpg: on top, the first, middle and last frames split down the middle
by a comparison line (1999 left, remastered right), a rough look at the trailer's CompareLine; below,
the same frames blended 50/50, where any misalignment between the two games shows as doubled edges.
"""
import os, sys, glob, argparse, subprocess
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import footage

RETAIL = os.path.join(ROOT, "work", "trailer", "retail")
REMASTER = os.path.join(ROOT, "work", "trailer", "remaster")
OUT = os.path.join(ROOT, "work", "trailer", "compare")
W, H = 960, 540

ap = argparse.ArgumentParser()
ap.add_argument("paths", nargs="*")
ap.add_argument("--all", action="store_true")
a = ap.parse_args()


def frame(ff, mp4, t):
    """The frame at t seconds as an image."""
    tmp = os.path.join(OUT, "_frame.png")
    subprocess.run([ff, "-y", "-loglevel", "error", "-ss", f"{t:.3f}", "-i", mp4, "-frames:v", "1", "-update", "1", tmp], check=True)
    img = Image.open(tmp).convert("RGB")
    img.load()
    os.remove(tmp)
    return img


def duration(ff, mp4):
    r = subprocess.run([ff, "-i", mp4], capture_output=True, text=True)
    for line in r.stderr.splitlines():
        if "Duration:" in line:
            h, m, s = line.split("Duration:")[1].split(",")[0].strip().split(":")
            return int(h) * 3600 + int(m) * 60 + float(s)
    return 0.0


def compare(ff, name):
    old, new = os.path.join(RETAIL, name + ".mp4"), os.path.join(REMASTER, name + ".mp4")
    if not (os.path.isfile(old) and os.path.isfile(new)):
        print(f"compare: {name}: needs both captures")
        return
    d = min(duration(ff, old), duration(ff, new))
    times = [0.0, d / 2, max(d - 1.5 / footage.FPS, 0.0)]   # a seek to the very end returns no frame
    sheet = Image.new("RGB", (W * 3, H * 2))
    draw = ImageDraw.Draw(sheet)
    for k, t in enumerate(times):
        r = frame(ff, old, t).resize((W, H), Image.LANCZOS)
        m = frame(ff, new, t).resize((W, H), Image.LANCZOS)
        split = r.copy()
        split.paste(m.crop((W // 2, 0, W, H)), (W // 2, 0))
        sheet.paste(split, (k * W, 0))
        sheet.paste(Image.blend(r, m, 0.5), (k * W, H))
        draw.line([(k * W + W // 2, 0), (k * W + W // 2, H)], fill=(230, 40, 30), width=3)
        draw.text((k * W + 10, 8), "1999", fill=(255, 255, 255))
        draw.text((k * W + W // 2 + 10, 8), "REMASTERED", fill=(255, 255, 255))
        draw.text((k * W + 10, H + 8), f"50/50 at {t:.2f} s", fill=(255, 255, 255))
    out = os.path.join(OUT, name + ".jpg")
    sheet.save(out, quality=90)
    print(f"compare: {out}")


def main():
    names = sorted(os.path.splitext(os.path.basename(f))[0] for f in glob.glob(os.path.join(REMASTER, "*.mp4"))) \
        if a.all else a.paths
    if not names:
        sys.exit("compare: name a path, or --all")
    os.makedirs(OUT, exist_ok=True)
    ff = footage.find_ffmpeg()
    for n in names:
        compare(ff, n)


main()
