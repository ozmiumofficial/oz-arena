"""Extract files from id Tech 3 .pk3 archives the way the engine layers them.

python extract_pk3.py <baseq3 folder> <out folder> [--all] [--prefix models/ scripts/ ...]

.pk3 files are zips. They are read in the engine's order (alphabetical, so pak8 overrides
pak0) and later files win. All paths are lower-cased so lookups work on any OS.
Default prefixes: models/ and scripts/ (players, weapons, items and their shaders).
Use --all for everything (maps, textures, sound: about 500 MB for baseq3).
"""
import sys, os, zipfile, glob, argparse

ap = argparse.ArgumentParser()
ap.add_argument("baseq3")
ap.add_argument("out")
ap.add_argument("--all", action="store_true")
ap.add_argument("--prefix", nargs="*", default=["models/", "scripts/"])
a = ap.parse_args()

paks = sorted(glob.glob(os.path.join(a.baseq3, "*.pk3")), key=lambda p: os.path.basename(p).lower())
if not paks:
    sys.exit("No .pk3 files in %s" % a.baseq3)
count, seen = 0, set()
for pk in paks:
    with zipfile.ZipFile(pk) as z:
        for info in z.infolist():
            name = info.filename.replace("\\", "/").lower()
            if info.is_dir() or (not a.all and not name.startswith(tuple(a.prefix))):
                continue
            if ".." in name.split("/"):
                continue  # never write outside the output folder
            dest = os.path.join(a.out, *name.split("/"))
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with z.open(info) as src, open(dest, "wb") as dst:
                dst.write(src.read())
            if name not in seen:
                count += 1
                seen.add(name)
    print("read", os.path.basename(pk))
print("%d files in %s" % (count, a.out))
