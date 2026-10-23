"""Mark the library's 3D textures as 3D in their Godot .import files, the way the editor's own
detection does (VRAM compressed, mipmaps, normal maps as RGTC with the roughness limiter off).

python flag_3d_textures.py [--godot ../godot]     then let Godot reimport (--import, or a scan)

Godot imports every new texture as 2D (no mipmaps, uncompressed, detect_3d pending) and only
switches it when a 3D material first uses it in the editor. That switch is a reimport started in
the middle of another one (a GLB's import loading its materials), which prints "Task 'reimport'
already exists" or, under a scan, "Do not use progress dialog (task) while flushing the message
queue" and a page of progress_dialog.cpp errors. Textures nobody opened in the editor shipped
without mipmaps. Flagging them up front avoids both.

Characters, props, fx and maps are all 3D. World textures count when a resource in the library
uses them (the unused black emissive maps stay as they are).
"""
import os, re, sys, glob, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

ap = argparse.ArgumentParser()
ap.add_argument("--godot", default=os.path.join(ROOT, "godot"))
ap.add_argument("--dry-run", action="store_true")
a = ap.parse_args()
lib = os.path.join(a.godot, "library")


def res(path):
    return "res://" + os.path.relpath(path, a.godot).replace(os.sep, "/")


imports = []
for d in ("characters", "props", "fx", "maps"):
    imports += glob.glob(os.path.join(lib, d, "**", "*.import"), recursive=True)
used = set()
for f in glob.glob(os.path.join(lib, "**", "*.tres"), recursive=True) + glob.glob(os.path.join(lib, "**", "*.tscn"), recursive=True):
    used.update(re.findall(r'path="(res://library/world/[^"]+\.(?:jpg|png|tga))"', open(f, encoding="utf-8", errors="replace").read()))
imports += [p for p in glob.glob(os.path.join(lib, "world", "**", "*.import"), recursive=True) if res(p[:-7]) in used]

changed = 0
for imp in imports:
    txt = open(imp, encoding="utf-8").read()
    if 'importer="texture"' not in txt or "\ndetect_3d/compress_to=1" not in txt or "\ncompress/mode=0" not in txt:
        continue   # not a texture, or Godot already decided
    want = {"compress/mode": "2", "mipmaps/generate": "true", "detect_3d/compress_to": "0"}
    if re.search(r"_normal\.\w+$", imp[:-7]):   # as Godot sets them for a StandardMaterial3D's normal map
        want.update({"compress/normal_map": "1", "roughness/mode": "1", "roughness/src_normal": '"%s"' % res(imp[:-7])})
    new = txt
    for k, v in want.items():
        new = re.sub(r"(?m)^%s=.*$" % re.escape(k), "%s=%s" % (k, v), new)
    if new != txt:
        changed += 1
        if not a.dry_run:
            with open(imp, "w", encoding="utf-8", newline="\n") as f:
                f.write(new)
print("%d of %d texture imports flagged as 3D%s" % (changed, len(imports), " (dry run)" if a.dry_run else ""))
