"""blender -b --python dump_final.py -- model.blend object out.npz
Per-corner data of the finished mesh (triangles): positions, split normals, UVs (Blender's, v up)."""
import bpy, sys
import numpy as np
a = sys.argv[sys.argv.index("--") + 1:]
bpy.ops.wm.open_mainfile(filepath=a[0])
ob = bpy.data.objects[a[1]]
me = ob.data
me.calc_loop_triangles()
n = len(me.loop_triangles)
tri_loops = np.zeros(n * 3, np.int32); me.loop_triangles.foreach_get("loops", tri_loops)
tri_verts = np.zeros(n * 3, np.int32); me.loop_triangles.foreach_get("vertices", tri_verts)
co = np.zeros(len(me.vertices) * 3, np.float32); me.vertices.foreach_get("co", co)
cn = np.zeros(len(me.loops) * 3, np.float32); me.corner_normals.foreach_get("vector", cn)
uv = np.zeros(len(me.loops) * 2, np.float32); me.uv_layers.active.data.foreach_get("uv", uv)
np.savez(a[2], co=co.reshape(-1, 3), tri_loops=tri_loops.reshape(-1, 3), tri_verts=tri_verts.reshape(-1, 3),
         cn=cn.reshape(-1, 3), uv=uv.reshape(-1, 2))
print("dumped", n, "tris")
