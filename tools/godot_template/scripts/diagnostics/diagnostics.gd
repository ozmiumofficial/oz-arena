extends Node
## Diagnostics (autoload, first in the list): watches the running game and writes a report whenever
## it crashes or freezes, so the problem can be found and fixed.
##
## While the game runs:
##   - every engine, script and shader error and warning is caught with its script backtrace
##     (Q3DiagLogger) and flushed to user://diagnostics/sessions/<pid>.errors.log;
##   - user://diagnostics/sessions/<pid>.json is the session: version, machine, and a heartbeat every
##     2 s with the scene, context (map, mode, editor map), frame rate, memory and the last breadcrumbs;
##   - q3core writes a minidump and a note for a native crash (Q3Core.install_crash_dumps);
##   - a watchdog thread writes a freeze report when no frame has finished for 15 s.
## On a clean exit the session file goes away. At the next launch, a session file whose process is
## gone means a crash: Diagnostics turns it into user://diagnostics/reports/crash_<date>_<pid>.md
## with Godot's backtrace from that session's log, the native note, the errors and the breadcrumbs,
## and the main menu says so. F10 shows the overlay (Q3DiagOverlay).

const DIR := "user://diagnostics"
const SESSIONS := DIR + "/sessions"
const REPORTS := DIR + "/reports"
const KEEP_REPORTS := 30
const HEARTBEAT := 2.0
const HANG_SECONDS := 15.0
const BREADCRUMBS := 60

var session_id := ""
var started := 0.0
var is_test := false                    ## run from a res://tests scene: reported, but no menu notice
var context := {}                       ## what the game is doing (map, mode, editor map ...), set_context()
var breadcrumbs: PackedStringArray = []
var new_reports: PackedStringArray = [] ## reports written at this launch, for crashes of earlier runs
var notice := ""                        ## the menu's "closed unexpectedly" line, once

var logger: Q3DiagLogger
var _session_path := ""
var _beat := 0.0
var _scene := ""
var _mutex := Mutex.new()
var _watchdog: Thread
var _stop := false
var _last_frame := 0                    ## Time.get_ticks_msec() at the last frame (read by the watchdog)
var _hang_report := ""                  ## the freeze report being written, while frozen
var _hang_since := 0
var _overlay: CanvasLayer
var _machine := {}                      ## read once on the main thread: the watchdog must never call
										## RenderingServer while the main thread is frozen (it deadlocks)
var _stalls := 0                        ## expect_stall() depth: the watchdog looks away


func _enter_tree() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	process_priority = -1000
	started = Time.get_unix_time_from_system()
	var pid := OS.get_process_id()
	session_id = "%d-%d" % [int(started), pid]
	for d in [SESSIONS, REPORTS]:
		DirAccess.make_dir_recursive_absolute(d)
	var args := " ".join(OS.get_cmdline_args())
	is_test = args.contains("res://tests/")
	# the marker that finds this session's lines in Godot's own log later
	print("[diagnostics] session %s pid %d" % [session_id, pid])
	_session_path = SESSIONS.path_join("%d.json" % pid)
	_machine = machine()
	logger = Q3DiagLogger.new()
	logger.path = SESSIONS.path_join("%d.errors.log" % pid)
	OS.add_logger(logger)
	collect_crashes()
	if ClassDB.class_exists("Q3Core"):
		ClassDB.class_call_static("Q3Core", "install_crash_dumps", ProjectSettings.globalize_path(REPORTS),
				"%s session %s" % [version(), session_id])
	note("started: " + args)
	_last_frame = Time.get_ticks_msec()
	_write_session()
	_watchdog = Thread.new()
	_watchdog.start(_watch)


func _exit_tree() -> void:
	_stop = true
	if _watchdog and _watchdog.is_started():
		_watchdog.wait_to_finish()
	OS.remove_logger(logger)
	logger.close()
	# a clean exit: no crash to report. Errors stay on record if there were any.
	var errs := logger.path
	if logger.errors > 0 and FileAccess.file_exists(errs):
		DirAccess.rename_absolute(errs, REPORTS.path_join("errors_%s_%d.log" % [_stamp(started), OS.get_process_id()]))
	else:
		DirAccess.remove_absolute(errs)
	DirAccess.remove_absolute(_session_path)
	_trim_reports()


func version() -> String:
	return "Q3Remaster %s (%s%s, Godot %s)" % [ProjectSettings.get_setting("application/config/version", "?"),
			"debug" if OS.is_debug_build() else "release", ", editor" if OS.has_feature("editor") else "",
			Engine.get_version_info()["string"]]


## A line in the breadcrumb trail (what the game was doing before a crash).
func note(text: String) -> void:
	_mutex.lock()
	breadcrumbs.append("%s %s" % [Time.get_time_string_from_system(), text])
	if breadcrumbs.size() > BREADCRUMBS:
		breadcrumbs = breadcrumbs.slice(breadcrumbs.size() - BREADCRUMBS)
	_mutex.unlock()


## What the game is doing, for the reports: set_context("map", "q3dm17"); null clears it.
func set_context(key: String, value: Variant) -> void:
	if value == null:
		context.erase(key)
	else:
		context[key] = value
	note("%s = %s" % [key, value])


func _process(delta: float) -> void:
	_last_frame = Time.get_ticks_msec()
	if _hang_report != "":
		_recovered()
	var scene := get_tree().current_scene.scene_file_path if get_tree().current_scene else ""
	if scene != _scene:
		_scene = scene
		note("scene " + scene)
	_beat += delta
	if _beat >= HEARTBEAT:
		_beat = 0.0
		_write_session()


func _input(ev: InputEvent) -> void:
	if ev is InputEventKey and ev.pressed and not ev.echo and ev.physical_keycode == KEY_F10:
		toggle_overlay()
		get_viewport().set_input_as_handled()


func toggle_overlay() -> void:
	if _overlay == null:
		_overlay = Q3DiagOverlay.new()
		add_child(_overlay)
	else:
		_overlay.visible = not _overlay.visible


## The state worth knowing after a crash.
func state() -> Dictionary:
	var s := {
		"scene": _scene,
		"context": context.duplicate(),
		"uptime_s": int(Time.get_unix_time_from_system() - started),
		"frames": Engine.get_process_frames(),
		"fps": Engine.get_frames_per_second(),
		"frame_ms": snappedf(Performance.get_monitor(Performance.TIME_PROCESS) * 1000.0, 0.01),
		"memory_mb": snappedf(Performance.get_monitor(Performance.MEMORY_STATIC) / 1048576.0, 0.1),
		"video_mb": snappedf(Performance.get_monitor(Performance.RENDER_VIDEO_MEM_USED) / 1048576.0, 0.1),
		"nodes": int(Performance.get_monitor(Performance.OBJECT_NODE_COUNT)),
		"objects": int(Performance.get_monitor(Performance.OBJECT_COUNT)),
		"draw_calls": int(Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME)),
		"errors": logger.errors,
		"warnings": logger.warnings,
	}
	if ClassDB.class_exists("Q3Server") and ClassDB.class_call_static("Q3Server", "running"):
		s["q3_map"] = ClassDB.class_call_static("Q3Server", "cvar", "mapname")
		s["q3_gametype"] = ClassDB.class_call_static("Q3Server", "cvar", "g_gametype")
	return s


func _write_session() -> void:
	_mutex.lock()
	var crumbs := breadcrumbs.duplicate()
	_mutex.unlock()
	var data := {
		"session": session_id, "pid": OS.get_process_id(), "started": started, "heartbeat": Time.get_unix_time_from_system(),
		"version": version(), "test": is_test, "args": OS.get_cmdline_args(), "machine": _machine,
		"state": state(), "breadcrumbs": crumbs, "hang": _hang_report != "",
	}
	_write_atomic(_session_path, JSON.stringify(data, "\t"))


static func machine() -> Dictionary:
	return {"os": "%s %s" % [OS.get_name(), OS.get_version()], "cpu": OS.get_processor_name(),
			"threads": OS.get_processor_count(), "gpu": RenderingServer.get_video_adapter_name(),
			"driver": RenderingServer.get_video_adapter_api_version(), "renderer": RenderingServer.get_current_rendering_method(),
			"ram_mb": int(OS.get_memory_info().get("physical", 0) / 1048576),
			"screen": str(DisplayServer.screen_get_size()) if DisplayServer.get_name() != "headless" else "headless",
			"locale": OS.get_locale()}


static func _write_atomic(path: String, text: String) -> void:
	var tmp := path + ".tmp"
	var f := FileAccess.open(tmp, FileAccess.WRITE)
	if f == null:
		return
	f.store_string(text)
	f.close()
	DirAccess.rename_absolute(tmp, path)


static func _stamp(unix: float) -> String:
	return Time.get_datetime_string_from_unix_time(int(unix + Time.get_time_zone_from_system()["bias"] * 60)).replace("T", "_").replace(":", ".")


# ---------------------------------------------------------------- freezes

## Watchdog thread: a main thread that hasn't finished a frame for HANG_SECONDS gets a freeze report
## at once (the player may kill the game before it ever comes back).
func _watch() -> void:
	while not _stop:
		OS.delay_msec(500)
		var since := Time.get_ticks_msec() - _last_frame
		if since >= HANG_SECONDS * 1000.0 and _hang_report == "" and _stalls == 0 and not _stop:
			_hang_since = _last_frame
			_hang_report = REPORTS.path_join("freeze_%s_%d.md" % [_stamp(Time.get_unix_time_from_system()), OS.get_process_id()])
			_write_report(_hang_report, "freeze", _session_snapshot(), "", "")


func _recovered() -> void:
	var secs := (Time.get_ticks_msec() - _hang_since) / 1000.0
	var f := FileAccess.open(_hang_report, FileAccess.READ_WRITE)
	if f:
		f.seek_end()
		f.store_string("\n**Recovered:** the game came back after %.1f s.\n" % secs)
		f.close()
	note("recovered from a %.1f s freeze" % secs)
	_hang_report = ""


func _session_snapshot() -> Dictionary:
	_mutex.lock()
	var crumbs := breadcrumbs.duplicate()
	_mutex.unlock()
	return {"session": session_id, "pid": OS.get_process_id(), "started": started, "heartbeat": Time.get_unix_time_from_system(),
			"version": version(), "test": is_test, "args": OS.get_cmdline_args(), "machine": _machine,
			"state": {"scene": _scene, "context": context.duplicate(), "errors": logger.errors}, "breadcrumbs": crumbs}


## Wraps work known to block the main thread for a long time (true before, false after), so the
## watchdog doesn't call it a freeze.
func expect_stall(on: bool) -> void:
	_stalls = maxi(_stalls + (1 if on else -1), 0)
	_last_frame = Time.get_ticks_msec()


# ---------------------------------------------------------------- crash reports

## Turns the session files of processes that are gone into crash reports. Runs at startup; the
## diagnostics test calls it again after its child crashes. Returns the reports written.
func collect_crashes() -> PackedStringArray:
	var made := PackedStringArray()
	var d := DirAccess.open(SESSIONS)
	if d == null:
		return made
	for f in d.get_files():
		if not f.ends_with(".json"):
			continue
		var path := SESSIONS.path_join(f)
		var s: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
		if not s is Dictionary:
			DirAccess.remove_absolute(path)
			continue
		var pid := int(s.get("pid", 0))
		if pid == OS.get_process_id():
			continue
		if _alive(pid, float(s.get("started", 0)), float(s.get("heartbeat", 0))):
			continue
		var errs_path := SESSIONS.path_join("%d.errors.log" % pid)
		var errs := FileAccess.get_file_as_string(errs_path) if FileAccess.file_exists(errs_path) else ""
		var kind := "freeze" if s.get("hang", false) else "crash"
		var report := REPORTS.path_join("%s_%s_%d.md" % [kind, _stamp(float(s.get("heartbeat", 0))), pid])
		_write_report(report, kind, s, errs, _godot_log_for(String(s.get("session", ""))))
		made.append(report)
		DirAccess.remove_absolute(path)
		DirAccess.remove_absolute(errs_path)
		if not s.get("test", false):
			notice = "Quake III Arena Remastered closed unexpectedly last time. A crash report was saved to %s" % ProjectSettings.globalize_path(REPORTS)
	new_reports.append_array(made)
	_trim_reports()
	return made


## Still running: another copy of the game or a test, perhaps busy or frozen, but not crashed.
## q3core asks Windows when that pid started, so a reused pid doesn't count; without q3core, a fresh
## heartbeat has to do.
static func _alive(pid: int, session_started: float, heartbeat: float) -> bool:
	if ClassDB.class_exists("Q3Core"):
		var t: float = ClassDB.class_call_static("Q3Core", "process_start_time", pid)
		return t > 0 and absf(t - session_started) < 30.0
	return Time.get_unix_time_from_system() - heartbeat < HEARTBEAT * 5


## The lines of Godot's own log (user://logs) from the session with this id, found by its marker.
func _godot_log_for(id: String) -> String:
	if id == "":
		return ""
	var log_path := String(ProjectSettings.get_setting("debug/file_logging/log_path", "user://logs/godot.log"))
	var dir := log_path.get_base_dir()
	var d := DirAccess.open(dir)
	if d == null:
		return ""
	var marker := "[diagnostics] session " + id
	for f in d.get_files():
		if not f.ends_with(".log"):
			continue
		var text := FileAccess.get_file_as_string(dir.path_join(f))
		var at := text.find(marker)
		if at >= 0:
			return text.substr(at)
	return ""


## Godot's crash handler output inside a log: "Program crashed" through the end of the backtraces.
static func backtrace_in(log_text: String) -> String:
	var at := log_text.find("Program crashed")
	if at < 0:
		return ""
	var start := log_text.rfind("\n", at)
	var end := log_text.find("-- END OF GDSCRIPT BACKTRACE --", at)
	if end < 0:
		end = log_text.find("-- END OF C++ BACKTRACE --", at)
	end = log_text.find("\n", end) if end >= 0 else log_text.length()
	return log_text.substr(maxi(start, 0), end - start).strip_edges()


## The native note q3core wrote for this process, and its dump's file name.
static func native_note(pid: int) -> Array:
	var d := DirAccess.open(REPORTS)
	if d == null:
		return ["", ""]
	for f in d.get_files():
		if f.begins_with("crash_") and f.ends_with("_%d.txt" % pid):
			var dmp := f.trim_suffix(".txt") + ".dmp"
			return [FileAccess.get_file_as_string(REPORTS.path_join(f)), dmp if FileAccess.file_exists(REPORTS.path_join(dmp)) else ""]
	return ["", ""]


func _write_report(path: String, kind: String, s: Dictionary, errors: String, log_text: String) -> void:
	var st: Dictionary = s.get("state", {})
	var m: Dictionary = s.get("machine", {})
	var pid := int(s.get("pid", 0))
	var lines := PackedStringArray()
	lines.append("# Quake III Arena Remastered %s report" % kind)
	lines.append("")
	if kind == "freeze":
		lines.append("**What happened:** the game stopped responding: no frame finished for %d s." % int(HANG_SECONDS))
	else:
		lines.append("**What happened:** the game closed without shutting down (a crash, or the process was killed).")
	lines.append("")
	lines.append("| | |")
	lines.append("|---|---|")
	lines.append("| When | %s (last sign of life) |" % Time.get_datetime_string_from_unix_time(int(float(s.get("heartbeat", 0)) + Time.get_time_zone_from_system()["bias"] * 60), true))
	lines.append("| Ran for | %d s |" % int(float(s.get("heartbeat", 0)) - float(s.get("started", 0))))
	lines.append("| Version | %s |" % s.get("version", "?"))
	lines.append("| Session | %s (pid %d)%s |" % [s.get("session", "?"), pid, ", a test run" if s.get("test", false) else ""])
	lines.append("| Command line | `%s` |" % " ".join(s.get("args", [])))
	lines.append("| Scene | %s |" % st.get("scene", "?"))
	for k in st.get("context", {}):
		lines.append("| %s | %s |" % [k, _cell(st["context"][k])])
	for k in ["q3_map", "q3_gametype", "fps", "frame_ms", "memory_mb", "video_mb", "nodes", "objects", "draw_calls", "errors", "warnings"]:
		if st.has(k):
			lines.append("| %s | %s |" % [k, _cell(st[k])])
	for k in m:
		if str(m[k]) != "":
			lines.append("| %s | %s |" % [k, _cell(m[k])])
	var nat := native_note(pid)
	if nat[0] != "":
		lines.append("")
		lines.append("## Native crash (q3core)")
		lines.append("")
		lines.append("```")
		lines.append(String(nat[0]).strip_edges())
		lines.append("```")
		if nat[1] != "":
			lines.append("Minidump: `%s` (open it in Visual Studio or WinDbg with the q3core PDB)." % ProjectSettings.globalize_path(REPORTS.path_join(nat[1])))
	var bt := backtrace_in(log_text)
	if bt != "":
		lines.append("")
		lines.append("## Godot's backtrace")
		lines.append("")
		lines.append("```")
		lines.append(bt)
		lines.append("```")
	lines.append("")
	lines.append("## Errors and warnings in that session")
	lines.append("")
	if errors.strip_edges() == "":
		lines.append("None.")
	else:
		var el := errors.strip_edges().split("\n")
		lines.append("```")
		lines.append("\n".join(el.slice(maxi(el.size() - 400, 0))))
		lines.append("```")
	lines.append("")
	lines.append("## What it was doing (breadcrumbs)")
	lines.append("")
	lines.append("```")
	lines.append("\n".join(PackedStringArray(s.get("breadcrumbs", []))))
	lines.append("```")
	if log_text != "":
		var ll := log_text.strip_edges().split("\n")
		lines.append("")
		lines.append("## The end of Godot's log")
		lines.append("")
		lines.append("```")
		lines.append("\n".join(ll.slice(maxi(ll.size() - 150, 0))))
		lines.append("```")
	_write_atomic(path, "\n".join(lines) + "\n")


## A table cell: whole numbers without the ".0" JSON gave them.
static func _cell(v: Variant) -> String:
	if v is float and v == roundf(v):
		return str(int(v))
	return str(v)


## Reports, newest first: [{path, name, kind, time}].
static func reports() -> Array:
	var out := []
	var d := DirAccess.open(REPORTS)
	if d == null:
		return out
	for f in d.get_files():
		if f.ends_with(".md") or (f.begins_with("errors_") and f.ends_with(".log")):
			var p := REPORTS.path_join(f)
			out.append({"path": p, "name": f, "kind": f.get_slice("_", 0), "time": FileAccess.get_modified_time(p)})
	out.sort_custom(func(a, b): return a["time"] > b["time"])
	return out


## Keeps the newest KEEP_REPORTS reports, with their dumps and notes.
static func _trim_reports() -> void:
	var all := reports()
	for r in all.slice(KEEP_REPORTS):
		DirAccess.remove_absolute(r["path"])
		var pid: String = String(r["name"]).get_basename().get_slice("_", String(r["name"]).get_basename().get_slice_count("_") - 1)
		for ext in [".dmp", ".txt"]:
			var d := DirAccess.open(REPORTS)
			if d:
				for f in d.get_files():
					if f.begins_with("crash_") and f.ends_with("_" + pid + ext):
						d.remove(f)


func open_reports_folder() -> void:
	OS.shell_open(ProjectSettings.globalize_path(REPORTS))
