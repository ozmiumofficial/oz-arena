"""python export_iqm.py corners.npz config.json out.iqm
The weapon as an IQM in id's units and axes, for the oz renderer to draw in place of id's md3 (Settings >
Experimental > Remodeled weapons: tr_model.c RE_RegisterModel loads <model>_remodel.iqm): one still frame, its
joints only the md3's tags.

- Axes: the config's "forward" and "up" (Tripo's model axes) become id's +x and +z; +y is left.
- Place: scaled by "scale", and moved so the config's "grip" point (where the palm sits, in the model's units)
  lands where it sits on id's weapons: 4.5 units behind and 3 below tag_weapon (tools/fighter_swap/q3weapons.py).
  So the hand and the fighters' hold line up as with id's gun.
- Tags: tag_weapon at the origin, as id's md3 has it, and each of the config's "tags" (model units) unrotated.
- Surfaces: welded corners split into pieces under the engine's 999 vertices a surface (SHADER_MAX_VERTEXES),
  each named <surface>_<n> with the config's "shader".
- Tangent frame: Blender's MikkTSpace tangent; id's t runs down the image, so t = 1 - v and the bitangent sign
  flips (the normal map's green is flipped to match, as Oz's other normal maps are).
"""
import sys, json, struct
import numpy as np

src, cfgp, dst = sys.argv[1], sys.argv[2], sys.argv[3]
cfg = json.load(open(cfgp))
d = np.load(src)
pos, nrm, uv, tan, sgn = d["pos"], d["nrm"], d["uv"], d["tan"], d["sgn"]
MAX_VERTS = 999
GRIP_AT = np.array([-4.5, 0.0, -3.0])
AX = {"x": 0, "y": 1, "z": 2}


def axis(s):
    v = np.zeros(3)
    v[AX[s[-1]]] = -1.0 if s[0] == "-" else 1.0
    return v


fwd, up = axis(cfg["forward"]), axis(cfg["up"])
left = np.cross(up, fwd)
R = np.stack([fwd, left, up])               # rows: id's x, y, z in model axes
assert abs(np.linalg.det(R) - 1) < 1e-6
s = float(cfg["scale"])
off = GRIP_AT - s * (R @ np.array(cfg["grip"], float))


def to_id(p):
    return (p @ R.T) * s + off


P = to_id(pos)
N = nrm @ R.T
T = tan @ R.T
W = -sgn                                   # t = 1 - v flips the bitangent
UV = np.stack([uv[:, 0], 1.0 - uv[:, 1]], 1)

# weld identical corners
key = np.concatenate([np.round(P * 4096), np.round(N * 1024), np.round(UV * 65536), np.round(T * 1024), W[:, None]], 1)
uniq, first, inv = np.unique(key, axis=0, return_index=True, return_inverse=True)
inv = inv.ravel()
vP, vN, vT, vW, vUV = P[first], N[first], T[first], W[first], UV[first]
vN /= np.linalg.norm(vN, axis=1, keepdims=True)
vT -= vN * np.sum(vN * vT, 1, keepdims=True)
vT /= np.maximum(np.linalg.norm(vT, axis=1, keepdims=True), 1e-9)
tris = inv.reshape(-1, 3)[:, ::-1]         # Blender's counter-clockwise front faces -> id's clockwise
print("verts %d tris %d" % (len(vP), len(tris)))

# pieces under MAX_VERTS, triangles in a spatially coherent order (sorted along the gun) so pieces stay compact
order = np.lexsort((vP[tris[:, 0], 2], np.floor(vP[tris[:, 0], 1] / 2), np.floor(vP[tris[:, 0], 0] / 2)))
meshes, cur, vmap = [], [], {}
for t in tris[order]:
    new = sum(1 for v in t if v not in vmap)
    if len(vmap) + new > MAX_VERTS:
        meshes.append((list(vmap), np.array(cur)))
        cur, vmap = [], {}
    cur.append([vmap.setdefault(v, len(vmap)) for v in t])
if cur:
    meshes.append((list(vmap), np.array(cur)))
print("surfaces", len(meshes))

# IQM v2
text = bytearray(b"\0")
strings = {}


def st(x):
    if x not in strings:
        strings[x] = len(text)
        text.extend(x.encode("latin-1") + b"\0")
    return strings[x]


tags = {"tag_weapon": [0.0, 0.0, 0.0]}
for k, p in cfg.get("tags", {}).items():
    tags[k] = to_id(np.array(p, float)).tolist()
mrecs, A = [], {"pos": [], "uv": [], "nrm": [], "tan": []}
nv = nt = 0
alltri = []
for i, (vs, tr) in enumerate(meshes):
    vs = np.array(vs)
    mrecs.append(struct.pack("<6I", st("%s_%d" % (cfg["surface"], i)), st(cfg["shader"]), nv, len(vs), nt, len(tr)))
    A["pos"].append(vP[vs]); A["uv"].append(vUV[vs]); A["nrm"].append(vN[vs])
    A["tan"].append(np.hstack([vT[vs], vW[vs, None]]))
    alltri.append(tr + nv)
    nv += len(vs)
    nt += len(tr)
pos_a = np.concatenate(A["pos"]).astype("<f4")
arrays = [(0, 7, 3, pos_a), (1, 7, 2, np.concatenate(A["uv"]).astype("<f4")),
          (2, 7, 3, np.concatenate(A["nrm"]).astype("<f4")), (3, 7, 4, np.concatenate(A["tan"]).astype("<f4")),
          (4, 1, 4, np.zeros((nv, 4), "u1")), (5, 1, 4, np.tile(np.array([255, 0, 0, 0], "u1"), (nv, 1)))]
tri = np.concatenate(alltri).astype("<u4")
jrecs = [struct.pack("<Ii10f", st(k), -1, *p, 0, 0, 0, 1, 1, 1, 1) for k, p in tags.items()]
# one frame, every pose constant (no channels: each joint's pose is its channel offsets), as the rocket
# launcher's remodel has it (tools/remodeled_weapons): the renderer's usual IQM path, tags from the joints
precs = [struct.pack("<iI20f", -1, 0, *p, 0, 0, 0, 1, 1, 1, 1, *([0.0] * 10)) for p in tags.values()]
lo3, hi3 = pos_a.min(0), pos_a.max(0)
brec = struct.pack("<8f", *lo3, *hi3, float(np.max(np.hypot(pos_a[:, 0], pos_a[:, 1]))),
                   float(np.max(np.linalg.norm(pos_a, axis=1))))
arec = struct.pack("<IIIfI", st("idle"), 0, 1, 15.0, 0)
comment = ("Oz Arena remodeled weapon (tools/weapon_swap): %s" % cfg["model"]).encode("latin-1") + b"\0"

out = bytearray(124)


def put(blob):
    while len(out) % 4:
        out.append(0)
    at = len(out)
    out.extend(blob)
    return at


ofs_text = put(bytes(text))
ofs_meshes = put(b"".join(mrecs))
va_at = put(bytes(20 * len(arrays)))
varecs = [struct.pack("<5I", kind, 0, fmt, size, put(arr.tobytes())) for kind, fmt, size, arr in arrays]
out[va_at:va_at + 20 * len(arrays)] = b"".join(varecs)
ofs_tris = put(tri.tobytes())
ofs_joints = put(b"".join(jrecs))
ofs_poses = put(b"".join(precs))
ofs_anims = put(arec)
ofs_frames = len(out)
ofs_bounds = put(brec)
ofs_comment = put(comment)
out[0:124] = struct.pack("<16s27I", b"INTERQUAKEMODEL\0", 2, len(out), 0,
                         len(text), ofs_text, len(mrecs), ofs_meshes,
                         len(arrays), nv, va_at, len(tri), ofs_tris, 0,
                         len(jrecs), ofs_joints, len(precs), ofs_poses, 1, ofs_anims,
                         1, 0, ofs_frames, ofs_bounds,
                         len(comment), ofs_comment, 0, 0)
open(dst, "wb").write(bytes(out))
print("wrote %s: %d bytes, bounds %s %s, tags %s" % (dst, len(out), pos_a.min(0).round(2), pos_a.max(0).round(2),
                                                     {k: np.round(v, 2).tolist() for k, v in tags.items()}))
