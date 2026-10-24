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


def subdivide_uv(P, F, FUV, S, Fh, UVh, levels):
    """Texture coordinates that stay where the original model had its paint. subdivide() moves the
    vertices but only interpolates uv, so the texture slid across the rounded surface (tens of
    texels on the guns). Each smoothed vertex instead takes the uv the md3 triangle it came from
    shows straight under it, averaged over the corners it shares within one uv island so the paint
    doesn't tear inside an island; seams keep a value per side.
    P: base-frame positions; F, FUV: md3 faces and uv; S, Fh, UVh: subdivide()'s output."""
    if levels == 0:
        return UVh
    Ph = np.asarray(S @ P, np.float64)
    parent = np.arange(len(Fh)) // (4 ** levels)
    A, B, C = P[F[parent, 0]], P[F[parent, 1]], P[F[parent, 2]]
    e1, e2 = B - A, C - A
    n = np.cross(e1, e2)
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
    d00, d01, d11 = (e1 * e1).sum(1), (e1 * e2).sum(1), (e2 * e2).sum(1)
    den = d00 * d11 - d01 * d01
    ok = den > 1e-9 * d00 * d11  # sliver triangles keep the interpolated uv
    den = np.where(ok, den, 1.0)
    out = UVh.astype(np.float64).copy()
    for k in range(3):
        d = Ph[Fh[:, k]] - A
        d -= (d * n).sum(1, keepdims=True) * n
        d20, d21 = (d * e1).sum(1), (d * e2).sum(1)
        v = (d11 * d20 - d01 * d21) / den
        w = (d00 * d21 - d01 * d20) / den
        uv = (1 - v - w)[:, None] * FUV[parent, 0] + v[:, None] * FUV[parent, 1] + w[:, None] * FUV[parent, 2]
        out[ok, k] = uv[ok]
    # one value per (vertex, island): corners of a vertex in one island share their linear uv
    keys = {}
    ids = np.zeros(Fh.shape, np.int64)
    for f in range(len(Fh)):
        for k in range(3):
            ids[f, k] = keys.setdefault((int(Fh[f, k]), round(float(UVh[f, k, 0]) * 1048576),
                                         round(float(UVh[f, k, 1]) * 1048576)), len(keys))
    acc = np.zeros((len(keys), 2))
    cnt = np.zeros(len(keys))
    np.add.at(acc, ids.reshape(-1), out.reshape(-1, 2))
    np.add.at(cnt, ids.reshape(-1), 1)
    return (acc / cnt[:, None])[ids]


def raster_uv(mask, st, tri):
    """Fill one triangle of texture coordinates into a coverage mask."""
    H, W = mask.shape
    pts = np.array([[st[i][0] * W, st[i][1] * H] for i in tri], np.float32)
    cv2.fillConvexPoly(mask, np.round(pts * 16).astype(np.int32), 255, cv2.LINE_8, shift=4)
