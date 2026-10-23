extends Node
## Headless check of Tab + right click in a match (events fed through Godot's input, no OS keys):
## with Tab held, right click frees the mouse for the scoreboard and right click again takes it
## back, and Mouse 2 (the menus' "back") never pauses the match.
##   godot --headless --path . res://tests/scoreboard_mouse_test.tscn

var _game: Q3Gameplay
var _t := 0.0
var _step := 0
var _ok := true


func _ready() -> void:
	var s := Session.new_setup("ffa", "q3dm17")
	s.bots = []
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	_game.test_input = true
	add_child(_game)


func _right_click() -> void:
	var ev := InputEventMouseButton.new()
	ev.button_index = MOUSE_BUTTON_RIGHT
	ev.pressed = true
	ev.position = Vector2(800, 450)
	Input.parse_input_event(ev)
	var up := ev.duplicate() as InputEventMouseButton
	up.pressed = false
	Input.parse_input_event(up)


func _process(delta: float) -> void:
	_t += delta
	match _step:
		0:
			if _t > 3.0:
				_right_click()   # no Tab: a game button, not the pause
				_step = 1
		1:
			if _t > 3.3:
				_check("right click alone doesn't pause", not _game._paused)
				Input.action_press("q3_scores")
				_step = 2
		2:
			if _t > 3.6:
				_check("Tab shows the scoreboard", _game.scoreboard.visible)
				_right_click()
				_step = 3
		3:
			if _t > 3.9:
				_check("Tab + right click frees the mouse", _game.scoreboard.interactive and not _game._paused)
				_right_click()
				_step = 4
		4:
			if _t > 4.2:
				_check("right click again takes it back", not _game.scoreboard.interactive and not _game._paused)
				_right_click()
				Input.action_release("q3_scores")
				_step = 5
		5:
			if _t > 4.5:
				_check("letting Tab go ends it", not _game.scoreboard.interactive and not _game.scoreboard.visible)
				print("SCOREBOARD MOUSE %s" % ("ok" if _ok else "FAILED"))
				Q3Server.exec_now("killserver")
				get_tree().quit(0 if _ok else 1)
				_step = 6


func _check(label: String, ok: bool) -> void:
	print(("ok   " if ok else "FAIL ") + label)
	_ok = _ok and ok
