"""Exports the game as a Windows build to play outside the editor.

python export_game.py [--out ../build/windows] [--godot <godot exe>] [--release v0.1.0] [--debug]

  <out>/Q3Remaster.exe          the game: Godot's release template with q3core's template_release
                                library (python build_q3core.py --release); --debug exports the debug
                                template and template_debug instead
  <out>/Q3Remaster.console.exe  the same with a console window for its log
  <out>/Q3Remaster.pck          scripts, scenes, the remastered library, the web menu (godot/export_presets.cfg)
  <out>/*.dll                   the GDExtensions (q3core, godot_wry), placed by the export
  <out>/q3core/baseq3/qagamex86_64.dll   id's game module, which the engine loads from disk (Q3Host.bin_path)
  <out>/q3core/tools/q3map.exe, bspc.exe   id's map compiler and bot-area compiler, which the map
                                editor runs for play-tests (Q3MapCompiler.tool_path)
  <out>/webui_assets.pck        the web menu's images, sounds and fonts as plain files (Q3WebLayer mounts it)
  <out>/textures-1.pck, -2.pck  the world textures, left out of Q3Remaster.pck so every file is under
                                GitHub's 2 GiB release limit (Q3._init mounts them)
  <out>/q3core/baseq3/pak1-8.pk3   id's free 1.32 point release (from --paks), so the player needs only
                                pak0.pk3: the engine refuses to start without all nine (files.c FS_CheckPak0)
  <out>/vcruntime140*.dll       the VC++ runtime godot_wry needs, so players don't need the redistributable
--release <tag> also writes build/release/<tag>/: Q3Remaster-<tag>-windows.zip (everything but the
texture packs) plus the texture packs themselves, the files a GitHub release carries.
The player's own Quake 3 install is still found as in the editor (fs_steampath), and pak0 is checked.
Close the Godot editor's project or let it be: the export only reads the project.
"""
import os, sys, glob, shutil, zipfile, argparse, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import q3common as q3

ap = argparse.ArgumentParser()
ap.add_argument("--out", default=os.path.join(ROOT, "build", "windows"))
ap.add_argument("--godot")
ap.add_argument("--release")
ap.add_argument("--debug", action="store_true", help="export Godot's debug template (q3core template_debug)")
ap.add_argument("--paks", default=r"C:\Program Files (x86)\Steam\steamapps\common\Quake 3 Arena\baseq3",
                help="a baseq3 folder with the 1.32 point release's pak1-8.pk3")
a = ap.parse_args()

project = os.path.join(ROOT, "godot")
godot = next((g for g in [a.godot] + q3.default_godot() if g and os.path.isfile(g)), None)
if not godot:
    sys.exit("export_game: Godot not found (pass --godot)")
target = "template_debug" if a.debug else "template_release"
lib = os.path.join(project, "addons", "q3core", "bin", "q3core.windows.%s.x86_64.dll" % target)
if not os.path.isfile(lib):
    sys.exit("export_game: %s is missing (python build_q3core.py%s builds it)" % (lib, "" if a.debug else " --release"))
# from an empty folder: the release zip takes everything in it, and a debug export's libraries left
# beside a release one would ship too
if os.path.abspath(a.out).startswith(os.path.join(ROOT, "build") + os.sep):
    shutil.rmtree(a.out, ignore_errors=True)
if a.release:
    # (its hard links would keep the old texture packs on disk through the whole export)
    shutil.rmtree(os.path.join(ROOT, "build", "release", a.release), ignore_errors=True)
free = shutil.disk_usage(os.path.dirname(os.path.abspath(a.out)) or ".").free
if free < 6 * 1024 ** 3:
    sys.exit("export_game: %.1f GB free; the export needs about 6 (a full disk cut a texture pack short "
             "without an error once)" % (free / 1024 ** 3))
os.makedirs(a.out, exist_ok=True)
exe = os.path.join(a.out, "Q3Remaster.exe")
log = os.path.join(a.out, "export.log")
print("exporting (%s) to" % ("debug" if a.debug else "release"), exe)
subprocess.run([godot, "--headless", "--path", project, "--export-debug" if a.debug else "--export-release", "Windows Desktop", exe,
                "--log-file", log],
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
if not os.path.isfile(exe) or not os.path.isfile(os.path.join(a.out, "Q3Remaster.pck")):
    sys.exit("export_game: the export failed; see " + log)
game = os.path.join(project, "addons", "q3core", "bin", "baseq3")
dst = os.path.join(a.out, "q3core", "baseq3")
os.makedirs(dst, exist_ok=True)
for f in os.listdir(game):
    if f.endswith(".dll"):
        shutil.copy2(os.path.join(game, f), os.path.join(dst, f))
tools = os.path.join(project, "addons", "q3core", "bin", "tools")
os.makedirs(os.path.join(a.out, "q3core", "tools"), exist_ok=True)
for f in ("q3map.exe", "bspc.exe"):
    if not os.path.isfile(os.path.join(tools, f)):
        sys.exit("export_game: %s is missing (python tools/build_q3core.py builds it)" % os.path.join(tools, f))
    shutil.copy2(os.path.join(tools, f), os.path.join(a.out, "q3core", "tools", f))
for i in range(1, 9):
    src = os.path.join(a.paks, "pak%d.pk3" % i)
    if not os.path.isfile(src):
        sys.exit("export_game: %s is missing (pass --paks <baseq3 folder>)" % src)
    shutil.copy2(src, os.path.join(dst, "pak%d.pk3" % i))
# the web menu's images, sounds and fonts as plain files (the export ships only Godot's imports)
pack = os.path.join(a.out, "webui_assets.pck")
r = subprocess.run([godot, "--headless", "--path", project, "--quit-after", "3000", "--log-file", os.path.join(a.out, "web_assets.log"),
                    "res://tests/pack_web_assets.tscn", "--", "out=" + pack.replace("\\", "/")],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
if r.returncode != 0 or not os.path.isfile(pack):
    sys.exit("export_game: packing the web menu's assets failed; see web_assets.log")
# the world textures as their own packs (export_presets.cfg excludes them from Q3Remaster.pck)
r = subprocess.run([godot, "--headless", "--path", project, "--quit-after", "20000", "--log-file", os.path.join(a.out, "textures.log"),
                    "res://tests/pack_textures.tscn", "--", "out=" + a.out.replace("\\", "/")],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
packs = sorted(f for f in os.listdir(a.out) if f.startswith("textures-") and f.endswith(".pck"))
if r.returncode != 0 or not packs:
    sys.exit("export_game: packing the world textures failed; see textures.log")
# PCKPacker reports no write errors: a full disk once left textures-1.pck 160 MB short. The packs
# on disk must hold what the packer says it wrote ("TEXTURE PACKS ok: <n> textures, <MB> MB ...")
said = [l for l in open(os.path.join(a.out, "textures.log"), encoding="utf-8", errors="replace") if "TEXTURE PACKS ok" in l]
mb = float(said[-1].split(" textures, ")[1].split(" MB")[0]) if said else -1.0
got = sum(os.path.getsize(os.path.join(a.out, f)) for f in packs) / 1e6   # (the packer counts MB as 1e6 bytes)
if mb < 0 or abs(got - mb) > max(4.0, mb * 0.002):
    sys.exit("export_game: the texture packs hold %.0f MB, but the packer wrote %.0f MB (a full disk?)" % (got, mb))
LIMIT = 2 * 1024 ** 3 - 16 * 1024 ** 2   # GitHub release files must be under 2 GiB
for f in ["Q3Remaster.pck"] + packs:
    if os.path.getsize(os.path.join(a.out, f)) >= LIMIT:
        sys.exit("export_game: %s is over GitHub's 2 GiB release limit; raise Q3.TEXTURE_PACKS" % f)
# the VC++ runtime (godot_wry links it); Microsoft allows these files app-local
crt = sorted(glob.glob(r"C:\Program Files*\Microsoft Visual Studio\*\*\VC\Redist\MSVC\*\x64\Microsoft.VC14*.CRT"))
for f in ["vcruntime140.dll", "vcruntime140_1.dll"]:
    if crt and os.path.isfile(os.path.join(crt[-1], f)):
        shutil.copy2(os.path.join(crt[-1], f), os.path.join(a.out, f))
    else:
        print("export_game: warning: %s not found (install the MSVC build tools)" % f)
size = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(a.out) for f in fs)
print("exported: %s (%.1f GB)" % (a.out, size / 1e9))

if a.release:
    rel = os.path.join(ROOT, "build", "release", a.release)
    shutil.rmtree(rel, ignore_errors=True)
    os.makedirs(rel)
    zpath = os.path.join(rel, "Q3Remaster-%s-windows.zip" % a.release)
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for d, _, fs in os.walk(a.out):
            for f in fs:
                if f.endswith(".log") or f in packs:
                    continue
                full = os.path.join(d, f)
                z.write(full, os.path.join("Q3Remaster", os.path.relpath(full, a.out)))
        z.write(os.path.join(HERE, "release_readme.txt"), "Q3Remaster/README.txt")
    for f in packs:
        # hard links (same drive): the packs are 2.5 GB, and a copy once filled the disk
        try:
            os.link(os.path.join(a.out, f), os.path.join(rel, f))
        except OSError:
            shutil.copy2(os.path.join(a.out, f), os.path.join(rel, f))
    for f in sorted(os.listdir(rel)):
        print("  %-36s %7.1f MB" % (f, os.path.getsize(os.path.join(rel, f)) / 1e6))
    if any(os.path.getsize(os.path.join(rel, f)) >= LIMIT for f in os.listdir(rel)):
        sys.exit("export_game: a release file is over GitHub's 2 GiB limit")
    print("release files:", rel)
