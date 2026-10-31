"""The player's Quake III Arena install, read the way the engine reads it (tools/make_pk3s.py, tools/make_fighters.py).

id's files are copyrighted: the tools read them from the player's install and never copy them into the repo.
"""
import os, re, sys, glob, zipfile


def find_baseq3():
    """baseq3 in any Steam library (libraryfolders.vdf), as the engine finds it."""
    roots = []
    try:
        import winreg
        for hive, key, val in ((winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam", "SteamPath"),
                               (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath")):
            try:
                roots.append(winreg.QueryValueEx(winreg.OpenKey(hive, key), val)[0])
            except OSError:
                pass
    except ImportError:
        pass
    roots.append(r"C:\Program Files (x86)\Steam")
    libs = []
    for r in roots:
        vdf = os.path.join(r, "steamapps", "libraryfolders.vdf")
        if os.path.isfile(vdf):
            libs += [p.replace("\\\\", "\\") for p in re.findall(r'"path"\s+"([^"]+)"', open(vdf, encoding="utf-8").read())]
    for lib in [r for r in roots] + libs:
        d = os.path.join(lib, "steamapps", "common", "Quake 3 Arena", "baseq3")
        if os.path.isfile(os.path.join(d, "pak0.pk3")):
            return d
    sys.exit("baseq3 not found: pass --baseq3")


class Paks:
    """id's pak0-pak8 as one file system (later paks win, as in the engine)."""
    def __init__(self, folder):
        self.zips, self.files = [], {}
        for p in sorted(glob.glob(os.path.join(folder, "pak[0-8].pk3"))):
            z = zipfile.ZipFile(p)
            self.zips.append(z)
            for n in z.namelist():
                self.files[n.lower()] = (z, n)

    def read(self, path):
        z, n = self.files[path.lower()]
        return z.read(n)

    def image(self, path):
        """An image path with or without extension -> the pak name, as the engine resolves it."""
        base = os.path.splitext(path.lower())[0]
        for ext in (".tga", ".jpg", ".png"):
            if base + ext in self.files:
                return base + ext
        return None
