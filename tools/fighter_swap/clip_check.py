"""How much the fighter's body parts sink into each other across doom's animations.

python -I clip_check.py oz_rig.iqm mech_segments.npz baseq3_pak0 [obj_dir]

For every legs+torso frame pair the game shows while moving and fighting, each body segment gets a "core" capsule
(its axis, and the radius half its own vertices fall within); a vertex of another segment inside that core is sunk
deep into it. Prints the worst pairs and a total; with obj_dir, writes OBJs of the worst frames for rendering.
"""
import sys, os, re, zipfile
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from iqm_io import Iqm

iqm_path, seg_path, pak = sys.argv[1:4]
obj_dir = sys.argv[4] if len(sys.argv) > 4 else None
ANIMS = ["BOTH_DEATH1", "BOTH_DEAD1", "BOTH_DEATH2", "BOTH_DEAD2", "BOTH_DEATH3", "BOTH_DEAD3",
         "TORSO_GESTURE", "TORSO_ATTACK", "TORSO_ATTACK2", "TORSO_DROP", "TORSO_RAISE", "TORSO_STAND", "TORSO_STAND2",
         "LEGS_WALKCR", "LEGS_WALK", "LEGS_RUN", "LEGS_BACK", "LEGS_SWIM", "LEGS_JUMP", "LEGS_LAND", "LEGS_JUMPB",
         "LEGS_LANDB", "LEGS_IDLE", "LEGS_IDLECR", "LEGS_TURN"]
SLOT = sys.argv[5] if len(sys.argv) > 5 else "doom"
cfg = zipfile.ZipFile(pak).read("models/players/%s/animation.cfg" % SLOT).decode("latin-1")
nums = []
for l in cfg.splitlines():
    t = l.split("//")[0].split()
    if len(t) >= 4 and all(re.fullmatch(r"-?\d+", x) for x in t[:4]):
        nums.append([int(x) for x in t[:4]])
an = {n: v for n, v in zip(ANIMS, nums)}
skip = an["LEGS_WALKCR"][0] - an["TORSO_GESTURE"][0]
q = Iqm(open(iqm_path, "rb").read())
sg = np.load(seg_path)
seg, names = sg["seg"], [str(x) for x in sg["names"]]
upper = int(q.comment.split("upper ")[1].split()[0])
jpart = q.comment.split("\njoints ")[1].split()[0]
tt = q.names.index("tag_torso")
invbind = np.linalg.inv(q.bind)
p4 = np.hstack([q.va[0], np.ones((len(q.va[0]), 1))])
w = q.va[5].astype(float) / 255
bi = q.va[4].astype(int)


def pose(lf, uf):
    L, U = q.frames[lf], q.frames[uf]
    base = L[tt] @ np.linalg.inv(U[tt])
    mats = np.array([L[j] if jpart[j] == "l" else base @ U[j] for j in range(len(q.names))])
    sk = mats @ invbind
    out = np.zeros((len(p4), 4))
    for c in range(4):
        out += w[:, c:c + 1] * np.einsum("vij,vj->vi", sk[bi[:, c]], p4)
    return out[:, :3]


def legs(n, k):
    return an[n][0] - skip + k


def torso(n, k):
    return upper + an[n][0] + k


frames = []
for n in ("LEGS_RUN", "LEGS_WALK", "LEGS_BACK", "LEGS_JUMP", "LEGS_LAND", "LEGS_WALKCR", "LEGS_IDLE", "LEGS_TURN"):
    for k in range(an[n][1]):
        frames.append(("%s%d+STAND" % (n[5:].lower(), k), legs(n, k), torso("TORSO_STAND", 0)))
for n in ("TORSO_ATTACK", "TORSO_ATTACK2", "TORSO_GESTURE", "TORSO_DROP", "TORSO_RAISE", "TORSO_STAND2"):
    for k in range(an[n][1]):
        frames.append(("run%d+%s%d" % (k % an["LEGS_RUN"][1], n[6:].lower(), k),
                       legs("LEGS_RUN", k % an["LEGS_RUN"][1]), torso(n, k)))

S = len(names)
ADJ = {("head", "torso")}
for s in "LR":
    for a, b in (("torso", "upperarm"), ("upperarm", "forearm"), ("forearm", "hand"), ("torso", "thigh"),
                 ("thigh", "shin"), ("shin", "foot")):
        ADJ.add(tuple(sorted((a if a in ("torso", "head") else a + "_" + s, b + "_" + s))))
totals = np.zeros((S, S))
per_frame = []
for name, lf, uf in frames:
    P = pose(lf, uf)
    count = 0
    for b in range(S):
        pb = P[seg == b]
        if len(pb) < 20:
            continue
        c = pb.mean(0)
        _, _, vt = np.linalg.svd(pb - c, full_matrices=False)
        ax = vt[0]
        t = (pb - c) @ ax
        lo, hi = np.percentile(t, [8, 92])
        rad = np.percentile(np.linalg.norm((pb - c) - t[:, None] * ax, axis=1), 50)
        for a in range(S):
            if a == b:
                continue
            pa = P[seg == a]
            ta = np.clip((pa - c) @ ax, lo, hi)
            dist = np.linalg.norm(pa - (c + ta[:, None] * ax), axis=1)
            inside = int((dist < rad).sum())
            totals[a, b] += inside
            count += inside
    per_frame.append((count, name, lf, uf))
nfr = len(frames)
print("frames checked:", nfr)
pairs = sorted(((totals[a, b] / nfr, names[a], names[b]) for a in range(S) for b in range(S) if totals[a, b] > 0),
               reverse=True)
print("worst: avg verts of A deep inside B per frame")
for v, a, b in pairs[:14]:
    print("  %7.1f  %-11s in %s" % (v, a, b))
print("TOTAL avg sunk verts per frame: %.1f" % (totals.sum() / nfr))
per_frame.sort(reverse=True)
print("worst frames:", [(n, c) for c, n, _, _ in per_frame[:6]])
if obj_dir:
    os.makedirs(obj_dir, exist_ok=True)
    open(os.path.join(obj_dir, "mech.mtl"), "w").write("newmtl mech\nKd 1 1 1\n")
    picks = [per_frame[0]] + [x for x in per_frame if x[1].startswith("run")][:3:2] + \
            [next(x for x in per_frame if x[1].startswith("back"))]
    for c, n, lf, uf in picks:
        P = pose(lf, uf)
        with open(os.path.join(obj_dir, n + ".obj"), "w") as f:
            f.write("mtllib mech.mtl\nusemtl mech\n")
            f.writelines("v %.4f %.4f %.4f\n" % tuple(p) for p in P)
            f.writelines("vt %.5f %.5f\n" % (t[0], 1 - t[1]) for t in q.va[1])
            f.writelines("f %d/%d %d/%d %d/%d\n" % (z, z, y, y, x, x) for x, y, z in q.tris + 1)
