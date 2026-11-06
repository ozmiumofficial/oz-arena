"""python atlas.py low.npz uv.npz res : xatlas charts + pack; writes per-corner UVs in the input triangle order"""
import sys, os, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "pylib"))
import numpy as np, xatlas
src, dst, res = sys.argv[1], sys.argv[2], int(sys.argv[3])
d = np.load(src)
v, f = d["v"], d["f"].astype(np.uint32)
t = time.time()
atlas = xatlas.Atlas()
atlas.add_mesh(v, f)
co = xatlas.ChartOptions()
co.max_iterations = 2
co.normal_deviation_weight = 2.0
co.roundness_weight = 0.02
co.straightness_weight = 6.0
co.normal_seam_weight = 4.0
co.max_cost = 2.0
po = xatlas.PackOptions()
po.resolution = res
po.padding = 6
po.bilinear = True
po.rotate_charts = True
po.bruteForce = False
atlas.generate(co, po)
vmap, idx, uvs = atlas[0]
print("charts", atlas.chart_count, "atlas", atlas.width, atlas.height, "util %.2f" % atlas.utilization, "%.1fs" % (time.time() - t))
assert len(idx) == len(f)
np.savez(dst, uv=uvs[idx].astype(np.float32))   # (tris, 3, 2)
