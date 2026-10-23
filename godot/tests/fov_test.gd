extends Node
## Headless check of the field of view (cg_fov, horizontal at 4:3) and +zoom (cg_zoomFov 22.5
## over 150 ms): the camera follows the setting live, zooms on the zoom action and comes back.
## The setting is put back afterwards.
##   godot --headless --path . res://tests/fov_test.tscn

var _game: Q3Gameplay
var _t := 0.0
var _step := 0
var _ok := true
var _old: float


func _ready() -> void:
	_old = float(Settings.get_value("video/fov"))
	var s := Session.new_setup("ffa", "q3dm17")
	s.bots = []
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	add_child(_game)


func _near(a: float, b: float) -> bool:
	return absf(a - b) < 0.05


func _process(delta: float) -> void:
	_t += delta
	match _step:
		0:
			if _t > 2.5:
				_check("default fov %.0f -> %.2f vertical" % [_old, _game.cam.fov], _near(_game.cam.fov, Settings.fov_vertical(_old)))
				Settings.set_value("video/fov", 110.0)
				_step = 1
		1:
			if _t > 2.8:
				_check("fov 110 applies live (%.2f)" % _game.cam.fov, _near(_game.cam.fov, Settings.fov_vertical(110.0)))
				Input.action_press("q3_zoom")
				_step = 2
		2:
			if _t > 3.2:
				_check("zoom reaches cg_zoomFov 22.5 (%.2f)" % _game.cam.fov, _near(_game.cam.fov, Settings.fov_vertical(22.5)))
				Input.action_release("q3_zoom")
				_step = 3
		3:
			if _t > 3.6:
				_check("zoom out returns to 110 (%.2f)" % _game.cam.fov, _near(_game.cam.fov, Settings.fov_vertical(110.0)))
				Settings.set_value("video/fov", _old)
				Settings.save()
				print("FOV %s" % ("ok" if _ok else "FAILED"))
				Q3Server.exec_now("killserver")
				get_tree().quit(0 if _ok else 1)
				_step = 4


func _check(label: String, ok: bool) -> void:
	print(("ok   " if ok else "FAIL ") + label)
	_ok = _ok and ok
