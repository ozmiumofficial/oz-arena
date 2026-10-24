"""Build q3core, the GDExtension that runs id's GPL Quake III Arena game code inside Godot.

python build_q3core.py [--release] [--clean] [--jobs 4]

1. Fetches the pinned sources into tools/q3core/thirdparty (once):
     ioq3/      ioquake3, the 64-bit-clean GPL code the extension compiles
     godot-cpp/ the C++ bindings, built against Godot 4.7's API
     id-q3a/    id's original 1.32 release, the reference for auditing ioquake3's game-code changes,
                and the source of the map editor's q3map and bspc
   then applies tools/q3core/patches/*.patch to ioq3: id's 1.32 behavior wherever ioquake3 changed
   gameplay (q3core/AUDIT_ioq3_vs_id.md), and patches/id-q3a/*.patch to id-q3a (the tools' 64-bit
   fixes and pak path). Already-applied patches are left alone.
2. Builds bin/q3core.windows.template_debug.x86_64.dll (and template_release with --release)
   with SCons and MSVC, with debug symbols (the .pdb stays in bin/ for reading crash dumps), and
   bin/tools/q3map.exe and bspc.exe (SConstruct.tools).
3. Copies the addon into tools/godot_template/addons/q3core and godot/addons/q3core.
   The extension is reloadable, so the open editor picks up a new build (Godot loads a copy of the DLL).
"""
import os, sys, json, shutil, subprocess, argparse, glob

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CORE = os.path.join(HERE, "q3core")
TP = os.path.join(CORE, "thirdparty")
PINS = json.load(open(os.path.join(CORE, "thirdparty.json")))

ap = argparse.ArgumentParser()
ap.add_argument("--release", action="store_true", help="also build template_release")
ap.add_argument("--clean", action="store_true")
ap.add_argument("--jobs", type=int, default=4, help="compile jobs (4 is safe on 16 GB)")
a = ap.parse_args()


def run(cmd, cwd):
    print(">", " ".join(cmd))
    subprocess.run(cmd, cwd=cwd, check=True)


def fetch(name, pin):
    """Shallow checkout of one pinned commit; leaves an existing checkout at that commit alone."""
    d = os.path.join(TP, name)
    if os.path.isdir(os.path.join(d, ".git")):
        head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=d, capture_output=True, text=True).stdout.strip()
        if head == pin["commit"]:
            return
    else:
        os.makedirs(d, exist_ok=True)
        run(["git", "init", "-q"], d)
        run(["git", "remote", "add", "origin", pin["url"]], d)
    run(["git", "fetch", "-q", "--depth", "1", "origin", pin["commit"]], d)
    run(["git", "checkout", "-q", "-f", "FETCH_HEAD"], d)   # -f: drops old patches; reapplied below


def apply_patches(d, patch_dir):
    for patch in sorted(glob.glob(os.path.join(patch_dir, "*.patch"))):
        if subprocess.run(["git", "apply", "--reverse", "--check", patch], cwd=d, capture_output=True).returncode == 0:
            continue   # already applied
        print("> patch", os.path.basename(patch))
        r = subprocess.run(["git", "apply", "--whitespace=nowarn", patch], cwd=d, capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit("patch %s doesn't apply to %s:\n%s" % (os.path.basename(patch), d, r.stderr))


for name, pin in PINS.items():
    if not name.startswith("_"):
        fetch(name, pin)
apply_patches(os.path.join(TP, "ioq3"), os.path.join(CORE, "patches"))
# id's map tools (q3map, bspc) for the map editor: 64-bit fixes, and reading the player's paks
apply_patches(os.path.join(TP, "id-q3a"), os.path.join(CORE, "patches", "id-q3a"))

hdr = os.path.join(CORE, "src", "ioq3_commit.h")
text = '/* written by tools/build_q3core.py */\n#define IOQ3_COMMIT "ioquake3 %s"\n' % PINS["ioq3"]["commit"][:12]
if not os.path.exists(hdr) or open(hdr).read() != text:
    open(hdr, "w").write(text)

try:
    import SCons  # noqa: F401
except ImportError:
    run([sys.executable, "-m", "pip", "install", "scons"], CORE)

targets = ["template_debug"] + (["template_release"] if a.release else [])
for t in targets:
    # debug_symbols: a .pdb beside each DLL, so crash dumps (Diagnostics) resolve q3core's frames
    cmd = [sys.executable, "-m", "SCons", "platform=windows", "target=" + t, "debug_symbols=yes", "-j%d" % a.jobs]
    if a.clean:
        cmd.append("-c")
    run(cmd, CORE)
# q3map.exe and bspc.exe (SConstruct.tools): separate programs, so a compile can't take the game down
cmd = [sys.executable, "-m", "SCons", "-f", "SConstruct.tools", "-j%d" % a.jobs]
if a.clean:
    cmd.append("-c")
run(cmd, CORE)

# The addon: the .gdextension (kept in the template) plus the built libraries.
for dst_root in (os.path.join(HERE, "godot_template"), os.path.join(ROOT, "godot")):
    if not os.path.isdir(dst_root):
        continue
    dst = os.path.join(dst_root, "addons", "q3core")
    os.makedirs(os.path.join(dst, "bin"), exist_ok=True)
    for f in glob.glob(os.path.join(CORE, "bin", "q3core.*")) + glob.glob(os.path.join(CORE, "bin", "baseq3", "qagame*")) +             glob.glob(os.path.join(CORE, "bin", "tools", "*.exe")):
        if f.endswith((".dll", ".so", ".dylib", ".exe")):
            rel = os.path.relpath(f, os.path.join(CORE, "bin"))
            os.makedirs(os.path.dirname(os.path.join(dst, "bin", rel)), exist_ok=True)
            target = os.path.join(dst, "bin", rel)
            if os.path.exists(target) and os.path.getsize(target) == os.path.getsize(f) and \
                    open(target, "rb").read() == open(f, "rb").read():
                continue   # unchanged: leave a DLL the editor has loaded alone
            try:
                shutil.copy2(f, target)
            except PermissionError:
                # A running Godot has it loaded. Windows lets a loaded DLL be renamed, so move it
                # aside and put the new one in place; the editor's extension reload picks it up.
                for old in glob.glob(target + ".old*"):
                    try:
                        os.remove(old)
                    except OSError:
                        pass
                aside = target + ".old%d" % len(glob.glob(target + ".old*"))
                os.replace(target, aside)
                shutil.copy2(f, target)
                print("  in use: moved aside to", os.path.basename(aside))
    src_ext = os.path.join(HERE, "godot_template", "addons", "q3core", "q3core.gdextension")
    if dst_root != os.path.join(HERE, "godot_template"):
        shutil.copy2(src_ext, os.path.join(dst, "q3core.gdextension"))
    print("addon ->", dst)
