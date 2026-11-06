"""Reference and rebuild rendered lit (Cycles, an HDRI from Blender's studio lights), both with their PBR textures, from
compare.py's cameras: render(out, views, models). Writes <view>_<ref|new>.png like compare.py, for sheet.py."""
import bpy, math, os
from mathutils import Vector
import compare

HDRI = os.path.join(os.path.dirname(bpy.app.binary_path), "%d.%d" % bpy.app.version[:2], "datafiles", "studiolights",
                    "world", "forest.exr")


def render(out, views, models=("ref", "new"), res=(1400, 900), samples=64, new_col="Final"):
    sc = bpy.context.scene
    os.makedirs(out, exist_ok=True)
    keep = (sc.render.engine, sc.camera, sc.world, sc.render.resolution_x, sc.render.resolution_y,
            sc.render.film_transparent, sc.view_settings.view_transform)
    sc.render.engine = "CYCLES"; sc.cycles.samples = samples; sc.cycles.use_denoising = True
    try:
        sc.cycles.device = "GPU"
    except Exception:
        pass
    w = bpy.data.worlds.new("_pbr_world"); w.use_nodes = True
    nt = w.node_tree
    env = nt.nodes.new("ShaderNodeTexEnvironment"); env.image = bpy.data.images.load(HDRI, check_existing=True)
    bg = nt.nodes.get("Background"); bg.inputs["Strength"].default_value = 1.0
    nt.links.new(env.outputs["Color"], bg.inputs["Color"])
    sc.world = w
    sc.render.film_transparent = False
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.resolution_percentage = 100
    sc.view_settings.view_transform = "AgX"
    cd = bpy.data.cameras.new("pbr_cam"); cam = bpy.data.objects.new("pbr_cam", cd)
    sc.collection.objects.link(cam); sc.camera = cam
    try:
        for m in models:
            compare._set_visible("Reference", m == "ref")
            for c in ("Rebuild", "Final", "High"):
                compare._set_visible(c, m == "new" and c == new_col)
            for v in views:
                d, ortho, tgt = compare.CAMS[v]
                d = Vector(d).normalized(); tgt = Vector(tgt)
                cam.location = tgt + d * 2.0
                cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
                cd.type = "ORTHO"; cd.ortho_scale = ortho; cd.clip_start = 0.01; cd.clip_end = 10
                sc.render.filepath = os.path.join(out, "%s_%s.png" % (v, m))
                bpy.ops.render.render(write_still=True)
    finally:
        compare._set_visible("Reference", True); compare._set_visible(new_col, True)
        sc.camera = keep[1]
        bpy.data.objects.remove(cam); bpy.data.cameras.remove(cd)
        sc.render.engine, _, sc.world, sc.render.resolution_x, sc.render.resolution_y, sc.render.film_transparent, \
            sc.view_settings.view_transform = keep
        bpy.data.worlds.remove(w)
