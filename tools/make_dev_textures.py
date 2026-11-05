"""Oz's prototyping textures -> game/assets/textures/oz_dev/*.png (packed into oz/pak_oz_code.pk3 by
engine/build_engine.py), for blocking out a map in the editor before it gets its real materials.

python tools/make_dev_textures.py [--size 512]

Each is a flat color with a grid that reads at any distance: a fine line every 16 units, a firmer one every
64 and a square of 128 units shaded a step apart, so sizes and heights read at a glance. At the editor's
default texture scale (0.5) a 512-pixel texture covers 256 units, so the lines fall on the editor's grid.
The colors are Oz's own (nothing of id's), matte and mid-toned so the map's lights read on them and a
texture color (the editor's Color) tints the light ones cleanly. Made here, so they're committed.
"""
import os, argparse
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), "game", "assets", "textures", "oz_dev")

# name -> base color (sRGB); light ones first: they take the editor's Color best
COLORS = {
    "light": (196, 196, 192),
    "gray": (128, 128, 126),
    "dark": (58, 58, 60),
    "white": (232, 232, 228),
    "orange": (226, 120, 40),
    "red": (196, 56, 48),
    "yellow": (222, 186, 58),
    "green": (84, 168, 76),
    "teal": (52, 160, 156),
    "blue": (60, 112, 200),
    "purple": (132, 84, 186),
    "pink": (214, 104, 150),
}


def make(rgb, size):
    """The texture: 2x2 squares of 128 units a shade apart, lines at 16 and 64 units, a 2-pixel edge."""
    px = size / 256.0  # pixels a unit (256 units across)
    y, x = np.mgrid[0:size, 0:size]
    base = np.array(rgb, np.float32)
    light = sum(rgb) / 3 > 150
    ink = -1 if light else 1  # lines darker on light colors, lighter on dark ones
    out = np.broadcast_to(base, (size, size, 3)).copy()
    checker = ((x // int(128 * px)) + (y // int(128 * px))) % 2 == 1
    out[checker] *= 0.92
    def lines(step, width, amount):
        p = int(round(step * px))
        on = ((x % p) < width) | ((y % p) < width)
        out[on] += ink * amount
    lines(16, 1, 18)
    lines(64, 2, 38)
    lines(256, 3, 60)  # the texture's own edge, where it repeats
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8), "RGB")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=int, default=512)
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    for name, rgb in COLORS.items():
        path = os.path.join(OUT, name + ".png")
        make(rgb, a.size).save(path, optimize=True)
        print("  %-8s %6d bytes" % (name, os.path.getsize(path)))
    print("wrote %d textures to %s" % (len(COLORS), OUT))


if __name__ == "__main__":
    main()
