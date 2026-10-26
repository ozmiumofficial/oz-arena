extends Node
## The map editor's race elements (RACE_PLAN.md D5), headless: Defrag's classes in the Entities tab and
## the inspector, the start line, finish line and checkpoint tools (a click on each side of the course),
## the course check, the course settings, publish and export (a course is Race, its .defi), the play-test
## as a run, and a course built with the tools, compiled with q3map and run in the game.
##   godot --headless --path . res://tests/race_editor_test.tscn
## Prints one line per check and "RACE EDITOR n/m passed"; exit code 1 on any failure.

const SCRATCH := "user://race_editor_test"
const NAME := "ed_raceeditor"

var passed := 0
var failed := 0
var ed: Q3MapEditor


func check(name: String, ok: bool, detail := "") -> void:
	if ok:
		passed += 1
	else:
		failed += 1
	print("  %s  race editor: %s%s" % ["PASS" if ok else "FAIL", name, ("  (" + detail + ")") if detail != "" else ""])


func _ready() -> void:
	_run.call_deferred()


func _bounds(id: int) -> PackedFloat64Array:
	return Q3MapGeo.brush_bounds(ed.doc.get_obj(id)["planes"])


## A corridor along +X, 1100 long, a spawn at its start facing down it, no race entities yet.
static func hall() -> Q3MapDoc:
	var d := Q3MapDoc.new()
	d.info["name"] = "raceeditor"
	var wall := "textures/gothic_block/blocks15"
	d.add(Q3MapDoc.box_brush(Vector3(-80, -144, -16), Vector3(1100, 144, 0), "textures/base_floor/clangdark"))
	d.add(Q3MapDoc.box_brush(Vector3(-80, -144, 128), Vector3(1100, 144, 144), wall))
	d.add(Q3MapDoc.box_brush(Vector3(-80, -144, 0), Vector3(-64, 144, 128), wall))
	d.add(Q3MapDoc.box_brush(Vector3(1084, -144, 0), Vector3(1100, 144, 128), wall))
	d.add(Q3MapDoc.box_brush(Vector3(-80, -144, 0), Vector3(1100, -128, 128), wall))
	d.add(Q3MapDoc.box_brush(Vector3(-80, 128, 0), Vector3(1100, 144, 128), wall))
	d.add(Q3MapEntities.point("info_player_deathmatch", Vector3(0, 0, 24), {"angle": "0"}))
	d.add(Q3MapEntities.point("light", Vector3(300, 0, 100), {"light": "400"}))
	d.add(Q3MapEntities.point("light", Vector3(800, 0, 100), {"light": "400"}))
	d.worldspawn = {"message": "Editor test course", "ambient": "8"}
	return d


func _run() -> void:
	var t0 := Time.get_ticks_msec()
	Q3MapEditor.maps_dir = SCRATCH.path_join("maps")
	Q3MapEditor.recent = SCRATCH.path_join("recent.json")
	Q3MapMaterials.prefs_path = SCRATCH.path_join("materials.json")
	Q3EditorUi.layout_path = SCRATCH.path_join("layout.json")
	Q3MapMaterials._prefs = {}
	Q3MapPrefabs.dir = SCRATCH.path_join("prefabs")
	ed = load("res://scenes/map_editor/map_editor.tscn").instantiate()
	add_child(ed)
	await get_tree().process_frame

	# ---- the catalog: Defrag's classes, with their keys and flags
	var section := Q3MapEntities.catalog().filter(func(s): return s[0] == "Race (Defrag)")
	var classes: Array = section[0][1] if section.size() else []
	check("the Entities tab has a Race (Defrag) section with all twelve classes", classes.size() == 12 and Q3MapRace.CLASSES.all(func(c): return classes.has(c)),
			str(classes.size()))
	check("every class has its keys in the inspector, and the flagged ones their flags",
			Q3MapRace.CLASSES.all(func(c): return not Q3MapEntities.key_specs(c).is_empty())
			and Q3MapEntities.flags("target_speed").size() == 9 and Q3MapEntities.flags("trigger_push_velocity").size() == 7)
	check("trigger_push_velocity is a brush class, the timers are points", Q3MapEntities.is_brush_class("trigger_push_velocity")
			and not Q3MapEntities.is_brush_class(Q3MapRace.START) and Q3MapEntities.category(Q3MapRace.START) == "race")
	var strip: Array = ed.find_children("*", "Button", true, false).filter(func(b): return String((b as Button).tooltip_text).begins_with("Race "))
	check("the tool strip has the three race tools", strip.size() == 3, str(strip.size()))
	check("the keymap has them on Y, U and C, and the check and settings on Shift+C and Shift+Y",
			Q3EditorKeymap.key_for("tool.startline") == "Y" and Q3EditorKeymap.key_for("tool.finishline") == "U"
			and Q3EditorKeymap.key_for("tool.checkpoint") == "C" and Q3EditorKeymap.key_for("race.check") == "Shift+C"
			and Q3EditorKeymap.key_for("race.settings") == "Shift+Y")

	# ---- the tools
	var d := hall()
	ed._set_doc(d, "")
	await get_tree().process_frame
	check("a map with no race entities has no race problems and isn't a course", Q3MapRace.problems(ed.doc).is_empty() and not Q3MapRace.has_course(ed.doc))
	ed.execute("view.top")
	await get_tree().process_frame
	ed.execute("tool.startline")
	var et: Q3EntityTool = ed.tool
	check("Y arms the start line tool", ed.tool is Q3EntityTool and et.kind == "startline" and et.current_class() == Q3MapRace.START)
	et.click(ed.screen_of(Vector3(128, -120, 0)), 0, false)
	check("the first click waits for the other side", et.busy() and et.first != null)
	et.click(ed.screen_of(Vector3(128, 120, 0)), 0, false)
	var starts := Q3MapRace.ids_of(ed.doc, Q3MapRace.START)
	check("the second click makes the start line: a trigger_multiple volume across the course, linked to a target_startTimer",
			starts.size() == 1 and not et.busy() and Q3MapEntities.owned(ed.doc).size() == 1)
	var trig_id: int = Q3MapEntities.owned(ed.doc).keys()[0] if Q3MapEntities.owned(ed.doc).size() else 0
	var vol: Array = Q3MapEntities.owned(ed.doc).get(trig_id, [])
	var b := _bounds(vol[0]) if vol.size() else PackedFloat64Array([0, 0, 0, 0, 0, 0])
	check("the volume spans the course (to the grid), is 32 thick along it and 128 tall (the roof)",
			b[1] <= -104 and b[4] >= 104 and absf(b[3] - b[0] - 32) < 1 and absf(b[5] - b[2] - 128) < 1, "%s" % [b])
	var trig: Dictionary = ed.doc.get_obj(trig_id)
	var timer: Dictionary = ed.doc.get_obj(starts[0])
	check("they are linked by name, and the target sits inside the volume",
			trig["classname"] == "trigger_multiple" and trig["keys"]["target"] == timer["keys"]["targetname"]
			and Q3MapEntities.origin(timer).x >= b[0] and Q3MapEntities.origin(timer).x <= b[3] and Q3MapEntities.origin(timer).z > 0)
	check("it is one undo step", ed.doc.undo_label() == "Start line")
	ed.execute("edit.undo")
	check("undo takes the whole line away", Q3MapRace.ids_of(ed.doc, Q3MapRace.START).is_empty() and Q3MapEntities.owned(ed.doc).is_empty())
	ed.execute("edit.redo")
	ed.place_race_line("checkpoint", Vector3(500, -120, 0), Vector3(500, 120, 0))
	ed.place_race_line("finishline", Vector3(900, 120, 0), Vector3(900, -120, 0))
	check("checkpoint and finish lines, drawn in either direction, join it: %s" % Q3MapRace.summary(ed.doc),
			Q3MapRace.ids_of(ed.doc, Q3MapRace.CHECKPOINT).size() == 1 and Q3MapRace.ids_of(ed.doc, Q3MapRace.STOP).size() == 1 and Q3MapRace.has_course(ed.doc))
	# a picture of the tools at work, when asked (windowed; -- out=<folder>)
	var out := ""
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("out="):
			out = arg.get_slice("=", 1)
	if out != "" and DisplayServer.get_name() != "headless":
		DirAccess.make_dir_recursive_absolute(out)
		ed.execute("view.top")
		ed.select(Q3MapRace.ids_of(ed.doc, Q3MapRace.START))
		for i in 6:
			await get_tree().process_frame
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png(out.path_join("race_editor_top.png"))
		ed.execute("view.3d")
		ed.camera.set_pose(Q3MapEditor.to_godot(Vector3(-40, 0, 90)), 180.0, -8.0)
		ed.select([Q3MapRace.ids_of(ed.doc, Q3MapRace.STOP)[0]])
		ed.ui.toast("Course OK: " + Q3MapRace.summary(ed.doc))
		for i in 6:
			await get_tree().process_frame
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png(out.path_join("race_editor_3d.png"))
		ed.execute("view.top")
	var names := Q3MapEntities.targetnames(ed.doc)
	check("each line got its own name (start1, cp1, finish1)", names.has("start1") and names.has("cp1") and names.has("finish1"), str(names.keys()))
	check("a good course has no problems, and the publish check agrees", Q3MapRace.problems(ed.doc).is_empty() and Q3MapPublish.problems(ed.doc).is_empty(),
			str(Q3MapRace.problems(ed.doc)))
	check("a course publishes as Race only", Q3MapPublish.types_of(ed.doc) == ["race"])

	# ---- the checks
	var snap: Dictionary = ed.doc.to_data()
	ed.doc.remove([Q3MapRace.ids_of(ed.doc, Q3MapRace.STOP)[0]])
	var probs := Q3MapRace.problems(ed.doc)
	check("without a finish line: an error, and publishing is refused",
			probs.any(func(p): return p["error"] and String(p["text"]).contains("finish line")) and Array(Q3MapPublish.problems(ed.doc)).any(func(p): return String(p).contains("finish line")))
	ed.doc.from_data(snap)
	var cp: int = Q3MapRace.ids_of(ed.doc, Q3MapRace.CHECKPOINT)[0]
	var e: Dictionary = ed.doc.edit(cp)
	e["keys"]["targetname"] = "nobody"
	ed.doc.put(e)
	probs = Q3MapRace.problems(ed.doc)
	check("a checkpoint nothing fires is a note, not an error", probs.size() == 1 and not probs[0]["error"] and probs[0]["ids"] == [cp], str(probs))
	ed.doc.from_data(snap)
	var sp: int = ed.doc.ids_of("entity").filter(func(i): return ed.doc.get_obj(i)["classname"] == "info_player_deathmatch")[0]
	var se: Dictionary = ed.doc.edit(sp)
	se["keys"]["origin"] = "128 0 24"
	ed.doc.put(se)
	probs = Q3MapRace.problems(ed.doc)
	check("a spawn inside the start line is caught (the timer would start at once)", probs.any(func(p): return String(p["text"]).contains("inside the start line")), str(probs))
	ed.doc.from_data(snap)
	var pk: int = ed.doc.add(Q3MapEntities.point("item_health", Vector3(300, 60, 24), {"wait": "-1"}))
	probs = Q3MapRace.problems(ed.doc)
	check("a wait -1 pickup nothing respawns is a note", probs.any(func(p): return p["ids"] == [pk] and String(p["text"]).contains("wait -1")))
	ed.doc.from_data(snap)

	# ---- settings, defi, publish entry, export
	ed.set_world({"race_style": "accuracy", "race_author": "Tester"})
	check("course settings are worldspawn keys", Q3MapRace.style(ed.doc) == "accuracy" and Q3MapRace.author(ed.doc) == "Tester")
	var defi := Q3MapRace.defi("my_course", ed.doc)
	var blocks := Q3MapPacks._info_blocks(defi)
	check("the .defi names the map, style, author and VQ3 physics, and the menu's reader takes it",
			blocks.size() == 1 and blocks[0]["map"] == "my_course" and blocks[0]["style"] == "accuracy" and blocks[0]["author"] == "Tester"
			and blocks[0]["vq3"] == "1" and blocks[0]["cpm"] == "0" and blocks[0]["longname"] == "Editor test course", defi)
	var ex := Q3MapExport.to_map(ed.doc)
	var text: String = ex["text"]
	check("the .map export keeps the Defrag classes and keys for Radiant", ex["warnings"].is_empty() and text.contains("\"classname\" \"target_startTimer\"")
			and text.contains("\"classname\" \"target_stopTimer\"") and text.contains("\"classname\" \"target_checkpoint\"") and text.contains("\"race_style\" \"accuracy\""))
	var back := Q3MapImport.parse(text, "back")
	var bd: Q3MapDoc = back["doc"]
	check("the exported .map reads back as the same course (a start, a checkpoint, a finish, with the style)",
			bd != null and Q3MapRace.has_course(bd) and Q3MapRace.ids_of(bd, Q3MapRace.CHECKPOINT).size() == 1 and Q3MapRace.style(bd) == "accuracy"
			and bd.ids_of("entity").size() == ed.doc.ids_of("entity").size())

	# ---- play-test and publish as a course
	Session.dry_run = true
	var errs := Session.play_test({"map": NAME, "race": true, "doc": ed.doc, "models": {}}, 3)
	var s := Session.setup
	check("a course's play-test is a Race: the mode, no bots, no cheats, no camera start",
			errs.is_empty() and s != null and s.mode == "race" and s.bots.is_empty() and not s.has_meta("devmap") and s.source == "editor", str(errs))
	check("race setup validates as a play-test (an editor map isn't in the arena list)", s != null and s.validate().is_empty())
	Session.dry_run = false
	Session.setup = null
	Session.playtest = {}

	# ---- a course made with the tools, run in the game
	var c := Q3MapCompiler.new()
	add_child(c)
	if Q3MapCompiler.ensure_kit() != "":
		check("kit", false)
		_finish(t0)
		return
	c.compile(NAME, text, ["bsp"])
	var r: Dictionary = await c.finished
	check("q3map compiles the course made with the tools, no leak", r["ok"] and not r["leaked"], "%s%s" % [r["error"], "" if r["ok"] else "\n" + String(r["log"]).right(900)])
	if not r["ok"]:
		_finish(t0)
		return
	var err := Q3Host.start()
	Q3Server.set_cvar("g_gametype", "0")
	Q3Server.set_cvar("g_ruleset", "classic")
	Q3Server.set_cvar("g_race", "1")
	Q3Server.set_cvar("fraglimit", "0")
	Q3Server.set_cvar("timelimit", "0")
	Q3Server.set_cvar("g_doWarmup", "0")
	var loaded := err == "" and Q3Server.exec_now("map " + NAME) == 0 and Q3Server.running()
	check("the game loads it", loaded, err if err != "" else Q3Server.error())
	if not loaded:
		_finish(t0)
		return
	Q3Server.frame(Q3Host.FRAME_MSEC)
	var me := Q3Server.connect_local(Q3Host.userinfo({"name": "Runner", "model": "sarge"}))
	var run := Q3RaceRun.new()
	var cps: Array = []
	run.checkpoint.connect(func(n: int, ms: int, split: int): cps.append([n, ms]))
	for f in 1600:
		var t := Q3Server.time() + Q3Host.FRAME_MSEC
		Q3Server.usercmd(me, t, Vector3.ZERO, 0, 0, 127, 0, 0)
		Q3Server.frame(Q3Host.FRAME_MSEC)
		for cmd in Q3Server.next_commands(me):
			run.feed(cmd)
		var ps := Q3Server.player_state(me)
		if not ps.is_empty() and (ps["origin"] as Vector3).x > 1000.0:
			break
	check("running it: the start line starts the timer, the checkpoint reports, the finish line stops it (the walk from x=128 to x=900, 2 to 3.5 s)",
			run.state == Q3RaceRun.State.FINISHED and cps.size() == 1 and run.last_ms > 2000 and run.last_ms < 3500 and run.checkpoints_total == 1,
			"state %d, %d checkpoints, %d ms" % [run.state, cps.size(), run.last_ms])
	Q3Server.exec_now("killserver")
	_finish(t0)


func _finish(t0: int) -> void:
	print("RACE EDITOR %d/%d passed (%.1f s)" % [passed, passed + failed, (Time.get_ticks_msec() - t0) / 1000.0])
	get_tree().quit(0 if failed == 0 else 1)
