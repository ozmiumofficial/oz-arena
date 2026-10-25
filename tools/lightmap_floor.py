"""Brighten a baked map after the bake: a gain, and a minimum light (q3map2's worldspawn _minlight).

python tools/lightmap_floor.py <map> [<map> ...] [--floor 0.1] [--gain 1.5]

q3map2 strips light entities from the BSP unless run with -keeplights, so a community map it
compiled comes to the remaster with only its shaders' surface lights and the sun; the rooms its
mapper lit with light entities bake nearly black (Achromatic, Rustgrad). The mapper's own fix
for dark corners in q3map2 is _minlight, a floor under every lightmap texel. This does the same
to the baked lightmap's L0: L0 = max(L0 x gain, floor), both from tools/map_lighting.json
"_bake" <map> "gain" / "floor" (linear light; the stock maps' bakes have a median L0 of about
0.13). The gains match the community maps to how bright Q3ClassicMap draws the same views
(2026-10-25, Ozmium: the remastered versions were darker than the classic ones).

godot/library/maps/<map>/<map>.exr is rewritten; the bake as Godot saved it is kept as
work/_lightmap_floor/<map>.exr and used as the source on every run, until a new bake replaces
it. Reimport the .exr in the editor afterwards (a scan, or reimport_files).
"""
import os, sys, json, shutil, hashlib, argparse
os.environ.setdefault("OPENCV_IO_ENABLE_OPENEXR", "1")
import numpy as np
import cv2

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ap = argparse.ArgumentParser()
ap.add_argument("maps", nargs="+")
ap.add_argument("--floor", type=float, help="override map_lighting.json")
ap.add_argument("--gain", type=float, help="override map_lighting.json")
ap.add_argument("--godot", default=os.path.join(ROOT, "godot"))
a = ap.parse_args()
KEEP = os.path.join(ROOT, "work", "_lightmap_floor")
os.makedirs(KEEP, exist_ok=True)
bake_cfg = json.load(open(os.path.join(HERE, "map_lighting.json"))).get("_bake", {})


def md5(p):
    return hashlib.md5(open(p, "rb").read()).hexdigest()


for m in a.maps:
    floor = a.floor if a.floor is not None else float(bake_cfg.get(m, {}).get("floor", 0.0))
    gain = a.gain if a.gain is not None else float(bake_cfg.get(m, {}).get("gain", 1.0))
    exr = os.path.join(a.godot, "library", "maps", m, m + ".exr")
    src = os.path.join(KEEP, m + ".exr")
    stamp = os.path.join(KEEP, m + ".md5")
    # the source is the pure bake: a fresh bake (the .exr isn't what this tool last wrote) replaces it
    if not os.path.exists(src) or not os.path.exists(stamp) or open(stamp).read() != md5(exr):
        shutil.copy2(exr, src)
    img = cv2.imread(src, cv2.IMREAD_UNCHANGED).astype(np.float32)
    imp = open(exr + ".import").read()
    layers = int(next(l.split("=")[1] for l in imp.splitlines() if l.startswith("slices/vertical=")))
    lh = img.shape[0] // layers
    # the maps bake directional (Q3MapBuilder): L0 and then L1 x, y, z per atlas slice
    l0 = range(0, layers, 4)
    before = np.concatenate([img[i * lh:(i + 1) * lh] for i in l0]).mean(2)
    for i in l0:
        img[i * lh:(i + 1) * lh] = np.maximum(img[i * lh:(i + 1) * lh] * gain, floor)
    after = np.concatenate([img[i * lh:(i + 1) * lh] for i in l0]).mean(2)
    cv2.imwrite(exr, img, [cv2.IMWRITE_EXR_TYPE, cv2.IMWRITE_EXR_TYPE_FLOAT])
    open(stamp, "w").write(md5(exr))
    print("%-14s gain %.2f, floor %.3f: L0 median %.4f -> %.4f" % (m, gain, floor, float(np.median(before)), float(np.median(after))))
