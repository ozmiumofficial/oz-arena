"""Step 3: PBR maps from the hand-painted id Tech 3 diffuse textures.

python make_pbr.py <work dir> [--face-band 0.62] [--seed 3]

For every texture in <work>/textures.json: pad UV islands -> upscale (up to 8x, long edge
about 2048) with edge-preserving cleanup -> material classes from color (bronze armor,
dark steel, heat, fiber, skin) -> move the painted lighting out of the albedo into AO ->
height from luminance + class relief -> normal + curvature -> edge wear, scratches, grunge.
Writes <work>/tex/<stem>_albedo / _normal (OpenGL, green up) / _orm (R=AO, G=rough,
B=metal, the glTF and Godot layout) / _emissive .jpg, plus 512 px previews prev_<stem>_*.jpg.

Textures used by the head get skin treatment for warm tones. --face-band Y (0..1) marks
everything above that image height as face, below as mechanical (Keel: 0.62). A texture with
"heat_from" (team and alternate skins, build_skins.py) glows only where that image, the default
set's painting of the same surfaces, glows too.

World textures (kind "tile", from build_world.py) tile: every filter runs on a wrap-padded
copy that is cropped afterwards, so the result stays seamless. Their "group" (metal, stone,
organic, light, liquid) sets the material rules; painted red never glows (only shader glow
layers do). Kind "glow" = a tiling additive layer (emissive only), "sky" = sky faces/clouds,
"additive" = an additive shader's base layer: upscaled like a glow layer, black kept black, and
written as <stem>_albedo (what the world_add shaders draw).
--jobs N processes textures in parallel (each texture is seeded from its name, so results
don't depend on the order).
"""
import os, sys, json, argparse, zlib
import numpy as np
import cv2
from PIL import Image

ap = argparse.ArgumentParser()
ap.add_argument("work")
ap.add_argument("--face-band", type=float, default=None)
ap.add_argument("--seed", type=int, default=3)
ap.add_argument("--jobs", type=int, default=1)
ap.add_argument("--only", help="comma-separated stems to (re)process")
a = ap.parse_args()
SRC, CAGE, OUT = (os.path.join(a.work, d) for d in ("src", "cage", "tex"))
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(a.seed)

# per class: roughness, metal. Kept matte on purpose (Ozmium 2026-10-22: low gloss, no shine);
# painted armor reads as satin paint over metal, not polished metal.
CLASSES = ["bronze", "steel", "heat", "fiber", "skin"]
PARAMS = {
    "body": dict(bronze=(0.70, 0.45), steel=(0.62, 0.50), heat=(0.78, 0.0), fiber=(0.88, 0.0), skin=(0.66, 0.0)),
    "head": dict(bronze=(0.70, 0.0), steel=(0.64, 0.45), heat=(0.78, 0.0), fiber=(0.88, 0.0), skin=(0.66, 0.0)),
}
# world groups: every class keeps the matte rule; stone and organic never turn metal
PARAMS.update({
    "metal": dict(bronze=(0.62, 0.55), steel=(0.58, 0.60), heat=(0.70, 0.15), fiber=(0.80, 0.10), skin=(0.72, 0.0)),
    "light": dict(bronze=(0.60, 0.45), steel=(0.58, 0.55), heat=(0.70, 0.10), fiber=(0.55, 0.05), skin=(0.70, 0.0)),
    "stone": dict(bronze=(0.80, 0.0), steel=(0.86, 0.0), heat=(0.85, 0.0), fiber=(0.82, 0.0), skin=(0.85, 0.0)),
    "organic": dict(bronze=(0.66, 0.0), steel=(0.75, 0.0), heat=(0.62, 0.0), fiber=(0.80, 0.0), skin=(0.66, 0.0)),
    "liquid": dict(bronze=(0.50, 0.0), steel=(0.55, 0.0), heat=(0.60, 0.0), fiber=(0.55, 0.0), skin=(0.55, 0.0)),
})
ROUGH_MIN = 0.45  # nothing gets glossier than this
VIS = dict(bronze=(70, 160, 230), steel=(120, 120, 120), heat=(40, 40, 230), fiber=(170, 220, 230), skin=(140, 170, 240))


def smooth(x, lo, hi):
    t = np.clip((x - lo) / (hi - lo), 0, 1)
    return t * t * (3 - 2 * t)


def blur(x, s):
    return cv2.GaussianBlur(x, (0, 0), s)


def fbm(h, w, octaves=5, base=8):
    acc = np.zeros((h, w), np.float32)
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        n = base * 2 ** o
        acc += amp * cv2.resize(rng.random((n, n)).astype(np.float32), (w, h), interpolation=cv2.INTER_CUBIC)
        tot += amp
        amp *= 0.5
    return acc / tot


def scratches(h, w, count, length):
    m = np.zeros((h, w), np.float32)
    for _ in range(count):
        x, y = rng.integers(0, w), rng.integers(0, h)
        ang = rng.normal(0.6, 0.5)
        L = rng.uniform(0.3, 1.0) * length
        cv2.line(m, (int(x), int(y)), (int(x + np.cos(ang) * L), int(y + np.sin(ang) * L)),
                 float(rng.uniform(0.3, 1.0)), 1, cv2.LINE_AA)
    return m


def write_maps(stem, albedo, n, orm, emis, alpha=None):
    """albedo/emis BGR floats, n xyz normals, orm = (ao, rough, metal). Albedo becomes a PNG
    when it carries alpha (cut-outs, translucent layers), otherwise JPEG like the rest."""
    def jpg(name, arr, q=92):
        cv2.imwrite(os.path.join(OUT, "%s_%s.jpg" % (stem, name)), np.clip(arr * 255, 0, 255).astype(np.uint8),
                    [cv2.IMWRITE_JPEG_QUALITY, q])
    if alpha is not None:
        rgba = np.dstack([albedo, alpha[..., None]])
        cv2.imwrite(os.path.join(OUT, "%s_albedo.png" % stem), np.clip(rgba * 255, 0, 255).astype(np.uint8))
    else:
        jpg("albedo", albedo)
    jpg("normal", n[..., ::-1] * 0.5 + 0.5, 95)    # xyz -> BGR for cv2
    ao, rough, metal = orm
    jpg("orm", np.dstack([metal, rough, ao]))      # BGR order, so the file is R=AO G=rough B=metal
    jpg("emissive", emis)


def upscale_clean(img, W, H):
    up = np.clip(cv2.resize(img, (W, H), interpolation=cv2.INTER_LANCZOS4), 0, 255).astype(np.uint8)
    for _ in range(2):
        up = cv2.bilateralFilter(up, 9, 18, 7)
    f = up.astype(np.float32) / 255.0
    return np.clip(f + 0.6 * (f - blur(f, 3.0)), 0, 1)


def previews(stem, maps, orig, H, W):
    pw, ph = 512, max(1, 512 * H // W)
    for name, arr in maps:
        small = cv2.resize(np.ascontiguousarray(arr).astype(np.float32), (pw, ph), interpolation=cv2.INTER_AREA)
        cv2.imwrite(os.path.join(OUT, "prev_%s_%s.jpg" % (stem, name)), np.clip(small * 255, 0, 255).astype(np.uint8),
                    [cv2.IMWRITE_JPEG_QUALITY, 85])
    cv2.imwrite(os.path.join(OUT, "prev_%s_original.jpg" % stem),
                cv2.resize(orig, (pw, ph), interpolation=cv2.INTER_NEAREST), [cv2.IMWRITE_JPEG_QUALITY, 90])


def wrap_pad(img, w0, h0):
    """Pad a tiling image with its own opposite edges so filters see the neighbor tile."""
    P = max(1, min(24, w0 // 2, h0 // 2))
    return cv2.copyMakeBorder(img, P, P, P, P, cv2.BORDER_WRAP), P


def process_flat(t, kind):
    """Additive energy layers, glow layers, skies and reflection-only items: no painted
    surface to analyze."""
    stem, UP = t["stem"], t["up"]
    img = np.ascontiguousarray(np.array(Image.open(os.path.join(SRC, t["file"])).convert("RGB"))[..., ::-1])
    h0, w0 = img.shape[:2]
    if kind in ("glow", "sky", "additive"):
        if kind in ("glow", "additive"):
            padded, P = wrap_pad(img, w0, h0)
            up = upscale_clean(padded, (w0 + 2 * P) * UP, (h0 + 2 * P) * UP)[P * UP:(P + h0) * UP, P * UP:(P + w0) * UP]
            name = "%s_%s.jpg" % (stem, "emissive" if kind == "glow" else "albedo")
        else:
            up = upscale_clean(img, w0 * UP, h0 * UP)
            name = "%s.jpg" % stem
        cv2.imwrite(os.path.join(OUT, name), np.clip(up * 255, 0, 255).astype(np.uint8), [cv2.IMWRITE_JPEG_QUALITY, 92])
        print("%-28s %-6s %4dx%-4d" % (stem, kind, w0 * UP, h0 * UP), flush=True)
        return dict(stem=stem, role=kind, size=[w0 * UP, h0 * UP], classes={})
    if kind == "add":
        H, W = h0 * UP, w0 * UP
        emis = upscale_clean(img, W, H)
        albedo = np.full((H, W, 3), 0.02, np.float32)
        orm = (np.ones((H, W), np.float32), np.full((H, W), 0.9, np.float32), np.zeros((H, W), np.float32))
    else:
        # one tone from the reflection map, kept satin (matte rule): chrome -> painted metal
        H = W = 256
        c = img.reshape(-1, 3).astype(np.float32).mean(0) / 255.0
        c = c * (0.8 / max(c.max(), 1e-3))
        var = (0.94 + 0.12 * fbm(H, W, 5, 8))[..., None]
        if kind == "env":
            albedo, emis = np.ones((H, W, 3), np.float32) * c * var, np.zeros((H, W, 3), np.float32)
            orm = (np.ones((H, W), np.float32), np.clip(0.5 + 0.1 * (fbm(H, W, 4, 16) - 0.5), ROUGH_MIN, 1),
                   np.full((H, W), 0.85, np.float32))
        else:  # envadd: glowing glass shell (health spheres)
            albedo = np.ones((H, W, 3), np.float32) * c * 0.15
            emis = np.ones((H, W, 3), np.float32) * c * 0.9 * var
            orm = (np.ones((H, W), np.float32), np.full((H, W), 0.5, np.float32), np.zeros((H, W), np.float32))
    n = np.dstack([np.zeros((H, W)), np.zeros((H, W)), np.ones((H, W))]).astype(np.float32)
    write_maps(stem, albedo, n, orm, emis)
    previews(stem, (("albedo", albedo), ("emissive", emis), ("rough", orm[1]), ("metal", orm[2])), img, H, W)
    print("%-20s %-6s %4dx%-4d" % (stem, kind, W, H))
    return dict(stem=stem, role=kind, size=[W, H], classes={})


def process(t):
    global rng
    stem, UP = t["stem"], t["up"]
    kind = t.get("kind", "paint")
    if kind in ("tile", "glow", "sky", "additive"):
        rng = np.random.default_rng(a.seed + zlib.crc32(stem.encode()))  # order-independent in a pool
    if kind in ("add", "env", "envadd", "glow", "sky", "additive"):
        return process_flat(t, kind)
    tile = kind == "tile"
    role = t.get("group", "metal") if tile else ("head" if "head" in t["parts"] else "body")
    src_img = Image.open(os.path.join(SRC, t["file"]))
    img = np.ascontiguousarray(np.array(src_img.convert("RGB"))[..., ::-1])  # BGR
    alpha_src = np.array(src_img.convert("RGBA"))[..., 3] if t.get("alpha") else None
    under_a = None
    if t.get("under"):
        # paint drawn over a reflection layer: fill its transparent parts with the reflection's
        # tone (they become satin metal below) and make the result opaque
        uimg = np.array(Image.open(os.path.join(SRC, t["under"])).convert("RGB"))[..., ::-1].astype(np.float32)
        tone = uimg.reshape(-1, 3).mean(0)
        tone = tone * (0.8 * 255 / max(tone.max(), 1.0))
        under_a = np.array(src_img.convert("RGBA"))[..., 3].astype(np.float32) / 255.0
        img = np.clip(img * under_a[..., None] + tone * (1 - under_a[..., None]), 0, 255).astype(np.uint8)
        alpha_src = None
    pad = 0
    if tile:  # filter a wrap-padded copy; cropped back before writing, so edges stay seamless
        img, pad = wrap_pad(img, img.shape[1], img.shape[0])
        if alpha_src is not None:
            alpha_src = cv2.copyMakeBorder(alpha_src, pad, pad, pad, pad, cv2.BORDER_WRAP)
    h0, w0 = img.shape[:2]
    H, W = h0 * UP, w0 * UP
    mpath = os.path.join(CAGE, "uv_%s.png" % stem)
    cover_hi = cv2.imread(mpath, cv2.IMREAD_GRAYSCALE) if os.path.exists(mpath) else None
    if cover_hi is None or cover_hi.max() == 0:
        cover = np.ones((h0, w0), bool)
    else:
        cover = cv2.resize(cover_hi, (w0, h0), interpolation=cv2.INTER_AREA) > 0
        cover = cv2.dilate(cover.astype(np.uint8), np.ones((5, 5), np.uint8)) > 0
    # 1. pad islands so filtering never pulls the gutter into used texels
    padded = cv2.inpaint(img, (~cover).astype(np.uint8) * 255, 4, cv2.INPAINT_TELEA) if not cover.all() else img
    # 2. upscale + edge-preserving cleanup of the interpolation steps. World art is fine grain
    # (granite, rust): a strong bilateral after the upscale posterized it into blobs and the
    # sharpening outlined them, so tiles get a light denoise at their own size and a gentle sharpen.
    if tile:
        den = cv2.bilateralFilter(padded, 5, 10, 1.5)
        up = np.clip(cv2.resize(den, (W, H), interpolation=cv2.INTER_LANCZOS4), 0, 255).astype(np.uint8)
        f = up.astype(np.float32) / 255.0
        sharp = np.clip(f + 0.3 * (f - blur(f, 0.5 * UP)), 0, 1)
    else:
        up = np.clip(cv2.resize(padded, (W, H), interpolation=cv2.INTER_LANCZOS4), 0, 255).astype(np.uint8)
        for _ in range(2):
            up = cv2.bilateralFilter(up, 9, 18, 7)
        f = up.astype(np.float32) / 255.0
        sharp = np.clip(f + 0.6 * (f - blur(f, 3.0)), 0, 1)
    b, g, r = sharp[..., 0], sharp[..., 1], sharp[..., 2]
    hsv = cv2.cvtColor((sharp * 255).astype(np.uint8), cv2.COLOR_BGR2HSV).astype(np.float32)
    hue, sat = hsv[..., 0] * 2, hsv[..., 1] / 255
    L = 0.2126 * r + 0.7152 * g + 0.0722 * b

    # 3. material classes (soft)
    redness = r - np.maximum(g, b)
    red_hue = np.maximum(1 - smooth(hue, 10, 16), smooth(hue, 340, 350))  # deep reds, not orange rust
    heat = smooth(redness, 0.12, 0.28) * smooth(sat, 0.45, 0.7) * red_hue
    if t.get("heat_from"):
        # a team or alternate skin (build_skins.py) glows only where its default image does at the
        # same texels: the red team's armor is red paint, not heat
        ref = np.array(Image.open(os.path.join(SRC, t["heat_from"])).convert("RGB"))[..., ::-1]
        ref = cv2.resize(ref, (W, H), interpolation=cv2.INTER_LINEAR).astype(np.float32) / 255.0
        rhsv = cv2.cvtColor((ref * 255).astype(np.uint8), cv2.COLOR_BGR2HSV).astype(np.float32)
        rhue = rhsv[..., 0] * 2
        rheat = smooth(ref[..., 2] - np.maximum(ref[..., 1], ref[..., 0]), 0.12, 0.28) * smooth(rhsv[..., 1] / 255, 0.45, 0.7) \
            * np.maximum(1 - smooth(rhue, 10, 16), smooth(rhue, 340, 350))
        heat = np.minimum(heat, blur(rheat, 2.0))
    dark = (1 - smooth(L, 0.10, 0.24)) * (1 - heat)
    warm = smooth(hue, 8, 18) * (1 - smooth(hue, 48, 60))
    fiber = smooth(L, 0.45, 0.6) * (1 - smooth(sat, 0.2, 0.38)) * (1 - heat) * (1 - dark)
    rest = np.clip(1 - heat - dark - fiber, 0, 1)
    if role == "head":
        if a.face_band is not None:
            yy = np.linspace(0, 1, H)[:, None] * np.ones((1, W))
            face = 1 - smooth(yy, a.face_band, a.face_band + 0.08)
            skin = rest * warm * face + dark * face  # eye sockets, beard: still skin
            dark = dark * (1 - face)
            bronze = rest * (1 - warm * face)
        else:
            skin = rest * warm
            bronze = rest * (1 - warm)
    else:
        skin = np.zeros_like(L)
        bronze = rest
    masks = dict(bronze=bronze, steel=dark, heat=heat, fiber=fiber, skin=skin)
    masks = {k: blur(v.astype(np.float32), 1.5) for k, v in masks.items()}
    tot = sum(masks.values()) + 1e-6
    masks = {k: v / tot for k, v in masks.items()}

    # 4. de-light: divide out the low-frequency painted shading, keep it for AO
    Llow = blur(L, 3.0 * UP)
    target = sum(masks[k] * (np.sum(masks[k] * L) / (np.sum(masks[k]) + 1e-6)) for k in masks)
    shade = np.clip(Llow / (target + 1e-4), 0.25, 2.5)
    albedo = sharp / shade[..., None] ** (0.3 if tile else 0.45)  # world art keeps more of its painting
    glint = smooth(L - Llow, 0.18, 0.4) * (1 - sat) * (masks["bronze"] + masks["steel"])
    albedo = albedo * (1 - 0.5 * glint[..., None]) + 0.5 * glint[..., None] * blur(albedo, 6)

    # 5. height: plates stand proud of the dark mechanics, plus painted detail
    hp_big = L - blur(L, 4.0 * UP)
    if tile:  # bumps from the grain itself crawled like worms under the map lights (2026-10-23)
        Ls = blur(L, 0.35 * UP)
        hp_fine = Ls - blur(Ls, 0.8 * UP)
    else:
        hp_fine = L - blur(L, 0.6 * UP)
    relief = blur(masks["bronze"] + 0.6 * masks["fiber"] + 0.4 * masks["skin"] + 0.3 * masks["heat"], 1.2 * UP)
    lump = (fbm(H, W, 4, 24) - 0.5) * masks["heat"]
    grain = (fbm(H, W, 3, 128) - 0.5) * (masks["bronze"] + masks["steel"])
    height = (0.55 * relief + 1.1 * hp_big + (0.2 if tile else 0.5) * hp_fine + 0.6 * lump + 0.04 * grain).astype(np.float32)

    # normal (OpenGL convention: green = up in the image); world art is flat paint, so half as steep
    strength = 5.0 if tile else 10.0
    gx = cv2.Sobel(height, cv2.CV_32F, 1, 0, ksize=3) / 8.0 * strength
    gy = cv2.Sobel(height, cv2.CV_32F, 0, 1, ksize=3) / 8.0 * strength
    n = np.dstack([-gx, gy, np.ones_like(gx)])
    n /= np.linalg.norm(n, axis=2, keepdims=True)

    # 6. curvature -> edge wear, cavity dirt
    lap = cv2.Laplacian(blur(height, max(1.2 * UP / 4, 0.8)), cv2.CV_32F, ksize=5)
    lap /= np.percentile(np.abs(lap), 99) + 1e-6
    convex, cavity = smooth(-lap, 0.15, 0.6), smooth(lap, 0.1, 0.5)
    if tile:
        metalmask = (masks["bronze"] + masks["steel"]) if role in ("metal", "light") else np.zeros_like(L)
    else:
        metalmask = masks["bronze"] * (1 if role == "body" else 0) + masks["steel"]
    wear = convex * smooth(fbm(H, W, 5, 16), 0.42, 0.62) * metalmask
    nscr = int((300 if tile else 900 if role == "body" else 250) * W * H / 2048 ** 2) + 20
    scr = blur(scratches(H, W, nscr, 40 * UP / 8), 0.6) * metalmask * smooth(fbm(H, W, 3, 6), 0.4, 0.7)
    grunge = fbm(H, W, 6, 6)

    bare = np.array([0.55, 0.62, 0.72], np.float32)  # BGR: worn bronze/brass
    albedo = albedo * (1 - 0.18 * cavity[..., None]) * (0.94 + 0.12 * grunge[..., None])
    wk = 0.3 if tile else 0.7  # full-strength wear streaked every panel line on the world metal
    albedo = albedo * (1 - wear[..., None] * wk) + wear[..., None] * wk * bare
    albedo = albedo * (1 - 0.25 * scr[..., None]) + 0.25 * scr[..., None] * bare
    if tile and role in ("stone", "organic"):  # chipped carving edges catch a little light, no metal
        chip = convex * smooth(fbm(H, W, 5, 16), 0.45, 0.65)
        albedo = albedo * (1 + 0.22 * chip[..., None])
    albedo = np.clip(albedo, 0.02, 0.95)

    P = PARAMS[role]
    rough = sum(masks[c] * P[c][0] for c in CLASSES) + 0.12 * (grunge - 0.5) + 0.15 * cavity - 0.05 * wear - 0.04 * scr
    metal = np.maximum(sum(masks[c] * P[c][1] for c in CLASSES), wear * 0.7)
    if under_a is not None:
        bare = 1 - np.clip(cv2.resize(under_a, (W, H), interpolation=cv2.INTER_LINEAR), 0, 1)
        metal = metal * (1 - bare) + 0.85 * bare
        rough = rough * (1 - bare) + 0.5 * bare
    rough, metal = np.clip(rough, ROUGH_MIN, 0.98), np.clip(metal, 0, 1)
    ao = np.clip(shade ** 0.8, 0, 1) * (1 - 0.35 * cavity)
    ao = np.clip(ao / max(np.percentile(ao, 97), 1e-3), 0, 1)

    # 7. emissive: the red heat pockets glow from their brightest cores
    core = masks["heat"] * smooth(r, 0.22, 0.55) * (0.6 + 0.8 * fbm(H, W, 3, 12))
    if tile and role != "liquid":
        core = core * 0.0  # painted red bricks/rust don't glow; world glow comes from shader layers
    emis = np.dstack([0.05 * core, 0.28 * core, 1.0 * core]) * np.clip(sharp[..., 2:3] * 1.6, 0, 1)

    if t.get("glow"):  # a shader's extra additive layer (glowing runes, energy strips)
        gimg = np.ascontiguousarray(np.array(Image.open(os.path.join(SRC, t["glow"])).convert("RGB"))[..., ::-1])
        emis = np.maximum(emis, upscale_clean(gimg, W, H) * 0.9)
    alpha = None
    if alpha_src is not None:
        alpha = np.clip(cv2.resize(alpha_src.astype(np.float32) / 255.0, (W, H), interpolation=cv2.INTER_LANCZOS4), 0, 1)
    covered = cv2.resize(cover.astype(np.uint8), (W, H)) > 0
    share = {c: round(float(np.mean(masks[c][covered])), 3) for c in CLASSES}
    vis = sum(np.array(VIS[c], np.float32)[None, None, :] / 255 * masks[c][..., None] for c in CLASSES)
    vis = vis * (0.5 + 0.5 * L[..., None] / (L.max() + 1e-6))
    if pad:  # drop the wrap padding
        c0 = pad * UP
        crop = lambda x: None if x is None else x[c0:H - c0, c0:W - c0]
        albedo, n, ao, rough, metal, emis, alpha, sharp, vis = map(crop, (albedo, n, ao, rough, metal, emis, alpha, sharp, vis))
        img = img[pad:h0 - pad, pad:w0 - pad]
        H, W = H - 2 * c0, W - 2 * c0
    write_maps(stem, albedo, n, (ao, rough, metal), emis, alpha)
    pw, ph = 512, max(1, 512 * H // W)
    maps = (("albedo", albedo),) if t.get("previews") == "light" else (
        ("classes", vis), ("upscaled", sharp), ("albedo", albedo), ("normal", n[..., ::-1] * 0.5 + 0.5),
        ("rough", rough), ("metal", metal), ("ao", ao), ("emissive", emis))
    for name, arr in maps:
        small = cv2.resize(np.ascontiguousarray(arr).astype(np.float32), (pw, ph), interpolation=cv2.INTER_AREA)
        cv2.imwrite(os.path.join(OUT, "prev_%s_%s.jpg" % (stem, name)), np.clip(small * 255, 0, 255).astype(np.uint8),
                    [cv2.IMWRITE_JPEG_QUALITY, 85])
    cv2.imwrite(os.path.join(OUT, "prev_%s_original.jpg" % stem),
                cv2.resize(img, (pw, ph), interpolation=cv2.INTER_NEAREST), [cv2.IMWRITE_JPEG_QUALITY, 90])
    print("%-14s %-4s %4dx%-4d %s" % (stem, role, W, H, share), flush=True)
    return dict(stem=stem, role=role, size=[W, H], classes=share)


def _safe(t):
    try:
        return process(t)
    except Exception as e:  # one bad image must not sink a 1,000-texture batch
        print("FAILED %s: %r" % (t["stem"], e), flush=True)
        return dict(stem=t["stem"], error=repr(e))


if __name__ == "__main__":
    textures = json.load(open(os.path.join(a.work, "textures.json")))
    if a.only:
        keep = set(a.only.split(","))
        textures = [t for t in textures if t["stem"] in keep]
    if a.jobs > 1:
        from multiprocessing import Pool
        with Pool(a.jobs) as pool:
            report = pool.map(_safe, textures, chunksize=4)
    else:
        report = [_safe(t) for t in textures]
    json.dump(report, open(os.path.join(a.work, "pbr_report.json"), "w"), indent=1)
    bad = [r for r in report if "error" in r]
    print("%d textures, %d failed" % (len(report), len(bad)))
