"""An outside color set swapped into the Oz packs: the Wonder upscales (<id image stem>-wonder-4.png, twice id's size,
Ozmium 2026-11-05) in place of the packs' base color, with the normal maps made again from that color.

python tools/apply_wonder.py <set folder> [--paks <folder with pak_oz_*.pk3>] [--out build/wonder] [--jobs 4]
                             [--sdl <SDL2.dll>] [--baseq3 <folder>] [--max-err 40] [--only <substring> ...]
                             [--report] [--install <the game's oz folder>]

The set is flat: one PNG per id image stem, no folders. Each is matched to id's images by stem (case-insensitive)
and, where several id images share a stem (every fighter has a red.tga), by comparing the PNG scaled down to each
candidate: identical copies (within 1.0 mean error) all take it, else only the closest; a PNG further than
--max-err from every candidate (the set's image is of something else) is left out and listed.

For every DDS in pak_oz_world*, pak_oz_fighters, pak_oz_props and pak_oz_fx whose id image is matched:
  <image>.dds        the set's color, brought to the DDS's size with make_textures' halo-free Lanczos (up) or an
                     area filter (down); id's alpha, scaled the same way, wherever id's image holds one; for
                     fighters and props the atlas gutters filled from the islands' edges first (make_textures.Islands)
  <image>_n.dds      make_textures.gentle_maps on that color (the normals come from the painting's shapes alone)
  <image>__glow.dds, __add.dds, __sky.dds   the set's color with id's black kept black (keep_black)
  pak_oz_fx's        by the stage's blend in oz_fx.ozmtr: additive frames keep black, blended ones take id's alpha,
                     darkening marks (GL_ZERO GL_ONE_MINUS_SRC_COLOR) take the color as it is
The specular maps (_s.dds: roughness, metal, AO), the scripts, rigs, cubemaps and relights are kept as they are.
Writes <out>/stage/<kind>/... (the new DDS, kept between runs: only a newer PNG is converted again) and
<out>/oz/pak_oz_*.pk3, each the original with its textures replaced. --install moves the game's packs aside
(<name>.pk3.orig, kept if already there) and copies the new ones in. Everything made is build output.
"""
import os, io, re, sys, json, glob, time, struct, shutil, zipfile, argparse, collections
import numpy as np
import cv2
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import make_textures as mt
import q3shader
from q3paks import find_baseq3, Paks

SUFFIX = re.compile(r"^(.*?)(__glow|__add|__sky|_n|_s)?\.dds$")
PAKS = ("pak_oz_world*.pk3", "pak_oz_fighters.pk3", "pak_oz_props.pk3", "pak_oz_fx.pk3")
TAG = "-wonder-4.png"


# ------------------------------------------------------------------ the set against id's images

def match_set(folder, paks, max_err):
    """-> ({id image path without extension: png path}, [(png, best error, best candidate)] left out)"""
    bystem = collections.defaultdict(list)
    for n in paks.files:
        if n.endswith((".tga", ".jpg", ".png")):
            bystem[os.path.splitext(os.path.basename(n))[0]].append(n)
    matched, left = {}, []
    for f in sorted(os.listdir(folder)):
        if not f.lower().endswith(".png"):
            continue
        stem = f[:-len(TAG)] if f.endswith(TAG) else os.path.splitext(f)[0]
        cands = bystem.get(stem.lower(), [])
        if not cands:
            left.append((f, None, None))
            continue
        w = Image.open(os.path.join(folder, f)).convert("RGBA")
        scored = []
        for c in cands:
            im = Image.open(io.BytesIO(paks.read(c))).convert("RGBA")
            ia = np.asarray(im, dtype=np.float32)
            wa = np.asarray(w.resize(im.size, Image.LANCZOS), dtype=np.float32)
            a = ia[..., 3:4] / 255.0
            scored.append((float((np.abs(wa[..., :3] - ia[..., :3]) * a).mean() / max(a.mean(), 0.05)), c))
        scored.sort()
        if scored[0][0] > max_err:
            left.append((f, round(scored[0][0], 1), scored[0][1]))
            continue
        for e, c in scored:
            if e - scored[0][0] <= 1.0:
                matched[os.path.splitext(c)[0]] = os.path.join(folder, f)
    return matched, left


def fx_modes(pk3):
    """pak_oz_fx's DDS -> 'add' | 'darken' | 'blend' from the stage naming it in oz_fx.ozmtr."""
    modes = {}
    with zipfile.ZipFile(pk3) as z:
        names = [n for n in z.namelist() if n.endswith(".ozmtr")]
        text = "\n".join(z.read(n).decode("latin-1") for n in names)
    for body in re.findall(r"\{((?:[^{}]|\{[^{}]*\})*)\}", text):
        for st in re.findall(r"\{([^{}]*)\}", body):
            m = re.search(r"(?im)^\s*(?:clampmap|map|animmap\s+\S+)\s+([^\n]*)", st)
            bf = re.search(r"(?im)^\s*blendfunc\s+([^\n]*)", st)
            if not m:
                continue
            b = (bf.group(1).strip().lower() if bf else "opaque")
            mode = "add" if b in ("add", "gl_one gl_one") else "darken" if b == "gl_zero gl_one_minus_src_color" else "blend"
            for d in re.findall(r"\S+\.dds", m.group(1)):
                modes.setdefault(d.lower(), mode)
    return modes


def dds_size(data):
    return struct.unpack_from("<I", data, 16)[0], struct.unpack_from("<I", data, 12)[0]


# ------------------------------------------------------------------ one texture (pool workers)

_paks = _islands = _encoder = None
_baseq3 = _sdl = None


def _init(baseq3, sdl):
    global _baseq3, _sdl
    _baseq3, _sdl = baseq3, sdl


def _ready():
    global _paks, _encoder
    if _paks is None:
        _paks = Paks(_baseq3)
    if _encoder is None:
        import gpu_bc
        _encoder = gpu_bc.Encoder(_sdl)


def _get_islands():
    global _islands
    if _islands is None:
        scripts = {}
        for name in sorted(n for n in _paks.files if n.startswith("scripts/") and n.endswith(".shader")):
            for sh in q3shader.parse_text(_paks.read(name).decode("latin-1"), os.path.basename(name)):
                scripts.setdefault(sh.name, sh)
        _islands = mt.Islands(_paks, scripts)
    return _islands


def convert(job):
    """job: dict(image, png, size, tile, role, albedo, normal, layers, stage) -> (image, [files written], error)"""
    try:
        _ready()
        image, png, (W, H), tile, role = job["image"], job["png"], job["size"], job["tile"], job["role"]
        found, rgba = mt.load_id(_paks, image)
        if rgba is None:
            return image, [], "no id image"
        h0, w0 = rgba.shape[:2]
        up = max(W / w0, H / h0)
        src = cv2.imread(png, cv2.IMREAD_COLOR)
        if src is None:
            return image, [], "unreadable png"
        if not tile:
            m = _get_islands().mask(image, w0, h0)
            if m is not None and not m.all():
                big = cv2.resize(m.astype(np.uint8), (src.shape[1], src.shape[0]), interpolation=cv2.INTER_NEAREST)
                src = cv2.inpaint(src, (1 - big) * 255, 4, cv2.INPAINT_TELEA)
        y = mt.lanczos(src, (W, H), tile)
        id_bgr = np.ascontiguousarray(rgba[..., 2::-1])
        if role in ("glow", "add", "sky"):
            y = mt.keep_black(y, mt.lanczos(id_bgr, (W, H), tile), up)
        col = mt.q8(y)
        written = []
        if job["albedo"]:
            alpha = rgba[..., 3]
            if role == "blend" or role == "paint":
                has_alpha = bool(alpha.min() < 255 and alpha.max() > 0)
                a = mt.q8(mt.lanczos(alpha[..., None], (W, H), tile)[..., 0]) if has_alpha else np.full((H, W), 255, np.uint8)
            else:
                a = np.full((H, W), 255, np.uint8)
            out = os.path.join(job["stage"], job["albedo"])
            _encoder.write_dds(out + ".part", np.dstack([col[..., ::-1], a]), "bc7")
            os.replace(out + ".part", out)
            written.append(job["albedo"])
        if job["normal"]:
            n, _ = mt.gentle_maps(col, "metal", up, tile)
            q = mt.q8((n * 0.5 + 0.5) * 255)                       # x, y, z
            q[..., 1] = 255 - q[..., 1]                            # rend2's t-down tangent space (make_pk3s.load)
            out = os.path.join(job["stage"], job["normal"])
            _encoder.write_dds(out + ".part", np.dstack([q, np.full((H, W), 255, np.uint8)]), "bc5")
            os.replace(out + ".part", out)
            written.append(job["normal"])
        for layer in job["layers"]:
            out = os.path.join(job["stage"], layer)
            _encoder.write_dds(out + ".part", np.dstack([col[..., ::-1], np.full((H, W), 255, np.uint8)]), "bc7")
            os.replace(out + ".part", out)
            written.append(layer)
        return image, written, None
    except Exception as e:
        return job.get("image"), [], "%s: %s" % (type(e).__name__, e)


# ------------------------------------------------------------------ main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("set", help="the folder of PNGs")
    ap.add_argument("--paks", default=os.path.join(ROOT, "build", "engine", "oz"), help="the packs to start from")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "wonder"))
    ap.add_argument("--baseq3", help="the player's baseq3 (default: found in the Steam libraries)")
    ap.add_argument("--sdl", default=os.path.join(ROOT, "build", "engine", "SDL2.dll"), help="SDL2.dll for the GPU encoder")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--max-err", type=float, default=40.0, help="a PNG this far from every id candidate is left out")
    ap.add_argument("--only", nargs="*", help="only images whose path holds one of these")
    ap.add_argument("--force", action="store_true", help="convert every matched image again")
    ap.add_argument("--report", action="store_true", help="list what would change, convert nothing")
    ap.add_argument("--install", metavar="OZ", help="move this game folder's packs aside and copy the new ones in")
    a = ap.parse_args()

    baseq3 = a.baseq3 or find_baseq3()
    paks = Paks(baseq3)
    print("baseq3:", baseq3)
    t0 = time.time()
    matched, left = match_set(a.set, paks, a.max_err)
    print("set: %d images matched to %d id images in %.0f s; %d left out" % (
        len(set(matched.values())), len(matched), time.time() - t0, len(left)))
    for f, e, c in left:
        print("   left out: %-40s %s" % (f, "no id image with that stem" if c is None else "%.0f from %s" % (e, c)))

    pk3s = [p for pat in PAKS for p in sorted(glob.glob(os.path.join(a.paks, pat)))]
    if not pk3s:
        sys.exit("no pak_oz_*.pk3 in " + a.paks)
    jobs, replaced, counts = [], {}, collections.Counter()
    for pk3 in pk3s:
        name = os.path.basename(pk3)
        kind = "world" if "world" in name else "fighters" if "fighters" in name else "props" if "props" in name else "fx"
        modes = fx_modes(pk3) if kind == "fx" else {}
        stage = os.path.join(a.out, "stage", kind)
        groups = {}
        with zipfile.ZipFile(pk3) as z:
            names = {i.filename.lower() for i in z.infolist()}
            for info in z.infolist():
                if not info.filename.endswith(".dds"):
                    continue
                m = SUFFIX.match(info.filename)
                base, suf = m.group(1).lower(), m.group(2) or ""
                # an image named like a map (gothic_block/dark_block_s) is an albedo of its own when the pack has
                # no base image for it, or has maps of its own
                own = info.filename.lower()[:-4]
                if suf in ("_n", "_s") and (base + ".dds" not in names or own + "_n.dds" in names or own + "_s.dds" in names):
                    base, suf = own, ""
                g = groups.setdefault(base, dict(size=None, albedo=None, normal=None, layers=[], files={}))
                g["files"][suf] = info.filename
                if suf == "":
                    g["albedo"] = info.filename
                    g["size"] = dds_size(z.read(info.filename)[:32])
                elif suf == "_n":
                    g["normal"] = info.filename
                elif suf != "_s":
                    g["layers"].append((suf, info.filename))
        for base, g in sorted(groups.items()):
            png = matched.get(base)
            if not png or (a.only and not any(o.lower() in base for o in a.only)):
                counts[name + " kept"] += 1
                continue
            size = g["size"]
            if size is None:      # a layer without an albedo: its own size
                with zipfile.ZipFile(pk3) as z:
                    size = dds_size(z.read(g["layers"][0][1])[:32])
            if kind == "fx":
                role = modes.get(g["albedo"].lower() if g["albedo"] else "", "blend")
                layers, albedo = [], g["albedo"]
            else:
                role = "paint"
                albedo = g["albedo"]
                layers = [f for suf, f in g["layers"]]
            # the glow/add/sky layers of a world image are the same color with black kept: one job per role
            for sub_role, alb, lay, nrm in ([(role, albedo, [], g["normal"])] if kind != "fx" else [(role, albedo, [], None)]) + \
                    [({"__glow": "glow", "__add": "add", "__sky": "sky"}[suf], None, [f], None) for suf, f in g["layers"]]:
                files = ([alb] if alb else []) + ([nrm] if nrm else []) + lay
                if not files:
                    continue
                fresh = all(os.path.isfile(os.path.join(stage, f)) and os.path.getmtime(os.path.join(stage, f)) >= os.path.getmtime(png)
                            for f in files)
                for f in files:
                    replaced[(name, f)] = os.path.join(stage, f)
                if fresh and not a.force:
                    counts[name + " up to date"] += len(files)
                    continue
                jobs.append(dict(image=base, png=png, size=size, tile=(kind == "world"), role=sub_role, albedo=alb,
                                 normal=nrm, layers=lay, stage=stage))
                counts[name + " to convert"] += len(files)
    for k, v in sorted(counts.items()):
        print("  %-36s %d" % (k, v))
    if a.report:
        return
    for job in jobs:
        os.makedirs(os.path.dirname(os.path.join(job["stage"], (job["albedo"] or job["normal"] or job["layers"][0]))), exist_ok=True)

    if jobs:
        import multiprocessing
        done, failed, t0 = 0, [], time.time()
        with multiprocessing.Pool(a.jobs, initializer=_init, initargs=(baseq3, a.sdl)) as pool:
            for image, written, err in pool.imap_unordered(convert, jobs, chunksize=2):
                done += 1
                if err:
                    failed.append((image, err))
                if done % 50 == 0 or done == len(jobs):
                    print("  %d / %d  (%.0f s)" % (done, len(jobs), time.time() - t0), flush=True)
        for image, err in failed[:20]:
            print("  failed:", image, err)
        if failed:
            print("%d failed" % len(failed))

    # the packs: each original with its textures swapped
    os.makedirs(os.path.join(a.out, "oz"), exist_ok=True)
    for pk3 in pk3s:
        name = os.path.basename(pk3)
        dst = os.path.join(a.out, "oz", name)
        n_swapped, n_missing = 0, 0
        with zipfile.ZipFile(pk3) as zin, zipfile.ZipFile(dst + ".tmp", "w") as zout:
            for info in zin.infolist():
                src = replaced.get((name, info.filename))
                if src and os.path.isfile(src):
                    zout.write(src, info.filename, zipfile.ZIP_STORED)
                    n_swapped += 1
                else:
                    if src:
                        n_missing += 1
                    zout.writestr(info, zin.read(info.filename), compress_type=info.compress_type)
        os.replace(dst + ".tmp", dst)
        print("  %-26s %5d MB  %d textures swapped%s" % (name, os.path.getsize(dst) // 2 ** 20, n_swapped,
                                                       (", %d not made" % n_missing) if n_missing else ""))
    if a.install:
        for pk3 in pk3s:
            name = os.path.basename(pk3)
            live = os.path.join(a.install, name)
            if os.path.isfile(live) and not os.path.isfile(live + ".orig"):
                os.replace(live, live + ".orig")
            shutil.copyfile(os.path.join(a.out, "oz", name), live)
        print("installed into", a.install, "(the originals beside them as .pk3.orig)")


if __name__ == "__main__":
    main()
