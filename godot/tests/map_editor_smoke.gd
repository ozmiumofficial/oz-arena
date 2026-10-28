extends Node
## Map editor smoke test (MAP_EDITOR_PLAN.md E1), after OZMIUM's MapEditorSmokeTest: loads the real
## Map editor scene and drives it the way the mouse and the keymap do (tool calls at screen points
## worked out from id-space targets, commands through execute() and key events).
##   godot --headless --path . res://tests/map_editor_smoke.tscn
## Prints one line per check and "MAP EDITOR n/m passed"; exit code 1 on any failure. Maps, the
## recent file and keymap overrides go to a scratch folder; the player's are left alone.

const SCRATCH := "user://map_editor_test"
const K = preload("res://scenes/map_editor/q3_editor_keymap.gd")

signal import_finished

var passed := 0
var failed := 0
var ed: Q3MapEditor


func check(name: String, ok: bool, detail := "") -> void:
	if ok:
		passed += 1
	else:
		failed += 1
	print("  %s  map editor: %s%s" % ["PASS" if ok else "FAIL", name, ("  (" + detail + ")") if detail != "" else ""])


func _ready() -> void:
	_run.call_deferred()


func _bounds(id: int) -> PackedFloat64Array:
	return Q3MapGeo.brush_bounds(ed.doc.get_obj(id)["planes"])


func _box(id: int) -> String:
	var b := _bounds(id)
	return "%d %d %d .. %d %d %d" % [b[0], b[1], b[2], b[3], b[4], b[5]]


func _key(code: int, mods := 0) -> void:
	var ev := InputEventKey.new()
	ev.keycode = code
	ev.pressed = true
	ev.ctrl_pressed = mods & K.CTRL != 0
	ev.shift_pressed = mods & K.SHIFT != 0
	ev.alt_pressed = mods & K.ALT != 0
	ed._unhandled_input(ev)


func _brushes() -> Array:
	return ed.doc.ids_of("brush")


## Press, move and release the left button along a path of id-space points.
func _drag(points: Array, mods := 0) -> void:
	var t: Q3EditorTool = ed.tool
	var a: Vector2 = ed.screen_of(points[0])
	t.press(a, mods, false)
	t.drag_start(a, mods)
	for p in points.slice(1):
		t.drag(ed.screen_of(p), mods)
	t.drag_end(ed.screen_of(points.back()), mods)


func _run() -> void:
	Q3MapEditor.maps_dir = SCRATCH.path_join("maps")
	Q3MapEditor.recent = SCRATCH.path_join("recent.json")
	Q3MapMaterials.prefs_path = SCRATCH.path_join("materials.json")
	Q3EditorUi.layout_path = SCRATCH.path_join("layout.json")
	Q3MapMaterials._prefs = {}
	Q3MapPrefabs.dir = SCRATCH.path_join("prefabs")
	_wipe()
	var keymap_backup := FileAccess.get_file_as_string(K.OVERRIDES) if FileAccess.file_exists(K.OVERRIDES) else ""
	ed = load("res://scenes/map_editor/map_editor.tscn").instantiate()
	add_child(ed)
	await get_tree().process_frame

	# ---- a fresh map
	var floor_ids := _brushes()
	check("a new map opens with one floor brush, drawn, and nothing to undo",
			floor_ids.size() == 1 and ed.mesher.cell_count() >= 1 and not ed.doc.can_undo() and not ed.doc.dirty,
			"%d brushes, %d cells" % [floor_ids.size(), ed.mesher.cell_count()])
	ed.camera.set_pose(Vector3(-6, 9, -6), -135, -50)   # looking down across the origin
	var floor_id: int = floor_ids[0]
	var hit := ed.pick(ed.screen_of(Vector3(64, 64, 0)))
	check("picking hits the floor's top face where the cursor points",
			hit.get("id", -1) == floor_id and int(hit["face"]) == 4 and (hit["point"] as Vector3).distance_to(Vector3(64, 64, 0)) < 0.5,
			str(hit.get("point")))

	# ---- create tool: footprint, then height
	ed.execute("tool.create")
	_drag([Vector3(0, 0, 0), Vector3(70, 30, 0), Vector3(126, 66, 0)])
	var ct: Q3CreateTool = ed.tool
	ct.hover(ed.screen_of(Vector3(0, 0, 96)), 0)
	var state := ct.state
	ct.click(Vector2.ZERO, 0, false)
	var made := _brushes().filter(func(i): return i != floor_id)
	check("create tool: drag a footprint on the floor, set the height, click: a grid-snapped box",
			state == "height" and made.size() == 1 and _box(made[0]) == "0 0 0 .. 128 64 96" and ed.selection.has(made[0]),
			_box(made[0]) if made.size() else "nothing made")
	var box_id: int = made[0] if made.size() else -1
	ed.execute("tool.create")
	ct = ed.tool
	ct.press(ed.screen_of(Vector3(40, 20, 96)), 0, false)
	ct.click(ed.screen_of(Vector3(40, 20, 96)), 0, false)
	var stacked := ct.box()
	ct.cancel()
	ed.execute("tool.select")
	check("create tool: a click on the box's top starts a one-cell box there, growing up by the last height",
			stacked[0] == Vector3(32, 16, 96) and stacked[1] == Vector3(48, 32, 192), "%s .. %s" % stacked)

	# ---- select tool
	var st: Q3SelectTool = ed.tool
	st.click(ed.screen_of(Vector3(64, 32, 96)), 0, false)
	var one := ed.selection.keys() == [box_id]
	st.click(ed.screen_of(Vector3(300, 300, 0)), K.SHIFT, false)
	var both := ed.selection.size() == 2
	st.click(ed.screen_of(Vector3(300, 300, 0)), K.CTRL, false)
	var toggled := ed.selection.keys() == [box_id]
	ed.camera.set_pose(Vector3(-6, 9, -6), -135, -10)   # looking over the floor at the sky
	st.click(Vector2(ed.get_viewport().get_visible_rect().size.x * 0.5, 40), 0, false)
	var cleared := ed.selection.is_empty()
	ed.camera.set_pose(Vector3(-6, 9, -6), -135, -50)
	check("select tool: click selects, Shift adds, Ctrl toggles, a click on nothing clears", one and both and toggled and cleared)

	# drag-move on the grid, then Alt for vertical
	st.click(ed.screen_of(Vector3(64, 32, 96)), 0, false)
	_drag([Vector3(64, 32, 96), Vector3(90, 50, 96), Vector3(131, 61, 96)])
	var moved := _box(box_id)
	_drag([Vector3(64 + 64, 32 + 32, 96), Vector3(128, 64, 96)])   # a drag that ends where it began moves nothing
	var undo_count := ed.doc._undo.size()
	check("drag moves the selection on the grid; a zero drag adds no undo step",
			moved == "64 32 0 .. 192 96 96" and _box(box_id) == moved, moved)
	var t: Q3SelectTool = ed.tool
	var a := ed.screen_of(Vector3(128, 64, 96))
	t.press(a, K.ALT, false)
	t.drag_start(a, K.ALT)
	# straight up from the grab point, 34 units: snaps to 32
	t.drag(ed.screen_of(Vector3(128, 64, 130)), K.ALT)
	t.drag_end(ed.screen_of(Vector3(128, 64, 130)), K.ALT)
	check("Alt drag moves straight up, snapped (34 units -> 32)", _box(box_id) == "64 32 32 .. 192 96 128", _box(box_id))
	ed.execute("edit.undo")
	check("undo puts the vertical move back", _box(box_id) == "64 32 0 .. 192 96 96" and ed.doc._undo.size() == undo_count, _box(box_id))

	# ---- transforms
	ed.select([box_id])
	ed.execute("edit.rotate")
	var r90 := _box(box_id)
	ed.execute("edit.rotateNeg")
	check("R turns 90 degrees about the grid-snapped center and stays on the grid; Shift+R turns back",
			r90 == "96 0 0 .. 160 128 96" and _box(box_id) == "64 32 0 .. 192 96 96", r90)
	ed.execute("edit.rotate15")
	var r15 := Q3MapGeo.brush_vertices(ed.doc.get_obj(box_id)["planes"]).size() == 24 and _box(box_id) != "64 32 0 .. 192 96 96"
	ed.execute("edit.undo")
	check("Ctrl+R turns 15 degrees; undo restores it exactly", r15 and _box(box_id) == "64 32 0 .. 192 96 96")
	ed.execute("edit.mirrorX")
	var mirrored := _box(box_id) == "64 32 0 .. 192 96 96" and Q3MapGeo.brush_valid(ed.doc.get_obj(box_id)["planes"])
	ed.execute("edit.undo")
	check("M mirrors in place (same bounds, still a valid brush)", mirrored)
	_key(KEY_RIGHT)
	var nudged := _box(box_id)
	ed.execute("edit.undo")
	check("arrow keys nudge one grid step along the camera's nearest axis", nudged != _box(box_id) and (
			nudged.begins_with("48 32") or nudged.begins_with("80 32") or nudged.begins_with("64 16") or nudged.begins_with("64 48")), nudged)
	_key(KEY_PAGEUP)
	var up := _box(box_id)
	ed.execute("edit.undo")
	check("Page Up nudges up", up == "64 32 16 .. 192 96 112", up)

	# ---- duplicate, copy, paste, delete
	var before := _brushes().size()
	ed.execute("edit.duplicate")
	var dup: Array = ed.selected_ids()
	check("Ctrl+D duplicates one grid step over and selects the copy",
			_brushes().size() == before + 1 and dup.size() == 1 and dup[0] != box_id and _box(dup[0]) != _box(box_id), _box(dup[0]) if dup.size() else "")
	ed.execute("edit.delete")
	check("Delete removes the selection", _brushes().size() == before and ed.selection.is_empty())
	ed.select([box_id])
	ed.execute("edit.copy")
	ed.mouse_pos = ed.screen_of(Vector3(-256, -256, 0))
	ed.execute("edit.paste")
	var pasted: Array = ed.selected_ids()
	check("Ctrl+C / Ctrl+V pastes at the cursor, on the grid",
			pasted.size() == 1 and _box(pasted[0]) == "-256 -256 0 .. -128 -192 96", _box(pasted[0]) if pasted.size() else "")
	ed.execute("edit.undo")
	check("undo removes the paste", _brushes().size() == before)

	# ---- groups
	ed.select([box_id])
	ed.execute("edit.duplicate")
	var copy_id: int = ed.selected_ids()[0]
	ed.select([box_id, copy_id])
	ed.execute("edit.group")
	ed.clear_selection()
	st = ed.tool
	st.click(ed.screen_of(Vector3(64 + 8, 32 + 8, 96)), 0, false)
	var whole := ed.selection.size() == 2
	st.click(ed.screen_of(Vector3(64 + 8, 32 + 8, 96)), 0, true)
	var single := ed.selection.size() == 1
	ed.select([box_id, copy_id])
	ed.execute("edit.ungroup")
	ed.clear_selection()
	st.click(ed.screen_of(Vector3(64 + 8, 32 + 8, 96)), 0, false)
	check("Ctrl+G groups: a click takes the group, a double-click one object; Ctrl+U ungroups",
			whole and single and ed.selection.size() == 1)
	ed.select([copy_id])
	ed.execute("edit.delete")

	# ---- hide, lock, isolate
	ed.select([box_id])
	ed.execute("edit.hide")
	var hidden_pick: int = ed.pick(ed.screen_of(Vector3(100, 60, 96))).get("id", -1)
	ed.execute("edit.unhideAll")
	ed.select([box_id])
	ed.execute("edit.lock")
	st.click(ed.screen_of(Vector3(100, 60, 96)), 0, false)
	var locked_sel := ed.selection.has(box_id)
	ed.execute("edit.unlockAll")
	ed.select([box_id])
	ed.execute("view.isolate")
	var iso := ed.doc.hidden.has(floor_id) and not ed.doc.hidden.has(box_id)
	ed.execute("view.isolate")
	check("H hides (picks go through), Ctrl+L locks (can't select), Shift+I isolates and restores",
			hidden_pick == floor_id and not locked_sel and iso and ed.doc.hidden.is_empty() and ed.doc.locked.is_empty())

	# ---- box select
	# a rectangle just around the box's corners (the floor's reach far outside it)
	ed.clear_selection()
	st = ed.tool
	var rect := Rect2(ed.screen_of(Vector3(64, 32, 0)), Vector2.ZERO)
	for x in [64, 192]:
		for y in [32, 96]:
			for z in [0, 96]:
				rect = rect.expand(ed.screen_of(Vector3(x, y, z)))
	rect = rect.grow(6)
	st._boxing = true
	st._start = rect.position
	st.drag_end(rect.end, 0)
	check("box select takes what lies wholly inside the rectangle", ed.selection.keys() == [box_id], str(ed.selection.keys()))

	# ---- CSG subtract
	var big := ed.doc.add(Q3MapDoc.box_brush(Vector3(-512, 256, 0), Vector3(-256, 512, 128)))
	var cutter := ed.doc.add(Q3MapDoc.box_brush(Vector3(-448, 192, 0), Vector3(-320, 576, 64)))
	ed.select([cutter])
	ed.execute("csg.subtract")
	var pieces := _brushes().filter(func(i): return i != floor_id and i != box_id)
	var vol := 0.0
	for i in pieces:
		var b := _bounds(i)
		vol += (b[3] - b[0]) * (b[4] - b[1]) * (b[5] - b[2])
	check("Ctrl+Shift+X cuts a tunnel: the pieces fill the rest, the cutter is gone",
			pieces.size() == 3 and not ed.doc.objects.has(cutter) and absf(vol - (256.0 * 256 * 128 - 128.0 * 256 * 64)) < 1e-6,
			"%d pieces" % pieces.size())
	ed.execute("edit.undo")
	check("undo brings the brush and the cutter back", ed.doc.objects.has(big) and ed.doc.objects.has(cutter))

	_e2_checks(box_id, floor_id)
	await _e3_checks(box_id, floor_id)
	await _e4_checks()

	# ---- grid, keys
	_key(KEY_7)
	var g64 := ed.grid.units() == 64
	_key(KEY_BRACKETLEFT)
	var g32 := ed.grid.units() == 32
	_key(KEY_5)
	check("number keys and [ ] set the grid", g64 and g32 and ed.grid.units() == 16)
	var unhandled := []
	for b in K.BINDINGS:
		var cmd: String = b[0]
		if cmd == "" or cmd in ["session.leave", "file.new", "file.open", "file.save", "file.saveAs", "help.cheatsheet",
				"map.playtest", "map.compile"]:   # these compile and change scenes (tests/map_playtest_test.tscn)
			continue
		if cmd.begins_with("tool.") or cmd.begins_with("edit.nudge") or cmd.begins_with("grid."):
			continue
		ed.select([box_id])
		if not ed.execute(cmd):
			unhandled.append(cmd)
	ed.ui.close_modal()           # prefab.save asks for a name
	ed.set_lights_only(false)
	while ed.doc.can_undo():
		ed.doc.undo()
	ed.execute("tool.select")
	check("every command in the keymap runs", unhandled.is_empty(), ", ".join(unhandled))
	var idx := -1
	for i in K.BINDINGS.size():
		if K.BINDINGS[i][0] == "view.focus":
			idx = i
	var clash := K.rebind(idx, KEY_F9, 0)
	var ev := InputEventKey.new()
	ev.keycode = KEY_F9
	var bound := K.commands_for(ev)
	K.reset()
	ev.keycode = KEY_F
	check("rebinding a key works and saves; reset brings the defaults back",
			clash == "" and bound == PackedStringArray(["view.focus"]) and K.commands_for(ev) == PackedStringArray(["view.focus"]))

	# ---- files
	ed.save_map("smoke_room")
	var saved := FileAccess.file_exists(Q3MapEditor.map_path("smoke_room")) and not ed.doc.dirty
	var count := ed.doc.objects.size()
	ed.new_map()
	var fresh := ed.doc.objects.size() == 1
	var err := ed.open_map(Q3MapEditor.map_path("smoke_room"))
	check("save, new, open: the map comes back with its objects, and the recent file points at it",
			saved and fresh and err == "" and ed.doc.objects.size() == count and Q3MapEditor.list_maps().size() == 1
			and FileAccess.get_file_as_string(Q3MapEditor.recent).contains("smoke_room"))
	ed.doc.add(Q3MapDoc.box_brush(Vector3(0, 0, 200), Vector3(16, 16, 216)))
	ed.execute("session.leave")
	var asked := ed.ui.modal_open()
	ed.ui.close_modal()
	check("leaving with unsaved changes asks first", asked and ed.doc.dirty)
	await _e6_checks()
	await _e7_checks()
	_e8_checks()
	await _ux_checks()

	# keymap overrides: put the player's back
	if keymap_backup != "":
		var f := FileAccess.open(K.OVERRIDES, FileAccess.WRITE)
		f.store_string(keymap_backup)
		f.close()
	K.load_overrides()
	ed.queue_free()
	_wipe()
	print("MAP EDITOR %d/%d passed" % [passed, passed + failed])
	get_tree().quit(1 if failed else 0)


func _face(id: int, f: int) -> Dictionary:
	return ed.doc.get_obj(id)["faces"][f]


## E2: the face tool, the vertex tool and curved patches. The box is 64 32 0 .. 192 96 96.
func _e2_checks(box_id: int, floor_id: int) -> void:
	ed.doc.checkpoint("E2 start")   # the undo stack is full by now (100 steps), so mark where E2 began
	ed.clear_selection()
	# ---- face tool
	_key(KEY_T)
	var ft: Q3FaceTool = ed.tool
	var top := ed.screen_of(Vector3(100, 60, 96))
	ft.click(top, 0, false)
	var one := ed.faces.keys() == [Vector2i(box_id, 4)]
	ft.click(ed.screen_of(Vector3(100, 32, 40)), K.SHIFT, false)   # the -Y side, facing the camera
	var two := ed.faces.size() == 2 and ed.faces.has(Vector2i(box_id, 3))
	ft.click(top, K.CTRL, false)
	check("face tool: click picks a face, Shift adds, Ctrl toggles", ed.tool == ed.tools["face"] and one and two
			and ed.faces.keys() == [Vector2i(box_id, 3)], str(ed.faces.keys()))
	ft.click(top, 0, false)
	_drag([Vector3(100, 60, 96), Vector3(100, 60, 110), Vector3(100, 60, 130)])
	var pushed := _box(box_id)
	ed.execute("edit.undo")
	check("face tool: dragging the top face pulls it up on the grid; undo puts it back",
			pushed == "64 32 0 .. 192 96 128" and _box(box_id) == "64 32 0 .. 192 96 96", pushed)
	ft.click(top, 0, false)
	var t0: Array = _face(box_id, 4)["tex"].duplicate()
	_key(KEY_RIGHT)
	var shifted: Array = _face(box_id, 4)["tex"]
	_key(KEY_UP, K.CTRL)
	var scaled: Array = _face(box_id, 4)["tex"]
	_key(KEY_LEFT, K.CTRL)
	var rotated: Array = _face(box_id, 4)["tex"]
	check("face tool: arrows shift the texture by the grid, Ctrl+Up scales, Ctrl+Left rotates 15",
			shifted[0] == t0[0] + 16 and is_equal_approx(scaled[3], t0[3] * 1.1) and rotated[2] == 15.0,
			"%s -> %s" % [t0, rotated])
	ed.execute("edit.undo")
	ed.execute("edit.undo")
	ed.execute("edit.undo")
	var floor_mat: String = _face(floor_id, 4)["material"]
	ft.click(ed.screen_of(Vector3(-300, -300, 0)), K.ALT, false)
	check("face tool: Alt+click copies the floor's material onto the selected face; undo reverts it",
			_face(box_id, 4)["material"] == floor_mat and ed.doc.undo_label() == "Eyedropper")
	ed.execute("edit.undo")
	ed.select_faces([])
	ed.select([box_id])
	_key(KEY_T, K.CTRL)
	var all_locked: bool = ed.doc.get_obj(box_id)["faces"].all(func(f): return f.get("lock", false) and f.has("uv"))
	_key(KEY_T, K.CTRL)
	var freed: bool = ed.doc.get_obj(box_id)["faces"].all(func(f): return not f.get("lock", false))
	check("Ctrl+T locks every face of the selected brush, again frees them", all_locked and freed)
	ed.execute("edit.undo")
	ed.execute("edit.undo")

	# ---- vertex tool
	ed.select([box_id])
	_key(KEY_V)
	var vt: Q3VertexTool = ed.tool
	var handles := vt.handles().size()
	var corner := Vector3(192, 96, 96)
	vt.click(ed.screen_of(corner), 0, false)
	var picked := ed.verts.size() == 1
	_drag([corner, corner + Vector3(16, 0, 0), corner + Vector3(32, 0, 0)])
	var dragged := Q3MapGeo.brush_bounds(ed.doc.get_obj(box_id)["planes"])
	var still := ed.verts.size() == 1
	ed.execute("edit.undo")
	check("vertex tool: 8 handles; dragging a corner reshapes the brush on the grid and keeps the corner selected",
			handles == 8 and picked and dragged[3] == 224.0 and Q3MapGeo.brush_valid(ed.doc.get_obj(box_id)["planes"]) and still,
			"%d handles, %s" % [handles, dragged])
	vt.click(ed.screen_of(corner), 0, false)
	_key(KEY_DELETE)
	var chamfer := [_corners(box_id), ed.doc.get_obj(box_id)["faces"].size()]
	ed.execute("edit.undo")
	vt.click(ed.screen_of(corner), 0, false)
	vt.click(ed.screen_of(Vector3(192, 32, 96)), K.SHIFT, false)
	_key(KEY_DELETE)
	var wedge := [_corners(box_id), ed.doc.get_obj(box_id)["faces"].size()]
	ed.execute("edit.undo")
	check("Delete on one corner makes a chamfer (7 corners, 7 faces); on a box edge a wedge (6, 5)",
			chamfer == [7, 7] and wedge == [6, 5] and _box(box_id) == "64 32 0 .. 192 96 96", "%s %s" % [chamfer, wedge])
	var tilted := ed.doc.add(Q3MapDoc.transform_brush(Q3MapDoc.box_brush(Vector3(256, 256, 0), Vector3(320, 320, 64)),
			Q3MapDoc.yaw_rotation(15, Vector3(288, 288, 0))))
	ed.select([tilted])
	ed.verts.clear()
	_key(KEY_G, K.CTRL | K.SHIFT)
	var on_grid := true
	var pts := ed.object_points(tilted)
	for v in pts:
		on_grid = on_grid and fmod(v, 16.0) == 0.0
	check("Ctrl+Shift+G snaps a turned brush's corners to the grid", on_grid and Q3MapGeo.brush_valid(ed.doc.get_obj(tilted)["planes"]))

	# ---- curves
	ed.mouse_pos = ed.screen_of(Vector3(-256, 128, 0))
	ed.execute("patch.bevel")
	var bevel: int = ed.selected_ids()[0] if ed.selection.size() == 1 else -1
	var bp := ed.doc.get_obj(bevel)
	var tess := ed.doc.patch_tess(bevel)
	var mid: int = (tess["height"] / 2) * tess["width"] + tess["width"] / 2
	var pt: Vector3 = Vector3(tess["points"][mid * 3], tess["points"][mid * 3 + 1], tess["points"][mid * 3 + 2])
	var gn: Vector3 = tess["arrays"][Mesh.ARRAY_NORMAL][mid]
	var n := Vector3(gn.z, gn.x, gn.y)
	var outward := n.dot(Vector3(pt.x + 256, pt.y - 128, 0).normalized()) > 0.9
	check("Curves > Bevel drops a 3x3 quarter circle at the cursor facing outward, selected, in the vertex tool",
			bp.get("width", 0) == 3 and bp.get("height", 0) == 3 and ed.tool == ed.tools["vertex"] and outward
			and Vector3(bp["ctrl"][0], bp["ctrl"][1], bp["ctrl"][2]) == Vector3(-192, 128, 0),
			"normal %s at %s" % [n, pt])
	var face_pick := ed.pick(ed.screen_of(pt))
	check("a patch can be picked where it is drawn", face_pick.get("id", -1) == bevel and face_pick.get("kind", "") == "patch",
			str(face_pick.get("id", -1)))
	vt = ed.tool
	var cp := Vector3(bp["ctrl"][20], bp["ctrl"][21], bp["ctrl"][22])   # the middle row's corner point
	vt.click(ed.screen_of(cp), 0, false)
	_drag([cp, cp + Vector3(16, 16, 0), cp + Vector3(32, 32, 0)])
	var moved_cp := Vector3(ed.doc.get_obj(bevel)["ctrl"][20], ed.doc.get_obj(bevel)["ctrl"][21], ed.doc.get_obj(bevel)["ctrl"][22])
	check("dragging a control point pulls the curve", moved_cp == cp + Vector3(32, 32, 0), "%s -> %s" % [cp, moved_cp])
	ed.execute("patch.cylinder")
	var cyl: Dictionary = ed.doc.get_obj(ed.selected_ids()[0])
	ed.execute("patch.arch")
	var arch: Dictionary = ed.doc.get_obj(ed.selected_ids()[0])
	ed.execute("tool.select")
	ed.select([bevel])
	ed.execute("edit.duplicate")
	var dup_ok: bool = ed.selection.size() == 1 and ed.doc.get_obj(ed.selected_ids()[0]).get("type", "") == "patch"
	check("cylinder is a 9x3 net, arch 5x3; a patch duplicates like a brush",
			cyl.get("width", 0) == 9 and cyl.get("height", 0) == 3 and arch.get("width", 0) == 5 and dup_ok)
	while ed.doc.can_undo() and ed.doc.undo_label() != "E2 start":
		ed.doc.undo()
	ed.doc.undo()
	ed.execute("tool.select")
	ed.clear_selection()
	check("undo takes every E2 change back", _box(box_id) == "64 32 0 .. 192 96 96" and ed.doc.ids_of("patch").is_empty()
			and not ed.doc.objects.has(tilted), "%s, %d patches" % [_box(box_id), ed.doc.ids_of("patch").size()])


## E3: the material browser, painting, tint, the inspector's mapping edits and alignment, skies and
## tool shaders. The box is 64 32 0 .. 192 96 96.
func _e3_checks(box_id: int, floor_id: int) -> void:
	ed.doc.checkpoint("E3 start")
	ed.execute("tool.select")
	ed.clear_selection()
	var br: Q3EditorBrowser = ed.ui.browser
	var all_n := br.visible_names().size()
	br._search.text = "concrete"
	br.rebuild()
	var found := br.visible_names()
	br._search.text = ""
	br.set_tab("sky")
	var skies := br.visible_names().size()
	br.set_tab("tool")
	var tools_n := br.visible_names().size()
	br.set_tab("surface")
	check("browser: 1,400+ materials, search narrows them, 49 skies, the tool shaders",
			all_n > 1400 and found.size() > 3 and found.size() < 40 and found.has("textures/base_wall/concrete") and skies == 49 and tools_n == 72,
			"%d / %d / %d / %d" % [all_n, found.size(), skies, tools_n])
	await get_tree().process_frame
	await get_tree().process_frame
	await get_tree().process_frame
	await get_tree().process_frame
	var thumbs := br._tiles.filter(func(t): return t["loaded"] and t["rect"].texture != null).size()
	check("browser: thumbnails load for the tiles in view (and only those)", thumbs > 0 and thumbs < 60, "%d loaded" % thumbs)

	# paint the box's top face from the face tool, then the whole box from the select tool
	ed.execute("tool.face")
	(ed.tool as Q3FaceTool).click(ed.screen_of(Vector3(100, 60, 96)), 0, false)
	var lava := "textures/liquids/lavahell"
	br._search.text = "lavahell"
	br.rebuild()
	var tile: Dictionary = br._tiles.filter(func(t): return t["name"] == lava)[0]
	tile["button"].emit_signal("pressed")
	var top_lava: bool = _face(box_id, 4)["material"] == lava and _face(box_id, 0)["material"] != lava
	ed.execute("tool.select")
	ed.select([box_id])
	ed.apply_material("textures/gothic_block/blocks15")
	var all_blocks: bool = ed.doc.get_obj(box_id)["faces"].all(func(f): return f["material"] == "textures/gothic_block/blocks15")
	check("clicking a tile paints the picked face; with the select tool, every face of the brush",
			top_lava and all_blocks and ed.material == "textures/gothic_block/blocks15"
			and Q3MapMaterials.prefs()["recent"][0] == "textures/gothic_block/blocks15")
	Q3MapMaterials.toggle_favorite(lava)
	br.folder = "*fav"
	br._search.text = ""
	br.rebuild()
	check("right-click favorites show in the Favorites list (and are saved)",
			br.visible_names() == PackedStringArray([lava]) and FileAccess.get_file_as_string(Q3MapMaterials.prefs_path).contains(lava))
	br.set_tab("surface")

	# tint
	ed.set_tint(Color(0.5, 0.8, 1.0))
	var tinted: bool = ed.doc.get_obj(box_id)["faces"].all(func(f): return f.get("color", Color.WHITE) == Color(0.5, 0.8, 1.0))
	var tmat := Q3MapMaterials.material("textures/gothic_block/blocks15", Color(0.5, 0.8, 1.0)) as ShaderMaterial
	ed.set_tint(Color.WHITE)
	var cleared: bool = ed.doc.get_obj(box_id)["faces"].all(func(f): return not f.has("color"))
	check("tint colors every face and draws with a tinted copy of the material; white clears it",
			# materials are cached by the tint's HTML code, so 8 bits a channel: 0.5 comes back as 0.502
			tinted and tmat != null and _close(tmat.get_shader_parameter("tint"), Color(0.5, 0.8, 1.0)) and cleared,
			"tinted %s, material %s, param %s, cleared %s" % [tinted, tmat, tmat.get_shader_parameter("tint") if tmat else null, cleared])

	# the inspector's mapping fields and alignment, on the top face
	ed.execute("tool.face")
	(ed.tool as Q3FaceTool).click(ed.screen_of(Vector3(100, 60, 96)), 0, false)
	await get_tree().process_frame
	var title: String = ed.ui.inspector._fields["title"].text
	ed.edit_faces("field", 2, 30.0)
	var rot: float = _face(box_id, 4)["tex"][2]
	ed.edit_faces("align", "fit")
	var b := ed.doc.get_obj(box_id)
	var r: Array = Q3FaceTool._ranges(Q3MapDoc.face_uv(_face(box_id, 4), Q3MapDoc.plane_of(b, 4)), ed.doc.windings(box_id)[4])
	var tw := Q3MapMaterials.size("textures/gothic_block/blocks15")
	var fit_ok := absf(r[0]) < 1e-4 and absf(r[1] - tw.x) < 1e-4 and absf(r[2]) < 1e-4 and absf(r[3] - tw.y) < 1e-4
	ed.edit_faces("reset")
	ed.edit_faces("align", "right")
	r = Q3FaceTool._ranges(Q3MapDoc.face_uv(_face(box_id, 4), Q3MapDoc.plane_of(ed.doc.get_obj(box_id), 4)), ed.doc.windings(box_id)[4])
	var right_ok := absf(fposmod(r[1], tw.x)) < 1e-4 or absf(fposmod(r[1], tw.x) - tw.x) < 1e-4
	ed.edit_faces("reset")
	check("inspector: shows the face, sets rotation, Fit puts one texture across the face, Right puts its edge on a texture edge, Reset",
			title == "1 FACE" and rot == 30.0 and fit_ok and right_ok and _face(box_id, 4)["tex"] == Q3MapDoc.DEFAULT_TEX,
			"%s, %s" % [title, r])
	ed.set_lock(true)
	var locked: bool = _face(box_id, 4).get("lock", false) and _face(box_id, 4).has("uv")
	ed.edit_faces("align", "fit")
	r = Q3FaceTool._ranges(PackedFloat64Array(_face(box_id, 4)["uv"]), ed.doc.windings(box_id)[4])
	check("alignment works on explicit (texture-locked) mappings too", locked and absf(r[1] - r[0] - tw.x) < 1e-4, str(r))

	# Shift+T: the face tool takes whole objects
	_key(KEY_T, K.SHIFT)
	(ed.tool as Q3FaceTool).click(ed.screen_of(Vector3(100, 60, 96)), 0, false)
	var whole := ed.faces.size() == 6
	_key(KEY_T, K.SHIFT)
	check("Shift+T switches the face tool to whole objects (all 6 faces) and back", whole and not (ed.tool as Q3FaceTool).object_mode)

	# skies and tool shaders
	var sky := Q3MapMaterials.material("textures/skies/hellsky") as ShaderMaterial
	var caulk := Q3MapMaterials.material("textures/common/caulk")
	var clip := Q3MapMaterials.material("textures/common/clip") as StandardMaterial3D
	check("sky faces draw the sky through the brush; caulk is solid pink, clip see-through",
			sky != null and sky.shader.resource_path.ends_with("editor_sky_face.gdshader") and sky.get_shader_parameter("layer1") != null
			and caulk is StandardMaterial3D and clip.transparency == BaseMaterial3D.TRANSPARENCY_ALPHA
			and Q3MapMaterials.thumbnail("textures/skies/hellsky") != null and Q3MapMaterials.thumbnail("textures/base_wall/concrete") != null)
	_key(KEY_TAB)
	var hidden: bool = not ed.ui.panels_shown()
	_key(KEY_TAB)
	check("Tab hides and shows the browser and inspector", hidden and ed.ui.browser.visible)

	ed.execute("tool.select")
	while ed.doc.can_undo() and ed.doc.undo_label() != "E3 start":
		ed.doc.undo()
	ed.doc.undo()
	ed.select_faces([])
	ed.clear_selection()
	check("undo takes every E3 change back", ed.doc.get_obj(box_id)["faces"].all(func(f): return f["material"] == Q3MapMaterials.DEFAULT and f.has("tex")))


func _ents(cn: String) -> Array:
	return ed.doc.ids_of("entity").filter(func(i): return ed.doc.get_obj(i)["classname"] == cn)


func _k(id: int, key: String) -> String:
	return String(ed.doc.get_obj(id).get("keys", {}).get(key, ""))


func _click(at: Vector3, mods := 0) -> void:
	ed.tool.hover(ed.screen_of(at), mods)
	ed.tool.click(ed.screen_of(at), mods, false)


func _e4_checks() -> void:
	ed.doc.checkpoint("E4 start")
	ed.execute("tool.select")
	ed.clear_selection()
	ed.grid.set_index(4)   # 16 units

	# P: a spawn on the floor, facing the way the camera looks (yaw 45: toward +x +y)
	_key(KEY_P)
	var et: Q3EntityTool = ed.tool
	_click(Vector3(304, -96, 0))   # clear of the box, which would take the click
	var spawns := _ents("info_player_deathmatch")
	var sp: int = spawns[0] if spawns.size() else -1
	check("P: a spawn point on the floor, 24 up, on the grid, facing the camera's way, selected",
			et.kind == "spawn" and spawns.size() == 1 and _k(sp, "origin") == "304 -96 24" and _k(sp, "angle") == "45" and ed.selection.has(sp),
			"%s angle %s" % [_k(sp, "origin"), _k(sp, "angle")])
	var hit := ed.pick(ed.screen_of(Vector3(304, -96, 30)), true)
	var floor_hit := ed.pick(ed.screen_of(Vector3(304, -96, 30)))
	check("entities are picked by their box (with_entities), and only then", hit.get("id", -1) == sp and hit["kind"] == "entity"
			and floor_hit.get("kind", "") != "entity")

	# I: weapons, . and , cycle the tool, then the selected pickup
	_key(KEY_I)
	_key(KEY_PERIOD)
	var next_w := et.weapon
	_click(Vector3(100, 300, 0))
	var w: Array = ed.doc.ids_of("entity").filter(func(i): return String(ed.doc.get_obj(i)["classname"]).begins_with("weapon_"))
	var placed_cls: String = ed.doc.get_obj(w[0])["classname"] if w.size() else ""
	_key(KEY_COMMA)
	check("I places a weapon on the floor (16 up); . steps the weapon; , changes the selected pickup back",
			next_w == "weapon_lightning" and placed_cls == "weapon_lightning" and _k(w[0], "origin") == "96 304 16"
			and ed.doc.get_obj(w[0])["classname"] == "weapon_rocketlauncher", "%s %s" % [next_w, _k(w[0], "origin") if w.size() else ""])
	_key(KEY_O)
	_click(Vector3(160, 400, 0))
	check("O places an item", _ents("item_armor_combat").size() == 1)

	# J: the pad, then where to land
	_key(KEY_J)
	_click(Vector3(256, -224, 0))   # rays to these miss the box
	var pending: bool = et.first != null
	_click(Vector3(448, -448, 0))
	var pushes := _ents("trigger_push")
	var push: int = pushes[0] if pushes.size() else -1
	var tgts := _ents("target_position")
	var own := Q3MapEntities.owned(ed.doc)
	var trig: int = own.get(push, [-1])[0]
	var grp := int(ed.doc.get_obj(push).get("group", 0))
	var pads := _brushes().filter(func(i): return _face(i, 0)["material"] == Q3MapEditor.JUMP_PAD_MATERIAL)
	var linked := tgts.size() == 1 and _k(push, "target") == "pad1" and _k(tgts[0], "targetname") == "pad1"
	check("J: click the pad, then the landing: a bounce pad, its trigger_push (trigger brush) and the target, linked and grouped",
			pending and pushes.size() == 1 and linked and trig > 0 and _face(trig, 0)["material"] == Q3MapEditor.TRIGGER_MATERIAL
			and pads.size() == 1 and grp != 0 and int(ed.doc.get_obj(trig)["group"]) == grp and int(ed.doc.get_obj(pads[0])["group"]) == grp)
	var from := Q3MapEntities.center(ed.doc, ed.doc.get_obj(push))
	var apex := Q3MapEntities.origin(ed.doc.get_obj(tgts[0]))
	# the player's origin rides 10 above the trigger's center, so the center's arc is followed down to
	# 14 above the floor, where the player's origin is 24 up: standing on the second click
	var arc := Q3EditorEntities.arc_points(from, apex, Q3MapEntities.gravity(ed.doc), func(_a, b: Vector3): return b if b.z <= 14.0 else null)
	var land: Vector3 = arc[0][arc[0].size() - 1] if arc.size() else Vector3.INF
	check("the pad's arc (AimAtTarget) tops out at the target and the player comes down where the second click was",
			from == Vector3(256, -224, 18) and Vector2(land.x - 448, land.y + 448).length() < 8.0
			and int(ed.doc.get_obj(tgts[0])["group"]) == grp, "from %s, lands at %s" % [from, land])
	for i in 120:
		await get_tree().process_frame
		if ed.ents._arcs.size():
			break
	check("the entity layer draws the links (arcs, arrows) and tests where the arc lands",
			ed.ents._lines.mesh != null and ed.ents._arcs.size() >= 1)

	# K: teleporter and its exit
	_key(KEY_K)
	_click(Vector3(-200, 300, 0))
	_click(Vector3(352, 48, 0))
	var teles := _ents("trigger_teleport")
	var dests := _ents("misc_teleporter_dest")
	check("K: a trigger_teleport and its misc_teleporter_dest, linked, the exit 24 up facing the camera's way",
			teles.size() == 1 and dests.size() == 1 and _k(teles[0], "target") == "tele1" and _k(dests[0], "targetname") == "tele1"
			and _k(dests[0], "origin") == "352 48 24" and _k(dests[0], "angle") == "45", _k(dests[0], "origin") if dests.size() else "")

	# L: lights; Shift+click aims a spot through an info_null
	_key(KEY_L)
	_click(Vector3(320, 320, 0))
	_click(Vector3(380, 300, 0), K.SHIFT)
	var lights := _ents("light")
	var nulls := _ents("info_null")
	var spot: int = lights.filter(func(i): return _k(i, "target") != "")[0] if lights.size() == 2 else -1
	ed.clear_selection()
	ed.ents.flush()
	var nodes := ed.ents.light_nodes()
	var omni: Array = nodes.filter(func(n): return n is OmniLight3D)
	var lp := Q3MapEntities.light_params({"light": "300"})
	check("L: a light 16 up; Shift+click a spot aimed at an info_null; drawn as real lights with q3map's falloff",
			lights.size() == 2 and nulls.size() == 1 and _k(spot, "target") == _k(nulls[0], "targetname") and _k(lights[0], "origin") == "320 320 16"
			and omni.size() == 1 and nodes.filter(func(n): return n is SpotLight3D).size() == 1
			and absf(omni[0].light_energy - 300.0 * 7500.0 / 255.0 / 1024.0) < 1e-3 and absf(lp["range"] - 1500.0 / 32.0) < 1e-3,
			"%d lights, %d nulls, %s, %d omni, energy %s" % [lights.size(), nulls.size(), _k(lights[0], "origin") if lights.size() else "",
			omni.size(), omni[0].light_energy if omni.size() else -1.0])
	_key(KEY_L, K.SHIFT)
	var lit: bool = ed.lights_only and not ed._sun.visible and omni[0].shadow_enabled
	_key(KEY_L, K.SHIFT)
	check("Shift+L: the map's own lights only (preview sun off, lights cast shadows), and back", lit and ed._sun.visible and not omni[0].shadow_enabled)

	# N: sound
	_key(KEY_N)
	_click(Vector3(200, 100, 0))
	var sp_ids := _ents("target_speaker")
	check("N: a looping target_speaker", sp_ids.size() == 1 and _k(sp_ids[0], "spawnflags") == "1" and _k(sp_ids[0], "noise").begins_with("sound/world/")
			and Q3MapEntities.sounds().size() > 20 and Q3MapEntities.sound_stream(_k(sp_ids[0], "noise")) != null)

	# Shift+P: spawn at the camera
	ed.execute("tool.select")
	_key(KEY_P, K.SHIFT)
	var eye := Q3MapEditor.to_id(ed.camera.global_position)
	var cam_spawn: int = _ents("info_player_deathmatch").filter(func(i): return i != sp)[0]
	check("Shift+P: a spawn that puts the player's eye at the camera (origin 35 below)",
			absf(Q3MapEntities.origin(ed.doc.get_obj(cam_spawn)).z - (eye.z - 35.0)) <= 0.5)

	# moving, turning and mirroring an entity
	ed.select([sp])
	ed.move_selection(Vector3(16, 0, 0))
	var moved := _k(sp, "origin")
	ed.rotate_selection(90)
	var turned := _k(sp, "angle")
	ed.execute("edit.mirrorX")
	check("entities move, turn (the angle follows) and mirror", moved == "320 -96 24" and turned == "135" and _k(sp, "angle") == "45",
			"%s %s %s" % [moved, turned, _k(sp, "angle")])

	# duplicating the pad with its target keeps the copy's links inside the copy
	ed.select(ed.group_of(push) + [tgts[0]])
	ed.execute("edit.duplicate")
	var push2: int = _ents("trigger_push").filter(func(i): return i != push)[0]
	var tgt2: int = _ents("target_position").filter(func(i): return i != tgts[0])[0]
	own = Q3MapEntities.owned(ed.doc)
	check("duplicate: the copied pad targets its own copied target (pad2), and the copied trigger belongs to the copy",
			_k(push2, "target") == "pad2" and _k(tgt2, "targetname") == "pad2" and _k(push, "target") == "pad1"
			and own.get(push2, []).size() == 1 and own.get(push, []).size() == 1)
	ed.select(ed.group_of(push2))
	ed.execute("edit.delete")
	var gone: bool = not ed.doc.objects.has(push2) and not own[push2].any(func(i): return ed.doc.objects.has(i))
	ed.select([trig])
	ed.execute("edit.delete")
	check("deleting a brush entity takes its brushes; deleting its last brush takes the entity", gone and not ed.doc.objects.has(push))
	ed.execute("edit.undo")

	# review fixes: up/down movers keep angle -1 / -2 when moved or turned; copies never merge links
	var lift := ed.doc.add(Q3MapEntities.point("func_bobbing", Vector3(0, 0, 0), {"angle": "-1"}))
	ed.select([lift])
	ed.move_selection(Vector3(32, 0, 0))
	ed.rotate_selection(90)
	var lift_angle := _k(lift, "angle")
	var a1 := Q3MapEntities.point("target_position", Vector3(-400, 400, 64), {"targetname": "pad1"})
	a1["id"] = 9001
	var a2 := Q3MapEntities.point("target_position", Vector3(-380, 400, 64), {"targetname": "pad2"})
	a2["id"] = 9002
	var b1 := Q3MapEntities.brush_entity("trigger_push", {"target": "pad2"})
	b1["id"] = 9003
	var copies := ed.place_copies([a1, a2, b1], Vector3(-400, 400, 64), "Paste")
	var cn := copies.map(func(i): return _k(i, "targetname")).filter(func(t): return t != "")
	var pushed: String = _k(copies[2], "target")
	var copy_tn2: String = _k(copies[1], "targetname")
	check("an up/down mover keeps angle -1 through moves and turns; copied links stay apart (pad1 and pad2 never merge)",
			lift_angle == "-1" and cn.size() == 2 and cn[0] != cn[1] and pushed == copy_tn2 and pushed != "pad1",
			"angle %s, names %s, target %s" % [lift_angle, cn, pushed])
	ed.doc.remove(copies + [lift])

	# brush entities from the Entities tab
	var door := ed.doc.add(Q3MapDoc.box_brush(Vector3(-300, -300, 0), Vector3(-280, -200, 128)))
	var hurt := ed.doc.add(Q3MapDoc.box_brush(Vector3(-200, -300, -16), Vector3(-100, -200, 0)))
	ed.select([door])
	ed.use_class("func_door")
	var de := int(ed.doc.get_obj(door)["entity"])
	ed.select([hurt])
	ed.use_class("trigger_hurt")
	var he := int(ed.doc.get_obj(hurt)["entity"])
	check("the Entities tab's brush classes make the selected brushes into that entity (triggers get the trigger shader)",
			de > 0 and ed.doc.get_obj(de)["classname"] == "func_door" and he > 0 and ed.doc.get_obj(he)["classname"] == "trigger_hurt"
			and _face(hurt, 0)["material"] == Q3MapEditor.TRIGGER_MATERIAL and _face(door, 0)["material"] != Q3MapEditor.TRIGGER_MATERIAL)

	# inspector
	ed.select([lights[0]])
	await get_tree().process_frame
	var ins: Q3EditorInspector = ed.ui.inspector
	var intensity: SpinBox = ins._fields.get("num:light")
	var shown := intensity != null and intensity.value == 300.0
	if intensity:
		intensity.value = 500
	ed.set_keys([lights[0]], {"_style": "warm"})
	await get_tree().process_frame
	var kv: LineEdit = ins._fields.get("kv:_style")
	check("inspector: a light's intensity field edits its key; new keys get a row",
			shown and _k(lights[0], "light") == "500" and kv != null and kv.text == "warm")
	ed.select(ed.group_of(push))
	await get_tree().process_frame
	var flight: String = ins._fields["flight"].text if ins._fields.has("flight") else ""
	ed.clear_selection()
	await get_tree().process_frame
	var world := ins._fields.has("num:gravity")
	ed.set_world({"message": "Smoke Arena"})
	await get_tree().process_frame
	var msg_field: LineEdit = ins._fields.get("world:message")
	var msg_ok := msg_field != null and msg_field.text == "Smoke Arena"
	ed.set_world({"message": ""})
	ed.set_world({"gravity": "400"})
	var v400 := Q3MapEntities.push_velocity(from, apex, Q3MapEntities.gravity(ed.doc))
	ed.set_world({"gravity": ""})
	check("inspector: a jump pad's flight readout; with nothing selected, the map settings (gravity reshapes the arcs)",
			flight.begins_with("Launch") and world and msg_ok and absf(v400.z - sqrt(2.0 * 400.0 * (apex.z - from.z))) < 0.01,
			"%s, message field %s" % [flight, msg_ok])

	# the map's skybox: sky faces take it, the background follows, undo puts it back
	var skies := Q3MapMaterials.names("sky")
	if skies.size() >= 2:
		var sb := Q3MapDoc.box_brush(Vector3(2000, 2000, 2000), Vector3(2064, 2064, 2064), skies[0])
		var sid := ed.doc.add(sb)
		var sn := ed.set_sky(skies[1])
		await get_tree().process_frame
		var took: bool = String(ed.doc.get_obj(sid)["faces"][0]["material"]) == skies[1]
		var bg: bool = ed._env.background_mode == Environment.BG_SKY or Q3MapMaterials.sky_resource(skies[1]) == null
		var sel: OptionButton = ins._fields.get("sky")
		var picked: bool = sel != null and sel.selected == 1 + skies.find(skies[1])
		ed.set_sky("")
		check("map settings: Sky retextures the sky faces, sets the view background and the field", sn >= 6 and took and bg and picked,
				"%d changed, took %s, bg %s, field %s" % [sn, took, bg, picked])
		ed.doc.remove([sid])

	# the browser's Entities tab
	var br: Q3EditorBrowser = ed.ui.browser
	br.set_tab("entity")
	var names := br.visible_names()
	var tile: Dictionary = br._tiles.filter(func(t): return t["name"] == "weapon_railgun")[0]
	tile["button"].emit_signal("pressed")
	var armed: bool = ed.tool == et and et.kind == "weapon" and et.weapon == "weapon_railgun"
	br.set_tab("surface")
	check("Entities tab: every pickup and map class; a tile arms the tool with it",
			names.size() == Q3.entities.size() + Q3.map_entities.size() - 1 and names.has("func_door") and armed, "%d" % names.size())
	ed.execute("tool.select")

	# prefabs: save the pad and its target, place it turned, links stay inside the copy
	ed.select(ed.group_of(push) + [tgts[0]])
	var err := ed.save_prefab("pad_test")
	br.set_tab("prefab")
	var listed := br.visible_names() == PackedStringArray(["pad_test"])
	br.set_tab("surface")
	ed.start_prefab("pad_test")
	var pt: Q3PrefabTool = ed.tool
	_key(KEY_R)
	var before := ed.doc.objects.size()
	_click(Vector3(-300, 300, 0))
	var placed := ed.selected_ids()
	var push3: Array = placed.filter(func(i): return ed.doc.get_obj(i).get("classname", "") == "trigger_push")
	var tgt3: Array = placed.filter(func(i): return ed.doc.get_obj(i).get("classname", "") == "target_position")
	own = Q3MapEntities.owned(ed.doc)
	var one_group: bool = placed.all(func(i): return int(ed.doc.get_obj(i)["group"]) == int(ed.doc.get_obj(placed[0])["group"]))
	var link_ok: bool = push3.size() == 1 and tgt3.size() == 1 and _k(push3[0], "target") == _k(tgt3[0], "targetname") and _k(push3[0], "target") != "pad1"
	# turned a quarter: the target went from +x+y of the pad to -x... check the pad -> target heading turned by 90
	var h_old := Q3MapEntities.origin(ed.doc.get_obj(tgts[0])) - Q3MapEntities.center(ed.doc, ed.doc.get_obj(push))
	var h_new := Q3MapEntities.origin(ed.doc.get_obj(tgt3[0])) - Q3MapEntities.center(ed.doc, ed.doc.get_obj(push3[0])) if link_ok else Vector3.ZERO
	check("Ctrl+P saves a prefab (listed in the Prefabs tab); the prefab tool places it turned (R), grouped, its link renamed",
			err == OK and listed and pt.turns == 1 and ed.doc.objects.size() - before == 4 and one_group and link_ok
			and own.get(push3[0], []).size() == 1 and absf(h_new.x + h_old.y) < 1.0 and absf(h_new.y - h_old.x) < 1.0,
			"%d new, %s vs %s" % [ed.doc.objects.size() - before, h_old, h_new])
	_key(KEY_ESCAPE)
	ed.delete_prefab("pad_test")
	check("deleting a prefab removes its file", not Q3MapPrefabs.exists("pad_test") and ed.tool == ed.tools["select"])

	# entities survive save and open
	var keys_before := {}
	for id in ed.doc.ids_of("entity"):
		keys_before[id] = [ed.doc.get_obj(id)["classname"], ed.doc.get_obj(id)["keys"]]
	var p := SCRATCH.path_join("e4_room" + Q3MapDoc.EXTENSION)
	ed.doc.save(p)
	var back := Q3MapDoc.new()
	var load_err := back.load_file(p)
	var same := load_err == "" and back.ids_of("entity").size() == keys_before.size()
	for id in keys_before:
		var e := back.get_obj(id)
		if e.is_empty() or e["classname"] != keys_before[id][0] or e["keys"] != keys_before[id][1]:
			same = false
	check("entities save and open with their classes and keys", same and keys_before.size() >= 14, "%d entities" % keys_before.size())

	ed.execute("tool.select")
	while ed.doc.can_undo() and ed.doc.undo_label() != "E4 start":
		ed.doc.undo()
	ed.doc.undo()
	ed.clear_selection()
	check("undo takes every E4 change back", ed.doc.ids_of("entity").is_empty() and not ed.doc.objects.has(door))


func _e6_checks() -> void:
	# ---- .map export and import through the editor
	var count := ed.doc.ids_of("brush").size() + ed.doc.ids_of("patch").size()
	var out :=ProjectSettings.globalize_path(SCRATCH.path_join("smoke_export.map"))
	var tb := ProjectSettings.globalize_path(SCRATCH.path_join("smoke_export_tb.map"))
	var e1 := ed.export_map_file(out, "brushdef")
	var e2 := ed.export_map_file(tb, "valve220")
	var texts := FileAccess.get_file_as_string(out).contains("brushDef") and FileAccess.get_file_as_string(tb).contains("\"mapversion\" \"220\"")
	ed.doc.dirty = false
	var err: String = await ed.import_map("map", tb)
	check("export .map in both forms, and import one back as a new unsaved map with every brush and curve",
			e1 == OK and e2 == OK and texts and err == "" and ed.path == "" and ed.doc.dirty
			and ed.doc.ids_of("brush").size() + ed.doc.ids_of("patch").size() == count and count > 0 and ed.importing == "",
			"%d brushes and curves before, %d after" % [count, ed.doc.ids_of("brush").size() + ed.doc.ids_of("patch").size()])
	err = await ed.import_map("map", SCRATCH.path_join("not_there.map"))
	check("importing a missing file says so and keeps the open map", err != "" and ed.doc.ids_of("brush").size() > 0, err)

	# ---- Quake 3's maps from the player's paks
	if Q3BspImport.maps_in_paks().has("q3dm17"):
		ed.doc.dirty = false
		ed.execute("file.importQ3")
		var listed := ed.ui.modal_open()
		ed.ui.close_modal()
		var before := ed.doc
		var pending: Signal = _import_signal("q3dm17")
		await get_tree().process_frame
		var blocking := ed.ui.modal_open() and ed.importing == "q3dm17"
		# Escape is the dialog's Cancel: the import finishes in the background and is thrown away
		var esc := InputEventKey.new()
		esc.keycode = KEY_ESCAPE
		esc.pressed = true
		ed.ui._input(esc)
		await pending
		check("Import an original map lists the paks' maps; the import runs behind a dialog whose Cancel drops it",
				listed and blocking and is_same(ed.doc, before) and ed.importing == "")
		ed.doc.dirty = false
		var t := Time.get_ticks_msec()
		err = await ed.import_map("bsp", "q3dm17")
		var spot := Vector3.ZERO
		for id in ed.doc.ids_of("entity"):
			if ed.doc.get_obj(id)["classname"] == "info_player_intermission":
				spot = Q3MapEntities.vec(String(ed.doc.get_obj(id)["keys"]["origin"]))
				break   # the first (q3dm17 has two)
		check("q3dm17 imports from the paks as an unsaved map, the camera at its intermission view",
				err == "" and ed.doc.ids_of("brush").size() == 456 and ed.doc.ids_of("patch").size() == 32 and ed.path == ""
				and ed.doc.dirty and spot != Vector3.ZERO and ed.camera.position.distance_to(Q3MapEditor.to_godot(spot)) < 0.01,
				"%d brushes, %d curves, %d ms, err '%s', camera %s, spot %s" % [ed.doc.ids_of("brush").size(), ed.doc.ids_of("patch").size(),
				Time.get_ticks_msec() - t, err, ed.camera.position, Q3MapEditor.to_godot(spot)])
		ed.doc.dirty = false
	else:
		print("  (no q3dm17 in the paks: the pak import checks are skipped)")


func _e7_checks() -> void:
	ed.doc.dirty = false
	ed.new_map()
	var d := ed.doc
	var floor_id: int = d.ids_of("brush")[0]
	var caulk := d.add(Q3MapDoc.box_brush(Vector3(-64, -64, 0), Vector3(64, 64, 64), "textures/common/caulk"))
	var hint := d.add(Q3MapDoc.box_brush(Vector3(200, -64, 0), Vector3(264, 64, 64), "textures/common/hint"))
	var wall := d.add(Q3MapDoc.box_brush(Vector3(-300, -64, 0), Vector3(-236, 64, 64), "textures/gothic_block/blocks15"))
	var curve := d.add(Q3PatchFactory.make("bevel", Vector3(300, 200, 0), 64.0, 64.0, "textures/gothic_block/blocks15"))
	var spawn := d.add(Q3MapEntities.point("info_player_deathmatch", Vector3(0, -200, 24)))
	ed.mesher.flush()

	# ---- view filters
	ed.camera.set_pose(Q3MapEditor.to_godot(Vector3(0, 0, 400)), 0, -89)
	var at := ed.screen_of(Vector3(0, 0, 64))
	var before: Dictionary = ed.pick(at)
	ed.execute("view.filter.caulk")
	var after: Dictionary = ed.pick(at)
	ed.execute("select.all")
	var all_ids := ed.selected_ids()
	check("Alt+1 hides caulk: not drawn, not picked (the pick goes to the floor), not in select all; the status bar says so",
			before.get("id") == caulk and after.get("id") == floor_id and not all_ids.has(caulk) and all_ids.has(wall)
			and ed.ui._flags_label.visible and ed.ui._flags_label.text.contains("caulk"),
			"pick %s -> %s, flags '%s'" % [before.get("id"), after.get("id"), ed.ui._flags_label.text])
	ed.execute("view.filter.tools")
	ed.execute("view.filter.curves")
	ed.execute("view.filter.entities")
	ed.execute("select.all")
	var left := ed.selected_ids()
	ed.execute("view.filter.none")
	ed.execute("select.all")
	check("tool brushes, curves and entities filter too; Alt+0 shows everything again",
			not left.has(hint) and not left.has(curve) and not left.has(spawn) and left.has(wall) and ed.ents.visible
			and ed.selected_ids().size() == d.objects.size() and ed.filters.is_empty() and not ed.ui._flags_label.visible,
			"%d left while filtered, %d after" % [left.size(), ed.selected_ids().size()])
	ed.clear_selection()
	check("the status bar counts the map by kind", ed.ui._sel_label.text == "4 brushes · 1 curve · 1 entity", ed.ui._sel_label.text)

	# ---- outliner
	ed.ui.side.set_tab("objects")
	var ol: Q3EditorOutliner = ed.ui.side.outliner
	ed.select([wall, curve])
	d.checkpoint("Group")
	var gid := ed._new_group()
	for id in [wall, curve]:
		var o := d.edit(id)
		o["group"] = gid
		d.put(o)
	ol.refresh()
	var rows := ol._tree.get_root().get_children()
	var group_row: TreeItem = rows[0]
	check("Objects tab: the group first, folded, with its members; then each object, the selection marked",
			rows.size() == d.objects.size() - 1 and group_row.get_metadata(0) is Dictionary and group_row.get_child_count() == 2
			and ol._head.text.begins_with("%d objects · 1 group ·" % d.objects.size()),
			"%d rows, head '%s'" % [rows.size(), ol._head.text])
	ol.set_flag([caulk], "show", false)
	var hidden_ok := d.hidden.has(caulk)
	ol.set_flag([wall, curve], "lock", true)
	var locked_ok := d.locked.has(wall) and d.locked.has(curve) and not ed.selection.has(wall)
	ed.execute("edit.undo")
	ed.execute("edit.undo")
	check("Show and Lock boxes hide and lock (a group row does all its members), each one undo step",
			hidden_ok and locked_ok and d.hidden.is_empty() and d.locked.is_empty())
	ol._filter.text = "hint"
	ol._filter.text_changed.emit("hint")
	ol.refresh()
	var filtered_rows := ol._tree.get_root().get_child_count()
	ol._filter.text = ""
	ol._filter.text_changed.emit("")
	var named := d.edit(spawn)
	named["name"] = "red base"
	d.put(named)
	ol.refresh()
	var found := false
	for it in ol._tree.get_root().get_children():
		if it.get_metadata(0) is int and int(it.get_metadata(0)) == spawn and it.get_text(0).contains("red base"):
			found = true
	check("the filter narrows the rows; a named object shows its name (and saves with it)",
			filtered_rows == 1 and found and Q3MapDoc.new().from_data(JSON.parse_string(JSON.stringify(d.to_data()))) == "",
			"%d rows for 'hint'" % filtered_rows)

	# ---- history
	for k in 2:
		d.checkpoint("Add pillar %d" % k)
		d.add(Q3MapDoc.box_brush(Vector3(400 + k * 64, 0, 0), Vector3(432 + k * 64, 32, 128)))
	ed.ui.side.set_tab("history")
	var hp: Q3EditorHistory = ed.ui.side.history
	hp.refresh()
	var steps: int = d.history()["done"].size()
	var listed := hp._list.item_count
	var last_label := hp._list.get_item_text(listed - 1)
	var count := d.objects.size()
	hp.go_to(0)
	var at_start := d.objects.size()
	hp.refresh()
	var redo_rows: int = d.history()["undone"].size()
	hp.go_to(steps)
	check("History tab: Start plus every step, the current one marked; clicking Start undoes them all at once, the last redoes them",
			listed == steps + 1 and last_label.begins_with("▶") and last_label.contains("Add pillar 1") and at_start == count - 2
			and redo_rows == steps and d.objects.size() == count,
			"%d steps, last '%s', %d objects at Start, %d after" % [steps, last_label, at_start, d.objects.size()])
	ed.ui.browser.set_tab("surface")

	# ---- files: recent maps, delete, publish's checks
	ed.save_map("smoke_recent_a")
	ed.save_map("smoke_recent_b")
	var recent := Q3MapEditor.recent_maps()
	var gone := ed.delete_map(Q3MapEditor.map_path("smoke_recent_a"))
	check("recent maps, newest first; deleting a map takes its file and its place in the list (the open map stays)",
			recent.size() >= 2 and String(recent[0]).contains("smoke_recent_b") and String(recent[1]).contains("smoke_recent_a")
			and gone == OK and not FileAccess.file_exists(Q3MapEditor.map_path("smoke_recent_a"))
			and not Q3MapEditor.recent_maps().any(func(p): return String(p).contains("smoke_recent_a")),
			str(recent.slice(0, 2)))
	ed.execute("file.open")
	var open_ok := ed.ui.modal_open()
	ed.ui.close_modal()
	d.remove([spawn])
	ed.execute("file.publish")
	var refused := ed.ui.modal_open() and not ed.compiler.busy
	ed.ui.close_modal()
	check("the Open dialog lists the maps; Publish refuses a map without a spawn point", open_ok and refused)
	ed.doc.dirty = false


## The checks from OZMIUM's smoke list (MapEditorSmokeTest.cs) that the sections above didn't cover
## yet and that apply here (its shadow budget, damage numbers and spawn choice are Unity-side; id's
## game code does those in this project).
func _e8_checks() -> void:
	ed.doc.dirty = false
	ed.new_map()
	var d := ed.doc
	var box := Q3MapDoc.box_brush(Vector3(0, 0, 0), Vector3(64, 32, 48))

	# ---- turns land exactly; mirrors keep faces pointing out
	var by_parts := Q3MapDoc.transform_brush(Q3MapDoc.transform_brush(box, Q3MapDoc.yaw_rotation(30, Vector3.ZERO)), Q3MapDoc.yaw_rotation(60, Vector3.ZERO))
	var at_once := Q3MapDoc.transform_brush(box, Q3MapDoc.yaw_rotation(90, Vector3.ZERO))
	var worst := 0.0
	var a: PackedFloat64Array = Q3MapGeo.brush_bounds(by_parts["planes"])
	var b: PackedFloat64Array = Q3MapGeo.brush_bounds(at_once["planes"])
	for i in 6:
		worst = maxf(worst, absf(a[i] - b[i]))
	check("30 degrees then 60 lands where 90 does", worst < 1e-6, String.num_scientific(worst))
	var mirrored := Q3MapDoc.transform_brush(box, Q3MapDoc.mirror(0, Vector3(32, 0, 0)))
	var mid := Vector3(32, 16, 24)
	var outward := true
	var ws: Array = Q3MapGeo.brush_windings(mirrored["planes"])
	for i in mirrored["faces"].size():
		var w: PackedFloat64Array = ws[i]
		var c := Vector3.ZERO
		for k in w.size() / 3:
			c += Vector3(w[k * 3], w[k * 3 + 1], w[k * 3 + 2]) / (w.size() / 3)
		var pl := Q3MapDoc.plane_of(mirrored, i)
		if Vector3(pl[0], pl[1], pl[2]).dot(c - mid) <= 0.0:
			outward = false
	check("a mirrored brush is still a solid with every face pointing out", Q3MapGeo.brush_valid(mirrored["planes"]) and outward)

	# ---- subtract with nothing selected; isolate gives back what was hidden before
	var b1 := d.add(box)
	var b2 := d.add(Q3MapDoc.box_brush(Vector3(100, 0, 0), Vector3(164, 32, 48)))
	var b3 := d.add(Q3MapDoc.box_brush(Vector3(200, 0, 0), Vector3(264, 32, 48)))
	ed.clear_selection()
	var steps_before: int = d.history()["done"].size()
	ed.execute("csg.subtract")
	check("CSG subtract with nothing selected does nothing (no undo step either)",
			d.ids_of("brush").size() == 4 and d.history()["done"].size() == steps_before)
	d.set_hidden([b3], true)
	ed.select([b1])
	ed.execute("view.isolate")
	var isolated := d.hidden.has(b2) and not d.hidden.has(b1)
	ed.execute("view.isolate")
	check("Isolate hides all but the selection; pressing it again gives back the hidden set from before",
			isolated and d.hidden.has(b3) and not d.hidden.has(b2), str(d.hidden.keys()))
	d.set_hidden([b3], false)

	# ---- groups: a paste is a new group under the same name; names, groups and locks save
	d.checkpoint("Group")
	var gid := ed._new_group()
	d.groups[gid]["name"] = "Pillars"
	for id in [b1, b2]:
		var o := d.edit(id)
		o["group"] = gid
		d.put(o)
	ed.select([b1, b2])
	ed.execute("edit.copy")
	var pasted := ed.place_copies(ed.clipboard, Vector3(0, 300, 0), "Paste")
	var pg := int(d.get_obj(pasted[0]).get("group", 0)) if pasted.size() else 0
	check("a pasted group is a new group of its own, under the same name",
			pasted.size() == 2 and pg != 0 and pg != gid and d.groups.has(pg) and d.groups[pg]["name"] == "Pillars"
			and int(d.get_obj(pasted[1])["group"]) == pg, "group %d of %d" % [pg, gid])
	var named := d.edit(b3)
	named["name"] = "Back wall"
	d.put(named)
	d.set_locked([b3], true)
	ed.save_map("smoke_names")
	ed.new_map()
	ed.open_map(Q3MapEditor.map_path("smoke_names"))
	var back := ed.doc
	check("names, groups (with their names) and locks survive save and open",
			back.get_obj(b3).get("name", "") == "Back wall" and back.locked.has(b3) and back.groups.has(gid)
			and back.groups[gid]["name"] == "Pillars" and int(back.get_obj(b1)["group"]) == gid)

	# ---- History: two steps back leaves them as redo; one forward redoes the first only
	var h: Dictionary = back.history()
	back.checkpoint("A")
	back.add(Q3MapDoc.box_brush(Vector3(0, 0, 200), Vector3(16, 16, 216)))
	back.checkpoint("B")
	back.add(Q3MapDoc.box_brush(Vector3(0, 0, 300), Vector3(16, 16, 316)))
	var n := back.objects.size()
	back.step(-2)
	var after_back := back.objects.size()
	var redo_labels: Array = back.history()["undone"]
	back.step(1)
	check("History: two steps back leaves both to redo; one forward redoes the first only",
			after_back == n - 2 and redo_labels == ["A", "B"] and back.objects.size() == n - 1 and back.history()["undone"] == ["B"],
			"%s, redo %s" % [h["done"].size(), redo_labels])

	# ---- tint: one material per tint, and it saves
	var m1 := Q3MapMaterials.material("textures/base_wall/concrete", Color(0.5, 0.8, 1.0))
	var m2 := Q3MapMaterials.material("textures/base_wall/concrete", Color(0.5, 0.8, 1.0))
	var tinted := back.edit(b2)
	tinted["faces"][0]["color"] = Color(0.5, 0.8, 1.0)
	back.put(tinted)
	var again := Q3MapDoc.new()
	var err := again.from_data(JSON.parse_string(JSON.stringify(back.to_data())))
	check("the same tint reuses one material, and tints survive the file",
			is_same(m1, m2) and m1 != Q3MapMaterials.material("textures/base_wall/concrete")
			and err == "" and _close(again.get_obj(b2)["faces"][0].get("color"), Color(0.5, 0.8, 1.0)))
	ed.doc.dirty = false


## Starts an import without waiting; the signal fires when it's done.
## The layout pass (2026-10-24): the 2D views, creating in one, a second click stepping to the object
## behind, the browser's sections, the inspector's typed fields and folding, the docks.
func _ux_checks() -> void:
	ed.new_map()
	await get_tree().process_frame
	var d := ed.doc
	var floor_id: int = _brushes()[0]
	var roof := d.add(Q3MapDoc.box_brush(Vector3(-256, -256, 256), Vector3(256, 256, 272)))
	ed.clear_selection()
	ed.set_tool("select")
	ed.execute("view.top")
	await get_tree().process_frame
	var top_ok: bool = ed.view == "top" and ed.camera.projection == Camera3D.PROJECTION_ORTHOGONAL and ed.wire.visible and not ed.mesher.visible 			and ed.wire.mesh != null
	var at := ed.screen_of(Vector3(64, 64, 300))
	ed.tool.click(at, 0, false)
	var first := ed.selected_ids()
	ed.tool.click(at, 0, false)
	var second := ed.selected_ids()
	ed.tool.click(at, 0, true)
	var double := ed.selected_ids()
	check("top view: orthographic, the map as lines; a click takes the roof, a second on the same spot the floor under it; a double-click takes the nearest alone",
			top_ok and first == [roof] and second == [floor_id] and double == [roof], "%s, %s then %s then %s" % [top_ok, first, second, double])
	# creating in a 2D view: a footprint drag makes the brush at once, as deep as the last one
	ed.clear_selection()
	ed.last_height = 64
	ed.set_tool("create")
	var ct: Q3CreateTool = ed.tool
	var a := ed.screen_of(Vector3(0, 0, 0))
	var b := ed.screen_of(Vector3(128, 64, 0))
	ct.press(a, 0, false)
	ct.drag_start(a, 0)
	ct.drag(b, 0)
	ct.drag_end(b, 0)
	var made: Array = _brushes().filter(func(i): return i != floor_id and i != roof)
	check("top view: a footprint drag creates the brush on the view's plane, the last height deep",
			made.size() == 1 and _bounds(made[0]) == PackedFloat64Array([0, 0, 0, 128, 64, 64]), _box(made[0]) if made.size() else "none")
	ed.set_tool("select")
	ed.execute("view.front")
	var front_ok: bool = ed.view == "front" and ed.view_axis() == 1 and ed.camera.ortho == "front"
	ed.execute("view.front")
	check("front view, and its key again goes back to 3D with the solid map",
			front_ok and ed.view == "3d" and ed.camera.projection == Camera3D.PROJECTION_PERSPECTIVE and ed.mesher.visible and not ed.wire.visible)

	# the browser: recents first, a section per folder, the models' shaders last; show_material finds a sky
	var br: Q3EditorBrowser = ed.ui.browser
	Q3MapMaterials.add_recent("textures/base_wall/concrete")
	br.set_tab("surface")
	var heads := []
	for c in br._list.get_children():
		if c is Label and not c.is_queued_for_deletion():
			heads.append(c.text)
	br.show_material("textures/skies/hellsky")
	check("browser: favorites and recents first, then folders, the models' own shaders last; the strip's swatch finds a sky in Skies",
			heads.size() > 3 and heads.find("RECENT") in [0, 1] and heads.find("RECENT") < heads.find("BASE_WALL") and heads.find("MODELS") > heads.find("BASE_WALL")
			and br.tab == "sky" and br.folder == "skies", "%s, %s %s" % [heads.slice(0, 4), br.tab, br.folder])
	br.set_tab("surface")

	# the inspector: a button's typed fields (speed at id's default, its target from the map's names), a
	# door's flags by name, the raw keys folded
	var door_brush := d.add(Q3MapDoc.box_brush(Vector3(300, 0, 0), Vector3(316, 64, 96)))
	ed.select([door_brush])
	var door := ed.make_brush_entity("func_door")
	ed.set_keys([door], {"targetname": "gate"})
	var button_brush := d.add(Q3MapDoc.box_brush(Vector3(300, 128, 32), Vector3(304, 144, 48)))
	ed.select([button_brush])
	var button := ed.make_brush_entity("func_button")
	ed.select(ed.group_of(button))
	await get_tree().process_frame
	var ins: Q3EditorInspector = ed.ui.inspector
	var speed: SpinBox = ins._fields.get("num:speed")
	var tgt: OptionButton = ins._fields.get("target:target")
	var speed_ok := speed != null and speed.value == 40.0 and _k(button, "speed") == ""
	var gate := -1
	if tgt:
		for i in tgt.item_count:
			if tgt.get_item_text(i) == "gate":
				gate = i
		if gate >= 0:
			tgt.select(gate)
			tgt.item_selected.emit(gate)
	var raw_folded := false
	for c in ins._root.get_children():
		if c is Button and (c as Button).text.ends_with("ALL KEYS"):
			raw_folded = (c as Button).text.begins_with("▸")
	ed.select(ed.group_of(door))
	await get_tree().process_frame
	var open: CheckBox = ins._fields.get("flag:1")
	if open:
		open.button_pressed = true
	check("inspector: a button's speed shows id's 40 unset, its target picks 'gate' from the map; a door's Start open sets spawnflags; raw keys folded",
			speed_ok and gate >= 0 and _k(button, "target") == "gate" and open != null and open.text == "Start open" and _k(door, "spawnflags") == "1" and raw_folded,
			"speed %s, gate %d, target '%s', spawnflags '%s', folded %s" % [speed_ok, gate, _k(button, "target"), _k(door, "spawnflags"), raw_folded])

	# the docks: Objects folds to its tabs and back, Tab hides both docks and leaves the tool strip, the
	# layout saves
	var side = ed.ui.side
	side.set_folded(true)
	var folded_h: float = side.custom_minimum_size.y
	side.set_folded(false)
	ed.ui.toggle_panels()
	var hidden := not ed.ui.panels_shown() and ed.ui.strip.visible
	ed.ui.toggle_panels()
	check("docks: Objects folds away and comes back at its height; Tab hides the docks but not the tool strip; the layout is saved",
			folded_h == 0.0 and side.custom_minimum_size.y == side.height and hidden and ed.ui.panels_shown()
			and FileAccess.file_exists(Q3EditorUi.layout_path))


func _import_signal(map: String) -> Signal:
	var s := Signal(self, "import_finished")
	(func():
		await ed.import_map("bsp", map)
		import_finished.emit()).call()
	return s


static func _close(a: Variant, b: Color) -> bool:
	return a is Color and absf(a.r - b.r) < 0.01 and absf(a.g - b.g) < 0.01 and absf(a.b - b.b) < 0.01


func _corners(id: int) -> int:
	return ed.object_points(id).size() / 3


func _wipe() -> void:
	var g := ProjectSettings.globalize_path(SCRATCH)
	for sub in ["maps", "prefabs", ""]:
		var d := DirAccess.open(g.path_join(sub))
		if d:
			for f in d.get_files():
				d.remove(f)
	DirAccess.remove_absolute(g.path_join("maps"))
	DirAccess.remove_absolute(g.path_join("prefabs"))
