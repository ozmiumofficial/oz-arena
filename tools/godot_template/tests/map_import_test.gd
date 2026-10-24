extends Node
## Map editor import audit (MAP_EDITOR_PLAN.md E6), headless: every Quake 3 map in the player's paks
## (or maps=a,b) imported from its .bsp, with the audit q3map's own output allows: every planar
## surface q3map drew must be reproduced by an imported face's mapping, every brush must make a
## solid, and the result must survive the editor's .map export and reading back.
##   godot --headless --path . res://tests/map_import_test.tscn [-- maps=q3dm17,q3dm1 report=C:/temp/audit.json]
## Prints a line per map and "MAP IMPORT n/m passed"; exit code 1 on any failure.

var passed := 0
var failed := 0


func check(name: String, ok: bool, detail := "") -> void:
	if ok:
		passed += 1
	else:
		failed += 1
	print("  %s  map import: %s%s" % ["PASS" if ok else "FAIL", name, ("  (" + detail + ")") if detail != "" else ""])


func _ready() -> void:
	_run.call_deferred()


func _run() -> void:
	var t0 := Time.get_ticks_msec()
	var only := PackedStringArray()
	var report_path := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("maps="):
			only = a.substr(5).split(",", false)
		elif a.begins_with("report="):
			report_path = a.substr(7)
	var all := Q3BspImport.maps_in_paks()
	var names := all.keys() if only.is_empty() else Array(only)
	names.sort()
	check("the player's paks list Quake 3's maps", all.size() >= 30, "%d maps" % all.size())
	var reports := {}
	var totals := {"surfaces": 0, "covered": 0, "brushes": 0, "dropped": 0, "faces": 0, "fitted": 0, "tex": 0, "patches": 0}
	for map in names:
		var t := Time.get_ticks_msec()
		var bytes := Q3BspImport.read_from_paks(map)
		var r := Q3BspImport.import_bsp(bytes, map)
		var rep: Dictionary = r["report"]
		rep["ms"] = Time.get_ticks_msec() - t
		reports[map] = rep
		for k in totals:
			totals[k] += rep[k]
		var doc: Q3MapDoc = r["doc"]
		var round_ok := false
		if doc:
			# the editor's own export, read back: same brushes, curves and entities
			var ex := Q3MapExport.to_map(doc)
			var text: String = ex["text"]
			if "debug=1" in OS.get_cmdline_user_args() and ex["warnings"].size():
				print("    export warnings: %s" % [ex["warnings"]])
			var back := Q3MapImport.parse(text, map)
			var bd: Q3MapDoc = back["doc"]
			round_ok = bd != null and bd.ids_of("brush").size() == doc.ids_of("brush").size() \
					and bd.ids_of("patch").size() == doc.ids_of("patch").size() and bd.ids_of("entity").size() == doc.ids_of("entity").size()
			if not round_ok and bd:
				print("    round trip: brushes %d -> %d, curves %d -> %d, entities %d -> %d, %s" % [doc.ids_of("brush").size(),
						bd.ids_of("brush").size(), doc.ids_of("patch").size(), bd.ids_of("patch").size(), doc.ids_of("entity").size(),
						bd.ids_of("entity").size(), back["report"]])
		if "debug=1" in OS.get_cmdline_user_args():
			for u in rep["uncovered"]:
				print("    uncovered: %s" % u)
			if rep["uncovered"].size() and doc:
				# what was imported around the first one
				var at: Vector3 = rep["uncovered"][0]["at"]
				for id in doc.ids_of("brush"):
					var bb := Q3MapEditor.bounds_in(doc, [id])
					if bb.grow(2.0).has_point(at):
						var b := doc.get_obj(id)
						var ws := doc.windings(id)
						for i in b["faces"].size():
							var pl := Q3MapDoc.plane_of(b, i)
							print("      brush %d face %d: %s n (%.3f %.3f %.3f) d %.2f, %s, %d corners" % [id, i, b["faces"][i]["material"],
									pl[0], pl[1], pl[2], pl[3], "uv" if b["faces"][i].has("uv") else str(b["faces"][i].get("tex")), ws[i].size() / 3])
		var ok: bool = doc != null and rep["covered"] == rep["surfaces"] and rep["dropped"] == 0 and round_ok
		check("%s: %d brushes, %d curves, %d entities; %d/%d drawn surfaces aligned, %d faces as shift/rotate/scale" % [
				map, rep["brushes"], rep["patches"], rep["entities"], rep["covered"], rep["surfaces"], rep["tex"]], ok,
				"%d ms, worst fit %.5f, %d dropped, %d undrawn, round trip %s%s" % [rep["ms"], rep["worst"], rep["dropped"], rep["undrawn"],
				round_ok, (", " + ", ".join(rep["errors"])) if rep["errors"].size() else ""])
	if report_path != "":
		var f := FileAccess.open(report_path, FileAccess.WRITE)
		if f:
			f.store_string(JSON.stringify({"maps": reports, "totals": totals}, "\t"))
	print("totals: %s" % totals)
	print("MAP IMPORT %d/%d passed (%.1f s)" % [passed, passed + failed, (Time.get_ticks_msec() - t0) / 1000.0])
	get_tree().quit(1 if failed else 0)
