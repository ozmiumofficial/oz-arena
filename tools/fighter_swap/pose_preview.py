"""Skin an Oz fighter rig at a legs frame + a torso frame (as the cgame pairs them) and write OBJs.
python -I pose_preview.py oz_rig.iqm baseq3_pak0 out_dir"""
import sys, os, re, zipfile
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from iqm_io import Iqm

iqm_path, pak, out = sys.argv[1], sys.argv[2], sys.argv[3]
ANIMS = ["BOTH_DEATH1", "BOTH_DEAD1", "BOTH_DEATH2", "BOTH_DEAD2", "BOTH_DEATH3", "BOTH_DEAD3",
         "TORSO_GESTURE", "TORSO_ATTACK", "TORSO_ATTACK2", "TORSO_DROP", "TORSO_RAISE", "TORSO_STAND", "TORSO_STAND2",
         "LEGS_WALKCR", "LEGS_WALK", "LEGS_RUN", "LEGS_BACK", "LEGS_SWIM", "LEGS_JUMP", "LEGS_LAND", "LEGS_JUMPB",
         "LEGS_LANDB", "LEGS_IDLE", "LEGS_IDLECR", "LEGS_TURN"]
SLOT = sys.argv[5] if len(sys.argv) > 5 else "doom"
cfg = zipfile.ZipFile(pak).read("models/players/%s/animation.cfg" % SLOT).decode("latin-1")
nums = [[int(x) for x in l.split("//")[0].split()[:4]] for l in cfg.splitlines()
        if len(l.split("//")[0].split()) >= 4 and all(re.fullmatch(r"-?\d+", x) for x in l.split("//")[0].split()[:4])]
an = {n: v for n, v in zip(ANIMS, nums)}
skip = an["LEGS_WALKCR"][0] - an["TORSO_GESTURE"][0]
q = Iqm(open(iqm_path, "rb").read())
upper = int(q.comment.split("upper ")[1].split()[0])
jpart = q.comment.split("\njoints ")[1].split()[0]
tt = q.names.index("tag_torso")


def legs_frame(name, k):
    return an[name][0] - (skip if name.startswith("LEGS") else 0) + k


def torso_frame(name, k):
    return upper + an[name][0] + k


def pose(lf, uf):
    L, U = q.frames[lf], q.frames[uf]
    base = L[tt] @ np.linalg.inv(U[tt])
    mats = np.array([L[j] if jpart[j] == "l" else base @ U[j] for j in range(len(q.names))])
    sk = mats @ np.linalg.inv(q.bind)
    p4 = np.hstack([q.va[0], np.ones((len(q.va[0]), 1))])
    w = q.va[5].astype(float) / 255
    out_ = np.zeros((len(p4), 4))
    for c in range(4):
        out_ += w[:, c:c + 1] * np.einsum("vij,vj->vi", sk[q.va[4][:, c].astype(int)], p4)
    return out_[:, :3], mats[q.names.index("tag_weapon")]


WEAPON = sys.argv[4] if len(sys.argv) > 4 else "rocket"
import q3weapons
GP, GT = q3weapons.load(pak, WEAPON)


def write_obj(path, P, gun):
    uv = q.va[1]
    with open(path, "w") as f:
        f.write("mtllib mech.mtl\no mech\nusemtl mech\n")
        for p in P:
            f.write("v %.4f %.4f %.4f\n" % tuple(p))
        for t in uv:
            f.write("vt %.5f %.5f\n" % (t[0], 1 - t[1]))
        for a, b, c in q.tris + 1:
            f.write("f %d/%d %d/%d %d/%d\n" % (c, c, b, b, a, a))
        # the real weapon model where the game hangs it: tag_weapon
        n = len(P)
        f.write("o gun\nusemtl gun\n")
        for p in GP @ gun[:3, :3].T + gun[:3, 3]:
            f.write("v %.4f %.4f %.4f\n" % tuple(p))
        for a, b, c in GT + 1 + n:
            f.write("f %d %d %d\n" % (a, b, c))


os.makedirs(out, exist_ok=True)
open(os.path.join(out, "mech.mtl"), "w").write("newmtl mech\nKd 1 1 1\nnewmtl gun\nKd 0.9 0.15 0.1\n")
nrun = an["LEGS_RUN"][1]
ST = torso_frame("TORSO_STAND", 0)
shots = {"a_stand": (legs_frame("LEGS_IDLE", 0), ST),
         "b_run0": (legs_frame("LEGS_RUN", 0), ST),
         "c_run2": (legs_frame("LEGS_RUN", 2), ST),
         "d_run4": (legs_frame("LEGS_RUN", 4), ST),
         "e_run6": (legs_frame("LEGS_RUN", 6), ST),
         "f_walk3": (legs_frame("LEGS_WALK", 3), ST),
         "g_back3": (legs_frame("LEGS_BACK", 3), ST),
         "h_crouch": (legs_frame("LEGS_WALKCR", 2), ST),
         "i_jump5": (legs_frame("LEGS_JUMP", 5), ST),
         "j_gauntlet": (legs_frame("LEGS_IDLE", 0), torso_frame("TORSO_ATTACK2", 2)),
         "k_taunt": (legs_frame("LEGS_IDLE", 0), torso_frame("TORSO_GESTURE", 14)),
         "l_drop": (legs_frame("LEGS_IDLE", 0), torso_frame("TORSO_DROP", 4))}
for name, fr in shots.items():
    if fr is None:
        P, gun = q.va[0], q.bind[q.names.index("tag_weapon")]
    else:
        P, gun = pose(*fr)
    write_obj(os.path.join(out, name + ".obj"), P, gun)
    print(name, fr, P.min(0).round(1), P.max(0).round(1))
