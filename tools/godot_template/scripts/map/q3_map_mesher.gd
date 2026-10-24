class_name Q3MapMesher
extends Node3D
## Draws a Q3MapDoc. Brushes are merged into one mesh per 16 m cell (512 units), with one surface per
## material, and only cells whose brushes changed are rebuilt (OZMIUM's MapChunkView). Each patch
## gets its own mesh.

const CELL := 512.0

var doc: Q3MapDoc
var exclude := {}                ## id -> true: drawn elsewhere (the selection, see-through brushes)
var show_backs := false          ## the editor: patch back faces drawn dim (Quake 3 culls them, so a
								 ## curve seen from behind would vanish)
var rebuilds := 0                ## cell rebuilds so far (tests, perf bench)
var skip_kinds := {}             ## material kinds not drawn (play-test: {"hidden": true, "fog": true},
								 ## the q3map tool shaders the game never draws)
var only := {}                   ## when not empty, the only ids drawn (one brush model)
var show_hidden := false         ## draw objects the editor hides too (a play-test: they're in the .bsp)
var face_filter := Callable()    ## material -> true when the editor's view filters hide it (VIEW menu)
var hide_patches := false        ## the Curves view filter

var _cell_of := {}               ## brush id -> Vector3i
var _cells := {}                 ## Vector3i -> {id: true}
var _nodes := {}                 ## Vector3i -> MeshInstance3D
var _patches := {}               ## patch id -> MeshInstance3D
var _dirty := {}                 ## Vector3i -> true
var _slots := {}                 ## material -> slot
var _slot_names: PackedStringArray = []


func bind(d: Q3MapDoc) -> void:
	if doc and doc.changed.is_connected(_on_changed):
		doc.changed.disconnect(_on_changed)
	doc = d
	doc.changed.connect(_on_changed)
	_rebuild_all()


## Redraws everything (after the view filters change).
func redraw_all() -> void:
	if doc:
		_rebuild_all()


## Redraws these objects at the next flush (after hide/show or a change in exclude).
func touch(ids: Array) -> void:
	for id in ids:
		_touch(id)


func _process(_delta: float) -> void:
	flush()


## Rebuilds the cells that changed. Runs every frame; call it directly to see changes at once.
func flush() -> void:
	for key in _dirty:
		_build_cell(key)
	_dirty.clear()


func cell_count() -> int:
	return _nodes.size()


func _on_changed(ids: Array) -> void:
	if ids.is_empty():
		_rebuild_all()
	else:
		touch(ids)


func _rebuild_all() -> void:
	for n in _nodes.values():
		n.queue_free()
	for n in _patches.values():
		n.queue_free()
	_cell_of.clear()
	_cells.clear()
	_nodes.clear()
	_patches.clear()
	_dirty.clear()
	for id in doc.objects:
		_touch(id)
	flush()


func _touch(id: int) -> void:
	if _cell_of.has(id):
		var old: Vector3i = _cell_of[id]
		_cells[old].erase(id)
		_dirty[old] = true
		_cell_of.erase(id)
	if _patches.has(id):
		_patches[id].queue_free()
		_patches.erase(id)
	var o := doc.get_obj(id)
	match o.get("type", ""):
		"brush":
			var key := _cell_key(o)
			_cell_of[id] = key
			if not _cells.has(key):
				_cells[key] = {}
			_cells[key][id] = true
			_dirty[key] = true
		"patch":
			var drawn: bool = (only.is_empty() or only.has(id)) and not hide_patches and not (face_filter.is_valid() and face_filter.call(o["material"]))
			if drawn and (show_hidden or not doc.hidden.has(id)) and not exclude.has(id) and not skip_kinds.has(Q3MapMaterials.kind(o["material"])):
				_patches[id] = _patch_node(o)


func _cell_key(b: Dictionary) -> Vector3i:
	var bb: PackedFloat64Array = Q3MapGeo.brush_bounds(b["planes"])
	if bb.size() < 6:
		return Vector3i.ZERO
	return Vector3i(floori((bb[0] + bb[3]) * 0.5 / CELL), floori((bb[1] + bb[4]) * 0.5 / CELL), floori((bb[2] + bb[5]) * 0.5 / CELL))


## One surface per material and tint (a tinted face uses a tinted copy of the material).
func _slot(material: String, tint: Color) -> int:
	var key := material if tint == Color.WHITE else material + "|" + tint.to_html(false)
	if not _slots.has(key):
		_slots[key] = _slot_names.size()
		_slot_names.append(key)
	return _slots[key]


static func slot_material(key: String) -> Material:
	var parts := key.split("|")
	return Q3MapMaterials.material(parts[0], Color.html(parts[1]) if parts.size() > 1 else Color.WHITE)


func _build_cell(key: Vector3i) -> void:
	rebuilds += 1
	var ws := []
	var planes := PackedFloat64Array()
	var uvs := PackedFloat64Array()
	var sizes := PackedFloat64Array()
	var slots := PackedInt32Array()
	for id in _cells.get(key, {}):
		if (doc.hidden.has(id) and not show_hidden) or exclude.has(id) or not (only.is_empty() or only.has(id)):
			continue
		var b := doc.get_obj(id)
		var w := doc.windings(id)
		for i in b["faces"].size():
			if i >= w.size() or w[i].is_empty():
				continue
			var f: Dictionary = b["faces"][i]
			if skip_kinds.size() and skip_kinds.has(Q3MapMaterials.kind(f["material"])):
				continue
			if face_filter.is_valid() and face_filter.call(f["material"]):
				continue
			var pl := Q3MapDoc.plane_of(b, i)
			var sz := Q3MapMaterials.size(f["material"])
			ws.append(w[i])
			planes.append_array(pl)
			uvs.append_array(Q3MapDoc.face_uv(f, pl))
			sizes.append_array([sz.x, sz.y])
			slots.append(_slot(f["material"], f.get("color", Color.WHITE)))
	var node: MeshInstance3D = _nodes.get(key)
	if ws.is_empty():
		if node:
			node.queue_free()
			_nodes.erase(key)
		if _cells.get(key, {}).is_empty():
			_cells.erase(key)
		return
	var surfaces: Dictionary = Q3MapGeo.mesh_faces(ws, planes, uvs, sizes, slots, PackedColorArray())
	var mesh := ArrayMesh.new()
	for slot in surfaces:
		mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, surfaces[slot])
		mesh.surface_set_material(mesh.get_surface_count() - 1, slot_material(_slot_names[slot]))
	if node == null:
		node = MeshInstance3D.new()
		node.name = "cell_%d_%d_%d" % [key.x, key.y, key.z]
		add_child(node)
		_nodes[key] = node
	node.mesh = mesh


func _patch_node(p: Dictionary) -> MeshInstance3D:
	var node := MeshInstance3D.new()
	node.name = "patch_%d" % p["id"]
	node.mesh = patch_mesh(doc.patch_tess(p["id"]), p["material"], p.get("color", Color.WHITE))
	if show_backs and node.mesh:
		var back := MeshInstance3D.new()
		back.mesh = node.mesh
		back.material_override = back_material()
		back.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		node.add_child(back)
	add_child(node)
	return node


static var _back: StandardMaterial3D

## The back side of a one-sided surface: dark and see-through, only where the front isn't facing.
static func back_material() -> StandardMaterial3D:
	if _back == null:
		_back = StandardMaterial3D.new()
		_back.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		_back.albedo_color = Color(0.55, 0.5, 0.48, 0.35)
		_back.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		_back.cull_mode = BaseMaterial3D.CULL_FRONT
	return _back


## A patch's tessellation as a mesh with tangents and its material (the overlay draws selected ones).
static func patch_mesh(tess: Dictionary, material: String, tint := Color.WHITE) -> ArrayMesh:
	var arrays: Array = tess["arrays"]
	if arrays[Mesh.ARRAY_VERTEX] == null:
		return null
	var st := SurfaceTool.new()
	st.create_from_arrays(arrays)
	st.generate_tangents()
	var mesh := st.commit()
	mesh.surface_set_material(0, Q3MapMaterials.material(material, tint))
	return mesh
