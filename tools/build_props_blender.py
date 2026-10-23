"""Props step 3 (Blender): every prop -> GLB with matte PBR materials, tag nodes, morph animation.

blender -b --factory-startup -P build_props_blender.py -- <props work dir> <out dir>

Writes <out>/<id>.glb for every entry in props.json (id = path under models/, e.g.
weapons2/rocketl/rocketl). Weapons come assembled: <w>_barrel hangs on tag_barrel and
<w>_flash on tag_flash (node "flash", hide it until firing). Tags become empty nodes named
like the md3 tag (tag_flash, tag_barrel, tag_weapon ...), placed at frame 0.
Material names carry the id Tech 3 draw mode for engine import scripts:
  <stem>            opaque        <stem>__mask   alpha cut-out
  <stem>__blend     alpha blend   <stem>__add    additive, unshaded
  ...__2s           two-sided (no backface culling)
Scale: 32 id units = 1 m. Axes: id +X forward -> Blender -Y -> glTF +Z.
"""
import bpy, sys, os, json
import numpy as np
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index("--") + 1:]
WORK, OUT = argv[0], argv[1]
props = json.load(open(os.path.join(WORK, "props.json")))
materials = json.load(open(os.path.join(WORK, "materials.json")))
textures = json.load(open(os.path.join(WORK, "textures.json")))
by_id = {p["id"]: p for p in props}
TEX = os.path.join(WORK, "tex")
FPS = 60
C = Matrix.Scale(1 / 32.0, 4) @ Matrix(((0, 1, 0, 0), (-1, 0, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1)))
Ci = C.inverted()


def sparse_dense(path):  # Blender bundles numpy but not scipy
    d = np.load(path)
    n, m = d["shape"]
    M = np.zeros((n, m))
    ptr, idx, val = d["indptr"], d["indices"], d["data"]
    for r in range(n):
        M[r, idx[ptr[r]:ptr[r + 1]]] = val[ptr[r]:ptr[r + 1]]
    return M


def tag_matrix(v):
    o, ax = np.array(v[:3]), np.array(v[3:]).reshape(3, 3).T  # columns = tag axes
    U, _, Vt = np.linalg.svd(ax)
    R = U @ Vt
    M = Matrix.Identity(4)
    for i in range(3):
        for j in range(3):
            M[i][j] = float(R[i][j])
        M[i][3] = float(o[i])
    return C @ M @ Ci  # same frame expressed in Blender space (uniform scale cancels)


_mats = {}


def make_mat(mi):
    md = materials[mi]
    if md["name"] in _mats:
        return _mats[md["name"]]
    t = textures[md["texture"]]
    stem = t["stem"]
    m = bpy.data.materials.new(md["name"])
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]

    def img(fn, color):
        n = nt.nodes.new("ShaderNodeTexImage")
        n.image = bpy.data.images.load(os.path.join(TEX, fn), check_existing=True)
        if not color:
            n.image.colorspace_settings.name = "Non-Color"
        return n

    albedo_png = os.path.exists(os.path.join(TEX, stem + "_albedo.png"))
    a = img(stem + ("_albedo.png" if albedo_png else "_albedo.jpg"), True)
    nt.links.new(a.outputs["Color"], bsdf.inputs["Base Color"])
    if albedo_png and md["blend"] in ("mask", "blend"):
        nt.links.new(a.outputs["Alpha"], bsdf.inputs["Alpha"])
        m.surface_render_method = "BLENDED" if md["blend"] == "blend" else "DITHERED"
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(img(stem + "_orm.jpg", False).outputs["Color"], sep.inputs["Color"])
    nt.links.new(sep.outputs["Green"], bsdf.inputs["Roughness"])
    nt.links.new(sep.outputs["Blue"], bsdf.inputs["Metallic"])
    nm = nt.nodes.new("ShaderNodeNormalMap")
    nt.links.new(img(stem + "_normal.jpg", False).outputs["Color"], nm.inputs["Color"])
    nt.links.new(nm.outputs["Normal"], bsdf.inputs["Normal"])
    nt.links.new(img(stem + "_emissive.jpg", True).outputs["Color"], bsdf.inputs["Emission Color"])
    bsdf.inputs["Emission Strength"].default_value = 2.0 if md["blend"] == "add" else 1.0
    m.use_backface_culling = not md["two_sided"]
    _mats[md["name"]] = m
    return m


def build(prop, name):
    """Mesh object (+ tag empties as children) for one prop, in Blender space."""
    d = dict(np.load(os.path.join(WORK, "cage", prop["file"] + ".npz")))
    S = sparse_dense(os.path.join(WORK, "cage", prop["file"] + "_S.npz"))
    frames = d["wpos"].astype(np.float64) / 64.0
    hi0 = S @ frames[0]
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(C @ Vector(tuple(v))) for v in hi0], [], [tuple(int(i) for i in f) for f in d["Fh"]])
    uvl = me.uv_layers.new(name="UVMap")
    uvl.data.foreach_set("uv", [c for f in d["UVh"] for u, v in f for c in (float(u), 1 - float(v))])
    slots = sorted(set(int(x) for x in d["Mh"]))
    me.polygons.foreach_set("material_index", [slots.index(int(x)) for x in d["Mh"]])
    me.polygons.foreach_set("use_smooth", [True] * len(me.polygons))
    ekey = {tuple(sorted(e.vertices)): e.index for e in me.edges}
    sh = [False] * len(me.edges)
    for x, y in d["crease_h"]:
        i = ekey.get((min(int(x), int(y)), max(int(x), int(y))))
        if i is not None:
            sh[i] = True
    attr = me.attributes.get("sharp_edge") or me.attributes.new("sharp_edge", "BOOLEAN", "EDGE")
    attr.data.foreach_set("value", sh)
    for mi in slots:
        me.materials.append(make_mat(mi))
    me.update()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    if prop["frames"] > 1:  # vertex animation -> shape keys cross-faded 0-1-0 (= id Tech 3 frame lerp)
        ob.shape_key_add(name="Basis", from_mix=False)
        for f in range(prop["frames"]):
            k = ob.shape_key_add(name="f%03d" % f, from_mix=False)
            k.data.foreach_set("co", [c for v in S @ frames[f] for c in (C @ Vector(tuple(v)))])
        step = FPS / 15.0
        kb = me.shape_keys.key_blocks
        for f in range(prop["frames"]):
            k = kb["f%03d" % f]
            for g in range(prop["frames"]):
                k.value = 1.0 if g == f else 0.0
                k.keyframe_insert("value", frame=1 + g * step)
        me.shape_keys.animation_data.action.name = name + "_anim"
    for tname in prop["tags"]:
        e = bpy.data.objects.new(tname, None)
        e.empty_display_type = "ARROWS"
        e.empty_display_size = 0.1
        bpy.context.scene.collection.objects.link(e)
        e.parent = ob
        e.matrix_world = tag_matrix(d["tag__" + tname][0])
    return ob


def attach(parent, tag_name, child_id, node_name):
    tag = next((c for c in parent.children if c.name.split(".")[0] == tag_name), None)
    if tag is None or child_id not in by_id:
        return
    child = build(by_id[child_id], node_name)
    child.parent = tag
    child.matrix_parent_inverse = Matrix.Identity(4)
    child.matrix_basis = Matrix.Identity(4)


done = 0
for prop in props:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    _mats.clear()
    bpy.context.scene.render.fps = FPS
    base = prop["id"].split("/")[-1]
    root = build(prop, base)
    parts = prop["id"].split("/")
    if parts[0] == "weapons2" and len(parts) == 3 and parts[1] == parts[2]:  # a weapon's main model
        stem = "/".join(parts[:2]) + "/" + parts[1]
        attach(root, "tag_barrel", stem + "_barrel", "barrel")
        attach(root, "tag_flash", stem + "_flash", "flash")
    path = os.path.join(OUT, *parts) + ".glb"
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", export_image_format="AUTO",
                              export_materials="EXPORT", export_animations=True, export_morph=True,
                              export_yup=True, export_apply=False)
    done += 1
print("PROPS exported %d to %s" % (done, OUT))
