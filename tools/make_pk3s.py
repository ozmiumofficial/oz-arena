"""The remastered world library -> the oz renderer's pk3 (ENGINE_PLAN.md P1).

python tools/make_pk3s.py [--maps q3dm17 ...] [--jobs 4] [--library build/oz_library/world]
                          [--baseq3 <folder>] [--out build/engine/oz]

Reads id's shader scripts and maps from the player's baseq3 paks and the world texture library
(tools/make_textures.py's build/oz_library/world, made from the player's own id images; the Godot
build's godot/library/world, read only, until that's made), converts what the maps use, and writes
build/engine/oz/pak_oz_world*.pk3:
  <image>.dds            base color, BC7, beside id's image path
  <image>_n.dds          normal map, BC5, green flipped for rend2 (its bitangent runs down the image)
  <image>_s.dds          rend2's PBR specular layout, BC7: R roughness (r_glossType 2, floor 0.45),
                         G metal, B ambient occlusion (the oz renderer's AO patch reads it)
  <image>__glow.dds      a remastered glow layer; __add.dds an additive shader's base; __sky.dds a cloud layer
  scripts/oz_world.ozmtr id's world shaders with only their images swapped and normalMap and
                         specularMap stages added after the painted stage. Stages, blends, deforms,
                         tcMods and sort stay id's. Only the oz renderer reads *.ozmtr (after every
                         other script), so the vulkan and opengl renderers never see them.
  env/oz_space1_*.dds    the space panorama as a cubemap skybox, for the space maps' skies
  cubemaps/<map>/env.json  rend2's cubemap spots: items, spawns and flags; rendered at map load
  maps/<map>/oz_relight.bsp  a map's q3map2 relight (--relight, tools/q3relight.py): its lightmaps,
                         deluxe maps, light grid and vertex light, read only by the oz renderer
Images no shader script owns need no script: the oz renderer prefers a .dds beside an image asked
for without an extension (an implicit shader's) and finds its _n and _s maps itself.

Textures are compressed by the GPU driver (tools/gpu_bc.py, one OpenGL context per worker) into
build/oz_assets/, which is kept between runs: only changed sources are converted again.
"""
import io, os, re, sys, json, glob, math, struct, zipfile, argparse
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import q3shader
import q3relight
from q3paks import find_baseq3, Paks
import gpu_bc

GODOT = r"F:\quake-3-arena-remastered-fan-project"


def library(kind):
    """The texture library the pak tools read: Oz's own, made from the player's id images by
    tools/make_textures.py (build/oz_library/<kind>), or the Godot build's until all of it is made
    (make_textures.py writes made.json last, so a pass stopped halfway never stands in)."""
    own = os.path.join(ROOT, "build", "oz_library", kind)
    return own if os.path.isfile(os.path.join(own, "made.json")) else os.path.join(GODOT, "godot", "library", kind)


STOCK_MAPS = ["q3dm%d" % i for i in range(20)] + ["q3tourney%d" % i for i in range(1, 7)] + \
             ["q3ctf%d" % i for i in range(1, 5)] + ["q3tourney6_ctf", "pro-q3dm6", "pro-q3dm13", "pro-q3tourney2",
                                                     "pro-q3tourney4"]
# the space maps' skies get the remastered space panorama as a cubemap box: id's space farbox
# (q3dm10) and the plain black skies (q3dm17, q3dm19, q3ctf4, the q3tourney6 maps)
SPACE_MAPS = {"q3dm10", "q3dm17", "q3dm19", "q3ctf4", "q3tourney6", "q3tourney6_ctf"}
SPACE_FARBOX = "env/space1"
# id's space skies are black: the panorama's nebula is darkened so they stay that way, and its
# stars (near white) keep their brightness. Its mean luma goes from 0.07 to about 0.01.
SPACE_GAMMA = 2.0
SPACE_PANO = "skies/env_space1_rt_pano.png"
SPACE_BOX = "env/oz_space1"
ROUGHNESS_FLOOR = 0.45    # matte rule (CLAUDE.md)
# The library's metal channel scaled down. rend2 takes a metal's diffuse light away (diffuse *
# (1 - metal)) and gives back only what the cubemap reflects, which is darker than a lit wall's
# own baked light: at full strength the metal-group materials (half the library, metal about
# 0.45) made maps 15-30% darker than id's (look_test.py, q3dm10 with and without specular maps).
METAL_SCALE = 0.35
# Cubemap spots: item and spawn spots closer than CUBEMAP_MERGE units merge, then at most
# CUBEMAP_MAX spread over the map. Each surface takes its nearest cubemap and every cubemap in view
# splits rend2's draw batches: on q3dm17's slowest spot 1 cubemap ran 775 fps, 6 ran 555, 14 ran 475
# and 28 ran 350 (with the engine's cubemap-ordered sort; 230 without). The reflections are blurry
# (roughness 0.45 and up, metal scaled down), so 8 spread out are enough.
CUBEMAP_MERGE = 96.0
CUBEMAP_MAX = 8
MAX_QPATH = 64

ap = argparse.ArgumentParser()
ap.add_argument("--library", default=library("world"))
ap.add_argument("--baseq3", help="the player's baseq3 (default: found in the Steam libraries)")
ap.add_argument("--out", default=os.path.join(ROOT, "build", "engine", "oz"))
ap.add_argument("--work", default=os.path.join(ROOT, "build", "oz_assets"))
ap.add_argument("--maps", nargs="*", help="only what these maps use (default: id's 35)")
ap.add_argument("--jobs", type=int, default=4, help="encoder processes (4 is safe on 16 GB)")
ap.add_argument("--max-size", type=int, default=1024, help="largest texture side")
ap.add_argument("--split-mb", type=int, default=1500, help="start a new pk3 past this size")
ap.add_argument("--report", action="store_true", help="list what would be written, convert nothing")
ap.add_argument("--relight", nargs="*", default=[], metavar="MAP",
                help="relight these maps with q3map2 (tools/q3relight.py); relights made before stay in the pak")
ap.add_argument("--q3map2", help="q3map2.exe (default: build/tools/netradiant-custom-*/app)")
# parsed when run (and in its encoder processes); imported by make_fighters.py, the defaults
a = ap.parse_args() if __name__ in ("__main__", "__mp_main__") else ap.parse_args([])
MAPS = [m.lower() for m in (a.maps or STOCK_MAPS)]
STAGE = os.path.join(a.work, "world")       # pk3 contents, at their paths
# The conversion's version: when it changes, the textures cached in STAGE are made again.
# 2: mip levels and downscales with straight alpha (gpu_bc.resize). 1 filtered premultiplied,
# which made id's textures with an empty alpha channel black past the first mip level.
# 3: the library made from id's originals (tools/make_textures.py) in place of the Godot build's.
ENCODING = 3


# ------------------------------------------------------------------ inputs

def read_bsp(data):
    """-> (shader names, [(shader index, lightmap number)], entity dicts) of a Quake 3 BSP."""
    if data[:4] != b"IBSP":
        raise ValueError("not a Quake 3 BSP")
    lumps = [struct.unpack_from("<ii", data, 8 + 8 * i) for i in range(17)]
    off, ln = lumps[1]
    shaders = [data[off + i * 72:off + i * 72 + 64].split(b"\0")[0].decode("latin-1").lower() for i in range(ln // 72)]
    off, ln = lumps[13]
    surfaces = [struct.unpack_from("<8i", data, off + i * 104) for i in range(ln // 104)]
    off, ln = lumps[0]
    ents = []
    for block in re.findall(r"\{([^{}]*)\}", data[off:off + ln].decode("latin-1")):
        ents.append(dict(re.findall(r'"([^"]*)"\s+"([^"]*)"', block)))
    return shaders, [(s[0], s[7]) for s in surfaces], ents


# ------------------------------------------------------------------ the library

class Library:
    def __init__(self, folder):
        self.folder = folder
        self.tex = {}
        for f in os.listdir(os.path.join(folder, "textures")):
            if f.endswith(".import"):
                continue
            stem, ext = os.path.splitext(f)
            key = stem.lower()
            if key not in self.tex or ext.lower() == ".png":   # an albedo with alpha is the png
                self.tex[key] = os.path.join(folder, "textures", f)

    @staticmethod
    def stem(image):
        """id's image path -> the library's stem (build_world.py's naming)."""
        return os.path.splitext(image.lower())[0].replace("textures/", "", 1).replace("/", "__")

    def get(self, image, suffix):
        return self.tex.get(self.stem(image) + suffix)


# ------------------------------------------------------------------ jobs

jobs = {}        # pk3 path -> (kind, source file, or (albedo, alpha) files)
paks = None      # id's paks (main)
def add_job(path, kind, src):
    if len(path) >= MAX_QPATH:
        print("  skipped (path too long for the engine):", path)
        return False
    jobs[path] = (kind, src)
    return True


def id_alpha(image):
    """id's image's alpha as a PNG in the work folder, if it has holes; else None."""
    found = paks.image(image)
    if not found:
        return None
    alpha = Image.open(io.BytesIO(paks.read(found))).convert("RGBA").getchannel("A")
    if (np.asarray(alpha) < 128).mean() < 0.01:
        return None
    out = os.path.join(a.work, "id_alpha", os.path.splitext(found)[0] + ".png")
    if not os.path.isfile(out):      # kept: a new one makes its albedo stale once
        os.makedirs(os.path.dirname(out), exist_ok=True)
        alpha.save(out)
    return out


def tile(image, lib, pbr, alpha=False):
    """Jobs for one painted image -> its .dds path, or None without a remaster. alpha: the stage
    needs one (alphaFunc or an alpha blend); a remastered albedo without it (pjgrate1 and 2, JPGs
    painted black in the holes) takes id's."""
    base = os.path.splitext(image.lower())[0]
    alb = lib.get(image, "_albedo")
    if alb and alpha and Image.open(alb).mode not in ("RGBA", "LA", "PA"):
        mask = id_alpha(image)
        if mask:
            alb = (alb, mask)
    if not alb or not add_job(base + ".dds", "albedo", alb):
        return None
    if pbr:
        n, orm = lib.get(image, "_normal"), lib.get(image, "_orm")
        if n and orm and add_job(base + "_n.dds", "normal", n) and add_job(base + "_s.dds", "spec", orm):
            return base + ".dds", base + "_n.dds", base + "_s.dds"
    return base + ".dds", None, None


def layer(image, lib, suffix, kind_suffix):
    base = os.path.splitext(image.lower())[0]
    src = lib.get(image, kind_suffix)
    if src and add_job(base + suffix + ".dds", "color", src):
        return base + suffix + ".dds"
    return None


# ------------------------------------------------------------------ shader rewriting

MAP_LINE = re.compile(r"(?im)^(\s*(?:map|clampmap)\s+)(\S+)")
ANIM_LINE = re.compile(r"(?im)^(\s*animmap\s+\S+)([^\n]*)")
SKYPARMS = re.compile(r"(?im)^(\s*skyparms\s+)(\S+)")


def rewrite(sh, lib, space):
    """id's shader -> its .ozmtr text with remastered images, or None if nothing changed."""
    d = sh.describe("")
    if d["kind"] in ("hidden", "fog"):
        return None
    new = {}       # stage index -> (replacement inner text, text to insert after the stage)
    lit = any(s.map == "$lightmap" for s in sh.stages) or any(
        s.rgbgen and s.rgbgen[0] in ("vertex", "exactvertex", "lightingdiffuse") for s in sh.stages)

    def swap(i, fn):
        a_, b_ = sh.stage_spans[i]
        text = sh.text[a_:b_]
        st = sh.stages[i]
        if st.anim:
            frames = [fn(f) for f in st.anim[1]]
            if not all(frames):
                return None
            m = ANIM_LINE.search(text)
            return text[:m.start(2)] + " " + " ".join(frames) + text[m.end(2):] if m else None
        m = MAP_LINE.search(text)
        out = fn(st.map) if m else None
        return text[:m.start(2)] + out + text[m.end(2):] if out else None

    if d["kind"] == "sky":
        edits = []
        if space:
            # the space panorama's box instead of id's farbox; a stage that only paints the sky
            # black would cover it
            m = SKYPARMS.search(sh.text)
            if m:
                edits.append((m.start(2), m.end(2), SPACE_BOX))
            else:
                at = sh.text.index("{") + 1
                edits.append((at, at, "\n\tskyParms %s - -" % SPACE_BOX))
            # A stage that paints the sky black would cover the box. It stays, drawing nothing,
            # because the fog pass needs it: q3tourney5 shares xblacksky_dm7 with q3dm19, in its
            # fog, and without the stage those patches drew black instead of fog.
            for i, st in enumerate(sh.stages):
                if st.map and q3shader._norm(st.map) == "gfx/colors/black":
                    a_, b_ = sh.stage_spans[i]
                    inner = re.sub(r"(?im)^\s*blendfunc[^\n]*\n?", "", sh.text[a_:b_]).rstrip()
                    edits.append((a_, b_, inner + "\n\t\tblendFunc GL_ZERO GL_ONE\n\t"))
        for i, st in enumerate(sh.stages):
            if st.map and not st.map.startswith("$") and not any(e[0] <= sh.stage_spans[i][0] < e[1] for e in edits):
                t = swap(i, lambda img: layer(img, lib, "__sky", "__sky"))
                if t:
                    edits.append(sh.stage_spans[i] + (t,))
        return apply_edits(sh.text, edits) if edits else None

    bi = d["base_index"]
    if bi is not None:
        st = sh.stages[bi]
        if d["blend"] == "add":
            t = swap(bi, lambda img: layer(img, lib, "__add", "__additive_albedo"))
            if t:
                new[bi] = (t, "")
        elif d["blend"] not in ("filter", "darken"):
            # PBR maps only for a painted, lit stage that rend2 collapses into one lightall pass
            pbr = (d["kind"] == "surface" and lit and not sh.deforms and not st.anim and st.tcgen != "environment")
            got = {}

            def base_fn(img):
                r = tile(img, lib, pbr, alpha=bool(st.alphafunc) or st.blend == "blend")
                if r:
                    got["n"], got["s"] = r[1], r[2]
                    return r[0]
                return None
            t = swap(bi, base_fn)
            if t:
                extra = ""
                if got.get("n"):
                    extra = ("\n\t{\n\t\tstage normalMap\n\t\tmap %s\n\t}\n\t{\n\t\tstage specularMap\n\t\tmap %s\n\t}"
                             % (got["n"], got["s"]))
                new[bi] = (t, extra)
    for gi in d["glow_indexes"]:
        t = swap(gi, lambda img: layer(img, lib, "__glow", "__glow_emissive"))
        if t:
            new[gi] = (t, "")
    for ui in (d["under_index"], d["glass_index"]):
        if ui is not None and ui not in new:
            t = swap(ui, lambda img: (tile(img, lib, False) or (None,))[0])
            if t:
                new[ui] = (t, "")
    if not new:
        return None
    edits = []
    for i, (inner, extra) in new.items():
        a_, b_ = sh.stage_spans[i]
        edits.append((a_, b_, inner))
        if extra:
            close = sh.text.index("}", b_) + 1
            edits.append((close, close, extra))
    return apply_edits(sh.text, edits)


def is_space_sky(sh):
    if not ("sky" in sh.parms or sh.skyparms):
        return False
    if sh.skyparms and sh.skyparms[0] == SPACE_FARBOX:
        return True
    layers = [q3shader._norm(st.map) for st in sh.stages if st.map and not st.map.startswith("$")]
    return bool(layers) and all(l == "gfx/colors/black" for l in layers) and not (sh.skyparms and sh.skyparms[0])


def apply_edits(text, edits):
    """(start, end, replacement) edits of non-overlapping spans, applied back to front."""
    for a_, b_, rep in sorted(edits, key=lambda e: (e[0], e[1]), reverse=True):
        text = text[:a_] + rep + text[b_:]
    return text.replace("\r\n", "\n")


# ------------------------------------------------------------------ cubemaps and the space sky

CUBEMAP_CLASSES = ("item_", "weapon_", "ammo_", "holdable_", "info_player_deathmatch", "info_player_start",
                   "team_ctf_redspawn", "team_ctf_bluespawn", "team_ctf_redplayer", "team_ctf_blueplayer",
                   "team_ctf_redflag", "team_ctf_blueflag", "info_player_intermission")


def cubemap_spots(ents, merge=CUBEMAP_MERGE):
    spots = []
    for e in ents:
        cls = e.get("classname", "").lower()
        if not cls.startswith(CUBEMAP_CLASSES) or "origin" not in e:
            continue
        try:
            o = [float(x) for x in e["origin"].split()]
        except ValueError:
            continue
        o[2] += 32.0    # eye height over an item or a spawn
        if all(math.dist(o, s[1]) > merge for s in spots):
            spots.append((cls, o))
    # at most CUBEMAP_MAX, spread over the map (farthest-point picks from the first spot)
    picked = spots[:1]
    while len(picked) < min(CUBEMAP_MAX, len(spots)):
        picked.append(max((s for s in spots if s not in picked),
                          key=lambda s: min(math.dist(s[1], p[1]) for p in picked)))
    return [dict(Name="%s_%d" % (c, i), Position=[round(x, 1) for x in o]) for i, (c, o) in enumerate(picked)]


# tr_sky.c's faces as make_godot_world.py's panorama() reads them: depth axis, s axis, t axis as
# (index, sign) in id's axes. The panorama follows sky_panorama.gdshader (Godot's y up).
FACES = {"rt": ((0, 1), (1, -1), (2, 1)), "lf": ((0, -1), (1, 1), (2, 1)),
         "bk": ((1, 1), (0, 1), (2, 1)), "ft": ((1, -1), (0, -1), (2, 1)),
         "up": ((2, 1), (1, -1), (0, -1)), "dn": ((2, -1), (1, -1), (0, 1))}


def space_faces(pano_path, size):
    """The equirectangular space panorama -> six skybox faces (the inverse of panorama())."""
    import cv2
    pano = np.asarray(Image.open(pano_path).convert("RGB"), dtype=np.float32)
    H, W = pano.shape[:2]
    out = {}
    c = (np.arange(size) + 0.5) / size
    s, t = np.meshgrid(c * 2 - 1, 1 - c * 2)
    for suf, ((di, ds), (si, ss), (ti, ts)) in FACES.items():
        q = [None, None, None]
        q[di] = np.full_like(s, float(ds))
        q[si] = ss * s
        q[ti] = ts * t
        gx, gy, gz = q[1], q[2], q[0]          # id -> Godot: x_g = y_q, y_g = z_q, z_g = x_q
        r = np.sqrt(gx * gx + gy * gy + gz * gz)
        u = np.arctan2(gx, gz) / (2 * math.pi) + 0.5
        v = np.arccos(np.clip(gy / r, -1, 1)) / math.pi
        mx = (u * W - 0.5).astype(np.float32)
        my = (v * H - 0.5).astype(np.float32)
        img = cv2.remap(pano, mx, my, cv2.INTER_LINEAR, borderMode=cv2.BORDER_WRAP)
        img = 255.0 * (np.clip(img, 0, 255) / 255.0) ** SPACE_GAMMA
        out[suf] = Image.fromarray(np.clip(img + 0.5, 0, 255).astype(np.uint8))
    return out


# ------------------------------------------------------------------ conversion

_encoder = None


def load(kind, src):
    """A job's source -> the RGBA image to compress."""
    if kind == "face":
        im = src
    elif isinstance(src, tuple):     # an albedo with id's alpha, scaled up (bilinear: smooth alpha-test edges)
        im = Image.open(src[0]).convert("RGBA")
        im.putalpha(Image.open(src[1]).convert("L").resize(im.size, Image.BILINEAR))
    else:
        im = Image.open(src)
    im = im.convert("RGBA")
    if kind in ("spec", "normal"):
        px = np.asarray(im).copy()
        if kind == "spec":
            orm = px.copy()
            px[..., 0] = np.maximum(orm[..., 1], int(round(ROUGHNESS_FLOOR * 255)))   # roughness
            px[..., 1] = (orm[..., 2] * METAL_SCALE + 0.5).astype(np.uint8)               # metal
            px[..., 2] = orm[..., 0]                                                    # occlusion
            px[..., 3] = 255
        else:
            px[..., 1] = 255 - px[..., 1]    # Godot's green-up normal maps -> rend2's t-down tangent space
        im = Image.fromarray(px, "RGBA")
    w, h = im.size
    scale = min(1.0, a.max_size / max(w, h))
    nw, nh = max(4, int(w * scale) // 4 * 4), max(4, int(h * scale) // 4 * 4)
    if (nw, nh) != (w, h):
        return gpu_bc.resize(np.asarray(im), (nw, nh), Image.LANCZOS)   # straight alpha, as the mips
    return np.asarray(im)


def encode(job):
    """One texture (in a pool worker with its own OpenGL context) -> its pk3 path, or an error."""
    global _encoder
    path, kind, src = job
    try:
        if _encoder is None:
            _encoder = gpu_bc.Encoder()
        dst = os.path.join(STAGE, path)
        _encoder.write_dds(dst + ".part", load(kind, src), "bc5" if kind == "normal" else "bc7")
        os.replace(dst + ".part", dst)
        return path, None
    except Exception as e:
        return path, "%s: %s" % (type(e).__name__, e)


def stale(path):
    dst = os.path.join(STAGE, path)
    if not os.path.isfile(dst):
        return True
    src = jobs[path][1]
    srcs = src if isinstance(src, tuple) else (src,)
    return any(isinstance(s, str) and os.path.getmtime(s) > os.path.getmtime(dst) for s in srcs)


# ------------------------------------------------------------------ main

def main():
    global paks
    baseq3 = a.baseq3 or find_baseq3()
    paks = Paks(baseq3)
    lib = Library(a.library)
    print("baseq3:", baseq3)
    print("library: %s (%d files)" % (a.library, len(lib.tex)))

    # id's scripts: first definition wins, as q3shader.parse_all reads them
    scripts = {}
    for name in sorted(n for n in paks.files if n.startswith("scripts/") and n.endswith(".shader")):
        for sh in q3shader.parse_text(paks.read(name).decode("latin-1"), os.path.basename(name)):
            scripts.setdefault(sh.name, sh)

    used, space_shaders, envs = {}, set(), {}
    for m in MAPS:
        path = "maps/%s.bsp" % m
        if path not in paks.files:
            print("  no map", m)
            continue
        shaders, surfaces, ents = read_bsp(paks.read(path))
        for si, lm in surfaces:
            if 0 <= si < len(shaders):
                used.setdefault(shaders[si], set()).add(m)
        if m in SPACE_MAPS:
            space_shaders.update(n for n in shaders if n in scripts and is_space_sky(scripts[n]))
        envs[m] = cubemap_spots(ents)

    ozmtr, implicit, unchanged = [], 0, 0
    for name in sorted(used):
        sh = scripts.get(name)
        if sh is not None:
            text = rewrite(sh, lib, name in space_shaders)
            if text:
                ozmtr.append(text)
            else:
                unchanged += 1
        elif paks.image(name) and tile(name, lib, True):
            implicit += 1
    if space_shaders:
        faces = space_faces(os.path.join(a.library, SPACE_PANO), a.max_size)
        for suf, img in faces.items():
            add_job("%s_%s.dds" % (SPACE_BOX, suf), "face", img)
    kinds = {}
    for k, _ in jobs.values():
        kinds[k] = kinds.get(k, 0) + 1
    print("%d maps use %d shaders: %d scripts rewritten, %d left as id made them, %d implicit images remastered"
          % (len(envs), len(used), len(ozmtr), unchanged, implicit))
    print("space skies:", ", ".join(sorted(space_shaders)) or "none")
    print("textures: %d (%s)" % (len(jobs), ", ".join("%s %d" % kv for kv in sorted(kinds.items()))))
    if a.report:
        return

    stamp = os.path.join(STAGE, "encoding.txt")
    if not os.path.isfile(stamp) or open(stamp).read().strip() != str(ENCODING):
        old = glob.glob(os.path.join(STAGE, "**", "*.dds"), recursive=True)
        for p in old:
            os.remove(p)
        if old:
            print("conversion changed (encoding %d): %d cached textures dropped" % (ENCODING, len(old)))
        os.makedirs(STAGE, exist_ok=True)
        open(stamp, "w").write("%d\n" % ENCODING)
    todo = [p for p in sorted(jobs) if stale(p)]
    print("converting %d (%d up to date)" % (len(todo), len(jobs) - len(todo)))
    if todo:
        import multiprocessing
        done, failed = 0, []
        with multiprocessing.Pool(a.jobs) as pool:
            for path, err in pool.imap_unordered(encode, [(p,) + jobs[p] for p in todo], chunksize=4):
                done += 1
                if err:
                    failed.append((path, err))
                if done % 100 == 0 or done == len(todo):
                    print("  %d / %d" % (done, len(todo)), flush=True)
        for path, err in failed[:10]:
            print("  failed:", path, err)

    # scripts and cubemap spots
    os.makedirs(os.path.join(STAGE, "scripts"), exist_ok=True)
    open(os.path.join(STAGE, "scripts", "oz_world.ozmtr"), "w", encoding="latin-1", newline="\n").write(
        "// Oz Arena: id's world shaders with the remastered images (tools/make_pk3s.py). Read only by the oz renderer.\n\n"
        + "\n\n".join(ozmtr) + "\n")
    for m, spots in envs.items():
        d = os.path.join(STAGE, "cubemaps", m)
        os.makedirs(d, exist_ok=True)
        json.dump({"Cubemaps": spots}, open(os.path.join(d, "env.json"), "w"), indent=1)

    # relights (optional, per map): kept in STAGE, so a relight made once stays in the pak
    for m in [m.lower() for m in a.relight]:
        q3map2 = a.q3map2 or q3relight.q3map2_path()
        if not q3map2:
            sys.exit("no q3map2 for --relight: pass --q3map2")
        if m in q3relight.saved_scales() and q3relight.saved_scales()[m] is None:
            print("%s keeps id's lightmaps (tools/relight_scales.json)" % m)
            continue
        print("relighting %s (q3map2) ..." % m, flush=True)
        ida = paks.read("maps/%s.bsp" % m)
        try:
            bsp, scales = q3relight.relight(m, ida, baseq3, q3map2, os.path.join(a.work, "relight"))
        except RuntimeError as e:
            print("  not relit: %s" % e, flush=True)
            continue
        os.makedirs(os.path.join(STAGE, "maps", m), exist_ok=True)
        open(os.path.join(STAGE, "maps", m, "oz_relight.bsp"), "wb").write(q3relight.light_only(bsp))
        q3relight.save_scales(m, scales)
        print("  " + q3relight.describe(ida, bsp, scales), flush=True)
    for m, scale in q3relight.saved_scales().items():   # maps kept on id's lightmaps leave the pak
        if scale is None and os.path.isfile(os.path.join(STAGE, "maps", m, "oz_relight.bsp")):
            os.remove(os.path.join(STAGE, "maps", m, "oz_relight.bsp"))
    relights = ["maps/%s/oz_relight.bsp" % m for m in envs if os.path.isfile(os.path.join(STAGE, "maps", m, "oz_relight.bsp"))]
    # A relight staged before they were cut down is id's whole map with new light: only the lumps the
    # renderer reads go in the pak (q3relight.light_only), so those are cut down where they lie.
    cut = 0
    for p in relights:
        staged = os.path.join(STAGE, p)
        data = open(staged, "rb").read()
        if not q3relight.is_light_only(data):
            open(staged, "wb").write(q3relight.light_only(data))
            cut += 1
    if cut:
        print("%d relights staged as whole maps cut down to their light" % cut)
    print("relit maps:", ", ".join(p.split("/")[1] for p in relights) or "none")

    # pack: everything the jobs and scripts name, split into pk3s under --split-mb
    files = sorted(set(p for p in jobs if os.path.isfile(os.path.join(STAGE, p))) |
                   {"scripts/oz_world.ozmtr"} | {"cubemaps/%s/env.json" % m for m in envs} | set(relights))
    os.makedirs(a.out, exist_ok=True)
    for old in glob.glob(os.path.join(a.out, "pak_oz_world*.pk3")):
        os.remove(old)
    part, size, z = 0, 0, None
    for f in files:
        full = os.path.join(STAGE, f)
        if z is None or size > a.split_mb * 2 ** 20:
            if z:
                z.close()
            z = zipfile.ZipFile(os.path.join(a.out, "pak_oz_world%s.pk3" % (part or "")), "w")
            part, size = part + 1, 0
        z.write(full, f, zipfile.ZIP_STORED if f.endswith(".dds") else zipfile.ZIP_DEFLATED)
        size += os.path.getsize(full)
    z.close()
    for p in sorted(glob.glob(os.path.join(a.out, "pak_oz_world*.pk3"))):
        print("  %-28s %6d MB" % (os.path.basename(p), os.path.getsize(p) // 2 ** 20))
    missing = [p for p in jobs if not os.path.isfile(os.path.join(STAGE, p))]
    if missing:
        print("%d textures failed to convert, e.g. %s" % (len(missing), missing[:3]))


if __name__ == "__main__":
    main()
