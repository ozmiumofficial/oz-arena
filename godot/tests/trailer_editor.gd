extends Node
## Trailer footage (TRAILER_PLAN.md): the Map editor building a room, driven only by mouse and
## keyboard events as tests/map_editor_input.gd drives it, so the tools, previews and panels react
## as they do for a person. A drawn cursor glides between the clicks. Two walls go up with the
## create tool, one is duplicated and dragged across, the walls and floor get materials from the
## browser's search, a weapon, armor and a light go in, and the camera eases around the room.
## Windowed (needs the GPU); maps go to a scratch folder:
##   godot --path . --fixed-fps 60 res://tests/trailer_editor.tscn -- window=1920x1080
##     out=C:/x/editor.mp4 ffmpeg=C:/x/ffmpeg.exe
## The take ends with the session; tests/trailer_capture.gd prints "TRAILER SHOT done <frames>".

const Capture := preload("res://tests/trailer_capture.gd")
const K = preload("res://scenes/map_editor/q3_editor_keymap.gd")
const SCRATCH := "user://trailer_editor"
const WALL := "textures/gothic_block/blocks15"
const FLOOR := "textures/gothic_floor/largerblock3b_ow"

var ed: Q3MapEditor
var _rec: Capture
var _mouse := Vector2(960, 540)
var _cursor: Polygon2D
var _recording := false


func _ready() -> void:
	Capture.quality()
	_rec = Capture.new()
	add_child(_rec)
	_rec.read_args()
	_rec.tag = "editor"
	_rec.preroll = 30
	_rec.frames = 1 << 20   # ended by finish_now() when the session is over
	_rec.use_window()
	Q3MapEditor.maps_dir = SCRATCH.path_join("maps")
	Q3MapEditor.recent = SCRATCH.path_join("recent.json")
	Q3MapMaterials.prefs_path = SCRATCH.path_join("materials.json")
	Q3MapMaterials._prefs = {}
	Q3MapPrefabs.dir = SCRATCH.path_join("prefabs")
	DirAccess.remove_absolute(ProjectSettings.globalize_path(Q3MapEditor.recent))
	ed = load("res://scenes/map_editor/map_editor.tscn").instantiate()
	add_child(ed)
	# the cursor: an arrow over everything, where the events say the mouse is
	var layer := CanvasLayer.new()
	layer.layer = 120
	add_child(layer)
	_cursor = Polygon2D.new()
	_cursor.polygon = PackedVector2Array([Vector2(0, 0), Vector2(0, 26), Vector2(7, 20), Vector2(12, 31), Vector2(17, 29), Vector2(12, 18), Vector2(21, 18)])
	_cursor.color = Color.WHITE
	var outline := Line2D.new()
	outline.points = _cursor.polygon + PackedVector2Array([Vector2(0, 0)])
	outline.width = 2.0
	outline.default_color = Color.BLACK
	_cursor.add_child(outline)
	layer.add_child(_cursor)
	_run.call_deferred()


func _process(_delta: float) -> void:
	if _recording:
		_rec.tick()
	_cursor.position = _mouse


# ---------------------------------------------------------------- events (map_editor_input.gd's)

func _frames(n := 1) -> void:
	for i in n:
		await get_tree().process_frame


func _send(ev: InputEvent) -> void:
	Input.parse_input_event(ev)
	Input.flush_buffered_events()


func _win(p: Vector2) -> Vector2:
	return get_tree().root.get_final_transform() * p


func tap(code: Key, mods := 0) -> void:
	for down in [true, false]:
		var ev := InputEventKey.new()
		ev.keycode = code
		ev.physical_keycode = code
		ev.pressed = down
		ev.shift_pressed = mods & K.SHIFT != 0
		ev.ctrl_pressed = mods & K.CTRL != 0
		_send(ev)
	await _frames(6)


func type_text(text: String) -> void:
	for ch in text:
		var ev := InputEventKey.new()
		ev.keycode = OS.find_keycode_from_string(ch.to_upper()) if ch != "_" else KEY_UNDERSCORE
		ev.unicode = ch.unicode_at(0)
		ev.pressed = true
		_send(ev)
		var up := ev.duplicate()
		up.pressed = false
		_send(up)
		await _frames(4)   # a typing pace


func _move(pos: Vector2, buttons := 0) -> void:
	var ev := InputEventMouseMotion.new()
	ev.relative = _win(pos) - _win(_mouse)
	ev.position = _win(pos)
	ev.global_position = _win(pos)
	ev.button_mask = buttons
	_mouse = pos
	_send(ev)


func _button(index: MouseButton, pressed: bool, mods := 0) -> void:
	var ev := InputEventMouseButton.new()
	ev.position = _win(_mouse)
	ev.global_position = _win(_mouse)
	ev.button_index = index
	ev.pressed = pressed
	ev.shift_pressed = mods & K.SHIFT != 0
	if pressed:
		ev.button_mask = MOUSE_BUTTON_MASK_LEFT
	_send(ev)


## The cursor eases to `to` over n frames (holding the buttons in `buttons`).
func glide(to: Vector2, n := 16, buttons := 0) -> void:
	var from := _mouse
	for i in range(1, n + 1):
		_move(from.lerp(to, smoothstep(0.0, 1.0, float(i) / n)), buttons)
		await _frames()


func click(mods := 0) -> void:
	_button(MOUSE_BUTTON_LEFT, true, mods)
	_button(MOUSE_BUTTON_LEFT, false, mods)
	await _frames(5)


func click_at(pos: Vector2, mods := 0, n := 16) -> void:
	await glide(pos, n)
	await click(mods)


func at(p: Vector3) -> Vector2:
	return ed.screen_of(p)


# ---------------------------------------------------------------- the camera

## The camera at `pos` looking at `target` (id units).
func _pose(pos: Vector3, target: Vector3) -> Array:
	var d := Q3MapEditor.to_godot(target) - Q3MapEditor.to_godot(pos)
	return [Q3MapEditor.to_godot(pos), rad_to_deg(atan2(-d.x, -d.z)), rad_to_deg(atan2(d.y, Vector2(d.x, d.z).length()))]


## Eases the camera to a new pose over n frames.
func cam_to(pos: Vector3, target: Vector3, n := 50) -> void:
	var a := [ed.camera.position, ed.camera.yaw, ed.camera.pitch]
	var b := _pose(pos, target)
	b[1] = a[1] + wrapf(b[1] - a[1], -180.0, 180.0)
	for i in range(1, n + 1):
		var t := smoothstep(0.0, 1.0, float(i) / n)
		ed.camera.set_pose((a[0] as Vector3).lerp(b[0], t), lerpf(a[1], b[1], t), lerpf(a[2], b[2], t))
		await _frames()


# ---------------------------------------------------------------- building

## A box brush with the create tool: press at `a`, drag the footprint to `b`, let go, raise the
## mouse to height h above the corner nearest the camera, click.
func brush(a: Vector3, b: Vector3, h: float) -> void:
	await glide(at(a), 14)
	_button(MOUSE_BUTTON_LEFT, true)
	await _frames(2)
	await glide(at(b), 18, MOUSE_BUTTON_MASK_LEFT)
	_button(MOUSE_BUTTON_LEFT, false)
	await _frames(4)
	# the tool reads the height off the vertical line through the footprint corner nearest the camera
	var cam: Vector3 = ed.ray(_mouse)[0]
	var near: Vector3 = ed.tool._corners()[0]
	for c in ed.tool._corners():
		if c.distance_to(cam) < near.distance_to(cam):
			near = c
	await glide(at(near + Vector3(0, 0, h)), 16)
	await _frames(4)
	await click()
	await _frames(6)


## Types into the material browser's search and clicks the tile named `shader` (else the first).
func pick_material(search: String, shader: String) -> void:
	var field: LineEdit = ed.ui.browser._search
	await click_at(field.get_global_rect().get_center(), 0, 20)
	field.clear()
	await type_text(search)
	await _frames(10)
	var tile: Control = null
	for t in ed.ui.browser._tiles:
		if t["name"] == shader or tile == null:
			tile = t["button"]
			if t["name"] == shader:
				break
	if tile:
		await click_at(tile.get_global_rect().get_center(), 0, 20)
	await _frames(20)


func _bounds(id: int) -> PackedFloat64Array:
	return Q3MapGeo.brush_bounds(ed.doc.get_obj(id)["planes"])


func _run() -> void:
	await _frames(3)
	ed.new_map()
	ed.ui.browser.set_tab("surface")
	var target := Vector3(0, 40, 48)
	var eye := Vector3(-620, -520, 420)
	ed.camera.set_pose.callv(_pose(eye, target))
	var waited := 0
	while not _rec.size_ok() and waited < 120:
		waited += 1
		await _frames()
	if not _rec.begin():
		get_tree().quit(1)
		return
	_recording = true
	await _frames(_rec.preroll + 20)

	# ---- a back wall and a side wall with the create tool
	await tap(KEY_B)
	await brush(Vector3(-288, 256, 0), Vector3(288, 288, 0), 160)
	await brush(Vector3(256, -256, 0), Vector3(288, 256, 0), 160)
	var side: int = ed.doc.ids_of("brush").back()

	# ---- the side wall duplicated (Ctrl+D) and dragged across to the other side
	await tap(KEY_ESCAPE)
	var sb := _bounds(side)
	await click_at(at(Vector3((sb[0] + sb[3]) * 0.5, (sb[1] + sb[4]) * 0.5, sb[5])))
	await tap(KEY_D, K.CTRL)
	await _frames(10)
	var copy: int = ed.selected_ids()[0]
	var cb := _bounds(copy)
	var grab := Vector3((cb[0] + cb[3]) * 0.5, (cb[1] + cb[4]) * 0.5, cb[5])
	var to := grab + Vector3(-288 - cb[0], sb[1] - cb[1], 0)
	await glide(at(grab), 14)
	_button(MOUSE_BUTTON_LEFT, true)
	await _frames(2)
	await glide(at(to), 40, MOUSE_BUTTON_MASK_LEFT)
	_button(MOUSE_BUTTON_LEFT, false)
	await _frames(12)

	# ---- materials: the three walls from the browser's search, then the floor
	var back: int = ed.doc.ids_of("brush")[1]
	var bb := _bounds(back)
	await click_at(at(Vector3(0, (bb[1] + bb[4]) * 0.5, bb[5])), K.SHIFT)
	await click_at(at(Vector3((sb[0] + sb[3]) * 0.5, 0, sb[5])), K.SHIFT)
	await pick_material("blocks15", WALL)
	await click_at(at(Vector3(-40, -120, 0)))
	await pick_material("largerblock3b", FLOOR)
	await tap(KEY_ESCAPE)
	ed.clear_selection()
	await _frames(10)

	# ---- a rocket launcher, armor and a light on the back wall
	await tap(KEY_I)
	await click_at(at(Vector3(0, 32, 0)))
	await tap(KEY_O)
	await click_at(at(Vector3(-144, -96, 0)))
	await tap(KEY_L)
	await click_at(at(Vector3(0, 256, 120)))
	await tap(KEY_ESCAPE)
	ed.clear_selection()
	await _frames(10)

	# ---- the room, the camera easing around it
	await glide(Vector2(1900, 1060), 20)   # the cursor out of the way
	var r := Vector2(eye.x - target.x, eye.y - target.y).length()
	var start := atan2(eye.y - target.y, eye.x - target.x)
	for i in 240:
		var ang := start + 0.9 * smoothstep(0.0, 1.0, i / 239.0)
		ed.camera.set_pose.callv(_pose(target + Vector3(cos(ang) * r, sin(ang) * r, eye.z - target.z - i * 0.4), target))
		await _frames()
	await _frames(20)
	print("TRAILER EDITOR brushes %d entities %d" % [ed.doc.ids_of("brush").size() - 1, ed.doc.ids_of("entity").size()])
	_rec.finish_now()
