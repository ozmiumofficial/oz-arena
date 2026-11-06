"""Exact outlines of the reference cut by a plane, as numbers: the section's segments chained into polylines,
simplified, printed (and drawn, with a grid, if an output image is given).

    python profile.py ref_mesh.npz axis value [u0 u1 v0 v1] [--tol 0.0005] [--png out.png]
axis x: (y, z) outlines; y: (x, z); z: (x, y). Only polylines with a point inside the u/v window are kept."""
import sys
import numpy as np

args = sys.argv[1:]
tol = 0.0005
png = None
if "--tol" in args:
    i = args.index("--tol"); tol = float(args[i + 1]); del args[i:i + 2]
if "--png" in args:
    i = args.index("--png"); png = args[i + 1]; del args[i:i + 2]
src, axis, val = args[0], args[1], float(args[2])
win = list(map(float, args[3:7])) if len(args) >= 7 else None
m = np.load(src); co, tv = m["co"], m["tv"]
ai = "xyz".index(axis)
pl = {"x": (1, 2), "y": (0, 2), "z": (0, 1)}[axis]
d = co[:, ai] - val
d[np.abs(d) < 1e-9] = 1e-9
D = d[tv]
cut = ~((D > 0).all(1) | (D < 0).all(1))
T, D = tv[cut], D[cut]
segs = []
for i in range(len(T)):
    pts = []
    for a, b in ((0, 1), (1, 2), (2, 0)):
        if D[i, a] * D[i, b] < 0:
            t = D[i, a] / (D[i, a] - D[i, b])
            p = co[T[i, a]] + (co[T[i, b]] - co[T[i, a]]) * t
            pts.append(p[list(pl)])
    if len(pts) == 2:
        segs.append(pts)
segs = np.array(segs)
# chain segments by shared endpoints
key = lambda p: (round(p[0] / 2e-6), round(p[1] / 2e-6))
ends = {}
for i, (a, b) in enumerate(segs):
    ends.setdefault(key(a), []).append((i, 0)); ends.setdefault(key(b), []).append((i, 1))
used = np.zeros(len(segs), bool)
lines = []
for s0 in range(len(segs)):
    if used[s0]:
        continue
    used[s0] = True
    line = [segs[s0][0], segs[s0][1]]
    for direction in (1, 0):
        while True:
            tip = line[-1] if direction else line[0]
            nxt = None
            for (j, e) in ends.get(key(tip), []):
                if not used[j]:
                    nxt = (j, e); break
            if nxt is None:
                break
            j, e = nxt
            used[j] = True
            other = segs[j][1 - e]
            if direction:
                line.append(other)
            else:
                line.insert(0, other)
    lines.append(np.array(line))


def simplify(P, tol):
    """Douglas-Peucker."""
    if len(P) < 3:
        return P
    a, b = P[0], P[-1]
    ab = b - a
    L = np.linalg.norm(ab)
    if L < 1e-12:
        dist = np.linalg.norm(P - a, axis=1)
    else:
        q = P - a
        dist = np.abs(ab[0] * q[:, 1] - ab[1] * q[:, 0]) / L
    k = int(np.argmax(dist))
    if dist[k] > tol:
        return np.vstack([simplify(P[:k + 1], tol)[:-1], simplify(P[k:], tol)])
    return np.array([a, b])


out = []
for L in lines:
    if win is not None:
        inside = (L[:, 0] >= win[0]) & (L[:, 0] <= win[1]) & (L[:, 1] >= win[2]) & (L[:, 1] <= win[3])
        if not inside.any():
            continue
    S = simplify(L, tol)
    closed = np.linalg.norm(L[0] - L[-1]) < 1e-5
    out.append((S, closed, L))
out.sort(key=lambda t: -len(t[2]))
for S, closed, L in out:
    lo, hi = L.min(0), L.max(0)
    print("# %s %d pts  u %.4f..%.4f  v %.4f..%.4f" % ("closed" if closed else "open", len(S), lo[0], hi[0], lo[1], hi[1]))
    print(" ".join("(%.4f, %.4f)," % (p[0], p[1]) for p in S))
if png:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    allp = np.vstack([L for _, _, L in out]) if out else np.zeros((1, 2))
    lo = np.array(win[0::2]) if win else allp.min(0) - 0.005
    hi = np.array(win[1::2]) if win else allp.max(0) + 0.005
    w, h = hi - lo
    sc = 14 / max(w, h)
    fig, ax = plt.subplots(figsize=(max(w * sc, 4), max(h * sc, 3)), dpi=110)
    for S, closed, L in out:
        ax.plot(L[:, 0], L[:, 1], "k-", lw=0.7)
        ax.plot(S[:, 0], S[:, 1], "o", ms=2.5, color="tab:blue")
    ax.set_xlim(lo[0], hi[0]); ax.set_ylim(lo[1], hi[1]); ax.set_aspect("equal")
    ax.set_xticks(np.arange(np.ceil(lo[0] / 0.01) * 0.01, hi[0], 0.01))
    ax.set_yticks(np.arange(np.ceil(lo[1] / 0.01) * 0.01, hi[1], 0.01))
    ax.set_xticks(np.arange(np.ceil(lo[0] / 0.002) * 0.002, hi[0], 0.002), minor=True)
    ax.set_yticks(np.arange(np.ceil(lo[1] / 0.002) * 0.002, hi[1], 0.002), minor=True)
    ax.grid(True, which="major", color="r", lw=0.5); ax.grid(True, which="minor", color="#fbb", lw=0.2)
    ax.tick_params(labelsize=6); plt.xticks(rotation=90)
    ax.set_title("%s = %.4f" % (axis, val))
    fig.tight_layout(); fig.savefig(png); plt.close(fig)
