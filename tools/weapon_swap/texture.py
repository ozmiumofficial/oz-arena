"""python texture.py bake_dir out_dir : the railgun's cleaned PBR maps and red masks from the bakes.

Inputs (bake_dir): seg_in.npz (segment.py: Tripo's color baked onto the new UVs, misses filled, chart coverage),
bake_normal.png, bake_ao.png and bake_curv.png (bake_curv.py: the sealed surface's pointiness, 0.5 flat).

The texture is made to follow the shape (the curvature bake):
- Tripo's lettering and logos, and the bright edge highlights it painted where the surface has no edge, go:
  bright fine detail on a flat part of the surface is painted over from its surroundings.
- Wear (lighter, barer metal) goes on the real convex edges and grime in the real grooves.
- Its dark painted panel seams stay, cut into the normal map so they read as seams; the red trim is cut in too,
  a little, as an inset strip.
- The red paint keeps crisp edges (Tripo's red bleed onto the gray around it goes back to gray), split into
  the trim (thin streaks and outlines) and the cells (the large marbled red parts).

Outputs (out_dir), 4096 square:
  basecolor.png     the gray parts as they look; the red parts as neutral gray (their brightness), for the
                    shader to color with the player's rail color
  normal.png        16-bit tangent-space normal (OpenGL, green up), the bake plus the seams
  orm.png           R occlusion, G roughness, B metal
  tint.png          the red parts (rgb = alpha = mask), for the shader's multiply stage
  glow_trim.png     the trim's glow (pulses), glow_cell.png the cells' glow (veins brighter)
  debug_*.jpg       previews of the masks
"""
import sys, os
import numpy as np, cv2
bd, od = sys.argv[1], sys.argv[2]
os.makedirs(od, exist_ok=True)
d = np.load(os.path.join(bd, "seg_in.npz"))
base, cover, known = d["base"], d["cover"].astype(bool), d["known"]
S = base.shape[0]
inner = cv2.erode(cover.astype(np.uint8), np.ones((3, 3), np.uint8)).astype(bool)
cov_f = cover.astype(np.float32)


def smooth(x, a, b):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def fill(img, known, iters=48):
    """push known texels outward: repeated masked 3x3 averaging"""
    img = img.astype(np.float32).copy()
    sq = img.ndim == 2
    if sq:
        img = img[..., None]
    k = known.astype(np.float32)
    for _ in range(iters):
        s = cv2.blur(img * k[..., None], (3, 3))
        if s.ndim == 2:
            s = s[..., None]
        w = cv2.blur(k, (3, 3))
        grow = (k == 0) & (w > 0)
        if not grow.any():
            break
        img[grow] = s[grow] / w[grow][:, None]
        k[grow] = 1
    return img[..., 0] if sq else img


def sblur(x, sigma, m=cov_f):
    """blur over the covered texels only"""
    a = cv2.GaussianBlur(x * m, (0, 0), sigma)
    w = cv2.GaussianBlur(m, (0, 0), sigma)
    return a / np.maximum(w, 1e-4)


def read16(name):
    im = cv2.imread(os.path.join(bd, name), cv2.IMREAD_UNCHANGED).astype(np.float32)
    return im / (65535.0 if im.max() > 255 else 255.0)


def dbg(name, img):
    v = np.clip(img, 0, 1)
    v = (v * 255).astype(np.uint8)
    cv2.imwrite(os.path.join(od, "debug_" + name + ".jpg"), cv2.resize(v, (1600, 1600), interpolation=cv2.INTER_AREA))


lab = cv2.cvtColor(base, cv2.COLOR_RGB2LAB).astype(np.float32)
L = lab[..., 0] * 100 / 255
A = lab[..., 1] - 128
B = lab[..., 2] - 128
V = base.max(2).astype(np.float32) / 255

# --- curvature (pointiness - 0.5), facet noise smoothed away
curv = read16("bake_curv.png")
curv = (curv[..., 0] if curv.ndim == 3 else curv) - 0.5
curv = fill(curv, inner, 24)
curv = sblur(curv, 1.2)
convex = smooth(curv, 0.012, 0.045) * cover
concave = smooth(-curv, 0.012, 0.045) * cover
flat = 1 - np.maximum(smooth(np.abs(sblur(curv, 3)), 0.006, 0.02), np.maximum(convex, concave))

# --- red paint: crisp mask, then trim (thin) vs cells (large)
red_soft = smooth(A, 14, 26) * cover
P = (red_soft > 0.5).astype(np.uint8)
P = cv2.medianBlur(P, 3)
P = cv2.morphologyEx(P, cv2.MORPH_CLOSE, np.ones((3, 3), np.uint8))
n, cc, st, _ = cv2.connectedComponentsWithStats(P, 8)
keep = np.zeros(n, bool)
keep[1:] = st[1:, 4] >= 24            # lone red specks are not paint
P = keep[cc].astype(np.uint8)
core = cv2.morphologyEx(P, cv2.MORPH_OPEN, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (15, 15)))
n, cc, st, _ = cv2.connectedComponentsWithStats(core, 8)
big = np.zeros(n, bool)
big[1:] = st[1:, 4] >= 2500
cells = cv2.dilate(big[cc].astype(np.uint8), np.ones((5, 5), np.uint8)) & P
trim = P & (1 - cells)
aa = lambda m: cv2.GaussianBlur(m.astype(np.float32), (0, 0), 0.7)
paint, cell_m, trim_m = aa(P), aa(cells), aa(trim)
print("paint %.4f  cells %.4f  trim %.4f of covered" % tuple(m[cover].mean() for m in (paint, cell_m, trim_m)))

# --- gray: lettering, logos and painted highlights off the flat parts
# light detail against two scales of surroundings (glyph strokes, and the logo's solid triangle), on flat parts
# only (an edge's highlight is real), and not on the red paint itself (the logo sits right beside a trim line)
nonred = cover & ~P.astype(bool)
dL = np.maximum(L - sblur(L, 4, nonred.astype(np.float32)), L - sblur(L, 10, nonred.astype(np.float32)))
fake = (dL > 4.5) & (flat > 0.4) & nonred
fake = cv2.morphologyEx(fake.astype(np.uint8), cv2.MORPH_CLOSE, np.ones((3, 3), np.uint8))
fake = cv2.dilate(fake, np.ones((5, 5), np.uint8)) & nonred.astype(np.uint8)
print("painted-over (text, fake highlights) %.4f of covered" % (fake[cover].mean()))
# the same on the gray's fine texture: a light text glyph is often several shades lighter than its neighbours,
# so paint over from outside the mask (Telea), with the red parts left out of the source
src = np.clip(base, 0, 255).astype(np.uint8)
gray_src = src.copy()
redm = cv2.dilate(P, np.ones((5, 5), np.uint8)).astype(bool)
gray_src[redm] = np.median(src[cover & ~redm], axis=0).astype(np.uint8)
inp = cv2.inpaint(gray_src, fake, 4, cv2.INPAINT_TELEA)
lab_g = cv2.cvtColor(inp, cv2.COLOR_RGB2LAB).astype(np.float32)
Lg = lab_g[..., 0] * 100 / 255

# the dark painted seams: keep them, and cut them into the normals
Lgb = sblur(Lg, 4)
seam = smooth(Lgb - Lg, 3, 9) * flat * (1 - paint) * cover
# fine texture (weave, grip, grime) kept relative to the local mean, its contrast capped
detail = np.clip(Lg / np.maximum(Lgb, 1), 0.7, 1.35)

# gray ramp: dark coating / rubber below L 18, gunmetal above; wear on the real edges, grime in the grooves
Lgray = Lg * (1 + 0.3 * convex * smooth(Lgb, 10, 22)) + 4 * convex
Lgray = Lgray * (1 - 0.35 * concave)
metal = smooth(Lgb, 16, 26) * 0.75 + 0.25 * convex
cool_a, cool_b = -0.6, -2.0

# red parts: neutral gray at the paint's brightness (V), detail kept, for the rail color to tint
pv = V[P.astype(bool) & cover]
pV = np.median(pv)
Vd = np.clip(V / np.maximum(sblur(V, 4, P.astype(np.float32) + 1e-3), 1e-3), 0.6, 1.6)
# calmer cells: Tripo's projection streaked their marbling along the cylinders, chart by chart
cellf = cells.astype(np.float32) + 1e-3
Vd = np.where(cells.astype(bool), 1 + (sblur(Vd, 10, cellf) - 1) * 0.35, Vd)
paint_gray = np.clip(pV * Vd ** 0.7, 0.25, 0.95) * 100     # as L (approximately, gray)
Lc = paint * paint_gray + (1 - paint) * Lgray
Ac = (1 - paint) * cool_a
Bc = (1 - paint) * cool_b
lab2 = np.stack([Lc * 255 / 100, Ac + 128, Bc + 128], -1).clip(0, 255).astype(np.uint8)
albedo = cv2.cvtColor(lab2, cv2.COLOR_LAB2RGB)

# roughness / metal
rough_gray = 0.62 - 0.18 * metal - 0.12 * convex + 0.15 * concave + 0.25 * np.clip(1 - detail, -0.3, 0.3)
rough = paint * (0.38 - 0.1 * cell_m) + (1 - paint) * rough_gray
rough = np.clip(rough, 0.12, 0.95)
metallic = np.clip((1 - paint) * metal, 0, 1)

ao = read16("bake_ao.png")
ao = ao[..., 0] if ao.ndim == 3 else ao
ao = np.clip(ao * (1 - 0.25 * concave), 0, 1)

# --- normal: the bake plus seams and the trim's inset, as height gradients in UV space
nrm = read16("bake_normal.png")[..., ::-1]                  # RGB, 0..1
nrm = fill(nrm, inner, 24)
h = -(0.6 * cv2.GaussianBlur(seam, (0, 0), 1.0) + 0.5 * cv2.GaussianBlur(trim_m, (0, 0), 1.2))
gy, gx = np.gradient(h)                                       # per texel; rows run down = -v
k = 2.2
dn = np.stack([-gx * k, gy * k], -1)                          # tangent x along u, y along v
n1 = nrm * 2 - 1
n1[..., :2] += dn
n1 /= np.linalg.norm(n1, axis=2, keepdims=True)
nrm_out = (n1 * 0.5 + 0.5)

# --- glow layers
glow_trim = trim_m
# the cells' veins, softened: Tripo's projection streaked them along the cylinders
cell_v = np.clip((sblur(V, 10, cells.astype(np.float32) + 1e-3) - 0.35) / 0.55, 0, 1)
glow_cell = cell_m * (0.7 + 0.3 * cell_v)

dbg("paint", paint); dbg("trim", trim_m); dbg("cells", cell_m); dbg("fake", fake.astype(np.float32))
dbg("convex", convex); dbg("concave", concave); dbg("seam", seam)


def save8(name, img, gray=False):
    img = fill(img, inner)
    out = np.clip(img, 0, 255).astype(np.uint8)
    cv2.imwrite(os.path.join(od, name), out[..., ::-1] if out.ndim == 3 else out)


save8("basecolor.png", albedo.astype(np.float32))
orm = np.stack([ao * 255, rough * 255, metallic * 255], -1)
save8("orm.png", orm)
save8("tint.png", np.repeat(paint[..., None], 4, 2)[..., :3] * 255)
save8("glow_trim.png", glow_trim * 255)
save8("glow_cell.png", glow_cell * 255)
save8("roughness.png", rough * 255)
save8("metallic.png", metallic * 255)
save8("ao.png", ao * 255)
nf = fill(nrm_out * 65535, inner)
cv2.imwrite(os.path.join(od, "normal.png"), np.clip(nf[..., ::-1], 0, 65535).astype(np.uint16))
cv2.imwrite(os.path.join(od, "pv_basecolor.jpg"), cv2.resize(cv2.imread(os.path.join(od, "basecolor.png")), (1600, 1600)))
print("written", od)
