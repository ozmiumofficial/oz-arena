"""The game icon: a beveled bronze OZ on a dark steel badge with an ember glow, in the style of the
OZMIUM ARENA logo. Writes engine/quake3e/code/win32/oz.ico and engine/installer/linux/oz-arena.png.

python engine/tools/make_icon.py [--preview out.png] [--master out.png]

The letters and badge are drawn as shapes (no font, no id art), so the icon is ours to ship under
the GPL. Sizes from 48 up come from one detailed 1024 master (scratches, rivets, rust); 40 and
below are drawn again at 8x without the fine detail, with bigger letters and a heavier outline, so
they stay readable.
The .ico holds PNG images, which Windows reads from Vista on; win_resource.rc builds it into both
exes, and SDL takes the window and taskbar icon from the exe.
"""
import argparse, io, os, struct
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage as ndi

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
ICO = os.path.join(ROOT, "engine", "quake3e", "code", "win32", "oz.ico")
LINUX_PNG = os.path.join(ROOT, "engine", "installer", "linux", "oz-arena.png")

SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
SMALL = 40                   # this size and below: the simple drawing


def noise(rng, shape, scales, weights):
    n = np.zeros(shape, np.float32)
    for s, w in zip(scales, weights):
        r = ndi.gaussian_filter(rng.standard_normal(shape).astype(np.float32), s)
        n += w * r / (r.std() + 1e-6)
    return n


def poly_mask(n, polys, holes=(), scale=1.0):
    m = Image.new("L", (n, n), 0)
    d = ImageDraw.Draw(m)
    at = lambda p: [((0.5 + (x - 0.5) * scale) * n, (0.5 + (y - 0.5) * scale) * n) for x, y in p]
    for p in polys:
        d.polygon(at(p), fill=255)
    for p in holes:
        d.polygon(at(p), fill=0)
    return np.asarray(m, np.float32) / 255


def chamfer_box(x0, y0, x1, y1, tl=0.0, tr=0.0, br=0.0, bl=0.0):
    return [(x0 + tl, y0), (x1 - tr, y0), (x1, y0 + tr), (x1, y1 - br), (x1 - br, y1),
            (x0 + bl, y1), (x0, y1 - bl), (x0, y0 + tl)]


# shapes in icon units (0..1)
BADGE = chamfer_box(0.02, 0.02, 0.98, 0.98, 0.16, 0.06, 0.16, 0.06)
TOP, BOT, T = 0.27, 0.73, 0.125          # letters' box and bar thickness
O_OUT = chamfer_box(0.10, TOP, 0.48, BOT, tl=0.11, br=0.11)
O_IN = chamfer_box(0.10 + T, TOP + T, 0.48 - T, BOT - T, tl=0.035, br=0.035)
ZX0, ZX1 = 0.52, 0.90
Z = [(ZX0, TOP), (ZX1, TOP), (ZX1, TOP + T), (ZX0 + T * 1.25, BOT - T), (ZX1 + 0.0, BOT - T),
     (ZX1 - 0.06, BOT), (ZX0, BOT), (ZX0, BOT - T), (ZX1 - T * 1.25, TOP + T), (ZX0 + 0.06, TOP + T)]


def shade(face, bevel, top, mid, bot, rng, detail, glint):
    """Beveled metal lit from the top left. face: 0..1 mask. Returns RGB 0..255."""
    n_ = face.shape[0]
    d = ndi.distance_transform_edt(face > 0.5)
    h = 1 - (1 - np.clip(d / bevel, 0, 1)) ** 2
    h = ndi.gaussian_filter(h, max(0.6, bevel / 12)) * face
    gy, gx = np.gradient(h * bevel * 1.6)
    nrm = np.dstack([-gx, -gy, np.ones_like(h)])
    nrm /= np.linalg.norm(nrm, axis=2, keepdims=True)
    L = np.array([-0.45, -0.75, 0.55]); L /= np.linalg.norm(L)
    H = L + [0, 0, 1]; H /= np.linalg.norm(H)
    diff = np.clip(nrm @ L, 0, 1)
    spec = np.clip(nrm @ H, 0, 1) ** 36

    ys = np.nonzero(face.max(1) > 0.5)[0]
    t = np.clip((np.arange(n_) - ys.min()) / max(1, ys.max() - ys.min()), 0, 1)[:, None, None]
    top, mid, bot = (np.array(c, np.float32) for c in (top, mid, bot))
    base = np.where(t < 0.5, top + (mid - top) * t / 0.5, mid + (bot - mid) * (t - 0.5) / 0.5)
    base = np.broadcast_to(base, (n_, n_, 3)).copy()
    if detail:
        base *= (1 + 0.12 * noise(rng, (n_, n_), (2, 6, 18, 48), (0.15, 0.25, 0.3, 0.3)))[..., None]
        rust = np.clip((noise(rng, (n_, n_), (8, 24, 60), (0.3, 0.4, 0.3)) - 0.8) * 1.2, 0, 1) * 0.5
        base = base * (1 - rust[..., None]) + np.array([110, 45, 20], np.float32) * rust[..., None]
        sc = Image.new("L", (n_, n_), 0); dr = ImageDraw.Draw(sc)
        for _ in range(260):
            x, y = rng.uniform(0, n_, 2); a = rng.normal(-0.35, 0.25); l = rng.uniform(10, 45)
            dr.line([(x, y), (x + np.cos(a) * l, y + np.sin(a) * l)], fill=int(rng.uniform(60, 200)))
        base += (np.asarray(sc, np.float32) / 255 * 60 * (h > 0.95))[..., None]
    col = base * (0.35 + 0.85 * diff)[..., None] + (spec * 255 * glint)[..., None] * np.array([1, 0.92, 0.8])
    crease = ((d > bevel * 0.9) & (d < bevel * 1.3)).astype(np.float32)
    col *= (1 - 0.35 * ndi.gaussian_filter(crease, 0.7))[..., None]
    return np.clip(col, 0, 255)


def over(dst, rgb, a):
    a = a[..., None]
    oa = dst[..., 3:4]
    na = a + oa * (1 - a)
    dst[..., :3] = np.where(na > 0, (rgb * a + dst[..., :3] * oa * (1 - a)) / np.maximum(na, 1e-6), 0)
    dst[..., 3:4] = na


def draw(n, detail):
    """The icon at n x n pixels. detail: the master's fine detail; else the simple small drawing."""
    rng = np.random.default_rng(11)
    u = n / 1024                                      # pixels per master pixel
    badge = poly_mask(n, [BADGE])
    letters = poly_mask(n, [O_OUT, Z], [O_IN], 1.0 if detail else 1.16)
    out = np.zeros((n, n, 4), np.float32)

    # badge: dark steel, a dark rim, an ember glow behind the letters
    plate = shade(badge, 26 * u, (88, 84, 82), (46, 43, 42), (26, 23, 22), rng, detail, 0.5)
    yy, xx = np.mgrid[:n, :n].astype(np.float32) / n
    ember = np.clip(1 - np.hypot(xx - 0.5, (yy - 0.5) * 1.5) / 0.55, 0, 1) ** 1.6
    plate = plate * (1 - 0.55 * ember[..., None]) + np.array([235, 70, 12], np.float32) * 0.55 * ember[..., None]
    glow = ndi.gaussian_filter(ndi.binary_dilation(letters > 0.5, iterations=max(1, round(30 * u))).astype(np.float32), 34 * u)
    plate = plate + np.array([255, 80, 10], np.float32) * np.clip(glow * 1.3, 0, 1)[..., None] * 0.7
    if detail:                                        # rivets in the four plain corners
        for rx, ry in ((0.10, 0.10), (0.90, 0.10), (0.10, 0.90), (0.90, 0.90)):
            if (rx < 0.5) == (ry < 0.5):              # the chamfered corners have no room
                continue
            r = np.hypot(xx - rx, yy - ry) * n; rv = np.clip(14 * u - r, 0, 1)
            lit = np.clip(1 - np.hypot(xx - rx + 0.005, yy - ry + 0.005) * n / (14 * u), 0, 1)
            plate = plate * (1 - rv[..., None]) + (40 + 200 * lit)[..., None] * rv[..., None]
    rim = badge - ndi.binary_erosion(badge > 0.5, iterations=max(1, round(10 * u))).astype(np.float32)
    plate *= (1 - 0.7 * np.clip(rim, 0, 1))[..., None]
    over(out, np.clip(plate, 0, 255), badge)

    # letters: black outline and shadow, then bronze
    edge = max(1, round((22 if detail else 34) * u))
    stroke = ndi.binary_dilation(letters > 0.5, iterations=edge).astype(np.float32)
    shadow = ndi.shift(ndi.gaussian_filter(stroke, 10 * u), (14 * u, 10 * u), order=1) * 0.8
    over(out, np.zeros((n, n, 3), np.float32), shadow * badge)
    over(out, np.full((n, n, 3), 10, np.float32), ndi.gaussian_filter(stroke, 0.8 * u + 0.3))
    oz = shade(letters, 30 * u, (245, 200, 130), (150, 120, 90), (160, 48, 20), rng, detail, 1.1)
    over(out, oz, letters)
    out[..., 3] *= 255
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8), "RGBA")


def icon(size, master):
    if size > SMALL:
        return master.resize((size, size), Image.LANCZOS)
    return draw(size * 8, detail=False).resize((size, size), Image.LANCZOS)


def write_ico(path, images):
    pngs = []
    for im in images:
        b = io.BytesIO()
        im.save(b, "PNG", optimize=True)
        pngs.append(b.getvalue())
    head = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries = b""
    for im, data in zip(images, pngs):
        w, h = im.size
        entries += struct.pack("<BBBBHHII", w % 256, h % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    with open(path, "wb") as f:
        f.write(head + entries + b"".join(pngs))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", help="also write a sheet of every size on light and dark grounds")
    ap.add_argument("--master", help="also write the 1024 master")
    a = ap.parse_args()

    master = draw(1024, detail=True)
    images = [icon(s, master) for s in SIZES]
    write_ico(ICO, images)
    images[-1].save(LINUX_PNG, optimize=True)
    print("wrote", os.path.relpath(ICO, ROOT), "(%s)" % ", ".join(map(str, SIZES)))
    print("wrote", os.path.relpath(LINUX_PNG, ROOT))
    if a.master:
        master.save(a.master, optimize=True)
        print("wrote", a.master)

    if a.preview:
        pad, row = 12, max(SIZES) + 24
        width = sum(SIZES) + pad * (len(SIZES) + 1)
        sheet = Image.new("RGBA", (width, row * 2), (0, 0, 0, 255))
        for r, ground in enumerate([(243, 243, 243), (32, 32, 32)]):
            sheet.paste(ground + (255,), (0, r * row, width, (r + 1) * row))
            x = pad
            for im in images:
                y = r * row + (row - im.size[1]) // 2
                sheet.alpha_composite(im, (x, y))
                x += im.size[0] + pad
        sheet.save(a.preview)
        print("wrote", a.preview)


if __name__ == "__main__":
    main()
