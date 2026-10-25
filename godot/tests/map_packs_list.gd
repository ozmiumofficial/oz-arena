extends Node
## Prints what the menu gets for the Maps folder: Q3MapPacks.dir() and every "pack" entry in Session.maps.
##   godot --headless --path . res://tests/map_packs_list.tscn


func _ready() -> void:
	print("Maps folder: %s (exists %s)" % [Q3MapPacks.dir(), DirAccess.dir_exists_absolute(Q3MapPacks.dir())])
	print("pk3s: %s" % [Q3MapPacks.pk3s()])
	for m in Session.maps:
		if String(m.get("special", "")) == "pack":
			print("pack map: %s %s types %s levelshot %d chars" % [m["map"], m["name"], m["types"], String(m["levelshot"]).length()])
	print("MAP PACKS listed %d" % Session.maps.filter(func(m): return String(m.get("special", "")) == "pack").size())
	get_tree().quit()
