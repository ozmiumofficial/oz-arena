"""blender -b --python render_hold.py -- dir texture.jpg [full|close]
Renders each pose OBJ (id units: x forward, y left, z up) with the mech texture and the gun in red.
full: three whole-body views. close: four views framed on the gun and hands."""
import bpy, sys, os, glob, mathutils
argv = sys.argv[sys.argv.index("--") + 1:]
folder, tex = argv[0], argv[1]
mode = argv[2] if len(argv) > 2 else "full"
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'BLENDER_WORKBENCH'
sc.display.shading.light = 'STUDIO'
sc.display.shading.color_type = 'TEXTURE'
sc.render.resolution_x, sc.render.resolution_y = (520, 640) if mode == "full" else (560, 420)
img = bpy.data.images.load(tex)
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
sc.collection.objects.link(cam); sc.camera = cam
cam.data.type = 'ORTHO'
for path in sorted(glob.glob(os.path.join(folder, "*.obj"))):
    for o in list(sc.objects):
        if o.type == 'MESH':
            bpy.data.objects.remove(o)
    bpy.ops.wm.obj_import(filepath=path, forward_axis='Y', up_axis='Z')
    gun = None
    for o in sc.objects:
        if o.type != 'MESH':
            continue
        for slot in o.material_slots:
            m = slot.material
            if m and m.name.startswith("mech"):
                m.use_nodes = True
                t = m.node_tree.nodes.new("ShaderNodeTexImage"); t.image = img
                m.node_tree.links.new(t.outputs[0], m.node_tree.nodes["Principled BSDF"].inputs[0])
            elif m:
                m.diffuse_color = (0.9, 0.15, 0.1, 1)
                gun = o
    if mode == "full":
        center, scale = mathutils.Vector((0, 0, 6)), 74
        views = {"q": (0.75, -0.65, 0.25), "front": (1, 0.0, 0.08), "side": (0.0, -1, 0.05)}
    else:
        bb = [gun.matrix_world @ mathutils.Vector(c) for c in gun.bound_box]
        center = sum(bb, mathutils.Vector()) / 8
        scale = 40
        views = {"right": (0.05, -1, 0.15), "left": (0.05, 1, 0.15), "top": (0.02, 0.0, 1), "fq": (0.8, -0.45, 0.35)}
    cam.data.ortho_scale = scale
    for view, d in views.items():
        d = mathutils.Vector(d).normalized()
        cam.location = center + d * 150
        cam.rotation_euler = (-d).to_track_quat('-Z', 'Y' if view == "top" else 'Z').to_euler() if view == "top" else (-d).to_track_quat('-Z', 'Y').to_euler()
        sc.render.filepath = path[:-4] + "_" + mode + "_" + view + ".png"
        bpy.ops.render.render(write_still=True)
