"""After rl_bake.py helpers: fold the high-detail normals' grooves into the AO and edge masks the materials use.

    python rl_post.py <bake dir>            masks: ao.png, edge.png (from rl_bake's ao_geo.png, edge_geo.png),
                                            detail_normal.png (the normal map the materials read)
    python rl_post.py <bake dir> flat <out> the flat-color set (one color per material, metal stays metallic,
                                            soft AO, flat normal): what ships now
    python rl_post.py <bake dir> pack <out> the game maps from the pbr bake: rocket_launcher_basecolor.png,
                                            rocket_launcher_normal.png, rocket_launcher_orm.png (R AO, G roughness,
                                            B metalness) in <out>

The normal map's divergence says where the surface bends: negative in grooves and seams (cavity, darkened in the AO so
grime gathers there), positive on the crests of small ridges and bolt rims (added to the edge mask so paint wears there).
"""
import sys, os, shutil
import numpy as np
from PIL import Image
from scipy import ndimage

Image.MAX_IMAGE_PIXELS = None
D = sys.argv[1]
rd = lambda n, mode="RGB": np.asarray(Image.open(os.path.join(D, n)).convert(mode)).astype(np.float32) / 255
wr = lambda a, n: Image.fromarray((np.clip(a, 0, 1) * 255 + 0.5).astype(np.uint8)).save(n)

if len(sys.argv) > 2 and sys.argv[2] == "pack":
    out = sys.argv[3]
    os.makedirs(out, exist_ok=True)
    shutil.copy(os.path.join(D, "basecolor.png"), os.path.join(out, "rocket_launcher_basecolor.png"))
    shutil.copy(os.path.join(D, "detail_normal.png"), os.path.join(out, "rocket_launcher_normal.png"))
    ao = rd("ao.png", "L"); r = rd("roughness.png", "L"); m = rd("metallic.png", "L")
    wr(np.dstack([ao, r, m]), os.path.join(out, "rocket_launcher_orm.png"))
    print("packed into", out)
    sys.exit(0)

if len(sys.argv) > 2 and sys.argv[2] == "flat":
    # Tony's pick (2026-11-06): the clean look of Blender's solid view, one flat color per material with the
    # lighting doing the shading, metal still metallic and reflective. No wear, grime, scratches or patterned normal
    # detail; a soft AO keeps the crevices' gradient. Colors are rl_materials.PAINTS' (measured on the v2-sharp maps).
    out = sys.argv[3]
    os.makedirs(out, exist_ok=True)
    # material id color: (sRGB base color, roughness, metalness, reflection). In rend2 (lightall_fp.glsl) a metal's
    # cubemap reflection is (1 - max(roughness, N.E))^3 + F0, untinted and strongest at grazing angles, times the
    # AO channel; a round barrel is mostly grazing angles, so at full strength it mirrors the map's bright cubemap and
    # reads silver. Scaling that channel down keeps the direct-light highlights (still shiny) and lets the dark base
    # show: the shiny black barrel Tony likes. Steel stays silver.
    FLAT = {
        (1, 0, 0): ((0.240, 0.252, 0.265), 0.34, 1.0, 0.32),    # gunmetal: shiny black
        (1, 0, 1): ((0.430, 0.448, 0.466), 0.27, 1.0, 1.00),    # steel: silver
        (0, 1, 0): ((0.170, 0.182, 0.195), 0.45, 0.8, 0.45),    # dark metal
        (0, 0, 1): ((0.384, 0.188, 0.224), 0.45, 0.0, 1.00),    # red paint
        (0, 1, 1): ((0.165, 0.172, 0.180), 0.80, 0.0, 1.00),    # rubber
        (1, 1, 0): ((0.090, 0.470, 0.440), 0.08, 0.0, 1.00),    # teal glass
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
    wr(base, os.path.join(out, "rocket_launcher_basecolor.png"))
    wr(np.dstack([ao, rough, metal]), os.path.join(out, "rocket_launcher_orm.png"))
    flat_n = np.zeros(base.shape, np.float32); flat_n[..., :2] = 0.5; flat_n[..., 2] = 1.0
    wr(flat_n, os.path.join(out, "rocket_launcher_normal.png"))
    print("flat maps into", out)
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
# keep only fine detail: every broad form is in the game mesh itself, so a broad tilt here can only be another part
# caught by the bake's rays (the grip's top inside the stock, say). High-pass: n - blur(n) + flat.
low = np.dstack([ndimage.gaussian_filter(nm[..., i], 6.0 * k) for i in range(3)])
nm = nm - low + np.array([0.0, 0.0, 1.0])
nm /= np.linalg.norm(nm, axis=2, keepdims=True)
Image.fromarray(((nm * 0.5 + 0.5) * 255 + 0.5).clip(0, 255).astype(np.uint8)).save(os.path.join(D, "normal_clean.png"))
dx = ndimage.sobel(nm[..., 0], axis=1) / 8.0
dy = -ndimage.sobel(nm[..., 1], axis=0) / 8.0            # +v is up the image
div = ndimage.gaussian_filter(dx + dy, 1.2 * k)
cav = np.clip(-div * 9.0, 0, 1)
ridge = np.clip(div * 7.0, 0, 1)
ao = rd("ao_geo.png", "L") ** 0.55 * (1 - 0.55 * ndimage.gaussian_filter(cav, 1.5 * k))   # lifted: rays reach 4 cm
edge = np.maximum(rd("edge_geo.png", "L"), ridge)
wr(ao, os.path.join(D, "ao.png"))
wr(edge, os.path.join(D, "edge.png"))
shutil.copy(os.path.join(D, "normal_clean.png"), os.path.join(D, "detail_normal.png"))
print("cavity texels %.1f%%, ridge texels %.1f%%" % (100 * (cav > 0.2).mean(), 100 * (ridge > 0.2).mean()))
