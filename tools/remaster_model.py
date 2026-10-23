"""Run the whole remaster pipeline for one, several or all id Tech 3 player models.

python remaster_model.py keel
python remaster_model.py keel sarge visor
python remaster_model.py --all
python remaster_model.py --list
python remaster_model.py keel --blender "D:/Blender/blender.exe" --q3 "D:/Games/Quake 3 Arena/baseq3"

Folders (all relative to the Q3Remaster folder unless overridden):
  extracted/baseq3/   files pulled from your pak*.pk3 (made automatically on the first run)
  work/<model>/       src, mesh, cage, tex, rig_viewer, export (blend/fbx/glb + textures +
                      unity/), viewer/ and report.json

Steps per model: build_mesh_data -> fit_rig -> make_pbr -> Blender rig (viewer copy) ->
Blender rig with PBR materials (export) -> unity_repack -> pack_viewer.
Per-model settings live in tools/models.json.
"""
import os, sys, json, time, argparse, subprocess, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import q3common as q3

DEFAULT_Q3, DEFAULT_BLENDER = q3.DEFAULT_Q3, q3.DEFAULT_BLENDER

ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument("models", nargs="*")
ap.add_argument("--all", action="store_true")
ap.add_argument("--list", action="store_true")
ap.add_argument("--q3", help="your Quake 3 baseq3 folder (with pak0.pk3)")
ap.add_argument("--extracted", default=os.path.join(ROOT, "extracted", "baseq3"))
ap.add_argument("--work", default=os.path.join(ROOT, "work"))
ap.add_argument("--blender")
ap.add_argument("--skip-pbr", action="store_true", help="reuse existing tex/ maps")
ap.add_argument("--from", dest="start", choices=["mesh", "fit", "pbr", "rig", "viewer"], default="mesh",
                help="start at this step and reuse earlier results (e.g. --from pbr after changing make_pbr.py)")
a = ap.parse_args()


def find(cands, test):
    for c in cands:
        if c and test(c):
            return c
    return None


if not os.path.isdir(os.path.join(a.extracted, "models", "players")):
    q3dir = find([a.q3] + DEFAULT_Q3, lambda p: os.path.isfile(os.path.join(p, "pak0.pk3")))
    if not q3dir:
        sys.exit("No extracted files and no Quake 3 install found. Pass --q3 <your baseq3 folder>.")
    print("Extracting models and shaders from", q3dir)
    subprocess.check_call([sys.executable, os.path.join(HERE, "extract_pk3.py"), q3dir, a.extracted])

available = q3.player_models(a.extracted)
if a.list or not (a.models or a.all):
    print("Player models:", " ".join(available))
    sys.exit(0)
blender = find([a.blender, shutil.which("blender")] + DEFAULT_BLENDER, os.path.isfile)
if not blender:
    sys.exit("Blender not found. Pass --blender <path to blender executable>.")
settings = json.load(open(os.path.join(HERE, "models.json")))
models = available if a.all else a.models
for m in models:
    if m not in available:
        sys.exit("Unknown model %r. Available: %s" % (m, " ".join(available)))


def run(args, log):
    log.write("\n$ " + " ".join(args) + "\n")
    log.flush()
    p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    log.write(p.stdout)
    if p.returncode != 0 or "Traceback" in p.stdout:
        raise RuntimeError("step failed: %s (see log)" % os.path.basename(args[1] if args[0] == sys.executable else args[-1]))
    return p.stdout


summary = []
for m in models:
    t0 = time.time()
    W = os.path.join(a.work, m)
    os.makedirs(W, exist_ok=True)
    cfg = settings.get(m, {})
    log = open(os.path.join(W, "pipeline.log"), "w", encoding="utf-8")
    py = [sys.executable]
    print("== %s" % m, flush=True)
    steps = ["mesh", "fit", "pbr", "rig", "viewer"]
    do = lambda s: steps.index(s) >= steps.index(a.start)
    try:
        if do("mesh"):
            run(py + [os.path.join(HERE, "build_mesh_data.py"), a.extracted, m, W, "--skin", cfg.get("skin", "default")], log)
        if do("fit"):
            fit = py + [os.path.join(HERE, "fit_rig.py"), W]
            for k in ("lower", "upper"):
                if k in cfg:
                    fit += ["--" + k, str(cfg[k])]
            run(fit, log)
        if do("pbr") and not (a.skip_pbr and os.path.isdir(os.path.join(W, "tex"))):
            pbr = py + [os.path.join(HERE, "make_pbr.py"), W]
            if "face_band" in cfg:
                pbr += ["--face-band", str(cfg["face_band"])]
            run(pbr, log)
        bl = [blender, "-b", "--factory-startup", "-P", os.path.join(HERE, "build_rigged.py"), "--"]
        if do("rig"):
            run(bl + [W, os.path.join(W, "rig_viewer"), "--name", m], log)
            run(bl + [W, os.path.join(W, "export"), "--pbr", "--name", m], log)
            run(py + [os.path.join(HERE, "unity_repack.py"), os.path.join(W, "export")], log)
        run(py + [os.path.join(HERE, "pack_viewer.py"), W], log)
        rig = json.load(open(os.path.join(W, "export", "rig.json")))
        row = dict(model=m, ok=True, seconds=round(time.time() - t0), bones=len(rig["bones"]), tris=rig["tris"],
                   clips=len(rig["clips"]), **rig["fit"], materials=rig["materials"])
    except Exception as e:
        row = dict(model=m, ok=False, seconds=round(time.time() - t0), error=str(e))
    log.close()
    json.dump(row, open(os.path.join(W, "report.json"), "w"), indent=1)
    summary.append(row)
    if row["ok"]:
        print("   ok in %ds: %d bones, %d tris, legs %.1f mm / torso %.1f mm RMS" % (
            row["seconds"], row["bones"], row["tris"], row["lower_rms_mm"], row["upper_rms_mm"]), flush=True)
    else:
        print("   FAILED after %ds: %s (log: %s)" % (row["seconds"], row["error"], os.path.join(W, "pipeline.log")), flush=True)

json.dump(summary, open(os.path.join(a.work, "summary.json"), "w"), indent=1)
subprocess.call([sys.executable, os.path.join(HERE, "make_gallery.py"), a.work])
ok = sum(r["ok"] for r in summary)
print("%d of %d models done. Summary: %s" % (ok, len(summary), os.path.join(a.work, "summary.json")))
