"""Shared id Tech 3 readers for the remaster tools: MD3, .skin, animation.cfg, shader lookup.

Paths inside an extracted tree are lower-case (extract_pk3.py lower-cases everything), so
lookups work the same on Windows, Linux and macOS.
"""
import os, re, struct, glob
import numpy as np

UNITS_PER_M = 32.0  # id Tech 3 units per meter used throughout (matches the GAME project)

# where remaster_model.py and setup_check.py look when no path is given
DEFAULT_Q3 = [
    r"C:\Program Files (x86)\Steam\steamapps\common\Quake 3 Arena\baseq3",
    r"C:\Program Files\Steam\steamapps\common\Quake 3 Arena\baseq3",
    r"D:\SteamLibrary\steamapps\common\Quake 3 Arena\baseq3",
    r"E:\SteamLibrary\steamapps\common\Quake 3 Arena\baseq3",
    r"C:\GOG Games\Quake III Arena\baseq3",
    os.path.expanduser("~/.steam/steam/steamapps/common/Quake 3 Arena/baseq3"),
]
DEFAULT_BLENDER = [
    r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe",
    r"C:\Program Files\Blender Foundation\Blender 5.1\blender.exe",
    r"C:\Program Files\Blender Foundation\Blender 5.0\blender.exe",
    r"C:\Program Files\Blender Foundation\Blender 4.5\blender.exe",
    r"C:\Program Files\Blender Foundation\Blender 4.4\blender.exe",
    "/Applications/Blender.app/Contents/MacOS/Blender", "/usr/bin/blender", "/snap/bin/blender",
]


def default_godot():
    """Where build_library.py and build_cards.py look for Godot when --godot isn't given."""
    import glob as _g, shutil as _s
    return [
        os.environ.get("GODOT_PATH"),
        r"C:\Program Files (x86)\Steam\steamapps\common\Godot Engine\godot.windows.opt.tools.64.exe",
        r"C:\Program Files\Steam\steamapps\common\Godot Engine\godot.windows.opt.tools.64.exe",
        r"D:\SteamLibrary\steamapps\common\Godot Engine\godot.windows.opt.tools.64.exe",
        _s.which("godot"), _s.which("godot4"),
    ] + sorted(_g.glob(os.path.expanduser(r"~\Desktop\Godot*.exe")) + _g.glob(os.path.expanduser(r"~\Downloads\Godot*.exe")))


FEET_Z = 24.0       # player origin sits 24 units above the feet (MINS_Z)

ANIM_NAMES = [
    "BOTH_DEATH1", "BOTH_DEAD1", "BOTH_DEATH2", "BOTH_DEAD2", "BOTH_DEATH3", "BOTH_DEAD3",
    "TORSO_GESTURE", "TORSO_ATTACK", "TORSO_ATTACK2", "TORSO_DROP", "TORSO_RAISE",
    "TORSO_STAND", "TORSO_STAND2", "LEGS_WALKCR", "LEGS_WALK", "LEGS_RUN", "LEGS_BACK",
    "LEGS_SWIM", "LEGS_JUMP", "LEGS_LAND", "LEGS_JUMPB", "LEGS_LANDB", "LEGS_IDLE",
    "LEGS_IDLECR", "LEGS_TURN",
    # Team Arena additions (torso only)
    "TORSO_GETFLAG", "TORSO_GUARDBASE", "TORSO_PATROL", "TORSO_FOLLOWME",
    "TORSO_AFFIRMATIVE", "TORSO_NEGATIVE",
]


def read_md3(path):
    """-> (num_frames, tags {name: [12 floats per frame]}, surfaces [dict])."""
    b = open(path, "rb").read()
    ident, ver = struct.unpack_from("<4si", b, 0)
    if ident != b"IDP3":
        raise ValueError("%s is not an MD3 file" % path)
    # header: ident, version, name[64], flags, then the counts (byte 76)
    nfr, ntag, nsurf, nskin, ofr, otag, osurf, oeof = struct.unpack_from("<8i", b, 76)
    tags = {}
    for f in range(nfr):
        for t in range(ntag):
            o = otag + (f * ntag + t) * 112
            name = b[o:o + 64].split(b"\0")[0].decode("latin-1").lower()
            tags.setdefault(name, []).append(struct.unpack_from("<12f", b, o + 64))
    surfs = []
    o = osurf
    for _ in range(nsurf):
        name = b[o + 4:o + 68].split(b"\0")[0].decode("latin-1").lower()
        (flags, snfr, nsh, nv, ntri, otri, osh, ost, oxyz, oend) = struct.unpack_from("<10i", b, o + 68)
        shaders = [b[o + osh + i * 68:o + osh + i * 68 + 64].split(b"\0")[0].decode("latin-1").lower()
                   for i in range(nsh)]
        tris = np.frombuffer(b, "<i4", ntri * 3, o + otri).reshape(-1, 3).copy()
        st = np.frombuffer(b, "<f4", nv * 2, o + ost).reshape(-1, 2).copy()
        xyzn = np.frombuffer(b, "<i2", snfr * nv * 4, o + oxyz).reshape(snfr, nv, 4)
        surfs.append(dict(name=name, shaders=shaders, tris=tris, st=st, xyz=xyzn[:, :, :3].copy(),
                          nrm=xyzn[:, :, 3].astype(np.uint16).copy()))
        o += oend
    return nfr, tags, surfs


def read_skin(path):
    """surface name -> texture/shader path (lower-case). Tag lines have no path."""
    m = {}
    if not os.path.exists(path):
        return m
    for line in open(path, encoding="latin-1"):
        if "," in line:
            k, v = line.strip().split(",", 1)
            v = v.strip().strip('"').replace("\\", "/").lower()
            if v:
                m[k.strip().lower()] = v
    return m


def read_cfg(path):
    """-> (anims {name: dict(first, num, loop, fps)}, legs skip, extra settings)."""
    rows, extra = [], {}
    for line in open(path, encoding="latin-1"):
        q = line.split("//")[0].split()
        if not q:
            continue
        if len(q) >= 4 and all(re.fullmatch(r"-?\d+", x) for x in q[:4]):
            rows.append([int(x) for x in q[:4]])
        else:
            extra[q[0].lower()] = q[1:]
    anims = {}
    for n, r in zip(ANIM_NAMES, rows):
        anims[n] = dict(first=r[0], num=r[1], loop=r[2], fps=max(r[3], 1))
    # cg_players.c: leg frames in lower.md3 skip the torso-only range
    skip = anims["LEGS_WALKCR"]["first"] - anims["TORSO_GESTURE"]["first"]
    return anims, skip, extra


_shader_cache = {}


def shader_maps(root):
    """shader name -> first non-lightmap 'map' image, from every scripts/*.shader."""
    if root in _shader_cache:
        return _shader_cache[root]
    out = {}
    for f in glob.glob(os.path.join(root, "scripts", "*.shader")):
        text = open(f, encoding="latin-1").read().lower()
        text = re.sub(r"//[^\n]*", "", text)
        for m in re.finditer(r"([^\s{}]+)\s*\{((?:[^{}]|\{[^{}]*\})*)\}", text):
            name, body = m.group(1), m.group(2)
            for mm in re.finditer(r"\b(?:map|clampmap|animmap\s+\S+)\s+(\S+)", body):
                p = mm.group(1)
                if not p.startswith("$"):
                    out.setdefault(name, p)
                    break
    _shader_cache[root] = out
    return out


_shader_bodies = {}


def shader_bodies(root):
    """shader name -> its body text (lower-case, comments stripped), from scripts/*.shader."""
    if root in _shader_bodies:
        return _shader_bodies[root]
    out = {}
    for f in glob.glob(os.path.join(root, "scripts", "*.shader")):
        text = re.sub(r"//[^\n]*", "", open(f, encoding="latin-1").read().lower())
        i, n = 0, len(text)
        while i < n:
            m = re.compile(r"([^\s{}]+)\s*\{").search(text, i)
            if not m:
                break
            depth, j = 0, m.end() - 1
            while j < n:
                if text[j] == "{":
                    depth += 1
                elif text[j] == "}":
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            out.setdefault(m.group(1), text[m.end():j])
            i = j + 1
    _shader_bodies[root] = out
    return out


def shader_info(root, name):
    """How id Tech 3 draws a shader, reduced to what a PBR material needs.
    -> dict(map, blend: opaque|mask|blend|add, two_sided, env, glow) or None if no shader."""
    body = shader_bodies(root).get(os.path.splitext(name)[0])
    if body is None:
        return None
    stages = re.findall(r"\{([^{}]*)\}", body)
    top = re.sub(r"\{[^{}]*\}", " ", body)
    layers = []
    for st in stages:
        m = re.search(r"\b(?:map|clampmap)\s+(\S+)|\banimmap\s+\S+\s+(\S+)", st)
        if not m or (m.group(1) or m.group(2)).startswith("$"):
            continue
        blend = ("add" if re.search(r"blendfunc\s+(add|gl_one\s+gl_one)\b", st) else
                 "blend" if re.search(r"blendfunc\s+(blend|gl_src_alpha\s+gl_one_minus_src_alpha)", st) else
                 "mask" if "alphafunc" in st else "opaque")
        layers.append(dict(path=m.group(1) or m.group(2), blend=blend, env="tcgen environment" in st))
    if not layers:
        return None
    # the painted base is the first layer that isn't a reflection; a shader that is only
    # reflection layers falls back to the image named like the shader
    base = next((l for l in layers if not l["env"]), None)
    env_only = False
    if base is None:
        own = os.path.splitext(name)[0]
        if any(os.path.isfile(os.path.join(root, own + e)) for e in (".tga", ".jpg")):
            base = dict(path=own + ".tga", blend=layers[0]["blend"], env=False)
        else:
            base, env_only = layers[0], True  # chrome/glass item: only a reflection map
    blend = base["blend"]
    under = None
    if base is not layers[0] and blend == "blend":
        blend = "opaque"  # painted layer drawn over a reflection layer: solid in PBR
        if layers[0]["env"]:
            under = layers[0]["path"]  # shows through where the paint is transparent
    glow = next((l["path"] for l in layers if l is not base and l["blend"] == "add" and not l["env"]
                 and l["path"] != base["path"]), None)
    return dict(map=base["path"], blend=blend, two_sided=bool(re.search(r"\bcull\s+(none|disable|twosided)", top)),
                env=any(l["env"] for l in layers), env_only=env_only, glow=glow, under=under)


def draw_mode(root, path):
    """How id Tech 3 draws a skin or shader path: dict(blend=opaque|mask|blend|add, two_sided)."""
    info = shader_info(root, path)
    if info is None:
        return dict(blend="opaque", two_sided=False)
    return dict(blend=info["blend"], two_sided=info["two_sided"])


def resolve_texture(root, path):
    """Find the image id Tech 3 would load for a skin/shader path: exact file, then the
    .tga/.jpg swap the engine does, then a shader of that name. -> absolute path or None."""
    if not path:
        return None
    stem = os.path.splitext(path)[0]
    for cand in (path, stem + ".tga", stem + ".jpg", stem + ".png"):
        full = os.path.join(root, cand)
        if os.path.isfile(full):
            return full
    sm = shader_maps(root).get(stem) or shader_maps(root).get(path)
    if sm:
        s2 = os.path.splitext(sm)[0]
        for cand in (sm, s2 + ".tga", s2 + ".jpg"):
            full = os.path.join(root, cand)
            if os.path.isfile(full):
                return full
    return None


def player_models(root):
    """Player model folders that have the full lower/upper/head + animation.cfg set."""
    base = os.path.join(root, "models", "players")
    out = []
    for d in sorted(glob.glob(os.path.join(base, "*"))):
        if all(os.path.isfile(os.path.join(d, f)) for f in ("lower.md3", "upper.md3", "head.md3", "animation.cfg")):
            out.append(os.path.basename(d))
    return out


def upscale_factor(w, h, target=2048):
    """Power-of-two upscale so the long edge reaches about `target`, between 1x and 8x."""
    f = 1
    while f < 8 and max(w, h) * f * 2 <= target:
        f *= 2
    return f
