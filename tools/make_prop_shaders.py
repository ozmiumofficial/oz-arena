"""Quake 3's multi-stage shaders for props: the shimmer on powerups, armor and health.

python make_prop_shaders.py <extracted baseq3> <work/_props>

build_props.py reduces every prop shader to one PBR texture set, which loses what makes pickups
read as pickups in Quake 3: environment-mapped chrome (tcGen environment), additive energy layers,
and tcMod scale/rotate/scroll/turb with rgbGen waves (Mega Health's crackling electric layer, the
armor's scrolling energy, Quad Damage's blue shine). This step finds the materials whose shader
has any of that and writes prop_shaders.json:
  {material name: {shader, two_sided, add, lit, stages: [{tex, mode, env, scale, rotate, scroll,
                   turb, wave}], deforms: [{a, b, move}]}}   (up to 3 stages, in draw order;
                   deforms only when the shader has deformVertexes, see q3shader.deform_uniforms)
plus stagetex/ (each stage's image, 2x). Godot's import script swaps those materials for the
q3_stages shader (shaders/q3_stages*.gdshader), which draws the stages the way the engine does.
Material names are matched the way build_props.py names them (texture stem + draw-mode flags).
"""
import os, sys, re, json, glob
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import q3common as q3
import q3shader

ROOT, WORK = sys.argv[1], sys.argv[2]
OUT_TEX = os.path.join(WORK, "stagetex")
os.makedirs(OUT_TEX, exist_ok=True)
textures = json.load(open(os.path.join(WORK, "textures.json")))
materials = json.load(open(os.path.join(WORK, "materials.json")))
shaders = q3shader.parse_all(ROOT)
MODES = {"opaque": 1, "add": 2, "blend": 3, "filter": 4, "mask": 5}
WAVES = {"sin": 1, "square": 2, "triangle": 3, "sawtooth": 4, "inversesawtooth": 5}


def material_name(shader):
    """The material build_props.py made for a surface shader (same rules as material_for there)."""
    info = q3.shader_info(ROOT, shader) or dict(map=shader, blend="opaque", two_sided=False, env_only=False)
    if info["env_only"]:
        kind = "envadd" if info["blend"] == "add" else "env"
    else:
        kind = "add" if info["blend"] == "add" else "paint"
    blend = info["blend"] if kind == "paint" else ("add" if kind in ("add", "envadd") else "opaque")
    for ti, t in enumerate(textures):
        if t["source"] == info["map"] and t.get("kind", "paint") == kind:
            for m in materials:
                if m["texture"] == ti and m["blend"] == blend and m["two_sided"] == info["two_sided"]:
                    return m["name"]
    return None


def animated(sh):
    st = [s for s in sh.stages if s.map and not s.map.startswith("$")]
    return bool(st) and (len(st) > 1 or any(s.tcgen == "environment" or s.tcmods or
                                            (s.rgbgen and s.rgbgen[0] == "wave") for s in st))


copied = {}


def stage_texture(path):
    full = q3.resolve_texture(ROOT, path)
    if not full:
        return None
    name = re.sub(r"[^a-z0-9_]+", "_", os.path.splitext(path.lower())[0]).strip("_") + ".png"
    if name not in copied:
        # color and alpha resized separately: a joint RGBA resize premultiplies, which blanks the
        # color wherever alpha is 0, and additive stages use that color (q3banner02's alpha is all 0)
        im = Image.open(full).convert("RGBA")
        size = (im.width * 2, im.height * 2)
        rgb = im.convert("RGB").resize(size, Image.LANCZOS)
        rgb.putalpha(im.getchannel("A").resize(size, Image.LANCZOS))
        rgb.save(os.path.join(OUT_TEX, name))
        copied[name] = path
    return "stagetex/" + name


def stage(s):
    tex = stage_texture(s.map)
    if tex is None:
        return None
    d = dict(tex=tex, mode=MODES["mask"] if s.alphafunc else MODES.get(s.blend, 1), env=s.tcgen == "environment",
             scale=[1.0, 1.0], rotate=0.0, scroll=[0.0, 0.0], turb=[0.0, 0.0], wave=[0, 1.0, 0.0, 0.0, 0.0])
    for m in s.tcmods:
        args = [x for x in m[1:] if isinstance(x, float)]
        if m[0] == "scale" and len(args) >= 2:
            d["scale"] = [d["scale"][0] * args[0], d["scale"][1] * args[1]]
        elif m[0] == "rotate" and args:
            d["rotate"] = args[0]
        elif m[0] == "scroll" and len(args) >= 2:
            d["scroll"] = args[:2]
        elif m[0] == "turb" and len(args) >= 4:   # turb base amp phase freq
            d["turb"] = [args[1], args[3]]
    if s.rgbgen and s.rgbgen[0] == "wave" and len(s.rgbgen) >= 6:
        d["wave"] = [WAVES.get(str(s.rgbgen[1]), 1)] + [float(x) for x in s.rgbgen[2:6]]
    d["lit"] = bool(s.rgbgen and s.rgbgen[0] in ("lightingdiffuse", "vertex", "exactvertex"))
    return d


out = {}
models = os.path.join(ROOT, "models")
for f in sorted(glob.glob(os.path.join(models, "**", "*.md3"), recursive=True)):
    if os.sep + "players" + os.sep in f:
        continue
    _, _, surfs = q3.read_md3(f)
    for s in surfs:
        if not s["shaders"]:
            continue
        shader = os.path.splitext(s["shaders"][0].replace("\\", "/").lower())[0]
        sh = shaders.get(shader)
        if sh is None or not animated(sh):
            continue
        mat = material_name(s["shaders"][0])
        if mat is None or mat in out:
            continue
        stages = [x for x in (stage(st) for st in sh.stages if st.map and not st.map.startswith("$")) if x][:3]
        if not stages:
            continue
        # pickups get their full Quake 3 look; elsewhere only pure effect materials do, so painted
        # weapon and map-object bodies keep their remastered PBR maps
        pickup = shader.split("/")[1] in ("powerups", "flags", "ammo") if shader.count("/") > 1 else False
        painted = any(not x["env"] and x["mode"] in (1, 3, 5) for x in stages)
        if painted and not pickup:
            continue
        out[mat] = dict(shader=shader, two_sided=sh.cull in ("none", "disable", "twosided"),
                        add=stages[0]["mode"] == MODES["add"], lit=any(x["lit"] for x in stages), stages=stages)
        # deformVertexes (the CTF flags wave), as godot/shaders/q3_deform.gdshaderinc's uniforms
        dfs = q3shader.deform_uniforms(sh.deforms)
        if dfs:
            out[mat]["deforms"] = [dict(a=d[0], b=d[1], move=d[2]) for d in dfs]
json.dump(out, open(os.path.join(WORK, "prop_shaders.json"), "w"), indent=1)
print("prop shaders: %d materials, %d stage textures" % (len(out), len(copied)))
for k, v in sorted(out.items()):
    print("  %-34s %-40s %s" % (k, v["shader"], " + ".join(("env " if x["env"] else "") + {1: "base", 2: "add", 3: "blend", 4: "filter", 5: "mask"}[x["mode"]] for x in v["stages"])))
