"""Step 4 (Blender): one skinned mesh + armature + a clip per animation.cfg entry.

blender -b --factory-startup -P build_rigged.py -- <work dir> <out dir> [--pbr] [--name keel]

Bone motions come from fit_rig.py (rest -> posed rigid transforms per md3 frame). The legs
half is driven by lower.md3 frames and the torso half by upper.md3 frames; 'waist' carries
id Tech 3's tag_torso, 'head' and 'weapon' carry tag_head / tag_weapon. Leg clips key only
leg bones and torso clips only torso bones, so a legs clip and a torso clip layer the way
the game plays them (Unity: avatar masks on two Animator layers; Godot: AnimationTree
blend with bone filters).

--pbr wires the maps from <work>/tex (copied to <out>/textures) into Principled BSDF
materials; without it the original painted textures are used.
Writes <name>_rigged.blend / .glb / .fbx and rig.json into <out>.
"""
import bpy, sys, os, json, shutil
import numpy as np
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index("--") + 1:]
WORK, OUT = argv[0], argv[1]
PBR = "--pbr" in argv
NAME = argv[argv.index("--name") + 1] if "--name" in argv else os.path.basename(os.path.normpath(WORK))
os.makedirs(OUT, exist_ok=True)
CAGE = os.path.join(WORK, "cage")
FPS, UNITS, FEET = 60, 32.0, 24.0


def sparse_dense(path):  # Blender bundles numpy but not scipy
    d = np.load(path)
    n, m = d["shape"]
    M = np.zeros((n, m))
    ptr, idx, val = d["indptr"], d["indices"], d["data"]
    for r in range(n):
        M[r, idx[ptr[r]:ptr[r + 1]]] = val[ptr[r]:ptr[r + 1]]
    return M


parts = {p: dict(np.load(os.path.join(CAGE, p + "_part.npz"))) for p in ("lower", "upper", "head")}
S = {p: sparse_dense(os.path.join(CAGE, p + "_S.npz")) for p in parts}
fit = dict(np.load(os.path.join(CAGE, "rig_fit.npz")))
manifest = json.load(open(os.path.join(WORK, "mesh", "mesh.json")))
anims, skip = manifest["anims"], manifest["legsSkip"]
textures = json.load(open(os.path.join(WORK, "textures.json")))
lb, ub = int(parts["lower"]["basis"]), int(parts["upper"]["basis"])
nfl, nfu = parts["lower"]["wpos"].shape[0], parts["upper"]["wpos"].shape[0]

# id Tech 3 (x fwd, y left, z up, units) -> Blender (-Y fwd, meters, feet at 0)
C = (Matrix.Scale(1 / UNITS, 4) @ Matrix(((0, 1, 0, 0), (-1, 0, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1)))
     @ Matrix.Translation((0, 0, FEET)))
Ci = C.inverted()


def rigid(R, t):
    M = Matrix.Identity(4)
    for i in range(3):
        for j in range(3):
            M[i][j] = float(R[i][j])
        M[i][3] = float(t[i])
    return M


def tagm(v):
    o, ax = np.array(v[:3]), np.array(v[3:]).reshape(3, 3).T  # columns = tag axes
    U, _, Vt = np.linalg.svd(ax)
    return rigid(U @ Vt, o)


def q3pt(p):
    return C @ Vector(tuple(float(x) for x in p))


def tag_torso(lf):
    return tagm(parts["lower"]["tag_torso"][lf])


def G_lower(b, lf):
    return rigid(fit["lower_R"][lf, b], fit["lower_t"][lf, b])


def G_upper(b, lf, uf):
    return tag_torso(lf) @ rigid(fit["upper_R"][uf, b], fit["upper_t"][uf, b]) @ tag_torso(lb).inverted()


def G_tag(name, lf, uf):
    T, T0 = tagm(parts["upper"][name][uf]), tagm(parts["upper"][name][ub])
    return tag_torso(lf) @ T @ (tag_torso(lb) @ T0).inverted()


def G_waist(lf, uf):
    return tag_torso(lf) @ tag_torso(lb).inverted()


lnames = [str(n) for n in fit["lower_names"]]
unames = [str(n) for n in fit["upper_names"]]
reserved = set(lnames) | {"root", "waist", "head", "weapon"}
unames = [n + "_up" if n in reserved else n for n in unames]
lpar, upar = fit["lower_parent"], fit["upper_parent"]
bones = {"root": dict(parent=None, head=None, motion=lambda lf, uf: Matrix.Identity(4), half=None)}
for b, n in enumerate(lnames):
    bones[n] = dict(parent=lnames[lpar[b]] if lpar[b] >= 0 else "root", head=fit["lower_head"][b],
                    cent=fit["lower_cent"][b], motion=(lambda b: lambda lf, uf: G_lower(b, lf))(b), half="legs")
T0 = tag_torso(lb)
bones["waist"] = dict(parent=lnames[int(np.where(lpar < 0)[0][0])], head=np.array(T0.translation),
                      motion=G_waist, half="legs", tail_dir=np.array(T0.col[2][:3]))
for b, n in enumerate(unames):
    h = T0 @ Vector(tuple(float(x) for x in fit["upper_head"][b]))
    c = T0 @ Vector(tuple(float(x) for x in fit["upper_cent"][b]))
    bones[n] = dict(parent=unames[upar[b]] if upar[b] >= 0 else "waist", head=np.array(h), cent=np.array(c),
                    motion=(lambda b: lambda lf, uf: G_upper(b, lf, uf))(b), half="torso")


def nearest_upper(p):
    """Upper bone whose vertex centroid is closest to a point (fallback parent for tags)."""
    return min(unames, key=lambda n: (Vector(bones[n]["cent"]) - p).length)


for tname, bname, want, axis in (("tag_head", "head", "neck", 2), ("tag_weapon", "weapon", "hand_R", 0)):
    if tname not in parts["upper"]:
        continue
    T = T0 @ tagm(parts["upper"][tname][ub])
    parent = want if want in bones else nearest_upper(T.translation)
    bones[bname] = dict(parent=parent, head=np.array(T.translation),
                        motion=(lambda tn: lambda lf, uf: G_tag(tn, lf, uf))(tname), half="torso",
                        tail_dir=np.array(T.col[axis][:3]))

# ------------------------------------------------------------------ rest mesh (combined, subdivided)
verts, faces, uvs, mats, weights, sharp = [], [], [], [], [], []
for p in ("lower", "upper", "head"):
    d = parts[p]
    hi = S[p] @ (d["wpos"][int(d["basis"])].astype(np.float64) / 64.0)
    if p == "upper":
        M = T0
    elif p == "head":
        M = T0 @ tagm(parts["upper"]["tag_head"][ub]) if "tag_head" in parts["upper"] else T0
    else:
        M = Matrix.Identity(4)
    base = len(verts)
    verts += [tuple(C @ M @ Vector(tuple(v))) for v in hi]
    faces += [tuple(int(i) + base for i in f) for f in d["Fh"]]
    uvs += [[(float(u), 1 - float(v)) for u, v in f] for f in d["UVh"]]
    mats += [int(m) for m in d["Mh"]]
    sharp += [(int(x) + base, int(y) + base) for x, y in d["crease_h"]]
    if p == "head":
        weights += [{"head" if "head" in bones else unames[0]: 1.0} for _ in hi]
    else:
        W = S[p] @ fit[p + "_W"]
        names = lnames if p == "lower" else unames
        for row in W:
            top = np.argsort(-row)[:4]
            w = {names[i]: float(row[i]) for i in top if row[i] > 0.01}
            s = sum(w.values())
            weights.append({k: v / s for k, v in w.items()})

for o in list(bpy.data.objects):
    bpy.data.objects.remove(o)
sc = bpy.context.scene
sc.render.fps = FPS

me = bpy.data.meshes.new(NAME)
me.from_pydata(verts, [], faces)
uvl = me.uv_layers.new(name="UVMap")
uvl.data.foreach_set("uv", [c for f in uvs for uv in f for c in uv])
used = sorted(set(mats))
remap = {m: i for i, m in enumerate(used)}
me.polygons.foreach_set("material_index", [remap[m] for m in mats])
me.polygons.foreach_set("use_smooth", [True] * len(faces))
ekey = {tuple(sorted(e.vertices)): e.index for e in me.edges}
sh = [False] * len(me.edges)
for x, y in sharp:
    i = ekey.get((min(x, y), max(x, y)))
    if i is not None:
        sh[i] = True
attr = me.attributes.get("sharp_edge") or me.attributes.new("sharp_edge", "BOOLEAN", "EDGE")
attr.data.foreach_set("value", sh)
me.update()

TEXOUT = os.path.join(OUT, "textures")
if PBR:
    os.makedirs(TEXOUT, exist_ok=True)


def make_mat(t):
    stem = t["stem"]
    m = bpy.data.materials.new(stem)  # material name = texture stem (viewer + engines key on it)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]

    def img(path, color):
        n = nt.nodes.new("ShaderNodeTexImage")
        n.image = bpy.data.images.load(path)
        if not color:
            n.image.colorspace_settings.name = "Non-Color"
        return n

    if PBR and os.path.exists(os.path.join(WORK, "tex", stem + "_albedo.jpg")):
        # cut-outs keep their alpha in a PNG albedo (make_pbr.py)
        ext = {s: ".png" if s == "albedo" and os.path.exists(os.path.join(WORK, "tex", stem + "_albedo.png")) else ".jpg"
               for s in ("albedo", "normal", "orm", "emissive")}
        for suffix in ext:
            shutil.copyfile(os.path.join(WORK, "tex", "%s_%s%s" % (stem, suffix, ext[suffix])),
                            os.path.join(TEXOUT, "%s_%s%s" % (stem, suffix, ext[suffix])))
        tp = lambda s: os.path.join(TEXOUT, "%s_%s%s" % (stem, s, ext[s]))
        alb = img(tp("albedo"), True)
        nt.links.new(alb.outputs["Color"], bsdf.inputs["Base Color"])
        if ext["albedo"] == ".png":
            nt.links.new(alb.outputs["Alpha"], bsdf.inputs["Alpha"])
        sep = nt.nodes.new("ShaderNodeSeparateColor")
        nt.links.new(img(tp("orm"), False).outputs["Color"], sep.inputs["Color"])
        nt.links.new(sep.outputs["Green"], bsdf.inputs["Roughness"])
        nt.links.new(sep.outputs["Blue"], bsdf.inputs["Metallic"])
        nm = nt.nodes.new("ShaderNodeNormalMap")
        nt.links.new(img(tp("normal"), False).outputs["Color"], nm.inputs["Color"])
        nt.links.new(nm.outputs["Normal"], bsdf.inputs["Normal"])
        nt.links.new(img(tp("emissive"), True).outputs["Color"], bsdf.inputs["Emission Color"])
        bsdf.inputs["Emission Strength"].default_value = 1.0
    else:
        n = img(os.path.join(WORK, "src", "orig_%s.png" % stem), True)
        nt.links.new(n.outputs["Color"], bsdf.inputs["Base Color"])
    return m


for m in used:
    me.materials.append(make_mat(textures[m]))
mesh_ob = bpy.data.objects.new(NAME + "_mesh", me)

# ------------------------------------------------------------------ armature
arm = bpy.data.armatures.new(NAME + "_rig")
arm_ob = bpy.data.objects.new(NAME, arm)
sc.collection.objects.link(arm_ob)
sc.collection.objects.link(mesh_ob)
bpy.context.view_layer.objects.active = arm_ob
bpy.ops.object.mode_set(mode="EDIT")
children = {}
for n, b in bones.items():
    if b["parent"]:
        children.setdefault(b["parent"], []).append(n)
order = ["root"]
while len(order) < len(bones):
    order += [n for n in bones if n not in order and bones[n]["parent"] in order]
eb = {}
for n in order:
    b = bones[n]
    e = arm.edit_bones.new(n)
    if n == "root":
        e.head, e.tail = (0, 0, 0), (0, 0.25, 0)
    else:
        head = q3pt(b["head"])
        kids = [k for k in children.get(n, []) if k not in ("head", "weapon", "waist")] or children.get(n, [])
        if "tail_dir" in b:
            tail = head + (C.to_3x3() @ Vector(tuple(float(x) for x in b["tail_dir"]))).normalized() * 0.15
        elif kids:
            tail = sum((q3pt(bones[k]["head"]) for k in kids), Vector()) / len(kids)
        else:
            tail = head + (q3pt(b["cent"]) - head) * 2
        if (tail - head).length < 0.03:
            tail = head + Vector((0, 0, 0.08))
        e.head, e.tail = head, tail
    if b["parent"]:
        e.parent = eb[b["parent"]]
    eb[n] = e
bpy.ops.object.mode_set(mode="OBJECT")

mesh_ob.parent = arm_ob
mesh_ob.modifiers.new("Armature", "ARMATURE").object = arm_ob
for n in bones:
    mesh_ob.vertex_groups.new(name=n)
vg = {g.name: g for g in mesh_ob.vertex_groups}
for vi, w in enumerate(weights):
    for n, x in w.items():
        vg[n].add([vi], x, "REPLACE")

# ------------------------------------------------------------------ actions
rest = {n: arm.bones[n].matrix_local.copy() for n in bones}


def motion_bl(n, lf, uf):
    return C @ bones[n]["motion"](lf, uf) @ Ci


def local_basis(n, lf, uf):
    P = motion_bl(n, lf, uf) @ rest[n]
    par = bones[n]["parent"]
    if par is None:
        return rest[n].inverted() @ P
    Pp = motion_bl(par, lf, uf) @ rest[par]
    return (rest[par].inverted() @ rest[n]).inverted() @ (Pp.inverted() @ P)


arm_ob.animation_data_create()
clips = []
for name, an in anims.items():
    n, fps, looping = an["num"], an["fps"], an["loop"] > 0
    if n <= 0:
        continue
    seq = list(range(n)) + ([0] if looping else [])
    if len(seq) == 1:
        seq = [0, 0]
    if name.startswith("BOTH"):
        frames = [(an["first"] + k, an["first"] + k) for k in seq]
        keyed = [b for b in bones if bones[b]["half"]]
    elif name.startswith("TORSO"):
        frames = [(lb, an["first"] + k) for k in seq]
        keyed = [b for b in bones if bones[b]["half"] == "torso"]
    else:
        frames = [(an["first"] - skip + k, ub) for k in seq]
        keyed = [b for b in bones if bones[b]["half"] == "legs"]
    frames = [(min(max(lf, 0), nfl - 1), min(max(uf, 0), nfu - 1)) for lf, uf in frames]  # bad cfgs exist
    act = bpy.data.actions.new(name)
    act.use_fake_user = True
    arm_ob.animation_data.action = act
    step = FPS / fps
    prevq = {}
    for i, (lf, uf) in enumerate(frames):
        t = 1 + i * step
        for bn in keyed:
            pb = arm_ob.pose.bones[bn]
            loc, q, _ = local_basis(bn, lf, uf).decompose()
            if bn in prevq:
                q.make_compatible(prevq[bn])
            prevq[bn] = q
            pb.location = loc
            pb.rotation_quaternion = q
            pb.keyframe_insert("location", frame=t)
            pb.keyframe_insert("rotation_quaternion", frame=t)
    clips.append(dict(name=name, part="both" if name.startswith("BOTH") else
                      ("torso" if name.startswith("TORSO") else "legs"),
                      loop=looping, fps=fps, frames=n, seconds=(len(seq) - 1) * step / FPS))
    for pb in arm_ob.pose.bones:
        pb.location = (0, 0, 0)
        pb.rotation_quaternion = (1, 0, 0, 0)

if "LEGS_IDLE" in bpy.data.actions:
    arm_ob.animation_data.action = bpy.data.actions["LEGS_IDLE"]
    idle = next(c for c in clips if c["name"] == "LEGS_IDLE")
    sc.frame_start, sc.frame_end = 1, 1 + int(idle["seconds"] * FPS)

blend = os.path.join(OUT, NAME + "_rigged.blend")
bpy.ops.wm.save_as_mainfile(filepath=blend)
bpy.ops.file.make_paths_relative()  # so the .blend still finds its textures on another PC
bpy.ops.wm.save_mainfile()
for o in bpy.context.selected_objects:
    o.select_set(False)
bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, NAME + "_rigged.glb"), export_format="GLB",
                          export_animations=True, export_animation_mode="ACTIONS",
                          export_force_sampling=True, export_skins=True, export_yup=True,
                          export_image_format="JPEG" if PBR else "NONE", export_materials="EXPORT")
bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, NAME + "_rigged.fbx"), object_types={"ARMATURE", "MESH"},
                         apply_scale_options="FBX_SCALE_UNITS", axis_forward="-Z", axis_up="Y",
                         add_leaf_bones=False, primary_bone_axis="Y", secondary_bone_axis="X",
                         bake_anim=True, bake_anim_use_all_actions=True, bake_anim_use_nla_strips=False,
                         bake_anim_force_startend_keying=True, bake_anim_simplify_factor=0.0,
                         path_mode="COPY", embed_textures=False, mesh_smooth_type="FACE")
fitj = json.load(open(os.path.join(WORK, "rig_report.json")))
rig = dict(model=NAME, bones=[dict(name=n, parent=bones[n]["parent"], half=bones[n]["half"]) for n in order],
           clips=clips, verts=len(verts), tris=len(faces), materials=[textures[m]["stem"] for m in used],
           fit=dict(lower_rms_mm=fitj["lower"]["rms_mm"], lower_max_mm=fitj["lower"]["max_mm"],
                    upper_rms_mm=fitj["upper"]["rms_mm"], upper_max_mm=fitj["upper"]["max_mm"]))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rig_extras import tag_frames, material_modes
rig["tags"] = tag_frames(CAGE)                  # tag_weapon / tag_head rest frames: the bones keep only their direction
rig["material_modes"] = material_modes(textures)  # additive, cut-out and two-sided materials
json.dump(rig, open(os.path.join(OUT, "rig.json"), "w"), indent=1)
print("RIG %s: %d bones, %d verts, %d tris, %d clips" % (NAME, len(order), len(verts), len(faces), len(clips)))
