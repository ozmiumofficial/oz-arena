"""Paint a fighter's texture by where its texels land on the model, not by where they sit in Tripo's scattered UV
layout. python retouch.py fighters/<name>.json clean.npz in.jpg out.jpg [--test]

The config's "retouch" is a list of spots on the face, each an ellipse seen from the front (Blender T-pose
coordinates, x his left, -y front, z up): {"center": [x, y, z], "radii": [rx, rz], "do": "socket" | "eye"}.
  socket: an eye taken out, a dark hollow fading to scarred red skin at the rim (with "dent" in the config, the mesh's
          eye there is pressed back too, stretch_arms.py)
  eye:    an eye whose texture doesn't cover its eyeball: inside the opening, skin-coloured texels turn to the white of
          the eye, shadowed under the upper lid
--test fills each spot with a flat colour, to see where it lands."""
import sys, json
import numpy as np
from PIL import Image

cfg_path, npz, src, dst = sys.argv[1:5]
TEST = "--test" in sys.argv
cfg = json.load(open(cfg_path))
d = np.load(npz)
co, tv, tl, uv = d["co"], d["tri_verts"], d["tri_loops"], d["uv"]
img = np.asarray(Image.open(src).convert("RGB")).astype(float)
H, W = img.shape[:2]
out = img.copy()

for spot in cfg["retouch"]:
    c = np.array(spot["center"], float)
    rx, rz = spot["radii"]
    grow = spot.get("grow", 1.5)          # how far out (in radii) the spot's edge blends into the skin
    tri_c = co[tv].mean(1)
    near = np.flatnonzero((np.abs(tri_c[:, 0] - c[0]) < rx * grow + 0.004) & (np.abs(tri_c[:, 2] - c[2]) < rz * grow + 0.004)
                          & (tri_c[:, 1] < c[1] + 0.02))
    # rasterize those triangles in UV space; each texel gets its 3D point
    mask = np.zeros((H, W), bool)
    pts = np.zeros((H, W, 3))
    for t in near:
        U = np.stack([uv[tl[t]][:, 0] * W, (1 - uv[tl[t]][:, 1]) * H], 1)
        P = co[tv[t]]
        x0, y0 = np.floor(U.min(0)).astype(int) - 1
        x1, y1 = np.ceil(U.max(0)).astype(int) + 1
        x0, y0, x1, y1 = max(x0, 0), max(y0, 0), min(x1, W - 1), min(y1, H - 1)
        if x1 <= x0 or y1 <= y0 or max(x1 - x0, y1 - y0) > 96:     # a cap spanning UV charts: not paintable
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        a, b, cc = U
        den = (b[1] - cc[1]) * (a[0] - cc[0]) + (cc[0] - b[0]) * (a[1] - cc[1])
        if abs(den) < 1e-12:
            continue
        l1 = ((b[1] - cc[1]) * (gx - cc[0]) + (cc[0] - b[0]) * (gy - cc[1])) / den
        l2 = ((cc[1] - a[1]) * (gx - cc[0]) + (a[0] - cc[0]) * (gy - cc[1])) / den
        l3 = 1 - l1 - l2
        e = -1.0 / max(np.ptp(U[:, 0]), np.ptp(U[:, 1]), 1)      # a texel's worth of slack: no seams at the edges
        inside = (l1 >= e) & (l2 >= e) & (l3 >= e)
        if not inside.any():
            continue
        p3 = l1[..., None] * P[0] + l2[..., None] * P[1] + l3[..., None] * P[2]
        sl = (slice(y0, y1 + 1), slice(x0, x1 + 1))
        mask[sl] |= inside
        pts[sl][inside] = p3[inside]
    yy, xx = np.nonzero(mask)
    p = pts[yy, xx]
    r = np.sqrt(((p[:, 0] - c[0]) / rx) ** 2 + ((p[:, 2] - c[2]) / rz) ** 2)     # 0 middle, 1 the opening's edge
    th = np.arctan2((p[:, 2] - c[2]) / rz, (p[:, 0] - c[0]) / rx)
    col = out[yy, xx]
    if TEST:
        hit = r < 1
        col[hit] = [255, 0, 255] if spot["do"] == "socket" else [0, 255, 0]
        col[(r >= 1) & (r < grow)] = [255, 255, 0]
    elif spot["do"] == "socket":
        # the hollow: near black in the middle, raw red toward a ragged rim, then healed scar fading into the skin
        # (and over whatever eye was painted there)
        rr = r * (1 + 0.08 * np.sin(7 * th + 0.6) + 0.05 * np.sin(13 * th + 2.1))
        mott = 1 + 0.10 * np.sin(p[:, 0] * 2300 + 1.3) * np.sin(p[:, 2] * 1900)
        hollow = np.array([14, 6, 6.0])
        raw = np.array([105, 36, 32.0])
        scar = np.array([168, 104, 92.0])
        w_in = np.clip((1.0 - rr) / 0.6, 0, 1) ** 0.6
        inner_col = (w_in[:, None] * hollow + (1 - w_in[:, None]) * raw) * mott[:, None]
        out_a = 0.9 * np.clip(1 - (rr - 1) / (grow - 1), 0, 1) ** 1.4
        white = (col.min(1) > 150) & (rr < grow)                     # the old eye's white: scar it over
        out_a = np.where(white, np.maximum(out_a, 0.95), out_a)
        outer_col = col * (1 - out_a[:, None]) + (scar * mott[:, None]) * out_a[:, None]
        edge = np.clip((rr - 0.92) / 0.16, 0, 1)[:, None]          # a soft step from the hollow to the scar
        col = inner_col * (1 - edge) + outer_col * edge
    elif spot["do"] == "paint_eye":
        # a fresh eye in the opening: the white, shaded toward the corners and under the upper lid, a brown iris and
        # a pupil looking straight ahead, a catch light; the old painted eye left on the lids around it turns to skin
        u, v = (p[:, 0] - c[0]) / rx, (p[:, 2] - c[2]) / rz
        ir = np.hypot((p[:, 0] - c[0]), (p[:, 2] - c[2] + rz * 0.15)) / spot.get("iris", 0.0036)
        sclera = np.array([214, 204, 196.0]) * (1 - 0.35 * np.clip(np.abs(u) - 0.45, 0, 1) / 0.55)[:, None]
        sclera *= (1 - 0.35 * np.clip(v - 0.2, 0, 1))[:, None]
        iris = np.array([96, 66, 46.0]) * (0.75 + 0.35 * np.clip(1 - ir, 0, 1))[:, None] * \
            (1 + 0.12 * np.sin(np.arctan2(v, u) * 23))[:, None]
        eye = np.where((ir < 1)[:, None], iris, sclera)
        eye = np.where((ir < 0.42)[:, None], np.array([14, 11, 11.0]), eye)
        hl = np.hypot(p[:, 0] - (c[0] + 0.0012), p[:, 2] - (c[2] + rz * 0.15)) < 0.0006
        eye[hl] = [235, 232, 228]
        eye *= (1 - 0.30 * np.clip(v, 0, 1))[:, None]                 # the upper lid's shadow
        inner = np.clip((1.0 - r) / 0.12, 0, 1)[:, None]
        ring = (r >= 1.0) & (r < grow)
        skinref = col[(r > grow * 0.85) & (col[:, 0] - col[:, 2] > 30)]
        tone = np.median(skinref, 0) if len(skinref) else np.array([190, 150, 130.0])
        odd = ring & ((col.min(1) > 150) | (col.sum(1) < 330))         # leftover white or dark from the old eye
        col[odd] = col[odd] * 0.15 + tone * 0.85
        col = col * (1 - inner) + eye * inner
    elif spot["do"] == "move":
        # the painted eye sits off the eyeball: each texel in the opening takes the colour painted where the eye's
        # picture has that spot ("from": the painted eye's middle, "scale": its size against the opening's), blended
        # out over the outer ring
        from scipy.spatial import cKDTree
        f = np.array(spot["from"], float)
        sx, sz = spot["scale"]
        src2 = np.stack([f[0] + (p[:, 0] - c[0]) * sx, f[2] + (p[:, 2] - c[2]) * sz], 1)
        tree = cKDTree(p[:, [0, 2]])
        _, k = tree.query(src2)
        w = np.clip((grow - r) / (grow - 1.0), 0, 1)[:, None]
        col = col * (1 - w) + img[yy[k], xx[k]] * w
    else:
        # skin-coloured texels inside the opening (red well above blue, not dark, not white) become the white of the
        # eye, darker toward the lids
        rgb = col
        skin = (rgb[:, 0] - rgb[:, 2] > 35) & (rgb.sum(1) > 300)
        hit = skin & (r < 0.95)
        lid = np.clip((p[:, 2] - c[2]) / rz, -1, 1)                  # -1 lower lid .. 1 upper lid
        white = np.array([205, 195, 188.0]) * (0.72 + 0.18 * (1 - np.abs(lid)))[:, None] - (lid > 0.4)[:, None] * 40
        soft = np.clip((0.95 - r) / 0.15, 0, 1)[:, None]
        col[hit] = col[hit] * (1 - soft[hit]) + white[hit] * soft[hit]
    out[yy, xx] = col
    print("%-6s %s: %d texels in reach, %d inside the opening" % (spot["do"], spot["center"], len(r), int((r < 1).sum())))

Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)).save(dst, quality=95)
print("wrote", dst)
