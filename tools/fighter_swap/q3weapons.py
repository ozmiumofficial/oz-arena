"""Quake 3 weapon models (frame 0) as triangle soups in their tag_weapon space, read from pak0."""
import struct, zipfile
import numpy as np

WEAPONS = {"machinegun": "machinegun/machinegun", "shotgun": "shotgun/shotgun", "rocket": "rocketl/rocketl",
           "lightning": "lightning/lightning", "railgun": "railgun/railgun", "plasma": "plasma/plasma",
           "grenade": "grenadel/grenadel", "bfg": "bfg/bfg", "gauntlet": "gauntlet/gauntlet"}


def md3_mesh(data):
    """-> (verts (n, 3), tris (m, 3)) of frame 0, every surface"""
    _, _, _, _, nf, nt, ns, _, _, ot, osf, _ = struct.unpack_from("<4si64siiiiiiiii", data, 0)
    o, P, T = osf, [], []
    base = 0
    for _ in range(ns):
        nsv = struct.unpack_from("<i", data, o + 80)[0]
        ntri = struct.unpack_from("<i", data, o + 84)[0]
        otri = struct.unpack_from("<i", data, o + 88)[0]
        ov = struct.unpack_from("<i", data, o + 100)[0]
        oe = struct.unpack_from("<i", data, o + 104)[0]
        v = np.frombuffer(data, "<i2", nsv * 4, o + ov).reshape(-1, 4)[:, :3] / 64.0
        t = np.frombuffer(data, "<i4", ntri * 3, o + otri).reshape(-1, 3)
        P.append(v)
        T.append(t + base)
        base += nsv
        o += oe
    return np.concatenate(P), np.concatenate(T)


def load(pak0, which):
    z = zipfile.ZipFile(pak0)
    return md3_mesh(z.read("models/weapons2/%s.md3" % WEAPONS[which]))
