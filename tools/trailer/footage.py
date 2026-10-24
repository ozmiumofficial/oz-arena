"""What capture_q3.py and capture_remaster.py share: ffmpeg, and turning a run's frames into
<name>.mp4 (60 fps H.264), <name>_first.png, <name>_last.png and <name>_sheet.jpg (six frames)."""
import os, sys, shutil, subprocess
from PIL import Image

FPS = 60


def find_ffmpeg():
    exe = shutil.which("ffmpeg")
    if exe:
        return exe
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except ImportError:
        sys.exit("trailer: ffmpeg not found (install it, or pip install imageio-ffmpeg)")


def finish(ff, files, out_dir, name, keep=False):
    """Moves a path's frames (in order, one per path frame) to <out_dir>/<name>_frames and makes the
    video, the first and last frames and the contact sheet from them. Returns the .mp4."""
    ext = os.path.splitext(files[0])[1]
    seq = os.path.join(out_dir, name + "_frames")
    if os.path.isdir(seq):
        shutil.rmtree(seq)
    os.makedirs(seq)
    for i, f in enumerate(files):
        shutil.move(f, os.path.join(seq, f"f{i:05d}{ext}"))
    mp4 = os.path.join(out_dir, name + ".mp4")
    subprocess.run([ff, "-y", "-loglevel", "error", "-framerate", str(FPS), "-i", os.path.join(seq, f"f%05d{ext}"),
                    "-c:v", "libx264", "-preset", "slow", "-crf", "12", "-pix_fmt", "yuv420p", mp4], check=True)
    sheet([os.path.join(seq, f"f{round(k * (len(files) - 1) / 5):05d}{ext}") for k in range(6)],
          os.path.join(out_dir, name + "_sheet.jpg"))
    for tag, idx in (("first", 0), ("last", len(files) - 1)):
        Image.open(os.path.join(seq, f"f{idx:05d}{ext}")).convert("RGB").save(os.path.join(out_dir, f"{name}_{tag}.png"))
    if not keep:
        shutil.rmtree(seq)
    return mp4


def sheet(images, out, w=640, h=360):
    """Six images, 3 x 2."""
    img = Image.new("RGB", (w * 3, h * 2))
    for k, p in enumerate(images[:6]):
        img.paste(Image.open(p).convert("RGB").resize((w, h), Image.LANCZOS), ((k % 3) * w, (k // 3) * h))
    img.save(out, quality=88)
