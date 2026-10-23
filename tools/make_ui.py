"""Main-menu step: menu sounds, fonts, map list, bots, crosshairs and player icons.

python make_ui.py <extracted baseq3> <ui work dir> [--q3 <baseq3 folder>]

Missing source folders (sound/, menu/, levelshots/, gfx/2d/) are extracted from the paks first.
  sounds/      menu1-4 (ui_qmenu.c in/move/out/buzz), weapons/change.wav, an ambient loop
  fonts/       font1_prop -> signed-distance atlas (crisp at any size), font1_prop_glo -> glow
               bitmap, font2_prop (banner) -> 4x bitmap with SDF-sharpened edges; fonts.json
               holds the glyph rects. The rects are found in the atlases themselves: each row
               is a fixed cell band, glyphs are ink columns (touching glyphs split at the
               thinnest column, split marks such as '"' merged).
  maps.json    scripts/arenas.txt + *.arena (map, long name, game types, bots, limits)
  levelshots/  one image per listed map: work/_levelshots/<map>.jpg when rendered (godot
               tests/levelshots.tscn, 1280x960 from the remastered map), else id's original
  bots.json    scripts/bots.txt (name, model, skin)
  crosshairs/  gfx/2d/crosshaira-j, 4x
  campaign/    single-player medals, skill icons and completion marks, 2x
  icons/       models/players/<model>/icon_default, 2x
  hud/         icons/*.tga (weapons, ammo, health, armor, powerups, flags) and menu/medals, 2x, for
               the in-game HUD (rewards)
"""
import os, sys, re, json, glob, shutil, argparse, subprocess
import numpy as np
from PIL import Image
from scipy.ndimage import distance_transform_edt

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import q3common as q3

ap = argparse.ArgumentParser()
ap.add_argument("extracted")
ap.add_argument("out")
ap.add_argument("--q3")
a = ap.parse_args()
SRC, OUT = a.extracted, a.out

NEEDED = {"sound/": "sound/misc/menu1.wav", "menu/": "menu/art/font1_prop.tga",
          "levelshots/": "levelshots/q3dm1.jpg", "gfx/2d/": "gfx/2d/crosshaira.tga"}
missing = [p for p, f in NEEDED.items() if not os.path.isfile(os.path.join(SRC, *f.split("/")))]
if missing:
    q3dir = next((c for c in [a.q3] + q3.DEFAULT_Q3 if c and os.path.isfile(os.path.join(c, "pak0.pk3"))), None)
    if not q3dir:
        sys.exit("make_ui: %s not extracted and no Quake 3 install found; pass --q3 <baseq3>" % " ".join(missing))
    print("extracting", " ".join(missing), "from", q3dir)
    subprocess.check_call([sys.executable, os.path.join(HERE, "extract_pk3.py"), q3dir, SRC, "--prefix"] + missing)


def src(rel):
    return os.path.join(SRC, *rel.split("/"))


def rgba(rel):
    return np.array(Image.open(src(rel)).convert("RGBA"))


# ---- sounds: copied as-is (the remastered sound set is a later phase)
SOUNDS = ["misc/menu1.wav", "misc/menu2.wav", "misc/menu3.wav", "misc/menu4.wav",
          "weapons/change.wav", "world/ambient/x_ominous.wav"]
for s in SOUNDS:
    d = os.path.join(OUT, "sounds", *s.split("/"))
    os.makedirs(os.path.dirname(d), exist_ok=True)
    shutil.copy2(src("sound/" + s), d)

# ---- fonts
SCALE = 4          # atlas pixels per original pixel
SDF_RANGE = 32     # signed-distance range in atlas pixels (Godot msdf_pixel_range)
MARGIN = 5         # original pixels around each glyph (room for the distance field and glow)

# cell bands (top of each row in the 256x256 atlas), cell height, baseline, gap/space like ui_atoms.c
FONTS = {
    "prop": dict(file="menu/art/font1_prop.tga", cell=30, ascent=27, gap=3, space=8, rows=[
        (0, "ABCDEFGHIJKLM"), (30, "NOPQRSTUVWXY"), (60, "Z0123456897"), (90, "`-=~"),
        (120, "!@#$%^&*()"), (150, "_+[]\\/{}<>"), (180, "|;:,.'\"?")]),
    "banner": dict(file="menu/art/font2_prop.tga", cell=36, ascent=30, gap=4, space=12, rows=[
        (12, "ABCDEFG"), (54, "HIJKLMN"), (96, "OPQRSTU"), (138, "VWXYZ")]),
}


def ink_runs(col, n, th=20):
    """Columns with ink -> exactly n [x0, x1] runs: merge the closest pairs or split the widest."""
    idx = np.where(col > th)[0]
    runs = []
    for x in idx:
        if runs and x == runs[-1][1] + 1:
            runs[-1][1] = x
        else:
            runs.append([x, x])
    while len(runs) > n:
        gaps = [runs[i + 1][0] - runs[i][1] for i in range(len(runs) - 1)]
        i = int(np.argmin(gaps))
        runs[i:i + 2] = [[runs[i][0], runs[i + 1][1]]]
    while len(runs) < n:
        i = int(np.argmax([r[1] - r[0] for r in runs]))
        x0, x1 = runs[i]
        w = x1 - x0
        lo, hi = x0 + int(w * 0.2), x1 - int(w * 0.2)
        cut = lo + int(np.argmin(col[lo:hi + 1]))
        runs[i:i + 1] = [[x0, cut - 1], [cut + 1, x1]]
    return [(int(r[0]), int(r[1])) for r in runs]


def upscale(img, s=SCALE):
    return np.array(Image.fromarray(img).resize((img.shape[1] * s, img.shape[0] * s), Image.LANCZOS))


def sdf(alpha_up):
    """Alpha (upscaled) -> signed distance, 0.5 at the edge, SDF_RANGE atlas pixels from 0 to 1."""
    inside = alpha_up >= 128
    d = distance_transform_edt(inside) - distance_transform_edt(~inside)
    return np.clip(0.5 + d / SDF_RANGE, 0.0, 1.0)


class Packer:
    def __init__(self, width):
        self.w, self.x, self.y, self.row_h, self.items = width, 0, 0, 0, []

    def add(self, img):
        h, w = img.shape[:2]
        if self.x + w > self.w:
            self.x, self.y, self.row_h = 0, self.y + self.row_h + 2, 0
        pos = (self.x, self.y)
        self.items.append((pos, img))
        self.x += w + 2
        self.row_h = max(self.row_h, h)
        return pos

    def image(self, channels):
        h = self.y + self.row_h
        h = 1 << (h - 1).bit_length()
        out = np.zeros((h, self.w, channels), np.uint8)
        for (x, y), img in self.items:
            ih, iw = img.shape[:2]
            out[y:y + ih, x:x + iw] = img.reshape(ih, iw, -1)
        return out


def glyph_boxes(spec, alpha):
    boxes = {}
    for top, chars in spec["rows"]:
        band = alpha[top:top + spec["cell"]]
        for ch, (x0, x1) in zip(chars, ink_runs(band.max(0), len(chars))):
            boxes[ch] = (x0, x1, top)
    return boxes


fonts_out = {}
os.makedirs(os.path.join(OUT, "fonts"), exist_ok=True)
M, S = MARGIN, SCALE
for name, spec in FONTS.items():
    img = rgba(spec["file"])
    alpha = img[..., 3]
    boxes = glyph_boxes(spec, alpha)
    cell = spec["cell"]
    layers = {"": Packer(2048)}
    glow = name == "prop"
    if glow:
        glo = rgba("menu/art/font1_prop_glo.tga")
        layers["_glow"] = Packer(2048)
    glyphs = {}
    for ch, (x0, x1, top) in boxes.items():
        w = x1 - x0 + 1
        # the glyph alone on a padded canvas, so neighbors never bleed into its distance field
        canvas = np.zeros((cell + 2 * M, w + 2 * M, 4), np.uint8)
        canvas[M:M + cell, M:M + w] = img[top:top + cell, x0:x1 + 1]
        a_up = upscale(canvas[..., 3])
        if name == "prop":
            d = (sdf(a_up) * 255).astype(np.uint8)
            tile = np.dstack([d, d, d])
        else:
            # banner: color texture, edges sharpened by the distance field, color bled outward
            edge = np.clip((sdf(a_up) - 0.5) * SDF_RANGE / 1.5 + 0.5, 0, 1)
            pm = canvas[..., :3].astype(np.float32) * (canvas[..., 3:] / 255.0)
            rgb = np.stack([upscale(pm[..., c].astype(np.uint8)) for c in range(3)], -1).astype(np.float32)
            rgb = rgb / np.maximum(a_up[..., None] / 255.0, 1e-3)
            tile = np.dstack([np.clip(rgb, 0, 255), edge * 255]).astype(np.uint8)
        pos = layers[""].add(tile)
        if glow:
            # the glow layer uses the same rects (ui_atoms.c draws it with propMap too), with room to spread
            # glow pixels nearer to a neighbor's ink belong to that neighbor (the atlas packs them tight)
            gc = np.zeros((cell + 2 * M, w + 2 * M), np.uint8)
            ys, xs = max(top - M, 0), max(x0 - M, 0)
            ye, xe = top + cell + M, x1 + 1 + M
            part = glo[ys:ye, xs:xe]
            ink = alpha[ys:ye, xs:xe] > 20
            own = np.zeros_like(ink)
            own[top - ys:top - ys + cell, x0 - xs:x1 + 1 - xs] = ink[top - ys:top - ys + cell, x0 - xs:x1 + 1 - xs]
            other = ink & ~own
            keep = distance_transform_edt(~own) <= (distance_transform_edt(~other) if other.any() else np.inf)
            ga = (part[..., :3].max(-1).astype(np.float32) * part[..., 3] / 255.0 * keep).astype(np.uint8)
            gc[ys - (top - M):ys - (top - M) + ga.shape[0], xs - (x0 - M):xs - (x0 - M) + ga.shape[1]] = ga
            gu = upscale(gc)
            gpos = layers["_glow"].add(np.dstack([np.full_like(gu, 255)] * 3 + [gu]))
            assert gpos == pos, "glow atlas out of step"
        glyphs[str(ord(ch))] = [pos[0], pos[1], (w + 2 * M) * S, (cell + 2 * M) * S]
        if ch.isalpha():
            glyphs[str(ord(ch.lower()))] = glyphs[str(ord(ch))]   # the prop fonts are caps only
    files = {}
    for suffix, pk in layers.items():
        fn = "q3_%s%s.png" % (name, suffix)
        nch = 3 if (name == "prop" and suffix == "") else 4
        Image.fromarray(pk.image(nch)).save(os.path.join(OUT, "fonts", fn), optimize=True)
        files["file" + suffix] = "fonts/" + fn
    fonts_out[name] = dict(files, kind="sdf" if name == "prop" else "bitmap", scale=S, size=cell * S,
                           ascent=spec["ascent"] * S, descent=(cell - spec["ascent"]) * S,
                           margin=M * S, gap=spec["gap"] * S, space=spec["space"] * S,
                           sdf_range=SDF_RANGE, glyphs=glyphs)
    print("font %s: %d glyphs" % (name, len(boxes)))

def trace_ttf(spec, path, family):
    """font1_prop -> a real TrueType font for the web UI: each glyph traced from its 8x upscale."""
    import cv2
    from fontTools.fontBuilder import FontBuilder
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    img = rgba(spec["file"])
    alpha = img[..., 3]
    cell, asc = spec["cell"], spec["ascent"]
    upm, up = 1000, 8
    u = upm / cell                        # font units per original pixel
    order, cmap, glyphs, metrics = [".notdef", "space"], {32: "space"}, {}, {}
    pen = TTGlyphPen(None)
    glyphs[".notdef"] = pen.glyph()
    glyphs["space"] = TTGlyphPen(None).glyph()
    metrics[".notdef"] = (int(spec["space"] * u), 0)
    metrics["space"] = (int(spec["space"] * u), 0)
    for ch, (x0, x1, top) in glyph_boxes(spec, alpha).items():
        w = x1 - x0 + 1
        canvas = np.zeros((cell + 2, w + 2), np.uint8)
        canvas[1:1 + cell, 1:1 + w] = alpha[top:top + cell, x0:x1 + 1]
        big = np.array(Image.fromarray(canvas).resize(((w + 2) * up, (cell + 2) * up), Image.LANCZOS))
        contours, hier = cv2.findContours((big >= 128).astype(np.uint8), cv2.RETR_CCOMP, cv2.CHAIN_APPROX_NONE)
        pen = TTGlyphPen(None)
        for ci, c in enumerate(contours):
            hole = hier[0][ci][3] != -1
            c = cv2.approxPolyDP(c, 1.2, True)[:, 0, :].astype(np.float64)
            if len(c) < 3:
                continue
            # image px (8x, 1 px border) -> font units, y up from the baseline
            pts = [((x / up - 1) * u, (asc - (y / up - 1)) * u) for x, y in c]
            cw = sum(pts[i][0] * pts[i - 1][1] - pts[i - 1][0] * pts[i][1] for i in range(len(pts))) > 0
            if cw == hole:   # TrueType: outlines clockwise, holes counter-clockwise
                pts.reverse()
            pen.moveTo(tuple(map(round, pts[0])))
            for p in pts[1:]:
                pen.lineTo(tuple(map(round, p)))
            pen.closePath()
        name = "g%04x" % ord(ch)
        order.append(name)
        glyphs[name] = pen.glyph()
        metrics[name] = (int(round((w + spec["gap"]) * u)), 0)
        cmap[ord(ch)] = name
        if ch.isalpha():
            cmap[ord(ch.lower())] = name
    fb = FontBuilder(upm, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=int(asc * u), descent=-int((cell - asc) * u))
    fb.setupNameTable({"familyName": family, "styleName": "Regular"})
    fb.setupOS2(sTypoAscender=int(asc * u), sTypoDescender=-int((cell - asc) * u), usWinAscent=int(asc * u),
                usWinDescent=int((cell - asc) * u))
    fb.setupPost()
    fb.save(path)


trace_ttf(FONTS["prop"], os.path.join(OUT, "fonts", "q3_prop.ttf"), "Q3 Prop")
print("font prop: traced to q3_prop.ttf")

# contact sheet for checking the glyph order by eye
sheet = Image.new("RGBA", (1100, 260), (30, 30, 40, 255))
img = Image.open(src(FONTS["prop"]["file"])).convert("RGBA")
x = y = 4
for ch, (x0, x1, top) in sorted(glyph_boxes(FONTS["prop"], np.array(img)[..., 3]).items(), key=lambda k: (k[1][2], k[1][0])):
    g = img.crop((x0, top, x1 + 1, top + 30)).resize(((x1 - x0 + 1) * 2, 60), Image.NEAREST)
    if x + g.width > 1096:
        x, y = 4, y + 64
    sheet.alpha_composite(g, (x, y))
    x += g.width + 6
sheet.save(os.path.join(OUT, "fonts", "_check_prop.png"))
json.dump(fonts_out, open(os.path.join(OUT, "fonts", "fonts.json"), "w"), indent=0)


# ---- maps and bots from the info scripts
def info_blocks(path):
    text = re.sub(r"//[^\n]*", "", open(path, encoding="latin-1").read())
    out = []
    for body in re.findall(r"\{([^}]*)\}", text):
        d = {}
        for k, v in re.findall(r'(\w+)\s+("[^"]*"|\S+)', body):
            d[k.lower()] = v.strip('"')
        out.append(d)
    return out


RENDERED = os.path.join(os.path.dirname(HERE), "work", "_levelshots")
maps, seen = [], set()
os.makedirs(os.path.join(OUT, "levelshots"), exist_ok=True)
arena_files = [src("scripts/arenas.txt")] + sorted(glob.glob(os.path.join(SRC, "scripts", "*.arena")))
for f in arena_files:
    for d in info_blocks(f):
        m = d.get("map", "").lower()
        if not m or m in seen:
            continue
        seen.add(m)
        # the remastered map's own shot (tests/levelshots.tscn) over the 128 px original
        shot = next((p for p in [os.path.join(RENDERED, m + ".jpg")] + [src("levelshots/%s.%s" % (m, e)) for e in ("jpg", "tga")]
                     if os.path.isfile(p)), None)
        if shot:
            Image.open(shot).convert("RGB").save(os.path.join(OUT, "levelshots", m + ".jpg"), quality=92)
        maps.append(dict(map=m, name=d.get("longname", m), types=d.get("type", "ffa").split(), special=d.get("special", ""),
                         bots=d.get("bots", "").split(), fraglimit=int(d.get("fraglimit", 0) or 0),
                         timelimit=int(d.get("timelimit", 0) or 0), levelshot=("levelshots/%s.jpg" % m) if shot else ""))
bots = []
for d in info_blocks(src("scripts/bots.txt")):
    model, _, skin = d.get("model", "").partition("/")
    bots.append(dict(name=d.get("name", ""), model=model, skin=skin or "default"))
json.dump(dict(maps=maps, bots=bots), open(os.path.join(OUT, "maps.json"), "w"), indent=1)

# ---- crosshairs and player icons
os.makedirs(os.path.join(OUT, "crosshairs"), exist_ok=True)
crosshairs = []
for c in "abcdefghij":
    p = src("gfx/2d/crosshair%s.tga" % c)
    if os.path.isfile(p):
        im = Image.open(p).convert("RGBA")
        im.resize((im.width * 4, im.height * 4), Image.LANCZOS).save(os.path.join(OUT, "crosshairs", "crosshair_%s.png" % c))
        crosshairs.append(c)
os.makedirs(os.path.join(OUT, "icons"), exist_ok=True)
icons = []
for m in q3.player_models(SRC):
    p = src("models/players/%s/icon_default.tga" % m)
    if os.path.isfile(p):
        im = Image.open(p).convert("RGBA")
        im.resize((im.width * 2, im.height * 2), Image.LANCZOS).save(os.path.join(OUT, "icons", m + ".png"))
        icons.append(m)
# single-player campaign art: medals (ui_sppostgame.c), skill icons, per-skill completion marks
os.makedirs(os.path.join(OUT, "campaign"), exist_ok=True)
camp = []
for rel in ["menu/medals/medal_%s.tga" % m for m in ("accuracy", "impressive", "excellent", "gauntlet", "frags", "victory")] +         ["menu/art/skill%d.tga" % i for i in range(1, 6)] + ["menu/art/level_complete%d.tga" % i for i in range(1, 6)]:
    if os.path.isfile(src(rel)):
        im = Image.open(src(rel)).convert("RGBA")
        name = os.path.splitext(os.path.basename(rel))[0].replace("medal_victory", "medal_perfect")
        im.resize((im.width * 2, im.height * 2), Image.LANCZOS).save(os.path.join(OUT, "campaign", name + ".png"))
        camp.append(name)
# in-game HUD icons (cg_draw.c / cg_weapons.c use these 32 px icons; the HUD draws them at 2x)
os.makedirs(os.path.join(OUT, "hud"), exist_ok=True)
hud = []
for f in sorted(glob.glob(src("icons/*.tga")) + glob.glob(src("menu/medals/*.tga"))):
    name = os.path.splitext(os.path.basename(f))[0]
    im = Image.open(f).convert("RGBA")
    im.resize((im.width * 2, im.height * 2), Image.LANCZOS).save(os.path.join(OUT, "hud", name + ".png"))
    hud.append(name)
json.dump(dict(sounds=SOUNDS, crosshairs=crosshairs, icons=icons, campaign=camp, hud=hud, maps=len(maps), bots=len(bots)),
          open(os.path.join(OUT, "ui.json"), "w"), indent=1)
print("ui: %d sounds, 2 fonts, %d maps, %d bots, %d crosshairs, %d icons" % (len(SOUNDS), len(maps), len(bots), len(crosshairs), len(icons)))
