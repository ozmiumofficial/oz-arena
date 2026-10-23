extends Node
## Headless smoke test of the Gameplay scene: FFA on q3dm17 with three bots, the local player
## holding forward and fire, for `seconds` of real frames.
##   godot --headless --path . res://tests/gameplay_smoke.tscn [-- seconds=15 map=q3dm17]
## Prints what the client drew and "GAMEPLAY SMOKE ok|FAILED".

var seconds := 15.0
var _game: Node
var _t := 0.0
var _fired := false


func _ready() -> void:
	var map := "q3dm17"
	for a in OS.get_cmdline_user_args():
		if a.begins_with("seconds="):
			seconds = float(a.substr(8))
		if a.begins_with("map="):
			map = a.substr(4)
	var s := Session.new_setup("ffa", map)
	s.bots = [{"name": "Sarge", "skill": 3, "team": ""}, {"name": "Xaero", "skill": 3, "team": ""},
		{"name": "Crash", "skill": 3, "team": ""}]
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	add_child(_game)
	Input.action_press("q3_forward")
	Input.action_press("q3_attack")


func _process(delta: float) -> void:
	_t += delta
	# turn slowly so the shots go somewhere new
	_game._view.y += delta * 40.0
	if _t < seconds:
		return
	set_process(false)
	Input.action_release("q3_forward")
	Input.action_release("q3_attack")
	var client: Q3Client = _game.client
	var players := 0
	var items := 0
	var other := 0
	for c in client.get_children():
		if c is Q3Character:
			players += 1
		elif c is Q3Pickup:
			items += 1
		else:
			other += 1
	var ps := Q3Server.player_state(_game.me)
	var ok: bool = _game._error == "" and _game.map_scene != null and _game.me >= 0 and players >= 1 and items > 0 and not ps.is_empty()
	print("map %s, me %d, error '%s'" % [_game.setup.map, _game.me, _game._error])
	print("drawn: %d players, %d items, %d effects/sounds; server time %d ms" % [players, items, other, Q3Server.time()])
	print("local: health %d, weapon %d, score %d" % [ps.get("stats", [0])[0], ps.get("weapon", 0), ps.get("persistant", [0])[0]])
	print("GAMEPLAY SMOKE ", "ok" if ok else "FAILED")
	Q3Server.exec_now("killserver")
	get_tree().quit(0 if ok else 1)
