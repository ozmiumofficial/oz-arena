"""Exports the game as a Windows build to play outside the editor.

python export_game.py [--out ../build/windows] [--godot <godot exe>]

  <out>/Q3Remaster.exe          the game (Godot's debug template: q3core is built as template_debug)
  <out>/Q3Remaster.console.exe  the same with a console window for its log
  <out>/Q3Remaster.pck          scripts, scenes, the remastered library, the web menu (godot/export_presets.cfg)
  <out>/*.dll                   the GDExtensions (q3core, godot_wry), placed by the export
  <out>/q3core/baseq3/qagamex86_64.dll   id's game module, which the engine loads from disk (Q3Host.bin_path)
  <out>/webui_assets.pck        the web menu's images, sounds and fonts as plain files (Q3WebLayer mounts it)
The player's own Quake 3 install is still found as in the editor (fs_steampath), and pak0 is checked.
Close the Godot editor's project or let it be: the export only reads the project.
"""
import os, sys, shutil, argparse, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import q3common as q3

ap = argparse.ArgumentParser()
ap.add_argument("--out", default=os.path.join(ROOT, "build", "windows"))
ap.add_argument("--godot")
a = ap.parse_args()

project = os.path.join(ROOT, "godot")
godot = next((g for g in [a.godot] + q3.default_godot() if g and os.path.isfile(g)), None)
if not godot:
    sys.exit("export_game: Godot not found (pass --godot)")
os.makedirs(a.out, exist_ok=True)
exe = os.path.join(a.out, "Q3Remaster.exe")
log = os.path.join(a.out, "export.log")
print("exporting to", exe)
subprocess.run([godot, "--headless", "--path", project, "--export-debug", "Windows Desktop", exe, "--log-file", log],
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
if not os.path.isfile(exe) or not os.path.isfile(os.path.join(a.out, "Q3Remaster.pck")):
    sys.exit("export_game: the export failed; see " + log)
game = os.path.join(project, "addons", "q3core", "bin", "baseq3")
dst = os.path.join(a.out, "q3core", "baseq3")
os.makedirs(dst, exist_ok=True)
for f in os.listdir(game):
    if f.endswith(".dll"):
        shutil.copy2(os.path.join(game, f), os.path.join(dst, f))
# the web menu's images, sounds and fonts as plain files (the export ships only Godot's imports)
pack = os.path.join(a.out, "webui_assets.pck")
r = subprocess.run([godot, "--headless", "--path", project, "--quit-after", "3000", "--log-file", os.path.join(a.out, "web_assets.log"),
                    "res://tests/pack_web_assets.tscn", "--", "out=" + pack.replace("\\", "/")],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
if r.returncode != 0 or not os.path.isfile(pack):
    sys.exit("export_game: packing the web menu's assets failed; see web_assets.log")
size = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(a.out) for f in fs)
print("exported: %s (%.1f GB)" % (a.out, size / 1e9))
