extends Node
## Writes the web menu's init payload (settings schema and values, modes, maps, bots ...) to a
## JSON file so the menu can run in a normal browser with a mock bridge (npm run dev).
##   godot --headless --path . res://tests/dump_menu_state.tscn -- out=<file>
## tools/build_webui.py runs it into webui/public/q3/dev_state.json. Settings come from a
## scratch path so the dump shows defaults, not this machine's choices.


func _ready() -> void:
	var out := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			out = a.substr(4)
	Settings.path = "user://settings_dump.cfg"
	Settings.reset_all()
	var b := Q3MenuBridge.new()
	var payload := b.init_payload()
	payload["continue"] = ""
	var f := FileAccess.open(out, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(payload, " "))
		print("menu state: ", out)
	else:
		push_error("menu state: can't write " + out)
	DirAccess.remove_absolute(ProjectSettings.globalize_path("user://settings_dump.cfg"))
	get_tree().quit(0 if f else 1)
