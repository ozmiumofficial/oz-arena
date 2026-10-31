"""The game icon: OZ in white letters, written to engine/quake3e/code/win32/oz.ico.

python engine/tools/make_icon.py [--preview out.png]

The letters are drawn as shapes (no font), so the icon is ours to ship under the GPL. Each size is
drawn on its own at 8x and filtered down, so the small ones stay sharp. A dark edge (the web
menu's stroke color) keeps the white readable on a light taskbar or Explorer window. The .ico holds
PNG images, which Windows reads from Vista on; win_resource.rc builds it into both exes, and SDL
takes the window and taskbar icon from the exe.
"""
import argparse, io, os, struct
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
ICO = os.path.join(ROOT, "engine", "quake3e", "code", "win32", "oz.ico")

SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
SS = 8                       # supersampling
WHITE = (255, 255, 255)
DARK = (10, 8, 7)            # the web menu's stroke, #0a0807

# the letters' box in icon units (0..1): two letters side by side, a gap between them
TOP, BOTTOM = 0.19, 0.81
LEFT, RIGHT, GAP = 0.04, 0.96, 0.05
STROKE = 0.14                # the bars' thickness
CORNER = 0.13                # the O's outer corner radius


def letters(px, grow=0.0):
    """The OZ shapes as an 'L' mask of px x px, grown by `grow` pixels all round (the dark edge)."""
    m = Image.new("L", (px, px), 0)
    d = ImageDraw.Draw(m)
    g = grow
    w = (RIGHT - LEFT - GAP) / 2
    t = STROKE * px
    y0, y1 = TOP * px, BOTTOM * px

    # O: a rounded box with a rounded hole
    x0, x1 = LEFT * px, (LEFT + w) * px
    r = CORNER * px
    d.rounded_rectangle((x0 - g, y0 - g, x1 + g, y1 + g), radius=r + g, fill=255)
    if x1 - t - g > x0 + t + g:
        hole = max(r - t * 0.6, px * 0.02)
        d.rounded_rectangle((x0 + t + g, y0 + t + g, x1 - t - g, y1 - t - g), radius=max(hole - g, 0), fill=0)

    # Z: the top and bottom bars and a diagonal as thick as the bars
    x0, x1 = (RIGHT - w) * px, RIGHT * px
    dy = (y1 - t) - (y0 + t)
    dw = t
    for _ in range(20):      # the diagonal's horizontal width for a perpendicular thickness of t
        dx = (x1 - x0) - dw
        dw = t * (dx * dx + dy * dy) ** 0.5 / dy
    z = [(x0, y0), (x1, y0), (x1, y0 + t), (x0 + dw, y1 - t), (x1, y1 - t), (x1, y1),
         (x0, y1), (x0, y1 - t), (x1 - dw, y0 + t), (x0, y0 + t)]
    d.polygon(z, fill=255)
    if g:                    # grown: the polygon plus a 2g line along each side and a disk at each corner
        for p, q in zip(z, z[1:] + z[:1]):
            d.line([p, q], fill=255, width=round(2 * g))
        for x, y in z:
            d.ellipse((x - g, y - g, x + g, y + g), fill=255)
    return m


def icon(size):
    px = size * SS
    edge = max(1.0, size * 0.045) * SS          # at least one pixel at every size
    ring = letters(px, edge).resize((size, size), Image.LANCZOS)
    mask = letters(px).resize((size, size), Image.LANCZOS)

    out = Image.new("RGBA", (size, size), DARK + (0,))
    out.putalpha(ring.point(lambda a: a * 235 // 255))
    white = Image.new("RGBA", (size, size), WHITE + (255,))
    white.putalpha(mask)
    return Image.alpha_composite(out, white)


def write_ico(path, images):
    pngs = []
    for im in images:
        b = io.BytesIO()
        im.save(b, "PNG", optimize=True)
        pngs.append(b.getvalue())
    head = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries = b""
    for im, data in zip(images, pngs):
        w, h = im.size
        entries += struct.pack("<BBBBHHII", w % 256, h % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    with open(path, "wb") as f:
        f.write(head + entries + b"".join(pngs))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", help="also write a sheet of every size on light and dark grounds")
    a = ap.parse_args()

    images = [icon(s) for s in SIZES]
    write_ico(ICO, images)
    print("wrote", os.path.relpath(ICO, ROOT), "(%s)" % ", ".join(map(str, SIZES)))

    if a.preview:
        pad, row = 12, max(SIZES) + 24
        width = sum(SIZES) + pad * (len(SIZES) + 1)
        sheet = Image.new("RGBA", (width, row * 2), (0, 0, 0, 255))
        for r, ground in enumerate([(243, 243, 243), (32, 32, 32)]):
            sheet.paste(ground + (255,), (0, r * row, width, (r + 1) * row))
            x = pad
            for im in images:
                y = r * row + (row - im.size[1]) // 2
                sheet.alpha_composite(im, (x, y))
                x += im.size[0] + pad
        sheet.save(a.preview)
        print("wrote", a.preview)


if __name__ == "__main__":
    main()
