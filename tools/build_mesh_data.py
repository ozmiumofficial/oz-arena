"""Step 1: MD3 player model -> welded + subdivided cage data.

python build_mesh_data.py <extracted baseq3> <model> <work dir> [--skin default]

Writes into <work dir>:
  src/          the model's resolved textures (original files) + orig_<stem>.png copies
  textures.json one entry per texture: stem, size, upscale factor, which parts use it
  mesh/         mesh.json + mesh.bin: original frames/normals/uvs/tags for the viewer
  cage/         <part>_part.npz, <part>_S.npz (subdivision operator), uv_<stem>.png masks

Seam duplicates that coincide in every frame are welded; two levels of Loop subdivision
with creases on edges sharper than CREASE_DEG (and open borders) are stored as a sparse
matrix S, so every animation frame gets the same smoothing: hi = S @ cage.
"""
import os, sys, json, math, argparse, shutil
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import scipy.sparse as sp
import cv2
from PIL import Image
import q3common as q3

ap = argparse.ArgumentParser()
ap.add_argument("root")
ap.add_argument("model")
ap.add_argument("work")
ap.add_argument("--skin", default="default")
ap.add_argument("--crease", type=float, default=50.0)
ap.add_argument("--levels", type=int, default=2)
a = ap.parse_args()

MDIR = os.path.join(a.root, "models", "players", a.model)
for d in ("src", "mesh", "cage"):
    os.makedirs(os.path.join(a.work, d), exist_ok=True)


# ------------------------------------------------------------------ Loop subdivision
def loop_step(nv, F, FUV, crease):
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


# ------------------------------------------------------------------ textures
textures, tex_index = [], {}


def texture_for(skinpath, part):
    """Global texture index for a skin path; copies the image into src/."""
    full = q3.resolve_texture(a.root, skinpath)
    key = full or "missing"
    if key not in tex_index:
        stem = os.path.splitext(os.path.basename(full))[0] if full else "missing"
        while any(t["stem"] == stem for t in textures):
            stem += "_b"
        if full:
            ext = os.path.splitext(full)[1]
            shutil.copyfile(full, os.path.join(a.work, "src", stem + ext))
            img = Image.open(full).convert("RGB")
        else:
            ext = ".png"
            img = Image.new("RGB", (64, 64), (128, 128, 128))
            img.save(os.path.join(a.work, "src", stem + ext))
        img.save(os.path.join(a.work, "src", "orig_%s.png" % stem))
        w, h = img.size
        tex_index[key] = len(textures)
        # id Tech 3's draw mode (xray skins are additive, feathers and wings cut out); make_pbr keeps
        # alpha for cut-outs, rig.json carries the mode, Godot's import script applies it
        mode = q3.draw_mode(a.root, skinpath)
        textures.append(dict(stem=stem, file=stem + ext, source=skinpath, w=w, h=h,
                             up=q3.upscale_factor(w, h), parts=[], blend=mode["blend"],
                             two_sided=mode["two_sided"], alpha=mode["blend"] in ("mask", "blend")))
    t = textures[tex_index[key]]
    if part not in t["parts"]:
        t["parts"].append(part)
    return tex_index[key]


# ------------------------------------------------------------------ build parts
blobs = []
manifest = {"model": a.model, "skin": a.skin, "arrays": {}, "parts": {}}


def add(name, arr):
    arr = np.ascontiguousarray(arr)
    off = sum(len(x) for x in blobs)
    raw = arr.tobytes()
    blobs.append(raw + b"\0" * ((-len(raw)) % 4))
    manifest["arrays"][name] = dict(offset=off, length=int(arr.size), dtype=str(arr.dtype))
    return name


masks = {}


def build_part(pname, basis_frame):
    nfr, tags, surfs = q3.read_md3(os.path.join(MDIR, pname + ".md3"))
    skin = q3.read_skin(os.path.join(MDIR, "%s_%s.skin" % (pname, a.skin)))
    basis_frame = min(basis_frame, nfr - 1)
    xyz = np.concatenate([s["xyz"] for s in surfs], axis=1)
    nrm = np.concatenate([s["nrm"] for s in surfs], axis=1)
    st = np.concatenate([s["st"] for s in surfs])
    tris, mats, base = [], [], 0
    for s in surfs:
        path = skin.get(s["name"]) or (s["shaders"][0] if s["shaders"] else "")
        ti = texture_for(path, pname)
        tris.append(s["tris"] + base)
        mats += [ti] * len(s["tris"])
        base += len(s["st"])
    tris = np.concatenate(tris)[:, [0, 2, 1]]  # md3 winding is opposite its normals
    mats = np.array(mats, np.int32)

    nmd3 = xyz.shape[1]
    keys, weld = {}, np.zeros(nmd3, np.int32)
    for i in range(nmd3):
        weld[i] = keys.setdefault(xyz[:, i, :].tobytes(), len(keys))
    nw = len(keys)
    wpos = np.zeros((nfr, nw, 3), np.int16)
    wpos[:, weld, :] = xyz

    F = weld[tris]
    keep = (F[:, 0] != F[:, 1]) & (F[:, 1] != F[:, 2]) & (F[:, 2] != F[:, 0])
    F, Fmat, Ftri = F[keep], mats[keep], tris[keep]
    FUV = st[Ftri].astype(np.float64)
    crease = dihedral_creases(wpos[basis_frame].astype(np.float64), F, a.crease)
    S = sp.identity(nw, format="csr")
    Fh, UVh, Ch, Mh = F, FUV, crease, Fmat
    for _ in range(a.levels):
        Sl, Fh, UVh, Ch = loop_step(S.shape[0], Fh, UVh, Ch)
        S = Sl @ S
        Mh = np.repeat(Mh, 4)
    S = S.tocsr()
    S.eliminate_zeros()

    for ti, tri in enumerate(Ftri):
        t = textures[Fmat[ti]]
        W, H = t["w"] * t["up"], t["h"] * t["up"]
        m = masks.setdefault(t["stem"], np.zeros((H, W), np.uint8))
        pts = np.array([[st[i][0] * W, st[i][1] * H] for i in tri], np.float32)
        cv2.fillConvexPoly(m, np.round(pts * 16).astype(np.int32), 255, cv2.LINE_8, shift=4)

    p = dict(frames=nfr, md3Verts=nmd3, welded=nw, hiVerts=int(S.shape[0]), origTris=int(len(tris)),
             hiTris=int(len(Fh)), creaseEdges=len(crease), textures=sorted(set(int(x) for x in mats)))
    pre = pname + "."
    p["arrays"] = {
        "wpos": add(pre + "wpos", wpos), "weld": add(pre + "weld", weld),
        "st": add(pre + "st", st.astype(np.float32)), "nrm": add(pre + "nrm", nrm),
        "otris": add(pre + "otris", tris.astype(np.int32)), "omat": add(pre + "omat", mats),
    }
    for tname, vals in tags.items():
        p["arrays"][tname] = add(pre + tname, np.array(vals, np.float32))
    manifest["parts"][pname] = p
    sp.save_npz(os.path.join(a.work, "cage", pname + "_S.npz"), S)
    np.savez(os.path.join(a.work, "cage", pname + "_part.npz"), wpos=wpos, weld=weld, st=st, tris=tris,
             mats=mats, F=F, Fmat=Fmat, Fh=Fh, UVh=UVh, Mh=Mh,
             crease_h=np.array(sorted(Ch), np.int32).reshape(-1, 2), basis=basis_frame,
             **{k: np.array(v, np.float32) for k, v in tags.items()})
    print("%-6s frames %3d  verts %4d -> welded %4d -> %5d  tris %4d -> %5d" % (
        pname, nfr, nmd3, nw, S.shape[0], len(tris), len(Fh)))


anims, skip, extra = q3.read_cfg(os.path.join(MDIR, "animation.cfg"))
manifest["anims"] = anims
manifest["legsSkip"] = skip
build_part("lower", anims["LEGS_IDLE"]["first"] - skip)
build_part("upper", anims["TORSO_STAND"]["first"])
build_part("head", 0)
manifest["textures"] = textures

with open(os.path.join(a.work, "mesh", "mesh.bin"), "wb") as f:
    for x in blobs:
        f.write(x)
json.dump(manifest, open(os.path.join(a.work, "mesh", "mesh.json"), "w"))
json.dump(textures, open(os.path.join(a.work, "textures.json"), "w"), indent=1)
for stem, m in masks.items():
    cv2.imwrite(os.path.join(a.work, "cage", "uv_%s.png" % stem), m)
print("textures:", ", ".join("%s %dx%d (x%d, %s)" % (t["stem"], t["w"], t["h"], t["up"], "+".join(t["parts"])) for t in textures))
