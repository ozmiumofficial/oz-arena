"""python tint_preview.py pbr_dir out_dir r,g,b [glow]
The maps as the game draws them for one rail color: the red parts tinted (the shader's multiply stage), the trim and
cells glowing (its additive stages, at the glow share given, 0..1 of the pulse). For preview.py."""
import sys, os, shutil
import numpy as np, cv2
pd, od = sys.argv[1], sys.argv[2]
E = np.array([float(x) for x in sys.argv[3].split(",")], np.float32)[::-1]      # BGR
g = float(sys.argv[4]) if len(sys.argv) > 4 else 0.7
os.makedirs(od, exist_ok=True)
rd = lambda n: cv2.imread(os.path.join(pd, n), cv2.IMREAD_UNCHANGED).astype(np.float32) / 255
base, tint = rd("basecolor.png"), rd("tint.png")
m = tint[..., :1] if tint.ndim == 3 else tint[..., None]
cv2.imwrite(os.path.join(od, "basecolor.png"), np.clip(base * (1 - m + m * E) * 255, 0, 255).astype(np.uint8))
gt, gc = rd("glow_trim.png"), rd("glow_cell.png")
gt = gt if gt.ndim == 2 else gt[..., 0]
gc = gc if gc.ndim == 2 else gc[..., 0]
em = (gt * g + gc * (0.35 + 0.2 * g))[..., None] * E
cv2.imwrite(os.path.join(od, "emissive.png"), np.clip(em * 255, 0, 255).astype(np.uint8))
for n in ("normal.png", "roughness.png", "metallic.png", "ao.png"):
    shutil.copyfile(os.path.join(pd, n), os.path.join(od, n))
