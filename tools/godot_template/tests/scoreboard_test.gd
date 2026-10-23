extends Node
## Windowed look at the Tab scoreboard's player panels: a match with three bots, Tab held, the
## mouse mode on, your panel open, then a bot's. Saves <out>_you.png and <out>_bot.png.
##   godot --path . res://tests/scoreboard_test.tscn -- out=C:/temp/board

var out := "user://board"
var _game: Q3Gameplay
var _t := 0.0
var _step := 0


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			out = a.substr(4)
	var s := Session.new_setup("ffa", "q3dm17")
	s.bots = [{"name": "Sarge", "skill": 3, "team": ""}, {"name": "Xaero", "skill": 4, "team": ""}, {"name": "Klesk", "skill": 2, "team": ""}]
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	add_child(_game)


func _process(delta: float) -> void:
	_t += delta
	if _t > 4.0 and _step == 0:
		_step = 1
		Input.action_press("q3_scores")
	elif _t > 4.6 and _step == 1:
		_step = 2
		_game.scoreboard.set_interactive(true)
		_game.scoreboard._selected = _game.me
		_game._board_t = 0.0
	elif _t > 5.6 and _step == 2:
		_step = 3
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png(out + "_you.png")
		for r in _game.standings():
			if r["bot"]:
				_game.scoreboard._selected = int(r["client"])
				break
		_game._board_t = 0.0
	elif _t > 6.6 and _step == 3:
		_step = 4
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png(out + "_bot.png")
		Input.action_release("q3_scores")
		print("SCOREBOARD done")
		Q3Server.exec_now("killserver")
		get_tree().quit()
