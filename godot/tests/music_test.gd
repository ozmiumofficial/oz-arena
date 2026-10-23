extends Node
## Headless check of the soundtrack: a match on q3dm13 starts CS_MUSIC's intro (fla22k_04_intro),
## then its loop takes over and repeats; a single track (the postgame's win) repeats itself.
##   godot --headless --path . res://tests/music_test.tscn

const E := preload("res://scripts/game/q3_enums.gd")
var _game: Q3Gameplay
var _t := 0.0
var _ok := true


func _ready() -> void:
	var s := Session.new_setup("ffa", "q3dm13")
	s.bots = []
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	add_child(_game)


func _process(delta: float) -> void:
	_t += delta
	if _t < 2.0:
		return
	set_process(false)
	var m: Q3Music = _game.music
	var p: AudioStreamPlayer = m._player
	_check("CS_MUSIC is the map's (%s)" % Q3Server.configstring(E.CS_MUSIC), Q3Server.configstring(E.CS_MUSIC).contains("fla22k_04_intro"))
	_check("the intro plays on the Music bus", p.playing and p.stream and p.stream.resource_path.ends_with("fla22k_04_intro.ogg") and p.bus == "Music")
	m._on_finished()
	_check("the loop follows the intro", p.playing and p.stream.resource_path.ends_with("fla22k_04_loop.ogg"))
	m._on_finished()
	_check("the loop repeats", p.playing and p.stream.resource_path.ends_with("fla22k_04_loop.ogg"))
	m.play("music/win")
	m._on_finished()
	_check("a single track repeats (postgame win)", p.playing and p.stream.resource_path.ends_with("win.ogg"))
	print("MUSIC %s" % ("ok" if _ok else "FAILED"))
	Q3Server.exec_now("killserver")
	get_tree().quit(0 if _ok else 1)


func _check(label: String, ok: bool) -> void:
	print(("ok   " if ok else "FAIL ") + label)
	_ok = _ok and ok
