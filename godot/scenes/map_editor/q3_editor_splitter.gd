extends Control
## A drag handle beside one of the map editor's panels: dragging it sets the panel's width (or height),
## double-clicking it folds the panel away or brings it back. `changed` fires when a drag ends or a fold
## flips, so the layout can be saved.

signal changed

var target: Control           ## the panel this handle sizes
var vertical := false         ## true: sizes the target's height (a handle under it)
var side := 1.0               ## 1: the target is left of (above) the handle, -1: right of (below) it
var lo := 220.0
var hi := 900.0

var _drag := false
var _from := 0.0
var _start := 0.0
var _hot := false


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_STOP
	mouse_default_cursor_shape = Control.CURSOR_VSIZE if vertical else Control.CURSOR_HSIZE
	if vertical:
		custom_minimum_size.y = 7
	else:
		custom_minimum_size.x = 7
	tooltip_text = "Drag to resize · double-click to fold away"
	mouse_entered.connect(func():
		_hot = true
		queue_redraw())
	mouse_exited.connect(func():
		_hot = false
		queue_redraw())


func _gui_input(ev: InputEvent) -> void:
	if ev is InputEventMouseButton and ev.button_index == MOUSE_BUTTON_LEFT:
		if ev.double_click and ev.pressed:
			if "folded" in target:
				target.set_folded(not target.folded)
			else:
				target.visible = not target.visible
			_drag = false
			UiSound.move()
			changed.emit()
		elif ev.pressed:
			_drag = true
			_from = _axis(ev.global_position)
			_start = _size()
			if not target.visible:
				target.visible = true
		elif _drag:
			_drag = false
			changed.emit()
		accept_event()
	elif ev is InputEventMouseMotion and _drag:
		set_size_of(_start + (_axis(ev.global_position) - _from) * side)
		accept_event()


func _axis(p: Vector2) -> float:
	return p.y if vertical else p.x


func _size() -> float:
	return target.custom_minimum_size.y if vertical else target.custom_minimum_size.x


func set_size_of(v: float) -> void:
	v = clampf(v, lo, hi)
	if vertical:
		target.custom_minimum_size.y = v
	else:
		target.custom_minimum_size.x = v


func _draw() -> void:
	var c := Q3Theme.GLOW if _hot or _drag else Color(0.3, 0.27, 0.25, 0.8)
	if vertical:
		draw_line(Vector2(size.x * 0.5 - 18, size.y * 0.5), Vector2(size.x * 0.5 + 18, size.y * 0.5), c, 2.0)
	else:
		draw_line(Vector2(size.x * 0.5, size.y * 0.5 - 18), Vector2(size.x * 0.5, size.y * 0.5 + 18), c, 2.0)
