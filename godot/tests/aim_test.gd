extends Node3D
## Windowed side view of three characters aiming up, level and down (aim_pitch -40, 0, 40):
## the torso and head bend as CG_PlayerAngles turns them. Saves <out>.png.
##   godot --path . res://tests/aim_test.tscn -- out=C:/temp/aim [model=sarge]

var out := "user://aim"
var model := "sarge"
var _t := 0.0


func _ready() -> void:
	Engine.max_fps = 60
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			out = a.substr(4)
		elif a.begins_with("model="):
			model = a.substr(6)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(0.18, 0.2, 0.24)
	env.environment.ambient_light_color = Color.WHITE
	env.environment.ambient_light_energy = 0.6
	add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-40, -30, 0)
	add_child(sun)
	for i in 3:
		var ch := Q3Character.new()
		ch.model = model
		add_child(ch)
		ch.position = Vector3((i - 1) * 1.6, 0, 0)
		ch.rotation_degrees = Vector3(0, 90, 0)   # facing +X: the camera sees them side on
		ch.aim_pitch = [-40.0, 0.0, 40.0][i]
		var gun: Array = Q3.entities.get("weapon_railgun", {}).get("models", [])
		if gun.size():
			ch.attach(gun[0])
	var cam := Camera3D.new()
	cam.position = Vector3(0, 1.0, 5.5)
	add_child(cam)
	cam.make_current()


func _process(delta: float) -> void:
	_t += delta
	if _t > 2.0:
		set_process(false)
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png(out + ".png")
		get_tree().quit()
