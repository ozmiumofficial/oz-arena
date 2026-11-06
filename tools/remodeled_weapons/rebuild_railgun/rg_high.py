"""The high-detail copy of the railgun rebuild, baked into the game mesh's normal map (rg_bake.py helpers).

Same parts as the game mesh (rg_parts.py run again with HI set, into the 'High' collection, names prefixed H_), with
what's too fine for the game mesh cut into it (rg_parts' HI cuts: the bulb's face seam, seams along the housing's
flanks and the stock tube's top). Patterned detail is shader bump on the high copy's own materials (only the bake
sees them): carbon weave on the face pad, stipple and a ribbed back strap on the grip, ribs across the recoil pad,
knurling on the rod's collars, a scale skin on the head's flanks round the vents.
Adapted from the rocket launcher's rl_high.py.
"""
import bpy, math
import rg_lib, rg_parts

HIGH = "High"


def _bump_material(name, base, pattern, distance):
    """A material whose surface is `base` with a bump from pattern(nt, sep) -> socket (0..1 height)."""
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    b = nt.nodes.new("ShaderNodeBsdfPrincipled")
    b.inputs["Base Color"].default_value = base
    tc = nt.nodes.new("ShaderNodeTexCoord")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(tc.outputs["Object"], sep.inputs[0])
    h = pattern(nt, sep)
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Distance"].default_value = distance
    bump.inputs["Strength"].default_value = 1.0
    nt.links.new(h, bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], b.inputs["Normal"])
    nt.links.new(b.outputs[0], out.inputs["Surface"])
    m.diffuse_color = base
    return m


def _m(nt, op, a, b=None, clamp=False):
    n = nt.nodes.new("ShaderNodeMath"); n.operation = op; n.use_clamp = clamp
    for i, v in enumerate((a, b)):
        if v is None:
            continue
        if isinstance(v, (int, float)):
            n.inputs[i].default_value = v
        else:
            nt.links.new(v, n.inputs[i])
    return n.outputs[0]


def _wave(nt, coord, period, sharp=0.55):
    """0..1 ridges: 1 on each ridge's crest, falling off to 0 between them."""
    s = _m(nt, "SINE", _m(nt, "MULTIPLY", coord, 2 * math.pi / period))
    return _m(nt, "MULTIPLY", _m(nt, "SUBTRACT", s, sharp), 1.0 / (1.0 - sharp), clamp=True)


def _noise(nt, scale, detail=2.0):
    n = nt.nodes.new("ShaderNodeTexNoise")
    n.inputs["Scale"].default_value = scale; n.inputs["Detail"].default_value = detail
    tc = nt.nodes.new("ShaderNodeTexCoord")
    nt.links.new(tc.outputs["Object"], n.inputs["Vector"])
    return n.outputs["Fac"]


def carbon(nt, sep):
    """A 2x2 twill weave on the face pad (it faces -x: the weave runs in y and z), tows 2.4 mm wide."""
    p = 0.0024
    y, z = sep.outputs["Y"], sep.outputs["Z"]
    a = _m(nt, "SINE", _m(nt, "MULTIPLY", _m(nt, "ADD", y, z), math.pi / p))
    b = _m(nt, "SINE", _m(nt, "MULTIPLY", _m(nt, "SUBTRACT", y, z), math.pi / p))
    cross = _m(nt, "GREATER_THAN", _m(nt, "MULTIPLY", a, b), 0.0)          # which tow lies on top
    along = _m(nt, "ABSOLUTE", _m(nt, "SINE", _m(nt, "MULTIPLY", y, math.pi / p)))
    across = _m(nt, "ABSOLUTE", _m(nt, "SINE", _m(nt, "MULTIPLY", z, math.pi / p)))
    return _m(nt, "ADD", _m(nt, "MULTIPLY", cross, along),
              _m(nt, "MULTIPLY", _m(nt, "SUBTRACT", 1.0, cross), across))


def grip_surface(nt, sep):
    """Stipple over the grip's side panels (ahead of the light strip), a ribbed back strap behind it."""
    x, z = sep.outputs["X"], sep.outputs["Z"]
    # the strip runs about 1.3 cm in front of the back edge: x_strip(z) ~ 0.228 - 0.42 * z above the palm swell
    behind = _m(nt, "GREATER_THAN", _m(nt, "ADD", x, _m(nt, "MULTIPLY", z, 0.42)), 0.2295)
    ribs = _wave(nt, _m(nt, "ADD", z, _m(nt, "MULTIPLY", x, 0.45)), 0.0040, 0.45)
    stip = _noise(nt, 1400.0, 2.0)
    return _m(nt, "ADD", _m(nt, "MULTIPLY", behind, _m(nt, "MULTIPLY", ribs, 0.8)),
              _m(nt, "MULTIPLY", _m(nt, "SUBTRACT", 1.0, behind), _m(nt, "MULTIPLY", stip, 0.6)))


def pad_ribs(nt, sep):
    """Ribs across the recoil pad (level, 5 mm apart)."""
    return _wave(nt, sep.outputs["Z"], 0.0050, 0.35)


def knurl(nt, sep):
    """Diamond knurling on the rod's collars (turned about the rod's axis: y 0, z 0.1614)."""
    th = _m(nt, "ARCTAN2", _m(nt, "SUBTRACT", sep.outputs["Z"], 0.1614), sep.outputs["Y"])
    arc = _m(nt, "MULTIPLY", th, 0.013)
    x = sep.outputs["X"]
    period = 2 * math.pi * 0.013 / 28
    a = _wave(nt, _m(nt, "ADD", x, arc), period, 0.3)
    b = _wave(nt, _m(nt, "SUBTRACT", x, arc), period, 0.3)
    return _m(nt, "SUBTRACT", 1.0, _m(nt, "MAXIMUM", a, b))


def scale_skin(nt, sep):
    """A cast scale texture (cells about 4 mm) on the head's flanks round the vents, smooth elsewhere."""
    v = nt.nodes.new("ShaderNodeTexVoronoi")
    v.feature = "DISTANCE_TO_EDGE"
    v.inputs["Scale"].default_value = 260.0
    tc = nt.nodes.new("ShaderNodeTexCoord")
    nt.links.new(tc.outputs["Object"], v.inputs["Vector"])
    edge = _m(nt, "MULTIPLY", v.outputs["Distance"], 12.0, clamp=True)
    ay = _m(nt, "ABSOLUTE", sep.outputs["Y"])
    zone = _m(nt, "MULTIPLY", _m(nt, "GREATER_THAN", ay, 0.080),
              _m(nt, "MULTIPLY", _m(nt, "GREATER_THAN", sep.outputs["X"], -0.478),
                 _m(nt, "LESS_THAN", sep.outputs["X"], -0.376)))
    return _m(nt, "ADD", _m(nt, "MULTIPLY", zone, edge), _m(nt, "SUBTRACT", 1.0, zone))


def build_high():
    keep = dict(rg_lib.TARGET)
    rg_lib.TARGET.update({"col": HIGH, "prefix": "H_"})
    rg_parts.HI = True
    try:
        for n in rg_parts.ALL:
            rg_parts.BUILDERS[n]()
    finally:
        rg_lib.TARGET.update(keep)
        rg_parts.HI = False
    dark = (0.05, 0.05, 0.05, 1)
    gun = (0.20, 0.21, 0.22, 1)
    mats = {
        "H_face_pad": _bump_material("hi_carbon", dark, carbon, 0.00030),
        "H_grip": _bump_material("hi_grip", dark, grip_surface, 0.00050),
        "H_butt_pad": _bump_material("hi_pad_ribs", dark, pad_ribs, 0.00070),
        "H_rod_collars": _bump_material("hi_knurl", gun, knurl, 0.00030),
        "H_head_bulb": _bump_material("hi_scale", gun, scale_skin, 0.00035),
    }
    for name, m in mats.items():
        ob = bpy.data.objects.get(name)
        if ob:
            ob.data.materials.clear(); ob.data.materials.append(m)
    col = bpy.data.collections[HIGH]
    lc = bpy.context.view_layer.layer_collection.children.get(HIGH)
    if lc:
        lc.hide_viewport = True      # out of Tony's way in the live viewport; the bake turns it on
    return col
