"""The rocket launcher's parts, measured off the Tripo reference (study renders and cross-sections, sections.py).

Each function builds one part (or a mirrored pair) into the 'Rebuild' collection; build(name) reruns one and points
the viewport at it. Units are Tripo's (the gun is 1.0 long, muzzle at x -0.5); the barrel axis is y 0, z BORE_Z.
Side parts are built on the left (-y) and mirrored. The finer detail (panel lines, ribs, knurling) is not here but
in rl_high.py, baked into the normal map.
"""
import math
from rl_lib import *

Z = BORE_Z          # barrel axis height
R_BARREL = 0.0592   # barrel outer radius (fitted on sections x -0.38 .. -0.20)
R_BORE = 0.046      # bore radius
LEFT = (0, -1, 0)
HI = False          # rl_high.py builds the high-detail copy with this set: the extra grooves below join in


def ring_groove(x0, x1, r, depth, cz=None):
    """A cutter for a ring groove round the barrel axis (or another axis height cz) from x0 to x1."""
    return prism(circle(0, Z if cz is None else cz, r + 0.01, 48), [circle(0, Z if cz is None else cz, r - depth, 48)],
                 plane="YZ", w0=x0, w1=x1)


def face_ring(x, r, width, depth, direction, cz=None):
    """A cutter for a groove ring on a face at x facing -x (direction -1) or +x (direction 1)."""
    p = circle(0, Z if cz is None else cz, r, 48)
    outer, holes = ribbon(p, width, closed=True)
    return prism(outer, holes, plane="YZ", w0=x + direction * 0.005, w1=x - direction * depth)


def seams(polys, surface, depth=0.0006, width=0.0010, both=True):
    """Cutters for panel lines on a flank at |y| = surface: each polyline a groove depth deep."""
    out = []
    for poly in polys:
        for s in ((-1, 1) if both else (-1,)):
            out.append(slab(ribbon(poly, width)[0], plane="XZ", w0=s * (surface + 0.01), w1=s * (surface - depth),
                            inset=width * 0.25))
    return out


def grooved(profile_pts, grooves, r, depth):
    """A lathe profile run along x at radius r with square ring grooves (x0, x1) cut depth deep."""
    out = []
    for x0, x1 in grooves:
        out += [(x0, r), (x0, r - depth), (x1, r - depth), (x1, r)]
    return out


# ---------------------------------------------------------------- barrel group

def barrel():
    R = R_BARREL
    # the barrel's bore is 0.6 mm inside the front collar's, so the two never share a surface (z-fighting)
    rb = R_BORE - 0.0006
    prof = ([(-0.215, 0), (-0.215, rb), (-0.470, rb), (-0.470, 0.054), (-0.392, 0.054), (-0.3865, R)] +
            # a raised knurled band, then two ring grooves near the housing
            [(-0.3460, R), (-0.3445, R + 0.0018), (-0.3225, R + 0.0018), (-0.3210, R)] +
            [(-0.020, R), (-0.020, 0)])
    cuts = [ring_groove(-0.2060, -0.2035, R, 0.0012), ring_groove(-0.1985, -0.1960, R, 0.0012),
            ring_groove(-0.3800, -0.3785, R, 0.0008)] if HI else None
    part("barrel", lathe(prof, segs=48), "gunmetal", bevel=0.0007, segments=1, cuts=cuts)
    # the bore's floor: a dark disc just in front of the barrel's end cap at x -0.215
    part("bore_floor", lathe([(-0.2168, 0), (-0.2168, rb - 0.0006), (-0.2152, rb - 0.0006), (-0.2152, 0)], segs=48),
         "dark_metal", bevel=0)
    rib = rrect(-0.0115, Z + 0.045, 0.0115, 0.2866, 0.004)
    part("barrel_rib", prism(rib, plane="YZ", w0=-0.392, w1=-0.165), "gunmetal", bevel=0.0012)


def sleeve():
    prof = ([(-0.459, 0.0525), (-0.459, 0.0548), (-0.4565, 0.0568)] +
            [(-0.4345, 0.0568), (-0.432, 0.0548), (-0.432, 0.0525)])
    lug = lambda: prism(fillet([(-0.012, Z + 0.05), (0.012, Z + 0.05), (0.012, 0.2856), (-0.012, 0.2856)],
                               [0, 0, 0.004, 0.004]), plane="YZ", w0=-0.4575, w1=-0.4335)
    cuts = [ring_groove(-0.4525, -0.4505, 0.0568, 0.0015), ring_groove(-0.4455, -0.4435, 0.0568, 0.0015)] if HI else None
    part("sleeve", join(lathe(prof, segs=48), lug()), "gunmetal", bevel=0.001, cuts=cuts)


def collar_mid():
    out = boolean(prism(circle(0, Z, 0.066, 48), plane="YZ", w0=-0.428, w1=-0.396),
                  [prism(fillet([(-0.016, Z + 0.04), (0.016, Z + 0.04), (0.016, 0.3027), (-0.016, 0.3027)],
                                [0, 0, 0.005, 0.005]), plane="YZ", w0=-0.428, w1=-0.396)], "UNION")
    out = boolean(out, [prism(circle(0, Z, 0.0535, 48), plane="YZ", w0=-0.44, w1=-0.38)])
    cuts = [face_ring(-0.428, 0.0600, 0.0016, 0.0010, -1), face_ring(-0.396, 0.0600, 0.0016, 0.0010, 1)] if HI else None
    part("collar_mid", out, "steel", bevel=0.0012, segments=1, rim=(radial_rim(0.0630), 0.0035, 3), cuts=cuts)


def collar_front():
    x0, x1 = -0.4985, -0.459
    ring = prism(circle(0, Z, 0.0685, 48), plane="YZ", w0=x0, w1=x1)
    lug = prism(fillet([(-0.0145, Z + 0.04), (0.0145, Z + 0.04), (0.0145, 0.302), (-0.0145, 0.302)], [0, 0, 0.005, 0.005]),
                plane="YZ", w0=x0, w1=x1)
    ears = prism(rrect(-0.097, 0.1975, 0.097, 0.2245, 0.007), plane="YZ", w0=x0, w1=x1 + 0.010)
    lower = prism([(-0.03, 0.165)] + arc(0.0, 0.128, 0.03, 180, 360, 12) + [(0.03, 0.165)],
                  plane="YZ", w0=x0 + 0.0015, w1=x0 + 0.0295)
    out = boolean(ring, [lug, ears], "UNION")
    out = boolean(out, [lower], "UNION")
    n = 16
    teeth = []
    for i in range(n):
        a = 2 * math.pi * (i + 0.5) / n
        c, s = math.cos(a), math.sin(a)
        w = 0.0028
        r0, r1 = R_BORE - 0.002, R_BORE + 0.0035
        teeth.append([(c * r0 - s * w, Z + s * r0 + c * w), (c * r1 - s * w, Z + s * r1 + c * w),
                      (c * r1 + s * w, Z + s * r1 - c * w), (c * r0 + s * w, Z + s * r0 - c * w)])
    cut = [prism(circle(0, Z, R_BORE, 48), plane="YZ", w0=x0 - 0.01, w1=x1 + 0.02)]
    cut += [prism(t, plane="YZ", w0=x0 - 0.01, w1=x0 + 0.012) for t in teeth]
    cut += [prism(slot(0.068, 0.089, 0.211, 0.0125), plane="YZ", w0=x0 - 0.01, w1=x1 + 0.02),
            prism(slot(-0.089, -0.068, 0.211, 0.0125), plane="YZ", w0=x0 - 0.01, w1=x1 + 0.02),
            prism(circle(0, 0.128, 0.0125, 32), plane="YZ", w0=x0 - 0.01, w1=x0 + 0.008)]
    out = boolean(out, cut)
    cuts = [face_ring(x0, 0.0585, 0.0018, 0.0012, -1), face_ring(x0, 0.0525, 0.0012, 0.0008, -1)] if HI else None
    part("collar_front", out, "steel", bevel=0.0012, segments=1, rim=(radial_rim(0.0655), 0.0045, 2), cuts=cuts)
    fan = lathe([(x0 + 0.006, 0), (x0 + 0.006, 0.0118), (x0 + 0.0095, 0.0118), (x0 + 0.0095, 0)], cz=0.128, segs=32)
    hub = lathe([(x0 + 0.003, 0), (x0 + 0.003, 0.0045), (x0 + 0.006, 0.0045), (x0 + 0.006, 0)], cz=0.128, segs=16)
    blades = []
    for i in range(8):     # the fan's blades, seen through the small tube's mouth
        a = 2 * math.pi * i / 8
        M = Matrix.Translation((x0 + 0.0060, 0, 0.128)) @ Matrix.Rotation(a, 4, "X") @ Matrix.Rotation(0.5, 4, "Z")
        blades.append(transformed(box(-0.0006, 0.0006, -0.0011, 0.0011, 0.0035, 0.0112), M))
    part("muzzle_fan", join(fan, hub, *blades), "dark_metal", bevel=0.0004)


def slotted_bar():
    x0, x1, z0, z1 = -0.426, -0.150, 0.1975, 0.2225
    outer = [(x0, z0 + 0.002), (x0 + 0.002, z0), (x1 - 0.012, z0), (x1, z0 + 0.006), (x1, z1 - 0.006), (x1 - 0.012, z1),
             (x0 + 0.002, z1), (x0, z1 - 0.002)]
    holes = []
    sz0, sz1 = 0.2055, 0.2180
    xa, xb = -0.4155, -0.2455
    n, rib = 5, 0.0045
    L = (xb - xa - (n - 1) * rib) / n
    lean = sz1 - sz0          # 45 degrees
    for i in range(n):
        a = xa + i * (L + rib)
        b = a + L
        if i == 0:
            holes.append([(a, sz0), (b - lean, sz0), (b, sz1), (a, sz1)])
        elif i == n - 1:
            holes.append([(a - lean, sz0), (b, sz0), (b, sz1), (a, sz1)])
        else:
            holes.append([(a - lean, sz0), (b - lean, sz0), (b, sz1), (a, sz1)])
    holes = [ccw(fillet(h, 0.0012, 2)) for h in holes]
    bar = prism(outer, holes, plane="XZ", w0=-0.0905, w1=-0.0545)
    # the slots sit in a recessed channel with sloped walls; a groove runs on along the bar behind them
    chan = slab(rrect(-0.4200, 0.2030, -0.2410, 0.2205, 0.002), plane="XZ", w0=-0.0950, w1=-0.0885, inset=0.0012)
    groove = slab(rrect(-0.2240, 0.2085, -0.1700, 0.2115, 0.0012), plane="XZ", w0=-0.0950, w1=-0.0892, inset=0.0005)
    bar = boolean(bar, [chan, groove])
    bar = join(bar, bolt((-0.2310, -0.0905, 0.2100), LEFT, r=0.0028))
    part("slotted_bar", bar, "steel", bevel=0.0009, mirror=True, uv_axis="y")


UNDER_SECTION = [(0.0, 0.1585), (0.0135, 0.1585), (0.0245, 0.1360), (0.0245, 0.1270), (0.0165, 0.1045), (0.0, 0.1045)]


def underbar():
    side = ([(-0.469, 0.1045), (-0.322, 0.1045)] + [(-0.322 + 0.030 * t, 0.1045 + 0.0075 * (0.5 - 0.5 * math.cos(math.pi * t)))
                                                   for t in (0.2, 0.4, 0.6, 0.8, 1.0)] +
            [(-0.165, 0.112), (-0.165, 0.1575), (-0.469, 0.1575)])
    side = fillet(side, [0.003, 0, 0, 0, 0, 0, 0, 0.003, 0.002, 0.003], 3)
    holes = [ccw(slot(-0.453, -0.330, 0.1275, 0.0110))]
    holes += [ccw(rrect(-0.248 + i * 0.0115, 0.1365, -0.248 + i * 0.0115 + 0.0065, 0.1455, 0.0012, 2)) for i in range(7)]
    b = boolean(prism(side, holes, plane="XZ", w0=-0.04, w1=0.04),
                [prism(ccw(sym(UNDER_SECTION)), plane="YZ", w0=-0.48, w1=-0.15)], "INTERSECT")
    # bolts on the lower angled faces
    n = Vector((0, 0.0225, -0.0080)).normalized()
    bolts = [bolt((x, s * 0.0222, 0.1215), (0, s * n.y, n.z), r=0.0026) for x in (-0.2760, -0.4620) for s in (-1, 1)]
    part("underbar", join(b, *bolts), "gunmetal", bevel=0.0012, uv_axis="y")


BUILDERS = {
    "barrel": barrel, "sleeve": sleeve, "collar_mid": collar_mid, "collar_front": collar_front,
    "slotted_bar": slotted_bar, "underbar": underbar,
}


def build(*names, view=(62, 0, -38), k=1.25):
    before = set(o.name for o in collection().objects)
    for n in names:
        BUILDERS[n]()
    new = [o for o in collection().objects if o.name not in before or any(o.name.startswith(n) for n in names)]
    if new:
        frame(new, view=view, dist_k=k)
    return new


# ---------------------------------------------------------------- front housing (red), around the barrel's rear

RECV_HALF = [(0.0235, 0.2970), (0.0285, 0.2955), (0.0725, 0.2565), (0.0745, 0.2525), (0.0745, 0.1765), (0.0700, 0.1720),
             (0.0545, 0.1580), (0.0440, 0.1495), (0.0395, 0.1420), (0.0320, 0.1190), (0.0255, 0.1095)]
NOTCHES = ((-0.1435, -0.1195), (-0.0575, -0.0345))
CHAMFER = (Vector((-0.0725, 0.2565)), Vector((-0.0285, 0.2955)))     # the housing's left top chamfer, (y, z)


def on_chamfer(bm_local, x, t, side=-1):
    """A shape built in the chamfer's frame (x along the gun, y up the chamfer, z out of it) placed at x and t
    (0 at the chamfer's outer edge, 1 at its top edge), on the left (side -1) or right."""
    a, b = CHAMFER
    d = (b - a).normalized(); n = Vector((-d.y, d.x))
    c = a + (b - a) * t
    M = Matrix(((1, 0, 0, x), (0, d.x, n.x, c.x), (0, d.y, n.y, c.y), (0, 0, 0, 1)))
    if side > 0:
        M = Matrix.Scale(-1, 4, (0, 1, 0)) @ M
        bm_local = transformed(bm_local, M)
        bmesh.ops.reverse_faces(bm_local, faces=bm_local.faces)
        return bm_local
    return transformed(bm_local, M)


def receiver():
    outline = ccw(sym(RECV_HALF))
    up = prism(ccw(clip(outline, 1, 0.2455, "above")), plane="YZ", w0=-0.168, w1=-0.004)
    cuts = [box(a - 0.001, b + 0.001, -0.1, 0.1, 0.2, 0.2595) for a, b in NOTCHES]
    # three triangular pockets on each top chamfer, points outward, sloped walls; a long slot behind them
    L = (CHAMFER[1] - CHAMFER[0]).length
    for side in (-1, 1):
        for xc in (-0.1425, -0.1055, -0.0685):
            tri = fillet([(-0.0155, 0.0430), (0.0155, 0.0430), (0.0, 0.0105)], 0.0025, 2)
            cuts.append(on_chamfer(slab(tri, plane="XY", w0=0.004, w1=-0.0038, inset=0.0016), xc, 0, side))
        cuts.append(on_chamfer(slab(slot(-0.0500, -0.0360, 0.0380, 0.0040), plane="XY", w0=0.004, w1=-0.0020,
                                    inset=0.0006), 0, 0, side))
    part("recv_upper", boolean(up, cuts), "red_paint", bevel=0.0018, segments=2, uv_axis="x")
    lo = prism(ccw(clip(outline, 1, 0.2125, "below")), plane="YZ", w0=-0.168, w1=0.012)
    cuts = [box(a - 0.001, b + 0.001, -0.1, 0.1, 0.1975, 0.3) for a, b in NOTCHES]
    cuts.append(prism([(-0.180, 0.2140), (-0.1680, 0.2140), (-0.1500, 0.1095), (-0.1500, 0.09), (-0.180, 0.09)], plane="XZ",
                      w0=-0.1, w1=0.1))
    step = [(-0.0095, 0.25), (-0.0095, 0.175)] + arc(0.0045, 0.175, 0.014, 180, 270, 6) + [(0.03, 0.161), (0.03, 0.25)]
    cuts.append(prism(ccw(step), plane="XZ", w0=-0.1, w1=0.1))
    for s in (-1, 1):
        cuts.append(slab(rrect(-0.0990, 0.1262, -0.0830, 0.1312, 0.0015), plane="XZ", w0=s * 0.050, w1=s * 0.0375,
                         inset=0.0006))
        # a recessed panel on the flank, its top edge stepped
        cuts.append(slab([(-0.1500, 0.1790), (-0.0420, 0.1790), (-0.0340, 0.1850), (-0.0150, 0.1850), (-0.0150, 0.1950),
                          (-0.1500, 0.1950)], plane="XZ", w0=s * 0.085, w1=s * 0.0728, inset=0.0010))
    lower = boolean(lo, cuts)
    lower = join(lower, *[bolt((-0.0010, s * 0.0402, 0.1300), (0, s, 0), r=0.0028) for s in (-1, 1)])
    part("recv_lower", lower, "red_paint", bevel=0.0018, segments=2, uv_axis="x")
    band = [(-0.163, 0.2115)]
    for a, b in NOTCHES:
        band += [(a, 0.2115), (a, 0.1985), (b, 0.1985), (b, 0.2115)]
    band += [(-0.012, 0.2115), (-0.012, 0.2465)]
    for a, b in reversed(NOTCHES):
        band += [(b, 0.2465), (b, 0.2585), (a, 0.2585), (a, 0.2465)]
    band += [(-0.163, 0.2465)]
    part("recv_band", prism(ccw(band), plane="XZ", w0=-0.0732, w1=0.0732), "gunmetal", bevel=0.001, uv_axis="y")


def lattice():
    base = slab(rrect(-0.140, 0.194, -0.014, 0.2285, 0.002), plane="XZ", w0=-0.0735, w1=-0.0812, inset=0.0012)
    blocks = []
    widths = (0.0105, 0.0105, 0.0150, 0.0105, 0.0105, 0.0150, 0.0105)
    x0 = -0.1375
    for i, w in enumerate(widths):
        for z0, z1 in ((0.2170, 0.2275), (0.1950, 0.2055)):
            blocks.append(slab(rrect(x0, z0, x0 + w, z1, 0.0), plane="XZ", w0=-0.0800, w1=-0.0862, inset=0.0012))
        x0 += w + 0.0068
    part("lattice", join(base, *blocks), "steel", bevel=0, mirror=True, uv_axis="y")


def rails():
    top = prism(ccw(sym([(0.0, 0.3035), (0.0165, 0.3035), (0.0185, 0.3015), (0.0185, 0.2985), (0.0165, 0.2965), (0.0, 0.2965)])),
                plane="YZ", w0=-0.172, w1=0.104)
    part("rail_top", top, "gunmetal", bevel=0.0008)
    teeth = [slab(rrect(-0.1595 + i * 0.0115, -0.0175, -0.1595 + i * 0.0115 + 0.0058, 0.0175, 0.0), plane="XY",
                  w0=0.3030, w1=0.3125, inset=0.0009)
             for i in range(23) if -0.1595 + i * 0.0115 + 0.0058 < 0.101]
    part("rail_top_teeth", join(*teeth), "gunmetal", bevel=0)
    bot = prism(ccw(sym([(0.0, 0.1100), (0.0165, 0.1100), (0.0185, 0.1080), (0.0185, 0.1055), (0.0165, 0.1035), (0.0, 0.1035)])),
                plane="YZ", w0=-0.1470, w1=0.013)
    part("rail_bottom", bot, "gunmetal", bevel=0.0008)
    teeth = [slab(rrect(-0.1420 + i * 0.0115, -0.0175, -0.1420 + i * 0.0115 + 0.0058, 0.0175, 0.0), plane="XY",
                  w0=0.1040, w1=0.0975, inset=0.0008)
             for i in range(14) if -0.1420 + i * 0.0115 + 0.0058 < 0.010]
    part("rail_bottom_teeth", join(*teeth), "gunmetal", bevel=0)


def windows():
    frame_ = on_chamfer(boolean(slab(rrect(-0.0090, 0.0145, 0.0090, 0.0335, 0.0015), plane="XY", w0=-0.002, w1=0.0024,
                                     inset=0.0010),
                                [box(-0.0064, 0.0064, 0.0176, 0.0304, 0.0010, 0.004)]), -0.024, 0)
    # the glass sits flush with the frame's top, well clear of the recess floor under it (no z-fighting)
    glass = on_chamfer(box(-0.0064, 0.0064, 0.0176, 0.0304, -0.0003, 0.0020), -0.024, 0)
    part("window_frame", frame_, "gunmetal", bevel=0.0004, mirror=True)
    part("window_glass", glass, "glass", bevel=0.0003, mirror=True)


def plates():
    disc = ccw(clip(clip(circle(0, 0.2170, 0.0780, 48), 0, 0.0765, "below"), 0, -0.0765, "above"))
    p1 = prism(disc, plane="YZ", w0=0.0010, w1=0.0115)
    p2 = prism(disc, plane="YZ", w0=0.0225, w1=0.0330)
    pl = join(p1, p2)
    cuts = [prism(clip(clip(circle(0, 0.2170, 0.0800, 48), 0, 0.0785, "below"), 0, -0.0785, "above"),
                  [ccw(clip(clip(circle(0, 0.2170, 0.0768, 48), 0, 0.0753, "below"), 0, -0.0753, "above"))],
                  plane="YZ", w0=w - 0.0006, w1=w + 0.0006) for w in (0.00625, 0.02775)] if HI else None
    part("plates", pl, "steel", cuts=cuts, bevel=0.0008, rim=(radial_rim(0.0700, cz=0.2170), 0.0030, 3))
    hub = lathe([(-0.012, 0), (-0.012, 0.066), (0.040, 0.066), (0.040, 0)], cz=0.2170, segs=40)
    part("plate_hub", hub, "dark_metal", bevel=0.001)


BUILDERS.update({"receiver": receiver, "lattice": lattice, "rails": rails, "windows": windows, "plates": plates})


# ---------------------------------------------------------------- the body: receiver and belly, stock, guard, grip

BODY_SIDE = ([(0.0350, 0.1470), (0.0830, 0.1470), (0.1100, 0.0760)] +
             arc(0.173, 0.101, 0.0665, 203, 322, 10)[1:] +
             [(0.2440, 0.0950), (0.2530, 0.1080), (0.2935, 0.1120), (0.2935, 0.2530), (0.2420, 0.2530),
              (0.1880, 0.2885), (0.0350, 0.2885)])
BODY_FRONT = [(0.0, 0.2895), (0.034, 0.2895), (0.058, 0.2480), (0.058, 0.1300), (0.0365, 0.0450), (0.0270, 0.0300),
              (0.0, 0.0300)]
BELLY_Z, BELLY_SLOPE = 0.1300, (0.058 - 0.0365) / (0.1300 - 0.0450)


def belly(bm, z0=None, slope=None):
    """Shear what sits on a flank below a crease (the body's, by default) inward with the angled facet under it, so
    it stays flush."""
    z0 = BELLY_Z if z0 is None else z0
    slope = BELLY_SLOPE if slope is None else slope
    for v in bm.verts:
        if v.co.z < z0:
            d = (z0 - v.co.z) * slope
            v.co.y += d if v.co.y < 0 else -d
    return bm


def body_rim(e):
    """The body's outline edges seen from the side (flank or belly facet against the rest), not the bay's floor."""
    f0, f1 = e.link_faces

    def flank(f):
        c = f.calc_center_median()
        return abs(f.normal.y) > 0.93 and (abs(c.y) > 0.0545 or c.z < BELLY_Z + 0.004)
    return flank(f0) != flank(f1) and f0.normal.dot(f1.normal) < 0.5


def crisp_creases(bm, axis=1, lo=0.9):
    """Mark the edges between two flank faces that meet at an angle (the belly's crease) sharp."""
    for e in bm.edges:
        if len(e.link_faces) == 2:
            f0, f1 = e.link_faces
            if abs(f0.normal[axis]) > lo and abs(f1.normal[axis]) > lo and f0.normal.dot(f1.normal) < 0.999:
                e.smooth = False
    return bm
BAY = [(0.0400, 0.1520), (0.0780, 0.1520), (0.1000, 0.1700), (0.1250, 0.2000), (0.1450, 0.2250), (0.1650, 0.2350),
       (0.1650, 0.2750), (0.1180, 0.2750), (0.1000, 0.2450), (0.0700, 0.2120), (0.0400, 0.2050)]
STRIP = [(0.1650, 0.2080), (0.2925, 0.2080), (0.2925, 0.2360), (0.1820, 0.2360)]
# panel lines on the body's flanks (the reference's, simplified): the front panel's back edge, the line under the
# square plate, the shoulder's diagonal, the belly frame's inner edge
BODY_SEAMS = [[(0.0985, 0.2060), (0.0985, 0.2840)],
              [(0.0360, 0.2290), (0.0490, 0.2290)],
              [(0.1660, 0.2400), (0.1890, 0.2640), (0.2380, 0.2640)],
              [(0.1180, 0.1040), (0.1230, 0.0880), (0.1370, 0.0700), (0.1580, 0.0600), (0.1880, 0.0590),
               (0.2080, 0.0660), (0.2230, 0.0810), (0.2350, 0.1020)],
              [(0.2000, 0.1450), (0.2900, 0.1450)]]
STOCK_SEAMS = [[(0.3310, 0.1250), (0.3310, 0.2470)], [(0.4475, 0.1900), (0.4475, 0.2460)],
               [(0.3000, 0.2470), (0.3290, 0.2470)], [(0.3500, 0.1520), (0.4200, 0.1820)]]


def body():
    rad = [0.002, 0.004, 0.004] + [0] * 10 + [0.006, 0.004, 0.0, 0.003, 0.008, 0.008, 0.002]
    side = prism(ccw(fillet(BODY_SIDE, rad, 3)), plane="XZ", w0=-0.07, w1=0.07)
    front = prism(ccw(sym(BODY_FRONT)), plane="YZ", w0=0.034, w1=0.2945)
    b = boolean(side, [front], "INTERSECT")
    # the cable bay: a pocket with sloped walls in each flank, floored by a dark plate
    bay = ccw(fillet(BAY, 0.003, 2))
    b = boolean(b, [slab(bay, plane="XZ", w0=-0.08, w1=-0.0520, inset=0.0025),
                    slab(bay, plane="XZ", w0=0.08, w1=0.0520, inset=0.0025)])
    part("body", crisp_creases(b), "red_paint", cuts=[belly(c) for c in seams(BODY_SEAMS, 0.058)] if HI else None, bevel=0.0015, segments=1, rim=(body_rim, 0.0045, 3))
    part("bay_plate", prism(offset_poly(bay, 0.0030), plane="XZ", w0=-0.0538, w1=-0.0500), "dark_metal", bevel=0,
         mirror=True, uv_axis="y")
    # raised panels on the flank: the long strip under the top slope, the square plate at the front
    strip = slab(fillet(STRIP, [0.002, 0.002, 0.002, 0.003], 2), plane="XZ", w0=-0.0575, w1=-0.0607, inset=0.0014)
    square = slab(rrect(0.0505, 0.2335, 0.0865, 0.2715, 0.004), plane="XZ", w0=-0.0575, w1=-0.0603, inset=0.0012)
    cuts = None
    if HI:
        cuts = [slab(rrect(x, 0.2430, x + 0.0035, 0.2620, 0.0012), plane="XZ", w0=s * 0.065, w1=s * 0.0594, inset=0.0005)
                for x in (0.0575, 0.0760) for s in (-1, 1)]
        cuts += [slab(ribbon([(0.2380, 0.2085), (0.2380, 0.2355)], 0.0010)[0], plane="XZ", w0=s * 0.07, w1=s * 0.0601,
                      inset=0.0002) for s in (-1, 1)]
    bolts = [bolt((0.1980, -0.0607, 0.2220), LEFT, r=0.0024), bolt((0.2820, -0.0607, 0.2220), LEFT, r=0.0024),
             bolt((0.1310, -0.0580, 0.1530), LEFT, r=0.0026), bolt((0.1060, -0.0580, 0.1600), LEFT, r=0.0024),
             bolt((0.0900, -0.0580, 0.2400), LEFT, r=0.0022), bolt((0.2480, -0.0580, 0.2440), LEFT, r=0.0022)]
    part("body_panels", join(strip, square, *bolts), "red_paint", bevel=0, mirror=True, uv_axis="y", cuts=cuts)


STOCK_SIDE = [(0.2925, 0.1120), (0.3150, 0.1150), (0.3440, 0.1380), (0.4200, 0.1700), (0.4400, 0.1765), (0.4500, 0.1720),
              (0.4600, 0.1640), (0.4680, 0.1530), (0.4740, 0.1420), (0.4790, 0.1350), (0.4810, 0.1350), (0.4810, 0.2795),
              (0.3300, 0.2795), (0.3080, 0.2620), (0.2925, 0.2545)]
STOCK_FRONT = fillet([(0.0, 0.2805), (0.040, 0.2805), (0.047, 0.2620), (0.047, 0.1800), (0.0330, 0.1150),
                      (0.0300, 0.1000), (0.0, 0.1000)], [0, 0.016, 0.010, 0, 0, 0, 0], 4)
STOCK_FACET = (0.1800, (0.047 - 0.0330) / (0.1800 - 0.1150))      # where the stock's facet starts, how it leans in


def stock_taper(bm):
    """The stock narrows toward the butt: 0.047 half-width at the body, 0.041 at the butt."""
    return taper(bm, along=0, a0=0.300, a1=0.485, s0=1.0, s1=0.875, axis=1)


def stock():
    rad = [0, 0.003, 0.004, 0.004, 0.003, 0, 0, 0, 0, 0.002, 0, 0.002, 0.006, 0.004, 0]
    side = prism(ccw(fillet(STOCK_SIDE, rad)), plane="XZ", w0=-0.06, w1=0.06)
    front = prism(ccw(sym(STOCK_FRONT)), plane="YZ", w0=0.29, w1=0.49)
    st = boolean(side, [front], "INTERSECT")
    cuts = [stock_taper(belly(c, *STOCK_FACET)) for c in seams(STOCK_SEAMS, 0.047)] if HI else None
    part("stock", crisp_creases(stock_taper(st)), "red_paint", cuts=cuts, bevel=0.0015, segments=1, rim=(side_silhouette(0.0400), 0.0040, 3))
    pad_side = [(0.3285, 0.2550), (0.4470, 0.2550), (0.4650, 0.2815), (0.3285, 0.2815)]
    pad_front = fillet([(0.0, 0.2815), (0.0405, 0.2815), (0.0478, 0.2635), (0.0478, 0.2540), (0.0, 0.2540)],
                       [0, 0.016, 0.004, 0, 0], 4)
    pad = boolean(prism(ccw(fillet(pad_side, [0.002, 0.002, 0.003, 0.003])), plane="XZ", w0=-0.06, w1=0.06),
                  [prism(ccw(sym(pad_front)), plane="YZ", w0=0.32, w1=0.47)], "INTERSECT")
    part("stock_pad", stock_taper(pad), "rubber", bevel=0.0018, segments=2)
    butt = [(0.4790, 0.1360), (0.4930, 0.1380), (0.4980, 0.1500), (0.4990, 0.1800), (0.4970, 0.1850), (0.4970, 0.2650),
            (0.4930, 0.2750), (0.4880, 0.2795), (0.4790, 0.2795)]
    butt_front = fillet([(0.0, 0.2810), (0.041, 0.2810), (0.0485, 0.2620), (0.0485, 0.1800), (0.0360, 0.1300),
                         (0.0, 0.1300)], [0, 0.016, 0.010, 0, 0, 0], 4)
    bp = boolean(prism(ccw(fillet(butt, 0.002, 2)), plane="XZ", w0=-0.06, w1=0.06),
                 [prism(ccw(sym(butt_front)), plane="YZ", w0=0.47, w1=0.51)], "INTERSECT")
    # the butt's end: a round socket, a long slot, a small square, and a seam round the side
    ends = [slab(circle(0, 0.2560, 0.0085, 24), plane="YZ", w0=0.51, w1=0.4935, inset=0.0012),
            slab(rrect(-0.0045, 0.1720, 0.0045, 0.2360, 0.0030), plane="YZ", w0=0.51, w1=0.4935, inset=0.0010),
            slab(rrect(-0.0060, 0.1480, 0.0060, 0.1580, 0.0012), plane="YZ", w0=0.51, w1=0.4950, inset=0.0008)]
    bp = boolean(bp, ends)
    part("butt_plate", stock_taper(bp), "steel", bevel=0.0018, segments=2)


MAGAZINE = [(0.2420, 0.1150), (0.3160, 0.1150), (0.3480, 0.0470), (0.3440, 0.0080), (0.2860, 0.0090)]
MAG_FRONT = ((0.2420, 0.1150), (0.2860, 0.0090))      # the magazine's slant, front edge top to bottom
MAG_BACK = ((0.3160, 0.1150), (0.3440, 0.0080))
GUARD = [(0.3300, 0.0012), (0.3720, 0.0062), (0.3920, 0.0140), (0.4045, 0.0230), (0.4065, 0.0420), (0.3950, 0.0425),
         (0.3720, 0.0368), (0.3570, 0.0385), (0.3480, 0.0470), (0.3350, 0.0450), (0.3300, 0.0200)]
MAG_PANEL = [(0.2600, 0.0930), (0.3010, 0.0930), (0.3215, 0.0420), (0.3150, 0.0170), (0.2890, 0.0170)]
# the grip, measured on the reference (study/g_grip.jpg): its front and back edges as x at heights z, and how wide
# it is toward the back (palm) and the front (fingers)
GRIP_FRONT = [(0.030, 0.3965), (0.040, 0.3935), (0.054, 0.3926), (0.070, 0.3877), (0.087, 0.3811), (0.103, 0.3778),
              (0.116, 0.3746), (0.130, 0.3690), (0.141, 0.3640), (0.165, 0.3560)]
GRIP_BACK = [(0.030, 0.4560), (0.060, 0.4543), (0.067, 0.4518), (0.093, 0.4370), (0.111, 0.4272), (0.119, 0.4189),
             (0.131, 0.4107), (0.141, 0.4091), (0.150, 0.4099), (0.159, 0.4124), (0.165, 0.4157)]
GRIP_HALF_BACK = [(0.030, 0.0215), (0.050, 0.0225), (0.070, 0.0258), (0.095, 0.0265), (0.115, 0.0255), (0.135, 0.0242),
                  (0.165, 0.0240)]
GRIP_HALF_FRONT = [(0.030, 0.0190), (0.060, 0.0200), (0.100, 0.0205), (0.135, 0.0202), (0.165, 0.0215)]


def interp(table, z):
    """Smooth interpolation in a (z, value) table: monotone cubic (Fritsch-Carlson), so the curve passes through every
    measured point without flattening at them (which rippled the grip) and without overshooting between them."""
    zs = [a for a, _ in table]; vs = [b for _, b in table]
    if z <= zs[0]:
        return vs[0]
    if z >= zs[-1]:
        return vs[-1]
    n = len(zs)
    d = [(vs[i + 1] - vs[i]) / (zs[i + 1] - zs[i]) for i in range(n - 1)]
    m = [d[0]] + [0.0 if d[i - 1] * d[i] <= 0 else
                  3 * (zs[i + 1] - zs[i - 1]) / ((2 * zs[i + 1] - zs[i] - zs[i - 1]) / d[i - 1] +
                                                (zs[i + 1] + zs[i] - 2 * zs[i - 1]) / d[i])
                  for i in range(1, n - 1)] + [d[-1]]
    for i in range(n - 1):
        if z <= zs[i + 1]:
            h = zs[i + 1] - zs[i]; t = (z - zs[i]) / h
            return ((2 * t ** 3 - 3 * t ** 2 + 1) * vs[i] + (t ** 3 - 2 * t ** 2 + t) * h * m[i] +
                    (-2 * t ** 3 + 3 * t ** 2) * vs[i + 1] + (t ** 3 - t ** 2) * h * m[i + 1])
    return vs[-1]


def grip_loft(n=28, n_exp=2.8, thumb=True):
    """The grip as a loft of rounded horizontal sections: egg-shaped (fuller at the palm), its front and back edges
    following the reference, a palm swell low on the back, and a shallow thumb rest scooped into the left flank
    near the top (a right hand's thumb lies there)."""
    bm = bmesh.new()
    rings = []
    zs = [0.030 + 0.0045 * i for i in range(31)]
    for z in zs:
        xf, xb = interp(GRIP_FRONT, z), interp(GRIP_BACK, z)
        xc, a = (xf + xb) / 2, (xb - xf) / 2
        hb, hf = interp(GRIP_HALF_BACK, z), interp(GRIP_HALF_FRONT, z)
        ring = []
        for i in range(n):
            th = 2 * math.pi * i / n
            c, s_ = math.cos(th), math.sin(th)
            x = xc + a * math.copysign(abs(c) ** (2 / n_exp), c)
            half = hf + (hb - hf) * (0.5 + 0.5 * c)
            y = half * math.copysign(abs(s_) ** (2 / n_exp), s_)
            if thumb and y < 0:
                # the thumb rest: a smooth dish, 2.5 mm deep at its middle
                d = ((x - 0.3930) / 0.0200) ** 2 + ((z - 0.1280) / 0.0140) ** 2
                if d < 1:
                    y += 0.0026 * (1 - d) ** 2 * abs(s_)
            ring.append(bm.verts.new((x, y, z)))
        rings.append(ring)
    for A, B in zip(rings, rings[1:]):
        for i in range(n):
            bm.faces.new((A[i], A[(i + 1) % n], B[(i + 1) % n], B[i]))
    bm.faces.new(rings[0][::-1]); bm.faces.new(rings[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return bm


def sweep(path, half_w, half_d, n=16, n_exp=3.0):
    """A blade swept along a path in the x-z plane: rounded-rectangle sections half_d(t) deep (in the plane) and
    half_w(t) wide (along y), t running 0..1 along the path; capped."""
    P = [Vector((p[0], 0.0, p[1])) if len(p) == 2 else Vector(p) for p in path]
    bm = bmesh.new()
    rings = []
    Y = Vector((0, 1, 0))
    for k, p in enumerate(P):
        t = k / (len(P) - 1)
        T = (P[min(k + 1, len(P) - 1)] - P[max(k - 1, 0)]).normalized()
        N = T.cross(Y).normalized()
        ring = []
        for i in range(n):
            th = 2 * math.pi * i / n
            c, s_ = math.cos(th), math.sin(th)
            u = half_d(t) * math.copysign(abs(c) ** (2 / n_exp), c)
            v = half_w(t) * math.copysign(abs(s_) ** (2 / n_exp), s_)
            ring.append(bm.verts.new(p + N * u + Y * v))
        rings.append(ring)
    for A, B in zip(rings, rings[1:]):
        for i in range(n):
            bm.faces.new((A[i], A[(i + 1) % n], B[(i + 1) % n], B[i]))
    bm.faces.new(rings[0][::-1]); bm.faces.new(rings[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return bm


TRIGGER = [(0.3565, 0.1440), (0.3574, 0.1340), (0.3570, 0.1255), (0.3552, 0.1172), (0.3520, 0.1100), (0.3478, 0.1043),
           (0.3430, 0.1003), (0.3388, 0.0988)]
GUARD_PATH = [(0.3330, 0.0185), (0.3520, 0.0172), (0.3700, 0.0188), (0.3860, 0.0232), (0.3980, 0.0298), (0.4065, 0.0370)]


def along(seg, t):
    (x0, z0), (x1, z1) = seg
    return (x0 + (x1 - x0) * t, z0 + (z1 - z0) * t)


def mag_taper(bm):
    """The magazine narrows toward its foot: full width at the magwell, 0.88 of it at the floor plate."""
    return taper(bm, along=2, a0=0.010, a1=0.110, s0=0.88, s1=1.0, axis=1)


def guard_grip():
    # the magazine: seated in the magwell, narrowing to its foot, a recessed window along its slant with three raised
    # grip ribs in it, a witness slot above, and a gunmetal floor plate
    mag = prism(ccw(fillet(MAGAZINE, [0, 0, 0.008, 0.006, 0.008], 3)), plane="XZ", w0=-0.0235, w1=0.0235)
    win = [along(MAG_FRONT, 0.22), along(MAG_BACK, 0.22), along(MAG_BACK, 0.80), along(MAG_FRONT, 0.80)]
    win = [(x + dx, z) for (x, z), dx in zip(win, (0.0085, -0.0085, -0.0085, 0.0085))]
    cuts = [slab(fillet(win, 0.003, 2), plane="XZ", w0=s * 0.03, w1=s * 0.0215, inset=0.0012) for s in (-1, 1)]
    wit = [along(MAG_FRONT, 0.13), along(MAG_BACK, 0.13)]
    cuts += [slab(slot(wit[0][0] + 0.020, wit[1][0] - 0.020, wit[0][1], 0.0045), plane="XZ", w0=s * 0.03,
                  w1=s * 0.0205, inset=0.0008) for s in (-1, 1)]
    mag = boolean(mag, cuts)
    ribs = []
    for t in (0.36, 0.51, 0.66):
        a_, b_ = along(MAG_FRONT, t), along(MAG_BACK, t)
        rib = [(a_[0] + 0.0115, a_[1] - 0.0016), (b_[0] - 0.0115, b_[1] - 0.0016), (b_[0] - 0.0115, b_[1] + 0.0016),
               (a_[0] + 0.0115, a_[1] + 0.0016)]
        ribs += [slab(rib, plane="XZ", w0=s * 0.0220, w1=s * 0.0238, inset=0.0006) for s in (-1, 1)]
    part("magazine", mag_taper(join(mag, *ribs)), "red_paint", bevel=0.0012, rim=(side_silhouette(0.0225), 0.0040, 3))
    floor_ = [(0.2835, 0.0145), (0.3455, 0.0135), (0.3470, 0.0020), (0.2900, 0.0020), (0.2815, 0.0080)]
    part("mag_floor", mag_taper(prism(ccw(fillet(floor_, [0.002, 0.002, 0.003, 0.003, 0.003], 2)), plane="XZ",
                                       w0=-0.0250, w1=0.0250)), "gunmetal", bevel=0.0010,
         rim=(side_silhouette(0.0200), 0.0022, 2))
    # the magwell: a red collar round the magazine's top, part of the body
    well_out = [(0.2370, 0.1200), (0.3220, 0.1200), (0.3310, 0.0950), (0.2470, 0.0950)]
    well = prism(ccw(fillet(well_out, [0, 0, 0.003, 0.003], 2)), plane="XZ", w0=-0.0272, w1=0.0272)
    part("magwell", well, "red_paint", bevel=0.0012, rim=(side_silhouette(0.0260), 0.0025, 2))
    # the trigger guard: a bar swept under the trigger from the magazine's foot to the grip's base, thick at the
    # magazine and slimmer toward the grip; a filleted web blends it into the magazine's back edge
    gb = sweep(GUARD_PATH, lambda t: 0.0175 - 0.0020 * t, lambda t: 0.0168 - 0.0040 * t, n=20, n_exp=3.2)
    web = ([(0.3420, 0.0270), (0.3660, 0.0290)] + arc(0.3660, 0.0470, 0.0180, 270, 180, 6)[1:] + [(0.3420, 0.0470)])
    gw = prism(ccw(web), plane="XZ", w0=-0.0165, w1=0.0165)
    part("guard", join(gb, gw), "red_paint", bevel=0.0012, angle=50)
    # the grip, lofted; its base plate and screw
    # the grip ends inside its base plate: cut along the plate's slanted middle so nothing shows below it
    keep = [(0.370, 0.0250), (0.470, 0.0482), (0.470, 0.2000), (0.370, 0.2000)]
    gr = boolean(grip_loft(), [prism(keep, plane="XZ", w0=-0.05, w1=0.05)], "INTERSECT")
    part("grip", gr, "rubber", bevel=0, angle=60, uv_axis="z")
    base = [(0.3910, 0.0400), (0.3985, 0.0225), (0.4060, 0.0200), (0.4370, 0.0300), (0.4510, 0.0375), (0.4565, 0.0450),
            (0.4560, 0.0520), (0.4400, 0.0480), (0.4000, 0.0440)]
    basep = prism(ccw(fillet(base, [0.003, 0.004, 0.004, 0.006, 0.004, 0.003, 0.002, 0, 0.002], 3)), plane="XZ",
                  w0=-0.0262, w1=0.0262)
    screws = [bolt((0.4206, s * 0.0262, 0.0488), (0, s, 0), r=0.0034, sides=12) for s in (-1, 1)]
    part("grip_base", join(basep, *screws), "gunmetal", bevel=0.0010, uv_axis="y",
         rim=(side_silhouette(0.0255), 0.0030, 2))
    # the trigger: a curved blade, thick at its pivot, a wider shoe where the finger pulls, about 4 cm of it showing
    part("trigger", sweep(TRIGGER, lambda t: 0.0040 + 0.0016 * math.sin(math.pi * min(1.0, t * 1.25)),
                          lambda t: 0.0040 - 0.0016 * t, n=16), "gunmetal", bevel=0, angle=60, uv_axis="none")
    # the trigger well's lip under the body
    lip = slab(rrect(0.3440, 0.1340, 0.3700, 0.1520, 0.0040), plane="XZ", w0=-0.0105, w1=0.0105, inset=0.0)
    part("trigger_house", lip, "dark_metal", bevel=0.0010, rim=(side_silhouette(0.0100), 0.0030, 2))


def armor():
    vent = [(0.1530, 0.1340), (0.2950, 0.1320), (0.3070, 0.1680), (0.3050, 0.2060), (0.2330, 0.2060), (0.2170, 0.1930),
            (0.1790, 0.1930)]
    v = slab(fillet(vent, 0.003, 2), plane="XZ", w0=-0.0560, w1=-0.0648, inset=0.0026)
    slots = [slab(slot(0.2480, 0.2830, zc, 0.0062), plane="XZ", w0=-0.08, w1=-0.0608, inset=0.0010)
             for zc in (0.1815, 0.1910, 0.2005)]
    latch = slab(rrect(0.2665, 0.1385, 0.2885, 0.1450, 0.0010), plane="XZ", w0=-0.0640, w1=-0.0668, inset=0.0008)
    vb = [bolt((0.2000, -0.0648, 0.1850), LEFT, r=0.0024), bolt((0.2960, -0.0648, 0.1880), LEFT, r=0.0024),
          bolt((0.1680, -0.0648, 0.1420), LEFT, r=0.0022)]
    part("armor_vent", join(boolean(v, slots), latch, *vb), "gunmetal", bevel=0, mirror=True, uv_axis="y")
    low = [(0.1460, 0.0570), (0.1850, 0.0555), (0.2120, 0.0840), (0.1930, 0.1040), (0.1290, 0.1040), (0.1290, 0.0780)]
    lo = slab(fillet(low, 0.004, 2), plane="XZ", w0=-0.0560, w1=-0.0642, inset=0.0026)
    part("armor_low", belly(join(lo, bolt((0.1520, -0.0642, 0.0920), LEFT, r=0.0030))), "steel", bevel=0, mirror=True,
         uv_axis="y")
    mid = [(0.1240, 0.1060), (0.2050, 0.1060), (0.2280, 0.1130), (0.2440, 0.1265), (0.2920, 0.1265), (0.2960, 0.1330),
           (0.1530, 0.1390), (0.1230, 0.1390), (0.1130, 0.1180)]
    part("armor_mid", belly(slab(fillet(mid, 0.002, 2), plane="XZ", w0=-0.0555, w1=-0.0615, inset=0.0016)), "dark_metal",
         bevel=0, mirror=True, uv_axis="y")
    sp = [(0.3900, 0.2050), (0.4040, 0.1840), (0.4440, 0.2030), (0.4440, 0.2340), (0.4360, 0.2370), (0.3950, 0.2370),
          (0.3880, 0.2290)]
    a = slab(fillet(sp, 0.003, 2), plane="XZ", w0=-0.0455, w1=-0.0525, inset=0.0020)
    b = slab(rrect(0.4510, 0.2010, 0.4740, 0.2360, 0.003), plane="XZ", w0=-0.0455, w1=-0.0518, inset=0.0018)
    part("stock_plates", stock_taper(join(a, b)), "steel", bevel=0, mirror=True, uv_axis="y")
    btn = [slab(circle(0.3660, 0.2170, 0.0118, 24), plane="XZ", w0=-0.0455, w1=-0.0552, inset=0.0016),
           slab(circle(0.3790, 0.1960, 0.0110, 24), plane="XZ", w0=-0.0455, w1=-0.0547, inset=0.0016)]
    # each button's face is dished
    btn = [boolean(btn[0], [slab(circle(0.3660, 0.2170, 0.0072, 24), plane="XZ", w0=-0.06, w1=-0.0542, inset=0.0012)]),
           boolean(btn[1], [slab(circle(0.3790, 0.1960, 0.0066, 24), plane="XZ", w0=-0.06, w1=-0.0537, inset=0.0012)])]
    part("buttons", stock_taper(join(*btn)), "steel", bevel=0, mirror=True, uv_axis="y")


def top_parts():
    cover = prism(ccw(sym([(0.0, 0.2965), (0.026, 0.2965), (0.034, 0.2880), (0.0, 0.2880)])), plane="YZ", w0=0.030, w1=0.104)
    part("top_cover", cover, "gunmetal", bevel=0.0010)
    hs = [(0.1000, 0.2870), (0.1930, 0.2870), (0.1900, 0.2990), (0.1700, 0.3120), (0.1600, 0.3155), (0.1180, 0.3155),
          (0.1000, 0.2960)]
    hump = boolean(prism(ccw(fillet(hs, [0, 0, 0.004, 0.006, 0.006, 0.008, 0.002], 3)), plane="XZ", w0=-0.05, w1=0.05),
                   [prism(ccw(sym([(0.0, 0.3165), (0.020, 0.3165), (0.035, 0.2860), (0.0, 0.2860)])), plane="YZ",
                          w0=0.09, w1=0.2)], "INTERSECT")
    # two vents along its top
    hump = boolean(hump, [slab(slot(0.1250, 0.1520, y, 0.0040), plane="XY", w0=0.330, w1=0.3135, inset=0.0005)
                          for y in (-0.0080, 0.0080)])
    part("hump", hump, "gunmetal", bevel=0.0018, segments=2)
    rb = [(0.2400, 0.2525), (0.2950, 0.2525), (0.2950, 0.2700), (0.3150, 0.2700), (0.3150, 0.2785), (0.2330, 0.2785),
          (0.2120, 0.2965), (0.2040, 0.2965)]
    part("rear_block", prism(ccw(fillet(rb, 0.002, 2)), plane="XZ", w0=-0.030, w1=0.030), "gunmetal", bevel=0.0012)
    cuts = [slab(rrect(x, 0.1340, x + 0.0060, 0.1430, 0.0010), plane="XZ", w0=s * 0.03, w1=s * 0.0235, inset=0.0005)
            for x in (0.0420, 0.0530, 0.0640) for s in (-1, 1)] if HI else None
    part("front_block", box(0.0340, 0.0810, -0.025, 0.025, 0.1295, 0.1480), "gunmetal", bevel=0.0010, cuts=cuts)


BUILDERS.update({"body": body, "stock": stock, "guard_grip": guard_grip, "armor": armor, "top_parts": top_parts})


# ---------------------------------------------------------------- cables and the bay's fittings

def smooth_path(pts, k=4):
    """Catmull-Rom through the points, k samples per span."""
    P = [Vector(p) for p in pts]
    P = [P[0] * 2 - P[1]] + P + [P[-1] * 2 - P[-2]]
    out = []
    for i in range(1, len(P) - 2):
        p0, p1, p2, p3 = P[i - 1], P[i], P[i + 1], P[i + 2]
        for j in range(k):
            t = j / k
            out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t +
                              (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t))
    out.append(P[-2])
    return [tuple(p) for p in out]


def cylinder(a, b, r, segs=16):
    """A capped cylinder from point a to point b."""
    a, b = Vector(a), Vector(b)
    L = (b - a).length
    bm = lathe([(0, 0), (0, r), (L, r), (L, 0)], cy=0, cz=0, segs=segs)
    q = Vector((1, 0, 0)).rotation_difference((b - a).normalized())
    M = Matrix.Translation(a) @ q.to_matrix().to_4x4()
    return transformed(bm, M)


SIDE_CABLE = [(0.036, -0.0505, 0.1955), (0.056, -0.0650, 0.2005), (0.078, -0.0790, 0.2115), (0.100, -0.0840, 0.2270),
              (0.118, -0.0810, 0.2420), (0.130, -0.0760, 0.2520)]
SIDE_CABLE_2 = [(0.040, -0.0510, 0.1885), (0.060, -0.0660, 0.1925), (0.083, -0.0805, 0.2035), (0.106, -0.0855, 0.2185),
                (0.124, -0.0830, 0.2345), (0.136, -0.0780, 0.2455)]
TOP_CABLE = [(0.140, 0.010, 0.3105), (0.160, 0.016, 0.3130), (0.180, 0.026, 0.3100), (0.198, 0.030, 0.3010),
             (0.215, 0.030, 0.2880), (0.232, 0.032, 0.2740), (0.252, 0.036, 0.2640), (0.275, 0.040, 0.2580),
             (0.300, 0.042, 0.2560)]


def cables():
    side = smooth_path(SIDE_CABLE, 4)
    part("side_cable", join(tube(side, 0.0066, 12), tube(smooth_path(SIDE_CABLE_2, 4), 0.0046, 10)), "rubber", bevel=0,
         mirror=True, uv_axis="none")
    sl = cylinder((0.0440, -0.0565, 0.1975), (0.0600, -0.0685, 0.2025), 0.0095, 16)
    con = cylinder((0.1240, -0.0790, 0.2470), (0.1520, -0.0690, 0.2650), 0.0105, 16)
    ring = cylinder((0.1300, -0.0770, 0.2510), (0.1340, -0.0755, 0.2535), 0.0120, 16)
    nut = cylinder((0.1500, -0.0697, 0.2637), (0.1555, -0.0677, 0.2672), 0.0088, 6)
    # clamps round both hoses, a third and two thirds along
    clamps = []
    for k in (len(side) // 3, 2 * len(side) // 3):
        a, b = Vector(side[k - 1]), Vector(side[k + 1])
        mid_ = (a + b) / 2 + Vector((0.002, -0.001, -0.0035))
        dvec = (b - a).normalized() * 0.0028
        clamps.append(cylinder(mid_ - dvec, mid_ + dvec, 0.0115, 16))
    part("cable_fittings", join(sl, con, ring, nut, *clamps), "steel", bevel=0, angle=35, mirror=True)
    top = smooth_path(TOP_CABLE, 4)
    tc = [None]
    for k in (6, 18):
        a, b = Vector(top[k - 1]), Vector(top[k + 1])
        dvec = (b - a).normalized() * 0.0025
        tc.append(cylinder(Vector(top[k]) - dvec, Vector(top[k]) + dvec, 0.0070, 12))
    part("top_cables", tube(top, 0.0052, 10), "rubber", bevel=0, mirror=True, uv_axis="none")
    part("top_cable_clamps", join(*tc[1:]), "steel", bevel=0, angle=35, mirror=True)
    sw = [(0.0410, 0.1570), (0.0650, 0.1560), (0.0760, 0.1680), (0.0770, 0.1850), (0.0830, 0.1970), (0.0800, 0.2030),
          (0.0700, 0.2010), (0.0570, 0.1940), (0.0410, 0.1970)]
    br = [(0.0870, 0.1900), (0.1120, 0.1960), (0.1240, 0.2020), (0.1290, 0.2100), (0.1290, 0.2230), (0.1150, 0.2210),
          (0.1020, 0.2070), (0.0880, 0.1980)]
    so = [(0.1180, 0.2400), (0.1500, 0.2330), (0.1640, 0.2400), (0.1640, 0.2720), (0.1250, 0.2720)]
    plates_ = [slab(fillet(p, 0.002, 2), plane="XZ", w0=-0.0530, w1=-0.0592, inset=0.0012) for p in (sw, br, so)]
    # the swivel's round pivot and the socket's ring
    piv = slab(circle(0.0640, 0.1720, 0.0075, 16), plane="XZ", w0=-0.0590, w1=-0.0625, inset=0.0012)
    sock = boolean(slab(circle(0.1520, 0.2580, 0.0080, 16), plane="XZ", w0=-0.0590, w1=-0.0622, inset=0.0010),
                   [slab(circle(0.1520, 0.2580, 0.0045, 12), plane="XZ", w0=-0.07, w1=-0.0605, inset=0.0008)])
    fb = [bolt((0.1030, -0.0592, 0.2010), LEFT, r=0.0021), bolt((0.1220, -0.0592, 0.2150), LEFT, r=0.0021)]
    part("bay_fittings", join(*plates_, piv, sock, *fb), "steel", bevel=0, mirror=True, uv_axis="y")


BUILDERS.update({"cables": cables})

ALL = ("barrel", "sleeve", "collar_mid", "collar_front", "slotted_bar", "underbar", "receiver", "lattice", "rails",
       "windows", "plates", "body", "stock", "guard_grip", "armor", "top_parts", "cables")
