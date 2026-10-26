class_name Q3MapEditor
extends Node3D
## The Map editor scene (MAP_EDITOR_PLAN.md), after OZMIUM's MapEditorController: it owns the
## document, the camera, the grid, the active tool and the selection, and every command runs through
## execute(), which the keymap, the toolbar and the tests all call.
## The document is in id space (Z up, game units); the scene is Godot's (Y up, meters).

static var maps_dir := "user://maps"            ## tests point these somewhere else
static var recent := "user://map_editor/recent.json"
const AUTOSAVE := "_autosave"
const RECENT_MAX := 10
const DRAG_PIXELS := 4.0
const Wire = preload("res://scenes/map_editor/q3_editor_wire.gd")
## the 2D views and the id axis each looks along
const VIEW_AXES := {"top": 2, "front": 1, "side": 0}

signal selection_changed
signal status_changed        ## file name, dirty flag, grid, tool, undo labels
signal prefabs_changed       ## a prefab saved or deleted (the Prefabs tab)
signal thumbnail_saved(name: String)   ## a saved map's thumbnail was written (thumb_path)
signal _import_done(result: Variant)   ## the import thread's result (null if it failed), from _process

var doc := Q3MapDoc.new()
var path := ""               ## the file this document was loaded from or saved to ("" = never saved)
var selection := {}          ## id -> true
var faces := {}              ## Vector2i(brush id, face) -> true: the face tool's selection
var verts := {}              ## Vector2i(object id, index) -> true: the vertex tool's corners and control points
var vertex_snap := false     ## Shift+V: dragged corners land on other brushes' corners
var clipboard: Array = []    ## object copies
var tool: Q3EditorTool
var tools := {}
var hover := {}              ## the last pick under the cursor
var mouse_pos := Vector2.ZERO
var last_height := 64.0      ## the create tool's last box height (units, along the surface normal)
var material := Q3MapMaterials.DEFAULT   ## what new brushes are made of (the material browser sets it, E3)
var test_input := false      ## tests drive the editor without a window in focus

var camera: Q3EditorCamera
var grid: Q3EditorGrid
var mesher: Q3MapMesher
var ents: Q3EditorEntities
var overlay: Q3EditorOverlay
var wire: MeshInstance3D       ## the 2D views' wireframe map
var view := "3d"             ## 3d, top, front, side
var _cycle := {}             ## the last click's hits under the cursor, to step through on the next one
var thumbs: Q3EditorThumbs
var compiler: Q3MapCompiler
var ui: Q3EditorUi
static var play_bots := 3    ## bots in a play-test (they need the .aas, so bspc runs too); 0 = none
static var play_from_camera := true   ## play-tests start where the 3D camera is (inside the map), facing its way
## the last play-test compile: {key (the .map text's md5 and the stages), r}; F5 on an unchanged map
## plays it again without running q3map (static: it outlives the trip into Gameplay and back)
static var _play_cache := {}
var last_compile := {}       ## the last compile's result (Q3MapCompiler.finished)
var _compile_job := {}       ## the compile under way: {models, warnings, play, map, doc, rev}
var _doc_rev := 0            ## counts document changes, so a play-test plays what was compiled
var lights_only := false     ## Shift+L: the preview sun and ambient off, the map's lights casting shadows
var _env: Environment
var _sun: DirectionalLight3D
var _isolated: Array = []    ## ids hidden by Isolate, shown again by the next Isolate
var importing := ""          ## the map or file being imported in the background ("" = none)
var _import_thread: Thread
var _import_cancelled := false
var filters := {}            ## view filter -> true: hidden from the view and from picking (VIEW menu)
var _shooting := false       ## a thumbnail is being rendered (take_thumbnail)
var _press := {}             ## the left button press being tracked: {pos, double, mods}
var _dragging := false


# ---------------------------------------------------------------- space

static func to_godot(v: Vector3) -> Vector3:
	return Vector3(v.y, v.z, v.x) / 32.0


static func to_id(g: Vector3) -> Vector3:
	return Vector3(g.z, g.x, g.y) * 32.0


static func f64(v: Vector3) -> PackedFloat64Array:
	return PackedFloat64Array([v.x, v.y, v.z])


# ---------------------------------------------------------------- setup

func _ready() -> void:
	# the keyboard to the game window (the menu's page held it; with no window focused, Windows dinged at every key)
	if DisplayServer.get_name() != "headless":
		get_window().grab_focus()
	var env := WorldEnvironment.new()
	_env = Environment.new()
	env.environment = _env
	_env.background_mode = Environment.BG_COLOR
	_env.background_color = Color(0.045, 0.04, 0.04)
	_env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	_env.ambient_light_color = Color(0.42, 0.4, 0.4)
	_env.ambient_light_energy = 1.0
	_env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	add_child(env)
	# editor preview light: a sun from above and behind the default view, soft shadows
	_sun = DirectionalLight3D.new()
	_sun.name = "PreviewSun"
	_sun.rotation_degrees = Vector3(-55, 35, 0)
	_sun.light_energy = 1.1
	_sun.shadow_enabled = true
	add_child(_sun)
	camera = Q3EditorCamera.new()
	add_child(camera)
	camera.current = true
	grid = Q3EditorGrid.new()
	grid.follow = camera
	grid.layers = Q3EditorCamera.ORTHO_LAYER   # the 2D views draw their own grid
	add_child(grid)
	mesher = Q3MapMesher.new()
	mesher.name = "Map"
	mesher.show_backs = true
	mesher.face_filter = face_filtered
	add_child(mesher)
	ents = Q3EditorEntities.new()
	ents.name = "Entities"
	add_child(ents)
	overlay = Q3EditorOverlay.new()
	add_child(overlay)
	wire = Wire.new()
	wire.name = "Wire"
	wire.editor = self
	add_child(wire)
	thumbs = Q3EditorThumbs.new()
	thumbs.name = "Thumbnails"
	add_child(thumbs)
	compiler = Q3MapCompiler.new()
	compiler.name = "Compiler"
	add_child(compiler)
	compiler.finished.connect(_on_compiled)
	tools = {"select": Q3SelectTool.new(self), "create": Q3CreateTool.new(self), "face": Q3FaceTool.new(self),
			"vertex": Q3VertexTool.new(self), "entity": Q3EntityTool.new(self), "prefab": Q3PrefabTool.new(self)}
	tool = tools["select"]
	ui = Q3EditorUi.new()
	ui.editor = self
	var layer := CanvasLayer.new()
	add_child(layer)
	layer.add_child(ui)
	doc.changed.connect(_on_doc_changed)
	mesher.bind(doc)
	ents.bind(doc)
	if Session.playtest.has("doc"):
		_back_from_playtest(Session.playtest)
	elif not _open_recent():
		new_map()
	UiSound.play_in()


func _open_recent() -> bool:
	var r: Variant = JSON.parse_string(FileAccess.get_file_as_string(recent)) if FileAccess.file_exists(recent) else null
	if r is Dictionary and FileAccess.file_exists(String(r.get("last", ""))):
		return open_map(String(r["last"])) == ""
	return false


## Recently saved or opened maps, newest first: paths that still exist, at most RECENT_MAX.
static func recent_maps() -> Array:
	var r: Variant = JSON.parse_string(FileAccess.get_file_as_string(recent)) if FileAccess.file_exists(recent) else null
	var out := []
	if r is Dictionary:
		for p in r.get("recent", [r.get("last", "")]):
			if String(p) != "" and FileAccess.file_exists(String(p)) and not out.has(String(p)):
				out.append(String(p))
	return out.slice(0, RECENT_MAX)


func _remember(p: String) -> void:
	DirAccess.make_dir_recursive_absolute(recent.get_base_dir())
	var list := recent_maps().filter(func(x): return x != p)
	if p.get_file() != AUTOSAVE + Q3MapDoc.EXTENSION:
		list.push_front(p)
	var f := FileAccess.open(recent, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify({"last": p, "recent": list.slice(0, RECENT_MAX)}))


## Deletes a saved map, its thumbnail and its place in the recent list (the open document stays).
func delete_map(p: String) -> Error:
	var err := DirAccess.remove_absolute(ProjectSettings.globalize_path(p))
	if err != OK:
		notify("Couldn't delete %s (error %d)" % [p.get_file(), err], true)
		return err
	var name := p.get_file().trim_suffix(Q3MapDoc.EXTENSION)
	if FileAccess.file_exists(thumb_path(name)):
		DirAccess.remove_absolute(ProjectSettings.globalize_path(thumb_path(name)))
	var list := recent_maps()
	DirAccess.make_dir_recursive_absolute(recent.get_base_dir())
	var f := FileAccess.open(recent, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify({"last": list[0] if list.size() else "", "recent": list}))
	if p == path:
		path = ""   # the map stays open, unsaved
		doc.dirty = true
		status_changed.emit()
	notify("Deleted " + name)
	return OK


# ---------------------------------------------------------------- files

static func map_path(name: String) -> String:
	return maps_dir.path_join(name + Q3MapDoc.EXTENSION)


## Saved maps, newest first: [{name, path, time}].
static func list_maps() -> Array:
	var out := []
	var d := DirAccess.open(maps_dir)
	if d == null:
		return out
	for f in d.get_files():
		if f.ends_with(Q3MapDoc.EXTENSION):
			var p := maps_dir.path_join(f)
			out.append({"name": f.trim_suffix(Q3MapDoc.EXTENSION), "path": p, "time": FileAccess.get_modified_time(p)})
	out.sort_custom(func(a, b): return a["time"] > b["time"])
	return out


## A fresh map: a 1024-unit floor at the origin, as a place to start building.
func new_map() -> void:
	var d := Q3MapDoc.new()
	d.info["name"] = "untitled"
	d.add(Q3MapDoc.box_brush(Vector3(-512, -512, -16), Vector3(512, 512, 0), "textures/base_floor/clangdark"))
	d.dirty = false
	_set_doc(d, "")
	camera.home()
	notify("New map")


func open_map(p: String) -> String:
	var d := Q3MapDoc.new()
	var err := d.load_file(p)
	if err != "":
		notify("Couldn't open %s: %s" % [p.get_file(), err], true)
		return err
	_set_doc(d, p)
	var c: Dictionary = d.camera
	if c.has("origin"):
		camera.set_pose(Vector3(c["origin"][0], c["origin"][1], c["origin"][2]), float(c.get("yaw", 0)), float(c.get("pitch", 0)))
	else:
		camera.home()
	_remember(p)
	notify("Opened " + d.info.get("name", p.get_file()))
	return ""


func save_map(name := "") -> Error:
	if name == "":
		name = doc.info.get("name", "untitled") if path != "" else ""
	if name == "":
		return ERR_INVALID_PARAMETER
	DirAccess.make_dir_recursive_absolute(maps_dir)
	doc.info["name"] = name
	doc.camera = {"origin": [camera.position.x, camera.position.y, camera.position.z], "yaw": camera.yaw, "pitch": camera.pitch}
	var p := map_path(name)
	var err := doc.save(p)
	if err == OK:
		path = p
		_remember(p)
		notify("Saved " + name)
		take_thumbnail(name)
	else:
		notify("Couldn't save %s (error %d)" % [name, err], true)
	status_changed.emit()
	return err


## A copy of the map as it stands, without touching its name, path or dirty flag (leaving, play-test).
func write_autosave() -> void:
	DirAccess.make_dir_recursive_absolute(maps_dir)
	var was := doc.dirty
	doc.save(map_path(AUTOSAVE))
	doc.dirty = was


func _set_doc(d: Q3MapDoc, p: String) -> void:
	if doc and doc.changed.is_connected(_on_doc_changed):
		doc.changed.disconnect(_on_doc_changed)
	doc = d
	path = p
	selection.clear()
	faces.clear()
	verts.clear()
	_isolated.clear()
	doc.changed.connect(_on_doc_changed)
	mesher.exclude = {}
	mesher.bind(doc)
	ents.exclude = {}
	ents.bind(doc)
	Diagnostics.set_context("editor_map", "%s (%d objects)" % [p.get_file() if p != "" else "new map", doc.objects.size()])
	set_tool("select")
	_refresh_selection()
	status_changed.emit()


## Imports a map as a new, unsaved document (MAP_EDITOR_PLAN.md E6): kind "bsp" takes one of the
## maps in the player's paks by name, "map" a Radiant .map file by path. It runs on a thread (a big
## map takes seconds) behind a dialog whose Cancel throws the result away; await it for the outcome
## ("" or what went wrong).
func import_map(kind: String, source: String) -> String:
	if importing != "" or compiler.busy:
		notify("Wait for the %s to finish" % ("import" if importing != "" else "compile"), true)
		return "busy"
	Q3MapMaterials.all()   # loaded here, so the import thread only reads it
	importing = source.get_file()
	_import_cancelled = false
	ui.show_progress("IMPORTING", "Reading %s ..." % importing, func(): _import_cancelled = true)
	_import_thread = Thread.new()
	_import_thread.start(_import_work.bind(kind, source))
	# _process hands over the result once the thread is done; if the editor goes first, this never
	# resumes (and _exit_tree has joined the thread)
	var got: Variant = await _import_done
	var r: Dictionary = got if got is Dictionary else {"doc": null, "report": {"errors": ["the import stopped with an error (see the log)"]}}
	var name := importing
	importing = ""
	ui.close_progress()
	if _import_cancelled:
		notify("Import cancelled")
		return "cancelled"
	var rep: Dictionary = r.get("report", {})
	var d: Q3MapDoc = r.get("doc")
	if d == null:
		var err := "; ".join(PackedStringArray(rep.get("errors", ["nothing could be read"])))
		notify("Couldn't import %s: %s" % [name, err], true)
		return err
	_set_doc(d, "")
	_view_from_spawn()
	var dropped := int(rep.get("dropped", 0))
	var problems: Array = rep.get("errors", [])
	for p in problems:
		push_warning("map editor import %s: %s" % [name, p])
	notify("Imported %s: %d brushes, %d curves, %d entities%s%s" % [name, rep.get("brushes", 0), rep.get("patches", 0),
			rep.get("entities", 0), (", %d brushes that made no solid left out" % dropped) if dropped else "",
			(", %d problems (see the log)" % problems.size()) if problems.size() else ""], dropped > 0 or problems.size() > 0)
	return ""


static func _import_work(kind: String, source: String) -> Dictionary:
	if kind == "bspfile":
		# a .bsp on disk, or "<pk3>::maps/<name>.bsp" inside a package
		var inner := source.get_slice("::", 1) if source.contains("::") else source
		var bytes := PackedByteArray()
		if source.contains("::"):
			var zr := ZIPReader.new()
			if zr.open(source.get_slice("::", 0)) == OK:
				bytes = zr.read_file(inner)
				zr.close()
		else:
			bytes = FileAccess.get_file_as_bytes(source)
		if bytes.is_empty():
			return {"doc": null, "report": {"errors": ["no map could be read from it"]}}
		return Q3BspImport.import_bsp(bytes, inner.get_file().get_basename(), false)
	if kind == "bsp":
		var bytes := Q3BspImport.read_from_paks(source)
		if bytes.is_empty():
			return {"doc": null, "report": {"errors": ["it isn't in your paks"]}}
		return Q3BspImport.import_bsp(bytes, source, false)
	if not FileAccess.file_exists(source):
		return {"doc": null, "report": {"errors": ["the file isn't there"]}}
	var name := Q3EditorUi.clean_name(source.get_file().get_basename())
	return Q3MapImport.parse(FileAccess.get_file_as_string(source), name if name != "" else "imported")


## The camera where the map's intermission view is, else at its first deathmatch spawn's eyes,
## else framing the whole map.
func _view_from_spawn() -> void:
	var spot := {}
	for id in doc.ids_of("entity"):
		var e := doc.get_obj(id)
		if e["classname"] == "info_player_intermission":
			spot = e
			break
		if spot.is_empty() and e["classname"] == "info_player_deathmatch":
			spot = e
	if spot.is_empty():
		var box := bounds_of(doc.objects.keys())
		camera.home()
		camera.focus(to_godot(box.get_center()), box.size.length() * 0.5 / 32.0)
		return
	var keys: Dictionary = spot["keys"]
	var at := Q3MapEntities.vec(String(keys.get("origin", "0 0 0")))
	if spot["classname"] == "info_player_deathmatch":
		at.z += 26.0   # DEFAULT_VIEWHEIGHT over the player's origin
	var ang := Q3MapEntities.vec(String(keys["angles"])) if keys.has("angles") else Vector3(0, float(keys.get("angle", 0)), 0)
	if spot["classname"] == "info_player_intermission" and keys.has("target"):
		# FindIntermissionPoint: it faces its target
		for id in doc.ids_of("entity"):
			var t := doc.get_obj(id)
			if String(t["keys"].get("targetname", "")) == String(keys["target"]):
				var dir := Q3MapEntities.vec(String(t["keys"].get("origin", "0 0 0"))) - at
				if dir.length() > 0.001:
					ang = Vector3(-rad_to_deg(atan2(dir.z, Vector2(dir.x, dir.y).length())), rad_to_deg(atan2(dir.y, dir.x)), 0)
				break
	# id's yaw 0 looks along +X, Godot's camera along -Z; id's pitch is positive looking down
	camera.set_pose(to_godot(at), ang.y + 180.0, -ang.x)


## Writes the map as a Radiant .map: "brushdef" for GtkRadiant and NetRadiant, "valve220" for
## TrenchBroom (Q3MapExport).
func export_map_file(p: String, format: String) -> Error:
	var ex := Q3MapExport.to_map(doc, format)
	var f := FileAccess.open(p, FileAccess.WRITE)
	if f == null:
		notify("Couldn't write %s (error %d)" % [p.get_file(), FileAccess.get_open_error()], true)
		return FileAccess.get_open_error()
	f.store_string(ex["text"])
	f.close()
	var warnings: Array = ex["warnings"]
	for w in warnings:
		push_warning("map editor export: " + String(w))
	notify("Exported %s%s" % [p.get_file(), (", %d warnings (see the log)" % warnings.size()) if warnings.size() else ""], warnings.size() > 0)
	return OK


func _process(_delta: float) -> void:
	if doc.batch_open():
		# a batch is only open inside one call; one left open (a script error part way through) would
		# keep every later change from reaching the view
		push_warning("map editor: an edit batch was left open; closed")
		doc.close_batches()
	if _import_thread and not _import_thread.is_alive():
		var r: Variant = _import_thread.wait_to_finish()   # null when the worker hit a script error
		_import_thread = null
		_import_done.emit(r)


func _exit_tree() -> void:
	if _import_thread:
		Diagnostics.expect_stall(true)
		_import_thread.wait_to_finish()
		_import_thread = null
		Diagnostics.expect_stall(false)


func leave() -> void:
	if compiler.busy or importing != "":
		notify("Wait for the %s to finish" % ("compile" if compiler.busy else "import"), true)
		return
	while _shooting:
		await get_tree().process_frame   # a thumbnail is two frames from done
	write_autosave()
	camera.stop_looking()
	UiSound.out()
	Session.to_main_menu()


# ---------------------------------------------------------------- view filters

## [id, label]: what the VIEW menu can hide, as Radiant's filters do. Hidden things can't be picked or
## box selected either; they stay in the map, and play-tests and compiles keep them.
const FILTERS := [["caulk", "Caulk"], ["tools", "Clip, hint, triggers and other tool brushes"], ["sky", "Sky"],
		["fog", "Fog"], ["curves", "Curves"], ["entities", "Entities"]]


## The filter a face's material falls under ("" for none).
static func material_filter(material: String) -> String:
	if material == "textures/common/caulk":
		return "caulk"
	match Q3MapMaterials.kind(material):
		"hidden": return "tools"
		"sky": return "sky"
		"fog": return "fog"
	return ""


## Shift+I is on: everything but what was selected is hidden.
func is_isolated() -> bool:
	return _isolated.size() > 0


## Counts document changes (anything that caches what it drew from the document can compare it).
func doc_revision() -> int:
	return _doc_rev


func face_filtered(material: String) -> bool:
	return filters.size() > 0 and filters.has(material_filter(material))


## Whether the view filters hide this object entirely (a brush: every face).
func filtered(id: int) -> bool:
	if filters.is_empty():
		return false
	var o := doc.get_obj(id)
	match o.get("type", ""):
		"entity":
			return filters.has("entities")
		"patch":
			return filters.has("curves") or face_filtered(o["material"])
		"brush":
			for f in o["faces"]:
				if not face_filtered(f["material"]):
					return false
			return true
	return false


func set_filter(id: String, on: bool) -> void:
	if filters.has(id) == on:
		return
	if on:
		filters[id] = true
	else:
		filters.erase(id)
	mesher.hide_patches = filters.has("curves")
	mesher.redraw_all()
	ents.visible = not filters.has("entities")
	# what the filter hides drops out of the selection
	select(selected_ids().filter(func(i): return not filtered(i)))
	notify("%s %s" % [_filter_label(id), "hidden" if on else "shown"])
	status_changed.emit()


static func _filter_label(id: String) -> String:
	for f in FILTERS:
		if f[0] == id:
			return f[1]
	return id


# ---------------------------------------------------------------- picking

## The mouse ray in id space: [origin, direction].
func ray(screen: Vector2) -> Array:
	var o := camera.project_ray_origin(screen)
	var d := camera.project_ray_normal(screen)
	return [to_id(o), Vector3(d.z, d.x, d.y)]


## The nearest visible brush or patch (and with_entities, point entity) under the cursor: {id, kind,
## face, t, point, normal, locked}, or {}. face is the brush face (-1 on a patch or entity).
func pick(screen: Vector2, with_entities := false, cycle := false) -> Dictionary:
	if not cycle:
		return _pick_all(screen, with_entities, true)[0]
	# a second click on the same spot takes the next object along the ray (a 2D view's floor under
	# its roof)
	var hits := _pick_all(screen, with_entities, false)
	if hits.is_empty():
		_cycle = {}
		return {}
	var again: bool = _cycle.size() and _cycle["screen"].distance_to(screen) < 3.0 and _cycle["rev"] == _doc_rev \
			and _cycle["count"] == hits.size() and selection.has(hits[_cycle["at"]]["id"])
	var at: int = (_cycle["at"] + 1) % hits.size() if again else 0
	_cycle = {"screen": screen, "rev": _doc_rev, "count": hits.size(), "at": at}
	return hits[at]


## The pickable objects under the cursor, nearest first ({} alone when none and nearest_only).
func _pick_all(screen: Vector2, with_entities: bool, nearest_only: bool) -> Array:
	var r := ray(screen)
	return _hits(r[0], r[1], with_entities, nearest_only)


## What a ray from `from` along `dir` (id space) passes through, as _pick_all.
func _hits(from: Vector3, dir: Vector3, with_entities: bool, nearest_only: bool) -> Array:
	var r := [from, dir]
	var o := f64(r[0])
	var d := f64(r[1])
	var hits := []
	var best := {}
	var best_t := INF
	for id in doc.objects:
		if doc.hidden.has(id) or (filters.size() and filtered(id)):
			continue
		var b: Dictionary = doc.objects[id]
		match b["type"]:
			"entity":
				if not with_entities:
					continue
				var box := entity_box(b)
				if box.size == Vector3.ZERO:
					continue
				var hit := ray_box(r[0], r[1], box)
				if hit.size() and (hit[0] < best_t or not nearest_only):
					best_t = minf(best_t, hit[0]) if not nearest_only else hit[0]
					best = {"id": id, "kind": "entity", "face": -1, "t": hit[0], "point": r[0] + r[1] * hit[0],
							"normal": hit[1], "locked": doc.locked.has(id)}
					hits.append(best)
			"brush":
				var hit: PackedFloat64Array = Q3MapGeo.ray(b["planes"], o, d)
				# entering through a face the view filters hide (caulk on a trim piece): nothing to see there
				if hit.size() == 2 and filters.size() and face_filtered(b["faces"][int(hit[1])]["material"]):
					continue
				if hit.size() == 2 and (hit[0] < best_t or not nearest_only):
					best_t = minf(best_t, hit[0]) if not nearest_only else hit[0]
					var face := int(hit[1])
					best = {"id": id, "kind": "brush", "face": face, "t": hit[0], "point": r[0] + r[1] * hit[0],
							"normal": Vector3(b["planes"][face * 4], b["planes"][face * 4 + 1], b["planes"][face * 4 + 2]),
							"locked": doc.locked.has(id)}
					hits.append(best)
			"patch":
				var hit := _ray_patch(r[0], r[1], doc.patch_tess(id))
				if hit.size() and (hit[0] < best_t or not nearest_only):
					best_t = minf(best_t, hit[0]) if not nearest_only else hit[0]
					best = {"id": id, "kind": "patch", "face": -1, "t": hit[0], "point": r[0] + r[1] * hit[0],
							"normal": hit[1], "locked": doc.locked.has(id)}
					hits.append(best)
	if nearest_only:
		return [best]
	hits.sort_custom(func(a, b): return a["t"] < b["t"])
	return hits


## [t, normal] where the ray first crosses a patch's triangles (either side), or [].
static func _ray_patch(o: Vector3, d: Vector3, tess: Dictionary) -> Array:
	var pts: PackedFloat64Array = tess.get("points", PackedFloat64Array())
	var w: int = tess.get("width", 0)
	var h: int = tess.get("height", 0)
	var best := []
	for y in h - 1:
		for x in w - 1:
			var a := y * w + x
			for tri in [[a, a + 1, a + w + 1], [a, a + w + 1, a + w]]:
				var p0 := Vector3(pts[tri[0] * 3], pts[tri[0] * 3 + 1], pts[tri[0] * 3 + 2])
				var p1 := Vector3(pts[tri[1] * 3], pts[tri[1] * 3 + 1], pts[tri[1] * 3 + 2])
				var p2 := Vector3(pts[tri[2] * 3], pts[tri[2] * 3 + 1], pts[tri[2] * 3 + 2])
				var t := _ray_tri(o, d, p0, p1, p2)
				if t > 0 and (best.is_empty() or t < best[0]):
					best = [t, (p1 - p0).cross(p2 - p0).normalized()]
	return best


## [t, normal] where the ray enters the box (slabs), or [] when it misses or starts inside.
static func ray_box(o: Vector3, d: Vector3, box: AABB) -> Array:
	var t0 := -INF
	var t1 := INF
	var n := Vector3.ZERO
	for i in 3:
		if absf(d[i]) < 1e-12:
			if o[i] < box.position[i] or o[i] > box.end[i]:
				return []
			continue
		var a := (box.position[i] - o[i]) / d[i]
		var b := (box.end[i] - o[i]) / d[i]
		var near := minf(a, b)
		if near > t0:
			t0 = near
			n = Vector3.ZERO
			n[i] = -signf(d[i])
		t1 = minf(t1, maxf(a, b))
	if t0 > t1 or t0 < 0.0:
		return []
	return [t0, n]


## Möller-Trumbore: distance along the ray to the triangle, or -1.
static func _ray_tri(o: Vector3, d: Vector3, a: Vector3, b: Vector3, c: Vector3) -> float:
	var e1 := b - a
	var e2 := c - a
	var p := d.cross(e2)
	var det := e1.dot(p)
	if absf(det) < 1e-9:
		return -1.0
	var inv := 1.0 / det
	var s := o - a
	var u := s.dot(p) * inv
	if u < 0.0 or u > 1.0:
		return -1.0
	var q := s.cross(e1)
	var v := d.dot(q) * inv
	if v < 0.0 or u + v > 1.0:
		return -1.0
	return e2.dot(q) * inv


## Where the mouse ray meets a plane (id space), or null when it runs parallel or behind.
func plane_point(screen: Vector2, point: Vector3, normal: Vector3) -> Variant:
	var r := ray(screen)
	var denom: float = r[1].dot(normal)
	if absf(denom) < 1e-6:
		return null
	var t: float = (point - r[0]).dot(normal) / denom
	return r[0] + r[1] * t if t > 0 else null


# ---------------------------------------------------------------- 2D views

## Into a 2D view (Radiant's: "top" looks down Z, "front" along Y, "side" along X) or back to "3d".
## Asking for the view already shown goes back to 3D. The map draws as edges there (wire); the 2D
## views center on the selection, or on what the 3D view was looking at.
func set_view(mode: String) -> void:
	if mode == view and mode != "3d":
		mode = "3d"
	if mode == view:
		return
	var center := Vector3.ZERO
	var mid := get_viewport().get_visible_rect().size * 0.5
	if selection.size():
		center = to_godot(selection_bounds().get_center())
	elif faces.size():
		var box := AABB()
		for k: Vector2i in faces:
			box = bounds_of([k.x]) if box.size == Vector3.ZERO else box.merge(bounds_of([k.x]))
		center = to_godot(box.get_center())
	elif camera.ortho != "":
		center = camera.position - camera.global_transform.basis.z * Q3EditorCamera.ORTHO_BACK
	else:
		# what the 3D view looks at: the surface in the middle of the screen, or a little way ahead
		var hit := pick(mid)
		center = to_godot(hit["point"]) if hit.size() else camera.position - camera.global_transform.basis.z * 12.0
	tool.cancel()
	view = mode
	camera.set_ortho("" if mode == "3d" else mode, center)
	var flat := mode != "3d"
	mesher.visible = not flat
	wire.visible = flat
	_cycle = {}
	notify({"3d": "3D view", "top": "Top view: X right, Y up", "front": "Front view: X right, Z up",
			"side": "Side view: Y right, Z up"}[mode] + ("" if mode == "3d" else " · right-drag pans, the wheel zooms · %s again: 3D" % Q3EditorKeymap.key_for("view." + mode)))
	status_changed.emit()


## The id axis a 2D view looks along, or -1 in 3D.
func view_axis() -> int:
	return VIEW_AXES.get(view, -1)


## Where a 2D view puts new things along its axis: the selection's low side, else 0.
func work_depth() -> float:
	var a := view_axis()
	if a < 0:
		return 0.0
	return selection_bounds().position[a] if selection.size() else 0.0


func screen_of(id_point: Vector3) -> Vector2:
	return camera.unproject_position(to_godot(id_point))


## The surface under the cursor, or the ground (z = 0) when nothing is there.
func cursor_point(screen: Vector2) -> Variant:
	if view_axis() >= 0:
		var n := Vector3.ZERO
		n[view_axis()] = 1.0
		return plane_point(screen, n * work_depth(), n)
	var p := pick(screen)
	if p.size():
		return p["point"]
	return plane_point(screen, Vector3.ZERO, Vector3(0, 0, 1))


# ---------------------------------------------------------------- selection

func selected_ids() -> Array:
	var out := selection.keys()
	out.sort()
	return out


## All objects in id's group (just id when it has none).
func group_of(id: int) -> Array:
	var g := int(doc.get_obj(id).get("group", 0))
	if g == 0:
		return [id]
	var out := []
	for o in doc.objects:
		if int(doc.objects[o].get("group", 0)) == g:
			out.append(o)
	return out


## mode: "set", "add", "toggle" or "remove".
func select(ids: Array, mode := "set") -> void:
	if mode == "set":
		selection.clear()
	for id in ids:
		if not doc.objects.has(id) or doc.locked.has(id) or doc.hidden.has(id):
			continue
		match mode:
			"toggle":
				if selection.has(id):
					selection.erase(id)
				else:
					selection[id] = true
			"remove":
				selection.erase(id)
			_:
				selection[id] = true
	_refresh_selection()


func clear_selection() -> void:
	if selection.size():
		select([], "set")


func _refresh_selection() -> void:
	var old := mesher.exclude.keys()
	mesher.exclude = selection.duplicate()
	mesher.touch(old + selection.keys())
	ents.exclude = selection.duplicate()
	ents.touch(old + selection.keys())
	overlay.show_selection(doc, selected_ids())
	overlay.show_faces(doc, faces.keys())
	overlay.clear_hover()
	selection_changed.emit()
	status_changed.emit()


func _on_doc_changed(ids: Array) -> void:
	_doc_rev += 1
	var gone := false
	for id in selection.keys():
		if not doc.objects.has(id):
			selection.erase(id)
			gone = true
	for k: Vector2i in faces.keys():
		var b := doc.get_obj(k.x)
		if b.is_empty() or k.y >= b.get("faces", []).size():
			faces.erase(k)
			gone = true
	for k: Vector2i in verts.keys():
		if not doc.objects.has(k.x):
			verts.erase(k)
	if gone or ids.is_empty() or ids.any(func(i): return selection.has(i) or faces.keys().any(func(k): return k.x == i)):
		_refresh_selection()
	else:
		status_changed.emit()


## mode: "set", "add", "toggle". keys: Vector2i(brush id, face).
func select_faces(keys: Array, mode := "set") -> void:
	if mode == "set":
		faces.clear()
	for k in keys:
		if doc.locked.has(k.x) or doc.hidden.has(k.x):
			continue
		if mode == "toggle" and faces.has(k):
			faces.erase(k)
		else:
			faces[k] = true
	_refresh_selection()


## A brush's corners or a patch's control points, id space, 3 numbers each.
func object_points(id: int) -> PackedFloat64Array:
	var o := doc.get_obj(id)
	match o.get("type", ""):
		"brush":
			return Q3MapGeo.brush_vertices(o["planes"])
		"patch":
			var c: PackedFloat64Array = o["ctrl"]
			var out := PackedFloat64Array()
			for i in c.size() / 5:
				out.append_array([c[i * 5], c[i * 5 + 1], c[i * 5 + 2]])
			return out
		"entity":
			if entity_box(o).size != Vector3.ZERO:
				return f64(Q3MapEntities.origin(o))
	return PackedFloat64Array()


## The brush rebuilt as the convex hull of pts, each new face taking the material and mapping of
## the old face pointing most nearly the same way. {} when the points don't enclose a volume.
static func rebuild_from_points(b: Dictionary, pts: PackedFloat64Array) -> Dictionary:
	var planes: PackedFloat64Array = Q3MapGeo.hull(pts)
	if planes.is_empty():
		return {}
	var out := b.duplicate(true)
	out["planes"] = planes
	var faces_out := []
	for i in planes.size() / 4:
		var n := Vector3(planes[i * 4], planes[i * 4 + 1], planes[i * 4 + 2])
		var best := 0
		var best_dot := -2.0
		for j in b["faces"].size():
			var m := Vector3(b["planes"][j * 4], b["planes"][j * 4 + 1], b["planes"][j * 4 + 2])
			if n.dot(m) > best_dot:
				best_dot = n.dot(m)
				best = j
		faces_out.append(b["faces"][best].duplicate(true))
	out["faces"] = faces_out
	return out


## Bounds of the selection in id space (AABB with size 0 when empty).
func selection_bounds() -> AABB:
	return bounds_of(selected_ids())


func bounds_of(ids: Array) -> AABB:
	return bounds_in(doc, ids)


static func bounds_in(d: Q3MapDoc, ids: Array) -> AABB:
	var box := AABB()
	var first := true
	for id in ids:
		var o := d.get_obj(id)
		var bb := PackedFloat64Array()
		match o.get("type", ""):
			"brush":
				bb = Q3MapGeo.brush_bounds(o["planes"])
			"patch":
				var c: PackedFloat64Array = o["ctrl"]
				for i in c.size() / 5:
					var p := Vector3(c[i * 5], c[i * 5 + 1], c[i * 5 + 2])
					bb = PackedFloat64Array([p.x, p.y, p.z, p.x, p.y, p.z]) if bb.is_empty() else PackedFloat64Array([
							minf(bb[0], p.x), minf(bb[1], p.y), minf(bb[2], p.z), maxf(bb[3], p.x), maxf(bb[4], p.y), maxf(bb[5], p.z)])
			"entity":
				var eb := entity_box(o)
				if eb.size != Vector3.ZERO:
					bb = PackedFloat64Array([eb.position.x, eb.position.y, eb.position.z, eb.end.x, eb.end.y, eb.end.z])
		if bb.size() < 6:
			continue
		var a := AABB(Vector3(bb[0], bb[1], bb[2]), Vector3(bb[3] - bb[0], bb[4] - bb[1], bb[5] - bb[2]))
		box = a if first else box.merge(a)
		first = false
	return box


static func entity_origin(e: Dictionary) -> Vector3:
	return Q3MapEntities.origin(e)


## A point entity's box in the map (id units); size zero for a brush entity, whose brushes are it.
static func entity_box(e: Dictionary) -> AABB:
	var cn := String(e.get("classname", ""))
	if Q3MapEntities.is_brush_class(cn) and not e.get("keys", {}).has("origin"):
		return AABB()
	var b := Q3MapEntities.bounds(cn)
	return AABB(b.position + Q3MapEntities.origin(e), b.size)


# ---------------------------------------------------------------- object edits

## One object moved by x' = M x + t: brush planes (texture lock honored), patch control points,
## entity origins and facings (an "angle" turns and mirrors with M).
static func transform_object(o: Dictionary, xform: PackedFloat64Array, _yaw_degrees := 0.0) -> Dictionary:
	match o.get("type", ""):
		"brush":
			return Q3MapDoc.transform_brush(o, xform)
		"patch":
			var out := o.duplicate(true)
			var c: PackedFloat64Array = out["ctrl"]
			for i in c.size() / 5:
				var p := _apply(xform, Vector3(c[i * 5], c[i * 5 + 1], c[i * 5 + 2]))
				c[i * 5] = p.x
				c[i * 5 + 1] = p.y
				c[i * 5 + 2] = p.z
			out["ctrl"] = c
			return out
		"entity":
			var out := o.duplicate(true)
			var k: Dictionary = out["keys"]
			if k.has("origin"):
				k["origin"] = Q3MapEntities.vec_key(_apply(xform, entity_origin(o)))
			# a turn or mirror (not a plain move) turns the facing; G_SetMovedir's -1 (up) and -2 (down)
			# aren't headings and stay as they are
			var linear_identity := xform[0] == 1.0 and xform[1] == 0.0 and xform[3] == 0.0 and xform[4] == 1.0
			if (k.has("angle") or k.has("angles")) and not linear_identity and String(k.get("angle", "")) not in ["-1", "-2"]:
				var a := deg_to_rad(Q3MapEntities.angle(o))
				var f := Vector3(cos(a), sin(a), 0)
				var g := Vector3(xform[0] * f.x + xform[1] * f.y, xform[3] * f.x + xform[4] * f.y, 0)
				if g.length() > 1e-6:
					var yaw := fposmod(roundf(rad_to_deg(atan2(g.y, g.x)) * 1000.0) / 1000.0, 360.0)
					if k.has("angles"):
						var v := Q3MapEntities.vec(String(k["angles"]))
						k["angles"] = Q3MapEntities.vec_key(Vector3(v.x, yaw, v.z))
					else:
						k["angle"] = _num(yaw)
			return out
	return o


static func _apply(x: PackedFloat64Array, p: Vector3) -> Vector3:
	return Vector3(x[0] * p.x + x[1] * p.y + x[2] * p.z + x[9], x[3] * p.x + x[4] * p.y + x[5] * p.z + x[10],
			x[6] * p.x + x[7] * p.y + x[8] * p.z + x[11])


static func _num(v: float) -> String:
	return str(int(v)) if v == roundf(v) else String.num(v, 3)


## Applies a transform to the whole selection as one undo step.
func transform_selection(label: String, xform: PackedFloat64Array, yaw_degrees := 0.0) -> void:
	if selection.is_empty():
		return
	doc.checkpoint(label)
	doc.begin_batch()
	for id in selected_ids():
		doc.put(transform_object(doc.get_obj(id), xform, yaw_degrees))
	doc.end_batch()


func move_selection(delta: Vector3, label := "Move") -> void:
	if delta != Vector3.ZERO:
		transform_selection(label, Q3MapDoc.translation(delta))


## Copies of objects with fresh ids, moved so their bounds' min corner lands at `at`. Links inside
## the copies stay inside them: brushes follow their copied entity (or join the world when it wasn't
## copied), and a targetname the map already has gets a new name, with the copies' targets renamed
## to match. group_all puts every copy in one new group (a placed prefab moves as one).
func place_copies(objs: Array, at: Vector3, label: String, group_all := false) -> Array:
	if objs.is_empty():
		return []
	var tmp := Q3MapDoc.new()
	for o in objs:
		tmp.objects[o["id"]] = o
	var box := bounds_in(tmp, tmp.objects.keys())
	var xform := Q3MapDoc.translation(at - box.position)
	doc.checkpoint(label)
	var id_map := {}
	for o in objs:
		id_map[int(o["id"])] = doc.new_id()
	var names := Q3MapEntities.targetnames(doc)
	var renamed := {}
	var taken := {}       # the copies' own names count as taken, so a renamed link can't land on one
	for o in objs:
		if o["type"] == "entity" and o["keys"].has("targetname"):
			taken[String(o["keys"]["targetname"])] = true
	for o in objs:
		var tn := String(o.get("keys", {}).get("targetname", "")) if o["type"] == "entity" else ""
		if tn != "" and names.has(tn) and not renamed.has(tn):
			var prefix := tn.rstrip("0123456789")
			renamed[tn] = Q3MapEntities.unique_name(doc, prefix if prefix != "" else "t", taken)
			taken[renamed[tn]] = true
	var group_map := {}
	var all_group := _new_group() if group_all and objs.size() > 1 else 0
	var ids := []
	doc.begin_batch()
	for o in objs:
		var c := transform_object(o.duplicate(true), xform)
		c["id"] = id_map[int(o["id"])]
		if c["type"] != "entity":
			c["entity"] = id_map.get(int(c.get("entity", 0)), 0)
		else:
			for key in ["targetname", "target"]:
				if c["keys"].has(key) and renamed.has(c["keys"][key]):
					c["keys"][key] = renamed[c["keys"][key]]
		var g := int(c.get("group", 0))
		if all_group != 0:
			c["group"] = all_group
		elif g != 0:
			if not group_map.has(g):
				group_map[g] = _new_group()
				if doc.groups.has(g):   # a pasted group is a new one, under the same name
					doc.groups[group_map[g]]["name"] = doc.groups[g]["name"]
			c["group"] = group_map[g]
		ids.append(doc.add(c))
	doc.end_batch()
	select(ids)
	return ids


func _new_group() -> int:
	var gid := doc.new_id()
	doc.groups[gid] = {"id": gid, "name": "Group %d" % gid, "parent": 0}
	return gid


# ---------------------------------------------------------------- tools

func set_tool(name: String) -> void:
	if not tools.has(name):
		return
	if tool:
		tool.cancel()
	var leaving_faces: bool = tool == tools.get("face") and name != "face"
	tool = tools[name]
	tool.activate()
	if leaving_faces and faces.size():
		select_faces([])   # picked faces belong to the face tool
	status_changed.emit()


func notify(text: String, error := false) -> void:
	if ui:
		ui.toast(text, error)
	if error:
		UiSound.buzz()


# ---------------------------------------------------------------- input

func _input(ev: InputEvent) -> void:
	# looking around comes first, so the UI never sees the captured mouse's motion
	if camera.looking and ev is InputEventMouseMotion:
		camera.look(ev.relative)
		get_viewport().set_input_as_handled()


func _unhandled_input(ev: InputEvent) -> void:
	if ui and ui.modal_open():
		return
	if ev is InputEventMouseButton:
		_mouse_button(ev)
	elif ev is InputEventMouseMotion:
		mouse_pos = ev.position
		if camera.looking:
			return
		if _press.size():
			var start: Vector2 = _press["pos"]
			if not _dragging and start.distance_to(ev.position) >= DRAG_PIXELS:
				_dragging = true
				tool.drag_start(start, _press["mods"])
			if _dragging:
				tool.drag(ev.position, Q3EditorKeymap.mods_of(ev))
		else:
			tool.hover(ev.position, Q3EditorKeymap.mods_of(ev))
		ui.cursor_moved()
	elif ev is InputEventKey and ev.pressed and (not ev.echo or _repeats(ev)):
		if camera.looking and Q3EditorKeymap.mods_of(ev) & ~Q3EditorKeymap.SHIFT == 0 and ev.physical_keycode in [KEY_W, KEY_A, KEY_S, KEY_D, KEY_Q, KEY_E]:
			return   # flying
		if tool.key(ev):
			get_viewport().set_input_as_handled()
			return
		for c in Q3EditorKeymap.commands_for(ev):
			execute(c)
			get_viewport().set_input_as_handled()
			return


## Arrow nudges and Page Up/Down repeat while held; nothing else does.
func _repeats(ev: InputEventKey) -> bool:
	return ev.keycode in [KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_PAGEUP, KEY_PAGEDOWN]


func _mouse_button(ev: InputEventMouseButton) -> void:
	mouse_pos = ev.position
	match ev.button_index:
		MOUSE_BUTTON_RIGHT:
			if ev.pressed:
				camera.start_looking()
			else:
				camera.stop_looking()
		MOUSE_BUTTON_LEFT:
			if ev.pressed:
				get_viewport().gui_release_focus()   # a click in the view takes the keys back from a panel field
				_press = {"pos": ev.position, "double": ev.double_click, "mods": Q3EditorKeymap.mods_of(ev)}
				_dragging = false
				tool.press(ev.position, _press["mods"], ev.double_click)
			elif _press.size():
				if _dragging:
					tool.drag_end(ev.position, Q3EditorKeymap.mods_of(ev))
				else:
					tool.click(ev.position, _press["mods"], _press["double"])
				_press = {}
				_dragging = false
		MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN:
			if not ev.pressed:
				return
			# a wheel turn a panel didn't use (a text field, a list already at its end) falls through
			# to here: it was meant for the panel, not the camera
			if get_viewport().gui_get_hovered_control() != null:
				return
			var dir := 1.0 if ev.button_index == MOUSE_BUTTON_WHEEL_UP else -1.0
			if ev.ctrl_pressed:
				execute("grid.bigger" if dir > 0 else "grid.smaller")
			elif camera.ortho != "":
				camera.zoom_at(ev.position, dir * maxf(ev.factor, 1.0))
			else:
				camera.dolly(dir * maxf(ev.factor, 1.0), ev.shift_pressed)


# ---------------------------------------------------------------- commands

## Runs one editor command (keymap ids). Returns false for an unknown command. Its edits reach the
## view as one change (Q3MapDoc.begin_batch), however many objects it touches.
func execute(cmd: String) -> bool:
	var d := doc   # a command can open another document; the batch belongs to this one
	d.begin_batch()
	var known: Variant = _execute(cmd)   # null when the command hit a script error
	d.end_batch()
	return known == true


func _execute(cmd: String) -> bool:
	if cmd.begins_with("grid.") and cmd.substr(5).is_valid_int():
		grid.set_index(int(cmd.substr(5)) - 1)
		notify("Grid " + grid.label())
		status_changed.emit()
		return true
	if cmd.begins_with("edit.nudge."):
		_nudge(cmd.substr(11))
		return true
	if cmd == "view.filter.none":
		for f in filters.keys():
			set_filter(f, false)
		return true
	if cmd.begins_with("view.filter."):
		var f := cmd.substr(12)
		set_filter(f, not filters.has(f))
		return true
	match cmd:
		"tool.select":
			if tool != tools["select"]:
				set_tool("select")
			elif not tool.busy():
				clear_selection()
		"tool.create":
			set_tool("create")
		"tool.face":
			set_tool("face")
		"tool.painttarget":
			var ft: Q3FaceTool = tools["face"]
			ft.object_mode = not ft.object_mode
			set_tool("face")
			notify("Paint target: " + ("whole objects" if ft.object_mode else "single faces"))
		"view.explorer":
			ui.toggle_panels()
		"tool.spawn", "tool.jumppad", "tool.teleporter", "tool.weapon", "tool.item", "tool.sound", "tool.light", "tool.startline", "tool.finishline", "tool.checkpoint":
			start_entity(cmd.substr(5))
		"race.check":
			race_check()
		"race.settings":
			race_settings()
		"entity.spawnAtCamera":
			spawn_at_camera()
		"pickup.prev":
			cycle_pickup(-1)
		"pickup.next":
			cycle_pickup(1)
		"prefab.save":
			ui.save_prefab_dialog()
		"view.lighting":
			set_lights_only(not lights_only)
		"map.playtest":
			play_test()
		"map.compile":
			compile_map(["bsp", "vis", "light", "aas"], false)
		"map.bots":
			play_bots = 0 if play_bots > 0 else 3
			notify("Play-test bots: " + ("%d (bspc builds their area file)" % play_bots if play_bots > 0 else "none"))
		"map.fromCamera":
			play_from_camera = not play_from_camera
			notify("Play-tests start " + ("where the camera is" if play_from_camera else "at a spawn point"))
		"tool.vertex":
			set_tool("vertex")
		"face.lockTexture":
			_toggle_lock()
		"grid.vertexSnap":
			vertex_snap = not vertex_snap
			notify("Vertex snap " + ("on" if vertex_snap else "off"))
		"edit.snapVertices":
			(tools["vertex"] as Q3VertexTool).snap_to_grid()
		"patch.bevel", "patch.curve", "patch.cylinder", "patch.arch":
			create_curve(cmd.substr(6))
		"csg.subtract":
			subtract_selection()
		"grid.smaller":
			grid.step(-1)
			notify("Grid " + grid.label())
		"grid.bigger":
			grid.step(1)
			notify("Grid " + grid.label())
		"grid.snap":
			grid.snapping = not grid.snapping
			notify("Grid snapping " + ("on" if grid.snapping else "off: things go where the cursor puts them"))
			ui._save_layout()
		"grid.toggle":
			grid.visible = not grid.visible
		"edit.undo":
			if doc.can_undo():
				var label := doc.undo_label()
				doc.undo()
				notify("Undo " + label)
			else:
				UiSound.buzz()
		"edit.redo":
			if doc.can_redo():
				var label := doc.redo_label()
				doc.redo()
				notify("Redo " + label)
			else:
				UiSound.buzz()
		"edit.delete":
			if selection.size():
				doc.checkpoint("Delete")
				doc.remove(delete_set(selected_ids()))
		"edit.duplicate":
			if selection.size():
				var objs := selected_ids().map(func(i): return doc.get_obj(i))
				place_copies(objs, selection_bounds().position + _camera_axis(Vector3.RIGHT) * grid.units(), "Duplicate")
		"edit.copy":
			clipboard = selected_ids().map(func(i): return doc.get_obj(i).duplicate(true))
			if clipboard.size():
				notify("Copied %d" % clipboard.size())
		"edit.paste":
			if clipboard.is_empty():
				notify("Clipboard is empty")
			else:
				var at: Variant = cursor_point(mouse_pos)
				if at == null:
					at = to_id(camera.global_position - camera.global_transform.basis.z * 6.0)
				place_copies(clipboard, grid.snap_vec(at), "Paste")
		"select.all":
			var ids := []
			for id in doc.objects:
				if not doc.hidden.has(id) and not doc.locked.has(id) and not filtered(id):
					ids.append(id)
			select(ids)
		"edit.rotate":
			rotate_selection(90)
		"edit.rotateNeg":
			rotate_selection(-90)
		"edit.rotate15":
			rotate_selection(15)
		"edit.rotate15Neg":
			rotate_selection(-15)
		"edit.mirrorX":
			_mirror(0)
		"edit.mirrorY":
			_mirror(1)
		"edit.mirrorZ":
			_mirror(2)
		"edit.hide":
			if selection.size():
				var ids := selected_ids()
				clear_selection()
				doc.checkpoint("Hide")
				doc.set_hidden(ids, true)
		"edit.unhideAll":
			if doc.hidden.size():
				doc.checkpoint("Unhide all")
				doc.set_hidden(doc.hidden.keys(), false)
				_isolated.clear()
		"edit.lock":
			if selection.size():
				var ids := selected_ids()
				clear_selection()
				doc.checkpoint("Lock")
				doc.set_locked(ids, true)
		"edit.unlockAll":
			if doc.locked.size():
				doc.checkpoint("Unlock all")
				doc.set_locked(doc.locked.keys(), false)
		"view.isolate":
			if _isolated.size():
				doc.set_hidden(_isolated, false)
				_isolated.clear()
			elif selection.size():
				var others := []
				for id in doc.objects:
					if not selection.has(id) and not doc.hidden.has(id):
						others.append(id)
				_isolated = others
				doc.set_hidden(others, true)
		"edit.group":
			if selection.size() > 1:
				doc.checkpoint("Group")
				var gid := _new_group()
				for id in selected_ids():
					var o := doc.edit(id)
					o["group"] = gid
					doc.put(o)
				notify("Grouped %d" % selection.size())
		"edit.ungroup":
			var changed := false
			for id in selected_ids():
				if int(doc.get_obj(id).get("group", 0)) != 0:
					if not changed:
						doc.checkpoint("Ungroup")
						changed = true
					var o := doc.edit(id)
					doc.groups.erase(int(o["group"]))
					o["group"] = 0
					doc.put(o)
		"file.new":
			ui.ask_unsaved(func(): new_map())
		"file.open":
			ui.ask_unsaved(func(): ui.open_dialog())
		"file.save":
			if path == "" or path == map_path(AUTOSAVE):
				ui.save_as_dialog()
			else:
				save_map()
		"file.saveAs":
			ui.save_as_dialog()
		"file.publish":
			publish()
		"file.importQ3":
			ui.ask_unsaved(func(): ui.import_q3_dialog())
		"file.importMap":
			ui.ask_unsaved(func(): ui.map_file_dialog("", func(p): import_map("map", p)))
		"file.importBsp":
			ui.ask_unsaved(func(): ui.file_dialog("", PackedStringArray(["*.bsp, *.pk3 ; Quake 3 maps and packages"]), "Import a .bsp or .pk3",
					func(p): import_bsp_file(p)))
		"file.exportBsp":
			ui.file_dialog(Q3EditorUi.clean_name(String(doc.info.get("name", "untitled"))) + ".pk3",
					PackedStringArray(["*.pk3 ; Quake 3 package (map, bot file, arena, levelshot)", "*.bsp ; Compiled map"]), "Export the compiled map",
					func(p): export_compiled(p))
		"file.exportMap", "file.exportValve":
			var format := "valve220" if cmd == "file.exportValve" else "brushdef"
			ui.map_file_dialog(Q3EditorUi.clean_name(String(doc.info.get("name", "untitled"))) + ".map",
					func(p): export_map_file(p, format))
		"session.leave":
			ui.ask_unsaved(func(): leave())
		"view.focus":
			var box := selection_bounds() if selection.size() else bounds_of(doc.objects.keys())
			if box.size == Vector3.ZERO and selection.is_empty():
				camera.home()
			else:
				camera.focus(to_godot(box.get_center()), box.size.length() * 0.5 / 32.0)
		"view.home":
			camera.home()
		"view.top", "view.front", "view.side", "view.3d":
			set_view(cmd.substr(5))
		"help.cheatsheet":
			ui.toggle_sheet()
		_:
			return false
	status_changed.emit()
	return true


## The id axis (unit, horizontal) closest to the camera's right or forward.
func _camera_axis(which: Vector3) -> Vector3:
	if camera.ortho != "":
		# a 2D view: the arrows move along the screen
		var s := camera.global_transform.basis.x if which == Vector3.RIGHT else camera.global_transform.basis.y
		return to_id(s).round()
	var g := camera.global_transform.basis.x if which == Vector3.RIGHT else -camera.global_transform.basis.z
	var v := Vector3(g.z, g.x, 0)   # Godot -> id, flattened
	if v.length() < 1e-4:
		return Vector3(1, 0, 0)
	return Vector3(signf(v.x), 0, 0) if absf(v.x) >= absf(v.y) else Vector3(0, signf(v.y), 0)


func _nudge(dir: String) -> void:
	var step := float(grid.units())
	var d := Vector3.ZERO
	match dir:
		"left": d = -_camera_axis(Vector3.RIGHT)
		"right": d = _camera_axis(Vector3.RIGHT)
		"forward": d = _camera_axis(Vector3.FORWARD)
		"back": d = -_camera_axis(Vector3.FORWARD)
		"up": d = Vector3(0, 0, 1)
		"down": d = Vector3(0, 0, -1)
	if tool.nudge(d * step):
		return
	move_selection(d * step, "Nudge")


## Turns the selection about the vertical through its center snapped to the grid, so 90 degree
## turns of on-grid brushes stay on the grid.
func rotate_selection(degrees: float) -> void:
	if selection.is_empty():
		return
	var c := grid.snap_vec(selection_bounds().get_center(), true)
	transform_selection("Rotate %d°" % int(degrees), Q3MapDoc.yaw_rotation(degrees, c), degrees)


func _mirror(axis: int) -> void:
	if selection.is_empty():
		return
	transform_selection("Mirror", Q3MapDoc.mirror(axis, selection_bounds().get_center()))


# ---------------------------------------------------------------- materials (E3)

## What painting reaches: the face tool's faces, else every face of the selected brushes and the
## selected patches. [face keys, patch ids].
func paint_targets() -> Array:
	var keys := faces.keys()
	var patches := []
	if keys.is_empty():
		for id in selected_ids():
			var o := doc.get_obj(id)
			match o.get("type", ""):
				"brush":
					for i in o["faces"].size():
						keys.append(Vector2i(id, i))
				"patch":
					patches.append(id)
	return [keys, patches]


## Makes shader the current material and paints it onto the selection (faces, or whole brushes and
## patches). Returns how many faces and patches changed.
func apply_material(shader: String) -> int:
	material = shader
	Q3MapMaterials.add_recent(shader)
	var t := paint_targets()
	var n: int = t[0].size() + t[1].size()
	if n == 0:
		notify("Current material: " + Q3MapMaterials.short_name(shader))
		status_changed.emit()
		return 0
	var on_face := func(f: Dictionary, _b: Dictionary, _i: int) -> Dictionary:
		var g := f.duplicate(true)
		g["material"] = shader
		return g
	var on_patch := func(p: Dictionary) -> void:
		p["material"] = shader
	_edit_targets("Paint " + Q3MapMaterials.short_name(shader), t, on_face, on_patch)
	return n


## Tints the selection (white clears it). The world shaders multiply their albedo by it.
func set_tint(c: Color) -> void:
	var t := paint_targets()
	if t[0].is_empty() and t[1].is_empty():
		return
	var on_face := func(f: Dictionary, _b: Dictionary, _i: int) -> Dictionary:
		var g := f.duplicate(true)
		if c == Color.WHITE:
			g.erase("color")
		else:
			g["color"] = c
		return g
	var on_patch := func(p: Dictionary) -> void:
		if c == Color.WHITE:
			p.erase("color")
		else:
			p["color"] = c
	_edit_targets("Tint", t, on_face, on_patch)


## Runs face_fn(face, brush, index) -> new face over the face targets and patch_fn(patch) over the
## patch targets, as one undo step.
func _edit_targets(label: String, t: Array, face_fn: Callable, patch_fn: Callable) -> void:
	doc.checkpoint(label)
	var by := {}
	for k: Vector2i in t[0]:
		if not by.has(k.x):
			by[k.x] = doc.edit(k.x)
		var b: Dictionary = by[k.x]
		b["faces"][k.y] = face_fn.call(b["faces"][k.y], b, k.y)
	doc.begin_batch()
	for id in by:
		doc.put(by[id])
	for id in t[1]:
		var p := doc.edit(id)
		patch_fn.call(p)
		doc.put(p)
	doc.end_batch()


## Face mapping edits from the inspector and the face tool, on every face target: "align" (how: fit,
## left, right, top, bottom, center, flipU, flipV), "reset" (Quake 3's world projection again) or
## "field" (how: 0 shift s, 1 shift t, 2 rotate, 3 scale s, 4 scale t; world-projected faces only).
func edit_faces(what: String, how: Variant = null, value := 0.0) -> void:
	var t := paint_targets()
	if t[0].is_empty():
		return
	var label: String = {"align": "Align texture", "reset": "Reset texture", "field": "Texture"}[what]
	var on_face := func(f: Dictionary, b: Dictionary, i: int) -> Dictionary:
		match what:
			"align":
				return Q3FaceTool.aligned(f, Q3MapDoc.plane_of(b, i), doc.windings(b["id"])[i], String(how))
			"reset":
				var r := {"material": f["material"], "tex": Q3MapDoc.DEFAULT_TEX.duplicate()}
				if f.has("color"):
					r["color"] = f["color"]
				return r
		if not f.has("tex"):
			return f
		var g := f.duplicate(true)
		var tex: Array = Array(g["tex"]).duplicate()
		tex[int(how)] = value
		g["tex"] = tex
		return g
	_edit_targets(label, [t[0], []], on_face, func(_p: Dictionary) -> void: pass)


## Drops a ready-made curve at the snapped cursor in the current material (64 units radius, 128
## tall), selects it and switches to the vertex tool so its control net is right there to pull on.
func create_curve(kind: String) -> int:
	var at: Variant = cursor_point(mouse_pos)
	if at == null:
		at = to_id(camera.global_position - camera.global_transform.basis.z * 6.0)
	var p := Q3PatchFactory.make(kind, grid.snap_vec(at), 64.0, 128.0, material)
	if p.is_empty():
		return -1
	doc.checkpoint("Create " + kind)
	var id := doc.add(p)
	select([id])
	set_tool("vertex")
	return id


## Ctrl+T: locks the texture on the selected faces (or every face of the selected brushes) so it
## moves with the brush, or frees it again. A locked face keeps an explicit mapping.
func _toggle_lock() -> void:
	var keys: Array = paint_targets()[0]
	if keys.is_empty():
		notify("Select faces or brushes to lock their textures", true)
		return
	set_lock(not keys.all(func(k): return doc.get_obj(k.x)["faces"][k.y].get("lock", false)))


func set_lock(lock: bool) -> void:
	var keys: Array = paint_targets()[0]
	if keys.is_empty():
		return
	doc.checkpoint("Lock textures" if lock else "Free textures")
	var by_brush := {}
	for k in keys:
		if not by_brush.has(k.x):
			by_brush[k.x] = doc.edit(k.x)
		var b: Dictionary = by_brush[k.x]
		var f: Dictionary = b["faces"][k.y]
		if lock:
			f["uv"] = Q3MapDoc.face_uv(f, Q3MapDoc.plane_of(b, k.y))
			f.erase("tex")
			f["lock"] = true
		else:
			f.erase("lock")
	for id in by_brush:
		doc.put(by_brush[id])
	notify("Texture %s on %d face%s" % ["locked" if lock else "free", keys.size(), "" if keys.size() == 1 else "s"])


## Ctrl+Shift+X: carves the selected brushes out of every brush they overlap, then deletes them.
func subtract_selection() -> void:
	var cutters := selected_ids().filter(func(i): return doc.get_obj(i).get("type", "") == "brush")
	if cutters.is_empty():
		notify("Select the brushes to cut with", true)
		return
	# Each target goes through every cutter in turn; pieces of pieces are cut again.
	var results := {}
	for tid in doc.objects:
		var t: Dictionary = doc.objects[tid]
		if t.get("type", "") != "brush" or selection.has(tid) or doc.hidden.has(tid) or doc.locked.has(tid):
			continue
		var pieces := [t]
		var touched := false
		for cid in cutters:
			var cut: PackedFloat64Array = doc.get_obj(cid)["planes"]
			var next := []
			for p in pieces:
				var r: Dictionary = Q3MapGeo.subtract(p["planes"], cut)
				if not r["touched"]:
					next.append(p)
					continue
				touched = true
				for np in r["pieces"]:
					var b := Q3MapDoc.brush(np["planes"])
					b["entity"] = p.get("entity", 0)
					b["group"] = p.get("group", 0)
					for i in np["attr"].size():
						b["faces"][i] = p["faces"][np["attr"][i]].duplicate(true)
					next.append(b)
			pieces = next
		if touched:
			results[tid] = pieces
	if results.is_empty():
		notify("The selection doesn't overlap any brush", true)
		return
	doc.checkpoint("CSG subtract")
	var made := 0
	for tid in results:
		doc.remove([tid])
		for p in results[tid]:
			p["id"] = 0
			doc.add(p)
			made += 1
	doc.remove(cutters)
	notify("Cut %d brush%s into %d" % [results.size(), "" if results.size() == 1 else "es", made])


# ---------------------------------------------------------------- entities and prefabs (E4)

const JUMP_PAD_MATERIAL := "textures/sfx/bouncepad01block18b"
const TRIGGER_MATERIAL := "textures/common/trigger"


## Arms the entity tool: kind is spawn, jumppad, teleporter, weapon, item, sound, light, or class
## (any point class, cn).
func start_entity(kind: String, cn := "") -> void:
	var et: Q3EntityTool = tools["entity"]
	et.cancel()
	et.kind = kind
	if cn != "":
		match kind:
			"weapon": et.weapon = cn
			"item": et.item = cn
			_: et.classname = cn
	set_tool("entity")


## The Entities tab's click. A point class arms the entity tool with it; a brush class turns the
## selected brushes into that entity (trigger_push and trigger_teleport with nothing selected start
## the jump pad and teleporter tools instead).
func use_class(cn: String) -> void:
	if Q3MapEntities.is_brush_class(cn):
		var any := selected_ids().any(func(i): return doc.get_obj(i).get("type", "") in ["brush", "patch"])
		if not any and cn in ["trigger_push", "trigger_teleport"]:
			start_entity("jumppad" if cn == "trigger_push" else "teleporter")
		else:
			make_brush_entity(cn)
		return
	match cn:
		"light": start_entity("light")
		"target_speaker": start_entity("sound")
		"info_player_deathmatch": start_entity("spawn")
		_:
			if cn in Q3MapEntities.weapons():
				start_entity("weapon", cn)
			elif cn in Q3MapEntities.items():
				start_entity("item", cn)
			else:
				start_entity("class", cn)


## A point entity at `at` (id units), selected. Returns its id.
func place_entity(cn: String, at: Vector3, keys := {}, label := "") -> int:
	doc.checkpoint(label if label != "" else "Add " + cn)
	var id := doc.add(Q3MapEntities.point(cn, at, keys))
	select([id])
	UiSound.move()
	return id


## J: a jump pad on the floor at `at` that lands on `landing`: a bounce pad brush, the trigger_push
## over it and the target_position at the top of the arc (AimAtTarget's apex). Returns the
## trigger_push's id.
func place_jump_pad(at: Vector3, landing: Vector3) -> int:
	var half := Q3MapEntities.PAD / 2.0
	var g := Q3MapEntities.gravity(doc)
	var apex := Q3MapEntities.pad_apex(at, landing, g, ceiling_over(at, landing)).round()
	doc.checkpoint("Jump pad")
	var name := Q3MapEntities.unique_name(doc, "pad")
	var gid := _new_group()
	var push := Q3MapEntities.brush_entity("trigger_push", {"target": name})
	push["group"] = gid
	var pid := doc.add(push)
	var pad := Q3MapDoc.box_brush(at + Vector3(-half, -half, 0), at + Vector3(half, half, 4), JUMP_PAD_MATERIAL)
	pad["group"] = gid
	var trig := Q3MapDoc.box_brush(at + Vector3(-half, -half, 0), at + Vector3(half, half, 36), TRIGGER_MATERIAL)
	trig["entity"] = pid
	trig["group"] = gid
	# the target is part of the pad (copies, prefabs and deletes take it along); double-click it to
	# move it alone
	var target := Q3MapEntities.point("target_position", apex, {"targetname": name})
	target["group"] = gid
	var ids := [pid, doc.add(pad), doc.add(trig), doc.add(target)]
	select(ids)
	var v := Q3MapEntities.push_velocity(at + Vector3(0, 0, 18), apex, g)
	notify("Jump pad: %d u/s, %d units up" % [int(v.length()), int(apex.z - at.z - 18)])
	return pid


## The lowest brush underside above the straight path from a to b (rays straight up at a few points
## along it), or INF under open sky.
func ceiling_over(a: Vector3, b: Vector3) -> float:
	var low := INF
	var up := f64(Vector3(0, 0, 1))
	for i in 7:
		var p := a.lerp(b, i / 6.0) + Vector3(0, 0, 40)
		for id in doc.objects:
			var o: Dictionary = doc.objects[id]
			if o["type"] != "brush" or not Q3PlayMap.world_part(doc, o) or doc.hidden.has(id):
				continue
			var hit: PackedFloat64Array = Q3MapGeo.ray(o["planes"], f64(p), up)
			if hit.size() == 2 and hit[0] >= 0.0:
				low = minf(low, p.z + hit[0])
	return low


## K: a teleporter on the floor at `at` whose exit is on the floor at `exit`, facing yaw. Returns
## the trigger_teleport's id.
func place_teleporter(at: Vector3, exit: Vector3, yaw: float) -> int:
	var half := Q3MapEntities.PAD / 2.0
	doc.checkpoint("Teleporter")
	var name := Q3MapEntities.unique_name(doc, "tele")
	var gid := _new_group()
	var tele := Q3MapEntities.brush_entity("trigger_teleport", {"target": name})
	tele["group"] = gid
	var eid := doc.add(tele)
	var trig := Q3MapDoc.box_brush(at + Vector3(-half, -half, 0), at + Vector3(half, half, 64), TRIGGER_MATERIAL)
	trig["entity"] = eid
	trig["group"] = gid
	var dest := Q3MapEntities.point("misc_teleporter_dest", exit + Vector3(0, 0, 24), {"targetname": name, "angle": _num(yaw)})
	dest["group"] = gid
	var ids := [eid, doc.add(trig), doc.add(dest)]
	select(ids)
	notify("Teleporter linked")
	return eid


## The volume of a race line across a course: the box from one side to the other (a corner of the floor
## each), as thick as LINE_DEPTH along the way the player runs (the longer of the two axes is the
## width) and tall as TRIGGER_HEIGHT, or up to the roof. {min, max}.
static func race_line_box(a: Vector3, b: Vector3, roof := 0.0) -> Array:
	var lo := Vector3(minf(a.x, b.x), minf(a.y, b.y), minf(a.z, b.z))
	var hi := Vector3(maxf(a.x, b.x), maxf(a.y, b.y), maxf(a.z, b.z))
	var half := Q3MapRace.LINE_DEPTH * 0.5
	if hi.x - lo.x >= hi.y - lo.y:
		var cy := (lo.y + hi.y) * 0.5
		lo.y = cy - half
		hi.y = cy + half
		if hi.x - lo.x < 64.0:
			var cx := (lo.x + hi.x) * 0.5
			lo.x = cx - 32.0
			hi.x = cx + 32.0
	else:
		var cx2 := (lo.x + hi.x) * 0.5
		lo.x = cx2 - half
		hi.x = cx2 + half
		if hi.y - lo.y < 64.0:
			var cy2 := (lo.y + hi.y) * 0.5
			lo.y = cy2 - 32.0
			hi.y = cy2 + 32.0
	var top := lo.z + Q3MapRace.TRIGGER_HEIGHT
	if roof > 0.0:
		top = maxf(minf(top, roof), lo.z + 64.0)
	lo.z = lo.z
	hi.z = top
	return [lo, hi]


## Y, U and C: a race line across the course from `a` to `b`: a trigger_multiple volume and the Defrag
## timer entity it fires (target_startTimer, target_stopTimer or target_checkpoint), grouped like a jump
## pad. Returns the trigger's id.
func place_race_line(kind: String, a: Vector3, b: Vector3) -> int:
	var cls := {"startline": Q3MapRace.START, "finishline": Q3MapRace.STOP, "checkpoint": Q3MapRace.CHECKPOINT}[kind] as String
	var prefix := {"startline": "start", "finishline": "finish", "checkpoint": "cp"}[kind] as String
	var label := {"startline": "Start line", "finishline": "Finish line", "checkpoint": "Checkpoint"}[kind] as String
	var mid := (a + b) * 0.5
	var roof := ceiling_over(mid, mid)
	var box := race_line_box(a, b, roof if roof < INF else 0.0)
	doc.checkpoint(label)
	var name := Q3MapEntities.unique_name(doc, prefix)
	var gid := _new_group()
	var trig := Q3MapEntities.brush_entity("trigger_multiple", {"target": name, "wait": "0.5"})
	trig["group"] = gid
	var tid := doc.add(trig)
	var vol := Q3MapDoc.box_brush(box[0], box[1], TRIGGER_MATERIAL)
	vol["entity"] = tid
	vol["group"] = gid
	# the timer entity sits inside the volume (an entity outside the hull is a leak)
	var at := Vector3((box[0].x + box[1].x) * 0.5, (box[0].y + box[1].y) * 0.5, box[0].z + 24.0)
	var timer := Q3MapEntities.point(cls, at.round(), {"targetname": name})
	timer["group"] = gid
	var ids := [tid, doc.add(vol), doc.add(timer)]
	select(ids)
	UiSound.move()
	notify("%s placed (%s)" % [label, Q3MapRace.summary(doc)])
	return tid


## Shift+C: what's wrong with the course. The problems come as a list; picking one selects what it's
## about.
func race_check() -> void:
	var problems := Q3MapRace.problems(doc)
	if problems.is_empty():
		if Q3MapRace.has_course(doc):
			notify("Course OK: %s" % Q3MapRace.summary(doc))
		else:
			notify("Not a course yet: place a start line (Y) and a finish line (U)")
		return
	var items := problems.map(func(p): return ("ERROR  " if p["error"] else "Note   ") + String(p["text"]))
	var by_item := {}
	for i in items.size():
		by_item[items[i]] = problems[i]
	ui.pick_dialog("RACE COURSE", "%s. Pick one to select what it's about." % Q3MapRace.summary(doc), items, func(it):
		var ids: Array = by_item[it]["ids"]
		if ids.size():
			select(ids)
			execute("view.focus"), "SELECT")


## Shift+Y: the course's style (what the menu files it under) and its author.
func race_settings() -> void:
	ui.pick_dialog("COURSE STYLE", "Run: start to finish. Training: practice. Accuracy: shoot targets. Level: collect items.",
			Q3MapRace.STYLES.duplicate(), func(style):
		ui.name_dialog("COURSE AUTHOR", Q3MapRace.author(doc), func(author):
			set_world({"race_style": String(style), "race_author": String(author)})
			notify("Course: %s, by %s" % [style, author if author != "" else "unknown"])), "NEXT")


## L: a light at `at`; with spot_at, a spot aimed there through an info_null target, as q3map wants.
func place_light(at: Vector3, spot_at: Variant = null) -> int:
	doc.checkpoint("Spot light" if spot_at != null else "Light")
	var keys := {"light": "300"}
	if spot_at != null:
		var name := Q3MapEntities.unique_name(doc, "spot")
		keys["target"] = name
		doc.add(Q3MapEntities.point("info_null", spot_at, {"targetname": name}))
	var id := doc.add(Q3MapEntities.point("light", at, keys))
	select([id])
	UiSound.move()
	return id


## Shift+P: a spawn point that puts the player's eye where the camera is, facing its way.
func spawn_at_camera() -> int:
	var eye := to_id(camera.global_position)
	var at := (eye - Vector3(0, 0, Q3MapEntities.VIEWHEIGHT + Q3MapEntities.SPAWN_LIFT)).round()
	var yaw: float = (tools["entity"] as Q3EntityTool).camera_yaw()
	return place_entity("info_player_deathmatch", at, {"angle": _num(yaw)}, "Spawn at camera")


## , and .: the selected pickups become the previous or next weapon or item; with none selected,
## the weapon or item tool steps instead.
func cycle_pickup(step: int) -> void:
	var picks := selected_ids().filter(func(i): return Q3MapEntities.is_item(String(doc.get_obj(i).get("classname", ""))))
	if picks.size():
		doc.checkpoint("Change pickup")
		for id in picks:
			var e := doc.edit(id)
			e["classname"] = Q3MapEntities.cycle(e["classname"], step)
			doc.put(e)
		notify(Q3MapEntities.description(doc.get_obj(picks[0])["classname"]))
		return
	var et: Q3EntityTool = tools["entity"]
	if tool != et or et.kind not in ["weapon", "item"]:
		notify("Select pickups, or use the weapon (I) or item (O) tool", true)
		return
	if et.kind == "weapon":
		et.weapon = Q3MapEntities.cycle(et.weapon, step)
	else:
		et.item = Q3MapEntities.cycle(et.item, step)
	notify(Q3MapEntities.description(et.current_class()))
	et.hover(mouse_pos, 0)
	status_changed.emit()


## The entities the inspector edits: selected entities, and the owners of selected brushes.
func entity_targets() -> Array:
	var out := []
	for id in selected_ids():
		var o := doc.get_obj(id)
		var e: int = id if o.get("type", "") == "entity" else int(o.get("entity", 0))
		if e != 0 and doc.objects.has(e) and not out.has(e):
			out.append(e)
	return out


## Sets keys on entities as one undo step: a value of "" or null removes the key; "classname"
## changes the class.
func set_keys(ids: Array, changes: Dictionary, label := "Edit keys") -> void:
	if ids.is_empty():
		return
	doc.checkpoint(label)
	for id in ids:
		var e := doc.edit(id)
		if e.get("type", "") != "entity":
			continue
		for k in changes:
			var v: Variant = changes[k]
			if k == "classname":
				if String(v) != "":
					e["classname"] = String(v)
			elif v == null or String(v) == "":
				e["keys"].erase(k)
			else:
				e["keys"][k] = String(v)
		doc.put(e)


## Map settings (worldspawn's keys: message, music, gravity, ambient ...), as for set_keys.
func set_world(changes: Dictionary) -> void:
	doc.checkpoint("Map settings")
	for k in changes:
		var v: Variant = changes[k]
		if v == null or String(v) == "":
			doc.worldspawn.erase(k)
		else:
			doc.worldspawn[k] = String(v)
	doc.dirty = true
	ents.touch([])      # gravity bends the jump pad arcs
	if lights_only:
		set_lights_only(true)
	status_changed.emit()


## Turns the selected brushes (and, for func_ classes, patches) into one brush entity of class cn.
## Triggers get the trigger shader, as q3map wants them unseen. Entities left with no brushes go.
func make_brush_entity(cn: String) -> int:
	var kinds := ["brush"] if cn.begins_with("trigger_") else ["brush", "patch"]
	var parts := selected_ids().filter(func(i): return doc.get_obj(i).get("type", "") in kinds)
	if parts.is_empty():
		notify("Select the brushes to make into %s" % cn, true)
		return -1
	doc.checkpoint("Make " + cn)
	var gid := _new_group()
	var e := Q3MapEntities.brush_entity(cn)
	e["group"] = gid
	var eid := doc.add(e)
	var before := {}
	for id in parts:
		var b := doc.edit(id)
		if int(b.get("entity", 0)) != 0:
			before[int(b["entity"])] = true
		b["entity"] = eid
		b["group"] = gid
		if cn.begins_with("trigger_") and b["type"] == "brush":
			for f in b["faces"]:
				f["material"] = TRIGGER_MATERIAL
		doc.put(b)
	var own := Q3MapEntities.owned(doc)
	doc.remove(before.keys().filter(func(o): return not own.has(o)))
	select(parts + [eid])
	notify("Made %s from %d object%s" % [cn, parts.size(), "" if parts.size() == 1 else "s"])
	return eid


## What deleting ids takes along: the brushes of deleted brush entities, and brush entities whose
## brushes are all going.
func delete_set(ids: Array) -> Array:
	var gone := {}
	for id in ids:
		gone[id] = true
	var own := Q3MapEntities.owned(doc)
	for e in own:
		if gone.has(e):
			for b in own[e]:
				gone[b] = true
		elif own[e].all(func(b): return gone.has(b)):
			gone[e] = true
	return gone.keys()


## Shift+L: the map lit by its own lights (preview sun off, worldspawn's ambient as q3map's minimum
## light, the lights casting shadows), or the editor's light again.
func set_lights_only(on: bool) -> void:
	lights_only = on
	_sun.visible = not on
	if on:
		var amb := float(doc.worldspawn.get("ambient", "0"))
		_env.ambient_light_color = Q3MapBuilder._color(String(doc.worldspawn.get("_color", "1 1 1")))
		_env.ambient_light_energy = clampf(0.06 + amb / 64.0, 0.06, 0.6)
	else:
		_env.ambient_light_color = Color(0.42, 0.4, 0.4)
		_env.ambient_light_energy = 1.0
	ents.set_lights_only(on)
	notify("Lighting: the map's own lights" if on else "Lighting: editor preview")


## Ctrl+P: saves the selection (with the entities its brushes belong to) as a prefab, and renders
## its thumbnail.
func save_prefab(name: String) -> Error:
	var ids := selected_ids()
	for e in entity_targets():
		if not ids.has(e):
			ids.append(e)
	# what the saved entities target comes too, so the prefab's links point inside it
	var names := Q3MapEntities.targetnames(doc)
	for e in ids.duplicate():
		for t in Q3MapEntities.targets_of(doc, doc.get_obj(e), names):
			if not ids.has(t):
				ids.append(t)
	if ids.is_empty():
		notify("Select something to save as a prefab", true)
		return ERR_INVALID_PARAMETER
	var err := Q3MapPrefabs.save(name, doc, ids)
	if err != OK:
		notify("Couldn't save prefab %s (error %d)" % [name, err], true)
		return err
	thumbs.render_objects("prefab:" + name, Q3MapPrefabs.load_objects(name), Q3MapPrefabs.thumb_path(name))
	notify("Saved prefab " + name)
	prefabs_changed.emit()
	return OK


## Arms the prefab tool with a saved prefab.
func start_prefab(name: String) -> void:
	var objs := Q3MapPrefabs.load_objects(name)
	if objs.is_empty():
		notify("Couldn't read prefab " + name, true)
		return
	(tools["prefab"] as Q3PrefabTool).set_prefab(name, objs)
	set_tool("prefab")


func place_prefab(name: String, objs: Array, at: Vector3) -> Array:
	var ids := place_copies(objs, at, "Place " + name, true)
	UiSound.move()
	return ids


func delete_prefab(name: String) -> void:
	Q3MapPrefabs.remove(name)
	notify("Deleted prefab " + name)
	prefabs_changed.emit()


# ---------------------------------------------------------------- compile and play-test (E5)

## F5: compiles the map (q3map, and bspc for the bots) and plays it in Gameplay; F6 there comes back.
func play_test() -> void:
	var course := Q3MapRace.has_course(doc)   # a course is run with the timer, not fought over
	compile_map(["bsp", "aas"] if play_bots > 0 and not course else ["bsp"], true)


## Writes the map for q3map and starts a background compile of these stages; `then_play` hands the
## result to Gameplay when it works, `publish` lists it for matches (publish()).
func compile_map(stages: Array, then_play: bool, publish := false, export_to := "") -> void:
	if compiler.busy or importing != "":
		notify("Already compiling" if compiler.busy else "Wait for the import to finish", true)
		return
	write_autosave()
	var ex := Q3MapExport.to_map(doc)
	var base := Q3EditorUi.clean_name(String(doc.info.get("name", "untitled")))
	var name := Q3MapCompiler.published_name(base) if publish else Q3MapCompiler.output_name(base)
	_compile_job = {"models": ex["models"], "warnings": ex["warnings"], "play": then_play, "map": name,
			"doc": doc, "rev": _doc_rev, "publish": publish, "path": path, "export": export_to}
	var key := "%s|%s|%s" % [String(ex["text"]).md5_text(), name, ",".join(stages)]
	_compile_job["key"] = key
	var cached_bsp := String(_play_cache.get("r", {}).get("bsp", ""))
	if then_play and _play_cache.get("key", "") == key and FileAccess.file_exists(cached_bsp) 			and FileAccess.get_modified_time(cached_bsp) == int(_play_cache.get("time", -1)):   # not since rewritten by another map of that name
		# nothing changed since the last play-test's compile: its .bsp is still right
		_on_compiled.call_deferred(_play_cache["r"])
		ui.set_hint("Starting the play-test ...")
		return
	if publish:
		# what's published is the map as compiled: a frozen copy of the document goes beside the .bsp,
		# so later edits (or opening another map meanwhile) can't change what players see
		_compile_job["snapshot"] = JSON.stringify(doc.to_data(), "\t", false, true)
		_compile_job["name"] = String(doc.info.get("name", "untitled"))
		_compile_job["message"] = String(doc.worldspawn.get("message", "")).replace("\\n", " ").strip_edges()
		_compile_job["types"] = Q3MapPublish.types_of(doc)
		if Q3MapRace.has_course(doc):
			_compile_job["race"] = {"style": Q3MapRace.style(doc), "author": Q3MapRace.author(doc)}
	overlay.show_leak(PackedVector3Array())
	compiler.compile(name, ex["text"], stages)
	ui.set_hint("Compiling %s: %s ..." % [name, ", ".join(stages)])
	notify("Compiling ...")



func _on_compiled(r: Dictionary) -> void:
	last_compile = r
	ui.set_hint("")
	var job := _compile_job
	_compile_job = {}
	if not r["ok"]:
		ui.show_compile_log("COMPILE FAILED", r["error"], r["log"])
		return
	var notes := PackedStringArray(job.get("warnings", []))
	if r["leaked"]:
		# q3map's leak line runs from an entity out to the void through the hole
		overlay.show_leak(r["leak"])
		notes.append("The map leaks: the red line runs from an entity out through the gap. Vis, light and bots need it sealed.")
	if job.get("export", "") != "":
		_finish_export(job, r, notes)
		return
	if job.get("publish", false):
		if r["leaked"] or r["aas"] == "":
			ui.show_compile_log("CAN'T PUBLISH YET", "\n".join(notes) if notes.size() else "The bot file (bspc) wasn't made.", r["log"])
		else:
			_finish_publish(job, r)
		return
	if not job.get("play", false):
		if notes.size():
			ui.show_compile_log("COMPILED, WITH WARNINGS", "\n".join(notes), r["log"])
		else:
			notify("Compiled in %.1f s: %s" % [r["seconds"], ", ".join(r["stages"])])
		return
	for n in notes:
		push_warning("map editor: " + n)
	if not is_same(job.get("doc"), doc) or job.get("rev", -1) != _doc_rev:
		# Gameplay would draw today's document over the .bsp compiled from the old one
		notify("The map changed while it compiled: press F5 again", true)
		return
	_play_cache = {"key": job.get("key", ""), "r": r, "time": FileAccess.get_modified_time(String(r["bsp"]))}
	var course := Q3MapRace.has_course(doc)
	var bots := play_bots if r["aas"] != "" and not course else 0
	# the 3D view's pose, also from a 2D view (where the player starts, and the view to come back to)
	var pose := [camera.position, camera.yaw, camera.pitch]
	if camera.ortho != "" and camera._pose_3d.size():
		pose = camera._pose_3d.duplicate()
	var start := camera_start(pose[0], pose[1], pose[2]) if play_from_camera and not course else []   # a course starts at its spawn: the timer is off with cheats
	if play_from_camera and not course and start.is_empty():
		notify("The camera is outside the map: the play-test starts at a spawn point")
	var errs := Session.play_test({"doc": doc, "models": job["models"], "map": job["map"], "path": path,
			"camera": pose, "view": view, "start": start, "selection": selected_ids(),
			"tool": tools.find_key(tool), "leaked": r["leaked"], "isolated": _isolated.duplicate(),
			"faces": faces.keys(), "verts": verts.keys(), "race": course}, bots)
	if errs.size():
		notify("Can't play: " + "; ".join(errs), true)


## Where a play-test starts the player: the camera's spot (Godot space) and facing as [origin, pitch,
## yaw] in id units and degrees, or [] when the camera isn't inside the map (a ray out from it meets
## nothing in some direction). Gameplay drops the player to the floor under it (Cmd_EditorPos_f).
func camera_start(pos: Vector3, yaw_deg: float, pitch_deg: float) -> Array:
	var o := to_id(pos)
	for d in [Vector3(1, 0, 0), Vector3(-1, 0, 0), Vector3(0, 1, 0), Vector3(0, -1, 0), Vector3(0, 0, 1), Vector3(0, 0, -1)]:
		if (_hits(o, d, false, true)[0] as Dictionary).is_empty():
			return []
	var fwd := to_id(Basis.from_euler(Vector3(deg_to_rad(pitch_deg), deg_to_rad(yaw_deg), 0.0)) * Vector3.FORWARD)
	return [o, -pitch_deg, rad_to_deg(atan2(fwd.y, fwd.x))]


## Publish (OZMIUM's "freeze for players"): saves the map, compiles it in full (bsp, vis, light and
## the bots' aas) as pub_<name>, and lists it in the menu's match setup (Q3MapPublish). A map that
## leaks, or has no spawn point, isn't published.
func publish() -> void:
	if path == "" or path == map_path(AUTOSAVE):
		ui.save_as_dialog(func(): publish())
		return
	if doc.dirty and save_map() != OK:
		return
	var problems := Q3MapPublish.problems(doc)
	if problems.size():
		ui.show_compile_log("CAN'T PUBLISH YET", "\n".join(problems), "")
		return
	if not FileAccess.file_exists(thumb_path(String(doc.info.get("name", "")))):
		take_thumbnail(String(doc.info.get("name", "")))   # the menu shows it; ready long before the compile
	compile_map(Array(Q3MapCompiler.STAGES), false, true)


func _finish_publish(job: Dictionary, r: Dictionary) -> void:
	var name: String = job["name"]
	var frozen := Q3MapCompiler.output_dir.path_join(String(job["map"]) + Q3MapDoc.EXTENSION)
	var f := FileAccess.open(frozen, FileAccess.WRITE)
	if f == null:
		notify("Couldn't write the published copy (error %d)" % FileAccess.get_open_error(), true)
		return
	f.store_string(job["snapshot"])
	f.close()
	var models := {}
	for e in job["models"]:
		models[str(e)] = job["models"][e]
	var entry := {"map": job["map"], "name": job["message"] if job["message"] != "" else name,
			"types": job["types"], "special": "custom", "bots": [], "fraglimit": 20, "timelimit": 0,
			"levelshot": Q3MapPublish.data_url(thumb_path(name)), "doc": frozen, "models": models,
			"published": int(Time.get_unix_time_from_system())}
	if job.has("race"):
		entry["race"] = job["race"]
	var err := Q3MapPublish.put(entry)
	if err != OK:
		notify("Couldn't list the published map (error %d)" % err, true)
		return
	Session.refresh_custom_maps()
	notify("Published %s in %.0f s: it's in Create Match as \"%s\"" % [name, r["seconds"], entry["name"]])


## File > Import a .bsp or .pk3: a .bsp imports as it is; a package's map does, or the player picks
## one when it holds several.
func import_bsp_file(p: String) -> void:
	if p.get_extension().to_lower() != "pk3":
		import_map("bspfile", p)
		return
	var zr := ZIPReader.new()
	if zr.open(p) != OK:
		notify("Couldn't open " + p.get_file(), true)
		return
	var maps := Array(zr.get_files()).filter(func(f): return String(f).to_lower().begins_with("maps/") and String(f).to_lower().ends_with(".bsp"))
	zr.close()
	if maps.is_empty():
		notify("%s has no maps in it" % p.get_file(), true)
	elif maps.size() == 1:
		import_map("bspfile", p + "::" + maps[0])
	else:
		ui.pick_dialog("IMPORT FROM " + p.get_file().to_upper(), "This package holds %d maps." % maps.size(), maps,
				func(m): import_map("bspfile", p + "::" + m))


## File > Export the compiled map: compiles it in full (bsp, vis, light and the bots' aas) and writes
## it where the player chose: a .bsp (its .aas beside it), or a .pk3 for Quake 3's baseq3 with
## maps/<name>.bsp and .aas, scripts/<name>.arena and the thumbnail as levelshots/<name>.jpg.
func export_compiled(p: String) -> void:
	compile_map(Q3MapCompiler.STAGES, false, false, p)


func _finish_export(job: Dictionary, r: Dictionary, notes: PackedStringArray) -> void:
	var dest: String = job["export"]
	var d: Q3MapDoc = job["doc"]
	var base := Q3EditorUi.clean_name(String(d.info.get("name", "untitled")))
	base = base if base != "" and base != AUTOSAVE else "untitled"
	var err := OK
	if dest.get_extension().to_lower() == "pk3":
		err = _write_pk3(dest, base, d, r)
	else:
		err = DirAccess.copy_absolute(r["bsp"], dest)
		if err == OK and r["aas"] != "":
			err = DirAccess.copy_absolute(r["aas"], dest.get_basename() + ".aas")
	if err != OK:
		notify("Couldn't write %s (error %d)" % [dest.get_file(), err], true)
		return
	if notes.size():
		ui.show_compile_log("EXPORTED, WITH WARNINGS", "Wrote %s.\n%s" % [dest, "\n".join(notes)], r["log"])
	else:
		notify("Exported %s in %.0f s" % [dest.get_file(), r["seconds"]])


## A Quake 3 package: the map, its bot file, an arena script (so the game's menus list it) and a
## levelshot from the editor's thumbnail.
func _write_pk3(dest: String, base: String, d: Q3MapDoc, r: Dictionary) -> Error:
	var files := {"maps/%s.bsp" % base: FileAccess.get_file_as_bytes(r["bsp"])}
	if r["aas"] != "":
		files["maps/%s.aas" % base] = FileAccess.get_file_as_bytes(r["aas"])
	var title := String(d.worldspawn.get("message", "")).replace("\\n", " ").replace("\"", "'").strip_edges()
	files["scripts/%s.arena" % base] = ("{\n\tmap \"%s\"\n\tlongname \"%s\"\n\ttype \"%s\"\n\tfraglimit \"20\"\n}\n" % [
			base, title if title != "" else base, " ".join(Q3MapPublish.types_of(d))]).to_utf8_buffer()
	if Q3MapRace.has_course(d):   # Defrag's own map file, so the game's menus (and Defrag's) list the course
		files["scripts/%s.defi" % base] = Q3MapRace.defi(base, d).to_utf8_buffer()
	var shot := thumb_path(String(d.info.get("name", "")))
	if FileAccess.file_exists(shot):
		var img := Image.load_from_file(ProjectSettings.globalize_path(shot))
		if img and not img.is_empty():
			files["levelshots/%s.jpg" % base] = img.save_jpg_to_buffer(0.9)
	var zp := ZIPPacker.new()
	var err := zp.open(dest)
	if err != OK:
		return err
	for f in files:
		zp.start_file(f)
		zp.write_file(files[f])
		zp.close_file()
	return zp.close()


## A saved map's thumbnail, beside it.
static func thumb_path(name: String) -> String:
	return maps_dir.path_join(name + ".png")


## Renders the view as the map's thumbnail (512 x 288, as OZMIUM's), the way the game sees it: no
## grid, selection tint or entity markers. Not in headless runs, which draw nothing.
func take_thumbnail(name: String) -> void:
	if DisplayServer.get_name() == "headless" or not is_inside_tree() or _shooting:
		return
	_shooting = true
	# the game's view: caulk and the tool brushes aren't drawn there, sky and fog are, whatever the
	# view filters say
	var user_filters := filters.duplicate()
	var game := {"caulk": true, "tools": true}
	var swap := user_filters != game   # a big map takes a moment to redraw: only when it differs
	if swap:
		filters = game
		mesher.hide_patches = false
		mesher.redraw_all()
	var sv := SubViewport.new()
	sv.size = Vector2i(512, 288)
	sv.world_3d = get_viewport().world_3d
	sv.render_target_update_mode = SubViewport.UPDATE_ONCE
	var cam := Camera3D.new()
	cam.fov = camera.fov
	cam.near = camera.near
	cam.far = camera.far
	cam.cull_mask = 1
	sv.add_child(cam)
	add_child(sv)
	cam.global_transform = camera.global_transform
	if camera.ortho != "" and camera._pose_3d.size():
		cam.global_transform = Transform3D(Basis.from_euler(Vector3(deg_to_rad(camera._pose_3d[2]), deg_to_rad(camera._pose_3d[1]), 0)), camera._pose_3d[0])
	var flat := not mesher.visible
	mesher.visible = true
	cam.current = true
	# the editor's own drawing moves to layer 2 for the shot, and the selection draws as the map
	var moved := {}
	for root in [grid, overlay, ents, wire]:
		for g in root.find_children("*", "GeometryInstance3D", true, false):
			moved[g] = g.layers
			g.layers = 2
		if root is GeometryInstance3D:
			moved[root] = root.layers
			root.layers = 2
	for g in mesher.find_children("*", "MeshInstance3D", true, false):
		if g.material_override == Q3MapMesher.back_material():   # curves' see-through backs (editor only)
			moved[g] = g.layers
			g.layers = 2
	mesher.exclude = {}
	mesher.touch(selection.keys())
	mesher.flush()
	await RenderingServer.frame_post_draw
	await RenderingServer.frame_post_draw
	var img := sv.get_texture().get_image()
	mesher.visible = not flat
	for g in moved:
		if is_instance_valid(g):
			g.layers = moved[g]
	sv.queue_free()
	if swap:
		filters = user_filters
		mesher.hide_patches = filters.has("curves")
		mesher.redraw_all()
	_refresh_selection()
	_shooting = false
	if img and not img.is_empty():
		img.save_png(thumb_path(name))
		thumbnail_saved.emit(name)


## Back from a play-test (F6): the same document (undo history and all), file, view and selection.
func _back_from_playtest(state: Dictionary) -> void:
	_set_doc(state["doc"], String(state.get("path", "")))
	var cam: Array = state.get("camera", [])
	if cam.size() == 3:
		camera.set_pose(cam[0], cam[1], cam[2])
	if String(state.get("view", "3d")) != "3d":
		set_view(String(state["view"]))
	select(Array(state.get("selection", [])).filter(func(i): return doc.objects.has(i)))
	_isolated = Array(state.get("isolated", [])).filter(func(i): return doc.objects.has(i))
	var t := String(state.get("tool", "select"))
	if tools.has(t) and t != "prefab":
		set_tool(t)
	# the face and vertex tools' picks come back after the tool, which drops picks it doesn't own
	var fk: Array = Array(state.get("faces", [])).filter(func(k): return doc.objects.has(k.x))
	if fk.size():
		select_faces(fk)
	for k in state.get("verts", []):
		if doc.objects.has(k.x):
			verts[k] = true
	Session.playtest = {}
	notify("Back from the play-test")

