class_name Q3EditorOverlay
extends Node3D
## What the map editor draws over the map: the selection (its own meshes, so a drag only moves
## this node instead of rebuilding cells), with a tint and its edges; the outline of the brush
## under the cursor; and the box the create tool is drawing.

const SELECT_TINT := Color(1.0, 0.35, 0.08, 0.12)
const SELECT_EDGE := Color(1.0, 0.55, 0.15)
const HOVER_EDGE := Color(1.0, 0.95, 0.85, 0.55)
const BOX_FILL := Color(0.3, 0.75, 1.0, 0.18)
const BOX_EDGE := Color(0.45, 0.85, 1.0)

var _sel := Node3D.new()
var _sel_mesh := MeshInstance3D.new()
var _sel_edges := MeshInstance3D.new()
var _sel_edges_xray := MeshInstance3D.new()
var _hover := MeshInstance3D.new()
var _box := MeshInstance3D.new()
var _box_edges := MeshInstance3D.new()
var _faces := MeshInstance3D.new()
var _faces_edges := MeshInstance3D.new()
var _patches := Node3D.new()        ## selected patches: their meshes and control nets
var _leak := MeshInstance3D.new()   ## q3map's leak line, from an entity out to the void
var _ents := Node3D.new()           ## selected point entities, drawn here so a drag moves them
var _hover_id := -1

const FACE_TINT := Color(0.3, 0.85, 1.0, 0.38)
const FACE_EDGE := Color(0.55, 0.95, 1.0)
const NET := Color(1.0, 0.8, 0.3, 0.9)


func _ready() -> void:
	add_child(_sel)
	_sel.add_child(_sel_mesh)
	_sel.add_child(_sel_edges)
	_sel.add_child(_sel_edges_xray)
	_sel_mesh.material_overlay = _flat(SELECT_TINT, false)
	(_sel_mesh.material_overlay as StandardMaterial3D).cull_mode = BaseMaterial3D.CULL_BACK
	_sel_edges.material_override = _flat(Color.WHITE, false, true)
	_sel_edges_xray.material_override = _flat(Color(1, 1, 1, 0.25), true, true)
	add_child(_hover)
	_hover.material_override = _flat(Color.WHITE, false, true)
	add_child(_box)
	_box.material_override = _flat(BOX_FILL, false)
	add_child(_box_edges)
	_box_edges.material_override = _flat(Color.WHITE, true, true)
	add_child(_faces)
	_faces.material_override = _flat(FACE_TINT, false)
	(_faces.material_override as StandardMaterial3D).render_priority = 2
	add_child(_faces_edges)
	_faces_edges.material_override = _flat(Color.WHITE, true, true)
	_sel.add_child(_patches)
	add_child(_leak)
	_leak.material_override = _flat(Color.WHITE, true, true)
	_leak.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_sel.add_child(_ents)
	for m in [_sel_mesh, _sel_edges, _sel_edges_xray, _hover, _box, _box_edges, _faces, _faces_edges]:
		m.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF


static func _flat(c: Color, xray: bool, vertex_color := false) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	m.albedo_color = c
	m.vertex_color_use_as_albedo = vertex_color
	if c.a < 1.0:
		m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	m.no_depth_test = xray
	m.cull_mode = BaseMaterial3D.CULL_DISABLED
	m.render_priority = 1
	return m


## Draws these brushes as the selection (real materials plus the tint and edges).
func show_selection(doc: Q3MapDoc, ids: Array) -> void:
	_sel.position = Vector3.ZERO
	var ws := []
	var planes := PackedFloat64Array()
	var uvs := PackedFloat64Array()
	var sizes := PackedFloat64Array()
	var slots := PackedInt32Array()
	var names: PackedStringArray = []
	# every edge and control net goes into one line surface: a mesh holds at most 256 surfaces, and
	# select-all on a real map has thousands of brushes
	var edge_ws := []
	var nets := []
	for id in ids:
		var b := doc.get_obj(id)
		if b.get("type", "") != "brush":
			continue
		var w := doc.windings(id)
		for i in b["faces"].size():
			if i >= w.size() or w[i].is_empty():
				continue
			var f: Dictionary = b["faces"][i]
			var pl := Q3MapDoc.plane_of(b, i)
			var sz := Q3MapMaterials.size(f["material"])
			var tint: Color = f.get("color", Color.WHITE)
			var key: String = f["material"] if tint == Color.WHITE else f["material"] + "|" + tint.to_html(false)
			var slot := names.find(key)
			if slot < 0:
				slot = names.size()
				names.append(key)
			ws.append(w[i])
			planes.append_array(pl)
			uvs.append_array(Q3MapDoc.face_uv(f, pl))
			sizes.append_array([sz.x, sz.y])
			slots.append(slot)
		edge_ws.append_array(w)
	var mesh := ArrayMesh.new()
	if ws.size():
		var surfaces: Dictionary = Q3MapGeo.mesh_faces(ws, planes, uvs, sizes, slots, PackedColorArray())
		for slot in surfaces:
			mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, surfaces[slot])
			mesh.surface_set_material(mesh.get_surface_count() - 1, Q3MapMesher.slot_material(names[slot]))
	# selected patches: the surface with the tint, and the control net
	for c in _patches.get_children():
		c.queue_free()
	for id in ids:
		var p := doc.get_obj(id)
		if p.get("type", "") != "patch":
			continue
		var mi := MeshInstance3D.new()
		mi.mesh = Q3MapMesher.patch_mesh(doc.patch_tess(id), p["material"], p.get("color", Color.WHITE))
		mi.material_overlay = _sel_mesh.material_overlay
		mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		_patches.add_child(mi)
		var back := MeshInstance3D.new()
		back.mesh = mi.mesh
		back.material_override = Q3MapMesher.back_material()
		back.material_overlay = _sel_mesh.material_overlay
		back.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		mi.add_child(back)
		nets.append(p)
	# selected point entities: their look, and their box
	for c in _ents.get_children():
		c.queue_free()
	for id in ids:
		var e := doc.get_obj(id)
		if e.get("type", "") != "entity":
			continue
		var v := Q3EditorEntities.make_visual(e, doc, true)
		if v:
			_ents.add_child(v)
		var box := Q3MapEditor.entity_box(e)
		if box.size != Vector3.ZERO:
			edge_ws.append_array(Q3MapGeo.brush_windings(_box_planes(box)))
	var edges := ImmediateMesh.new()
	var any_edge := edge_ws.any(func(w): return w.size() >= 6) or nets.any(func(p): return p["width"] >= 2 and p["height"] >= 2)
	if any_edge:
		edges.surface_begin(Mesh.PRIMITIVE_LINES)
		_edges_into(edges, edge_ws, SELECT_EDGE)
		for p in nets:
			_net_into(edges, p)
		edges.surface_end()
	_sel_mesh.mesh = mesh if ws.size() else null
	_sel_edges.mesh = edges if any_edge else null
	_sel_edges_xray.mesh = edges if any_edge else null


## A patch's control net: the rows and the columns of its control points, into an open line surface.
func _net_into(im: ImmediateMesh, p: Dictionary) -> void:
	var c: PackedFloat64Array = p["ctrl"]
	var w: int = p["width"]
	var h: int = p["height"]
	if w < 2 or h < 2:
		return
	for j in h:
		for i in w:
			var a := Q3MapEditor.to_godot(Vector3(c[(j * w + i) * 5], c[(j * w + i) * 5 + 1], c[(j * w + i) * 5 + 2]))
			for nb in [[i + 1, j], [i, j + 1]]:
				if nb[0] < w and nb[1] < h:
					var k: int = nb[1] * w + nb[0]
					im.surface_set_color(NET)
					im.surface_add_vertex(a)
					im.surface_set_color(NET)
					im.surface_add_vertex(Q3MapEditor.to_godot(Vector3(c[k * 5], c[k * 5 + 1], c[k * 5 + 2])))


## The face tool's selection: each face tinted and outlined, lifted a hair off its brush.
func show_faces(doc: Q3MapDoc, keys: Array) -> void:
	if keys.is_empty():
		_faces.mesh = null
		_faces_edges.mesh = null
		return
	var polys := []
	for k: Vector2i in keys:
		var w := doc.windings(k.x)
		if k.y >= w.size() or w[k.y].is_empty():
			continue
		var b := doc.get_obj(k.x)
		var n := Vector3(b["planes"][k.y * 4], b["planes"][k.y * 4 + 1], b["planes"][k.y * 4 + 2])
		var poly: PackedFloat64Array = w[k.y]
		var lifted := PackedFloat64Array()
		for i in poly.size() / 3:
			var p := Vector3(poly[i * 3], poly[i * 3 + 1], poly[i * 3 + 2]) + n * 0.25
			lifted.append_array([p.x, p.y, p.z])
		polys.append(lifted)
	if polys.is_empty():
		_faces.mesh = null
		_faces_edges.mesh = null
		return
	var tris := ImmediateMesh.new()
	tris.surface_begin(Mesh.PRIMITIVE_TRIANGLES)
	for lifted: PackedFloat64Array in polys:
		for i in range(1, lifted.size() / 3 - 1):
			for v in [0, i + 1, i]:
				tris.surface_add_vertex(Q3MapEditor.to_godot(Vector3(lifted[v * 3], lifted[v * 3 + 1], lifted[v * 3 + 2])))
	tris.surface_end()
	_faces.mesh = tris
	var edges := ImmediateMesh.new()
	_faces_edges.mesh = edges if _add_edges(edges, polys, FACE_EDGE) else null


## q3map's leak line (id-space points); empty hides it. Drawn through everything, in red.
func show_leak(points: PackedVector3Array) -> void:
	if points.size() < 2:
		_leak.mesh = null
		return
	var im := ImmediateMesh.new()
	im.surface_begin(Mesh.PRIMITIVE_LINES)
	for i in points.size() - 1:
		for p in [points[i], points[i + 1]]:
			im.surface_set_color(Color(1.0, 0.15, 0.1))
			im.surface_add_vertex(Q3MapEditor.to_godot(p))
	im.surface_end()
	_leak.mesh = im


func leak_shown() -> bool:
	return _leak.mesh != null


## Moves the drawn selection by an offset in id units (a drag in progress).
func set_selection_offset(id_offset: Vector3) -> void:
	_sel.position = Q3MapEditor.to_godot(id_offset)


func show_hover(doc: Q3MapDoc, id: int) -> void:
	if id == _hover_id and _hover.mesh != null:
		return
	_hover_id = id
	var o := doc.get_obj(id)
	var w: Array = []
	match o.get("type", ""):
		"brush":
			w = doc.windings(id)
		"entity":
			var box := Q3MapEditor.entity_box(o)
			if box.size != Vector3.ZERO:
				w = Q3MapGeo.brush_windings(_box_planes(box))
	if w.is_empty():
		_hover.mesh = null
		return
	var edges := ImmediateMesh.new()
	_hover.mesh = edges if _add_edges(edges, w, HOVER_EDGE) else null


static func _box_planes(box: AABB) -> PackedFloat64Array:
	return Q3MapGeo.box(Q3MapEditor.f64(box.position), Q3MapEditor.f64(box.end))


func clear_hover() -> void:
	_hover_id = -1
	_hover.mesh = null


## The create tool's box, id-space corners; empty mins == maxs hides it.
func show_box(mins: Vector3, maxs: Vector3) -> void:
	if mins == maxs:
		_box.mesh = null
		_box_edges.mesh = null
		return
	# a flat box (the base being dragged, no height yet) has no hull: show its footprint as a thin slab
	for k in 3:
		if maxs[k] - mins[k] < 0.5:
			maxs[k] = mins[k] + 0.5
	var planes: PackedFloat64Array = Q3MapGeo.box(PackedFloat64Array([mins.x, mins.y, mins.z]), PackedFloat64Array([maxs.x, maxs.y, maxs.z]))
	var w: Array = Q3MapGeo.brush_windings(planes)
	if not w.any(func(p): return p.size() >= 9):
		_box.mesh = null
		_box_edges.mesh = null
		return
	var tris := ImmediateMesh.new()
	tris.surface_begin(Mesh.PRIMITIVE_TRIANGLES)
	for poly in w:
		for i in range(1, poly.size() / 3 - 1):
			for k in [0, i + 1, i]:
				tris.surface_add_vertex(Q3MapEditor.to_godot(Vector3(poly[k * 3], poly[k * 3 + 1], poly[k * 3 + 2])))
	tris.surface_end()
	_box.mesh = tris
	var edges := ImmediateMesh.new()
	_box_edges.mesh = edges if _add_edges(edges, w, BOX_EDGE) else null


## Polygon outlines as their own line surface; false (and nothing added) when there are none.
func _add_edges(im: ImmediateMesh, windings: Array, c: Color) -> bool:
	if not windings.any(func(w): return w.size() >= 6):
		return false
	im.surface_begin(Mesh.PRIMITIVE_LINES)
	_edges_into(im, windings, c)
	im.surface_end()
	return true


## Polygon outlines into an open line surface.
func _edges_into(im: ImmediateMesh, windings: Array, c: Color) -> void:
	for poly in windings:
		var n: int = poly.size() / 3
		if n < 2:
			continue
		for i in n:
			var j := (i + 1) % n
			im.surface_set_color(c)
			im.surface_add_vertex(Q3MapEditor.to_godot(Vector3(poly[i * 3], poly[i * 3 + 1], poly[i * 3 + 2])))
			im.surface_set_color(c)
			im.surface_add_vertex(Q3MapEditor.to_godot(Vector3(poly[j * 3], poly[j * 3 + 1], poly[j * 3 + 2])))
