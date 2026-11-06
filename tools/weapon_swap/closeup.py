"""blender -b --python closeup.py -- file.blend objs out_prefix view x0,y0,x1,y1 [res]
Close-up clay renders of each named object (comma list) of a .blend, inside faces magenta. The region is a
box in the full view's image (0..1, y down), the view framed on the object as look.py/preview.py frame it."""
import bpy, sys, mathutils
a = sys.argv[sys.argv.index("--") + 1:]
blend, names, prefix, view = a[0], a[1].split(","), a[2], a[3]
x0, y0, x1, y1 = (float(v) for v in a[4].split(","))
res = int(a[5]) if len(a) > 5 else 1600
bpy.ops.wm.open_mainfile(filepath=blend)
sc = bpy.context.scene
sc.render.engine = 'CYCLES'
sc.cycles.samples = 32
try:
    pr = bpy.context.preferences.addons['cycles'].preferences
    pr.compute_device_type = 'OPTIX'; pr.get_devices()
    for d in pr.devices: d.use = True
    sc.cycles.device = 'GPU'
except Exception:
    pass
sc.view_settings.view_transform = 'Standard'
w = sc.world or bpy.data.worlds.new("w"); sc.world = w
w.use_nodes = True
w.node_tree.nodes["Background"].inputs[0].default_value = (0.05, 0.05, 0.06, 1)
clay = bpy.data.materials.new("clay"); clay.use_nodes = True
N, L = clay.node_tree.nodes, clay.node_tree.links
out = next(n for n in N if n.type == 'OUTPUT_MATERIAL')
bs = N["Principled BSDF"]; bs.inputs[0].default_value = (0.5, 0.5, 0.5, 1); bs.inputs["Roughness"].default_value = 0.55
geo = N.new("ShaderNodeNewGeometry"); em = N.new("ShaderNodeEmission"); em.inputs[0].default_value = (1, 0, 1, 1)
mx = N.new("ShaderNodeMixShader")
L.new(geo.outputs["Backfacing"], mx.inputs[0]); L.new(bs.outputs[0], mx.inputs[1]); L.new(em.outputs[0], mx.inputs[2])
L.new(mx.outputs[0], out.inputs[0])
ref = bpy.data.objects[names[-1]]
pts = [ref.matrix_world @ v.co for v in ref.data.vertices]
lo = mathutils.Vector([min(p[i] for p in pts) for i in range(3)]); hi = mathutils.Vector([max(p[i] for p in pts) for i in range(3)])
ctr = (lo + hi) / 2; R = (hi - lo).length
V = {"iso_a": (1, -1, 1), "iso_b": (-1, -1, 1), "iso_c": (1, 1, 1), "iso_d": (-1, 1, 1), "side": (0, -1, 0.0001),
     "side_r": (0, 1, 0.0001), "top": (0.0001, 0, 1), "left": (-1, 0.0001, 0.0001), "right": (1, 0.0001, 0.0001),
     "bottom": (0.0001, 0, -1)}
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); sc.collection.objects.link(cam); sc.camera = cam
cam.data.type = 'ORTHO'; cam.data.clip_end = R * 20
for d_, e in (((0.5, -0.8, 1.0), 60), ((-0.8, 0.6, 0.4), 22), ((1, 0.2, 0.3), 30), ((0, 0.3, -1), 8)):
    l = bpy.data.objects.new("l", bpy.data.lights.new("l", 'AREA')); l.data.energy = e * R * R; l.data.size = R
    v = mathutils.Vector(d_).normalized(); l.location = ctr + v * R * 2
    l.rotation_euler = (-v).to_track_quat('-Z', 'Y').to_euler(); sc.collection.objects.link(l)
d = mathutils.Vector(V[view]).normalized()
cam.location = ctr + d * R * 5
cam.rotation_euler = (-d).to_track_quat('-Z', 'Z' if abs(d.z) < 0.99 else 'Y').to_euler()
bpy.context.view_layer.update()
inv = cam.matrix_world.inverted()
xs = [(inv @ p).x for p in pts]; ys = [(inv @ p).y for p in pts]
cx, cy = (max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2
scale = max(max(xs) - min(xs), (max(ys) - min(ys)) / 0.75) * 1.08
# full frame: width scale, height 0.75 scale; the region's centre and size within it
rx, ry = (x0 + x1) / 2 - 0.5, 0.5 - (y0 + y1) / 2
cam.location = cam.matrix_world @ mathutils.Vector((cx + rx * scale, cy + ry * scale * 0.75, 0))
wr, hr = (x1 - x0) * scale, (y1 - y0) * scale * 0.75
cam.data.ortho_scale = max(wr, hr)
sc.render.resolution_x = int(res * wr / max(wr, hr)); sc.render.resolution_y = int(res * hr / max(wr, hr))
for o in sc.objects:
    if o.type == 'MESH':
        o.data.materials.clear(); o.data.materials.append(clay)
for n in names:
    for o in sc.objects:
        if o.type == 'MESH':
            o.hide_render = o.name != n
    sc.render.filepath = "%s_%s.png" % (prefix, n)
    bpy.ops.render.render(write_still=True)
