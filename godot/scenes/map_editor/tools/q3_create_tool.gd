class_name Q3CreateTool
extends Q3EditorTool
## Box brushes, after OZMIUM's CreateBrushTool: drag a footprint on the surface under the cursor (or
## the ground grid), let go, move the mouse to set the height along the surface's normal, then click
## or press Enter. A click without a drag makes a one-cell footprint.

var state := "idle"            ## idle, footprint, height
var _axis := 2                 ## the axis the footprint plane faces (0..2)
var _sign := 1.0               ## which way out of the surface
var _plane := 0.0              ## the plane's coordinate on _axis
var _a := Vector3.ZERO         ## footprint corners (id), on the plane
var _b := Vector3.ZERO
var height := 0.0              ## along the normal (negative: into the surface)


func _init(e: Q3MapEditor) -> void:
	super(e)
	title = "Create"


func activate() -> void:
	state = "idle"
	editor.overlay.show_box(Vector3.ZERO, Vector3.ZERO)


func busy() -> bool:
	return state != "idle"


func cancel() -> void:
	state = "idle"
	editor.overlay.show_box(Vector3.ZERO, Vector3.ZERO)


func press(pos: Vector2, _mods: int, _double: bool) -> void:
	if state != "idle":
		return
	var p := editor.pick(pos) if editor.view_axis() < 0 else {}
	if editor.view_axis() >= 0:
		# a 2D view: the footprint on the view's plane at the working depth, the brush as deep as the last
		_axis = editor.view_axis()
		_sign = 1.0
		_plane = editor.work_depth()
		var n := Vector3.ZERO
		n[_axis] = 1.0
		var at: Variant = editor.plane_point(pos, n * _plane, n)
		if at == null:
			return
		_a = _in_plane(at)
	elif p.size():
		var n: Vector3 = p["normal"]
		_axis = n.abs().max_axis_index()
		_sign = signf(n[_axis])
		_plane = p["point"][_axis]
		if absf(n[_axis]) > 0.999:
			_plane = roundf(_plane * 1024.0) / 1024.0
		_a = _in_plane(p["point"])
	else:
		_axis = 2
		_sign = 1.0
		_plane = 0.0
		var g: Variant = editor.plane_point(pos, Vector3.ZERO, Vector3(0, 0, 1))
		if g == null:
			return
		_a = _in_plane(g)
	_b = _a
	height = editor.last_height


## Snaps the two in-plane coordinates to the grid and pins the third to the plane.
func _in_plane(p: Vector3) -> Vector3:
	var s := editor.grid.snap_vec(p, true)
	s[_axis] = _plane
	return s


func _normal() -> Vector3:
	var n := Vector3.ZERO
	n[_axis] = _sign
	return n


func click(pos: Vector2, _mods: int, _double: bool) -> void:
	if state == "height":
		commit()
		return
	if state == "idle":
		press(pos, 0, false)
		# one cell, at least 8 units so it shows
		var cell := maxf(editor.grid.units(), 8.0)
		_b = _a
		for i in 3:
			if i != _axis:
				_b[i] += cell
		if editor.view_axis() >= 0:
			commit()
			return
		_enter_height()


func drag_start(_pos: Vector2, _mods: int) -> void:
	if state == "idle":
		state = "footprint"


func drag(pos: Vector2, _mods: int) -> void:
	if state != "footprint":
		return
	var p: Variant = editor.plane_point(pos, _a, _normal())
	if p != null:
		_b = _in_plane(p)
	_preview()


func drag_end(_pos: Vector2, _mods: int) -> void:
	if state == "height":
		commit()   # a shaky click
		return
	if state != "footprint":
		return
	var cell := float(editor.grid.units())
	for i in 3:
		if i != _axis and _a[i] == _b[i]:
			_b[i] += cell
	if editor.view_axis() >= 0:
		commit()   # a 2D view can't show a height to drag: the last one's
		return
	_enter_height()


func _enter_height() -> void:
	state = "height"
	_preview()
	editor.ui.set_hint(hint())


func hover(pos: Vector2, _mods: int) -> void:
	if state != "height":
		return
	# the height is where the mouse ray passes the line up from the footprint corner nearest the camera
	var r := editor.ray(pos)
	var cam: Vector3 = r[0]
	var corner := _a
	var best := INF
	for c in _corners():
		if c.distance_to(cam) < best:
			best = c.distance_to(cam)
			corner = c
	var e := _normal() * _sign   # the bare axis
	var o: Vector3 = r[0]
	var d: Vector3 = r[1]
	var w := o - corner
	var b := d.dot(e)
	var denom := d.dot(d) - b * b
	if absf(denom) < 1e-6:
		return
	var t := (d.dot(d) * e.dot(w) - b * d.dot(w)) / denom
	var h := editor.grid.snap(t * _sign, true)
	if absf(h) >= 1.0:
		height = h
	_preview()


func _corners() -> Array:
	var out := []
	for i in 4:
		var c := _a
		var other := [0, 1, 2]
		other.erase(_axis)
		if i & 1:
			c[other[0]] = _b[other[0]]
		if i & 2:
			c[other[1]] = _b[other[1]]
		out.append(c)
	return out


## The box as it stands: [mins, maxs].
func box() -> Array:
	var lo := Vector3(minf(_a.x, _b.x), minf(_a.y, _b.y), minf(_a.z, _b.z))
	var hi := Vector3(maxf(_a.x, _b.x), maxf(_a.y, _b.y), maxf(_a.z, _b.z))
	var top := _plane + _sign * height
	lo[_axis] = minf(_plane, top)
	hi[_axis] = maxf(_plane, top)
	return [lo, hi]


func _preview() -> void:
	var bx := box()
	editor.overlay.show_box(bx[0], bx[1])


func commit() -> void:
	var bx := box()
	var size: Vector3 = bx[1] - bx[0]
	if size.x <= 0 or size.y <= 0 or size.z <= 0:
		UiSound.buzz()
		return
	editor.doc.checkpoint("Create brush")
	var id := editor.doc.add(Q3MapDoc.box_brush(bx[0], bx[1], editor.material))
	editor.last_height = height
	cancel()
	editor.select([id])
	UiSound.move()
	editor.ui.set_hint(hint())


func key(ev: InputEventKey) -> bool:
	if ev.keycode in [KEY_ENTER, KEY_KP_ENTER] and state == "height":
		commit()
		return true
	if ev.keycode == KEY_ESCAPE and state != "idle":
		cancel()
		editor.ui.set_hint(hint())
		return true
	return false


func hint() -> String:
	match state:
		"footprint":
			return "Drag the footprint · let go to set the height"
		"height":
			return "Move the mouse to set the height · click or Enter: create · Esc: cancel"
	return "Drag on a surface or the grid to draw a brush"
