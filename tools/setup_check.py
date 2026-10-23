"""Check that everything the remaster tools need is installed.

python tools/setup_check.py [--blender PATH] [--q3 PATH]
"""
import sys, os, shutil, subprocess, importlib, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from q3common import DEFAULT_BLENDER, DEFAULT_Q3  # noqa: E402  (same search lists as remaster_model.py)

ap = argparse.ArgumentParser()
ap.add_argument("--blender")
ap.add_argument("--q3")
a, _ = ap.parse_known_args()
ok = True


def line(good, what, detail=""):
    global ok
    ok &= good
    print("  [%s] %-22s %s" % ("ok" if good else "!!", what, detail))


print("Q3Remaster setup check")
line(sys.version_info >= (3, 10), "Python", sys.version.split()[0] + ("" if sys.version_info >= (3, 10) else " (need 3.10+)"))
for mod, pipname in (("numpy", "numpy"), ("scipy", "scipy"), ("cv2", "opencv-python"), ("PIL", "pillow")):
    try:
        m = importlib.import_module(mod)
        line(True, pipname, getattr(m, "__version__", ""))
    except ImportError:
        line(False, pipname, "missing: pip install -r tools/requirements.txt")

bl = next((c for c in [a.blender, shutil.which("blender")] + DEFAULT_BLENDER if c and os.path.isfile(c)), None)
if bl:
    try:
        v = subprocess.run([bl, "-b", "--version"], capture_output=True, text=True, timeout=60).stdout.splitlines()[0]
    except Exception:
        v = "(version check failed)"
    line(True, "Blender", "%s  %s" % (v, bl))
else:
    line(False, "Blender", "not found: install it or pass --blender PATH to remaster_model.py")

ext = os.path.join(ROOT, "extracted", "baseq3", "models", "players")
q3 = next((c for c in [a.q3] + DEFAULT_Q3 if c and os.path.isfile(os.path.join(c, "pak0.pk3"))), None)
if os.path.isdir(ext):
    line(True, "Extracted models", ext)
if q3:
    line(True, "Quake 3 install", q3)
elif not os.path.isdir(ext):
    line(False, "Quake 3 install", "not found: pass --q3 <your baseq3 folder> to remaster_model.py")
print("Ready. Next: python tools/remaster_model.py --list" if ok else "Fix the items marked !! above.")
