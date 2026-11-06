"""After rl_bake.py helpers: fold the high-detail normals' grooves into the AO and edge masks the materials use.

    python rl_post.py <bake dir>            masks: ao.png, edge.png (from rl_bake's ao_geo.png, edge_geo.png),
                                            detail_normal.png (the normal map the materials read)
    python rl_post.py <bake dir> pack <out> the game's maps from the pbr bake: rocket_launcher_basecolor.png,
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
