"""tools/oz_pbr_swap.py as one Windows exe, for players without Python.

python engine/tools/make_pbr_swap_exe.py [--out build/pbr_swap]   -> build/pbr_swap/OzPbrSwap.exe

PyInstaller (pip install pyinstaller) bundles Python, numpy, Pillow and tkinter into a single windowed
exe with Oz's icon. The exe finds the game beside itself, in the folder above, or where OzArenaSetup.exe
installed it.
"""
import os, sys, shutil, argparse, subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ap = argparse.ArgumentParser()
ap.add_argument("--out", default=os.path.join(ROOT, "build", "pbr_swap"))
a = ap.parse_args()

work = os.path.join(a.out, "work")
ico = os.path.join(ROOT, "engine", "quake3e", "code", "win32", "oz.ico")
cmd = [sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean", "--onefile", "--windowed",
       "--name", "OzPbrSwap", "--icon", ico, "--add-data", ico + os.pathsep + ".",
       "--distpath", a.out, "--workpath", work, "--specpath", work,
       # only numpy, Pillow and tkinter are used: keep the rest of this PC's packages out
       "--exclude-module", "cv2", "--exclude-module", "torch", "--exclude-module", "matplotlib",
       "--exclude-module", "scipy", "--exclude-module", "pandas", "--exclude-module", "IPython",
       os.path.join(ROOT, "tools", "oz_pbr_swap.py")]
subprocess.check_call(cmd)
shutil.rmtree(work, ignore_errors=True)
exe = os.path.join(a.out, "OzPbrSwap.exe")
print("%s (%.1f MB)" % (exe, os.path.getsize(exe) / 2**20))
