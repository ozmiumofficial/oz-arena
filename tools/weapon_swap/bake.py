"""blender -b --python bake.py -- geo.blend out_dir size
On 'low': new UV layout, then bakes base color (from 'orig', Tripo's texture), tangent normal and AO (from 'high').
Texels no ray reached stay pure magenta (1,0,1) in the color bake, for the cleanup step to fill."""
import bpy, sys, os, time
a = sys.argv[sys.argv.index("--") + 1:]
blend, out, size = a[0], a[1], int(a[2])
os.makedirs(out, exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=blend)
sc = bpy.context.scene
orig, high, low = (bpy.data.objects[n] for n in ("orig", "high", "low"))
sc.render.engine = 'CYCLES'
try:
    pr = bpy.context.preferences.addons['cycles'].preferences
    pr.compute_device_type = 'OPTIX'; pr.get_devices()
    for d in pr.devices: d.use = True
    sc.cycles.device = 'GPU'
except Exception as e:
    print("cpu", e)


def only(*objs, active):
    for o in sc.objects:
        o.select_set(o in objs)
        o.hide_render = o not in objs
    bpy.context.view_layer.objects.active = active


# --- UVs for low
t = time.time()
only(low, active=low)
me = low.data
while me.uv_layers:
    me.uv_layers.remove(me.uv_layers[0])
uvl = me.uv_layers.new(name="UVMap")
import numpy as np
uv = np.load(a[3])["uv"].reshape(-1, 2)          # xatlas, per corner in polygon order (atlas.py)
assert len(uv) == len(me.loops)
uvl.data.foreach_set("uv", uv.ravel())
print("uv %.1fs" % (time.time() - t))

# --- low's bake material: one image node, swapped per pass
low.data.materials.clear()
mat = bpy.data.materials.new("weapon")
mat.use_nodes = True
low.data.materials.append(mat)
N = mat.node_tree.nodes
tex = N.new("ShaderNodeTexImage")
N.active = tex


def target(name, color=(0, 0, 0, 1), non_color=False):
    img = bpy.data.images.new(name, size, size, alpha=False, float_buffer=non_color)
    img.generated_color = color
    if non_color:
        img.colorspace_settings.name = 'Non-Color'
    tex.image = img
    N.active = tex
    return img


def save(img, fn):
    img.filepath_raw = os.path.join(out, fn)
    img.file_format = 'PNG'
    if img.is_float:
        img.save_render(img.filepath_raw)   # 16-bit png from the float buffer
    else:
        img.save()


bk = sc.render.bake
bk.margin = 16
bk.margin_type = 'EXTEND'

# base color from Tripo's mesh + texture
t = time.time()
img = target("base", (1, 0, 1, 1))
only(orig, low, active=low)
sc.cycles.samples = 1
bk.use_selected_to_active = True
bk.cage_extrusion = 0.004
bk.max_ray_distance = 0.012
bk.use_pass_direct = False
bk.use_pass_indirect = False
bk.use_pass_color = True
bk.margin = 0                      # keep misses magenta; cleanup fills + pads
bpy.ops.object.bake(type='DIFFUSE')
save(img, "bake_base.png")
print("base %.1fs" % (time.time() - t))

# normal from the 5M-tri sealed surface
t = time.time()
bk.margin = 16
sc.render.image_settings.color_depth = '16'
img = target("normal", (0.5, 0.5, 1, 1), non_color=True)
only(high, low, active=low)
bk.cage_extrusion = 0.003
bk.max_ray_distance = 0.008
bk.normal_space = 'TANGENT'
bpy.ops.object.bake(type='NORMAL')
save(img, "bake_normal.png")
print("normal %.1fs" % (time.time() - t))

# AO from the sealed surface
t = time.time()
img = target("ao", (1, 1, 1, 1), non_color=True)
sc.cycles.samples = 128
if sc.world is None:
    sc.world = bpy.data.worlds.new('w')
sc.world.light_settings.distance = 0.08
bpy.ops.object.bake(type='AO')
save(img, "bake_ao.png")
print("ao %.1fs" % (time.time() - t))

# keep high out of the saved file: it is 5M tris
bpy.data.objects.remove(high)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out, "baked.blend"))
