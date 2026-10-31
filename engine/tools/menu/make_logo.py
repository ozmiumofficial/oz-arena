"""Ozmium's logo -> the main menu's title art, engine/ui/art/ozmium_logo.png and ozmium_logo_hi.png.

python engine/tools/menu/make_logo.py <logo.png>

The source is white lettering with a black outline on a transparent square. Cropped to the lettering,
it's written twice: at the size the main menu draws it in a 1080p window (shell.rcss .logo .mark,
26rem of 18dp), filtered down here since the UI's images have no mipmaps, and at its own size for
windows and menu sizes that draw it larger (min-resolution 1.25x).
"""
import os, sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "ui", "art")
WIDTH = 26 * 18		# .logo .mark's width at 1080p
MARGIN = 2			# transparent texels kept around the outline, so its edge filters

if len(sys.argv) != 2:
    sys.exit(__doc__)
im = Image.open(sys.argv[1]).convert("RGBA")
x0, y0, x1, y1 = im.getchannel("A").getbbox()
im = im.crop((x0 - MARGIN, y0 - MARGIN, x1 + MARGIN, y1 + MARGIN))
os.makedirs(OUT, exist_ok=True)
im.save(os.path.join(OUT, "ozmium_logo_hi.png"), optimize=True)
# Pillow filters RGBA premultiplied: right here, where everything under zero alpha is the outline's black
small = im.resize((WIDTH, round(im.height * WIDTH / im.width)), Image.LANCZOS)
small.save(os.path.join(OUT, "ozmium_logo.png"), optimize=True)
print("%dx%d, %dx%d -> %s (the box's aspect: %.4f)" % (im.width, im.height, small.width, small.height, OUT, im.width / im.height))
