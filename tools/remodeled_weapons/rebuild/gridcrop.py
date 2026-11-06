"""A study render cut to a region with a measured grid on it (lines every 0.01, labels every 0.05).

    python gridcrop.py view.png out.jpg  u0 u1 v0 v1  [left|right|top|bottom]
The study renders are orthographic at 4000 px per unit; left/right views span x -0.52..0.52, z -0.02..0.34 (camera
at z 0.16); top/bottom span x -0.52..0.52, y -0.12..0.12. u is x (left/top/bottom) or -x (right), v is z or y."""
import sys
from PIL import Image, ImageDraw, ImageFont
src, out = sys.argv[1], sys.argv[2]
u0, u1, v0, v1 = map(float, sys.argv[3:7])
view = sys.argv[7] if len(sys.argv) > 7 else "left"
PX = 4000.0
im = Image.open(src).convert("RGBA")
bg = Image.new("RGBA", im.size, (255, 255, 255, 255)); bg.alpha_composite(im); im = bg.convert("RGB")
W, H = im.size
vc = {"left": 0.16, "right": 0.16, "top": 0.0, "bottom": 0.0}[view]
def to_px(u, v):
    if view == "right":
        x = W / 2 + (-u) * PX
    else:
        x = W / 2 + u * PX
    if view == "bottom":
        y = H / 2 + (v - vc) * PX
    else:
        y = H / 2 - (v - vc) * PX
    return x, y
xa, ya = to_px(u0, v1); xb, yb = to_px(u1, v0)
box = (int(min(xa, xb)), int(min(ya, yb)), int(max(xa, xb)), int(max(ya, yb)))
c = im.crop(box)
scale = 1.0
if c.width < 1400:
    scale = 1400 / c.width
    c = c.resize((int(c.width * scale), int(c.height * scale)), Image.LANCZOS)
d = ImageDraw.Draw(c)
try:
    f = ImageFont.truetype("arial.ttf", 14)
except Exception:
    f = ImageFont.load_default()
import math
k0 = math.floor(min(u0, u1) * 100) ; k1 = math.ceil(max(u0, u1) * 100)
for k in range(k0, k1 + 1):
    u = k / 100.0
    x, _ = to_px(u, 0); x = (x - box[0]) * scale
    big = k % 5 == 0
    d.line([(x, 0), (x, c.height)], fill=(255, 0, 0) if big else (255, 170, 170), width=1)
    if big:
        d.text((x + 2, 2), "%.2f" % u, fill=(200, 0, 0), font=f)
k0 = math.floor(min(v0, v1) * 100); k1 = math.ceil(max(v0, v1) * 100)
for k in range(k0, k1 + 1):
    v = k / 100.0
    _, y = to_px(0, v); y = (y - box[1]) * scale
    big = k % 5 == 0
    d.line([(0, y), (c.width, y)], fill=(0, 0, 255) if big else (170, 170, 255), width=1)
    if big:
        d.text((2, y + 2), "%.2f" % v, fill=(0, 0, 200), font=f)
c.save(out, quality=92)
print(out, c.size)
