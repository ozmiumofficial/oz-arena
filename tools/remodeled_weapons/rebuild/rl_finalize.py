"""The rebuild's parts as one game mesh: modifiers applied, seams marked, unwrapped, joined, packed.

Seams, per part: faces are grouped into caps (facing along the part's axis: its uv_axis, the direction it was extruded
or turned about) and sides; seams run between groups, and each group piece that is a band or has holes gets cut
along the shortest edge path between its boundary loops, so every island is a disc that unwraps flat (angle based).
Cables (uv_axis 'none') split at their end caps. Then the parts are joined into 'rocket_launcher' (collection
'Final'), islands scaled to one texel density and packed.
"""
import bpy, bmesh, math, heapq
from mathutils import Vector

AXES = {"x": Vector((1, 0, 0)), "y": Vector((0, 1, 0)), "z": Vector((0, 0, 1))}


def _groups(bm, axis):
    g = {}
    for f in bm.faces:
        if axis is None:
            g[f] = 1 if len(f.verts) > 4 else 0
        else:
            d = f.normal.dot(axis)
            g[f] = 1 if d > 0.7 else (2 if d < -0.7 else 0)
    return g


def _components(faces, g):
    seen, comps = set(), []
    for f0 in faces:
        if f0 in seen:
            continue
        comp, st = [], [f0]
        seen.add(f0)
        while st:
            f = st.pop(); comp.append(f)
            for e in f.edges:
                if e.seam:
                    continue
                for h in e.link_faces:
                    if h not in seen and g[h] == g[f0]:
                        seen.add(h); st.append(h)
        comps.append(comp)
    return comps


def _boundary_loops(comp):
    cs = set(comp)
    bnd = [e for f in comp for e in f.edges
           if e.seam or sum(1 for h in e.link_faces if h in cs) < 2 or e.is_boundary]
    bnd = list(set(bnd))
    left, loops = set(bnd), []
    while left:
        e0 = left.pop(); loop, st = {e0}, [e0]
        while st:
            e = st.pop()
            for v in e.verts:
                for e2 in v.link_edges:
                    if e2 in left:
                        left.discard(e2); loop.add(e2); st.append(e2)
        loops.append(loop)
    return loops


def _cut(comp, src_loop, dst_loop):
    """Mark the shortest edge path inside comp from one boundary loop to another as seam."""
    cs = set(comp)
    inner = {e for f in comp for e in f.edges}
    src = {v for e in src_loop for v in e.verts}
    dst = {v for e in dst_loop for v in e.verts}
    dist = {v: 0.0 for v in src}
    prev = {}
    pq = [(0.0, i, v) for i, v in enumerate(src)]
    heapq.heapify(pq)
    cnt = len(pq)
    end = None
    while pq:
        d, _, v = heapq.heappop(pq)
        if d > dist.get(v, 1e9):
            continue
        if v in dst:
            end = v; break
        for e in v.link_edges:
            if e not in inner:
                continue
            w = e.other_vert(v)
            nd = d + e.calc_length()
            if nd < dist.get(w, 1e9):
                dist[w] = nd; prev[w] = (v, e); cnt += 1
                heapq.heappush(pq, (nd, cnt, w))
    v = end
    while v is not None and v in prev:
        u, e = prev[v]
        e.seam = True
        v = u


def mark_seams(me, uv_axis):
    bm = bmesh.new(); bm.from_mesh(me)
    for e in bm.edges:
        e.seam = False
    axis = AXES.get(uv_axis)
    g = _groups(bm, axis)
    for e in bm.edges:
        fs = e.link_faces
        if len(fs) == 2 and g[fs[0]] != g[fs[1]]:
            e.seam = True
    for _ in range(3):           # a cut can split a loop list; repeat until every piece is a disc
        changed = False
        for comp in _components(bm.faces, g):
            loops = _boundary_loops(comp)
            if len(loops) >= 2:
                for L in loops[1:]:
                    _cut(comp, loops[0], L)
                changed = True
            elif len(loops) == 0:     # a closed piece: open it along a shortest loop-free path (two cuts)
                vs = [v for f in comp for v in f.verts]
                a = min(vs, key=lambda v: v.co.z)
                b = max(vs, key=lambda v: v.co.z)
                _cut(comp, [e for e in a.link_edges][:1], [e for e in b.link_edges][:1])
                changed = True
        if not changed:
            break
    seams = [e.seam for e in bm.edges]
    bm.free()
    me.edges.foreach_set("use_seam", seams)


def applied_copy(ob, col):
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(ob.evaluated_get(dg), preserve_all_data_layers=True, depsgraph=dg)
    me.name = "F_" + ob.name
    # drop zero-area faces left where a bevel closes on itself
    bm = bmesh.new(); bm.from_mesh(me)
    deg = [f for f in bm.faces if f.calc_area() < 1e-11]
    if deg:
        bmesh.ops.dissolve_degenerate(bm, dist=1e-6, edges=list({e for f in deg for e in f.edges}))
        bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new("F_" + ob.name, me)
    col.objects.link(o)
    return o


def finalize(texel_margin=0.0025):
    sc = bpy.context.scene
    col = bpy.data.collections.get("Final")
    if col is None:
        col = bpy.data.collections.new("Final"); sc.collection.children.link(col)
    for o in list(col.objects):
        me = o.data; bpy.data.objects.remove(o)
        if me and me.users == 0:
            bpy.data.meshes.remove(me)
    src = [o for o in bpy.data.collections["Rebuild"].objects if o.type == "MESH"]
    parts = []
    for ob in src:
        o = applied_copy(ob, col)
        mark_seams(o.data, ob.get("uv_axis", "x"))
        if not o.data.uv_layers:
            o.data.uv_layers.new(name="UVMap")
        parts.append(o)
    # unwrap each part on its own seams
    if bpy.context.object and bpy.context.object.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    for o in parts:
        o.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.unwrap(method="ANGLE_BASED", margin=0.001)
    bpy.ops.object.mode_set(mode="OBJECT")
    # one object
    bpy.ops.object.join()
    final = bpy.context.view_layer.objects.active
    final.name = "rocket_launcher"; final.data.name = "rocket_launcher"
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.select_all(action="SELECT")
    bpy.ops.uv.average_islands_scale()
    bpy.ops.uv.pack_islands(rotate=True, margin=texel_margin, shape_method="CONCAVE")
    bpy.ops.object.mode_set(mode="OBJECT")
    return final


def stats(ob):
    me = ob.data
    me.calc_loop_triangles()
    import numpy as np
    n = len(me.loop_triangles)
    tl = np.zeros(n * 3, np.int32); me.loop_triangles.foreach_get("loops", tl)
    uv = np.zeros(len(me.loops) * 2, np.float32); me.uv_layers.active.data.foreach_get("uv", uv)
    uv = uv.reshape(-1, 2)[tl].reshape(-1, 3, 2)
    a = uv[:, 1] - uv[:, 0]; b = uv[:, 2] - uv[:, 0]
    uv_area = 0.5 * np.abs(a[:, 0] * b[:, 1] - a[:, 1] * b[:, 0]).sum()
    area3 = sum(t.area for t in me.loop_triangles)
    bm = bmesh.new(); bm.from_mesh(me)
    islands = 0
    seen = set()
    uvl = bm.loops.layers.uv.active
    # islands: faces connected through non-seam edges
    for f0 in bm.faces:
        if f0 in seen:
            continue
        islands += 1
        st = [f0]; seen.add(f0)
        while st:
            f = st.pop()
            for e in f.edges:
                if e.seam:
                    continue
                for h in e.link_faces:
                    if h not in seen:
                        seen.add(h); st.append(h)
    bm.free()
    return {"tris": n, "uv_coverage": round(float(uv_area), 3), "surface": round(area3, 4), "islands": islands,
            "texels_per_unit_at_4k": round(4096 * math.sqrt(uv_area / area3), 0)}
