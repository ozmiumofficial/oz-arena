extends Node
## Windowed: a match on a probe-heavy map must show no lighting pops once the screen is uncovered.
## Records every frame's change after Q3Map.cover_probes() ends and reports the biggest.
##   godot --path . res://tests/probe_cover_test.tscn -- map=pukka3tourney2
var map := "pukka3tourney2"
var _game: Q3Gameplay
var _frame := 0
var _uncovered := -1
var _prev: Array = []
var _worst := 0.0
var _covered_worst := 0.0


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


func _process(_d: float) -> void:
	_frame += 1
	var ms: Node = _game.map_scene
	if ms == null:
		return
	var covered: bool = ms.has_node("ProbeCover")
	if not covered and _uncovered < 0 and _frame > 5:
		_uncovered = _frame
		print("COVER ended at frame ", _frame)
	var img := get_viewport().get_texture().get_image()
	img.resize(160, 90, Image.INTERPOLATE_BILINEAR)
	img.convert(Image.FORMAT_L8)
	var cur := Array(img.get_data())
	if _prev.size() == cur.size() and _uncovered >= 0 and _frame > _uncovered + 20:
		var s := 0.0
		for i in range(0, cur.size(), 3):
			s += absf(float(cur[i]) - float(_prev[i]))
		_worst = maxf(_worst, s / (cur.size() / 3.0))
	_prev = cur
	if _uncovered >= 0 and _frame > _uncovered + 360:
		print("PROBE COVER: uncovered at frame %d, biggest change after fade %.3f" % [_uncovered, _worst])
		get_tree().quit()
	if _frame > 1500:
		print("PROBE COVER: never uncovered")
		get_tree().quit()
