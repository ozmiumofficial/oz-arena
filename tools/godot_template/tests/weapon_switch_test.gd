extends Node
## The view weapon through switches: the gun's camera-space position every frame, checking for a
## snap, a one-frame move far bigger than the frames on either side (CG_RunLerpFrame eases each
## torso animation in over a frame time, so drop -> raise -> idle moves in steps of 3+ frames).
##   godot --path . res://tests/weapon_switch_test.tscn --headless

const SWITCHES := [[3.0, 5], [4.5, 7], [6.0, 3], [7.5, 2]]   # [time s, weapon]
var _game: Q3Gameplay
var _t := 0.0
var _moves: Array = []      ## [distance, time, weapon] per frame, 0 across a model swap
var _last := Vector3.INF
var _last_w := -1
var _switches := 0


func _ready() -> void:
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
	for sw in SWITCHES:
		if _t > sw[0] and _t - delta <= sw[0]:
			_game._select_weapon(sw[1])
			_switches += 1
	if _t > 2.5 and _game._gun:
		var p := _game._gun.position
		var w := _game._gun_weapon
		_moves.append([p.distance_to(_last) if w == _last_w and _last != Vector3.INF else 0.0, _t, w])
		_last = p
		_last_w = w
	if _t > 9.0:
		var snaps: Array = []
		for i in range(1, _moves.size() - 1):
			var m: float = _moves[i][0]
			if m > 0.02 and m > 2.5 * maxf(_moves[i - 1][0], _moves[i + 1][0]):
				snaps.append("t %.2f weapon %d: %.3f m" % [_moves[i][1], _moves[i][2], m])
		var ok := _switches == SWITCHES.size() and snaps.is_empty() and _game._guns.size() >= 8
		print("WEAPON SWITCH %s: %d switches, %d guns made, %d frames, snaps %s" % [
			"OK" if ok else "FAIL", _switches, _game._guns.size(), _moves.size(), snaps])
		get_tree().quit(0 if ok else 1)
