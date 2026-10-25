extends Node
## Renders the showcase and saves screenshots, then quits. Windowed (needs a GPU):
##   godot --path . res://tests/capture.tscn -- out=C:/temp/shot frames=240 every=90 count=3
## Writes <out>_0.png, <out>_1.png ... `every` frames apart, starting after `frames`.
## view=fx frames the effects range; fire=<effect> fires it `lead` frames before each shot
## (effects: rocket rail plasma lightning grenade bfg bullets gibs teleport).
## cam= places a camera in scenes without one (maps): cam=intermission, cam=spawn:<n>
## (info_player_deathmatch n), or cam=x,y,z,lx,ly,lz in Godot meters (position, look-at point).
## probes=0 / shafts=0 remove a map's reflection probes or light shafts (Q3Map "Probes", "ShaftSun")
## before the first frame, for comparisons. heat=0 turns Graphics > Heat shimmer off for the run.
## tonemap=filmic|agx|aces sets the tone mapper of every WorldEnvironment in the scene.
## screen=1 grabs the window from the screen instead of the viewport, so native overlays such as
## the main menu's web view are in the picture (keep the window uncovered while it runs).

var out := "user://capture"
var frames := 240
var every := 90
var count := 3
var view := ""
var fire := ""
var lead := 8
var screen := false
var probes := true
var shafts := true
var tonemap := ""   ## filmic / agx / aces: every WorldEnvironment in the scene, for comparisons
const SHOWCASE := "res://showcase/showcase.tscn"
var cam := ""
var scene := SHOWCASE   ## scene=res://showcase/materials.tscn to capture the material browser
var classic := ""       ## classic=<map>: a Maps folder map drawn from its .bsp (Q3ClassicMap), not a scene
var _n := 0
var _shot := 0
var _show   ## the scene under capture (the showcase is a Node3D, the menu a Node)


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			match kv[0]:
				"out": out = kv[1]
				"frames": frames = int(kv[1])
				"every": every = int(kv[1])
				"count": count = int(kv[1])
				"view": view = kv[1]
				"fire": fire = kv[1]
				"lead": lead = int(kv[1])
				"scene": scene = kv[1]
				"cam": cam = kv[1]
				"probes": probes = kv[1] != "0"
				"shafts": shafts = kv[1] != "0"
				"heat": Settings.set_value("graphics/heat_shimmer", kv[1] != "0", false)   # this run only, not saved
				"tonemap": tonemap = kv[1]
				"classic": classic = kv[1]
	Engine.max_fps = 60   # frames= counts real time, whatever the vsync setting
	if classic != "":
		var t := Time.get_ticks_msec()
		_show = Q3ClassicMap.build(classic)
		if _show == null:
			push_error("capture: no classic map %s" % classic)
			get_tree().quit(1)
			return
		scene = "classic"
		print("classic %s built in %d ms" % [classic, Time.get_ticks_msec() - t])
	else:
		_show = load(scene).instantiate()
	if screen:
		# bring the window forward (always_on_top would stop the web view repainting)
		DisplayServer.window_move_to_foreground()
	add_child(_show)
	if not probes and _show.has_node("Probes"):
		_show.get_node("Probes").free()
	if tonemap != "":
		var mode: int = {"filmic": Environment.TONE_MAPPER_FILMIC, "agx": Environment.TONE_MAPPER_AGX,
			"aces": Environment.TONE_MAPPER_ACES}.get(tonemap, Environment.TONE_MAPPER_FILMIC)
		for we in _show.find_children("*", "WorldEnvironment", true, false):
			(we as WorldEnvironment).environment.tonemap_mode = mode as Environment.ToneMapper
	if not shafts and _show.has_node("ShaftSun"):
		_show.get_node("ShaftSun").free()
		(_show.get_node("WorldEnvironment") as WorldEnvironment).environment.volumetric_fog_density = 0.0
	if cam != "":
		_place_camera()
	if scene != SHOWCASE:
		return  # other scenes read their own options (page=, filter=, sky=) from the same args
	if fire != "":
		_show.fx_loop = false
	if view == "fx":
		_show.cam.position = _show.fx_origin + Vector3(-3.0, 2.0, 5.5)
		_show.cam.look_at(_show.fx_origin + Vector3(1.5, 1.2, 0))


func _process(_d: float) -> void:
	_n += 1
	if fire != "" and scene == SHOWCASE and _n >= frames - lead and (_n - (frames - lead)) % every == 0:
		_show._fire(fire)
	if _n >= frames and (_n - frames) % every == 0:
		var img := get_viewport().get_texture().get_image()
		if screen:
			var w := get_window()
			img = DisplayServer.screen_get_image(w.current_screen)
			img = img.get_region(Rect2i(w.position - DisplayServer.screen_get_position(w.current_screen), w.size))
		var path := "%s_%d.png" % [out, _shot]
		img.save_png(path)
		print("saved ", path)
		_shot += 1
		if _shot >= count:
			get_tree().quit()


func _place_camera() -> void:
	var c := Camera3D.new()
	c.fov = 74.0   # Quake 3's 90 degrees horizontal at 4:3 is about 74 vertical
	c.far = 400.0
	add_child(c)
	if cam == "intermission" and _show.has_method("intermission_transform"):
		c.global_transform = _show.intermission_transform()
	elif cam.begins_with("spawn:") and _show.has_method("entities_of"):
		var spawns: Array = _show.entities_of("info_player_deathmatch")
		if spawns.size():
			c.global_transform = _show.view_transform(spawns[int(cam.get_slice(":", 1)) % spawns.size()])
	else:
		var v := cam.split_floats(",", false)
		if v.size() >= 6:
			c.look_at_from_position(Vector3(v[0], v[1], v[2]), Vector3(v[3], v[4], v[5]))
	c.make_current()
