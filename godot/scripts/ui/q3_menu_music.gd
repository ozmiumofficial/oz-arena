class_name Q3MenuMusic
extends Node
## The main menu's music (a remaster addition: q3_ui's menu was silent): Quake 3's soundtrack,
## shuffled. Each song plays once, an intro running straight into its loop, and every song is heard
## before any comes round again; a new round never opens with the song that closed the last. The
## order lives on the class, so coming back from a match carries on with the next song instead of
## starting over. Off with the Menu music setting (audio/menu_music). Win and loss are the
## campaign's postgame stings, not songs, and stay out.

const DIR := "res://library/music/"
const LEVEL_DB := -10.0          ## as Q3Music: Quake 3's music sat about 10 dB under effects
const FADE_IN := 2.0
const GAP := 1.5                 ## seconds of quiet between songs

static var _bag: Array = []      ## songs still to play this round, next last
static var _last := ""

var current := ""                ## the song playing (its first file's name), "" if none
var _player: AudioStreamPlayer
var _parts: Array = []           ## the current song's files still to play
var _gap: SceneTreeTimer
var _enabled := true
var _paused := false


func _ready() -> void:
	add_to_group(&"menu_music")
	_player = AudioStreamPlayer.new()
	_player.bus = "Music" if AudioServer.get_bus_index("Music") >= 0 else "Master"
	_player.volume_db = LEVEL_DB
	_player.finished.connect(_on_finished)
	add_child(_player)
	set_enabled(Settings.get_value("audio/menu_music"))


## Every song on disk: "fla22k_01_intro" + "fla22k_01_loop" are one song, the rest stand alone.
static func songs() -> Array:
	var names := {}
	for f in ResourceLoader.list_directory(DIR):
		if f.get_extension() == "ogg":
			names[f.get_basename()] = true
	var out := []
	for n: String in names:
		if n in ["win", "loss"] or n.ends_with("_loop"):
			continue
		var loop := n.trim_suffix("_intro") + "_loop"
		out.append([n, loop] if n.ends_with("_intro") and names.has(loop) else [n])
	out.sort_custom(func(a, b): return a[0] < b[0])
	return out


func set_enabled(on: bool) -> void:
	_enabled = on
	if not is_inside_tree():
		return
	if on and current == "":
		next()
	elif not on:
		_stop()


## Held while a cinematic plays over the menu.
func set_paused(on: bool) -> void:
	_paused = on
	_player.stream_paused = on


## Skips to the next song of the shuffle.
func next() -> void:
	_stop()
	if not _enabled:
		return
	if _bag.is_empty():
		_bag = songs()
		_bag.shuffle()
		# the bag is drawn from the back: keep the song just heard from opening the new round
		if _bag.size() > 1 and _bag[-1][0] == _last:
			_bag.insert(0, _bag.pop_back())
	if _bag.is_empty():
		return
	_parts = (_bag.pop_back() as Array).duplicate()
	current = _parts[0]
	_last = current
	_player.volume_db = -60.0
	create_tween().tween_property(_player, "volume_db", LEVEL_DB, FADE_IN)
	_play_part()


func _stop() -> void:
	current = ""
	_parts.clear()
	_gap = null
	_player.stop()


func _play_part() -> void:
	var p: String = DIR + _parts.pop_front() + ".ogg"
	if not ResourceLoader.exists(p):
		push_warning("Q3MenuMusic: no track " + p)
		next.call_deferred()
		return
	_player.stream = load(p)
	_player.play()
	_player.stream_paused = _paused


func _on_finished() -> void:
	if not _parts.is_empty():
		_play_part()
		return
	current = ""
	var t := get_tree().create_timer(GAP)
	_gap = t
	t.timeout.connect(func(): if _gap == t and _enabled: next())
