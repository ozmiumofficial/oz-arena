"""blender -b --python render_arms.py -- dir texture.jpg
Close renders of the arms of each pose OBJ (from pose_preview.py): framed on the upper body from four sides, so
elbows, shoulders and wrists can be judged."""
import bpy, sys, os, glob, mathutils
argv = sys.argv[sys.argv.index("--") + 1:]
folder, tex = argv[0], argv[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'BLENDER_WORKBENCH'
sc.display.shading.light = 'STUDIO'
sc.display.shading.color_type = 'TEXTURE'
sc.display.shading.show_cavity = True
sc.render.resolution_x, sc.render.resolution_y = 600, 480
img = bpy.data.images.load(tex)
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
sc.collection.objects.link(cam); sc.camera = cam
cam.data.type = 'ORTHO'; cam.data.ortho_scale = 44
for path in sorted(glob.glob(os.path.join(folder, "*.obj"))):
    for o in list(sc.objects):
        if o.type == 'MESH':
            bpy.data.objects.remove(o)
    bpy.ops.wm.obj_import(filepath=path, forward_axis='Y', up_axis='Z')
    body = None
    for o in sc.objects:
        if o.type != 'MESH':
            continue
        for slot in o.material_slots:
            m = slot.material
            if m and m.name.startswith("mech"):
                m.use_nodes = True
                t = m.node_tree.nodes.new("ShaderNodeTexImage"); t.image = img
                m.node_tree.links.new(t.outputs[0], m.node_tree.nodes["Principled BSDF"].inputs[0])
                body = o
            elif m:
                m.diffuse_color = (0.9, 0.15, 0.1, 1)
    bb = [body.matrix_world @ mathutils.Vector(c) for c in body.bound_box]
    top = max(v.z for v in bb)
    center = mathutils.Vector((0, 0, top - 22))
    for view, d in {"front": (1, -0.15, 0.1), "right": (0.1, -1, 0.15), "left": (0.1, 1, 0.15),
                    "back": (-1, 0.2, 0.2)}.items():
        d = mathutils.Vector(d).normalized()
        cam.location = center + d * 150
        cam.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()
        sc.render.filepath = path[:-4] + "_arms_" + view + ".png"
        bpy.ops.render.render(write_still=True)
