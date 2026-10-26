extends Node
## A/B of Q3Client's player fill lights on one fixed spot: a Sarge stood 2.5 m in front of a map's
## spawn camera, shot with the fill lights off then on. Saves <out>_off.png / <out>_on.png and prints
## the mean brightness of the body's patch of the picture. Windowed.
##   godot --path . res://tests/fill_ab.tscn -- out=C:/temp/fill map=q3dm1 spawn=3 fill=0.35

var out := "user://fill"
var map := "q3dm1"
var spawn := 0
var ent := "info_player_deathmatch"   ## ent=weapon_rocketlauncher stands the player at an item instead
var fill := 0.35
var _cam: Camera3D
var _lights: Array[DirectionalLight3D] = []
var _step := 0
var _frames := 0
var _off_lum := 0.0


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			out = a.substr(4)
		elif a.begins_with("map="):
			map = a.substr(4)
		elif a.begins_with("ent="):
			ent = a.substr(4)
		elif a.begins_with("spawn="):
			spawn = int(a.substr(6))
		elif a.begins_with("fill="):
			fill = float(a.substr(5))
	var show: Node = load("res://library/maps/%s/%s.tscn" % [map, map]).instantiate()
	add_child(show)
	var spawns: Array = show.entities_of(ent)
	var t: Transform3D = show.view_transform(spawns[spawn % spawns.size()])
	_cam = Camera3D.new()
	_cam.fov = 74.0
	add_child(_cam)
	_cam.global_transform = t
	_cam.make_current()
	var fwd := -t.basis.z
	fwd.y = 0.0
	fwd = fwd.normalized()
	var ch := Q3Character.new()
	ch.model = "sarge"
	add_child(ch)
	ch.global_position = t.origin + fwd * 2.5 - Vector3.UP * 1.55   # eye height is about 1.55 m above the feet
	ch.rotation.y = atan2(-fwd.x, -fwd.z) + PI
	for yaw in [35.0, 215.0]:
		var l := DirectionalLight3D.new()
		l.light_cull_mask = Q3Character.FILL_LAYER
		l.light_energy = fill
		l.light_color = Color(0.9, 0.93, 1.0)
		l.light_bake_mode = Light3D.BAKE_DISABLED
		l.light_volumetric_fog_energy = 0.0
		l.rotation_degrees = Vector3(-40.0, yaw, 0.0)
		l.visible = false
		add_child(l)
		_lights.append(l)


func _process(_d: float) -> void:
	_frames += 1
	if _frames < 90 + _step * 40:
		return
	await RenderingServer.frame_post_draw
	var img := get_viewport().get_texture().get_image()
	var w := img.get_width()
	var h := img.get_height()
	var sum := 0.0
	var n := 0
	for y in range(int(h * 0.3), int(h * 0.8), 4):
		for x in range(int(w * 0.45), int(w * 0.55), 4):
			sum += img.get_pixel(x, y).get_luminance()
			n += 1
	var lum := sum / maxf(n, 1)
	if _step == 0:
		img.save_png(out + "_off.png")
		_off_lum = lum
		for l in _lights:
			l.visible = true
	else:
		img.save_png(out + "_on.png")
		print("FILL AB %s spawn %d: body patch brightness off %.3f, on %.3f" % [map, spawn, _off_lum, lum])
		get_tree().quit()
	_step += 1
