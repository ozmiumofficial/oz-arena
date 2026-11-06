"""The high-detail copy of the rebuild, baked into the game mesh's normal map (rl_bake.py normal).

Same parts as the game mesh (rl_parts.py run again with HI set, into the 'High' collection, names prefixed H_), with
what's too fine for the game mesh cut into it: the barrel's and sleeve's ring grooves, the collars' face rings, the
disc plates' rim grooves, panel seams on the body and stock flanks, slots in the square plate and the front block.
Patterned detail is shader bump on the high copy's own materials (only the bake sees them): diamond knurling on the
barrel (coarse on its raised band), ribs along the grip's side panels, ribs across the stock pad, a fine stipple on
the grip and pad rubber.
"""
import bpy, math
import rl_lib, rl_parts

HIGH = "High"


def _bump_material(name, base, pattern, distance):
    """A material whose surface is `base` with a bump from pattern(nt, P) -> socket (0..1 height)."""
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


def knurl(nt, sep):
    """Diamond knurling on the barrel: two helices of grooves crossing, coarser and deeper on the raised band."""
    th = _m(nt, "ARCTAN2", _m(nt, "SUBTRACT", sep.outputs["Z"], rl_lib.BORE_Z), sep.outputs["Y"])
    arc = _m(nt, "MULTIPLY", th, rl_parts.R_BARREL)
    x = sep.outputs["X"]
    fine_p = 2 * math.pi * rl_parts.R_BARREL / 124      # a whole number of grooves round the barrel: no seam
    coarse_p = 2 * math.pi * rl_parts.R_BARREL / 89
    a = _wave(nt, _m(nt, "ADD", x, arc), fine_p, 0.3)
    b = _wave(nt, _m(nt, "SUBTRACT", x, arc), fine_p, 0.3)
    fine = _m(nt, "MULTIPLY", _m(nt, "MAXIMUM", a, b), 0.35)
    a2 = _wave(nt, _m(nt, "ADD", x, arc), coarse_p, 0.2)
    b2 = _wave(nt, _m(nt, "SUBTRACT", x, arc), coarse_p, 0.2)
    coarse = _m(nt, "MAXIMUM", a2, b2)
    band = _m(nt, "MULTIPLY", _m(nt, "GREATER_THAN", x, -0.3440), _m(nt, "LESS_THAN", x, -0.3230))
    return _m(nt, "SUBTRACT", 1.0, _m(nt, "ADD", _m(nt, "MULTIPLY", band, coarse),
                                       _m(nt, "MULTIPLY", _m(nt, "SUBTRACT", 1.0, band), fine)))


def grip_ribs(nt, sep):
    """Ribs running down the grip's side panels (along the grip's slant), 3.5 mm apart."""
    across = _m(nt, "ADD", _m(nt, "MULTIPLY", sep.outputs["X"], 0.94), _m(nt, "MULTIPLY", sep.outputs["Z"], 0.33))
    return _wave(nt, across, 0.0035, 0.45)


def pad_ribs(nt, sep):
    """Ribs across the stock pad, leaning back a little, 4.5 mm apart."""
    across = _m(nt, "ADD", sep.outputs["X"], _m(nt, "MULTIPLY", sep.outputs["Z"], 0.35))
    return _wave(nt, across, 0.0045, 0.35)


def grip_surface(nt, sep):
    """The grip: ribs along its slant on the flanks' middle (where the reference has its ribbed panels), a fine
    stipple everywhere."""
    ribs = grip_ribs(nt, sep)
    ay = _m(nt, "ABSOLUTE", sep.outputs["Y"])
    side = _m(nt, "MULTIPLY", _m(nt, "SUBTRACT", ay, 0.0140), 1.0 / 0.0045, clamp=True)
    z = sep.outputs["Z"]
    band = _m(nt, "MULTIPLY", _m(nt, "GREATER_THAN", z, 0.0500), _m(nt, "LESS_THAN", z, 0.1220))
    mask = _m(nt, "MULTIPLY", side, band)
    return _m(nt, "ADD", _m(nt, "MULTIPLY", ribs, _m(nt, "MULTIPLY", mask, 0.8)),
              _m(nt, "MULTIPLY", stipple(nt, sep), 0.25))


def stipple(nt, sep):
    n = nt.nodes.new("ShaderNodeTexNoise")
    n.inputs["Scale"].default_value = 900.0; n.inputs["Detail"].default_value = 2.0
    tc = nt.nodes.new("ShaderNodeTexCoord")
    nt.links.new(tc.outputs["Object"], n.inputs["Vector"])
    return n.outputs["Fac"]


def build_high():
    keep = dict(rl_lib.TARGET)
    rl_lib.TARGET.update({"col": HIGH, "prefix": "H_"})
    rl_parts.HI = True
    try:
        for n in rl_parts.ALL:
            rl_parts.BUILDERS[n]()
    finally:
        rl_lib.TARGET.update(keep)
        rl_parts.HI = False
    gun = (0.25, 0.26, 0.28, 1)
    mats = {
        "H_barrel": _bump_material("hi_knurl", gun, knurl, 0.00035),
        "H_stock_pad": _bump_material("hi_pad_ribs", (0.05, 0.05, 0.05, 1), pad_ribs, 0.0007),
        "H_grip": _bump_material("hi_grip", (0.05, 0.05, 0.05, 1), grip_surface, 0.0006),
    }
    for name, m in mats.items():
        ob = bpy.data.objects.get(name)
        if ob:
            ob.data.materials.clear(); ob.data.materials.append(m)
    col = bpy.data.collections[HIGH]
    return col
