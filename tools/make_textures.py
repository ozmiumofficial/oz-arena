"""Oz's texture library, made from the player's own id images: every texture the pak tools ship, in every layer.

python tools/make_textures.py [--inventory] [--kind world|characters|props|fx ...] [--only <id image> ...]
                              [--force] [--sheets] [--report] [--recheck]

Replaces the frozen Godot build's library as the pak tools' source (ENGINE_PLAN.md P1, the texture pass,
2026-10-30). That library's make_pbr.py smeared id's paint with bilateral filters, ringed its edges with
an unsharp mask and stamped procedural wear, scratches and grunge into every map; Ozmium found it noisy
and blurry. A pilot on a few subjects settled the method:

  color   Real-ESRGAN x4plus (tools/sr_model.py) on id's original, its colors kept id's below 2 source
          texels; where the model smeared id's grain into a flat tone, the halo-free Lanczos upscale's
          detail instead; elsewhere 70% of the model's detail, capped at twice id's own near each texel
          (without the cap it drew bricks and ornament where id painted soft bands). Images on
          tools/texture_overrides.json (the model restyled them: diamond plate) and sources of 32 px or less
          take the halo-free Lanczos alone.
  maps    the albedo as painted (no de-lighting), id's alpha kept wherever it holds anything. Normals only from the painting's shapes: brightness
          smoothed over half a source texel, high-passed at 4 texels, slopes under about 4 degrees dropped,
          a fixed strength per source texel. Roughness and metal from make_pbr.py's own color classes and
          values, without its noise; AO from cavities. Roughness never under 0.45.
  layers  glow, additive and sky layers: the color the same way, black kept black. The effects' flipbooks
          (godot/library/fx/fx.json's layout) with make_fx.py's conversions: darkening marks become soot
          decals, additive frames opaque.

The inventory (--inventory, build/oz_library/inventory.json) is what the pak tools look up: make_pk3s.py's,
make_props.py's and make_fighters.py's own discovery runs with make_pk3s.tile and make_pk3s.layer recording
each (image, layer) instead of converting it, and each lookup is named as the Godot library names it, so
the tools read build/oz_library/<kind> unchanged. Images the Godot library never remastered are in it too.

Writes build/oz_library/{world,characters,props,fx} (PNG; an albedo with alpha keeps it) and report.json:
per image the method and its checks against id's original (detail, halos, flat-area noise, brightness,
seams, alpha, normals); --report lists the flagged, --recheck measures again from the files (no model run).
--sheets writes review sheets (build/oz_library/sheets/, index.html): id's image, the Godot library's map, the
new one and the Lanczos alone, 1:1. The pak tools take a library once its made.json says every image of its
kind was made. id's images and everything made from them are build output, never committed.
"""
import os, io, sys, json, glob, time, shutil, struct, hashlib, argparse
import numpy as np
import cv2
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

ap = argparse.ArgumentParser()
ap.add_argument("--inventory", action="store_true", help="record what the pak tools look up, then stop")
ap.add_argument("--kind", nargs="*", choices=["world", "characters", "props", "fx"], help="only these libraries")
ap.add_argument("--only", nargs="*", help="only these id images (paths without extension)")
ap.add_argument("--force", action="store_true", help="make every image again")
ap.add_argument("--sheets", action="store_true", help="write the review sheets from what's made")
ap.add_argument("--report", action="store_true", help="print report.json's failing list")
ap.add_argument("--recheck", action="store_true", help="redo the image checks from the files made (no model run)")
ap.add_argument("--out", default=os.path.join(ROOT, "build", "oz_library"))
ap.add_argument("--baseq3", help="the player's baseq3 (default: found in the Steam libraries)")
# parsed when run; imported, the defaults
a = ap.parse_args() if __name__ == "__main__" else ap.parse_args([])

# the pak tools parse their own arguments on import (make_fighters.py always does)
_argv, sys.argv = sys.argv, sys.argv[:1]
import make_pk3s, make_props, make_fx_pk3, make_fighters, q3shader
sys.argv = _argv
from q3paks import find_baseq3, Paks

GODOT = os.path.join(make_pk3s.GODOT, "godot", "library")
VERSION = 2                   # the method's version: a change makes every image again
OVERRIDES = os.path.join(HERE, "texture_overrides.json")
MAX = 1024                    # the pak tools' --max-size
ROUGH_MIN = 0.45              # matte rule (CLAUDE.md)
GAN = 0.7                     # the model's share of the detail where it keeps id's
# The model's detail is capped at this many times id's own (as energy, near each texel): where id painted a
# soft band it drew bricks, struts or ornament (grate1_4, baseboard08_e, the teleporter's widget); where id
# painted edges, its sharper ones stay (the machinegun, Sarge's hair and face, the gothic carvings).
CAP = 2.0
TINY = 32                     # sources this small (Sarge's 16x8 cigar glow) give the model too little: Lanczos


# ------------------------------------------------------------------ image helpers

def smooth(x, lo, hi):
    t = np.clip((x - lo) / (hi - lo), 0, 1)
    return t * t * (3 - 2 * t)


def blur(x, s):
    return cv2.GaussianBlur(x, (0, 0), s)


def wpad(x, n):
    return np.pad(x, ((n, n), (n, n)) + ((0, 0),) * (x.ndim - 2), mode="wrap")


def lanczos(img, size, tile):
    """The halo-free Lanczos: Lanczos, a light unsharp mask, then each pixel clamped into its source
    neighborhood's range, so edges are crisp with no halos and flat paint stays flat. img: HxWxC uint8."""
    h0, w0 = img.shape[:2]
    W, H = size
    if W <= w0 and H <= h0:
        return cv2.resize(img, (W, H), interpolation=cv2.INTER_AREA).astype(np.float32).reshape(H, W, -1)
    sx, sy = W / w0, H / h0
    P = 8 if tile else 2
    src = wpad(img, P) if tile else np.pad(img, ((P, P), (P, P)) + ((0, 0),) * (img.ndim - 2), mode="edge")
    Wp, Hp = int(round(src.shape[1] * sx)), int(round(src.shape[0] * sy))
    f = cv2.resize(src, (Wp, Hp), interpolation=cv2.INTER_LANCZOS4).astype(np.float32).reshape(Hp, Wp, -1)
    up = max(sx, sy)
    f = f + 0.5 * (f - blur(f, 0.6 * up).reshape(f.shape))
    k = np.ones((3, 3), np.uint8)
    lo = cv2.resize(cv2.erode(src, k), (Wp, Hp), interpolation=cv2.INTER_LINEAR).astype(np.float32).reshape(f.shape)
    hi = cv2.resize(cv2.dilate(src, k), (Wp, Hp), interpolation=cv2.INTER_LINEAR).astype(np.float32).reshape(f.shape)
    f = np.clip(np.minimum(np.maximum(f, lo), hi), 0, 255)
    cx, cy = int(round(P * sx)), int(round(P * sy))
    return np.ascontiguousarray(f[cy:cy + H, cx:cx + W])


_net = None


def model_up(img, size, tile):
    """Real-ESRGAN 4x of a BGR uint8 image, brought to size (area down, or the halo-free Lanczos up)."""
    global _net
    if _net is None:
        import sr_model
        _net = sr_model.Net()
    h0, w0 = img.shape[:2]
    P = 16 if tile else 0
    y = _net.up4(wpad(img, P) if P else img)
    if P:
        y = y[4 * P:-4 * P, 4 * P:-4 * P]
    W, H = size
    if (W, H) == (4 * w0, 4 * h0):
        return y
    if W <= 4 * w0:
        return cv2.resize(y, (W, H), interpolation=cv2.INTER_AREA)
    return lanczos(np.clip(y + 0.5, 0, 255).astype(np.uint8), size, tile)


def hybrid(sr, cl, up, tile):
    """The model's upscale with id's colors below 2 source texels, and its detail where it keeps id's;
    the halo-free Lanczos upscale's (id's own grain) where the model smeared it flat. -> (color, keep share)"""
    P = int(6 * up) + 8 if tile else 0
    if P:
        sr, cl = wpad(sr, P), wpad(cl, P)
    sr = sr + blur(cl, 2 * up) - blur(sr, 2 * up)
    s = 0.75 * up
    base = blur(cl, s)
    d_sr, d_cl = sr - blur(sr, s), cl - base
    energy = lambda d: blur((d * d).mean(axis=2), 2 * up)
    e_sr, e_cl = energy(d_sr), energy(d_cl)
    keep = smooth(e_sr / (e_cl + 4.0), 0.3, 0.7)[..., None]
    d_sr = d_sr * np.minimum(1.0, np.sqrt(CAP * (e_cl + 4.0) / (e_sr + 1e-3)))[..., None]
    y = base + keep * (GAN * d_sr + (1 - GAN) * d_cl) + (1 - keep) * d_cl
    if P:
        y, keep, sr = y[P:-P, P:-P], keep[P:-P, P:-P], sr[P:-P, P:-P]
    return y, float((keep > 0.5).mean()), sr


def keep_black(y, cl, up):
    """Additive and glow layers: where id's image is black, it stays black (it adds nothing)."""
    m = smooth(blur(cl.mean(axis=2), max(1.0, up)), 2, 10)[..., None]
    return y * m + cl * (1 - m)


# ------------------------------------------------------------------ the gentle maps

CLASSES = ["bronze", "steel", "heat", "fiber", "skin"]
# make_pbr.py's (roughness, metal) per role and class
PARAMS = {
    "body": dict(bronze=(0.70, 0.45), steel=(0.62, 0.50), heat=(0.78, 0.0), fiber=(0.88, 0.0), skin=(0.66, 0.0)),
    "head": dict(bronze=(0.70, 0.0), steel=(0.64, 0.45), heat=(0.78, 0.0), fiber=(0.88, 0.0), skin=(0.66, 0.0)),
    "metal": dict(bronze=(0.62, 0.55), steel=(0.58, 0.60), heat=(0.70, 0.15), fiber=(0.80, 0.10), skin=(0.72, 0.0)),
    "light": dict(bronze=(0.60, 0.45), steel=(0.58, 0.55), heat=(0.70, 0.10), fiber=(0.55, 0.05), skin=(0.70, 0.0)),
    "stone": dict(bronze=(0.80, 0.0), steel=(0.86, 0.0), heat=(0.85, 0.0), fiber=(0.82, 0.0), skin=(0.85, 0.0)),
    "organic": dict(bronze=(0.66, 0.0), steel=(0.75, 0.0), heat=(0.62, 0.0), fiber=(0.80, 0.0), skin=(0.66, 0.0)),
    "liquid": dict(bronze=(0.50, 0.0), steel=(0.55, 0.0), heat=(0.60, 0.0), fiber=(0.55, 0.0), skin=(0.55, 0.0)),
}


def masks_from(color, role):
    """make_pbr.py's soft material classes from color, without its noise."""
    f = color.astype(np.float32) / 255.0
    b, g, r = f[..., 0], f[..., 1], f[..., 2]
    hsv = cv2.cvtColor(color, cv2.COLOR_BGR2HSV).astype(np.float32)
    hue, sat = hsv[..., 0] * 2, hsv[..., 1] / 255
    L = 0.2126 * r + 0.7152 * g + 0.0722 * b
    red_hue = np.maximum(1 - smooth(hue, 10, 16), smooth(hue, 340, 350))
    heat = smooth(r - np.maximum(g, b), 0.12, 0.28) * smooth(sat, 0.45, 0.7) * red_hue
    dark = (1 - smooth(L, 0.10, 0.24)) * (1 - heat)
    fiber = smooth(L, 0.45, 0.6) * (1 - smooth(sat, 0.2, 0.38)) * (1 - heat) * (1 - dark)
    rest = np.clip(1 - heat - dark - fiber, 0, 1)
    if role == "head":       # the face's warm tones are skin (make_pbr.py without a face band)
        warm = smooth(hue, 8, 18) * (1 - smooth(hue, 48, 60))
        skin, bronze = rest * warm, rest * (1 - warm)
    else:
        skin, bronze = np.zeros_like(L), rest
    m = dict(bronze=bronze, steel=dark, heat=heat, fiber=fiber, skin=skin)
    m = {k: blur(v.astype(np.float32), 1.5) for k, v in m.items()}
    tot = sum(m.values()) + 1e-6
    return {k: v / tot for k, v in m.items()}, L


def gentle_maps(color, role, up, tile):
    """color (BGR uint8, at its final size) -> normal (xyz), orm (AO, roughness, metal)."""
    P = int(16 * up) if tile else 0
    c = wpad(color, P) if P else color
    m, L = masks_from(c, role)
    Ls = blur(L, 0.5 * up)
    height = Ls - blur(Ls, 4.0 * up)
    strength = 1.8 * up             # a 0.2 brightness step across one source texel tilts about 20 degrees
    gx = cv2.Sobel(height, cv2.CV_32F, 1, 0, ksize=3) / 8.0 * strength
    gy = cv2.Sobel(height, cv2.CV_32F, 0, 1, ksize=3) / 8.0 * strength
    keep = smooth(np.sqrt(gx * gx + gy * gy), 0.07, 0.2)
    n = np.dstack([-gx * keep, gy * keep, np.ones_like(gx)])
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    lap = cv2.Laplacian(blur(height, max(1.0, up)), cv2.CV_32F, ksize=5)
    lap /= np.percentile(np.abs(lap), 99) + 1e-6
    cavity = smooth(lap, 0.1, 0.5)
    pr = PARAMS[role]
    rough = sum(m[k] * pr[k][0] for k in CLASSES) + 0.1 * cavity
    metal = sum(m[k] * pr[k][1] for k in CLASSES)
    orm = np.dstack([1 - 0.3 * cavity, np.clip(rough, ROUGH_MIN, 0.98), np.clip(metal, 0, 1)])
    if P:
        n, orm = n[P:-P, P:-P], orm[P:-P, P:-P]
    return n, orm


# ------------------------------------------------------------------ md3 UVs (atlas islands)

def md3_surfaces(data):
    """-> [(surface name, [shader names], st Nx2, triangles Mx3)]"""
    ns, osf = struct.unpack_from("<i", data, 84)[0], struct.unpack_from("<i", data, 100)[0]
    out, o = [], osf
    for _ in range(ns):
        name = data[o + 4:o + 68].split(b"\0")[0].decode("latin-1").lower()
        _, _, nsh, nv, nt, otri, osh, ost, _, oend = struct.unpack_from("<10i", data, o + 68)
        shaders = [data[o + osh + i * 68:o + osh + i * 68 + 64].split(b"\0")[0].decode("latin-1").lower() for i in range(nsh)]
        st = np.frombuffer(data, "<f4", nv * 2, o + ost).reshape(nv, 2)
        tri = np.frombuffer(data, "<i4", nt * 3, o + otri).reshape(nt, 3)
        out.append((name, shaders, st, tri))
        o += oend
    return out


IMAGE_EXTS = (".tga", ".jpg", ".jpeg", ".png")


def norm(path):
    """An image path without its extension, as the engine names images; a name like cigar.glow keeps its dot."""
    p = path.lower().replace("\\", "/")
    return os.path.splitext(p)[0] if p.endswith(IMAGE_EXTS) else p


class Islands:
    """Which md3 triangles use each image: fighters through their .skin files, props through their surfaces'
    shaders (a script's stages, or the shader's own image)."""
    def __init__(self, paks, scripts):
        self.paks, self.scripts, self.uses = paks, scripts, {}
        for f in sorted(paks.files):
            if not f.endswith(".md3") or not f.startswith("models/") or f.startswith(("models/mapobjects/", "models/weaphits/")):
                continue
            surfs = md3_surfaces(paks.read(f))
            names = {}
            if f.startswith("models/players/"):
                part = os.path.splitext(os.path.basename(f))[0]
                for skin in [s for s in paks.files if s.startswith(os.path.dirname(f) + "/" + part + "_") and s.endswith(".skin")]:
                    for surf, img in make_fighters.read_skin(paks.read(skin).decode("latin-1")):
                        names.setdefault(surf, set()).add(img)
            for name, shaders, st, tri in surfs:
                for sh in names.get(name, set()) | set(shaders):
                    for img in self.images(sh):
                        self.uses.setdefault(img, []).append((st, tri))

    def images(self, shader):
        sh = self.scripts.get(norm(shader))
        if sh is None:
            return [norm(shader)] if shader else []
        return [norm(s.map) for s in sh.stages if s.map and not s.map.startswith("$")]

    def mask(self, image, w, h):
        """id's image's texels some triangle covers (UVs clipped, as the Godot build's cage), or None."""
        uses = self.uses.get(norm(image))
        if not uses:
            return None
        S = 4                                        # drawn at 4x, then any covered subtexel counts
        m = np.zeros((h * S, w * S), np.uint8)
        for st, tri in uses:
            pts = np.clip(st, 0, 1) * [w * S, h * S] * 16
            for t in tri:
                cv2.fillConvexPoly(m, pts[t].astype(np.int32), 255, cv2.LINE_8, 4)
        m = cv2.resize(m, (w, h), interpolation=cv2.INTER_AREA) > 0
        return cv2.dilate(m.astype(np.uint8), np.ones((3, 3), np.uint8)) > 0


# ------------------------------------------------------------------ the inventory

ROLE = {"_albedo": "paint", "__glow_emissive": "glow", "__additive_albedo": "add", "__sky": "sky"}


def lib_kind(lib):
    if isinstance(lib, make_pk3s.Library):
        return "world"
    if isinstance(lib, make_fighters.CharLib):
        return "characters"
    return "props"


def target_name(kind, lib, image, suffix):
    """The file the Godot library names for this lookup, relative to its folder, as .png; or one named as the
    tool's library class would find it."""
    root = os.path.join(GODOT, kind)
    hit = lib.get(image, suffix)
    if hit:
        return os.path.splitext(os.path.relpath(hit, root).replace("\\", "/"))[0] + ".png"
    p = norm(image)
    if kind == "world":
        return "textures/%s%s.png" % (make_pk3s.Library.stem(image), suffix)
    if kind == "characters":     # CharLib finds only models/players/<m>/<image>: anything else stays id's
        parts = p.split("/")
        if len(parts) != 4 or parts[:2] != ["models", "players"]:
            return None
        return "%s_%s_%s.png" % (parts[2], parts[3], make_fighters.CharLib.KIND[suffix])
    if not p.startswith("models/"):
        return None
    # props: a stand-in oz.glb in the folder gives the name its prefix (make_textures writes it)
    return "%s/oz_%s%s.png" % (os.path.dirname(p)[len("models/"):], os.path.basename(p), make_props.PropLib.SUFFIX[suffix])


def record(inv, kind, lib, image, suffix, pbr=False, alpha=False):
    p = norm(image)
    role = ROLE[suffix]
    if target_name(kind, lib, image, "_albedo" if role == "paint" else suffix) is None:
        return
    key = "%s|%s|%s" % (kind, p, "paint" if role == "paint" else role)
    e = inv.setdefault(key, dict(kind=kind, image=p, role=role, pbr=False, alpha=False, files={}))
    e["pbr"] |= bool(pbr)
    e["alpha"] |= bool(alpha)
    if role == "paint":
        e["files"]["albedo"] = target_name(kind, lib, image, "_albedo")
        if e["pbr"]:
            e["files"]["normal"] = target_name(kind, lib, image, "_normal")
            e["files"]["orm"] = target_name(kind, lib, image, "_orm")
        godot = lib.get(image, "_albedo")
    else:
        e["files"]["color"] = target_name(kind, lib, image, suffix)
        godot = lib.get(image, suffix)
    if godot:
        e["godot"] = godot


def inventory(paks, scripts):
    inv = {}
    real_tile, real_layer = make_pk3s.tile, make_pk3s.layer

    def rec_tile(image, lib, pbr, alpha=False):
        record(inv, lib_kind(lib), lib, image, "_albedo", pbr, alpha)
        base = norm(image)
        return base + ".dds", (base + "_n.dds" if pbr else None), (base + "_s.dds" if pbr else None)

    def rec_layer(image, lib, suffix, kind_suffix):
        if kind_suffix in ROLE and (lib_kind(lib) != "props" or kind_suffix in make_props.PropLib.SUFFIX):
            record(inv, lib_kind(lib), lib, image, kind_suffix)
            return norm(image) + suffix + ".dds"
        return None

    make_pk3s.tile, make_pk3s.layer = rec_tile, rec_layer
    try:
        print("world: make_pk3s.py's discovery", flush=True)
        make_pk3s.a.report = True
        make_pk3s.a.library = os.path.join(GODOT, "world")
        make_pk3s.main()
        print("props: make_props.py's discovery", flush=True)
        make_props.a.report = True
        make_props.a.library = os.path.join(GODOT, "props")
        make_props.main()
        print("characters: make_fighters.py's discovery (without the rigs)", flush=True)
        lib = make_fighters.CharLib(os.path.join(GODOT, "characters"))
        models = sorted(f[:-4] for f in os.listdir(os.path.join(GODOT, "characters")) if f.endswith(".glb"))
        images = set()
        for m in models:
            for f in sorted(paks.files):
                if f.startswith("models/players/%s/" % m) and f.endswith(".skin"):
                    images.update(t for _, t in make_fighters.read_skin(paks.read(f).decode("latin-1")) if t)
            for mh in ("lower", "upper", "head"):
                images.update(sh for _, sh in make_fighters.read_md3(paks.read("models/players/%s/%s.md3" % (m, mh)))[2] if sh)
        shaders = {os.path.splitext(i)[0] for i in images}
        shaders |= {n for n in scripts if any(n.startswith("models/players/%s/" % m) for m in models)}
        for name in sorted(shaders):
            sh = scripts.get(name)
            if sh is not None:
                make_pk3s.rewrite(sh, lib, False)
            elif paks.image(name):
                make_pk3s.tile(name, lib, True)
    finally:
        make_pk3s.tile, make_pk3s.layer = real_tile, real_layer
    # the effects: godot/library/fx/fx.json's flipbooks at make_fx_pk3.py's id frame paths
    fx = json.load(open(os.path.join(GODOT, "fx", "fx.json")))
    for name, frames in make_fx_pk3.FRAMES.items():
        t = fx["textures"].get(name)
        if t:
            inv["fx|%s" % name] = dict(kind="fx", image=name, role="fx", frames=[norm(f) for f in frames],
                                       mode=t["mode"], frame_size=t["frame_size"], files={"color": t["file"]})
    # parts (fighters: a head's warm paint is skin) and the world's material groups
    groups = json.load(open(os.path.join(GODOT, "world", "world_materials.json")))["materials"]
    for e in inv.values():
        if e["kind"] == "world":
            g = groups.get(e["image"], {}).get("group")
            if not g:      # an image named by another shader: the group most of its folder has
                folder = os.path.dirname(e["image"]) + "/"
                gs = [v.get("group") for k, v in groups.items() if k.startswith(folder) and v.get("group")]
                g = max(set(gs), key=gs.count) if gs else "metal"
            e["group"] = g
        elif e["kind"] == "characters":
            p = e["image"].split("/")
            m = p[2] if len(p) > 2 else ""
            head = make_fighters.read_md3(paks.read("models/players/%s/head.md3" % m))[2] if m and \
                "models/players/%s/head.md3" % m in paks.files else []
            heads = {norm(s) for _, s in head}
            for f in paks.files:
                if f.startswith("models/players/%s/head_" % m) and f.endswith(".skin"):
                    heads |= {norm(t) for _, t in make_fighters.read_skin(paks.read(f).decode("latin-1"))}
            e["group"] = "head" if e["image"] in heads else "body"
        elif e["kind"] == "props":
            e["group"] = "body"
    return inv


# ------------------------------------------------------------------ making one image

def id_file(paks, image):
    """id's file for an image name (Paks.image would take cigar.glow for cigar)."""
    found = next((image + x for x in (".tga", ".jpg", ".png") if image + x in paks.files), None)
    return found or (paks.image(image) if image.endswith(IMAGE_EXTS) else None)


def load_id(paks, image):
    found = id_file(paks, image)
    if not found:
        return None, None
    im = Image.open(io.BytesIO(paks.read(found)))
    rgba = np.asarray(im.convert("RGBA"))
    return found, rgba


def target_size(e, w0, h0):
    """What the pak ships today (the Godot library's size, capped at 1024), or 4x for images new to it."""
    if e.get("godot"):
        W, H = Image.open(e["godot"]).size
    else:
        W, H = w0 * 4, h0 * 4
    k = min(1.0, MAX / max(W, H))
    W, H = max(4, int(W * k)), max(4, int(H * k))
    return W, H


def color_of(bgr, size, tile, method):
    """-> (color float32 BGR, the halo-free Lanczos upscale, raw model upscale or None, model detail kept share)"""
    h0, w0 = bgr.shape[:2]
    up = max(size[0] / w0, size[1] / h0)
    cl = lanczos(bgr, size, tile)
    if method == "lanczos" or up <= 1.0:
        return cl, cl, None, 0.0
    sr = model_up(bgr, size, tile)
    y, kept, sr = hybrid(sr, cl, up, tile)
    return y, cl, sr, kept


def checks(y, cl, sr, src, up, tile, alpha=None, alpha_cl=None, n=None, orm=None, ref=None, vis=None):
    """The automatic checks, against id's image upscaled by the halo-free Lanczos (cl) at the same size. Brightness
    and color are measured against id's own image (ref, before any gutter or hole was filled), over the texels the
    game shows (vis)."""
    L = lambda x: x.mean(axis=2)
    s = 0.75 * up
    hf = lambda x: L(x) - blur(L(x), s)
    band = lambda x: blur(L(x), s) - blur(L(x), 2 * up)
    e = lambda x: float(np.sqrt((x * x).mean()))
    out = {"detail": round(e(hf(y)) / (e(hf(cl)) + 1e-3), 2)}
    if sr is not None:
        out["model_detail"] = round(e(hf(sr)) / (e(hf(cl)) + 1e-3), 2)
        bs, bc = band(sr), band(cl)
        out["model_band_corr"] = round(float((bs * bc).sum() / (np.sqrt((bs * bs).sum() * (bc * bc).sum()) + 1e-6)), 3)
    k = np.ones((3, 3), np.uint8)
    H, W = y.shape[:2]
    lo = cv2.resize(cv2.erode(src, k), (W, H), interpolation=cv2.INTER_LINEAR).astype(np.float32)
    hi = cv2.resize(cv2.dilate(src, k), (W, H), interpolation=cv2.INTER_LINEAR).astype(np.float32)
    over = np.maximum(y - hi, lo - y).max(axis=2)
    out["halo"] = round(float((over > 12).mean()) * 100, 2)                    # % of pixels past id's range
    flat = blur(hf(cl) ** 2, 2 * up) < 4.0                                     # where id's paint is flat
    out["flat_noise"] = round(e(hf(y)[flat]) if flat.any() else 0.0, 2)
    ref = cl if ref is None else ref
    v = np.ones(y.shape[:2], bool) if vis is None else cv2.resize(vis.astype(np.uint8), (W, H), interpolation=cv2.INTER_NEAREST) > 0
    if not v.any():
        v[:] = True
    my, mc = float(L(y)[v].mean()), float(L(ref)[v].mean())
    out["brightness"] = round((my - mc) / max(mc, 16.0) * 100, 2)             # % against id's (near-black glows: of 16)
    out["color"] = round(float(np.abs(blur(y, 2 * up) - blur(ref, 2 * up))[v].mean()), 2)
    if tile:      # the wrapped edges' step against any texel step (1: seamless), the new map's and id's
        def seam(x):
            inner = float(np.abs(np.diff(x, axis=1)).mean() + np.abs(np.diff(x, axis=0)).mean()) / 2
            return float(np.abs(x[:, 0] - x[:, -1]).mean() + np.abs(x[0] - x[-1]).mean()) / 2 / (inner + 1e-3)
        out["seam"], out["seam_id"] = round(seam(y), 2), round(seam(cl), 2)
        out["seam_vs_id"] = round(out["seam"] / max(1.0, out["seam_id"]), 2)    # many of id's images don't tile
    if alpha is not None:      # the mean change; the cut-out's overlap only where alpha is one (mostly 0 or 255)
        out["alpha_diff"] = round(float(np.abs(alpha - alpha_cl).mean()), 2)
        if ((alpha_cl < 16) | (alpha_cl > 239)).mean() > 0.9:
            a, b = alpha > 127, alpha_cl > 127
            out["alpha_iou"] = round(float((a & b).sum() / max(1, (a | b).sum())), 3)
    if n is not None:
        tilt = np.degrees(np.arccos(np.clip(n[..., 2], -1, 1)))
        out["normal_flat"] = round(float((tilt < 2).mean()) * 100, 1)
        out["normal_p99"] = round(float(np.percentile(tilt, 99)), 1)
    if orm is not None:
        out["rough_min"] = round(float(orm[..., 1].min()), 3)
    return out


# (check, test, what it means). The model's own measures (model_detail, model_band_corr) are recorded but
# flag nothing: what it drew past id's detail is capped (CAP), and below 2 source texels the shapes are id's.
# The final map's detail: the first full pass (no cap) went to 2.6 where it restyled; with the cap 99% of the
# images stay under 1.31.
FLAGS = [
    ("detail", lambda v: v > 1.5, "much more detail than id painted: restyled?", True),
    ("halo", lambda v: v > 4.0, "halos past id's range", False),
    ("brightness", lambda v: abs(v) > 3.0, "brightness off id's", False),
    ("seam_vs_id", lambda v: v > 1.5, "a seam where the tile wraps that id's image hasn't", False),
    ("alpha_iou", lambda v: v < 0.97, "the cut-out moved", False),
    ("alpha_diff", lambda v: v > 10, "alpha changed", False),
    ("rough_min", lambda v: v < ROUGH_MIN - 1e-3, "glossier than the matte rule", False),
]


def flags(c, kept=1.0):
    return [why for k, test, why, model in FLAGS if k in c and test(c[k]) and (not model or kept > 0.2)]


def write_png(path, arr):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    cv2.imwrite(path + ".part.png", arr)
    os.replace(path + ".part.png", path)


def q8(x):
    return np.clip(x + 0.5, 0, 255).astype(np.uint8)


def prepare(e, paks, islands):
    """id's image for an entry, ready for the upscale: -> dict, or None when id's paks have no such image
    (a script naming an image that never shipped: the grapple's, the CTF flags' b_flag)."""
    found, rgba = load_id(paks, e["image"])
    if rgba is None:
        return None
    h0, w0 = rgba.shape[:2]
    W, H = target_size(e, w0, h0)
    tile = e["kind"] == "world"
    bgr = np.ascontiguousarray(rgba[..., 2::-1])
    alpha = rgba[..., 3]
    # id's alpha kept wherever it holds anything, whether or not the stage reads it (the Godot library kept it
    # too); an empty channel (q3dm1's skull doors: all zero, never read) is dropped
    has_alpha = bool(alpha.min() < 255 and alpha.max() > 0)
    raw, vis = bgr.copy(), np.ones((h0, w0), bool)      # vis: the texels the game shows, for the checks
    if not tile:      # atlas gutters filled from the islands' edges, so the model and mip levels see no black
        m = islands.mask(e["image"], w0, h0)
        if m is not None and not m.all():
            bgr = cv2.inpaint(bgr, (~m).astype(np.uint8) * 255, 4, cv2.INPAINT_TELEA)
            vis &= m
    # no dark fringes from the color under a cut-out's holes: only where the stage reads the image's alpha.
    # metalfloor_wall_14_specular's alpha is nearly all zero, and its stage draws the color everywhere (its alpha
    # comes from alphaGen): filled from its few opaque texels, q3dm16's floor came out half again as bright.
    if e.get("alpha") and has_alpha and (alpha < 8).any() and (alpha >= 8).any():
        bgr = cv2.inpaint(bgr, (alpha < 8).astype(np.uint8) * 255, 4, cv2.INPAINT_TELEA)
        vis &= alpha >= 8
    return dict(found=found, w0=w0, h0=h0, W=W, H=H, up=max(W / w0, H / h0), tile=tile, bgr=bgr, alpha=alpha,
                has_alpha=has_alpha, raw=raw, vis=vis)


SKIPPED = "id's paks have no such image: the tools keep id's stage as it is"


def make_entry(e, paks, islands, overrides):
    """One inventory entry -> its files; -> the report's record."""
    out = os.path.join(a.out, e["kind"])
    method = overrides.get(e["image"], {}).get("method", "hybrid")
    if e["role"] == "fx":
        return make_fx(e, paks, method)
    s = prepare(e, paks, islands)
    if s is None:
        return dict(skipped=SKIPPED)
    w0, h0, W, H, up, tile, bgr, alpha = (s[k] for k in ("w0", "h0", "W", "H", "up", "tile", "bgr", "alpha"))
    found, has_alpha = s["found"], s["has_alpha"]
    if max(w0, h0) <= TINY:
        method = "lanczos"
    y, cl, sr, kept = color_of(bgr, (W, H), tile, method)
    if e["role"] in ("glow", "add", "sky"):
        y = keep_black(y, cl, up)
    a_up = a_cl = None
    if has_alpha:
        a_up = lanczos(alpha[..., None], (W, H), tile)[..., 0]
        a_cl = cv2.resize(alpha, (W, H), interpolation=cv2.INTER_LINEAR).astype(np.float32)
    n = orm = None
    if e["role"] == "paint":
        col = q8(y)
        if a_up is not None:
            write_png(os.path.join(out, e["files"]["albedo"]), np.dstack([col, q8(a_up)]))
        else:
            write_png(os.path.join(out, e["files"]["albedo"]), col)
        if e.get("pbr"):
            n, orm = gentle_maps(col, e["group"], up, tile)
            write_png(os.path.join(out, e["files"]["normal"]), q8((n[..., ::-1] * 0.5 + 0.5) * 255))
            write_png(os.path.join(out, e["files"]["orm"]), q8(orm[..., ::-1] * 255))   # file: R=AO G=rough B=metal
    else:
        write_png(os.path.join(out, e["files"]["color"]), q8(y))
    ref = lanczos(s["raw"], (W, H), tile)
    c = checks(y, cl, sr, bgr, up, tile, a_up, a_cl, n, orm, ref, s["vis"])
    return dict(method=method, src=found, src_size=[w0, h0], size=[W, H], model_kept=round(kept, 2), checks=c, flags=flags(c, kept))


def make_fx(e, paks, method):
    """A flipbook: each of id's frames the same way, then make_fx.py's conversion for its mode."""
    fw, fh = e["frame_size"]
    tiles, report, kept_all = [], [], []
    for path in e["frames"]:
        found, rgba = load_id(paks, path)
        if rgba is None:
            return dict(error="no id image %s" % path)
        h0, w0 = rgba.shape[:2]
        bgr = np.ascontiguousarray(rgba[..., 2::-1])
        y, cl, sr, kept = color_of(bgr, (fw, fh), False, "lanczos" if max(w0, h0) <= TINY else method)
        up = max(fw / w0, fh / h0)
        if e["mode"] in ("add", "env"):
            y = keep_black(y, cl, up)
        rgb = np.clip(y[..., ::-1] / 255.0, 0, 1)
        if e["mode"] == "darken":   # dst *= (1 - src): the darkening amount becomes a soot decal's alpha
            amt = rgb.mean(axis=2)
            px = np.dstack([np.full(amt.shape + (3,), 0.04, np.float32) * np.array([1.0, 0.85, 0.75], np.float32), amt])
        elif e["mode"] in ("add", "env"):
            px = np.dstack([rgb, np.ones(rgb.shape[:2], np.float32)])
        else:
            px = np.dstack([rgb, lanczos(rgba[..., 3:4], (fw, fh), False)[..., 0] / 255.0])
        tiles.append(px)
        report.append(checks(y, cl, sr, bgr, up, False))
        kept_all.append(kept)
    atlas = np.concatenate(tiles, axis=1)
    dst = os.path.join(a.out, "fx", e["files"]["color"])
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    Image.fromarray(np.clip(atlas * 255 + 0.5, 0, 255).astype(np.uint8), "RGBA").save(dst)
    c = {k: round(float(np.mean([r[k] for r in report])), 3) for k in report[0] if all(k in r for r in report)}
    kept = float(np.mean(kept_all))
    return dict(method=method, frames=len(tiles), size=[fw, fh], model_kept=round(kept, 2), checks=c, flags=flags(c, kept))


def recheck(e, r, paks, islands):
    """An entry's checks again from its files on disk: the model's own (model_detail, model_band_corr) stay
    as the run measured them."""
    s = prepare(e, paks, islands)
    out = os.path.join(a.out, e["kind"])
    f = os.path.join(out, e["files"].get("albedo") or e["files"]["color"])
    im = cv2.imread(f, cv2.IMREAD_UNCHANGED)
    if s is None or im is None:
        return r
    W, H, up, tile, bgr, alpha = (s[k] for k in ("W", "H", "up", "tile", "bgr", "alpha"))
    y = im[..., :3].astype(np.float32)
    cl = lanczos(bgr, (W, H), tile)
    a_up = a_cl = None
    if im.shape[2] == 4 and s["has_alpha"]:
        a_up = im[..., 3].astype(np.float32)
        a_cl = cv2.resize(alpha, (W, H), interpolation=cv2.INTER_LINEAR).astype(np.float32)
    n = orm = None
    if e["files"].get("normal"):
        n = cv2.imread(os.path.join(out, e["files"]["normal"]))[..., ::-1].astype(np.float32) / 127.5 - 1
        n /= np.linalg.norm(n, axis=2, keepdims=True) + 1e-6
        orm = cv2.imread(os.path.join(out, e["files"]["orm"]))[..., ::-1].astype(np.float32) / 255
    c = checks(y, cl, None, bgr, up, tile, a_up, a_cl, n, orm, lanczos(s["raw"], (W, H), tile), s["vis"])
    c.update({k: v for k, v in r["checks"].items() if k.startswith("model_")})
    return dict(r, checks=c, flags=flags(c, r.get("model_kept", 0.0)))


# ------------------------------------------------------------------ the library around the maps

def stand_ins():
    """What the tools read beside the maps: the props' .glb names (make_props.PropLib keys the maps on them),
    a stand-in oz.glb where a map is named after it, the space panorama (Godot-made, make_pk3s.py's sky box)
    and fx.json."""
    for root, _, files in os.walk(os.path.join(GODOT, "props")):
        rel = os.path.relpath(root, os.path.join(GODOT, "props"))
        for f in files:
            if f.endswith(".glb"):
                d = os.path.join(a.out, "props", rel)
                os.makedirs(d, exist_ok=True)
                open(os.path.join(d, f), "wb").close()
    for f in glob.glob(os.path.join(a.out, "props", "**", "oz_*.png"), recursive=True):
        open(os.path.join(os.path.dirname(f), "oz.glb"), "wb").close()
    sky = os.path.join("skies", "env_space1_rt_pano.png")
    os.makedirs(os.path.join(a.out, "world", "skies"), exist_ok=True)
    shutil.copyfile(os.path.join(GODOT, "world", sky), os.path.join(a.out, "world", sky))
    os.makedirs(os.path.join(a.out, "fx"), exist_ok=True)
    shutil.copyfile(os.path.join(GODOT, "fx", "fx.json"), os.path.join(a.out, "fx", "fx.json"))


# ------------------------------------------------------------------ review sheets

CROP = 256      # each tile: this much of the map at its size in the pak, 1 pixel a texel
ROWS = 5        # images a sheet


def group_of(e):
    p = e["image"].split("/")
    if e["kind"] == "world":
        return "world_" + "_".join(p[1:-1] if p[0] == "textures" else p[:-1])
    if e["kind"] == "characters":
        return "fighters_" + p[2]
    if e["kind"] == "props":
        return "props_" + "_".join(p[1:3])
    return "fx"


def on_checker(rgba):
    """An RGBA map over a grey checker, so cut-outs and soft alpha show."""
    h, w = rgba.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w]
    bg = np.where(((yy // 16 + xx // 16) % 2)[..., None] == 0, 96, 160).astype(np.float32)
    al = rgba[..., 3:4].astype(np.float32) / 255
    return (rgba[..., :3] * al + bg * (1 - al)).astype(np.uint8)


def read_rgb(path, size, checker=True):
    im = cv2.imread(path, cv2.IMREAD_UNCHANGED)
    if im is None:
        return None
    if im.ndim == 2:
        im = cv2.cvtColor(im, cv2.COLOR_GRAY2BGR)
    if im.shape[2] == 4:
        im = on_checker(im) if checker else np.ascontiguousarray(
            (im[..., :3].astype(np.float32) * (im[..., 3:4] / 255.0)).astype(np.uint8))
    if (im.shape[1], im.shape[0]) != tuple(size):
        im = cv2.resize(im, tuple(size), interpolation=cv2.INTER_AREA if im.shape[1] > size[0] else cv2.INTER_LANCZOS4)
    return im


def window(img):
    """The CROP-square of the map with the most detail (where the methods differ most)."""
    h, w = img.shape[:2]
    if w <= CROP and h <= CROP:
        return 0, 0
    L = img.astype(np.float32).mean(axis=2)
    d = blur((L - blur(L, 2)) ** 2, 16)
    best, at = -1, (0, 0)
    for y in range(0, max(1, h - CROP + 1), 32):
        for x in range(0, max(1, w - CROP + 1), 32):
            v = d[y:y + CROP, x:x + CROP].mean()
            if v > best:
                best, at = v, (x, y)
    return at


def sheet_tiles(e, r, paks):
    """[(label, image)] for one entry: id's, the Godot library's, the new map, the halo-free Lanczos alone,
    and the normal maps; crops of the same window."""
    out = os.path.join(a.out, e["kind"])
    if e["role"] == "fx":
        fw, fh = e["frame_size"]
        new = read_rgb(os.path.join(out, e["files"]["color"]), (fw * len(e["frames"]), fh))
        godot = read_rgb(os.path.join(GODOT, "fx", e["files"]["color"]), (fw * len(e["frames"]), fh))
        found, rgba = load_id(paks, e["frames"][0])
        size = (fw, fh)
        new, godot = new[:, :fw], godot[:, :fw] if godot is not None else None
    else:
        found, rgba = load_id(paks, e["image"])
        size = tuple(r["size"])
        new = read_rgb(os.path.join(out, e["files"].get("albedo") or e["files"]["color"]), size)
        godot = read_rgb(e["godot"], size) if e.get("godot") else None
    if rgba is None or new is None:
        return None
    src = on_checker(rgba[..., [2, 1, 0, 3]]) if rgba[..., 3].min() < 255 else np.ascontiguousarray(rgba[..., 2::-1])
    idn = cv2.resize(src, size, interpolation=cv2.INTER_NEAREST)
    lz = q8(lanczos(np.ascontiguousarray(src), size, e["kind"] == "world"))
    path = os.path.join(out, e["files"].get("albedo") or e["files"]["color"])
    x, y = window(new if e["role"] == "fx" else read_rgb(path, size, checker=False))   # the checker isn't detail
    c = lambda im: None if im is None else im[y:y + CROP, x:x + CROP]
    tiles = [("id's original", c(idn)), ("Godot library", c(godot)), ("new (%s)" % r.get("method", "?"), c(new)),
             ("Lanczos alone" + (" (id's colors)" if e["role"] == "fx" else ""), c(lz))]
    if e["files"].get("normal"):
        gn = e["godot"].replace("_albedo.", "_normal.").replace(".png", ".jpg") if e.get("godot") else None
        tiles += [("Godot normal", c(read_rgb(gn, size) if gn and os.path.isfile(gn) else None)),
                  ("new normal", c(read_rgb(os.path.join(out, e["files"]["normal"]), size)))]
    return tiles


def sheets(inv, report, paks):
    from PIL import ImageDraw, ImageFont
    font = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 15)
    bold = ImageFont.truetype("C:/Windows/Fonts/segoeuib.ttf", 16)
    d_out = os.path.join(a.out, "sheets")
    os.makedirs(d_out, exist_ok=True)
    groups = {}
    for k, e in sorted(inv.items()):
        if k in report and report[k].get("key") and not report[k].get("skipped"):
            groups.setdefault(group_of(e), []).append(k)
    pages, flagged = [], []
    for g, keys in sorted(groups.items()):
        for n in range(0, len(keys), ROWS):
            part = keys[n:n + ROWS]
            rows = []
            for k in part:
                t = sheet_tiles(inv[k], report[k], paks)
                if t:
                    rows.append((k, t))
            if not rows:
                continue
            W = 6 * (CROP + 6) + 6
            img = Image.new("RGB", (W, len(rows) * (CROP + 44) + 6), (24, 24, 26))
            dr = ImageDraw.Draw(img)
            for i, (k, tiles) in enumerate(rows):
                y0 = 6 + i * (CROP + 44)
                r = report[k]
                fl = r.get("flags") or []
                dr.text((6, y0), "%s  [%s]  model kept %s%s" % (inv[k]["image"], inv[k]["role"], r.get("model_kept"),
                        ("  FLAGS: " + "; ".join(fl)) if fl else ""), font=bold, fill=(255, 190, 90) if fl else (235, 235, 235))
                for j, (label, im) in enumerate(tiles):
                    x0 = 6 + j * (CROP + 6)
                    dr.text((x0, y0 + 20), label, font=font, fill=(190, 190, 190))
                    if im is not None:
                        img.paste(Image.fromarray(np.ascontiguousarray(im[..., ::-1])), (x0, y0 + 40))
                if fl:
                    flagged.append((k, fl, "%s_%d.png" % (g, n // ROWS + 1)))
            name = "%s_%d.png" % (g, n // ROWS + 1)
            img.save(os.path.join(d_out, name))
            pages.append((g, name, [inv[k]["image"] for k, _ in rows]))
    html = ["<!doctype html><meta charset=utf-8><title>Texture pass</title><style>body{background:#18181a;color:#ddd;"
            "font:14px 'Segoe UI',sans-serif;margin:16px}a{color:#9cf}img{display:block;margin:4px 0 24px}"
            "td{padding:2px 8px;border-bottom:1px solid #333}</style>",
            "<h1>Texture pass: review sheets</h1><p>%d sheets, %d images flagged. Columns: id's original (nearest), the "
            "Godot library's map, the new map, the halo-free Lanczos alone, and the normal maps; 1 pixel a texel of the "
            "map the pak ships.</p>" % (len(pages), len(flagged)),
            "<h2>Flagged</h2><table>" + "".join("<tr><td><a href='#%s'>%s</a></td><td>%s</td></tr>" % (
                s, html_escape(k), html_escape("; ".join(fl))) for k, fl, s in flagged) + "</table>"]
    for g, name, images in pages:
        html.append("<h3 id='%s'>%s</h3><img src='%s' loading=lazy>" % (name, name, name))
    open(os.path.join(d_out, "index.html"), "w", encoding="utf-8").write("\n".join(html))
    print("%d sheets, %d flagged: %s" % (len(pages), len(flagged), os.path.join(d_out, "index.html")))


def html_escape(t):
    return t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


# ------------------------------------------------------------------ main

def cache_key(e, paks, method):
    h = hashlib.sha1(("%d|%s|%s" % (VERSION, method, json.dumps(e, sort_keys=True))).encode())
    for p in e.get("frames") or [e["image"]]:
        f = id_file(paks, p)
        if f:
            h.update(paks.read(f))
    return h.hexdigest()


def main():
    baseq3 = a.baseq3 or find_baseq3()
    paks = Paks(baseq3)
    scripts = {}
    for name in sorted(n for n in paks.files if n.startswith("scripts/") and n.endswith(".shader")):
        for sh in q3shader.parse_text(paks.read(name).decode("latin-1"), os.path.basename(name)):
            scripts.setdefault(sh.name, sh)
    os.makedirs(a.out, exist_ok=True)
    inv_path = os.path.join(a.out, "inventory.json")
    if a.inventory or not os.path.isfile(inv_path):
        inv = inventory(paks, scripts)
        json.dump(inv, open(inv_path, "w"), indent=1, sort_keys=True)
        kinds = {}
        for e in inv.values():
            kinds.setdefault(e["kind"], {}).setdefault(e["role"], 0)
            kinds[e["kind"]][e["role"]] += 1
        print("inventory: %d images: %s" % (len(inv), json.dumps(kinds)))
        print("  not in the Godot library: %d" % sum(1 for e in inv.values() if e["kind"] != "fx" and not e.get("godot")))
        if a.inventory:
            return
    inv = json.load(open(inv_path))
    rep_path = os.path.join(a.out, "report.json")
    report = json.load(open(rep_path)) if os.path.isfile(rep_path) else {}
    if a.sheets:
        sheets({k: e for k, e in inv.items() if (not a.kind or e["kind"] in a.kind)}, report, paks)
        return
    if a.report:
        bad = {k: r for k, r in report.items() if r.get("flags") or r.get("error")}
        for k, r in sorted(bad.items()):
            print("%-70s %s" % (k, "; ".join(r.get("flags") or [r.get("error")])))
        print("%d of %d flagged" % (len(bad), len(report)))
        return
    overrides = json.load(open(OVERRIDES)).get("images", {}) if os.path.isfile(OVERRIDES) else {}
    todo = [k for k, e in sorted(inv.items()) if (not a.kind or e["kind"] in a.kind) and
            (not a.only or e["image"] in [norm(o) for o in a.only])]
    islands = Islands(paks, scripts)
    if a.recheck:
        for k in todo:
            r = report.get(k)
            if r and r.get("checks") and inv[k]["role"] != "fx":
                report[k] = recheck(inv[k], r, paks, islands)
        json.dump(report, open(rep_path, "w"), indent=1, sort_keys=True)
        print("rechecked %d; %d flagged" % (len(todo), sum(1 for k in todo if report.get(k, {}).get("flags"))))
        return
    done, t0 = 0, time.time()
    for k in todo:
        e = inv[k]
        method = overrides.get(e["image"], {}).get("method", "hybrid")
        key = cache_key(e, paks, method)
        if not a.force and report.get(k, {}).get("key") == key:
            continue
        try:
            r = make_entry(e, paks, islands, overrides)
        except Exception as ex:          # one bad image must not sink the pass
            r = dict(error="%s: %s" % (type(ex).__name__, ex))
        r["key"] = key if "error" not in r else None
        report[k] = r
        done += 1
        if done % 25 == 0:
            json.dump(report, open(rep_path, "w"), indent=1, sort_keys=True)
            print("  %d / %d  (%.0f s)" % (done, len(todo), time.time() - t0), flush=True)
    json.dump(report, open(rep_path, "w"), indent=1, sort_keys=True)
    stand_ins()
    bad = [k for k in todo if report[k].get("flags") or report[k].get("error")]
    print("made %d of %d (%d up to date); %d flagged (--report lists them)" % (done, len(todo), len(todo) - done, len(bad)))
    # a library the pak tools may read (make_pk3s.library): every image of its kind made, none failed
    for kind in sorted({e["kind"] for e in inv.values()}):
        keys = [k for k, e in inv.items() if e["kind"] == kind]
        made = [k for k in keys if report.get(k, {}).get("key")]
        failed = [k for k in keys if report.get(k, {}).get("error")]
        marker = os.path.join(a.out, kind, "made.json")
        if len(made) == len(keys):
            json.dump(dict(images=len(keys), version=VERSION, date=time.strftime("%Y-%m-%d")), open(marker, "w"))
        else:
            if os.path.isfile(marker):
                os.remove(marker)
            print("%s: %d of %d made, %d failed%s: the pak tools keep the Godot library" % (
                kind, len(made), len(keys), len(failed), (" (%s)" % ", ".join(failed[:3])) if failed else ""))


if __name__ == "__main__":
    main()
