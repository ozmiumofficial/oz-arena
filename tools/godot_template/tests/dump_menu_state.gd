extends Node
## Writes the web menu's init payload (settings schema and values, modes, maps, bots ...) to a
## JSON file so the menu can run in a normal browser with a mock bridge (npm run dev).
##   godot --headless --path . res://tests/dump_menu_state.tscn -- out=<file>
## tools/build_webui.py runs it into webui/public/q3/dev_state.json. Settings come from a
## scratch path so the dump shows defaults, not this machine's choices; the profile is a fresh one,
## plus "profile_demo", a mid-career profile the preview shows with ?demo (the card editor then has
## a mix of earned and locked parts).

const DEMO := {"xp": 47800, "campaign": {"q3dm0": 3, "q3dm1": 3, "q3dm2": 4, "q3dm3": 3, "q3tourney1": 3},
	"stats": {"matches": 212, "wins": 81, "frags": 3120, "deaths": 2410, "playtime": 61200, "duel_wins": 9,
		"ranked_matches": 46, "ranked_wins": 27, "ranked_rating": 1642, "ranked_peak": 1710,
		"medal": {"excellent": 140, "impressive": 61, "gauntlet": 23, "capture": 12, "assist": 5, "defend": 9, "perfect": 3},
		"frags_weapon": {"gauntlet": 40, "machinegun": 310, "shotgun": 280, "grenade": 160, "rocket": 1100, "lightning": 240,
			"railgun": 820, "plasma": 150, "bfg": 20},
		"wins_map": {"q3dm17": 14, "q3dm6": 6, "q3tourney2": 3, "q3dm7": 2}, "wins_mode": {"ffa": 60, "duel": 9, "tdm": 12},
		"matches_model": {"sarge": 120, "xaero": 30, "visor": 26}, "matches_skin": {"sarge/krusade": 12},
		"pickups": {"quad": 55, "mega": 80}, "deaths_by": {"lava": 12}, "best": {"speed": 812, "rail_accuracy": 54}},
	"card": {"banner": "map_q3dm17", "emblem": "head_sarge_krusade", "frame": "frame_gothic_stone", "title": "title_rocket_scientist",
		"accent": "accent_bronze", "taunt": "taunt_sarge", "rail": "rail_red", "stats": ["frags", "kd", "excellents"],
		"badges": ["medal_excellent_100", "matches_100"]}}


func _ready() -> void:
	# the dump is committed (webui/public): never this PC's published maps (their paths, their images)
	Q3MapPublish.path = "user://dump_menu_state/none.json"
	Session.refresh_custom_maps()
	var out := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			out = a.substr(4)
	Settings.path = "user://settings_dump.cfg"
	Settings.reset_all()
	var demo: Dictionary = DEMO.duplicate(true)
	demo["level"] = int(Q3Cards.level_for(demo["xp"])["level"])
	Q3Profile._data = demo
	var profile_demo := Q3Profile.menu_payload()
	Q3Profile._data = Q3Cards.new_profile()
	Q3Profile._data["card"] = Q3Cards.default_card()
	var b := Q3MenuBridge.new()
	var payload := b.init_payload()
	payload["profile_demo"] = profile_demo
	Q3Profile._data = {}   # nothing here saves; the next data() reads user://profile.json again
	payload["continue"] = ""
	var f := FileAccess.open(out, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(payload, " "))
		print("menu state: ", out)
	else:
		push_error("menu state: can't write " + out)
	DirAccess.remove_absolute(ProjectSettings.globalize_path("user://settings_dump.cfg"))
	get_tree().quit(0 if f else 1)
