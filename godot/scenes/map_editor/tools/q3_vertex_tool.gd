class_name Q3VertexTool
extends Q3EditorTool
## The vertex tool (V), after OZMIUM's VertexTool: handles on the selected brushes' corners and the
## selected patches' control points. Click a handle to select it (Shift adds, Ctrl toggles); drag
## moves the selected handles on the grid (Alt vertical, Ctrl no snap) on the axis plane that faces
## the camera best, and each brush is rebuilt as the convex hull of its corners. Delete removes the
## selected corners (one corner: a chamfer; a box edge: a wedge). Shift+V lands a dragged corner on
## another brush's corner within reach; Ctrl+Shift+G snaps the selected corners to the grid.

const K = preload("res://scenes/map_editor/q3_editor_keymap.gd")
const PICK_PIXELS := 10.0
const SNAP_PIXELS := 14.0
const HANDLE := Color(1.0, 0.9, 0.4)
const HANDLE_SELECTED := Color(1.0, 0.25, 0.15)

var _dragging := false
var _grab := Vector3.ZERO           ## the grabbed handle's position when the drag began
var _normal := Vector3.ZERO         ## the drag plane's normal
var _vertical := false
var _orig := {}                     ## object id -> its points (PackedFloat64Array) when the drag began
var _moving := {}                   ## object id -> [indices being dragged]
var _start_objs := {}               ## object id -> the object before the drag
var delta := Vector3.ZERO           ## the move so far (id units)
var _hover := Vector2i(-1, -1)


func _init(e: Q3MapEditor) -> void:
	super(e)
	title = "Vertex"


func activate() -> void:
	editor.verts.clear()


func busy() -> bool:
	return _dragging


func cancel() -> void:
	if _dragging:
		editor.doc.begin_batch()
		for id in _start_objs:
			editor.doc.put(_start_objs[id])
		editor.doc.end_batch()
		editor.doc.drop_checkpoint()
	_dragging = false


func wants_2d() -> bool:
	return true


## Every handle on show: [Vector2i(id, index), id-space point]. brushes_only: corners, no control
## points (the select tool's handles).
func handles(brushes_only := false) -> Array:
	var out := []
	for id in editor.selected_ids():
		var o := editor.doc.get_obj(id)
		if o.get("type", "") not in (["brush"] if brushes_only else ["brush", "patch"]):
			continue
		var pts := editor.object_points(id)
		for i in pts.size() / 3:
			out.append([Vector2i(id, i), Vector3(pts[i * 3], pts[i * 3 + 1], pts[i * 3 + 2])])
	return out


## The handle nearest the cursor within reach, or Vector2i(-1, -1).
func handle_at(pos: Vector2, brushes_only := false) -> Vector2i:
	var best := Vector2i(-1, -1)
	var best_d := PICK_PIXELS
	for h in handles(brushes_only):
		var g := Q3MapEditor.to_godot(h[1])
		if editor.camera.is_position_behind(g):
			continue
		var d := editor.camera.unproject_position(g).distance_to(pos)
		if d < best_d:
			best_d = d
			best = h[0]
	return best


func click(pos: Vector2, mods: int, _double: bool) -> void:
	var h := handle_at(pos)
	if h.x >= 0:
		select_handle(h, mods)
		return
	var p := editor.pick(pos)
	if p.is_empty() or p["locked"]:
		if mods == 0:
			editor.verts.clear()
			editor.clear_selection()
		return
	editor.verts.clear()
	editor.select([p["id"]], "toggle" if mods & K.CTRL else ("add" if mods & K.SHIFT else "set"))
	UiSound.move()


func select_handle(h: Vector2i, mods: int) -> void:
	if mods & K.CTRL:
		if editor.verts.has(h):
			editor.verts.erase(h)
		else:
			editor.verts[h] = true
	else:
		if not mods & K.SHIFT:
			editor.verts.clear()
		editor.verts[h] = true
	UiSound.move()
	editor.status_changed.emit()


func drag_start(pos: Vector2, mods: int) -> void:
	var h := handle_at(pos)
	if h.x >= 0:
		begin_move(h, mods)


## Starts moving the selected handles (h joins them first) from handle h; the select tool's corner
## handles start their drags here.
func begin_move(h: Vector2i, mods: int) -> void:
	if not editor.verts.has(h):
		select_handle(h, mods & K.SHIFT)
	_orig.clear()
	_moving.clear()
	_start_objs.clear()
	for k: Vector2i in editor.verts:
		if not _orig.has(k.x):
			_orig[k.x] = editor.object_points(k.x)
			_moving[k.x] = []
			_start_objs[k.x] = editor.doc.get_obj(k.x)
		_moving[k.x].append(k.y)
	var pts: PackedFloat64Array = _orig[h.x]
	_grab = Vector3(pts[h.y * 3], pts[h.y * 3 + 1], pts[h.y * 3 + 2])
	_set_plane(mods & K.ALT != 0)
	editor.doc.checkpoint("Move vertices")
	_dragging = true
	delta = Vector3.ZERO


## The axis plane through the grabbed handle that faces the camera best: the floor plane from
## above, a wall from the side. Alt: the upright one, for moving straight up and down.
func _set_plane(vertical: bool) -> void:
	_vertical = vertical
	var g := -editor.camera.global_transform.basis.z
	var f := Vector3(g.z, g.x, g.y)   # Godot -> id
	if vertical and editor.view_axis() < 0:
		f.z = 0
	var axis := f.abs().max_axis_index()
	_normal = Vector3.ZERO
	_normal[axis] = 1.0


func drag(pos: Vector2, mods: int) -> void:
	if not _dragging:
		return
	if (mods & K.ALT != 0) != _vertical:
		_set_plane(mods & K.ALT != 0)
	var p: Variant = editor.plane_point(pos, _grab, _normal)
	if p == null:
		return
	var target: Vector3 = p
	if mods & K.CTRL == 0:
		target = editor.grid.snap_vec(target, true)
	var d := target - _grab
	d -= _normal * d.dot(_normal)       # stay in the drag plane
	if _vertical:
		d = Vector3(0, 0, d.z)
	if editor.vertex_snap:
		var s: Variant = _snap_target(_grab + d)
		if s != null:
			d = (s as Vector3) - _grab
	if d == delta:
		return
	if _apply(d):
		delta = d
		editor.ui.set_hint("Move %s" % Q3SelectTool._fmt(delta))


## Another brush's corner within reach of where the grabbed handle would go, or null.
func _snap_target(at: Vector3) -> Variant:
	var at_screen := editor.camera.unproject_position(Q3MapEditor.to_godot(at))
	var best: Variant = null
	var best_d := SNAP_PIXELS
	for id in editor.doc.objects:
		if _orig.has(id) or editor.doc.hidden.has(id) or editor.doc.objects[id]["type"] != "brush":
			continue
		var pts: PackedFloat64Array = Q3MapGeo.brush_vertices(editor.doc.objects[id]["planes"])
		for i in pts.size() / 3:
			var q := Vector3(pts[i * 3], pts[i * 3 + 1], pts[i * 3 + 2])
			var g := Q3MapEditor.to_godot(q)
			if editor.camera.is_position_behind(g):
				continue
			var dd := editor.camera.unproject_position(g).distance_to(at_screen)
			if dd < best_d:
				best_d = dd
				best = q
	return best


## Rebuilds every dragged object with its moving points offset. False (nothing changed) when a
## brush would flatten.
func _apply(d: Vector3) -> bool:
	var out := {}
	for id in _orig:
		var pts: PackedFloat64Array = _orig[id].duplicate()
		for i in _moving[id]:
			pts[i * 3] += d.x
			pts[i * 3 + 1] += d.y
			pts[i * 3 + 2] += d.z
		var o: Dictionary = _start_objs[id]
		if o["type"] == "brush":
			var b := Q3MapEditor.rebuild_from_points(o, pts)
			if b.is_empty():
				return false
			out[id] = b
		else:
			var pp := o.duplicate(true)
			var c: PackedFloat64Array = pp["ctrl"]
			for i in pts.size() / 3:
				c[i * 5] = pts[i * 3]
				c[i * 5 + 1] = pts[i * 3 + 1]
				c[i * 5 + 2] = pts[i * 3 + 2]
			pp["ctrl"] = c
			out[id] = pp
	editor.doc.begin_batch()
	for id in out:
		editor.doc.put(out[id])
	editor.doc.end_batch()
	return true


func drag_end(_pos: Vector2, _mods: int) -> void:
	if not _dragging:
		return
	_dragging = false
	if delta == Vector3.ZERO:
		editor.doc.drop_checkpoint()
	else:
		_reselect(delta)
	delta = Vector3.ZERO
	editor.ui.set_hint("")


## A rebuilt brush numbers its corners anew: find the moved ones again by where they went.
func _reselect(d: Vector3) -> void:
	var wanted := {}
	for id in _moving:
		var pts: PackedFloat64Array = _orig[id]
		for i in _moving[id]:
			if not wanted.has(id):
				wanted[id] = []
			wanted[id].append(Vector3(pts[i * 3], pts[i * 3 + 1], pts[i * 3 + 2]) + d)
	editor.verts.clear()
	for id in wanted:
		var now := editor.object_points(id)
		for i in now.size() / 3:
			var q := Vector3(now[i * 3], now[i * 3 + 1], now[i * 3 + 2])
			for w: Vector3 in wanted[id]:
				if q.distance_to(w) < 0.01:
					editor.verts[Vector2i(id, i)] = true
	editor.status_changed.emit()


func hover(pos: Vector2, _mods: int) -> void:
	_hover = handle_at(pos)


func key(ev: InputEventKey) -> bool:
	if ev.keycode == KEY_ESCAPE:
		if _dragging:
			cancel()
			return true
		if editor.verts.size():
			editor.verts.clear()
			editor.status_changed.emit()
			return true
		return false
	if ev.keycode in [KEY_DELETE, KEY_BACKSPACE] and not ev.ctrl_pressed and editor.verts.size():
		delete_corners()
		return true
	return false


## Arrow nudges move the selected handles instead of the whole selection.
func nudge(d: Vector3) -> bool:
	if editor.verts.is_empty():
		return false
	_orig.clear()
	_moving.clear()
	_start_objs.clear()
	for k: Vector2i in editor.verts:
		if not _orig.has(k.x):
			_orig[k.x] = editor.object_points(k.x)
			_moving[k.x] = []
			_start_objs[k.x] = editor.doc.get_obj(k.x)
		_moving[k.x].append(k.y)
	editor.doc.checkpoint("Nudge vertices")
	if _apply(d):
		_reselect(d)
	else:
		editor.doc.drop_checkpoint()
		UiSound.buzz()
	return true


## Removes the selected corners and rebuilds each brush from the rest. Refused when a brush would be
## left with fewer than four corners or would flatten. Patch control points can't be deleted.
func delete_corners() -> void:
	var by := {}
	for k: Vector2i in editor.verts:
		if editor.doc.get_obj(k.x).get("type", "") != "brush":
			editor.notify("Patch control points can't be deleted", true)
			return
		if not by.has(k.x):
			by[k.x] = []
		by[k.x].append(k.y)
	var out := {}
	for id in by:
		var pts := editor.object_points(id)
		var keep := PackedFloat64Array()
		for i in pts.size() / 3:
			if not by[id].has(i):
				keep.append_array([pts[i * 3], pts[i * 3 + 1], pts[i * 3 + 2]])
		var b := Q3MapEditor.rebuild_from_points(editor.doc.get_obj(id), keep) if keep.size() >= 12 else {}
		if b.is_empty():
			editor.notify("That would leave a flat brush", true)
			return
		out[id] = b
	editor.doc.checkpoint("Delete corners")
	editor.doc.begin_batch()
	for id in out:
		editor.doc.put(out[id])
	editor.doc.end_batch()
	editor.verts.clear()
	editor.status_changed.emit()


## Ctrl+Shift+G: the selected corners (all corners of the selected brushes when none are) onto the grid.
func snap_to_grid() -> void:
	var ids := {}
	if editor.verts.size():
		for k: Vector2i in editor.verts:
			if not ids.has(k.x):
				ids[k.x] = []
			ids[k.x].append(k.y)
	else:
		for id in editor.selected_ids():
			if editor.doc.get_obj(id).get("type", "") in ["brush", "patch"]:
				var n := editor.object_points(id).size() / 3
				ids[id] = range(n)
	if ids.is_empty():
		return
	var out := {}
	for id in ids:
		var o := editor.doc.get_obj(id)
		var pts := editor.object_points(id)
		for i in ids[id]:
			var q := editor.grid.snap_vec(Vector3(pts[i * 3], pts[i * 3 + 1], pts[i * 3 + 2]), true)
			pts[i * 3] = q.x
			pts[i * 3 + 1] = q.y
			pts[i * 3 + 2] = q.z
		if o["type"] == "brush":
			var b := Q3MapEditor.rebuild_from_points(o, pts)
			if b.is_empty():
				editor.notify("Snapping would flatten a brush", true)
				return
			out[id] = b
		else:
			var pp := o.duplicate(true)
			var c: PackedFloat64Array = pp["ctrl"]
			for i in pts.size() / 3:
				c[i * 5] = pts[i * 3]
				c[i * 5 + 1] = pts[i * 3 + 1]
				c[i * 5 + 2] = pts[i * 3 + 2]
			pp["ctrl"] = c
			out[id] = pp
	editor.doc.checkpoint("Snap vertices")
	editor.doc.begin_batch()
	for id in out:
		editor.doc.put(out[id])
	editor.doc.end_batch()
	editor.verts.clear()
	editor.status_changed.emit()


func draw_2d(canvas: Control) -> void:
	for h in handles():
		var g := Q3MapEditor.to_godot(h[1])
		if editor.camera.is_position_behind(g):
			continue
		var at := editor.camera.unproject_position(g)
		var sel: bool = editor.verts.has(h[0])
		var r := 6.0 if (sel or h[0] == _hover) else 4.5
		var rect := Rect2(at - Vector2(r, r), Vector2(r, r) * 2)
		canvas.draw_rect(rect, HANDLE_SELECTED if sel else HANDLE)
		canvas.draw_rect(rect, Color.BLACK, false, 1.0)


func hint() -> String:
	return "Click a corner (Shift add, Ctrl toggle) · drag to move (Alt vertical, Ctrl no snap) · Del removes corners · Shift+V vertex snap · Ctrl+Shift+G snap to grid"
