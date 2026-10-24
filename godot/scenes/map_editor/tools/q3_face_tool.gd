class_name Q3FaceTool
extends Q3EditorTool
## The face / texture tool (T), after OZMIUM's FaceTool: click selects faces (Shift adds, Ctrl
## toggles), Alt+click is the eyedropper (the clicked face's material and alignment onto the selected
## faces, or into the current material when none are), dragging a face pushes or pulls it along its
## normal, the arrows shift the texture (Ctrl+Up/Down scale, Ctrl+Left/Right rotate).

const K = preload("res://scenes/map_editor/q3_editor_keymap.gd")
const SCALE_STEP := 1.1
const ROTATE_STEP := 15.0

var object_mode := false      ## Shift+T: a click takes every face of the brush (or its group)
var _pushing := false
var _grab := Vector3.ZERO      ## the grabbed point on the face (id)
var _normal := Vector3.ZERO    ## the grabbed face's normal
var _start := {}               ## brush id -> the brush before the drag
var _keys := []                ## the faces being pushed: Vector2i(brush id, face)
var _on_grid := false          ## one axis-aligned face: its plane lands on the grid, not the push
var distance := 0.0            ## the push so far (units, along each face's normal)


func _init(e: Q3MapEditor) -> void:
	super(e)
	title = "Face"


func busy() -> bool:
	return _pushing


func cancel() -> void:
	if _pushing:
		editor.doc.begin_batch()
		for id in _start:
			editor.doc.put(_start[id])
		editor.doc.end_batch()
		editor.doc.drop_checkpoint()
	_pushing = false
	_start.clear()
	_keys.clear()


func click(pos: Vector2, mods: int, _double: bool) -> void:
	var p := editor.pick(pos)
	if p.is_empty() or p["kind"] != "brush":
		if mods == 0:
			editor.select_faces([])
		return
	var key := Vector2i(p["id"], p["face"])
	if mods & K.ALT:
		eyedropper(key)
		return
	if p["locked"]:
		return
	var keys := [key]
	if object_mode:
		keys = []
		for id in ([p["id"]] if mods & K.CTRL else editor.group_of(p["id"])):
			var b := editor.doc.get_obj(id)
			if b.get("type", "") == "brush":
				for i in b["faces"].size():
					keys.append(Vector2i(id, i))
	editor.select_faces(keys, "toggle" if mods & K.CTRL else ("add" if mods & K.SHIFT else "set"))
	UiSound.move()


## The clicked face's material and alignment onto every selected face; with none selected, its
## material becomes the current one.
func eyedropper(key: Vector2i) -> void:
	var src: Dictionary = editor.doc.get_obj(key.x)["faces"][key.y]
	if editor.faces.is_empty():
		editor.material = src["material"]
		editor.notify("Material: " + String(src["material"]).trim_prefix("textures/"))
		return
	editor.doc.checkpoint("Eyedropper")
	var by_brush := {}
	for k: Vector2i in editor.faces:
		if not by_brush.has(k.x):
			by_brush[k.x] = editor.doc.edit(k.x)
		var f := src.duplicate(true)
		by_brush[k.x]["faces"][k.y] = f
	editor.doc.begin_batch()
	for id in by_brush:
		editor.doc.put(by_brush[id])
	editor.doc.end_batch()
	editor.notify("Copied %s onto %d face%s" % [String(src["material"]).trim_prefix("textures/"), editor.faces.size(), "" if editor.faces.size() == 1 else "s"])


func drag_start(pos: Vector2, mods: int) -> void:
	var p := editor.pick(pos)
	if p.is_empty() or p["kind"] != "brush" or p["locked"]:
		return
	var key := Vector2i(p["id"], p["face"])
	if not editor.faces.has(key):
		editor.select_faces([key], "add" if mods & K.SHIFT else "set")
	begin_push(editor.faces.keys(), p["point"], p["normal"], "Push faces")


## Starts pushing the given faces from grab along normal; the select tool's face handles push one
## face this way without touching the face selection.
func begin_push(keys: Array, grab: Vector3, normal: Vector3, label: String) -> void:
	_keys = keys.duplicate()
	_grab = grab
	_normal = normal
	_start.clear()
	for k: Vector2i in _keys:
		_start[k.x] = editor.doc.get_obj(k.x)
	_on_grid = false
	if _keys.size() == 1:
		var pl: PackedFloat64Array = _start[_keys[0].x]["planes"]
		var a := Vector3(pl[_keys[0].y * 4], pl[_keys[0].y * 4 + 1], pl[_keys[0].y * 4 + 2]).abs()
		_on_grid = is_equal_approx(a[a.max_axis_index()], 1.0)
	editor.doc.checkpoint(label)
	_pushing = true
	distance = 0.0


func drag(pos: Vector2, mods: int) -> void:
	if not _pushing:
		return
	var along: Variant = along_axis(editor.ray(pos), _grab, _normal)
	if along == null:
		return
	var s: float = along
	if mods & K.CTRL == 0:
		if _on_grid:
			var d0: float = _start[_keys[0].x]["planes"][_keys[0].y * 4 + 3]
			s = editor.grid.snap(d0 + s, true) - d0
		else:
			s = editor.grid.snap(s, true)
	if s == distance:
		return
	var pushed := {}
	for k: Vector2i in _keys:
		var bb: Dictionary = pushed.get(k.x, _start[k.x].duplicate(true))
		bb["planes"][k.y * 4 + 3] = _start[k.x]["planes"][k.y * 4 + 3] + s
		pushed[k.x] = bb
	for id in pushed:
		if not Q3MapGeo.brush_valid(pushed[id]["planes"]):
			return   # too far: a face would pass through the brush
	distance = s
	editor.doc.begin_batch()
	for id in pushed:
		editor.doc.put(pushed[id])
	editor.doc.end_batch()
	editor.ui.set_hint("Push %s" % Q3MapEditor._num(s))


## How far along the line out of origin (along unit n) the mouse ray r ([origin, direction]) passes
## closest to it, or null when the ray runs along the line.
static func along_axis(r: Array, origin: Vector3, n: Vector3) -> Variant:
	var o: Vector3 = r[0]
	var d: Vector3 = r[1]
	var w := o - origin
	var b := d.dot(n)
	var denom := d.dot(d) - b * b
	if absf(denom) < 1e-6:
		return null
	return (d.dot(d) * n.dot(w) - b * d.dot(w)) / denom


func drag_end(_pos: Vector2, _mods: int) -> void:
	if _pushing and distance == 0.0:
		editor.doc.drop_checkpoint()
	_pushing = false
	_start.clear()
	_keys.clear()
	editor.ui.set_hint("")


func key(ev: InputEventKey) -> bool:
	var ctrl := ev.ctrl_pressed or ev.meta_pressed
	if ev.keycode == KEY_ESCAPE:
		if _pushing:
			cancel()
			return true
		if editor.faces.size():
			editor.select_faces([])
			return true
		return false
	if editor.faces.is_empty() or ev.shift_pressed or ev.alt_pressed:
		return false
	var step := float(editor.grid.units())
	match ev.keycode:
		KEY_LEFT:
			adjust("rotate" if ctrl else "shift", ROTATE_STEP if ctrl else -step, 0.0)
		KEY_RIGHT:
			adjust("rotate" if ctrl else "shift", -ROTATE_STEP if ctrl else step, 0.0)
		KEY_UP:
			if ctrl:
				adjust("scale", SCALE_STEP, 0.0)
			else:
				adjust("shift", 0.0, -step)
		KEY_DOWN:
			if ctrl:
				adjust("scale", 1.0 / SCALE_STEP, 0.0)
			else:
				adjust("shift", 0.0, step)
		_:
			return false
	return true


## Texture moves on every selected face, each in its own mapping: shift (texels a, b), rotate (a
## degrees), scale (by a). World-projected faces change their shift/rotate/scale; explicit ones
## (texture-locked, imported) move their axes.
func adjust(what: String, a: float, b: float) -> void:
	var label: String = {"shift": "Shift texture", "rotate": "Rotate texture", "scale": "Scale texture"}[what]
	editor.doc.checkpoint(label)
	var by_brush := {}
	for k: Vector2i in editor.faces:
		if not by_brush.has(k.x):
			by_brush[k.x] = editor.doc.edit(k.x)
		var bb: Dictionary = by_brush[k.x]
		bb["faces"][k.y] = adjusted(bb["faces"][k.y], Q3MapDoc.plane_of(bb, k.y), what, a, b)
	editor.doc.begin_batch()
	for id in by_brush:
		editor.doc.put(by_brush[id])
	editor.doc.end_batch()


static func adjusted(f: Dictionary, plane: PackedFloat64Array, what: String, a: float, b: float) -> Dictionary:
	var out := f.duplicate(true)
	if out.has("uv"):
		var uv := PackedFloat64Array(out["uv"])
		var U := Vector3(uv[0], uv[1], uv[2])
		var V := Vector3(uv[4], uv[5], uv[6])
		match what:
			"shift":
				uv[3] += a
				uv[7] += b
			"rotate":
				var n := Vector3(plane[0], plane[1], plane[2]).normalized()
				U = U.rotated(n, deg_to_rad(a))
				V = V.rotated(n, deg_to_rad(a))
			"scale":
				U /= a
				V /= a
		if what != "shift":
			uv[0] = U.x
			uv[1] = U.y
			uv[2] = U.z
			uv[4] = V.x
			uv[5] = V.y
			uv[6] = V.z
		out["uv"] = uv
	else:
		var t: Array = Array(out.get("tex", Q3MapDoc.DEFAULT_TEX)).duplicate()
		match what:
			"shift":
				t[0] = float(t[0]) + a
				t[1] = float(t[1]) + b
			"rotate":
				t[2] = fposmod(float(t[2]) + a, 360.0)
			"scale":
				t[3] = float(t[3]) * a
				t[4] = float(t[4]) * a
		out["tex"] = t
	return out


## Alignment on one face, in texels of its own material (one texture = its size at the face's
## scale): fit stretches one texture across the face; left / right / top / bottom put that edge of
## the face on a texture edge, center puts the face's middle on the texture's middle; flipU / flipV
## mirror it.
static func aligned(f: Dictionary, plane: PackedFloat64Array, winding: PackedFloat64Array, how: String) -> Dictionary:
	var size := Q3MapMaterials.size(f["material"])
	var out := f.duplicate(true)
	var uv := Q3MapDoc.face_uv(f, plane)
	var r := _ranges(uv, winding)   # [umin, umax, vmin, vmax]
	if out.has("uv"):
		var e := PackedFloat64Array(out["uv"])
		match how:
			"fit":
				for k in 2:
					var span: float = r[k * 2 + 1] - r[k * 2]
					if span > 1e-6:
						var s := size[k] / span
						for c in 3:
							e[k * 4 + c] *= s
						e[k * 4 + 3] = 0.0
				var r2 := _ranges(e, winding)
				e[3] = -r2[0]
				e[7] = -r2[2]
			"flipU", "flipV":
				var k := 0 if how == "flipU" else 1
				for c in 4:
					e[k * 4 + c] = -e[k * 4 + c]
			_:
				var d := _edge_shift(r, size, how)
				e[3] += d.x
				e[7] += d.y
		out["uv"] = e
	else:
		var t: Array = Array(out.get("tex", Q3MapDoc.DEFAULT_TEX)).duplicate()
		match how:
			"fit":
				for k in 2:
					var span: float = r[k * 2 + 1] - r[k * 2]
					if span > 1e-6:
						t[3 + k] = float(t[3 + k]) * span / size[k]
				t[0] = 0.0
				t[1] = 0.0
				var r2 := _ranges(Q3MapDoc.face_uv({"material": f["material"], "tex": t}, plane), winding)
				t[0] = -r2[0]
				t[1] = -r2[2]
			"flipU":
				t[3] = -float(t[3])
			"flipV":
				t[4] = -float(t[4])
			_:
				var d := _edge_shift(r, size, how)
				t[0] = float(t[0]) + d.x
				t[1] = float(t[1]) + d.y
		out["tex"] = t
	return out


## [umin, umax, vmin, vmax] of a face's corners under a mapping (texels).
static func _ranges(uv: PackedFloat64Array, w: PackedFloat64Array) -> Array:
	var r := [INF, -INF, INF, -INF]
	for i in w.size() / 3:
		var u := uv[0] * w[i * 3] + uv[1] * w[i * 3 + 1] + uv[2] * w[i * 3 + 2] + uv[3]
		var v := uv[4] * w[i * 3] + uv[5] * w[i * 3 + 1] + uv[6] * w[i * 3 + 2] + uv[7]
		r = [minf(r[0], u), maxf(r[1], u), minf(r[2], v), maxf(r[3], v)]
	return r


## The shift (texels) that puts the chosen edge or the middle on a texture edge or middle.
static func _edge_shift(r: Array, size: Vector2, how: String) -> Vector2:
	match how:
		"left": return Vector2(-fposmod(r[0], size.x), 0)
		"right": return Vector2(-fposmod(r[1], size.x), 0)
		"top": return Vector2(0, -fposmod(r[2], size.y))
		"bottom": return Vector2(0, -fposmod(r[3], size.y))
		"center":
			return Vector2(-fposmod((r[0] + r[1]) * 0.5 - size.x * 0.5, size.x), -fposmod((r[2] + r[3]) * 0.5 - size.y * 0.5, size.y))
	return Vector2.ZERO


func hint() -> String:
	if object_mode:
		return "Click objects to take all their faces (Shift add, Ctrl one object, Alt eyedropper) · Shift+T single faces · pick a material to paint"
	return "Click faces (Shift add, Ctrl toggle, Alt eyedropper) · drag to push/pull · arrows shift the texture, Ctrl+arrows scale/rotate · Ctrl+T texture lock"
