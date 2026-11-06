"""python pairs.py <dir> <tag> <out.jpg> view [view ...] [--w 900]
rg_look.shots' renders as a sheet: one row per view, the rebuild on the left, the reference on the right (when there
is a <tag>_<view>_ref.png)."""
import sys, os
from PIL import Image, ImageDraw, ImageFont

a = sys.argv[1:]
W = 900
if "--w" in a:
    i = a.index("--w"); W = int(a[i + 1]); del a[i:i + 2]
d, tag, out = a[0], a[1], a[2]
views = a[3:]
rows = []
for v in views:
    p = os.path.join(d, "%s_%s.png" % (tag, v))
    r = os.path.join(d, "%s_%s_ref.png" % (tag, v))
    ims = [Image.open(p).convert("RGB")] + ([Image.open(r).convert("RGB")] if os.path.exists(r) else [])
    ims = [im.resize((W, int(im.height * W / im.width)), Image.LANCZOS) for im in ims]
    row = Image.new("RGB", (W * len(ims), ims[0].height), "white")
    for i, im in enumerate(ims):
        row.paste(im, (i * W, 0))
    dr = ImageDraw.Draw(row)
    try:
        f = ImageFont.truetype("arial.ttf", 18)
    except Exception:
        f = ImageFont.load_default()
    dr.text((8, 6), v, fill=(255, 255, 0), font=f)
    rows.append(row)
H = sum(r.height for r in rows)
s = Image.new("RGB", (max(r.width for r in rows), H), "white")
y = 0
for r in rows:
    s.paste(r, (0, y)); y += r.height
s.save(out, quality=88)
print(out, s.size)
