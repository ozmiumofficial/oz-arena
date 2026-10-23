class_name Q3CinematicPlayer
extends CanvasLayer
## Plays Quake 3's cinematics (library/cinematics, converted by tools/make_cinematics.py) full
## screen over whatever is up: 4:3, pillarboxed on wider screens like the engine's stretched RoQ.
## A queue plays in order ("idlogo", "intro" at first launch); Escape, Enter, Space, a click or a
## pad button skips to the next one, as the engine's cinematic skip does. `finished` fires at the end.

signal finished

const DIR := "res://library/cinematics/"

var queue: PackedStringArray = []
var current := ""
var _player: VideoStreamPlayer
var _hint: Label
var _since := 0.0


static func path_for(id: String) -> String:
	return DIR + id + ".ogv"


static func exists(id: String) -> bool:
	return ResourceLoader.exists(path_for(id))


## Adds a player to `parent` and starts the queue; it frees itself when done.
static func play(parent: Node, ids: PackedStringArray) -> Q3CinematicPlayer:
	var p := Q3CinematicPlayer.new()
	p.queue = ids
	parent.add_child(p)
	return p


func _ready() -> void:
	layer = 100
	var bg := ColorRect.new()
	bg.color = Color.BLACK
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	bg.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(bg)
	var box := AspectRatioContainer.new()
	box.ratio = 4.0 / 3.0
	box.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(box)
	_player = VideoStreamPlayer.new()
	_player.expand = true
	_player.bus = &"Music"
	_player.finished.connect(_next)
	box.add_child(_player)
	_hint = Label.new()
	_hint.text = "ESC  SKIP"
	_hint.add_theme_font_override("font", Q3Fonts.prop())
	_hint.add_theme_font_size_override("font_size", 16)
	_hint.add_theme_color_override("font_color", Color(1, 1, 1, 0.5))
	_hint.anchor_left = 1.0
	_hint.anchor_right = 1.0
	_hint.anchor_top = 1.0
	_hint.anchor_bottom = 1.0
	_hint.offset_left = -160
	_hint.offset_top = -48
	_hint.modulate.a = 0.0
	add_child(_hint)
	_next()


func _next() -> void:
	while queue.size() and not exists(queue[0]):
		queue.remove_at(0)
	if queue.is_empty():
		current = ""
		finished.emit()
		queue_free()
		return
	current = queue[0]
	queue.remove_at(0)
	_player.stream = load(path_for(current))
	_player.play()
	_since = 0.0


func _process(delta: float) -> void:
	_since += delta
	_hint.modulate.a = clampf((_since - 1.0) * 2.0, 0.0, 1.0) * clampf((5.0 - _since) * 0.5, 0.0, 1.0)


func _input(ev: InputEvent) -> void:
	var pressed := (ev is InputEventKey or ev is InputEventMouseButton or ev is InputEventJoypadButton) and ev.is_pressed() and not ev.is_echo()
	if pressed and _since > 0.4:
		get_viewport().set_input_as_handled()
		_player.stop()
		_next()
