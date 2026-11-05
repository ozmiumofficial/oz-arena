"""A fighter's own skeleton, from its config (fighters/<name>.json), in the model's Blender T-pose coordinates
(x = his left, -y = front, z = up, about 1 tall, as Tripo exports them).

Joints come parents first: (name, parent, part, position, tail). part: l = legs entity, u = torso entity,
h = head entity (tr_oz_rig.c). tail: where the bone points in the T-pose (Blender's heat weights, and aiming the
bone when posing).

Arms: Tripo's fighters have arms about two thirds of a person's, too short to hold a gun with both hands. Each config
lengthens them along the T-pose axis between shoulder and wrist ("arm_stretch"); the stretch eases in past the
shoulder joint so the pads barely move, and the hand keeps its size. arm_x maps the original |x| to the new one.
"""
import json, os


def load(path):
    cfg = json.load(open(path))
    cfg["_dir"] = os.path.dirname(os.path.abspath(path))
    return Skeleton(cfg)


class Skeleton:
    def __init__(self, cfg):
        self.cfg = cfg
        self.name = cfg["name"]
        self.slot = cfg.get("slot", cfg["name"])
        a = cfg["arms"]
        self.stretch = a["stretch"]
        self.ramp = tuple(a["ramp"])
        self.wrist = a["wrist_x"]
        self.min_z = a["min_z"]
        L = {k: tuple(v) for k, v in cfg["left"].items()}
        for n in ("upperarm", "forearm", "hand", "fingers"):
            x, y, z = L[n]
            L[n] = (self.arm_x(x), y, z)
        C = {k: tuple(v) for k, v in cfg["center"].items()}
        self.L, self.C = L, C
        self.sole_z = cfg.get("sole_z", 0.0)
        self.heel_y = cfg.get("heel_y", 0.065)
        self.board = cfg.get("board")

        R = {k: tuple(v) for k, v in cfg.get("right", {}).items()}     # where the right side isn't a mirror

        def side(n, s):
            if s == "R" and n in R:
                return R[n]
            x, y, z = L[n]
            return (x if s == "L" else -x, y, z)
        J = [("root", None, "l", C["root"], C["pelvis"]),
             ("pelvis", "root", "l", C["pelvis"], C["spine"])]
        if self.board:
            c = tuple(self.board["center"])
            J.append(("board", "root", "l", c, (c[0] + self.board["half_length"], c[1], c[2])))
        for s in "LR":
            J += [("thigh_" + s, "pelvis", "l", side("thigh", s), side("calf", s)),
                  ("calf_" + s, "thigh_" + s, "l", side("calf", s), side("foot", s)),
                  ("foot_" + s, "calf_" + s, "l", side("foot", s), side("toe", s)),
                  ("toe_" + s, "foot_" + s, "l", side("toe", s), side("toetip", s))]
        J += [("spine", "pelvis", "u", C["spine"], C["chest"]),
              ("chest", "spine", "u", C["chest"], C["neck"]),
              ("neck", "chest", "u", C["neck"], C["head"]),
              ("head", "neck", "h", C["head"], C["headtop"])]
        for s in "LR":
            J += [("clavicle_" + s, "chest", "u", side("clavicle", s), side("upperarm", s)),
                  ("upperarm_" + s, "clavicle_" + s, "u", side("upperarm", s), side("forearm", s)),
                  ("forearm_" + s, "upperarm_" + s, "u", side("forearm", s), side("hand", s)),
                  ("hand_" + s, "forearm_" + s, "u", side("hand", s), side("fingers", s))]
        self.JOINTS = J
        self.DEFORM = [j[0] for j in J if j[0] != "root"]

    def arm_x(self, ax):
        """|x| of a point on the original arm -> |x| on the lengthened arm"""
        a, b = self.ramp
        k = self.stretch - 1
        if ax <= a:
            return ax
        if ax <= b:
            return ax + k * (ax - a) ** 2 / (2 * (b - a))
        full = k * (b - a) / 2
        if ax <= self.wrist:
            return ax + full + k * (ax - b)
        return ax + full + k * (self.wrist - b)
