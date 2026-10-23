extends Node
## Packs the web menu's imported files (images, sounds, fonts: everything under res://webui with
## a .import beside it) as the plain files the page loads, into a .pck an exported game mounts
## (Q3WebLayer._mount_assets). Run by tools/export_game.py with the editor binary:
##   godot --headless --path . res://tests/pack_web_assets.tscn -- out=C:/path/webui_assets.pck


func _ready() -> void:
	var out := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			out = a.substr(4)
	var files: PackedStringArray = []
	_collect("res://webui", files)
	var packer := PCKPacker.new()
	var ok := out != "" and packer.pck_start(out) == OK
	for f in files:
		ok = ok and packer.add_file(f, ProjectSettings.globalize_path(f)) == OK
	ok = ok and packer.flush() == OK
	print("WEB ASSETS %s: %d files -> %s" % ["ok" if ok else "FAILED", files.size(), out])
	get_tree().quit(0 if ok else 1)


func _collect(dir: String, out: PackedStringArray) -> void:
	var d := DirAccess.open(dir)
	if d == null:
		return
	for sub in d.get_directories():
		_collect(dir.path_join(sub), out)
	for f in d.get_files():
		if f.ends_with(".import"):
			var src := dir.path_join(f.trim_suffix(".import"))
			if FileAccess.file_exists(src):
				out.append(src)
