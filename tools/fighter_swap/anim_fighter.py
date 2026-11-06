"""An upgraded fighter as an Oz Arena rig: its own skeleton and hand-built animations in the frame slots of the id
model it replaces (models/players/<slot>/oz_rig.iqm).

python anim_fighter.py fighters/<name>.json mesh.npz proxy_weights.npz baseq3/pak0.pk3 out_dir

- Skeleton: skeleton.py from the fighter's config, at its own joints. Bind pose = its T-pose, in id units.
- Skin: Blender heat weights (rig_weights.py, on a watertight voxel copy) moved onto the mesh by nearest proxy points.
- Animation: every lower.md3 / upper.md3 frame the slot's animation.cfg names gets a pose built here: gait cycles with
  planted feet and two-bone leg IK (or, for a board rider, the feet on a hovering board), a two-handed hold built from
  the gun outward (right palm on the grip, left under the fore-end), recoil, gauntlet, weapon drop/raise, taunt, falls.
  Where two leg animations claim the same frames in an id cfg (doom's LEGS_BACK and LEGS_SWIM), the first keeps them.
- A walker (orbb: an eye on two arms, skeleton.py) walks on its arms with the leg gaits, elbows back; its gun sits in
  the mount on top, which is the torso: it turns and pitches with the aim, kicks on recoil, lunges with the gauntlet
  and tips the gun away on a weapon switch. tag_weapon hangs from the mount's cradle.
- Tags, spine, ragdoll and the IQM itself come from tools/make_fighters.py, so the engine reads it like its own rigs.
"""
import sys, os, math, zipfile
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import skeleton
TOOLS = os.path.dirname(HERE)
sys.path.insert(0, TOOLS)
_argv = sys.argv
sys.argv = ["make_fighters.py"]
import make_fighters as mf
sys.argv = _argv

fighter_json, mesh_npz, proxy_npz, pak0, out = sys.argv[1:6]
SK = skeleton.load(fighter_json)
JOINTS, DEFORM, SLOT = SK.JOINTS, SK.DEFORM, SK.slot
BOARD = SK.board is not None
WALKER = SK.walker
KNEE_DIR = SK.cfg.get("knee_dir", 1)       # -1: the knees (a walker's elbows) bend backward
K = 62.0
A = np.array([[0, -1, 0], [1, 0, 0], [0, 0, 1.0]])   # Blender (x his left, -y front, z up) -> id (x front, y left, z up)
FLOOR = -24.0
SOLE = FLOOR + K * SK.sole_z          # what the feet stand on: the floor, or the top of a board
MATERIAL, MESH_NAME = "models/players/%s/oz_skin" % SLOT, "oz_skin"


def to_id(p):
    return K * (np.asarray(p, float) @ A.T) + np.array([0, 0, FLOOR])


def nrm(v):
    v = np.asarray(v, float)
    return v / max(np.linalg.norm(v), 1e-9)


def rot(axis, deg):
    a = nrm(axis)
    t = math.radians(deg)
    k = np.array([[0, -a[2], a[1]], [a[2], 0, -a[0]], [-a[1], a[0], 0]])
    return np.eye(3) + math.sin(t) * k + (1 - math.cos(t)) * k @ k


def ypr(yaw, pitch, roll):
    """yaw about z (+ turns left), pitch about y (+ tips forward/down), roll about x (+ raises the left side)."""
    return rot((0, 0, 1), yaw) @ rot((0, 1, 0), pitch) @ rot((1, 0, 0), roll)


def frame(d, u):
    d = nrm(d)
    u = nrm(np.asarray(u, float) - (np.dot(u, d)) * d)
    return np.column_stack([d, u, np.cross(d, u)])


def aim(rest_d, rest_u, d, u):
    return frame(d, u) @ frame(rest_d, rest_u).T


def matrix_quat(m):
    return mf.matrix_quat(m)


def untwist(R, axis, keep):
    """R with its twist about axis cut to the fraction keep (swing-twist split)"""
    q = matrix_quat(R)                      # x, y, z, w
    p = (q[:3] @ axis) * axis
    tw = np.array([p[0], p[1], p[2], q[3]])
    n = np.linalg.norm(tw)
    if n < 1e-9:
        return R
    tw /= n
    ang = 2 * math.atan2(np.linalg.norm(tw[:3]), tw[3])
    a = axis if tw[:3] @ axis >= 0 else -axis
    if ang > math.pi:
        ang -= 2 * math.pi
    # R = swing @ twist: keep the swing, scale the twist
    return R @ rot(a, math.degrees(ang)).T @ rot(a, math.degrees(ang) * keep)


def ik2(S, T, a, b, pole):
    v = T - S
    dist = np.linalg.norm(v)
    dv = v / max(dist, 1e-9)
    dist = float(np.clip(dist, abs(a - b) + 0.05, (a + b) * 0.995))
    ca = (a * a + dist * dist - b * b) / (2 * a * dist)
    sa = math.sqrt(max(0.0, 1 - ca * ca))
    perp = nrm(np.asarray(pole, float) - np.dot(pole, dv) * dv)
    return S + a * (ca * dv + sa * perp), S + dist * dv


# ------------------------------------------------------------------ skeleton
names = [j[0] for j in JOINTS] + ["tag_torso", "tag_head", "tag_weapon"]
ix = {n: i for i, n in enumerate(names)}
parents = [ix[j[1]] if j[1] else -1 for j in JOINTS] + [ix["pelvis"], ix["head"], ix["chest" if WALKER else "hand_R"]]
jpart = "".join(j[2] for j in JOINTS) + "lhu"
BP = {j[0]: to_id(j[3]) for j in JOINTS}
REST = {j[0]: to_id(j[4]) - to_id(j[3]) for j in JOINTS}
LEN = {n: float(np.linalg.norm(REST[n])) for n in REST}
_flat = lambda p: np.array([p[0], p[1], SOLE])
HEEL = {s: _flat((-K * SK.heel_y, BP["foot_" + s][1])) for s in "LR"}      # heel_y: the heel's Blender y
TIP = {s: _flat(to_id(next(j[4] for j in JOINTS if j[0] == "toe_" + s))) for s in "LR"}
BALL = {s: _flat(BP["toe_" + s]) for s in "LR"}
STANCE_Z = BP["pelvis"][2] - (3.0 if BOARD else 1.5)      # knees a little bent; more on a board
HIP_Y = abs(BP["thigh_L"][1] - BP["thigh_R"][1]) / 2
print("bone lengths: thigh %.1f calf %.1f upperarm %.1f forearm %.1f hand %.1f; pelvis z %.1f" % (
    LEN["thigh_L"], LEN["calf_L"], LEN.get("upperarm_L", 0), LEN.get("forearm_L", 0), LEN.get("hand_L", 0),
    BP["pelvis"][2]))
if WALKER:
    # the gun in the cradle: an id weapon's grip sits about 4.5 behind and 3 below tag_weapon (q3weapons.py), so the
    # tag goes that far ahead of and above the cradle's cup; "gun_offset" in the config nudges it (id units)
    TAGW = to_id(SK.cfg["mount"]["cup"]) + np.array([4.5, 0.0, 3.0]) + np.array(SK.cfg.get("gun_offset", [0.0, 0.0, 0.0]))
    HAND_Y = BP["foot_L"][1]


def M(R, p):
    m = np.eye(4)
    m[:3, :3], m[:3, 3] = R, p
    return m


# ------------------------------------------------------------------ posing
def pose(lo, up):
    """lo: legs params, up: torso params -> {joint: 4x4 world}"""
    W = {"root": M(np.eye(3), BP["root"])}
    Rp = ypr(lo["pyaw"], lo["ppitch"], lo["proll"])
    pp = np.array([lo["px"], lo["py"], lo["pz"]])
    W["pelvis"] = M(Rp, pp)
    if BOARD:
        # the board hovers on its own; "onboard" says how much the feet stay where they stand on it
        Rb = ypr(lo["byaw"], lo["bpitch"], lo["broll"])
        pb = BP["board"] + np.array([lo["bx"], lo["by"], lo["bz"]])
        W["board"] = M(Rb, pb)
    for s, sg in (("L", 1), ("R", -1)):
        H = pp + Rp @ (BP["thigh_" + s] - BP["pelvis"])
        Rf = ypr(lo[s + "yaw"], lo[s + "pitch"], 0)
        contacts = [Rf @ (c - BP["foot_" + s]) for c in (HEEL[s], BALL[s], TIP[s])]
        az = SOLE + lo[s + "lift"] - min(c[2] for c in contacts)
        T = np.array([lo[s + "x"], lo[s + "y"], az])
        if BOARD and lo["onboard"] > 0:
            on = lo["onboard"]
            Tb = pb + Rb @ (BP["foot_" + s] - BP["board"])
            T = on * Tb + (1 - on) * T
            Rf = Rb if on >= 0.5 else Rf
        pole = Rp @ nrm((KNEE_DIR, sg * lo["kneeout"], 0.0))
        Kn, T2 = ik2(H, T, LEN["thigh_" + s], LEN["calf_" + s], pole)
        # the knee's hinge axis, to the left for a knee bending forward: a backward bend (a walker's elbow) turns
        # the cross product round, so flip it back, or the thigh and calf spin half a turn about their length
        hinge = KNEE_DIR * np.cross(Kn - H, T2 - Kn)
        hinge = nrm(hinge) if np.linalg.norm(hinge) > 1e-3 else Rp @ np.array([0, 1.0, 0])
        Rt = aim(REST["thigh_" + s], (0, 1, 0), Kn - H, hinge)
        Rc = aim(REST["calf_" + s], (0, 1, 0), T2 - Kn, hinge)
        Rtoe = Rf if (BOARD and lo["onboard"] >= 0.5) else ypr(lo[s + "yaw"], lo[s + "toe"], 0)
        if WALKER:      # a hand on the floor rolls with its forearm: keep only a little of the wrist's twist
            Rf2 = Rc @ untwist(Rc.T @ Rf, nrm(REST["calf_" + s]), 0.25)
            Rtoe = Rf2 @ Rf.T @ Rtoe
            Rf = Rf2
        W["thigh_" + s] = M(Rt, H)
        W["calf_" + s] = M(Rc, Kn)
        W["foot_" + s] = M(Rf, T2)
        W["toe_" + s] = M(Rtoe, T2 + Rf @ (BP["toe_" + s] - BP["foot_" + s]))
    # the torso hangs from tag_torso (pelvis rotation, at the spine's bind point); its pose is in torso space,
    # where the bind pose sits as built
    # the torso keeps a third of a small hip lean (running, crouching) so the gun stays near level; big tilts
    # (falls) carry it all
    lean = lambda a: a * (0.35 + 0.65 * float(np.clip((abs(a) - 20.0) / 25.0, 0.0, 1.0)))
    Rtag = Rp if WALKER else ypr(lo["pyaw"], lean(lo["ppitch"]), lean(lo["proll"]))      # a walker's mount sits on its body
    ptag = pp + Rp @ (BP["spine"] - BP["pelvis"])
    W["tag_torso"] = M(Rtag, ptag)

    def put(n, R, p):            # torso space -> world
        W[n] = M(Rtag @ R, Rtag @ (p - BP["spine"]) + ptag)
    Rs = ypr(up["syaw"], up["spitch"], up["sroll"])
    ps = BP["spine"]
    Rch = Rs @ ypr(up["cyaw"], up["cpitch"], up["croll"])
    pch = ps + Rs @ (BP["chest"] - BP["spine"])
    if WALKER:
        put("spine", Rs, ps)
        put("chest", Rch, pch)
        W["head"] = W["pelvis"] @ np.linalg.inv(M(np.eye(3), BP["pelvis"])) @ M(np.eye(3), BP["head"])
        W["tag_head"] = W["head"].copy()
        G = M(ypr(up["gyaw"], up["gpitch"], up["groll"]), TAGW + np.array([up["gx"], up["gy"], up["gz"]]))
        W["tag_weapon"] = W["chest"] @ np.linalg.inv(M(np.eye(3), BP["chest"])) @ G
        return W, {"R": 0.0, "L": 0.0}
    Rn = Rch @ ypr(0, up["npitch"], 0)
    pn = pch + Rch @ (BP["neck"] - BP["chest"])
    ph = pn + Rn @ (BP["head"] - BP["neck"])
    put("spine", Rs, ps)
    put("chest", Rch, pch)
    put("neck", Rn, pn)
    put("head", Rn, ph)
    # hands from the gun: the gun's frame in torso space, the right hand always on its grip, the left on its
    # fore-end as much as "lon" says (else its own guard pose)
    G = M(ypr(up["gyaw"], up["gpitch"], up["groll"]), np.array([up["gx"], up["gy"], up["gz"]]))
    target = {}
    HR = G @ HG["R"]
    target["R"] = (HR[:3, 3], HR[:3, :3])
    HL = G @ HG["L"]
    lon = up["lon"]
    free_p = np.array([up["Lwx"], up["Lwy"], up["Lwz"]])
    hd = lon * (HL[:3, :3] @ nrm(REST["hand_L"])) + (1 - lon) * nrm((up["Lhx"], up["Lhy"], up["Lhz"]))
    bu = lon * (HL[:3, :3] @ np.array([0, 0, 1.0])) + (1 - lon) * nrm((up["Lbx"], up["Lby"], up["Lbz"]))
    target["L"] = (lon * HL[:3, 3] + (1 - lon) * free_p, aim(REST["hand_L"], (0, 0, 1), hd, bu))
    TS = {}
    for s, sg in (("L", 1), ("R", -1)):
        Rcl = Rch @ ypr(-sg * up[s + "protract"], 0, sg * up[s + "shrug"])
        pcl = pch + Rch @ (BP["clavicle_" + s] - BP["chest"])
        S = pcl + Rcl @ (BP["upperarm_" + s] - BP["clavicle_" + s])
        Wt, Rh = target[s]
        pole = np.array([up[s + "ex"], up[s + "ey"], up[s + "ez"]])
        E, W2 = ik2(S, Wt, LEN["upperarm_" + s], LEN["forearm_" + s], pole)
        hr = nrm(np.cross(REST["upperarm_" + s], (1, 0, 0)))
        hinge = np.cross(E - S, W2 - E)
        hinge = nrm(hinge) if np.linalg.norm(hinge) > 1e-2 else nrm(np.cross(E - S, pole))
        Ru = aim(REST["upperarm_" + s], hr, E - S, hinge)
        Rfa = aim(REST["forearm_" + s], hr, W2 - E, hinge)
        # the hand's roll about the forearm (pronation) is shared: half in the forearm, half at the wrist, so
        # neither the wrist nor the elbow wrings
        ax = nrm(REST["forearm_" + s])
        q = matrix_quat(Rfa.T @ Rh)
        twist = 2 * math.degrees(math.atan2(float(q[:3] @ ax), q[3]))
        twist = (twist + 180) % 360 - 180
        Rfa = Rfa @ rot(ax, twist * 0.5)
        put("clavicle_" + s, Rcl, pcl)
        put("upperarm_" + s, Ru, S)
        put("forearm_" + s, Rfa, E)
        put("hand_" + s, Rh, W2)
        TS[s] = float(np.linalg.norm(Wt - W2))          # how far the hand fell short of its target
    W["tag_head"] = W["head"].copy()
    W["tag_weapon"] = W["hand_R"] @ np.linalg.inv(HG["R"])
    return W, TS


# ------------------------------------------------------------------ the hands on the gun
# Every Quake III weapon hangs from tag_weapon with its barrel along +x and its pistol grip about 4.5 units behind
# the tag and below it (q3weapons.py measures them). A hand's place on the gun, in the gun's frame: where the palm
# touches, which way the fingers point, which way the back of the hand faces.
def hand_in_gun(s, palm, fingers, back):
    if WALKER:
        return np.eye(4)
    R = aim(REST["hand_" + s], (0, 0, 1), fingers, back)
    touch = 2.8 * nrm(REST["hand_" + s]) + np.array([0, 0, -1.3])     # palm centre, from the wrist, T-pose
    return M(R, np.asarray(palm, float) - R @ touch)


HG = {"R": hand_in_gun("R", (-4.4, -1.7, -2.8), (1.0, 0.15, -0.5), (0.15, -1.0, 0.3)),     # wrapped on the grip
      "L": hand_in_gun("L", (5.0, 0.8, -3.2), (0.55, -0.8, 0.12), (0.1, 0.35, -1.0))}     # cupped under the fore-end


# ------------------------------------------------------------------ the poses
def legs_base(**kw):
    d = dict(px=0.0, py=0.0, pz=STANCE_Z, pyaw=0.0, ppitch=0.0, proll=0.0, kneeout=0.12,
             Lx=2.0, Ly=HIP_Y + 0.5, Llift=0.0, Lpitch=0.0, Ltoe=0.0, Lyaw=10.0,
             Rx=-2.5, Ry=-HIP_Y - 0.5, Rlift=0.0, Rpitch=0.0, Rtoe=0.0, Ryaw=-14.0,
             onboard=1.0 if BOARD else 0.0, bx=0.0, by=0.0, bz=0.0, byaw=0.0, bpitch=0.0, broll=0.0)
    d.update(kw)
    return d


def board_legs(name, k, n):
    """A board rider: the feet stay where they stand on the board, the board hovers, bobs, sways and tilts into the
    ride, the hips sit low with the knees bent; jumps tuck the board up under him."""
    phi = k / n
    w = 2 * math.pi * phi

    def ride(lean, tilt, sway, hover, bob, crouch=0.0, **kw):
        bz = hover + bob * math.sin(2 * w)
        return legs_base(ppitch=lean, bpitch=tilt, broll=sway * math.sin(w), bz=bz,
                         pz=STANCE_Z - crouch + bz * 0.9, pyaw=0.4 * sway * math.sin(w), **kw)
    if name == "LEGS_IDLE":
        return ride(0, 1.0 * math.cos(w), 1.5, 1.0, 0.3)
    if name == "LEGS_RUN":
        return ride(10, 7, 4, 1.6, 0.6, crouch=1.5, byaw=3 * math.sin(w))
    if name == "LEGS_WALK":
        return ride(5, 4, 2.5, 1.2, 0.4)
    if name == "LEGS_BACK":
        return ride(-5, -5, 2, 1.2, 0.4)
    if name == "LEGS_WALKCR":
        return ride(14, 5, 2, 0.8, 0.3, crouch=9, kneeout=0.3)
    if name == "LEGS_IDLECR":
        return ride(10, 0, 1, 0.6, 0.2, crouch=9, kneeout=0.3)
    if name == "LEGS_TURN":
        return legs_base(byaw=10 * math.sin(w), pyaw=6 * math.sin(w), bz=1.0, pz=STANCE_Z + 0.9)
    if name == "LEGS_SWIM":
        return legs_base(ppitch=20, bpitch=20, bz=2.0, pz=STANCE_Z + 1.8)
    if name in ("LEGS_JUMP", "LEGS_JUMPB"):
        b = -1 if name == "LEGS_JUMPB" else 1
        keys = [(0.0, legs_base(pz=STANCE_Z - 4)),
                (0.2, legs_base(pz=STANCE_Z + 1, bz=1.5, bpitch=-6 * b)),
                (0.5, legs_base(pz=STANCE_Z + 2.5, bz=7.0, bpitch=10 * b, ppitch=-2 * b, kneeout=0.2)),
                (1.0, legs_base(pz=STANCE_Z + 2.5, bz=8.0, bpitch=8 * b, ppitch=-3 * b, kneeout=0.2))]
        return keyed(keys, phi)
    if name in ("LEGS_LAND", "LEGS_LANDB"):
        b = -1 if name == "LEGS_LANDB" else 1
        keys = [(0.0, legs_base(pz=STANCE_Z + 2, bz=6.0, bpitch=6 * b)),
                (0.2, legs_base(pz=STANCE_Z - 1, bz=0.5)),
                (0.4, legs_base(pz=STANCE_Z - 5, bz=0.0, ppitch=8 * b, kneeout=0.2)),
                (0.7, legs_base(pz=STANCE_Z - 2, bz=0.8)),
                (1.0, legs_base(pz=STANCE_Z + 0.9, bz=1.0))]
        return keyed(keys, phi)
    return None


TORSO0 = dict(syaw=-8.0, spitch=0.0, sroll=0.0, cyaw=-12.0, cpitch=0.0, croll=0.0, npitch=0.0,
              Lprotract=14.0, Lshrug=0.0, Rprotract=8.0, Rshrug=2.0,
              # the gun: ahead of his chest, a little right of centre, level; a rifleman's stance, the left
              # shoulder turned forward 20 degrees so the left hand reaches the fore-end
              gx=14.5, gy=-2.5, gz=18.0, gyaw=0.0, gpitch=0.0, groll=0.0, lon=1.0,
              Rex=-0.3, Rey=-0.85, Rez=-0.45,
              # the left hand when it's off the gun: a guard in front of the chest
              Lwx=10.0, Lwy=5.0, Lwz=19.0, Lhx=0.55, Lhy=-0.8, Lhz=0.1, Lbx=0.15, Lby=0.35, Lbz=1.0,
              Lex=-0.2, Ley=0.65, Lez=-0.75)
TORSO0.update(SK.cfg.get("hold", {}))      # a fighter's own tweaks to the gun hold


def torso(**kw):
    d = dict(MOUNT0 if WALKER else TORSO0)
    d.update(kw)
    return d


def lerp(a, b, t):
    return {k: a[k] + (b[k] - a[k]) * t for k in a}


def keyed(keys, t):
    """keys: [(time 0..1, params)] -> params at t (smoothstep between keys)"""
    if t <= keys[0][0]:
        return dict(keys[0][1])
    for (t0, a), (t1, b) in zip(keys, keys[1:]):
        if t <= t1:
            u = (t - t0) / max(t1 - t0, 1e-9)
            return lerp(a, b, u * u * (3 - 2 * u))
    return dict(keys[-1][1])


def gait(phi, S, H, duty, lean, bob, yawamp, rollamp, width, base=STANCE_Z, splay=7.0, back=False, kneeout=0.12):
    d = legs_base(pz=base, ppitch=lean, kneeout=kneeout)
    for s, off, sg in (("L", 0.0, 1), ("R", 0.5, -1)):
        t = (phi + off) % 1.0
        if t < duty:                                   # planted: slides back under him, heel peels off at the end
            u = t / duty
            x = S / 2 - S * u
            lift = 0.0
            pitch = 0.0 if back else 32.0 * max(0.0, (u - 0.6) / 0.4) ** 1.6
            toe = 0.0
        else:                                          # swing: up and forward, foot flexes for the next contact
            u = (t - duty) / (1 - duty)
            e = 0.5 - 0.5 * math.cos(math.pi * u)
            x = -S / 2 + S * e
            lift = H * math.sin(math.pi * u ** 0.8)
            pitch = (12.0 * math.sin(math.pi * u)) if back else (32.0 * (1 - u) ** 1.5 - 12.0 * u * u)
            toe = pitch * 0.4
        if back:
            x = -x
        d[s + "x"], d[s + "y"], d[s + "lift"], d[s + "pitch"], d[s + "toe"] = x, sg * width, lift, pitch, toe
        d[s + "yaw"] = sg * splay
    d["pz"] = base + bob * math.cos(4 * math.pi * (phi - duty / 2))
    d["pyaw"] = -yawamp * math.cos(2 * math.pi * phi) * (-1 if back else 1)
    d["proll"] = rollamp * math.cos(2 * math.pi * (phi - duty / 2))
    return d


def legs_anim(name, k, n):
    if BOARD and name.startswith("LEGS"):
        return board_legs(name, k, n)
    if WALKER:
        return walker_legs(name, k, n)
    return human_legs(name, k, n)


def walker_legs(name, k, n):
    """A walker on its arms: the humans' gaits with the hands wide under the shoulders (which sit behind the body's
    middle), a shorter, quicker stride, the body rocking over the planted hand."""
    phi = k / n
    W0 = HAND_Y
    if name == "LEGS_RUN":
        d = gait(phi, 26, 7.0, 0.4, 4, -1.0, 7, 5, W0, splay=10)
    elif name == "LEGS_WALK":
        d = gait(phi, 18, 4.0, 0.6, 2, 0.5, 5, 4, W0, splay=10)
    elif name == "LEGS_BACK":
        d = gait(phi, 16, 4.5, 0.55, -2, 0.5, 4, 3, W0, back=True, splay=10)
    elif name == "LEGS_WALKCR":
        d = gait(phi, 13, 3.0, 0.6, 6, 0.3, 4, 3, W0 + 1.5, base=STANCE_Z - 8, splay=14, kneeout=0.3)
    elif name == "LEGS_IDLE":
        a = math.sin(2 * math.pi * phi)
        d = legs_base(pz=STANCE_Z + 0.5 * a, proll=1.2 * a, pyaw=1.5 * math.sin(4 * math.pi * phi),
                      Ly=W0, Ry=-W0, Lx=1.0, Rx=-1.0, Lyaw=12.0, Ryaw=-12.0)
    elif name == "LEGS_IDLECR":
        d = legs_base(pz=STANCE_Z - 8 + 0.3 * math.sin(2 * math.pi * phi), ppitch=6, kneeout=0.3,
                      Ly=W0 + 1.5, Ry=-W0 - 1.5, Lx=2.0, Rx=-1.0, Lyaw=16.0, Ryaw=-16.0)
    elif name == "LEGS_TURN":
        d = legs_base(Ly=W0, Ry=-W0)
        a = math.sin(2 * math.pi * phi)
        d["Llift"], d["Rlift"] = max(0, a) * 3.5, max(0, -a) * 3.5
        d["pyaw"] = 5 * a
    elif name == "LEGS_SWIM":                  # paddling, the hands reaching forward in turn
        a = math.sin(2 * math.pi * phi)
        d = legs_base(ppitch=15, pz=STANCE_Z + 4, Lx=4 + 6 * a, Rx=4 - 6 * a, Ly=W0, Ry=-W0, Llift=8 + 4 * a,
                      Rlift=8 - 4 * a, Lpitch=-30, Rpitch=-30, kneeout=0.2)
    else:                                      # jumps, landings and deaths: the humans', hands set wide
        d = human_legs(name, k, n)
        d["Ly"] += W0 - HIP_Y - 0.5
        d["Ry"] -= W0 - HIP_Y - 0.5
    d["Lx"] += BP["foot_L"][0]
    d["Rx"] += BP["foot_R"][0]
    d["px"] += BP["pelvis"][0]
    return d


def human_legs(name, k, n):
    phi = k / n
    if name == "LEGS_RUN":
        return gait(phi, 30, 9.0, 0.36, 7, -1.0, 8, 3, 3.6)
    if name == "LEGS_WALK":
        return gait(phi, 21, 4.5, 0.62, 2, 0.6, 6, 2.5, 4.4)
    if name == "LEGS_BACK":
        return gait(phi, 19, 5.0, 0.55, -3, 0.5, 5, 2, 4.6, back=True)
    if name == "LEGS_WALKCR":
        return gait(phi, 15, 3.5, 0.6, 16, 0.4, 5, 2, 6.0, base=STANCE_Z - 10, splay=14, kneeout=0.35)
    if name == "LEGS_IDLE":
        return legs_base(pz=STANCE_Z + 0.35 * math.sin(2 * math.pi * phi), proll=0.6 * math.sin(2 * math.pi * phi))
    if name == "LEGS_IDLECR":
        return legs_base(pz=STANCE_Z - 10 + 0.3 * math.sin(2 * math.pi * phi), ppitch=12, kneeout=0.35,
                         Lx=4.0, Ly=6.8, Rx=-4.5, Ry=-6.8, Rpitch=22, Rtoe=0)
    if name == "LEGS_TURN":
        st = legs_base()
        a = math.sin(2 * math.pi * phi)
        st["Llift"], st["Rlift"] = max(0, a) * 3.0, max(0, -a) * 3.0
        st["pyaw"] = 4 * a
        return st
    if name == "LEGS_SWIM":
        a = math.sin(2 * math.pi * phi)
        return legs_base(ppitch=25, Lx=-9, Rx=-9, Ly=4.5, Ry=-4.5, Llift=6 + 4 * a, Rlift=6 - 4 * a,
                         Lpitch=60, Rpitch=60, Ltoe=60, Rtoe=60, kneeout=0.1)
    if name in ("LEGS_JUMP", "LEGS_JUMPB"):
        b = -1 if name == "LEGS_JUMPB" else 1
        keys = [(0.0, legs_base(pz=STANCE_Z - 4, ppitch=4 * b)),
                (0.15, legs_base(pz=STANCE_Z + 1, ppitch=2 * b, Lpitch=30, Rpitch=30, Ltoe=0, Rtoe=0)),
                (0.35, legs_base(pz=STANCE_Z + 1.5, ppitch=0, Llift=4, Rlift=3, Lpitch=35, Rpitch=35,
                                 Ltoe=35, Rtoe=35, Lx=1 + 2 * b, Rx=-3 + 2 * b)),
                (0.7, legs_base(pz=STANCE_Z + 2.5, ppitch=-3 * b, Llift=11, Rlift=9, Lpitch=15, Rpitch=20,
                                Ltoe=15, Rtoe=20, Lx=4 + 3 * b, Rx=-1 + 3 * b, kneeout=0.2)),
                (1.0, legs_base(pz=STANCE_Z + 2.5, ppitch=-3 * b, Llift=12, Rlift=10, Lpitch=10, Rpitch=15,
                                Ltoe=10, Rtoe=15, Lx=5 + 3 * b, Rx=0 + 3 * b, kneeout=0.2))]
        return keyed(keys, phi)
    if name in ("LEGS_LAND", "LEGS_LANDB"):
        b = -1 if name == "LEGS_LANDB" else 1
        keys = [(0.0, legs_base(pz=STANCE_Z + 1, Llift=4, Rlift=3, Lpitch=25, Rpitch=25, Ltoe=25, Rtoe=25,
                                Lx=2 + 2 * b, Rx=-2 + 2 * b)),
                (0.15, legs_base(pz=STANCE_Z - 2, ppitch=4 * b)),
                (0.35, legs_base(pz=STANCE_Z - 6, ppitch=10 * b, kneeout=0.2)),
                (0.6, legs_base(pz=STANCE_Z - 3, ppitch=5 * b)),
                (1.0, legs_base())]
        return keyed(keys, phi)
    if name.startswith("BOTH_DEAD"):
        return death_legs({"BOTH_DEAD1": 1, "BOTH_DEAD2": 2, "BOTH_DEAD3": 3}[name], 1.0)
    if name.startswith("BOTH_DEATH"):
        return death_legs(int(name[-1]), phi * n / max(n - 1, 1))
    raise ValueError(name)


def death_legs(which, t):
    lie = FLOOR + K * SK.cfg.get("lie", 6.0 / K)      # the pelvis's height lying down
    if which == 3:      # on the side: a wide body (a walker's shoulders) lies higher than on its back
        lie += K * SK.cfg.get("lie_side", 0.0)
    if which == 1:      # shot backward onto his back
        keys = [(0.0, legs_base()),
                (0.15, legs_base(px=-3, ppitch=-12, pz=STANCE_Z - 1)),
                (0.4, legs_base(px=-8, ppitch=-35, pz=STANCE_Z - 9, kneeout=0.3)),
                (0.7, legs_base(px=-15, ppitch=-80, pz=lie + 2, Lx=3, Rx=5, Llift=2, Rlift=4, Lpitch=-40,
                                Rpitch=-50, Ltoe=-40, Rtoe=-50, kneeout=0.3)),
                (1.0, legs_base(px=-17, ppitch=-90, pz=lie, Lx=7, Rx=5, Ly=7, Ry=-6, Llift=1, Rlift=4,
                                Lpitch=-75, Rpitch=-60, Ltoe=-75, Rtoe=-60, kneeout=0.2))]
    elif which == 2:    # pitched forward onto his front
        keys = [(0.0, legs_base()),
                (0.2, legs_base(px=2, ppitch=15, pz=STANCE_Z - 3)),
                (0.45, legs_base(px=5, ppitch=35, pz=STANCE_Z - 11, kneeout=0.3, Lx=0, Rx=-4)),
                (0.75, legs_base(px=14, ppitch=80, pz=lie + 2, Lx=-6, Rx=-8, Llift=3, Rlift=2, Lpitch=70,
                                 Rpitch=70, Ltoe=70, Rtoe=70)),
                (1.0, legs_base(px=16, ppitch=90, pz=lie, Lx=-10, Rx=-9, Ly=6, Ry=-7, Llift=1, Rlift=1,
                                Lpitch=85, Rpitch=85, Ltoe=85, Rtoe=85, kneeout=0.1))]
    else:               # spun and crumpled onto his right side
        keys = [(0.0, legs_base()),
                (0.25, legs_base(pyaw=25, pz=STANCE_Z - 4, proll=-10)),
                (0.5, legs_base(pyaw=45, pz=STANCE_Z - 12, proll=-30, kneeout=0.5, Lx=4, Rx=-2)),
                (0.8, legs_base(pyaw=55, pz=lie + 2, proll=-80, py=-8, kneeout=0.6, Lx=6, Ly=-2, Rx=2,
                                Ry=-10, Llift=6, Rlift=1)),
                (1.0, legs_base(pyaw=55, pz=lie, proll=-88, py=-10, kneeout=0.6, Lx=7, Ly=-4, Rx=3, Ry=-11,
                                Llift=5, Rlift=1, Lyaw=40, Ryaw=30))]
    d = keyed(keys, t)
    if BOARD:       # he comes off the board, which skids away flat on the floor
        d["onboard"] = max(0.0, 1.0 - t / 0.15)
        d["bx"], d["by"], d["bz"] = (6 if which == 2 else -6) * t, 4 * t, 0.0
        d["byaw"], d["bpitch"], d["broll"] = 25 * t, 0.0, 0.0
    return d


# the gun lowered at his right side, muzzle down, left hand off it (weapon switch)
GUN_DOWN = dict(gx=7.0, gy=-9.5, gz=9.0, gpitch=55.0, lon=0.0, syaw=-2.0, cyaw=-4.0,
                Lwx=10.0, Lwy=6.0, Lwz=17.0, Rprotract=4.0)
# the gauntlet: right fist ahead at chest height (the gauntlet hangs from the same grip), left hand up in a guard
FIST = dict(gx=15.0, gy=-6.0, gz=19.0, gpitch=6.0, lon=0.0, syaw=-4.0, cyaw=-4.0,
            Lwx=10.5, Lwy=4.5, Lwz=19.5, Rex=-0.4, Rey=-0.8, Rez=-0.4)


# a walker's mount: the post (spine) and the cradle (chest) turn with the aim; the gun sits in the cradle
MOUNT0 = dict(syaw=0.0, spitch=0.0, sroll=0.0, cyaw=0.0, cpitch=0.0, croll=0.0, npitch=0.0,
              gx=0.0, gy=0.0, gz=0.0, gyaw=0.0, gpitch=0.0, groll=0.0)


def mount(**kw):
    d = dict(MOUNT0)
    d.update(kw)
    return d


def walker_torso(name, k, n):
    t = k / max(n - 1, 1)
    if name == "TORSO_STAND":
        return mount()
    if name == "TORSO_STAND2":                 # gauntlet ready: the cradle leans forward
        return mount(spitch=8.0, cpitch=6.0, gx=1.0)
    if name == "TORSO_ATTACK":                 # recoil: the gun slides back in the cradle, the cradle rocks up
        kick = mount(gx=-2.2, gz=0.4, cpitch=-9.0, spitch=-3.0)
        return keyed([(0.0, kick), (0.2, kick), (1.0, mount())], t)
    if name == "TORSO_ATTACK2":                # gauntlet jab: the post lunges forward and back
        ready = mount(spitch=8.0, cpitch=6.0, gx=1.0)
        wind = mount(spitch=-10.0, cpitch=-6.0, gx=-1.5)
        hit = mount(spitch=18.0, cpitch=4.0, gx=4.0)
        return keyed([(0.0, wind), (0.3, hit), (0.5, hit), (1.0, ready)], t)
    if name in ("TORSO_DROP", "TORSO_RAISE"):  # weapon switch: the cradle tips the gun up and back, out of the way
        down = mount(spitch=-18.0, cpitch=-55.0, gx=-2.0, gz=1.0)
        a, b = (mount(), down) if name == "TORSO_DROP" else (down, mount())
        return keyed([(0.0, a), (1.0, b)], t)
    if name == "TORSO_GESTURE":                # taunt: the mount looks around, then up, and shakes the gun
        keys = [(0.0, mount()), (0.2, mount(syaw=35.0, cpitch=-12.0)), (0.4, mount(syaw=-35.0, cpitch=-12.0)),
                (0.55, mount(cpitch=-25.0, spitch=-8.0)), (0.65, mount(cpitch=-25.0, spitch=-8.0, croll=12.0)),
                (0.75, mount(cpitch=-25.0, spitch=-8.0, croll=-12.0)), (0.85, mount(cpitch=-20.0, croll=8.0)),
                (1.0, mount())]
        return keyed(keys, t)
    if name.startswith("BOTH_DEAD") or name.startswith("BOTH_DEATH"):     # the mount sags
        which = int(name[-1])
        tt = 1.0 if name.startswith("BOTH_DEAD") else t
        end = mount(spitch=(-35.0, 35.0, 10.0)[which - 1], cpitch=(-25.0, 25.0, 15.0)[which - 1],
                    sroll=(0.0, 0.0, -30.0)[which - 1], croll=(0.0, 0.0, -20.0)[which - 1])
        return keyed([(0.0, mount()), (0.4, mount(spitch=end["spitch"] * 0.5, sroll=end["sroll"] * 0.5)),
                      (1.0, end)], tt)
    raise ValueError(name)


def torso_anim(name, k, n):
    t = k / max(n - 1, 1)
    if WALKER:
        return walker_torso(name, k, n)
    if name == "TORSO_STAND":
        return torso()
    if name == "TORSO_STAND2":
        return torso(**FIST)
    if name == "TORSO_ATTACK":           # recoil: the gun kicks back and up, the shoulders take it
        kick = torso(gx=TORSO0["gx"] - 1.8, gz=TORSO0["gz"] + 0.6, gpitch=-6.0, spitch=-2.0, cpitch=-2.5)
        return keyed([(0.0, kick), (0.2, kick), (1.0, torso())], t)
    if name == "TORSO_ATTACK2":          # gauntlet jab
        wind = torso(**dict(FIST, gx=11.0, gy=-8.5, cyaw=-12.0))
        hit = torso(**dict(FIST, gx=19.5, gy=-3.5, gz=20.0, cyaw=10.0, spitch=3.0, Rprotract=16.0))
        return keyed([(0.0, wind), (0.3, hit), (0.5, hit), (1.0, torso(**FIST))], t)
    if name == "TORSO_DROP":
        return keyed([(0.0, torso()), (1.0, torso(**GUN_DOWN))], t)
    if name == "TORSO_RAISE":
        return keyed([(0.0, torso(**GUN_DOWN)), (1.0, torso())], t)
    if name == "TORSO_GESTURE":          # taunt: gun raised overhead, left fist pumping
        up_ = dict(gx=5.0, gy=-10.0, gz=40.0, gpitch=-70.0, lon=0.0, syaw=0.0, cyaw=0.0, cpitch=-4.0,
                   Rshrug=10.0, Rex=0.1, Rey=-0.9, Rez=-0.3)
        hi = dict(up_, Lwx=5.0, Lwy=11.0, Lwz=32.0, Lhx=0.0, Lhy=0.0, Lhz=1.0, Lbx=-1.0, Lby=0.0, Lbz=0.0,
                  Lex=0.0, Ley=0.9, Lez=-0.4)
        lo = dict(hi, Lwx=8.0, Lwy=9.0, Lwz=24.0)
        keys = [(0.0, torso()), (0.2, torso(**lo)), (0.35, torso(**hi)), (0.5, torso(**lo)),
                (0.65, torso(**hi)), (0.8, torso(**hi)), (1.0, torso())]
        return keyed(keys, t)
    if name.startswith("BOTH_DEAD"):
        return death_torso({"BOTH_DEAD1": 1, "BOTH_DEAD2": 2, "BOTH_DEAD3": 3}[name], 1.0)
    if name.startswith("BOTH_DEATH"):
        return death_torso(int(name[-1]), t)
    raise ValueError(name)


def death_torso(which, t):
    if which == 1:      # arms thrown up and back
        mid = torso(spitch=-12, cpitch=-10, syaw=0, cyaw=0, gx=4, gy=-14, gz=34, gpitch=-60, lon=0,
                    Lwx=-1, Lwy=15, Lwz=33, Lhx=0, Lhy=0.3, Lhz=1)
        end = torso(spitch=-5, syaw=0, cyaw=0, gx=0, gy=-21, gz=25, gpitch=-20, gyaw=-40, lon=0,
                    Lwx=-3, Lwy=19, Lwz=24, Lhx=-0.2, Lhy=1, Lhz=0.2, Lex=0, Ley=0.3, Lez=-1, Rex=0, Rey=-0.3, Rez=-1)
    elif which == 2:    # arms flung forward to break the fall
        mid = torso(spitch=12, cpitch=10, syaw=0, cyaw=0, gx=15, gy=-9, gz=12, gpitch=30, lon=0,
                    Lwx=12, Lwy=10, Lwz=12, Lhx=1, Lhy=0, Lhz=-0.3)
        end = torso(spitch=4, syaw=0, cyaw=0, gx=13, gy=-16, gz=20, gpitch=0, gyaw=-30, lon=0,
                    Lwx=11, Lwy=16, Lwz=20, Lhx=0.6, Lhy=0.6, Lhz=0)
    else:               # twisted, arms limp
        mid = torso(cyaw=25, croll=-8, syaw=0, gx=6, gy=-13, gz=8, gpitch=60, lon=0, Lwx=7, Lwy=12, Lwz=24)
        end = torso(cyaw=20, croll=-6, syaw=0, gx=4, gy=-12, gz=8, gpitch=70, lon=0, Lwx=10, Lwy=10, Lwz=12,
                    Lhx=0.6, Lhy=-0.4, Lhz=-0.6)
    return keyed([(0.0, torso()), (0.35, mid), (1.0, end)], t)


# ------------------------------------------------------------------ the slot's frames
pk = zipfile.ZipFile(pak0)
anims = mf.read_cfg(pk.read("models/players/%s/animation.cfg" % SLOT).decode("latin-1"))
nl = mf.read_md3(pk.read("models/players/%s/lower.md3" % SLOT))[0]
nu = mf.read_md3(pk.read("models/players/%s/upper.md3" % SLOT))[0]


def sources(part, count):
    skip = anims["LEGS_WALKCR"][0] - anims["TORSO_GESTURE"][0]
    o = [None] * count
    for name, (first, n, fps) in anims.items():
        kind = name.split("_")[0]
        if kind == "BOTH" or (kind == "TORSO" and part == "upper") or (kind == "LEGS" and part == "lower"):
            f0 = first - (skip if kind == "LEGS" else 0)
            for k in range(n):
                if 0 <= f0 + k < count and o[f0 + k] is None:     # first claim wins: the backpedal keeps its frames
                    o[f0 + k] = (name, k, n)
    last = next(x for x in o if x)
    for i, x in enumerate(o):
        o[i] = last = x or last
    return o


# how close the hands get to the gun in the main poses (0 = on it)
for label, tp in () if WALKER else (("stand", torso()), ("recoil", torso_anim("TORSO_ATTACK", 0, 6)),
                                    ("gauntlet", torso(**FIST)), ("jab", torso_anim("TORSO_ATTACK2", 2, 6))):
    _, short = pose(legs_base(), tp)
    print("hands short of target, %-8s right %.2f  left %.2f" % (label, short["R"], short["L"]))

frames = []
for (ln, lk, lnn) in sources("lower", nl):
    W, _ = pose(legs_anim(ln, lk, lnn), torso())
    frames.append(W)
lower_idle = legs_anim("LEGS_IDLE", 0, 10)
for (un, uk, unn) in sources("upper", nu):
    W, _ = pose(lower_idle, torso_anim(un, uk, unn))
    frames.append(W)
F = np.array([[W[n] for n in names] for W in frames])
bind = np.array([M(np.eye(3), BP[n]) for n in names[:-3]] +
                [M(np.eye(3), BP["spine"]), M(np.eye(3), BP["head"]),
                 M(np.eye(3), TAGW) if WALKER else M(np.eye(3), BP["hand_R"]) @ np.linalg.inv(HG["R"])])
print("frames %d + %d" % (nl, nu))

# ------------------------------------------------------------------ mesh and skin
d = np.load(mesh_npz)
pw = np.load(proxy_npz)
P = to_id(d["co"])
QW = pw["w"].astype(float)
weighted = QW.sum(1) > 1e-4            # heat weighting can leave a few loose bits of the proxy unsolved
Q = to_id(pw["co"])[weighted]
QW = QW[weighted]
dcols = [ix[str(n)] for n in pw["names"]]
dense = np.zeros((len(P), len(names)))
for c0 in range(0, len(P), 400):
    ch = P[c0:c0 + 400]
    dist = np.linalg.norm(ch[:, None, :] - Q[None, :, :], axis=2)
    nn = np.argsort(dist, axis=1)[:, :6]
    dn = np.take_along_axis(dist, nn, 1)
    iw = 1.0 / np.maximum(dn, 0.05) ** 2
    iw /= iw.sum(1, keepdims=True)
    dense[c0:c0 + 400][:, dcols] = np.einsum("vk,vkj->vj", iw, QW[nn])
dense /= np.maximum(dense.sum(1, keepdims=True), 1e-9)


# crisp joints: heat weights spread a limb's bend over most of its length, so arms and legs bend like hoses. Within
# each limb the weight is shared out again by where the vertex lies along the limb: all on one segment, with a short
# blend across the elbow, wrist, knee or ankle (half-widths in id units). What the limb shares with the torso stays.
def smooth01(x):
    x = np.clip(x, 0.0, 1.0)
    return x * x * (3 - 2 * x)


JOINT_BLEND = [("upperarm_%s", "forearm_%s", "hand_%s", 1.2, 0.8), ("thigh_%s", "calf_%s", "foot_%s", 1.6, 1.0)]
if WALKER:
    JOINT_BLEND = JOINT_BLEND[1:]
for s in "LR":
    for b0, b1, b2, w1, w2 in JOINT_BLEND:
        i0, i1, i2 = ix[b0 % s], ix[b1 % s], ix[b2 % s]
        cw = dense[:, [i0, i1, i2]].sum(1)
        m = cw > 0
        f1 = smooth01(((P - BP[b1 % s]) @ nrm(REST[b0 % s]) + w1) / (2 * w1))
        f2 = smooth01(((P - BP[b2 % s]) @ nrm(REST[b1 % s]) + w2) / (2 * w2))
        dense[m, i0] = cw[m] * (1 - f1[m])
        dense[m, i1] = cw[m] * f1[m] * (1 - f2[m])
        dense[m, i2] = cw[m] * f1[m] * f2[m]
# shoulder pads: armor above the shoulder joint rides the collarbone, not the arm, so it stays on the shoulder
Pb = d["co"]
for s, sg in () if WALKER else (("L", 1), ("R", -1)):
    sx, _, sz = next(j[3] for j in JOINTS if j[0] == "upperarm_" + s)
    near = (sg * Pb[:, 0] > abs(sx) - 0.09) & (sg * Pb[:, 0] < abs(sx) + 0.07)
    lift = smooth01((Pb[:, 2] - sz - 0.015) / 0.03) * near
    iu, ic = ix["upperarm_" + s], ix["clavicle_" + s]
    move = dense[:, iu] * 0.75 * lift
    dense[:, iu] -= move
    dense[:, ic] += move
# a shoulder pad that is its own piece of the mesh (compact, over a shoulder joint) moves as one rigid plate: 70% with
# the collarbone, 30% with the arm, so it tips a little as the arm swings instead of bending
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import connected_components
_e = np.concatenate([d["tri_verts"][:, [0, 1]], d["tri_verts"][:, [1, 2]], d["tri_verts"][:, [2, 0]]])
_, island = connected_components(coo_matrix((np.ones(len(_e)), (_e[:, 0], _e[:, 1])), shape=(len(Pb),) * 2),
                                 directed=False)
pads = 0
for k in np.unique(island):
    sel = island == k
    p = Pb[sel]
    if sel.sum() > 4000 or np.ptp(p, axis=0).max() > 0.24:
        continue
    c = p.mean(0)
    for s in "" if WALKER else "LR":
        sh = np.array(next(j[3] for j in JOINTS if j[0] == "upperarm_" + s))
        if np.linalg.norm(c - sh) < 0.09 and c[2] > sh[2]:
            dense[sel] = 0
            dense[np.ix_(sel, [ix["clavicle_" + s]])] = 0.7
            dense[np.ix_(sel, [ix["upperarm_" + s]])] = 0.3
            pads += 1
print("rigid shoulder pads:", pads)
if WALKER:
    # the mount: what stands above the body's top plate, around the post, turns with it (post, then cradle); nothing
    # else follows the torso, and the eye stays put in its housing (it is one piece with the body)
    mt = SK.cfg["mount"]
    ip, isp, ic, ih = ix["pelvis"], ix["spine"], ix["chest"], ix["head"]
    dense[:, ip] += dense[:, [isp, ic, ih]].sum(1)
    dense[:, [isp, ic, ih]] = 0
    r = mt.get("radius", 0.065)
    zone = (np.abs(Pb[:, 0] - mt["post"][0]) < r) & (np.abs(Pb[:, 1] - mt["cradle"][1]) < r * 1.2)
    up_ = smooth01((Pb[:, 2] - mt["post"][2] - 0.003) / 0.02) * zone
    top = smooth01((Pb[:, 2] - mt["cradle"][2] + 0.012) / 0.02)
    dense *= (1 - up_)[:, None]
    dense[:, isp] += up_ * (1 - top)
    dense[:, ic] += up_ * top
    # long thin loose pieces (the antenna) ride the body whole
    rigid = 0
    for k in np.unique(island):
        sel = island == k
        ext = np.ptp(Pb[sel], axis=0)
        if ext[2] > 0.25 and max(ext[0], ext[1]) < 0.06:
            dense[sel] = 0
            dense[sel, ip] = 1.0
            rigid += 1
    print("mount verts %d, rigid antennas %d" % (int((up_ > 0.5).sum()), rigid))
dense[dense < 0.02] = 0
top = np.argsort(-dense, axis=1)[:, :4]
topw = np.take_along_axis(dense, top, 1)
topw /= np.maximum(topw.sum(1, keepdims=True), 1e-9)
print("unweighted verts:", int((topw.sum(1) < 0.5).sum()))

tl, tv = d["tri_loops"], d["tri_verts"]
cv, cl = tv.ravel(), tl.ravel()
cn = d["cnrm"][cl] @ A.T
key = np.hstack([cv[:, None], np.round(d["uv"][cl], 5), np.round(cn, 3)])
_, first, inv = np.unique(key, axis=0, return_index=True, return_inverse=True)
inv = inv.ravel()
src = cv[first]
uv = d["uv"][cl[first]].copy()
uv[:, 1] = 1.0 - uv[:, 1]
prim = dict(pos=P[src], nrm=cn[first] / np.linalg.norm(cn[first], axis=1, keepdims=True), uv=uv,
            jw=topw[src], ji=top[src], tris=inv.reshape(-1, 3)[:, ::-1], stem="oz_mech")
prim["tan"] = mf.tangents(prim["pos"], prim["nrm"], prim["uv"], prim["tris"])

score = np.zeros((len(prim["tris"]), 3))
for c in range(4):
    for p_, letter in enumerate("luh"):
        mask = np.array([jpart[j] == letter for j in range(len(names))])[prim["ji"][:, c]]
        score[:, p_] += (prim["jw"][:, c] * mask)[prim["tris"]].sum(1)
tri_part = score.argmax(1)
meshes = []
for p_, letter in enumerate("luh"):
    for ct, verts in mf.chunks(prim["tris"][tri_part == p_]):
        meshes.append(dict(name=MESH_NAME, material=MATERIAL, part=letter, prim=prim, verts=verts, tris=ct))

skinm = F @ np.linalg.inv(bind)[None]
p4 = np.hstack([prim["pos"], np.ones((len(prim["pos"]), 1))])
bounds = []
for f in range(len(F)):
    acc = np.zeros((len(p4), 4))
    for c in range(4):
        acc += prim["jw"][:, c:c + 1] * np.einsum("vij,vj->vi", skinm[f][prim["ji"][:, c]], p4)
    pts = acc[:, :3]
    bounds.append((pts.min(0), pts.max(0), float(np.max(np.hypot(pts[:, 0], pts[:, 1]))),
                   float(np.max(np.linalg.norm(pts, axis=1)))))

head = ix["head"]
particles, links = mf.ragdoll([prim], names, parents, bind, head)
bodies, apart = mf.ragdoll_bodies([prim], names, parents, bind, head)
spine = [ix["spine"], ix["chest"]]
comment = "oz_rig 1\nupper %d\ndeaths %d\ndead %d %d %d\nmeshes %s\njoints %s\nspine %d %s\n" % (
    nl, anims["TORSO_GESTURE"][0], anims["BOTH_DEAD1"][0], anims["BOTH_DEAD2"][0], anims["BOTH_DEAD3"][0],
    "".join(x["part"] for x in meshes), jpart, len(spine), " ".join(str(j) for j in spine))
comment += "particles %d %s\n" % (len(particles), " ".join("%d %.1f" % p for p in particles))
comment += "links %d %s\n" % (len(links), " ".join("%d %d %.2f %.2f" % l for l in links))
kinds = {"root": 0, "cone": 1, "hinge": 2}
comment += "bodies %d %s\n" % (len(bodies), " ".join(
    "%d %d %d %.2f %.2f %.2f %.4f %.4f %.4f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.0f %.0f %.0f %.0f %.0f" % (
        e["bone"], e["parent"], kinds[e["kind"]], *e["center"], *e["axis"], e["radius"], e["height"], e["mass"],
        e["reach"], *e["joint"], e["swing"], e["twist"], e["bent"], e["neutral"], e["lean"]) for e in bodies))
comment += "apart %d %s\n" % (len(apart), " ".join("%d %d" % p for p in apart))
data = mf.write_iqm(names, parents, bind, F, [("lower", 0, nl), ("upper", nl, nu)], meshes, bounds, comment)
os.makedirs(out, exist_ok=True)
open(os.path.join(out, "oz_rig.iqm"), "wb").write(data)

SEGS = ["torso", "head", "upperarm_L", "forearm_L", "hand_L", "thigh_L", "shin_L", "foot_L",
        "upperarm_R", "forearm_R", "hand_R", "thigh_R", "shin_R", "foot_R", "board"]
J2S = {"root": "torso", "pelvis": "torso", "spine": "torso", "chest": "torso", "neck": "head", "head": "head",
       "clavicle_L": "torso", "clavicle_R": "torso", "board": "board"}
for s in "LR":
    J2S.update({"upperarm_" + s: "upperarm_" + s, "forearm_" + s: "forearm_" + s, "hand_" + s: "hand_" + s,
                "thigh_" + s: "thigh_" + s, "calf_" + s: "shin_" + s, "foot_" + s: "foot_" + s,
                "toe_" + s: "foot_" + s})
lead = prim["ji"][np.arange(len(prim["ji"])), prim["jw"].argmax(1)]
segv = np.array([SEGS.index(J2S[names[j]]) for j in lead])
np.savez(os.path.join(out, "segments.npz"), seg=np.concatenate([segv[m["verts"]] for m in meshes]),
         names=np.array(SEGS))
print("wrote %s: %d joints, %d verts, %d tris, %d meshes (%s), %d ragdoll bodies, %d KB" % (
    out, len(names), sum(len(m["verts"]) for m in meshes), sum(len(m["tris"]) for m in meshes), len(meshes),
    "".join(m["part"] for m in meshes), len(bodies), len(data) // 1024))
