"""Windowed captures of every built map (needs the GPU; run with the editor idle), plus a contact sheet.

python capture_maps.py [--maps q3dm17 q3dm1] [--cam intermission] [--out ../work/_maps/captures]

For each library/maps/<map>/<map>.tscn: tests/capture.tscn with cam=intermission (and spawn:0),
saved as <out>/<map>_<cam>.png; then <out>/contact_sheet.jpg with every map labeled.
"""
import os, sys, glob, argparse, subprocess
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import q3common as q3

ap = argparse.ArgumentParser()
ap.add_argument("--maps", nargs="*")
ap.add_argument("--cams", nargs="*", default=["intermission", "spawn:0"])
ap.add_argument("--out", default=os.path.join(ROOT, "work", "_maps", "captures"))
ap.add_argument("--godot")
a = ap.parse_args()

godot = next((c for c in [a.godot] + q3.default_godot() if c and os.path.isfile(c)), None)
if not godot:
    sys.exit("Godot not found (--godot)")
proj = os.path.join(ROOT, "godot")
os.makedirs(a.out, exist_ok=True)
maps = a.maps or sorted(os.path.basename(d) for d in glob.glob(os.path.join(proj, "library", "maps", "*"))
                        if os.path.exists(os.path.join(d, os.path.basename(d) + ".tscn")))
shots = []
for m in maps:
    for cam in a.cams:
        stem = os.path.join(a.out, "%s_%s" % (m, cam.replace(":", "")))
        subprocess.run([godot, "--path", proj, "res://tests/capture.tscn", "--",
                        "scene=res://library/maps/%s/%s.tscn" % (m, m), "cam=" + cam, "out=" + stem.replace("\\", "/"),
                        "frames=90", "count=1"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=180)
        png = stem + "_0.png"
        if os.path.exists(png):
            os.replace(png, stem + ".png")
            shots.append((m, cam, stem + ".png"))
        print("%-16s %-13s %s" % (m, cam, "ok" if os.path.exists(stem + ".png") else "FAILED"), flush=True)

# contact sheet: one row per map, one column per camera
if shots:
    tw, th = 480, 270
    cols = len(a.cams)
    rows = sorted(set(s[0] for s in shots))
    sheet = Image.new("RGB", (cols * tw, len(rows) * th), (12, 12, 12))
    d = ImageDraw.Draw(sheet)
    for m, cam, path in shots:
        r, c = rows.index(m), a.cams.index(cam)
        sheet.paste(Image.open(path).convert("RGB").resize((tw, th)), (c * tw, r * th))
        d.text((c * tw + 8, r * th + 6), "%s  %s" % (m, cam), fill=(255, 220, 120))
    sheet.save(os.path.join(a.out, "contact_sheet.jpg"), quality=88)
    print("contact sheet:", os.path.join(a.out, "contact_sheet.jpg"))
