"""Step 6: assemble the web viewer for one model.

python pack_viewer.py <work dir> [--title Keel]

Writes <work>/viewer/: index.html (open through a local web server), viewer.html (the same
page without a document skeleton, the form a Claude artifact takes), mesh + rig data with
binaries wrapped as base64 JSON (artifacts serve text and media types only), textures,
previews and the original textures as PNG.
"""
import os, sys, json, struct, base64, shutil, glob, argparse

ap = argparse.ArgumentParser()
ap.add_argument("work")
ap.add_argument("--title")
a = ap.parse_args()
HERE = os.path.dirname(os.path.abspath(__file__))
W = a.work
OUT = os.path.join(W, "viewer")
os.makedirs(OUT, exist_ok=True)
man = json.load(open(os.path.join(W, "mesh", "mesh.json")))
model = man["model"]
title = a.title or model.capitalize()

raw = open(os.path.join(W, "mesh", "mesh.bin"), "rb").read()
json.dump({"b64": base64.b64encode(raw).decode()}, open(os.path.join(OUT, "mesh_bin.json"), "w"))
shutil.copyfile(os.path.join(W, "mesh", "mesh.json"), os.path.join(OUT, "mesh.json"))

rigdir = os.path.join(W, "rig_viewer")
glb = open(os.path.join(rigdir, model + "_rigged.glb"), "rb").read()
jl = struct.unpack_from("<I", glb, 12)[0]
gj = json.loads(glb[20:20 + jl])
o = 20 + jl
bl = struct.unpack_from("<I", glb, o)[0]
gj["buffers"][0]["uri"] = "data:application/octet-stream;base64," + base64.b64encode(glb[o + 8:o + 8 + bl]).decode()
json.dump(gj, open(os.path.join(OUT, "rigged.json"), "w"), separators=(",", ":"))
shutil.copyfile(os.path.join(rigdir, "rig.json"), os.path.join(OUT, "rig.json"))

for f in glob.glob(os.path.join(W, "tex", "*.jpg")):
    shutil.copyfile(f, os.path.join(OUT, os.path.basename(f)))
for f in glob.glob(os.path.join(W, "src", "orig_*.png")):
    shutil.copyfile(f, os.path.join(OUT, os.path.basename(f)))

page = open(os.path.join(HERE, "viewer_template.html"), encoding="utf-8").read()
page = page.replace("{{TITLE}}", title).replace("{{MODEL}}", model)
open(os.path.join(OUT, "viewer.html"), "w", encoding="utf-8").write(page)
open(os.path.join(OUT, "index.html"), "w", encoding="utf-8").write(
    '<!doctype html><html><head><meta charset="utf-8">'
    '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover"></head><body>'
    + page + "</body></html>")
print("viewer: %d files in %s" % (len(os.listdir(OUT)), OUT))
