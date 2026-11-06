"""Renders of the finished game mesh with its maps (EEVEE), for checking bakes: preview(out, tag, maps, views).

maps: a folder with basecolor.png / orm.png / normal.png (rg_post.py pack's names), or normal_only=True to see just
a normal map (gray clay with that normal map) - the quickest way to find a bad bake. The Final object's materials
are swapped for one baked material during the render and put back afterwards."""
import bpy, os
from mathutils import Vector, Matrix
import rg_look


def _material(maps, normal_file, normal_only):
    m = bpy.data.materials.new("_preview")
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    b = nt.nodes.new("ShaderNodeBsdfPrincipled")
    nt.links.new(b.outputs[0], out.inputs["Surface"])

    def img(path, data=True):
        t = nt.nodes.new("ShaderNodeTexImage")
        t.image = bpy.data.images.load(path, check_existing=True)
        t.image.colorspace_settings.name = "Non-Color" if data else "sRGB"
        return t
    nm = nt.nodes.new("ShaderNodeNormalMap")
    nt.links.new(img(normal_file).outputs["Color"], nm.inputs["Color"])
    nt.links.new(nm.outputs["Normal"], b.inputs["Normal"])
    if normal_only:
        b.inputs["Base Color"].default_value = (0.45, 0.45, 0.47, 1)
        b.inputs["Roughness"].default_value = 0.45
    else:
        nt.links.new(img(os.path.join(maps, "basecolor.png"), False).outputs["Color"], b.inputs["Base Color"])
        sep = nt.nodes.new("ShaderNodeSeparateColor")
        nt.links.new(img(os.path.join(maps, "orm.png")).outputs["Color"], sep.inputs["Color"])
        nt.links.new(sep.outputs["Green"], b.inputs["Roughness"])
        nt.links.new(sep.outputs["Blue"], b.inputs["Metallic"])
    return m


def preview(out, tag, center, size, views=("fl", "br"), maps=None, normal_file=None, normal_only=False,
            res=(1200, 800)):
    sc = bpy.context.scene
    fin = bpy.data.objects["railgun"]
    lc = bpy.context.view_layer.layer_collection.children
    vis = {c.name: (c.exclude, c.hide_viewport) for c in lc}
    for c in lc:
        c.exclude = c.name != "Final"
        if c.name == "Final":
            c.hide_viewport = False
    old = [s.material for s in fin.material_slots]
    m = _material(maps, normal_file or os.path.join(maps, "normal.png"), normal_only)
    for s in fin.material_slots:
        s.material = m
    keep = (sc.render.engine, sc.camera, sc.render.resolution_x, sc.render.resolution_y, sc.render.film_transparent)
    sc.render.engine = "BLENDER_EEVEE" if "BLENDER_EEVEE" in [e.identifier for e in
                                                              bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items] else "BLENDER_EEVEE_NEXT"
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.film_transparent = False
    world = sc.world or bpy.data.worlds.new("World")
    sc.world = world
    cd = bpy.data.cameras.new("pv_cam"); cam = bpy.data.objects.new("pv_cam", cd)
    sc.collection.objects.link(cam); sc.camera = cam
    sun = bpy.data.lights.new("pv_sun", "SUN"); sun.energy = 3.0
    so = bpy.data.objects.new("pv_sun", sun); sc.collection.objects.link(so)
    so.rotation_euler = (0.9, 0.2, -0.8)
    files = []
    try:
        for v in views:
            d = Vector(rg_look.DIRS[v]).normalized()
            cd.lens = 50
            cam.location = Vector(center) + d * size * 3.0
            up = Vector((0, 0, 1)) if abs(d.z) < 0.95 else Vector((1, 0, 0))
            cx = up.cross(d).normalized(); cy = d.cross(cx)
            cam.rotation_euler = Matrix((cx, cy, d)).transposed().to_euler()
            cd.clip_start = 0.002
            f = os.path.join(out, "%s_%s.png" % (tag, v))
            sc.render.filepath = f
            bpy.ops.render.render(write_still=True)
            files.append(f)
    finally:
        for s, o in zip(fin.material_slots, old):
            s.material = o
        bpy.data.materials.remove(m)
        bpy.data.objects.remove(cam); bpy.data.cameras.remove(cd)
        bpy.data.objects.remove(so); bpy.data.lights.remove(sun)
        for c in lc:
            c.exclude, c.hide_viewport = vis[c.name]
        sc.render.engine, sc.camera, sc.render.resolution_x, sc.render.resolution_y, sc.render.film_transparent = keep
    return files
