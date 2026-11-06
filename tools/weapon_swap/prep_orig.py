"""blender -b --python prep_orig.py -- in.fbx out.blend out.npz
Tripo's mesh, transforms applied and welded, as 'orig' (kept for baking its texture) and as triangles for
solid.py. No hole capping: solid.py's winding numbers don't need a closed mesh."""
import bpy, bmesh, sys
import numpy as np
a = sys.argv[sys.argv.index("--") + 1:]
src, out, npz = a[0], a[1], a[2]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=src)
ob = next(o for o in bpy.context.scene.objects if o.type == 'MESH')
for o in list(bpy.context.scene.objects):
    if o is not ob:
        bpy.data.objects.remove(o)
bpy.context.view_layer.objects.active = ob
ob.select_set(True)
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
ob.name = "orig"
bm = bmesh.new()
bm.from_mesh(ob.data)
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.0004)
bmesh.ops.dissolve_degenerate(bm, edges=bm.edges, dist=1e-7)
bm.to_mesh(ob.data)
tri = bm.copy()
bmesh.ops.triangulate(tri, faces=tri.faces)
tri.verts.index_update()
v = np.array([x.co[:] for x in tri.verts], np.float64)
f = np.array([[x.index for x in t.verts] for t in tri.faces], np.int64)
bm.free(); tri.free()
np.savez(npz, v=v, f=f)
print("orig", len(v), "verts", len(f), "tris", "bounds", v.min(0), v.max(0))
bpy.ops.wm.save_as_mainfile(filepath=out)
