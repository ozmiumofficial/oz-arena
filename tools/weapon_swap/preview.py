"""blender -b --python preview.py -- baked.blend tex_dir out_prefix res views [maps]
PBR preview of 'low' with tex_dir/{basecolor,normal,roughness,metallic,ao}.png (whatever exists).
maps: comma list to use (default all). Missing basecolor -> bake_base.png, missing normal -> bake_normal.png."""
import bpy, sys, os, mathutils
a = sys.argv[sys.argv.index("--") + 1:]
blend, td, prefix, res, views = a[0], a[1], a[2], int(a[3]), a[4].split(",")
use = a[5].split(",") if len(a) > 5 else ["basecolor", "normal", "roughness", "metallic", "ao"]
bpy.ops.wm.open_mainfile(filepath=blend)
sc = bpy.context.scene
for o in list(sc.objects):
    if o.name != "low":
        bpy.data.objects.remove(o)
low = bpy.data.objects["low"]
sc.render.engine = 'CYCLES'
sc.cycles.samples = 64
sc.cycles.use_denoising = True
try:
    pr = bpy.context.preferences.addons['cycles'].preferences
    pr.compute_device_type = 'OPTIX'; pr.get_devices()
    for d in pr.devices: d.use = True
    sc.cycles.device = 'GPU'
except Exception:
    pass
sc.render.film_transparent = True
sc.render.resolution_x = res; sc.render.resolution_y = int(res * 0.75)
sc.view_settings.view_transform = 'AgX'
w = sc.world or bpy.data.worlds.new("w"); sc.world = w
w.use_nodes = True
w.node_tree.nodes["Background"].inputs[0].default_value = (0.3, 0.31, 0.33, 1)
mat = low.data.materials[0]
N, L = mat.node_tree.nodes, mat.node_tree.links
N.clear()
out = N.new("ShaderNodeOutputMaterial")
bs = N.new("ShaderNodeBsdfPrincipled")
L.new(bs.outputs[0], out.inputs[0])


def img(name, nc):
    for fn in (name + ".png", {"basecolor": "bake_base.png", "normal": "bake_normal.png"}.get(name, "")):
        p = os.path.join(td, fn)
        if fn and os.path.exists(p):
            t = N.new("ShaderNodeTexImage")
            t.image = bpy.data.images.load(p)
            if nc:
                t.image.colorspace_settings.name = 'Non-Color'
            return t
    return None


bc = img("basecolor", False) if "basecolor" in use else None
ao = img("ao", True) if "ao" in use else None
if bc:
    col = bc.outputs[0]
    if ao:
        m = N.new("ShaderNodeMix"); m.data_type = 'RGBA'; m.blend_type = 'MULTIPLY'; m.inputs[0].default_value = 1
        L.new(col, m.inputs[6]); L.new(ao.outputs[0], m.inputs[7]); col = m.outputs[2]
    L.new(col, bs.inputs["Base Color"])
else:
    bs.inputs["Base Color"].default_value = (0.35, 0.35, 0.35, 1)
for name, inp, default in (("roughness", "Roughness", 0.5), ("metallic", "Metallic", 0.0)):
    t = img(name, True) if name in use else None
    if t:
        L.new(t.outputs[0], bs.inputs[inp])
    else:
        bs.inputs[inp].default_value = default
em = img("emissive", False) if "emissive" in use or os.path.exists(os.path.join(td, "emissive.png")) else None
if em:
    L.new(em.outputs[0], bs.inputs["Emission Color"]); bs.inputs["Emission Strength"].default_value = 2.0
nt = img("normal", True) if "normal" in use else None
if nt:
    nm = N.new("ShaderNodeNormalMap"); L.new(nt.outputs[0], nm.inputs[1]); L.new(nm.outputs[0], bs.inputs["Normal"])
pts = [low.matrix_world @ v.co for v in low.data.vertices]
lo = mathutils.Vector([min(p[i] for p in pts) for i in range(3)]); hi = mathutils.Vector([max(p[i] for p in pts) for i in range(3)])
ctr = (lo + hi) / 2; R = (hi - lo).length
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); sc.collection.objects.link(cam); sc.camera = cam
cam.data.type = 'ORTHO'; cam.data.clip_end = R * 20


def light(d, e, size=1.0):
    l = bpy.data.objects.new("l", bpy.data.lights.new("l", 'AREA'))
    l.data.energy = e * R * R; l.data.size = R * size
    v = mathutils.Vector(d).normalized(); l.location = ctr + v * R * 2
    l.rotation_euler = (-v).to_track_quat('-Z', 'Y').to_euler(); sc.collection.objects.link(l)


light((0.5, -0.8, 1.0), 60); light((-0.8, 0.6, 0.4), 22); light((0.2, 0.9, 0.8), 30, 0.5); light((0, 0.3, -1), 8)
V = {"iso_a": (1, -1, 1), "iso_b": (-1, -1, 1), "iso_c": (1, 1, 1), "iso_d": (-1, 1, 1), "side": (0, -1, 0.0001),
     "side_r": (0, 1, 0.0001), "top": (0.0001, 0, 1), "left": (-1, 0.0001, 0.0001), "right": (1, 0.0001, 0.0001),
     "bottom": (0.0001, 0, -1)}
for name in views:
    d = mathutils.Vector(V[name]).normalized()
    cam.location = ctr + d * R * 5
    cam.rotation_euler = (-d).to_track_quat('-Z', 'Z' if abs(d.z) < 0.99 else 'Y').to_euler()
    bpy.context.view_layer.update()
    inv = cam.matrix_world.inverted()
    xs = [(inv @ p).x for p in pts]; ys = [(inv @ p).y for p in pts]
    cam.location = cam.matrix_world @ mathutils.Vector(((max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2, 0))
    cam.data.ortho_scale = max(max(xs) - min(xs), (max(ys) - min(ys)) / 0.75) * 1.08
    sc.render.filepath = prefix + "_" + name + ".png"
    bpy.ops.render.render(write_still=True)
