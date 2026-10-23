"""Player card step: the catalog (cards.json) and every piece of card art.

python build_cards.py <extracted baseq3> <cards work dir> [--library ../godot/library] [--q3 <baseq3>]
                      [--godot <exe>] [--no-render]

Needs the Godot library already built (characters, world materials, skies): it renders from it.
  emblems/   256 px PNGs (alpha): every player head icon (default, alternate and team skins), the
             weapon, item and flag icons, the medals. 4x from the 64 px originals, premultiplied so
             the edges don't halo.
  banners/   1024x256 JPGs:
               map_*   the arena levelshots (rendered from the remastered maps by tests/levelshots.tscn,
                       else the 128 px originals): the shot sharp on the right, fading into a
                       blurred wash of itself
               char_*  the 29 fighters in the menu showcase's smoky void, close framing   } Godot
               sky_*   each distinct sky, looking just above the horizon                 } windowed,
               mat_*   world materials as a lit wall at Quake 3's scale                  } tests/card_renders
  frames/    256 px JPG swatches for material frames (the world material's albedo) and powerup
             frames (the shell texture)
  cards.json the catalog (tools/card_catalog.py) with each part's art path, relative to library/
  _sheet.jpg a check sheet of everything (not copied into the project)
--no-render keeps the previous Godot renders (the Godot step takes about two minutes).
"""
import os, sys, re, json, glob, shutil, argparse, subprocess
import numpy as np
import cv2
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import q3common as q3
import card_catalog

ap = argparse.ArgumentParser()
ap.add_argument("extracted")
ap.add_argument("out")
ap.add_argument("--library", default=os.path.join(ROOT, "godot", "library"))
ap.add_argument("--q3")
ap.add_argument("--godot")
ap.add_argument("--no-render", action="store_true")
a = ap.parse_args()
SRC, OUT, LIB = a.extracted, a.out, a.library
GODOT_PROJECT = os.path.dirname(os.path.abspath(LIB))
BW, BH = 1024, 256

NEEDED = {"icons/": "icons/quad.tga", "menu/": "menu/medals/medal_excellent.tga", "levelshots/": "levelshots/q3dm1.jpg"}
missing = [p for p, f in NEEDED.items() if not os.path.isfile(os.path.join(SRC, *f.split("/")))]
if missing:
    q3dir = next((c for c in [a.q3] + q3.DEFAULT_Q3 if c and os.path.isfile(os.path.join(c, "pak0.pk3"))), None)
    if not q3dir:
        sys.exit("build_cards: %s not extracted and no Quake 3 install found; pass --q3 <baseq3>" % " ".join(missing))
    print("extracting", " ".join(missing), "from", q3dir)
    subprocess.check_call([sys.executable, os.path.join(HERE, "extract_pk3.py"), q3dir, SRC, "--prefix"] + missing)
for d in ("emblems", "banners", "frames"):   # rebuilt from scratch, so dropped parts don't linger
    shutil.rmtree(os.path.join(OUT, d), ignore_errors=True)
    os.makedirs(os.path.join(OUT, d))


def src(rel):
    return os.path.join(SRC, *rel.split("/"))


def lib(res):
    """res://library/x -> the file under --library."""
    return os.path.join(LIB, *res.replace("res://library/", "").split("/"))


def sharpen(f, amount=0.5, sigma=1.2):
    return np.clip(f + amount * (f - cv2.GaussianBlur(f, (0, 0), sigma)), 0, 1)


def save_jpg(path, rgb, q=90):
    Image.fromarray(np.clip(rgb * 255 + 0.5, 0, 255).astype(np.uint8)).save(path, quality=q)


# ---------------------------------------------------------------- emblems

def emblem(path, eid, size=256):
    im = np.array(Image.open(path).convert("RGBA")).astype(np.float32) / 255.0
    al = im[..., 3:4]
    pm = np.concatenate([im[..., :3] * al, al], -1)            # premultiplied: no dark fringes
    up = cv2.resize(pm, (size, size), interpolation=cv2.INTER_LANCZOS4)
    up = np.clip(up, 0, 1)
    al2 = up[..., 3:4]
    rgb = np.where(al2 > 1e-3, up[..., :3] / np.maximum(al2, 1e-3), 0)
    rgb = sharpen(np.clip(rgb, 0, 1), 0.35)
    out = np.concatenate([rgb, al2], -1)
    Image.fromarray(np.clip(out * 255 + 0.5, 0, 255).astype(np.uint8), "RGBA").save(
        os.path.join(OUT, "emblems", eid + ".png"))
    return "cards/emblems/%s.png" % eid


art, icons, skins = {}, set(), {}
for f in sorted(glob.glob(src("models/players/*/icon_*.tga"))):
    model = os.path.basename(os.path.dirname(f))
    skin = os.path.basename(f)[5:-4]
    eid = "head_%s" % model if skin == "default" else "head_%s_%s" % (model, skin)
    art[eid] = emblem(f, eid)
    icons.add(eid)
    skins.setdefault(model, []).append(skin)
for k in skins:   # default first, then team skins, then the rest
    skins[k].sort(key=lambda s: (s != "default", s not in ("red", "blue"), s))
icon_files = [w[2] for w in card_catalog.WEAPONS] + [i[2] for i in card_catalog.ITEMS] + ["iconf_red", "iconf_blu"]
for name in icon_files:
    f = src("icons/%s.tga" % name)
    if os.path.isfile(f):
        art[name] = emblem(f, name)
        icons.add(name)
for f in sorted(glob.glob(src("menu/medals/medal_*.tga"))):
    name = os.path.basename(f)[:-4]
    art[name] = emblem(f, name)
    icons.add(name)
print("emblems: %d" % len(icons))

# ---------------------------------------------------------------- levelshot banners

uimaps = json.load(open(os.path.join(LIB, "ui", "maps.json")))
for m in uimaps["maps"]:
    # the remastered map's own shot (tests/levelshots.tscn), else the 128 px original
    shot = next((p for p in [os.path.join(ROOT, "work", "_levelshots", m["map"] + ".jpg")]
                 + [src("levelshots/%s.%s" % (m["map"], e)) for e in ("jpg", "tga")] if os.path.isfile(p)), None)
    if not shot:
        continue
    im = np.array(Image.open(shot).convert("RGB")).astype(np.float32) / 255.0
    # background: the shot stretched across the banner, blurred into a wash and darkened
    h0, w0 = im.shape[:2]
    band = im[h0 * 3 // 8: h0 * 5 // 8]
    wash = cv2.resize(band, (BW, BH), interpolation=cv2.INTER_CUBIC)
    wash = cv2.GaussianBlur(wash, (0, 0), 18) * 0.45
    # foreground: the shot at 384 px (3x the usual 128), cropped to the banner's height, on the right,
    # fading in from the left
    fg = cv2.resize(im, (384, 384 * h0 // w0), interpolation=cv2.INTER_LANCZOS4 if w0 < 384 else cv2.INTER_AREA)
    fg = sharpen(np.clip(fg, 0, 1), 0.4 if w0 < 384 else 0.15)
    top = (fg.shape[0] - BH) // 2
    fg = fg[top:top + BH]
    x0 = BW - fg.shape[1]
    ramp = np.clip(np.arange(fg.shape[1], dtype=np.float32) / 140.0, 0, 1)[None, :, None]
    ramp = ramp * ramp * (3 - 2 * ramp)
    out = wash.copy()
    out[:, x0:] = wash[:, x0:] * (1 - ramp) + fg * ramp
    save_jpg(os.path.join(OUT, "banners", "map_%s.jpg" % m["map"]), out)
    art["map_" + m["map"]] = "cards/banners/map_%s.jpg" % m["map"]

# ---------------------------------------------------------------- frame swatches

def tres_textures(path):
    t = open(path, encoding="utf-8").read()
    return dict(re.findall(r'\[ext_resource type="(?:Texture2D|Shader)" path="([^"]+)" id="([^"]+)"\]', t)), t


def albedo_of(res):
    """A world material's albedo texture (res:// path), from its .tres."""
    p = lib(res)
    if not os.path.isfile(p):
        return None
    paths, t = tres_textures(p)
    ids = {v: k for k, v in paths.items()}
    m = re.search(r'shader_parameter/albedo_tex = ExtResource\("([^"]+)"\)', t)
    return ids.get(m.group(1)) if m else None


wm = json.load(open(os.path.join(LIB, "world", "world_materials.json")))["materials"]
have_mats = {k[len("textures/"):] for k, v in wm.items() if v.get("res")}
for lvl, fid, name, mat, tint in card_catalog.LEVEL_FRAMES:
    info = wm.get("textures/" + mat, {})
    tex = albedo_of(info["res"]) if info.get("res") else None
    if not tex:
        print("frame %s: no albedo for %s" % (fid, mat))
        continue
    im = np.array(Image.open(lib(tex)).convert("RGB")).astype(np.float32) / 255.0
    s = min(im.shape[:2])
    im = cv2.resize(im[:s, :s], (256, 256), interpolation=cv2.INTER_AREA)
    save_jpg(os.path.join(OUT, "frames", "frame_%s.jpg" % fid), im)
    art["frame_" + fid] = "cards/frames/frame_%s.jpg" % fid
for key, name in card_catalog.POWERUP_FRAMES:
    # the shell textures Q3FX uses; Haste has no shell in Quake 3 (it trails smoke puffs)
    f = os.path.join(LIB, "fx", "textures", "smoke_puff.png" if key == "haste" else "shell_%s.png" % key)
    if not os.path.isfile(f):
        print("frame %s: no shell texture" % key)
        continue
    im = np.array(Image.open(f).convert("RGB")).astype(np.float32) / 255.0
    save_jpg(os.path.join(OUT, "frames", "frame_%s.jpg" % key), cv2.resize(im, (256, 256), interpolation=cv2.INTER_AREA))
    art["frame_" + key] = "cards/frames/frame_%s.jpg" % key

# ---------------------------------------------------------------- skies: one per distinct look

SKY_WORDS = {"toxicsky": "Toxic Sky", "hellsky": "Hell Sky", "nightsky": "Night Sky", "blacksky": "Black Sky",
             "killsky": "Kill Sky", "skybox": "Skybox", "space1": "Space"}


def sky_name(sid):
    words = []
    for tok in re.split(r"[_]+", sid):
        tok = re.sub(r"^(x|tim|pj|kc|mkc)(?=[a-z]{4,})", "", tok)
        if tok in ("tim", "pj", "kc", "mkc", "x", "sky", "arena1", "km2", "90", "60"):
            continue
        words.append(SKY_WORDS.get(tok, tok.upper() if re.match(r"^(dm|ctf|q3)", tok) else tok.capitalize()))
    return " ".join(words) or sid


skies, seen = [], {}
for f in sorted(glob.glob(os.path.join(LIB, "world", "skies", "skies", "*.tres"))):
    sid = os.path.basename(f)[:-5]
    paths, t = tres_textures(f)
    look = tuple(sorted(p for p in paths if p.endswith((".jpg", ".png"))))
    if not look or look in seen or all("colors__black" in p for p in look):   # blacksky is plain black
        continue
    seen[look] = sid
    skies.append((sid, sky_name(sid)))
# names must be unique on the card
names = {}
for i, (sid, n) in enumerate(skies):
    names.setdefault(n, []).append(i)
for n, idx in names.items():
    if len(idx) > 1:
        for k, i in enumerate(idx):
            skies[i] = (skies[i][0], "%s %s" % (n, ["I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"][k]))
print("skies: %d distinct of %d" % (len(skies), len(glob.glob(os.path.join(LIB, "world", "skies", "skies", "*.tres")))))

# ---------------------------------------------------------------- Godot renders

index = json.load(open(os.path.join(LIB, "index.json")))
chars = index["characters"]
render_dir = os.path.join(OUT, "_render")
jobs = [dict(id="char_" + c, kind="char", subject=c) for c in chars]
jobs += [dict(id="sky_" + s, kind="sky", subject="res://library/world/skies/skies/%s.tres" % s) for s, _ in skies]
jobs += [dict(id="mat_" + b[0], kind="material", subject=wm["textures/" + b[2]]["res"])
         for b in card_catalog.MATERIAL_BANNERS if b[2] in have_mats]
for j in jobs:
    j["out"] = os.path.abspath(os.path.join(render_dir, j["id"] + ".png")).replace("\\", "/")
if not a.no_render:
    godot = next((c for c in [a.godot] + q3.default_godot() if c and os.path.isfile(c)), None)
    if not godot:
        sys.exit("build_cards: Godot not found; pass --godot or use --no-render")
    os.makedirs(render_dir, exist_ok=True)
    job = os.path.abspath(os.path.join(OUT, "_render_job.json"))
    json.dump(dict(size=[BW * 2, BH * 2], items=jobs), open(job, "w"), indent=1)
    log = os.path.abspath(os.path.join(OUT, "_render.log"))
    print("rendering %d banners in Godot ..." % len(jobs), flush=True)
    subprocess.run([godot, "--path", GODOT_PROJECT, "--quit-after", "20000", "--log-file", log,
                    "res://tests/card_renders.tscn", "--", "job=" + job.replace("\\", "/")],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    errs = [l.strip() for l in open(log, encoding="utf-8", errors="replace") if "SCRIPT ERROR" in l or l.startswith("ERROR")]
    if errs:
        print("godot: %d error line(s): %s" % (len(errs), errs[0]))
done = 0
for j in jobs:
    f = j["out"]
    if not os.path.isfile(f):
        print("missing render:", j["id"])
        continue
    im = np.array(Image.open(f).convert("RGB")).astype(np.float32) / 255.0
    im = cv2.resize(im, (BW, BH), interpolation=cv2.INTER_AREA)
    save_jpg(os.path.join(OUT, "banners", j["id"] + ".jpg"), im)
    art[j["id"]] = "cards/banners/%s.jpg" % j["id"]
    done += 1
print("renders: %d/%d" % (done, len(jobs)))

# ---------------------------------------------------------------- catalog

cat = card_catalog.build(dict(maps=uimaps["maps"], bots=uimaps["bots"], characters=chars, skins=skins, skies=skies,
                              materials=have_mats, icons=icons, art=art))
json.dump(cat, open(os.path.join(OUT, "cards.json"), "w"), indent=1)
counts = {k: len(v) for k, v in cat["parts"].items()}
print("cards: %d parts %s, %d achievements" % (sum(counts.values()), counts, len(cat["achievements"])))

# ---------------------------------------------------------------- check sheet

tiles = []
for kind in ("banners", "emblems", "frames"):
    for p in cat["parts"][kind]:
        if "art" in p:
            tiles.append((kind, p["name"], os.path.join(OUT, *p["art"].split("/")[1:])))
bw, bh, ew = 384, 96, 96
cols = 6
rows_b = (sum(1 for t in tiles if t[0] == "banners") + 2) // 3
width = 3 * (bw + 8) + 8
per_row = (width - 8) // (ew + 2)
rows_e = (sum(1 for t in tiles if t[0] != "banners") + per_row - 1) // per_row
sheet = Image.new("RGB", (width, rows_b * (bh + 22) + rows_e * (ew + 22) + 16), (14, 12, 12))
dr = ImageDraw.Draw(sheet)
x = y = 8
for kind, name, f in [t for t in tiles if t[0] == "banners"]:
    sheet.paste(Image.open(f).convert("RGB").resize((bw, bh), Image.LANCZOS), (x, y))
    dr.text((x + 2, y + bh + 4), name, fill=(220, 200, 190))
    x += bw + 8
    if x + bw > sheet.width:
        x, y = 8, y + bh + 22
y += bh + 22 if x != 8 else 0
x = 8
for kind, name, f in [t for t in tiles if t[0] != "banners"]:
    im = Image.open(f).convert("RGBA").resize((ew, ew), Image.LANCZOS)
    sheet.paste(im, (x, y), im)
    dr.text((x + 2, y + ew + 4), name[:16], fill=(220, 200, 190))
    x += ew + 2
    if x + ew > sheet.width:
        x, y = 8, y + ew + 22
sheet.save(os.path.join(OUT, "_sheet.jpg"), quality=85)
print("sheet: %s" % os.path.join(OUT, "_sheet.jpg"))
