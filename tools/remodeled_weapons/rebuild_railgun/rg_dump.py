"""blender -b railgun_rebuild.blend --python rg_dump.py -- out.npz [object]

The rebuilt railgun's game mesh per corner, as tools/weapon_swap/export_iqm.py reads it (export_dump.py's layout):
position, split normal, UV and MikkTSpace tangent with its bitangent sign (the frame the normal map was baked in).
The mesh is already triangles (rg_finalize.py), so these are the bake's own tangents."""
import bpy, sys
import numpy as np

a = sys.argv[sys.argv.index("--") + 1:]
name = a[1] if len(a) > 1 else "railgun"
me = bpy.data.objects[name].data
assert all(len(p.vertices) == 3 for p in me.polygons), "the game mesh must be triangles (rg_finalize.py)"
me.calc_tangents(uvmap=me.uv_layers[0].name)
n = len(me.loops)
vi = np.zeros(n, np.int32); me.loops.foreach_get("vertex_index", vi)
co = np.zeros(len(me.vertices) * 3, np.float32); me.vertices.foreach_get("co", co)
nrm = np.zeros(n * 3, np.float32); me.corner_normals.foreach_get("vector", nrm)
uv = np.zeros(n * 2, np.float32); me.uv_layers[0].data.foreach_get("uv", uv)
tan = np.zeros(n * 3, np.float32); me.loops.foreach_get("tangent", tan)
sgn = np.zeros(n, np.float32); me.loops.foreach_get("bitangent_sign", sgn)
np.savez(a[0], pos=co.reshape(-1, 3)[vi], nrm=nrm.reshape(-1, 3), uv=uv.reshape(-1, 2), tan=tan.reshape(-1, 3), sgn=sgn)
print("corners", n, "triangles", n // 3)
