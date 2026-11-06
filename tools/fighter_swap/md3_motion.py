"""An id fighter's own motion, read from its md3s, for a modern model to move like it (anim_fighter.py, a config's
"motion": "md3").

The md3s are vertex animation in three parts: lower.md3 (legs, its own frames), upper.md3 (the torso, hung at the
legs' tag_torso) and head.md3 (at the torso's tag_head). Each body segment here is a set of the md3's vertices,
picked in a reference pose (TORSO_STAND over LEGS_IDLE) as the ones nearest that segment's bone between the
fighter's joints (the config's "motion" joints, measured on that pose). In every frame each segment's turn is the
rigid fit (Kabsch) of its vertices to their reference places, so a modern bone can take the same turn.
"""
import struct, zipfile
import numpy as np


def read_md3_verts(data):
    """-> (frames, {tag: [4x4 per frame]}, [(surface name, verts (frames, n, 3))])"""
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
        sf_frames, _, nv, ntri, otri, _, ost, oxyz, oend = struct.unpack_from("<9i", data, o + 72)
        xyz = np.frombuffer(data, "<i2", sf_frames * nv * 4, o + oxyz).reshape(sf_frames, nv, 4)[:, :, :3] / 64.0
        tris = np.frombuffer(data, "<i4", ntri * 3, o + otri).reshape(-1, 3)
        surfs.append((name, xyz.astype(float), tris))
        o += oend
    return nf, tags, surfs


class Md3Fighter:
    """lower/upper/head md3 of an id fighter: vertices per frame, composed like the cgame puts them together"""

    def __init__(self, pak0, slot):
        z = zipfile.ZipFile(pak0)
        base = "models/players/%s/" % slot
        self.lower = read_md3_verts(z.read(base + "lower.md3"))
        self.upper = read_md3_verts(z.read(base + "upper.md3"))
        self.head = read_md3_verts(z.read(base + "head.md3"))

    @staticmethod
    def part_verts(md3, frame):
        return np.concatenate([s[1][min(frame, len(s[1]) - 1)] for s in md3[2]])

    def lower_verts(self, f):
        return self.part_verts(self.lower, f)

    def upper_verts(self, f):
        """upper.md3 in its own (torso) space"""
        return self.part_verts(self.upper, f)

    def head_verts(self):
        return self.part_verts(self.head, 0)

    def tag(self, md3, name, f):
        t = md3[1][name]
        return t[min(f, len(t) - 1)]

    def world(self, lf, uf):
        """the whole fighter at legs frame lf, torso frame uf: (lower verts, upper verts, head verts, torso tag,
        head tag) in the legs' space (the torso not turned against the legs)"""
        T = self.tag(self.lower, "tag_torso", lf)
        H = T @ self.tag(self.upper, "tag_head", uf)
        tf = lambda M, p: p @ M[:3, :3].T + M[:3, 3]
        return self.lower_verts(lf), tf(T, self.upper_verts(uf)), tf(H, self.head_verts()), T, H


def kabsch(A, B):
    """rotation R and translation t with R @ a + t ~ b for the rows of A, B"""
    ca, cb = A.mean(0), B.mean(0)
    H = (A - ca).T @ (B - cb)
    U, _, Vt = np.linalg.svd(H)
    d = np.sign(np.linalg.det(Vt.T @ U.T))
    R = Vt.T @ np.diag([1, 1, d]) @ U.T
    return R, cb - R @ ca


class Md3Motion:
    """The id fighter's segments and joints over its frames.

    cfg (a fighter config's "motion"): "joints" {name: [x, y, z]} in the reference pose (id units, the whole fighter
    as Md3Fighter.world puts it); "segments" {segment: [[joint, joint], ...]} the bones each segment's vertices lie
    along, with "lower:" or "upper:" naming the md3 they come from; "skip" surface names to leave out (the scorpion
    arm, cloth). Joints found by pivot fits: where two rigid pieces' motions agree over every frame."""

    def __init__(self, pak0, slot, cfg, lf_ref, uf_ref):
        self.f = Md3Fighter(pak0, slot)
        self.J = {k: np.asarray(v, float) for k, v in cfg["joints"].items()}
        self.lf_ref, self.uf_ref = lf_ref, uf_ref
        skip = set(cfg.get("skip", []))
        f = self.f
        self.T_ref = f.tag(f.lower, "tag_torso", lf_ref)
        self.seg = {}
        for part, md3 in (("lower", f.lower), ("upper", f.upper)):
            V = np.concatenate([s[1] for s in md3[2]], axis=1)
            keep = np.concatenate([[s[0] not in skip] * s[1].shape[1] for s in md3[2]])
            ref = V[lf_ref] if part == "lower" else self._to_world(V[uf_ref], self.T_ref)
            mine = {n: [(self.J[a], self.J[b]) for a, b in bones] for n, (p, bones) in
                    ((n, (v[0], v[1:])) for n, v in cfg["segments"].items()) if p == part}
            names = list(mine)
            D = np.array([np.min([np.linalg.norm(ref - (a + np.clip(((ref - a) @ (b - a)) / max((b - a) @ (b - a), 1e-9), 0, 1)[:, None] * (b - a)), axis=1)
                                  for a, b in mine[n]], axis=0) for n in names])
            # whole rigid pieces go to one bone: vertices that keep their distances over every frame (the md3 moves
            # its segments rigidly) are clustered, and each piece goes to the bone nearest it on average, so a hand
            # crossing the belly doesn't pick up belly vertices
            piece = rigid_pieces(V, keep)
            owner = np.full(len(ref), -1)
            for k in np.unique(piece[piece >= 0]):
                s = piece == k
                owner[s] = int(np.argmin(D[:, s].mean(1)))
            for i, n in enumerate(names):
                sel = (owner == i) & keep & (D[i] < cfg.get("reach", 8.0))
                self.seg[n] = (part, V[:, sel], ref[sel])

    @staticmethod
    def _to_world(P, M):
        return P @ M[:3, :3].T + M[:3, 3]

    def segment_transforms(self, lf, uf):
        """-> {segment: 4x4 world transform from the reference pose to this frame}"""
        f = self.f
        T = f.tag(f.lower, "tag_torso", lf)
        out = {}
        for n, (part, V, ref) in self.seg.items():
            if part == "lower":
                R, t = kabsch(ref, V[min(lf, len(V) - 1)])
            else:
                cur = self._to_world(V[min(uf, len(V) - 1)], T)
                R, t = kabsch(ref, cur)
            M = np.eye(4)
            M[:3, :3], M[:3, 3] = R, t
            out[n] = M
        H = T @ f.tag(f.upper, "tag_head", uf)
        Href = self.T_ref @ f.tag(f.upper, "tag_head", self.uf_ref)
        out["head"] = H @ np.linalg.inv(Href)
        out["tag_torso"] = T @ np.linalg.inv(self.T_ref)
        return out

    def tag_weapon(self, lf, uf):
        f = self.f
        return f.tag(f.lower, "tag_torso", lf) @ f.tag(f.upper, "tag_weapon", uf)


def rigid_pieces(V, keep, tol=0.3):
    """V (frames, n, 3) -> piece number per vertex (-1 where not kept): average-linkage clusters of the spread of
    each vertex pair's distance over the frames, cut at tol units"""
    from scipy.cluster.hierarchy import linkage, fcluster
    from scipy.spatial.distance import squareform
    idx = np.flatnonzero(keep)
    W = V[:, idx]
    s1 = np.zeros((len(idx), len(idx)))
    s2 = np.zeros_like(s1)
    for fr in W:
        d = np.linalg.norm(fr[:, None] - fr[None], axis=2)
        s1 += d
        s2 += d * d
    n = len(W)
    S = np.sqrt(np.maximum(s2 / n - (s1 / n) ** 2, 0))
    np.fill_diagonal(S, 0)
    S = (S + S.T) / 2
    lab = fcluster(linkage(squareform(S, checks=False), "average"), tol, "distance")
    out = np.full(V.shape[1], -1)
    out[idx] = lab
    return out


def nearest_segment(P, segments):
    """P: (n, 3) points; segments: {name: (a, b)} -> name per point of the nearest segment"""
    names = list(segments)
    D = []
    for n in names:
        a, b = (np.asarray(x, float) for x in segments[n])
        ab = b - a
        t = np.clip(((P - a) @ ab) / max(ab @ ab, 1e-9), 0, 1)
        D.append(np.linalg.norm(P - (a + t[:, None] * ab), axis=1))
    return np.array(names)[np.argmin(np.array(D), axis=0)]
