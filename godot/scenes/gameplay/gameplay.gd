class_name Q3Gameplay
extends Node3D
## The Gameplay scene: one match of id's game code (q3core) under the remastered library.
##   1. Session.setup (or, run on its own, FFA on q3dm17 with three bots) becomes game cvars,
##      "map <map>" and "addbot" commands; the human joins as a local client.
##   2. Every frame: mouse and keys become a usercmd (cl_input.c), the server runs it at once
##      (so the view has no latency), then the server advances its 125 Hz frames.
##   3. Q3Client draws the snapshot; the camera sits at the live playerState's eye.
## Escape opens the pause menu: the web menu (webui/, PauseApp) in a hidden web view made at the
## start, shown over the paused match with Resume, Crosshair (edited live), Settings, Restart,
## Leave and Quit. Without a web view (headless) a small native panel stands in.
## T and Y open the chat (Q3HudChat): the line goes to the game as "say" / "say_team", and while
## typing the usercmd carries only BUTTON_TALK, as cl_input.c sends it with a key catcher up (the
## game stands you still and others see the talk balloon).
## A controller plays too: one stick moves, the other looks (Settings > Controls).

const E := preload("res://scripts/game/q3_enums.gd")
## weapons2/<dir> per weapon number, for the hand tags in library/props/view_weapons.json
const WEAPON_DIRS := ["", "gauntlet", "machinegun", "shotgun", "grenadel", "rocketl", "lightning", "railgun",
	"plasma", "bfg", "grapple"]
const LAND_DEFLECT_TIME := 150.0
const LAND_RETURN_TIME := 300.0
## g_gametype for the setup's mode (bg_public.h gametype_t). Clan Arena, Freeze Tag and Instagib
## play as their base types until their rules land in the game module.
const GAMETYPES := Q3MatchSetup.GAMETYPES

var setup: Q3MatchSetup
var me := -1
var map_scene: Q3Map
var client: Q3Client
var cam: Camera3D
var hud: Q3Hud

var _view := Vector3.ZERO          ## client view angles (pitch, yaw, roll), degrees, cl.viewangles
var _client_time := 0              ## usercmd serverTime (ms)
var _acc := 0.0
var _weapon := 0                   ## the weapon the client asks for (cmd.weapon)
var _error := ""
var scoreboard: Q3Scoreboard
var _board_t := 0.0
var _ended := false
var _results: Q3ResultsScreen
var music: Q3Music              ## the map's soundtrack (CS_MUSIC)
const ZOOM_FOV := 22.5             ## cg_zoomFov
const ZOOM_TIME := 150.0           ## ms
var _zoomed := false               ## cg.zoomed (+zoom)
var _zoom_time := -100000.0
var _gun: Node3D               ## the first-person weapon (cg_weapons.c CG_AddViewWeapon)
var _gun_weapon := -1
var _gun_fx: Q3WeaponFx
var _hand_tags: Dictionary = {}    ## weapon dir -> every frame of its hand model's tag_weapon
var _torso_anim := -1              ## the local torso animation the hand frames follow
var _torso_start := 0.0
var _hand_from: Array = []         ## the pose a new torso animation eases from (lerpFrame oldFrame)
var _hand_last: Array = []
var _guns: Dictionary = {}         ## weapon -> [prop, Q3WeaponFx], made once (CG_RegisterWeapon)
var _land_change := 0.0            ## cg.landChange / cg.landTime: the landing dip
var _land_time := -100000.0
var _step_change := 0.0            ## cg.stepChange / cg.stepTime: the eye easing up a stair
var _step_time := -100000.0
const STEP_TIME := 200.0
const MAX_STEP_CHANGE := 32.0
var _duck_change := 0.0            ## cg.duckChange / cg.duckTime: the eye easing to a crouch and back
var _duck_time := -100000.0
var _last_viewheight := 0
var _last_view_client := -1
const DUCK_TIME := 100.0
var test_input := false           ## tests: fire without a captured mouse (headless has none)
var _paused := false
var _pause_panel: Node             ## the native stand-in when there's no web view
var pause_bridge: Q3MenuBridge     ## the pause menu's protocol (context "pause")
var pause_layer: Q3WebLayer        ## the pause menu's web view, hidden while playing
var _ready_sent := false           ## ready-up toggled from the pause menu
## usercmd actions held when the chat closed, ignored until let go: the Enter that sent the line
## isn't Use item (Quake 3's keys pressed in message mode never reach their binds either)
var _chat_held: Array[StringName] = []
const USERCMD_ACTIONS: Array[StringName] = [&"q3_walk", &"q3_forward", &"q3_back", &"q3_moveright", &"q3_moveleft",
	&"q3_jump", &"q3_crouch", &"q3_attack", &"q3_use_item", &"q3_gesture"]
var _mouse := Vector2.ZERO         ## mouse counts since the last frame (CL_MouseMove reads them per frame)
var _pad_move := Vector2.ZERO      ## the controller's move stick, -1..1 past the deadzone
var _crouch_on := false            ## Controls > Toggle crouch
## HUD > Item timers: the major items, in the order they're listed
const TIMED_ITEMS := ["item_armor_body", "item_armor_combat", "item_health_mega", "item_quad", "item_enviro",
	"item_haste", "item_invis", "item_regen", "item_flight"]
var _map_items: Dictionary = {}    ## the map's own item entities (not ones dropped later)
var _timers_t := 0.0
var match_stats := Q3MatchStats.new()   ## your match for the scoreboard's left panel
var recorder: Q3ReplayRecorder     ## Gameplay > Record replays: this match, written as it plays
var replay_path := ""              ## the finished recording (the results screen's Watch replay)
const TEST_REPLAYS := "user://test_replays"
var _last_standings: Array = []    ## the recording's trailer when the match is quit (standings once a second)
var _standings_t := 0.0
var feed: Q3Feed = Q3LiveFeed.shared()   ## where the match comes from: the server here, or a replay
var replay_feed: Q3ReplayFeed      ## playing a replay (Session.replay) instead of a match
var _reported := false              ## a matchmade ranked match's result was sent to the hub
var net_feed: Q3NetFeed            ## playing on someone else's server (Session.join): the match comes over the network
var _join_loaded := false
var _preloaded_map: Q3Map      ## loaded at the gamestate, added by _build_view
var host_net: Q3HostNet            ## hosting for other players (setup.server_type listen/dedicated and Online): their WebRTC connections
var _joining := false              ## connecting: the view is built once the gamestate is in
var _join_layer: CanvasLayer
var _join_label: Label
var replay_playing := true
var replay_speed := 1.0
## Replays: whose view is shown (me) and how: "first" (their eyes, HUD and gun), "chase" (behind
## them), "free" (fly anywhere), "orbit" (circle them), "cinematic" (Q3CineCam's shots of them)
var cam_mode := "first"
var viewer: Q3ReplayViewer
var _replay_cards: Dictionary = {} ## client -> {card, level} as the recording kept them


func _ready() -> void:
	if not InputMap.has_action("q3_ready"):
		InputMap.add_action("q3_ready")
		var f3 := InputEventKey.new()
		f3.physical_keycode = KEY_F3
		InputMap.action_add_event("q3_ready", f3)
	for v in [["q3_vote_yes", KEY_F1], ["q3_vote_no", KEY_F2]]:
		if not InputMap.has_action(v[0]):
			InputMap.add_action(v[0])
			var vk := InputEventKey.new()
			vk.physical_keycode = v[1]
			InputMap.action_add_event(v[0], vk)
	setup = Session.setup
	if not Session.replay.is_empty():
		_error = _start_replay(String(Session.replay.get("path", "")))
	elif not Session.join.is_empty():
		_error = _start_join()
		if _error == "":
			return   # the view is built when the server's gamestate arrives (_finish_join)
		setup = Session.new_setup("ffa", "q3dm17")   # the error screen's view
	else:
		if setup == null:
			setup = Session.new_setup("ffa", "q3dm17")
			setup.bots = [{"name": "Sarge", "skill": 3, "team": ""}, {"name": "Xaero", "skill": 3, "team": ""},
				{"name": "Crash", "skill": 3, "team": ""}]
		_error = _start_match()
	_build_view()
	if _error != "" and _rendering_clip():
		printerr("clip: ", _error)
		_end_clip(1)   # no one is there to read the message
		return
	if _error != "":
		var what := "play the replay" if replay_feed else "start the match"
		hud.fatal("Couldn't %s:\n%s\n\nEscape: back to the %s" % [what, _error, "editor" if setup.source == "editor" else "menu"])
		return
	if replay_feed:
		_begin_replay()
		return
	if setup.source == "editor":
		hud.notify("Play-test: F6 goes back to the editor, F7 shows the entities")
		_start_at = Array(Session.playtest.get("start", []))
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


func _start_match() -> String:
	var err := Q3Host.start()
	if err != "":
		return err
	var campaign := setup.source in ["campaign", "training"]
	var cvars := setup.game_cvars(_campaign_skill())
	for k in cvars:
		Q3Server.set_cvar(k, str(cvars[k]))
	# the campaign starts like q3_ui's: "spmap" keeps g_gametype single player (plain "map" turns it
	# back into free-for-all, sv_ccmds.c SV_Map_f), so the game spawns the arena's bots itself.
	# devmap (cheats on) only when a test asks for it: setup.set_meta("devmap", true)
	var map_cmd := ("sp" if campaign else "") + ("devmap " if setup.has_meta("devmap") else "map ")
	if Q3Server.exec_now(map_cmd + setup.map) != 0 or not Q3Server.running():
		return "map %s: %s" % [setup.map, Q3Server.error()]
	if not campaign:   # in single player the game spawns the arena's bots (G_SpawnBots, arenas.txt)
		for b in setup.bots:
			Q3Server.exec("addbot %s %d %s" % [String(b["name"]).to_lower(), int(b.get("skill", 3)), String(b.get("team", ""))])
	Q3Server.frame(Q3Host.FRAME_MSEC)
	var p := setup.player.duplicate()
	if p.get("team_pref", "") != "":
		p["teampref"] = p["team_pref"]
	# g_session.c G_InitSessionData: the first team comes from teampref; without one, a team game
	# starts you on team spectator. "auto" is PickTeam (the smaller team), "spectator" watches.
	p["teampref"] = "spectator" if setup.spectate else String(p.get("teampref", "auto"))
	if not setup.matchmade.is_empty() and int(setup.matchmade.get("team", 0)) > 0:
		p["teampref"] = {1: "red", 2: "blue"}.get(int(setup.matchmade["team"]), "auto")   # the matchmaker's balanced teams
	if setup.spectate:
		Q3Server.set_cvar("g_readyUp", "0")   # no human would ever ready up: the warmup would never end
	spec_view = String(Settings.get_value("gameplay/spec_view"))
	director = bool(Settings.get_value("gameplay/spec_director"))
	_director_forced = spec_view == "cinematic" and not director
	director = director or spec_view == "cinematic"
	me = Q3Server.connect_local(Q3Host.userinfo(p))
	if me < 0:
		return "joining: " + Q3Server.error()
	_client_time = feed.time()
	var ps := feed.player_state(me)
	if not ps.is_empty():
		_weapon = int(ps["weapon"])
	_start_recording()
	_start_hosting()
	return ""


## A listen server lists the match with the hub and lets other players join over WebRTC (Q3HostNet). The
## host plays through the server's own local client, as always; Session.setup.online carries the listing
## (name, password, ranked, max).
func _start_hosting() -> void:
	if setup.server_type not in ["listen", "dedicated"] or setup.source == "editor" or not Online.enabled():
		return
	var o: Dictionary = setup.online
	var rules := setup.effective_rules()
	host_net = Q3HostNet.new()
	add_child(host_net)
	var expected: int = setup.matchmade.get("players", []).size()
	host_net.start(Online.client, {"name": String(o.get("name", "%s's match" % String(setup.player.get("name", "Player")))),
		"mode": setup.mode, "map": setup.map, "max": expected if expected > 0 else int(rules.get("max_players", 16)), "match": String(setup.matchmade.get("match", "")), "password": String(o.get("password", "")),
		"ranked": bool(o.get("ranked", false)), "version": Online.game_version()})
	# friends see the match in their list and can join it from there (join "match:<host id>")
	var me_id := String(Online.client.state.get("me", {}).get("id", ""))
	if not setup.matchmade.is_empty():
		Online.match_joined(String(setup.matchmade.get("match", "")))
	if me_id != "":
		Online.set_presence("match", "Hosting " + String(host_net.listing.get("name", "a match")), "match:" + me_id)


## Gameplay > Record replays: the whole match from here on (Q3ReplayRecorder). Map editor
## play-tests aren't recorded: their map exists only in the editor's document.
func _start_recording() -> void:
	if not Settings.get_value("gameplay/record_replays") or setup.source == "editor":
		return
	recorder = Q3ReplayRecorder.new()
	var m := setup.game_mode()
	# a match a test runs (the scene is one of res://tests/) records into the tests' folder, never
	# the player's Replays
	var path := ""
	var top := get_tree().current_scene
	if top and top != self and top.scene_file_path.begins_with("res://tests/"):
		DirAccess.make_dir_recursive_absolute(TEST_REPLAYS)
		path = TEST_REPLAYS.path_join(Q3ReplayFile.new_path(setup.map, setup.mode).get_file())
	var err := recorder.start({"map": setup.map, "map_name": String(Session.map_info(setup.map).get("name", setup.map)),
		"mode": setup.mode, "mode_name": m.display_name if m else setup.mode, "title": _match_title(), "source": setup.source,
		"local_client": me, "player": String(setup.player.get("name", "Player"))}, path)
	if err != "":
		push_warning("replay not recorded: " + err)
		recorder = null


## Ends the recording with the match's result: the trailer the replay browser lists. Quitting
## stops the server before this runs, so the standings are the last ones seen.
func _finish_recording(results: Dictionary) -> void:
	if recorder == null:
		return
	var rows := standings()
	var t := {"standings": rows if not rows.is_empty() else _last_standings}
	t.merge(results, true)
	replay_path = recorder.finish(t)
	recorder = null


func _build_view() -> void:
	var path := Q3MapBuilder.scene_path(setup.map)
	var custom := Session.map_info(setup.map)
	if setup.source == "editor" and Session.playtest.has("doc"):
		# a map editor play-test: drawn from the editor's document (Q3PlayMap), run from its .bsp
		map_scene = Q3PlayMap.build(Session.playtest["doc"], Session.playtest.get("models", {}))
		add_child(map_scene)
	elif custom.get("special", "") == "custom":
		# a map published from the editor: drawn from its saved document, run from its .bsp
		var d := Q3MapDoc.new()
		var err := d.load_file(String(custom.get("doc", "")))
		var models := {}
		for e in custom.get("models", {}):
			models[int(e)] = int(custom["models"][e])
		map_scene = Q3PlayMap.build(d, models) if err == "" else null
		if map_scene:
			add_child(map_scene)
		elif _error == "":
			# the .bsp would play with nothing drawn: refuse instead
			_error = "%s's published copy can't be read (%s). Publish it again from the map editor." % [custom.get("name", setup.map), err]
	elif _preloaded_map != null:
		map_scene = _preloaded_map
		_preloaded_map = null
		add_child(map_scene)
	elif ResourceLoader.exists(path):
		map_scene = (load(path) as PackedScene).instantiate() as Q3Map
		add_child(map_scene)
	client = Q3Client.new()
	client.name = "Client"
	client.map = map_scene
	client.local_client = me
	client.feed = feed
	add_child(client)
	client.obituary.connect(_on_obituary)
	client.death_noted.connect(func(attacker: int, target: int, means: int):
		if target >= 0 and target < E.MAX_CLIENTS:
			_deaths[target] = {"attacker": attacker, "means": means}   # the death cam's card
			_death_cards.erase(target))
	client.landed.connect(func(change: float):
		_land_change = change
		_land_time = _now_ms())
	client.stepped.connect(_on_stepped)
	client.team_sound.connect(func(path: String): hud.announce(path))
	var tags = Q3.load_json(Q3.LIB + "props/view_weapons.json")
	if tags is Dictionary:
		_hand_tags = tags
	cam = Camera3D.new()
	cam.fov = Settings.fov_vertical()
	cam.near = 0.05
	cam.far = 500.0
	add_child(cam)
	cam.make_current()
	if map_scene and not map_scene.portals.is_empty():
		client.self_body = true   # mirrors see the player's own body; the player's eyes don't
		cam.cull_mask &= ~Q3Portal.MIRROR_LAYER
	hud = Q3Hud.new()
	hud.match_feed = feed
	add_child(hud)
	hud.chat.sent.connect(_say)
	hud.chat.closed.connect(func():
		_chat_held.clear()
		for a in USERCMD_ACTIONS:
			if Input.is_action_pressed(a):
				_chat_held.append(a))
	hud.chat.names = func() -> Array: return standings().map(func(r): return r["plain"])
	Online.queue_changed.connect(_on_queue_changed)
	client.local_event.connect(_on_local_event)
	music = Q3Music.new()
	music.name = "Music"
	add_child(music)
	if replay_feed == null:   # a replay's controls are its own bar (Q3ReplayViewer)
		_build_pause_menu()
	scoreboard = Q3Scoreboard.new()
	add_child(scoreboard)
	scoreboard.picked.connect(_follow_from_board)


## cg_view.c CG_CalcFov: Settings > Video > Field of view (cg_fov, horizontal at 4:3), and +zoom
## easing to cg_zoomFov 22.5 over ZOOM_TIME 150 ms and back.
func _fov_x() -> float:
	var fov := float(Settings.get_value("video/fov"))
	var f := (_now_ms() - _zoom_time) / ZOOM_TIME
	if _zoomed:
		return ZOOM_FOV if f > 1.0 else fov + f * (ZOOM_FOV - fov)
	return fov if f > 1.0 else ZOOM_FOV + f * (fov - ZOOM_FOV)


## +zoom held (or, with Toggle zoom, each press flips it). Not while Tab is held: there the right
## mouse button is the scoreboard's.
func _poll_zoom() -> void:
	if Input.is_action_pressed("q3_scores"):
		return
	if Settings.get_value("controls/toggle_zoom"):
		if Input.is_action_just_pressed("q3_zoom"):
			_zoom(not _zoomed)
	else:
		_zoom(Input.is_action_pressed("q3_zoom"))


## CG_ZoomDown / CG_ZoomUp. The dead don't zoom.
func _zoom(on: bool) -> void:
	if on == _zoomed:
		return
	var ps := feed.player_state(me)
	if on and not ps.is_empty() and int(ps["pm_type"]) == E.PM_DEAD:
		return
	_zoomed = on
	_zoom_time = _now_ms()


## Tab + right click frees the mouse for the scoreboard's player cards (and right click again takes
## it back). Here, before the scoreboard's own click handling and before Mouse 2's menu meaning
## (ui_cancel, "back") could pause the match.
func _input(event: InputEvent) -> void:
	if replay_feed:
		return   # the viewer's own mouse; the board stays look-only
	if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_RIGHT \
			and not _paused and scoreboard.visible and Input.is_action_pressed("q3_scores"):
		scoreboard.set_interactive(not scoreboard.interactive)
		get_viewport().set_input_as_handled()


func _unhandled_input(event: InputEvent) -> void:
	if replay_feed:
		_replay_input(event)
		return
	if net_feed and hud == null:   # still connecting, or it failed: Escape gives up
		if event.is_action_pressed("ui_cancel") and not event is InputEventMouseButton:
			_leave()
		return
	# map editor play-tests: F6 straight back to the editor, F7 the editor's entity markers over the
	# game (spawns, targets, jump pad arcs)
	var key := event as InputEventKey
	if setup.source == "editor" and key and key.pressed and not key.echo:
		if key.keycode == KEY_F6:
			get_viewport().set_input_as_handled()
			_leave()
			return
		if key.keycode == KEY_F7 and map_scene is Q3PlayMap:
			get_viewport().set_input_as_handled()
			var mk: Q3EditorEntities = (map_scene as Q3PlayMap).markers
			mk.visible = not mk.visible
			return
	# Mouse 2 means "back" in the menus only; in a match it's a game button, never the pause
	if event.is_action_pressed("ui_cancel") and not event is InputEventMouseButton:
		if _paused and pause_layer and pause_layer.web:
			# the page didn't have the keyboard (the web view lost focus): its Escape still goes
			# back a page or resumes, and the page takes the keyboard back
			pause_layer.post({"type": "nav", "dir": "back"})
			pause_layer.set_shown(true)
			return
		if _error != "" or _ended:
			_leave()
		else:
			_set_paused(not _paused)
		return
	if _paused:
		return
	if hud.chat.typing and not event is InputEventMouseMotion:
		return   # the chat line has the keyboard; you can still look around
	if event.is_action_pressed("q3_crouch") and Settings.get_value("controls/toggle_crouch"):
		_crouch_on = not _crouch_on
	if (event.is_action_pressed("q3_chat") or event.is_action_pressed("q3_teamchat")) and me >= 0 and _error == "" and not _ended:
		hud.chat.open(event.is_action_pressed("q3_teamchat"))
		get_viewport().set_input_as_handled()
		return
	if event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		_mouse += event.relative   # turned into view angles once per frame (_mouse_move)
	if not event is InputEventMouseMotion and _spectator_input(event):
		get_viewport().set_input_as_handled()
		return
	for w in range(1, 10):
		if event.is_action_pressed("q3_weapon%d" % w):
			_select_weapon(w)
	if event.is_action_pressed("q3_vote_yes") or event.is_action_pressed("q3_vote_no"):
		_cmd("vote yes" if event.is_action_pressed("q3_vote_yes") else "vote no")   # only does anything while a vote is on
	if event.is_action_pressed("q3_ready") and me >= 0 and not _spectating_now:
		_cmd("ready")
		_ready_sent = not _ready_sent
	if event.is_action_pressed("q3_weapnext"):
		_cycle_weapon(1)
	if event.is_action_pressed("q3_weapprev"):
		_cycle_weapon(-1)
	if event is InputEventMouseButton and event.pressed and Input.mouse_mode != Input.MOUSE_MODE_CAPTURED and _error == "" \
			and not scoreboard.interactive:
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


func _process(delta: float) -> void:
	if replay_feed and _error == "":
		_process_replay(delta)
		return
	if net_feed and (_error == "" or _joining):
		_process_net(delta)
		return
	# a match other players are in goes on behind the pause menu (they can't pause it); you stand still
	if _error != "" or not Q3Server.running() or (_paused and host_net == null):
		return
	if not _paused:
		_mouse_move(delta * 1000.0)
		_pad_sticks(delta)
	# the client clock runs in whole milliseconds, like Sys_Milliseconds
	_acc += delta * 1000.0
	var ms := int(_acc)
	_acc -= ms
	if ms > 0:
		_client_time += ms
		if _paused:
			_usercmd(0, 0, 0, 0)
		else:
			_send_usercmd()
		if host_net:
			host_net.pump_in(ms / 1000.0)   # the players' packets, before the frame that reads them
		if Q3Server.frame(ms) != 0:
			_error = Q3Server.error()
			hud.fatal("The match stopped: " + _error + "\n\nEscape: back to the menu")
			return
		if host_net:
			host_net.pump_out()   # what the frame made for them
		if recorder:
			recorder.pump()
			_standings_t -= delta
			if _standings_t <= 0.0:
				_standings_t = 1.0
				_last_standings = standings()
	_present_frame(delta)


## Everything drawn and shown in a frame of a live match, from the feed's state: the server commands,
## the camera, the bodies, the HUD, the scoreboard. A network match reaches it the same way (the
## feed is a Q3NetFeed).
func _present_frame(delta: float) -> void:
	for c in feed.next_commands(me):
		_server_command(c)
	var ps := feed.player_state(me)
	_poll_zoom()
	if not ps.is_empty() and int(ps["pm_type"]) == E.PM_DEAD:
		_crouch_on = false
	var was_spectating := _spectating_now
	_spectating_now = spectating(ps) and not _ended
	if was_spectating and not _spectating_now and not ps.is_empty():
		_weapon = int(ps["weapon"])   # in again: your own weapon, as CG_Respawn takes it
	_respawn(ps)
	_track_viewheight(ps)
	_spectate_view(ps)
	if not _spectating_now:
		match_stats.moved(ps)
	client.apply_snapshot(feed.snapshot(me), ps)
	client.muted = false
	if not _ended:
		music.play(feed.configstring(E.CS_MUSIC))   # CG_StartMusic, again when CS_MUSIC changes
	if net_feed:
		client.set_next(net_feed.next_snapshot(), net_feed.fraction())
		client.animate(float(net_feed.render_time()))
	else:
		client.animate(float(feed.time()) + _acc)
	# (dead, the death cam takes over: the server never sends a follower the body to chase; the
	# cinematic camera stays on the ragdoll)
	var cine := following(ps) and spec_view == "cinematic" and not _ended
	var chase := following(ps) and spec_chase and int(ps["pm_type"]) != E.PM_DEAD
	client.self_visible = chase or cine   # the followed player's body, built from the playerState
	var death_view := _update_death(ps, delta)
	if cine:
		client.own_death = true   # their ragdoll, to stay on when they go down
		_cine_camera(ps, int(ps["clientNum"]), client.own_body)
	elif chase:
		_chase_camera(ps, delta)
	if chase or cine:
		if _gun:
			_gun.visible = false
		client.view_fx = client._self.get("wfx")   # their flash and beam come from the body's gun
		_gun_weapon = -1
	elif not death_view:
		_update_camera(ps)
		_update_gun(ps)
		_scan_crosshair(ps)
	if _results and _results.frame_camera(cam) and _gun:   # the podium, in the map, while the results show
		_gun.visible = false
	client.present(ps, cam.global_position)
	_update_hud(ps)
	_cine_hud(cine)
	_spectator_hud(ps)
	_direct(ps, delta)
	hud.lagometer(delta * 1000.0, net_feed.ping() if net_feed else int(ps.get("ping", 0)))
	_poll_item_timers(delta)
	_board_t -= delta
	# the scores show while Tab is held (Quake 3 also showed them while you're dead; Ozmium,
	# 2026-10-23: Tab only, the dead view is the death cam's)
	var show_board := Input.is_action_pressed("q3_scores") and not hud.chat.typing and not _ended
	scoreboard.visible = show_board
	if scoreboard.interactive and not Input.is_action_pressed("q3_scores"):
		scoreboard.set_interactive(false)
	if show_board and _board_t <= 0.0:
		_board_t = 0.25
		var ts := Vector2i(-1, -1)
		if feed.cvar("g_gametype").to_int() >= E.GT_TEAM:   # however the match was started (cgs.gametype)
			ts = Vector2i(int(feed.configstring(E.CS_SCORES1)), int(feed.configstring(E.CS_SCORES2)))
		scoreboard.refresh(standings(), _match_title(), ts,
			{"map": setup.map, "clock": hud.status.clock, "limit": hud.status.limit},
			{} if _spectating_now else match_stats.summary(ps))
	if not _ended and feed.configstring(E.CS_INTERMISSION) == "1":
		_end_match(ps)


## A matchmade ranked match: this game's account of how it ended, for the hub to compare with everyone else's
## (online/src/ranked.ts decide). `left`: this player quit before the end, which is a loss.
func _report_match(left: bool) -> void:
	var mm := Session.matchmade
	if mm.is_empty() or not bool(mm.get("ranked", false)) or _reported or me < 0:
		return
	_reported = true
	var rows: Array = []
	if not left and hud != null:
		for r in standings():
			rows.append({"client": int(r["client"]), "score": int(r["score"]), "team": int(r["team"])})
	Online.match_report(String(mm.get("match", "")), me, rows, left, host_net.peer_pids() if host_net else {})


## The hub's answer to the report arrives a moment after the match: the rating change goes on the results screen.
func _on_queue_changed() -> void:
	var r: Dictionary = Online.last_result
	if _results == null or r.is_empty() or String(r.get("match", "")) != String(Session.matchmade.get("match", "")):
		return
	if bool(r.get("voided", false)):
		_results.rank_note("No rating change: %s" % String(r.get("reason", "the result couldn't be confirmed")), false)
	elif r.has("after"):
		var d := int(r.get("delta", 0))
		_results.rank_note("Rating %d -> %d (%s%d)" % [int(r["before"]), int(r["after"]), "+" if d >= 0 else "", d], d >= 0)
	Online.last_result = {}


## A command from the player to the game (say, team, follow, ready ...).
func _cmd(text: String) -> void:
	if net_feed:
		net_feed.command(text)
	elif me >= 0:
		Q3Server.client_command(me, text)


## One usercmd (cl_input.c CL_FinishMove): to the server in this process at once, or to the net client,
## which sends it in its next packet.
func _usercmd(buttons: int, fwd: int, side: int, up: int) -> void:
	if net_feed:
		net_feed.usercmd(_client_time, _view, buttons, _weapon, fwd, side, up)
	else:
		Q3Server.usercmd(me, _client_time, _view, buttons, _weapon, fwd, side, up)


## Ends the match this process runs, if it runs one (a joined match is the other side's).
func _stop_server() -> void:
	if host_net:
		host_net.stop()
	if net_feed:
		net_feed.disconnect_now()
	elif Q3Server.running():
		Q3Server.exec_now("killserver")


## Joining someone's match (Session.join: {transport: Q3Transport, player: {...}}). The engine starts
## for its collision code; the connection runs from _process_net.
func _start_join() -> String:
	var err := Q3Host.start()
	if err != "":
		return err
	var j: Dictionary = Session.join
	net_feed = Q3NetFeed.new()
	net_feed.transport = j.get("transport")
	net_feed.interp = int(Settings.get_value("network/interp"))
	feed = net_feed
	var p: Dictionary = j.get("player", {})
	if p.is_empty():
		p = {"name": Settings.get_value("profile/name"), "model": Settings.get_value("profile/model"),
			"skin": Settings.get_value("profile/skin")}
	p["teampref"] = String(j.get("teampref", "auto"))
	var info := Q3Host.userinfo(p) + "\\snaps\\%d" % int(j.get("snaps", Q3NetFeed.SNAPS))
	if Q3Net.connect_to(info, int(j.get("maxpackets", Q3NetFeed.MAXPACKETS))) != 0:
		return "joining: " + Q3Net.error()
	Q3Net.hold_entry(true)   # until the map is loaded (_preload_join_map), as Quake 3 loads before it enters
	_joining = true
	_join_layer = CanvasLayer.new()
	_join_layer.layer = 30
	add_child(_join_layer)
	_join_label = Label.new()
	_join_label.set_anchors_preset(Control.PRESET_FULL_RECT)
	_join_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_join_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_join_label.add_theme_font_override("font", Q3Fonts.prop())
	_join_label.add_theme_font_size_override("font_size", 36)
	_join_label.text = "CONNECTING"
	_join_layer.add_child(_join_label)
	return ""


## The gamestate is in and the first snapshot is here: the map is known, so the view can be built.
func _finish_join() -> void:
	me = net_feed.me
	var info := Q3Host.parse_info(net_feed.configstring(E.CS_SERVERINFO))
	var gt := int(info.get("g_gametype", "0"))
	var mode := "ffa"
	if gt == 1:
		mode = "duel"
	elif gt == 4:
		mode = "ctf"
	elif gt >= 3:
		mode = "tdm"
	setup = Session.new_setup(mode, String(info.get("mapname", "q3dm17")))
	setup.source = "online"
	spec_view = String(Settings.get_value("gameplay/spec_view"))
	director = bool(Settings.get_value("gameplay/spec_director"))
	_director_forced = spec_view == "cinematic" and not director
	director = director or spec_view == "cinematic"
	_client_time = Q3Net.time()
	var ps := net_feed.player_state(me)
	if not ps.is_empty():
		_weapon = int(ps["weapon"])
	_build_view()
	if not Session.matchmade.is_empty():
		Online.match_joined(String(Session.matchmade.get("match", "")))
	if _join_layer:
		_join_layer.queue_free()
		_join_layer = null
	_joining = false
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


## The gamestate is in: load the map now, before the server puts us in the world (Q3Net.hold_entry), so the
## load's stall doesn't happen with the player already standing in the match.
func _preload_join_map() -> void:
	_join_loaded = true
	var map := String(Q3Host.parse_info(net_feed.configstring(E.CS_SERVERINFO)).get("mapname", ""))
	var path := Q3MapBuilder.scene_path(map)
	if map != "" and Session.map_info(map).get("special", "") != "custom" and ResourceLoader.exists(path):
		_preloaded_map = (load(path) as PackedScene).instantiate() as Q3Map
	Q3Net.hold_entry(false)


## A connection that failed or ended: the message stays up until Escape.
func _join_failed(why: String) -> void:
	_error = why
	if _preloaded_map != null:
		_preloaded_map.free()
		_preloaded_map = null
	_joining = false
	if hud:
		hud.fatal("Disconnected:\n%s\n\nEscape: back to the menu" % why)
	elif _join_label:
		_join_label.text = "COULDN'T JOIN\n%s\n\nEscape: back to the menu" % why
		_join_label.add_theme_font_size_override("font_size", 28)
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE


## A frame of a networked match: the transport and the net client's clock, this frame's usercmd, then the
## same presentation as a local match. Pausing doesn't stop the world, only your input.
func _process_net(delta: float) -> void:
	_acc += delta * 1000.0
	var ms := int(_acc)
	_acc -= ms
	var st := net_feed.pump(ms)
	if st == 0:
		_join_failed(net_feed.error if net_feed.error != "" else "The connection ended")
		return
	if _joining:
		if _join_label:
			_join_label.text = ["CONNECTING", "CONNECTING", "CONNECTING", "LOADING", "LOADING", ""][clampi(st, 0, 5)]
		if st == 4 and not _join_loaded:
			_preload_join_map()
		net_feed.update(delta)
		if st == 5 and not net_feed.snapshot(-1).is_empty():
			_finish_join()
		return
	if not _paused:
		_mouse_move(delta * 1000.0)
		_pad_sticks(delta)
	if ms > 0:
		_client_time = Q3Net.time()
		if _paused:
			_usercmd(0, 0, 0, 0)   # the match goes on behind the menu: stand still, stay connected
		else:
			_send_usercmd()
	net_feed.update(delta)
	_present_frame(delta)


func _send_usercmd() -> void:
	if hud.chat.typing:
		# CL_CmdButtons: BUTTON_TALK while a key catcher is up; bg_pmove.c PmoveSingle then holds still
		_usercmd(E.BUTTON_TALK, 0, 0, 0)
		return
	var fwd := 0
	var side := 0
	var up := 0
	var speed := 64 if _held(&"q3_walk") else 127
	if _held(&"q3_forward"): fwd += speed
	if _held(&"q3_back"): fwd -= speed
	if _held(&"q3_moveright"): side += speed
	if _held(&"q3_moveleft"): side -= speed
	if _pad_move != Vector2.ZERO:
		fwd = clampi(fwd - roundi(_pad_move.y * speed), -127, 127)
		side = clampi(side + roundi(_pad_move.x * speed), -127, 127)
	if _held(&"q3_jump"): up += 127
	if (_crouch_on if Settings.get_value("controls/toggle_crouch") else _held(&"q3_crouch")): up -= 127
	var buttons := 0
	# (the click that recaptures the mouse doesn't fire; tests have no mouse to capture)
	if _held(&"q3_attack") and (Input.mouse_mode == Input.MOUSE_MODE_CAPTURED or test_input):
		buttons |= E.BUTTON_ATTACK
	if _held(&"q3_use_item"):
		buttons |= E.BUTTON_USE_HOLDABLE
	if _held(&"q3_gesture"):
		buttons |= E.BUTTON_GESTURE
	if speed < 127:
		buttons |= E.BUTTON_WALKING
	if fwd != 0 or side != 0 or up != 0 or buttons != 0:
		buttons |= E.BUTTON_ANY
	_usercmd(buttons, fwd, side, up)


## cl_input.c CL_MouseMove over the frame's counts: m_yaw / m_pitch 0.022 degrees per count times
## sensitivity, plus cl_mouseAccel (Mouse > Acceleration): the faster the swipe (counts per ms),
## the higher the sensitivity. Godot reads captured mouse motion raw (WM_INPUT on Windows).
func _mouse_move(frame_ms: float) -> void:
	var m := _mouse
	_mouse = Vector2.ZERO
	if m == Vector2.ZERO:
		return
	var sens := float(Settings.get_value("mouse/sensitivity"))
	sens += m.length() / maxf(frame_ms, 1.0) * float(Settings.get_value("mouse/acceleration"))
	_turn(m * sens * Settings.M_YAW)


## Turns the view by (yaw, pitch) degrees: Mouse > Invert, and while zoomed cg.zoomSensitivity
## (fov_y / 75) times Mouse > Zoom sensitivity.
func _turn(d: Vector2) -> void:
	if _zoomed:
		d *= cam.fov / 75.0 * float(Settings.get_value("mouse/zoom_sensitivity"))
	var inv := -1.0 if Settings.get_value("mouse/invert") else 1.0
	_view.y -= d.x
	# No limit here: the server limits the real view (PM_UpdateViewAngles, ±88°) through delta_angles, which a
	# respawn or teleporter resets to cancel the pitch you had. Clamping these angles to ±89 cut the range by that
	# much: look 60° down through a teleporter and you could only look 29° down after it. Like CL_CreateCmd, only a
	# step of more than 90° in one frame is cut, and the angle wraps as ANGLE2SHORT would.
	_view.x = wrapf(_view.x + clampf(d.y * inv, -90.0, 90.0), -180.0, 180.0)


## The first controller's sticks (Controls > Stick deadzone, Stick look speed, Stick layout): the
## move stick drives the usercmd like the movement keys, scaled by how far it's pushed; the look
## stick turns up to Stick look speed turns a second, on a squared curve for fine aim.
func _pad_sticks(delta: float) -> void:
	_pad_move = Vector2.ZERO
	var pads := Input.get_connected_joypads()
	if pads.is_empty():
		return
	var dev: int = pads[0]
	var dz := float(Settings.get_value("controls/pad_deadzone"))
	var stick := func(ax: JoyAxis, ay: JoyAxis) -> Vector2:
		var v := Vector2(Input.get_joy_axis(dev, ax), Input.get_joy_axis(dev, ay))
		var l := v.length()
		return Vector2.ZERO if l <= dz else v / l * minf((l - dz) / maxf(1.0 - dz, 0.01), 1.0)
	var left: Vector2 = stick.call(JOY_AXIS_LEFT_X, JOY_AXIS_LEFT_Y)
	var right: Vector2 = stick.call(JOY_AXIS_RIGHT_X, JOY_AXIS_RIGHT_Y)
	var southpaw: bool = Settings.get_value("controls/stick_layout") == "southpaw"
	_pad_move = right if southpaw else left
	var look: Vector2 = left if southpaw else right
	if look != Vector2.ZERO and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		_turn(look * look.length() * float(Settings.get_value("controls/pad_look")) * 360.0 * delta)


## HUD > Item timers: the map's armors, Mega Health and powerups from the game (Q3Server.items),
## each with the seconds until it's back, four times a second.
func _poll_item_timers(delta: float) -> void:
	_timers_t -= delta
	if _timers_t > 0.0 or not ClassDB.class_has_method("Q3Server", "items"):
		return
	_timers_t = 0.25
	var items: Array = feed.items()
	if _map_items.is_empty():
		for it in items:
			_map_items[int(it["number"])] = true
	if not Settings.get_value("hud/item_timers"):
		return
	var names: PackedStringArray = ClassDB.class_call_static("Q3Core", "item_classnames")
	var now := feed.time()
	var list: Array = []
	for it in items:
		var idx := int(it["item"])
		if not _map_items.has(int(it["number"])) or idx < 1 or idx > names.size():
			continue
		var order := TIMED_ITEMS.find(names[idx - 1])
		if order < 0:
			continue
		var left := -1.0
		if it["taken"]:
			left = maxf((int(it["respawn_time"]) - now) / 1000.0, 0.0)
		list.append([String(Q3.entities.get(names[idx - 1], {}).get("icon", "")).get_file(), left, order])
	list.sort_custom(func(a, b): return a[2] < b[2])
	hud.set_item_timers(list)


## Input.is_action_pressed, except for an action that was already down when the chat closed.
func _held(action: StringName) -> bool:
	if not Input.is_action_pressed(action):
		_chat_held.erase(action)
		return false
	return not _chat_held.has(action)


## The chat line goes to the game as a client command (cl_keys.c Message_Key: say "<text>"). The
## game's tokenizer would end the argument at a quote, so quotes become apostrophes.
func _say(text: String, team: bool) -> void:
	var t := text.replace("\"", "'").strip_edges()
	while t.to_utf8_buffer().size() > Q3HudChat.MAX_SAY:
		t = t.left(-1)
	if t.begins_with("/") and t.get_slice(" ", 0).trim_prefix("/") in ["callvote", "vote"]:
		_cmd(t.substr(1))   # /callvote kick <name>, /vote yes: Quake 3's own voting (the game checks everything)
		return
	if t != "" and me >= 0:
		_cmd("%s \"%s\"" % ["say_team" if team else "say", t])


## cg_event.c EV_STEP_n: a step up eases the eye after the body, adding to one still easing. A
## replay or a followed player is drawn between recorded frames, which already smooths it.
func _on_stepped(change: float) -> void:
	if replay_feed != null or following(client._local_ps):
		return
	var since := _now_ms() - _step_time
	var old := _step_change * (STEP_TIME - since) / STEP_TIME if since < STEP_TIME else 0.0
	_step_change = minf(old + change, MAX_STEP_CHANGE)
	_step_time = _now_ms()


## The eye (cg_view.c CG_OffsetFirstPersonView): the live playerState's origin and view height,
## plus Quake 3's motion: the run pitch and roll, the bob (cg_bobup 0.005, cg_bobpitch and
## cg_bobroll 0.002; Settings > Gameplay > View bob), the damage kick, the landing dip, and the
## dead view lying on its side facing the killer (STAT_DEAD_YAW).
func _update_camera(ps: Dictionary) -> void:
	if ps.is_empty():
		return
	var eye: Vector3 = ps["origin"]
	var a: Vector3 = ps["viewangles"]
	var stats: PackedInt32Array = ps["stats"]
	if int(stats[E.STAT_HEALTH]) <= 0 and int(ps["pm_type"]) == E.PM_DEAD:
		a = Vector3(-15.0, float(stats[E.STAT_DEAD_YAW]), 40.0)
		eye.z += float(ps["viewheight"])
	else:
		var vel: Vector3 = ps["velocity"]
		var xyspeed := Vector2(vel.x, vel.y).length()
		var bob := int(ps["bobCycle"])
		var fracsin := absf(sin(float(bob & 127) / 127.0 * PI))
		# (no cg_runpitch / cg_runroll: id's CG_OffsetFirstPersonView reads cg.refdef.viewaxis after
		# CG_CalcViewValues has zeroed it, so in 1.32 the view never tilts with speed)
		if Settings.get_value("gameplay/view_bob"):
			var speed := maxf(xyspeed, 200.0)
			var ducked := (int(ps["pm_flags"]) & 1) != 0    # PMF_DUCKED
			a.x += fracsin * 0.002 * speed * (3.0 if ducked else 1.0)
			var roll := fracsin * 0.002 * speed * (3.0 if ducked else 1.0)
			a.z += -roll if (bob & 128) else roll
			eye.z += minf(fracsin * xyspeed * 0.005, 6.0)   # cg_bobup
		var kick: Vector2 = hud.view_kick() if hud else Vector2.ZERO
		a.x += kick.x
		a.z += kick.y
		eye.z += float(ps["viewheight"])
		var since := _now_ms() - _land_time
		if since < 0.0:
			pass   # stamped after the playhead (a replay seek)
		elif since < LAND_DEFLECT_TIME:
			eye.z += _land_change * since / LAND_DEFLECT_TIME
		elif since < LAND_DEFLECT_TIME + LAND_RETURN_TIME:
			eye.z += _land_change * (1.0 - (since - LAND_DEFLECT_TIME) / LAND_RETURN_TIME)
		# CG_TransitionPlayerState's duck smoothing (cg.duckChange, _track_viewheight) and
		# CG_StepOffset: crouching, standing up, respawning and stairs ease the eye over 100 and
		# 200 ms instead of snapping it
		var duck_since := _now_ms() - _duck_time
		if duck_since >= 0.0 and duck_since < DUCK_TIME:
			eye.z -= _duck_change * (DUCK_TIME - duck_since) / DUCK_TIME
		var step_since := _now_ms() - _step_time
		if step_since >= 0.0 and step_since < STEP_TIME:
			eye.z -= _step_change * (STEP_TIME - step_since) / STEP_TIME
	cam.global_position = Q3.q3_to_godot(eye)
	cam.basis = Q3Client.angles_basis(a) * Basis(Vector3.UP, PI)
	cam.fov = Settings.fov_vertical(_fov_x())


## cg_draw.c CG_ScanForCrosshairEntity: a trace from the eye along the aim; a player it reaches,
## not standing in fog and not invisible, has their name shown under the crosshair (the HUD fades it).
func _scan_crosshair(ps: Dictionary) -> void:
	if ps.is_empty() or String(Settings.get_value("crosshair/shape")) == "none":
		return
	var g := cam.global_position
	var start := Vector3(g.z, g.x, g.y) / Q3.UNIT
	var tr := feed.trace(start, start + Q3Client.forward_of(ps["viewangles"]) * 131072.0, Vector3.ZERO, Vector3.ZERO,
		int(ps["clientNum"]), 0x1 | 0x2000000)   # CONTENTS_SOLID | CONTENTS_BODY
	if tr.is_empty():
		return
	var hit := int(tr["entityNum"])
	if hit < 0 or hit >= E.MAX_CLIENTS or feed.point_contents(tr["endpos"], 0) & 64:   # CONTENTS_FOG
		return
	var es: Dictionary = client._ents.get(hit, {}).get("es", {})
	if int(es.get("powerups", 0)) & (1 << E.PW_INVIS):
		return
	hud.crosshair_target(_name(hit))


## cg_playerstate.c CG_TransitionPlayerState: any change of the watched view height starts the duck
## ease, dead or alive (a respawn rises from DEAD_VIEWHEIGHT). A different player's eyes start over.
func _track_viewheight(ps: Dictionary) -> void:
	if ps.is_empty():
		return
	if int(ps["viewheight"]) != _last_viewheight and int(ps["clientNum"]) == _last_view_client:
		_duck_change = float(int(ps["viewheight"]) - _last_viewheight)
		_duck_time = _now_ms()
	_last_viewheight = int(ps["viewheight"])
	_last_view_client = int(ps["clientNum"])


func _now_ms() -> float:
	if replay_feed:
		return replay_feed.playhead
	return float(feed.time()) + _acc


## The first-person weapon (cg_weapons.c CG_AddViewWeapon): the held weapon's model hung on its
## hand model's tag_weapon, whose frames are the only motion: the recoil (frames 1-6, in step with
## TORSO_ATTACK) and the weapon switch (6-14, TORSO_DROP then TORSO_RAISE). The hand itself sits
## at the eye turned by the run bob and the idle drift (CG_CalculateWeaponPosition), dips when
## you land, and drops 0.2 units per degree of fov over 90. Gameplay > Gun position picks the side
## (or hides it), and Gameplay > Gun field of view draws it as a camera at that fov would.
func _update_gun(ps: Dictionary) -> void:
	var side := String(Settings.get_value("gameplay/draw_gun"))
	var w := int(ps.get("weapon", 0))
	var pm := int(ps.get("pm_type", 0))
	var dead := pm == E.PM_DEAD or pm == E.PM_INTERMISSION or pm == E.PM_SPECTATOR
	if _guns.is_empty():
		_register_guns()
	if w != _gun_weapon:
		_gun_weapon = w
		if _gun:
			_gun.visible = false
		var g: Array = _guns.get(w, [null, null])
		_gun = g[0]
		_gun_fx = g[1]
		if _gun_fx:
			_gun_fx.rail_color = client.client_colors(me)[0]
		client.view_fx = _gun_fx
	if _gun == null:
		return
	_gun.visible = not side in ["hidden", "off"] and not dead
	# CG_AddViewWeapon draws your gun with the powerups' weapon shells (quad, battle suit) too
	var bits := Q3Client.powerup_bits(ps)
	Q3FX.powerup_shells(_gun, Q3Client.shell_kinds(bits, Q3Client.WEAPON_SHELLS), bits & (1 << E.PW_INVIS) != 0, true)
	var now := _now_ms()
	# the hand: bob and drift angles (degrees), landing dip, fov drop
	var vel: Vector3 = ps["velocity"]
	var xyspeed := Vector2(vel.x, vel.y).length()
	var bob := int(ps["bobCycle"])
	var fracsin := absf(sin(float(bob & 127) / 127.0 * PI))
	var scale := -xyspeed if (bob & 128) else xyspeed
	var roll := scale * fracsin * 0.005
	var yaw := scale * fracsin * 0.01
	var pitch := xyspeed * fracsin * 0.005
	var drift := (xyspeed + 40.0) * sin(now * 0.001) * 0.01
	roll += drift
	yaw += drift
	pitch += drift
	var hand_z := 0.0
	var since := now - _land_time
	if since < LAND_DEFLECT_TIME:
		hand_z = _land_change * 0.25 * since / LAND_DEFLECT_TIME
	elif since < LAND_DEFLECT_TIME + LAND_RETURN_TIME:
		hand_z = _land_change * 0.25 * (LAND_DEFLECT_TIME + LAND_RETURN_TIME - since) / LAND_RETURN_TIME
	var gun_fov := float(Settings.get_value("gameplay/gun_fov"))
	hand_z -= 0.2 * maxf((gun_fov if gun_fov > 0.0 else float(Settings.get_value("video/fov"))) - 90.0, 0.0)
	# id view axes -> camera space: forward -Z, left -X, up +Y
	var hand := Basis(Vector3.UP, deg_to_rad(yaw)) * Basis(Vector3.RIGHT, deg_to_rad(-pitch)) * Basis(Vector3.FORWARD, deg_to_rad(roll))
	var tag := _hand_pose(ps, now, w)
	var o: Vector3 = tag[0]
	var ax: Array = tag[1]
	# Gameplay > Gun side offset: cg_gunY's sideways nudge (id's +Y is left), out toward the gun's side
	o.y -= float(Settings.get_value("gameplay/gun_offset"))
	# left mirrors the side offset, center drops it (Quake Live's cg_drawGun choices)
	o.y *= -1.0 if side == "left" else 0.0 if side == "center" else 1.0
	var conv := func(v: Vector3) -> Vector3: return Vector3(-v.y, v.z, -v.x)
	var pos: Vector3 = hand * conv.call(o) * Q3.UNIT + Vector3(0, hand_z * Q3.UNIT, 0)
	# Quake 3 placed the gun for a 4:3 screen; a wider one at the same vertical view squeezes it
	# toward the middle. Widescreen placement spreads its sideways place by aspect / (4/3), so it sits
	# as far toward the edge as it did on a 4:3 screen.
	if Settings.get_value("gameplay/gun_widescreen"):
		var vp := get_viewport().get_visible_rect().size
		pos.x *= maxf((vp.x / maxf(vp.y, 1.0)) / (4.0 / 3.0), 1.0)
	# the tag's rows (gun forward, left, up) become the prop's axes (+X left, +Y up, +Z forward)
	var basis := Basis(conv.call(ax[1]), conv.call(ax[2]), conv.call(ax[0])).orthonormalized()
	# scaling camera-space x and y by tan(view/2) / tan(gun/2) gives exactly the gun fov's projection
	var k := 1.0
	if gun_fov > 0.0:
		k = tan(deg_to_rad(Settings.fov_vertical()) * 0.5) / tan(deg_to_rad(Settings.fov_vertical(gun_fov)) * 0.5)
	_gun.transform = Transform3D(Basis.from_scale(Vector3(k, k, 1.0)), Vector3.ZERO) * Transform3D(hand * basis, pos)


## Every view weapon, made once when the match starts (CG_RegisterWeapon at level load), so a switch
## shows the next gun at once instead of loading it mid-raise.
func _register_guns() -> void:
	for w in range(1, Q3Client.WEAPON_ITEMS.size()):
		var item: Dictionary = Q3.entities.get(Q3Client.WEAPON_ITEMS[w], {})
		if item.get("models", []).is_empty():
			continue
		var gun := Q3.instance_prop(item["models"][0])
		if gun == null:
			continue
		cam.add_child(gun)
		gun.visible = false
		for mi in gun.find_children("*", "GeometryInstance3D", true, false):
			(mi as GeometryInstance3D).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
			(mi as GeometryInstance3D).layers = Q3Portal.VIEW_LAYER   # not in mirrors
		var fx := Q3WeaponFx.bind(gun, w)
		fx.rail_color = client.client_colors(me)[0]
		_guns[w] = [gun, fx]


## The hand's tag_weapon pose from the local torso animation: CG_MapTorsoToWeaponFrame over
## CG_RunLerpFrame. Every player model plays TORSO_ATTACK(2) at 15 fps and TORSO_DROP / RAISE at
## 20 fps ("MUST NOT CHANGE -- hand animation is synced to this"). A new animation reaches its
## first frame one frame time (initialLerp) after it starts, easing from the pose shown before, so
## the drop runs into the raise and the raise settles into the idle pose without a jump.
func _hand_pose(ps: Dictionary, now: float, w: int) -> Array:
	var anim := int(ps.get("torsoAnim", 0))
	if anim != _torso_anim:
		_torso_anim = anim
		_torso_start = now
		_hand_from = _hand_last
	# [first hand frame, frames, fps]
	var seq := [0, 1, 20.0]
	match anim & ~E.ANIM_TOGGLEBIT:
		E.TORSO_ATTACK, E.TORSO_ATTACK2:
			seq = [1, 6, 15.0]
		E.TORSO_DROP:
			seq = [6, 5, 20.0]
		E.TORSO_RAISE:
			seq = [11, 4, 20.0]
	var lerp_ms := 1000.0 / float(seq[2])
	var t := now - _torso_start
	# CG_PlayerAnimation's speedScale: under haste the torso, and the hand with it, run 1.5 times
	# as fast (the first frame's ease isn't scaled)
	var speed := 1.5 if Q3Client.powerup_bits(ps) & (1 << E.PW_HASTE) else 1.0
	var pose: Array
	if t < lerp_ms and not _hand_from.is_empty():
		var a: Array = _hand_from
		var b := _hand_tag(w, float(seq[0]))
		var k := t / lerp_ms
		pose = [(a[0] as Vector3).lerp(b[0], k), [0, 1, 2].map(func(i: int) -> Vector3:
			return (a[1][i] as Vector3).lerp(b[1][i], k).normalized())]
	else:
		pose = _hand_tag(w, float(seq[0]) + minf((t - lerp_ms) / lerp_ms * speed if t > lerp_ms else 0.0, float(seq[1] - 1)))
	_hand_last = pose
	return pose


## tag_weapon at a fractional hand frame: [origin, [axis rows]] in id view axes.
func _hand_tag(w: int, frame: float) -> Array:
	var frames: Array = _hand_tags.get(WEAPON_DIRS[w] if w > 0 and w < WEAPON_DIRS.size() else "", [])
	if frames.is_empty():
		return [Vector3(0, -5, -10), [Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1)]]
	var f0 := clampi(int(frame), 0, frames.size() - 1)
	var f1 := clampi(f0 + 1, 0, frames.size() - 1)
	var k := frame - floorf(frame)
	var a: Array = frames[f0]
	var b: Array = frames[f1]
	var v := func(i: int) -> Vector3:
		return Vector3(lerpf(a[i], b[i], k), lerpf(a[i + 1], b[i + 1], k), lerpf(a[i + 2], b[i + 2], k))
	return [v.call(0), [v.call(3).normalized(), v.call(6).normalized(), v.call(9).normalized()]]


func _set_paused(on: bool) -> void:
	_paused = on
	var web := pause_layer != null and pause_layer.web != null
	if on:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		if web:
			pause_layer.post({"type": "pause", "match": pause_info()})
			pause_layer.set_shown(true)
		else:
			_pause_panel = _make_pause_panel()
			add_child(_pause_panel)
		UiSound.play_in()
	else:
		if web:
			pause_layer.set_shown(false)
		if _pause_panel:
			_pause_panel.queue_free()
			_pause_panel = null
		hud.crosshair.preview_weapon = 0
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


## The web pause menu, made hidden at the start so Escape opens it at once. Its bridge runs in
## the "pause" context: the page draws PauseApp and the match's commands come back here.
func _build_pause_menu() -> void:
	pause_bridge = Q3MenuBridge.new()
	pause_bridge.context = "pause"
	pause_layer = Q3WebLayer.new(pause_bridge)
	pause_layer.name = "PauseMenu"
	add_child(pause_layer)
	pause_bridge.post.connect(pause_layer.post)
	if not pause_layer.build(false, false):
		return
	pause_bridge.resume_requested.connect(func(): _set_paused(false))
	pause_bridge.leave_requested.connect(_leave)
	pause_bridge.quit_requested.connect(Session.quit_game)
	pause_bridge.restart_requested.connect(func():
		# q3_ui's Restart Arena: the same map from the start (a joined match is the host's to restart)
		if net_feed:
			return
		Q3Server.exec("map_restart 0")
		_set_paused(false))
	pause_bridge.spectate_requested.connect(func(on: bool):
		set_spectating(on)
		_set_paused(false))
	pause_bridge.ready_requested.connect(func():
		_cmd("ready")
		_ready_sent = not _ready_sent
		pause_layer.post({"type": "pause", "match": pause_info()}))
	pause_bridge.crosshair_preview.connect(func(w: int, hit: bool):
		hud.crosshair.preview_weapon = w
		if hit:
			hud.crosshair.hit())


## What the pause menu shows of the match: its title, the clock, the leaders, the ready-up state.
func pause_info() -> Dictionary:
	var warmup := feed.configstring(E.CS_WARMUP)
	var ps := feed.player_state(me)
	var rows: Array = []
	for r in standings():
		rows.append({"name": String(r["name"]), "score": int(r["score"]), "local": bool(r["local"])})
	return {"title": _match_title(), "map": setup.map, "map_name": String(Session.map_info(setup.map).get("name", setup.map)),
		"clock": hud.status.clock if hud else "", "warmup": warmup != "" and warmup != "0",
		"ready_up": feed.cvar("g_readyUp") == "1", "ready": _ready_sent, "can_restart": net_feed == null,
		"weapon": int(ps.get("weapon", 0)), "standings": rows, "spectating": spectating(ps), "can_spectate": can_spectate()}


## The native stand-in for the pause menu when there's no web view (headless runs): resume or
## leave. Offline matches stop while it's up.
func _make_pause_panel() -> Node:
	var layer := CanvasLayer.new()
	layer.layer = 25
	var root := Control.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	layer.add_child(root)
	var bg := ColorRect.new()
	bg.color = Color(0, 0, 0, 0.6)
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.add_child(bg)
	var box := VBoxContainer.new()
	box.set_anchors_preset(Control.PRESET_CENTER)
	box.add_theme_constant_override("separation", 18)
	root.add_child(box)
	var title := Label.new()
	title.text = "PAUSED"
	title.add_theme_font_override("font", Q3Fonts.banner())
	title.add_theme_font_size_override("font_size", 72)
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	box.add_child(title)
	for item in [["RESUME", func(): _set_paused(false)], ["LEAVE MATCH", _leave]]:
		var b := Button.new()
		b.text = item[0]
		b.add_theme_font_override("font", Q3Fonts.prop())
		b.add_theme_font_size_override("font_size", 36)
		b.flat = true
		b.mouse_entered.connect(func(): UiSound.play("move"))
		b.pressed.connect(item[1])
		box.add_child(b)
	box.position = -box.get_combined_minimum_size() * 0.5
	return layer


func _update_hud(ps: Dictionary) -> void:
	if ps.is_empty():
		return
	var gt := feed.cvar("g_gametype").to_int()
	hud.update(ps, {"level_time": feed.time(), "gametype": gt, "standings": standings})


# ---------------------------------------------------------------- scores and the end of the match

func _campaign_skill() -> int:
	return int(Session.campaign.progress.get("skill", 3)) if setup.source in ["campaign", "training"] else 3


## Everyone in the match, best first: name (with its color codes; "plain" without), score, deaths,
## ping, team, card and level. Bots get cards built from their own heads; you get your profile's card.
func standings() -> Array:
	var out: Array = []
	for cl in E.MAX_CLIENTS:
		if feed.client_state(cl) < 4:
			continue
		var ps := feed.player_state(cl)
		if ps.is_empty() or int(ps["persistant"][E.PERS_TEAM]) == E.TEAM_SPECTATOR or following(ps):
			continue   # spectators watch (a follower's playerState is the followed player's copy)
		var info := Q3Host.parse_info(feed.configstring(E.CS_PLAYERS + cl))
		var name := String(info.get("n", "?"))
		var plain := Q3Colors.strip(name)
		var model := String(info.get("model", "sarge")).get_slice("/", 0).to_lower()
		var bot := info.has("skill")
		var row := {"client": cl, "name": name, "plain": plain, "score": int(ps["persistant"][E.PERS_SCORE]), "model": model,
			"skin": String(info.get("model", "sarge")).get_slice("/", 1).to_lower() if String(info.get("model", "")).contains("/") else "default",
			"skill": int(float(info.get("skill", "0"))),
			"frags": maxi(int(ps["persistant"][E.PERS_SCORE]), 0), "deaths": int(ps["persistant"][E.PERS_KILLED]),
			"ping": int(ps["ping"]), "team": int(ps["persistant"][E.PERS_TEAM]), "bot": bot, "local": cl == me,
			"flag": Q3Client.powerup_bits(ps) & ((1 << E.PW_REDFLAG) | (1 << E.PW_BLUEFLAG))}   # a CTF flag carried
		if replay_feed and _replay_cards.has(cl):
			# the card the player had in the match, as the recording kept it
			row["card"] = _replay_cards[cl]["card"]
			row["level"] = int(_replay_cards[cl]["level"])
		elif cl == me and not replay_feed:
			row["card"] = Q3Profile.card()
			row["level"] = int(Q3Profile.level()["level"])
		else:
			var c: Dictionary = Q3Profile.bot_card(plain, model, int(float(info.get("skill", "3"))))
			row["card"] = c
			row["level"] = int(c.get("level", 1))
		out.append(row)
	out.sort_custom(func(a, b): return a["score"] > b["score"])
	return out


func _match_title() -> String:
	var m := setup.game_mode()
	var map_name := String(Session.map_info(setup.map).get("name", setup.map))
	return "%s  -  %s" % [(m.display_name if m else setup.mode).to_upper(), map_name.to_upper()]


## Intermission (CS_INTERMISSION): the results screen, the career XP, and for the campaign the
## arena result and any tier movie it unlocks. A campaign arena is judged the way q3_ui's postgame
## menu judges it, from the game's own "postgame" report (g_arenas.c UpdateTournamentInfo): a win
## is first place, ties included, and the awards are accuracy (50% or better), impressive,
## excellent, gauntlet, the frags toward the next 100, and perfect. The announcer says who won.
func _end_match(ps: Dictionary) -> void:
	_ended = true
	if not spectating(ps):
		_report_match(false)
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	hud.chat.close()
	hud.visible = false
	var campaign := setup.source in ["campaign", "training"]
	var post := PackedStringArray()
	if campaign:
		# the game sends it as a console command at the intermission; the host keeps it
		for i in 60:
			var pg := Q3Server.postgame()
			if pg != "":
				post = pg.split(" ", false)
				break
			await get_tree().process_frame
	var rows := standings()
	if spectating(ps) and not campaign:
		_end_as_spectator(rows)
		return
	var place := 0
	for i in rows.size():
		if rows[i]["local"]:
			place = i
	var p: PackedInt32Array = ps.get("persistant", PackedInt32Array())
	var tied: bool = rows.size() > 1 and int(rows[0]["score"]) == int(rows[1]["score"])
	var won: bool = place == 0 and not tied and rows.size() > 1
	# team games are won by the team (cg_scoreboard.c: "Red leads 8 to 5", "Teams are tied at 4"):
	# your frag rank doesn't decide it, and no one player "wins"
	var team_line := ""
	if not campaign and feed.cvar("g_gametype").to_int() >= E.GT_TEAM:
		var ts := Vector2i(int(feed.configstring(E.CS_SCORES1)), int(feed.configstring(E.CS_SCORES2)))
		var my_team := int(p[E.PERS_TEAM]) if p.size() else 0
		tied = ts.x == ts.y
		won = not tied and ((my_team == E.TEAM_RED and ts.x > ts.y) or (my_team == E.TEAM_BLUE and ts.y > ts.x))
		team_line = "Teams are tied at %d" % ts.x if tied else ("Red leads %d to %d" % [ts.x, ts.y] if ts.x > ts.y \
			else "Blue leads %d to %d" % [ts.y, ts.x])
	var medals := {}
	if p.size():
		medals = {"impressive": p[E.PERS_IMPRESSIVE_COUNT], "excellent": p[E.PERS_EXCELLENT_COUNT],
			"gauntlet": p[E.PERS_GAUNTLET_FRAG_COUNT], "defend": p[E.PERS_DEFEND_COUNT],
			"assist": p[E.PERS_ASSIST_COUNT], "captures": p[E.PERS_CAPTURES]}
	var perfect: bool = won and p.size() > 0 and p[E.PERS_KILLED] == 0
	var awards := {}
	if post.size() >= 8:
		# postgame <clients> <you> <accuracy> <impressive> <excellent> <gauntlet> <score> <perfect>
		#          then <client> <rank> <score> for each player
		var you := int(post[1])
		var rank := 8
		for i in range(8, post.size() - 2, 3):
			if int(post[i]) == you:
				rank = (int(post[i + 1]) & ~Q3Hud.RANK_TIED) + 1
		won = rank == 1
		place = maxi(rank - 1, 0) if rank < 8 else place
		perfect = int(post[7]) != 0
		var frags_before := int(Session.campaign.progress["awards"].get("frags", 0))
		awards = {"accuracy": 1 if int(post[2]) >= 50 else 0, "impressive": int(post[3]), "excellent": int(post[4]),
			"gauntlet": int(post[5]), "frags": maxi(int(post[6]), 0), "perfect": 1 if perfect else 0}
		medals = {"impressive": int(post[3]), "excellent": int(post[4]), "gauntlet": int(post[5])}
		if int(post[2]) >= 50:
			medals["accuracy"] = 1
		if (frags_before + awards["frags"]) / 100 > frags_before / 100:
			medals["frags"] = (frags_before + awards["frags"]) / 100 * 100
	var movie := ""
	var extra: Array = []
	if campaign:
		if awards.is_empty():
			awards = medals.duplicate()
			if perfect:
				awards["perfect"] = 1
		movie = Session.campaign.record_result(setup.map, _campaign_skill(), won, awards)
		extra = Session.campaign.xp_lines()
	var xp: Dictionary = Q3Profile.award_match({"map": setup.map, "mode": setup.mode, "won": won,
		"frags": maxi(int(rows[place]["score"]) if place < rows.size() else 0, 0), "deaths": p[E.PERS_KILLED] if p.size() else 0,
		"medals": medals, "perfect": perfect, "campaign_skill": _campaign_skill() if campaign else 0, "extra": extra,
		"model": String(rows[place].get("model", "")) if place < rows.size() else "", "skin": String(setup.player.get("skin", "default")),
		"seconds": int(feed.time() / 1000.0)})
	# ui_sppostgame.c: "music music/win" (or loss) over the postgame, then "You win" or "<winner> wins"
	if campaign:
		music.play("music/win" if won else "music/loss")
	var draw := tied and (place == 0 or team_line != "")
	_finish_recording({"ended": "intermission", "won": won, "place": place + 1, "tied": tied,
		"result": "VICTORY" if won else ("DRAW" if draw else "DEFEAT")})
	var winner := String(rows[0].get("plain", "")).to_lower() if rows.size() else ""
	if won:
		hud.announce("sound/player/announce/youwin.wav")
	elif team_line == "":
		hud.announce("sound/player/announce/%s_wins.wav" % winner)
	var ordinal := ["1st", "2nd", "3rd"]
	_results = Q3ResultsScreen.new()
	add_child(_results)
	_results.show_results({"won": won, "title": "VICTORY" if won else ("DRAW" if draw else "DEFEAT"),
		"subtitle": "%s  -  %s" % [_match_title(), team_line] if team_line != "" \
			else "%s  -  %s of %d" % [_match_title(), ordinal[place] if place < 3 else "%dth" % (place + 1), rows.size()],
		"standings": rows, "medals": medals if not perfect else medals.merged({"perfect": 1}), "xp": xp, "replay": replay_path, "me": me, "camera": cam})
	_results.taunt_requested.connect(_send_taunt)
	_results.watch_requested.connect(func():
		_stop_server()
		Session.watch_replay(replay_path, 0, ""))
	_results.finished.connect(func():
		_stop_server()
		if movie != "" and Settings.get_value("gameplay/tier_movies"):
			Session.play_cinematics(PackedStringArray([movie]), Session.MAIN_MENU)
		elif setup.source == "editor":
			Session.back_to_editor()
		else:
			Session.to_main_menu())


func _select_weapon(w: int) -> void:
	var ps := feed.player_state(me)
	if ps.is_empty():
		return
	# cg_weapons.c CG_Weapon_f: owning it is enough; an empty gun comes up, and firing it
	# (EV_NOAMMO) switches away
	if int(ps["stats"][E.STAT_WEAPONS]) & (1 << w):
		_weapon = w


## cg_weapons.c CG_NextWeapon / CG_PrevWeapon: the next owned weapon with ammo, never the gauntlet
## (weapon 1 selects it); with nothing else, the weapon stays.
func _cycle_weapon(dir: int) -> void:
	var ps := feed.player_state(me)
	if ps.is_empty():
		return
	var owned := int(ps["stats"][E.STAT_WEAPONS])
	var w := _weapon
	for i in 16:
		w = (w + dir + 16) % 16
		if w > E.WP_GAUNTLET and w < 10 and owned & (1 << w) and int(ps["ammo"][w]) != 0:
			_weapon = w
			return


var _spawn_count := -1         ## the PERS_SPAWN_COUNT last seen
var _start_at := []            ## a map editor play-test's start: [origin, pitch, yaw] (id), done at the first spawn
var _map_restarted := false    ## cg.mapRestart: a restart re-selects the spawn weapon too


## cg_playerstate.c CG_Respawn: on every respawn (and after a restart) the selection becomes the
## weapon the server spawned you with, so the usercmd stops asking for the one you died holding.
## Not while following: that playerState is someone else's.
func _respawn(ps: Dictionary) -> void:
	if ps.is_empty() or _spectating_now or following(ps):
		return
	var sc := int(ps["persistant"][E.PERS_SPAWN_COUNT])
	if sc != _spawn_count or _map_restarted:
		if _spawn_count >= 0 or _map_restarted:
			_weapon = int(ps["weapon"])
		_spawn_count = sc
		_map_restarted = false
		if _start_at.size() and Q3Server.running():
			_playtest_start()


## A map editor play-test starts where the editor's camera was: on the floor under it, facing its
## way. A camera in a wall, or over nothing, leaves the player at the spawn point.
func _playtest_start() -> void:
	var o: Vector3 = _start_at[0]
	var angles := Vector2(_start_at[1], _start_at[2])
	_start_at = []
	const MASK_PLAYERSOLID := 1 | 0x10000 | 0x2000000   # solid, player clip, bodies
	var tr := Q3Server.trace(o, o - Vector3(0, 0, 8192), Vector3(-15, -15, -24), Vector3(15, 15, 32), me, MASK_PLAYERSOLID)
	if tr.is_empty() or tr["startsolid"] or float(tr["fraction"]) >= 1.0:
		hud.notify("The camera was in a wall or over nothing: you start at a spawn point")
		return
	var at: Vector3 = tr["endpos"]
	Q3Server.client_command(me, "edpos %.2f %.2f %.2f %.2f %.2f" % [at.x, at.y, at.z + 0.25, angles.x, angles.y])


## The results screen's podium taunt: to the server, which plays it on everyone's podium ("ptaunt").
func _send_taunt(id: String) -> void:
	if net_feed:
		Q3Net.command("ptaunt " + id)
	elif Q3Server.running():
		Q3Server.client_command(me, "ptaunt " + id)


func _server_command(c: String) -> void:
	# cgame tokenizes: the server sends "map_restart\n", so the newline must go before matching
	c = c.strip_edges()
	var cmd := c.get_slice(" ", 0)
	var args := c.split(" ", false)
	if cmd == "ptaunt" and args.size() >= 3:   # a podium taunt (patch 0009), from anyone on the podium
		if _results:
			_results.play_taunt(int(args[1]), String(args[2]))
		return
	if cmd == "dmg" and args.size() >= 3:
		hud.hit_damage += int(args[2])   # Audio > Hit sound: Pitch by damage
		client.damage(int(args[1]), int(args[2]))
		# dmg <target> <amount> <armor> <means> <time> (the last two since the scoreboard's stats)
		match_stats.damaged(int(args[2]), int(args[4]) if args.size() > 4 else -1, int(args[5]) if args.size() > 5 else -1,
			int(feed.player_state(me).get("weapon", 0)))
		return
	if cmd == "armorhit" and args.size() >= 3:
		client.armor_hit(int(args[1]), int(args[2]))
		return
	if cmd == "map_restart":
		# cg_servercmds.c CG_MapRestart: a clean arena (no marks, gibs, smoke or corpses), the
		# limit warnings armed again, the music from its start, and "FIGHT!" unless it's a warmup
		match_stats.reset()   # the warmup doesn't count
		_map_restarted = true
		# SV_MapRestart_f runs four 100 ms game frames, so the server clock is 400 ms ahead of ours
		# and ClientThink would hold our commands behind it (up to 1 s after a few restarts); id's
		# client catches up on the next snapshot (CL_AdjustTimeDelta)
		_client_time = maxi(_client_time, feed.time())
		client.clear_local()
		hud.map_restart()
		if not _ended:
			music.restart(feed.configstring(E.CS_MUSIC))
		var warmup := feed.configstring(E.CS_WARMUP)
		if warmup == "" or warmup == "0":
			hud.fight()
		return
	if cmd in ["print", "cp", "chat", "tchat"]:
		var text := c.substr(cmd.length() + 1).strip_edges().trim_prefix("\"").trim_suffix("\"").replace("\\n", "\n").strip_edges()
		if cmd == "cp":
			hud.center_print(text, 0.25)
		elif cmd == "print":
			if text != "":
				hud.notify(text.replace("\n", " "))
				if host_net and text.ends_with(" was kicked"):   # a passed vote kick: the hub bans them from this host for 30 minutes
					host_net.note_kick(Q3Colors.strip(text.trim_suffix(" was kicked")))
		else:
			# cg_servercmds.c: the talk sound, then the line without G_Say's EC (0x19) markers.
			# Settings > HUD > Team chat only is cg_teamChatsOnly: plain chat is dropped.
			if cmd == "chat" and Settings.get_value("hud/team_chats_only"):
				return
			client.play_sound("sound/player/talk.wav", Vector3.ZERO, true)
			hud.chat.add(text.replace(char(0x19), "").replace("\n", " "), cmd == "tchat")


## EV_OBITUARY: the kill feed, and cg_event.c CG_Obituary's center print when you frag someone.
func _on_obituary(attacker: int, target: int, means: int) -> void:
	var world := attacker == target or attacker < 0 or attacker >= E.MAX_CLIENTS
	var gt := feed.cvar("g_gametype").to_int()
	var team := func(cl: int) -> int:
		return int(Q3Host.parse_info(feed.configstring(E.CS_PLAYERS + cl)).get("t", "0")) if gt >= E.GT_TEAM else 0
	# cg.snap->ps.clientNum: the followed player while following, else you
	var viewed := int(feed.player_state(me).get("clientNum", me))
	hud.obituary("" if world else _name(attacker), _name(target), means, not world and attacker == viewed, target == viewed,
		0 if world else team.call(attacker), team.call(target))
	if not world:
		_last_frag = {"attacker": attacker, "at": float(feed.time())}
	_cine_obituary(attacker, target, world)
	if attacker == viewed and target != viewed:
		var ps := feed.player_state(me)
		var pers: PackedInt32Array = ps.get("persistant", PackedInt32Array())
		if gt < E.GT_TEAM and pers.size():
			hud.center_print("You fragged %s\n%s place with %d" % [_name(target), Q3HudStyle.place(int(pers[E.PERS_RANK])),
				int(pers[E.PERS_SCORE])], 0.3)
		else:
			hud.center_print("You fragged %s" % _name(target), 0.3)


## A player's name with its color codes.
func _name(cl: int) -> String:
	return String(Q3Host.parse_info(feed.configstring(E.CS_PLAYERS + cl)).get("n", "?"))


## The local player's predictable events (Q3Client.local_event): the speedometer's jump readout,
## the pickup display and cg_autoswitch, and CG_OutOfAmmoChange.
func _on_local_event(ev: int, parm: int) -> void:
	if ev >= E.EV_USE_ITEM0 and ev <= E.EV_USE_ITEM15:
		# cg_event.c CG_UseItem's center print, for whoever's eyes these are
		var item := ev - E.EV_USE_ITEM0
		if item > E.HI_NUM_HOLDABLE:
			item = 0
		var tag := "HI_TELEPORTER" if item == E.HI_TELEPORTER else "HI_MEDKIT" if item == E.HI_MEDKIT else ""
		var label := "No item to use"
		for cls in Q3.entities:
			var it: Dictionary = Q3.entities[cls]
			if item != 0 and String(it.get("type", "")) == "holdable" and String(it.get("tag", "")) == tag:
				label = "Use " + String(it.get("name", ""))
		if item == 0 or label != "No item to use":
			hud.center_print(label, 0.3)
		return
	if _spectating_now and ev != E.EV_JUMP and ev != E.EV_ITEM_PICKUP:
		return   # the followed player's empty guns and shots aren't your weapon choices
	var ps := feed.player_state(me)
	match ev:
		E.EV_FIRE_WEAPON:
			if not ps.is_empty():
				match_stats.fired(int(ps["weapon"]))
		E.EV_JUMP:
			if not ps.is_empty():
				var v: Vector3 = ps["velocity"]
				hud.jumped(Vector2(v.x, v.y).length())
		E.EV_ITEM_PICKUP:
			var names: PackedStringArray = ClassDB.class_call_static("Q3Core", "item_classnames")
			if parm >= 1 and parm <= names.size():
				var cls := names[parm - 1]
				hud.pickup(cls)   # the followed player's pickups show too (CG_ItemPickup)
				if not _spectating_now:
					_autoswitch(cls, ps)
		E.EV_NOAMMO:
			_out_of_ammo_change(ps)


## CG_ItemPickup: a picked-up weapon is selected at once (never the machinegun). "If better"
## switches only to a stronger weapon than the one held.
func _autoswitch(cls: String, ps: Dictionary) -> void:
	var mode := String(Settings.get_value("gameplay/autoswitch"))
	var it: Dictionary = Q3.entities.get(cls, {})
	if mode == "never" or String(it.get("type", "")) != "weapon":
		return
	var w := E.WEAPON_NAMES.find(String(it.get("tag", "")))
	if w <= 0 or w == E.WP_MACHINEGUN:
		return
	if mode == "better" and w <= _weapon:
		return
	_weapon = w


## CG_OutOfAmmoChange: the best weapon that still has ammo.
func _out_of_ammo_change(ps: Dictionary) -> void:
	if ps.is_empty():
		return
	var owned := int(ps["stats"][E.STAT_WEAPONS])
	for w in range(9, 0, -1):
		if owned & (1 << w) and (w == E.WP_GAUNTLET or int(ps["ammo"][w]) != 0):
			_weapon = w
			return


## Quit from the pause menu (or any way out that skips _leave): the replay still gets its trailer.
func _exit_tree() -> void:
	_finish_recording({"ended": "quit"})
	if replay_feed:
		replay_feed.close()


func _leave() -> void:
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	if not _ended:
		_report_match(true)   # leaving a ranked match early is a loss
	if _rendering_clip():
		_end_clip(1)
		return
	_finish_recording({"ended": "left"})
	_stop_server()
	if setup != null and setup.source == "editor":
		Session.back_to_editor()
	elif replay_feed:
		Session.leave_replay()
	else:
		Session.to_main_menu()


# ---------------------------------------------------------------- replays (REPLAY_PLAN.md R2, R3)
# A replay plays in this same scene: the feed is a Q3ReplayFeed instead of the server, so Q3Client,
# the HUD, the scoreboard, chat, music and the announcer all work unchanged. The playhead moves at
# the chosen speed; each recorded frame reached goes to Q3Client once (its events play), a jump
# rebuilds Q3Client from the target frame with its sounds and effects muted, and between frames the
# players and the view are interpolated. Q3ReplayViewer is the transport bar and the controls.

const FREE_SPEED := 400.0           ## free camera, id units a second (x2.5 with Walk held)
const ORBIT_SPEED := 14.0           ## degrees a second the orbit camera turns by itself
var _chase: Q3ChaseCam
var _free_pos := Vector3.ZERO      ## the free camera, id units
var _free_angles := Vector3.ZERO   ## its pitch and yaw, degrees
var _orbit_yaw := 0.0
var _orbit_pitch := 20.0
var _looking := false              ## the right mouse button is held: the mouse turns the camera
var _clip: Dictionary = {}         ## a clip render's range and camera (Session.replay.clip)
var _replay_damaged := false


## Opens the replay and makes a setup that names its map and mode. "" on success, else why not.
func _start_replay(path: String) -> String:
	setup = Session.new_setup("ffa", "q3dm17")
	replay_feed = Q3ReplayFeed.new()   # first: an error still leaves by the replay's way
	feed = replay_feed
	var err := Q3Host.start()
	if err != "":
		return err
	_stop_server()
	err = replay_feed.open(path)
	var h := replay_feed.header
	var mode := String(h.get("mode", "ffa"))
	setup = Session.new_setup(mode if mode in Session.MODES else "ffa", String(h.get("map", "q3dm17")))
	setup.source = "replay"
	if err != "":
		return err
	for row in h.get("standings", []):
		if row is Dictionary and row.has("client"):
			_replay_cards[int(row["client"])] = {"card": row.get("card", {}), "level": int(row.get("level", 1))}
	var cls := replay_feed.clients()
	me = int(h.get("local_client", -1))
	if not cls.has(me):
		me = cls[0] if cls.size() else -1
	# a match you spectated: the player you followed, else the first one playing
	var ps := replay_feed.frame_player_state(me)
	if not ps.is_empty() and following(ps):
		me = int(ps["clientNum"])
	elif not ps.is_empty() and int(ps["persistant"][E.PERS_TEAM]) == E.TEAM_SPECTATOR:
		for cl in cls:
			var cps := replay_feed.frame_player_state(cl)
			if int(cps["persistant"][E.PERS_TEAM]) != E.TEAM_SPECTATOR and not following(cps):
				me = cl
				break
	return ""


func _begin_replay() -> void:
	_chase = Q3ChaseCam.new()
	hud.respawn_text = ""   # nobody here respawns
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	replay_feed.playhead = clampf(replay_feed.start_time() + float(Session.replay.get("at", 0)), replay_feed.start_time(),
		replay_feed.end_time())
	replay_feed.jump(replay_feed.target_frame())
	_rebuild_view()
	viewer = Q3ReplayViewer.new()
	viewer.game = self
	add_child(viewer)
	_clip = Session.replay.get("clip", {})
	if not _clip.is_empty():
		# a clip render (Q3ClipExport): the range, from the viewer's camera as it was, without the
		# controls, then quit. Whose eyes is settled at the range's start: they may join late.
		replay_feed.playhead = clampf(float(_clip["from"]), replay_feed.start_time(), replay_feed.end_time())
		if not replay_feed.jump(replay_feed.target_frame()):
			_end_clip(1)
			return
		if replay_feed.clients().has(int(_clip["watch"])):
			me = int(_clip["watch"])
		_rebuild_view(true)
		set_cam_mode(String(_clip["cam"]))
		if _clip.has("pos"):
			_free_pos = _clip["pos"]
			_free_angles = _clip["angles"]
		if _clip.has("orbit"):
			_orbit_yaw = (_clip["orbit"] as Vector2).x
			_orbit_pitch = (_clip["orbit"] as Vector2).y
		viewer.chrome = false
		viewer.bar_shown = false
		viewer.names = bool(_clip.get("names", false))
		viewer.set_outlines(bool(_clip.get("outlines", false)))
		hud.visible = bool(_clip["hud"])
		replay_speed = 1.0
		replay_playing = true
		Input.mouse_mode = Input.MOUSE_MODE_HIDDEN


## A clip render's copy of the game (Q3ClipExport): no controls; it quits at the range's end.
func _rendering_clip() -> bool:
	return Session.replay.has("clip")


## Ends a clip render: Movie Maker closes the file as the game quits. Code 1: it went wrong.
func _end_clip(code := 0) -> void:
	_clip = {}
	replay_playing = false
	get_tree().quit(code)


## The camera as the viewer shows it, for a clip render to start from (Q3ClipExport.start's view).
func clip_view() -> Dictionary:
	var v := {}
	if cam_mode == "free":
		v["pos"] = _free_pos
		v["angles"] = _free_angles
	elif cam_mode == "orbit":
		v["orbit"] = Vector2(_orbit_yaw, _orbit_pitch)
	return v


## The client whose snapshot Q3Client draws: the watched player in first person (their own
## predicted events come through their playerState), else a free camera's (-1).
func _snap_client() -> int:
	return me if cam_mode == "first" else -1


## Rebuilds the drawing from the frame shown: after a jump, or when whose eyes or which camera
## changes. Sounds and one-shot effects stay quiet; bodies already down aren't ragdolled again.
func _rebuild_view(jumped := false) -> void:
	match_stats.reset()
	if jumped:
		_deaths.clear()   # the muted rebuild notes the obituaries still in the frame
	client.reset()
	hud.reset_view(jumped)
	if _chase:
		_chase.reset()
	_cine_reset()
	client.local_client = me if cam_mode == "first" else -1
	client.team_client = me   # the announcer speaks for the replay's player in every camera
	client.muted = true
	client.apply_snapshot(feed.snapshot(_snap_client()), replay_feed.frame_player_state(me) if cam_mode == "first" else {})
	client.muted = false
	_forget_times()
	_zoomed = false


## After a jump (or a new pair of eyes): the landing dip, the view gun's hand pose and its flash
## were stamped at another time; a later stamp would hold them (the camera lifted, a flash lit)
## until the playhead got there.
func _forget_times() -> void:
	_death_at = -1.0
	_death_cards.clear()
	hud.set_death({})
	_land_time = -100000.0
	_land_change = 0.0
	# the duck and step eases too: a stamp later than the playhead would push the eye far off
	_duck_time = -100000.0
	_duck_change = 0.0
	_last_view_client = -1   # the next viewheight is taken quietly, as *ops = *ps
	_step_time = -100000.0
	_step_change = 0.0
	_gun_weapon = -1
	_torso_anim = -1
	_hand_from = []
	for g in _guns.values():
		if g[1]:
			(g[1] as Q3WeaponFx).reset(_now_ms())


## Brings Q3Client to the playhead: frame by frame while playing, a jump when it moved backward or
## far ahead.
func _sync_replay() -> void:
	if replay_feed.needs_jump():
		if not replay_feed.jump(replay_feed.target_frame()):
			_replay_broken()
			return
		_rebuild_view(true)
		return
	var target := replay_feed.target_frame()
	# a scrub a little ahead plays only its last frames' events; the rest are noted quietly
	var loud_from := target - 3
	while replay_feed.frame < target:
		if not replay_feed.step():
			_replay_broken()
			break
		client.muted = replay_feed.frame < loud_from
		for c in feed.next_commands(me):
			if not client.muted:
				_server_command(c)
		client.apply_snapshot(feed.snapshot(_snap_client()), replay_feed.frame_player_state(me) if cam_mode == "first" else {})
	client.muted = false


## The file can't be decoded past here (damaged): playback stops with a message, once.
func _replay_broken() -> void:
	if _rendering_clip():
		_end_clip(1)
		return
	replay_playing = false
	if replay_feed.replay:
		replay_feed.playhead = float(replay_feed.replay.frame_time(maxi(replay_feed.frame, 0)))
	if not _replay_damaged:
		_replay_damaged = true
		hud.notify("This replay is damaged past %s; it stops here." % Q3ReplayViewer._clock(replay_feed.playhead - replay_feed.start_time()))


func _process_replay(delta: float) -> void:
	if not _clip.is_empty() and replay_feed.playhead >= minf(float(_clip["to"]), replay_feed.end_time()):
		_end_clip()
		return
	if replay_playing:
		replay_feed.playhead = minf(replay_feed.playhead + delta * 1000.0 * replay_speed, replay_feed.end_time())
		if replay_feed.at_end():
			replay_playing = false
	_sync_replay()
	if replay_feed.header.get("demo", false) and not replay_feed.clients().has(me) and replay_feed.clients().size():
		watch(replay_feed.clients()[0])   # the demo's player followed someone else (follownext)
	var first := cam_mode == "first"
	var ps := feed.player_state(me)
	if first:
		match_stats.moved(ps)
	_track_viewheight(ps)
	client.set_next(replay_feed.next_snapshot(_snap_client()), replay_feed.fraction())
	music.play(feed.configstring(E.CS_MUSIC))
	client.animate(replay_feed.playhead)
	_update_replay_camera(ps, delta)
	if first and not ps.is_empty():
		_update_gun(ps)
	elif _gun:
		_gun.visible = false
	client.present(ps if first else {}, cam.global_position)
	_update_hud(ps)
	if not first:
		# the middle of the screen is the player's body: no crosshair, speedometer or weapon row
		hud.crosshair.visible = false
		hud.speedometer.visible = false
		hud.weapon_bar.visible = false
	hud.lagometer(delta * 1000.0, 0)
	_board_t -= delta
	var pm := int(ps.get("pm_type", 0))
	var show_board := ((Input.is_action_pressed("q3_scores") and not _rendering_clip()) or pm == E.PM_INTERMISSION) and _results == null
	if viewer:
		show_board = show_board and not viewer.picking
	scoreboard.visible = show_board
	if show_board and _board_t <= 0.0:
		_board_t = 0.25
		var ts := Vector2i(-1, -1)
		if feed.cvar("g_gametype").to_int() >= E.GT_TEAM:
			ts = Vector2i(int(feed.configstring(E.CS_SCORES1)), int(feed.configstring(E.CS_SCORES2)))
		# the watched player's match, from where the playhead last jumped (or the start)
		scoreboard.refresh(standings(), _match_title(), ts, {"map": setup.map, "clock": hud.status.clock, "limit": hud.status.limit,
			"stats_title": "%s^7'S MATCH" % _name(me)}, match_stats.summary(ps) if first else {})


## The replay's cameras. First person is the live view (_update_camera); chase backs off behind
## the player through Q3ChaseCam; orbit circles them; free flies anywhere (the recording has every
## entity, so there's something to see everywhere).
func _update_replay_camera(ps: Dictionary, delta: float) -> void:
	var look := _mouse * float(Settings.get_value("mouse/sensitivity")) * Settings.M_YAW
	_mouse = Vector2.ZERO
	if cam_mode == "first":
		if not _update_death(ps, delta):
			_update_camera(ps)
		return
	cam.fov = Settings.fov_vertical(float(Settings.get_value("video/fov")))
	if cam_mode == "free":
		_free_angles.y -= look.x
		_free_angles.x = clampf(_free_angles.x + look.y, -89.0, 89.0)
		var move := Vector3.ZERO
		var fwd := Q3Client.forward_of(_free_angles)
		var yaw := deg_to_rad(_free_angles.y)
		var left := Vector3(-sin(yaw), cos(yaw), 0)
		if not _rendering_clip():   # a clip render's camera holds still
			if Input.is_action_pressed("q3_forward"): move += fwd
			if Input.is_action_pressed("q3_back"): move -= fwd
			if Input.is_action_pressed("q3_moveleft"): move += left
			if Input.is_action_pressed("q3_moveright"): move -= left
			if Input.is_action_pressed("q3_jump"): move.z += 1.0
			if Input.is_action_pressed("q3_crouch"): move.z -= 1.0
		var speed := FREE_SPEED * (2.5 if Input.is_action_pressed("q3_walk") else 1.0)
		_free_pos += move.normalized() * speed * delta
		cam.global_position = Q3.q3_to_godot(_free_pos)
		cam.basis = Q3Client.angles_basis(_free_angles) * Basis(Vector3.UP, PI)
		return
	if ps.is_empty():
		return
	if cam_mode == "cinematic":
		_cine_camera(ps, me, client.body_of(me))
		return
	var eye: Vector3 = ps["origin"]
	eye.z += float(ps["viewheight"])
	var t: Transform3D
	if cam_mode == "orbit":
		if _looking:
			_orbit_yaw -= look.x
			_orbit_pitch = clampf(_orbit_pitch + look.y, -30.0, 80.0)
		else:
			_orbit_yaw += ORBIT_SPEED * delta
		_chase.distance = 170.0
		_chase.height = 0.0
		t = _chase.place(feed, eye, _orbit_yaw, _orbit_pitch, delta, me, 0.0)
	else:
		var a: Vector3 = ps["viewangles"]
		_chase.distance = 110.0
		_chase.height = 20.0
		t = _chase.place(feed, eye, a.y, clampf(a.x * 0.5, -20.0, 45.0) + 8.0, delta, me)
	cam.global_transform = t


func _set_looking(on: bool) -> void:
	_looking = on
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED if on else Input.MOUSE_MODE_VISIBLE


func _notification(what: int) -> void:
	if what == NOTIFICATION_APPLICATION_FOCUS_OUT and _looking:
		_set_looking(false)


## Switches the camera. Leaving first person draws the watched player's body; entering it hides it.
func set_cam_mode(mode: String) -> void:
	if mode == cam_mode or not mode in ["first", "chase", "free", "orbit", "cinematic"]:
		return
	if _looking:
		_set_looking(false)
	if mode == "free":
		# the free camera starts where the view is
		_free_pos = _godot_to_id(cam.global_position)
		var f := -cam.global_basis.z
		var fi := Vector3(f.z, f.x, f.y)
		_free_angles = Vector3(rad_to_deg(-asin(clampf(fi.z, -1.0, 1.0))), rad_to_deg(atan2(fi.y, fi.x)), 0.0)
	if mode == "orbit":
		var ps := feed.player_state(me)
		_orbit_yaw = float((ps.get("viewangles", Vector3.ZERO) as Vector3).y) + 180.0
	var was_first := cam_mode == "first"
	cam_mode = mode
	if was_first != (mode == "first"):
		_rebuild_view()
		hud.layout()   # brings back what first person shows
	elif _chase:
		_chase.reset()
	_cine_reset()


## Watches another player (first person shows their eyes, HUD and gun).
func watch(cl: int) -> void:
	if cl == me or not replay_feed.clients().has(cl):
		return
	me = cl
	_rebuild_view()


## Moves the playhead (server ms); Q3Client catches up on the next frame.
func seek_to(ms: float) -> void:
	replay_feed.playhead = clampf(ms, replay_feed.start_time(), replay_feed.end_time())


## One recorded frame forward or back, paused.
func step_frame(dir: int) -> void:
	replay_playing = false
	var f := clampi(replay_feed.frame + dir, 0, replay_feed.replay.frame_count() - 1)
	seek_to(replay_feed.replay.frame_time(f))


## Godot meters -> id units and axes (the inverse of Q3.q3_to_godot).
static func _godot_to_id(p: Vector3) -> Vector3:
	return Vector3(p.z, p.x, p.y) / Q3.UNIT


func _replay_input(event: InputEvent) -> void:
	if _rendering_clip():
		# the render takes no controls (a key meant for the player's own game mustn't change it);
		# Escape stops it
		get_viewport().set_input_as_handled()
		if event.is_action_pressed("ui_cancel") and not event is InputEventMouseButton:
			_end_clip(1)
		return
	if viewer and viewer.handle_input(event):
		get_viewport().set_input_as_handled()
		return
	if event.is_action_pressed("ui_cancel") and not event is InputEventMouseButton:
		get_viewport().set_input_as_handled()
		_leave()
		return
	var mb := event as InputEventMouseButton
	if mb and mb.button_index == MOUSE_BUTTON_RIGHT and (cam_mode in ["free", "orbit"] or (_looking and not mb.pressed)):
		_set_looking(mb.pressed)
		get_viewport().set_input_as_handled()
		return
	if event is InputEventMouseMotion and _looking:
		_mouse += event.relative


# ---------------------------------------------------------------- spectating (REPLAY_PLAN.md R5)
# Spectators are id's own: "team spectator" flies free (PM_SPECTATOR), fire follows the next
# player (g_active.c SpectatorThink), "follow" picks one, and the duel's waiting line and the team
# rules stay the game's. Following, the playerState is a copy of the followed player's
# (PMF_FOLLOW), so the view, HUD, gun and events are theirs, and Q3Client takes them as the local
# player. The remaster adds a chase camera and a cinematic camera (Use item cycles them) that move
# only the camera, and an optional auto-director (Settings > Gameplay, or Gesture while
# spectating) that follows the action.

const DIRECTOR_HOLD := 4.0         ## seconds the auto-director stays with a player before it may switch
const CINE_HOLD := 9.0             ## ... under the cinematic camera, so a player gets a shot or two
const SPEC_VIEWS := ["first", "chase", "cinematic"]
var _spectating_now := false       ## spectating() this frame (input handlers read it)
var _spec_card_client := -1        ## whose card the spectator HUD holds
var _spec_card := {}
## Following someone: their eyes ("first"), the chase camera or the cinematic camera (Use item
## cycles them; Settings > Gameplay > Spectator camera is the first one)
var spec_view := "first"
var spec_chase: bool:
	get:
		return spec_view == "chase"
	set(on):
		set_spec_view("chase" if on else "first")
var director := false
var _director_t := 0.0
var _director_hold := 0.0
var _last_frag := {}               ## the latest frag: {attacker, at (ms)}, for the auto-director


## Whether you're watching rather than playing: on team spectator, or following someone.
func spectating(ps := {}) -> bool:
	if ps.is_empty():
		ps = feed.player_state(me)
	if ps.is_empty():
		return false
	return int(ps["persistant"][E.PERS_TEAM]) == E.TEAM_SPECTATOR or following(ps)


static func following(ps: Dictionary) -> bool:
	return not ps.is_empty() and (int(ps["pm_flags"]) & E.PMF_FOLLOW) != 0


## Before each snapshot: the player whose body Q3Client leaves out and whose events it plays as
## its own (the followed player; nobody in the chase camera). A change rebuilds the drawing quietly.
func _spectate_view(ps: Dictionary) -> void:
	var want := int(ps["clientNum"]) if following(ps) else me
	if want == client.local_client:
		return
	client.set_view_client(want)
	hud.follow_changed()   # id keeps announcer lines and rewards through a follow switch
	_forget_times()
	if _chase:
		_chase.reset()


## The remaster's chase camera on the followed player (Q3ChaseCam): it moves only the camera.
func _chase_camera(ps: Dictionary, delta: float) -> void:
	if _chase == null:
		_chase = Q3ChaseCam.new()
	var eye: Vector3 = ps["origin"]
	eye.z += float(ps["viewheight"])
	var a: Vector3 = ps["viewangles"]
	_chase.distance = 110.0
	_chase.height = 20.0
	cam.global_transform = _chase.place(feed, eye, a.y, clampf(a.x * 0.5, -20.0, 45.0) + 8.0, delta, int(ps["clientNum"]))
	cam.fov = Settings.fov_vertical(float(Settings.get_value("video/fov")))


## The spectator's HUD: the followed player's card and the keys (Q3Hud.set_spectator).
func _spectator_hud(ps: Dictionary) -> void:
	if not spectating(ps):
		if not hud.spectator.is_empty():
			hud.set_spectator({})
		return
	var key := func(action: String) -> String:
		var b = Settings.get_value("controls/" + action)
		return Settings.bind_label(String(b[0])) if b is Array and b.size() and String(b[0]) != "" else "?"
	var info := {"mode": "free", "director": director}
	var hints: Array = [[key.call("attack"), "next player"], [key.call("weapnext"), "cycle"]]
	if following(ps):
		var cl := int(ps["clientNum"])
		if cl != _spec_card_client:
			_spec_card_client = cl
			_spec_card = _card_of(cl)
		info = {"mode": {"chase": "chase", "cinematic": "cinematic"}.get(spec_view, "follow"), "client": cl, "name": _name(cl),
			"card": _spec_card["card"], "level": _spec_card["level"], "director": director}
		hints.append([key.call("jump"), "free camera"])
		hints.append([key.call("use_item"), {"first": "chase camera", "chase": "cinematic camera"}.get(spec_view, "first person")])
	else:
		hints.append([key.call("use_item"), "cinematic camera"])
	hints.append([key.call("gesture"), "auto-director " + ("off" if director else "on")])
	info["hints"] = hints
	# cg_draw.c CG_DrawSpectator: a free spectator in a duel is queued for the next match; in a team
	# game, the menu's Join gets you in
	if not following(ps) and int(ps["persistant"][E.PERS_TEAM]) == E.TEAM_SPECTATOR and replay_feed == null:
		var gt := feed.cvar("g_gametype").to_int()
		if gt == E.GT_TOURNAMENT:
			info["note"] = "WAITING TO PLAY"
		elif gt >= E.GT_TEAM:
			info["note"] = "PRESS ESC AND JOIN THE MATCH TO PLAY"
	hud.set_spectator(info)


## A player's card and level as the scoreboard shows them.
func _card_of(cl: int) -> Dictionary:
	for r in standings():
		if int(r["client"]) == cl:
			return {"card": r.get("card", {}), "level": int(r.get("level", 1))}
	return {"card": {}, "level": 1}


## Spectator keys: the weapon wheel cycles players, Jump leaves the followed player for the free
## camera (Cmd_Follow_f without a name stops following), Use item toggles the chase camera and
## Gesture the auto-director. Fire is id's own: the usercmd's attack follows the next player.
## Returns true when the key was a spectator's.
func _spectator_input(event: InputEvent) -> bool:
	if not _spectating_now or _ended:
		return false   # (at the intermission every unknown command is chat)
	var ps := feed.player_state(me)
	if event.is_action_pressed("q3_attack"):
		_director_hold = DIRECTOR_HOLD   # id follows the next player; the director lets you have it
		_cine_follow = {}
		return false
	if event.is_action_pressed("q3_weapnext") or event.is_action_pressed("q3_weapprev"):
		_cmd("follownext" if event.is_action_pressed("q3_weapnext") else "followprev")
		_director_hold = DIRECTOR_HOLD
		_cine_follow = {}
		return true
	if following(ps) and event.is_action_pressed("q3_jump"):
		_cmd("follow")
		director = false   # the free camera is your choice
		_director_forced = false
		_cine_follow = {}
		return true
	if event.is_action_pressed("q3_use_item"):
		# their eyes, the chase camera, the cinematic camera; free, it starts the cinematic camera
		var next: String = "cinematic" if not following(ps) else SPEC_VIEWS[(SPEC_VIEWS.find(spec_view) + 1) % SPEC_VIEWS.size()]
		set_spec_view(next)
		UiSound.play("move")
		return true
	if event.is_action_pressed("q3_gesture"):
		director = not director
		_director_forced = false   # yours now
		_director_hold = 0.0
		_cine_follow = {}
		UiSound.play("move")
		return true
	for w in range(1, 10):
		if event.is_action_pressed("q3_weapon%d" % w):
			return true
	return false


## The pause menu's Spectate / Join match. id's SetTeam decides: a duel's waiting line and team
## balance say when you're in.
func set_spectating(on: bool) -> void:
	if _ended:
		return
	if on:
		Q3Server.set_cvar("g_readyUp", "0")   # a warmup waiting on you would never end
	_cmd("team spectator" if on else "team free")


## Whether the pause menu offers Spectate: not in the campaign (the match is yours to win), and in a
## duel only with someone waiting to take your place (id's tournament would pull you straight back).
func can_spectate() -> bool:
	if setup.source in ["campaign", "training"]:
		return false
	if GAMETYPES.get(setup.mode, 0) == E.GT_TOURNAMENT:
		# id's AddTournamentPlayer pulls a free spectator in: only with another one waiting
		for cl in E.MAX_CLIENTS:
			if cl != me and feed.client_state(cl) >= 4:
				var ps := feed.player_state(cl)
				if not ps.is_empty() and int(ps["persistant"][E.PERS_TEAM]) == E.TEAM_SPECTATOR and not following(ps):
					return true
		return false
	return true


## Follows a player from the scoreboard (Tab, right click, then a row).
func _follow_from_board(cl: int) -> void:
	if cl == me or not _spectating_now or _ended:
		return
	_cmd("follow %d" % cl)
	_director_hold = DIRECTOR_HOLD
	_cine_follow = {}
	scoreboard.set_interactive(false)


## The auto-director: twice a second it scores every player in view: a flag carrier first, then
## whoever just fragged, a powerup holder, anyone firing and anyone with enemies close by. It
## stays with a pick for DIRECTOR_HOLD seconds unless nobody is followed.
func _direct(ps: Dictionary, delta: float) -> void:
	if not director or not _spectating_now or _ended:
		return
	_director_t -= delta
	_director_hold -= delta
	var current := int(ps["clientNum"]) if following(ps) else -1
	if not _cine_follow.is_empty() and float(feed.time()) >= float(_cine_follow["at"]) and spec_view == "cinematic":
		# the cinematic camera's cut to whoever fragged the one it watched
		var killer := int(_cine_follow["client"])
		_cine_follow = {}
		if killer != current and feed.client_state(killer) >= 4:
			_cmd("follow %d" % killer)
			_director_hold = CINE_HOLD
			return
	if _director_t > 0.0:
		return
	_director_t = 0.5
	var flags := (1 << E.PW_REDFLAG) | (1 << E.PW_BLUEFLAG) | (1 << E.PW_NEUTRALFLAG)
	var players: Array = []
	for es in feed.snapshot(me).get("entities", []):
		var n := int(es["number"])
		if int(es["eType"]) == E.ET_PLAYER and n < E.MAX_CLIENTS and n != me and not (int(es["eFlags"]) & E.EF_DEAD):
			players.append(es)
	var best := -1
	var best_score := -1.0
	var now := float(feed.time())
	if current >= 0 and int(ps["stats"][E.STAT_HEALTH]) > 0:
		# the followed player is never in your snapshot (sv_snapshot.c): their playerState stands in
		var pw := 0
		var pws: PackedInt32Array = ps["powerups"]
		for i in pws.size():
			if pws[i] > 0:
				pw |= 1 << i
		players.append({"number": current, "powerups": pw, "eFlags": int(ps["eFlags"]), "pos": {"trBase": ps["origin"]}})
	for es in players:
		var n := int(es["number"])
		var p: Vector3 = es["pos"]["trBase"]
		var score := 0.0
		var pw := int(es["powerups"])
		if pw & flags:
			score += 100.0
		elif pw:
			score += 20.0
		if int(_last_frag.get("attacker", -1)) == n and now - float(_last_frag.get("at", -1e9)) < 5000.0:
			score += 60.0
		if int(es["eFlags"]) & E.EF_FIRING:
			score += 15.0
		for o in players:
			if o != es and p.distance_to(o["pos"]["trBase"]) < 800.0:
				score += 8.0
		if n == current:
			score += 12.0   # a pick holds unless someone beats it
		if score > best_score:
			best_score = score
			best = n
	if best >= 0 and best != current and (_director_hold <= 0.0 or current < 0):
		_cmd("follow %d" % best)
		_director_hold = CINE_HOLD if spec_view == "cinematic" else DIRECTOR_HOLD


## The intermission while spectating: who won, no XP (nothing was played), and the replay.
func _end_as_spectator(rows: Array) -> void:
	_finish_recording({"ended": "intermission", "won": false, "place": 0, "tied": false, "result": "SPECTATED"})
	var winner := String(rows[0].get("plain", "")) if rows.size() else ""
	var line := "%s wins" % winner.to_upper()
	if feed.cvar("g_gametype").to_int() >= E.GT_TEAM:
		# a team won, not its top fragger (cg_scoreboard.c's team line)
		var ts := Vector2i(int(feed.configstring(E.CS_SCORES1)), int(feed.configstring(E.CS_SCORES2)))
		line = "Teams are tied at %d" % ts.x if ts.x == ts.y else ("Red leads %d to %d" % [ts.x, ts.y] if ts.x > ts.y \
			else "Blue leads %d to %d" % [ts.y, ts.x])
	elif winner != "":
		hud.announce("sound/player/announce/%s_wins.wav" % winner.to_lower())
	var total := Q3Profile.total_xp()
	_results = Q3ResultsScreen.new()
	add_child(_results)
	_results.show_results({"won": false, "title": "MATCH OVER", "subtitle": "%s  -  %s" % [_match_title(), line],
		"standings": rows, "medals": {}, "xp": {"before": total, "after": total, "lines": []}, "replay": replay_path, "me": me, "camera": cam})
	_results.taunt_requested.connect(_send_taunt)
	_results.watch_requested.connect(func():
		_stop_server()
		Session.watch_replay(replay_path, 0, ""))
	_results.finished.connect(func():
		_stop_server()
		if setup.source == "editor":
			Session.back_to_editor()
		else:
			Session.to_main_menu())


# ---------------------------------------------------------------- the cinematic camera
# Spectating, Use item's third view (or Settings > Gameplay > Spectator camera), and the replay
# viewer's CINEMATIC camera: Q3CineCam's broadcast-style shots of the followed (or watched) player,
# cut to what happens to them. Spectating, it turns the auto-director on (Gesture still turns it
# off) and holds each pick for CINE_HOLD; when the player it watches is fragged it stays on the
# body, then follows the killer. It moves only the camera.

## ms after the one watched is fragged to the cut to the killer: before they can respawn (1.7 s in
## id's rules, 2.1 s in Quake Live's), whose teleport would cut a second time
const CINE_TO_KILLER := 1400.0
var _cine: Q3CineCam
var _cine_shown := false           ## the cinematic camera drew last frame (the HUD parts it hides come back)
var _cine_client := -1             ## whose powerups _cine_powerups holds
var _cine_powerups := 0
var _cine_follow := {}             ## the auto-director's cut to a killer: {client, at (server ms)}
var _director_forced := false      ## the cinematic camera turned the auto-director on (leaving it turns it off)


## Switches the spectator's view of the followed player ("first", "chase", "cinematic"). The
## cinematic camera brings the auto-director (it follows someone at once when nobody is followed).
func set_spec_view(view: String) -> void:
	if not view in SPEC_VIEWS or (view == spec_view and view != "cinematic"):
		return
	_cine_follow = {}
	if view == "cinematic":
		# (again from the free camera: it picks someone to follow)
		if not director:
			director = true
			_director_forced = true
			_director_hold = 0.0
		_director_t = 0.0
	elif spec_view == "cinematic" and _director_forced:
		director = false
		_director_forced = false
	spec_view = view
	_cine_reset()


## The cinematic camera's clock: a replay's playhead (it stops with a pause and follows the speed),
## else game time (Q3.game_ms), which only moves forward. _now_ms() (server time plus the frame's
## leftover ms) steps back a hair between server frames, and the camera took each step for a jump.
func _cine_ms() -> float:
	return replay_feed.playhead if replay_feed else Q3.game_ms


## Forgets the cinematic camera's shot and what it knew of the player watched (a jump, a new view).
func _cine_reset() -> void:
	if _cine:
		_cine.reset()
	_cine_client = -1


## The cinematic camera on `cl` (their playerState `ps`; `body`: their ragdoll while they're down).
func _cine_camera(ps: Dictionary, cl: int, body: Q3Character) -> void:
	if _cine == null:
		_cine = Q3CineCam.new()
	var dead := int(ps["pm_type"]) == E.PM_DEAD
	var origin: Vector3 = ps["origin"]
	var look := origin + Vector3(0, 0, 10)
	if dead and body and is_instance_valid(body):
		look = _godot_to_id(body.body_center())
	var others: Array = []
	for es in feed.snapshot(-1 if replay_feed else me).get("entities", []):
		var n := int(es["number"])
		if int(es["eType"]) == E.ET_PLAYER and n < E.MAX_CLIENTS and n != cl and n != me and not (int(es["eFlags"]) & E.EF_DEAD):
			others.append(Vector3(es["pos"]["trBase"]) + Vector3(0, 0, 10))
	_cine_events(ps, cl)
	cam.global_transform = _cine.update(feed, {"client": cl, "origin": origin, "vel": ps["velocity"],
		"yaw": float((ps["viewangles"] as Vector3).y), "dead": dead, "look": look, "others": others}, _cine_ms())
	cam.fov = _cine.fov


## Cuts timed to the watched player: a powerup or a flag taken (a new bit in their powerups).
func _cine_events(ps: Dictionary, cl: int) -> void:
	var bits := 0
	var pws: PackedInt32Array = ps["powerups"]
	for i in mini(pws.size(), E.PW_NEUTRALFLAG + 1):
		if pws[i] > 0:
			bits |= 1 << i
	if cl == _cine_client:
		var new := bits & ~_cine_powerups
		var flags := (1 << E.PW_REDFLAG) | (1 << E.PW_BLUEFLAG) | (1 << E.PW_NEUTRALFLAG)
		if new & flags:
			_cine.hint("flag", _cine_ms())
		elif new:
			_cine.hint("powerup", _cine_ms())
	_cine_client = cl
	_cine_powerups = bits


## Cuts timed to frags: the one watched fragged someone (a quick orbit), or was fragged (the shot
## stays on the body; spectating, the auto-director then follows the killer).
func _cine_obituary(attacker: int, target: int, world: bool) -> void:
	if _cine == null or not _cine_on():
		return
	var watched := _cine_client
	if attacker == watched and not world:
		_cine.hint("frag", _cine_ms())
	elif target == watched:
		_cine.hint("died", _cine_ms())
		if not world and director and replay_feed == null:
			_cine_follow = {"client": attacker, "at": float(feed.time()) + CINE_TO_KILLER}


## Whether the cinematic camera is the view now.
func _cine_on() -> bool:
	if replay_feed:
		return cam_mode == "cinematic"
	return _spectating_now and spec_view == "cinematic" and following(feed.player_state(me))


## Spectating under the cinematic camera, the middle of the screen is a body, not an aim: no
## crosshair, speedometer or weapon row (they come back with the HUD's layout after).
func _cine_hud(on: bool) -> void:
	if on:
		hud.crosshair.visible = false
		hud.speedometer.visible = false
		hud.weapon_bar.visible = false
	elif _cine_shown:
		hud.layout()
	_cine_shown = on


# ---------------------------------------------------------------- the death cam (REPLAY_PLAN.md R6)
# While you're dead (Settings > Gameplay > Death cam, on by default) the camera leaves your eyes and
# follows your own ragdoll from behind and above (Q3ChaseCam, so walls pull it in); about a second
# later KILLED BY and the killer's player card come up (bots' cards for bots), or id's own
# obituary line when nobody did it. Respawning stays id's: fire works when the game allows it
# (1.7 s, g_combat.c player_die), and the line under the card counts down to it.

const DEATH_CARD_DELAY := 1000.0   ## ms after the death: the body has mostly settled
const RESPAWN_DELAY := 1700.0      ## g_combat.c: respawnTime = level.time + 1700


## The match plays Quake Live's rules (g_ruleset, patch 0005; QUAKE_LIVE_PLAN.md). Replays and demos
## from before it have no g_ruleset and were id 1.32's.
func ql_rules() -> bool:
	return not feed.cvar("g_ruleset").to_lower() in ["", "classic"]
## cg_event.c CG_Obituary's lines for deaths nobody caused, then for your own weapon
const WORLD_DEATHS := {E.MOD_SUICIDE: "suicides", E.MOD_FALLING: "cratered", E.MOD_CRUSH: "was squished",
	E.MOD_WATER: "sank like a rock", E.MOD_SLIME: "melted", E.MOD_LAVA: "does a back flip into the lava",
	E.MOD_TARGET_LASER: "saw the light", E.MOD_TRIGGER_HURT: "was in the wrong place"}
const SELF_DEATHS := {E.MOD_GRENADE_SPLASH: "tripped on his own grenade", E.MOD_ROCKET_SPLASH: "blew himself up",
	E.MOD_PLASMA_SPLASH: "melted himself", E.MOD_BFG_SPLASH: "should have used a smaller gun"}
## animation.cfg "sex": cgame's gender for "herself" / "itself"
const FEMALE_MODELS := ["crash", "hunter", "lucy", "major", "mynx", "slash", "sorlag"]
const NEUTER_MODELS := ["klesk", "orbb", "tankjr"]
const MOD_WEAPONS := {E.MOD_SHOTGUN: "Shotgun", E.MOD_GAUNTLET: "Gauntlet", E.MOD_MACHINEGUN: "Machinegun",
	E.MOD_GRENADE: "Grenade Launcher", E.MOD_GRENADE_SPLASH: "Grenade Launcher", E.MOD_ROCKET: "Rocket Launcher",
	E.MOD_ROCKET_SPLASH: "Rocket Launcher", E.MOD_PLASMA: "Plasma Gun", E.MOD_PLASMA_SPLASH: "Plasma Gun",
	E.MOD_RAILGUN: "Railgun", E.MOD_LIGHTNING: "Lightning Gun", E.MOD_BFG: "BFG10K", E.MOD_BFG_SPLASH: "BFG10K",
	E.MOD_GRAPPLE: "Grappling Hook"}
var _deaths: Dictionary = {}       ## client -> the obituary that last killed them: {attacker, means}
var _death_cards: Dictionary = {}  ## client -> the card for their current death
var _death_at := -1.0              ## when the watched player went down (ms), -1 while alive
var _death_yaw := 0.0
var _death_chase: Q3ChaseCam


## Whether the death cam runs: the setting, and someone's eyes to leave (not the free spectator).
func _death_cam_on() -> bool:
	return bool(Settings.get_value("gameplay/death_cam")) and client.local_client >= 0


## Every frame: while the watched player is dead, the camera on their body and the card. Returns
## true when it placed the camera.
func _update_death(ps: Dictionary, delta: float) -> bool:
	client.own_death = _death_cam_on()
	var dead := not ps.is_empty() and int(ps["pm_type"]) == E.PM_DEAD
	if not dead:
		if _death_at >= 0.0:
			_death_at = -1.0
			hud.set_death({})
			hud.respawn_text = "" if replay_feed else "FIRE TO RESPAWN"
		return false
	var now := _now_ms()
	if _death_at < 0.0:
		_death_at = now
		_death_yaw = float((ps["viewangles"] as Vector3).y)
		if _death_chase == null:
			_death_chase = Q3ChaseCam.new()
		_death_chase.reset()
	# id's respawn: fire works 1.7 s after the death; g_forcerespawn brings you back by itself.
	# Quake Live's (patch 0005): fire works after g_respawn_delay_min, and g_respawn_delay_max
	# brings you back
	var delay := RESPAWN_DELAY
	var forced := feed.cvar("g_forcerespawn").to_int()
	if ql_rules():
		delay = feed.cvar("g_respawn_delay_min").to_float()
		forced = 0
		var auto_ms := feed.cvar("g_respawn_delay_max").to_float()
		if auto_ms > 0.0:
			forced = ceili((auto_ms - delay) / 1000.0)
	var wait := (_death_at + delay - now) / 1000.0
	if replay_feed:
		hud.respawn_text = ""
	elif _spectating_now:
		hud.respawn_text = "FIRE  -  NEXT PLAYER"   # SpectatorThink: fire follows the next one
	else:
		hud.respawn_text = ("RESPAWN IN %.1f" % wait) if wait > 0.0 else ("FIRE TO RESPAWN" + (
			"  -  AUTO IN %d" % ceili(forced - (now - _death_at - delay) / 1000.0) if forced > 0 else ""))
	if not _death_cam_on():
		return false
	if now - _death_at >= DEATH_CARD_DELAY:
		var who := int(ps["clientNum"])
		if not _death_cards.has(who):   # built once a death (standings() and the bot cards cost)
			_death_cards[who] = _death_card(who)
		hud.set_death(_death_cards[who])
	var body := client.own_body
	if body == null or not is_instance_valid(body):
		return false
	_death_chase.distance = 130.0
	_death_chase.height = 0.0
	cam.global_transform = _death_chase.place(feed, _godot_to_id(body.body_center()), _death_yaw, 34.0, delta, int(ps["clientNum"]), 0.0)
	cam.fov = Settings.fov_vertical(float(Settings.get_value("video/fov")))
	if _gun:
		_gun.visible = false
	return true


## What the card says about `who`'s death: the killer's card and weapon, or id's obituary line.
func _death_card(who: int) -> Dictionary:
	var d: Dictionary = _deaths.get(who, {})
	if d.is_empty():
		return {}
	var attacker := int(d.get("attacker", -1))
	var means := int(d.get("means", E.MOD_UNKNOWN))
	# CG_Obituary: killing yourself overrides the world's lines (/kill is "killed himself"), and
	# the world's lines win over a player who set it off (a laser's activator)
	if attacker == who:
		var model := String(Q3Host.parse_info(feed.configstring(E.CS_PLAYERS + who)).get("model", "")).get_slice("/", 0).to_lower()
		var self_line: String = SELF_DEATHS.get(means, "killed himself")
		if model in FEMALE_MODELS:
			self_line = self_line.replace("himself", "herself").replace("his own", "her own")
		elif model in NEUTER_MODELS:
			self_line = self_line.replace("himself", "itself").replace("his own", "its own")
		return {"line": "%s^7 %s." % [_name(who), self_line]}
	if WORLD_DEATHS.has(means):
		return {"line": "%s^7 %s." % [_name(who), WORLD_DEATHS[means]]}
	if attacker >= 0 and attacker < E.MAX_CLIENTS:
		var c := _card_of(attacker)
		var weapon := ""
		if means == E.MOD_TELEFRAG:
			weapon = "TELEFRAGGED"
		elif MOD_WEAPONS.has(means):
			weapon = "WITH THE " + String(MOD_WEAPONS[means]).to_upper()
		return {"name": _name(attacker), "card": c["card"], "level": c["level"], "weapon": weapon}
	return {"line": "%s^7 died." % _name(who)}
