"""Build the web main menu (webui/, Next.js + React + Tailwind + Motion) into the Godot project.

python build_webui.py [--godot <exe>] [--skip-npm]

The menu runs live inside Godot through godot_wry (a native web view, WebView2 on Windows),
layered transparently over the 3D showcase. Steps:
  1. UI assets from work/_ui (make_ui.py) -> webui/public/q3 (sounds, banner font atlas,
     levelshots, icons, crosshairs, HUD icons), work/_cards -> webui/public/q3/cards (player card art, from the
     last build_cards.py run) and webui/app/fonts/q3_prop.ttf (the traced menu font)
  2. Godot dumps the menu's init payload -> webui/public/q3/dev_state.json, so `npm run dev`
     works in a normal browser with a mock bridge
  3. npm install (first run) + npm run build -> webui/out (static export)
  4. webui/out + lib/menu.json -> godot/webui (served to the web view as res://webui/index.html; Next's
     absolute /_next/ paths resolve there because godot_wry maps res://webui/x to http://res.webui/x)
Without Node.js the previous godot/webui is kept.
"""
import os, sys, glob, shutil, argparse, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
WEB = os.path.join(ROOT, "webui")
UI = os.path.join(ROOT, "work", "_ui")
GODOT_PROJECT = os.path.join(ROOT, "godot")
sys.path.insert(0, HERE)
import q3common as q3
ap = argparse.ArgumentParser()
ap.add_argument("--godot")
ap.add_argument("--skip-npm", action="store_true")
a = ap.parse_args()


def copytree(src, dst):
    if os.path.isdir(src):
        shutil.copytree(src, dst, dirs_exist_ok=True)


pub = os.path.join(WEB, "public", "q3")
os.makedirs(pub, exist_ok=True)
for d in ("sounds", "levelshots", "icons", "crosshairs", "campaign", "hud"):
    copytree(os.path.join(UI, d), os.path.join(pub, d))
copytree(os.path.join(ROOT, "work", "_cinematics", "thumbs"), os.path.join(pub, "cinematics"))
for d in ("emblems", "banners", "frames"):   # player card art (build_cards.py)
    copytree(os.path.join(ROOT, "work", "_cards", d), os.path.join(pub, "cards", d))
os.makedirs(os.path.join(pub, "fonts"), exist_ok=True)
shutil.copy2(os.path.join(UI, "fonts", "q3_banner.png"), os.path.join(pub, "fonts", "q3_banner.png"))
os.makedirs(os.path.join(WEB, "app", "fonts"), exist_ok=True)
shutil.copy2(os.path.join(UI, "fonts", "q3_prop.ttf"), os.path.join(WEB, "app", "fonts", "q3_prop.ttf"))
print("webui: assets copied")

godot = next((c for c in [a.godot] + q3.default_godot() if c and os.path.isfile(c)), None)
if godot:
    state = os.path.join(pub, "dev_state.json")
    log = os.path.join(ROOT, "work", "godot_dump_state.log")
    subprocess.run([godot, "--headless", "--path", GODOT_PROJECT, "--quit-after", "600", "--log-file", log,
                    "res://tests/dump_menu_state.tscn", "--", "out=" + state],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print("webui: dev state", "written" if os.path.isfile(state) else "FAILED (see %s)" % log)
else:
    print("webui: Godot not found; dev_state.json not refreshed")

npm = shutil.which("npm")
if a.skip_npm or not npm:
    print("webui: %s; keeping the previous godot/webui" % ("--skip-npm" if a.skip_npm else "npm not found"))
    sys.exit(0)
if not os.path.isdir(os.path.join(WEB, "node_modules")):
    subprocess.check_call([npm, "ci" if os.path.isfile(os.path.join(WEB, "package-lock.json")) else "install"], cwd=WEB)
p = subprocess.run([npm, "run", "build"], cwd=WEB, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
if p.returncode != 0:
    sys.exit("webui: build failed\n" + p.stdout[-4000:])
out = os.path.join(WEB, "out")
dst = os.path.join(GODOT_PROJECT, "webui")
if os.path.isdir(dst):
    # keep Godot's .import files for unchanged assets; drop everything else so stale chunks go
    for f in glob.glob(os.path.join(dst, "**", "*"), recursive=True):
        if os.path.isfile(f) and not f.endswith(".import"):
            os.remove(f)
shutil.copytree(out, dst, dirs_exist_ok=True)
# the menu tree, for the Godot self-test (every page built, every action handled)
shutil.copy2(os.path.join(WEB, "lib", "menu.json"), os.path.join(dst, "menu.json"))
n = sum(len(fs) for _, _, fs in os.walk(out))
print("webui: built %d files -> %s" % (n, dst))
