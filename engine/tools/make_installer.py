"""The Windows installer: a staged build/engine/ packed into one setup program with Inno Setup 6.

python engine/tools/make_installer.py [--stage build/engine] [--out <setup exe>] [--compression lzma2/normal|none]
                                      [--app-id OzArena] [--iscc <ISCC.exe>]

What goes in (FILES): OzArena.exe, OzArena.ded.exe, the renderer modules, OpenAL64.dll, SDL2.dll, oz/*.pk3,
the map editor's compile tools (tools/, when staged) and the LICENSE. Nothing of id's: a baseq3 folder or
an id pak in the stage stops the run. The version is OZ_VERSION (engine/quake3e/code/qcommon/q_shared.h),
and the staged OzArena.exe must carry it, so the installer never says a version the game isn't.

The result: build/installer/OzArena-<version>-setup.exe. engine/installer/OzArena.iss says what it does
(the Quake III Arena folder as the default place, an uninstaller, a Start Menu shortcut, silent installs
with /VERYSILENT, upgrades that drop the paks this version no longer ships).

Inno Setup 6: winget install JRSoftware.InnoSetup (per user, no elevation).
"""
import argparse
import glob
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ISS = os.path.join(ROOT, "engine", "installer", "OzArena.iss")
ICON = os.path.join(ROOT, "engine", "quake3e", "code", "win32", "oz.ico")
Q_SHARED = os.path.join(ROOT, "engine", "quake3e", "code", "qcommon", "q_shared.h")
FILES = ["OzArena.exe", "OzArena.ded.exe", "OpenAL64.dll", "SDL2.dll"]   # plus the patterns below
PATTERNS = ["OzArena_*_x86_64.dll", "oz/*.pk3", "tools/*"]
ID_PAK = re.compile(r"^pak[0-8]\.pk3$", re.I)

ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
ap.add_argument("--stage", default=os.path.join(ROOT, "build", "engine"), help="the staged build folder")
ap.add_argument("--out", help="the setup exe to write (default build/installer/OzArena-<version>-setup.exe)")
ap.add_argument("--compression", default="lzma2/normal", help="Inno Setup's Compression (none: a quick build)")
ap.add_argument("--app-id", default="OzArena", help="the install's identity in the registry (tests use their own)")
ap.add_argument("--iscc", help="Inno Setup's ISCC.exe")
a = ap.parse_args()


def find_iscc():
    if a.iscc:
        return a.iscc
    for base in (os.environ.get("LOCALAPPDATA", ""), os.environ.get("ProgramFiles(x86)", ""), os.environ.get("ProgramFiles", "")):
        for sub in (os.path.join("Programs", "Inno Setup 6"), "Inno Setup 6"):
            p = os.path.join(base, sub, "ISCC.exe")
            if base and os.path.exists(p):
                return p
    sys.exit("no Inno Setup 6: winget install JRSoftware.InnoSetup")


def version():
    m = re.search(r'#define\s+OZ_VERSION\s+"([^"]+)"', open(Q_SHARED, encoding="latin-1").read())
    if not m:
        sys.exit("no OZ_VERSION in q_shared.h")
    return m.group(1)


def version_numbers(v):
    """0.7.0-beta -> 0.7.0.0 for Windows' version info"""
    nums = [int(n) for n in re.findall(r"\d+", v.split("-")[0])][:4]
    return ".".join(str(n) for n in (nums + [0, 0, 0, 0])[:4])


def staged_files(stage):
    files = []
    for name in FILES:
        if not os.path.exists(os.path.join(stage, name)):
            sys.exit("%s isn't staged in %s: python engine/build_engine.py" % (name, stage))
        files.append(name)
    for pattern in PATTERNS:
        for p in sorted(glob.glob(os.path.join(stage, pattern))):
            if os.path.isfile(p):
                files.append(os.path.relpath(p, stage).replace("\\", "/"))
    if not any(f == "oz/pak_oz_code.pk3" for f in files):
        sys.exit("oz/pak_oz_code.pk3 isn't staged")
    for f in files:
        if "baseq3" in f.lower().split("/") or ID_PAK.match(os.path.basename(f)):
            sys.exit("%s is id's: the installer never carries the game's own files" % f)
    if os.path.isdir(os.path.join(stage, "baseq3")):
        sys.exit("a baseq3 folder is in %s: the installer never carries the game's own files" % stage)
    return files


def main():
    iscc = find_iscc()
    stage = os.path.abspath(a.stage)
    v = version()
    exe = open(os.path.join(stage, "OzArena.exe"), "rb").read()
    if ("Oz Arena %s (" % v).encode() not in exe:
        sys.exit("the staged OzArena.exe doesn't say %s: python engine/build_engine.py" % v)
    files = staged_files(stage)
    out = os.path.abspath(a.out or os.path.join(ROOT, "build", "installer", "OzArena-%s-setup.exe" % v))
    if not out.lower().endswith(".exe"):
        sys.exit("--out names the setup exe")
    os.makedirs(os.path.dirname(out), exist_ok=True)

    inc = os.path.splitext(out)[0] + ".files.iss"
    with open(inc, "w", encoding="utf-8") as f:
        f.write("; made by engine/tools/make_installer.py from %s\n" % stage)
        for rel in files:
            dest = "{app}" + ("\\" + os.path.dirname(rel).replace("/", "\\") if "/" in rel else "")
            f.write('Source: "%s"; DestDir: "%s"; Flags: ignoreversion\n' % (os.path.join(stage, rel.replace("/", "\\")), dest))
        f.write('Source: "%s"; DestDir: "{app}"; Flags: ignoreversion\n' % os.path.join(ROOT, "LICENSE"))
    keep = ";".join(os.path.basename(f) for f in files if f.startswith("oz/") or (f.startswith("OzArena_") and f.endswith(".dll")))

    size = sum(os.path.getsize(os.path.join(stage, f)) for f in files)
    print("%d files, %.2f GB, version %s -> %s (%s)" % (len(files), size / 1e9, v, out, a.compression))
    cmd = [iscc, "/Qp", "/O" + os.path.dirname(out), "/F" + os.path.splitext(os.path.basename(out))[0],
           "/DStage=" + stage, "/DVersion=" + v, "/DVersionNum=" + version_numbers(v), "/DAppId=" + a.app_id,
           "/DFilesInc=" + inc, "/DKeep=" + keep, "/DIcon=" + ICON, "/DCompression=" + a.compression, ISS]
    r = subprocess.run(cmd)
    if r.returncode:
        sys.exit("ISCC failed (%d)" % r.returncode)
    print("%s: %.2f GB" % (out, os.path.getsize(out) / 1e9))


if __name__ == "__main__":
    main()
