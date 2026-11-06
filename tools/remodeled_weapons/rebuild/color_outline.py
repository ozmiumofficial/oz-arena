"""Outlines of one paint in a side study render: the gray panels on the red body, say. Traced, simplified, drawn
over the render with numbers so a part's outline can be read off.

    python color_outline.py view_left.png out_prefix  x0 x1 z0 z1  gray|red|dark [min_area_px] [tol]"""
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from skimage.measure import find_contours, approximate_polygon
from scipy import ndimage

view, out = sys.argv[1:3]
x0, x1, z0, z1 = map(float, sys.argv[3:7])
kind = sys.argv[7]
amin = float(sys.argv[8]) if len(sys.argv) > 8 else 3000
tol = float(sys.argv[9]) if len(sys.argv) > 9 else 0.0008
PX = 4000.0
im = Image.open(view).convert("RGBA")
a = np.asarray(im).astype(float) / 255
W, H = im.size
rgb = a[..., :3]; al = a[..., 3]
mx = rgb.max(-1); mn = rgb.min(-1); sat = (mx - mn) / np.maximum(mx, 1e-3)
if kind == "gray":
    m = (al > 0.5) & (sat < 0.25) & (mx > 0.30)
elif kind == "red":
    m = (al > 0.5) & (sat > 0.35) & (rgb[..., 0] > rgb[..., 1] * 1.5)
else:
    m = (al > 0.5) & (sat < 0.3) & (mx <= 0.30)
def px(x, z):
    return (W / 2 + x * PX, H / 2 - (z - 0.16) * PX)
c0, r1 = px(x0, z0); c1, r0 = px(x1, z1)
reg = np.zeros_like(m); reg[int(r0):int(r1), int(c0):int(c1)] = True
m &= reg
m = ndimage.binary_opening(m, iterations=2)
m = ndimage.binary_closing(m, iterations=3)
m = ndimage.binary_fill_holes(m)
lab, n = ndimage.label(m)
sizes = ndimage.sum(m, lab, range(1, n + 1))
bg = Image.new("RGBA", im.size, (255, 255, 255, 255)); bg.alpha_composite(im); img = bg.convert("RGB")
d = ImageDraw.Draw(img)
try:
    f = ImageFont.truetype("arial.ttf", 22)
except Exception:
    f = ImageFont.load_default()
fo = open(out + ".txt", "w")
k = 0
for i in np.argsort(-sizes):
    if sizes[i] < amin:
        break
    comp = lab == i + 1
    c = max(find_contours(np.pad(comp, 1).astype(float), 0.5), key=len) - 1
    c = approximate_polygon(c, tolerance=tol * PX)
    pts = [((col - W / 2) / PX, 0.16 - (row - H / 2) / PX) for row, col in c]
    P = [px(x, z) for x, z in pts]
    d.line(P, fill=(0, 140, 255), width=3)
    cx = sum(p[0] for p in P) / len(P); cy = sum(p[1] for p in P) / len(P)
    d.text((cx, cy), str(k), fill=(255, 0, 0), font=f)
    fo.write("# %d area %.5f\n" % (k, sizes[i] / PX / PX))
    for x, z in pts[:-1]:
        fo.write("%.4f %.4f\n" % (x, z))
    k += 1
fo.close()
img.crop((int(c0) - 20, int(r0) - 20, int(c1) + 20, int(r1) + 20)).save(out + ".png")
print("regions", k)
