"""Cross-sections of the reference mesh, drawn over the study renders on a measured grid.

    python sections.py ref_mesh.npz study_dir axis value [value ...]
axis y: outlines in the x-z plane (side), x: y-z (front), z: x-y (top). Writes study_dir/sec_<axis>_<value>.png."""
import sys, os
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.collections import LineCollection

def section(co, tv, axis, val):
    """Segments where the plane coord[axis] == val cuts the triangles."""
    d = co[:, axis] - val
    D = d[tv]                                    # (n,3)
    s = np.sign(D)
    cut = ~((s > 0).all(1) | (s < 0).all(1))
    T = tv[cut]; D = D[cut]
    segs = []
    for e0, e1 in ((0, 1), (1, 2), (2, 0)):
        a, b = D[:, e0], D[:, e1]
        m = (a * b) < 0
        t = np.where(m, a / np.where(m, a - b, 1), 0)
        p = co[T[:, e0]] + (co[T[:, e1]] - co[T[:, e0]]) * t[:, None]
        segs.append((m, p))
    out = []
    for i in range(len(T)):
        pts = [segs[k][1][i] for k in range(3) if segs[k][0][i]]
        if len(pts) == 2:
            out.append(pts)
    return np.array(out).reshape(-1, 2, 3)

if __name__ == "__main__":
    src, study, axis = sys.argv[1], sys.argv[2], sys.argv[3]
    vals = [float(v) for v in sys.argv[4:]]
    m = np.load(src); co, tv = m["co"], m["tv"]
    ax_i = "xyz".index(axis)
    plane = {"y": (0, 2), "x": (1, 2), "z": (0, 1)}[axis]
    for v in vals:
        S = section(co, tv, ax_i, v)[:, :, plane]
        lo = S.reshape(-1, 2).min(0); hi = S.reshape(-1, 2).max(0)
        pad = 0.01
        lo -= pad; hi += pad
        w = hi[0] - lo[0]; h = hi[1] - lo[1]
        sc = 16 / max(w, h)
        fig, ax = plt.subplots(figsize=(max(w * sc, 4), max(h * sc, 3)), dpi=110)
        ax.add_collection(LineCollection(S, colors="k", linewidths=0.8))
        ax.set_xlim(lo[0], hi[0]); ax.set_ylim(lo[1], hi[1]); ax.set_aspect("equal")
        major = 0.05 if max(w, h) > 0.3 else 0.01
        ax.set_xticks(np.arange(np.floor(lo[0] / major) * major, hi[0], major))
        ax.set_yticks(np.arange(np.floor(lo[1] / major) * major, hi[1], major))
        ax.set_xticks(np.arange(np.floor(lo[0] / 0.005) * 0.005, hi[0], 0.005), minor=True)
        ax.set_yticks(np.arange(np.floor(lo[1] / 0.005) * 0.005, hi[1], 0.005), minor=True)
        ax.grid(True, which="major", color="r", lw=0.5); ax.grid(True, which="minor", color="#f99", lw=0.2)
        ax.tick_params(labelsize=6); plt.xticks(rotation=90)
        ax.set_title("%s = %.3f" % (axis, v))
        fig.tight_layout()
        fig.savefig(os.path.join(study, "sec_%s_%+.3f.png" % (axis, v)))
        plt.close(fig)
        print(axis, v, "segments", len(S), "bounds", lo + pad, hi - pad)
