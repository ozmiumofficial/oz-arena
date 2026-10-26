extends Node
## Race in the real scenes (RACE_PLAN.md D3, D4), windowed: the race_test corridor as a course in the
## Maps folder (a pk3 with its .defi), picked up by Q3MapPacks, started as the Race mode through
## Session's setup, and run in the Gameplay scene with held keys: the timer HUD, a record and its
## file, then a second run against the ghost of the first (held back so the ghost pulls ahead), and
## R restarting.
##   godot --path . res://tests/race_play_test.tscn -- out=C:/temp/race
## Saves race_idle / race_checkpoint / race_finish / race_ghost .png into out= when given. Prints one
## line per check and "RACE PLAY n/m passed"; exit code 1 on any failure.

const NAME := "ed_raceplay"
const SCRATCH := "user://race_play_test"

var passed := 0
var failed := 0
var out_dir := ""


func check(name: String, ok: bool, detail := "") -> void:
	if ok:
		passed += 1
	else:
		failed += 1
	print("  %s  race play: %s%s" % ["PASS" if ok else "FAIL", name, ("  (" + detail + ")") if detail != "" else ""])


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			out_dir = a.get_slice("=", 1)
			DirAccess.make_dir_recursive_absolute(out_dir)
	_run.call_deferred()


func _shot(name: String) -> void:
	if out_dir == "":
		return
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(out_dir.path_join(name + ".png"))


func _wait(cond: Callable, seconds := 20.0) -> bool:
	var t0 := Time.get_ticks_msec()
	while Time.get_ticks_msec() - t0 < seconds * 1000.0:
		if cond.call():
			return true
		await get_tree().process_frame
	return cond.call()


func _press(action: String, on: bool) -> void:
	if on:
		Input.action_press(action)
	else:
		Input.action_release(action)


func _tap(action: String) -> void:
	var ev := InputEventAction.new()
	ev.action = action
	ev.pressed = true
	Input.parse_input_event(ev)
	await get_tree().process_frame
	ev = InputEventAction.new()
	ev.action = action
	ev.pressed = false
	Input.parse_input_event(ev)


func _run() -> void:
	var t0 := Time.get_ticks_msec()
	# the course: compiled, then packed with its .defi into a Maps folder of its own
	var d: Q3MapDoc = (load("res://tests/race_test.gd") as Script).call("corridor")
	d.info["name"] = "raceplay"
	var c := Q3MapCompiler.new()
	add_child(c)
	if Q3MapCompiler.ensure_kit() != "":
		check("kit", false)
		_finish(t0)
		return
	c.compile(NAME, Q3MapExport.to_map(d)["text"], ["bsp", "light"])
	var r: Dictionary = await c.finished
	check("q3map compiles the course", r["ok"], String(r["error"]))
	if not r["ok"]:
		_finish(t0)
		return
	var maps_dir := ProjectSettings.globalize_path(SCRATCH).path_join("Maps")
	DirAccess.make_dir_recursive_absolute(maps_dir)
	var zp := ZIPPacker.new()
	zp.open(maps_dir.path_join("raceplay.pk3"))
	zp.start_file("maps/%s.bsp" % NAME)
	zp.write_file(FileAccess.get_file_as_bytes(r["bsp"]))
	zp.close_file()
	zp.start_file("scripts/%s.defi" % NAME)
	zp.write_file(('{\nmap "%s"\nlongname "Race test corridor"\nstyle "run"\ncpm "0"\nvq3 "1"\nauthor "the tests"\n}\n' % NAME).to_utf8_buffer())
	zp.close_file()
	zp.start_file("scripts/cpmonly.defi")
	zp.write_file('{\nmap "cpmonly"\nstyle "run"\ncpm "1"\nvq3 "0"\n}\n'.to_utf8_buffer())
	zp.close_file()
	zp.close()
	Q3MapPacks.dir_override = maps_dir
	Q3RaceRecords.path = ProjectSettings.globalize_path(SCRATCH).path_join("records.json")
	Q3RaceRecords.ghost_dir = ProjectSettings.globalize_path(SCRATCH).path_join("ghosts")
	DirAccess.remove_absolute(Q3RaceRecords.path)
	Session.refresh_custom_maps()
	var info := Session.map_info(NAME)
	check("the Maps folder lists the course as a race map from its .defi",
			info.get("types", []) == ["race"] and info.get("race", {}).get("style", "") == "run"
			and info.get("race", {}).get("author", "") == "the tests" and info.get("name", "") == "Race test corridor", str(info))
	check("a CPM-only map is left out", Session.map_info("cpmonly").is_empty())
	check("Race plays it, and no other mode does", Session.maps_for("race").any(func(m): return m["map"] == NAME)
			and not Session.maps_for("ffa").any(func(m): return m["map"] == NAME))

	var setup := Session.new_setup("race", NAME)
	var cv := setup.game_cvars()
	check("the setup validates, with Classic movement and the game's race rules, no limits",
			setup.validate().is_empty() and cv.get("g_ruleset", "") == "classic" and int(cv.get("g_race", 0)) == 1
			and int(cv.get("fraglimit", -1)) == 0 and int(cv.get("timelimit", -1)) == 0, str(setup.validate()) + " " + str(cv))
	var ffa := Session.new_setup("ffa", "q3dm17").game_cvars()
	check("other modes don't set the race cvars", not ffa.has("g_race") and not ffa.has("g_ruleset"))

	Session.setup = setup
	Session.replay = {}
	Session.join = {}
	var gp: Q3Gameplay = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	add_child(gp)
	gp.test_input = true
	var ok := await _wait(func(): return gp.me >= 0 and gp.race_active and not gp.feed.player_state(gp.me).is_empty())
	check("Gameplay starts the race: timer HUD on, the run loaded from the game", ok and gp.hud.race.visible and not gp.hud.status.visible)
	if not ok:
		_finish(t0)
		return
	await _wait(func(): return gp.race_run.checkpoints_total == 2, 5.0)
	check("the game tells the HUD the course has two checkpoints", gp.race_run.checkpoints_total == 2, str(gp.race_run.checkpoints_total))
	for i in 30:
		await get_tree().process_frame
	await _shot("race_idle")

	# run one: hold forward the whole way
	_press("q3_forward", true)
	var cp := await _wait(func(): return gp.race_run.splits.size() >= 1, 15.0)
	check("running through the start line starts the timer, the first checkpoint reports", cp and gp.race_run.state == Q3RaceRun.State.RUNNING)
	await get_tree().create_timer(0.35).timeout
	await _shot("race_checkpoint")
	var fin := await _wait(func(): return gp.race_run.state == Q3RaceRun.State.FINISHED, 15.0)
	_press("q3_forward", false)
	check("the finish line stops it, a first run is a record", fin and gp.race_run.last_record, "%d ms" % gp.race_run.last_ms)
	check("the time is the walk (4 to 8 s: the first seconds of a windowed run hitch)", gp.race_run.last_ms > 3500 and gp.race_run.last_ms < 8000, "%d ms" % gp.race_run.last_ms)
	await get_tree().create_timer(0.4).timeout
	await _shot("race_finish")
	var rec := Q3RaceRecords.best(NAME)
	check("the record is kept, with its splits", int(rec.get("ms", 0)) == gp.race_run.last_ms and (rec.get("splits", []) as Array).size() == 2, str(rec))
	check("the ghost of the run is saved", FileAccess.file_exists(Q3RaceRecords.ghost_path(NAME)))
	var first_ms: int = gp.race_run.last_ms
	check("the ghost file loads back", Q3RaceGhost.load_for(NAME) != null and Q3RaceGhost.load_for(NAME).ms == first_ms)

	# R restarts, and the best run is the ghost
	await _tap("q3_race_restart")
	var back := await _wait(func(): return gp.race_run.state == Q3RaceRun.State.IDLE and (gp.feed.player_state(gp.me)["origin"] as Vector3).x < 100.0, 6.0)
	check("R restarts: the run is idle again at the spawn", back)
	check("your best run waits as the ghost", gp.race_ghost != null)
	# run two: held back for three seconds so the ghost pulls ahead and the run is slower of the camera
	_press("q3_forward", true)
	await _wait(func(): return gp.race_run.state == Q3RaceRun.State.RUNNING, 8.0)
	await get_tree().create_timer(0.8).timeout
	_press("q3_forward", false)
	await get_tree().create_timer(3.0).timeout
	check("the ghost runs while you hold back", gp.race_ghost != null and gp.race_ghost.visible)
	var gx := gp.race_ghost.global_position if gp.race_ghost else Vector3.ZERO
	var px: Vector3 = gp.cam.global_position
	await _shot("race_ghost")
	check("the ghost is ahead of you on the course", gx.distance_to(px) > 1.0 and (gp.race_ghost.global_position - px).dot(-gp.cam.global_transform.basis.z) > 0.0,
			"ghost %s, you %s" % [gx, px])
	_press("q3_forward", true)
	fin = await _wait(func(): return gp.race_run.state == Q3RaceRun.State.FINISHED, 15.0)
	_press("q3_forward", false)
	check("the slower second run finishes, and isn't a record", fin and not gp.race_run.last_record and gp.race_run.last_ms > first_ms,
			"%d ms against %d" % [gp.race_run.last_ms, first_ms])
	check("the best on file stays the first run's", int(Q3RaceRecords.best(NAME)["ms"]) == first_ms)
	var runs: Array = Q3RaceRecords.board().get(NAME, [])
	check("the leaderboard keeps both finishes, fastest first", runs.size() == 2 and int(runs[0]["ms"]) == first_ms and int(runs[1]["ms"]) == gp.race_run.last_ms,
			"%s" % [runs])
	Session.race_queue = ["a_next_course"]
	check("a playlist's queue lives on Session", Session.race_queue.size() == 1)
	Session.race_queue = []
	gp.queue_free()
	Q3MapPacks.dir_override = ""
	_finish(t0)


func _finish(t0: int) -> void:
	print("RACE PLAY %d/%d passed (%.1f s)" % [passed, passed + failed, (Time.get_ticks_msec() - t0) / 1000.0])
	get_tree().quit(0 if failed == 0 else 1)
