"""After rg_bake.py: the helper masks, then the maps tools/weapon_swap/pack.py packs (adapted from rl_post.py).

    python rg_post.py <bake dir>                masks: ao.png, edge.png (from ao_geo.png, edge_geo.png) and
                                                detail_normal.png (the normal map the materials read)
    python rg_post.py <bake dir> pack <pbr dir> the game's maps, as weapon_swap/pack.py reads them:
        basecolor.png   the pbr bake, the red parts neutral gray at their brightness (the shader tints them)
        normal.png      detail_normal.png as 16 bit, Blender's green up (pack.py flips it for rend2)
        orm.png         R AO, G roughness, B metalness
        tint.png        the red parts: light strips, cells, vent glow (rgbGen entity: the rail color)
        glow_trim.png   the light strips (the trim's pulse)
        glow_cell.png   the cells (their veins brighter) and the vents' glow (the cells' breathing)

The cells are the gun's energy tanks: their veins are Tripo's own, cast onto the rebuilt cells (rg_bake.py refcolor)
and treated as tools/weapon_swap/texture.py treats them for the shipped railgun: gray at the red's brightness with
its detail relative to the local mean (calmed, Tripo's projection streaks it), the glow 0.7 + 0.3 of the veins.

The normal map's divergence says where the surface bends: negative in grooves and seams (cavity, darkened in the AO),
positive on the crests of ridges and bolt rims (added to the edge mask).
"""
import sys, os, shutil
import numpy as np
from PIL import Image
from scipy import ndimage

Image.MAX_IMAGE_PIXELS = None
D = sys.argv[1]
rd = lambda n, mode="RGB": np.asarray(Image.open(os.path.join(D, n)).convert(mode)).astype(np.float32) / 255
wr = lambda a, n: Image.fromarray((np.clip(a, 0, 1) * 255 + 0.5).astype(np.uint8)).save(n)
CELL_DENSITY = 1.7          # rg_finalize.py: the cells' texels per unit relative to the rest


def sblur(x, sigma, m):
    """Blur over the masked texels only."""
    a = ndimage.gaussian_filter(x * m, sigma)
    w = ndimage.gaussian_filter(m, sigma)
    return a / np.maximum(w, 1e-4)


def srgb_from_L(L):
    """CIE lightness (0..100) of a neutral gray -> its sRGB value 0..1."""
    Y = np.where(L > 8, ((L + 16) / 116) ** 3, L / 903.3)
    return np.where(Y <= 0.0031308, 12.92 * Y, 1.055 * np.power(np.maximum(Y, 0), 1 / 2.4) - 0.055)


if len(sys.argv) > 2 and sys.argv[2] == "pack":
    out = sys.argv[3]
    os.makedirs(out, exist_ok=True)
    base = rd("basecolor.png")
    S = base.shape[0]
    k = S / 4096.0
    mid = rd("matid.png")
    near = lambda c: (np.abs(mid - np.array(c, np.float32)).max(2) < 0.2).astype(np.float32)
    trim, cell, vent = near((0, 0, 1)), near((1, 1, 0)), near((1, 1, 1))
    aa = lambda m: ndimage.gaussian_filter(m, 0.7 * k)
    trim_m, cell_m, vent_m = aa(trim), aa(cell), aa(vent)
    ao = rd("ao.png", "L")
    # the cells' energy: Tripo's own pattern, cast onto the rebuilt cells (refcolor), filled in where the cast found
    # nothing from what it found nearby. As in the shipped railgun (weapon_swap/texture.py), its red takes the rail
    # color (the tint mask follows the red's saturation) while the white veins stay out of the tint and read as
    # bright filaments; the gray is the pattern's brightness, kept sharp (only a light blur for Tripo's chart seams).
    ref = rd("refcolor.png")
    hit = (rd("refhit.png", "L") > 0.5).astype(np.float32)
    cm = (cell > 0.5)
    fill = np.dstack([sblur(ref[..., i], 14 * k * CELL_DENSITY, hit) for i in range(3)])
    refc = np.where(hit[..., None] > 0.5, ref, fill)
    refc = np.dstack([ndimage.gaussian_filter(refc[..., i], 0.6 * k * CELL_DENSITY) for i in range(3)])
    V = refc.max(2)
    sat = 1 - refc.min(2) / np.maximum(V, 1e-3)              # the red ~0.85, the pale veins ~0.2
    redness = np.clip((sat - 0.30) / (0.65 - 0.30), 0, 1)
    redness = redness * redness * (3 - 2 * redness)
    cell_gray = np.clip(V, 0.25, 0.95)
    cell_v = np.clip((sblur(V, 3 * k * CELL_DENSITY, cm.astype(np.float32) + 1e-3) - 0.35) / 0.55, 0, 1)
    print("cells: %.1f%% of texels, %.1f%% of them found on the reference, red %.0f%% of them, median V %.3f"
          % (100 * cell.mean(), 100 * (hit * cell).sum() / max(cell.sum(), 1),
             100 * (redness * cell).sum() / max(cell.sum(), 1), float(np.median(V[cm])) if cm.any() else 0))
    gray = cell_m * cell_gray + trim_m * 0.82 + vent_m * 0.62
    red_parts = np.clip(cell_m + trim_m + vent_m, 0, 1)                   # stored gray in the base color
    red = np.clip(cell_m * redness + trim_m + vent_m, 0, 1)              # what the rail color tints
    base = base * (1 - red_parts[..., None]) + gray[..., None] * red_parts[..., None]
    wr(base, os.path.join(out, "basecolor.png"))
    nm = np.asarray(Image.open(os.path.join(D, "detail_normal.png")).convert("RGB")).astype(np.uint16) * 257
    import cv2
    cv2.imwrite(os.path.join(out, "normal.png"), nm[..., ::-1])
    r = rd("roughness.png", "L"); m = rd("metallic.png", "L")
    wr(np.dstack([ao, r, m]), os.path.join(out, "orm.png"))
    wr(red, os.path.join(out, "tint.png"))
    wr(trim_m, os.path.join(out, "glow_trim.png"))
    wr(np.clip(cell_m * (0.7 + 0.3 * cell_v) + vent_m * 0.6, 0, 1), os.path.join(out, "glow_cell.png"))
    for n in ("roughness.png", "metallic.png", "ao.png"):
        shutil.copy(os.path.join(D, n), os.path.join(out, n))
    print("packed into", out)
    sys.exit(0)

nm = rd("normal.png") * 2 - 1
S = nm.shape[0]
k = S / 4096.0
# a face buried in another part can catch that part's side in the bake: a broad patch tilted far over. Real detail
# (grooves, bolt rims, ribs) only tilts in thin lines, so whatever stays tilted after a 3-texel erosion is reset flat.
tilted = nm[..., 2] < 0.75
thick = ndimage.binary_dilation(ndimage.binary_erosion(tilted, iterations=int(3 * k) or 1), iterations=int(5 * k) or 1)
nm[thick] = (0.0, 0.0, 1.0)
print("flattened %.2f%% of texels" % (100 * thick.mean()))
Image.fromarray(((nm * 0.5 + 0.5) * 255 + 0.5).clip(0, 255).astype(np.uint8)).save(os.path.join(D, "normal_clean.png"))
dx = ndimage.sobel(nm[..., 0], axis=1) / 8.0
dy = -ndimage.sobel(nm[..., 1], axis=0) / 8.0            # +v is up the image
div = ndimage.gaussian_filter(dx + dy, 1.2 * k)
cav = np.clip(-div * 9.0, 0, 1)
ridge = np.clip(div * 4.0, 0, 1)
ao = rd("ao_geo.png", "L") ** 0.55 * (1 - 0.55 * ndimage.gaussian_filter(cav, 1.5 * k))   # lifted: rays reach 4 cm
edge = np.maximum(rd("edge_geo.png", "L"), ridge)
wr(ao, os.path.join(D, "ao.png"))
wr(edge, os.path.join(D, "edge.png"))
shutil.copy(os.path.join(D, "normal_clean.png"), os.path.join(D, "detail_normal.png"))
print("cavity texels %.1f%%, ridge texels %.1f%%" % (100 * (cav > 0.2).mean(), 100 * (ridge > 0.2).mean()))
