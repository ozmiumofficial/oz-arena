extends Node
## Loads every script and scene in the project the way the game does (autoloads up), so every
## parse error and warning-as-error shows in the log, as the editor's Errors panel would list it.
##   godot --headless --path . res://tests/check_scripts.tscn
## Prints "CHECK SCRIPTS <n> scripts, <m> failed".

const SKIP := ["res://addons/", "res://.godot/", "res://webui/", "res://library/"]


func _ready() -> void:
	var files: Array[String] = []
	_walk("res://", files)
	var failed: Array[String] = []
	for f in files:
		var r: Resource = load(f)
		if r == null or (r is GDScript and not (r as GDScript).can_instantiate()):
			failed.append(f)
	for f in failed:
		print("FAILED ", f)
	print("CHECK SCRIPTS %d scripts, %d failed" % [files.size(), failed.size()])
	get_tree().quit(1 if failed.size() else 0)


func _walk(dir: String, out: Array[String]) -> void:
	for s in SKIP:
		if dir.begins_with(s):
			return
	for f in DirAccess.get_files_at(dir):
		if f.ends_with(".gd"):
			out.append(dir.path_join(f))
	for d in DirAccess.get_directories_at(dir):
		if not d.begins_with("."):
			_walk(dir.path_join(d), out)
