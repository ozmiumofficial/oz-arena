"""A study render (study.py) cut to a region with a measured grid on it: lines every 0.01, labels every 0.05.

    python gridcrop.py <study dir> <view|clay> <left|right|top|bottom|front|back> out.jpg u0 u1 v0 v1 [step]

u and v are world coordinates along the view's image right and up axes (views.json): left: u = x, v = z;
right: u = -x; top: u = x, v = y; bottom: u = x, v = -y; front: u = -y, v = z; back: u = y, v = z."""
import sys, os, json, math
from PIL import Image, ImageDraw, ImageFont

study, look, view, out = sys.argv[1:5]
u0, u1, v0, v1 = map(float, sys.argv[5:9])
step = float(sys.argv[9]) if len(sys.argv) > 9 else 0.01
V = json.load(open(os.path.join(study, "views.json")))[view]
im = Image.open(os.path.join(study, "%s_%s.png" % (look, view))).convert("RGBA")
bg = Image.new("RGBA", im.size, (255, 255, 255, 255)); bg.alpha_composite(im); im = bg.convert("RGB")
W, H = im.size
PX = V["px"]
cu = sum(a * b for a, b in zip(V["center"], V["u"]))
cv = sum(a * b for a, b in zip(V["center"], V["v"]))


def to_px(u, v):
    return W / 2 + (u - cu) * PX, H / 2 - (v - cv) * PX


xa, ya = to_px(u0, v1); xb, yb = to_px(u1, v0)
box = (int(max(0, xa)), int(max(0, ya)), int(min(W, xb)), int(min(H, yb)))
c = im.crop(box)
scale = 1400 / c.width if c.width < 1400 else (1800 / c.width if c.width > 1800 else 1.0)
c = c.resize((int(c.width * scale), int(c.height * scale)), Image.LANCZOS)
d = ImageDraw.Draw(c)
try:
    f = ImageFont.truetype("arial.ttf", 14)
except Exception:
    f = ImageFont.load_default()
for k in range(math.floor(u0 / step), math.ceil(u1 / step) + 1):
    u = k * step
    x = (to_px(u, 0)[0] - box[0]) * scale
    big = abs((u / 0.05) - round(u / 0.05)) < 1e-6
    d.line([(x, 0), (x, c.height)], fill=(255, 0, 0) if big else (255, 175, 175), width=1)
    if big:
        d.text((x + 2, 2), "%.2f" % u, fill=(200, 0, 0), font=f)
for k in range(math.floor(v0 / step), math.ceil(v1 / step) + 1):
    v = k * step
    y = (to_px(0, v)[1] - box[1]) * scale
    big = abs((v / 0.05) - round(v / 0.05)) < 1e-6
    d.line([(0, y), (c.width, y)], fill=(0, 0, 255) if big else (175, 175, 255), width=1)
    if big:
        d.text((2, y + 2), "%.2f" % v, fill=(0, 0, 200), font=f)
c.save(out, quality=90)
print(out, c.size)
