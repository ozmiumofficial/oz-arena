extends Node
## Map editor input test (MAP_EDITOR_PLAN.md E8), after OZMIUM's MapEditorInputTest: the real editor
## scene driven only by input events through Input.parse_input_event, as the mouse and keyboard
## deliver them (the GUI first, then the editor), so focus, panels and hotkeys are tested together.
##   godot --headless --path . res://tests/map_editor_input.tscn
## Prints one line per check and "MAP EDITOR INPUT n/m passed"; exit code 1 on any failure.

const SCRATCH := "user://map_editor_input_test"
const K = preload("res://scenes/map_editor/q3_editor_keymap.gd")

var passed := 0
var failed := 0
var ed: Q3MapEditor


func check(name: String, ok: bool, detail := "") -> void:
	if ok:
		passed += 1
	else:
		failed += 1
	print("  %s  map editor input: %s%s" % ["PASS" if ok else "FAIL", name, ("  (" + detail + ")") if detail != "" else ""])


func _ready() -> void:
	_run.call_deferred()


# ---------------------------------------------------------------- events

func _send(ev: InputEvent) -> void:
	Input.parse_input_event(ev)
	Input.flush_buffered_events()


func _frames(n := 1) -> void:
	for i in n:
		await get_tree().process_frame


func key(code: Key, mods := 0, pressed := true) -> void:
	var ev := InputEventKey.new()
	ev.keycode = code
	ev.physical_keycode = code
	ev.pressed = pressed
	ev.ctrl_pressed = mods & K.CTRL != 0
	ev.shift_pressed = mods & K.SHIFT != 0
	ev.alt_pressed = mods & K.ALT != 0
	if pressed and mods & (K.CTRL | K.ALT) == 0:
		var ch := OS.get_keycode_string(code)
		if ch.length() == 1:
			ev.unicode = (ch.to_upper() if mods & K.SHIFT else ch.to_lower()).unicode_at(0)
	_send(ev)


## A key pressed and let go.
func tap(code: Key, mods := 0) -> void:
	key(code, mods, true)
	key(code, mods, false)
	await _frames()


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
	await _frames()


## Viewport coordinates (what screen_of and Control rects give) as the window's, which is where
## events arrive before the stretch transform maps them back.
func _win(p: Vector2) -> Vector2:
	return get_tree().root.get_final_transform() * p


func move(pos: Vector2, buttons := 0) -> void:
	var ev := InputEventMouseMotion.new()
	ev.position = _win(pos)
	ev.global_position = _win(pos)
	ev.button_mask = buttons
	_send(ev)


func button(pos: Vector2, index: MouseButton, pressed: bool, mods := 0, factor := 1.0) -> void:
	var ev := InputEventMouseButton.new()
	ev.position = _win(pos)
	ev.global_position = _win(pos)
	ev.button_index = index
	ev.pressed = pressed
	ev.factor = factor
	ev.ctrl_pressed = mods & K.CTRL != 0
	ev.shift_pressed = mods & K.SHIFT != 0
	ev.alt_pressed = mods & K.ALT != 0
	if pressed and index in [MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT]:
		ev.button_mask = MOUSE_BUTTON_MASK_LEFT if index == MOUSE_BUTTON_LEFT else MOUSE_BUTTON_MASK_RIGHT
	_send(ev)


func click(pos: Vector2, mods := 0) -> void:
	move(pos)
	button(pos, MOUSE_BUTTON_LEFT, true, mods)
	button(pos, MOUSE_BUTTON_LEFT, false, mods)
	await _frames()


## A left drag through the given screen points.
func drag(points: Array, mods := 0) -> void:
	move(points[0])
	button(points[0], MOUSE_BUTTON_LEFT, true, mods)
	for p in points.slice(1):
		move(p, MOUSE_BUTTON_MASK_LEFT)
	button(points.back(), MOUSE_BUTTON_LEFT, false, mods)
	await _frames()


func scroll(pos: Vector2, up: bool, mods := 0) -> void:
	move(pos)
	var idx := MOUSE_BUTTON_WHEEL_UP if up else MOUSE_BUTTON_WHEEL_DOWN
	button(pos, idx, true, mods)
	button(pos, idx, false, mods)
	await _frames()


func center_of(c: Control) -> Vector2:
	return c.get_global_rect().get_center()


func at(p: Vector3) -> Vector2:
	return ed.screen_of(p)


func _brushes() -> Array:
	return ed.doc.ids_of("brush")


# ---------------------------------------------------------------- the run

func _run() -> void:
	Q3MapEditor.maps_dir = SCRATCH.path_join("maps")
	Q3MapEditor.recent = SCRATCH.path_join("recent.json")
	Q3MapMaterials.prefs_path = SCRATCH.path_join("materials.json")
	Q3MapMaterials._prefs = {}
	Q3MapPrefabs.dir = SCRATCH.path_join("prefabs")
	DirAccess.remove_absolute(ProjectSettings.globalize_path(Q3MapEditor.recent))
	var keymap_backup := FileAccess.get_file_as_string(K.OVERRIDES) if FileAccess.file_exists(K.OVERRIDES) else ""
	K.reset()
	ed = load("res://scenes/map_editor/map_editor.tscn").instantiate()
	add_child(ed)
	await _frames(3)
	ed.new_map()
	ed.camera.set_pose(Q3MapEditor.to_godot(Vector3(-400, -400, 500)), -135, -45)
	await _frames(2)
	check("the editor, its panels and a new map are up", ed.ui != null and ed.ui.browser.visible and _brushes().size() == 1)

	# ---- 1. a brush with the create tool
	await tap(KEY_B)
	check("B picks the create tool", ed.tool == ed.tools["create"])
	var n0 := _brushes().size()
	await drag([at(Vector3(0, 0, 0)), at(Vector3(40, 30, 0)), at(Vector3(96, 64, 0))])
	await tap(KEY_ENTER)
	var made := _brushes().size() == n0 + 1
	var box_id: int = _brushes().back()
	var bb := Q3MapGeo.brush_bounds(ed.doc.get_obj(box_id)["planes"]) if made else PackedFloat64Array([1, 1, 1, 1, 1, 1])
	check("a footprint drag and Enter make a brush, on the 16-unit grid", made and int(bb[0]) % 16 == 0 and int(bb[1]) % 16 == 0
			and bb[3] - bb[0] >= 64, "bounds %s" % bb)
	check("the create tool stays armed after a brush", ed.tool == ed.tools["create"])

	# ---- 2. select tool
	await tap(KEY_ESCAPE)
	check("Escape goes back to the select tool", ed.tool == ed.tools["select"])
	var top := Vector3((bb[0] + bb[3]) * 0.5, (bb[1] + bb[4]) * 0.5, bb[5])
	await click(at(top))
	check("clicking a brush selects it", ed.selection.has(box_id) and ed.selection.size() == 1)
	await tap(KEY_D, K.CTRL)
	var copy: int = ed.selected_ids()[0] if ed.selection.size() == 1 else -1
	check("Ctrl+D duplicates and selects the copy", copy != box_id and copy > 0 and _brushes().size() == n0 + 2)
	await tap(KEY_DELETE)
	check("Delete removes the selection", _brushes().size() == n0 + 1 and ed.selection.is_empty())
	await click(at(top))
	var before := Q3MapGeo.brush_bounds(ed.doc.get_obj(box_id)["planes"])
	await tap(KEY_PAGEUP)
	var after := Q3MapGeo.brush_bounds(ed.doc.get_obj(box_id)["planes"])
	check("Page Up nudges up one grid step", after[2] - before[2] == 16.0, "%s -> %s" % [before[2], after[2]])
	await tap(KEY_Z, K.CTRL)
	check("Ctrl+Z undoes the nudge", Q3MapGeo.brush_bounds(ed.doc.get_obj(box_id)["planes"]) == before)
	await drag([at(top), at(top + Vector3(20, 0, 0)), at(top + Vector3(64, 0, 0))])
	var moved := Q3MapGeo.brush_bounds(ed.doc.get_obj(box_id)["planes"])
	check("dragging the selected brush moves it on the grid", moved[0] != before[0] and int(moved[0]) % 16 == 0, "%s -> %s" % [before[0], moved[0]])
	await tap(KEY_Z, K.CTRL)

	# ---- 2b. the select tool's in-place handles (OZMIUM's grab / push / pull gizmo)
	var st: Q3SelectTool = ed.tools["select"]
	var b0 := Q3MapGeo.brush_bounds(ed.doc.get_obj(box_id)["planes"])
	var top_h := {}
	for h in st.face_handles():
		if h["key"].x == box_id and h["normal"].z > 0.9:
			top_h = h
	check("a selected brush shows a diamond on its top face and squares on its corners", top_h.size() > 0 and st.corners().size() == 8)
	if top_h.size():
		var tip: Vector3 = top_h["tip"]
		await drag([at(tip), at(tip + Vector3(0, 0, 24)), at(tip + Vector3(0, 0, 64))])
		var pulled := Q3MapGeo.brush_bounds(ed.doc.get_obj(box_id)["planes"])
		check("dragging the top diamond pulls the top face up, onto the grid, and nothing else",
				pulled[5] > b0[5] and int(pulled[5]) % 16 == 0 and pulled[2] == b0[2] and pulled[0] == b0[0], "%s -> %s" % [b0, pulled])
		await tap(KEY_Z, K.CTRL)
		check("Ctrl+Z undoes the pull in one step", Q3MapGeo.brush_bounds(ed.doc.get_obj(box_id)["planes"]) == b0)
	var corner := Vector3(b0[3], b0[4], b0[5])
	await drag([at(corner), at(corner + Vector3(16, 0, 0)), at(corner + Vector3(32, 0, 0))])
	var dragged := Q3MapGeo.brush_bounds(ed.doc.get_obj(box_id)["planes"])
	check("dragging a corner square moves just that corner", dragged[3] == b0[3] + 32 and dragged[0] == b0[0] and ed.selection.has(box_id), "%s -> %s" % [b0, dragged])
	await tap(KEY_Z, K.CTRL)
	await click(at(corner))
	check("clicking a corner square picks the corner", ed.verts.size() == 1 and ed.selection.has(box_id))
	await tap(KEY_DELETE)
	check("Delete removes the picked corner, not the brush", ed.doc.objects.has(box_id)
			and Q3MapGeo.brush_vertices(ed.doc.get_obj(box_id)["planes"]).size() / 3 == 7)
	await tap(KEY_Z, K.CTRL)
	var side := Vector3(b0[0], (b0[1] + b0[4]) * 0.5, (b0[2] + b0[5]) * 0.5)
	await drag([at(side), at(side + Vector3(-16, 0, 0)), at(side + Vector3(-32, 0, 0))], K.SHIFT)
	var pushed := Q3MapGeo.brush_bounds(ed.doc.get_obj(box_id)["planes"])
	check("Shift+drag on a face pushes / pulls that face", pushed[0] == b0[0] - 32 and pushed[3] == b0[3], "%s -> %s" % [b0, pushed])
	await tap(KEY_Z, K.CTRL)
	check("the brush is back as it was", Q3MapGeo.brush_bounds(ed.doc.get_obj(box_id)["planes"]) == b0)

	# ---- 3. multi-select
	var other := ed.doc.add(Q3MapDoc.box_brush(Vector3(-200, 0, 0), Vector3(-136, 64, 64)))
	ed.mesher.flush()
	await click(at(top))
	await click(at(Vector3(-168, 32, 64)), K.SHIFT)
	check("Shift+click adds to the selection", ed.selection.size() == 2, str(ed.selected_ids()))
	await tap(KEY_ESCAPE)
	check("Escape clears the selection", ed.selection.is_empty())
	await tap(KEY_A, K.CTRL)
	check("Ctrl+A selects everything", ed.selection.size() == ed.doc.objects.size())
	await tap(KEY_ESCAPE)
	await click(at(top))
	await tap(KEY_H)
	check("H hides the selection", ed.doc.hidden.has(box_id))
	await tap(KEY_H, K.SHIFT)
	check("Shift+H shows everything again", ed.doc.hidden.is_empty())
	await click(at(top))
	var pre_r := Q3MapGeo.brush_bounds(ed.doc.get_obj(box_id)["planes"])
	await tap(KEY_R)
	var post_r := Q3MapGeo.brush_bounds(ed.doc.get_obj(box_id)["planes"])
	check("R turns the selection 90 degrees", is_equal_approx(post_r[3] - post_r[0], pre_r[4] - pre_r[1]) and is_equal_approx(post_r[4] - post_r[1], pre_r[3] - pre_r[0]))
	await tap(KEY_Z, K.CTRL)

	# ---- 4. face tool and text focus
	await tap(KEY_T)
	check("T picks the face tool", ed.tool == ed.tools["face"])
	await click(at(top))
	check("clicking a face picks it", ed.faces.size() == 1)
	var search: LineEdit = ed.ui.browser._search
	await click(center_of(search))
	check("clicking the material search field focuses it", search.has_focus())
	await type_text("met")
	check("typing lands in the search field", search.text == "met", search.text)
	await click(at(top))
	check("a click in the view takes the keys back from the field", not search.has_focus())
	await tap(KEY_V)
	check("typing in the view runs hotkeys and never reaches the field", search.text == "met" and ed.tool == ed.tools["vertex"], search.text)
	search.text = ""
	search.text_changed.emit("")
	await tap(KEY_TAB)
	check("Tab hides the panels without focusing the search field", not ed.ui.browser.visible and not search.has_focus())
	await tap(KEY_TAB)
	check("Tab shows them again", ed.ui.browser.visible)
	await tap(KEY_ESCAPE)

	# ---- 5. grid keys
	await tap(KEY_3)
	check("3 sets the 4-unit grid", ed.grid.units() == 4.0, ed.grid.label())
	await tap(KEY_BRACKETRIGHT)
	check("] steps the grid up", ed.grid.units() == 8.0, ed.grid.label())
	await tap(KEY_BRACKETLEFT)
	check("[ steps the grid down", ed.grid.units() == 4.0, ed.grid.label())
	var view_mid := at(Vector3(0, 0, 0))
	await scroll(view_mid, true, K.CTRL)
	check("Ctrl+scroll steps the grid", ed.grid.units() == 8.0, ed.grid.label())
	await tap(KEY_5)

	# ---- 6. camera
	var cam0 := ed.camera.position
	key(KEY_W)
	await _frames(5)
	key(KEY_W, 0, false)
	check("W without the right button doesn't fly", ed.camera.position == cam0)
	await scroll(view_mid, true)
	check("the wheel in the view dollies the camera", ed.camera.position != cam0)
	var cam1 := ed.camera.position
	await scroll(center_of(ed.ui.browser), false)
	var down_ok := ed.camera.position == cam1
	await scroll(center_of(ed.ui.browser), true)
	var up_ok := ed.camera.position == cam1
	await scroll(center_of(ed.ui.browser._search), true)
	var field_ok := ed.camera.position == cam1
	check("the wheel over the browser (its tiles, already at the top, its fields) never moves the camera",
			down_ok and up_ok and field_ok, "down %s, up %s, field %s" % [down_ok, up_ok, field_ok])
	move(view_mid)
	button(view_mid, MOUSE_BUTTON_RIGHT, true)
	await _frames()
	var yaw0: float = ed.camera.yaw
	var look := InputEventMouseMotion.new()
	look.position = _win(view_mid)
	look.relative = Vector2(120, 0)
	look.button_mask = MOUSE_BUTTON_MASK_RIGHT
	_send(look)
	await _frames()
	check("a right drag turns the camera", ed.camera.looking and not is_equal_approx(ed.camera.yaw, yaw0), "%.1f -> %.1f" % [yaw0, ed.camera.yaw])
	var cam2 := ed.camera.position
	key(KEY_W)
	await _frames(10)
	key(KEY_W, 0, false)
	check("W while looking flies forward", ed.camera.position.distance_to(cam2) > 0.1, "%.2f m" % ed.camera.position.distance_to(cam2))
	button(view_mid, MOUSE_BUTTON_RIGHT, false)
	await _frames()
	check("letting go of the right button stops looking", not ed.camera.looking)
	await tap(KEY_HOME)
	check("Home resets the camera", ed.camera.position.is_equal_approx(Q3EditorCamera.HOME_POSITION))
	ed.camera.set_pose(Q3MapEditor.to_godot(Vector3(-400, -400, 500)), -135, -45)
	await _frames()

	# ---- 7. view filters
	await tap(KEY_1, K.ALT)
	check("Alt+1 hides caulk", ed.filters.has("caulk"))
	await tap(KEY_0, K.ALT)
	check("Alt+0 shows everything", ed.filters.is_empty())

	# ---- 8. dialogs and the sheet
	await tap(KEY_F1)
	check("F1 opens the hotkey sheet", ed.ui._sheet != null and ed.ui._sheet.visible)
	await tap(KEY_F1)
	check("F1 again closes it", not ed.ui._sheet.visible)
	await tap(KEY_S, K.CTRL | K.SHIFT)
	await _frames(2)
	var field := ed.ui._modal.find_children("*", "LineEdit", true, false)[0] as LineEdit if ed.ui.modal_open() else null
	check("Ctrl+Shift+S opens Save as with its name field focused", field != null and field.has_focus())
	if field:
		await type_text("input_room")
		await tap(KEY_ENTER)
	check("typing a name and Enter saves the map", FileAccess.file_exists(Q3MapEditor.map_path("input_room")) and not ed.doc.dirty
			and not ed.ui.modal_open())
	await tap(KEY_O, K.CTRL)
	check("Ctrl+O opens the Open dialog", ed.ui.modal_open())
	await tap(KEY_ESCAPE)
	check("Escape closes it", not ed.ui.modal_open())
	await tap(KEY_D, K.CTRL)   # nothing selected: a no-op, the dialog keys are done with
	ed.select([box_id])
	var count := _brushes().size()
	await tap(KEY_DELETE)
	await click(center_of(ed.ui._undo))
	check("the top bar's Undo button brings a deleted brush back", _brushes().size() == count, "%d -> %d" % [count, _brushes().size()])

	# ---- 9. the browser and outliner by mouse
	ed.ui.browser.set_tab("objects")
	await _frames(2)
	var ol: Q3EditorOutliner = ed.ui.browser.outliner
	ol.refresh()
	check("the Objects tab lists the map's objects", ol._tree.get_root().get_child_count() == ed.doc.objects.size())
	ed.ui.browser.set_tab("surface")

	if keymap_backup != "":
		var f := FileAccess.open(K.OVERRIDES, FileAccess.WRITE)
		f.store_string(keymap_backup)
		f.close()
		K.load_overrides()
	ed.doc.dirty = false
	ed.queue_free()
	for p in ["maps/input_room.q3map.json", "maps/input_room.png", "maps/_autosave.q3map.json", "recent.json", "materials.json"]:
		DirAccess.remove_absolute(ProjectSettings.globalize_path(SCRATCH.path_join(p)))
	print("MAP EDITOR INPUT %d/%d passed" % [passed, passed + failed])
	get_tree().quit(1 if failed else 0)
