"""Build everything that isn't a player model, then the Godot project, and test it.

python build_library.py                 props + effects + entities + Godot project + import + self-test
python build_library.py --skip-props    reuse work/_props (e.g. after only changing Godot scripts)
python build_library.py --no-godot      stop after the asset steps

Steps:
  1. build_props.py -> make_pbr.py -> build_props_blender.py   weapons, items, gibs, effects models (work/_props)
  2. make_fx.py                                                 effect textures + fx.json (work/_fx)
  3. make_entities.py                                           entity catalog (work/_entities)
     make_ui.py                                                 menu sounds, fonts, maps, bots, crosshairs (work/_ui)
     make_cinematics.py                                         Quake 3's RoQ movies -> Ogg Theora (work/_cinematics)
  4. build_world.py -> make_pbr.py --jobs -> (after 5) make_godot_world.py   world textures (work/_world)
  5. build_q3core.py                                            id's game code as the q3core GDExtension (MSVC + SCons)
     make_godot_project.py                                      godot/ with characters, props, fx, catalogs
     build_webui.py                                             the main menu's web UI (webui/, Next.js) -> godot/webui
     make_music.py                                              Quake 3's soundtrack -> Ogg (godot/library/music)
  6. Godot --import
     flag_3d_textures.py, Godot --import                        3D textures get mipmaps and VRAM compression up front
     build_cards.py                                             player card catalog + art (renders in Godot, windowed)
  7. Godot --import again, then tests/selftest.tscn             headless checks of the whole library
Player models are built separately: python remaster_model.py --all
"""
import os, sys, glob, shutil, argparse, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import q3common as q3

GODOT_CANDIDATES = q3.default_godot()

ap = argparse.ArgumentParser()
ap.add_argument("--extracted", default=os.path.join(ROOT, "extracted", "baseq3"))
ap.add_argument("--work", default=os.path.join(ROOT, "work"))
ap.add_argument("--blender")
ap.add_argument("--godot")
ap.add_argument("--skip-props", action="store_true")
ap.add_argument("--skip-world", action="store_true", help="reuse work/_world textures (still writes the Godot materials)")
ap.add_argument("--jobs", type=int, default=4, help="parallel PBR workers for world textures (mind the RAM)")
ap.add_argument("--no-godot", action="store_true")
ap.add_argument("--skip-q3core", action="store_true", help="keep the last q3core build (no C++ toolchain needed)")
ap.add_argument("--skip-card-renders", action="store_true", help="reuse the last Godot-rendered card banners")
a = ap.parse_args()


def find(cands):
    return next((c for c in cands if c and os.path.isfile(c)), None)


def run(name, args):
    print("== " + name, flush=True)
    p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    tail = "\n".join(p.stdout.strip().splitlines()[-3:])
    print("   " + tail.replace("\n", "\n   "), flush=True)
    if p.returncode != 0 or "Traceback" in p.stdout:
        sys.exit("%s failed:\n%s" % (name, p.stdout[-3000:]))


py = sys.executable
T = lambda s: os.path.join(HERE, s)
props = os.path.join(a.work, "_props")
if not a.skip_props:
    blender = find([a.blender, shutil.which("blender")] + q3.DEFAULT_BLENDER)
    if not blender:
        sys.exit("Blender not found; pass --blender")
    run("props: meshes", [py, T("build_props.py"), a.extracted, props])
    run("props: PBR maps", [py, T("make_pbr.py"), props])
    run("props: Blender export", [blender, "-b", "--factory-startup", "-P", T("build_props_blender.py"), "--",
                                  props, os.path.join(props, "export")])
if os.path.isfile(os.path.join(props, "materials.json")):
    run("props: Quake 3 shader stages", [py, T("make_prop_shaders.py"), a.extracted, props])
    run("props: view weapon tags", [py, T("make_view_weapons.py"), a.extracted, os.path.join(props, "view_weapons.json")])
run("effects", [py, T("make_fx.py"), a.extracted, os.path.join(a.work, "_fx")])
run("entity catalog", [py, T("make_entities.py"), a.extracted, os.path.join(a.work, "_entities", "entities.json")])
run("menu assets", [py, T("make_ui.py"), a.extracted, os.path.join(a.work, "_ui")])
run("cinematics", [py, T("make_cinematics.py"), a.extracted, os.path.join(a.work, "_cinematics")])
world = os.path.join(a.work, "_world")
if not a.skip_world:
    run("world: materials", [py, T("build_world.py"), a.extracted, world])
    run("world: PBR maps (%d workers)" % a.jobs, [py, T("make_pbr.py"), world, "--jobs", str(a.jobs)])
if a.no_godot:
    sys.exit(0)
out = os.path.join(ROOT, "godot")
if not a.skip_q3core:
    run("game code (q3core)", [py, T("build_q3core.py")])
run("godot project", [py, T("make_godot_project.py"), "--work", a.work, "--out", out])
run("web menu", [py, T("build_webui.py")] + (["--godot", a.godot] if a.godot else []))
if os.path.exists(os.path.join(world, "materials.json")):
    run("world: Godot materials", [py, T("make_godot_world.py"), "--work", world, "--godot", out])
run("music", [py, T("make_music.py"), a.extracted, "--godot", out])
godot = find([a.godot] + GODOT_CANDIDATES)
if not godot:
    print("Godot not found (set GODOT_PATH); open godot/project.godot in the editor to import it.")
    sys.exit(0)
log = os.path.join(a.work, "godot_import.log")
subprocess.run([godot, "--headless", "--path", out, "--import", "--log-file", log],
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
errs = [l for l in open(log, encoding="utf-8", errors="replace") if "SCRIPT ERROR" in l or l.startswith("ERROR")]
print("== godot import: %d error line(s)%s" % (len(errs), (": " + errs[0].strip()) if errs else ""))
# new textures import as 2D; flag the 3D ones now, or the editor switches them mid-import later
run("3D texture imports", [py, T("flag_3d_textures.py"), "--godot", out])
subprocess.run([godot, "--headless", "--path", out, "--import", "--log-file", log],
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
# player cards render from the imported library (characters, skies, world materials), then go back in
run("player cards", [py, T("build_cards.py"), a.extracted, os.path.join(a.work, "_cards"), "--godot", godot]
    + (["--no-render"] if a.skip_card_renders else []))
run("godot project: cards", [py, T("make_godot_project.py"), "--work", a.work, "--out", out])
subprocess.run([godot, "--headless", "--path", out, "--import", "--log-file", log],
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
log = os.path.join(a.work, "godot_selftest.log")
r = subprocess.run([godot, "--headless", "--path", out, "--quit-after", "6000", "--log-file", log,
                    "res://tests/selftest.tscn"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
lines = [l.rstrip() for l in open(log, encoding="utf-8", errors="replace") if "PASS" in l or "FAIL" in l or "SELFTEST" in l]
print("\n".join(lines))
if not any("SELFTEST" in l for l in lines):   # a script that fails to parse quits cleanly and prints nothing
    sys.exit("self-test didn't run; see %s" % log)
sys.exit(r.returncode)
