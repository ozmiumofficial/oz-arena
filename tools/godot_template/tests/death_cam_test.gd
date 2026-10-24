extends Node
## Headless death cam check (REPLAY_PLAN.md R6): a devmap free-for-all on q3dm17 where a second
## local client ("Killer", given everything) machineguns the test player (no gib). Then: the camera
## leaves the eyes for the player's own ragdoll, behind and above it; after a second KILLED BY with
## Killer's card and weapon; the ruleset's respawn timing (fire does nothing until the delay, 2.1 s
## under Quake Live's rules or id's 1.7 s, then respawns); the body stays as a corpse; /kill (always
## a gib) builds no body and shows id's obituary line, no card; with the setting off the camera
## stays in the dead view.
##   godot --headless --path . res://tests/death_cam_test.tscn [-- ruleset=ql|classic]
##   godot --path . res://tests/death_cam_test.tscn -- out=C:/temp/dcam window=1920x1080   (also saves _body.png, _card.png)
## Prints "DEATH CAM TEST ok|FAILED"; exit code 1 on failure.

const E := preload("res://scripts/game/q3_enums.gd")

var _game: Q3Gameplay
var _ok := true
var _killer := -1
var _aim := Vector3.ZERO       ## the killer's usercmd angles
var _fire := false
var _saved: Dictionary = {}
var _out := ""
var _ruleset := ""             ## ruleset=: g_ruleset for this run (the game's default, Quake Live's, if empty)


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			_out = a.substr(4)
		if a.begins_with("ruleset="):
			_ruleset = a.substr(8)
	if _ruleset != "":
		Q3Host.start()
		Q3Server.set_cvar("g_ruleset", _ruleset)   # latched: before the map
	for k in ["gameplay/death_cam", "gameplay/record_replays"]:
		_saved[k] = Settings.get_value(k)
	Settings.set_value("gameplay/death_cam", true)
	Settings.set_value("gameplay/record_replays", false)
	var s := Session.new_setup("ffa", "q3dm17")
	s.bots = []
	s.rules = {"fraglimit": 0, "timelimit": 0, "warmup": 0}
	s.set_meta("devmap", true)
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	_game.test_input = true
	add_child(_game)
	await _run()
	for k in _saved:
		Settings.set_value(k, _saved[k])
	Settings.save()
	Input.action_release("q3_attack")
	if Q3Server.running():
		Q3Server.exec_now("killserver")
	print("DEATH CAM TEST ", "ok" if _ok else "FAILED")
	get_tree().quit(0 if _ok else 1)


func _check(what: String, ok: bool) -> void:
	print(("  ok    " if ok else "  FAIL  ") + what)
	if not ok:
		_ok = false


func _process(_delta: float) -> void:
	# the killer's own usercmds (Gameplay sends the test player's)
	if _killer >= 0 and Q3Server.running():
		if _fire:
			_aim_at_me()
		Q3Server.usercmd(_killer, Q3Server.time() + 8, _aim, 1 if _fire else 0, E.WP_MACHINEGUN, 0, 0, 0)


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame


func _wait_ms(ms: int) -> void:
	var until := Q3Server.time() + ms
	while Q3Server.time() < until:
		await get_tree().process_frame


func _ps(cl: int) -> Dictionary:
	return Q3Server.player_state(cl)


func _dead() -> bool:
	return int(_ps(_game.me).get("pm_type", 0)) == E.PM_DEAD


## The killer's usercmd angles that look at the test player's chest (view = cmd + delta_angles).
func _aim_at_me() -> void:
	var k := _ps(_killer)
	var me := _ps(_game.me)
	var d: Vector3 = Vector3(me["origin"]) - (Vector3(k["origin"]) + Vector3(0, 0, float(k["viewheight"])))
	var da: PackedInt32Array = k["delta_angles"]
	var yaw := rad_to_deg(atan2(d.y, d.x))
	var pitch := -rad_to_deg(atan2(d.z, Vector2(d.x, d.y).length()))
	_aim = Vector3(pitch - da[0] * 360.0 / 65536.0, yaw - da[1] * 360.0 / 65536.0, 0)


func _run() -> void:
	await _frames(20)
	_check("the match runs (%s)" % _game._error, _game._error == "" and Q3Server.running())
	_killer = Q3Server.connect_local(Q3Host.userinfo({"name": "^1Killer", "model": "klesk"}))
	_check("a second player joins (client %d)" % _killer, _killer >= 0)
	await _frames(10)
	Q3Server.client_command(_killer, "give all")
	# the killer machineguns the test player (no gib), who hops next to them until it lands
	var side := 0
	for attempt in 12:
		if _dead():
			break
		var at: Vector3 = _ps(_killer)["origin"]
		var a := side * TAU / 8.0
		side += 1
		# floor under the spot (q3dm17's edges drop into the void) and a clear line to the killer
		for tries in 16:
			var spot := at + Vector3(cos(a) * 160.0, sin(a) * 160.0, 8.0)
			var down := Q3Server.trace(spot, spot - Vector3(0, 0, 96), Vector3(-15, -15, -24), Vector3(15, 15, 32), _killer, 1)
			var sight := Q3Server.trace(at, spot, Vector3.ZERO, Vector3.ZERO, _killer, 1)
			if not down.is_empty() and float(down["fraction"]) < 1.0 and not down["startsolid"] and float(sight["fraction"]) == 1.0:
				break
			a = side * TAU / 8.0
			side += 1
		Q3Server.client_command(_game.me, "setviewpos %d %d %d %d" % [int(at.x + cos(a) * 160.0), int(at.y + sin(a) * 160.0),
			int(at.z) + 8, int(rad_to_deg(a)) + 180])   # facing the killer: the 400 ups shove goes toward them
		await _wait_ms(600)
		_aim_at_me()
		await _frames(2)
		_fire = true
		var until := Q3Server.time() + 4000
		while Q3Server.time() < until and not _dead():
			await get_tree().process_frame
		_fire = false
		await _wait_ms(200)
	_check("the killer frags the test player (health %d, deaths noted %s)" % [int(_ps(_game.me)["stats"][E.STAT_HEALTH]), _game._deaths],
		_dead())
	if not _dead():
		return
	var died := Q3Server.time()
	# the respawn delay: g_respawn_delay_min under Quake Live's rules (2.1 s), id's 1.7 s otherwise
	var delay := int(Q3Server.cvar("g_respawn_delay_min")) if _game.ql_rules() else 1700
	var secs := "%.1f s" % (delay / 1000.0)
	# fire right away: the game keeps you down for the delay
	Input.action_press("q3_attack")
	await _frames(3)
	Input.action_release("q3_attack")
	await _frames(3)
	_check("fire before %s doesn't respawn (%s)" % [secs, Q3Server.cvar("g_ruleset")], _dead())
	_check("the line counts down to the respawn (%s)" % _game.hud.respawn_text, _game.hud.respawn_text.begins_with("RESPAWN IN"))
	var body := _game.client.own_body
	_check("your own body goes limp where you died", body != null and is_instance_valid(body) and body.ragdolled)
	await _frames(4)
	await _shot("body")
	if body:
		var center := body.body_center()
		var d := _game.cam.global_position.distance_to(center)
		_check("the camera follows it from behind and above (%.2f m, %.2f m higher)" % [d, _game.cam.global_position.y - center.y],
			d > 0.5 and d < 5.0 and _game.cam.global_position.y > center.y)
	await _wait_ms(1100)
	await _shot("card")
	var card: Dictionary = _game.hud.death
	_check("then KILLED BY with the killer's card (%s, %s)" % [card.get("name", ""), card.get("weapon", "")],
		String(card.get("name", "")).contains("Killer") and card.has("card") and _game.hud._killer_card.visible
		and String(card.get("weapon", "")) == "WITH THE MACHINEGUN")
	await _wait_ms(maxi(delay + 100 - (Q3Server.time() - died), 0))
	_check("after %s the line says fire (%s)" % [secs, _game.hud.respawn_text], _game.hud.respawn_text.begins_with("FIRE TO RESPAWN"))
	Input.action_press("q3_attack")
	await _wait_ms(200)
	Input.action_release("q3_attack")
	await _frames(4)
	_check("fire respawns, as in Quake 3", not _dead())
	_check("the card goes and the body stays as a corpse", _game.hud.death.is_empty() and not _game.hud._killer_card.visible
		and _game.client.own_body == null and _game.client._corpses.has(body))
	# /kill: always a gib (g_combat.c player_die), so no body; id's obituary line, no card
	await _wait_ms(1000)
	Q3Server.client_command(_game.me, "kill")
	await _wait_ms(1300)
	_check("/kill shows id's line and no card (%s)" % _game.hud.death.get("line", ""), _dead()
		and String(_game.hud.death.get("line", "")).ends_with("killed himself.") and not _game.hud._killer_card.visible)
	_check("a gibbed player leaves no body to follow", _game.client.own_body == null)
	# the setting off: the dead view stays at the eyes
	Settings.set_value("gameplay/death_cam", false)
	await _wait_ms(2000)
	Input.action_press("q3_attack")
	await _wait_ms(200)
	Input.action_release("q3_attack")
	await _wait_ms(1000)
	Q3Server.client_command(_game.me, "kill")
	await _wait_ms(600)
	var ps := _ps(_game.me)
	var eye := Q3.q3_to_godot(Vector3(ps["origin"]) + Vector3(0, 0, float(ps["viewheight"])))
	_check("with Death cam off the view stays at the eyes (dead %s, %.2f m from the eye, body %s)" % [_dead(),
		_game.cam.global_position.distance_to(eye), _game.client.own_body], _dead() and _game.cam.global_position.distance_to(eye) < 0.3
		and _game.client.own_body == null)


func _shot(name: String) -> void:
	if _out == "" or DisplayServer.get_name() == "headless":
		return
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png("%s_%s.png" % [_out, name])
