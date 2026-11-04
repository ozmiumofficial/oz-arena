"""Oz Arena's web installer: engine/installer/setup built into build/installer/OzArenaSetup.exe.

python engine/tools/make_setup.py [--out <exe>]

A small setup program (about 1 MB, .NET Framework 4.8, which every Windows 10 and 11 has) in the
menus' look. It finds the player's Quake III Arena as the engine does (Steam and every Steam library,
GOG, the usual folders; Locate for any other), installs into a folder of its own inside it ("Oz Arena",
so the engine reads baseq3 from the folder above), and downloads GitHub's latest release of
ozmiumofficial/oz-arena: the game zip and every pk3 asset (the world packs) into oz\. Paks already in
place at the release's size aren't downloaded again; a stopped download carries on where it stopped.
With the release's files beside it (OzArena-<version>-windows.zip and the pak_oz_*.pk3), it installs
from those instead, offline.

It adds a Start Menu shortcut (and a desktop one if asked), the ozarena:// links, and an entry in
Apps & features whose uninstaller ("Uninstall Oz Arena.exe" in the install) removes what Setup put
there and nothing of the player's (Maps, %APPDATA%\\Oz Arena). A folder Setup can't write (Program
Files) makes it start again with administrator rights.

The setup never changes with a release: upload it to each release beside the zip and the world packs.
engine/installer/OzArena.iss (make_installer.py) is the older all-in-one Inno Setup installer, too big
for a GitHub release asset (2 GB each).

Needs the .NET SDK (any from 6 on): winget install Microsoft.DotNet.SDK.8
"""
import argparse
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PROJECT = os.path.join(ROOT, "engine", "installer", "setup")

ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
ap.add_argument("--out", default=os.path.join(ROOT, "build", "installer", "OzArenaSetup.exe"), help="the setup exe to write")
a = ap.parse_args()


def main():
    dotnet = shutil.which("dotnet")
    if not dotnet:
        sys.exit("no .NET SDK: winget install Microsoft.DotNet.SDK.8")
    r = subprocess.run([dotnet, "build", "-c", "Release", "-nologo", "-v", "q", PROJECT])
    if r.returncode:
        sys.exit("the build failed (%d)" % r.returncode)
    exe = os.path.join(PROJECT, "bin", "Release", "net48", "OzArenaSetup.exe")
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    shutil.copyfile(exe, a.out)
    print("%s: %.2f MB" % (a.out, os.path.getsize(a.out) / 1e6))


if __name__ == "__main__":
    main()
