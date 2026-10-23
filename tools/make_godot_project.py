"""Assemble the Godot 4 project from tools/godot_template + the remastered library.

python make_godot_project.py [--work ../work] [--out ../godot] [--bridge C:/Users/<you>/godot-mcp/addon]

library/
  characters/<model>.glb + <model>.rig.json   from work/<model>/export (rigged, PBR, 25 clips)
  props/<id>.glb                              from work/_props/export (weapons, items, gibs, effects models)
  fx/textures/*.png + fx/fx.json              from work/_fx
  entities.json, tag_models.json, index.json  catalogs
  ui/ (fonts, levelshots, crosshairs, icons, maps.json) + sounds/   from work/_ui (make_ui.py)
  cinematics/<id>.ogv + cinematics.json       from work/_cinematics (make_cinematics.py)
  cards/ (cards.json, emblems, banners, frames) from work/_cards (build_cards.py)
Files are copied only when newer, so re-running after a pipeline change is cheap.
The godot-mcp editor bridge addon is copied in when found, so Claude can drive the open editor.
"""
import os, sys, json, shutil, glob, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ap = argparse.ArgumentParser()
ap.add_argument("--work", default=os.path.join(ROOT, "work"))
ap.add_argument("--out", default=os.path.join(ROOT, "godot"))
ap.add_argument("--bridge", default=os.path.expanduser(r"~/godot-mcp/addon"))
a = ap.parse_args()
copied = 0


def put(src, dst):
    global copied
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if not os.path.exists(dst) or os.path.getmtime(src) > os.path.getmtime(dst) + 1:
        shutil.copy2(src, dst)
        copied += 1


TPL = os.path.join(HERE, "godot_template")
for d, _, fs in os.walk(TPL):
    for f in fs:
        src = os.path.join(d, f)
        put(src, os.path.join(a.out, os.path.relpath(src, TPL)))
bridge = os.path.join(a.bridge, "godot_mcp_bridge")
if os.path.isdir(bridge):
    for d, _, fs in os.walk(bridge):
        for f in fs:
            src = os.path.join(d, f)
            put(src, os.path.join(a.out, "addons", "godot_mcp_bridge", os.path.relpath(src, bridge)))
else:
    # no bridge: drop the plugin line so the project opens cleanly
    pg = os.path.join(a.out, "project.godot")
    text = open(pg, encoding="utf-8").read().replace(
        'enabled=PackedStringArray("res://addons/godot_mcp_bridge/plugin.cfg")', 'enabled=PackedStringArray()')
    open(pg, "w", encoding="utf-8").write(text)

LIB = os.path.join(a.out, "library")
characters = []
for rep in sorted(glob.glob(os.path.join(a.work, "*", "report.json"))):
    r = json.load(open(rep))
    m = r["model"]
    glb = os.path.join(a.work, m, "export", m + "_rigged.glb")
    if r.get("ok") and os.path.exists(glb):
        put(glb, os.path.join(LIB, "characters", m + ".glb"))
        put(os.path.join(a.work, m, "export", "rig.json"), os.path.join(LIB, "characters", m + ".rig.json"))
        characters.append(m)
props = []
pexp = os.path.join(a.work, "_props", "export")
for f in sorted(glob.glob(os.path.join(pexp, "**", "*.glb"), recursive=True)):
    rel = os.path.relpath(f, pexp).replace("\\", "/")
    put(f, os.path.join(LIB, "props", rel))
    props.append(rel[:-4])
# Quake 3 shader stages for pickups and effect materials (make_prop_shaders.py)
put_json = os.path.join(a.work, "_props", "prop_shaders.json")
if os.path.isfile(put_json):
    put(put_json, os.path.join(LIB, "props", "prop_shaders.json"))
    for f in glob.glob(os.path.join(a.work, "_props", "stagetex", "*.png")):
        put(f, os.path.join(LIB, "props", "stagetex", os.path.basename(f)))
# first-person weapon tags, every hand frame (make_view_weapons.py)
vw_json = os.path.join(a.work, "_props", "view_weapons.json")
if os.path.isfile(vw_json):
    put(vw_json, os.path.join(LIB, "props", "view_weapons.json"))
for f in glob.glob(os.path.join(a.work, "_fx", "textures", "*.png")):
    put(f, os.path.join(LIB, "fx", "textures", os.path.basename(f)))
put(os.path.join(a.work, "_fx", "fx.json"), os.path.join(LIB, "fx", "fx.json"))
put(os.path.join(a.work, "_entities", "entities.json"), os.path.join(LIB, "entities.json"))
put(os.path.join(a.work, "_props", "tag_models.json"), os.path.join(LIB, "tag_models.json"))
cine = os.path.join(a.work, "_cinematics")
for f in glob.glob(os.path.join(cine, "*.ogv")) + glob.glob(os.path.join(cine, "cinematics.json")):
    put(f, os.path.join(LIB, "cinematics", os.path.basename(f)))
ui = os.path.join(a.work, "_ui")
for d, _, fs in os.walk(ui):
    for f in fs:
        if f.startswith("_"):
            continue  # check sheets
        rel = os.path.relpath(os.path.join(d, f), ui)
        dst = os.path.join(LIB, rel) if rel.startswith("sounds") else os.path.join(LIB, "ui", rel)
        put(os.path.join(d, f), dst)
cards = os.path.join(a.work, "_cards")
for sub in ("emblems", "banners", "frames"):
    for f in glob.glob(os.path.join(cards, sub, "*")):
        put(f, os.path.join(LIB, "cards", sub, os.path.basename(f)))
if os.path.isfile(os.path.join(cards, "cards.json")):
    put(os.path.join(cards, "cards.json"), os.path.join(LIB, "cards", "cards.json"))
# game sounds for Gameplay (weapons, players, items, world, movers, announcer), straight from the
# player's paks until the sound remaster pass; same paths as Quake 3 under library/sounds/
snd = os.path.join(ROOT, "extracted", "baseq3", "sound")
for part in ("weapons", "player", "items", "world", "movers", "feedback", "misc", "teamplay"):
    for d, _, fs in os.walk(os.path.join(snd, part)):
        for f in fs:
            if f.lower().endswith(".wav"):
                src = os.path.join(d, f)
                put(src, os.path.join(LIB, "sounds", os.path.relpath(src, snd)))
json.dump(dict(characters=characters, props=props), open(os.path.join(LIB, "index.json"), "w"), indent=1)
open(os.path.join(a.out, ".gitignore"), "w").write(".godot/\n")  # keep *.import: they hold import settings
print("godot project: %s  (%d files updated, %d characters, %d props)" % (a.out, copied, len(characters), len(props)))
