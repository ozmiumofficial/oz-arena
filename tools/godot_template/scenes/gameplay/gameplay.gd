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

const E := preload("res://scripts/game/q3_enums.gd")
## weapons2/<dir> per weapon number, for the hand tags in library/props/view_weapons.json
const WEAPON_DIRS := ["", "gauntlet", "machinegun", "shotgun", "grenadel", "rocketl", "lightning", "railgun",
	"plasma", "bfg", "grapple"]
const LAND_DEFLECT_TIME := 150.0
const LAND_RETURN_TIME := 300.0
## g_gametype for the setup's mode (bg_public.h gametype_t). Clan Arena, Freeze Tag and Instagib
## play as their base types until their rules land in the game module.
const GAMETYPES := {"ffa": 0, "duel": 1, "tdm": 3, "ctf": 4, "clan_arena": 3, "freeze_tag": 3, "instagib": 0}

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


func _ready() -> void:
	if not InputMap.has_action("q3_ready"):
		InputMap.add_action("q3_ready")
		var f3 := InputEventKey.new()
		f3.physical_keycode = KEY_F3
		InputMap.action_add_event("q3_ready", f3)
	setup = Session.setup
	if setup == null:
		setup = Session.new_setup("ffa", "q3dm17")
		setup.bots = [{"name": "Sarge", "skill": 3, "team": ""}, {"name": "Xaero", "skill": 3, "team": ""},
			{"name": "Crash", "skill": 3, "team": ""}]
	_error = _start_match()
	_build_view()
	if _error != "":
		hud.fatal("Couldn't start the match:\n" + _error + "\n\nEscape: back to the menu")
		return
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


func _start_match() -> String:
	var err := Q3Host.start()
	if err != "":
		return err
	var rules := setup.effective_rules()
	var mods := setup.effective_modifiers()
	var campaign := setup.source in ["campaign", "training"]
	var cvars := {
		"g_gametype": E.GT_SINGLE_PLAYER if campaign else GAMETYPES.get(setup.mode, 0),
		"g_spSkill": _campaign_skill(),
		"fraglimit": rules.get("fraglimit", 20), "timelimit": rules.get("timelimit", 10),
		"capturelimit": rules.get("capturelimit", 8),
		"g_doWarmup": 1 if int(rules.get("warmup", 0)) > 0 else 0, "g_warmup": rules.get("warmup", 0),
		# ready-up (tools/q3core/patches/0003): with a warmup, the countdown waits for F3 from every human
		"g_readyUp": 1 if int(rules.get("warmup", 0)) > 0 and not campaign else 0,
		"g_gravity": mods.get("gravity", 800), "g_quadfactor": mods.get("quad_factor", 3),
		"g_weaponrespawn": mods.get("weapon_respawn", 5), "g_friendlyfire": 1 if mods.get("friendly_fire", false) else 0,
		"sv_maxclients": maxi(int(rules.get("max_players", 16)), setup.bots.size() + 1),
	}
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
	me = Q3Server.connect_local(Q3Host.userinfo(p))
	if me < 0:
		return "joining: " + Q3Server.error()
	_client_time = Q3Server.time()
	var ps := Q3Server.player_state(me)
	if not ps.is_empty():
		_weapon = int(ps["weapon"])
	return ""


func _build_view() -> void:
	var path := Q3MapBuilder.scene_path(setup.map)
	if ResourceLoader.exists(path):
		map_scene = (load(path) as PackedScene).instantiate() as Q3Map
		add_child(map_scene)
	client = Q3Client.new()
	client.name = "Client"
	client.map = map_scene
	client.local_client = me
	add_child(client)
	client.obituary.connect(_on_obituary)
	client.landed.connect(func(change: float):
		_land_change = change
		_land_time = _now_ms())
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
	add_child(hud)
	hud.chat.sent.connect(_say)
	hud.chat.closed.connect(func():
		_chat_held.clear()
		for a in USERCMD_ACTIONS:
			if Input.is_action_pressed(a):
				_chat_held.append(a))
	hud.chat.names = func() -> Array: return standings().map(func(r): return r["plain"])
	client.local_event.connect(_on_local_event)
	music = Q3Music.new()
	music.name = "Music"
	add_child(music)
	_build_pause_menu()
	scoreboard = Q3Scoreboard.new()
	add_child(scoreboard)


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("ui_cancel"):
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
	if (event.is_action_pressed("q3_chat") or event.is_action_pressed("q3_teamchat")) and me >= 0 and _error == "" and not _ended:
		hud.chat.open(event.is_action_pressed("q3_teamchat"))
		get_viewport().set_input_as_handled()
		return
	if event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		# cl_input.c CL_MouseMove: m_yaw / m_pitch 0.022 degrees per count times sensitivity
		var sens := float(Settings.get_value("mouse/sensitivity")) * Settings.M_YAW
		var inv := -1.0 if Settings.get_value("mouse/invert") else 1.0
		_view.y -= event.relative.x * sens
		_view.x = clampf(_view.x + event.relative.y * sens * inv, -89.0, 89.0)
	for w in range(1, 10):
		if event.is_action_pressed("q3_weapon%d" % w):
			_select_weapon(w)
	# Tab + right click: the mouse for the scoreboard's player cards
	if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_RIGHT \
			and scoreboard.visible and Input.is_action_pressed("q3_scores"):
		scoreboard.set_interactive(not scoreboard.interactive)
		get_viewport().set_input_as_handled()
		return
	if event.is_action_pressed("q3_ready") and me >= 0:
		Q3Server.client_command(me, "ready")
		_ready_sent = not _ready_sent
	if event.is_action_pressed("q3_weapnext"):
		_cycle_weapon(1)
	if event.is_action_pressed("q3_weapprev"):
		_cycle_weapon(-1)
	if event is InputEventMouseButton and event.pressed and Input.mouse_mode != Input.MOUSE_MODE_CAPTURED and _error == "" \
			and not scoreboard.interactive:
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


func _process(delta: float) -> void:
	if _error != "" or not Q3Server.running() or _paused:
		return
	# the client clock runs in whole milliseconds, like Sys_Milliseconds
	_acc += delta * 1000.0
	var ms := int(_acc)
	_acc -= ms
	if ms > 0:
		_client_time += ms
		_send_usercmd()
		if Q3Server.frame(ms) != 0:
			_error = Q3Server.error()
			hud.fatal("The match stopped: " + _error + "\n\nEscape: back to the menu")
			return
	for c in Q3Server.next_commands(me):
		_server_command(c)
	var ps := Q3Server.player_state(me)
	client.apply_snapshot(Q3Server.snapshot(me), ps)
	if not _ended:
		music.play(Q3Server.configstring(E.CS_MUSIC))   # CG_StartMusic, again when CS_MUSIC changes
	client.animate(float(Q3Server.time()) + _acc)
	_update_camera(ps)
	_update_gun(ps)
	client.present(ps, cam.global_position)
	_update_hud(ps)
	_board_t -= delta
	# cg_draw.c CG_DrawScoreboard: the scores show while Tab is held, and while you're dead
	var dead_now := not ps.is_empty() and int(ps["pm_type"]) == E.PM_DEAD
	var show_board := ((Input.is_action_pressed("q3_scores") and not hud.chat.typing) or dead_now) and not _ended
	scoreboard.visible = show_board
	if scoreboard.interactive and not Input.is_action_pressed("q3_scores"):
		scoreboard.set_interactive(false)
	if show_board and _board_t <= 0.0:
		_board_t = 0.25
		var ts := Vector2i(-1, -1)
		if GAMETYPES.get(setup.mode, 0) >= E.GT_TEAM and setup.source == "menu":
			ts = Vector2i(int(Q3Server.configstring(E.CS_SCORES1)), int(Q3Server.configstring(E.CS_SCORES2)))
		scoreboard.refresh(standings(), _match_title(), ts,
			{"map": setup.map, "clock": hud.status.clock, "limit": hud.status.limit})
	if not _ended and Q3Server.configstring(E.CS_INTERMISSION) == "1":
		_end_match(ps)


func _send_usercmd() -> void:
	if hud.chat.typing:
		# CL_CmdButtons: BUTTON_TALK while a key catcher is up; bg_pmove.c PmoveSingle then holds still
		Q3Server.usercmd(me, _client_time, _view, E.BUTTON_TALK, _weapon, 0, 0, 0)
		return
	var fwd := 0
	var side := 0
	var up := 0
	var speed := 64 if _held(&"q3_walk") else 127
	if _held(&"q3_forward"): fwd += speed
	if _held(&"q3_back"): fwd -= speed
	if _held(&"q3_moveright"): side += speed
	if _held(&"q3_moveleft"): side -= speed
	if _held(&"q3_jump"): up += 127
	if _held(&"q3_crouch"): up -= 127
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
	Q3Server.usercmd(me, _client_time, _view, buttons, _weapon, fwd, side, up)


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
	if t != "" and me >= 0:
		Q3Server.client_command(me, "%s \"%s\"" % ["say_team" if team else "say", t])


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
		var fwd := Q3Client.forward_of(a)
		var yaw := deg_to_rad(a.y)
		var left := Vector3(-sin(yaw), cos(yaw), 0)
		if Settings.get_value("gameplay/view_bob"):
			a.x += vel.dot(fwd) * 0.002                     # cg_runpitch
			a.z -= vel.dot(left) * 0.005                    # cg_runroll
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
		if since < LAND_DEFLECT_TIME:
			eye.z += _land_change * since / LAND_DEFLECT_TIME
		elif since < LAND_DEFLECT_TIME + LAND_RETURN_TIME:
			eye.z += _land_change * (1.0 - (since - LAND_DEFLECT_TIME) / LAND_RETURN_TIME)
	cam.global_position = Q3.q3_to_godot(eye)
	cam.basis = Q3Client.angles_basis(a) * Basis(Vector3.UP, PI)


func _now_ms() -> float:
	return float(Q3Server.time()) + _acc


## The first-person weapon (cg_weapons.c CG_AddViewWeapon): the held weapon's model hung on its
## hand model's tag_weapon, whose frames are the only motion: the recoil (frames 1-6, in step with
## TORSO_ATTACK) and the weapon switch (6-14, TORSO_DROP then TORSO_RAISE). The hand itself sits
## at the eye turned by the run bob and the idle drift (CG_CalculateWeaponPosition), dips when
## you land, and drops 0.2 units per degree of fov over 90. Gameplay > Gun position picks the side.
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
	_gun.visible = side != "off" and not dead
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
	hand_z -= 0.2 * maxf(float(Settings.get_value("video/fov")) - 90.0, 0.0)
	# id view axes -> camera space: forward -Z, left -X, up +Y
	var hand := Basis(Vector3.UP, deg_to_rad(yaw)) * Basis(Vector3.RIGHT, deg_to_rad(-pitch)) * Basis(Vector3.FORWARD, deg_to_rad(roll))
	var tag := _hand_pose(ps, now, w)
	var o: Vector3 = tag[0]
	var ax: Array = tag[1]
	# left mirrors the side offset, center drops it (Quake Live's cg_drawGun choices)
	o.y *= -1.0 if side == "left" else 0.0 if side == "center" else 1.0
	var conv := func(v: Vector3) -> Vector3: return Vector3(-v.y, v.z, -v.x)
	var pos: Vector3 = hand * conv.call(o) * Q3.UNIT + Vector3(0, hand_z * Q3.UNIT, 0)
	# the tag's rows (gun forward, left, up) become the prop's axes (+X left, +Y up, +Z forward)
	var basis := Basis(conv.call(ax[1]), conv.call(ax[2]), conv.call(ax[0])).orthonormalized()
	_gun.transform = Transform3D(hand * basis, pos)


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
	var pose: Array
	if t < lerp_ms and not _hand_from.is_empty():
		var a: Array = _hand_from
		var b := _hand_tag(w, float(seq[0]))
		var k := t / lerp_ms
		pose = [(a[0] as Vector3).lerp(b[0], k), [0, 1, 2].map(func(i: int) -> Vector3:
			return (a[1][i] as Vector3).lerp(b[1][i], k).normalized())]
	else:
		pose = _hand_tag(w, float(seq[0]) + minf((t - lerp_ms) / lerp_ms if t > lerp_ms else 0.0, float(seq[1] - 1)))
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
	pause_bridge.quit_requested.connect(func():
		if Q3Server.running():
			Q3Server.exec_now("killserver")
		get_tree().quit())
	pause_bridge.restart_requested.connect(func():
		# q3_ui's Restart Arena: the same map from the start
		Q3Server.exec("map_restart 0")
		_set_paused(false))
	pause_bridge.ready_requested.connect(func():
		Q3Server.client_command(me, "ready")
		_ready_sent = not _ready_sent
		pause_layer.post({"type": "pause", "match": pause_info()}))
	pause_bridge.crosshair_preview.connect(func(w: int, hit: bool):
		hud.crosshair.preview_weapon = w
		if hit:
			hud.crosshair.hit())


## What the pause menu shows of the match: its title, the clock, the leaders, the ready-up state.
func pause_info() -> Dictionary:
	var warmup := Q3Server.configstring(E.CS_WARMUP)
	var ps := Q3Server.player_state(me)
	var rows: Array = []
	for r in standings():
		rows.append({"name": String(r["name"]), "score": int(r["score"]), "local": bool(r["local"])})
	return {"title": _match_title(), "map": setup.map, "map_name": String(Session.map_info(setup.map).get("name", setup.map)),
		"clock": hud.status.clock if hud else "", "warmup": warmup != "" and warmup != "0",
		"ready_up": Q3Server.cvar("g_readyUp") == "1", "ready": _ready_sent, "can_restart": true,
		"weapon": int(ps.get("weapon", 0)), "standings": rows}


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
	var gt := Q3Server.cvar("g_gametype").to_int()
	hud.update(ps, {"level_time": Q3Server.time(), "gametype": gt, "standings": standings})


# ---------------------------------------------------------------- scores and the end of the match

func _campaign_skill() -> int:
	return int(Session.campaign.progress.get("skill", 3)) if setup.source in ["campaign", "training"] else 3


## Everyone in the match, best first: name (with its color codes; "plain" without), score, deaths,
## ping, team, card and level. Bots get cards built from their own heads; you get your profile's card.
func standings() -> Array:
	var out: Array = []
	for cl in E.MAX_CLIENTS:
		if Q3Server.client_state(cl) < 4:
			continue
		var ps := Q3Server.player_state(cl)
		if ps.is_empty():
			continue
		var info := Q3Host.parse_info(Q3Server.configstring(E.CS_PLAYERS + cl))
		var name := String(info.get("n", "?"))
		var plain := Q3Colors.strip(name)
		var model := String(info.get("model", "sarge")).get_slice("/", 0).to_lower()
		var bot := info.has("skill")
		var row := {"client": cl, "name": name, "plain": plain, "score": int(ps["persistant"][E.PERS_SCORE]), "model": model,
			"skill": int(float(info.get("skill", "0"))),
			"frags": maxi(int(ps["persistant"][E.PERS_SCORE]), 0), "deaths": int(ps["persistant"][E.PERS_KILLED]),
			"ping": int(ps["ping"]), "team": int(ps["persistant"][E.PERS_TEAM]), "bot": bot, "local": cl == me}
		if cl == me:
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
	var place := 0
	for i in rows.size():
		if rows[i]["local"]:
			place = i
	var p: PackedInt32Array = ps.get("persistant", PackedInt32Array())
	var tied: bool = rows.size() > 1 and int(rows[0]["score"]) == int(rows[1]["score"])
	var won: bool = place == 0 and not tied and rows.size() > 1
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
		"seconds": int(Q3Server.time() / 1000.0)})
	# ui_sppostgame.c: "music music/win" (or loss) over the postgame, then "You win" or "<winner> wins"
	if campaign:
		music.play("music/win" if won else "music/loss")
	var winner := String(rows[0].get("plain", "")).to_lower() if rows.size() else ""
	client.play_sound("sound/player/announce/youwin.wav" if won else "sound/player/announce/%s_wins.wav" % winner, Vector3.ZERO, true)
	var ordinal := ["1st", "2nd", "3rd"]
	_results = Q3ResultsScreen.new()
	add_child(_results)
	_results.show_results({"won": won, "title": "VICTORY" if won else ("DRAW" if tied and place == 0 else "DEFEAT"),
		"subtitle": "%s  -  %s of %d" % [_match_title(), ordinal[place] if place < 3 else "%dth" % (place + 1), rows.size()],
		"standings": rows, "medals": medals if not perfect else medals.merged({"perfect": 1}), "xp": xp})
	_results.finished.connect(func():
		if Q3Server.running():
			Q3Server.exec_now("killserver")
		if movie != "" and Settings.get_value("gameplay/tier_movies"):
			Session.play_cinematics(PackedStringArray([movie]), Session.MAIN_MENU)
		else:
			Session.to_main_menu())


func _select_weapon(w: int) -> void:
	var ps := Q3Server.player_state(me)
	if ps.is_empty():
		return
	var owned := int(ps["stats"][E.STAT_WEAPONS])
	if owned & (1 << w) and (w == E.WP_GAUNTLET or int(ps["ammo"][w]) != 0):
		_weapon = w


## cg_weapons.c CG_NextWeapon / CG_PrevWeapon: the next owned weapon with ammo.
func _cycle_weapon(dir: int) -> void:
	var ps := Q3Server.player_state(me)
	if ps.is_empty():
		return
	var owned := int(ps["stats"][E.STAT_WEAPONS])
	var w := _weapon
	for i in 16:
		w = (w + dir + 16) % 16
		if w > 0 and w < 10 and owned & (1 << w) and (w == E.WP_GAUNTLET or int(ps["ammo"][w]) != 0):
			_weapon = w
			return


func _server_command(c: String) -> void:
	var cmd := c.get_slice(" ", 0)
	var args := c.split(" ", false)
	if cmd == "dmg" and args.size() >= 3:
		client.damage(int(args[1]), int(args[2]))
		return
	if cmd == "armorhit" and args.size() >= 3:
		client.armor_hit(int(args[1]), int(args[2]))
		return
	if cmd == "map_restart":
		hud.fight()   # cg_servercmds.c CG_MapRestart after a warmup
		return
	if cmd in ["print", "cp", "chat", "tchat"]:
		var text := c.substr(cmd.length() + 1).strip_edges().trim_prefix("\"").trim_suffix("\"").replace("\\n", "\n").strip_edges()
		if cmd == "cp":
			hud.center_print(text, 0.25)
		elif cmd == "print":
			if text != "":
				hud.notify(text.replace("\n", " "))
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
	var gt := Q3Server.cvar("g_gametype").to_int()
	var team := func(cl: int) -> int:
		return int(Q3Host.parse_info(Q3Server.configstring(E.CS_PLAYERS + cl)).get("t", "0")) if gt >= E.GT_TEAM else 0
	hud.obituary("" if world else _name(attacker), _name(target), means, not world and attacker == me, target == me,
		0 if world else team.call(attacker), team.call(target))
	if attacker == me and target != me:
		var ps := Q3Server.player_state(me)
		var pers: PackedInt32Array = ps.get("persistant", PackedInt32Array())
		if gt < E.GT_TEAM and pers.size():
			hud.center_print("You fragged %s\n%s place with %d" % [_name(target), Q3HudStyle.place(int(pers[E.PERS_RANK])),
				int(pers[E.PERS_SCORE])], 0.3)
		else:
			hud.center_print("You fragged %s" % _name(target), 0.3)


## A player's name with its color codes.
func _name(cl: int) -> String:
	return String(Q3Host.parse_info(Q3Server.configstring(E.CS_PLAYERS + cl)).get("n", "?"))


## The local player's predictable events (Q3Client.local_event): the speedometer's jump readout,
## the pickup display and cg_autoswitch, and CG_OutOfAmmoChange.
func _on_local_event(ev: int, parm: int) -> void:
	var ps := Q3Server.player_state(me)
	match ev:
		E.EV_JUMP:
			if not ps.is_empty():
				var v: Vector3 = ps["velocity"]
				hud.jumped(Vector2(v.x, v.y).length())
		E.EV_ITEM_PICKUP:
			var names: PackedStringArray = ClassDB.class_call_static("Q3Core", "item_classnames")
			if parm >= 1 and parm <= names.size():
				var cls := names[parm - 1]
				hud.pickup(cls)
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


func _leave() -> void:
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	if Q3Server.running():
		Q3Server.exec_now("killserver")
	Session.to_main_menu()
