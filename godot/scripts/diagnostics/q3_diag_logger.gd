class_name Q3DiagLogger
extends Logger
## Diagnostics' hook into Godot's logging (OS.add_logger): every engine, script and shader error and
## warning, with its script backtraces, goes into a ring the overlay reads and straight into the
## session's error file, flushed, so the lines survive a crash. Called from any thread.

const RING := 200

var path := ""                  ## the session's error file (set by Diagnostics)
var lines: PackedStringArray = []
var errors := 0
var warnings := 0
var last_error := ""

var _mutex := Mutex.new()
var _file: FileAccess
var _busy := false              ## a write that fails must not log itself again


func _log_error(function: String, file: String, line: int, code: String, rationale: String,
		_editor_notify: bool, error_type: int, script_backtraces: Array[ScriptBacktrace]) -> void:
	var kind := "ERROR"
	match error_type:
		ERROR_TYPE_WARNING: kind = "WARNING"
		ERROR_TYPE_SCRIPT: kind = "SCRIPT ERROR"
		ERROR_TYPE_SHADER: kind = "SHADER ERROR"
	var text := rationale if rationale != "" else code
	var entry := "%s %s: %s\n    at %s (%s:%d)" % [_clock(), kind, text, function, file, line]
	for bt in script_backtraces:
		if bt != null and not bt.is_empty():
			entry += "\n" + bt.format(4, 6).rstrip("\n")
	_add(entry, error_type != ERROR_TYPE_WARNING, text)


func _log_message(message: String, error: bool) -> void:
	if error:
		_add("%s ERROR: %s" % [_clock(), message.strip_edges()], true, message.strip_edges())


func _add(entry: String, is_error: bool, short: String) -> void:
	_mutex.lock()
	if not _busy:
		_busy = true
		if is_error:
			errors += 1
			last_error = short
		else:
			warnings += 1
		lines.append(entry)
		if lines.size() > RING:
			lines = lines.slice(lines.size() - RING)
		if path != "":
			if _file == null:
				_file = FileAccess.open(path, FileAccess.WRITE)
			if _file:
				_file.store_line(entry)
				_file.flush()
		_busy = false
	_mutex.unlock()


func snapshot() -> PackedStringArray:
	_mutex.lock()
	var out := lines.duplicate()
	_mutex.unlock()
	return out


func close() -> void:
	_mutex.lock()
	_file = null
	_mutex.unlock()


static func _clock() -> String:
	return Time.get_time_string_from_system()
