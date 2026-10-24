extends Node
## A game process that dies on purpose, for tests/diagnostics_test.gd:
##   godot --headless --path . res://tests/diagnostics_crash.tscn -- kind=null|abort|stack|godot|hang
## It logs an error and a script error first, leaves breadcrumbs, then crashes (or, for hang, blocks
## the main thread for 17 s and exits normally).


func _ready() -> void:
	var kind := "null"
	for a in OS.get_cmdline_user_args():
		if a.begins_with("kind="):
			kind = a.substr(5)
	Diagnostics.set_context("test", kind)
	Diagnostics.note("about to crash: " + kind)
	push_error("diagnostics test error before the crash")
	_script_error.call_deferred()
	await get_tree().create_timer(2.5).timeout   # a heartbeat carries the breadcrumbs to disk
	match kind:
		"null": ClassDB.class_call_static("Q3Core", "crash_test", 0)
		"abort": ClassDB.class_call_static("Q3Core", "crash_test", 1)
		"stack": ClassDB.class_call_static("Q3Core", "crash_test", 2)
		"godot": OS.crash("diagnostics test: OS.crash")
		"hang":
			OS.delay_msec(17000)
			await get_tree().process_frame
			await get_tree().process_frame
	get_tree().quit(0)


func _script_error() -> void:
	var nothing: Node = null
	nothing.get_child(0)   # SCRIPT ERROR with a backtrace
