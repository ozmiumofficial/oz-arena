"""Build the modern fighters: a Tripo T-pose model -> an Oz Arena rig for an id fighter's slot, and the pack that
carries them (Settings > Experimental > Modern player models).

python tools/fighter_swap/build_fighter.py doom visor anarki

For each fighter (fighters/<name>.json) under build/fighter_swap/<name>/:
  1. prep.py (Blender)          weld the mesh, cap its holes                       -> clean.blend, clean.npz
  2. stretch_arms.py (Blender)  lengthen the arms to a person's proportions      -> rigged/mesh.npz, rigged_mesh.blend
  3. rig_weights.py (Blender)   heat weights on a watertight voxel copy           -> rigged/proxy_weights.npz
  4. anim_fighter.py            skeleton, skin, every animation frame, tags, ragdoll -> oz_rig.iqm
Then build/engine/oz/pak_oz_modern_fighters.pk3 (beside the other packs, so the release tools ship it): each
fighter's rig as models/players/<slot>/oz_rig_modern.iqm, its texture as oz_skin.jpg, and
scripts/oz_modern_fighters.shader. With r_ozModernFighters 1 the oz renderer swaps a slot's md3s for that rig in place
of the remastered oz_rig.iqm (tr_oz_rig.c). --out puts the pack elsewhere.

Needs Blender 4.2+ (BLENDER, or the usual install path) and the player's baseq3 (found as the other tools find it).
"""
import os, sys, json, zipfile, argparse, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.dirname(HERE))
from q3paks import find_baseq3

ap = argparse.ArgumentParser()
ap.add_argument("fighters", nargs="+")
ap.add_argument("--blender", default=os.environ.get("BLENDER", r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"))
ap.add_argument("--baseq3", help="the player's baseq3 (default: found in the Steam libraries)")
ap.add_argument("--from", dest="start", default="prep", choices=["prep", "stretch", "weights", "anim", "pack"],
                help="skip the steps before this one (their output must already be there)")
ap.add_argument("--out", default=os.path.join(ROOT, "build", "engine", "oz"), help="where the pack goes")
a = ap.parse_args()
OUT = os.path.join(ROOT, "build", "fighter_swap")
STEPS = ["prep", "stretch", "weights", "anim", "pack"]
pak0 = os.path.join(a.baseq3 or find_baseq3(), "pak0.pk3")


def blender(script, *args):
    cmd = [a.blender, "-b", "--factory-startup", "--python", os.path.join(HERE, script), "--"] + list(args)
    r = subprocess.run(cmd, capture_output=True, text=True)
    keep = [l for l in r.stdout.splitlines() if l.startswith(("STATS", "CAPS", "EXPORTED", "STRETCHED", "PROXY"))]
    print("\n".join("    " + l for l in keep))
    if r.returncode or "Traceback" in r.stdout + r.stderr:
        sys.exit("%s failed:\n%s" % (script, (r.stdout + r.stderr)[-3000:]))


def run(name):
    cfg_path = os.path.join(HERE, "fighters", name + ".json")
    cfg = json.load(open(cfg_path))
    d = os.path.join(OUT, name)
    rig = os.path.join(d, "rigged")
    os.makedirs(rig, exist_ok=True)
    todo = STEPS[STEPS.index(a.start):]
    print("== %s (replaces %s)" % (name, cfg.get("slot", name)))
    if "prep" in todo:
        blender("prep.py", os.path.join(HERE, cfg["source"]), d, "100000000", *(["repair"] if cfg.get("repair") else []),
                *(["cap_max=%g" % cfg["cap_max"]] if "cap_max" in cfg else []))
    if "stretch" in todo:
        blender("stretch_arms.py", cfg_path, os.path.join(d, "clean.blend"), rig)
    if "weights" in todo:
        blender("rig_weights.py", cfg_path, os.path.join(rig, "rigged_mesh.blend"), os.path.join(rig, "proxy_weights.npz"))
    if cfg.get("retouch") and "pack" in todo:     # the texture touched up where the config says (retouch.py)
        r = subprocess.run([sys.executable, os.path.join(HERE, "retouch.py"), cfg_path, os.path.join(d, "clean.npz"),
                            os.path.join(HERE, cfg["texture"]), os.path.join(d, "oz_skin.jpg")], capture_output=True, text=True)
        print("\n".join("    " + l for l in r.stdout.splitlines()))
        if r.returncode:
            sys.exit("retouch.py failed:\n" + r.stderr[-3000:])
    if "anim" in todo:
        r = subprocess.run([sys.executable, os.path.join(HERE, "anim_fighter.py"), cfg_path, os.path.join(rig, "mesh.npz"),
                            os.path.join(rig, "proxy_weights.npz"), pak0, d], capture_output=True, text=True)
        print("\n".join("    " + l for l in r.stdout.splitlines()))
        if r.returncode:
            sys.exit("anim_fighter.py failed:\n" + r.stderr[-3000:])


def pack(names):
    shader = ["// Oz Arena: the modern fighters (tools/fighter_swap). Two-sided, so the models' leftover seams never",
              "// show through.", ""]
    os.makedirs(a.out, exist_ok=True)
    path = os.path.join(a.out, "pak_oz_modern_fighters.pk3")
    with zipfile.ZipFile(path + ".part", "w", zipfile.ZIP_DEFLATED) as z:
        for name in names:
            cfg = json.load(open(os.path.join(HERE, "fighters", name + ".json")))
            slot = cfg.get("slot", name)
            z.write(os.path.join(OUT, name, "oz_rig.iqm"), "models/players/%s/oz_rig_modern.iqm" % slot)
            tex = os.path.join(OUT, name, "oz_skin.jpg") if cfg.get("retouch") else os.path.join(HERE, cfg["texture"])
            z.write(tex, "models/players/%s/oz_skin.jpg" % slot, zipfile.ZIP_STORED)
            shader += ["models/players/%s/oz_skin" % slot, "{", "\tcull disable", "\t{",
                       "\t\tmap models/players/%s/oz_skin.jpg" % slot, "\t\trgbGen lightingDiffuse", "\t}", "}", ""]
        z.writestr("scripts/oz_modern_fighters.shader", "\n".join(shader))
    os.replace(path + ".part", path)
    print("== %s  %d MB  (%s)" % (path, os.path.getsize(path) // 2 ** 20, ", ".join(names)))


if a.start != "pack":
    for n in a.fighters:
        run(n)
pack(a.fighters)
