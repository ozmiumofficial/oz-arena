"""Build upgraded fighters (experimental): a Tripo T-pose model -> an Oz Arena rig in place of an id model, and the
pack that carries them.

python tools/fighter_swap/build_fighter.py doom visor anarki [--install]

For each fighter (fighters/<name>.json) under build/fighter_swap/<name>/:
  1. prep.py (Blender)          weld the mesh, cap its holes                       -> clean.blend, clean.npz
  2. stretch_arms.py (Blender)  lengthen the arms to a person's proportions      -> rigged/mesh.npz, rigged_mesh.blend
  3. rig_weights.py (Blender)   heat weights on a watertight voxel copy           -> rigged/proxy_weights.npz
  4. anim_fighter.py            skeleton, skin, every animation frame, tags, ragdoll -> oz_rig.iqm
Then build/fighter_swap/zz-oz-experimental-fighters.pk3: each fighter's models/players/<slot>/oz_rig.iqm, its
texture as oz_skin.jpg, and scripts/oz_experimental_fighters.shader. The oz renderer swaps a slot's md3s for the rig
(tr_oz_rig.c); the pack sits in oz/ beside the release packs and sorts after them. --install copies it to the
player's oz folder (%APPDATA%\\Oz Arena\\oz).

Needs Blender 4.2+ (BLENDER, or the usual install path) and the player's baseq3 (found as the other tools find it).
"""
import os, sys, json, shutil, zipfile, argparse, subprocess

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
ap.add_argument("--install", action="store_true")
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
        blender("prep.py", os.path.join(HERE, cfg["source"]), d, "100000000")
    if "stretch" in todo:
        blender("stretch_arms.py", cfg_path, os.path.join(d, "clean.blend"), rig)
    if "weights" in todo:
        blender("rig_weights.py", cfg_path, os.path.join(rig, "rigged_mesh.blend"), os.path.join(rig, "proxy_weights.npz"))
    if "anim" in todo:
        r = subprocess.run([sys.executable, os.path.join(HERE, "anim_fighter.py"), cfg_path, os.path.join(rig, "mesh.npz"),
                            os.path.join(rig, "proxy_weights.npz"), pak0, d], capture_output=True, text=True)
        print("\n".join("    " + l for l in r.stdout.splitlines()))
        if r.returncode:
            sys.exit("anim_fighter.py failed:\n" + r.stderr[-3000:])


def pack(names):
    shader = ["// Oz Arena experimental: upgraded fighters (tools/fighter_swap). Two-sided, so the models' leftover",
              "// seams never show through.", ""]
    path = os.path.join(OUT, "zz-oz-experimental-fighters.pk3")
    with zipfile.ZipFile(path + ".part", "w", zipfile.ZIP_DEFLATED) as z:
        for name in names:
            cfg = json.load(open(os.path.join(HERE, "fighters", name + ".json")))
            slot = cfg.get("slot", name)
            z.write(os.path.join(OUT, name, "oz_rig.iqm"), "models/players/%s/oz_rig.iqm" % slot)
            z.write(os.path.join(HERE, cfg["texture"]), "models/players/%s/oz_skin.jpg" % slot, zipfile.ZIP_STORED)
            shader += ["models/players/%s/oz_skin" % slot, "{", "\tcull disable", "\t{",
                       "\t\tmap models/players/%s/oz_skin.jpg" % slot, "\t\trgbGen lightingDiffuse", "\t}", "}", ""]
        z.writestr("scripts/oz_experimental_fighters.shader", "\n".join(shader))
    os.replace(path + ".part", path)
    print("== %s  %d MB  (%s)" % (path, os.path.getsize(path) // 2 ** 20, ", ".join(names)))
    if a.install:
        dst = os.path.join(os.environ["APPDATA"], "Oz Arena", "oz")
        shutil.copy2(path, dst)
        print("   installed in", dst)


if a.start != "pack":
    for n in a.fighters:
        run(n)
pack(a.fighters)
