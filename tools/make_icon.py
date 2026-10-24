"""The game's icon: Quake 3's emblem with an R beside it (for Remastered).

python make_icon.py [--baseq3 extracted/baseq3] [--preview <png>]

  godot/icon.png    256 px, the window and taskbar icon (project.godot application/config/icon)
  godot/icon.ico    16-256 px, the exe icon (export_presets.cfg application/icon)

The emblem is id's own logo512 texture (textures/sfx/logo512.jpg: red with a silver bevel, drawn
dark), brightened 4x and cut out by its brightness; the R is Quake 3's menu font
(library/ui/fonts/q3_prop.ttf, made by make_ui.py) in the emblem's red. Both get a dark rim so
they stand out on light taskbars.
"""
import os, argparse
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
GODOT = os.path.join(ROOT, "godot")
FONT = os.path.join(GODOT, "library", "ui", "fonts", "q3_prop.ttf")
SIZE = 1024                      # drawn at this size, then scaled down
ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
TOP = np.array([240, 40, 26], float)     # the R's gradient, light end (top), the emblem's red
BOTTOM = np.array([140, 0, 0], float)    # its dark end
RIM = (14, 3, 3)


def emblem(baseq3: str) -> Image.Image:
    """id's emblem brightened, with its coverage as alpha, cropped to it."""
    a = np.asarray(Image.open(os.path.join(baseq3, "textures", "sfx", "logo512.jpg")).convert("RGB"), float)
    rgb = np.clip(a * 4.0, 0, 255)
    m = np.clip((rgb.max(axis=2) - 60.0) / 70.0, 0.0, 1.0)   # the jpeg's black is noisy below ~15
    mask = Image.fromarray((m * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.6))
    img = Image.fromarray(rgb.astype(np.uint8), "RGB").convert("RGBA")
    img.putalpha(mask)
    return img.crop(mask.point(lambda v: 255 if v > 40 else 0).getbbox())


def gradient(mask: Image.Image) -> Image.Image:
    """A coverage mask filled with the red gradient."""
    w, h = mask.size
    t = np.linspace(0.0, 1.0, h)[:, None, None]
    rgb = TOP * (1.0 - t) + BOTTOM * t
    rgb = np.broadcast_to(rgb, (h, w, 3)).astype(np.uint8)
    fill = Image.fromarray(rgb, "RGB").convert("RGBA")
    fill.putalpha(mask)
    return fill


def rimmed(fill: Image.Image, rim: int) -> Image.Image:
    """`fill` with a dark rim around its coverage."""
    w, h = fill.size
    mask = fill.getchannel("A")
    out = Image.new("RGBA", (w + 2 * rim, h + 2 * rim), RIM + (0,))
    edge = Image.new("L", out.size, 0)
    edge.paste(mask, (rim, rim))
    edge = edge.filter(ImageFilter.MaxFilter(rim * 2 + 1)).filter(ImageFilter.GaussianBlur(rim * 0.25))
    out.putalpha(edge)
    out.alpha_composite(fill, (rim, rim))
    return out


def letter_mask(height: int) -> Image.Image:
    font = ImageFont.truetype(FONT, int(height * 1.35))
    box = font.getbbox("R")
    img = Image.new("L", (box[2] - box[0] + 8, box[3] - box[1] + 8), 0)
    ImageDraw.Draw(img).text((4 - box[0], 4 - box[1]), "R", font=font, fill=255)
    img = img.crop(img.getbbox())
    return img.resize((max(1, round(img.width * height / img.height)), height), Image.LANCZOS)


def build(baseq3: str) -> Image.Image:
    canvas = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    rim = SIZE // 64
    em = emblem(baseq3)
    let = rimmed(gradient(letter_mask(int(SIZE * 0.38))), rim)
    ew = SIZE - let.width - 2 * rim - SIZE // 40   # the rest of the width, less a small gap
    em = em.resize((ew, round(em.height * ew / em.width)), Image.LANCZOS)
    emb = rimmed(em, rim)
    # the emblem left of center, the R beside its right arm, both standing on one baseline
    total_h = max(emb.height, let.height)
    top = (SIZE - total_h) // 2
    canvas.alpha_composite(emb, (0, top + total_h - emb.height))
    canvas.alpha_composite(let, (SIZE - let.width, top + total_h - let.height))
    return canvas


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseq3", default=os.path.join(ROOT, "extracted", "baseq3"), help="id's unpacked baseq3")
    ap.add_argument("--preview", help="also write a sheet of every size on light and dark grounds")
    a = ap.parse_args()
    big = build(a.baseq3)
    big.resize((256, 256), Image.LANCZOS).save(os.path.join(GODOT, "icon.png"))
    big.save(os.path.join(GODOT, "icon.ico"), sizes=[(s, s) for s in ICO_SIZES])
    print("icon:", os.path.join(GODOT, "icon.png"), "and icon.ico", ICO_SIZES)
    if a.preview:
        sizes = [256, 64, 48, 32, 24, 16]
        sheet = Image.new("RGBA", (sum(sizes) + 20 * len(sizes) + 20, 2 * 276), (0, 0, 0, 255))
        for row, ground in enumerate([(32, 32, 32, 255), (230, 230, 230, 255)]):
            sheet.paste(ground, (0, row * 276, sheet.width, row * 276 + 276))
            x = 20
            for s in sizes:
                sheet.alpha_composite(big.resize((s, s), Image.LANCZOS), (x, row * 276 + 10))
                x += s + 20
        sheet.save(a.preview)


if __name__ == "__main__":
    main()
