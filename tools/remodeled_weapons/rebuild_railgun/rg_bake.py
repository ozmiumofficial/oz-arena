"""Cycles bakes for the rebuilt railgun, headless so the live Blender stays responsive (adapted from the rocket
launcher's rl_bake.py):

    blender -b railgun_rebuild.blend --python rg_bake.py -- <out dir> helpers|refcolor|pbr [size]

helpers: normal.png (the high-detail copy, rg_high.py's 'High' collection, baked onto the game mesh in tangent
         space), ao_geo.png (ambient occlusion of the game mesh alone), edge_geo.png (how sharply the surface bends
         within 2 mm: the bevel node against the true normal), matid.png (one flat color per material); rg_post.py
         turns them into ao.png, edge.png and detail_normal.png.
refcolor: refcolor.png + refhit.png (the Tripo reference's color cast onto the rebuild's UVs, and where it was
         found): the energy cells' veins come from it (rg_post.py).
pbr:     basecolor.png, roughness.png, metallic.png from the materials (rg_materials.py), each through
         an emission shader so nothing is lit.
"""
import bpy, sys, os

a = sys.argv[sys.argv.index("--") + 1:]
OUT, WHAT = a[0], a[1]
SIZE = int(a[2]) if len(a) > 2 else 4096
os.makedirs(OUT, exist_ok=True)
sc = bpy.context.scene
sc.render.engine = "CYCLES"
prefs = bpy.context.preferences.addons["cycles"].preferences
try:
    prefs.compute_device_type = "OPTIX"; prefs.get_devices()
    for d in prefs.devices:
        d.use = d.type != "CPU"
    sc.cycles.device = "GPU"
except Exception as e:
    print("GPU unavailable:", e)
bk = sc.render.bake
bk.margin = 24; bk.margin_type = "EXTEND"
fin = bpy.data.objects["railgun"]
ref = bpy.data.objects.get("REF_tripo")
if ref:      # the live session's ghost is drawn inset; the bake needs the real surface
    for m in list(ref.modifiers):
        ref.modifiers.remove(m)
def show(*names):
    for lc in bpy.context.view_layer.layer_collection.children:
        lc.exclude = lc.name not in names
        lc.hide_viewport = False


show("Final", "Reference", "High")
for o in bpy.context.view_layer.objects:
    o.hide_set(False); o.hide_render = False
if ref:
    ref.hide_select = False

MATID = {"paint": (0, 0, 0), "gunmetal": (1, 0, 0), "dark_metal": (0, 1, 0), "steel": (1, 0, 1), "rubber": (0, 1, 1),
         "carbon": (0.5, 0.5, 0.5), "glass_cell": (1, 1, 0), "trim": (0, 0, 1), "vent_glow": (1, 1, 1)}


def image(name, data=True, fill=(0, 0, 0, 1)):
    im = bpy.data.images.get(name)
    if im:
        bpy.data.images.remove(im)
    im = bpy.data.images.new(name, SIZE, SIZE, alpha=False, float_buffer=False)
    im.colorspace_settings.name = "Non-Color" if data else "sRGB"
    im.generated_color = fill
    return im


def save(im, name):
    im.filepath_raw = os.path.join(OUT, name)
    im.file_format = "PNG"
    im.save()
    print("saved", name)


def bake_materials(builder):
    """Temporary materials on the target: builder(nt, slot_name) wires the surface; returns the old ones."""
    old = [s.material for s in fin.material_slots]
    for i, s in enumerate(fin.material_slots):
        m = bpy.data.materials.new("_bake_%d" % i)
        m.use_nodes = True
        nt = m.node_tree
        for n in list(nt.nodes):
            nt.nodes.remove(n)
        out = nt.nodes.new("ShaderNodeOutputMaterial")
        sh = builder(nt, old[i].name if old[i] else "")
        if sh is not None:
            nt.links.new(sh, out.inputs["Surface"])
        s.material = m
    return old


def target_image(im):
    for s in fin.material_slots:
        nt = s.material.node_tree
        t = nt.nodes.new("ShaderNodeTexImage"); t.image = im
        for n in nt.nodes:
            n.select = False
        t.select = True; nt.nodes.active = t


def restore(old):
    for s, m in zip(fin.material_slots, old):
        tmp = s.material
        s.material = m
        if tmp and tmp.name.startswith("_bake_"):
            bpy.data.materials.remove(tmp)


def select_only(objs, active):
    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = active


def emission(nt, color_socket=None, value=None):
    e = nt.nodes.new("ShaderNodeEmission")
    if color_socket is not None:
        nt.links.new(color_socket, e.inputs["Color"])
    elif value is not None:
        e.inputs["Color"].default_value = value
    return e.outputs["Emission"]


if WHAT == "helpers":
    # the high-detail copy's normals, in the game mesh's tangent space
    show("Final", "High")
    high = [o for o in bpy.data.collections["High"].objects if o.type == "MESH"]
    sc.cycles.samples = 8
    old = bake_materials(lambda nt, name: nt.nodes.new("ShaderNodeBsdfDiffuse").outputs[0])
    im = image("normal", fill=(0.5, 0.5, 1.0, 1)); target_image(im)
    select_only(high + [fin], fin)
    bpy.ops.object.bake(type="NORMAL", normal_space="TANGENT", use_selected_to_active=True, cage_extrusion=0.0025,
                        max_ray_distance=0.0065, margin=24)
    save(im, "normal.png"); restore(old)
    show("Final")
    select_only([fin], fin)
    # ambient occlusion: the whole gun shadows itself
    sc.cycles.samples = 128
    old = bake_materials(lambda nt, name: nt.nodes.new("ShaderNodeBsdfDiffuse").outputs[0])
    im = image("ao"); target_image(im)
    if sc.world:
        sc.world.light_settings.distance = 0.04
    bpy.ops.object.bake(type="AO", margin=24)
    save(im, "ao_geo.png"); restore(old)

    # edges: 1 - dot(bevel normal, true normal), strong where the surface turns within the bevel radius
    def edge_nodes(nt, name):
        bev = nt.nodes.new("ShaderNodeBevel"); bev.samples = 16; bev.inputs["Radius"].default_value = 0.0022
        geo = nt.nodes.new("ShaderNodeNewGeometry")
        dot = nt.nodes.new("ShaderNodeVectorMath"); dot.operation = "DOT_PRODUCT"
        nt.links.new(bev.outputs["Normal"], dot.inputs[0]); nt.links.new(geo.outputs["True Normal"], dot.inputs[1])
        inv = nt.nodes.new("ShaderNodeMath"); inv.operation = "SUBTRACT"; inv.inputs[0].default_value = 1.0
        nt.links.new(dot.outputs["Value"], inv.inputs[1])
        mul = nt.nodes.new("ShaderNodeMath"); mul.operation = "MULTIPLY"; mul.use_clamp = True
        mul.inputs[1].default_value = 6.0
        nt.links.new(inv.outputs[0], mul.inputs[0])
        return emission(nt, mul.outputs[0])
    sc.cycles.samples = 32
    old = bake_materials(edge_nodes)
    im = image("edge"); target_image(im)
    bpy.ops.object.bake(type="EMIT", margin=24)
    save(im, "edge_geo.png"); restore(old)

    # material ids
    sc.cycles.samples = 1
    old = bake_materials(lambda nt, name: emission(nt, value=MATID.get(name, (0, 0, 0)) + (1,)))
    im = image("matid"); target_image(im)
    bpy.ops.object.bake(type="EMIT", margin=24)
    save(im, "matid.png"); restore(old)

elif WHAT == "refcolor":
    if ref:
        rm = ref.active_material
        rnt = rm.node_tree
        out = next(n for n in rnt.nodes if n.type == "OUTPUT_MATERIAL")
        keep_link = next((l.from_socket for l in rnt.links if l.to_node == out and l.to_socket.name == "Surface"), None)
        tex = next(n for n in rnt.nodes if n.type == "TEX_IMAGE" and n.image)
        em = rnt.nodes.new("ShaderNodeEmission")
        rnt.links.new(tex.outputs["Color"], em.inputs["Color"])
        rnt.links.new(em.outputs[0], out.inputs["Surface"])
        sc.cycles.samples = 4
        old = bake_materials(lambda nt, name: nt.nodes.new("ShaderNodeEmission").outputs[0])
        for name, white, data in (("refcolor", False, False), ("refhit", True, True)):
            if white:
                rnt.links.new(em.outputs[0], out.inputs["Surface"])
                for l in list(rnt.links):
                    if l.to_node == em and l.to_socket.name == "Color":
                        rnt.links.remove(l)
                em.inputs["Color"].default_value = (1, 1, 1, 1)
            im = image(name, data=data, fill=(1, 0, 1, 1) if not white else (0, 0, 0, 1))
            for s in fin.material_slots:
                for n in list(s.material.node_tree.nodes):
                    if n.type == "TEX_IMAGE":
                        s.material.node_tree.nodes.remove(n)
            target_image(im)
            select_only([ref, fin], fin)
            bpy.ops.object.bake(type="EMIT", use_selected_to_active=True, cage_extrusion=0.006,
                                max_ray_distance=0.015, margin=0)
            save(im, name + ".png")
        restore(old)
        rnt.nodes.remove(em)
        if keep_link:
            rnt.links.new(keep_link, out.inputs["Surface"])

elif WHAT == "pbr":
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import rg_materials as rl_materials
    rl_materials.setup(fin, OUT)
    select_only([fin], fin)
    sc.cycles.samples = 16
    for chan in ("basecolor", "roughness", "metallic"):
        rl_materials.route(fin, chan)
        im = image(chan, data=(chan != "basecolor"))
        for s in fin.material_slots:
            nt = s.material.node_tree
            t = nt.nodes.new("ShaderNodeTexImage"); t.name = "_bake_target"; t.image = im
            for n in nt.nodes:
                n.select = False
            t.select = True; nt.nodes.active = t
        bpy.ops.object.bake(type="EMIT", margin=24)
        save(im, chan + ".png")
        for s in fin.material_slots:
            nt = s.material.node_tree
            n = nt.nodes.get("_bake_target")
            if n:
                nt.nodes.remove(n)
    rl_materials.route(fin, "shader")
print("bake done", WHAT)
