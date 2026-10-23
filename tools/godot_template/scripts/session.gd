extends Node
## Autoload "Session": which top-level scene is up and the match setup handed between them.
## Splash -> Main menu -> Gameplay (with a Q3MatchSetup) or Map editor, and back.
## One scene loads at a time; Q3, Settings, UiSound and Session live across scene changes.
## The Online autoload (friends, parties) follows the scene changes for presence.

const SPLASH := "res://scenes/splash/splash.tscn"
const MAIN_MENU := "res://scenes/main_menu/main_menu.tscn"
const GAMEPLAY := "res://scenes/gameplay/gameplay.tscn"
const MAP_EDITOR := "res://scenes/map_editor/map_editor.tscn"
const MODES := ["ffa", "duel", "tdm", "ctf", "clan_arena", "freeze_tag", "instagib"]
const CINEMATIC := "res://scenes/cinematic/cinematic.tscn"
const LAST_SETUP := "user://last_match.json"
const INTRO_PLAYED := "user://intro_played"

var setup: Q3MatchSetup              ## the match Gameplay runs (null in the menu)
var modifiers: Array = []            ## game/modifiers.json
var maps: Array = []                 ## library/ui/maps.json: arenas.txt + .arena files
var bots: Array = []                 ## scripts/bots.txt
var campaign: Q3Campaign             ## Quake 3's single-player tiers and this player's progress
var cinematics: Array = []           ## library/cinematics/cinematics.json, q3_ui's Cinematics menu order
var cinematic_queue: PackedStringArray = []   ## what the cinematic scene plays before cinematic_then
var cinematic_then := MAIN_MENU
var dry_run := false                 ## tests: record scene requests instead of changing scenes
var last_request := ""
var _modes: Dictionary = {}


func _ready() -> void:
	get_tree().auto_accept_quit = false   # closing the window goes through quit_game too
	for id in MODES:
		var m := load("res://game/modes/%s.tres" % id) as Q3GameMode
		if m:
			_modes[id] = m
	modifiers = Q3.load_json("res://game/modifiers.json").get("modifiers", [])
	var mj: Dictionary = Q3.load_json(Q3.LIB + "ui/maps.json")
	maps = mj.get("maps", [])
	bots = mj.get("bots", [])
	campaign = Q3Campaign.new()
	if FileAccess.file_exists(Q3.LIB + "cinematics/cinematics.json"):
		cinematics = Q3.load_json(Q3.LIB + "cinematics/cinematics.json").get("cinematics", [])


func mode(id: String) -> Q3GameMode:
	return _modes.get(id)


func mode_ids() -> PackedStringArray:
	return PackedStringArray(_modes.keys())


func map_info(map: String) -> Dictionary:
	for m in maps:
		if m["map"] == map:
			return m
	return {}


func maps_for(mode_id: String) -> Array:
	var m := mode(mode_id)
	return maps.filter(func(x): return m != null and m.accepts_map(x))


func modifier_label(id: String, value: Variant) -> String:
	for d in modifiers:
		if d["id"] == id:
			for o in d["options"]:
				if o[0] == value:
					return String(o[1])
	return str(value)


## A setup with the Profile's player filled in.
func new_setup(mode_id := "ffa", map := "") -> Q3MatchSetup:
	var s := Q3MatchSetup.new()
	s.mode = mode_id
	var pool := maps_for(mode_id)
	s.map = map if map != "" else (String(pool[0]["map"]) if pool.size() else "q3dm17")
	s.player = {"name": Settings.get_value("profile/name"), "model": Settings.get_value("profile/model"),
		"skin": Settings.get_value("profile/skin"), "team_color": Settings.get_value("profile/team_color").to_html()}
	return s


## Validates, remembers it for "continue", and loads Gameplay. Returns the problems if it can't start.
func start_match(s: Q3MatchSetup) -> PackedStringArray:
	var errs := s.validate()
	if errs.size():
		return errs
	setup = s
	var f := FileAccess.open(LAST_SETUP, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(s.to_dict(), " "))
	_go(GAMEPLAY)
	return errs


func has_last_setup() -> bool:
	return FileAccess.file_exists(LAST_SETUP)


func last_setup() -> Q3MatchSetup:
	if not has_last_setup():
		return null
	var d: Variant = Q3.load_json(LAST_SETUP)
	return Q3MatchSetup.from_dict(d) if d is Dictionary and not d.is_empty() else null


## q3_ui's rules: a tier movie once the tier before it is won (tier1 after Training), the ending
## once the final is won.
func cinematic_unlocked(id: String) -> bool:
	for c in cinematics:
		if c["id"] != id:
			continue
		var unlock := String(c.get("unlock", "always"))
		if unlock == "always":
			return true
		if unlock == "final":
			return campaign.tier_complete(campaign.tiers.size() - 1)
		if unlock.begins_with("tier:"):
			var i := int(unlock.get_slice(":", 1)) - 1
			return i < campaign.tiers.size() and campaign.tier_complete(i)
	return false


## Plays cinematics between scenes (the intro at launch, a tier movie after a campaign match).
func play_cinematics(ids: PackedStringArray, then := MAIN_MENU) -> void:
	cinematic_queue = ids
	cinematic_then = then
	_go(CINEMATIC)


## After the splash: Quake 3's id logo and intro on the first launch (Settings: gameplay/intro).
## Launched with `map=<map> [mode=ffa] [bots=<n>] [skill=1-5]` (Quake 3's +map), it goes
## straight into that match instead: the arena's own bots first, then the classic line-up.
func after_splash() -> void:
	var launch := {}
	# after "--", or bare: an exported game's console wrapper passes them on without the "--"
	for a in OS.get_cmdline_args() + OS.get_cmdline_user_args():
		var kv := String(a).split("=", true, 1)
		if kv.size() == 2 and kv[0] in ["map", "mode", "bots", "skill"]:
			launch[kv[0]] = kv[1]
	if launch.has("map"):
		var s := new_setup(String(launch.get("mode", "ffa")), String(launch["map"]))
		s.source = "launch"
		var names: Array = map_info(s.map).get("bots", []).duplicate()
		names.append_array(["sarge", "visor", "major", "crash", "xaero", "anarki", "keel"])
		s.bots = []
		for i in clampi(int(launch.get("bots", "3")), 0, 15):
			s.bots.append({"name": String(names[i % names.size()]).capitalize(), "skill": clampi(int(launch.get("skill", "3")), 1, 5), "team": ""})
		if start_match(s).is_empty():
			return
	var mode := String(Settings.get_value("gameplay/intro"))
	var first := not FileAccess.file_exists(INTRO_PLAYED)
	if (mode == "always" or (mode == "first" and first)) and Q3CinematicPlayer.exists("intro"):
		FileAccess.open(INTRO_PLAYED, FileAccess.WRITE)
		play_cinematics(PackedStringArray(["idlogo", "intro"]))
	else:
		to_main_menu()


func open_map_editor() -> void:
	_go(MAP_EDITOR)


## Quits the game. A live web view (godot_wry) keeps the process alive after Godot's own exit,
## leaving a frozen window, so every one is freed first; settings are saved and the match stopped.
func quit_game() -> void:
	Settings.save()
	if ClassDB.class_exists("Q3Server") and Q3Server.running():
		Q3Server.exec_now("killserver")
	for w in get_tree().root.find_children("*", "WebView", true, false):
		w.queue_free()
	await get_tree().process_frame
	await get_tree().process_frame
	get_tree().quit()


func _notification(what: int) -> void:
	if what == NOTIFICATION_WM_CLOSE_REQUEST:
		quit_game()


func to_main_menu() -> void:
	setup = null
	_go(MAIN_MENU)


func _go(path: String) -> void:
	last_request = path
	Online.scene_changed(path)
	if dry_run:
		return
	get_tree().change_scene_to_file.call_deferred(path)
