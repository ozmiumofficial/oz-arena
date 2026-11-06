"""python grid.py <dir> <tag> <out.jpg> view [view ...] [--w 400] [--cols 2]
rg_look.shots' renders in a grid: each cell the rebuild then the reference for one view, `cols` views a row."""
import sys, os
from PIL import Image, ImageDraw, ImageFont

a = sys.argv[1:]
W, C = 400, 2
for flag in ("--w", "--cols"):
    if flag in a:
        i = a.index(flag); v = int(a[i + 1]); del a[i:i + 2]
        if flag == "--w":
            W = v
        else:
            C = v
d, tag, out = a[0], a[1], a[2]
views = a[3:]
cells = []
for v in views:
    ims = [Image.open(os.path.join(d, "%s_%s%s.png" % (tag, v, k))).convert("RGB") for k in ("", "_ref")
           if os.path.exists(os.path.join(d, "%s_%s%s.png" % (tag, v, k)))]
    ims = [im.resize((W, int(im.height * W / im.width)), Image.LANCZOS) for im in ims]
    cell = Image.new("RGB", (W * 2, ims[0].height), "black")
    for i, im in enumerate(ims):
        cell.paste(im, (i * W, 0))
    dr = ImageDraw.Draw(cell)
    try:
        f = ImageFont.truetype("arial.ttf", 16)
    except Exception:
        f = ImageFont.load_default()
    dr.text((6, 4), v, fill=(255, 255, 0), font=f)
    dr.line([(W, 0), (W, cell.height)], fill=(90, 90, 90), width=2)
    cells.append(cell)
rows = (len(cells) + C - 1) // C
H = cells[0].height
s = Image.new("RGB", (W * 2 * C, H * rows), (40, 40, 40))
for i, c in enumerate(cells):
    r, k = divmod(i, C)
    s.paste(c, (k * W * 2, r * H))
s.save(out, quality=86)
print(out, s.size)
