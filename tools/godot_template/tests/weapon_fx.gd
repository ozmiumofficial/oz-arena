extends Node
## Windowed check of every weapon's effects in a real match: devmap q3dm17, "give all", then each
## weapon (gauntlet to BFG) fired for 1.5 s at the floor ahead, with frames saved while it fires and
## after it stops (marks, smoke). One bot runs about so remote players' effects show up too.
##   godot --path . res://tests/weapon_fx.tscn -- out=C:/temp/wfx
## Writes <out>_<weapon>_<n>.png and prints "WEAPON FX done".
## sweep=<degrees> swings the aim left and right that far while the lightning gun fires (burn lines).

var out := "user://wfx"
var _game: Q3Gameplay
var _t := 0.0
var _plan: Array = []   ## [time, action, arg]
var _i := 0
var pitch := 22.0
var _move := false
var _old_preset := ""        ## move=1: run and jump the whole time (speedometer, bob)
var _flash_frames := {}   ## weapon -> frames its view flash was on (-1: the model has no flash)
var sweep := 0.0
var _yaw0 := INF


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			out = a.substr(4)
		if a.begins_with("pitch="):
			pitch = float(a.substr(6))
		if a.begins_with("sweep="):
			sweep = float(a.substr(6))
		if a == "move=1":
			_move = true
		if a.begins_with("preset="):
			# shown for this run only; the player's own setting is put back before quitting
			_old_preset = String(Settings.get_value("hud/preset"))
			Settings.set_value("hud/preset", a.substr(7))
	Engine.max_fps = 60
	var s := Session.new_setup("ffa", "q3dm17")
	s.bots = [{"name": "Sarge", "skill": 1, "team": ""}]
	s.set_meta("devmap", true)
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	add_child(_game)
	if _move:
		Input.action_press("q3_forward")
		Input.action_press("q3_jump")
	var t := 2.0
	_plan.append([t, "give", 0])
	t += 0.5
	for w in range(1, 10):
		_plan.append([t, "weapon", w])
		t += 0.8
		_plan.append([t, "fire", 1])
		for k in 4:
			_plan.append([t + 0.05 + k * 0.33, "shot", "%d_%d" % [w, k]])
		t += 1.5
		_plan.append([t, "fire", 0])
		_plan.append([t + 0.15, "shot", "%d_after" % w])
		t += 0.6
	_plan.append([t, "quit", 0])


func _process(delta: float) -> void:
	_t += delta
	if _game and _game.me >= 0:
		_game._view.x = pitch
		var fx: Q3WeaponFx = _game._gun_fx
		if sweep > 0.0 and fx and fx.weapon == 6 and Input.is_action_pressed("q3_attack"):
			if _yaw0 == INF:
				_yaw0 = _game._view.y
			_game._view.y = _yaw0 + sin(_t * 2.5) * sweep
		if fx and fx.flash and fx.flash.visible:
			_flash_frames[fx.weapon] = int(_flash_frames.get(fx.weapon, 0)) + 1
		elif fx and fx.flash == null:
			_flash_frames[fx.weapon] = -1
	while _i < _plan.size() and _t >= float(_plan[_i][0]):
		var step: Array = _plan[_i]
		_i += 1
		match step[1]:
			"give":
				Q3Server.client_command(_game.me, "give all")
			"weapon":
				_game._select_weapon(int(step[2]))
			"fire":
				if int(step[2]):
					Input.action_press("q3_attack")
				else:
					Input.action_release("q3_attack")
			"shot":
				await RenderingServer.frame_post_draw
				var img := get_viewport().get_texture().get_image()
				img.save_png("%s_%s.png" % [out, step[2]])
			"quit":
				print("flash frames per weapon: ", _flash_frames)
				if _old_preset != "":
					Settings.set_value("hud/preset", _old_preset)
					Settings.save()
				print("WEAPON FX done")
				get_tree().quit()
