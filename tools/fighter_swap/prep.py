"""A Tripo model's cleanup in Blender: weld, cap holes, decimate (only past target_tris), export arrays.
blender -b --python prep.py -- in.fbx out_dir target_tris [repair]

repair (a fighter config's "repair": true, for meshes that come with real holes, like orbb's): before capping, drop
zero-area and doubled faces, split the edges three or more faces share (fins and cables fused into the body) so each
side gets its own cap, and turn every shell's faces the same way out; then cap again until no open edge is left."""
import bpy, bmesh, sys, os
import numpy as np
argv = sys.argv[sys.argv.index("--") + 1:]
src, out, target = argv[0], argv[1], int(argv[2])
REPAIR = "repair" in argv[3:]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=src)
ob = next(o for o in bpy.context.scene.objects if o.type == 'MESH')
bpy.context.view_layer.objects.active = ob
ob.select_set(True)
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
me = ob.data


def stats(bm, tag):
    bnd = sum(1 for e in bm.edges if e.is_boundary)
    seen, isl = set(), 0
    for v in bm.verts:
        if v in seen:
            continue
        isl += 1
        st = [v]
        while st:
            x = st.pop()
            if x in seen:
                continue
            seen.add(x)
            st.extend(e.other_vert(x) for e in x.link_edges)
    print("STATS %-8s verts %6d faces %6d boundary_edges %5d islands %3d" % (tag, len(bm.verts), len(bm.faces), bnd, isl))


bm = bmesh.new()
bm.from_mesh(me)
stats(bm, "raw")
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.0006)
stats(bm, "welded")
# tiny floating crumbs
bm.verts.ensure_lookup_table()
seen, crumbs = set(), []
for v in bm.verts:
    if v in seen:
        continue
    comp, st = [], [v]
    while st:
        x = st.pop()
        if x in seen:
            continue
        seen.add(x)
        comp.append(x)
        st.extend(e.other_vert(x) for e in x.link_edges)
    if len(comp) < 12:
        crumbs.extend(comp)
bmesh.ops.delete(bm, geom=crumbs, context='VERTS')
bm.verts.index_update()
if REPAIR:
    bmesh.ops.dissolve_degenerate(bm, edges=bm.edges, dist=1e-7)
    seen, dup = {}, []
    for f in bm.faces:
        k = frozenset(v.index for v in f.verts)
        if k in seen:
            dup.append(f)
        else:
            seen[k] = f
    bmesh.ops.delete(bm, geom=dup, context='FACES_ONLY')
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
    nm = [e for e in bm.edges if len(e.link_faces) > 2]
    bmesh.ops.split_edges(bm, edges=nm)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    stats(bm, "repaired")
    print("CAPS doubled %d split %d" % (len(dup), len(nm)))
# cap every open boundary loop, then give the caps the UVs their corners already have
uv = bm.loops.layers.uv.active
caps = 0
for attempt in range(4 if REPAIR else 1):
    open_edges = [e for e in bm.edges if e.is_boundary]
    if not open_edges:
        break
    before = set(bm.faces)
    res = bmesh.ops.holes_fill(bm, edges=open_edges, sides=0)
    new = [f for f in bm.faces if f not in before]
    if not new:
        break
    if not REPAIR:
        bmesh.ops.recalc_face_normals(bm, faces=new)
    for f in new:
        for l in f.loops:
            for ol in l.vert.link_loops:
                if ol.face not in new and ol.face in before:
                    l[uv].uv = ol[uv].uv.copy()
                    break
    tri = bmesh.ops.triangulate(bm, faces=new)
    caps += len(new)
    if REPAIR:     # turn the caps the way the rest of their shell faces
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)


def fan_fill(bm):
    """the loops holes_fill leaves (it won't make a face whose corners another face already has): a fan from a new
    point at the loop's middle, with the UVs of the faces around it"""
    import mathutils
    be, n = {e for e in bm.edges if e.is_boundary}, 0
    while be:
        e0 = be.pop()
        ring, v, e = [e0.verts[0]], e0.verts[1], e0
        while v is not ring[0]:
            ring.append(v)
            nxt = [x for x in v.link_edges if x in be and x is not e]
            if not nxt:
                break
            e = nxt[0]
            be.discard(e)
            v = e.other_vert(v)
        if v is not ring[0] or len(ring) < 3:
            continue
        uvs = []
        for rv in ring:
            l = next((l for l in rv.link_loops), None)
            uvs.append(l[uv].uv.copy() if l else mathutils.Vector((0, 0)))
        mid = bm.verts.new(sum((rv.co for rv in ring), mathutils.Vector()) / len(ring))
        span = max((a - b).length for a in uvs for b in uvs)
        muv = sum(uvs, mathutils.Vector((0, 0))) / len(uvs) if span < 0.05 else uvs[0]
        for i in range(len(ring)):
            a, b = ring[i], ring[(i + 1) % len(ring)]
            try:
                f = bm.faces.new((b, a, mid))
            except ValueError:
                continue
            for l, t in zip(f.loops, (uvs[(i + 1) % len(ring)], uvs[i], muv)):
                l[uv].uv = t
            n += 1
    return n


if REPAIR:     # what's left: fan the loops holes_fill wouldn't take, split edges the caps made three-sided, again
    for attempt in range(4):
        nm = [e for e in bm.edges if len(e.link_faces) > 2]
        if nm:
            bmesh.ops.split_edges(bm, edges=nm)
        if not any(e.is_boundary for e in bm.edges):
            break
        bmesh.ops.holes_fill(bm, edges=[e for e in bm.edges if e.is_boundary], sides=0)
        caps += fan_fill(bm)
        bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 4])
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    print("CAPS left open %d, three-sided %d" % (sum(1 for e in bm.edges if e.is_boundary),
                                                  sum(1 for e in bm.edges if len(e.link_faces) > 2)))
stats(bm, "capped")
print("CAPS", caps)
bm.to_mesh(me)
bm.free()
me.update()

ntri = sum(len(p.vertices) - 2 for p in me.polygons)
if ntri > target:
    mod = ob.modifiers.new("dec", 'DECIMATE')
    mod.ratio = target / ntri
    bpy.ops.object.modifier_apply(modifier="dec")
bpy.ops.object.mode_set(mode='OBJECT')
me = ob.data
me.shade_smooth()
me.calc_loop_triangles()
bm = bmesh.new(); bm.from_mesh(me); stats(bm, "final"); bm.free()

nv = len(me.vertices)
co = np.zeros(nv * 3); me.vertices.foreach_get("co", co)
lt = me.loop_triangles
tl = np.zeros(len(lt) * 3, int); lt.foreach_get("loops", tl)
tv = np.zeros(len(lt) * 3, int); lt.foreach_get("vertices", tv)
uvl = np.zeros(len(me.loops) * 2); me.uv_layers.active.data.foreach_get("uv", uvl)
cn = np.zeros(len(me.loops) * 3); me.corner_normals.foreach_get("vector", cn)
os.makedirs(out, exist_ok=True)
np.savez(os.path.join(out, "clean.npz"), co=co.reshape(-1, 3), tri_loops=tl.reshape(-1, 3),
         tri_verts=tv.reshape(-1, 3), uv=uvl.reshape(-1, 2), cnrm=cn.reshape(-1, 3))
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out, "clean.blend"))
print("EXPORTED", nv, "verts", len(lt), "tris")
