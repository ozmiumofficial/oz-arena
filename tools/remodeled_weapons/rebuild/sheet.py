"""python sheet.py folder out.jpg view [view ...]: reference | rebuild rows from compare.py's renders."""
import sys, os
from PIL import Image, ImageDraw, ImageFont
folder, out = sys.argv[1], sys.argv[2]
views = sys.argv[3:]
f = ImageFont.truetype("arial.ttf", 30)
rows = []
for v in views:
    a = Image.open(os.path.join(folder, v + "_ref.png")); b = Image.open(os.path.join(folder, v + "_new.png"))
    r = Image.new("RGB", (a.width * 2, a.height)); r.paste(a, (0, 0)); r.paste(b, (a.width, 0)); rows.append(r)
W = rows[0].width; H = sum(r.height for r in rows) + 50
s = Image.new("RGB", (W, H), "white"); d = ImageDraw.Draw(s)
d.text((W // 4 - 120, 8), "Tripo reference", fill="black", font=f)
d.text((3 * W // 4 - 120, 8), "Rebuild", fill="black", font=f)
y = 50
for r in rows:
    s.paste(r, (0, y)); y += r.height
s = s.resize((W // 2, H // 2)); s.save(out, quality=88); print(out, s.size)
