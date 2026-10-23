class_name Q3MapMesher
extends Node3D
## Draws a Q3MapDoc. Brushes are merged into one mesh per 16 m cell (512 units), with one surface per
## material, and only cells whose brushes changed are rebuilt (OZMIUM's MapChunkView). Each patch
## gets its own mesh.

const CELL := 512.0
const PATCH_TOLERANCE := 4.0     ## units a curve may stray from its triangles (r_subdivisions' default)

var doc: Q3MapDoc
var exclude := {}                ## id -> true: drawn elsewhere (the selection, see-through brushes)
var rebuilds := 0                ## cell rebuilds so far (tests, perf bench)

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
			if not doc.hidden.has(id) and not exclude.has(id):
				_patches[id] = _patch_node(o)


func _cell_key(b: Dictionary) -> Vector3i:
	var bb: PackedFloat64Array = Q3MapGeo.brush_bounds(b["planes"])
	if bb.size() < 6:
		return Vector3i.ZERO
	return Vector3i(floori((bb[0] + bb[3]) * 0.5 / CELL), floori((bb[1] + bb[4]) * 0.5 / CELL), floori((bb[2] + bb[5]) * 0.5 / CELL))


func _slot(material: String) -> int:
	if not _slots.has(material):
		_slots[material] = _slot_names.size()
		_slot_names.append(material)
	return _slots[material]


func _build_cell(key: Vector3i) -> void:
	rebuilds += 1
	var ws := []
	var planes := PackedFloat64Array()
	var uvs := PackedFloat64Array()
	var sizes := PackedFloat64Array()
	var slots := PackedInt32Array()
	var colors := PackedColorArray()
	var tinted := false
	for id in _cells.get(key, {}):
		if doc.hidden.has(id) or exclude.has(id):
			continue
		var b := doc.get_obj(id)
		var w := doc.windings(id)
		for i in b["faces"].size():
			if i >= w.size() or w[i].is_empty():
				continue
			var f: Dictionary = b["faces"][i]
			var pl := Q3MapDoc.plane_of(b, i)
			var sz := Q3MapMaterials.size(f["material"])
			ws.append(w[i])
			planes.append_array(pl)
			uvs.append_array(Q3MapDoc.face_uv(f, pl))
			sizes.append_array([sz.x, sz.y])
			slots.append(_slot(f["material"]))
			colors.append(f.get("color", Color.WHITE))
			tinted = tinted or f.has("color")
	var node: MeshInstance3D = _nodes.get(key)
	if ws.is_empty():
		if node:
			node.queue_free()
			_nodes.erase(key)
		if _cells.get(key, {}).is_empty():
			_cells.erase(key)
		return
	var surfaces: Dictionary = Q3MapGeo.mesh_faces(ws, planes, uvs, sizes, slots, colors if tinted else PackedColorArray())
	var mesh := ArrayMesh.new()
	for slot in surfaces:
		mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, surfaces[slot])
		mesh.surface_set_material(mesh.get_surface_count() - 1, Q3MapMaterials.material(_slot_names[slot]))
	if node == null:
		node = MeshInstance3D.new()
		node.name = "cell_%d_%d_%d" % [key.x, key.y, key.z]
		add_child(node)
		_nodes[key] = node
	node.mesh = mesh


func _patch_node(p: Dictionary) -> MeshInstance3D:
	var r: Dictionary = Q3MapGeo.patch_mesh(p["ctrl"], p["width"], p["height"], PATCH_TOLERANCE, p.get("color", Color.WHITE))
	var node := MeshInstance3D.new()
	node.name = "patch_%d" % p["id"]
	var arrays: Array = r["arrays"]
	if arrays[Mesh.ARRAY_VERTEX] != null:
		var st := SurfaceTool.new()
		st.create_from_arrays(arrays)
		st.generate_tangents()
		node.mesh = st.commit()
		node.mesh.surface_set_material(0, Q3MapMaterials.material(p["material"]))
	add_child(node)
	return node
