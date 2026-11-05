"""The Linux game's release file: a staged build/linux/ packed as OzArena-<version>-linux.tar.gz.

python engine/tools/make_linux_release.py [--stage build/linux] [--out <tar.gz>]

Runs on Windows or Linux. What goes in: OzArena.x86_64, OzArena.ded.x86_64, the renderer modules
(OzArena_<renderer>_x86_64.so), libSDL2-2.0.so.0, libopenal.so.1, oz/*.pk3, oz-arena-launcher,
oz-arena.png, the LICENSE and the map editor's tools/ (q3map2 and mbspc with what they load, from
engine/tools/linux_editor_tools.py), under one OzArena/ folder (as the Windows zip), with the programs
executable whatever disk they were staged on. The world packs go up as release files of their own,
as for Windows; the launcher fetches them beside this.

It stops when: an id pak or a baseq3 folder is staged; the staged OzArena.x86_64 doesn't carry
OZ_VERSION (engine/quake3e/code/qcommon/q_shared.h); or a staged oz/ pak differs from the main
build's (build/engine/oz), since a pure server checks paks by checksum and the Windows and Linux
games must carry the same ones. Ozmium's local-only CZ45 paks (pak_oz_zz_cz*) never go in.
"""
import argparse
import filecmp
import os
import re
import sys
import tarfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
Q_SHARED = os.path.join(ROOT, "engine", "quake3e", "code", "qcommon", "q_shared.h")
MAIN_OZ = os.path.join(ROOT, "build", "engine", "oz")
FILES = ["OzArena.x86_64", "OzArena.ded.x86_64", "libSDL2-2.0.so.0", "libopenal.so.1", "oz-arena-launcher",
         "oz-arena.png", "LICENSE"]
PROGRAMS = {"OzArena.x86_64", "OzArena.ded.x86_64", "oz-arena-launcher"}
ID_PAK = re.compile(r"^pak[0-8]\.pk3$", re.I)
WORLD = re.compile(r"^pak_oz_world\d*\.pk3$", re.I)


def version():
    m = re.search(r'#define\s+OZ_VERSION\s+"([^"]+)"', open(Q_SHARED, encoding="latin-1").read())
    if not m:
        sys.exit("no OZ_VERSION in q_shared.h")
    return m.group(1)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--stage", default=os.path.join(ROOT, "build", "linux"))
    ap.add_argument("--out", help="default build/release/OzArena-<version>-linux.tar.gz")
    a = ap.parse_args()
    stage = os.path.abspath(a.stage)
    v = version()

    files = []
    for name in FILES:
        if not os.path.isfile(os.path.join(stage, name)):
            sys.exit("%s isn't staged in %s: python3 engine/build_engine.py on Linux" % (name, stage))
        files.append(name)
    files += sorted(f for f in os.listdir(stage) if re.match(r"^OzArena_\w+_x86_64\.so$", f))
    # the map editor's compile tools (linux_editor_tools.py): the editor can't compile maps without them
    tools = os.path.join(stage, "tools")
    for t in ("q3map2", "mbspc", "bin/q3map2.x86_64", "bin/mbspc.x86_64", "lib/ld-linux-x86-64.so.2"):
        if not os.path.isfile(os.path.join(tools, t)):
            sys.exit("tools/%s isn't staged: python engine/build_engine.py --linux" % t)
    files += sorted(os.path.relpath(os.path.join(d, f), stage).replace("\\", "/") for d, _, fs in os.walk(tools) for f in fs)
    if not any(f.endswith(".so") and f.startswith("OzArena_oz_") for f in files):
        sys.exit("the oz renderer (OzArena_oz_x86_64.so) isn't staged")
    for f in sorted(os.listdir(os.path.join(stage, "oz"))):
        if not f.lower().endswith(".pk3") or WORLD.match(f) or f.startswith("pak_oz_zz_cz"):
            continue
        main_pak = os.path.join(MAIN_OZ, f)
        if os.path.isdir(MAIN_OZ) and (not os.path.isfile(main_pak) or not filecmp.cmp(main_pak, os.path.join(stage, "oz", f), shallow=False)):
            sys.exit("oz/%s isn't the main build's: rebuild on Linux after the Windows build" % f)
        files.append("oz/" + f)
    if "oz/pak_oz_code.pk3" not in files:
        sys.exit("oz/pak_oz_code.pk3 isn't staged")
    for f in files:
        if ID_PAK.match(os.path.basename(f)) or "baseq3" in f.lower().split("/"):
            sys.exit("%s is id's: the release never carries the game's own files" % f)
    if os.path.isdir(os.path.join(stage, "baseq3")):
        sys.exit("a baseq3 folder is in %s: the release never carries the game's own files" % stage)
    exe = open(os.path.join(stage, "OzArena.x86_64"), "rb").read()
    if b"\0" + v.encode() + b"\0" not in exe:   # OZ_VERSION, a string of its own (common.c puts the line together)
        sys.exit("the staged OzArena.x86_64 doesn't say %s: rebuild it on Linux" % v)

    out = os.path.abspath(a.out or os.path.join(ROOT, "build", "release", "OzArena-%s-linux.tar.gz" % v))
    os.makedirs(os.path.dirname(out), exist_ok=True)

    def norm(info, name):
        info.uid = info.gid = 0
        info.uname = info.gname = ""
        program = name in PROGRAMS or name.endswith(".so") or ".so." in name or (name.startswith("tools/") and not name.endswith(".txt"))
        info.mode = 0o755 if program else 0o644
        return info

    with tarfile.open(out + ".tmp", "w:gz", compresslevel=6) as t:
        for f in files:
            t.add(os.path.join(stage, f), "OzArena/" + f, filter=lambda info, f=f: norm(info, f))
    os.replace(out + ".tmp", out)
    size = sum(os.path.getsize(os.path.join(stage, f)) for f in files)
    print("%d files, %.2f GB staged, version %s -> %s (%.2f GB)" % (len(files), size / 1e9, v, out, os.path.getsize(out) / 1e9))


if __name__ == "__main__":
    main()
