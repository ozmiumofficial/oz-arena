"""blender -b --python bake_curv.py -- geo.blend uv.npz out.png size
Curvature of the sealed surface ('high') baked onto 'low' in its new UVs: Cycles' pointiness (0.5 flat, above
convex, below concave), so the texture cleanup can put edge wear on real edges and grime in real grooves, and
snap painted lines to the shape (texture.py). 16-bit, gray."""
import bpy, sys
import numpy as np
a = sys.argv[sys.argv.index("--") + 1:]
blend, uvp, out, size = a[0], a[1], a[2], int(a[3])
bpy.ops.wm.open_mainfile(filepath=blend)
sc = bpy.context.scene
high, low = bpy.data.objects["high"], bpy.data.objects["low"]
bpy.data.objects.remove(bpy.data.objects["orig"])
sc.render.engine = 'CYCLES'
try:
    pr = bpy.context.preferences.addons['cycles'].preferences
    pr.compute_device_type = 'OPTIX'; pr.get_devices()
    for d in pr.devices: d.use = True
    sc.cycles.device = 'GPU'
except Exception as e:
    print("cpu", e)
sc.cycles.samples = 4

me = low.data
while me.uv_layers:
    me.uv_layers.remove(me.uv_layers[0])
uvl = me.uv_layers.new(name="UVMap")
uv = np.load(uvp)["uv"].reshape(-1, 2)
assert len(uv) == len(me.loops)
uvl.data.foreach_set("uv", uv.ravel())

# high: emission = pointiness
m = bpy.data.materials.new("curv"); m.use_nodes = True
N, L = m.node_tree.nodes, m.node_tree.links
N.clear()
o = N.new("ShaderNodeOutputMaterial")
em = N.new("ShaderNodeEmission")
geo = N.new("ShaderNodeNewGeometry")
L.new(geo.outputs["Pointiness"], em.inputs["Color"])
L.new(em.outputs[0], o.inputs[0])
high.data.materials.clear(); high.data.materials.append(m)

# low: target image
tm = bpy.data.materials.new("tgt"); tm.use_nodes = True
tex = tm.node_tree.nodes.new("ShaderNodeTexImage")
img = bpy.data.images.new("curv", size, size, alpha=False, float_buffer=True)
img.colorspace_settings.name = 'Non-Color'
img.generated_color = (0.5, 0.5, 0.5, 1)
tex.image = img
tm.node_tree.nodes.active = tex
low.data.materials.clear(); low.data.materials.append(tm)

for ob in sc.objects:
    ob.select_set(ob in (high, low))
    ob.hide_render = ob not in (high, low)
bpy.context.view_layer.objects.active = low
bk = sc.render.bake
bk.use_selected_to_active = True
bk.cage_extrusion = 0.003
bk.max_ray_distance = 0.008
bk.margin = 16
bk.margin_type = 'EXTEND'
bpy.ops.object.bake(type='EMIT')
sc.render.image_settings.color_depth = '16'
img.filepath_raw = out
img.file_format = 'PNG'
img.save_render(out)
print("curvature baked", out)
