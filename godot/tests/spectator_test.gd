extends Node
## Headless spectator mode check (REPLAY_PLAN.md R5): a free-for-all on q3dm17 with three bots,
## joined as a spectator from match setup. Free camera first, then fire follows a bot (id's
## SpectatorThink), the weapon wheel cycles, the chase and cinematic cameras, Jump back to the free camera, a
## scoreboard click, the auto-director, and Join / Spectate from the pause menu.
##   godot --headless --path . res://tests/spectator_test.tscn
## Prints "SPECTATOR TEST ok|FAILED"; exit code 1 on failure.

const E := preload("res://scripts/game/q3_enums.gd")

var _game: Q3Gameplay
var _ok := true
var _saved_director: Variant
var _saved_record: Variant
var _saved_view: Variant


func _ready() -> void:
	_saved_director = Settings.get_value("gameplay/spec_director")
	_saved_record = Settings.get_value("gameplay/record_replays")
	_saved_view = Settings.get_value("gameplay/spec_view")
	Settings.set_value("gameplay/spec_view", "first")
	Settings.set_value("gameplay/spec_director", false)
	Settings.set_value("gameplay/record_replays", false)
	var s := Session.new_setup("ffa", "q3dm17")
	s.bots = [{"name": "Sarge", "skill": 2, "team": ""}, {"name": "Xaero", "skill": 2, "team": ""},
		{"name": "Crash", "skill": 2, "team": ""}]
	s.rules = {"fraglimit": 0, "timelimit": 0, "warmup": 0}
	s.spectate = true
	_check("setup validates with spectate (%s)" % [s.validate()], s.validate().is_empty())
	var lone := Session.new_setup("ffa", "q3dm17")
	lone.spectate = true
	_check("spectating with no bots is refused", not lone.validate().is_empty())
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	_game.test_input = true
	add_child(_game)
	await _run()
	Settings.set_value("gameplay/spec_director", _saved_director)
	Settings.set_value("gameplay/record_replays", _saved_record)
	Settings.set_value("gameplay/spec_view", _saved_view)
	Settings.save()
	if Q3Server.running():
		Q3Server.exec_now("killserver")
	print("SPECTATOR TEST ", "ok" if _ok else "FAILED")
	get_tree().quit(0 if _ok else 1)


func _check(what: String, ok: bool) -> void:
	print(("  ok    " if ok else "  FAIL  ") + what)
	if not ok:
		_ok = false


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame


## Waits for that much game time (headless frames run faster than real time).
func _wait_ms(ms: int) -> void:
	var until := Q3Server.time() + ms
	while Q3Server.time() < until:
		await get_tree().process_frame


func _ps() -> Dictionary:
	return Q3Server.player_state(_game.me)


func _action(name: StringName) -> void:
	var ev := InputEventAction.new()
	ev.action = name
	ev.pressed = true
	_game._unhandled_input(ev)


func _run() -> void:
	await _frames(30)
	_check("the match runs (%s)" % _game._error, _game._error == "" and Q3Server.running())
	var ps := _ps()
	_check("you join on team spectator (teampref), flying free", int(ps["persistant"][E.PERS_TEAM]) == E.TEAM_SPECTATOR
		and int(ps["pm_type"]) == E.PM_SPECTATOR and not Q3Gameplay.following(ps))
	_check("the standings leave the spectator out (%d rows)" % _game.standings().size(),
		_game.standings().size() == 3 and not _game.standings().any(func(r): return r["local"]))
	_check("the HUD says SPECTATING", _game.hud.spectator.get("mode", "") == "free")
	# fire follows the next player (id's own SpectatorThink)
	Input.action_press("q3_attack")
	await _frames(4)
	Input.action_release("q3_attack")
	await _frames(4)
	ps = _ps()
	var followed := int(ps["clientNum"])
	_check("fire follows a player (client %d)" % followed, Q3Gameplay.following(ps) and followed != _game.me)
	_check("Q3Client sees through their eyes: their body isn't drawn", _game.client.local_client == followed
		and not _game.client.player_nodes().has(followed))
	_check("the HUD shows their card (%s)" % _game.hud.spectator.get("name", ""), _game.hud.spectator.get("mode", "") == "follow"
		and String(_game.hud.spectator.get("name", "")) != "" and _game.hud._spec_card.visible)
	var eye := Q3.q3_to_godot(Vector3(ps["origin"]) + Vector3(0, 0, float(ps["viewheight"])))
	_check("the camera is at their eye", _game.cam.global_position.distance_to(eye) < 0.6)
	# the weapon wheel cycles
	_action(&"q3_weapnext")
	await _frames(6)
	_check("the weapon wheel follows the next one (%d -> %d)" % [followed, int(_ps()["clientNum"])],
		Q3Gameplay.following(_ps()) and int(_ps()["clientNum"]) != followed)
	followed = int(_ps()["clientNum"])
	# the chase camera moves only the camera
	_action(&"q3_use_item")
	await _frames(6)
	ps = _ps()
	eye = Q3.q3_to_godot(Vector3(ps["origin"]) + Vector3(0, 0, float(ps["viewheight"])))
	var d := _game.cam.global_position.distance_to(eye)
	var body: Node3D = _game.client._self.get("node")
	_check("Use item: the chase camera, behind them (%.2f m) with their body drawn" % d, _game.spec_chase and d > 0.1 and d < 4.0
		and _game.client.self_visible and ((body != null and body.visible) or int(ps["pm_type"]) == E.PM_DEAD))
	_check("the chase camera hides the view gun", _game._gun == null or not _game._gun.visible)
	# again: the cinematic camera (and the auto-director with it), then back to their eyes
	_action(&"q3_use_item")
	await _frames(6)
	ps = _ps()
	_check("Use item again: the cinematic camera (%s shot), the auto-director on, the HUD says so" % [
		_game._cine.shot_name() if _game._cine else "no"], _game.spec_view == "cinematic" and _game._cine != null
		and _game._cine.shot_name() != "" and _game.director and _game.hud.spectator.get("mode", "") == "cinematic")
	_check("the cinematic lens is clear of the walls", Q3Server.point_contents(_game._cine.lens, -1) & 1 == 0
		and Q3Gameplay._godot_to_id(_game.cam.global_position).distance_to(_game._cine.lens) < 0.5)
	_action(&"q3_use_item")
	await _frames(3)
	_check("... and again: first person, the auto-director off as it was", _game.spec_view == "first" and not _game.director)
	# Jump: back to the free camera
	_action(&"q3_jump")
	await _frames(6)
	ps = _ps()
	_check("Jump leaves them for the free camera", not Q3Gameplay.following(ps) and int(ps["pm_type"]) == E.PM_SPECTATOR
		and _game.client.local_client == _game.me)
	# a scoreboard row follows that player
	var target := int(_game.standings()[0]["client"])
	_game.scoreboard.picked.emit(target)
	await _frames(6)
	_check("a scoreboard click follows that player (%d)" % target, Q3Gameplay.following(_ps()) and int(_ps()["clientNum"]) == target)
	_action(&"q3_jump")
	await _frames(6)
	# the auto-director picks someone by itself
	_action(&"q3_gesture")
	await _frames(90)
	_check("the auto-director follows the action (client %d)" % int(_ps()["clientNum"]), _game.director and Q3Gameplay.following(_ps()))
	_action(&"q3_gesture")
	# Join and Spectate from the pause menu
	await _wait_ms(5200)   # id's SetTeam: a team switch at most every 5 s
	_game.set_spectating(false)
	await _frames(20)
	ps = _ps()
	_check("Join match puts you in (team free, in the standings)", int(ps["persistant"][E.PERS_TEAM]) == E.TEAM_FREE
		and _game.standings().any(func(r): return r["local"]) and _game.hud.spectator.is_empty())
	await _wait_ms(5200)
	_game.set_spectating(true)
	await _frames(20)
	_check("Spectate takes you out again", int(_ps()["persistant"][E.PERS_TEAM]) == E.TEAM_SPECTATOR)
	_check("the pause menu knows you're spectating", _game.pause_info().get("spectating", false))
