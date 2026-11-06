"""blender -b --python geo.py -- orig.blend high.npz out.blend target_tris
The watertight surface from solid.py as 'high' (stray crumbs dropped), decimated to target_tris as 'low', beside
Tripo's welded mesh 'orig' (prep_orig.py), which the bake reads the old texture from."""
import bpy, bmesh, sys, time
import numpy as np
a = sys.argv[sys.argv.index("--") + 1:]
src, hnpz, out, target = a[0], a[1], a[2], int(a[3])
bpy.ops.wm.open_mainfile(filepath=src)
d = np.load(hnpz)
v, f = d["v"].astype(np.float64), d["f"].astype(np.int64)
me = bpy.data.meshes.new("high")
me.from_pydata(v.tolist(), [], f.tolist())
me.validate()
hi = bpy.data.objects.new("high", me)
bpy.context.scene.collection.objects.link(hi)


def tris(o):
    return sum(len(p.vertices) - 2 for p in o.data.polygons)


bm = bmesh.new()
bm.from_mesh(hi.data)
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-7)
seen, drop = set(), []
for vv in bm.verts:
    if vv in seen:
        continue
    comp, st = [], [vv]
    while st:
        x = st.pop()
        if x in seen:
            continue
        seen.add(x)
        comp.append(x)
        st.extend(e.other_vert(x) for e in x.link_edges)
    if len(comp) < 300:
        drop.extend(comp)
bmesh.ops.delete(bm, geom=drop, context='VERTS')
bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
bnd = sum(1 for e in bm.edges if e.is_boundary)
nm = sum(1 for e in bm.edges if len(e.link_faces) > 2)
bm.to_mesh(hi.data)
bm.free()
print("high tris %d: dropped %d crumb verts, boundary %d nonmanifold %d" % (tris(hi), len(drop), bnd, nm))

t = time.time()
lo = hi.copy()
lo.data = hi.data.copy()
lo.name = "low"
bpy.context.scene.collection.objects.link(lo)
dm = lo.modifiers.new("dec", 'DECIMATE')
dm.decimate_type = 'COLLAPSE'
dm.ratio = min(1.0, target / max(1, tris(hi)))
dm.use_collapse_triangulate = True
bpy.context.view_layer.objects.active = lo
for o in bpy.context.scene.objects:
    o.select_set(o is lo)
bpy.ops.object.modifier_apply(modifier="dec")
print("low tris %d (%.1fs)" % (tris(lo), time.time() - t))
for o in (hi, lo):
    o.data.shade_smooth()
    if hasattr(o.data, "set_sharp_from_angle"):
        o.data.set_sharp_from_angle(angle=0.6)
bpy.ops.wm.save_as_mainfile(filepath=out)
