"""blender -b --python export_dump.py -- baked.blend out.npz
'low' per corner, as Blender shades it: position, split normal (smooth, sharp past 34 degrees), UV and
MikkTSpace tangent with its bitangent sign (the frame the normal map was baked in). export_iqm.py welds the
corners back into vertices."""
import bpy, sys
import numpy as np
a = sys.argv[sys.argv.index("--") + 1:]
bpy.ops.wm.open_mainfile(filepath=a[0])
me = bpy.data.objects["low"].data
assert all(len(p.vertices) == 3 for p in me.polygons)
me.calc_tangents(uvmap=me.uv_layers[0].name)
n = len(me.loops)
vi = np.zeros(n, np.int32); me.loops.foreach_get("vertex_index", vi)
co = np.zeros(len(me.vertices) * 3, np.float32); me.vertices.foreach_get("co", co)
nrm = np.zeros(n * 3, np.float32); me.corner_normals.foreach_get("vector", nrm)
uv = np.zeros(n * 2, np.float32); me.uv_layers[0].data.foreach_get("uv", uv)
tan = np.zeros(n * 3, np.float32); me.loops.foreach_get("tangent", tan)
sgn = np.zeros(n, np.float32); me.loops.foreach_get("bitangent_sign", sgn)
np.savez(a[1], pos=co.reshape(-1, 3)[vi], nrm=nrm.reshape(-1, 3), uv=uv.reshape(-1, 2), tan=tan.reshape(-1, 3), sgn=sgn)
print("corners", n)
