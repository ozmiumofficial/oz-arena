extends Node
## Crash reporting test (Diagnostics): runs tests/diagnostics_crash.tscn as child game processes that
## really crash (a null write in q3core, abort(), a stack overflow, Godot's own OS.crash) or freeze
## for 17 s, then checks the reports Diagnostics makes of them.
##   godot --headless --path . res://tests/diagnostics_test.tscn [-- kinds=null,abort]
## Prints one line per check and "DIAGNOSTICS n/m passed"; exit code 1 on any failure. Takes about
## 40 s. The reports it makes stay in user://diagnostics/reports (marked as test runs).

var passed := 0
var failed := 0


func check(name: String, ok: bool, detail := "") -> void:
	if ok:
		passed += 1
	else:
		failed += 1
	print("  %s  diagnostics: %s%s" % ["PASS" if ok else "FAIL", name, ("  (" + detail + ")") if detail != "" else ""])


func _ready() -> void:
	_run.call_deferred()


func _child(kind: String) -> int:
	var out := []
	Diagnostics.expect_stall(true)   # OS.execute blocks this process while the child runs
	var code := OS.execute(OS.get_executable_path(), ["--headless", "--path", ProjectSettings.globalize_path("res://"),
			"res://tests/diagnostics_crash.tscn", "--", "kind=" + kind], out, true)
	Diagnostics.expect_stall(false)
	return code


func _report_for(paths: PackedStringArray, kind: String) -> String:
	for p in paths:
		var t := FileAccess.get_file_as_string(p)
		if t.contains("| test | %s |" % kind):
			return t
	return ""


func _run() -> void:
	var kinds := ["null", "abort", "stack", "godot", "hang"]
	for a in OS.get_cmdline_user_args():
		if a.begins_with("kinds="):
			kinds = Array(a.substr(6).split(","))

	check("this run has a session file, a logger and the q3core dump hook",
			FileAccess.file_exists(Diagnostics._session_path) and Diagnostics.logger != null and ClassDB.class_exists("Q3Core"))
	var before := Diagnostics.logger.errors
	push_error("diagnostics self-check")
	check("errors are caught with where they came from",
			Diagnostics.logger.errors == before + 1 and Diagnostics.logger.snapshot()[-1].contains("diagnostics self-check")
			and Diagnostics.logger.snapshot()[-1].contains("diagnostics_test.gd"))

	for kind in kinds:
		var t0 := Time.get_unix_time_from_system()
		var code := _child(kind)
		if kind == "hang":
			var freeze := ""
			for r in Diagnostics.reports():
				if r["kind"] == "freeze" and r["time"] >= int(t0):
					freeze = FileAccess.get_file_as_string(r["path"])
					break
			check("a 17 s freeze gets a freeze report, marked recovered when the game comes back",
					freeze.contains("stopped responding") and freeze.contains("**Recovered:**") and freeze.contains("about to crash: hang"),
					"exit %d" % code)
			continue
		var made := Diagnostics.collect_crashes()
		var text := _report_for(made, kind)
		var has := func(s: String) -> bool: return text.contains(s)
		var parts := []
		for p in ["about to crash: " + kind, "diagnostics test error before the crash", "SCRIPT ERROR", "_script_error"]:
			if not has.call(p):
				parts.append("missing '%s'" % p)
		match kind:
			"null":
				if not has.call("access violation") or not has.call("writing 0x0") or not has.call("Minidump:"):
					parts.append("no native note or dump")
				if not has.call("Program crashed"):
					parts.append("no Godot backtrace")
			"abort":
				if not has.call("abort()") or not has.call("Minidump:"):
					parts.append("no native note or dump")
			"stack":
				if not has.call("stack overflow") or not has.call("Minidump:"):
					parts.append("no native note or dump")
			"godot":
				if not has.call("Program crashed"):
					parts.append("no Godot backtrace")
		var dump_ok := true
		var m := RegEx.create_from_string("Minidump: `([^`]+)`").search(text)
		if m:
			dump_ok = FileAccess.file_exists(m.get_string(1)) and FileAccess.open(m.get_string(1), FileAccess.READ).get_length() > 10000
		check("%s: the crash leaves a report with the errors, backtraces and breadcrumbs%s" % [kind,
				", a native note and a minidump" if kind in ["null", "abort", "stack"] else ""],
				text != "" and parts.is_empty() and dump_ok and code != 0,
				"exit %d, %s" % [code, "no report" if text == "" else (", ".join(parts) if parts.size() else "ok")])
	# the children all looked at this process's session file while it was blocked in OS.execute
	var mine := Diagnostics.reports().filter(func(r): return String(r["name"]).ends_with("_%d.md" % OS.get_process_id()))
	check("a running copy of the game is never taken for a crashed one", mine.is_empty() and FileAccess.file_exists(Diagnostics._session_path),
			str(mine.map(func(r): return r["name"])))
	print("DIAGNOSTICS %d/%d passed" % [passed, passed + failed])
	get_tree().quit(1 if failed else 0)
