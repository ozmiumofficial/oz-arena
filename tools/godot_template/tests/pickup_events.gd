extends Node
## Headless check that every item the server gives the local player plays its pickup event,
## while running (predicted footsteps share the playerState event slots: cgame's
## CG_CheckChangedPredictableEvents). On q3dm0 the player is teleported into its rows of armor
## shards and health with forward held; the client's EV_ITEM_PICKUP count must match the
## server's "Item: <client>" lines in the log, and each taken item plays the pickup effect.
##   godot --headless --path . res://tests/pickup_events.tscn

const E := preload("res://scripts/game/q3_enums.gd")
const RUNS := [[2.0, "-336 -230 40 90"], [4.0, "-24 40 40 90"], [6.0, "692 -820 40 90"], [8.0, "-1318 -1600 40 -90"]]
var _game: Q3Gameplay
var _t := 0.0
var _run := 0
var _client_pickups: Array[String] = []


func _ready() -> void:
	var s := Session.new_setup("ffa", "q3dm0")
	s.bots = []
	s.set_meta("devmap", true)
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	_game.test_input = true
	add_child(_game)
	_game.client.local_event.connect(func(ev: int, parm: int):
		if ev == E.EV_ITEM_PICKUP:
			var names: PackedStringArray = ClassDB.class_call_static("Q3Core", "item_classnames")
			_client_pickups.append(names[parm - 1] if parm >= 1 and parm <= names.size() else "?"))


func _process(delta: float) -> void:
	_t += delta
	Input.action_press("q3_forward")
	if _run < RUNS.size() and _t > RUNS[_run][0]:
		Q3Server.client_command(_game.me, "setviewpos " + String(RUNS[_run][1]))
		_game._view = Vector3.ZERO
		_run += 1
	if _t > 10.0:
		Input.action_release("q3_forward")
		print("PICKUPS client %d: %s" % [_client_pickups.size(), _client_pickups])
		print("PICKUPS effects %d" % _game.client.pickup_effects)
		print("PICKUPS me %d (compare the log's \"Item: %d\" lines)" % [_game.me, _game.me])
		Q3Server.exec_now("killserver")
		get_tree().quit()
