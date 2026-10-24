class_name Q3Music
extends Node
## The background music, as cg_main.c CG_StartMusic and snd_main.c S_StartBackgroundTrack do it:
## CS_MUSIC (the map's worldspawn "music") names an intro and a loop; the intro plays once and
## the loop repeats, and a single track just repeats. Quake 3 mixes music at s_musicVolume 0.25
## against s_volume 0.8 for effects, about 10 dB under them, on the Music bus (the music volume
## setting). The tracks are library/music/*.ogg (tools/make_music.py).

const DIR := "res://library/music/"
const LEVEL_DB := -10.0

var _player: AudioStreamPlayer
var _tracks := ""        ## what's playing: the CS_MUSIC string
var _loop: AudioStream


func _ready() -> void:
	_player = AudioStreamPlayer.new()
	_player.bus = "Music" if AudioServer.get_bus_index("Music") >= 0 else "Master"
	_player.volume_db = LEVEL_DB
	_player.finished.connect(_on_finished)
	add_child(_player)


## Plays "intro [loop]" (id paths such as music/fla22k_04_intro.wav, either slash); "" stops.
## Asking again for what already plays changes nothing, as CG_StartMusic restarting the same track
## would be heard.
func play(tracks: String) -> void:
	tracks = tracks.strip_edges().replace("\\", "/")
	if tracks == _tracks:
		return
	_tracks = tracks
	_player.stop()
	_loop = null
	var parts := tracks.split(" ", false)
	if parts.is_empty():
		return
	var intro := _load(parts[0])
	var loop := _load(parts[1]) if parts.size() > 1 else intro
	if intro == null:
		return
	_loop = loop
	_player.stream = intro
	_player.play()


func stop() -> void:
	play("")


## cg_servercmds.c CG_MapRestart's CG_StartMusic: the same track from its intro again.
func restart(tracks: String) -> void:
	_tracks = "\n"   # a value play() never matches
	play(tracks)


func _on_finished() -> void:
	if _loop == null:
		return
	if _player.stream != _loop:
		_player.stream = _loop
	_player.play()


func _load(id_path: String) -> AudioStream:
	var p := DIR + id_path.get_file().get_basename().to_lower() + ".ogg"
	if not ResourceLoader.exists(p):
		push_warning("Q3Music: no track " + p)
		return null
	return load(p)
