extends Node
## Map editor play-test, headless (MAP_EDITOR_PLAN.md E5): the real scenes, as a player goes
## through them. The Map editor with the compile test's arena -> F5 (q3map, bspc) -> Gameplay
## drawing the editor's map, with the player and bots in it -> F6 -> the editor again, with the same
## document, undo history, selection and view.
##   godot --headless --path . res://tests/map_playtest_test.tscn
##   godot --path . res://tests/map_playtest_test.tscn -- shots=C:/temp/pt   (windowed: saves pictures
##   of the play-test, with the F7 markers, and of the editor after F6)
## The test runs on a node of its own beside the scenes, so it outlives the scene changes. Prints
## "MAP PLAYTEST n/m passed"; exit code 1 on any failure.

const SCRATCH := "user://map_editor_test"

var passed := 0
var failed := 0


func check(name: String, ok: bool, detail := "") -> void:
	if ok:
		passed += 1
	else:
		failed += 1
	print("  %s  map playtest: %s%s" % ["PASS" if ok else "FAIL", name, ("  (" + detail + ")") if detail != "" else ""])


func _ready() -> void:
	_start.call_deferred()


func _start() -> void:
	# the scene changes free the current scene, so the test runs on a second node beside it
	if not has_meta("runner"):
		var runner: Node = (get_script() as Script).new()
		runner.set_meta("runner", true)
		get_tree().root.add_child(runner)
		return
	_run()


## Frames until cond() holds, or a limit; returns whether it held.
func _wait(cond: Callable, frames := 1200) -> bool:
	for i in frames:
		if cond.call():
			return true
		await get_tree().process_frame
	return cond.call()


func _run() -> void:
	var t0 := Time.get_ticks_msec()
	Q3MapEditor.maps_dir = SCRATCH.path_join("maps")
	Q3MapEditor.recent = SCRATCH.path_join("recent.json")
	var bots_before := Q3MapEditor.play_bots
	Q3MapEditor.play_bots = 2
	Session.open_map_editor()
	var ok := await _wait(func(): return get_tree().current_scene is Q3MapEditor)
	var ed := get_tree().current_scene as Q3MapEditor
	check("the Map editor opens", ok)
	if not ok:
		_finish(t0, bots_before)
		return

	var arena: Dictionary = (load("res://tests/map_compile_test.gd") as Script).call("arena", true).to_data()
	var d := Q3MapDoc.new()
	d.from_data(arena)
	d.info["name"] = "playtest"
	ed._set_doc(d, "")
	ed.doc.checkpoint("Test edit")
	var block := ed.doc.add(Q3MapDoc.box_brush(Vector3(-96, -96, 0), Vector3(-64, -64, 32), "textures/gothic_block/blocks15"))
	ed.select([block])
	# the camera inside the room, 200 units up, looking along id +X (Godot yaw 180): the player starts on
	# the floor under it, facing that way
	var cam_id := Vector3(64, -96, 200)
	ed.camera.set_pose(Q3MapEditor.to_godot(cam_id), 180, -20)
	var cam_pose := ed.camera.position
	var doc_before := ed.doc
	ed.execute("map.playtest")
	ok = await _wait(func(): return get_tree().current_scene != null and get_tree().current_scene.name == "Gameplay", 1800)
	var gp := get_tree().current_scene
	check("F5 compiles (q3map and bspc) and starts Gameplay", ok,
			"" if ok else "%s\n%s" % [String(ed.last_compile.get("error", "")) if is_instance_valid(ed) else "", String(ed.last_compile.get("log", "")).right(800) if is_instance_valid(ed) else ""])
	if not ok:
		_finish(t0, bots_before)
		return
	for i in 240:
		await get_tree().process_frame
	var map_scene: Variant = gp.get("map_scene")
	var me: int = gp.get("me")
	var ps := Q3Server.player_state(me) if me >= 0 else {}
	var o: Vector3 = ps.get("origin", Vector3.INF)
	var bots := 0
	for cl in 16:
		if cl != me and Q3Server.client_state(cl) == 4:
			bots += 1
	check("Gameplay draws the editor's map (Q3PlayMap), the player is in it, with 2 bots",
			map_scene is Q3PlayMap and absf(o.x) < 384 and absf(o.y) < 384 and o.z > 0 and o.z < 128 and bots == 2,
			"map %s, origin %s, %d bots" % [map_scene, o, bots])
	var yaw := fposmod(float(ps.get("viewangles", Vector3.ZERO).y), 360.0)
	check("the play-test starts on the floor under the editor's camera, facing its way",
			Vector2(o.x, o.y).distance_to(Vector2(cam_id.x, cam_id.y)) < 48.0 and o.z < 40.0 and (yaw < 10.0 or yaw > 350.0),
			"origin %s, yaw %.1f" % [o, yaw])
	await _shot("playtest")
	gp.call("_unhandled_input", _key(KEY_F7))
	for i in 3:
		await get_tree().process_frame
	await _shot("playtest_markers")
	check("a play-test isn't the menu's last match", not FileAccess.get_file_as_string(Session.LAST_SETUP).contains("ed_playtest"))

	gp.call("_unhandled_input", _key(KEY_F6))
	ok = await _wait(func(): return get_tree().current_scene is Q3MapEditor)
	ed = get_tree().current_scene as Q3MapEditor
	await get_tree().process_frame
	check("F6 goes back to the Map editor", ok)
	for i in 10:
		await get_tree().process_frame
	await _shot("editor_after")
	if ok:
		check("the editor comes back as it was: the same document with its undo history, the selection and the view",
				is_same(ed.doc, doc_before) and ed.doc.can_undo() and ed.doc.undo_label() == "Test edit"
				and ed.selected_ids() == [block] and ed.camera.position.distance_to(cam_pose) < 0.01
				and Session.playtest.is_empty(), "undo %s, selection %s" % [ed.doc.undo_label(), ed.selected_ids()])
	# F5 again on the unchanged map: no q3map run, straight into the match
	var compiled_before: Dictionary = ed.last_compile
	ed.execute("map.playtest")
	var fast := not ed.compiler.busy
	ok = await _wait(func(): return get_tree().current_scene != null and get_tree().current_scene.name == "Gameplay", 1800)
	check("F5 on an unchanged map skips the compile and plays the last one", ok and fast, "busy at once %s" % (not fast))
	if ok:
		for i in 30:
			await get_tree().process_frame
		get_tree().current_scene.call("_unhandled_input", _key(KEY_F6))
		await _wait(func(): return get_tree().current_scene is Q3MapEditor)
		for i in 5:
			await get_tree().process_frame
	_finish(t0, bots_before)


func _key(code: int) -> InputEventKey:
	var k := InputEventKey.new()
	k.keycode = code
	k.pressed = true
	return k


## shots=<dir>: the window as it is now, to <dir>/<name>.png (windowed runs only).
func _shot(name: String) -> void:
	var dir := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("shots="):
			dir = a.substr(6)
	if dir == "" or DisplayServer.get_name() == "headless":
		return
	await RenderingServer.frame_post_draw
	DirAccess.make_dir_recursive_absolute(dir)
	get_viewport().get_texture().get_image().save_png(dir.path_join(name + ".png"))


func _finish(t0: int, bots_before: int) -> void:
	Q3MapEditor.play_bots = bots_before
	for ext in [".bsp", ".aas"]:
		var p := Q3MapCompiler.output_dir.path_join("ed_playtest" + ext)
		if FileAccess.file_exists(p):
			DirAccess.remove_absolute(ProjectSettings.globalize_path(p))
	print("MAP PLAYTEST %d/%d passed (%.1f s)" % [passed, passed + failed, (Time.get_ticks_msec() - t0) / 1000.0])
	if Q3Server.running():
		Q3Server.exec_now("killserver")
	get_tree().quit(1 if failed else 0)
