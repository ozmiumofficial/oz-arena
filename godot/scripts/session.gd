extends Node
## Autoload "Session": which top-level scene is up and the match setup handed between them.
## Splash -> Main menu -> Gameplay (with a Q3MatchSetup) or Map editor, and back.
## One scene loads at a time; Q3, Settings, UiSound and Session live across scene changes.
## The Online autoload (friends, parties) follows the scene changes for presence.

const SPLASH := "res://scenes/splash/splash.tscn"
const MAIN_MENU := "res://scenes/main_menu/main_menu.tscn"
const GAMEPLAY := "res://scenes/gameplay/gameplay.tscn"
const MAP_EDITOR := "res://scenes/map_editor/map_editor.tscn"
const DEDICATED := "res://scenes/dedicated/dedicated.tscn"
const MODES := ["ffa", "duel", "tdm", "ctf", "clan_arena", "freeze_tag", "instagib", "race"]
const CINEMATIC := "res://scenes/cinematic/cinematic.tscn"
const LAST_SETUP := "user://last_match.json"
const INTRO_PLAYED := "user://intro_played"

var setup: Q3MatchSetup              ## the match Gameplay runs (null in the menu)
var modifiers: Array = []            ## game/modifiers.json
var maps: Array = []                 ## library/ui/maps.json (arenas.txt + .arena files), the Maps folder's pk3s, then published maps
var bots: Array = []                 ## scripts/bots.txt
var race_queue: Array = []           ## Race playlist: the courses to run after the current one (menu_bridge race_queue)
var campaign: Q3Campaign             ## Quake 3's single-player tiers and this player's progress
var cinematics: Array = []           ## library/cinematics/cinematics.json, q3_ui's Cinematics menu order
var cinematic_queue: PackedStringArray = []   ## what the cinematic scene plays before cinematic_then
var cinematic_then := MAIN_MENU
## A map editor play-test (F5): {doc, models, map, path, camera, selection, tool}. Gameplay draws
## the map from doc; F6 brings the editor back as it was (undo history included).
var playtest: Dictionary = {}
## A replay Gameplay plays instead of a match: {path, at (ms into it, optional), back (the menu page
## to return to)}. Empty for a match.
var replay: Dictionary = {}
## A match Gameplay joins over the network instead of running one: {transport: Q3Transport, player: {...},
## snaps, maxpackets}. Empty for a match or a replay.
var join: Dictionary = {}
## The matchmaker's match_found for the match being played (host or joiner), so Gameplay joins it, reports its result, and Online
## can tell the menu what happened. Empty for every other match.
var matchmade: Dictionary = {}
## `-- dedicated=<config.json>`: this process is a dedicated server (scenes/dedicated), no menu, no window needed.
var dedicated := ""
## The page the main menu opens on next ("" the main page): leaving a replay goes back to Replays.
var menu_page := ""
var dry_run := false               ## tests: record scene requests instead of changing scenes
var last_request := ""
var _modes: Dictionary = {}
var _arenas: Array = []


func _ready() -> void:
	get_tree().auto_accept_quit = false   # closing the window goes through quit_game too
	_clip_args()
	for id in MODES:
		var m := load("res://game/modes/%s.tres" % id) as Q3GameMode
		if m:
			_modes[id] = m
	modifiers = Q3.load_json("res://game/modifiers.json").get("modifiers", [])
	var mj: Dictionary = Q3.load_json(Q3.LIB + "ui/maps.json")
	_arenas = mj.get("maps", [])
	bots = mj.get("bots", [])
	refresh_custom_maps()
	campaign = Q3Campaign.new()
	if FileAccess.file_exists(Q3.LIB + "cinematics/cinematics.json"):
		cinematics = Q3.load_json(Q3.LIB + "cinematics/cinematics.json").get("cinematics", [])


## maps is Quake 3's arenas, then the maps dropped into the Maps folder (Q3MapPacks, special "pack":
## found at startup, nothing rebuilt), then the maps published from the map editor (Q3MapPublish),
## which carry special "custom" and the document Gameplay draws them from.
func refresh_custom_maps() -> void:
	var known := {}
	for m in _arenas:
		known[m["map"]] = true
	maps = _arenas + Q3MapPacks.list(known) + Q3MapPublish.list()


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
	var out := maps.filter(func(x): return m != null and m.accepts_map(x))
	if mode_id == "duel":   # the duel pool leads the list; the other tourney maps follow
		var pool := out.filter(func(x): return in_duel_pool(String(x["map"])))
		pool.append_array(out.filter(func(x): return not in_duel_pool(String(x["map"]))))
		return pool
	return out


## The duel pool (DUEL_MAPS_PLAN.md): all 15 entries, built or not.
func duel_pool_entries() -> Array:
	return Q3.load_json("res://game/duel_pool.json").get("maps", [])


## The pool's maps that exist and can be played, in pool order. Ranked duels use only these.
func ranked_maps() -> Array:
	var out: Array = []
	for e in duel_pool_entries():
		var info := map_info(String(e.get("map", "")))
		if not info.is_empty():
			out.append(info)
	return out


func in_duel_pool(map: String) -> bool:
	for e in duel_pool_entries():
		if String(e.get("map", "")) == map and map != "":
			return true
	return false


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
	replay = {}
	join = {}
	matchmade = s.matchmade
	Diagnostics.set_context("match", s.summary().replace("\n", "; "))
	# a play-test isn't the menu's "continue": its map and drawing exist only for the editor
	var f := FileAccess.open(LAST_SETUP, FileAccess.WRITE) if s.source != "editor" else null
	if f:
		f.store_string(JSON.stringify(s.to_dict(), " "))
	_go(GAMEPLAY)
	return errs


## Joins someone's match over the network (Q3NetJoin made the transport): Gameplay starts as a client.
func join_match(transport: Q3Transport, player := {}) -> void:
	setup = null
	replay = {}
	playtest = {}
	join = {"transport": transport, "player": player}
	if not matchmade.is_empty():
		var me := String(Online.client.state.get("me", {}).get("id", ""))
		for p in matchmade.get("players", []):
			if String(p.get("id", "")) == me:
				join["teampref"] = {1: "red", 2: "blue"}.get(int(p.get("team", 0)), "auto")
	Diagnostics.set_context("match", "online")
	_go(GAMEPLAY)


## Plays a replay file in the Gameplay scene (REPLAY_PLAN.md R2). `at`: ms into the match to start
## from; `back`: the menu page to return to ("replays", or "" for the main menu).
func watch_replay(path: String, at := 0, back := "replays") -> void:
	setup = null
	replay = {"path": path, "at": at, "back": back}
	Diagnostics.set_context("match", "replay " + path.get_file())
	_go(GAMEPLAY)


## Leaves a replay for the menu page it was opened from (menu_page; the menu opens there).
func leave_replay() -> void:
	menu_page = String(replay.get("back", ""))
	to_main_menu()


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
## A clip render (Q3ClipExport starts the game again under Movie Maker with clip=<replay>
## clip_from= clip_to= [clip_cam= clip_watch= clip_hud= clip_free=x,y,z,pitch,yaw
## clip_orbit=yaw,pitch clip_tags=<names><outlines>]): Gameplay plays that range and quits.
func _clip_args() -> void:
	var a := {}
	for arg in OS.get_cmdline_args() + OS.get_cmdline_user_args():
		var kv := String(arg).split("=", true, 1)
		if kv.size() == 2 and kv[0].begins_with("clip"):
			a[kv[0]] = kv[1]
	if not a.has("clip"):
		return
	var clip := {"from": int(a.get("clip_from", "0")), "to": int(a.get("clip_to", "0")), "cam": String(a.get("clip_cam", "first")),
		"watch": int(a.get("clip_watch", "-1")), "hud": a.get("clip_hud", "1") == "1",
		"names": String(a.get("clip_tags", "00")).begins_with("1"), "outlines": String(a.get("clip_tags", "00")).ends_with("1")}
	var fr := String(a.get("clip_free", "")).split_floats(",")
	if fr.size() == 5:
		clip["pos"] = Vector3(fr[0], fr[1], fr[2])
		clip["angles"] = Vector3(fr[3], fr[4], 0.0)
	var orb := String(a.get("clip_orbit", "")).split_floats(",")
	if orb.size() == 2:
		clip["orbit"] = Vector2(orb[0], orb[1])
	replay = {"path": String(a["clip"]), "at": 0, "back": "", "clip": clip}


func after_splash() -> void:
	if replay.has("clip"):
		_go(GAMEPLAY)   # a clip render: straight to the replay
		return
	if dedicated_arg() != "":
		dedicated = dedicated_arg()
		_go(DEDICATED)
		return
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
	playtest = {}
	_go(MAP_EDITOR)


## F5 in the map editor: plays the compiled map (state["map"], in fs_homepath) in Gameplay as a
## free-for-all without limits, with `bots` of id's bots when the map has a bot area file.
func play_test(state: Dictionary, bots: int) -> PackedStringArray:
	var race := bool(state.get("race", false))   # a course (a start and a finish line) is run, with the timer: no cheats, no bots
	var s := new_setup("race" if race else "ffa", String(state["map"]))
	s.source = "editor"
	if race:
		s.rules = {}
	else:
		s.rules = {"fraglimit": 0, "timelimit": 0, "warmup": 0}
		s.set_meta("devmap", true)   # cheats on: the start at the camera (edpos), and noclip / give for testing
	var names := ["Sarge", "Visor", "Major", "Crash", "Xaero", "Anarki", "Keel", "Hunter"]
	for i in clampi(0 if race else bots, 0, names.size()):
		s.bots.append({"name": names[i], "skill": 3, "team": ""})
	playtest = state
	var errs := start_match(s)
	if errs.size():
		playtest = {}   # nothing started; the editor stays as it is
	return errs


## F6 in a play-test (or leaving it): back to the map editor, which picks up Session.playtest.
func back_to_editor() -> void:
	setup = null
	Diagnostics.set_context("match", null)
	_go(MAP_EDITOR)


## Quits the game. A live web view (godot_wry) keeps the process alive after Godot's own exit,
## leaving a frozen window, so every one is freed first; settings are saved and the match stopped.
func quit_game() -> void:
	Settings.save()
	if Q3UpdateInstall.pending():   # a downloaded update installs once the game has exited
		Q3UpdateInstall.instance().apply(false)
	if ClassDB.class_exists("Q3Server") and Q3Server.running():
		Q3Server.exec_now("killserver")
	for w in get_tree().root.find_children("*", "WebView", true, false):
		w.queue_free()
	await get_tree().process_frame
	await get_tree().process_frame
	get_tree().quit()


## The config of `-- dedicated=<config.json>` on the command line, "" without: this process is a dedicated server.
func dedicated_arg() -> String:
	for a in OS.get_cmdline_args() + OS.get_cmdline_user_args():
		if String(a).begins_with("dedicated="):
			return String(a).get_slice("=", 1)
	return ""


func _notification(what: int) -> void:
	if what == NOTIFICATION_WM_CLOSE_REQUEST:
		# a dedicated server's window: closing it stops the server cleanly (unlisted, status removed)
		var top := get_tree().current_scene
		if dedicated != "" and top is Q3Dedicated:
			(top as Q3Dedicated).stop_now()
			return
		quit_game()


func to_main_menu() -> void:
	setup = null
	matchmade = {}
	playtest = {}
	replay = {}
	join = {}
	refresh_custom_maps()   # the editor may have published (or the files gone) since
	Diagnostics.set_context("match", null)
	Diagnostics.set_context("editor_map", null)
	_go(MAIN_MENU)


func _go(path: String) -> void:
	last_request = path
	Online.scene_changed(path)
	if dry_run:
		return
	for l in get_tree().get_nodes_in_group("web_layers"):   # the keyboard back to the game window first
		l.release_focus()
	get_tree().change_scene_to_file.call_deferred(path)
