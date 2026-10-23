extends Node
## Windowed look at the view railgun's charge glow: ready (breathing), a shot, the recharge through
## magenta, the ready flash. Saves <out>_<n>.png (the gun's corner of the screen).
##   godot --path . res://tests/railgun_test.tscn -- out=C:/temp/rail

const AT := [4.0, 4.6, 5.05, 5.45, 5.9, 6.3, 6.55, 7.5]   # the shot is at 5.0 s
var out := "user://rail"
var _game: Q3Gameplay
var _t := 0.0
var _shot := 0
var _paint := false       ## paint=1: each surface a flat color (which part is which)


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			out = a.substr(4)
		if a == "paint=1":
			_paint = true
	Engine.max_fps = 60
	var s := Session.new_setup("ffa", "q3dm17")
	s.set_meta("devmap", true)
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	_game.test_input = true
	add_child(_game)


func _process(delta: float) -> void:
	_t += delta
	if _t > 2.0 and _t - delta <= 2.0:
		Q3Server.client_command(_game.me, "give all")
	if _t > 2.5:
		_game._select_weapon(7)
		_game._view.x = 30.0
	if _paint and _t > 3.5 and _game._gun:
		_paint = false
		var cols := [Color.RED, Color.GREEN, Color.BLUE, Color.YELLOW, Color.MAGENTA]
		for mi in _game._gun.find_children("*", "MeshInstance3D", true, false):
			for i in (mi as MeshInstance3D).mesh.get_surface_count():
				var m := StandardMaterial3D.new()
				m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
				m.albedo_color = cols[i % cols.size()]
				(mi as MeshInstance3D).set_surface_override_material(i, m)
	if _t > 5.0 and _t < 5.1:
		Input.action_press("q3_attack")
	elif Input.is_action_pressed("q3_attack"):
		Input.action_release("q3_attack")
	if _shot < AT.size() and _t > AT[_shot]:
		var k := _shot
		_shot += 1
		var fx: Q3WeaponFx = _game._gun_fx
		if fx:
			print("t %.2f rail mats %d: %s" % [_t, fx._rail.size(), fx._rail.map(func(r): return [r[1], (r[0] as StandardMaterial3D).albedo_color, (r[0] as StandardMaterial3D).emission])])
			for mi in _game._gun.find_children("*", "MeshInstance3D", true, false):
				var m := mi as MeshInstance3D
				for i in m.mesh.get_surface_count():
					print("   ", m.name, " surface ", i, ": ", m.get_active_material(i).resource_name)
		await RenderingServer.frame_post_draw
		var img := get_viewport().get_texture().get_image()
		var w := img.get_width()
		var h := img.get_height()
		img.get_region(Rect2i(int(w * 0.45), int(h * 0.5), int(w * 0.55), int(h * 0.5))).save_png("%s_%d.png" % [out, k])
		if _shot == AT.size():
			print("RAILGUN done")
			Q3Server.exec_now("killserver")
			get_tree().quit()
