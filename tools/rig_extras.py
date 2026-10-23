"""Extra rig.json data the Blender rig doesn't carry: attachment tag frames and material draw modes.

python rig_extras.py [--work ../work] [--extracted ../extracted/baseq3] [model ...]
    refreshes textures.json draw modes and patches work/<model>/export/rig.json (all models by default)

Tags:
The rig's "weapon" and "head" bones are built from the tags' position and forward axis only, so
their roll around that axis is whatever Blender chose. Attaching a weapon to the bone therefore
needs the tag's full frame: rig.json "tags": {"weapon": {"origin", "x", "y", "z"}} in the
character's model space (meters, Godot axes: id x forward -> +Z, y left -> +X, z up -> +Y; feet at 0).
A weapon model (whose own axes are the tag's) attaches with basis columns (y, z, x); see
Q3Character.attach. build_rigged.py writes the same blocks when it builds a rig.

Material modes: rig.json "material_modes": {stem: {"blend": add|mask|blend, "two_sided"}} for the
materials id Tech 3 doesn't draw opaque (Bones' xray skin is additive, Hunter's feathers cut out).
Godot's import script (q3_post_import.gd) applies them to the character's GLB.
"""
import os, sys, json, glob, argparse
import numpy as np

UNITS = 32.0
FEET = 24.0


def tagm(v):
    """12 floats (origin, 3 axes) -> 4x4 with the axes orthonormalized, like build_rigged.py."""
    o, ax = np.array(v[:3], float), np.array(v[3:], float).reshape(3, 3).T
    u, _, vt = np.linalg.svd(ax)
    m = np.eye(4)
    m[:3, :3] = u @ vt
    m[:3, 3] = o
    return m


def to_godot(v):
    return [round(float(v[1]), 6), round(float(v[2]), 6), round(float(v[0]), 6)]


def tag_frames(cage):
    """cage folder (cage/upper_part.npz, lower_part.npz) -> {"weapon": {...}, "head": {...}}."""
    lower = dict(np.load(os.path.join(cage, "lower_part.npz")))
    upper = dict(np.load(os.path.join(cage, "upper_part.npz")))
    lb, ub = int(lower["basis"]), int(upper["basis"])
    torso = tagm(lower["tag_torso"][lb])
    out = {}
    for tag, name in (("tag_weapon", "weapon"), ("tag_head", "head")):
        if tag not in upper:
            continue
        t = torso @ tagm(upper[tag][ub])
        origin = (t[:3, 3] + np.array([0, 0, FEET])) / UNITS
        out[name] = {"origin": to_godot(origin), "x": to_godot(t[:3, 0]), "y": to_godot(t[:3, 1]), "z": to_godot(t[:3, 2])}
    return out


def material_modes(textures):
    """textures.json entries -> {stem: {blend, two_sided}} for everything that isn't plain opaque."""
    return {t["stem"]: {"blend": t.get("blend", "opaque"), "two_sided": bool(t.get("two_sided"))}
            for t in textures if t.get("blend", "opaque") != "opaque" or t.get("two_sided")}


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("models", nargs="*")
    ap.add_argument("--work", default=os.path.join(os.path.dirname(here), "work"))
    ap.add_argument("--extracted", default=os.path.join(os.path.dirname(here), "extracted", "baseq3"))
    a = ap.parse_args()
    sys.path.insert(0, here)
    import q3common as q3
    n = 0
    for rig_path in sorted(glob.glob(os.path.join(a.work, "*", "export", "rig.json"))):
        model = os.path.basename(os.path.dirname(os.path.dirname(rig_path)))
        if a.models and model not in a.models:
            continue
        cage = os.path.join(a.work, model, "cage")
        if not os.path.isfile(os.path.join(cage, "upper_part.npz")):
            continue
        tj = os.path.join(a.work, model, "textures.json")
        textures = json.load(open(tj))
        for t in textures:   # work folders made before draw modes were recorded
            mode = q3.draw_mode(a.extracted, t["source"])
            t.update(blend=mode["blend"], two_sided=mode["two_sided"], alpha=mode["blend"] in ("mask", "blend"))
        json.dump(textures, open(tj, "w"), indent=1)
        rig = json.load(open(rig_path))
        rig["tags"] = tag_frames(cage)
        rig["material_modes"] = material_modes(textures)
        json.dump(rig, open(rig_path, "w"), indent=1)
        n += 1
    print("rig extras: %d rig.json files updated" % n)
