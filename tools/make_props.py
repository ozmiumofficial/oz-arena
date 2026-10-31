"""Weapons and items: the Godot build's remastered prop textures -> build/engine/oz/pak_oz_props.pk3.

python tools/make_props.py [--report] [--jobs 4]

Reads the props texture library (tools/make_textures.py's build/oz_library/props, made from the player's
own id images, or the Godot build's godot/library/props, read only, until that's made: the maps named as its
GLBs were exported with, <glb>_<image stem>[_add]_<albedo|normal|orm|emissive>.jpg/png, upscales of id's own
images in their UV layout) and id's paks (the md3s and their shader scripts). Writes pak_oz_props.pk3:
  <image>.dds, _n.dds, _s.dds   beside id's image paths, as the world's (tools/make_pk3s.py): base
                                color, normal map and rend2's specular layout (roughness, metal, AO)
  <image>__add.dds              an additive layer (energy, muzzle flashes)
  scripts/oz_props.ozmtr        id's shaders for these models with the remastered images, the normal
                                and specular maps after the painted stage when it's lit
                                (rgbGen lightingDiffuse) and has no deforms. A surface no script names
                                (an md3's implicit shader) gets one written as the engine builds it:
                                one stage lit by the light grid, plus the two maps. An md3 names its
                                images with an extension (rocketl.TGA), and for such a name the engine
                                loads id's image, then finds the _n and _s maps beside it: id's 256 px
                                color under the remastered normal map (imagelist). The fighters' skins
                                and the maps' surfaces ask without one and get the .dds.
The models are every md3 under models/ in id's paks but the fighters' (pak_oz_fighters), the map
objects' (built into the maps: pak_oz_world) and the weapon hits' (effects: pak_oz_fx): the weapons in
the hand and on the floor with their barrels and flashes, armor, health, powerups, holdables, ammo
boxes, rockets and grenades in flight, the CTF flags and gibs. Shaders and images pak_oz_fx owns stay
its; images outside models/ (environment maps under textures/) stay id's, as do environment-mapped
stages. Only the oz renderer reads .ozmtr scripts and DDS, so the other renderers and pure servers
keep id's look; under other mods the pak loads below the mod (FS_AddOzAssets) as the other asset paks.
"""
import os, sys, glob, struct, zipfile, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import make_pk3s, make_fx_pk3, q3shader
from q3paks import find_baseq3, Paks

# the md3s other paks cover: the fighters, the map objects (built into the maps), the weapon hits (effects)
SKIP_PREFIX = ("models/players/", "models/mapobjects/", "models/weaphits/")

ap = argparse.ArgumentParser()
ap.add_argument("--library", default=make_pk3s.library("props"))
ap.add_argument("--baseq3", help="the player's baseq3 (default: found in the Steam libraries)")
ap.add_argument("--out", default=os.path.join(ROOT, "build", "engine", "oz"))
ap.add_argument("--work", default=os.path.join(ROOT, "build", "oz_assets"))
ap.add_argument("--jobs", type=int, default=4, help="encoder processes (4 is safe on 16 GB)")
ap.add_argument("--max-size", type=int, default=1024, help="largest texture side")
ap.add_argument("--report", action="store_true", help="list the shaders and textures, convert none")
# parsed when run (and in its encoder processes); imported by engine/tests/props_test.py, the defaults
a = ap.parse_args() if __name__ in ("__main__", "__mp_main__") else ap.parse_args([])
make_pk3s.a.max_size = a.max_size
make_pk3s.a.work = a.work
STAGE = os.path.join(a.work, "props")
PK3 = "pak_oz_props.pk3"


class PropLib:
    """The Godot build's prop maps, by the folder under models/ and the image's stem: a weapon's GLB
    carries its barrel's and flash's maps too (bfg_barrel_bfg_k and bfg_bfg_k are the same file). An
    additive layer's color is its _add_emissive (its _add_albedo is near black: id's rockflar averages
    112 45 26, the emissive 111 44 26, the albedo 5 5 5), unlike the world library's."""
    SUFFIX = {"_albedo": "_albedo", "_normal": "_normal", "_orm": "_orm", "__additive_albedo": "_add_emissive"}

    def __init__(self, folder):
        self.tex = {}
        for root, _, files in os.walk(folder):
            rel = os.path.relpath(root, folder).replace("\\", "/").lower()
            glbs = sorted((f[:-4].lower() for f in files if f.endswith(".glb")), key=len, reverse=True)
            for f in files:
                stem, ext = os.path.splitext(f)
                if ext.lower() not in (".jpg", ".png"):
                    continue
                g = next((g for g in glbs if stem.lower().startswith(g + "_")), None)
                key = (rel, stem.lower()[len(g) + 1:]) if g else None
                if key and (key not in self.tex or ext.lower() == ".png"):   # an albedo with alpha is the png
                    self.tex[key] = os.path.join(root, f)

    def get(self, image, suffix):
        p = os.path.splitext(image.lower().replace("\\", "/"))[0]
        if not p.startswith("models/") or suffix not in self.SUFFIX:
            return None
        return self.tex.get((os.path.dirname(p)[len("models/"):], os.path.basename(p) + self.SUFFIX[suffix]))


def norm(image):
    return os.path.splitext(image.lower().replace("\\", "/"))[0]


def md3_shaders(data):
    """An md3's surfaces' shader names, as the file names them (make_fighters.read_md3 without the tags)."""
    ns, osf = struct.unpack_from("<i", data, 84)[0], struct.unpack_from("<i", data, 100)[0]
    out, o = [], osf
    for _ in range(ns):
        _, _, nsh, _, _, _, osh, _, _, oend = struct.unpack_from("<10i", data, o + 68)
        if nsh:
            out.append(data[o + osh:o + osh + 64].split(b"\0")[0].decode("latin-1"))
        o += oend
    return out


def maps_after(n, s):
    return "\n\t{\n\t\tstage normalMap\n\t\tmap %s\n\t}\n\t{\n\t\tstage specularMap\n\t\tmap %s\n\t}" % (n, s)


def rewrite(sh, lib, owned):
    """id's shader -> its .ozmtr text with the remastered images, or None if nothing changed."""
    if sh.describe("")["kind"] in ("hidden", "fog"):
        return None
    base = sh.describe("")["base_index"]
    edits = []
    for i, st in enumerate(sh.stages):
        if not st.map or st.anim or st.tcgen == "environment" or not owned(st.map):
            continue
        a_, b_ = sh.stage_spans[i]
        text = sh.text[a_:b_]
        m = make_pk3s.MAP_LINE.search(text)
        if not m:
            continue
        extra = ""
        if st.blend == "add":
            new = make_pk3s.layer(st.map, lib, "__add", "__additive_albedo")
        elif st.blend in ("opaque", "blend", "mask"):
            # the normal and specular maps for the painted stage when the light grid lights it: rend2
            # collapses it into one lightall pass with the first normalMap and specularMap after it
            lit = st.rgbgen and st.rgbgen[0] in ("lightingdiffuse", "vertex", "exactvertex")
            pbr = i == base and lit and not sh.deforms
            r = make_pk3s.tile(st.map, lib, pbr, alpha=bool(st.alphafunc) or st.blend == "blend")
            new = r[0] if r else None
            if r and r[1]:
                extra = maps_after(r[1], r[2])
        else:
            continue
        if new:
            edits.append((a_ + m.start(2), a_ + m.end(2), new))
            if extra:
                close = sh.text.index("}", b_) + 1
                edits.append((close, close, extra))
    return make_pk3s.apply_edits(sh.text, edits) if edits else None


def implicit(name, lib):
    """An md3 surface's image no script names -> the shader the engine would make (one stage lit by
    the light grid, R_FindShader's LIGHTMAP_NONE default) with the remastered maps, or None."""
    r = make_pk3s.tile(name, lib, True)
    if not r:
        return None
    return "%s\n{\n\t{\n\t\tmap %s\n\t\trgbGen lightingDiffuse\n\t}%s\n}" % (
        norm(name), r[0], maps_after(r[1], r[2]) if r[1] else "")


def defined_elsewhere(out):
    """{shader name: Shader} for what Oz's cgame scripts (game/assets, pak_oz_code) and the other Oz paks'
    .ozmtr scripts define (two definitions: only one would win)."""
    found = {}
    for f in glob.glob(os.path.join(ROOT, "game", "assets", "scripts", "*.ozmtr")):
        for sh in q3shader.parse_text(open(f, encoding="latin-1").read(), f):
            found[sh.name] = sh
    for p in glob.glob(os.path.join(out, "pak_oz_*.pk3")):
        if os.path.basename(p) == PK3:
            continue
        with zipfile.ZipFile(p) as z:
            for f in z.namelist():
                if f.endswith(".ozmtr"):
                    for sh in q3shader.parse_text(z.read(f).decode("latin-1"), f):
                        found.setdefault(sh.name, sh)
    return found


_encoder = None


def encode(job):
    global _encoder
    path, kind, src = job
    try:
        if _encoder is None:
            import gpu_bc
            _encoder = gpu_bc.Encoder()
        dst = os.path.join(STAGE, path)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        _encoder.write_dds(dst + ".part", make_pk3s.load(kind, src), "bc5" if kind == "normal" else "bc7")
        os.replace(dst + ".part", dst)
        return path, None
    except Exception as e:
        return path, "%s: %s" % (type(e).__name__, e)


def main():
    baseq3 = a.baseq3 or find_baseq3()
    paks = Paks(baseq3)
    make_pk3s.paks = paks          # id's alpha for remastered albedos without one (make_pk3s.tile)
    lib = PropLib(a.library)
    print("baseq3:", baseq3)
    print("library: %s (%d maps)" % (a.library, len(lib.tex)))

    scripts = {}
    for name in sorted(n for n in paks.files if n.startswith("scripts/") and n.endswith(".shader")):
        for sh in q3shader.parse_text(paks.read(name).decode("latin-1"), os.path.basename(name)):
            scripts.setdefault(sh.name, sh)
    fx_images = {p.lower() for frames in make_fx_pk3.FRAMES.values() for p in frames}
    fx_shaders = set(make_fx_pk3.shaders(paks, fx_images))
    elsewhere = defined_elsewhere(a.out)

    def owned(image):
        p = norm(image)
        return p.startswith("models/") and not p.startswith(SKIP_PREFIX) and p not in fx_images

    models = sorted(f for f in paks.files if f.startswith("models/") and f.endswith(".md3") and not f.startswith(SKIP_PREFIX))
    surfaces = {norm(sh) for f in models for sh in md3_shaders(paks.read(f))}
    names = surfaces | {n for n in scripts if n.startswith("models/") and not n.startswith(SKIP_PREFIX)}

    ozmtr, rewritten, made, theirs, without = [], 0, 0, [], []
    for name in sorted(names):
        sh = scripts.get(name)
        if name in fx_shaders or name in elsewhere:
            # an image Oz's script names without an extension (the rail gun's coil, under its charge-up
            # shader) is the .dds with its _n and _s maps where they're there: made here, the script stays its
            for st in elsewhere[name].stages if name in elsewhere else []:
                if st.map and not os.path.splitext(st.map)[1] and owned(st.map):
                    make_pk3s.tile(st.map, lib, True)
            theirs.append(name)
            continue
        if sh is not None:
            text = rewrite(sh, lib, owned)
            if text:
                ozmtr.append(text)
                rewritten += 1
        elif owned(name) and paks.image(name):
            text = implicit(name, lib)
            if text:
                ozmtr.append(text)
                made += 1
            else:
                without.append(name)
    jobs = make_pk3s.jobs
    kinds = {}
    for k, _ in jobs.values():
        kinds[k] = kinds.get(k, 0) + 1
    print("%d models, %d shaders: %d scripts rewritten, %d written for implicit shaders, %d left to other paks%s, "
          "%d images without a remaster%s" % (
              len(models), len(names), rewritten, made, len(theirs), (" (%s)" % ", ".join(theirs)) if theirs else "",
              len(without), (": " + ", ".join(without)) if without else ""))
    print("textures: %d (%s)" % (len(jobs), ", ".join("%s %d" % kv for kv in sorted(kinds.items()))))
    if a.report:
        for text in ozmtr:
            print(text.split("\n", 1)[0])
        return

    stamp = os.path.join(STAGE, "encoding.txt")
    if not os.path.isfile(stamp) or open(stamp).read().strip() != str(make_pk3s.ENCODING):
        for p in glob.glob(os.path.join(STAGE, "**", "*.dds"), recursive=True):
            os.remove(p)
        os.makedirs(STAGE, exist_ok=True)
        open(stamp, "w").write("%d\n" % make_pk3s.ENCODING)
    todo = [p for p in sorted(jobs) if not os.path.isfile(os.path.join(STAGE, p)) or any(
        isinstance(s, str) and os.path.getmtime(s) > os.path.getmtime(os.path.join(STAGE, p))
        for s in (jobs[p][1] if isinstance(jobs[p][1], tuple) else (jobs[p][1],)))]
    print("converting %d (%d up to date)" % (len(todo), len(jobs) - len(todo)))
    if todo:
        import multiprocessing
        done, failed = 0, []
        with multiprocessing.Pool(a.jobs) as pool:
            for path, e in pool.imap_unordered(encode, [(p,) + jobs[p] for p in todo], chunksize=4):
                done += 1
                if e:
                    failed.append((path, e))
                if done % 50 == 0 or done == len(todo):
                    print("  %d / %d" % (done, len(todo)), flush=True)
        for path, e in failed[:10]:
            print("  failed:", path, e)

    os.makedirs(os.path.join(STAGE, "scripts"), exist_ok=True)
    open(os.path.join(STAGE, "scripts", "oz_props.ozmtr"), "w", encoding="latin-1", newline="\n").write(
        "// Oz Arena: id's weapon and item shaders with the remastered images (tools/make_props.py). Read only by the oz renderer.\n\n"
        + "\n\n".join(ozmtr) + "\n")
    files = sorted(set(p for p in jobs if os.path.isfile(os.path.join(STAGE, p))) | {"scripts/oz_props.ozmtr"})
    os.makedirs(a.out, exist_ok=True)
    pk3 = os.path.join(a.out, PK3)
    with zipfile.ZipFile(pk3 + ".part", "w") as z:
        for f in files:
            z.write(os.path.join(STAGE, f), f, zipfile.ZIP_STORED if f.endswith(".dds") else zipfile.ZIP_DEFLATED)
    os.replace(pk3 + ".part", pk3)
    print("  %-28s %6d MB" % (os.path.basename(pk3), os.path.getsize(pk3) // 2 ** 20))
    missing = [p for p in jobs if not os.path.isfile(os.path.join(STAGE, p))]
    if missing:
        print("%d textures failed to convert, e.g. %s" % (len(missing), missing[:3]))


if __name__ == "__main__":
    main()
