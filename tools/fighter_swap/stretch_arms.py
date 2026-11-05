"""Blender: lengthen a fighter's arms (skeleton.arm_x) and export the mesh arrays like prep.py.
blender -b --python stretch_arms.py -- fighters/<name>.json in.blend out_dir"""
import bpy, sys, os
import numpy as np
argv = sys.argv[sys.argv.index("--") + 1:]
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import skeleton

sk = skeleton.load(argv[0])
bpy.ops.wm.open_mainfile(filepath=argv[1])
out = argv[2]
ob = next(o for o in bpy.context.scene.objects if o.type == 'MESH')
me = ob.data
co = np.zeros(len(me.vertices) * 3)
me.vertices.foreach_get("co", co)
co = co.reshape(-1, 3)
arm = co[:, 2] > sk.min_z
co[arm, 0] = np.sign(co[arm, 0]) * np.array([sk.arm_x(abs(x)) for x in co[arm, 0]])
me.vertices.foreach_set("co", co.ravel())
me.update()
print("STRETCHED x range", co[:, 0].min(), co[:, 0].max())

me.calc_loop_triangles()
lt = me.loop_triangles
tl = np.zeros(len(lt) * 3, int); lt.foreach_get("loops", tl)
tv = np.zeros(len(lt) * 3, int); lt.foreach_get("vertices", tv)
uvl = np.zeros(len(me.loops) * 2); me.uv_layers.active.data.foreach_get("uv", uvl)
cn = np.zeros(len(me.loops) * 3); me.corner_normals.foreach_get("vector", cn)
os.makedirs(out, exist_ok=True)
np.savez(os.path.join(out, "mesh.npz"), co=co, tri_loops=tl.reshape(-1, 3), tri_verts=tv.reshape(-1, 3),
         uv=uvl.reshape(-1, 2), cnrm=cn.reshape(-1, 3))
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out, "rigged_mesh.blend"))
print("EXPORTED", len(co), "verts", len(lt), "tris")
