extends Node
## Trailer footage (TRAILER_PLAN.md): flies a camera path, tools/trailer/paths/<name>.cam, through
## the remastered map, on the same spline q3cam flies in retail Quake 3 (cg_q3cam.c CG_CamEval,
## cam_path.py evaluate), so every frame lines up with the original's. Windowed (needs the GPU);
## tools/trailer/capture_remaster.py runs it:
##   godot --path . --fixed-fps 60 res://tests/trailer_shot.tscn -- path=q3dm17_flyover
##     out=C:/x/q3dm17_flyover.mp4 ffmpeg=C:/x/ffmpeg.exe [stills=0,72,360 stills_dir=C:/x] [preroll=120]
##     [paths=<folder>] [window=960x540]
## The view renders offscreen at exactly 1920x1080, whatever the window does, and each frame goes
## straight into ffmpeg as raw pixels (no image files). --fixed-fps steps the game 1/60 s a frame
## however long a frame takes, as Quake 3's cl_avidemo does. Holds the first key for `preroll`
## frames (shaders compile, probes and portals settle), then records one frame per path frame,
## saves the `stills` frames as PNG, prints "TRAILER SHOT done <frames>" and quits. Items stand where
## the game drops them, as in tests/levelshots.tscn. The Ultra preset is used for this run only:
## Settings saves to a scratch file, never the player's settings.

const FPS := 60.0
const SIZE := Vector2i(1920, 1080)
const ITEM_PREFIXES := ["item_", "weapon_", "ammo_", "holdable_", "team_CTF_redflag", "team_CTF_blueflag"]
## the root viewport's quality settings (Settings applies them there), copied to ours
const VIEWPORT_PROPS := ["msaa_3d", "screen_space_aa", "use_taa", "use_debanding", "mesh_lod_threshold",
	"anisotropic_filtering_level", "texture_mipmap_bias", "positional_shadow_atlas_size",
	"positional_shadow_atlas_16_bits"]

var paths_dir := ProjectSettings.globalize_path("res://").path_join("../tools/trailer/paths").simplify_path()
var path_name := ""
var preroll := 120
var out_mp4 := ""
var ffmpeg := ""
var stills: PackedInt32Array = []
var stills_dir := ""
var _keys: Array[PackedFloat64Array] = []   ## [time, x, y, z, pitch, yaw, roll, fov]
var _vp: SubViewport
var _map: Q3Map
var _cam: Camera3D
var _pipe: FileAccess
var _pid := -1
var _frame := -1     ## frames processed, preroll included
var _frames := 0     ## frames in the path, both ends included
var _done := false


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			match kv[0]:
				"path": path_name = kv[1]
				"paths": paths_dir = kv[1]
				"preroll": preroll = int(kv[1])
				"out": out_mp4 = kv[1]
				"ffmpeg": ffmpeg = kv[1]
				"stills": stills = PackedInt32Array(Array(kv[1].split(",", false)).map(func(s: String) -> int: return int(s)))
				"stills_dir": stills_dir = kv[1]
	_quality()
	AudioServer.set_bus_mute(0, true)
	var map_name := _load(paths_dir.path_join(path_name + ".cam"))
	var scene := Q3MapBuilder.scene_path(map_name) if map_name != "" else ""
	if scene == "" or not ResourceLoader.exists(scene):
		push_error("trailer_shot: no map scene for %s" % path_name)
		get_tree().quit(1)
		return
	if out_mp4 == "" or ffmpeg == "":
		push_error("trailer_shot: out= and ffmpeg= are needed")
		get_tree().quit(1)
		return
	_frames = roundi((_keys[-1][0] - _keys[0][0]) * FPS) + 1

	_vp = SubViewport.new()
	_vp.size = SIZE
	_vp.own_world_3d = true
	_vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	for p in VIEWPORT_PROPS:
		_vp.set(p, get_tree().root.get(p))
	add_child(_vp)
	var show := TextureRect.new()   # the window shows what's recorded
	show.texture = _vp.get_texture()
	show.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	show.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	show.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(show)

	Diagnostics.expect_stall(true)   # the map loads in one go
	_map = (load(scene) as PackedScene).instantiate() as Q3Map
	_vp.add_child(_map)
	Diagnostics.expect_stall(false)
	_cam = Camera3D.new()
	_cam.keep_aspect = Camera3D.KEEP_WIDTH   # the path's fov is across, as cg_fov at any aspect
	_cam.near = 0.05
	_cam.far = 500.0
	_cam.cull_mask &= ~Q3Portal.MIRROR_LAYER
	_vp.add_child(_cam)
	_cam.make_current()
	_view(_keys[0][0])

	var pipe := OS.execute_with_pipe(ffmpeg, ["-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgba",
		"-s", "%dx%d" % [SIZE.x, SIZE.y], "-r", str(int(FPS)), "-i", "-",
		"-c:v", "libx264", "-preset", "medium", "-crf", "12", "-pix_fmt", "yuv420p", out_mp4])   # blocking: a full pipe waits for ffmpeg
	if pipe.is_empty():
		push_error("trailer_shot: could not start %s" % ffmpeg)
		get_tree().quit(1)
		return
	_pipe = pipe["stdio"]
	_pid = int(pipe["pid"])
	RenderingServer.frame_post_draw.connect(_on_drawn)
	await get_tree().physics_frame
	await get_tree().physics_frame
	_place_items()


## The Ultra preset plus the settings outside it, for this run only.
func _quality() -> void:
	Settings.path = "user://trailer_settings.cfg"
	Settings.set_value("graphics/preset", "ultra", false)
	Settings.set_value("video/render_scale", 1.0, false)
	Settings.set_value("video/upscaler", "bilinear", false)
	for k in ["video/brightness", "video/contrast", "accessibility/colorblind", "accessibility/reduced_flashing"]:
		Settings.set_value(k, Settings.default_of(k), false)


func _process(_delta: float) -> void:
	if _keys.is_empty() or _cam == null or _done:
		return
	_frame += 1
	_view(_keys[0][0] + maxf(_frame - preroll, 0) / FPS)


## After each frame is drawn: past the preroll, its pixels go to ffmpeg.
func _on_drawn() -> void:
	var f := _frame - preroll
	if _done or _frame < 0 or f < 0:
		return
	var img := _vp.get_texture().get_image()
	if img.get_format() != Image.FORMAT_RGBA8:
		img.convert(Image.FORMAT_RGBA8)
	_pipe.store_buffer(img.get_data())
	if f in stills and stills_dir != "":
		img.save_png(stills_dir.path_join("%s_f%05d.png" % [path_name, f]))
	if f >= _frames - 1:
		_finish()


func _finish() -> void:
	_done = true
	RenderingServer.frame_post_draw.disconnect(_on_drawn)
	_pipe.close()   # end of input: ffmpeg finishes the file
	Diagnostics.expect_stall(true)
	while OS.is_process_running(_pid):
		OS.delay_msec(50)
	Diagnostics.expect_stall(false)
	print("TRAILER SHOT done %d" % _frames)
	get_tree().quit()


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
	var space := _vp.find_world_3d().direct_space_state
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
