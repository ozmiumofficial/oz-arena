"""Hard-surface building blocks for the rocket launcher rebuild, run inside Blender (the live session or -b).

Every part is its own closed mesh in the 'Rebuild' collection, in Tripo's space (x -0.5 muzzle .. 0.5 butt, y left/right,
z 0 bottom .. 0.32 top, barrel axis y 0 z 0.2206). Builders return bmesh; part() turns one into an object with a
material, a Bevel modifier (hardened normals) and, for side parts, a Mirror across y = 0. Rebuilding a part by name
replaces the old one, so each part script can be rerun while the shape is tuned.
"""
import bpy, bmesh, math
from mathutils import Vector, Matrix

REBUILD = "Rebuild"
TARGET = {"col": REBUILD, "prefix": ""}   # rl_high.py points part() at the High collection
BORE_Z = 0.2206

# profile plane -> world: u, v in the plane, w along the extrusion
PLANES = {
    "XZ": lambda u, v, w: (u, w, v),    # side profile, extruded along y
    "YZ": lambda u, v, w: (w, u, v),    # front profile, extruded along x
    "XY": lambda u, v, w: (u, v, w),    # top profile, extruded along z
}

MATS = {   # viewport colors; the PBR node trees come from rl_materials.py
    "red_paint": (0.40, 0.07, 0.08, 1),
    "gunmetal": (0.20, 0.21, 0.22, 1),
    "dark_metal": (0.07, 0.07, 0.075, 1),
    "steel": (0.55, 0.56, 0.57, 1),
    "rubber": (0.035, 0.035, 0.038, 1),
    "glass": (0.10, 0.55, 0.50, 1),
}


def collection(name=REBUILD):
    col = bpy.data.collections.get(name)
    if col is None:
        col = bpy.data.collections.new(name)
        bpy.context.scene.collection.children.link(col)
    return col


def material(name):
    m = bpy.data.materials.get(name)
    if m is None:
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        m.diffuse_color = MATS[name]
        bsdf = m.node_tree.nodes.get("Principled BSDF")
        if bsdf:
            bsdf.inputs["Base Color"].default_value = MATS[name]
    return m


def remove(name):
    ob = bpy.data.objects.get(name)
    if ob is not None:
        me = ob.data
        bpy.data.objects.remove(ob)
        if me is not None and me.users == 0:
            bpy.data.meshes.remove(me)


# ---------------------------------------------------------------- 2D outlines

def arc(cx, cy, r, a0, a1, n=8):
    """Points on a circle from angle a0 to a1 (degrees), both ends included."""
    return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


def circle(cx, cy, r, n=32, a0=0.0):
    return [(cx + r * math.cos(2 * math.pi * i / n + math.radians(a0)),
             cy + r * math.sin(2 * math.pi * i / n + math.radians(a0))) for i in range(n)]


def rrect(u0, v0, u1, v1, r, n=4):
    """Rounded rectangle, counter-clockwise."""
    r = min(r, abs(u1 - u0) / 2 - 1e-6, abs(v1 - v0) / 2 - 1e-6)
    if r <= 1e-6:
        return [(u0, v0), (u1, v0), (u1, v1), (u0, v1)]
    return (arc(u1 - r, v0 + r, r, -90, 0, n) + arc(u1 - r, v1 - r, r, 0, 90, n) +
            arc(u0 + r, v1 - r, r, 90, 180, n) + arc(u0 + r, v0 + r, r, 180, 270, n))


def slot(u0, u1, v, h, n=6):
    """Stadium (rounded slot) from u0 to u1 at height v, h tall."""
    r = h / 2
    return arc(u1 - r, v, r, -90, 90, n) + arc(u0 + r, v, r, 90, 270, n)


def fillet(pts, radii, n=4):
    """Round the corners of a closed polygon: radii is a number or one per corner (0 = keep sharp)."""
    if not isinstance(radii, (list, tuple)):
        radii = [radii] * len(pts)
    radii = list(radii) + [0] * (len(pts) - len(radii))
    out = []
    N = len(pts)
    for i in range(N):
        r = radii[i]
        p = Vector(pts[i]); a = Vector(pts[i - 1]); b = Vector(pts[(i + 1) % N])
        if r <= 0:
            out.append(tuple(p)); continue
        da = (a - p); db = (b - p)
        la, lb = da.length, db.length
        da.normalize(); db.normalize()
        ang = math.acos(max(-1.0, min(1.0, da.dot(db))))
        if ang < 1e-3 or ang > math.pi - 1e-3:
            out.append(tuple(p)); continue
        t = r / math.tan(ang / 2)
        t = min(t, la * 0.49, lb * 0.49)
        r = t * math.tan(ang / 2)
        p0 = p + da * t; p1 = p + db * t
        bis = (da + db).normalized()
        c = p + bis * (r / math.sin(ang / 2))
        a0 = math.atan2(p0.y - c.y, p0.x - c.x); a1 = math.atan2(p1.y - c.y, p1.x - c.x)
        d = a1 - a0
        while d > math.pi: d -= 2 * math.pi
        while d < -math.pi: d += 2 * math.pi
        for k in range(n + 1):
            t_ = a0 + d * k / n
            out.append((c.x + r * math.cos(t_), c.y + r * math.sin(t_)))
    return out


def clip(pts, axis, value, keep="below"):
    """The part of a closed polygon on one side of the line coord[axis] = value (Sutherland-Hodgman)."""
    inside = (lambda p: p[axis] <= value) if keep == "below" else (lambda p: p[axis] >= value)
    out = []
    N = len(pts)
    for i in range(N):
        a, b = pts[i], pts[(i + 1) % N]
        ia, ib = inside(a), inside(b)
        if ia:
            out.append(a)
        if ia != ib:
            t = (value - a[axis]) / (b[axis] - a[axis])
            out.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
    return out


def sym(half):
    """A left-right symmetric outline from its +u half (listed from the top centre down to the bottom centre)."""
    return [(u, v) for (u, v) in half] + [(-u, v) for (u, v) in reversed(half)]


def transformed(bm, M):
    bmesh.ops.transform(bm, matrix=M, verts=bm.verts)
    return bm


def ccw(pts):
    s = sum(pts[i][0] * pts[(i + 1) % len(pts)][1] - pts[(i + 1) % len(pts)][0] * pts[i][1] for i in range(len(pts)))
    return pts if s > 0 else pts[::-1]


def dedupe(pts, eps=1e-5):
    out = []
    for p in pts:
        if not out or (abs(p[0] - out[-1][0]) > eps or abs(p[1] - out[-1][1]) > eps):
            out.append(p)
    if len(out) > 1 and abs(out[0][0] - out[-1][0]) < eps and abs(out[0][1] - out[-1][1]) < eps:
        out.pop()
    return out


# ---------------------------------------------------------------- solids

def prism(outer, holes=(), plane="XZ", w0=-0.01, w1=0.01):
    """A closed slab: the outline (with holes) in a plane, extruded from w0 to w1."""
    m = PLANES[plane]
    bm = bmesh.new()
    edges = []
    for loop in [outer] + list(holes):
        loop = dedupe(loop)
        vs = [bm.verts.new(m(u, v, w0)) for (u, v) in loop]
        for i in range(len(vs)):
            edges.append(bm.edges.new((vs[i], vs[(i + 1) % len(vs)])))
    res = bmesh.ops.triangle_fill(bm, use_beauty=True, use_dissolve=True, edges=edges)
    faces = [g for g in res["geom"] if isinstance(g, bmesh.types.BMFace)]
    ext = bmesh.ops.extrude_face_region(bm, geom=faces, use_keep_orig=True)
    d = Vector(m(0, 0, w1)) - Vector(m(0, 0, w0))
    bmesh.ops.translate(bm, vec=d, verts=[g for g in ext["geom"] if isinstance(g, bmesh.types.BMVert)])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return bm


def lathe(profile, cy=0.0, cz=BORE_Z, segs=48, a0=0.0):
    """Revolve a profile of (x, r) about the line y=cy, z=cz parallel to x.
    A profile whose first and last r are 0 is a solid (the ends become flat caps); otherwise it is a closed loop
    (a ring's cross-section)."""
    bm = bmesh.new()
    solid = profile[0][1] == 0 and profile[-1][1] == 0
    pts = profile[1:-1] if solid else profile
    rings = []
    for (x, r) in pts:
        rings.append([bm.verts.new((x, cy + r * math.cos(2 * math.pi * i / segs + math.radians(a0)),
                                    cz + r * math.sin(2 * math.pi * i / segs + math.radians(a0))))
                      for i in range(segs)])
    n = len(rings)
    for k in range(n if not solid else n - 1):
        A, B = rings[k], rings[(k + 1) % n]
        for i in range(segs):
            bm.faces.new((A[i], A[(i + 1) % segs], B[(i + 1) % segs], B[i]))
    if solid:
        bm.faces.new(rings[0])
        bm.faces.new(rings[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return bm


def box(x0, x1, y0, y1, z0, z1):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co = Vector((x0 if v.co.x < 0 else x1, y0 if v.co.y < 0 else y1, z0 if v.co.z < 0 else z1))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return bm


def tube(path, radius, segs=12, cap=True):
    """A round tube along a 3D polyline (parallel-transported frames), closed with flat caps."""
    P = [Vector(p) for p in path]
    bm = bmesh.new()
    T = []
    for i in range(len(P)):
        a = P[max(i - 1, 0)]; b = P[min(i + 1, len(P) - 1)]
        T.append((b - a).normalized())
    up = Vector((0, 0, 1)) if abs(T[0].z) < 0.9 else Vector((0, 1, 0))
    N = T[0].cross(up).normalized()
    rings = []
    for i in range(len(P)):
        if i > 0:
            ax = T[i - 1].cross(T[i])
            if ax.length > 1e-8:
                ang = T[i - 1].angle(T[i])
                N = (Matrix.Rotation(ang, 3, ax.normalized()) @ N)
            N = (N - T[i] * N.dot(T[i])).normalized()
        B = T[i].cross(N)
        rings.append([bm.verts.new(P[i] + (N * math.cos(2 * math.pi * k / segs) + B * math.sin(2 * math.pi * k / segs)) * radius)
                      for k in range(segs)])
    for i in range(len(rings) - 1):
        A, Bq = rings[i], rings[i + 1]
        for k in range(segs):
            bm.faces.new((A[k], A[(k + 1) % segs], Bq[(k + 1) % segs], Bq[k]))
    if cap:
        bm.faces.new(rings[0]); bm.faces.new(rings[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return bm


def join(*bms):
    out = bmesh.new()
    me = bpy.data.meshes.new("_tmp_join")
    for b in bms:
        b.to_mesh(me)
        out.from_mesh(me)
        b.free()
    bpy.data.meshes.remove(me)
    return out


def boolean(bm, cutters, op="DIFFERENCE", self_=False):
    """bm op cutters (bmesh list), exact solver; returns a new bmesh (inputs freed)."""
    col = collection("_tmp_bool")
    def obj(b, name):
        me = bpy.data.meshes.new(name); b.to_mesh(me); b.free()
        o = bpy.data.objects.new(name, me); col.objects.link(o); return o
    A = obj(bm, "_bool_A")
    Bs = [obj(c, "_bool_B%d" % i) for i, c in enumerate(cutters)]
    for B in Bs:
        mod = A.modifiers.new("b", "BOOLEAN")
        mod.operation = op; mod.solver = "EXACT"; mod.object = B
        mod.use_self = self_; mod.use_hole_tolerant = self_
    dg = bpy.context.evaluated_depsgraph_get()
    ev = A.evaluated_get(dg)
    out = bmesh.new(); out.from_mesh(ev.to_mesh()); ev.to_mesh_clear()
    for o in [A] + Bs:
        me = o.data; bpy.data.objects.remove(o); bpy.data.meshes.remove(me)
    bpy.data.collections.remove(col)
    tidy(out)
    return out


def tidy(bm, angle=0.5):
    """Weld, dissolve coplanar splits left by booleans, drop loose bits; normals outward."""
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    bmesh.ops.dissolve_limit(bm, angle_limit=math.radians(angle), verts=bm.verts, edges=bm.edges,
                               delimit={"NORMAL"})
    bmesh.ops.dissolve_degenerate(bm, dist=1e-7, edges=bm.edges)
    loose = [v for v in bm.verts if not v.link_faces]
    bmesh.ops.delete(bm, geom=loose, context="VERTS")
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)


def offset_poly(pts, d):
    """A counter-clockwise polygon moved inward by d (outward for d < 0), mitred corners."""
    P = [Vector(p) for p in pts]
    N = len(P)
    out = []
    for i in range(N):
        a, p, b = P[i - 1], P[i], P[(i + 1) % N]
        e1 = (p - a).normalized(); e2 = (b - p).normalized()
        n1 = Vector((-e1.y, e1.x)); n2 = Vector((-e2.y, e2.x))
        k = 1 + n1.dot(n2)
        m = (n1 + n2) / k if k > 0.25 else (n1 + n2).normalized() * 1.6
        out.append(tuple(p + m * d))
    return out


def slab(outline, plane="XZ", w0=0.0, w1=0.01, inset=0.001):
    """A raised plate with angled sides: the outline at w0, the same outline inset by `inset` at w1."""
    outline = ccw(dedupe(outline))
    top = offset_poly(outline, inset)
    m = PLANES[plane]
    bm = bmesh.new()
    B = [bm.verts.new(m(u, v, w0)) for u, v in outline]
    T = [bm.verts.new(m(u, v, w1)) for u, v in top]
    n = len(B)
    for i in range(n):
        bm.faces.new((B[i], B[(i + 1) % n], T[(i + 1) % n], T[i]))
    for ring in (B, T):
        es = [bm.edges.get((ring[i], ring[(i + 1) % n])) for i in range(n)]
        bmesh.ops.triangle_fill(bm, use_beauty=True, use_dissolve=True, edges=es)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return bm


def frame_matrix(origin, n, up=None):
    """A matrix whose x, y run along a surface with normal n at origin (z = n)."""
    n = Vector(n).normalized()
    up = Vector(up) if up is not None else (Vector((0, 0, 1)) if abs(n.z) < 0.9 else Vector((1, 0, 0)))
    u = up.cross(n).normalized()
    v = n.cross(u)
    M = Matrix((u, v, n)).transposed().to_4x4()
    M.translation = Vector(origin)
    return M


def bolt(c, n, r=0.0026, h=0.0013, sides=6, up=None):
    """A hex bolt head (chamfered) standing on a surface at c with normal n; its base sinks a little into it."""
    bm = slab(circle(0, 0, r, sides, 30.0 if sides == 6 else 0.0), plane="XY", w0=-0.0008, w1=h, inset=r * 0.22)
    return transformed(bm, frame_matrix(c, n, up))


def taper(bm, along=0, a0=0.0, a1=1.0, s0=1.0, s1=1.0, axis=1, center=0.0):
    """Scale one coordinate by a factor that runs from s0 to s1 along another (a0 .. a1, clamped)."""
    for vt in bm.verts:
        t = min(1.0, max(0.0, (vt.co[along] - a0) / (a1 - a0)))
        vt.co[axis] = center + (vt.co[axis] - center) * (s0 + (s1 - s0) * t)
    return bm


def ribbon(poly, width, closed=False):
    """The outline of a pen stroke of this width along a polyline (a groove's cutter, a strip)."""
    if closed:
        p = ccw(poly)
        return offset_poly(p, -width / 2), [offset_poly(p, width / 2)]
    P = [Vector(p) for p in poly]
    left, right = [], []
    for i, p in enumerate(P):
        d = (P[min(i + 1, len(P) - 1)] - P[max(i - 1, 0)]).normalized()
        nrm = Vector((-d.y, d.x)) * (width / 2)
        left.append(tuple(p + nrm)); right.append(tuple(p - nrm))
    return ccw(left + right[::-1]), []


def mirror_y(bm):
    """bm plus its copy mirrored across y = 0 (for building, not as a modifier)."""
    me = bpy.data.meshes.new("_tmp_m"); bm.to_mesh(me)
    other = bmesh.new(); other.from_mesh(me); bpy.data.meshes.remove(me)
    bmesh.ops.scale(other, vec=(1, -1, 1), verts=other.verts)
    bmesh.ops.reverse_faces(other, faces=other.faces)
    return join(bm, other)


# ---------------------------------------------------------------- objects

def round_edges(bm, pred):
    """Mark the edges pred(edge) accepts for part()'s rounded (weighted) bevel."""
    lay = bm.edges.layers.float.get("bevel_weight_edge") or bm.edges.layers.float.new("bevel_weight_edge")
    n = 0
    for e in bm.edges:
        if len(e.link_faces) == 2 and pred(e):
            e[lay] = 1.0; n += 1
    return n


def side_silhouette(min_abs_y, axis=1):
    """Edges where a big flank (facing +-axis, beyond min_abs_y) meets the rest: a part's outline seen from the side."""
    def pred(e):
        f0, f1 = e.link_faces
        s0, s1 = abs(f0.normal[axis]) > 0.95, abs(f1.normal[axis]) > 0.95
        return s0 != s1 and all(abs(v.co[axis]) > min_abs_y for v in e.verts) and f0.normal.dot(f1.normal) < 0.5
    return pred


def radial_rim(r_min, cy=0.0, cz=BORE_Z):
    """Edges of a part turned about the barrel axis that lie beyond r_min and are sharp: its outer rims."""
    def pred(e):
        f0, f1 = e.link_faces
        return (all(math.hypot(v.co.y - cy, v.co.z - cz) > r_min for v in e.verts) and f0.normal.dot(f1.normal) < 0.5)
    return pred


def part(name, bm, mat, bevel=0.0012, segments=1, mirror=False, angle=40, uv_axis="x", parent_col=None, rim=None,
         cuts=None):
    """bm as an object of the rebuild: material, a bevel on edges over `angle`, area-weighted normals (big flat
    faces stay flat, cylinders stay round), optional Mirror across y. rim = (edge predicate, width, segments): a
    larger rounded bevel on just those edges, before the general one. cuts (the high-detail copy's grooves) are cut
    into the finished part, bevels and mirror applied, so its rims match the game part exactly."""
    name = TARGET["prefix"] + name
    remove(name)
    tidy(bm, 0.05)
    if rim:
        round_edges(bm, rim[0])
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me); bm.free()
    ob = bpy.data.objects.new(name, me)
    (parent_col or collection(TARGET["col"])).objects.link(ob)
    me.materials.append(material(mat))
    me.shade_smooth()
    if rim:
        r = ob.modifiers.new("Round", "BEVEL")
        r.width = rim[1]; r.segments = rim[2]; r.limit_method = "WEIGHT"; r.profile = 0.5
        r.miter_outer = "MITER_ARC"; r.use_clamp_overlap = True
    if bevel:
        b = ob.modifiers.new("Bevel", "BEVEL")
        b.width = bevel; b.segments = segments; b.limit_method = "ANGLE"; b.angle_limit = math.radians(angle)
        b.harden_normals = False; b.miter_outer = "MITER_ARC"; b.use_clamp_overlap = True
        b.profile = 0.6
    else:
        # no bevel: keep its hard corners hard
        if hasattr(me, "set_sharp_from_angle"):
            me.set_sharp_from_angle(angle=math.radians(angle))
    wn = ob.modifiers.new("WeightedNormal", "WEIGHTED_NORMAL")
    wn.mode = "FACE_AREA"; wn.weight = 100; wn.keep_sharp = True; wn.thresh = 1e-7
    if mirror:
        mm = ob.modifiers.new("Mirror", "MIRROR")
        mm.use_axis = (False, True, False); mm.use_bisect_axis = (False, False, False)
    ob["uv_axis"] = uv_axis
    ob["mat"] = mat
    if cuts:
        dg = bpy.context.evaluated_depsgraph_get()
        ev = ob.evaluated_get(dg)
        done = bmesh.new(); done.from_mesh(ev.to_mesh()); ev.to_mesh_clear()
        done = boolean(done, cuts, self_=True)
        for f in done.faces:
            f.smooth = True
        for m in list(ob.modifiers):
            if m.type in ("BEVEL", "MIRROR"):
                ob.modifiers.remove(m)
        done.to_mesh(me); done.free()
        # the grooves' walls stay crisp (the bevels' own steps are at most 45 degrees, so they stay smooth)
        me.set_sharp_from_angle(angle=math.radians(55))
    return ob


def tri_count(objs=None):
    dg = bpy.context.evaluated_depsgraph_get()
    total = 0
    for ob in (objs or collection().objects):
        if ob.type != "MESH":
            continue
        me = ob.evaluated_get(dg).to_mesh()
        total += sum(len(p.vertices) - 2 for p in me.polygons)
        ob.evaluated_get(dg).to_mesh_clear()
    return total


def frame(objs=None, center=None, size=None, view=(62, 0, -38), dist_k=1.6):
    """Point Tony's viewport at what's being built: a 3/4 view on the parts (or a box)."""
    if center is None:
        pts = []
        for ob in objs:
            bb = [ob.matrix_world @ Vector(c) for c in ob.bound_box]
            if all(abs(x) < 10 for p in bb for x in p):      # an empty mesh's box is garbage
                pts += bb
        lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
        hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
        center = (lo + hi) / 2; size = (hi - lo).length
    from mathutils import Euler
    for win in bpy.context.window_manager.windows:
        for area in win.screen.areas:
            if area.type == "VIEW_3D":
                r3d = area.spaces.active.region_3d
                r3d.view_perspective = "PERSP"
                r3d.view_location = Vector(center)
                r3d.view_rotation = Euler([math.radians(a) for a in view], "XYZ").to_quaternion()
                r3d.view_distance = max(size, 0.02) * dist_k
                area.tag_redraw()
