"""The fighters: the Godot build's 29 rigged models -> IQM rigs beside id's player md3s (ENGINE_PLAN.md P2).

python tools/make_fighters.py [--models sarge ...] [--report]

Reads the Godot build's godot/library/characters (read only: <m>.glb and <m>.rig.json), the fighters' texture
maps (--maps-library: tools/make_textures.py's build/oz_library/characters, made from the player's own id
images, or the Godot build's until that's made) and id's paks (the md3s' frames and tags, animation.cfg, the skins and models.shader). Writes
build/engine/oz/pak_oz_fighters.pk3:

- models/players/<m>/oz_rig.iqm: the rigged mesh in id's units and axes, split by md3 part and surface into
  pieces under the engine's 1,000 vertices a surface. Each piece is named after its md3 surface, so the cgame's
  .skin files pick its shader. Two animations, "lower" and "upper", hold one frame for each frame of lower.md3
  and upper.md3, sampled from the Godot build's clip for that frame (animation.cfg's ranges), so a cgame's md3
  frame numbers pick the same pose. Three extra joints, tag_torso, tag_head and tag_weapon, sit where id's md3
  tags are (a constant offset from their bone, fitted over every frame; the fit is printed). A facet the Godot
  build's creases cut out of one surface is shaded with that surface, as id's md3 shades it (island_normals).
- models/players/<m>/<image>.dds, _n.dds, _s.dds: the remastered maps beside id's image paths, as for the world.
- scripts/oz_fighters.ozmtr: id's player shaders with the remastered images.

The oz renderer swaps a cgame's lower/upper/head.md3 for the rig beside them (tr_oz_rig.c). The IQM's comment
tells it each mesh's part and each joint's part (l, u or h), where the "upper" frames start, which frames are
deaths and corpses, and the ragdoll (tr_oz_ragdoll.c, on Jolt): its bodies, capsules fitted to the skin and
joined by cones and hinges as the Godot build planned them, and the pairs that never collide. (The particles
and links before them are still written, for engines older than the bodies.)
"""
import os, re, sys, json, math, struct, zipfile, argparse
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import make_pk3s, q3shader
from q3paks import find_baseq3, Paks

UNIT = 32.0           # id units per meter (the Godot build's Q3.UNIT)
FEET = -24.0          # id's player origin is 24 units above the feet
# Godot's axes (x left, y up, z forward, as the Godot build placed id's models) -> id's (x forward, y left, z up)
AXES = np.array([[0.0, 0.0, 1.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]])
MAX_VERTS = 999       # SHADER_MAX_VERTEXES - 1, the engine's limit for one surface
RIG = "oz_rig.iqm"
# animation.cfg's lines, in order (the cgame's animNumber_t)
ANIMS = ["BOTH_DEATH1", "BOTH_DEAD1", "BOTH_DEATH2", "BOTH_DEAD2", "BOTH_DEATH3", "BOTH_DEAD3",
         "TORSO_GESTURE", "TORSO_ATTACK", "TORSO_ATTACK2", "TORSO_DROP", "TORSO_RAISE", "TORSO_STAND", "TORSO_STAND2",
         "LEGS_WALKCR", "LEGS_WALK", "LEGS_RUN", "LEGS_BACK", "LEGS_SWIM", "LEGS_JUMP", "LEGS_LAND", "LEGS_JUMPB",
         "LEGS_LANDB", "LEGS_IDLE", "LEGS_IDLECR", "LEGS_TURN"]

ap = argparse.ArgumentParser()
ap.add_argument("--library", default=os.path.join(make_pk3s.GODOT, "godot", "library", "characters"))
ap.add_argument("--maps-library", default=make_pk3s.library("characters"), help="the texture maps")
ap.add_argument("--baseq3", help="the player's baseq3 (default: found in the Steam libraries)")
ap.add_argument("--out", default=os.path.join(ROOT, "build", "engine", "oz"))
ap.add_argument("--work", default=os.path.join(ROOT, "build", "oz_assets"))
ap.add_argument("--models", nargs="*", help="only these fighters (default: all 29)")
ap.add_argument("--jobs", type=int, default=4, help="encoder processes (4 is safe on 16 GB)")
ap.add_argument("--max-size", type=int, default=1024, help="largest texture side")
ap.add_argument("--report", action="store_true", help="export the rigs and list the textures, convert none")
a = ap.parse_args()
make_pk3s.a.max_size = a.max_size
STAGE = os.path.join(a.work, "fighters")


# ------------------------------------------------------------------ math

def quat_matrix(q):
    x, y, z, w = np.asarray(q, float) / np.linalg.norm(q)
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def matrix_quat(m):
    """An orthonormal 3x3 -> (x, y, z, w) with w >= 0."""
    t = m[0, 0] + m[1, 1] + m[2, 2]
    if t > 0:
        s = math.sqrt(t + 1.0) * 2
        q = [(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, 0.25 * s]
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = math.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        q = [0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s, (m[2, 1] - m[1, 2]) / s]
    elif m[1, 1] > m[2, 2]:
        s = math.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        q = [(m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s, (m[0, 2] - m[2, 0]) / s]
    else:
        s = math.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        q = [(m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s, (m[1, 0] - m[0, 1]) / s]
    q = np.array(q) / np.linalg.norm(q)
    return -q if q[3] < 0 else q


def trs(t, r, s):
    m = np.eye(4)
    m[:3, :3] = quat_matrix(r) * np.asarray(s, float)
    m[:3, 3] = t
    return m


def decompose(m):
    s = np.linalg.norm(m[:3, :3], axis=0)
    return m[:3, 3].copy(), matrix_quat(m[:3, :3] / s), s


def orthonormal(m):
    u, _, vt = np.linalg.svd(m)
    return u @ vt


def mean_rigid(mats):
    """The average of rigid transforms (mean origin, sign-aligned mean quaternion)."""
    qs = [matrix_quat(orthonormal(m[:3, :3])) for m in mats]
    q = sum(x if np.dot(x, qs[0]) >= 0 else -x for x in qs)
    out = np.eye(4)
    out[:3, :3] = quat_matrix(q)
    out[:3, 3] = np.mean([m[:3, 3] for m in mats], axis=0)
    return out


S = np.eye(4)
S[:3, :3] = UNIT * AXES
S[:3, 3] = (0.0, 0.0, FEET)
S_FRAME_INV = np.eye(4)
S_FRAME_INV[:3, :3] = np.linalg.inv(UNIT * AXES)


def to_id(w):
    """A joint's frame in Godot's space -> in id's: still rigid, its origin at the joint (S of Godot's origin).
    Two frames' relative motion is the same as in Godot's space, so skinning with them matches."""
    return S @ w @ S_FRAME_INV


# ------------------------------------------------------------------ glTF

class Glb:
    def __init__(self, path):
        b = open(path, "rb").read()
        off, self.g, self.bin = 12, None, None
        while off < len(b):
            n, kind = struct.unpack_from("<II", b, off)
            if kind == 0x4E4F534A:
                self.g = json.loads(b[off + 8:off + 8 + n])
            elif kind == 0x004E4942:
                self.bin = b[off + 8:off + 8 + n]
            off += 8 + n
        self.nodes = self.g["nodes"]
        self.parent = {c: i for i, n in enumerate(self.nodes) for c in n.get("children", [])}

    def acc(self, i):
        ac = self.g["accessors"][i]
        bv = self.g["bufferViews"][ac["bufferView"]]
        dt = np.dtype({5120: "i1", 5121: "u1", 5122: "<i2", 5123: "<u2", 5125: "<u4", 5126: "<f4"}[ac["componentType"]])
        k = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[ac["type"]]
        start = bv.get("byteOffset", 0) + ac.get("byteOffset", 0)
        stride = bv.get("byteStride", dt.itemsize * k)
        n = ac["count"]
        raw = np.frombuffer(self.bin, np.uint8, stride * (n - 1) + dt.itemsize * k, start)
        out = np.lib.stride_tricks.as_strided(raw, (n, dt.itemsize * k), (stride, 1)).copy().view(dt).reshape(n, k)
        if ac.get("normalized"):
            out = out.astype(float) / np.iinfo(dt).max
        return out

    def local(self, i, ch=None, t=0.0):
        n = self.nodes[i]
        if ch is None or not any((i, p) in ch for p in ("translation", "rotation", "scale")):
            if "matrix" in n:
                return np.array(n["matrix"], float).reshape(4, 4).T
            return trs(n.get("translation", [0, 0, 0]), n.get("rotation", [0, 0, 0, 1]), n.get("scale", [1, 1, 1]))
        v = {}
        for p, d in (("translation", [0, 0, 0]), ("rotation", [0, 0, 0, 1]), ("scale", [1, 1, 1])):
            v[p] = sample(*ch[(i, p)], t, p == "rotation") if (i, p) in ch else np.array(n.get(p, d), float)
        return trs(v["translation"], v["rotation"], v["scale"])

    def worlds(self, ch=None, t=0.0):
        out = [None] * len(self.nodes)

        def get(i):
            if out[i] is None:
                loc = self.local(i, ch, t)
                out[i] = get(self.parent[i]) @ loc if i in self.parent else loc
            return out[i]
        for i in range(len(self.nodes)):
            get(i)
        return out

    def clip(self, name):
        an = next((x for x in self.g["animations"] if x["name"] == name), None)
        if an is None:
            return None
        ch, t0 = {}, None
        for c in an["channels"]:
            smp = an["samplers"][c["sampler"]]
            interp = smp.get("interpolation", "LINEAR")
            if interp == "CUBICSPLINE":
                raise ValueError("cubic spline keys in %s" % name)
            times = self.acc(smp["input"])[:, 0].astype(float)
            ch[(c["target"]["node"], c["target"]["path"])] = (times, self.acc(smp["output"]).astype(float), interp)
            t0 = times[0] if t0 is None else min(t0, times[0])
        return ch, t0 or 0.0


def sample(times, vals, interp, t, rot):
    if len(times) == 1 or t <= times[0]:
        return vals[0]
    if t >= times[-1]:
        return vals[-1]
    i = int(np.searchsorted(times, t, side="right")) - 1
    if interp == "STEP":
        return vals[i]
    f = (t - times[i]) / (times[i + 1] - times[i])
    x, y = vals[i], vals[i + 1]
    if rot:
        if np.dot(x, y) < 0:
            y = -y
        q = x * (1 - f) + y * f      # between 60 fps keys
        return q / np.linalg.norm(q)
    return x * (1 - f) + y * f


# ------------------------------------------------------------------ id's files

def read_md3(data):
    """-> (number of frames, {tag name: [4x4 per frame]}, [(surface name, default shader)])"""
    _, _, _, _, nf, nt, ns, _, _, ot, osf, _ = struct.unpack_from("<4si64siiiiiiiii", data, 0)
    tags = {}
    for f in range(nf):
        for i in range(nt):
            o = ot + (f * nt + i) * 112
            name = data[o:o + 64].split(b"\0")[0].decode("latin-1").lower()
            v = struct.unpack_from("<12f", data, o + 64)
            m = np.eye(4)
            m[:3, 3] = v[0:3]
            m[:3, :3] = np.array(v[3:12]).reshape(3, 3).T      # axis[k] is column k
            tags.setdefault(name, []).append(m)
    surfs, o = [], osf
    for i in range(ns):
        name = data[o + 4:o + 68].split(b"\0")[0].decode("latin-1").lower()
        _, _, nsh, _, _, _, osh, _, _, oend = struct.unpack_from("<10i", data, o + 68)
        sh = data[o + osh:o + osh + 64].split(b"\0")[0].decode("latin-1").lower() if nsh else ""
        surfs.append((name, sh))
        o += oend
    return nf, tags, surfs


def read_skin(text):
    out = []
    for line in text.splitlines():
        line = line.strip()
        if "," in line and not line.lower().startswith("tag_"):
            s, t = line.split(",", 1)
            out.append((s.strip().lower(), t.strip().lower().replace("\\", "/")))
    return out


def read_cfg(text):
    """animation.cfg -> {anim name: (first frame, frames, fps)} (the cgame's parse: lines of 4 numbers, in order)."""
    nums = []
    for line in text.splitlines():
        line = line.split("//")[0].split()
        if len(line) >= 4 and all(re.fullmatch(r"-?\d+", x) for x in line[:4]):
            nums.append([int(x) for x in line[:4]])
    anims = {}
    for name, (first, n, _, fps) in zip(ANIMS, nums):
        anims[name] = (first, abs(n), fps if fps > 0 else 1)
    return anims


def frame_sources(anims, part, count):
    """Each frame of lower.md3 or upper.md3 -> (clip name, seconds into the clip), the way the cgame numbers them
    (legs frames lose the torso-only frames: cg_players.c's skip). Frames no animation uses (some of id's
    animation.cfg files leave gaps) repeat the one before, marked unused."""
    skip = anims["LEGS_WALKCR"][0] - anims["TORSO_GESTURE"][0]
    out = [None] * count
    for name, (first, n, fps) in anims.items():
        kind = name.split("_")[0]
        if kind == "BOTH" or (kind == "TORSO" and part == "upper") or (kind == "LEGS" and part == "lower"):
            f0 = first - (skip if kind == "LEGS" else 0)
            for k in range(n):
                if 0 <= f0 + k < count:
                    out[f0 + k] = (name, k / fps, True)
    last = next(x for x in out if x)
    for i, x in enumerate(out):
        out[i] = last = x or (last[0], last[1], False)
    return out


# ------------------------------------------------------------------ the rig

def tangents(pos, nrm, uv, tris):
    """Per-vertex tangents the way rend2 makes them (Lengyel: R_CalcTexDirs, R_CalcTangentSpace)."""
    v1, v2, v3 = pos[tris[:, 0]], pos[tris[:, 1]], pos[tris[:, 2]]
    w1, w2, w3 = uv[tris[:, 0]], uv[tris[:, 1]], uv[tris[:, 2]]
    e1, e2 = v2 - v1, v3 - v1
    s1, s2 = w2[:, 0] - w1[:, 0], w3[:, 0] - w1[:, 0]
    t1, t2 = w2[:, 1] - w1[:, 1], w3[:, 1] - w1[:, 1]
    r = s1 * t2 - s2 * t1
    r = np.where(r != 0, 1.0 / np.where(r != 0, r, 1), 0.0)[:, None]
    sdir = (t2[:, None] * e1 - t1[:, None] * e2) * r
    tdir = (s1[:, None] * e2 - s2[:, None] * e1) * r
    sacc, tacc = np.zeros_like(pos), np.zeros_like(pos)
    for k in range(3):
        np.add.at(sacc, tris[:, k], sdir)
        np.add.at(tacc, tris[:, k], tdir)
    tan = sacc - nrm * np.sum(nrm * sacc, axis=1, keepdims=True)
    ln = np.linalg.norm(tan, axis=1, keepdims=True)
    fallback = np.cross(nrm, np.where(np.abs(nrm[:, 2:3]) < 0.9, [[0, 0, 1.0]], [[1.0, 0, 0]]))
    tan = np.where(ln > 1e-8, tan / np.maximum(ln, 1e-12), fallback / np.linalg.norm(fallback, axis=1, keepdims=True))
    hand = np.where(np.sum(np.cross(nrm, sacc) * tacc, axis=1) < 0, -1.0, 1.0)
    return np.hstack([tan, hand[:, None]])


ISLAND_TRIS = 64      # the largest island: four of id's triangles after the Godot build's two Loop steps


def island_normals(prims):
    """The Godot build split its normals at every edge sharper than 50 degrees. Where those creases close around a
    facet of a few of id's triangles, the facet is shaded on its own: Sarge's brow and many noses' undersides
    face down, so they drew as dark cuts. id's md3s shade such a facet with the surface around it, and so does
    the rig: an island (a smoothing group whose creases all lead to one larger group, with no open edge) takes
    that surface's normals along its edge, relaxed across its inside. Changes the prims' normals; -> islands."""
    offs = np.cumsum([0] + [len(p["pos"]) for p in prims])
    pos = np.concatenate([p["pos"] for p in prims])
    nrm = np.concatenate([p["nrm"] for p in prims])
    tris = np.concatenate([p["tris"] + o for p, o in zip(prims, offs)])
    wid = np.unique(np.round(pos, 3), axis=0, return_inverse=True)[1].ravel()     # the copies of one position
    edges = {}
    for f, t in enumerate(tris):
        for k in range(3):
            a, b = t[k], t[(k + 1) % 3]
            edges.setdefault((min(wid[a], wid[b]), max(wid[a], wid[b])), []).append((f, a, b))
    group = np.arange(len(tris))

    def find(f):
        while group[f] != f:
            group[f] = group[group[f]]
            f = group[f]
        return f
    creases, opened = [], set()
    for fl in edges.values():
        if len(fl) != 2:
            opened.update(f for f, _, _ in fl)
            continue
        (f1, a1, b1), (f2, a2, b2) = fl
        c1, c2 = {wid[a1]: a1, wid[b1]: b1}, {wid[a2]: a2, wid[b2]: b2}
        if all(nrm[c1[w]] @ nrm[c2[w]] > 0.999 for w in c1):
            group[find(f1)] = find(f2)
        else:
            creases.append((f1, f2))
    group = np.array([find(f) for f in range(len(tris))])
    size = np.bincount(group, minlength=len(tris))
    nbrs = {}
    for f1, f2 in creases:
        if group[f1] != group[f2]:
            nbrs.setdefault(group[f1], set()).add(group[f2])
            nbrs.setdefault(group[f2], set()).add(group[f1])
    opened = {group[f] for f in opened}
    out, count = nrm.copy(), 0
    for g, nb in nbrs.items():
        (q,) = nb if len(nb) == 1 else (None,)
        if q is None or g in opened or size[g] > ISLAND_TRIS or size[q] <= size[g]:
            continue
        mine = np.unique(tris[group == g])
        around = np.unique(tris[group == q])
        edge = {}
        for v in around[np.isin(wid[around], wid[mine])]:
            edge[wid[v]] = edge.get(wid[v], 0) + nrm[v]
        val = {w: n / np.linalg.norm(n) for w, n in edge.items()}
        inside = [w for w in set(wid[mine]) if w not in val]
        adj = {w: set() for w in inside}
        for t in wid[tris[group == g]]:
            for k in range(3):
                for a, b in ((t[k], t[(k + 1) % 3]), (t[(k + 1) % 3], t[k])):
                    if a in adj:
                        adj[a].add(b)
        start = sum(val.values())
        for w in inside:
            val[w] = start / np.linalg.norm(start)
        for _ in range(100):
            for w in inside:
                n = sum(val[x] for x in adj[w])
                val[w] = n / max(np.linalg.norm(n), 1e-12)
        out[mine] = [val[w] for w in wid[mine]]
        count += 1
    for p, o in zip(prims, offs):
        p["nrm"] = out[o:o + len(p["pos"])]
    return count


def build_rig(m, paks, lib_dir):
    """One fighter -> (IQM bytes, report dict)."""
    glb = Glb(os.path.join(lib_dir, m + ".glb"))
    rig = json.load(open(os.path.join(lib_dir, m + ".rig.json")))
    g = glb.g
    half = {b["name"]: b.get("half") for b in rig["bones"]}

    # joints, parents first
    skin = g["skins"][0]
    jnodes = skin["joints"]
    jset = set(jnodes)

    def jparent(n):
        p = glb.parent.get(n)
        while p is not None and p not in jset:
            p = glb.parent.get(p)
        return p
    order = []

    def visit(n):
        order.append(n)
        for c in glb.nodes[n].get("children", []):
            if c in jset:
                visit(c)
    for n in jnodes:
        if jparent(n) is None:
            visit(n)
    names = [glb.nodes[n]["name"] for n in order]
    index = {n: i for i, n in enumerate(order)}
    parents = [index[jparent(n)] if jparent(n) is not None else -1 for n in order]
    skin_to_iqm = np.array([index[n] for n in jnodes])
    ibm = glb.acc(skin["inverseBindMatrices"]).reshape(-1, 4, 4).transpose(0, 2, 1)
    bind = np.zeros((len(order), 4, 4))
    for si, n in enumerate(jnodes):
        bind[index[n]] = to_id(np.linalg.inv(ibm[si]))

    # parts: h = the head bone and below, u = the rest of the torso half, l = legs and root
    head = names.index("head")
    torso_root = next(i for i, nm in enumerate(names) if half.get(nm) == "torso" and parents[i] >= 0
                      and half.get(names[parents[i]]) != "torso")
    weapon = names.index("weapon")
    jpart = []
    for i, nm in enumerate(names):
        p, under_head = i, False
        while p >= 0:
            under_head |= p == head
            p = parents[p]
        jpart.append("h" if under_head else "u" if half.get(nm) == "torso" else "l")

    # the mesh: every primitive of the skinned mesh node, in id's space
    mnode = next(n for n in glb.nodes if "mesh" in n and "skin" in n)
    prims = []
    for pr in g["meshes"][mnode["mesh"]]["primitives"]:
        at = pr["attributes"]
        pos = glb.acc(at["POSITION"]).astype(float)
        pos = (S @ np.hstack([pos, np.ones((len(pos), 1))]).T).T[:, :3]
        nrm = (AXES @ glb.acc(at["NORMAL"]).astype(float).T).T
        nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-12)
        uv = glb.acc(at["TEXCOORD_0"]).astype(float)
        jw = glb.acc(at["WEIGHTS_0"]).astype(float)
        ji = skin_to_iqm[glb.acc(at["JOINTS_0"]).astype(int)]
        tris = glb.acc(pr["indices"]).astype(int).reshape(-1, 3)[:, ::-1]   # glTF's counter-clockwise -> id's
        stem = g["materials"][pr["material"]]["name"].lower()
        prims.append(dict(pos=pos, nrm=nrm, uv=uv, jw=jw, ji=ji, tris=tris, stem=stem))
    islands = island_normals(prims)
    for pr in prims:
        pr["tan"] = tangents(pr["pos"], pr["nrm"], pr["uv"], pr["tris"])

    # id's md3s: frame counts, tags, surface names and default skins
    base = "models/players/%s/" % m
    md3 = {p: read_md3(paks.read(base + p + ".md3")) for p in ("lower", "upper", "head")}
    anims = read_cfg(paks.read(base + "animation.cfg").decode("latin-1"))
    surf_of = {}      # (part letter, texture stem) -> md3 surface name (the first that default skin gives it)
    for p in ("lower", "upper", "head"):
        dflt = dict(read_skin(paks.read(base + p + "_default.skin").decode("latin-1"))) \
            if base + p + "_default.skin" in paks.files else {}
        for sname, sh in md3[p][2]:
            tex = dflt.get(sname, sh)
            if tex:
                surf_of.setdefault((p[0], os.path.splitext(os.path.basename(tex))[0]), sname)

    # frames: every frame of lower.md3, then of upper.md3
    nl, nu = md3["lower"][0], md3["upper"][0]
    sources = frame_sources(anims, "lower", nl) + frame_sources(anims, "upper", nu)
    clips, frames = {}, []
    for name, t, _ in sources:
        if name not in clips:
            clips[name] = glb.clip(name)
            if clips[name] is None:
                raise ValueError("%s has no clip %s" % (m, name))
        ch, t0 = clips[name]
        w = glb.worlds(ch, t0 + t)
        frames.append(np.array([to_id(w[n]) for n in order]))
    frames = np.array(frames)             # (frame, joint, 4, 4): joints in model space

    # tags: constant offsets from their bones, fitted to id's tags over every frame
    def fit(bone, tag_frames):
        return mean_rigid([np.linalg.inv(frames[f, bone]) @ tm for f, tm in tag_frames])
    tt = md3["lower"][1]["tag_torso"]
    off_torso = fit(parents[torso_root], [(f, tt[f]) for f in range(nl)])
    torso_at = lambda f: frames[f, parents[torso_root]] @ off_torso       # tag_torso in model space
    up = md3["upper"][1]
    off_head = fit(head, [(nl + f, torso_at(nl + f) @ up["tag_head"][f]) for f in range(nu)])
    off_weapon = fit(weapon, [(nl + f, torso_at(nl + f) @ up["tag_weapon"][f]) for f in range(nu)])

    def err(rig_at, md3_at, first, count):
        """-> (largest distance in units, largest angle in degrees, the clip of the worst frame)"""
        worst = (0.0, 0.0, "")
        for f in range(count):
            if not sources[first + f][2]:
                continue
            r, q = rig_at(f), md3_at(f)
            d = float(np.linalg.norm(r[:3, 3] - q[:3, 3]))
            c = np.clip((np.trace(orthonormal(r[:3, :3]).T @ orthonormal(q[:3, :3])) - 1) / 2, -1, 1)
            ang = math.degrees(math.acos(c))
            if d + ang / 10 > worst[0] + worst[1] / 10:
                worst = (round(d, 2), round(ang, 1), sources[first + f][0])
        return worst
    fit_report = {
        "tag_torso": err(torso_at, lambda f: tt[f], 0, nl),
        "tag_head": err(lambda f: np.linalg.inv(torso_at(nl + f)) @ frames[nl + f, head] @ off_head,
                        lambda f: up["tag_head"][f], nl, nu),
        "tag_weapon": err(lambda f: np.linalg.inv(torso_at(nl + f)) @ frames[nl + f, weapon] @ off_weapon,
                          lambda f: up["tag_weapon"][f], nl, nu)}
    for nm, bone, off, part in (("tag_torso", parents[torso_root], off_torso, "l"), ("tag_head", head, off_head, "h"),
                                ("tag_weapon", weapon, off_weapon, "u")):
        names.append(nm)
        parents.append(bone)
        jpart.append(part)
        bind = np.concatenate([bind, (bind[bone] @ off)[None]])
        frames = np.concatenate([frames, (frames[:, bone] @ off)[:, None]], axis=1)

    # meshes: each primitive's triangles by part (the bone weights' majority), then into pieces
    meshes, vparts = [], []
    for pr in prims:
        tri_part = np.zeros(len(pr["tris"]), int)
        score = np.zeros((len(pr["tris"]), 3))
        for k in range(pr["jw"].shape[1]):
            for c, letter in enumerate("luh"):
                mask = np.array([jpart[j] == letter for j in range(len(jpart))])[pr["ji"][:, k]]
                score[:, c] += (pr["jw"][:, k] * mask)[pr["tris"]].sum(axis=1)
        tri_part = score.argmax(axis=1)
        for c, letter in enumerate("luh"):
            sel = pr["tris"][tri_part == c]
            if not len(sel):
                continue
            sname = surf_of.get((letter, pr["stem"]), "%s_%s" % (letter, pr["stem"]))
            for chunk_tris, verts in chunks(sel):
                meshes.append(dict(name=sname, material=base + pr["stem"], part=letter, prim=pr,
                                   verts=verts, tris=chunk_tris))

    # bounds of each frame (the skinned mesh)
    skinmats = frames @ np.linalg.inv(bind)[None]          # (frame, joint, 4, 4)
    bounds = []
    for f in range(len(frames)):
        pts = []
        for pr in prims:
            p4 = np.hstack([pr["pos"], np.ones((len(pr["pos"]), 1))])
            acc = np.zeros((len(p4), 4))
            for k in range(pr["jw"].shape[1]):
                acc += pr["jw"][:, k:k + 1] * np.einsum("vij,vj->vi", skinmats[f][pr["ji"][:, k]], p4)
            pts.append(acc[:, :3])
        pts = np.concatenate(pts)
        bounds.append((pts.min(0), pts.max(0), float(np.max(np.hypot(pts[:, 0], pts[:, 1]))),
                       float(np.max(np.linalg.norm(pts, axis=1)))))

    particles, links = ragdoll(prims, names, parents, bind, head)
    # the spine: the torso's joints from its first up to where the head's and the gun hand's branches meet;
    # the engine spreads the cgame's torso turn along it
    def chain(j):
        out = []
        while j >= 0:
            out.append(j)
            j = parents[j]
        return out
    meet = next(j for j in chain(weapon) if j in chain(head))
    spine = [j for j in reversed(chain(meet)) if jpart[j] == "u"]
    comment = "oz_rig 1\nupper %d\ndeaths %d\ndead %d %d %d\nmeshes %s\njoints %s\nspine %d %s\n" % (
        nl, anims["TORSO_GESTURE"][0], anims["BOTH_DEAD1"][0], anims["BOTH_DEAD2"][0], anims["BOTH_DEAD3"][0],
        "".join(x["part"] for x in meshes), "".join(jpart), len(spine), " ".join(str(j) for j in spine))
    comment += "particles %d %s\n" % (len(particles), " ".join("%d %.1f" % p for p in particles))
    comment += "links %d %s\n" % (len(links), " ".join("%d %d %.2f %.2f" % l for l in links))
    bodies, apart = ragdoll_bodies(prims, names, parents, bind, head)
    kinds = {"root": 0, "cone": 1, "hinge": 2}
    comment += "bodies %d %s\n" % (len(bodies), " ".join(
        "%d %d %d %.2f %.2f %.2f %.4f %.4f %.4f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.0f %.0f %.0f %.0f %.0f" % (
            e["bone"], e["parent"], kinds[e["kind"]], *e["center"], *e["axis"], e["radius"], e["height"], e["mass"],
            e["reach"], *e["joint"], e["swing"], e["twist"], e["bent"], e["neutral"], e["lean"]) for e in bodies))
    comment += "apart %d %s\n" % (len(apart), " ".join("%d %d" % p for p in apart))
    data = write_iqm(names, parents, bind, frames, [("lower", 0, nl), ("upper", nl, nu)], meshes, bounds, comment)
    report = dict(joints=len(names), verts=sum(len(x["verts"]) for x in meshes),
                  tris=sum(len(x["tris"]) for x in meshes), meshes=len(meshes), frames=[nl, nu], fit=fit_report,
                  islands=islands, surfaces=sorted(set((x["part"], x["name"]) for x in meshes)), bytes=len(data))
    return data, report


LIMB_BEND = 150.0       # degrees a limb joint folds from straight (elbows, knees, hips, shoulders)
MIN_BONE = 2.0          # units: a joint nearer than this to the particle above isn't a particle of its own
CORE_GIVE = (0.97, 1.02)   # the spine and neck keep their length to the joint two up within these
SIBLING_GIVE = (0.92, 1.05)


def ragdoll(prims, names, parents, bind, head):
    """The engine's ragdoll for this rig (tr_oz_ragdoll.c): a particle at each joint that moves skin, a radius for
    each (the median reach of the vertices it leads), and distance links with a min and max:
    - a joint to the particle above it: the bone, rigid;
    - a joint to the particle two up: the bend at the one between. The spine and neck (the head's ancestors) stay
      nearly straight; limbs fold up to LIMB_BEND degrees;
    - the particles below one joint to each other: hips and shoulders keep their spread, which also holds the
      torso's twist.
    -> ([(joint, radius)], [(particle a, particle b, min, max)])"""
    nj = len(names)
    weighted = np.zeros(nj, bool)
    for pr in prims:
        for k in range(pr["jw"].shape[1]):
            weighted[np.unique(pr["ji"][:, k][pr["jw"][:, k] > 0.01])] = True
    kids = [[c for c in range(nj) if parents[c] == j] for j in range(nj)]

    def any_weighted_below(j):
        return weighted[j] or any(any_weighted_below(c) for c in kids[j])
    is_tag = [n.startswith("tag_") for n in names]
    pos = bind[:, :3, 3]
    pindex = {}

    def up(j):                      # the nearest particle above a joint
        p = parents[j]
        while p >= 0 and p not in pindex:
            p = parents[p]
        return p
    # joints in parent order; one closer than MIN_BONE to the particle above it (the Godot rigs have
    # near-duplicate joints, a toe at its toe) follows that particle instead of being one
    joints = []
    for j in range(nj):
        if is_tag[j] or not any_weighted_below(j) or (parents[j] < 0 and not weighted[j]):
            continue
        if up(j) >= 0 and np.linalg.norm(pos[j] - pos[up(j)]) < MIN_BONE:
            continue
        pindex[j] = len(joints)
        joints.append(j)
    core, p = set(), up(head)
    while p >= 0:
        core.add(p)
        p = up(p)

    # radius: the median distance from the vertices a particle leads to its bone (to the particle below it)
    near = {j: [] for j in joints}
    for pr in prims:
        lead = pr["ji"][np.arange(len(pr["ji"])), pr["jw"].argmax(axis=1)]
        for j in joints:
            sel = pr["pos"][lead == j]
            if not len(sel):
                continue
            below = [c for c in joints if up(c) == j]
            a_ = pos[j]
            b_ = np.mean([pos[c] for c in below], axis=0) if below else pos[j]
            ab = b_ - a_
            t = np.clip(((sel - a_) @ ab) / max(float(ab @ ab), 1e-6), 0, 1)[:, None]
            near[j].append(np.linalg.norm(sel - (a_ + t * ab), axis=1))
    particles = [(j, float(np.clip(np.median(np.concatenate(near[j])) if near[j] else 2.0, 2.0, 8.0))) for j in joints]

    links = []
    def link(a, b, lo, hi):
        if hi >= MIN_BONE:          # shorter ones hold nothing a bone link doesn't
            links.append((pindex[a], pindex[b], lo, hi))
    for j in joints:
        p = up(j)
        if p < 0:
            continue
        rest = float(np.linalg.norm(pos[j] - pos[p]))
        link(j, p, rest, rest)
        g = up(p)
        if g >= 0:
            d0 = float(np.linalg.norm(pos[j] - pos[g]))
            if p in core and (j in core or j == head):
                link(j, g, d0 * CORE_GIVE[0], d0 * CORE_GIVE[1])
            else:
                a_, b_ = float(np.linalg.norm(pos[p] - pos[g])), rest
                folded = math.sqrt(max(a_ * a_ + b_ * b_ - 2 * a_ * b_ * math.cos(math.radians(180 - LIMB_BEND)), 0))
                link(j, g, min(folded, d0), max(a_ + b_, d0))
    for j in joints:
        below = [c for c in joints if up(c) == j]
        for i_ in range(len(below)):
            for k_ in range(i_ + 1, len(below)):
                d0 = float(np.linalg.norm(pos[below[i_]] - pos[below[k_]]))
                link(below[i_], below[k_], d0 * SIBLING_GIVE[0], d0 * SIBLING_GIVE[1])
    return particles, links


BODY_KG = 80.0          # a fighter's weight, shared out by the capsules' volume
BODY_MIN_VERTS = 40     # a bone carrying fewer vertices rides along with its parent body
GHOST = 8.0             # units: a body whose joint sits this far outside its rig parent hangs from the nearest body
BODY_MIN_RADIUS = 2.0   # units: thinner capsules (the Godot build's were 1.6) sank into the floor under the trunk
BODY_LIGHTEST = 3.0     # no part is lighter than its parent's weight over this (Jolt's solver lets parts much
                        # lighter than their parent, Orbb's 1 kg toes on 10 kg, push into the floor)
REROOT = 4.0            # units: a limb's far end this much nearer the neck than where the rig hangs it is where it hangs


def fit_capsule(p):
    """A capsule around points (id units): along their principal axis, as long as all but the outer 3% at
    each end, as thick as 80% of them stay within (Q3's bulky models are thicker than their bones).
    -> dict(center, axis, radius, height)"""
    c = p.mean(0)
    w, v = np.linalg.eigh(np.cov((p - c).T))
    axis = v[:, int(np.argmax(w))]
    t = (p - c) @ axis
    r = np.linalg.norm((p - c) - t[:, None] * axis, axis=1)
    lo, hi = np.percentile(t, [3, 97])
    radius = float(np.clip(np.percentile(r, 80), BODY_MIN_RADIUS, 8.0))
    return dict(center=c + axis * (lo + hi) * 0.5, axis=axis, radius=radius, height=max(float(hi - lo), radius * 2 + 0.32))


def capsule_segment(c):
    h = max(c["height"] * 0.5 - c["radius"], 0.0)
    return c["center"] - c["axis"] * h, c["center"] + c["axis"] * h


def closest(a0, a1, b0, b1):
    """The closest points of segments a0-a1 and b0-b1."""
    d1, d2, r = a1 - a0, b1 - b0, a0 - b0
    a, e, f = d1 @ d1, d2 @ d2, d2 @ r
    s = t = 0.0
    if a < 1e-9 and e < 1e-9:
        return a0, b0
    if a < 1e-9:
        t = float(np.clip(f / e, 0, 1))
    else:
        c = d1 @ r
        if e < 1e-9:
            s = float(np.clip(-c / a, 0, 1))
        else:
            b = d1 @ d2
            den = a * e - b * b
            s = float(np.clip((b * f - c * e) / den, 0, 1)) if den > 1e-9 else 0.0
            t = (b * s + f) / e
            if t < 0:
                t, s = 0.0, float(np.clip(-c / a, 0, 1))
            elif t > 1:
                t, s = 1.0, float(np.clip((b - c) / a, 0, 1))
    return a0 + d1 * s, b0 + d2 * t


def ragdoll_bodies(prims, names, parents, bind, head):
    """The fighter's ragdoll bodies (tr_oz_ragdoll.c, on Jolt), as the Godot build made them
    (q3_character.gd _ragdoll_plan), from the skin, not bone names (the rig fit misnames many limbs): every
    bone carrying 40 vertices or more gets a capsule fitted to its own vertices; the trunk is the chain from
    the pelvis to the head, everything else hangs off it as a limb, a leg when it reaches the floor. Leg
    joints take their kind from their height: hips swing 45 degrees (around a thigh hanging 30 forward), the
    first joint at knee height is a hinge bending one way, ankles 25; arms swing 60 at the shoulder and 40
    further out, the trunk 12 a joint. Mass follows the capsules' volume (about 80 kg in all).
    Past the Godot build: a body whose joint sits far outside its parent (the rig hangs some feet off the
    pelvis) hangs from the body nearest its joint, and a limb the rig hangs by its far end (Ranger's arms by
    the hands) hangs from the end nearer the neck.
    -> (bodies: [dict], apart: [(a, b)] bodies that never collide)"""
    nj = len(names)
    lead = {}
    for pr in prims:
        best = pr["ji"][np.arange(len(pr["ji"])), pr["jw"].argmax(axis=1)]
        for j in np.unique(best):
            lead.setdefault(int(j), []).append(pr["pos"][best == j])
    lead = {j: np.concatenate(v) for j, v in lead.items()}
    pelvis, hj = names.index("pelvis"), names.index("head")
    is_body = [j == pelvis or (j in lead and len(lead[j]) >= BODY_MIN_VERTS) for j in range(nj)]

    def body_up(j):                 # the nearest body above a bone in the rig
        p = parents[j]
        while p >= 0 and not is_body[p]:
            p = parents[p]
        return p
    up = {j: body_up(j) for j in range(nj)}
    bodies = []
    for j in range(nj):
        a = j
        while a >= 0 and a != pelvis:
            a = up[a] if is_body[a] else -1
        if is_body[j] and a == pelvis:
            bodies.append(j)
    caps, low = {}, {}
    for j in bodies:
        p = lead.get(j)
        if p is None or len(p) < 4:
            o = bind[j, :3, 3]
            p = np.array([o + (0, 0, -1.6), o + (0, 0, 1.6), o + (1, 0, 0), o + (-1, 0, 0)])
        caps[j] = fit_capsule(p)
        low[j] = float(p[:, 2].min())
    # the tree: as the rig has it, joints at the bones' origins (where the animation turns each part), but a
    # body whose joint sits far outside its parent (the rig hangs some feet off the pelvis) hangs from the
    # body nearest its joint
    par = {j: up[j] for j in bodies if j != pelvis}
    joint = {j: bind[j, :3, 3] for j in bodies if j != pelvis}

    def off(p, c):              # how far a point is outside a capsule
        a_, b_ = capsule_segment(c)
        d = b_ - a_
        t = float(np.clip((p - a_) @ d / max(d @ d, 1e-9), 0, 1))
        return float(np.linalg.norm(p - (a_ + d * t))) - c["radius"]

    def under(j):
        out, todo = {j}, [j]
        while todo:
            x = todo.pop()
            for c, p in par.items():
                if p == x and c not in out:
                    out.add(c)
                    todo.append(c)
        return out
    for j in bodies:
        if j != pelvis and off(joint[j], caps[par[j]]) > GHOST:
            sub = under(j)
            best = min((c for c in bodies if c not in sub), key=lambda c: off(joint[j], caps[c]))
            if off(joint[j], caps[best]) < off(joint[j], caps[par[j]]) - 4.0:
                par[j] = best
    # the trunk: the pelvis up to the head
    head = hj
    while head >= 0 and head not in caps:
        head = parents[head]
    if head < 0 or head not in caps:
        head = pelvis
    trunk, t = set(), head
    while t >= 0:
        trunk.add(t)
        t = par.get(t, -1)
    if pelvis not in trunk:         # the head's body didn't lead back down to the pelvis: the trunk is the pelvis
        trunk, head = {pelvis}, pelvis
    floor = FEET
    near_floor = lambda j: low[j] - floor < (bind[pelvis, 2, 3] - floor) * 0.3
    # a limb the rig hangs by its far end (the rig fit rooted Ranger's arms at the hands, which hold the gun at
    # his waist, so his shoulders hung free and came away from the arms; Xaero's sash at its tip): a chain of
    # parts off the trunk without branches, not a leg, whose last part's far end lies nearer the neck than where
    # it hangs, hangs from that end instead (on the trunk part nearest it), each part from the one beyond it
    neck = joint[head] if head != pelvis else caps[pelvis]["center"]
    for r in [j for j in bodies if j not in trunk and par[j] in trunk]:
        chain = [r]
        while True:
            kids = [c for c in bodies if par.get(c) == chain[-1]]
            if len(kids) != 1:
                break
            chain.append(kids[0])
        if kids or len(chain) < 2 or any(near_floor(j) for j in chain):
            continue
        end = max(capsule_segment(caps[chain[-1]]), key=lambda p: np.linalg.norm(p - joint[chain[-1]]))
        if np.linalg.norm(end - neck) < np.linalg.norm(joint[r] - neck) - REROOT:
            was = {j: joint[j] for j in chain}
            par[chain[-1]], joint[chain[-1]] = min(trunk, key=lambda c: off(end, caps[c])), end
            for k in range(len(chain) - 1, 0, -1):
                par[chain[k - 1]], joint[chain[k - 1]] = chain[k], was[chain[k]]
    order = []

    def visit(j):
        order.append(j)
        for c in bodies:
            if par.get(c) == j:
                visit(c)
    visit(pelvis)
    bodies = order
    for j in bodies:                # each capsule points away from its joint
        if j in joint and caps[j]["axis"] @ (caps[j]["center"] - joint[j]) < 0:
            caps[j]["axis"] = -caps[j]["axis"]
    limb_root = {}
    for j in bodies:
        r = j
        while r not in trunk and par[r] not in trunk:
            r = par[r]
        limb_root[j] = r
    leg = {limb_root[j] for j in bodies if j not in trunk and near_floor(j)}
    # chunks hanging off the trunk inside the body (Keel's belly, a shoulder blade) move with the trunk
    spur = {}
    mid = caps[pelvis]["center"]
    bottom = mid[2] - caps[pelvis]["height"] * 0.5
    for j in bodies:
        chain, a = [], j
        while a not in trunk:
            chain.insert(0, a)
            a = par[a]
        for k in chain:
            c = caps[k]["center"]
            spur[k] = (par[k] in trunk or spur[par[k]]) and np.hypot(c[0] - mid[0], c[1] - mid[1]) < 3.84 \
                and bottom < c[2] < caps[head]["center"][2]
    for j in bodies:
        c = caps[j]
        seg = max(c["height"] - 2 * c["radius"], 0.0)
        c["volume"] = math.pi * c["radius"] ** 2 * (seg + c["radius"] * 4 / 3)
    total = sum(caps[j]["volume"] for j in bodies)
    band = {}
    for j in bodies:
        if j != pelvis and j not in trunk and not spur.get(j) and limb_root[j] in leg:
            h = (joint[j][2] - floor) / max(joint[limb_root[j]][2] - floor, 0.3)
            band[j] = "hip" if h > 0.75 else "knee" if h > 0.3 else "foot"
    out = []
    for j in bodies:
        c = caps[j]
        e = dict(bone=j, center=c["center"], axis=c["axis"], radius=c["radius"], height=c["height"],
                 mass=float(np.clip(BODY_KG * c["volume"] / total, 1.0, 12.0)), parent=-1, kind="root",
                 swing=0.0, twist=0.0, bent=0.0, neutral=0.0, lean=0.0, joint=np.zeros(3))
        if j != pelvis:
            e["parent"], e["joint"] = bodies.index(par[j]), joint[j]
            e["kind"] = "cone"
            if j in trunk or spur.get(j):
                e["swing"], e["twist"] = (30.0, 25.0) if j == head else (12.0, 8.0)
                e["lean"] = 12.0 if j == head else 4.0          # the neutral posture: head and back a little forward
            elif j in band:
                again, a = False, par[j]
                while a in band:
                    again = again or band[a] == band[j]
                    a = par[a]
                if band[j] == "knee" and not again:
                    # Quake 3 stands in a crouch: the knee's 0-140 degrees count from a straight leg, less the
                    # bend it has standing
                    e["kind"] = "hinge"
                    upper, lower = caps[par[j]]["axis"], c["axis"]
                    tp, sp = np.array([upper[2], upper[0]]), np.array([lower[2], lower[0]])
                    ang = math.degrees(math.atan2(tp[0] * sp[1] - tp[1] * sp[0], tp @ sp))
                    e["bent"] = float(np.clip(abs(ang), 0.0, 90.0)) if par[j] in band else 0.0
                else:
                    e["swing"] = 10.0 if again and band[j] != "foot" else 45.0 if band[j] == "hip" else 25.0
                    e["twist"] = 12.0 if band[j] == "hip" else 4.0
                    hang = np.array([math.sin(math.pi / 6), 0.0, -math.cos(math.pi / 6)])
                    if band[j] == "hip" and not again and math.acos(np.clip(c["axis"] @ hang, -1, 1)) < math.radians(45):
                        # a hip's cone is centered on a thigh hanging 30 degrees forward, not on the stance
                        # (Sarge stands with his thighs spread 30 degrees: from there legs flop out like a frog's)
                        e["neutral"] = 1.0
                        e["lean"] = 20.0                         # and the neutral posture's thighs 20 more
            else:
                # arms (and tails, wings): the shoulder swings 60, everything further out 40
                shoulder, a = True, par[j]
                while a not in trunk:
                    shoulder = shoulder and spur.get(a, False)
                    a = par[a]
                e["swing"], e["twist"] = (60.0, 25.0) if shoulder else (40.0, 10.0)
        out.append(e)
    # weights: the solver copes with a part up to about 2.5 times its parent's, and down to a third of it; the
    # trunk is never light
    for e in out:
        if e["bone"] == pelvis:
            e["mass"] = max(e["mass"], 8.0)
        elif e["bone"] in trunk and e["bone"] != head:
            e["mass"] = max(e["mass"], 4.0)
        if e["parent"] >= 0:
            up_ = out[e["parent"]]["mass"]
            e["mass"] = float(np.clip(e["mass"], up_ / BODY_LIGHTEST, up_ * 2.5))
    # reach: how far its joints sit from each part's middle (its own and its children's)
    for e in out:
        e["reach"] = 0.0
    for e in out:
        if e["parent"] >= 0:
            e["reach"] = max(e["reach"], float(np.linalg.norm(e["joint"] - e["center"])))
            up_ = out[e["parent"]]
            up_["reach"] = max(up_["reach"], float(np.linalg.norm(e["joint"] - up_["center"])))
    # bodies touching standing, and every body and its parent, never collide
    apart = []
    for a_ in range(len(out)):
        for b_ in range(a_ + 1, len(out)):
            ea, eb = out[a_], out[b_]
            if eb["parent"] == a_ or ea["parent"] == b_:
                apart.append((a_, b_))
                continue
            p, q = closest(*capsule_segment(ea), *capsule_segment(eb))
            if np.linalg.norm(p - q) < ea["radius"] + eb["radius"] + 0.64:
                apart.append((a_, b_))
    return out, apart


def chunks(tris):
    """Triangles -> pieces of at most MAX_VERTS vertices: [(local triangles, source vertex indexes)]."""
    out, vmap, cur = [], {}, []
    for t in tris:
        new = len({v for v in t if v not in vmap})
        if len(vmap) + new > MAX_VERTS:
            out.append((np.array(cur), np.array(list(vmap))))
            vmap, cur = {}, []
        for v in t:
            vmap.setdefault(int(v), len(vmap))
        cur.append([vmap[int(v)] for v in t])
    if cur:
        out.append((np.array(cur), np.array(list(vmap))))
    return out


def write_iqm(names, parents, bind, frames, anims, meshes, bounds, comment):
    """IQM version 2 (iqm.h): text, meshes, vertex arrays, triangles, joints, poses, anims, frames, bounds, comment."""
    text = bytearray(b"\0")
    strings = {}

    def s(x):
        if x not in strings:
            strings[x] = len(text)
            text.extend(x.encode("latin-1") + b"\0")
        return strings[x]
    pos, tc, nrm, tan, bi, bw, tris, mrecs = [], [], [], [], [], [], [], []
    nv = nt = 0
    for mh in meshes:
        pr, v = mh["prim"], mh["verts"]
        mrecs.append((s(mh["name"]), s(mh["material"]), nv, len(v), nt, len(mh["tris"])))
        nt += len(mh["tris"])
        pos.append(pr["pos"][v])
        tc.append(pr["uv"][v])
        nrm.append(pr["nrm"][v])
        tan.append(pr["tan"][v])
        w = np.zeros((len(v), 4))
        ix = np.zeros((len(v), 4), int)
        k = min(4, pr["jw"].shape[1])
        w[:, :k], ix[:, :k] = pr["jw"][v, :k], pr["ji"][v, :k]
        w = w / np.maximum(w.sum(axis=1, keepdims=True), 1e-12)
        wb = np.floor(w * 255 + 0.5).astype(int)
        top = wb.argmax(axis=1)
        wb[np.arange(len(v)), top] += 255 - wb.sum(axis=1)        # sums to 255
        bi.append(np.where(wb > 0, ix, 0))
        bw.append(wb)
        tris.append(mh["tris"] + nv)
        nv += len(v)
    arrays = [(0, 7, 3, np.concatenate(pos).astype("<f4")), (1, 7, 2, np.concatenate(tc).astype("<f4")),
              (2, 7, 3, np.concatenate(nrm).astype("<f4")), (3, 7, 4, np.concatenate(tan).astype("<f4")),
              (4, 1, 4, np.concatenate(bi).astype("u1")), (5, 1, 4, np.concatenate(bw).astype("u1"))]
    tri = np.concatenate(tris).astype("<u4")
    njoint = len(names)
    jrecs = []
    for i in range(njoint):
        loc = bind[i] if parents[i] < 0 else np.linalg.inv(bind[parents[i]]) @ bind[i]
        t, q, sc = decompose(loc)
        jrecs.append(struct.pack("<Ii10f", s(names[i]), parents[i], *t, *q, *sc))
    # poses: each joint's local transform per frame, quantized per channel (t xyz, q xyzw, s xyz)
    nf = len(frames)
    ch = np.zeros((nf, njoint, 10))
    for f in range(nf):
        for i in range(njoint):
            loc = frames[f, i] if parents[i] < 0 else np.linalg.inv(frames[f, parents[i]]) @ frames[f, i]
            t, q, sc = decompose(loc)
            if f and np.dot(q, ch[f - 1, i, 3:7]) < 0:
                q = -q                          # keep each joint's quaternions on one side, for quantization
            ch[f, i] = np.concatenate([t, q, sc])
    lo, hi = ch.min(0), ch.max(0)
    rng = hi - lo
    moving = rng > 1e-6
    scale = np.where(moving, rng / 65535.0, 0.0)
    precs, cols = [], []
    for i in range(njoint):
        mask = sum(1 << c for c in range(10) if moving[i, c])
        precs.append(struct.pack("<iI20f", parents[i], mask, *lo[i], *scale[i]))
        for c in range(10):
            if moving[i, c]:
                cols.append(np.floor((ch[:, i, c] - lo[i, c]) / scale[i, c] + 0.5).clip(0, 65535))
    framedata = np.stack(cols, axis=1).astype("<u2") if cols else np.zeros((nf, 0), "<u2")
    arecs = [struct.pack("<IIIfI", s(nm), first, n, 60.0, 0) for nm, first, n in anims]
    brecs = [struct.pack("<8f", *lo_, *hi_, xy, r) for lo_, hi_, xy, r in bounds]
    cm = comment.encode("latin-1") + b"\0"

    out = bytearray(124)
    def put(blob):
        while len(out) % 4:
            out.append(0)
        at = len(out)
        out.extend(blob)
        return at
    ofs_text = put(bytes(text))
    ofs_meshes = put(b"".join(struct.pack("<6I", *r) for r in mrecs))
    va_at = put(bytes(20 * len(arrays)))
    varecs = []
    for kind, fmt, size, arr in arrays:
        varecs.append(struct.pack("<5I", kind, 0, fmt, size, put(arr.tobytes())))
    out[va_at:va_at + 20 * len(arrays)] = b"".join(varecs)
    ofs_tris = put(tri.tobytes())
    ofs_joints = put(b"".join(jrecs))
    ofs_poses = put(b"".join(precs))
    ofs_anims = put(b"".join(arecs))
    ofs_frames = put(framedata.tobytes())
    ofs_bounds = put(b"".join(brecs))
    ofs_comment = put(cm)
    out[0:124] = struct.pack("<16s27I", b"INTERQUAKEMODEL\0", 2, len(out), 0,
                             len(text), ofs_text, len(mrecs), ofs_meshes,
                             len(arrays), nv, va_at, len(tri), ofs_tris, 0,
                             njoint, ofs_joints, njoint, ofs_poses, len(arecs), ofs_anims,
                             nf, framedata.shape[1], ofs_frames, ofs_bounds,
                             len(cm), ofs_comment, 0, 0)
    return bytes(out)


# ------------------------------------------------------------------ textures

class CharLib:
    """The Godot build's character maps: <model>_<image stem>_<albedo|normal|orm|emissive>.jpg/png."""
    KIND = {"_albedo": "albedo", "_normal": "normal", "_orm": "orm", "__glow_emissive": "emissive",
            "__additive_albedo": "albedo"}

    def __init__(self, folder):
        self.tex = {}
        for f in os.listdir(folder):
            stem, ext = os.path.splitext(f)
            if ext.lower() in (".jpg", ".png") and (stem.lower() not in self.tex or ext.lower() == ".png"):
                self.tex[stem.lower()] = os.path.join(folder, f)

    def get(self, image, suffix):
        p = image.lower().replace("\\", "/").split("/")
        kind = self.KIND.get(suffix)
        if len(p) != 4 or p[:2] != ["models", "players"] or not kind:
            return None
        stem = os.path.splitext(p[3])[0]
        for s in (stem, re.sub(r"[._]glow$", "", stem)):
            hit = self.tex.get("%s_%s_%s" % (p[2], s, kind))
            if hit:
                return hit
        return None


def encode(job):
    global _encoder
    path, kind, src = job
    try:
        if _encoder is None:
            import gpu_bc
            _encoder = gpu_bc.Encoder()
        dst = os.path.join(STAGE, path)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        _encoder.write_dds(dst + ".part", make_pk3s.load(kind, src), "bc5" if kind == "normal" else "bc7")
        os.replace(dst + ".part", dst)
        return path, None
    except Exception as e:
        return path, "%s: %s" % (type(e).__name__, e)


_encoder = None


# ------------------------------------------------------------------ main

def main():
    baseq3 = a.baseq3 or find_baseq3()
    paks = Paks(baseq3)
    make_pk3s.paks = paks          # id's alpha for remastered albedos without one (make_pk3s.tile)
    lib = CharLib(a.maps_library)
    models = a.models or sorted(f[:-4] for f in os.listdir(a.library) if f.endswith(".glb"))
    print("baseq3:", baseq3)
    print("library: %s (%d maps, %d fighters)" % (a.library, len(lib.tex), len(models)))

    scripts = {}
    for name in sorted(n for n in paks.files if n.startswith("scripts/") and n.endswith(".shader")):
        for sh in q3shader.parse_text(paks.read(name).decode("latin-1"), os.path.basename(name)):
            scripts.setdefault(sh.name, sh)

    reports, ozmtr, images = {}, [], set()
    for m in models:
        data, rep = build_rig(m, paks, a.library)
        reports[m] = rep
        dst = os.path.join(STAGE, "models", "players", m, RIG)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        open(dst, "wb").write(data)
        print("%-9s %2d joints %5d verts %5d tris %3d meshes %2d islands  frames %d+%d  %4d KB  tag fit (units, degrees): "
              "%s" % (m, rep["joints"], rep["verts"], rep["tris"], rep["meshes"], rep["islands"], rep["frames"][0],
                      rep["frames"][1], rep["bytes"] // 1024,
                      "  ".join("%s %s/%s %s" % (k[4:], *v) for k, v in rep["fit"].items())), flush=True)
        for f in sorted(paks.files):
            if f.startswith("models/players/%s/" % m) and f.endswith(".skin"):
                images.update(t for _, t in read_skin(paks.read(f).decode("latin-1")) if t)
        for mh in ("lower", "upper", "head"):
            images.update(sh for _, sh in read_md3(paks.read("models/players/%s/%s.md3" % (m, mh)))[2] if sh)

    shaders = {os.path.splitext(i)[0] for i in images}
    shaders |= {n for n in scripts if any(n.startswith("models/players/%s/" % m) for m in models)}
    rewritten, implicit, missing = 0, 0, []
    for name in sorted(shaders):
        sh = scripts.get(name)
        if sh is not None:
            text = make_pk3s.rewrite(sh, lib, False)
            if text:
                ozmtr.append(text)
                rewritten += 1
        elif paks.image(name):
            if make_pk3s.tile(name, lib, True):
                implicit += 1
            else:
                missing.append(name)
    jobs = make_pk3s.jobs
    kinds = {}
    for k, _ in jobs.values():
        kinds[k] = kinds.get(k, 0) + 1
    print("%d shaders: %d scripts rewritten, %d implicit images remastered, %d without a remaster%s" % (
        len(shaders), rewritten, implicit, len(missing), (": " + ", ".join(missing[:8])) if missing else ""))
    print("textures: %d (%s)" % (len(jobs), ", ".join("%s %d" % kv for kv in sorted(kinds.items()))))
    json.dump(reports, open(os.path.join(a.work, "fighters_report.json"), "w"), indent=1)
    if a.report:
        return

    srcs = lambda p: jobs[p][1] if isinstance(jobs[p][1], tuple) else (jobs[p][1],)     # (albedo, id's alpha)
    todo = [p for p in sorted(jobs) if not os.path.isfile(os.path.join(STAGE, p)) or
            any(os.path.getmtime(f) > os.path.getmtime(os.path.join(STAGE, p)) for f in srcs(p))]
    print("converting %d (%d up to date)" % (len(todo), len(jobs) - len(todo)))
    if todo:
        import multiprocessing
        done, failed = 0, []
        with multiprocessing.Pool(a.jobs) as pool:
            for path, e in pool.imap_unordered(encode, [(p,) + jobs[p] for p in todo], chunksize=4):
                done += 1
                if e:
                    failed.append((path, e))
                if done % 100 == 0 or done == len(todo):
                    print("  %d / %d" % (done, len(todo)), flush=True)
        for path, e in failed[:10]:
            print("  failed:", path, e)

    os.makedirs(os.path.join(STAGE, "scripts"), exist_ok=True)
    open(os.path.join(STAGE, "scripts", "oz_fighters.ozmtr"), "w", encoding="latin-1", newline="\n").write(
        "// Oz Arena: id's player shaders with the remastered images (tools/make_fighters.py). Read only by the oz renderer.\n\n"
        + "\n\n".join(ozmtr) + "\n")
    files = sorted(set(p for p in jobs if os.path.isfile(os.path.join(STAGE, p))) | {"scripts/oz_fighters.ozmtr"} |
                   {"models/players/%s/%s" % (m, RIG) for m in models})
    os.makedirs(a.out, exist_ok=True)
    pk3 = os.path.join(a.out, "pak_oz_fighters.pk3")
    with zipfile.ZipFile(pk3 + ".part", "w") as z:
        for f in files:
            z.write(os.path.join(STAGE, f), f, zipfile.ZIP_STORED if f.endswith(".dds") else zipfile.ZIP_DEFLATED)
    os.replace(pk3 + ".part", pk3)
    print("  %-28s %6d MB" % (os.path.basename(pk3), os.path.getsize(pk3) // 2 ** 20))


if __name__ == "__main__":
    main()
