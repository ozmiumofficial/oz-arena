"""After rg_bake.py: the helper masks, then the maps tools/weapon_swap/pack.py packs (adapted from rl_post.py).

    python rg_post.py <bake dir>                masks: ao.png, edge.png (from ao_geo.png, edge_geo.png) and
                                                detail_normal.png (the normal map the materials read)
    python rg_post.py <bake dir> flat <pbr dir> what ships: the game's maps as weapon_swap/pack.py reads them, one
                                                flat color per material (needs only matid, ao_geo, refcolor/refhit):
        basecolor.png   flat colors; the red parts neutral gray at their brightness (the shader tints them)
        normal.png      flat (16 bit; pack.py flips green for rend2)
        orm.png         R AO (soft, scaled down on shiny black metal), G roughness, B metalness
    python rg_post.py <bake dir> pack <pbr dir> the same files from the worn PBR bake and the detail normal map
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


def red_parts():
    """The parts that take the rail color, from the material ids: light strips, cells, the vents' glow. Returns their
    gray (what the base color stores there), how much of each texel is a red part, the tint mask and the two glows.

    The cells are the gun's energy tanks: Tripo's own pattern cast onto the rebuilt cells (refcolor), filled in where
    the cast found nothing from what it found nearby. As in the shipped Tripo railgun (weapon_swap/texture.py) their
    red takes the rail color (the tint follows the red's saturation, so pale vein nodes stay out of it) and the gray
    is the pattern's brightness, kept sharp (a light blur only, for Tripo's chart seams); their glow is 0.7 + 0.3 of
    the veins."""
    S = rd("matid.png").shape[0]
    k = S / 4096.0
    mid = rd("matid.png")
    near = lambda c: (np.abs(mid - np.array(c, np.float32)).max(2) < 0.2).astype(np.float32)
    trim, cell, vent = near((0, 0, 1)), near((1, 1, 0)), near((1, 1, 1))
    aa = lambda m: ndimage.gaussian_filter(m, 0.7 * k)
    trim_m, cell_m, vent_m = aa(trim), aa(cell), aa(vent)
    ref = rd("refcolor.png")
    hit = (rd("refhit.png", "L") > 0.5).astype(np.float32)
    cm = (cell > 0.5)
    fill = np.dstack([sblur(ref[..., i], 14 * k * CELL_DENSITY, hit) for i in range(3)])
    refc = np.where(hit[..., None] > 0.5, ref, fill)
    refc = np.dstack([ndimage.gaussian_filter(refc[..., i], 0.6 * k * CELL_DENSITY) for i in range(3)])
    V = refc.max(2)
    sat = 1 - refc.min(2) / np.maximum(V, 1e-3)              # the red ~0.85, the pale vein nodes ~0.2
    redness = np.clip((sat - 0.30) / (0.65 - 0.30), 0, 1)
    redness = redness * redness * (3 - 2 * redness)
    cell_gray = np.clip(V, 0.25, 0.95)
    cell_v = np.clip((sblur(V, 3 * k * CELL_DENSITY, cm.astype(np.float32) + 1e-3) - 0.35) / 0.55, 0, 1)
    print("cells: %.1f%% of texels, %.1f%% of them found on the reference, red %.0f%% of them, median V %.3f"
          % (100 * cell.mean(), 100 * (hit * cell).sum() / max(cell.sum(), 1),
             100 * (redness * cell).sum() / max(cell.sum(), 1), float(np.median(V[cm])) if cm.any() else 0))
    gray = cell_m * cell_gray + trim_m * 0.82 + vent_m * 0.62
    share = np.clip(cell_m + trim_m + vent_m, 0, 1)                     # stored gray in the base color
    tint = np.clip(cell_m * redness + trim_m + vent_m, 0, 1)            # what the rail color tints
    glow_cell = np.clip(cell_m * (0.7 + 0.3 * cell_v) + vent_m * 0.6, 0, 1)
    return gray, share, tint, trim_m, glow_cell


def write_red(out, base, gray, share, tint, glow_trim, glow_cell):
    base = base * (1 - share[..., None]) + gray[..., None] * share[..., None]
    wr(base, os.path.join(out, "basecolor.png"))
    wr(tint, os.path.join(out, "tint.png"))
    wr(glow_trim, os.path.join(out, "glow_trim.png"))
    wr(glow_cell, os.path.join(out, "glow_cell.png"))


def write_normal16(out, rgb):
    import cv2
    nm = (np.clip(rgb, 0, 1) * 65535 + 0.5).astype(np.uint16)
    cv2.imwrite(os.path.join(out, "normal.png"), nm[..., ::-1])


if len(sys.argv) > 2 and sys.argv[2] == "flat":
    # Tony's pick (2026-11-06, the rocket launcher's review; hard-surface-detail memory): the clean look of Blender's
    # solid view, one flat color per material with the lighting doing the shading, metal still metallic and
    # reflective, a soft AO; no wear, grime, scratches or patterned normal detail. As the rocket launcher's
    # rl_post.py flat: (sRGB base, roughness, metalness, reflection). In rend2 a metal's cubemap reflection is
    # scaled by the specular map's occlusion channel, so a lower `reflection` keeps the highlights (still shiny) and
    # lets the dark base show: the shiny black gunmetal Tony likes. The red parts are rg's usual gray + masks.
    out = sys.argv[3]
    os.makedirs(out, exist_ok=True)
    FLAT = {
        (0, 0, 0): ((0.165, 0.170, 0.180), 0.42, 0.0, 1.00),        # the body's charcoal coating
        (1, 0, 0): ((0.240, 0.252, 0.265), 0.34, 1.0, 0.32),        # gunmetal: shiny black
        (1, 0, 1): ((0.430, 0.448, 0.466), 0.27, 1.0, 1.00),        # steel: silver
        (0, 1, 0): ((0.170, 0.182, 0.195), 0.45, 0.8, 0.45),        # dark metal
        (0, 1, 1): ((0.095, 0.098, 0.105), 0.80, 0.0, 1.00),        # rubber
        (0.5, 0.5, 0.5): ((0.120, 0.124, 0.132), 0.30, 0.0, 1.00),  # carbon (the weave left out: no pattern)
        (1, 1, 0): ((0.800, 0.800, 0.800), 0.25, 0.0, 1.00),        # cells (gray replaced by their pattern)
        (0, 0, 1): ((0.820, 0.820, 0.820), 0.35, 0.0, 1.00),        # light strips
        (1, 1, 1): ((0.620, 0.620, 0.620), 0.40, 0.0, 1.00),        # the vents' glow
    }
    mid = rd("matid.png")
    keys = np.array(list(FLAT.keys()), np.float32)
    cls = np.argmin(((mid[..., None, :] - keys[None, None]) ** 2).sum(-1), axis=-1)
    vals = list(FLAT.values())
    base = np.array([v[0] for v in vals], np.float32)[cls]
    rough = np.array([v[1] for v in vals], np.float32)[cls]
    metal = np.array([v[2] for v in vals], np.float32)[cls]
    refl = np.array([v[3] for v in vals], np.float32)[cls]
    ao = np.clip(rd("ao_geo.png", "L"), 0, 1) ** 0.4 * refl
    write_red(out, base, *red_parts())
    wr(np.dstack([ao, rough, metal]), os.path.join(out, "orm.png"))
    flat_n = np.zeros(base.shape, np.float32); flat_n[..., :2] = 0.5; flat_n[..., 2] = 1.0
    write_normal16(out, flat_n)
    print("flat maps into", out)
    sys.exit(0)

if len(sys.argv) > 2 and sys.argv[2] == "pack":
    # the worn, detailed set (rg_materials.py's pbr bake and the baked detail normal), kept for comparison
    out = sys.argv[3]
    os.makedirs(out, exist_ok=True)
    write_red(out, rd("basecolor.png"), *red_parts())
    write_normal16(out, rd("detail_normal.png"))
    wr(np.dstack([rd("ao.png", "L"), rd("roughness.png", "L"), rd("metallic.png", "L")]), os.path.join(out, "orm.png"))
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
