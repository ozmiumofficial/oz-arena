"""Convert the id Tech 3 Keel player (lower/upper/head .md3, default skin) to one FBX.

Run headless:
  blender -b --factory-startup -P convert_keel.py -- <md3 dir> <out dir>

MD3 animation is per-vertex frames, so each frame becomes a shape key and the
timeline cross-fades the keys (linear 0->1->0), which is exactly id Tech 3's
frame lerp. Every animation.cfg entry is laid out one after another on a 60 fps
timeline; clips.json lists the ranges for splitting into clips in Unity.
Torso clips hold the legs in LEGS_IDLE, leg clips hold the torso in TORSO_STAND.
Scale is 32 units per meter (the GAME project's unit), feet at the origin.
"""
import bpy, struct, math, os, sys, json, shutil
from mathutils import Matrix, Vector, Quaternion

argv = sys.argv[sys.argv.index("--") + 1:]
SRC, OUT = argv[0], argv[1]
os.makedirs(OUT, exist_ok=True)

UNITS_PER_M = 32.0
FEET_Z = 24.0          # player origin is 24 units above the feet (MINS_Z)
FPS = 60
GAP = 10               # empty timeline frames between clips
# id Tech 3: +X forward, +Y left, +Z up. Blender: -Y forward. Rotate -90 about Z.
R = Matrix(((0, 1, 0), (-1, 0, 0), (0, 0, 1)))


def conv(v):
    return R @ Vector(v) / UNITS_PER_M


# ---------------------------------------------------------------- MD3 reader
def read_md3(path):
    b = open(path, "rb").read()
    ident, ver = struct.unpack_from("<4si", b, 0)
    assert ident == b"IDP3" and ver == 15, path
    nfr, ntag, nsurf, nskin, ofr, otag, osurf, oeof = struct.unpack_from("<8i", b, 76)
    tags = []  # tags[frame] = {name: (origin, axis rows)}
    for f in range(nfr):
        d = {}
        for t in range(ntag):
            o = otag + (f * ntag + t) * 112
            name = b[o:o + 64].split(b"\0")[0].decode().lower()
            vals = struct.unpack_from("<12f", b, o + 64)
            d[name] = (vals[0:3], (vals[3:6], vals[6:9], vals[9:12]))
        tags.append(d)
    surfs = []
    o = osurf
    for _ in range(nsurf):
        name = b[o + 4:o + 68].split(b"\0")[0].decode().lower()
        (flags, snfr, nsh, nv, ntri, otri, osh, ost, oxyz, oend) = struct.unpack_from("<10i", b, o + 68)
        tris = [struct.unpack_from("<3i", b, o + otri + i * 12) for i in range(ntri)]
        st = [struct.unpack_from("<2f", b, o + ost + i * 8) for i in range(nv)]
        frames, normals = [], []
        for f in range(snfr):
            fr, nr = [], []
            for i in range(nv):
                x, y, z, n = struct.unpack_from("<3hH", b, o + oxyz + (f * nv + i) * 8)
                fr.append((x, y, z))
                lat, lng = (n >> 8) * 2 * math.pi / 255, (n & 255) * 2 * math.pi / 255
                nr.append((math.cos(lat) * math.sin(lng), math.sin(lat) * math.sin(lng), math.cos(lng)))
            frames.append(fr)
            normals.append(nr)
        surfs.append(dict(name=name, tris=tris, st=st, frames=frames, normals=normals))
        o += oend
    return dict(nframes=nfr, tags=tags, surfs=surfs)


def read_skin(path):
    m = {}
    for line in open(path):
        if "," in line:
            k, v = line.strip().split(",", 1)
            if v:
                m[k.lower()] = os.path.basename(v.replace("\\", "/")).lower()
    return m


def read_cfg(path):
    names = ["BOTH_DEATH1", "BOTH_DEAD1", "BOTH_DEATH2", "BOTH_DEAD2", "BOTH_DEATH3", "BOTH_DEAD3",
             "TORSO_GESTURE", "TORSO_ATTACK", "TORSO_ATTACK2", "TORSO_DROP", "TORSO_RAISE",
             "TORSO_STAND", "TORSO_STAND2", "LEGS_WALKCR", "LEGS_WALK", "LEGS_RUN", "LEGS_BACK",
             "LEGS_SWIM", "LEGS_JUMP", "LEGS_LAND", "LEGS_JUMPB", "LEGS_LANDB", "LEGS_IDLE",
             "LEGS_IDLECR", "LEGS_TURN"]
    rows = []
    for line in open(path):
        p = line.split("//")[0].split()
        if len(p) == 4 and p[0].lstrip("-").isdigit():
            rows.append(tuple(int(x) for x in p))
    anims = {}
    for n, (first, num, loop, fps) in zip(names, rows):
        anims[n] = dict(first=first, num=num, loop=loop, fps=fps)
    # leg frames in lower.md3 skip the torso-only frames (cg_players.c)
    skip = anims["LEGS_WALKCR"]["first"] - anims["TORSO_GESTURE"]["first"]
    return anims, skip


# ---------------------------------------------------------------- mesh build
def build_mesh(md3, name, skin, basis_frame):
    """One object per md3, surfaces as material slots, one shape key per frame.
    Seam duplicates that coincide in every frame are welded so shading is smooth."""
    verts_key, verts, loops_uv, faces, face_mat = {}, [], [], [], []
    vert_frames = []
    mats = []
    for si, s in enumerate(md3["surfs"]):
        tex = skin.get(s["name"])
        mats.append((s["name"], tex))
        remap = []
        for i in range(len(s["st"])):
            key = tuple(fr[i] for fr in s["frames"])
            if key not in verts_key:
                verts_key[key] = len(vert_frames)
                vert_frames.append([fr[i] for fr in s["frames"]])
            remap.append(verts_key[key])
        # pick the winding that agrees with the stored vertex normals
        flip_votes = 0
        f0, n0 = s["frames"][0], s["normals"][0]
        for a, b_, c in s["tris"]:
            pa, pb, pc = Vector(f0[a]), Vector(f0[b_]), Vector(f0[c])
            fn = (pb - pa).cross(pc - pa)
            avg = Vector(n0[a]) + Vector(n0[b_]) + Vector(n0[c])
            flip_votes += 1 if fn.dot(avg) < 0 else -1
        flip = flip_votes > 0
        for a, b_, c in s["tris"]:
            tri = (a, c, b_) if flip else (a, b_, c)
            if len({remap[i] for i in tri}) < 3:
                continue
            faces.append(tuple(remap[i] for i in tri))
            loops_uv.extend((s["st"][i][0], 1.0 - s["st"][i][1]) for i in tri)
            face_mat.append(si)

    nfr = md3["nframes"]
    me = bpy.data.meshes.new(name)
    base = [conv(Vector(vf[basis_frame]) / 64.0) for vf in vert_frames]
    me.from_pydata([tuple(v) for v in base], [], faces)
    uv = me.uv_layers.new(name="UVMap")
    uv.data.foreach_set("uv", [c for p in loops_uv for c in p])
    me.polygons.foreach_set("material_index", face_mat)
    me.polygons.foreach_set("use_smooth", [True] * len(faces))
    me.update()
    for mname, tex in mats:
        me.materials.append(make_material(tex))
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    if nfr > 1:
        ob.shape_key_add(name="Basis", from_mix=False)
        for f in range(nfr):
            k = ob.shape_key_add(name="f%03d" % f, from_mix=False)
            co = []
            for vf in vert_frames:
                co.extend(conv(Vector(vf[f]) / 64.0))
            k.data.foreach_set("co", co)
            k.value = 0.0
        me.shape_keys.use_relative = True
    print("%s: %d verts (welded from %d), %d tris, %d frames, flip=%s" % (
        name, len(vert_frames), sum(len(s["st"]) for s in md3["surfs"]), len(faces), nfr, flip))
    return ob


_mats = {}


def make_material(tex):
    if tex in _mats:
        return _mats[tex]
    m = bpy.data.materials.new(os.path.splitext(tex)[0] if tex else "untextured")
    m.use_nodes = True
    if tex:
        dst = os.path.join(OUT, tex)
        shutil.copyfile(os.path.join(SRC, tex), dst)
        img = bpy.data.images.load(dst)
        nt = m.node_tree
        bsdf = nt.nodes.get("Principled BSDF")
        tn = nt.nodes.new("ShaderNodeTexImage")
        tn.image = img
        nt.links.new(tn.outputs["Color"], bsdf.inputs["Base Color"])
        bsdf.inputs["Roughness"].default_value = 0.7
    _mats[tex] = m
    return m


def make_empty(name, parent):
    e = bpy.data.objects.new(name, None)
    e.empty_display_type = "ARROWS"
    e.empty_display_size = 0.15
    bpy.context.scene.collection.objects.link(e)
    e.parent = parent
    e.rotation_mode = "QUATERNION"
    return e


def tag_local(tag):
    origin, axis = tag
    m = Matrix((axis[0], axis[1], axis[2])).transposed()  # columns = tag axes
    m = R @ m @ R.transposed()
    return conv(origin), m.to_quaternion()


# ---------------------------------------------------------------- keying
def set_linear(id_data):
    ad = id_data.animation_data
    if not ad or not ad.action:
        return
    curves = None
    try:
        from bpy_extras import anim_utils
        cb = anim_utils.action_get_channelbag_for_slot(ad.action, ad.action_slot)
        curves = cb.fcurves if cb else None
    except Exception:
        pass
    if curves is None:
        curves = ad.action.fcurves
    for fc in curves:
        for kp in fc.keyframe_points:
            kp.interpolation = "LINEAR"


def key_shapes(ob, track):
    """track = list of clips, each a list of (time, frame). Cross-fade shape keys."""
    kb = ob.data.shape_keys.key_blocks
    for clip in track:
        for i, (t, f) in enumerate(clip):
            k = kb["f%03d" % f]
            k.value = 1.0
            k.keyframe_insert("value", frame=t)
            for j in (i - 1, i + 1):
                if 0 <= j < len(clip):
                    if clip[j][1] != f:
                        k.value = 0.0
                        k.keyframe_insert("value", frame=clip[j][0])
                else:
                    k.value = 0.0
                    k.keyframe_insert("value", frame=t + (-1 if j < i else 1))
    set_linear(ob.data.shape_keys)


def key_tag(empty, md3, tagname, track):
    prev = None
    for clip in track:
        for t, f in clip:
            loc, q = tag_local(md3["tags"][f][tagname])
            if prev is not None:
                q.make_compatible(prev)
            prev = q
            empty.location = loc
            empty.rotation_quaternion = q
            empty.keyframe_insert("location", frame=t)
            empty.keyframe_insert("rotation_quaternion", frame=t)
    set_linear(empty)


# ---------------------------------------------------------------- main
scene = bpy.context.scene
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o)
scene.render.fps = FPS
bpy.context.preferences.edit.keyframe_new_interpolation_type = "LINEAR"

anims, skip = read_cfg(os.path.join(SRC, "animation.cfg"))
lower = read_md3(os.path.join(SRC, "lower.md3"))
upper = read_md3(os.path.join(SRC, "upper.md3"))
head = read_md3(os.path.join(SRC, "head.md3"))

legs_idle = anims["LEGS_IDLE"]["first"] - skip
torso_stand = anims["TORSO_STAND"]["first"]

root = bpy.data.objects.new("Keel", None)
scene.collection.objects.link(root)
lo = build_mesh(lower, "lower", read_skin(os.path.join(SRC, "lower_default.skin")), legs_idle)
lo.parent = root
lo.location.z = FEET_Z / UNITS_PER_M
t_torso = make_empty("tag_torso", lo)
up = build_mesh(upper, "upper", read_skin(os.path.join(SRC, "upper_default.skin")), torso_stand)
up.parent = t_torso
t_head = make_empty("tag_head", up)
t_weapon = make_empty("tag_weapon", up)
hd = build_mesh(head, "head", read_skin(os.path.join(SRC, "head_default.skin")), 0)
hd.parent = t_head

# lay out clips: per clip, (timeline time, source frame) for lower and upper
low_track, up_track, clips = [], [], []
t0 = 1
for name, a in anims.items():
    n, fps = a["num"], a["fps"]
    looping = a["loop"] > 0
    step = FPS / fps
    seq = list(range(n)) + ([0] if looping else [])
    length = max(len(seq) - 1, 1) * step
    times = [t0 + i * step for i in range(len(seq))]
    if len(seq) == 1:
        times = [t0, t0 + length]
        seq = [0, 0]
    if name.startswith("BOTH"):
        lseq = [a["first"] + i for i in seq]
        useq = [a["first"] + i for i in seq]
    elif name.startswith("TORSO"):
        lseq = [legs_idle, legs_idle]
        useq = [a["first"] + i for i in seq]
    else:
        lseq = [a["first"] - skip + i for i in seq]
        useq = [torso_stand, torso_stand]
    ltimes = times if len(lseq) == len(times) else [times[0], times[-1]]
    utimes = times if len(useq) == len(times) else [times[0], times[-1]]
    low_track.append(list(zip(ltimes, lseq)))
    up_track.append(list(zip(utimes, useq)))
    clips.append(dict(name=name, start_frame=t0, end_frame=t0 + length,
                      start_time=(t0) / FPS, end_time=(t0 + length) / FPS,
                      loop=looping, source_fps=fps, source_frames=n))
    t0 = math.ceil(t0 + length) + GAP

key_shapes(lo, low_track)
key_shapes(up, up_track)
key_tag(t_torso, lower, "tag_torso", low_track)
key_tag(t_head, upper, "tag_head", up_track)
key_tag(t_weapon, upper, "tag_weapon", up_track)

scene.frame_start = 0
scene.frame_end = int(math.ceil(clips[-1]["end_frame"])) + 1
for c in clips:
    scene.timeline_markers.new(c["name"], frame=int(c["start_frame"]))
scene.frame_set(0)

bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "keel.blend"))
bpy.ops.export_scene.fbx(
    filepath=os.path.join(OUT, "keel.fbx"),
    use_selection=False, object_types={"EMPTY", "MESH"},
    apply_scale_options="FBX_SCALE_UNITS", axis_forward="-Z", axis_up="Y",
    use_mesh_modifiers=True, mesh_smooth_type="FACE", use_tspace=False,
    add_leaf_bones=False, path_mode="RELATIVE",
    bake_anim=True, bake_anim_use_all_bones=False, bake_anim_use_nla_strips=False,
    bake_anim_use_all_actions=False, bake_anim_force_startend_keying=True,
    bake_anim_step=1.0, bake_anim_simplify_factor=0.0)
json.dump(dict(fps=FPS, units_per_meter=UNITS_PER_M, clips=clips),
          open(os.path.join(OUT, "clips.json"), "w"), indent=2)
print("clips:", len(clips), "timeline frames:", scene.frame_end)
