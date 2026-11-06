"""The Tripo railgun as the rebuild's reference: imported, turned into the work frame, measured and rendered.

    blender -b --factory-startup --python study.py -- <tripo.fbx> <out dir>

Work frame (the rocket launcher rebuild's, so rl_lib's planes, lathes and mirror hold): x along the gun with the
muzzle at -x, y the gun's right (id's left is -y), z up. Tripo's railgun has its muzzle at -y (weapons/railgun.json),
so it is turned a quarter turn about z: work (x, y, z) = Tripo (y, -x, z). Scale stays Tripo's.

Writes into <out dir>: ref.blend (REF_tripo in collection 'Reference', textured), ref_mesh.npz (co, tv: triangles),
views.json (each study view's camera: pixel = c + (p - center) . (u, v) * px), view_<name>.png (texture) and
clay_<name>.png (gray with cavity) for left, right, top, bottom, front, back.
"""
import bpy, bmesh, sys, os, json, math
import numpy as np
from mathutils import Matrix, Vector

a = sys.argv[sys.argv.index("--") + 1:]
SRC, OUT = a[0], a[1]
PX = float(a[2]) if len(a) > 2 else 4000.0
os.makedirs(OUT, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=SRC)
ob = next(o for o in bpy.context.scene.objects if o.type == "MESH")
for o in list(bpy.context.scene.objects):
    if o is not ob:
        bpy.data.objects.remove(o)
bpy.context.view_layer.objects.active = ob
ob.select_set(True)
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
ob.data.transform(Matrix.Rotation(-math.pi / 2, 4, "Z"))      # Tripo (x, y, z) -> work (y, -x, z)
ob.data.update()
ob.name = "REF_tripo"
col = bpy.data.collections.new("Reference")
bpy.context.scene.collection.children.link(col)
for c in list(ob.users_collection):
    c.objects.unlink(ob)
col.objects.link(ob)

bm = bmesh.new(); bm.from_mesh(ob.data)
bmesh.ops.triangulate(bm, faces=bm.faces)
bm.verts.index_update()
co = np.array([v.co[:] for v in bm.verts], np.float64)
tv = np.array([[v.index for v in f.verts] for f in bm.faces], np.int64)
bm.free()
np.savez(os.path.join(OUT, "ref_mesh.npz"), co=co, tv=tv)
lo, hi = co.min(0), co.max(0)
print("ref verts", len(co), "tris", len(tv), "bounds", lo.round(4), hi.round(4))

# study renders: orthographic, PX pixels per unit, a margin of 0.02 round the gun
sc = bpy.context.scene
sc.render.engine = "BLENDER_WORKBENCH"
sh = sc.display.shading
sh.light = "STUDIO"; sh.show_cavity = True; sh.cavity_type = "BOTH"
sc.render.film_transparent = True
sc.render.image_settings.file_format = "PNG"; sc.render.image_settings.color_mode = "RGBA"
sc.render.resolution_percentage = 100
cd = bpy.data.cameras.new("study"); cam = bpy.data.objects.new("study", cd)
sc.collection.objects.link(cam); sc.camera = cam
cd.type = "ORTHO"; cd.clip_start = 0.001; cd.clip_end = 10
M = 0.02
ctr = (lo + hi) / 2
# name: (camera direction from the target, image right, image up)
VIEWS = {
    "left": ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
    "right": ((0, 1, 0), (-1, 0, 0), (0, 0, 1)),
    "top": ((0, 0, 1), (1, 0, 0), (0, 1, 0)),
    "bottom": ((0, 0, -1), (1, 0, 0), (0, -1, 0)),
    "front": ((-1, 0, 0), (0, -1, 0), (0, 0, 1)),
    "back": ((1, 0, 0), (0, 1, 0), (0, 0, 1)),
}
views = {}
for name, (d, u, v) in VIEWS.items():
    d, u, v = Vector(d), Vector(u), Vector(v)
    ext_u = sum(abs(u[i]) * (hi[i] - lo[i]) for i in range(3)) + 2 * M
    ext_v = sum(abs(v[i]) * (hi[i] - lo[i]) for i in range(3)) + 2 * M
    W, H = int(round(ext_u * PX)), int(round(ext_v * PX))
    sc.render.resolution_x, sc.render.resolution_y = W, H
    cd.ortho_scale = max(ext_u, ext_v)
    c = Vector(ctr)
    cam.location = c + d * 2.0
    R = Matrix((u, v, d)).transposed()             # camera x = image right, y = image up, z = back toward the eye
    cam.rotation_euler = R.to_euler()
    views[name] = {"center": list(c), "u": list(u), "v": list(v), "px": PX, "size": [W, H]}
    for look in ("view", "clay"):
        sh.color_type = "TEXTURE" if look == "view" else "SINGLE"
        sh.single_color = (0.62, 0.62, 0.62)
        sc.render.filepath = os.path.join(OUT, "%s_%s.png" % (look, name))
        bpy.ops.render.render(write_still=True)
json.dump(views, open(os.path.join(OUT, "views.json"), "w"), indent=1)
bpy.data.objects.remove(cam); bpy.data.cameras.remove(cd)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "ref.blend"))
print("study done")
