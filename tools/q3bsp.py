"""id Tech 3 .bsp reader (IBSP version 46/47) for the map importer (build_maps.py).

read_bsp(data) -> Bsp with numpy arrays for every lump the importer uses:
  shaders, planes, models, brushes, brushsides, verts, indexes, fogs, surfaces, entities.
Helpers:
  surface_mesh(bsp, surf)   positions, normals, st, triangles of one drawn surface; patches are
                            tessellated finely (the remaster's smooth curves), planar and
                            triangle-soup surfaces are taken as stored, flares give nothing
  brush_hull(bsp, brush)    corner points of a convex brush (intersection of its side planes)
  parse_entities(text)      the entity string as a list of dicts
Positions stay in id units and axes (+X forward, +Y left, +Z up); build_maps.py converts.
Layouts follow qfiles.h in id's GPL source.
"""
import re
import itertools
import numpy as np

LUMPS = ["entities", "shaders", "planes", "nodes", "leafs", "leafsurfaces", "leafbrushes", "models",
         "brushes", "brushsides", "drawverts", "drawindexes", "fogs", "surfaces", "lightmaps",
         "lightgrid", "visibility"]

MST_BAD, MST_PLANAR, MST_PATCH, MST_TRIANGLE_SOUP, MST_FLARE = range(5)

# surfaceflags.h
CONTENTS_SOLID = 1
CONTENTS_LAVA = 8
CONTENTS_SLIME = 16
CONTENTS_WATER = 32
CONTENTS_FOG = 64
CONTENTS_PLAYERCLIP = 0x10000
CONTENTS_TRIGGER = 0x40000000
SURF_NODRAW = 0x80
SURF_SKY = 0x4

DT = {
    "shaders": np.dtype([("name", "S64"), ("surface_flags", "<i4"), ("content_flags", "<i4")]),
    "planes": np.dtype([("normal", "<f4", 3), ("dist", "<f4")]),
    "models": np.dtype([("mins", "<f4", 3), ("maxs", "<f4", 3), ("first_surface", "<i4"), ("num_surfaces", "<i4"),
                        ("first_brush", "<i4"), ("num_brushes", "<i4")]),
    "brushes": np.dtype([("first_side", "<i4"), ("num_sides", "<i4"), ("shader", "<i4")]),
    "brushsides": np.dtype([("plane", "<i4"), ("shader", "<i4")]),
    "drawverts": np.dtype([("xyz", "<f4", 3), ("st", "<f4", 2), ("lightmap", "<f4", 2), ("normal", "<f4", 3),
                           ("color", "u1", 4)]),
    "drawindexes": np.dtype("<i4"),
    "fogs": np.dtype([("shader", "S64"), ("brush", "<i4"), ("visible_side", "<i4")]),
    "surfaces": np.dtype([("shader", "<i4"), ("fog", "<i4"), ("type", "<i4"), ("first_vert", "<i4"), ("num_verts", "<i4"),
                          ("first_index", "<i4"), ("num_indexes", "<i4"), ("lm_num", "<i4"), ("lm_x", "<i4"),
                          ("lm_y", "<i4"), ("lm_w", "<i4"), ("lm_h", "<i4"), ("lm_origin", "<f4", 3),
                          ("lm_vecs", "<f4", (3, 3)), ("patch_w", "<i4"), ("patch_h", "<i4")]),
}


class Bsp:
    pass


def read_bsp(data):
    if data[:4] != b"IBSP":
        raise ValueError("not an IBSP file")
    version = int.from_bytes(data[4:8], "little")
    if version not in (46, 47):
        raise ValueError("unsupported BSP version %d" % version)
    dirs = np.frombuffer(data, "<i4", 34, 8).reshape(17, 2)
    b = Bsp()
    b.version = version
    for i, name in enumerate(LUMPS):
        off, length = int(dirs[i][0]), int(dirs[i][1])
        raw = data[off:off + length]
        if name in DT:
            setattr(b, name, np.frombuffer(raw, DT[name], length // DT[name].itemsize))
        else:
            setattr(b, name, raw)
    b.shader_names = [s.split(b"\0")[0].decode("latin-1").lower() for s in b.shaders["name"]]
    b.entity_list = parse_entities(b.entities.split(b"\0")[0].decode("latin-1"))
    return b


def parse_entities(text):
    out = []
    for block in re.findall(r"\{(.*?)\}", text, re.S):
        e = {}
        for k, v in re.findall(r'"([^"]*)"\s+"([^"]*)"', block):
            e[k] = v   # later keys win, as in the engine
        out.append(e)
    return out


# ---------------------------------------------------------------- drawn surfaces

def surface_mesh(bsp, s, max_error=0.25, max_steps=16):
    """(positions[n,3], normals[n,3], st[n,2], triangles[m,3]) in id units, or None.
    Triangles wind so that cross(b - a, c - a) follows the vertex normals (right-handed, front
    facing the normal), whatever the source order was; callers convert to their convention."""
    t = int(s["type"])
    v = bsp.drawverts[s["first_vert"]:s["first_vert"] + s["num_verts"]]
    if t in (MST_PLANAR, MST_TRIANGLE_SOUP):
        tris = bsp.drawindexes[s["first_index"]:s["first_index"] + s["num_indexes"]].reshape(-1, 3).astype(np.int64)
        pos, nrm, st = v["xyz"].astype(np.float64), v["normal"].astype(np.float64), v["st"].astype(np.float64)
    elif t == MST_PATCH:
        pos, nrm, st, tris = tessellate_patch(v, int(s["patch_w"]), int(s["patch_h"]), max_error, max_steps)
    else:
        return None
    if len(tris) == 0:
        return None
    return pos, nrm, st, orient(pos, nrm, tris)


def orient(pos, nrm, tris):
    """Flips triangles whose geometric normal disagrees with their vertex normals."""
    a, b, c = pos[tris[:, 0]], pos[tris[:, 1]], pos[tris[:, 2]]
    g = np.cross(b - a, c - a)
    n = nrm[tris[:, 0]] + nrm[tris[:, 1]] + nrm[tris[:, 2]]
    flip = (g * n).sum(1) < 0
    tris = tris.copy()
    tris[flip] = tris[flip][:, [0, 2, 1]]
    return tris


def _steps(ctrl, max_error, max_steps):
    """Segments for quadratic Bezier rows: chord error of a quadratic with n segments is
    |P0 - 2P1 + P2| / (4 n^2)."""
    d = np.linalg.norm(ctrl[..., 0, :] - 2 * ctrl[..., 1, :] + ctrl[..., 2, :], axis=-1).max() / 4.0
    if d < 1e-3:
        return 1
    return int(min(max_steps, max(2, np.ceil(np.sqrt(d / max_error)))))


def tessellate_patch(v, w, h, max_error, max_steps):
    """Biquadratic Bezier patch (w x h control points, odd sizes) -> grid mesh. One step count per
    direction for the whole patch, so its 3x3 sub-patches share their seams exactly."""
    xyz = v["xyz"].astype(np.float64).reshape(h, w, 3)
    nrm = v["normal"].astype(np.float64).reshape(h, w, 3)
    st = v["st"].astype(np.float64).reshape(h, w, 2)
    nu, nv = (w - 1) // 2, (h - 1) // 2
    if nu < 1 or nv < 1:
        return np.zeros((0, 3)), np.zeros((0, 3)), np.zeros((0, 2)), np.zeros((0, 3), np.int64)
    # rows of 3 control points along u (width) and along v (height)
    along_u = np.stack([xyz[:, 0:w - 2:2], xyz[:, 1:w - 1:2], xyz[:, 2:w:2]], axis=2)       # h, nu, 3, 3
    along_v = np.stack([xyz[0:h - 2:2], xyz[1:h - 1:2], xyz[2:h:2]], axis=2)                 # nv, w, 3, 3
    su, sv = _steps(along_u, max_error, max_steps), _steps(along_v, max_error, max_steps)
    tu = np.linspace(0, 1, su + 1)
    tv = np.linspace(0, 1, sv + 1)
    bu = np.stack([(1 - tu) ** 2, 2 * tu * (1 - tu), tu ** 2], 1)   # su+1, 3
    bv = np.stack([(1 - tv) ** 2, 2 * tv * (1 - tv), tv ** 2], 1)
    gw, gh = nu * su + 1, nv * sv + 1
    P = np.zeros((gh, gw, 3))
    N = np.zeros((gh, gw, 3))
    S = np.zeros((gh, gw, 2))
    for j in range(nv):
        for i in range(nu):
            cp = slice(2 * j, 2 * j + 3), slice(2 * i, 2 * i + 3)
            for src, dst in ((xyz, P), (nrm, N), (st, S)):
                c = src[cp]                                           # 3, 3, k
                val = np.einsum("au,bv,vuk->bak", bu, bv, c)          # sv+1, su+1, k
                dst[j * sv:j * sv + sv + 1, i * su:i * su + su + 1] = val
    # normals from the surface derivatives where they're well defined, else the blended ones
    du = np.gradient(P, axis=1)
    dv = np.gradient(P, axis=0)
    geo = np.cross(du, dv)
    ln = np.linalg.norm(geo, axis=-1, keepdims=True)
    blend = N / np.maximum(np.linalg.norm(N, axis=-1, keepdims=True), 1e-9)
    geo = np.where(ln > 1e-6, geo / np.maximum(ln, 1e-9), blend)
    geo *= np.where((geo * blend).sum(-1, keepdims=True) < 0, -1.0, 1.0)   # face the way id's normals do
    idx = np.arange(gw * gh).reshape(gh, gw)
    a, b, c, d = idx[:-1, :-1].ravel(), idx[:-1, 1:].ravel(), idx[1:, 1:].ravel(), idx[1:, :-1].ravel()
    tris = np.concatenate([np.stack([a, b, c], 1), np.stack([a, c, d], 1)])
    # drop triangles collapsed by coincident control points
    pa, pb, pc = P.reshape(-1, 3)[tris[:, 0]], P.reshape(-1, 3)[tris[:, 1]], P.reshape(-1, 3)[tris[:, 2]]
    tris = tris[np.linalg.norm(np.cross(pb - pa, pc - pa), axis=1) > 1e-6]
    return P.reshape(-1, 3), geo.reshape(-1, 3), S.reshape(-1, 2), tris


# ---------------------------------------------------------------- brushes

def brush_planes(bsp, brush):
    sides = bsp.brushsides[brush["first_side"]:brush["first_side"] + brush["num_sides"]]
    pl = bsp.planes[sides["plane"]]
    return pl["normal"].astype(np.float64), pl["dist"].astype(np.float64)


def brush_hull(bsp, brush, eps=0.01):
    """Corner points of a brush: every triple of side planes meeting at a point inside all of them."""
    n, d = brush_planes(bsp, brush)
    pts = []
    for i, j, k in itertools.combinations(range(len(n)), 3):
        m = np.array([n[i], n[j], n[k]])
        if abs(np.linalg.det(m)) < 1e-6:
            continue
        p = np.linalg.solve(m, np.array([d[i], d[j], d[k]]))
        if np.all(n @ p <= d + eps):
            pts.append(p)
    if len(pts) < 4:
        return np.zeros((0, 3))
    pts = np.unique(np.round(np.array(pts), 3), axis=0)
    return pts


def brush_contents(bsp, brush):
    return int(bsp.shaders[brush["shader"]]["content_flags"])
