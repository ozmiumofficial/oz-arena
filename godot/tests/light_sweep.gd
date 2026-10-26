extends Node
## Whether a map's lighting changes with where you look (windowed, needs a GPU):
##   godot --path . res://tests/light_sweep.tscn -- classic=<map> | scene=res://library/maps/<m>/<m>.tscn
##     [spawn=<n>] [step=15] [preset=high] [off=ssr,ssao,ssil,sdfgi,glow,fog,adjust] [out=C:/temp/sweep]
## Stands at a spawn point and turns on the spot in `step` degree steps. World points seen in one
## frame are found again in the next (a ray against the map's collision) and their colors compared:
## lighting that belongs to the world gives the same color from both views. Prints each turn's mean
## and worst change of the same points (0-255) and the frame's mean brightness.

var classic := ""
var scene := ""
var spawn := 0
var step := 15.0
var off: PackedStringArray = []
var out := ""
var _map: Node3D
var _cam: Camera3D
var _yaw := 0.0
var _n := 0
var _prev: Array = []   ## [world point, color] of the last frame
var _rows: Array = []
const SETTLE := 20      ## frames each view is held before it is read (temporal effects settle)
const GRID := 24


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() != 2:
			continue
		match kv[0]:
			"classic": classic = kv[1]
			"scene": scene = kv[1]
			"spawn": spawn = int(kv[1])
			"step": step = float(kv[1])
			"off": off = kv[1].split(",")
			"out": out = kv[1]
			"draw": get_viewport().debug_draw = {"lighting": Viewport.DEBUG_DRAW_LIGHTING,
				"unshaded": Viewport.DEBUG_DRAW_UNSHADED}.get(kv[1], Viewport.DEBUG_DRAW_DISABLED)
			"preset": Settings.set_value("graphics/preset", kv[1], false)
	_map = Q3ClassicMap.build(classic) if classic != "" else load(scene).instantiate()
	add_child(_map)
	var env := (_map.get_node("WorldEnvironment") as WorldEnvironment).environment
	for f in off:
		match f:
			"ssr": env.ssr_enabled = false
			"ssao": env.ssao_enabled = false
			"ssil": env.ssil_enabled = false
			"sdfgi": env.sdfgi_enabled = false
			"glow": env.glow_enabled = false
			"fog": env.volumetric_fog_enabled = false
			"adjust": env.adjustment_enabled = false
			"sky_refl": env.reflected_light_source = Environment.REFLECTION_SOURCE_DISABLED
	print("env: ssr=%s ssao=%s ssil=%s sdfgi=%s glow=%s fog=%s ambient=%s %s" % [env.ssr_enabled, env.ssao_enabled,
		env.ssil_enabled, env.sdfgi_enabled, env.glow_enabled, env.volumetric_fog_enabled,
		env.ambient_light_source, env.ambient_light_color])
	_cam = Camera3D.new()
	_cam.fov = 74.0
	_cam.far = 400.0
	add_child(_cam)
	var spawns: Array = _map.entities_of("info_player_deathmatch")
	_cam.global_transform = _map.view_transform(spawns[spawn % spawns.size()])


func _process(_d: float) -> void:
	_n += 1
	if _n % SETTLE != 0:
		return
	var img := get_viewport().get_texture().get_image()
	if out != "":
		img.save_png("%s_%03d.png" % [out, int(_yaw)])
	var size := Vector2(img.get_size())
	# lighting, not texture detail: each sample is the mean of the 16x16 block around it
	img.convert(Image.FORMAT_RGBA8)
	for i in 4:
		img.shrink_x2()
	var to_small := Vector2(img.get_size()) / size
	var space := _cam.get_world_3d().direct_space_state
	# the last frame's points, seen again from here
	var diffs := PackedFloat32Array()
	for pc in _prev:
		var p: Vector3 = pc[0]
		if _cam.is_position_behind(p):
			continue
		var s := _cam.unproject_position(p)
		if s.x < 2 or s.y < 2 or s.x > size.x - 3 or s.y > size.y - 3:
			continue
		if not _visible(space, p):
			continue
		var c := img.get_pixelv(Vector2i(s * to_small))
		diffs.append(absf(_lum(c) - _lum(pc[1])) * 255.0)
	# this frame's points
	_prev.clear()
	var mean_lum := 0.0
	for gy in GRID:
		for gx in GRID:
			var s := Vector2((gx + 0.5) / GRID * size.x, (gy + 0.5) / GRID * size.y)
			var from := _cam.project_ray_origin(s)
			var q := PhysicsRayQueryParameters3D.create(from, from + _cam.project_ray_normal(s) * 300.0)
			var hit := space.intersect_ray(q)
			var c := img.get_pixelv(Vector2i(s * to_small))
			mean_lum += _lum(c)
			if hit:
				_prev.append([hit["position"] + hit["normal"] * 0.01, c])
	mean_lum /= GRID * GRID
	if diffs.size():
		var sorted := diffs.duplicate()
		sorted.sort()
		var mean := 0.0
		for d in diffs:
			mean += d
		mean /= diffs.size()
		_rows.append([_yaw, mean, sorted[int(sorted.size() * 0.95)], diffs.size()])
		print("yaw %3d  same points: mean change %5.1f  95th %5.1f  (n=%d)   frame brightness %.1f" % [int(_yaw), mean,
			sorted[int(sorted.size() * 0.95)], diffs.size(), mean_lum * 255.0])
	_yaw += step
	if _yaw >= 360.0 + step:
		var all := 0.0
		var worst := 0.0
		for r in _rows:
			all += r[1]
			worst = maxf(worst, r[2])
		print("SWEEP mean change %.1f, worst 95th %.1f over %d turns" % [all / maxf(_rows.size(), 1), worst, _rows.size()])
		get_tree().quit()
		return
	_cam.rotate_y(deg_to_rad(-step))


func _visible(space: PhysicsDirectSpaceState3D, p: Vector3) -> bool:
	var q := PhysicsRayQueryParameters3D.create(_cam.global_position, p)
	var hit := space.intersect_ray(q)
	return hit.is_empty() or (hit["position"] as Vector3).distance_to(p) < 0.05


static func _lum(c: Color) -> float:
	return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b
