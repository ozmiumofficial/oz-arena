extends Node
## Builds library/maps/<map>/<map>.tscn for every imported map (or -- maps=q3dm17,q3dm1).
##   godot --headless --path godot res://tools/build_map_scenes.tscn [-- maps=q3dm17]
## Run --import first so the .glb files are imported. Prints one line per map, then quits.


func _ready() -> void:
	var want: PackedStringArray = []
	for a in OS.get_cmdline_user_args():
		if a.begins_with("maps="):
			want = a.trim_prefix("maps=").split(",", false)
	if want.is_empty():
		for d in DirAccess.get_directories_at("res://library/maps"):
			if FileAccess.file_exists("res://library/maps/%s/%s.json" % [d, d]):
				want.append(d)
	var failed := 0
	for m in want:
		var t := Time.get_ticks_msec()
		var err := Q3MapBuilder.build(m)
		print("%-16s %s (%d ms)" % [m, "ok" if err == OK else error_string(err), Time.get_ticks_msec() - t])
		if err != OK:
			failed += 1
	print("MAP SCENES %d/%d built" % [want.size() - failed, want.size()])
	get_tree().quit(1 if failed else 0)
