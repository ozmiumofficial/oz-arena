"""q3map2 relight of id's maps from their own light entities (ENGINE_PLAN.md P1 step 2).

python tools/q3relight.py q3dm17 [--colors] [--samplesize 8] [--q3map2 <q3map2.exe>] [--baseq3 <folder>]

id's BSPs keep their light entities, so the map sources aren't needed: the player's BSP gets
"_keepLights" "1" in its worldspawn (q3map2 then lights from the BSP's own lights instead of loading
the .map) and q3map2 -light rebuilds its lightmaps, deluxe maps (light directions, which let the
normal maps answer the baked light), light grid and vertex colors. Nothing else in the BSP changes.

q3map2 lights point and spot lights as id's q3map did (the same formula and scale), but its
surface lights (q3map_surfacelight shaders, the sky's included) and suns come out brighter, by a
different amount on each map (q3dm17 1.1 times in all, q3dm11 1.3), while its vertex light (models)
and light grid come out about half as bright. So each map's surface and sky light is scaled to give
its lightmaps id's average brightness, and its vertex light and grid to give theirs (calibrate()).
The scales are kept in relight_scales.json beside this script; edit them there to set them by hand.
Surface lights whose light image never shipped get their color read off id's lightmaps (--colors,
light_colors()).

The result, maps/<map>/oz_relight.bsp in the world pak (tools/make_pk3s.py --relight), is read
only by the oz renderer, which takes those four lumps from it when its geometry matches the map
being loaded (tr_bsp.c, r_ozRelight). The file holds those four lumps alone (light_only()): the
rest of id's map never ships. Lightmaps stay 128 x 128 pages inside the BSP: rend2 reads
internal pages only, and q3map2's -external rewrites the BSP's shader names to its own scripts.
"""
import os, re, sys, glob, json, math, struct, shutil, argparse, subprocess
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

LUMPS = 17
ENTITIES, VERTS, SURFACES, LIGHTMAPS, LIGHTGRID = 0, 10, 13, 14, 15
LIT = (VERTS, SURFACES, LIGHTMAPS, LIGHTGRID)     # the lumps a relight writes
VERT_SIZE, SURF_SIZE = 44, 104
# q3map2's light options: deluxe maps in world space (rend2's lightall), 2 x 2 supersampling as id's
# -extra, and one bounce of radiosity, which fills in the shadows q3map2 leaves darker than id's
# q3map did (q3dm17's by about 25%). The sample size (8 units a texel, id's are 16) and the map's
# scales are relight()'s.
OPTIONS = ["-deluxe", "-deluxemode", "0", "-super", "2", "-bounce", "1"]
# calibrate()'s quick runs: the bounce, but no deluxe maps or supersampling, at id's sample size
QUICK = ["-bounce", "1"]
# A map's scales, each matched to id's by calibrate(), and the q3map2 options that set them
SCALE_OPTIONS = {"surface": ("-area", "-sky"),          # surface lights and suns: the lightmaps
                 "vertex": ("-vertexscale",),           # vertex-lit surfaces (models)
                 "grid": ("-gridscale",),               # the light grid (items, players)...
                 "gridambient": ("-gridambientscale",)}  # ...and its ambient part, on top of grid
SCALE_RANGE = {"surface": (0.05, 1.5), "vertex": (0.25, 4.0), "grid": (0.25, 4.0), "gridambient": (0.25, 4.0)}


def lumps(data):
    if data[:4] != b"IBSP" or struct.unpack_from("<i", data, 4)[0] != 46:
        raise ValueError("not a Quake 3 BSP")
    return [data[o:o + n] for o, n in (struct.unpack_from("<ii", data, 8 + 8 * i) for i in range(LUMPS))]


def build(parts):
    """Lumps -> a BSP file (version 46), each lump 4-byte aligned."""
    head, body, off = bytearray(b"IBSP" + struct.pack("<i", 46)), bytearray(), 8 + 8 * LUMPS
    for p in parts:
        head += struct.pack("<ii", off + len(body), len(p))
        body += p + b"\0" * (-len(p) % 4)
    return bytes(head + body)


def light_only(data):
    """A relit BSP -> the file the game ships as maps/<map>/oz_relight.bsp: its header and the four lumps
    the oz renderer's R_OzRelight reads (LIT: the vertices and surfaces, which it matches against the
    loaded map's and takes the lightmap places, numbers and vertex light from, the lightmaps and the
    light grid), every other lump empty. It checks nothing else of the file but that each lump lies
    inside it. The whole BSP was id's map with new light, which a console could load and play; this
    one has no entities, planes, nodes, brushes or shaders, so nothing loads it as a map."""
    parts = lumps(data)
    return build([p if i in LIT else b"" for i, p in enumerate(parts)])


def is_light_only(data):
    return not any(p for i, p in enumerate(lumps(data)) if i not in LIT)


def worldspawn(data):
    """-> the BSP's worldspawn keys."""
    block = re.search(r"\{([^{}]*)\}", lumps(data)[ENTITIES].decode("latin-1")).group(1)
    return dict(re.findall(r'"([^"]*)"\s+"([^"]*)"', block))


def ambient_color(data):
    """-> the worldspawn's ambient light as id's q3map took it: "_color" times "ambient", so none
    without a "_color" (q3map2 takes white there)."""
    ws = worldspawn(data)
    try:
        color = [float(c) for c in ws["_color"].split()]
        return [c * float(ws.get("ambient", 0)) for c in color]
    except (KeyError, ValueError):
        return [0.0, 0.0, 0.0]


def keep_lights(data):
    """The BSP with "_keepLights" "1" in its worldspawn, and its lights without styles: id's q3map
    read a light's style but lit it as any other (q3dm6's), where q3map2 makes styled lightmaps
    and shaders of its own for it, which changes the BSP's shader list. A worldspawn ambient
    without a "_color" goes too, as id's q3map lit it (q3map2 would light the grid with it)."""
    parts = lumps(data)
    ents = parts[ENTITIES].rstrip(b"\0").decode("latin-1")
    ents = re.sub(r"\{[^{}]*\}", lambda b: re.sub(r'"_?style"\s+"[^"]*"\s*', "", b.group(0))
                  if re.search(r'"classname"\s+"light"', b.group(0)) else b.group(0), ents)
    if not any(ambient_color(data)):
        ents = re.sub(r"\{[^{}]*\}", lambda b: re.sub(r'"_?ambient"\s+"[^"]*"\s*', "", b.group(0)), ents, count=1)
    at = ents.index("{") + 1
    parts[ENTITIES] = (ents[:at] + '\n"_keepLights" "1"' + ents[at:]).encode("latin-1") + b"\0"
    return build(parts)


def lightmap_axis(n):
    """q3map2's CalcLightmapAxis: a normal's major axis, biased to z."""
    ax, ay, az = (abs(c) for c in n)
    if az > ax - 0.0001 and az > ay - 0.0001:
        return (0, 0, 1 if n[2] > 0 else -1)
    if ax > ay - 0.0001 and ax > az - 0.0001:
        return (1 if n[0] > 0 else -1, 0, 0)
    return (0, 1 if n[1] > 0 else -1, 0)


PATCH_AXES = ((0, 0, -1), (0, 0, 1), (-1, 0, 0), (1, 0, 0), (0, -1, 0), (0, 1, 0))


def curve_length(a, b, c):
    """q3map2's ExpandLongestCurve for one row or column of 3 control points (0 if straight)."""
    def sub(p, q):
        return [p[i] - q[i] for i in range(3)]

    def unit(v):
        n = math.sqrt(sum(x * x for x in v))
        return [x / n for x in v] if n else v, n
    (ab, lab), (bc, lbc), (ac, lac) = unit(sub(b, a)), unit(sub(c, b)), unit(sub(c, a))
    if min(lab, lbc, lac) < 0.125:
        return 0.0
    if sum(x * y for x, y in zip(ab, bc)) > 0.99 and sum(x * y for x, y in zip(ab, ac)) > 0.99:
        return 0.0
    ab, bc = sub(b, a), sub(c, b)
    last, length = a, 0.0
    for i in range(8):
        t = i / 8.0
        pt = [a[k] + ab[k] * (1 - t) + bc[k] * t for k in range(3)]
        length += math.dist(pt, last)
        last = pt
    return length


def surface_extras(data, samplesize):
    """id's BSP -> the .srf text q3map2's own compile would have left beside it: each surface's
    lightmap axis, sample size, entity and shadow flags (without one, every axis is zero and
    q3map2 makes no lightmaps). Shadows as id's q3map cast them (light_trace.c): the world's faces
    and models (triangle soups) do, patches don't (it had no -patchshadows), and brush entities
    don't (their surfaces aren't in the world's leaves). q3map2's own defaults left q3dm11's
    shadowed walls 2.7 times as bright as id's. The worldspawn's ambient goes on every surface
    (q3map2's light stage takes it from here, not from the worldspawn: without it q3dm16's
    darkest walls were black where id's are 15 to 25)."""
    parts = lumps(data)
    ambient = ambient_color(data)
    owner = {}     # model number -> entity number
    ents = re.findall(r"\{([^{}]*)\}", parts[ENTITIES].decode("latin-1"))
    for i, block in enumerate(ents):
        m = re.search(r'"model"\s+"\*(\d+)"', block)
        if m:
            owner[int(m.group(1))] = i
    model_of = {}
    for mi in range(len(parts[7]) // 40):
        first, count = struct.unpack_from("<ii", parts[7], mi * 40 + 24)
        for s in range(first, first + count):
            model_of[s] = mi
    verts = parts[VERTS]
    out = []
    for si in range(len(parts[SURFACES]) // SURF_SIZE):
        s = struct.unpack_from("<12i12f2i", parts[SURFACES], si * SURF_SIZE)
        kind, first, count, pw, ph = s[2], s[3], s[4], s[24], s[25]
        mi = model_of.get(si, 0)
        lines = ["entity %d" % (owner.get(mi, 0) if mi else 0), "castShadows %d" % (1 if mi == 0 and kind in (1, 3) else 0),
                 "receiveShadows 1"]
        if any(ambient):
            lines.append("ambientColor ( %f %f %f )" % tuple(ambient))
        if kind == 1:                                   # planar: the plane's major axis
            axis = lightmap_axis(s[21:24])
        elif kind == 2:                                 # patch: an axis every control point faces, if any
            pts = [struct.unpack_from("<3f", verts, (first + v) * VERT_SIZE) for v in range(count)]
            normals = [struct.unpack_from("<3f", verts, (first + v) * VERT_SIZE + 28) for v in range(count)]
            axis = next((ax for ax in PATCH_AXES if all(sum(a * b for a, b in zip(n, ax)) >= 0.25 for n in normals)),
                        (0, 0, 0))
            longest = 0.0
            for j in range(0, pw - 2, 2):
                for i in range(0, ph - 2, 2):
                    p = lambda r, c: pts[r * pw + c]
                    longest = max(longest, curve_length(p(i, j), p(i, j + 1), p(i, j + 2)),
                                  curve_length(p(i, j), p(i + 1, j), p(i + 2, j)))
            lines.append("longestCurve %f" % longest)
        else:                                           # triangle soups (models) are vertex lit; flares
            axis = None
        if axis is None:
            lines.append("sampleSize 0")
        else:
            lines += ["sampleSize %d" % samplesize, "lightmapAxis ( %d %d %d )" % axis]
        out.append("%d\n{\n\t%s\n}\n" % (si, "\n\t".join(lines)))
    return "".join(out)


def geometry_differs(ida, relit):
    """-> None if the relit BSP has id's geometry (what the engine checks too), else the reason."""
    a, b = lumps(ida), lumps(relit)
    for i in range(1, LUMPS):
        if i not in LIT and a[i] != b[i]:
            return "lump %d differs" % i
    if len(a[VERTS]) != len(b[VERTS]) or len(a[SURFACES]) != len(b[SURFACES]):
        return "vertex or surface count differs"
    if len(a[LIGHTGRID]) != len(b[LIGHTGRID]):
        return "light grid size differs"
    for i in range(0, len(a[VERTS]), VERT_SIZE):
        # xyz, st, (lightmap st), normal; the color is light
        if a[VERTS][i:i + 20] != b[VERTS][i:i + 20] or a[VERTS][i + 28:i + 40] != b[VERTS][i + 28:i + 40]:
            return "vertex %d moved" % (i // VERT_SIZE)
    for i in range(0, len(a[SURFACES]), SURF_SIZE):
        # shader, fog, type, verts, indexes; patch size. Lightmap number, place and vectors are light.
        if a[SURFACES][i:i + 28] != b[SURFACES][i:i + 28] or a[SURFACES][i + 96:i + 104] != b[SURFACES][i + 96:i + 104]:
            return "surface %d differs" % (i // SURF_SIZE)
    return None


def surface_light(data, rgb=False):
    """-> {surface: (lightmap mean, area)} for the lightmapped faces and patches: the mean over the
    lightmap rectangle the surface's vertices span (q3map2's stored rectangle isn't where its
    packing put the surface), clamped at 127, where the game's overbright shift saturates. rgb:
    the mean color instead, unclamped."""
    parts = lumps(data)
    lm = np.frombuffer(parts[LIGHTMAPS], np.uint8).reshape(-1, 128, 128, 3).astype(np.float32)
    lm = lm if rgb else np.minimum(lm, 127).mean(3)
    verts = np.frombuffer(parts[VERTS], np.uint8).reshape(-1, VERT_SIZE)
    xyz = verts[:, 0:12].copy().view(np.float32).reshape(-1, 3)
    lst = verts[:, 20:28].copy().view(np.float32).reshape(-1, 2) * 128
    idx = np.frombuffer(parts[11], np.int32)
    out = {}
    for i in range(len(parts[SURFACES]) // SURF_SIZE):
        s = struct.unpack_from("<12i", parts[SURFACES], i * SURF_SIZE)
        if s[2] not in (1, 2) or s[7] < 0:
            continue
        st = lst[s[3]:s[3] + s[4]]
        x0, y0 = np.clip(np.floor(st.min(0)).astype(int), 0, 127)
        x1, y1 = np.clip(np.ceil(st.max(0)).astype(int), (x0 + 1, y0 + 1), 128)
        if s[2] == 1:
            p = xyz[idx[s[5]:s[5] + s[6]].reshape(-1, 3) + s[3]]
            area = 0.5 * np.linalg.norm(np.cross(p[:, 1] - p[:, 0], p[:, 2] - p[:, 0]), axis=1).sum()
        else:                                           # a patch: its control net's area
            pw, ph = struct.unpack_from("<2i", parts[SURFACES], i * SURF_SIZE + 96)
            g = xyz[s[3]:s[3] + s[4]].reshape(ph, pw, 3)
            area = 0.5 * np.linalg.norm(np.cross(g[1:, 1:] - g[:-1, :-1], g[1:, :-1] - g[:-1, 1:]), axis=2).sum()
        if area > 0:
            texels = lm[s[7], y0:y1, x0:x1]
            out[i] = (texels.reshape(-1, 3).mean(0) if rgb else texels.mean(), area)
    return out


def brightness(ida, relit):
    """-> (id's, the relight's) area-weighted mean lightmap value over the surfaces both light."""
    a, b = surface_light(ida), surface_light(relit)
    keys = [k for k in a if k in b]
    w = [a[k][1] for k in keys]
    return float(np.average([a[k][0] for k in keys], weights=w)), float(np.average([b[k][0] for k in keys], weights=w))


def vertex_light(data):
    """-> the mean vertex color of the vertex-lit surfaces (models, nolightmap shaders), or None."""
    parts = lumps(data)
    colors = np.frombuffer(parts[VERTS], np.uint8).reshape(-1, VERT_SIZE)[:, 40:43]
    lit = np.zeros(len(colors), bool)
    for i in range(len(parts[SURFACES]) // SURF_SIZE):
        kind, first, count = struct.unpack_from("<3i", parts[SURFACES], i * SURF_SIZE + 8)
        if kind in (1, 2, 3) and struct.unpack_from("<i", parts[SURFACES], i * SURF_SIZE + 28)[0] < 0:
            lit[first:first + count] = True
    return float(colors[lit].mean()) if lit.any() else None


def grid_light(data):
    """-> the light grid's (ambient, directed) means."""
    g = np.frombuffer(lumps(data)[LIGHTGRID], np.uint8).reshape(-1, 8).astype(np.float32)
    return float(g[:, 0:3].mean()), float(g[:, 3:6].mean())


# Each map's scales, kept so a relight is made again the same way: calibrate()'s. "target" (1 if
# missing) is the brightness they aim for against id's, set where the look test found the relit map
# darker or lighter than with id's lightmaps (the same average light lands differently: q3map2's
# shadows are sharper, its bounce fills them). "bounce" (1 if missing) is the map's radiosity
# bounces, 0 where the bounce left it flatter than id's. "colors" are light_colors()'s. To calibrate
# a map again, leave it only its target, bounce and colors (or delete it). null: the map keeps id's
# lightmaps (make_pk3s.py relights it no more).
SCALES = os.path.join(HERE, "relight_scales.json")


def saved_scales():
    return json.load(open(SCALES)) if os.path.exists(SCALES) else {}


def save_scales(name, scales):
    saved = saved_scales()
    rounded = lambda v: ({k: rounded(x) for k, x in v.items()} if isinstance(v, dict) else
                         [rounded(x) for x in v] if isinstance(v, list) else round(v, 3))
    saved[name] = None if scales is None else rounded(scales)
    with open(SCALES, "w", newline="\n") as f:
        json.dump(dict(sorted(saved.items())), f, indent=1)
        f.write("\n")


_id_shaders = {}


def id_shaders(baseq3):
    """-> ({name: q3shader.Shader} of id's scripts, first definition winning, [script names], Paks)."""
    if baseq3 not in _id_shaders:
        import q3shader
        from q3paks import Paks
        paks = Paks(baseq3)
        names = sorted(n for n in paks.files if n.startswith("scripts/") and n.endswith(".shader"))
        shaders = {}
        for n in names:
            for sh in q3shader.parse_text(paks.read(n).decode("latin-1"), os.path.basename(n)):
                shaders.setdefault(sh.name, sh)
        _id_shaders[baseq3] = (shaders, [os.path.basename(n)[:-len(".shader")] for n in names], paks)
    return _id_shaders[baseq3]


def missing_light_images(bsp, baseq3):
    """-> the map's surface light shaders whose light image isn't in id's paks: q3map_lightImage,
    else qer_editorImage, else the shader's own name. id's q3map took the light's color from it,
    and the paks ship without these editor-only images (the hell
    fogs' hellfog.tga, the red skies' stars_red.tga, q3ctf3's team light images), so q3map2 falls
    back to another image and lights them grey or white."""
    shaders, _, paks = id_shaders(baseq3)
    parts = lumps(bsp)
    used = {parts[1][i * 72:i * 72 + 64].split(b"\0")[0].decode("latin-1").lower() for i in range(len(parts[1]) // 72)}
    out = []
    for name in sorted(used):
        sh = shaders.get(name)
        if not sh or sh.surfacelight <= 0:
            continue
        if not paks.image(sh.lightimage or sh.editorimage or name):
            out.append(name)
    return out


def write_overrides(home, overrides, baseq3):
    """q3map2 shader overrides in home/baseq3/scripts, read before id's scripts (the first
    definition wins): {shader: [r, g, b]} lights it in that color (q3map_lightRGB), None not at all."""
    shaders, scripts, _ = id_shaders(baseq3)
    d = os.path.join(home, "baseq3", "scripts")
    os.makedirs(d, exist_ok=True)
    out = []
    for name, color in sorted(overrides.items()):
        text = shaders[name].text
        if color is None:
            text = re.sub(r"(?i)(q3map_surfacelight\s+)\S+", r"\g<1>0", text)
        else:
            at = text.index("{") + 1
            text = text[:at] + "\n\tq3map_lightRGB %g %g %g" % tuple(color) + text[at:]
        out.append(text)
    open(os.path.join(d, "oz_relight.shader"), "w").write("\n\n".join(out) + "\n")
    open(os.path.join(d, "shaderlist.txt"), "w").write("\n".join(["oz_relight"] + scripts) + "\n")


def light_colors(name, bsp, baseq3, q3map2, work):
    """-> {shader: [r, g, b]} for the map's missing_light_images(), read off id's lightmaps: a quick
    run with each such shader alone lit white finds the surfaces it lights most (60% or more of
    their light), and id's lightmaps there hold its light's color. A shader that lights too little
    to tell is left out."""
    work = os.path.join(work, "colors")
    unknown = missing_light_images(bsp, baseq3)
    if not unknown:
        return {}
    run = lambda lit: surface_light(relight(name, bsp, baseq3, q3map2, work, 16, [], {"surface": 1.0, "bounce": 0},
                                            {s: [1, 1, 1] if s == lit else None for s in unknown})[0], rgb=True)
    ida, base = surface_light(bsp, rgb=True), run(None)
    total = sum(v[1] for v in ida.values())
    colors = {}
    for s in unknown:
        lit = run(s)
        keys = [k for k in lit if k in base and k in ida and lit[k][0].sum() > 20 and
                (lit[k][0].sum() - base[k][0].sum()) > 0.6 * lit[k][0].sum()]
        if sum(ida[k][1] for k in keys) < 0.002 * total or len(keys) < 5:
            continue
        c = np.average([ida[k][0] for k in keys], axis=0, weights=[ida[k][1] for k in keys])
        colors[s] = [float(x) for x in c / max(c.max(), 0.001)]
    return colors


def calibrate(name, bsp, baseq3, q3map2, work, target=1.0, bounce=1, colors=None):
    """-> the map's scales, each giving the relight id's average times target. Light adds up, so a
    mean is the point lights' share (ambient included, bounce and all) plus the surface scale times
    the rest: two quick runs, all lights at 1 and the point lights alone, find the surface scale;
    the vertex and grid scales then bring the vertex light and the grid at that scale to id's."""
    work = os.path.join(work, "calibrate")
    full = relight(name, bsp, baseq3, q3map2, work, 16, QUICK, {"surface": 1.0, "bounce": bounce}, colors)[0]
    point = relight(name, bsp, baseq3, q3map2, work, 16, QUICK, {"surface": 0.0001, "bounce": bounce}, colors)[0]
    ida, f = brightness(bsp, full)
    p = brightness(bsp, point)[1]
    scales = {"surface": (target * ida - p) / max(f - p, 0.001)}
    at = lambda p, f: p + scales["surface"] * (f - p)     # a mean at the surface scale
    if vertex_light(bsp):
        scales["vertex"] = target * vertex_light(bsp) / max(at(vertex_light(point), vertex_light(full)), 0.001)
    (ia, idir), (pa, pd), (fa, fd) = grid_light(bsp), grid_light(point), grid_light(full)
    scales["grid"] = target * idir / max(at(pd, fd), 0.001)
    scales["gridambient"] = target * ia / max(at(pa, fa), 0.001) / scales["grid"]
    scales = {k: min(SCALE_RANGE[k][1], max(SCALE_RANGE[k][0], v)) for k, v in scales.items()}
    if target != 1.0:
        scales["target"] = target
    if bounce != 1:
        scales["bounce"] = bounce
    if colors:
        scales["colors"] = colors
    return scales


def q3map2_path():
    found = sorted(glob.glob(os.path.join(ROOT, "build", "tools", "netradiant-custom-*", "app", "q3map2.exe")))
    return found[-1] if found else None


def relight(name, bsp, baseq3, q3map2, work, samplesize=8, options=OPTIONS, scales=None, overrides=None):
    """id's BSP -> (its relit copy, its scales). work: a scratch folder of its own. Without scales,
    the map's saved ones, else calibrate()'s (to the saved target, bounce and colors, if the map
    has only those). overrides: write_overrides()'s, by default the scales' light colors."""
    if scales is None:
        scales = saved_scales().get(name) or {}
        if "surface" not in scales:
            scales = calibrate(name, bsp, baseq3, q3map2, work, scales.get("target", 1.0), scales.get("bounce", 1),
                               scales.get("colors"))
    if overrides is None:
        overrides = scales.get("colors") or {}
    # the map's radiosity bounces ("bounce", 1 if missing) in place of the options' own
    i = options.index("-bounce") if "-bounce" in options else len(options)
    bounce = int(scales.get("bounce", 1))
    options = options[:i] + options[i + 2:] + (["-bounce", str(bounce)] if bounce else [])
    folder = os.path.abspath(os.path.join(work, name))
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(os.path.join(folder, "maps"))
    src = os.path.join(folder, "maps", name + ".bsp")
    open(src, "wb").write(keep_lights(bsp))
    open(os.path.join(folder, "maps", name + ".srf"), "w").write(surface_extras(bsp, samplesize))
    # only id's paks: a map pack's broken script stops q3map2, and its shaders could replace id's
    forbid = []
    for p in sorted(glob.glob(os.path.join(baseq3, "*.pk3"))):
        if not re.fullmatch(r"pak[0-8]\.pk3", os.path.basename(p), re.I):
            forbid += ["-fs_forbiddenpath", os.path.basename(p)]
    home = []
    if overrides:
        write_overrides(os.path.join(folder, "home"), overrides, baseq3)
        home = ["-fs_homepath", os.path.join(folder, "home")]
    cmd = ([q3map2, "-game", "quake3", "-fs_basepath", os.path.dirname(baseq3)] + forbid + home +
           ["-light", "-threads", str(os.cpu_count() or 4), "-samplesize", str(samplesize)] + options +
           [o for k, v in scales.items() for opt in SCALE_OPTIONS.get(k, ()) for o in (opt, "%g" % v)] + [src])
    with open(os.path.join(folder, "light.log"), "w") as f:
        r = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, cwd=folder)
    if r.returncode:
        raise RuntimeError("q3map2 failed on %s (%s)" % (name, os.path.join(folder, "light.log")))
    relit = open(src, "rb").read()
    why = geometry_differs(bsp, relit)
    if why:
        raise RuntimeError("q3map2 changed %s's geometry: %s" % (name, why))
    # Surfaces id left without a lightmap (models, nolightmap shaders) keep id's number: q3map2
    # marks them vertex lit (-3) where id's are -1, and the renderer makes their shaders by it.
    a, b = lumps(bsp), lumps(relit)
    surfs = bytearray(b[SURFACES])
    for i in range(0, len(surfs), SURF_SIZE):
        ida, now = struct.unpack_from("<i", a[SURFACES], i + 28)[0], struct.unpack_from("<i", surfs, i + 28)[0]
        if ida < 0 and now < 0:
            struct.pack_into("<i", surfs, i + 28, ida)
    b[SURFACES] = bytes(surfs)
    return build(b), scales


def describe(ida, relit, scales):
    """-> a line on a relight: its scales, and its lightmaps, vertex light and grid against id's."""
    a, b = brightness(ida, relit)
    v = "vertex %.2f, " % (vertex_light(relit) / vertex_light(ida)) if vertex_light(ida) else ""
    return "%s; %d lightmap pages; lightmaps %.2f, %sgrid %.2f times id's" % (
        " ".join("%s %.2f" % kv for kv in scales.items() if not isinstance(kv[1], dict)) +
        (" colors %d" % len(scales["colors"]) if scales.get("colors") else ""),
        len(lumps(relit)[LIGHTMAPS]) // (128 * 128 * 3), b / a, v,
        sum(grid_light(relit)) / max(sum(grid_light(ida)), 0.001))


def main():
    from q3paks import find_baseq3, Paks
    ap = argparse.ArgumentParser()
    ap.add_argument("maps", nargs="+")
    ap.add_argument("--samplesize", type=int, default=8)
    ap.add_argument("--q3map2", default=q3map2_path())
    ap.add_argument("--baseq3")
    ap.add_argument("--options", help="q3map2 light options instead of %s" % " ".join(OPTIONS))
    ap.add_argument("--work", default=os.path.join(ROOT, "build", "relight"))
    ap.add_argument("--colors", action="store_true",
                    help="read the light colors of shaders whose light image is missing off id's lightmaps "
                         "(saved in relight_scales.json; the map calibrates again)")
    a = ap.parse_args()
    if not a.q3map2:
        sys.exit("no q3map2: pass --q3map2")
    baseq3 = a.baseq3 or find_baseq3()
    paks = Paks(baseq3)
    for m in a.maps:
        bsp = paks.read("maps/%s.bsp" % m)
        if a.colors:
            colors = light_colors(m, bsp, baseq3, a.q3map2, a.work)
            keep = {k: v for k, v in (saved_scales().get(m) or {}).items() if k in ("target", "bounce")}
            save_scales(m, dict(keep, colors=colors) if colors else keep)
            print("%s: light colors %s" % (m, ", ".join("%s (%.2f %.2f %.2f)" % ((s,) + tuple(c)) for s, c in colors.items())
                                           or "none found"), flush=True)
        relit, scales = relight(m, bsp, baseq3, a.q3map2, a.work, a.samplesize,
                                a.options.split() if a.options is not None else OPTIONS)
        out = os.path.join(a.work, m, "oz_relight.bsp")
        open(out, "wb").write(light_only(relit))     # as it ships: the light alone, not id's map
        save_scales(m, scales)
        print("%s: %s -> %s" % (m, describe(bsp, relit, scales), out), flush=True)


if __name__ == "__main__":
    main()
