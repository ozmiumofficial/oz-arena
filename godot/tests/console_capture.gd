extends Node
## Windowed shot of the developer console over a match (q3dm17, one bot).
##   godot --path . res://tests/console_capture.tscn -- out=C:/path/console.png [window=1920x1080] [full=1] [wait=4]
##       ["cmds=cg_thirdPerson 1; cg_thirdPersonAngle 30"] [closed=1]
## cmds run first, through the console as typed; closed=1 shoots the match with the console shut.

var _out := "user://console.png"
var _full := false
var _closed := false
var _cmds := ""
var _wait := 4.0
var _game: Q3Gameplay
var _t := 0.0
var _step := 0


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			_out = a.substr(4)
		elif a == "full=1":
			_full = true
		elif a == "closed=1":
			_closed = true
		elif a.begins_with("wait="):
			_wait = float(a.substr(5))
		elif a.begins_with("cmds="):
			_cmds = a.substr(5)
		elif a.begins_with("window="):
			var wh := a.substr(7).split("x")
			DisplayServer.window_set_size(Vector2i(int(wh[0]), int(wh[1])))
	var s := Session.new_setup("ffa", "q3dm17")
	s.bots = [{"name": "Sarge", "skill": 1, "team": ""}]
	Session.setup = s
	Diagnostics.console.history_file = "user://console_history_capture.txt"
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	add_child(_game)


func _process(delta: float) -> void:
	_t += delta
	var con := Diagnostics.console
	if _step == 0 and _t > _wait:
		if _cmds != "":
			con.commands.run(_cmds)
		if not _closed:
			con.open(_full)
			for line in ["cmdlist k", "sv_fps", "cheats on", "give all", "status", "cg_fov", "echo ^1red ^2green ^3yellow ^4blue ^7white"]:
				con._edit.text_submitted.emit(line)
		_step = 1
	elif _step == 1 and _t > _wait + 1.2:
		get_viewport().get_texture().get_image().save_png(_out)
		print("saved ", _out)
		con.commands.run("cg_thirdPerson 0; cg_thirdPersonAngle 0; reset cg_thirdPersonRange")
		Q3Server.exec_now("killserver")
		DirAccess.remove_absolute("user://console_history_capture.txt")
		get_tree().quit()
