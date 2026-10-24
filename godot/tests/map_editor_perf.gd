extends Node
## Map editor performance on a big map (MAP_EDITOR_PLAN.md E7): q3dm12, the biggest stock map
## (4,865 brushes, 254 curves), imported into the real editor scene, then timed through what a mapper
## does all day. Windowed for frame times (headless draws nothing):
##   godot --path . res://tests/map_editor_perf.tscn [-- map=q3dm12 report=C:/temp/perf.json]
## Prints each timing and "MAP EDITOR PERF n/m passed" against the budgets below.

const SCRATCH := "user://map_editor_test"

var passed := 0
var failed := 0
var ed: Q3MapEditor
var times := {}


func check(name: String, ok: bool, detail := "") -> void:
	if ok:
		passed += 1
	else:
		failed += 1
	print("  %s  map editor perf: %s%s" % ["PASS" if ok else "FAIL", name, ("  (" + detail + ")") if detail != "" else ""])


func _ready() -> void:
	_run.call_deferred()


func _ms(t0: int) -> float:
	return (Time.get_ticks_usec() - t0) / 1000.0


func _run() -> void:
	var map := "q3dm12"
	var report := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("map="):
			map = a.substr(4)
		elif a.begins_with("report="):
			report = a.substr(7)
	if not Q3BspImport.maps_in_paks().has(map):
		print("  (no %s in the paks: skipped)" % map)
		get_tree().quit(0)
		return
	Q3MapEditor.maps_dir = SCRATCH.path_join("maps")
	Q3MapEditor.recent = SCRATCH.path_join("recent.json")
	ed = load("res://scenes/map_editor/map_editor.tscn").instantiate()
	add_child(ed)
	await get_tree().process_frame

	var t := Time.get_ticks_usec()
	var err: String = await ed.import_map("bsp", map)
	times["import"] = _ms(t)
	var objects := ed.doc.objects.size()
	t = Time.get_ticks_usec()
	ed._set_doc(ed.doc, "")
	times["draw_all"] = _ms(t)
	await _frames(30)
	times["frame_view"] = await _frame_time(120)

	t = Time.get_ticks_usec()
	ed.save_map("perf_" + map)
	times["save"] = _ms(t)
	t = Time.get_ticks_usec()
	ed.open_map(Q3MapEditor.map_path("perf_" + map))
	times["open"] = _ms(t)
	await _frames(10)

	# one brush picked, nudged, undone: the everyday edit
	var some: int = ed.doc.ids_of("brush")[ed.doc.ids_of("brush").size() / 2]
	ed.select([some])
	t = Time.get_ticks_usec()
	ed.execute("edit.nudge.up")
	ed.mesher.flush()
	times["nudge_one"] = _ms(t)
	t = Time.get_ticks_usec()
	ed.doc.undo()
	ed.mesher.flush()
	times["undo_one"] = _ms(t)

	# everything at once
	t = Time.get_ticks_usec()
	ed.execute("select.all")
	times["select_all"] = _ms(t)
	await _frames(5)
	times["frame_all_selected"] = await _frame_time(30)
	t = Time.get_ticks_usec()
	ed.execute("edit.nudge.up")
	ed.mesher.flush()
	times["nudge_all"] = _ms(t)
	t = Time.get_ticks_usec()
	ed.doc.undo()
	ed.mesher.flush()
	times["undo_all"] = _ms(t)
	ed.clear_selection()

	for k in times:
		print("  %-20s %8.1f ms" % [k, times[k]])
	var headless := DisplayServer.get_name() == "headless"
	check("%s imports (%d objects)" % [map, objects], err == "" and times["import"] < 20000.0, "%.1f s" % (times["import"] / 1000.0))
	check("save and open the imported map in under 3 s each", times["save"] < 3000.0 and times["open"] < 3000.0,
			"save %.0f ms, open %.0f ms" % [times["save"], times["open"]])
	check("one brush nudged and undone, redrawn, in under 50 ms each", times["nudge_one"] < 50.0 and times["undo_one"] < 50.0,
			"%.1f / %.1f ms" % [times["nudge_one"], times["undo_one"]])
	check("select all, nudge all, undo all in under 2 s each", times["select_all"] < 2000.0 and times["nudge_all"] < 2000.0 and times["undo_all"] < 2000.0,
			"%.0f / %.0f / %.0f ms" % [times["select_all"], times["nudge_all"], times["undo_all"]])
	if not headless:
		check("the whole map in view holds 60 fps (frame under 16.7 ms)", times["frame_view"] < 16.7, "%.2f ms" % times["frame_view"])
		check("everything selected stays above 30 fps", times["frame_all_selected"] < 33.3, "%.2f ms" % times["frame_all_selected"])
	if report != "":
		var f := FileAccess.open(report, FileAccess.WRITE)
		if f:
			f.store_string(JSON.stringify({"map": map, "objects": objects, "ms": times}, "\t"))
	var g := ProjectSettings.globalize_path(SCRATCH.path_join("maps"))
	for n in ["perf_" + map, Q3MapEditor.AUTOSAVE]:
		DirAccess.remove_absolute(g.path_join(n + Q3MapDoc.EXTENSION))
	print("MAP EDITOR PERF %d/%d passed" % [passed, passed + failed])
	get_tree().quit(1 if failed else 0)


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame


## The mean frame time over n frames, in ms.
func _frame_time(n: int) -> float:
	var t := Time.get_ticks_usec()
	await _frames(n)
	return _ms(t) / n
