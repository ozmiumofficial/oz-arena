"""Mesh operations shared by the character and prop steps: seam welding, crease detection,
Loop subdivision as a sparse operator, UV coverage masks."""
import math
import numpy as np
import scipy.sparse as sp
import cv2


def weld(xyz):
    """xyz (frames, n, 3) int16 -> (weld index per md3 vertex, welded positions (frames, nw, 3)).
    Merges seam duplicates that coincide in every frame."""
    nfr, n, _ = xyz.shape
    keys, idx = {}, np.zeros(n, np.int32)
    for i in range(n):
        idx[i] = keys.setdefault(xyz[:, i, :].tobytes(), len(keys))
    wpos = np.zeros((nfr, len(keys), 3), np.int16)
    wpos[:, idx, :] = xyz
    return idx, wpos


def dihedral_creases(P, F, deg):
    fn = np.cross(P[F[:, 1]] - P[F[:, 0]], P[F[:, 2]] - P[F[:, 0]])
    fn /= np.linalg.norm(fn, axis=1, keepdims=True) + 1e-12
    edge_faces = {}
    for fi, (x, y, z) in enumerate(F):
        for u, v in ((x, y), (y, z), (z, x)):
            edge_faces.setdefault((min(u, v), max(u, v)), []).append(fi)
    cr = set()
    cosmax = math.cos(math.radians(deg))
    for e, fl in edge_faces.items():
        if len(fl) != 2 or float(fn[fl[0]] @ fn[fl[1]]) < cosmax:
            cr.add(e)
    return cr


def loop_step(nv, F, FUV, crease):
    """One level of Loop subdivision with sharp creases. Returns (S, F', FUV', crease')."""
    edge_faces = {}
    for fi, (x, y, z) in enumerate(F):
        for u, v, w in ((x, y, z), (y, z, x), (z, x, y)):
            edge_faces.setdefault((min(u, v), max(u, v)), []).append((fi, w))
    sharp = set(crease)
    for e, lst in edge_faces.items():
        if len(lst) != 2:
            sharp.add(e)
    edges = list(edge_faces.keys())
    eidx = {e: nv + i for i, e in enumerate(edges)}
    rows, cols, vals = [], [], []

    def put(r, c, v):
        rows.append(r); cols.append(c); vals.append(v)

    for e in edges:
        r = eidx[e]
        u, v = e
        if e in sharp:
            put(r, u, 0.5); put(r, v, 0.5)
        else:
            (f0, c), (f1, d) = edge_faces[e]
            put(r, u, 3 / 8); put(r, v, 3 / 8); put(r, c, 1 / 8); put(r, d, 1 / 8)
    nbrs = [set() for _ in range(nv)]
    sharp_nbrs = [[] for _ in range(nv)]
    for u, v in edges:
        nbrs[u].add(v); nbrs[v].add(u)
        if (u, v) in sharp:
            sharp_nbrs[u].append(v); sharp_nbrs[v].append(u)
    for v in range(nv):
        k, n = len(sharp_nbrs[v]), len(nbrs[v])
        if n == 0:
            put(v, v, 1.0)
        elif k == 2:
            put(v, v, 3 / 4)
            for u in sharp_nbrs[v]:
                put(v, u, 1 / 8)
        elif k > 2:
            put(v, v, 1.0)
        else:
            beta = 3 / (8 * n) if n > 3 else 3 / 16
            put(v, v, 1 - n * beta)
            for u in nbrs[v]:
                put(v, u, beta)
    S = sp.csr_matrix((vals, (rows, cols)), shape=(nv + len(edges), nv))
    F2, UV2, crease2 = [], [], set()
    for fi, (x, y, z) in enumerate(F):
        xy, yz, zx = eidx[(min(x, y), max(x, y))], eidx[(min(y, z), max(y, z))], eidx[(min(z, x), max(z, x))]
        ux, uy, uz = FUV[fi]
        uxy, uyz, uzx = (ux + uy) / 2, (uy + uz) / 2, (uz + ux) / 2
        F2 += [(x, xy, zx), (xy, y, yz), (zx, yz, z), (xy, yz, zx)]
        UV2 += [(ux, uxy, uzx), (uxy, uy, uyz), (uzx, uyz, uz), (uxy, uyz, uzx)]
    for (u, v) in sharp:
        m = eidx[(u, v)]
        crease2.add((min(u, m), max(u, m)))
        crease2.add((min(v, m), max(v, m)))
    return S, np.array(F2), np.array(UV2), crease2


def subdivide(nw, F, FUV, Fmat, crease, levels):
    """Repeated Loop subdivision. -> (S total, F hi, UV hi, material per hi face, crease hi)."""
    S = sp.identity(nw, format="csr")
    Fh, UVh, Ch, Mh = F, FUV, crease, Fmat
    for _ in range(levels):
        Sl, Fh, UVh, Ch = loop_step(S.shape[0], Fh, UVh, Ch)
        S = Sl @ S
        Mh = np.repeat(Mh, 4)
    S = S.tocsr()
    S.eliminate_zeros()
    return S, Fh, UVh, Mh, Ch


def raster_uv(mask, st, tri):
    """Fill one triangle of texture coordinates into a coverage mask."""
    H, W = mask.shape
    pts = np.array([[st[i][0] * W, st[i][1] * H] for i in tri], np.float32)
    cv2.fillConvexPoly(mask, np.round(pts * 16).astype(np.int32), 255, cv2.LINE_8, shift=4)
