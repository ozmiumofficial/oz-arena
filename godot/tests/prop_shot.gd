extends Node3D
## Close-up of one model under a plain studio light, saved as a PNG, then quits. Windowed (GPU):
##   godot --path . res://tests/prop_shot.tscn -- model=res://library/props/weapons2/rocketl/rocketl.glb
##       out=C:/temp/rocketl.png yaw=35 pitch=-15 zoom=1.0 size=1600x1000
## For checking how textures sit on the meshes (the subdivided UVs, 2026-10-23).

var model := "res://library/props/weapons2/rocketl/rocketl.glb"
var out := "user://prop_shot.png"
var yaw := 35.0
var pitch := -15.0
var zoom := 1.0
var _n := 0


func _ready() -> void:
	var size := Vector2i(1600, 1000)
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() != 2:
			continue
		match kv[0]:
			"model": model = kv[1]
			"out": out = kv[1]
			"yaw": yaw = float(kv[1])
			"pitch": pitch = float(kv[1])
			"zoom": zoom = float(kv[1])
			"size":
				var wh := kv[1].split("x")
				size = Vector2i(int(wh[0]), int(wh[1]))
	get_window().size = size
	Engine.max_fps = 60
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.16, 0.16, 0.18)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.55, 0.55, 0.6)
	env.ambient_light_energy = 0.8
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-40, 30, 0)
	sun.light_energy = 1.6
	add_child(sun)
	var body: Node3D = load(model).instantiate()
	add_child(body)
	var box := AABB()
	var first := true
	for m in body.find_children("*", "MeshInstance3D", true, false):
		var b: AABB = m.global_transform * m.get_aabb()
		box = b if first else box.merge(b)
		first = false
	var cam := Camera3D.new()
	cam.fov = 30.0
	add_child(cam)
	var r := box.size.length() * 0.5
	var dir := Basis.from_euler(Vector3(deg_to_rad(pitch), deg_to_rad(yaw), 0)) * Vector3(0, 0, 1)
	cam.position = box.get_center() + dir * r / tan(deg_to_rad(cam.fov * 0.5)) / zoom
	cam.look_at(box.get_center())
	cam.near = 0.01


func _process(_d: float) -> void:
	_n += 1
	if _n == 30:
		get_viewport().get_texture().get_image().save_png(out)
		print("prop_shot: ", out)
		get_tree().quit()
