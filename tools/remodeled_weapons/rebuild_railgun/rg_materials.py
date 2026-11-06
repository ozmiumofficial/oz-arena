"""The rebuilt railgun's PBR materials (adapted from the rocket launcher's rl_materials.py): one node tree per paint,
procedural and seam-free (3D noise in object space), lightly worn and dirtied by the helper bakes (ao.png, edge.png
from rg_bake.py helpers) and carrying the detail normal map (detail_normal.png from rg_post.py).

Colors measured on Tripo's railgun texture (sRGB): the body's charcoal coating about 0.245 gray with a cool cast,
darker trim parts 0.155, rubber 0.075, bright metal 0.41. A clean finish: very little wear (a hint of bare steel only on the
sharpest convex edges), light grime in crevices, faint scratches. The red parts (cells, light strips, the vents'
glow) are a neutral light gray here: rg_post.py pack writes their brightness, the shader tints them with the rail
color.

setup(obj, bake_dir) builds the trees on the object's materials; route(obj, channel) sends 'basecolor', 'roughness',
'metallic' through an emission shader for baking, or 'shader' back to the Principled BSDF.
"""
import bpy, os


def lin(c):
    return tuple(((x + 0.055) / 1.055) ** 2.4 if x > 0.04045 else x / 12.92 for x in c)


# name: base color (sRGB), roughness, metallic, wear (how much bare metal shows on edges), grime, scratch
PAINTS = {
    "paint": dict(color=(0.235, 0.240, 0.252), rough=0.50, metal=0.20, wear=0.08, grime=0.50, scratch=0.06, var=0.03),
    "gunmetal": dict(color=(0.300, 0.310, 0.325), rough=0.42, metal=0.85, wear=0.12, grime=0.50, scratch=0.08, var=0.03),
    "dark_metal": dict(color=(0.150, 0.152, 0.160), rough=0.50, metal=0.60, wear=0.08, grime=0.40, scratch=0.05, var=0.02),
    "steel": dict(color=(0.470, 0.480, 0.500), rough=0.32, metal=1.00, wear=0.10, grime=0.40, scratch=0.08, var=0.02),
    "rubber": dict(color=(0.080, 0.082, 0.088), rough=0.82, metal=0.00, wear=0.05, grime=0.30, scratch=0.00, var=0.03),
    "carbon": dict(color=(0.120, 0.122, 0.130), rough=0.38, metal=0.10, wear=0.05, grime=0.30, scratch=0.02, var=0.02),
    "glass_cell": dict(color=(0.800, 0.800, 0.800), rough=0.25, metal=0.00, wear=0.00, grime=0.00, scratch=0.00, var=0.0),
    "trim": dict(color=(0.820, 0.820, 0.820), rough=0.35, metal=0.00, wear=0.00, grime=0.00, scratch=0.00, var=0.0),
    "vent_glow": dict(color=(0.700, 0.700, 0.700), rough=0.40, metal=0.00, wear=0.00, grime=0.00, scratch=0.00, var=0.0),
}
BARE = (0.640, 0.650, 0.660)     # worn-through steel


def _img(nt, path, x, y, data=True):
    t = nt.nodes.new("ShaderNodeTexImage")
    t.location = (x, y)
    if os.path.exists(path):
        im = bpy.data.images.get(os.path.basename(path))
        if im is None or im.filepath != path:
            im = bpy.data.images.load(path, check_existing=True)
        im.colorspace_settings.name = "Non-Color" if data else "sRGB"
        t.image = im
    return t


def _math(nt, op, a=None, b=None, x=0, y=0, clamp=False):
    m = nt.nodes.new("ShaderNodeMath"); m.operation = op; m.location = (x, y); m.use_clamp = clamp
    for i, v in enumerate((a, b)):
        if v is None:
            continue
        if isinstance(v, (int, float)):
            m.inputs[i].default_value = v
        else:
            nt.links.new(v, m.inputs[i])
    return m.outputs[0]


def _maprange(nt, v, a, b, x=0, y=0):
    m = nt.nodes.new("ShaderNodeMapRange"); m.location = (x, y)
    m.interpolation_type = "SMOOTHSTEP"
    nt.links.new(v, m.inputs["Value"])
    m.inputs["From Min"].default_value = a; m.inputs["From Max"].default_value = b
    return m.outputs["Result"]


def _noise(nt, scale, detail=4.0, rough=0.55, x=0, y=0, stretch=None):
    tc = nt.nodes.new("ShaderNodeTexCoord"); tc.location = (x - 400, y)
    src = tc.outputs["Object"]
    if stretch:
        mp = nt.nodes.new("ShaderNodeMapping"); mp.location = (x - 200, y)
        mp.inputs["Scale"].default_value = stretch
        nt.links.new(src, mp.inputs["Vector"]); src = mp.outputs["Vector"]
    n = nt.nodes.new("ShaderNodeTexNoise"); n.location = (x, y)
    n.inputs["Scale"].default_value = scale; n.inputs["Detail"].default_value = detail
    n.inputs["Roughness"].default_value = rough
    nt.links.new(src, n.inputs["Vector"])
    return n.outputs["Fac"]


def _mix(nt, a, b, fac, x=0, y=0, color=True):
    m = nt.nodes.new("ShaderNodeMix"); m.location = (x, y)
    m.data_type = "RGBA" if color else "FLOAT"
    ia, ib, it = (6, 7, 0) if color else (2, 3, 0)
    for sock, v in ((m.inputs[ia], a), (m.inputs[ib], b)):
        if isinstance(v, tuple):
            sock.default_value = v + (1.0,) if color and len(v) == 3 else v
        elif isinstance(v, (int, float)):
            sock.default_value = v
        else:
            nt.links.new(v, sock)
    if isinstance(fac, (int, float)):
        m.inputs[it].default_value = fac
    else:
        nt.links.new(fac, m.inputs[it])
    return m.outputs[2] if color else m.outputs[0]


def build(mat, p, bake_dir):
    mat.use_nodes = True
    nt = mat.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial"); out.location = (1400, 0)
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled"); bsdf.location = (1100, 0); bsdf.name = "PBR"
    ao = _img(nt, os.path.join(bake_dir, "ao.png"), -1200, 300).outputs["Color"]
    edge = _img(nt, os.path.join(bake_dir, "edge.png"), -1200, 0).outputs["Color"]
    nrm_t = _img(nt, os.path.join(bake_dir, "detail_normal.png"), -1200, -300)
    nm = nt.nodes.new("ShaderNodeNormalMap"); nm.location = (800, -400)
    nt.links.new(nrm_t.outputs["Color"], nm.inputs["Color"])
    nt.links.new(nm.outputs["Normal"], bsdf.inputs["Normal"])
    # masks
    big = _noise(nt, 18.0, 3.0, 0.5, -700, 600)
    fine = _noise(nt, 140.0, 6.0, 0.6, -700, 400)
    convex = _math(nt, "MULTIPLY", edge, _maprange(nt, ao, 0.86, 0.98, -900, 200), -600, 100)
    breakup = _math(nt, "MULTIPLY", _math(nt, "ADD", big, _math(nt, "MULTIPLY", fine, 0.6, x=-500, y=500), -400, 500), 0.9,
                    -300, 500)
    wear = _maprange(nt, _math(nt, "MULTIPLY", convex, breakup, -300, 100), 0.30, 0.62, -100, 100)
    wear = _math(nt, "MULTIPLY", wear, p["wear"], 50, 100, clamp=True)
    cav = _math(nt, "SUBTRACT", 1.0, ao, -900, -100)
    # grime follows the crevices; noise only breaks it up a little (big patches of it read as blotches)
    grime = _math(nt, "MULTIPLY", _maprange(nt, cav, 0.22, 0.65, -300, -100),
                  _math(nt, "ADD", _math(nt, "MULTIPLY", big, 0.30, -500, -250), 0.70, -400, -250), -100, -100)
    grime = _math(nt, "MULTIPLY", grime, p["grime"], 50, -100, clamp=True)
    scratches = _noise(nt, 90.0, 2.0, 0.4, -700, -500, stretch=(1.0, 22.0, 22.0))
    scr = _maprange(nt, scratches, 0.55, 0.75, -400, -500)
    # base color with a slight tone variation, worn to bare steel, darkened by grime
    tone = _math(nt, "ADD", 1.0 - p["var"], _math(nt, "MULTIPLY", big, 2 * p["var"], -400, 300), -200, 300)
    base = nt.nodes.new("ShaderNodeMix"); base.data_type = "RGBA"; base.blend_type = "MULTIPLY"; base.location = (0, 300)
    base.inputs[0].default_value = 1.0
    base.inputs[6].default_value = lin(p["color"]) + (1.0,)
    comb = nt.nodes.new("ShaderNodeCombineColor"); comb.location = (-100, 400)
    for i in range(3):
        nt.links.new(tone, comb.inputs[i])
    nt.links.new(comb.outputs[0], base.inputs[7])
    col = _mix(nt, base.outputs[2], lin(BARE), wear, 250, 300)
    dark = _mix(nt, (1.0, 1.0, 1.0), (0.45, 0.42, 0.40), grime, 250, 150)
    colf = nt.nodes.new("ShaderNodeMix"); colf.data_type = "RGBA"; colf.blend_type = "MULTIPLY"; colf.location = (450, 250)
    colf.inputs[0].default_value = 1.0
    nt.links.new(col, colf.inputs[6]); nt.links.new(dark, colf.inputs[7])
    color = colf.outputs[2]
    # roughness: base, scratched, polished where worn, rougher with grime
    r = _math(nt, "ADD", p["rough"], _math(nt, "MULTIPLY", scr, p["scratch"], 200, -500), 350, -400)
    r = _mix(nt, r, 0.28, wear, 500, -350, color=False)
    r = _math(nt, "ADD", r, _math(nt, "MULTIPLY", grime, 0.18, 500, -550), 650, -400, clamp=True)
    m = _mix(nt, p["metal"], 1.0, wear, 650, -200, color=False)
    m = _math(nt, "MULTIPLY", m, _math(nt, "SUBTRACT", 1.0, _math(nt, "MULTIPLY", grime, 0.35, 500, -700), 650, -700),
              800, -200, clamp=True)
    nt.links.new(color, bsdf.inputs["Base Color"])
    nt.links.new(r, bsdf.inputs["Roughness"])
    nt.links.new(m, bsdf.inputs["Metallic"])
    if "Coat Weight" in bsdf.inputs and mat.name in ("glass_cell", "carbon"):
        bsdf.inputs["Coat Weight"].default_value = 0.3
    nt.links.new(bsdf.outputs[0], out.inputs["Surface"])
    # outputs for baking
    em = nt.nodes.new("ShaderNodeEmission"); em.name = "BAKE_EMIT"; em.location = (1100, -400)
    mat["_sock_basecolor"] = color.node.name
    for key, sock in (("basecolor", color), ("roughness", r), ("metallic", m)):
        rr = nt.nodes.new("NodeReroute"); rr.name = "OUT_" + key; rr.location = (950, {"basecolor": -300, "roughness": -350, "metallic": -400}[key])
        nt.links.new(sock, rr.inputs[0])
    mat.diffuse_color = lin(p["color"]) + (1.0,)
    return mat


def setup(obj, bake_dir):
    for s in obj.material_slots:
        if s.material and s.material.name in PAINTS:
            build(s.material, PAINTS[s.material.name], bake_dir)


def route(obj, channel):
    for s in obj.material_slots:
        nt = s.material.node_tree
        out = next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL")
        if channel == "shader":
            nt.links.new(nt.nodes["PBR"].outputs[0], out.inputs["Surface"])
        else:
            em = nt.nodes["BAKE_EMIT"]
            nt.links.new(nt.nodes["OUT_" + channel].outputs[0], em.inputs["Color"])
            nt.links.new(em.outputs[0], out.inputs["Surface"])


def use_baked(obj, tex_dir, name="railgun_baked"):
    """One material on the whole mesh with the final baked textures (what the game gets)."""
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial"); out.location = (600, 0)
    b = nt.nodes.new("ShaderNodeBsdfPrincipled"); b.location = (300, 0)
    c = _img(nt, os.path.join(tex_dir, "basecolor.png"), -400, 300, data=False)
    o = _img(nt, os.path.join(tex_dir, "orm.png"), -400, 0)
    n = _img(nt, os.path.join(tex_dir, "normal.png"), -400, -300)
    sep = nt.nodes.new("ShaderNodeSeparateColor"); sep.location = (-100, 0)
    nm = nt.nodes.new("ShaderNodeNormalMap"); nm.location = (0, -300)
    nt.links.new(c.outputs["Color"], b.inputs["Base Color"])
    nt.links.new(o.outputs["Color"], sep.inputs["Color"])
    nt.links.new(sep.outputs["Green"], b.inputs["Roughness"])
    nt.links.new(sep.outputs["Blue"], b.inputs["Metallic"])
    nt.links.new(n.outputs["Color"], nm.inputs["Color"])
    nt.links.new(nm.outputs["Normal"], b.inputs["Normal"])
    nt.links.new(b.outputs[0], out.inputs["Surface"])
    return m
