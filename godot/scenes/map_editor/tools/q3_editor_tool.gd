class_name Q3EditorTool
extends RefCounted
## A map editor tool (OZMIUM's MapTool): the controller feeds it the left mouse button and hover,
## and offers it keys and nudges before the keymap does. Positions are screen pixels.

var editor: Q3MapEditor
var title := ""


func _init(e: Q3MapEditor) -> void:
	editor = e


func activate() -> void:
	pass


## Drops whatever is in progress (tool switch, Escape).
func cancel() -> void:
	pass


## True while a drag or a multi-step action is under way (Escape cancels it instead of deselecting).
func busy() -> bool:
	return false


func press(_pos: Vector2, _mods: int, _double: bool) -> void:
	pass


## A press and release without a drag.
func click(_pos: Vector2, _mods: int, _double: bool) -> void:
	pass


func drag_start(_pos: Vector2, _mods: int) -> void:
	pass


func drag(_pos: Vector2, _mods: int) -> void:
	pass


func drag_end(_pos: Vector2, _mods: int) -> void:
	pass


func hover(_pos: Vector2, _mods: int) -> void:
	pass


## Return true to take the key from the keymap.
func key(_ev: InputEventKey) -> bool:
	return false


## Return true to take an arrow-key nudge (id units) from the selection.
func nudge(_delta: Vector3) -> bool:
	return false


## The status bar's hint for this tool right now.
func hint() -> String:
	return ""


## True while the tool draws 2D marks over the view every frame (vertex handles).
func wants_2d() -> bool:
	return false


## Draws the tool's 2D marks on the UI's canvas control.
func draw_2d(_canvas: Control) -> void:
	pass
