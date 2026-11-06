"""blender -b --python dump_low.py -- geo.blend out.npz : low's verts + triangles (it is all triangles)"""
import bpy, sys
import numpy as np
a = sys.argv[sys.argv.index("--") + 1:]
bpy.ops.wm.open_mainfile(filepath=a[0])
me = bpy.data.objects["low"].data
v = np.zeros(len(me.vertices) * 3, np.float32); me.vertices.foreach_get("co", v)
assert all(len(p.vertices) == 3 for p in me.polygons)
f = np.zeros(len(me.polygons) * 3, np.int32); me.polygons.foreach_get("vertices", f)
np.savez(a[1], v=v.reshape(-1, 3), f=f.reshape(-1, 3))
print("dumped", len(v) // 3, len(f) // 3)
