class_name Q3MapDoc
extends RefCounted
## The map editor's document (MAP_EDITOR_PLAN.md E0), after OZMIUM's MapDocument.
## Brushes, patches and entities share one id pool. A stored object is never changed in place: an
## edit puts a new Dictionary under its id, so an undo snapshot is a shallow copy of the pool that
## shares every unchanged object.
##
## Space is id Tech 3's (Z up, game units) in double precision; the geometry is Q3MapGeo's.
##   brush:  {type: "brush", id, entity, group, planes: PackedFloat64Array (4 per face), faces: [face]}
##   face:   {material, tex: [shift s, shift t, rotate, scale s, scale t]}    world projection, as Quake 3
##        or {material, uv: [Ux, Uy, Uz, Uoff, Vx, Vy, Vz, Voff]}          explicit, in texels
##           optional: lock (texture lock: the mapping moves with the brush), color (tint),
##           flags ([contents, surface, value], kept from a .map)
##   patch:  {type: "patch", id, entity, group, width, height, ctrl: PackedFloat64Array (x, y, z, s, t
##           per control point, row by row), material, color}
##   entity: {type: "entity", id, group, classname, keys: {String: String}}   id's key/values
## A brush or patch whose entity isn't 0 belongs to that brush entity (doors, triggers ...).

signal changed(ids: Array)   ## objects added, edited or removed; empty means everything (load, undo)

const FORMAT := "q3remaster-map"
const VERSION := 1
const EXTENSION := ".q3map.json"
const UNDO_LIMIT := 100
const DEFAULT_TEX: Array[float] = [0.0, 0.0, 0.0, 0.5, 0.5]   ## Quake 3's default scale is 0.5

var info := {"name": "untitled", "author": "", "description": ""}
var worldspawn := {}     ## worldspawn's key/values (message, music, gravity ...)
var objects := {}        ## id -> object
var groups := {}         ## id -> {id, name, parent}
var hidden := {}         ## id -> true
var locked := {}         ## id -> true
var camera := {}         ## the editor view when saved: {origin: [x, y, z], yaw, pitch}
var next_id := 1
var dirty := false       ## changed since the last save or load

var _undo: Array = []
var _redo: Array = []
var _windings := {}      ## id -> [object, windings]


# ---------------------------------------------------------------- objects

func new_id() -> int:
	next_id += 1
	return next_id - 1


## Stores obj (giving it an id if it has none) and returns the id.
func add(obj: Dictionary) -> int:
	if int(obj.get("id", 0)) <= 0:
		obj["id"] = new_id()
	else:
		next_id = maxi(next_id, int(obj["id"]) + 1)
	put(obj)
	return obj["id"]


## Replaces the object under obj.id. obj must be a new Dictionary (see edit()).
func put(obj: Dictionary) -> void:
	var id: int = obj["id"]
	objects[id] = obj
	dirty = true
	changed.emit([id])


func remove(ids: Array) -> void:
	var gone := []
	for id in ids:
		if objects.erase(id):
			gone.append(id)
			hidden.erase(id)
			locked.erase(id)
			_windings.erase(id)
	if gone.size():
		dirty = true
		changed.emit(gone)


func get_obj(id: int) -> Dictionary:
	return objects.get(id, {})


## A private copy of an object to change and put() back.
func edit(id: int) -> Dictionary:
	return objects.get(id, {}).duplicate(true)


func ids_of(type: String) -> Array:
	var out := []
	for id in objects:
		if objects[id]["type"] == type:
			out.append(id)
	return out


## Face polygons of a brush (Q3MapGeo.brush_windings), cached until the brush changes.
func windings(id: int) -> Array:
	var b: Dictionary = objects.get(id, {})
	var c: Array = _windings.get(id, [])
	if c.size() and is_same(c[0], b):
		return c[1]
	var w: Array = Q3MapGeo.brush_windings(b.get("planes", PackedFloat64Array()))
	_windings[id] = [b, w]
	return w


# ---------------------------------------------------------------- undo (whole-document snapshots)

## Call before a change: the state now becomes one undo step.
func checkpoint(label: String) -> void:
	_undo.push_back(_snapshot(label))
	if _undo.size() > UNDO_LIMIT:
		_undo.pop_front()
	_redo.clear()


func can_undo() -> bool:
	return not _undo.is_empty()


func can_redo() -> bool:
	return not _redo.is_empty()


func undo_label() -> String:
	return _undo.back()["label"] if _undo.size() else ""


func redo_label() -> String:
	return _redo.back()["label"] if _redo.size() else ""


func undo() -> bool:
	if _undo.is_empty():
		return false
	var s: Dictionary = _undo.pop_back()
	_redo.push_back(_snapshot(s["label"]))
	_restore(s)
	return true


func redo() -> bool:
	if _redo.is_empty():
		return false
	var s: Dictionary = _redo.pop_back()
	_undo.push_back(_snapshot(s["label"]))
	_restore(s)
	return true


func _snapshot(label: String) -> Dictionary:
	return {"label": label, "objects": objects.duplicate(), "groups": groups.duplicate(true),
			"worldspawn": worldspawn.duplicate(), "hidden": hidden.duplicate(), "locked": locked.duplicate()}


func _restore(s: Dictionary) -> void:
	var touched := []
	for id in objects:
		if not s["objects"].has(id) or not is_same(s["objects"][id], objects[id]):
			touched.append(id)
	for id in s["objects"]:
		if not objects.has(id):
			touched.append(id)
	objects = s["objects"].duplicate()
	groups = s["groups"].duplicate(true)
	worldspawn = s["worldspawn"].duplicate()
	hidden = s["hidden"].duplicate()
	locked = s["locked"].duplicate()
	dirty = true
	changed.emit(touched)


# ---------------------------------------------------------------- brushes and faces

static func brush(planes: PackedFloat64Array, material := Q3MapMaterials.DEFAULT) -> Dictionary:
	var faces := []
	for i in planes.size() / 4:
		faces.append({"material": material, "tex": DEFAULT_TEX.duplicate()})
	return {"type": "brush", "id": 0, "entity": 0, "group": 0, "planes": planes, "faces": faces}


static func box_brush(mins: Vector3, maxs: Vector3, material := Q3MapMaterials.DEFAULT) -> Dictionary:
	return brush(Q3MapGeo.box(PackedFloat64Array([mins.x, mins.y, mins.z]), PackedFloat64Array([maxs.x, maxs.y, maxs.z])), material)


static func plane_of(b: Dictionary, face: int) -> PackedFloat64Array:
	return b["planes"].slice(face * 4, face * 4 + 4)


## A face's texture mapping in texels, [Ux, Uy, Uz, Uoff, Vx, Vy, Vz, Voff] (u = U . p + Uoff).
## World-projected faces follow q3map's QuakeTextureVecs: the default axes for the normal, rotated,
## divided by the scale, plus the shift.
static func face_uv(face: Dictionary, plane: PackedFloat64Array) -> PackedFloat64Array:
	if face.has("uv"):
		return PackedFloat64Array(face["uv"])
	var t: Array = face.get("tex", DEFAULT_TEX)
	var ax: PackedFloat64Array = Q3MapGeo.texture_axes(plane)
	var vecs := [[ax[0], ax[1], ax[2]], [ax[3], ax[4], ax[5]]]
	var rot := fposmod(float(t[2]), 360.0)
	var s := 0.0
	var c := 1.0
	if rot == 90.0:
		s = 1.0; c = 0.0
	elif rot == 180.0:
		s = 0.0; c = -1.0
	elif rot == 270.0:
		s = -1.0; c = 0.0
	elif rot != 0.0:
		s = sin(deg_to_rad(rot)); c = cos(deg_to_rad(rot))
	var sv := 0 if vecs[0][0] != 0.0 else (1 if vecs[0][1] != 0.0 else 2)
	var tv := 0 if vecs[1][0] != 0.0 else (1 if vecs[1][1] != 0.0 else 2)
	for v in vecs:
		var ns: float = c * v[sv] - s * v[tv]
		var nt: float = s * v[sv] + c * v[tv]
		v[sv] = ns
		v[tv] = nt
	var out := PackedFloat64Array()
	for i in 2:
		var scale := float(t[3 + i])
		if scale == 0.0:
			scale = 1.0
		out.append_array([vecs[i][0] / scale, vecs[i][1] / scale, vecs[i][2] / scale, float(t[i])])
	return out


## Moves a brush by x' = M x + t (xform: M's rows, then t). Locked faces keep their texels on the
## surface; world-projected faces re-project, as Quake 3 does without texture lock.
static func transform_brush(b: Dictionary, xform: PackedFloat64Array) -> Dictionary:
	var out := b.duplicate(true)
	out["planes"] = Q3MapGeo.transform(b["planes"], xform)
	for i in out["faces"].size():
		var f: Dictionary = out["faces"][i]
		if f.get("lock", false):
			f["uv"] = Q3MapGeo.transform_uv(face_uv(b["faces"][i], plane_of(b, i)), xform)
			f.erase("tex")
	return out


static func translation(offset: Vector3) -> PackedFloat64Array:
	return PackedFloat64Array([1, 0, 0, 0, 1, 0, 0, 0, 1, offset.x, offset.y, offset.z])


## Rotation about a vertical axis through pivot, in degrees (exact for multiples of 90).
static func yaw_rotation(degrees: float, pivot: Vector3) -> PackedFloat64Array:
	var r := fposmod(degrees, 360.0)
	var c := 1.0 if r == 0.0 else (0.0 if r == 90.0 or r == 270.0 else (-1.0 if r == 180.0 else cos(deg_to_rad(r))))
	var s := 0.0 if r == 0.0 or r == 180.0 else (1.0 if r == 90.0 else (-1.0 if r == 270.0 else sin(deg_to_rad(r))))
	# x' = R (x - p) + p
	return PackedFloat64Array([c, -s, 0, s, c, 0, 0, 0, 1,
			pivot.x - (c * pivot.x - s * pivot.y), pivot.y - (s * pivot.x + c * pivot.y), 0])


## Mirror across the plane axis = pivot[axis] (axis 0..2).
static func mirror(axis: int, pivot: Vector3) -> PackedFloat64Array:
	var x := PackedFloat64Array([1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0])
	x[axis * 4] = -1
	x[9 + axis] = 2 * pivot[axis]
	return x


# ---------------------------------------------------------------- files

func to_data() -> Dictionary:
	var brushes := []
	var patches := []
	var entities := []
	var ids := objects.keys()
	ids.sort()
	for id in ids:
		var o: Dictionary = objects[id]
		match o["type"]:
			"brush":
				var faces := []
				for i in o["faces"].size():
					var f: Dictionary = o["faces"][i]
					var fd := {"plane": _nums(plane_of(o, i)), "material": f["material"]}
					if f.has("uv"):
						fd["uv"] = _nums(PackedFloat64Array(f["uv"]))
					else:
						fd["tex"] = _nums(PackedFloat64Array(f.get("tex", DEFAULT_TEX)))
					if f.get("lock", false):
						fd["lock"] = true
					if f.has("color"):
						fd["color"] = (f["color"] as Color).to_html(false)
					if f.has("flags"):
						fd["flags"] = Array(f["flags"])
					faces.append(fd)
				brushes.append(_common(o).merged({"faces": faces}))
			"patch":
				var pd := _common(o).merged({"width": o["width"], "height": o["height"],
						"ctrl": _nums(o["ctrl"]), "material": o["material"]})
				if o.has("color"):
					pd["color"] = (o["color"] as Color).to_html(false)
				patches.append(pd)
			"entity":
				entities.append({"id": id, "group": o.get("group", 0), "classname": o["classname"], "keys": o["keys"]})
	var gl := []
	for g in groups.values():
		gl.append(g)
	return {"format": FORMAT, "version": VERSION, "info": info, "worldspawn": worldspawn, "camera": camera,
			"next_id": next_id, "brushes": brushes, "patches": patches, "entities": entities, "groups": gl,
			"hidden": hidden.keys(), "locked": locked.keys()}


## Number lists in the file: plain JSON when every value is a whole number (axial brushes stay
## readable), else "f64:" + base64 of the doubles, since Godot's JSON parser can land one bit off.
static func _nums(a: PackedFloat64Array) -> Variant:
	for x in a:
		if x != roundf(x) or absf(x) > 1e15:
			return "f64:" + Marshalls.raw_to_base64(a.to_byte_array())
	return Array(a).map(func(x): return int(x))


static func _unnums(v: Variant) -> PackedFloat64Array:
	if v is String and v.begins_with("f64:"):
		return Marshalls.base64_to_raw(v.substr(4)).to_float64_array()
	return PackedFloat64Array(v) if v is Array else PackedFloat64Array()


static func _common(o: Dictionary) -> Dictionary:
	return {"id": o["id"], "entity": o.get("entity", 0), "group": o.get("group", 0)}


## Replaces the whole document. Returns "" or what was wrong with the data.
func from_data(d: Dictionary) -> String:
	if d.get("format", "") != FORMAT:
		return "not a map file"
	if int(d.get("version", 0)) > VERSION:
		return "made by a newer version (format %d)" % int(d["version"])
	objects = {}
	_windings = {}
	info = d.get("info", info)
	worldspawn = d.get("worldspawn", {})
	camera = d.get("camera", {})
	for b in d.get("brushes", []):
		var planes := PackedFloat64Array()
		var faces := []
		for fd in b["faces"]:
			planes.append_array(_unnums(fd["plane"]))
			var f := {"material": String(fd["material"])}
			if fd.has("uv"):
				f["uv"] = _unnums(fd["uv"])
			else:
				f["tex"] = Array(_unnums(fd["tex"])) if fd.has("tex") else DEFAULT_TEX.duplicate()
			if fd.get("lock", false):
				f["lock"] = true
			if fd.has("color"):
				f["color"] = Color.html(fd["color"])
			if fd.has("flags"):
				f["flags"] = Array(fd["flags"]).map(func(x): return int(x))
			faces.append(f)
		objects[int(b["id"])] = {"type": "brush", "id": int(b["id"]), "entity": int(b.get("entity", 0)),
				"group": int(b.get("group", 0)), "planes": planes, "faces": faces}
	for p in d.get("patches", []):
		var po := {"type": "patch", "id": int(p["id"]), "entity": int(p.get("entity", 0)), "group": int(p.get("group", 0)),
				"width": int(p["width"]), "height": int(p["height"]), "ctrl": _unnums(p["ctrl"]),
				"material": String(p["material"])}
		if p.has("color"):
			po["color"] = Color.html(p["color"])
		objects[po["id"]] = po
	for e in d.get("entities", []):
		var keys := {}
		for k in e.get("keys", {}):
			keys[String(k)] = String(e["keys"][k])
		objects[int(e["id"])] = {"type": "entity", "id": int(e["id"]), "group": int(e.get("group", 0)),
				"classname": String(e["classname"]), "keys": keys}
	groups = {}
	for g in d.get("groups", []):
		groups[int(g["id"])] = {"id": int(g["id"]), "name": String(g.get("name", "")), "parent": int(g.get("parent", 0))}
	hidden = {}
	for id in d.get("hidden", []):
		hidden[int(id)] = true
	locked = {}
	for id in d.get("locked", []):
		locked[int(id)] = true
	next_id = int(d.get("next_id", 1))
	for id in objects:
		next_id = maxi(next_id, id + 1)
	_undo.clear()
	_redo.clear()
	dirty = false
	changed.emit([])
	return ""


## Full-precision JSON, written to a temporary file first so a failed save never eats the map.
func save(path: String) -> Error:
	var text := JSON.stringify(to_data(), "\t", false, true)
	var tmp := path + ".tmp"
	var f := FileAccess.open(tmp, FileAccess.WRITE)
	if f == null:
		return FileAccess.get_open_error()
	f.store_string(text)
	f.close()
	var err := DirAccess.rename_absolute(tmp, path)
	if err == OK:
		dirty = false
	return err


## Returns "" or the reason the file couldn't be read.
func load_file(path: String) -> String:
	if not FileAccess.file_exists(path):
		return "no such file"
	var d: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if not d is Dictionary:
		return "not valid JSON"
	return from_data(d)
