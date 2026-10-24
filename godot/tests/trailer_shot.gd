extends Node
## Trailer footage (TRAILER_PLAN.md): flies a camera path, tools/trailer/paths/<name>.cam, through
## the remastered map, on the same spline q3cam flies in retail Quake 3 (cg_q3cam.c CG_CamEval,
## cam_path.py evaluate), so every frame lines up with the original's. Windowed (needs the GPU);
## tools/trailer/capture_remaster.py runs it:
##   godot --path . --fixed-fps 60 res://tests/trailer_shot.tscn -- path=q3dm17_flyover
##     out=C:/x/q3dm17_flyover.mp4 ffmpeg=C:/x/ffmpeg.exe [stills=0,72,360 stills_dir=C:/x] [preroll=120]
##     [paths=<folder>] [window=960x540]
## tests/trailer_capture.gd records it (offscreen 1920x1080, straight into ffmpeg, the Ultra preset
## for this run only). The first key holds through the preroll, then one frame per path frame.
## Items stand where the game drops them, as in tests/levelshots.tscn.

const FPS := 60.0
const ITEM_PREFIXES := ["item_", "weapon_", "ammo_", "holdable_", "team_CTF_redflag", "team_CTF_blueflag"]
const Capture := preload("res://tests/trailer_capture.gd")

var paths_dir := ProjectSettings.globalize_path("res://").path_join("../tools/trailer/paths").simplify_path()
var path_name := ""
var _keys: Array[PackedFloat64Array] = []   ## [time, x, y, z, pitch, yaw, roll, fov]
var _rec: Capture
var _map: Q3Map
var _cam: Camera3D


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			match kv[0]:
				"path": path_name = kv[1]
				"paths": paths_dir = kv[1]
	Capture.quality()
	_rec = Capture.new()
	add_child(_rec)
	_rec.read_args()
	_rec.tag = path_name
	var map_name := _load(paths_dir.path_join(path_name + ".cam"))
	var scene := Q3MapBuilder.scene_path(map_name) if map_name != "" else ""
	if scene == "" or not ResourceLoader.exists(scene):
		push_error("trailer_shot: no map scene for %s" % path_name)
		get_tree().quit(1)
		return
	_rec.frames = roundi((_keys[-1][0] - _keys[0][0]) * FPS) + 1
	var vp := _rec.make_viewport()
	Diagnostics.expect_stall(true)   # the map loads in one go
	_map = (load(scene) as PackedScene).instantiate() as Q3Map
	vp.add_child(_map)
	Diagnostics.expect_stall(false)
	_cam = Camera3D.new()
	_cam.keep_aspect = Camera3D.KEEP_WIDTH   # the path's fov is across, as cg_fov at any aspect
	_cam.near = 0.05
	_cam.far = 500.0
	_cam.cull_mask &= ~Q3Portal.MIRROR_LAYER
	vp.add_child(_cam)
	_cam.make_current()
	_view(_keys[0][0])
	if not _rec.begin():
		get_tree().quit(1)
		return
	await get_tree().physics_frame
	await get_tree().physics_frame
	_place_items()


func _process(_delta: float) -> void:
	if _keys.is_empty() or _cam == null:
		return
	var f := _rec.tick()
	_view(_keys[0][0] + maxf(f, 0) / FPS)


## Reads a .cam file: "// map <name>" first, then one key per line, later in time each.
## Returns the map, or "".
func _load(file: String) -> String:
	var text := FileAccess.get_file_as_string(file)
	if text == "":
		push_error("trailer_shot: %s not found" % file)
		return ""
	var lines := text.split("\n")
	var head := lines[0].strip_edges().trim_prefix("//").replace("\t", " ").split(" ", false)
	if not lines[0].strip_edges().begins_with("//") or head.size() < 2 or head[0] != "map":
		push_error("trailer_shot: %s: the first line must be // map <name>" % file)
		return ""
	for line in lines:
		var body := line.get_slice("//", 0).replace("\t", " ").strip_edges()
		if body == "":
			continue
		var v := PackedFloat64Array(body.split_floats(" ", false))
		if v.size() != 8:
			push_error("trailer_shot: %s: a key needs 8 numbers: %s" % [file, body])
			return ""
		if not _keys.is_empty() and v[0] <= _keys[-1][0]:
			push_error("trailer_shot: %s: a key is not later than the one before: %s" % [file, body])
			return ""
		_keys.append(v)
	if _keys.is_empty():
		push_error("trailer_shot: %s has no keys" % file)
		return ""
	return head[1]


## Catmull-Rom tangent of channel c at key i, flat at both ends.
func _tangent(i: int, c: int) -> float:
	if i <= 0 or i >= _keys.size() - 1:
		return 0.0
	return (_keys[i + 1][c] - _keys[i - 1][c]) / (_keys[i + 1][0] - _keys[i - 1][0])


## The path at time t: [x, y, z, pitch, yaw, roll, fov], as CG_CamEval.
func _eval(t: float) -> PackedFloat64Array:
	if _keys.size() == 1 or t <= _keys[0][0]:
		return _keys[0].slice(1)
	if t >= _keys[-1][0]:
		return _keys[-1].slice(1)
	var i := 0
	while i < _keys.size() - 2 and t >= _keys[i + 1][0]:
		i += 1
	var a := _keys[i]
	var b := _keys[i + 1]
	var dt := b[0] - a[0]
	var u := (t - a[0]) / dt
	var u2 := u * u
	var u3 := u2 * u
	var out := PackedFloat64Array()
	out.resize(7)
	for c in range(1, 8):
		out[c - 1] = (2.0 * u3 - 3.0 * u2 + 1.0) * a[c] + (u3 - 2.0 * u2 + u) * dt * _tangent(i, c) \
			+ (-2.0 * u3 + 3.0 * u2) * b[c] + (u3 - u2) * dt * _tangent(i + 1, c)
	return out


func _view(t: float) -> void:
	var v := _eval(t)
	_cam.global_position = Q3.q3_to_godot(Vector3(v[0], v[1], v[2]))
	_cam.basis = Q3Client.angles_basis(Vector3(v[3], v[4], v[5])) * Basis(Vector3.UP, PI)
	_cam.fov = v[6]


## Items where the game puts them: dropped to the floor (G_SpawnItem, FinishSpawningItem's trace
## with the item's 15-unit box) unless suspended (spawnflags 1). Same as tests/levelshots.gd.
func _place_items() -> void:
	var space := _rec.vp.find_world_3d().direct_space_state
	for marker in _map.get_node("Entities").get_children():
		var e: Dictionary = marker.get_meta("entity", {})
		var cls := String(e.get("classname", ""))
		if not ITEM_PREFIXES.any(func(p: String) -> bool: return cls.begins_with(p)) or not Q3.entities.has(cls):
			continue
		var p := Q3Pickup.new()
		p.classname = cls
		_map.add_child(p)
		var at: Vector3 = (marker as Node3D).global_position
		if not (int(e.get("spawnflags", "0")) & 1):
			var q := PhysicsRayQueryParameters3D.create(at + Vector3.UP * 0.05, at + Vector3.DOWN * 128.0)
			var hit := space.intersect_ray(q)
			if hit:
				at = hit["position"] + Vector3.UP * 15.0 * Q3.UNIT
		p.global_position = at
