class_name Q3SelectTool
extends Q3EditorTool
## The default tool, after OZMIUM's SelectTool: click to select (a grouped object takes its whole
## group; double-click or Ctrl reaches inside), Shift adds, Ctrl toggles; drag a selected or unselected
## brush to move the selection on the grid (Alt vertical, Ctrl no snap); drag on empty space to box
## select.

const K = preload("res://scenes/map_editor/q3_editor_keymap.gd")

var _moving := false
var _boxing := false
var _start := Vector2.ZERO
var _anchor := Vector3.ZERO     ## where the drag plane was grabbed (id)
var _normal := Vector3.ZERO     ## the drag plane's normal
var _vertical := false
var _base := Vector3.ZERO       ## delta carried over from before an Alt switch
var _min := Vector3.ZERO        ## the selection's min corner when the drag began
var delta := Vector3.ZERO       ## the move so far (id units), applied on release


func _init(e: Q3MapEditor) -> void:
	super(e)
	title = "Select"


func busy() -> bool:
	return _moving or _boxing


func cancel() -> void:
	if _moving:
		editor.overlay.set_selection_offset(Vector3.ZERO)
	_moving = false
	_boxing = false
	delta = Vector3.ZERO
	editor.ui.hide_rect()


func click(pos: Vector2, mods: int, double: bool) -> void:
	var p := editor.pick(pos, true)
	if p.is_empty():
		if mods == 0:
			editor.clear_selection()
		return
	if p["locked"]:
		return   # a locked brush stops the ray but is never selected from the viewport
	var id: int = p["id"]
	var ids := [id] if double or mods & K.CTRL else editor.group_of(id)
	var mode := "toggle" if mods & K.CTRL else ("add" if mods & K.SHIFT else "set")
	editor.select(ids, mode)
	UiSound.move()


func drag_start(pos: Vector2, mods: int) -> void:
	_start = pos
	var p := editor.pick(pos, true)
	if p.is_empty() or p["locked"]:
		_boxing = true
		return
	if not editor.selection.has(p["id"]):
		editor.select(editor.group_of(p["id"]), "add" if mods & K.SHIFT else "set")
	_moving = true
	_base = Vector3.ZERO
	_min = editor.selection_bounds().position
	_grab(p["point"], mods & K.ALT != 0)


## Starts the drag plane through point: the ground plane, or (Alt) an upright one facing the camera.
func _grab(point: Vector3, vertical: bool) -> void:
	_anchor = point
	_set_plane(vertical)


func _set_plane(vertical: bool) -> void:
	_vertical = vertical
	if vertical:
		var g := -editor.camera.global_transform.basis.z
		var f := Vector3(g.z, g.x, 0)
		_normal = f.normalized() if f.length() > 1e-4 else Vector3(1, 0, 0)
	else:
		_normal = Vector3(0, 0, 1)


func drag(pos: Vector2, mods: int) -> void:
	if _boxing:
		editor.ui.show_rect(Rect2(_start, pos - _start).abs())
		return
	if not _moving:
		return
	var alt := mods & K.ALT != 0
	if alt != _vertical:
		# switch planes through where the grab point is now, so the switch itself moves nothing
		var q := _anchor + (delta - _base)
		_base = delta
		_set_plane(alt)
		var here: Variant = editor.plane_point(pos, q, _normal)
		_anchor = here if here != null else q
	var p: Variant = editor.plane_point(pos, _anchor, _normal)
	if p == null:
		return
	var raw: Vector3 = (p as Vector3) - _anchor
	var snap := mods & K.CTRL == 0
	var d := _base
	if _vertical:
		d.z = _snap_axis(_min.z, _base.z + raw.z, snap)
	else:
		d.x = _snap_axis(_min.x, _base.x + raw.x, snap)
		d.y = _snap_axis(_min.y, _base.y + raw.y, snap)
	delta = d
	editor.overlay.set_selection_offset(delta)
	editor.ui.set_hint("Move %s" % _fmt(delta))


## The move along one axis that lands the selection's min corner on the grid.
func _snap_axis(min_v: float, move: float, snap: bool) -> float:
	if not snap:
		return move
	return editor.grid.snap(min_v + move, true) - min_v


static func _fmt(v: Vector3) -> String:
	return "%s %s %s" % [Q3MapEditor._num(v.x), Q3MapEditor._num(v.y), Q3MapEditor._num(v.z)]


func drag_end(pos: Vector2, mods: int) -> void:
	if _boxing:
		_box_select(Rect2(_start, pos - _start).abs(), mods)
	elif _moving:
		var d := delta
		editor.overlay.set_selection_offset(Vector3.ZERO)
		editor.move_selection(d)
	_moving = false
	_boxing = false
	delta = Vector3.ZERO
	editor.ui.hide_rect()
	editor.ui.set_hint("")


## Objects whose every corner lands inside the rectangle (and in front of the camera).
func _box_select(rect: Rect2, mods: int) -> void:
	var ids := []
	for id in editor.doc.objects:
		var o: Dictionary = editor.doc.objects[id]
		if editor.doc.hidden.has(id) or editor.doc.locked.has(id) or o["type"] not in ["brush", "patch", "entity"] or editor.filtered(id):
			continue
		var v: PackedFloat64Array = editor.object_points(id)
		var inside := v.size() > 0
		for i in v.size() / 3:
			var g := Q3MapEditor.to_godot(Vector3(v[i * 3], v[i * 3 + 1], v[i * 3 + 2]))
			if editor.camera.is_position_behind(g) or not rect.has_point(editor.camera.unproject_position(g)):
				inside = false
				break
		if inside:
			ids.append(id)
	editor.select(ids, "toggle" if mods & K.CTRL else ("add" if mods & K.SHIFT else "set"))


func hover(pos: Vector2, _mods: int) -> void:
	var p := editor.pick(pos, true)
	editor.hover = p
	if p.is_empty() or editor.selection.has(p["id"]) or p["locked"]:
		editor.overlay.clear_hover()
	else:
		editor.overlay.show_hover(editor.doc, p["id"])


func key(ev: InputEventKey) -> bool:
	if ev.keycode == KEY_ESCAPE and busy():
		cancel()
		return true
	return false


func hint() -> String:
	return "Click to select · drag to move (Alt vertical, Ctrl no snap) · drag on empty space to box select"
