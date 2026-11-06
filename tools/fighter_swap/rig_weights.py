"""Blender: heat weights for a fighter's skeleton, solved on a watertight voxel copy of the mesh (Tripo meshes are
dozens of loose shells, which heat weighting can't cross). Saves the copy's vertices and weights; anim_fighter.py
moves them onto the real mesh by nearest points.
blender -b --python rig_weights.py -- fighters/<name>.json mesh.blend out.npz"""
import bpy, bmesh, sys, os
import numpy as np
argv = sys.argv[sys.argv.index("--") + 1:]
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import skeleton

sk = skeleton.load(argv[0])
VOXELS = [float(v) for v in argv[3].split(",")] if len(argv) > 3 else [0.006, 0.008, 0.005, 0.01]
bpy.ops.wm.open_mainfile(filepath=argv[1])
mech = next(o for o in bpy.context.scene.objects if o.type == 'MESH')


def make_proxy(size):
    proxy = mech.copy()
    proxy.data = mech.data.copy()
    bpy.context.scene.collection.objects.link(proxy)
    mod = proxy.modifiers.new("vox", 'REMESH')
    mod.mode = 'VOXEL'
    mod.voxel_size = size
    bpy.context.view_layer.objects.active = proxy
    for o in bpy.context.scene.objects:
        o.select_set(False)
    proxy.select_set(True)
    bpy.ops.object.modifier_apply(modifier="vox")
    print("PROXY voxel %.3f verts %d" % (size, len(proxy.data.vertices)))
    return proxy


arm_data = bpy.data.armatures.new("skel")
arm = bpy.data.objects.new("skel", arm_data)
bpy.context.scene.collection.objects.link(arm)
bpy.context.view_layer.objects.active = arm
arm.select_set(True)
bpy.ops.object.mode_set(mode='EDIT')
eb = {}
for name, parent, part, pos, tail in sk.JOINTS:
    b = arm_data.edit_bones.new(name)
    b.head, b.tail = pos, tail
    b.use_deform = name in sk.DEFORM
    if parent:
        b.parent = eb[parent]
    eb[name] = b
bpy.ops.object.mode_set(mode='OBJECT')

def heat(proxy):
    """heat weights on the proxy -> (vertices, weights); Blender's solver sometimes gives up on a whole mesh
    ("failed to find solution"), so the caller tries another voxel size"""
    for o in bpy.context.scene.objects:
        o.select_set(False)
    proxy.select_set(True)
    arm.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.parent_set(type='ARMATURE_AUTO')
    me = proxy.data
    co = np.zeros(len(me.vertices) * 3)
    me.vertices.foreach_get("co", co)
    gi = {g.index: sk.DEFORM.index(g.name) for g in proxy.vertex_groups if g.name in sk.DEFORM}
    W = np.zeros((len(me.vertices), len(sk.DEFORM)), np.float32)
    for v in me.vertices:
        for g in v.groups:
            if g.group in gi:
                W[v.index, gi[g.group]] = g.weight
    return co.reshape(-1, 3), W


def prune(proxy):
    """keep only the proxy's main shell: crumbs the voxels leave of thin cables can make the solver give up on the
    whole mesh (orbb); the mesh's bits there take the nearest proxy points' weights anyway"""
    bm = bmesh.new()
    bm.from_mesh(proxy.data)
    seen, comps = set(), []
    for v in bm.verts:
        if v in seen:
            continue
        comp, st = [], [v]
        while st:
            x = st.pop()
            if x in seen:
                continue
            seen.add(x)
            comp.append(x)
            st.extend(e.other_vert(x) for e in x.link_edges)
        comps.append(comp)
    comps.sort(key=len, reverse=True)
    bmesh.ops.delete(bm, geom=[v for c in comps[1:] for v in c], context='VERTS')
    bm.to_mesh(proxy.data)
    bm.free()
    for g in list(proxy.vertex_groups):
        proxy.vertex_groups.remove(g)
    proxy.parent = None
    proxy.modifiers.clear()
    print("PROXY pruned to its main shell: %d of %d islands dropped" % (len(comps) - 1, len(comps)))


best = None
for size in VOXELS:
    proxy = make_proxy(size)
    co, W = heat(proxy)
    bad = float((W.sum(1) < 1e-4).mean())
    if bad > 0.99:
        prune(proxy)
        co, W = heat(proxy)
        bad = float((W.sum(1) < 1e-4).mean())
    print("PROXY voxel %.3f unweighted %.1f%%" % (size, 100 * bad))
    if best is None or bad < best[0]:
        best = (bad, co, W)
    if bad < 0.01:
        break
    bpy.data.objects.remove(proxy)
bad, co, W = best
if bad > 0.05:
    raise SystemExit("heat weighting failed at every voxel size (%.0f%% unweighted)" % (100 * bad))
print("PROXY unweighted", int((W.sum(1) < 1e-4).sum()))
np.savez(argv[2], co=co, w=W, names=np.array(sk.DEFORM))
