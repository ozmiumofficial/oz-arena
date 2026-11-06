"""Reference and rebuild rendered from the same cameras (Workbench), one PNG per view and model, for side-by-side sheets.

Run inside the live Blender (or -b): exec with OUT (folder), VIEWS (names from CAMS) and MODES set in the namespace, or
call render(out, views, look). look 'solid' = material colors with cavity, 'tex' = the rebuild's baked textures."""
import bpy, math, os
from mathutils import Vector, Euler

CAMS = {  # name: (direction the camera looks from, ortho scale, target)
    "left": ((0, -1, 0), 1.06, (0, 0, 0.16)),
    "right": ((0, 1, 0), 1.06, (0, 0, 0.16)),
    "top": ((0, 0, 1), 1.06, (0, 0, 0.16)),
    "front34": ((-0.75, -0.6, 0.45), 1.0, (0, 0, 0.16)),
    "rear34": ((0.75, 0.6, 0.40), 1.0, (0, 0, 0.16)),
    "under34": ((-0.45, -0.75, -0.45), 1.0, (0, 0, 0.16)),
    "muzzle": ((-0.9, -0.35, 0.25), 0.45, (-0.40, 0, 0.21)),
    "body34": ((0.35, -0.8, 0.40), 0.62, (0.22, 0, 0.16)),
    "first": ((0.9, -0.45, 0.35), 0.75, (-0.05, 0, 0.20)),
}


def _set_visible(col_name, on):
    lc = bpy.context.view_layer.layer_collection.children.get(col_name)
    if lc:
        lc.exclude = not on


def render(out, views=("left", "front34", "rear34"), look="solid", res=(1600, 800), models=("ref", "new"), new_col="Final"):
    sc = bpy.context.scene
    os.makedirs(out, exist_ok=True)
    keep = (sc.render.engine, sc.camera, sc.render.resolution_x, sc.render.resolution_y, sc.render.film_transparent)
    sc.render.engine = "BLENDER_WORKBENCH"
    sh = sc.display.shading
    sh.light = "STUDIO"; sh.show_cavity = True; sh.cavity_type = "BOTH"; sh.show_specular_highlight = True
    sc.render.film_transparent = False
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.resolution_percentage = 100
    sc.render.image_settings.file_format = "PNG"; sc.render.image_settings.color_mode = "RGB"
    cd = bpy.data.cameras.new("cmp_cam"); cam = bpy.data.objects.new("cmp_cam", cd)
    sc.collection.objects.link(cam); sc.camera = cam
    ref = bpy.data.objects.get("REF_tripo")
    ghost = ref.active_material.diffuse_color[:] if ref else None
    if ref:
        ref.active_material.diffuse_color = ghost[:3] + (1.0,)
    try:
        for m in models:
            _set_visible("Reference", m == "ref")
            for c in ("Rebuild", "Final"):
                _set_visible(c, m == "new" and c == new_col)
            sh.color_type = "TEXTURE" if (m == "ref" or look == "tex") else "MATERIAL"
            for v in views:
                d, ortho, tgt = CAMS[v]
                d = Vector(d).normalized(); tgt = Vector(tgt)
                cam.location = tgt + d * 2.0
                up = Vector((0, 1, 0)) if abs(d.z) > 0.99 else Vector((0, 0, 1))
                cam.rotation_euler = (-d).to_track_quat("-Z", "Y" if abs(d.z) > 0.99 else "Y").to_euler()
                cd.type = "ORTHO"; cd.ortho_scale = ortho
                cd.clip_start = 0.01; cd.clip_end = 10
                sc.render.filepath = os.path.join(out, "%s_%s.png" % (v, m))
                bpy.ops.render.render(write_still=True)
    finally:
        _set_visible("Reference", True); _set_visible(new_col, True)
        if ref:
            ref.active_material.diffuse_color = ghost
        sc.camera = keep[1]
        bpy.data.objects.remove(cam); bpy.data.cameras.remove(cd)
        sc.render.engine, _, sc.render.resolution_x, sc.render.resolution_y, sc.render.film_transparent = keep
