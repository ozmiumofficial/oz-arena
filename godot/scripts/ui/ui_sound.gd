extends Node
## Autoload "UiSound": Quake 3's menu sounds, played the way q3_ui does (ui_qmenu.c, ui_atoms.c).
##   in      menu1.wav  a menu opens (UI_PushMenu sets m_entersound; played after its first frame)
##   move    menu2.wav  the cursor moves to another item (keys or mouse hover), an item with an
##                      action is activated, a spin/radio/slider value changes
##   out     menu3.wav  a menu closes (UI_PopMenu: Escape or Mouse 2)
##   buzz    menu4.wav  a slider pushed past its end, a list scrolled past its end, a refused action
##   change  weapons/change.wav  the menu showcase changes subject
## All on the UI bus; overlapping like Quake 3's local sound channel.

const SOUNDS := {
	"in": "res://library/sounds/misc/menu1.wav",
	"move": "res://library/sounds/misc/menu2.wav",
	"out": "res://library/sounds/misc/menu3.wav",
	"buzz": "res://library/sounds/misc/menu4.wav",
	"change": "res://library/sounds/weapons/change.wav",
}
const AMBIENT := "res://library/sounds/world/ambient/x_ominous.wav"

var streams: Dictionary = {}
var history: PackedStringArray = []   ## recent events, newest last (the self-test reads it)
var quiet := false                    ## set while focus moves programmatically (opening a page)
var _player: AudioStreamPlayer
var _ambient: AudioStreamPlayer
var _in_pending := false


func _ready() -> void:
	for k in SOUNDS:
		streams[k] = load(SOUNDS[k]) if ResourceLoader.exists(SOUNDS[k]) else null
	_player = AudioStreamPlayer.new()
	_player.bus = &"UI"
	var poly := AudioStreamPolyphonic.new()
	poly.polyphony = 8
	_player.stream = poly
	add_child(_player)
	_ambient = AudioStreamPlayer.new()
	_ambient.bus = &"Music"
	_ambient.volume_db = -6.0
	add_child(_ambient)
	set_ambient(Settings.get_value("audio/menu_ambient"))


func play(event: String) -> void:
	history.append(event)
	if history.size() > 32:
		history = history.slice(-32)
	var s: AudioStream = streams.get(event)
	if s == null:
		return
	if not _player.playing:
		_player.play()
	var pb := _player.get_stream_playback() as AudioStreamPlaybackPolyphonic
	if pb:
		pb.play_stream(s)


## Any other sound on the UI channel (the Profile page's taunt preview). False if it isn't there.
func play_file(path: String) -> bool:
	if not ResourceLoader.exists(path):
		return false
	if not _player.playing:
		_player.play()
	var pb := _player.get_stream_playback() as AudioStreamPlaybackPolyphonic
	if pb:
		pb.play_stream(load(path))
	return true


## Quake 3 plays the enter sound once the new menu has drawn its first frame.
func play_in() -> void:
	if _in_pending:
		return
	_in_pending = true
	await get_tree().process_frame
	_in_pending = false
	play("in")


func move() -> void:
	if not quiet:
		play("move")


func out() -> void:
	play("out")


func buzz() -> void:
	play("buzz")


func change() -> void:
	play("change")


func set_ambient(on: bool) -> void:
	if _ambient == null:
		return  # Settings applies before this autoload is ready; _ready() reads the setting itself
	if on and _ambient.stream == null and ResourceLoader.exists(AMBIENT):
		var s := (load(AMBIENT) as AudioStreamWAV).duplicate() as AudioStreamWAV
		s.loop_mode = AudioStreamWAV.LOOP_FORWARD
		s.loop_begin = 0
		s.loop_end = int(s.get_length() * s.mix_rate)
		_ambient.stream = s
	if on and not _ambient.playing and _ambient.stream:
		_ambient.play()
	elif not on:
		_ambient.stop()
