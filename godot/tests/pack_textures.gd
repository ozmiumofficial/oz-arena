extends Node
## Packs the world textures' imports (each .import and the files it points to) into
## Q3.TEXTURE_PACKS packs of about equal size, textures-1.pck ... textures-N.pck, which an exported
## game mounts at startup (Q3._init). The main export leaves them out (export_presets.cfg), because
## a GitHub release file must be under 2 GiB. Run by tools/export_game.py with the editor binary:
##   godot --headless --path . res://tests/pack_textures.tscn -- out=C:/path/to/build

const DIR := "res://library/world/textures"


func _ready() -> void:
	var out := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			out = a.substr(4)
	var groups: Array = []   # [[res path, ...] per texture]
	var sizes: Array = []
	var total := 0
	var names := Array(DirAccess.get_files_at(DIR))
	names.sort()
	for f in names:
		if not f.ends_with(".import"):
			continue
		var imp := DIR.path_join(f)
		var cfg := ConfigFile.new()
		if cfg.load(imp) != OK:
			continue
		var files := [imp]
		for k in cfg.get_section_keys("remap"):
			if k.begins_with("path"):
				files.append(cfg.get_value("remap", k))
		var size := 0
		for p in files:
			size += FileAccess.get_size(p) if FileAccess.file_exists(p) else 0
		groups.append(files)
		sizes.append(size)
		total += size
	var ok := out != "" and groups.size() > 0
	var part := 0
	var done := 0
	var packer: PCKPacker = null
	for i in groups.size():
		if packer == null or (done >= total * part / Q3.TEXTURE_PACKS and part < Q3.TEXTURE_PACKS):
			if packer:
				ok = ok and packer.flush() == OK
			part += 1
			packer = PCKPacker.new()
			ok = ok and packer.pck_start(out.path_join("textures-%d.pck" % part)) == OK
		for p in groups[i]:
			if FileAccess.file_exists(p):
				ok = ok and packer.add_file(p, ProjectSettings.globalize_path(p)) == OK
		done += sizes[i]
	ok = ok and packer != null and packer.flush() == OK and part == Q3.TEXTURE_PACKS
	print("TEXTURE PACKS %s: %d textures, %.0f MB in %d packs -> %s" % ["ok" if ok else "FAILED", groups.size(), total / 1e6, part, out])
	get_tree().quit(0 if ok else 1)
