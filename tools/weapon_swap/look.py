"""blender -b --python look.py -- model(.fbx|.blend obj name) out_prefix [res] [mode]
Renders 6 views (iso x4, side, top). Inside-facing surfaces (seen through holes / flipped normals) are magenta.
mode: tex (default) | clay"""
import bpy, sys, os, math, mathutils
a = sys.argv[sys.argv.index("--") + 1:]
src, prefix = a[0], a[1]
res = int(a[2]) if len(a) > 2 else 900
mode = a[3] if len(a) > 3 else "tex"
bpy.ops.wm.read_factory_settings(use_empty=True)
if src.lower().endswith(".fbx"):
    bpy.ops.import_scene.fbx(filepath=src)
elif src.lower().endswith(".glb"):
    bpy.ops.import_scene.gltf(filepath=src)
elif ".blend:" in src.lower():
    path, name = src.rsplit(":", 1)
    bpy.ops.wm.open_mainfile(filepath=path)
    for o in list(bpy.context.scene.objects):
        if o.name != name:
            bpy.data.objects.remove(o)
else:
    bpy.ops.wm.obj_import(filepath=src)
sc = bpy.context.scene
sc.render.engine = 'CYCLES'
sc.cycles.samples = 48
sc.cycles.use_denoising = True
try:
    pr = bpy.context.preferences.addons['cycles'].preferences
    pr.compute_device_type = 'OPTIX'; pr.get_devices()
    for d in pr.devices: d.use = True
    sc.cycles.device = 'GPU'
except Exception:
    pass
sc.render.film_transparent = True
sc.render.resolution_x = res
sc.render.resolution_y = int(res * 0.75)
sc.view_settings.view_transform = 'Standard'
w = bpy.data.worlds.new("w"); sc.world = w
w.node_tree.nodes["Background"].inputs[0].default_value = (0.32, 0.33, 0.35, 1)
objs = [o for o in sc.objects if o.type == 'MESH']
for o in objs:
    if not o.material_slots or not o.material_slots[0].material:
        mm = bpy.data.materials.new("clay"); mm.use_nodes = True; o.data.materials.append(mm)
for o in objs:
    for s in o.material_slots:
        m = s.material
        if not m:
            continue
        N, L = m.node_tree.nodes, m.node_tree.links
        out = next(n for n in N if n.type == 'OUTPUT_MATERIAL')
        surf = out.inputs[0].links[0].from_socket
        if mode == "clay":
            bs = N.new("ShaderNodeBsdfPrincipled"); bs.inputs[0].default_value = (0.35, 0.35, 0.35, 1)
            bs.inputs["Roughness"].default_value = 0.6
            surf = bs.outputs[0]
        geo = N.new("ShaderNodeNewGeometry")
        em = N.new("ShaderNodeEmission"); em.inputs[0].default_value = (1, 0, 1, 1)
        mx = N.new("ShaderNodeMixShader")
        L.new(geo.outputs["Backfacing"], mx.inputs[0]); L.new(surf, mx.inputs[1]); L.new(em.outputs[0], mx.inputs[2])
        L.new(mx.outputs[0], out.inputs[0])
pts = [o.matrix_world @ v.co for o in objs for v in o.data.vertices]
lo = mathutils.Vector([min(p[i] for p in pts) for i in range(3)])
hi = mathutils.Vector([max(p[i] for p in pts) for i in range(3)])
ctr = (lo + hi) / 2
R = (hi - lo).length
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); sc.collection.objects.link(cam); sc.camera = cam
cam.data.type = 'ORTHO'; cam.data.clip_end = R * 20


def light(d, e):
    l = bpy.data.objects.new("l", bpy.data.lights.new("l", 'AREA'))
    l.data.energy = e * R * R; l.data.size = R
    v = mathutils.Vector(d).normalized(); l.location = ctr + v * R * 2
    l.rotation_euler = (-v).to_track_quat('-Z', 'Y').to_euler(); sc.collection.objects.link(l)


light((0.5, -0.8, 1.0), 40); light((-0.8, 0.6, 0.4), 15); light((0, 0.3, -1), 6)
views = {"iso_a": (1, -1, 1), "iso_b": (-1, -1, 1), "iso_c": (1, 1, 1), "iso_d": (-1, 1, 1),
         "side": (0, -1, 0.0001), "top": (0.0001, 0, 1), "bottom": (0.0001, 0, -1), "front": (1, 0.0001, 0.0001)}
only = a[4].split(",") if len(a) > 4 else list(views)
for name in only:
    d = mathutils.Vector(views[name]).normalized()
    cam.location = ctr + d * R * 5
    cam.rotation_euler = (-d).to_track_quat('-Z', 'Z' if abs(d.z) < 0.99 else 'Y').to_euler()
    bpy.context.view_layer.update()
    inv = cam.matrix_world.inverted()
    xs = [(inv @ p).x for p in pts]; ys = [(inv @ p).y for p in pts]
    cam.location = cam.matrix_world @ mathutils.Vector(((max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2, 0))
    cam.data.ortho_scale = max(max(xs) - min(xs), (max(ys) - min(ys)) / 0.75) * 1.08
    sc.render.filepath = prefix + "_" + name + ".png"
    bpy.ops.render.render(write_still=True)
