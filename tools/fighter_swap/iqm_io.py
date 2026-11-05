"""Read an IQM v2 (as tools/make_fighters.py writes it) and rewrite it with new meshes, keeping the skeleton,
poses, frames, anims and bounds byte for byte."""
import struct, math
import numpy as np

HDR = "<16s27I"
FIELDS = ["version", "filesize", "flags", "num_text", "ofs_text", "num_meshes", "ofs_meshes", "num_vertexarrays",
          "num_vertexes", "ofs_vertexarrays", "num_triangles", "ofs_triangles", "ofs_adjacency", "num_joints",
          "ofs_joints", "num_poses", "ofs_poses", "num_anims", "ofs_anims", "num_frames", "num_framechannels",
          "ofs_frames", "ofs_bounds", "num_comment", "ofs_comment", "num_extensions", "ofs_extensions"]


def quat_matrix(q):
    x, y, z, w = np.asarray(q, float) / np.linalg.norm(q)
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def trs(t, r, s):
    m = np.eye(4)
    m[:3, :3] = quat_matrix(r) * np.asarray(s, float)
    m[:3, 3] = t
    return m


class Iqm:
    def __init__(self, data):
        self.data = data
        v = struct.unpack_from(HDR, data, 0)
        assert v[0] == b"INTERQUAKEMODEL\0"
        self.h = dict(zip(FIELDS, v[1:]))
        h = self.h
        self.text = data[h["ofs_text"]:h["ofs_text"] + h["num_text"]]
        self.str = lambda o: self.text[o:self.text.index(b"\0", o)].decode("latin-1")
        # joints
        self.names, self.parents, self.local = [], [], []
        for i in range(h["num_joints"]):
            n, p, *f = struct.unpack_from("<Ii10f", data, h["ofs_joints"] + i * 48)
            self.names.append(self.str(n))
            self.parents.append(p)
            self.local.append(trs(f[0:3], f[3:7], f[7:10]))
        self.bind = []
        for i, p in enumerate(self.parents):
            self.bind.append(self.local[i] if p < 0 else self.bind[p] @ self.local[i])
        self.bind = np.array(self.bind)
        # poses and frames
        self.poses = []
        for i in range(h["num_poses"]):
            p, mask, *f = struct.unpack_from("<iI20f", data, h["ofs_poses"] + i * 88)
            self.poses.append((p, mask, np.array(f[:10]), np.array(f[10:])))
        nf, nc = h["num_frames"], h["num_framechannels"]
        fd = np.frombuffer(data, "<u2", nf * nc, h["ofs_frames"]).reshape(nf, nc)
        self.frames = np.zeros((nf, len(self.poses), 4, 4))
        for f in range(nf):
            c = 0
            for j, (p, mask, off, sc) in enumerate(self.poses):
                ch = off.copy()
                for k in range(10):
                    if mask & (1 << k):
                        ch[k] += fd[f, c] * sc[k]
                        c += 1
                loc = trs(ch[0:3], ch[3:7], ch[7:10])
                self.frames[f, j] = loc if p < 0 else self.frames[f, p] @ loc
        self.anims = []
        for i in range(h["num_anims"]):
            n, first, cnt, fr, fl = struct.unpack_from("<IIIfI", data, h["ofs_anims"] + i * 20)
            self.anims.append((self.str(n), first, cnt, fr, fl))
        self.comment = data[h["ofs_comment"]:h["ofs_comment"] + h["num_comment"]].split(b"\0")[0].decode("latin-1")
        # meshes and vertex arrays
        self.meshes = []
        for i in range(h["num_meshes"]):
            n, mat, fv, nv, ft, nt = struct.unpack_from("<6I", data, h["ofs_meshes"] + i * 24)
            self.meshes.append(dict(name=self.str(n), material=self.str(mat), fv=fv, nv=nv, ft=ft, nt=nt))
        self.va = {}
        for i in range(h["num_vertexarrays"]):
            kind, flags, fmt, size, ofs = struct.unpack_from("<5I", data, h["ofs_vertexarrays"] + i * 20)
            dt = {7: "<f4", 1: "u1"}[fmt]
            self.va[kind] = np.frombuffer(data, dt, h["num_vertexes"] * size, ofs).reshape(-1, size)
        self.tris = np.frombuffer(data, "<u4", h["num_triangles"] * 3, h["ofs_triangles"]).reshape(-1, 3)

    def block(self, ofs_key, size):
        o = self.h[ofs_key]
        return self.data[o:o + size]

    def rewrite(self, meshes, pos, uv, nrm, tan, bi, bw, tris, comment):
        """meshes: [(name, material, first_vertex, num_vertexes, first_triangle, num_triangles)]"""
        h = self.h
        text = bytearray(self.text)
        cache = {}

        def s(x):
            if x not in cache:
                cache[x] = len(text)
                text.extend(x.encode("latin-1") + b"\0")
            return cache[x]
        mrecs = [struct.pack("<6I", s(n), s(m), a, b, c, d) for n, m, a, b, c, d in meshes]
        arrays = [(0, 7, 3, pos.astype("<f4")), (1, 7, 2, uv.astype("<f4")), (2, 7, 3, nrm.astype("<f4")),
                  (3, 7, 4, tan.astype("<f4")), (4, 1, 4, bi.astype("u1")), (5, 1, 4, bw.astype("u1"))]
        nj, np_, na, nf, nc = h["num_joints"], h["num_poses"], h["num_anims"], h["num_frames"], h["num_framechannels"]
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
        recs = []
        for kind, fmt, size, arr in arrays:
            recs.append(struct.pack("<5I", kind, 0, fmt, size, put(arr.tobytes())))
        out[va_at:va_at + 20 * len(arrays)] = b"".join(recs)
        ofs_tris = put(tris.astype("<u4").tobytes())
        ofs_joints = put(self.block("ofs_joints", nj * 48))
        ofs_poses = put(self.block("ofs_poses", np_ * 88))
        ofs_anims = put(self.block("ofs_anims", na * 20))
        ofs_frames = put(self.block("ofs_frames", nf * nc * 2))
        ofs_bounds = put(self.block("ofs_bounds", nf * 32))
        cm = comment.encode("latin-1") + b"\0"
        ofs_comment = put(cm)
        out[0:124] = struct.pack(HDR, b"INTERQUAKEMODEL\0", 2, len(out), 0, len(text), ofs_text, len(mrecs),
                                 ofs_meshes, len(arrays), len(pos), va_at, len(tris), ofs_tris, 0, nj, ofs_joints,
                                 np_, ofs_poses, na, ofs_anims, nf, nc, ofs_frames, ofs_bounds, len(cm), ofs_comment,
                                 0, 0)
        return bytes(out)
