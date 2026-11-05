"""A Tripo model's cleanup in Blender: weld, cap holes, decimate (only past target_tris), export arrays.
blender -b --python prep.py -- in.fbx out_dir target_tris"""
import bpy, bmesh, sys, os
import numpy as np
argv = sys.argv[sys.argv.index("--") + 1:]
src, out, target = argv[0], argv[1], int(argv[2])
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
# cap every open boundary loop, then give the caps the UVs their corners already have
uv = bm.loops.layers.uv.active
before = set(bm.faces)
res = bmesh.ops.holes_fill(bm, edges=[e for e in bm.edges if e.is_boundary], sides=0)
new = [f for f in bm.faces if f not in before]
bmesh.ops.recalc_face_normals(bm, faces=new)
for f in new:
    for l in f.loops:
        for ol in l.vert.link_loops:
            if ol.face not in new and ol.face in before:
                l[uv].uv = ol[uv].uv.copy()
                break
tri = bmesh.ops.triangulate(bm, faces=new)
stats(bm, "capped")
print("CAPS", len(new))
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
