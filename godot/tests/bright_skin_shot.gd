extends Node
## Gameplay > Bright skins in a lit map: three bodies in the enemy color in front of a spawn point,
## next to one in its own skin. Windowed (needs a GPU):
##   godot --path . res://tests/bright_skin_shot.tscn -- map=q3dm6 spawn=0 out=C:/temp/bright.png
##   models=sarge,visor,hunter

var map := "q3dm6"
var spawn := 0
var out := "user://bright.png"
var models := ["sarge", "visor", "hunter", "grunt"]
var _n := 0


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			match kv[0]:
				"map": map = kv[1]
				"spawn": spawn = int(kv[1])
				"out": out = kv[1]
				"models": models = Array(kv[1].split(","))
	Engine.max_fps = 60
	var show: Node3D = load("res://library/maps/%s/%s.tscn" % [map, map]).instantiate()
	add_child(show)
	var c := Camera3D.new()
	c.fov = 74.0
	c.far = 400.0
	add_child(c)
	var spawns: Array = show.entities_of("info_player_deathmatch")
	c.global_transform = show.view_transform(spawns[spawn % spawns.size()])
	c.make_current()
	var fwd := -c.global_basis.z
	fwd.y = 0.0
	fwd = fwd.normalized()
	var right := fwd.cross(Vector3.UP)
	var feet := c.global_position - Vector3.UP * 50.0 * Q3.UNIT
	var color: Color = Settings.get_value("gameplay/enemy_color")
	for i in models.size():
		var ch := Q3Character.new()
		ch.model = models[i]
		add_child(ch)
		ch.global_position = feet + fwd * 3.2 + right * (float(i) - (models.size() - 1) * 0.5) * 1.1
		ch.look_at(ch.global_position - fwd, Vector3.UP)
		ch.bright_skin(color if i < models.size() - 1 else Color(0, 0, 0, 0))   # the last in its own skin


func _process(_delta: float) -> void:
	_n += 1
	if _n == 90:
		var img := get_viewport().get_texture().get_image()
		img.save_png(out)
		print("saved ", out)
		get_tree().quit()
