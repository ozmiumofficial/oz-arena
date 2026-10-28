extends Node
## Packs the named project files into a .pck. tools/export_game.py makes a base release's patch.pck
## with it: Godot's --export-patch writes a pack with no files when nothing changed, and mounting that
## pack hangs the game at startup (load_resource_pack never returns; v0.6.0-beta's first export,
## 2026-10-27), so the patch carries game/version.txt instead.
##   godot --headless --path . res://tests/pack_files.tscn -- out=C:/path/patch.pck file=res://game/version.txt [file=...]


func _ready() -> void:
	var out := ""
	var files: PackedStringArray = []
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			out = a.substr(4)
		elif a.begins_with("file="):
			files.append(a.substr(5))
	var packer := PCKPacker.new()
	var ok := out != "" and files.size() > 0 and packer.pck_start(out) == OK
	for f in files:
		ok = ok and FileAccess.file_exists(f) and packer.add_file(f, ProjectSettings.globalize_path(f)) == OK
	ok = ok and packer.flush() == OK
	print("PACK FILES %s: %d files -> %s" % ["ok" if ok else "FAILED", files.size(), out])
	get_tree().quit(0 if ok else 1)
