"""Build a remodeled weapon from a Tripo export: sealed, retextured, in Oz's pack.

python tools/weapon_swap/build_weapon.py railgun [--from texture] [--work DIR] [--out PAK]

weapons/<name>.json names the source FBX (sources/<name>/) and how the model sits in id's weapon space. Steps,
each a script beside this one, writing under --work (default build/weapon_swap/<name>):
  prep    prep_orig.py (Blender)   Tripo's mesh welded, as 'orig' and as triangles      orig.blend, orig.npz
  solid   solid.py                 watertight surface from winding numbers               high.npz
  geo     geo.py (Blender)         'high' cleaned, decimated to 'low'                    geo.blend
  uv      dump_low.py, atlas.py    xatlas charts for 'low'                                uv.npz
  bake    bake.py, bake_curv.py    Tripo's color, normal, AO, curvature onto 'low'       bake/
  texture segment.py, texture.py   cleaned PBR maps and the red masks                    pbr/
  export  export_dump.py, export_iqm.py   the IQM in id's space                          <model>_remodel.iqm
  pack    pack.py                  DDS maps, shader, pak                                 build/engine/oz/pak_oz_remodeled_<name>.pk3
Needs Blender 4.2+ (BLENDER or the usual path) and Python with numpy, opencv, scipy, scikit-image, libigl and xatlas
(pip install --target <dir> ...; PYTHONPATH=<dir>).
"""
import os, sys, json, argparse, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
STEPS = ["prep", "solid", "geo", "uv", "bake", "texture", "export", "pack"]
ap = argparse.ArgumentParser()
ap.add_argument("weapon")
ap.add_argument("--from", dest="start", default="prep", choices=STEPS)
ap.add_argument("--work")
ap.add_argument("--out")
ap.add_argument("--blender", default=os.environ.get("BLENDER", r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"))
a = ap.parse_args()
cfgp = os.path.join(HERE, "weapons", a.weapon + ".json")
cfg = json.load(open(cfgp))
W = os.path.abspath(a.work or os.path.join(ROOT, "build", "weapon_swap", a.weapon))
OUT = a.out or os.path.join(ROOT, "build", "engine", "oz", "pak_oz_remodeled_%s.pk3" % a.weapon)
os.makedirs(W, exist_ok=True)
P = lambda *x: os.path.join(W, *x)


def py(script, *args):
    print(">", script, *args, flush=True)
    subprocess.run([sys.executable, os.path.join(HERE, script)] + [str(x) for x in args], cwd=W, check=True)


def blender(script, *args):
    print(">", script, *args, flush=True)
    r = subprocess.run([a.blender, "-b", "--factory-startup", "--python", os.path.join(HERE, script), "--"] +
                       [str(x) for x in args], capture_output=True, text=True)
    for line in r.stdout.splitlines():
        if not line.startswith(("Blender", "Read", "Info", "Fra:", "00:")) and line.strip():
            print("   ", line)
    if r.returncode:
        sys.exit(r.stderr[-2000:])


run = STEPS[STEPS.index(a.start):]
size = int(cfg.get("texture", 4096))
if "prep" in run:
    blender("prep_orig.py", os.path.join(HERE, cfg["source"]), P("orig.blend"), P("orig.npz"))
if "solid" in run:
    py("solid.py", P("orig.npz"), P("high.npz"), cfg["voxel"], cfg["thick"], ",".join(map(str, cfg.get("drop_faces", []))))
if "geo" in run:
    blender("geo.py", P("orig.blend"), P("high.npz"), P("geo.blend"), cfg["tris"])
if "uv" in run:
    blender("dump_low.py", P("geo.blend"), P("low.npz"))
    py("atlas.py", P("low.npz"), P("uv.npz"), size)
if "bake" in run:
    blender("bake.py", P("geo.blend"), P("bake"), size, P("uv.npz"))
    blender("bake_curv.py", P("geo.blend"), P("uv.npz"), P("bake", "bake_curv.png"), size)
if "texture" in run:
    py("segment.py", P("bake"), P("uv.npz"), 8)
    py("texture.py", P("bake"), P("pbr"))
if "export" in run:
    blender("export_dump.py", P("bake", "baked.blend"), P("corners.npz"))
    py("export_iqm.py", P("corners.npz"), cfgp, P(os.path.basename(cfg["model"]) + "_remodel.iqm"))
if "pack" in run:
    py("pack.py", OUT, "%s=%s,%s" % (a.weapon, W, cfgp))
