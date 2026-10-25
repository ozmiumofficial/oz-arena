extends Node3D
## Windowed look at the remaster's glow beam (Q3FX.glow_beam) alone: one beam seen from the side and
## one starting at the eye, as the shooter sees their own rail.
##   godot --path . res://tests/glow_beam_shot.tscn -- out=C:/temp/beam.png

var out := "user://glow_beam.png"
var _frames := 0


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			out = a.substr(4)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(0.08, 0.08, 0.1)
	add_child(env)
	var floor_mesh := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(40, 40)
	floor_mesh.mesh = pm
	floor_mesh.position = Vector3(0, -1.5, 0)
	add_child(floor_mesh)
	var cam := Camera3D.new()
	add_child(cam)
	cam.position = Vector3(0, 0, 0)
	cam.current = true
	var side := Q3FxTimed.new()
	side.life = 1.0e6
	add_child(side)
	side.materials.append(Q3FX.glow_beam(side, Vector3(-6, 1, -8), Vector3(6, 1, -8), 14.0, Color.RED))
	var own := Q3FxTimed.new()
	own.life = 1.0e6
	add_child(own)
	own.materials.append(Q3FX.glow_beam(own, Vector3(0.3, -0.4, -0.5), Vector3(-2, 0, -20), 14.0, Color(0.3, 0.6, 1.0)))


func _process(_d: float) -> void:
	_frames += 1
	if _frames == 6:
		get_viewport().get_texture().get_image().save_png(out)
		print("GLOW BEAM saved ", out)
		get_tree().quit()
