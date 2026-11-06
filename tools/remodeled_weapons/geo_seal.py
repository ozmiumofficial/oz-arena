"""blender -b --python geo_seal.py -- in.fbx out.blend
Tripo's own surface, sealed: welded, cleaned, every open boundary capped (holes_fill, then a fan to the centroid
for loops it can't take), normals made consistent, triangulated, smooth shading with edges over 35 degrees sharp.
Objects: 'orig' (as imported, for the color bake), 'high' and 'low' (both the sealed surface)."""
import bpy, bmesh, sys, math
a = sys.argv[sys.argv.index("--") + 1:]
src, out = a[0], a[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=src)
ob = next(o for o in bpy.context.scene.objects if o.type == 'MESH')
for o in list(bpy.context.scene.objects):
    if o is not ob:
        bpy.data.objects.remove(o)
bpy.context.view_layer.objects.active = ob
ob.select_set(True)
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
ob.name = "orig"

lo = ob.copy()
lo.data = ob.data.copy()
lo.name = "low"
bpy.context.scene.collection.objects.link(lo)
bm = bmesh.new()
bm.from_mesh(lo.data)
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.0004)
bmesh.ops.dissolve_degenerate(bm, edges=bm.edges, dist=1e-7)
seen, dup = {}, []
for f in bm.faces:
    k = frozenset(v.index for v in f.verts)
    if k in seen:
        dup.append(f)
    else:
        seen[k] = f
bmesh.ops.delete(bm, geom=dup, context='FACES_ONLY')
bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
# tiny floating crumbs
seen, crumbs = set(), []
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
    if len(comp) < 12:
        crumbs.extend(comp)
bmesh.ops.delete(bm, geom=crumbs, context='VERTS')
bmesh.ops.split_edges(bm, edges=[e for e in bm.edges if len(e.link_faces) > 2])
print("open edges", sum(1 for e in bm.edges if e.is_boundary))
before = set(bm.faces)
bmesh.ops.holes_fill(bm, edges=[e for e in bm.edges if e.is_boundary], sides=0)
# each cap faces the way the surface around it does (Tripo's own faces are oriented right): a shared edge runs
# opposite ways in two consistently facing faces
flip = []
for f in bm.faces:
    if f in before:
        continue
    vote = 0
    for lp in f.loops:
        for other in lp.edge.link_loops:
            if other.face in before:
                vote += 1 if other.vert is lp.vert else -1   # same start vertex = same direction = wrong way
    if vote > 0:
        flip.append(f)
bmesh.ops.reverse_faces(bm, faces=flip)
print("caps", len(set(bm.faces) - before), "turned", len(flip))
rest = set(e for e in bm.edges if e.is_boundary)
fans = 0
while rest:
    e0 = rest.pop()
    comp, st = [e0], [e0]
    while st:
        x = st.pop()
        for v in x.verts:
            for f in v.link_edges:
                if f in rest:
                    rest.remove(f)
                    comp.append(f)
                    st.append(f)
    vl = list({v for e in comp for v in e.verts})
    ctr = vl[0].co.copy() * 0
    for v in vl:
        ctr += v.co
    c = bm.verts.new(ctr / len(vl))
    for e in comp:
        lp = e.link_loops[0]
        a_, b_ = lp.vert, lp.edge.other_vert(lp.vert)
        try:
            bm.faces.new((b_, a_, c))
        except ValueError:
            pass
    fans += 1
bmesh.ops.triangulate(bm, faces=bm.faces)
# every piece is closed now: one whose signed volume comes out negative is inside out (some of Tripo's are)
seen, turned = set(), 0
for f0 in bm.faces:
    if f0 in seen:
        continue
    comp, st = [], [f0]
    seen.add(f0)
    while st:
        f = st.pop()
        comp.append(f)
        for e in f.edges:
            for g in e.link_faces:
                if g not in seen:
                    seen.add(g)
                    st.append(g)
    vol = 0.0
    for f in comp:
        a_, b_, c_ = (v.co for v in f.verts)
        vol += a_.dot(b_.cross(c_))
    if vol < 0:
        bmesh.ops.reverse_faces(bm, faces=comp)
        turned += 1
print("pieces turned right side out", turned)
print("fans", fans, "open edges left", sum(1 for e in bm.edges if e.is_boundary),
      "tris", len(bm.faces))
bm.to_mesh(lo.data)
bm.free()
lo.data.shade_smooth()
if hasattr(lo.data, "set_sharp_from_angle"):
    lo.data.set_sharp_from_angle(angle=math.radians(35))
hi = lo.copy()
hi.data = lo.data.copy()
hi.name = "high"
bpy.context.scene.collection.objects.link(hi)
bpy.ops.wm.save_as_mainfile(filepath=out)
