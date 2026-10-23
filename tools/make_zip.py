"""Packs the whole project into one zip to carry to another PC:

    python tools/make_zip.py [--out C:/Users/<you>/Documents/Q3Remaster_<date>.zip]

Everything except what rebuilds itself or doesn't travel:
  godot/.godot             Godot's import cache (the editor re-imports on first open, a few minutes)
  node_modules, .next, out, .wrangler   npm installs and build output (npm ci / npm run build)
  tools/q3core/thirdparty, tools/q3core/build   refetched and rebuilt by python tools/build_q3core.py
  work/_world/tex          full-size world maps (python tools/build_library.py rebuilds them)
  __pycache__, *.old<n> DLLs, Godot's ~ copies of the loaded extension, test captures
  .env*, .dev.vars         secrets never go in the zip
Already-compressed files (png, jpg, ogv, glb, wav ...) are stored, the rest deflated. The zip is
written next to the target, then moved over it, so a failed run never leaves half a zip.
The player's own data (settings, profile, campaign, online identity) lives in Godot's user folder
(%APPDATA%/Godot/app_userdata/Q3Remaster), not in the project, so it isn't included.
"""
import argparse
import datetime
import os
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIP_DIRS = {"node_modules", ".next", ".wrangler", "__pycache__", ".git"}
SKIP_PATHS = {"build", "godot/.godot", "webui/out", "tools/q3core/thirdparty", "tools/q3core/build", "work/_world/tex",
              "work/_fx_test", "work/_pause_test", "tools/godot-mcp/node_modules"}
SKIP_NAMES = {".dev.vars"}
STORED = {".png", ".jpg", ".jpeg", ".ogv", ".ogg", ".glb", ".wav", ".mp3", ".zip", ".pk3", ".woff2", ".webp",
          ".ctex", ".lmbake", ".exr", ".res", ".scn"}


def skipped(rel_dir: str) -> bool:
    return rel_dir in SKIP_PATHS or os.path.basename(rel_dir) in SKIP_DIRS


def files():
    for d, dirs, fs in os.walk(ROOT):
        rel = os.path.relpath(d, ROOT).replace("\\", "/")
        rel = "" if rel == "." else rel
        dirs[:] = [x for x in dirs if not skipped((rel + "/" + x).lstrip("/"))]
        for f in fs:
            low = f.lower()
            if f in SKIP_NAMES or low.startswith(".env") or ".dll.old" in low or low.startswith("~") or low.endswith(".pyc"):
                continue
            yield os.path.join(d, f), (rel + "/" + f).lstrip("/")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(os.path.dirname(ROOT), "Q3Remaster_%s.zip" % datetime.date.today().isoformat()))
    a = ap.parse_args()
    tmp = a.out + ".part"
    n = 0
    total = 0
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as z:
        for path, rel in files():
            ext = os.path.splitext(rel)[1].lower()
            z.write(path, "Q3Remaster/" + rel, compress_type=zipfile.ZIP_STORED if ext in STORED else zipfile.ZIP_DEFLATED)
            n += 1
            total += os.path.getsize(path)
            if n % 2000 == 0:
                print("zip: %d files, %.1f GB read" % (n, total / 1e9), flush=True)
    os.replace(tmp, a.out)
    print("zip: %d files, %.2f GB of source -> %s (%.2f GB)" % (n, total / 1e9, a.out, os.path.getsize(a.out) / 1e9))


if __name__ == "__main__":
    sys.exit(main())
