"""Quake 3's soundtrack -> Godot: every music/*.wav in the player's paks as Ogg Vorbis.

python make_music.py [<extracted baseq3>] [--godot ../godot] [--q3 <baseq3>]

  <godot>/library/music/<name>.ogg   fla22k_01..06 (Front Line Assembly; 01 and 04 have an intro and
                                     a loop), sonic1..6 (Sonic Mayhem), win and loss (the single
                                     player postgame)
Each map's worldspawn "music" key names its tracks ("intro loop", or one track that loops); the
game sends it as CS_MUSIC and Q3Music plays it (cg_main.c CG_StartMusic). music/ is extracted
from the paks first when it's missing. Uses imageio-ffmpeg's ffmpeg (as make_cinematics.py does).
"""
import os, sys, glob, argparse, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import q3common as q3

ap = argparse.ArgumentParser()
ap.add_argument("root", nargs="?", default=os.path.join(ROOT, "extracted", "baseq3"))
ap.add_argument("--godot", default=os.path.join(ROOT, "godot"))
ap.add_argument("--q3")
a = ap.parse_args()

src = os.path.join(a.root, "music")
if not glob.glob(os.path.join(src, "*.wav")):
    q3dir = next((c for c in [a.q3] + q3.DEFAULT_Q3 if c and os.path.isfile(os.path.join(c, "pak0.pk3"))), None)
    if not q3dir:
        sys.exit("make_music: music/ not extracted and no Quake 3 install found; pass --q3 <baseq3>")
    subprocess.check_call([sys.executable, os.path.join(HERE, "extract_pk3.py"), q3dir, a.root, "--prefix", "music/"])

import imageio_ffmpeg
ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
out = os.path.join(a.godot, "library", "music")
os.makedirs(out, exist_ok=True)
total_in = total_out = 0
for wav in sorted(glob.glob(os.path.join(src, "*.wav"))):
    name = os.path.splitext(os.path.basename(wav))[0].lower()
    ogg = os.path.join(out, name + ".ogg")
    if not os.path.exists(ogg) or os.path.getmtime(ogg) < os.path.getmtime(wav):
        subprocess.check_call([ffmpeg, "-y", "-loglevel", "error", "-i", wav, "-c:a", "libvorbis", "-q:a", "6", ogg])
    total_in += os.path.getsize(wav)
    total_out += os.path.getsize(ogg)
    print("%-16s %6.1f MB -> %5.1f MB" % (name, os.path.getsize(wav) / 1e6, os.path.getsize(ogg) / 1e6))
print("music: %.0f MB of WAV -> %.0f MB of Ogg in %s" % (total_in / 1e6, total_out / 1e6, out))
