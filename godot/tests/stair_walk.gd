extends Node
## Windowed check of the eye on stairs in a real match: finds a staircase with trace scans, teleports the
## player to its foot (setviewpos, devmap), holds forward, and logs the camera height every frame.
## Reports reversals (the eye dropping while climbing) and the biggest frame-to-frame jumps.
##   godot --path . res://tests/stair_walk.tscn -- map=pukka3tourney2
var map := "pukka3tourney2"
var _game: Q3Gameplay
var _t := 0.0
var _state := "wait"
var _eye: Array = []
var _org: Array = []
var _dir := Vector2.RIGHT
var _from := Vector3.ZERO


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("map="):
			map = a.substr(4)
	Engine.max_fps = 60
	var s := Session.new_setup("ffa", map)
	s.bots = []
	s.set_meta("devmap", true)
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	add_child(_game)


func _floor(x: float, y: float, z: float) -> float:
	var tr := Q3Server.trace(Vector3(x, y, z + 300.0), Vector3(x, y, z - 700.0), Vector3.ZERO, Vector3.ZERO, 1023, 1)
	if tr.is_empty() or float(tr["fraction"]) >= 1.0 or tr.get("startsolid", false):
		return NAN
	return float(tr["endpos"].z)


func _find(at: Vector3) -> bool:
	var step := 16.0
	var n := 160
	var grid := {}
	for i in n:
		for j in n:
			var x := at.x + (i - n / 2) * step
			var y := at.y + (j - n / 2) * step
			grid[Vector2i(i, j)] = _floor(x, y, at.z)
	for dirv in [Vector2i(1, 0), Vector2i(-1, 0), Vector2i(0, 1), Vector2i(0, -1)]:
		for i in range(4, n - 14):
			for j in range(4, n - 14):
				var c := Vector2i(i, j)
				var f0: float = grid[c]
				if is_nan(f0):
					continue
				var last := f0
				var ok := true
				for k in range(1, 12):
					var f: float = grid.get(c + dirv * k, NAN)
					if is_nan(f) or f < last - 0.1 or f - last > 20.0:
						ok = false
						break
					last = f
				if ok and last - f0 >= 36.0 and last - f0 <= 120.0:
					var cb: Vector2i = c - dirv * 3
					var fb: float = grid.get(cb, NAN)
					if not is_nan(fb) and absf(fb - f0) < 2.0:
						var wx := at.x + (cb.x - n / 2) * step
						var wy := at.y + (cb.y - n / 2) * step
						_from = Vector3(wx, wy, f0 + 26.0)
						_dir = Vector2(dirv.x, dirv.y)
						print("STAIRS at ", _from, " dir ", _dir, " rise ", last - f0)
						return true
	return false


func _process(delta: float) -> void:
	_t += delta
	if _game.me < 0:
		return
	match _state:
		"wait":
			if _t > 3.0:
				var ps := Q3Server.player_state(_game.me)
				if _find(ps["origin"]):
					Q3Server.client_command(_game.me, "setviewpos %d %d %d %d" % [int(_from.x), int(_from.y), int(_from.z), int(rad_to_deg(atan2(_dir.y, _dir.x)))])
					_state = "settle"
					_t = 0.0
				else:
					print("STAIR WALK: no staircase found")
					get_tree().quit()
		"settle":
			if _t > 1.0:
				Input.action_press("q3_forward")
				_state = "walk"
				_t = 0.0
		"walk":
			_eye.append(_game.cam.global_position.y * 32.0)
			var ps := Q3Server.player_state(_game.me)
			_org.append(float(ps["origin"].z))
			if _t > 2.5:
				Input.action_release("q3_forward")
				_report()
				get_tree().quit()


func _report() -> void:
	var rev := 0
	var big := 0.0
	var back := 0
	for i in range(1, _eye.size()):
		var d: float = _eye[i] - _eye[i - 1]
		if d < -0.05 and _eye[i] > _eye[0] + 1.0:
			back += 1
		big = maxf(big, absf(d))
	var n_up := 0
	for i in range(1, _org.size()):
		if _org[i] > _org[i - 1] + 0.01:
			n_up += 1
	print("STAIR WALK frames=%d eye %.1f -> %.1f body %.1f -> %.1f, reversals %d, biggest eye step %.2f, body step-ups %d" % [_eye.size(), _eye[0], _eye[-1], _org[0], _org[-1], back, big, n_up])
	print("EYE ", _eye.map(func(x): return snappedf(x, 0.1)))
