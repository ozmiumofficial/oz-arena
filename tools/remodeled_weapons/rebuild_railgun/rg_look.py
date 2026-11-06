"""Renders for checking the rebuild, from inside the live Blender (Workbench, so they're quick).

shots(out, tag, center, size, views, ...) renders the Rebuild (and optionally the reference: 'ref' beside it with
the same camera, or 'ghost' over it) from named directions around a box, perspective or orthographic, and writes
<out>/<tag>_<view>.png (+ _ref). sheet() puts them side by side for one image to look at."""
import bpy, os, math
from mathutils import Vector

DIRS = {   # where the camera sits, seen from the target (x muzzle -> butt, y the gun's right, z up)
    "left": (0, -1, 0), "right": (0, 1, 0), "top": (0, 0, 1), "bottom": (0, 0, -1), "front": (-1, 0, 0),
    "back": (1, 0, 0),
    "fl": (-0.7, -0.7, 0.35), "fr": (-0.7, 0.7, 0.35), "bl": (0.7, -0.7, 0.35), "br": (0.7, 0.7, 0.35),
    "fl_low": (-0.7, -0.6, -0.45), "fr_low": (-0.7, 0.6, -0.45), "bl_low": (0.7, -0.6, -0.45),
    "br_low": (0.7, 0.6, -0.45), "top_l": (0.2, -0.55, 0.8), "top_r": (0.2, 0.55, 0.8),
    "fp": (0.85, 0.35, 0.40),        # about where the player's eye is, behind and above the right side
}


def _vis(name, on):
    lc = bpy.context.view_layer.layer_collection.children.get(name)
    if lc:
        lc.exclude = not on


def shots(out, tag, center, size, views=("fl", "bl", "fl_low", "br"), res=(1100, 800), ref="side", ortho=False,
          col="Rebuild", cavity=True, xray=False):
    """ref: None (rebuild only), 'side' (also the reference alone, same camera), 'ghost' (reference over it)."""
    sc = bpy.context.scene
    os.makedirs(out, exist_ok=True)
    keep = (sc.render.engine, sc.camera, sc.render.resolution_x, sc.render.resolution_y, sc.render.film_transparent)
    sc.render.engine = "BLENDER_WORKBENCH"
    sh = sc.display.shading
    sh.light = "STUDIO"; sh.show_cavity = cavity; sh.cavity_type = "BOTH"; sh.show_specular_highlight = True
    sh.show_xray = xray
    sh.show_backface_culling = True
    sc.render.film_transparent = False
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.resolution_percentage = 100
    sc.render.image_settings.file_format = "PNG"; sc.render.image_settings.color_mode = "RGB"
    cd = bpy.data.cameras.new("look_cam"); cam = bpy.data.objects.new("look_cam", cd)
    sc.collection.objects.link(cam); sc.camera = cam
    refo = bpy.data.objects.get("REF_tripo")
    ghost = refo.active_material.diffuse_color[:] if refo and refo.active_material else None
    files = []
    center = Vector(center)
    try:
        passes = [("new", False)]
        if ref == "side":
            passes.append(("ref", True))
        for kind, show_ref in passes:
            _vis("Reference", show_ref or ref == "ghost")
            _vis(col, kind == "new")
            if refo and refo.active_material:
                refo.active_material.diffuse_color = (ghost[:3] + (1.0,)) if kind == "ref" else ghost
            sh.color_type = "TEXTURE" if kind == "ref" else "MATERIAL"
            for v in views:
                d = Vector(DIRS[v] if isinstance(v, str) else v).normalized()
                name = v if isinstance(v, str) else "v%d" % views.index(v)
                if ortho:
                    cd.type = "ORTHO"; cd.ortho_scale = size * 1.15
                    cam.location = center + d * 3.0
                else:
                    cd.type = "PERSP"; cd.lens = 50
                    cam.location = center + d * size * 3.0
                up = Vector((0, 0, 1)) if abs(d.z) < 0.95 else Vector((1, 0, 0))
                cx = up.cross(d).normalized(); cy = d.cross(cx)          # camera: x right, y up, z back to the eye
                from mathutils import Matrix
                cam.rotation_euler = Matrix((cx, cy, d)).transposed().to_euler()
                cd.clip_start = 0.002; cd.clip_end = 20
                f = os.path.join(out, "%s_%s%s.png" % (tag, name, "_ref" if kind == "ref" else ""))
                sc.render.filepath = f
                bpy.ops.render.render(write_still=True)
                files.append(f)
    finally:
        _vis("Reference", True); _vis(col, True)
        if refo and ghost:
            refo.active_material.diffuse_color = ghost
        sh.show_xray = False
        sc.camera = keep[1]
        bpy.data.objects.remove(cam); bpy.data.cameras.remove(cd)
        sc.render.engine, _, sc.render.resolution_x, sc.render.resolution_y, sc.render.film_transparent = keep
    return files


def sheet(files, out, cols=2, scale=0.5):
    """The images in a grid (rebuild | reference pairs read left to right)."""
    import numpy as np
    ims = []
    for f in files:
        im = bpy.data.images.load(f, check_existing=False)
        w, h = im.size
        px = np.empty(w * h * 4, np.float32); im.pixels.foreach_get(px)
        ims.append(px.reshape(h, w, 4)); bpy.data.images.remove(im)
    h, w = ims[0].shape[:2]
    rows = (len(ims) + cols - 1) // cols
    big = np.ones((rows * h, cols * w, 4), np.float32)
    for i, a in enumerate(ims):
        r, c = divmod(i, cols)
        big[(rows - 1 - r) * h:(rows - r) * h, c * w:(c + 1) * w] = a[:h, :w]
    img = bpy.data.images.new("_sheet", cols * w, rows * h, alpha=True)
    img.pixels.foreach_set(big.ravel())
    img.filepath_raw = out; img.file_format = "JPEG"
    img.save()
    bpy.data.images.remove(img)
    return out
