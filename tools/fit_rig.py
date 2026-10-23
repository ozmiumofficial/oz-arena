"""Step 2: recover a skeleton + skin weights from the MD3 vertex frames.

python fit_rig.py <work dir> [--lower N] [--upper N] [--restarts 12]

Skinning decomposition (the SSDR / Dem Bones idea): find B rigid bone motions per frame
and per-vertex weights so that  x_vf ~= sum_b w_vb (R_bf p_v + t_bf)  over every frame.
Legs are fitted on lower.md3's frames and torso + arms on upper.md3's, so the rig keeps
id Tech 3's independent legs/torso animation. Joints are the points two neighboring bones
move identically; the hierarchy is the minimum-residual tree over those joints.

Without --lower/--upper the bone count is picked automatically: the smallest count whose
error is within 15% (or 1 mm) of the best count tried. Writes cage/rig_fit.npz and
rig_report.json.
"""
import sys, os, json, argparse
import numpy as np
from scipy.cluster.vq import kmeans2
from scipy.optimize import nnls

ap = argparse.ArgumentParser()
ap.add_argument("work")
ap.add_argument("--lower", type=int)
ap.add_argument("--upper", type=int)
ap.add_argument("--restarts", type=int, default=12)
ap.add_argument("--scan", default="4-13", help="bone counts tried when picking automatically")
a = ap.parse_args()
CAGE = os.path.join(a.work, "cage")
UNIT_MM = 1000 / 32.0
rng = np.random.default_rng(7)


def kabsch(P, Q, w=None):
    """P (n,3) rest, Q (F,n,3) targets, w (n,) -> R (F,3,3), t (F,3)."""
    if w is None:
        w = np.ones(len(P))
    w = w / w.sum()
    pc = w @ P
    qc = np.einsum("n,fnk->fk", w, Q)
    H = np.einsum("n,ni,fnj->fij", w, P - pc, Q - qc[:, None, :])
    U, S, Vt = np.linalg.svd(H)
    d = np.sign(np.linalg.det(np.einsum("fji,fkj->fik", Vt, U)))
    D = np.zeros_like(H)
    D[:, 0, 0] = 1; D[:, 1, 1] = 1; D[:, 2, 2] = d
    R = np.einsum("fji,fjk,flk->fil", Vt, D, U)
    t = qc - np.einsum("fij,j->fi", R, pc)
    return R, t


def apply(R, t, P):
    return np.einsum("fbij,nj->fbni", R, P) + t[:, :, None, :]


def rigid_cluster(P, X, B, iters=40):
    F, N, _ = X.shape
    feat = X.transpose(1, 0, 2).reshape(N, -1)
    _, lab = kmeans2(feat, B, minit="++", seed=rng, iter=30)
    best_err = np.zeros(N)
    for it in range(iters):
        R = np.zeros((F, B, 3, 3)); t = np.zeros((F, B, 3))
        for b in range(B):
            idx = np.where(lab == b)[0]
            if len(idx) < 3:  # reseed an empty bone at the worst-fit vertices
                idx = np.argsort(-best_err)[:3] if it else rng.choice(N, 3, replace=False)
            R[:, b], t[:, b] = kabsch(P[idx], X[:, idx])
        err = ((apply(R, t, P) - X[:, None]) ** 2).sum(-1).sum(0).T
        new = err.argmin(1)
        best_err = err.min(1)
        if (new == lab).all():
            break
        lab = new
    return lab, R, t, err


def solve_weights(P, X, R, t, err, K=4):
    F, N, _ = X.shape
    Y = apply(R, t, P)
    W = np.zeros((N, R.shape[1]))
    lam = 50.0 * np.sqrt(F)
    for v in range(N):
        cand = np.argsort(err[v])[:K]
        A = Y[:, cand, v, :].transpose(0, 2, 1).reshape(-1, len(cand))
        A2 = np.vstack([A, lam * np.ones((1, len(cand)))])
        b2 = np.concatenate([X[:, v, :].reshape(-1), [lam]])
        w, _ = nnls(A2, b2)
        w[w < 0.02] = 0
        if w.sum() == 0:
            w[0] = 1
        W[v, cand] = w / w.sum()
    return W


def update_bones(P, X, R, t, W):
    for b in range(R.shape[1]):
        idx = np.where(W[:, b] > 0.1)[0]
        if len(idx) < 3:
            continue
        Y = apply(R, t, P[idx])
        other = np.einsum("nb,fbni->fni", W[idx], Y) - W[idx, b][None, :, None] * Y[:, b]
        wb = W[idx, b]
        R[:, b], t[:, b] = kabsch(P[idx], (X[:, idx] - other) / wb[None, :, None], wb ** 2)
    return R, t


def rms_mm(P, X, R, t, W):
    d = np.linalg.norm(np.einsum("nb,fbni->fni", W, apply(R, t, P)) - X, axis=-1)
    return float(np.sqrt((d ** 2).mean()) * UNIT_MM), float(d.max() * UNIT_MM)


def fit(P, X, B):
    lab, R, t, err = rigid_cluster(P, X, B)
    rigid = rms_mm(P, X, R, t, np.eye(B)[lab])
    for _ in range(4):
        W = solve_weights(P, X, R, t, err)
        R, t = update_bones(P, X, R, t, W)
        err = ((apply(R, t, P) - X[:, None]) ** 2).sum(-1).sum(0).T
    return R, t, W, rigid, rms_mm(P, X, R, t, W)


def best_fit(P, X, B, restarts):
    best = None
    for _ in range(restarts):  # k-means seeding lands in different minima; keep the best
        r = fit(P, X, B)
        tiny = np.bincount(r[2].argmax(1), minlength=B).min()
        score = r[4][0] + (100 if tiny < min(6, len(P) // (2 * B)) else 0)
        if best is None or score < best[0]:
            best = (score, r)
    return best[1]


def joints_and_tree(P, F_tris, R, t, W, root):
    B = R.shape[1]
    dom = W.argmax(1)
    side_y = [P[dom == b, 1].mean() if (dom == b).any() else 0.0 for b in range(B)]
    adj = {}
    for tri in F_tris:
        for u, v in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
            bu, bv = dom[u], dom[v]
            if bu != bv:
                adj.setdefault((min(bu, bv), max(bu, bv)), []).append((P[u] + P[v]) / 2)
    joint, cost = {}, {}
    for (i, j), mids in adj.items():
        c = np.mean(mids, axis=0)
        A = (R[:, i] - R[:, j]).reshape(-1, 3)
        y = (t[:, j] - t[:, i]).reshape(-1)
        lam = 0.3
        p = np.linalg.lstsq(np.vstack([A, lam * np.eye(3)]), np.concatenate([y, lam * c]), rcond=None)[0]
        res = np.sqrt(((A @ p - y) ** 2).reshape(-1, 3).sum(1).mean())
        joint[(i, j)] = p
        cost[(i, j)] = res
        # a two-handed grip makes the hands look jointed to each other; never link across sides
        if abs(side_y[i]) > 4 and abs(side_y[j]) > 4 and side_y[i] * side_y[j] < 0:
            cost[(i, j)] = res * 10 + 100
    parent, head = {root: -1}, {root: None}
    while len(parent) < B:
        best = None
        for (i, j), c in cost.items():
            for u, v in ((i, j), (j, i)):
                if u in parent and v not in parent and (best is None or c < best[0]):
                    best = (c, u, v, (i, j))
        if best is None:
            v = next(k for k in range(B) if k not in parent)
            parent[v] = root
            head[v] = P[dom == v].mean(0) if (dom == v).any() else P.mean(0)
            continue
        c, u, v, key = best
        parent[v] = u
        head[v] = joint[key]
    return parent, head


def name_bones(pname, parent, cent, root):
    """Center bones by height, side bones (id Tech 3 +Y = left) by limb chain depth."""
    B = len(cent)
    names, depth_side = [""] * B, {}
    order = [root]
    while len(order) < B:
        order += [b for b in range(B) if parent.get(b) in order and b not in order]
    zs = sorted(cent[:, 2])
    for b in order:
        if b == root:
            names[b] = "pelvis" if pname == "lower" else "spine"
        elif abs(cent[b][1]) > 4 or parent[b] in depth_side:
            p = parent[b]
            if p in depth_side:
                d, s = depth_side[p][0] + 1, depth_side[p][1]
            else:
                d, s = 1, "L" if cent[b][1] > 0 else "R"
            depth_side[b] = (d, s)
            chain = ["thigh", "calf", "foot", "toe"] if pname == "lower" else ["upperarm", "forearm", "hand", "finger"]
            names[b] = "%s_%s" % (chain[min(d, len(chain)) - 1], s)
        else:
            if pname == "lower":
                names[b] = "hips"
            else:
                z = cent[b][2]
                names[b] = "neck" if z >= zs[-1] - 1e-6 or z > 18 else ("chest" if z > 13 else "belly")
    seen = {}
    for b in range(B):
        n = names[b]
        seen[n] = seen.get(n, 0) + 1
        if seen[n] > 1:
            names[b] = "%s_%d" % (n, seen[n])
    return names


lo, hi = (int(x) for x in a.scan.split("-"))
out, report = {}, {}
for pname in ("lower", "upper"):
    d = np.load(os.path.join(CAGE, pname + "_part.npz"))
    X = d["wpos"].astype(np.float64) / 64.0
    basis = int(d["basis"])
    P = X[basis]
    fixed = getattr(a, pname)
    scan = {}
    if fixed:
        B = fixed
    else:
        for B in range(lo, min(hi, len(P) // 8) + 1):
            scan[B] = best_fit(P, X, B, 3)[4][0]
            print("  %s scan B=%2d  %.2f mm" % (pname, B, scan[B]))
        best = min(scan.values())
        B = min(b for b, e in scan.items() if e <= max(best * 1.15, best + 1.0))
    R, t, W, rigid, skinned = best_fit(P, X, B, a.restarts)
    dom = W.argmax(1)
    cent = np.array([P[dom == b].mean(0) if (dom == b).any() else P.mean(0) for b in range(B)])
    tag = d["tag_torso"][basis][:3] if pname == "lower" and "tag_torso" in d else np.zeros(3)
    root = int(np.argmin(np.linalg.norm(cent - tag, axis=1)))
    parent, head = joints_and_tree(P, d["F"], R, t, W, root)
    head[root] = np.array(tag, np.float64)
    names = name_bones(pname, parent, cent, root)
    print("%s: %d bones, rigid %.1f mm, skinned %.2f mm RMS, max %.1f mm" % (pname, B, rigid[0], skinned[0], skinned[1]))
    for b in range(B):
        print("    %-12s <- %-12s verts %3d" % (names[b], names[parent[b]] if parent[b] >= 0 else "-", (dom == b).sum()))
    out.update({pname + "_R": R.astype(np.float32), pname + "_t": t.astype(np.float32),
                pname + "_W": W.astype(np.float32),
                pname + "_parent": np.array([parent[b] for b in range(B)], np.int32),
                pname + "_head": np.array([head[b] for b in range(B)], np.float32),
                pname + "_cent": cent.astype(np.float32), pname + "_names": np.array(names),
                pname + "_stats": np.array([rigid[0], skinned[0], skinned[1]], np.float32)})
    report[pname] = dict(bones=B, names=names, rms_mm=round(skinned[0], 2), max_mm=round(skinned[1], 1),
                         rigid_rms_mm=round(rigid[0], 2), scan={k: round(v, 2) for k, v in scan.items()})
np.savez(os.path.join(CAGE, "rig_fit.npz"), **out)
json.dump(report, open(os.path.join(a.work, "rig_report.json"), "w"), indent=1)
