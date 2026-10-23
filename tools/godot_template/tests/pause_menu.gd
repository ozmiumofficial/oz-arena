extends Node
## Windowed check of the in-match pause menu: a match on q3dm17, Escape's pause after 4 s, the
## crosshair editor opened over it, a crosshair change that must show on the HUD at once, then
## resume. Grab the screen meanwhile to see it (the web view is invisible to Godot's viewport).
##   godot --path . res://tests/pause_menu.tscn
## Prints "PAUSE MENU ok|FAILED" with what it checked, and saves Godot's own frame of the paused
## match with the edited crosshair to user://pause_menu_3d.png.

var _game: Q3Gameplay
var _t := 0.0
var _step := 0
var _old_shape := ""
var _checks: Array[String] = []
var _ok := true


func _ready() -> void:
	var s := Session.new_setup("ffa", "q3dm17")
	s.bots = [{"name": "Sarge", "skill": 1, "team": ""}]
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	add_child(_game)
	_old_shape = String(Settings.get_value("crosshair/shape"))


func _check(name: String, ok: bool) -> void:
	_checks.append(("ok   " if ok else "FAIL ") + name)
	_ok = _ok and ok


func _process(delta: float) -> void:
	_t += delta
	match _step:
		0 when _t > 4.0:
			_step = 1
			_check("web view made", _game.pause_layer != null and _game.pause_layer.web != null)
			_check("pause page loaded (ready received)", _game.pause_bridge.received.any(func(m): return m.get("type") == "ready"))
			_game._set_paused(true)
			print("PAUSE SHOWN at %.1f s" % _t)
		1 when _t > 7.0:
			_step = 2
			_check("server stopped while paused", _game._paused)
			_game.pause_layer.post({"type": "open", "page": "crosshair"})
			print("CROSSHAIR EDITOR at %.1f s" % _t)
		2 when _t > 9.0:
			_step = 3
			Settings.set_value("crosshair/shape", "circle")
			print("SHAPE CIRCLE at %.1f s" % _t)
		3 when _t > 10.0:
			_step = 4
			_check("HUD crosshair follows the setting", _game.hud.crosshair._shape() == "circle")
			_check("page asked for a crosshair preview", _game.pause_bridge.received.any(func(m): return m.get("type") == "crosshair_preview"))
			# Godot's own frame: the paused match and HUD (the web layer isn't in it)
			await RenderingServer.frame_post_draw
			get_viewport().get_texture().get_image().save_png(ProjectSettings.globalize_path("user://pause_menu_3d.png"))
		4 when _t > 12.0:
			_step = 5
			_game.pause_bridge.handle({"type": "resume"})
			_check("resume unpauses", not _game._paused)
			print("RESUMED at %.1f s" % _t)
		5 when _t > 14.0:
			_step = 6
			Settings.set_value("crosshair/shape", _old_shape)
			Settings.save()
			for c in _checks:
				print(c)
			print("PAUSE MENU ", "ok" if _ok else "FAILED")
			Q3Server.exec_now("killserver")
			get_tree().quit(0 if _ok else 1)
