"""The railgun's parts, measured off the Tripo reference (study.py renders, profile.py sections; PARTS.md lists
what each part is, how a real one is built, its details and joints).

Each function builds one part (or a group) into the 'Rebuild' collection; build(name) reruns one and points the
viewport at it. Units are Tripo's in the work frame (x -0.5 muzzle .. 0.5 butt, y the gun's right, z up). Side
parts are built on the left (-y) and mirrored. Tertiary detail (seams, knurl, stipple, weave) is not here but in
rg_high.py, baked into the normal map; HI adds the grooves that only the high copy gets.
"""
import math
import bmesh
from rg_lib import *

HI = False
LEFT = (0, -1, 0)

# ---------------------------------------------------------------- shared sections (half outlines, y >= 0, top down)

# the roof every long part shares: domed top, walls, the flange groove at z 0.300 .. 0.307, the flange below
ROOF = [(0.0, 0.3818), (0.0105, 0.3812), (0.0305, 0.3764), (0.0398, 0.3718), (0.0476, 0.3652), (0.0593, 0.3516),
        (0.0662, 0.3394), (0.0667, 0.3070), (0.0622, 0.3036), (0.0622, 0.3006), (0.0647, 0.2990)]
HOUSING_HALF = ROOF + [(0.0647, 0.2891), (0.0564, 0.2822), (0.0, 0.2822)]
# the emitter head's bell, at its widest (x -0.45): key points of a smooth outline
BELL_HALF = [(0.0, 0.3770), (0.0200, 0.3732), (0.0380, 0.3635), (0.0515, 0.3505), (0.0598, 0.3375),
             (0.0624, 0.3200), (0.0636, 0.2960), (0.0690, 0.2815), (0.0840, 0.2635), (0.0985, 0.2475),
             (0.1060, 0.2345), (0.1085, 0.2200), (0.1078, 0.2010), (0.1045, 0.1855), (0.0940, 0.1715),
             (0.0740, 0.1550), (0.0505, 0.1405), (0.0255, 0.1347), (0.0, 0.1337)]
# the head's rear, behind the bell: the roof over a body narrowing to a keel (x -0.34)
HEAD_REAR_HALF = ROOF + [(0.0647, 0.2891), (0.0471, 0.1816), (0.0437, 0.1689), (0.0334, 0.1440), (0.0271, 0.1342),
                         (0.0130, 0.1280), (0.0, 0.1280)]
X_FACE, X_BULB, X_NECK = -0.4945, -0.3720, -0.2927      # the head's face, the bulb's back, the neck's back face
WINDOWS = ((-0.2874, -0.2229), (-0.1838, -0.1194))        # the top cells' windows
X_RECV = -0.1228                                         # the receiver's front face (upper box)


def half(h, radii=None, n=3):
    """A closed symmetric outline from a half outline, corners filleted."""
    pts = sym(h)
    if radii:
        r = list(radii) + list(reversed(radii))
        pts = fillet(pts, r, n)
    return ccw(dedupe(pts))


# ---------------------------------------------------------------- emitter head

def inside(poly, q):
    """Point-in-polygon (even-odd) for q = (u, v)."""
    c = False
    n = len(poly)
    for i in range(n):
        (x0, y0), (x1, y1) = poly[i], poly[(i + 1) % n]
        if (y0 > q[1]) != (y1 > q[1]) and q[0] < x0 + (q[1] - y0) * (x1 - x0) / (y1 - y0):
            c = not c
    return c


def outer_rim(section):
    """Cap edges (facing x) outside a part's section: a ring's visible rims, not the outline buried in the part."""
    def pred(e):
        if not cap_rim(0)(e):
            return False
        m = (e.verts[0].co + e.verts[1].co) / 2
        return not inside(section, (m.y, m.z))
    return pred


def near_outline(poly, d=0.0025):
    """Edges whose midpoint (y, z) lies within d of an outline's points (a dense, filleted outline): its rim only."""
    P = [Vector(p) for p in poly]
    def pred(e):
        m = (e.verts[0].co + e.verts[1].co) / 2
        q = Vector((m.y, m.z))
        return min((q - p).length for p in P) < d
    return pred


def bay_outline():
    """The emitter bay's opening: an elliptic arch (half-width 0.044, 3.1 cm tall) over straight sides, 4 mm
    rounded corners at the bottom."""
    arch = [(0.0440 * math.cos(math.radians(a)), 0.3300 + 0.0310 * math.sin(math.radians(a))) for a in range(0, 181, 10)]
    low = arc(-0.0400, 0.2900, 0.0040, 180, 270, 3) + arc(0.0400, 0.2900, 0.0040, 270, 360, 3)
    return ccw(dedupe(arch + low))
X_BAY = -0.4740         # the emitter bay's floor
# light strips on the head's flanks (side view paths): the L before the vent, the bracket round its slots
HEAD_TRIM_L = [(-0.4645, 0.2860), (-0.4645, 0.1620), (-0.4560, 0.1540), (-0.4000, 0.1540)]
HEAD_TRIM_BRACKET = [(-0.4560, 0.2690), (-0.4000, 0.2690), (-0.3990, 0.2680), (-0.3990, 0.1700)]
# the neck's armor plate (side view, above its lower edge) and the light strip under that edge
NECK_PLATE = [(-0.4200, 0.4000), (-0.4200, 0.3700), (-0.3580, 0.3150), (-0.2800, 0.3150), (-0.2800, 0.4000)]
NECK_TRIM = [(-0.4170, 0.3605), (-0.3610, 0.3105), (-0.2960, 0.3105)]


def bulb_taper(bm):
    """The bulb narrows behind its widest point (x -0.43) to 0.965 of it at its back: cut at the knee and at the
    back so the faces bend there, then scale y."""
    for x in (-0.43, X_BULB):
        geom = bm.verts[:] + bm.edges[:] + bm.faces[:]
        bmesh.ops.bisect_plane(bm, geom=geom, plane_co=(x, 0, 0), plane_no=(1, 0, 0))
    return taper(bm, along=0, a0=-0.43, a1=X_BULB, s0=1.0, s1=0.965, axis=1)


def head():
    bell = smooth_half(BELL_HALF, 2)
    bay = bay_outline()
    # the bulb: a cast shell, its front a rounded lip round a seat for the face plate, the emitter bay cut through
    shell = prism(bell, plane="YZ", w0=X_FACE, w1=X_BULB)
    shell = bulb_taper(shell)
    seat = prism(offset_poly(bell, 0.0055), plane="YZ", w0=X_FACE - 0.01, w1=X_FACE + 0.0060)
    pocket = prism(bay, plane="YZ", w0=X_FACE - 0.01, w1=X_BAY)
    # four vent slots in each flank, framed by light strips (an L in front, a bracket round the slots)
    skin = lambda: shell_band(bell, -0.470, -0.395, 0.01, 0.0065)
    slots = [boolean(skin(), [prism(ccw(rrect(xc - 0.0041, 0.2070, xc + 0.0041, 0.2520, 0.0040)), plane="XZ",
                                    w0=-0.2, w1=0.2)], "INTERSECT")
             for xc in (-0.4515, -0.4390, -0.4265, -0.4140)]
    trims = [HEAD_TRIM_L, HEAD_TRIM_BRACKET]
    grooves = [channel(bell, -0.480, -0.385, t, 0.0034, 0.0022) for t in trims]
    slots = [bulb_taper(b) for b in slots]
    grooves = [bulb_taper(b) for b in grooves]
    shell = boolean(shell, [seat, pocket] + slots + grooves)
    rim_front = lambda e: cap_rim(0)(e) and near_outline(bell, 0.0030)(e)
    rim_back = lambda e: cap_rim(0)(e) and min(v.co.x for v in e.verts) > X_BULB - 0.003
    hi = [shell_band(bell, -0.4632, -0.4622, 0.01, 0.0007)] if HI else None      # the cast's parting line
    part("head_bulb", shell, "paint", bevel=0.0010, rim=(any_of(rim_front, rim_back), 0.0050, 2), uv_axis="x",
         cuts=hi)
    # the light strip in the seat's outer channel, below the lip
    ring_o, ring_i = offset_poly(bell, 0.0056), offset_poly(bell, 0.0086)
    strips = [bulb_taper(inlay(bell, -0.480, -0.385, t, 0.0034, 0.0022)) for t in trims]
    part("face_trim", join(prism(ring_o, [ccw(ring_i)], plane="YZ", w0=X_FACE + 0.0062, w1=X_FACE + 0.0032),
                           *strips), "trim", bevel=0, uv_axis="x")
    # the skids under the head: two runners, their tops buried in the shell
    sk = ccw(fillet([(-0.4880, 0.1440), (-0.4840, 0.1290), (-0.4700, 0.1262), (-0.3000, 0.1240), (-0.2930, 0.1300),
                     (-0.2930, 0.1440)], [0, 0.004, 0.006, 0.004, 0.003, 0], 3))
    part("head_skids", slab(sk, plane="XZ", w0=-0.0400, w1=-0.0650, inset=0.0015), "gunmetal", bevel=0.0008,
         mirror=True, uv_axis="y")
    # the face plate in the seat, its bay hole lined up with the pocket
    fp = boolean(prism(offset_poly(bell, 0.0092), plane="YZ", w0=X_FACE + 0.0070, w1=X_FACE + 0.0008),
                 [prism(offset_poly(bay, -0.0004), plane="YZ", w0=X_FACE - 0.01, w1=X_FACE + 0.02)])
    part("face_plate", fp, "gunmetal", bevel=0, rim=(lambda e: cap_rim(0)(e) and
                                                         min(v.co.x for v in e.verts) < X_FACE + 0.002, 0.0022, 2),
         uv_axis="x")
    # the bay's floor plate and the emitter: a stepped nozzle with a bore
    part("bay_floor", prism(offset_poly(bay, 0.0010), plane="YZ", w0=X_BAY + 0.0010, w1=X_BAY - 0.0012),
         "dark_metal", bevel=0.0005, uv_axis="x")
    # a rounded collar (its lip a quarter round), a groove, the nozzle tip with a rounded nose, the bore
    collar = ([(X_BAY + 0.0005, 0), (X_BAY + 0.0005, 0.0262), (-0.4850, 0.0262)] +
              [(x, r) for r, x in arc(0.0222, -0.4850, 0.0040, 0, -90, 4)[1:]] +
              [(-0.4890, 0.0170), (-0.4870, 0.0150), (-0.4870, 0)])
    part("emitter_collar", lathe(collar, cz=BORE_Z, segs=28), "gunmetal", bevel=0.0005, uv_axis="x")
    nozzle = ([(-0.4880, 0), (-0.4880, 0.0148), (-0.4945, 0.0148)] +
              [(x, r) for r, x in arc(0.0108, -0.4945, 0.0040, 0, -90, 4)[1:]] +
              [(-0.4985, 0.0060), (-0.4980, 0.0052), (-0.4920, 0.0052), (-0.4920, 0)])
    part("emitter", lathe(nozzle, cz=BORE_Z, segs=24), "steel", bevel=0.0005, uv_axis="x")
    # the carbon pad, screwed onto the plate below the bay
    pad = clip(offset_poly(bell, 0.0140), 1, 0.2830, "below")
    pad = ccw(fillet(pad, [0.008 if abs(p[1] - 0.2830) < 1e-6 else 0 for p in pad], 4))
    part("face_pad", slab(pad, plane="YZ", w0=X_FACE + 0.0020, w1=X_FACE - 0.0025, inset=0.0022), "carbon",
         bevel=0.0006, uv_axis="x")


def head_rear():
    # the neck's armor and the body under it, from the band at x -0.416 back to the lower cell's socket
    sec = half(HEAD_REAR_HALF)
    plain = half([pt for pt in HEAD_REAR_HALF if pt not in ((0.0622, 0.3036), (0.0622, 0.3006))])
    rear = boolean(prism(sec, plane="YZ", w0=X_BULB - 0.0040, w1=X_NECK),
                   [prism(ccw(clip(plain, 1, 0.2960, "above")), plane="YZ", w0=-0.4160, w1=X_BULB - 0.0020)], "UNION")
    lights = [[(x0, 0.1700), (x0 + 0.0100, 0.1700)] for x0 in (-0.3560, -0.3400, -0.3240)]
    cuts = [channel(sec, -0.42, -0.28, NECK_TRIM, 0.0034, 0.0022)]
    cuts += [boolean(shell_band(sec, -0.40, -0.28, 0.01, 0.0015),
                     [prism(ccw(rrect(-0.3620, 0.1530, -0.3000, 0.1860, 0.003)), plane="XZ", w0=-0.2, w1=0.2)],
                     "INTERSECT")]
    cuts += [channel(sec, -0.42, -0.28, l, 0.0040, 0.0040) for l in lights]
    part("head_rear", boolean(rear, cuts), "paint", bevel=0.0012, rim=(cap_rim(0), 0.0030, 3), uv_axis="x")
    part("neck_trim", join(inlay(sec, -0.42, -0.28, NECK_TRIM, 0.0034, 0.0022),
                           *[inlay(sec, -0.42, -0.28, l, 0.0040, 0.0040, below=0.0026) for l in lights]), "trim", bevel=0,
         uv_axis="x")
    # the armor plate: a skin 1.6 mm proud over the neck's top, its lower edge sloping down and running back
    plate = boolean(shell_band(sec, -0.4130, X_NECK - 0.0015, 0.0016, 0.0020),
                    [prism(ccw(NECK_PLATE), plane="XZ", w0=-0.2, w1=0.2)], "INTERSECT")
    part("neck_plate", plate, "paint", bevel=0, rim=(sharp_over(50), 0.0012, 1), uv_axis="x")
    shield = ccw(fillet([(-0.3620, 0.2680), (-0.3040, 0.2680), (-0.3040, 0.2160), (-0.3190, 0.1970),
                         (-0.3470, 0.1970), (-0.3620, 0.2160)], 0.003, 2))
    sp = boolean(shell_band(sec, -0.37, -0.29, 0.0013, 0.0020), [prism(shield, plane="XZ", w0=-0.2, w1=0.2)],
                 "INTERSECT")
    part("shield_plates", sp, "paint", bevel=0, rim=(sharp_over(50), 0.0010, 1), uv_axis="y")


# ---------------------------------------------------------------- the upper housing, its hoops and top cells

def housing():
    h = prism(half(HOUSING_HALF), plane="YZ", w0=X_NECK, w1=X_RECV + 0.004)
    cuts = []
    for a, b in WINDOWS:
        win = fillet([(a, 0.3145), (b, 0.3145), (b, 0.40), (a, 0.40)], [0.006, 0.006, 0, 0], 4)
        cuts.append(prism(ccw(win), plane="XZ", w0=-0.1, w1=0.1))
    in_window = lambda e: all(v.co.z > 0.3135 and any(a - 0.0005 < v.co.x < b + 0.0005 for a, b in WINDOWS)
                              for v in e.verts) and sharp_over(30)(e)
    hi = [channel(half(HOUSING_HALF), X_NECK, X_RECV, [(X_NECK - 0.01, z), (X_RECV + 0.01, z)], 0.0009, 0.0006)
          for z in (0.3110,)] if HI else None
    part("housing", boolean(h, cuts), "paint", bevel=0.0010, rim=(in_window, 0.0012, 2), uv_axis="x", cuts=hi)
    # clamp rings over the roof: one at each end of the cells, one between them; round-profiled bands
    sec = half(HOUSING_HALF)
    rings = [shell_band(sec, a, b, 0.0030, 0.0040) for a, b in
             ((X_NECK - 0.0010, X_NECK + 0.0042), (WINDOWS[0][1] + 0.0014, WINDOWS[1][0] - 0.0014),
              (X_RECV - 0.0080, X_RECV - 0.0008))]
    part("hoops", join(*rings), "gunmetal", bevel=0, rim=(outer_rim(sec), 0.0020, 3), uv_axis="x")
    covered = [(X_NECK - 0.0010, X_NECK + 0.0042), (WINDOWS[0][1] + 0.0014, WINDOWS[1][0] - 0.0014),
               (X_RECV - 0.0080, 0.2)]           # under the rings and inside the receiver: no frame there
    frames = []
    for a, b in WINDOWS:
        c = 0.0035      # the frame's centreline: its inner edge 1.5 mm outside the window's edge and sill
        path = ([(a - c, 0.4000), (a - c, 0.3145 + 0.006)] + arc(a + 0.006, 0.3145 + 0.006, 0.006 + c, 180, 270, 3)[1:] +
                arc(b - 0.006, 0.3145 + 0.006, 0.006 + c, 270, 360, 3) + [(b + c, 0.4000)])
        rib, _ = ribbon(dedupe(path), 0.0040)
        fr = boolean(shell_band(sec, a - 0.01, b + 0.01, 0.0022, 0.0020),
                     [prism(rib, plane="XZ", w0=-0.2, w1=0.2)], "INTERSECT")
        # the rings cover the windows' ends: the frame runs along the sill and stops 1.5 mm short of each ring
        cut = [box(x0 - 0.0015, x1 + 0.0015, -0.2, 0.2, 0.0, 0.5) for x0, x1 in covered
               if x1 + 0.0015 > a - 0.01 and x0 - 0.0015 < b + 0.01]
        frames.append(boolean(fr, cut) if cut else fr)
    part("window_frames", join(*frames), "gunmetal", bevel=0, rim=(sharp_over(40), 0.0016, 1), uv_axis="x")


def top_cells():
    out = []
    for a, b in WINDOWS:
        side = prism(ccw(rrect(a + 0.0012, 0.2960, b - 0.0012, 0.3690, 0.016)), plane="XZ", w0=-0.05, w1=0.05)
        front = prism(ccw(superellipse(0.0, 0.3200, 0.0457, 0.0480, 32, 2.4)), plane="YZ", w0=a - 0.01, w1=b + 0.01)
        plan = prism(ccw(rrect(a + 0.0012, -0.0460, b - 0.0012, 0.0460, 0.0120)), plane="XY", w0=0.28, w1=0.38)
        out.append(boolean(side, [front, plan], "INTERSECT"))
    part("top_cells", join(*out), "glass_cell", bevel=0.0010, uv_axis="x")


# ---------------------------------------------------------------- the lower cell and the guide rod

def lower_cell():
    R = 0.0460
    glass = lathe([(X_NECK, 0), (X_NECK, R), (-0.1000, R), (-0.1000, 0)], cz=CELL_Z, segs=48)
    part("lower_cell", glass, "glass_cell", bevel=0, uv_axis="x")
    # end caps: stepped collars seated in the head's and the receiver's sockets
    front = [(X_NECK - 0.002, 0), (X_NECK - 0.002, 0.0490), (-0.2860, 0.0490), (-0.2845, 0.0475),
             (-0.2790, 0.0475), (-0.2775, 0.0462), (-0.2775, 0)]
    back = [(-0.1165, 0), (-0.1165, 0.0462), (-0.1150, 0.0476), (-0.1060, 0.0476), (-0.1045, 0.0490),
            (-0.0800, 0.0490), (-0.0800, 0)]
    part("cell_caps", join(lathe(front, cz=CELL_Z, segs=28), lathe(back, cz=CELL_Z, segs=28)), "gunmetal",
         bevel=0.0006, uv_axis="x")


def rod():
    Zr, r = 0.1614, 0.0092
    shaft = lathe([(-0.2950, 0), (-0.2950, r), (-0.0900, r), (-0.0900, 0)], cz=Zr, segs=20)
    col = [(-0.2830, 0), (-0.2830, 0.0122), (-0.2815, 0.0130), (-0.2625, 0.0130), (-0.2610, 0.0122), (-0.2610, 0)]
    col2 = [(-0.1410, 0), (-0.1410, 0.0122), (-0.1395, 0.0130), (-0.1100, 0.0130), (-0.1085, 0.0122),
            (-0.1085, 0)]
    part("rod", shaft, "steel", bevel=0.0004, uv_axis="x")
    part("rod_collars", join(lathe(col, cz=Zr, segs=24), lathe(col2, cz=Zr, segs=24)), "gunmetal", bevel=0.0005,
         uv_axis="x")


# ---------------------------------------------------------------- receiver

RECV_UP_HALF = [(0.0, 0.3838), (0.0110, 0.3833), (0.0217, 0.3818), (0.0325, 0.3784), (0.0422, 0.3735),
                (0.0588, 0.3575), (0.0692, 0.3418), (0.0688, 0.3070), (0.0642, 0.3036), (0.0642, 0.3006),
                (0.0668, 0.2990), (0.0668, 0.2700), (0.0610, 0.2640), (0.0, 0.2640)]
RECV_UP_SIDE = [(X_RECV, 0.2640), (X_RECV, 0.3780), (-0.1170, 0.3838), (0.0994, 0.3838), (0.1600, 0.3716),
                (0.1640, 0.3640), (0.1690, 0.3440), (0.1850, 0.3140), (0.1915, 0.2980), (0.1915, 0.2640)]
RECV_LOW_SIDE = [(-0.1150, 0.2900), (-0.1106, 0.2725), (-0.1023, 0.2603), (-0.0691, 0.1226), (-0.0564, 0.1162),
                 (-0.0344, 0.1187), (0.0373, 0.1511), (0.0603, 0.1583), (0.0862, 0.1802), (0.1428, 0.2690),
                 (0.1500, 0.2900)]
RECV_LOW_R = [0, 0.004, 0.004, 0.008, 0.010, 0.012, 0.020, 0.012, 0.006, 0.006, 0]
RECV_LOW_HALF = [(0.0, 0.2950), (0.0700, 0.2950), (0.0700, 0.2640), (0.0686, 0.2050), (0.0668, 0.1600),
                 (0.0620, 0.1150), (0.0, 0.1100)]
RECV_CORE_SIDE = [(0.0000, 0.2900), (0.0000, 0.1750), (0.0420, 0.1750), (0.0470, 0.1470), (0.0900, 0.1490), (0.1400, 0.1727),
                  (0.1936, 0.1919), (0.2039, 0.2017), (0.2092, 0.2178), (0.2092, 0.2222), (0.2019, 0.2456),
                  (0.1882, 0.2734), (0.1794, 0.2905), (0.1580, 0.2950)]
RECV_CORE_R = [0, 0, 0.003, 0.003, 0.02, 0.02, 0.006, 0.006, 0.004, 0.004, 0.010, 0.010, 0.006, 0]
VENT = (-0.0605, 0.0890, 0.2135, 0.2600)        # the side vent window: x0 x1 z0 z1
# the light strip round the side armor: down the front, along the bottom, up the back diagonal (side view)
SIDE_PLATE = [(-0.0835, 0.2700), (-0.0815, 0.2080), (-0.0670, 0.1530), (-0.0570, 0.1400), (-0.0400, 0.1355),
              (0.0400, 0.1655), (0.0680, 0.1795), (0.0915, 0.2075), (0.1210, 0.2700)]
UPPER_PLATE = [(-0.1080, 0.3640), (-0.0880, 0.3040), (0.0980, 0.3040), (0.1360, 0.3380), (0.1360, 0.3560),
               (0.1180, 0.3700), (-0.1000, 0.3720)]
RECV_TRIM = [(-0.0860, 0.2080), (-0.0715, 0.1500), (-0.0610, 0.1360), (-0.0400, 0.1300), (0.0400, 0.1600),
             (0.0700, 0.1740), (0.0950, 0.2050), (0.1260, 0.2700)]
CORE_Y = 0.0408


def ring_groove(x0, x1, outline_half, depth):
    """A cutter for a groove round a long part with this section, from x0 to x1."""
    o = half(outline_half)
    return prism(offset_poly(o, -0.01), [ccw(offset_poly(o, depth))], plane="YZ", w0=x0, w1=x1)


def receiver():
    up = boolean(prism(ccw(fillet(RECV_UP_SIDE, [0, 0.003, 0.003, 0.010, 0.006, 0.004, 0, 0.004, 0, 0], 3)),
                       plane="XZ", w0=-0.08, w1=0.08),
                 [prism(half(RECV_UP_HALF), plane="YZ", w0=X_RECV - 0.01, w1=0.20)], "INTERSECT")
    # the fork at the back, round the stock tube; a groove round the front collar
    cuts = [box(0.1580, 0.25, -0.0290, 0.0290, 0.20, 0.45), ring_groove(-0.0875, -0.0850, RECV_UP_HALF, 0.0018)]
    front_rim = lambda e: cap_rim(0)(e) and max(v.co.x for v in e.verts) < X_RECV + 0.002
    part("recv_upper", boolean(up, cuts), "paint", bevel=0.0012, rim=(front_rim, 0.0030, 3), uv_axis="x")
    # the lower shell: an angular front, its flanks the side armor, the vent window in each
    low = boolean(prism(ccw(fillet(RECV_LOW_SIDE, RECV_LOW_R, 2)), plane="XZ", w0=-0.08, w1=0.08),
                  [prism(half(RECV_LOW_HALF), plane="YZ", w0=-0.13, w1=0.16)], "INTERSECT")
    x0, x1, z0, z1 = VENT
    win = ccw(fillet([(x0, z0), (x1 - 0.006, z0), (x1, z0 + 0.012), (x1, z1 - 0.006), (x1 - 0.006, z1),
                      (x0, z1)], [0.005, 0.004, 0.004, 0.004, 0.004, 0.005], 3))
    pockets = [slab(win, plane="XZ", w0=s * 0.09, w1=s * 0.0502, inset=0.0) for s in (-1, 1)]
    lsec = half(RECV_LOW_HALF)
    path = smooth_path(RECV_TRIM, 3)
    part("recv_lower", boolean(low, pockets + [channel(lsec, -0.13, 0.16, path, 0.0034, 0.0022)]), "paint",
         bevel=0.0012, rim=(side_silhouette(0.060), 0.0030, 3), uv_axis="y")
    screws = []
    for x, z in ((-0.0765, 0.2550), (-0.0560, 0.1460), (0.0380, 0.1735), (0.1080, 0.2560)):
        y = interp([(b_, a_) for a_, b_ in reversed(RECV_LOW_HALF[1:-1])], z) + 0.0018      # the plate's face
        dz = 0.002
        dy = (interp([(b_, a_) for a_, b_ in reversed(RECV_LOW_HALF[1:-1])], z + dz) -
              interp([(b_, a_) for a_, b_ in reversed(RECV_LOW_HALF[1:-1])], z - dz)) / (2 * dz)
        for s in (-1, 1):
            screws.append(bolt((x, s * y, z), (0, s, -dy), r=0.0030, h=0.0012, sides=12))
    screws += [bolt((x, s * 0.0690, 0.3200), (0, s, 0), r=0.0028, h=0.0010, sides=12)
               for x in (-0.1060, 0.1430) for s in (-1, 1)]
    part("recv_trim", inlay(lsec, -0.13, 0.16, path, 0.0034, 0.0022), "trim", bevel=0, uv_axis="y")
    # the side armor: a plate 1.8 mm proud inside the strip, the vent window through it
    plate_out = ccw(fillet(SIDE_PLATE, [0.004, 0.006, 0.008, 0.010, 0.012, 0.010, 0.006, 0.004, 0], 3))
    plate = boolean(shell_band(lsec, -0.12, 0.15, 0.0018, 0.0020),
                    [prism(plate_out, plane="XZ", w0=-0.2, w1=0.2)], "INTERSECT")
    plate = boolean(plate, [slab(offset_poly(win, -0.0015), plane="XZ", w0=s_ * 0.09, w1=s_ * 0.05, inset=0.0)
                            for s_ in (-1, 1)])
    part("side_plates", plate, "paint", bevel=0, rim=(sharp_over(50), 0.0014, 1), uv_axis="y")
    usec = half(RECV_UP_HALF)
    up_plates = boolean(shell_band(usec, -0.115, 0.15, 0.0014, 0.0020),
                        [prism(ccw(fillet(UPPER_PLATE, 0.004, 2)), plane="XZ", w0=-0.2, w1=0.2)], "INTERSECT")
    part("upper_plates", up_plates, "paint", bevel=0, rim=(sharp_over(50), 0.0010, 1), uv_axis="y")
    spine = boolean(shell_band(usec, -0.105, 0.092, 0.0012, 0.0020),
                    [box(-0.2, 0.2, -0.2, 0.2, 0.3790, 0.40)], "INTERSECT")
    part("spine_plate", spine, "paint", bevel=0, rim=(sharp_over(50), 0.0010, 1), uv_axis="z")
    part("recv_screws", join(*screws), "steel", bevel=0, angle=35, uv_axis="y")
    # the latch on the upper flank: a block standing off the roof's curve
    latch = slab(ccw(rrect(0.0450, 0.3400, 0.1150, 0.3640, 0.004)), plane="XZ", w0=-0.0500, w1=-0.0728, inset=0.0012)
    part("latch", latch, "gunmetal", bevel=0.0006, mirror=True, uv_axis="y")
    # the trigger well's lip under the core
    lip = prism(ccw(rrect(0.0520, -0.0110, 0.1000, 0.0110, 0.004)), [ccw(rrect(0.0548, -0.0087, 0.0972, 0.0087, 0.002))],
                plane="XY", w0=0.1440, w1=0.1530)
    part("trigger_lip", lip, "dark_metal", bevel=0.0006, uv_axis="z")
    # the grill in the window: five uprights, two rails, set back from the shell's face
    bars = [box(x - 0.0024, x + 0.0024, -0.0640, -0.0495, z0 - 0.002, z1 + 0.002)
            for x in (-0.0333, -0.0093, 0.0141, 0.0380, 0.0615)]
    bars += [box(x0 - 0.002, x1 + 0.002, -0.0640, -0.0495, zc - 0.0025, zc + 0.0025) for zc in (0.2313, 0.2441)]
    grill = boolean(join(*bars), [prism(ccw(offset_poly(win, -0.0005)), plane="XZ", w0=-0.09, w1=0.0)],
                    "INTERSECT", self_=True)
    part("vent_grill", grill, "gunmetal", bevel=0, mirror=True, uv_axis="y")
    part("vent_glow", prism(offset_poly(win, 0.0015), plane="XZ", w0=-0.0525, w1=-0.0496), "vent_glow", bevel=0,
         mirror=True, uv_axis="y")
    # the core behind it: the trigger housing, the grip's socket, the rounded back under the stock
    core = prism(ccw(fillet(RECV_CORE_SIDE, RECV_CORE_R, 3)), plane="XZ", w0=-CORE_Y, w1=CORE_Y)
    core = boolean(core, [box(0.0560, 0.0960, -0.0075, 0.0075, 0.1400, 0.1650)])      # the trigger's slot
    part("recv_core", core, "paint", bevel=0.0010, rim=(side_silhouette(CORE_Y - 0.001), 0.0045, 3), uv_axis="y")
    # the pivot boss on each flank of the core, and the rod's block under the front
    boss = lathe([(-0.0465, 0), (-0.0465, 0.0090), (-0.0450, 0.0105), (-0.0450, 0.0128), (-0.0430, 0.0140),
                  (-0.0380, 0.0140), (-0.0380, 0)], cy=0, cz=0, segs=18)
    boss = boolean(boss, [lathe([(-0.06, 0), (-0.06, 0.0052), (-0.0440, 0.0052), (-0.0440, 0)], cy=0, cz=0,
                                segs=16)])
    M = Matrix.Translation((0.1550, 0, 0.2260)) @ Matrix.Rotation(math.pi / 2, 4, "Z")
    part("pivot_boss", transformed(boss, M), "gunmetal", bevel=0, mirror=True, uv_axis="y")
    blk = boolean(box(-0.1120, -0.0640, -0.0340, 0.0340, 0.1426, 0.1880),
                  [prism(ccw(sym([(0.0, 0.20), (0.0360, 0.20), (0.0360, 0.1600), (0.0270, 0.1426), (0.0, 0.1426)])),
                         plane="YZ", w0=-0.12, w1=-0.06)], "INTERSECT")
    part("rod_block", blk, "gunmetal", bevel=0.0010, rim=(cap_rim(0), 0.0025, 2), uv_axis="x")


def sight():
    base = prism(ccw(rrect(-0.0081, 0.3800, 0.0471, 0.3875, 0.003)), plane="XZ", w0=-0.0200, w1=0.0200)
    blk = boolean(prism(ccw([(-0.0002, 0.3800), (0.0393, 0.3800), (0.0393, 0.3950), (-0.0002, 0.3950)]), plane="XZ",
                        w0=-0.03, w1=0.03),
                  [prism(half([(0.0, 0.3950), (0.0115, 0.3950), (0.0164, 0.3921), (0.0168, 0.3800), (0.0, 0.3800)]),
                         plane="YZ", w0=-0.01, w1=0.05)], "INTERSECT")
    blk = boolean(blk, [prism(ccw([(-0.0026, 0.3990), (0.0026, 0.3990), (0.0012, 0.3915), (-0.0012, 0.3915)]),
                              plane="YZ", w0=-0.01, w1=0.05)])          # the notch along its top
    part("sight", join(base, blk), "gunmetal", bevel=0.0007, uv_axis="z")


# ---------------------------------------------------------------- grip and trigger

GRIP_FRONT = [(0.004, 0.1520), (0.012, 0.1452), (0.022, 0.1470), (0.035, 0.1478), (0.050, 0.1340),
              (0.068, 0.1312), (0.085, 0.1165), (0.102, 0.1142), (0.115, 0.1062), (0.125, 0.1050), (0.145, 0.1042),
              (0.165, 0.1050), (0.182, 0.1062), (0.195, 0.1070)]
GRIP_BACK = [(0.004, 0.1950), (0.012, 0.2110), (0.020, 0.2210), (0.034, 0.2276), (0.045, 0.2290), (0.056, 0.2288),
             (0.066, 0.2268), (0.080, 0.2225), (0.096, 0.2140), (0.110, 0.2030), (0.122, 0.1950), (0.134, 0.1832),
             (0.146, 0.1745), (0.158, 0.1718), (0.170, 0.1748), (0.182, 0.1830), (0.195, 0.1930)]
GRIP_HALF_BACK = [(0.0, 0.0238), (0.03, 0.0254), (0.06, 0.0263), (0.09, 0.0261), (0.12, 0.0251), (0.15, 0.0246),
                  (0.182, 0.0250)]
GRIP_HALF_FRONT = [(0.0, 0.0222), (0.03, 0.0234), (0.06, 0.0221), (0.09, 0.0222), (0.12, 0.0231), (0.15, 0.0240),
                   (0.182, 0.0244)]
POMMEL = ((0.1400, 0.0150), (0.2350, 0.0560))      # the base cap's top edge: front low, back high
GRIP_TRIM = [(0.1590, 0.1580), (0.1615, 0.1460), (0.1700, 0.1340), (0.1820, 0.1220), (0.1900, 0.1100),
             (0.2010, 0.0960), (0.2095, 0.0800), (0.2120, 0.0700), (0.2075, 0.0610)]


def grip_rings(n=24, n_exp=2.7, grow=0.0, z0=0.0020, z1=0.1950, steps=36, thumb=True):
    rings = []
    for k in range(steps + 1):
        z = z0 + (z1 - z0) * k / steps
        xf, xb = interp(GRIP_FRONT, z) - grow, interp(GRIP_BACK, z) + grow
        xc, a = (xf + xb) / 2, (xb - xf) / 2
        hb, hf = interp(GRIP_HALF_BACK, z) + grow, interp(GRIP_HALF_FRONT, z) + grow
        ring = []
        for i in range(n):
            th = 2 * math.pi * i / n
            c, s_ = math.cos(th), math.sin(th)
            x = xc + a * math.copysign(abs(c) ** (2 / n_exp), c)
            hw = hf + (hb - hf) * (0.5 + 0.5 * c)
            y = hw * math.copysign(abs(s_) ** (2 / n_exp), s_)
            if thumb and y < 0:        # a right hand's thumb rest, high on the left flank
                d = ((x - 0.1600) / 0.0220) ** 2 + ((z - 0.1450) / 0.0180) ** 2
                if d < 1:
                    y += 0.0024 * (1 - d) ** 2 * abs(s_)
            ring.append((x, y, z))
        rings.append(ring)
    return rings


def _plane_box(p0, p1, keep="above", w=0.05, lift=0.0):
    """A big prism on one side of the line through p0 and p1 (x-z), extruded across y."""
    (xa, za), (xb, zb) = p0, p1
    k = (zb - za) / (xb - xa)
    za -= lift; zb -= lift
    L, R = xa - 0.1, xb + 0.1
    zl, zr = za + k * (L - xa), za + k * (R - xa)
    far = 0.4 if keep == "above" else -0.3
    return prism(ccw([(L, zl), (R, zr), (R, far), (L, far)]), plane="XZ", w0=-w, w1=w)


def grip():
    g = loft(grip_rings())
    rib, _ = ribbon(smooth_path(GRIP_TRIM, 3), 0.0034)
    rib_in, _ = ribbon(shorten(smooth_path(GRIP_TRIM, 3), 0.0008), 0.0026)
    flanks = box(0.0, 0.4, -0.06, 0.06, -0.1, 0.4)
    groove = boolean(boolean(loft(grip_rings(grow=0.004)), [loft(grip_rings(grow=-0.0040))]),
                     [prism(rib, plane="XZ", w0=-0.2, w1=0.2)], "INTERSECT")
    strip = boolean(boolean(loft(grip_rings(grow=-0.0015)), [loft(grip_rings(grow=-0.0046))]),
                    [prism(rib_in, plane="XZ", w0=-0.2, w1=0.2)], "INTERSECT")
    g = boolean(g, [groove])
    part("grip", boolean(g, [_plane_box(*POMMEL, keep="above")], "INTERSECT"), "rubber", bevel=0, angle=70,
         uv_axis="z")
    part("grip_trim", strip, "trim", bevel=0, uv_axis="none")
    cap = loft(grip_rings(grow=0.0018, z0=0.0005, z1=0.0700, steps=12, thumb=False))
    cap = boolean(cap, [_plane_box(*POMMEL, keep="below", lift=-0.0020)], "INTERSECT")
    part("grip_cap", cap, "dark_metal", bevel=0.0008, rim=(sharp_over(40), 0.0015, 2), uv_axis="z")
    # the cap's screws, one each side, low on the palm swell
    part("grip_screws", join(*[bolt((0.2010, s_ * 0.0262, 0.0300), (0, s_, 0), r=0.0034, h=0.0010, sides=12)
                               for s_ in (-1, 1)]), "steel", bevel=0, angle=35, uv_axis="y")


TRIGGER = [(0.0775, 0.1640), (0.0765, 0.1530), (0.0752, 0.1420), (0.0738, 0.1320), (0.0715, 0.1225),
           (0.0685, 0.1145), (0.0648, 0.1085), (0.0608, 0.1048)]


def trigger():
    # a blade 1.3 cm deep at the pivot, a wider shoe where the finger pulls, rounded edges
    t = sweep(smooth_path(TRIGGER, 3),
              lambda t: 0.0050 - 0.0012 * t + 0.0016 * math.sin(math.pi * min(1.0, t * 1.3)),
              lambda t: 0.0078 - 0.0028 * t, n=12, n_exp=3.2)
    part("trigger", t, "dark_metal", bevel=0, angle=60, uv_axis="none")


# ---------------------------------------------------------------- stock

STOCK_HALF = [(0.0, 0.3579), (0.0125, 0.3555), (0.0185, 0.3515), (0.0225, 0.3455), (0.0248, 0.3370),
              (0.0256, 0.3250), (0.0256, 0.3090), (0.0232, 0.2960), (0.0190, 0.2885), (0.0110, 0.2832), (0.0, 0.2813)]
BUTT_SIDE = [(0.4026, 0.3711), (0.4211, 0.3799), (0.4456, 0.3809), (0.4529, 0.3789), (0.4719, 0.3677),
             (0.4778, 0.3584), (0.4807, 0.3481), (0.4827, 0.3306), (0.4949, 0.1997), (0.4993, 0.1919),
             (0.4998, 0.1670), (0.4988, 0.1631), (0.4919, 0.1533), (0.4885, 0.1519), (0.4485, 0.1519),
             (0.4148, 0.2671), (0.4120, 0.2740), (0.4158, 0.3096), (0.4089, 0.3569)]
BUTT_R = [0.008, 0.012, 0, 0.012, 0.012, 0.006, 0.004, 0.004, 0.004, 0.003, 0, 0.003, 0.003, 0.002, 0.004, 0.004,
          0.004, 0.004, 0.006]
BUTT_HALF = [(0.0, 0.3810), (0.0200, 0.3790), (0.0330, 0.3720), (0.0420, 0.3580), (0.0451, 0.3350), (0.0451, 0.2900),
             (0.0420, 0.2200), (0.0385, 0.1700), (0.0330, 0.1530), (0.0, 0.1500)]
PAD_LINE = ((0.4666, 0.3620), (0.4855, 0.1519))     # the recoil pad's front edge, top to bottom
BUTT_TRIM = [(0.4155, 0.3650), (0.4215, 0.3100), (0.4190, 0.2740), (0.4530, 0.1650)]


def stock():
    sec = smooth_half(STOCK_HALF, 2)
    tube = prism(sec, plane="YZ", w0=0.1500, w1=0.4250)
    grooves = [prism(offset_poly(sec, -0.01), [ccw(offset_poly(sec, 0.0016))], plane="YZ", w0=a, w1=b)
               for a, b in ((0.1770, 0.1830),)]
    tpath = [(0.2150, 0.3210), (0.3860, 0.3210)]
    hi = [channel(sec, 0.15, 0.425, [(0.1880, 0.3545), (0.3980, 0.3545)], 0.0009, 0.0006),
          channel(sec, 0.15, 0.425, [(0.2700, 0.2900), (0.2700, 0.3560)], 0.0009, 0.0006)] if HI else None
    part("stock_tube", boolean(tube, grooves + [channel(sec, 0.15, 0.425, tpath, 0.0030, 0.0022)]), "paint",
         bevel=0, uv_axis="x", cuts=hi)
    part("stock_collars", join(shell_band(sec, 0.1585, 0.1700, 0.0030, 0.004), shell_band(sec, 0.3930, 0.4030, 0.0030, 0.004)),
         "gunmetal", bevel=0, rim=(outer_rim(sec), 0.0015, 1), uv_axis="x")
    bsec = smooth_half(BUTT_HALF, 3)
    bpath = smooth_path(BUTT_TRIM, 3)
    body = boolean(prism(ccw(fillet(BUTT_SIDE, BUTT_R, 3)), plane="XZ", w0=-0.06, w1=0.06),
                   [prism(smooth_half(BUTT_HALF, 3), plane="YZ", w0=0.39, w1=0.51)], "INTERSECT")
    (xa, za), (xb, zb) = PAD_LINE
    k = (xb - xa) / (zb - za)
    behind = prism(ccw([(xa + k * (0.40 - za), 0.40), (0.55, 0.40), (0.55, 0.10), (xa + k * (0.10 - za), 0.10)]),
                   plane="XZ", w0=-0.06, w1=0.06)
    pad_vol = boolean(behind, [box(0.3, 0.6, -0.06, 0.06, 0.10, za)], "INTERSECT")
    part("stock_trim", join(inlay(sec, 0.15, 0.425, tpath, 0.0030, 0.0022), inlay(bsec, 0.39, 0.51, bpath, 0.0034, 0.0022)),
         "trim", bevel=0, uv_axis="x")
    part("butt", boolean(body.copy(), [pad_vol.copy(), channel(bsec, 0.39, 0.51, bpath, 0.0034, 0.0022)]), "paint", bevel=0.0012,
         rim=(side_silhouette(0.037), 0.0070, 3), uv_axis="y")
    grown = boolean(prism(ccw(offset_poly(ccw(fillet(BUTT_SIDE, BUTT_R, 3)), -0.0020)), plane="XZ", w0=-0.06, w1=0.06),
                    [prism(ccw(offset_poly(smooth_half(BUTT_HALF, 3), 0.0030)), plane="YZ", w0=0.39, w1=0.52)],
                    "INTERSECT")
    pad_in = boolean(prism(ccw([(xa + k * (0.40 - za) - 0.002, 0.40), (0.55, 0.40), (0.55, 0.10),
                                (xa + k * (0.10 - za) - 0.002, 0.10)]), plane="XZ", w0=-0.06, w1=0.06),
                     [box(0.3, 0.6, -0.06, 0.06, 0.10, za - 0.002)], "INTERSECT")
    body.free()
    part("butt_pad", boolean(grown, [pad_in], "INTERSECT"), "rubber", bevel=0.0010, rim=(sharp_over(40), 0.0040, 3),
         uv_axis="x")


BUILDERS = {"head": head, "head_rear": head_rear, "housing": housing, "top_cells": top_cells,
            "lower_cell": lower_cell, "rod": rod, "receiver": receiver, "sight": sight, "grip": grip,
            "trigger": trigger, "stock": stock}
ALL = ("head", "head_rear", "housing", "top_cells", "lower_cell", "rod", "receiver", "sight", "grip", "trigger",
       "stock")


def build(*names, view=(62, 0, -38), k=1.25):
    before = set(o.name for o in collection().objects)
    for n in names:
        BUILDERS[n]()
    new = [o for o in collection().objects if o.name not in before]
    if not new:
        new = list(collection().objects)
    frame(new, view=view, dist_k=k)
    return new
