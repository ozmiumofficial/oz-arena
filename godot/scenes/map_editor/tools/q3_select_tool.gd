class_name Q3SelectTool
extends Q3EditorTool
## The default tool, after OZMIUM's SelectTool: click to select (a grouped object takes its whole
## group; double-click or Ctrl reaches inside), Shift adds, Ctrl toggles; drag a selected or unselected
## brush to move the selection on the grid (Alt vertical, Ctrl no snap); drag on empty space to box
## select.
## Selected brushes are editable in place: every corner gets a square and every face turned to the
## camera a diamond on a stem out of its middle. Dragging a corner moves it (the vertex tool does the
## work; click selects corners, Delete removes them), dragging a diamond pushes or pulls its face (the
## face tool does the work), and Shift+drag on any face of a brush pushes or pulls that face directly.

const K = preload("res://scenes/map_editor/q3_editor_keymap.gd")
const MAX_CORNERS := 512            ## handles show for selections up to this many corners
const PICK_PIXELS := 10.0           ## reach of a corner or a diamond
const FACE_OFFSET_PIXELS := 28.0    ## how far a diamond floats out of its face, on screen
const STEM_PICK_PIXELS := 8.0       ## reach of the stem's outer half
const STEM_GRAB_START := 0.5        ## the stem's inner half belongs to the face (dragging there moves the brush)
const CORNER := Color(0.2, 0.9, 1.0)
const CORNER_SELECTED := Color(1.0, 0.35, 0.2)
const CORNER_HOVER := Color(1.0, 1.0, 1.0)
const FACE := Color(0.3, 1.0, 0.5)
const FACE_HOVER := Color(1.0, 0.82, 0.25)

var _moving := false
var _boxing := false
var _start := Vector2.ZERO
var _anchor := Vector3.ZERO     ## where the drag plane was grabbed (id)
var _normal := Vector3.ZERO     ## the drag plane's normal
var _vertical := false
var _base := Vector3.ZERO       ## delta carried over from before an Alt switch
var _min := Vector3.ZERO        ## the selection's min corner when the drag began
var delta := Vector3.ZERO       ## the move so far (id units), applied on release
var _forward: Q3EditorTool      ## the vertex or face tool running a handle drag for us
var _hover_handle := {}         ## the handle under the cursor: {kind: "corner" | "face", key: Vector2i, ...}
var _corners := {}              ## brush id -> [the brush, its corners], cached until it changes


func _init(e: Q3MapEditor) -> void:
	super(e)
	title = "Select"


func busy() -> bool:
	return _moving or _boxing or _forward != null


func cancel() -> void:
	if _forward:
		_forward.cancel()
		_forward = null
	if _moving:
		editor.overlay.set_selection_offset(Vector3.ZERO)
	_moving = false
	_boxing = false
	delta = Vector3.ZERO
	editor.ui.hide_rect()


func click(pos: Vector2, mods: int, double: bool) -> void:
	var h := handle_at(pos)
	if h.get("kind", "") == "corner":
		_prune_corners()
		_vertex_tool().select_handle(h["key"], mods)
		return
	if h.get("kind", "") == "face":
		return   # a diamond only drags
	var p := editor.pick(pos, true)
	if p.is_empty():
		if mods == 0:
			editor.verts.clear()
			editor.clear_selection()
		return
	if p["locked"]:
		return   # a locked brush stops the ray but is never selected from the viewport
	var id: int = p["id"]
	var ids := [id] if double or mods & K.CTRL else editor.group_of(id)
	var mode := "toggle" if mods & K.CTRL else ("add" if mods & K.SHIFT else "set")
	if mods == 0 and editor.verts.size():
		editor.verts.clear()   # a plain click on the body drops the corners (Delete deletes the brush again)
		editor.status_changed.emit()
	editor.select(ids, mode)
	UiSound.move()


func drag_start(pos: Vector2, mods: int) -> void:
	_start = pos
	var h := handle_at(pos)
	if h.get("kind", "") == "corner":
		_prune_corners()
		var vt := _vertex_tool()
		vt.begin_move(h["key"], mods)
		_forward = vt
		return
	if h.get("kind", "") == "face":
		# grab where the press ray passes the face's axis, so the face doesn't jump to the cursor
		var along: Variant = Q3FaceTool.along_axis(editor.ray(pos), h["center"], h["normal"])
		var grab: Vector3 = h["center"] + h["normal"] * (along if along != null else 0.0)
		_push(h["key"], grab, h["normal"])
		return
	var p := editor.pick(pos, true)
	if p.is_empty() or p["locked"]:
		_boxing = true
		return
	if mods & K.SHIFT and p["kind"] == "brush":
		# Shift+drag on a face pushes or pulls it: the diamond's drag, without aiming for it
		if not editor.selection.has(p["id"]):
			editor.select([p["id"]], "add")
		_push(Vector2i(p["id"], p["face"]), p["point"], p["normal"])
		return
	if not editor.selection.has(p["id"]):
		editor.select(editor.group_of(p["id"]), "add" if mods & K.SHIFT else "set")
	_moving = true
	_base = Vector3.ZERO
	_min = editor.selection_bounds().position
	_grab(p["point"], mods & K.ALT != 0)


func _push(key: Vector2i, grab: Vector3, normal: Vector3) -> void:
	var ft: Q3FaceTool = editor.tools["face"]
	ft.begin_push([key], grab, normal, "Push / pull face")
	_forward = ft
	_hover_handle = {"kind": "face", "key": key}


func _vertex_tool() -> Q3VertexTool:
	return editor.tools["vertex"]


## Only the selected brushes' corners count here (not ones left from brushes no longer selected,
## nor the vertex tool's curve control points, nor indices an undo or redo took past the brush's
## corners).
func _prune_corners() -> void:
	var counts := {}
	for k: Vector2i in editor.verts.keys():
		if not editor.selection.has(k.x) or editor.doc.get_obj(k.x).get("type", "") != "brush":
			editor.verts.erase(k)
			continue
		if not counts.has(k.x):
			counts[k.x] = editor.object_points(k.x).size() / 3
		if k.y >= counts[k.x]:
			editor.verts.erase(k)


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
	if _forward:
		_forward.drag(pos, mods)
		return
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
	if _forward:
		var pushed := _forward is Q3FaceTool
		_forward.drag_end(pos, mods)
		_forward = null
		if pushed:
			editor.verts.clear()   # the brush numbers its corners anew
		hover(pos, mods)
		return
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
	var h := handle_at(pos)
	if h.get("kind", "") != _hover_handle.get("kind", "") or h.get("key", Vector2i(-1, -1)) != _hover_handle.get("key", Vector2i(-1, -1)):
		_hover_handle = h
		editor.ui.set_hint("")
	if h.size():
		editor.hover = {}
		editor.overlay.clear_hover()
		return
	var p := editor.pick(pos, true)
	editor.hover = p
	if p.is_empty() or editor.selection.has(p["id"]) or p["locked"]:
		editor.overlay.clear_hover()
	else:
		editor.overlay.show_hover(editor.doc, p["id"])


func key(ev: InputEventKey) -> bool:
	if ev.keycode == KEY_ESCAPE:
		if busy():
			cancel()
			return true
		_prune_corners()
		if editor.verts.size():
			editor.verts.clear()
			editor.status_changed.emit()
			return true
		return false
	if ev.keycode in [KEY_DELETE, KEY_BACKSPACE] and not ev.ctrl_pressed and not busy():
		_prune_corners()
		if editor.verts.size():
			_vertex_tool().delete_corners()
			return true
	return false


func hint() -> String:
	match _hover_handle.get("kind", ""):
		"face":
			return "Drag: push / pull this face (Ctrl no snap)"
		"corner":
			return "Drag: move this corner (Alt vertical, Ctrl no snap) · click: select corners (Shift add, Ctrl toggle) · Del removes them"
	_prune_corners()
	if editor.verts.size():
		return "Drag a corner to reshape · Del removes the selected corners · Esc keeps the brush, drops the corners"
	if editor.selection.size():
		return "Drag to move (Alt vertical, Ctrl no snap) · corners and diamonds reshape · Shift+drag a face to push / pull it · Ctrl+click toggles"
	return "Click to select · drag to move (Alt vertical, Ctrl no snap) · drag on empty space to box select · Shift+drag a face to push / pull it"


# ---------------------------------------------------------------- handles

## The selected brushes' corners: [[Vector2i(id, index), id point], ...], or [] when there are more
## than MAX_CORNERS (a big selection would be all squares).
func corners() -> Array:
	var out := []
	for id in editor.selection:
		var b := editor.doc.get_obj(id)
		if b.get("type", "") != "brush":
			continue
		var c: Array = _corners.get(id, [])
		if c.is_empty() or not is_same(c[0], b):
			c = [b, Q3MapGeo.brush_vertices(b["planes"])]
			_corners[id] = c
		var pts: PackedFloat64Array = c[1]
		if out.size() + pts.size() / 3 > MAX_CORNERS:
			return []
		for i in pts.size() / 3:
			out.append([Vector2i(id, i), Vector3(pts[i * 3], pts[i * 3 + 1], pts[i * 3 + 2])])
	for id in _corners.keys():
		if not editor.selection.has(id):
			_corners.erase(id)
	return out


## The diamonds of the selected brushes' faces turned toward the camera: [{key, center, normal,
## tip}, ...] in id space. Faces the view filters hide have none.
func face_handles() -> Array:
	var out := []
	var eye := Q3MapEditor.to_id(editor.camera.global_position)
	for id in editor.selection:
		var b := editor.doc.get_obj(id)
		if b.get("type", "") != "brush":
			continue
		var w := editor.doc.windings(id)
		var pl: PackedFloat64Array = b["planes"]
		for f in w.size():
			var pts: PackedFloat64Array = w[f]
			if pts.size() < 9 or (editor.filters.size() and editor.face_filtered(b["faces"][f]["material"])):
				continue
			var c := Vector3.ZERO
			for i in pts.size() / 3:
				c += Vector3(pts[i * 3], pts[i * 3 + 1], pts[i * 3 + 2])
			c /= pts.size() / 3
			var n := Vector3(pl[f * 4], pl[f * 4 + 1], pl[f * 4 + 2])
			if n.dot(c - eye) >= 0.0:
				continue   # turned away: its diamond would sit behind the brush
			out.append({"key": Vector2i(id, f), "center": c, "normal": n, "tip": c + n * _units_per_pixel(c) * FACE_OFFSET_PIXELS})
	return out


## Map units one screen pixel covers at an id point's depth.
func _units_per_pixel(at: Vector3) -> float:
	var cam := editor.camera
	var depth := maxf(0.05, (Q3MapEditor.to_godot(at) - cam.global_position).dot(-cam.global_transform.basis.z))
	var h := maxf(1.0, cam.get_viewport().get_visible_rect().size.y)
	return 2.0 * depth * tan(deg_to_rad(cam.fov * 0.5)) / h * 32.0


## The handle under the cursor: {kind: "corner", key} or {kind: "face", key, center, normal, tip},
## or {}. Corners win over diamonds; on screen nearest wins, nearer the camera on a tie.
func handle_at(pos: Vector2) -> Dictionary:
	if editor.selection.is_empty():
		return {}
	var cs := corners()
	if cs.is_empty():
		return {}
	var best := {}
	var best_d := PICK_PIXELS
	var best_depth := INF
	for c in cs:
		var g := Q3MapEditor.to_godot(c[1])
		if editor.camera.is_position_behind(g):
			continue
		var d := editor.camera.unproject_position(g).distance_to(pos)
		var depth := editor.camera.global_position.distance_to(g)
		if d < best_d or (d == best_d and depth < best_depth):
			best_d = d
			best_depth = depth
			best = {"kind": "corner", "key": c[0]}
	if best.size():
		return best
	for h in face_handles():
		var gt := Q3MapEditor.to_godot(h["tip"])
		var gc := Q3MapEditor.to_godot(h["center"])
		if editor.camera.is_position_behind(gt) or editor.camera.is_position_behind(gc):
			continue
		var tip := editor.camera.unproject_position(gt)
		var center := editor.camera.unproject_position(gc)
		var d := tip.distance_to(pos)
		# the stem's outer half grabs too; its inner half is left to the face, which moves the brush
		var stem := Geometry2D.get_closest_point_to_segment(pos, center.lerp(tip, STEM_GRAB_START), tip).distance_to(pos)
		if stem <= STEM_PICK_PIXELS:
			d = minf(d, stem)
		var depth := editor.camera.global_position.distance_to(gt)
		if d < best_d or (d == best_d and depth < best_depth):
			best_d = d
			best_depth = depth
			best = h.duplicate()
			best["kind"] = "face"
	return best


func wants_2d() -> bool:
	return editor.selection.size() > 0


## Stems and diamonds for the faces, squares for the corners (selected ones red); the hovered or
## dragged handle grows. Nothing while the selection is being moved or a box drawn.
func draw_2d(canvas: Control) -> void:
	if _moving or _boxing:
		return
	var cs := corners()
	if cs.is_empty():
		return
	var cam := editor.camera
	var hover_key: Vector2i = _hover_handle.get("key", Vector2i(-1, -1))
	var hover_kind: String = _hover_handle.get("kind", "")
	for h in face_handles():
		var gt := Q3MapEditor.to_godot(h["tip"])
		var gc := Q3MapEditor.to_godot(h["center"])
		if cam.is_position_behind(gt) or cam.is_position_behind(gc):
			continue
		var tip := cam.unproject_position(gt)
		var on: bool = hover_kind == "face" and h["key"] == hover_key
		var col := FACE_HOVER if on else FACE
		canvas.draw_line(cam.unproject_position(gc), tip, col, 3.0 if on else 2.0, true)
		var r := 10.0 if on else 8.0
		var diamond := PackedVector2Array([tip + Vector2(0, -r), tip + Vector2(r, 0), tip + Vector2(0, r), tip + Vector2(-r, 0)])
		canvas.draw_colored_polygon(diamond, col)
		diamond.append(diamond[0])
		canvas.draw_polyline(diamond, Color.BLACK, 1.0)
	for c in cs:
		var g := Q3MapEditor.to_godot(c[1])
		if cam.is_position_behind(g):
			continue
		var at := cam.unproject_position(g)
		var sel: bool = editor.verts.has(c[0])
		var on: bool = hover_kind == "corner" and c[0] == hover_key
		var r := 6.0 if on else (5.0 if sel else 4.0)
		var rect := Rect2(at - Vector2(r, r), Vector2(r, r) * 2)
		canvas.draw_rect(rect, CORNER_SELECTED if sel else (CORNER_HOVER if on else CORNER))
		canvas.draw_rect(rect, Color.BLACK, false, 1.0)
