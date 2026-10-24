extends Node
## Headless check of the main menu's shuffled soundtrack (Q3MenuMusic): the songs are the 12 of
## Quake 3's soundtrack (intro + loop counted once, win and loss left out); two full rounds play
## every song once each, with no song twice in a row across the round break; an intro runs into its
## loop; the Menu music setting stops and restarts it.
##   godot --headless --path . res://tests/menu_music_test.tscn --quit-after 2000

var _ok := true


func _ready() -> void:
	var songs := Q3MenuMusic.songs()
	var firsts := songs.map(func(s): return s[0])
	_check("12 songs %s" % [firsts], songs.size() == 12)
	_check("win and loss left out", not firsts.has("win") and not firsts.has("loss"))
	_check("intro and loop are one song", songs.any(func(s): return s == ["fla22k_04_intro", "fla22k_04_loop"]))
	var was: bool = Settings.get_value("audio/menu_music")
	Settings.set_value("audio/menu_music", true)
	var m := Q3MenuMusic.new()
	add_child(m)
	_check("plays on open", m.current != "" and m._player.playing)
	var heard: Array = [m.current]
	for i in songs.size() * 2 - 1:
		m.next()
		heard.append(m.current)
	var round1 := heard.slice(0, songs.size())
	var round2 := heard.slice(songs.size())
	round1.sort()
	round2.sort()
	firsts.sort()
	_check("each round plays every song once", round1 == firsts and round2 == firsts)
	var repeats := 0
	for i in heard.size() - 1:
		repeats += int(heard[i] == heard[i + 1])
	_check("no song twice in a row", repeats == 0)
	while m.current != "fla22k_01_intro":
		m.next()
	m._on_finished()
	_check("intro runs into its loop", String(m._player.stream.resource_path).ends_with("fla22k_01_loop.ogg"))
	Settings.set_value("audio/menu_music", false)
	_check("setting off stops it", m.current == "" and not m._player.playing)
	Settings.set_value("audio/menu_music", true)
	_check("setting on starts it", m.current != "" and m._player.playing)
	m.free()
	Settings.set_value("audio/menu_music", was)
	Settings.save()
	print("MENU_MUSIC_TEST ", "PASS" if _ok else "FAIL")
	get_tree().quit(0 if _ok else 1)


func _check(what: String, ok: bool) -> void:
	_ok = _ok and ok
	print("  [%s] %s" % ["ok" if ok else "FAIL", what])
