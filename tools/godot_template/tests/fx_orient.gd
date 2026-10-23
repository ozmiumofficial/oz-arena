extends Node3D
## Windowed check that impact effects lie on the surface they hit: rail, plasma and bullet impacts
## on a floor, a side wall and a facing wall. Saves <out>.png.
##   godot --path . res://tests/fx_orient.tscn -- out=C:/temp/orient

var out := "user://orient"
var _t := 0.0
var _saved := false


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			out = a.substr(4)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(0.15, 0.16, 0.18)
	add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-45, 30, 0)
	add_child(sun)
	# floor, a wall on the left (normal +X), a wall ahead (normal +Z)
	for w in [[Vector3(0, -0.05, 0), Vector3(8, 0.1, 8)], [Vector3(-2.05, 1.5, 0), Vector3(0.1, 3, 8)], [Vector3(0, 1.5, -2.05), Vector3(8, 3, 0.1)]]:
		var mi := MeshInstance3D.new()
		var bm := BoxMesh.new()
		bm.size = w[1]
		mi.mesh = bm
		mi.position = w[0]
		add_child(mi)
	var cam := Camera3D.new()
	cam.position = Vector3(1.6, 1.6, 2.6)
	add_child(cam)
	cam.look_at(Vector3(-0.8, 0.8, -1.0))


func _process(delta: float) -> void:
	_t += delta
	if _t > 0.5 and _t - delta <= 0.5:
		for k in [["rail", 0.0], ["plasma", 0.8], ["bullet", 1.6]]:
			Q3FX.explosion(self, k[0], Vector3(-1.2 + k[1], 0.0, 0.2), Vector3.UP)
			Q3FX.explosion(self, k[0], Vector3(-2.0, 0.6 + k[1], 0.6), Vector3.RIGHT)
			Q3FX.explosion(self, k[0], Vector3(-1.2 + k[1], 1.2, -2.0), Vector3.BACK)
	if _t > 0.62 and not _saved:
		_saved = true
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png(out + ".png")
		print("ORIENT done")
		get_tree().quit()
