"""Side silhouette of a y-slab of the reference: the outline of what lies within |y| <= h (or y in [a, b]), traced and
simplified into a polygon in x-z, drawn over the left study render for checking.

    python slab_outline.py ref_mesh.npz view_left.png out_prefix ymin ymax xmin xmax [tol]
Writes out_prefix.png (overlay) and out_prefix.txt (the polygon(s), x z per line, blank line between)."""
import sys
import numpy as np
from PIL import Image, ImageDraw
from skimage.draw import polygon as rpoly
from skimage.measure import find_contours, approximate_polygon
from scipy import ndimage

src, view, out = sys.argv[1:4]
y0, y1, x0, x1 = map(float, sys.argv[4:8])
tol = float(sys.argv[8]) if len(sys.argv) > 8 else 0.0006
PX = 4000.0
m = np.load(src); co, tv = m["co"], m["tv"]
T = co[tv]                                   # n,3,3
sel = ((T[:, :, 1] >= y0) & (T[:, :, 1] <= y1)).all(1) & (T[:, :, 0].max(1) >= x0) & (T[:, :, 0].min(1) <= x1)
W, H = int(1.04 * PX), int(0.36 * PX)
mask = np.zeros((H, W), bool)
def px(x, z):
    return (W / 2 + x * PX, H / 2 - (z - 0.16) * PX)
for t in T[sel]:
    cs, rs = px(t[:, 0], t[:, 2])
    rr, cc = rpoly(rs, cs, (H, W))
    mask[rr, cc] = True
# clip to x range
c0 = int(W / 2 + x0 * PX); c1 = int(W / 2 + x1 * PX)
mask[:, :c0] = False; mask[:, c1:] = False
mask = ndimage.binary_closing(mask, iterations=2)
mask = ndimage.binary_fill_holes(mask)
lab, n = ndimage.label(mask)
sizes = ndimage.sum(mask, lab, range(1, n + 1))
keep = np.zeros_like(mask)
polys = []
for i in np.argsort(-sizes)[:6]:
    if sizes[i] < 4000:
        continue
    comp = lab == i + 1
    cs = find_contours(np.pad(comp, 1).astype(float), 0.5)
    c = max(cs, key=len) - 1
    c = approximate_polygon(c, tolerance=tol * PX)
    pts = [((col - W / 2) / PX, 0.16 - (row - H / 2) / PX) for row, col in c]
    polys.append(pts)
im = Image.open(view).convert("RGBA"); bg = Image.new("RGBA", im.size, (255, 255, 255, 255)); bg.alpha_composite(im)
im = bg.convert("RGB"); d = ImageDraw.Draw(im)
for pts in polys:
    P = [px(x, z) for x, z in pts]
    d.line(P + [P[0]], fill=(0, 160, 255), width=3)
    for p in P:
        d.ellipse([p[0] - 4, p[1] - 4, p[0] + 4, p[1] + 4], outline=(0, 0, 255))
box = (max(c0 - 40, 0), 0, min(c1 + 40, W), H)
im.crop(box).save(out + ".png")
with open(out + ".txt", "w") as f:
    for pts in polys:
        for x, z in pts:
            f.write("%.4f %.4f\n" % (x, z))
        f.write("\n")
print("polygons", [len(p) for p in polys])
