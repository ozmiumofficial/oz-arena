extends Node
## GPU cost of each graphics setting on one map, from fixed views so runs compare: the map with a
## few bodies standing at every view, each config held `frames` frames per view, the median GPU and
## render-thread time printed per config. Windowed (needs a GPU):
##   godot --path . res://tests/gpu_bench.tscn -- map=q3dm6 views=6 frames=90
## Configs are Settings overrides (this run only) plus a few map experiments (lamps=off hides the
## map's baked lamps, which still light bodies and the mist at run time).

var map := "q3dm6"
var views := 6
var frames := 90
var configs := [
	["base", {}],
	["fog off", {"graphics/volumetric_fog": false}],
	["ssr off", {"graphics/reflections": false}],
	["ao off", {"graphics/ao": "off"}],
	["bloom off", {"graphics/bloom": false}],
	["fxaa", {"graphics/antialiasing": "fxaa"}],
	["msaa2", {"graphics/antialiasing": "msaa2"}],
	["shadows low", {"graphics/shadows": "low"}],
	["lamps off", {"lamps": false}],
	["range 0.5", {"range": 0.5}],
	["range 0.35", {"range": 0.35}],
	["fade 30m", {"fade": 30.0}],
	["fade 20m", {"fade": 20.0}],
	["r0.5+fade30", {"range": 0.5, "fade": 30.0}],
	["lamp fog 0", {"lampfog": 0.0}],
	["r0.5 fog x2", {"range": 0.5, "lampfog": 2.0}],
	["r0.5 fog x1.6", {"range": 0.5, "lampfog": 1.6}],
	["r0.5 fog x2.5", {"range": 0.5, "lampfog": 2.5}],
	["r0.6 fog x1.8", {"range": 0.6, "lampfog": 1.8}],
	["r0.4 fog x2.5", {"range": 0.4, "lampfog": 2.5}],
	["scale 0.8", {"video/render_scale": 0.8}],
]
var _fog := {}   ## lamp -> its share of the mist (Q3Map)
var _ranges := {}   ## lamp -> its baked range
var shots := ""     ## shots=<folder>: each config's last frame at each view, for comparing the look
var only: Array = []   ## only=base,range_0.5: just these configs
var _show: Node3D
var _cam: Camera3D
var _vp: RID
var _spots: Array = []
var _saved := {}


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			match kv[0]:
				"map": map = kv[1]
				"views": views = int(kv[1])
				"frames": frames = int(kv[1])
				"shots": shots = kv[1]
				"only": only = Array(kv[1].split(","))
	if not only.is_empty():
		configs = configs.filter(func(c): return c[0].replace(" ", "_") in only)
	Engine.max_fps = 0
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	_show = load("res://library/maps/%s/%s.tscn" % [map, map]).instantiate()
	add_child(_show)
	_cam = Camera3D.new()
	_cam.fov = 74.0
	_cam.far = 400.0
	add_child(_cam)
	_cam.make_current()
	var spawns: Array = _show.entities_of("info_player_deathmatch")
	for i in mini(views, spawns.size()):
		_spots.append(_show.view_transform(spawns[i * spawns.size() / mini(views, spawns.size())]))
	# bodies in view, as in a match
	for i in _spots.size():
		var t: Transform3D = _spots[i]
		var fwd := -t.basis.z
		fwd.y = 0.0
		for j in 2:
			var ch := Q3Character.new()
			ch.model = ["sarge", "visor"][j]
			add_child(ch)
			ch.global_position = t.origin + fwd.normalized() * (3.0 + j * 2.0) - Vector3.UP * 50.0 * Q3.UNIT
	for k in ["graphics/volumetric_fog", "graphics/reflections", "graphics/ao", "graphics/bloom",
			"graphics/antialiasing", "graphics/shadows", "video/render_scale"]:
		_saved[k] = Settings.get_value(k)
	_vp = get_viewport().get_viewport_rid()
	RenderingServer.viewport_set_measure_render_time(_vp, true)
	await _run()
	get_tree().quit(0)


func _apply(over: Dictionary) -> void:
	for k in _saved:
		Settings.set_value(k, over.get(k, _saved[k]), false)
	for l in _show._lamps:
		var light := l as Light3D
		light.visible = over.get("lamps", true)
		if not _ranges.has(light):
			_ranges[light] = light.get("omni_range") if light is OmniLight3D else light.get("spot_range")
		light.set("omni_range" if light is OmniLight3D else "spot_range", _ranges[light] * float(over.get("range", 1.0)))
		if not _fog.has(light):
			_fog[light] = light.light_volumetric_fog_energy
		light.light_volumetric_fog_energy = _fog[light] * float(over.get("lampfog", 1.0))
		light.distance_fade_enabled = over.has("fade")
		light.distance_fade_begin = float(over.get("fade", 40.0))
		light.distance_fade_length = 5.0


func _run() -> void:
	var base := 0.0
	for c in configs:
		_apply(c[1])
		var gpu: Array = []
		var cpu: Array = []
		for t in _spots:
			_cam.global_transform = t
			for f in frames:
				await get_tree().process_frame
				if shots != "" and f == frames - 1:
					get_viewport().get_texture().get_image().save_png("%s/%s_%d.png" % [shots, String(c[0]).replace(" ", "_"), _spots.find(t)])
				if f >= 20:   # settle: shader compiles, temporal effects
					gpu.append(RenderingServer.viewport_get_measured_render_time_gpu(_vp))
					cpu.append(RenderingServer.viewport_get_measured_render_time_cpu(_vp) + RenderingServer.get_frame_setup_time_cpu())
		gpu.sort()
		cpu.sort()
		var g: float = gpu[gpu.size() / 2]
		if c[0] == "base":
			base = g
		print("GPU %-12s gpu %.2f ms (%+.2f)  p90 %.2f  render cpu %.2f" % [c[0], g, g - base, gpu[int(gpu.size() * 0.9)], cpu[cpu.size() / 2]])
	_apply({})
