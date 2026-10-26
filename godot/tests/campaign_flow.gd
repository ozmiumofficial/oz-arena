extends Node
## Headless check of a campaign arena from start to finish: the first arena of the campaign as the
## Single Player page starts it (GT_SINGLE_PLAYER, the arena's own bots at skill 5), the frag
## limit dropped to 1 so it ends fast, then the intermission, the results screen, the recorded
## result, the XP, and Continue going back to the menu (or the tier movie). The test player uses
## cheats (spdevmap, give all, god, setviewpos next to the bot) and aims for it, so it usually wins. The player's own
## campaign and profile are saved first and put back after.
##   godot --headless --path . res://tests/campaign_flow.tscn
## Prints "CAMPAIGN FLOW ok|FAILED".

const BACKUP := ["user://campaign.json", "user://profile.json"]

var _game: Q3Gameplay
var _t := 0.0
var _saved := {}
var _xp_before := 0
var _map := ""
var _ended_at := -1.0
var _hop_t := 0.0
var _side := 0
var _done := false
var _checks: Array[String] = []
var _ok := true


func _ready() -> void:
	for p in BACKUP:
		_saved[p] = FileAccess.get_file_as_string(p) if FileAccess.file_exists(p) else null
	Session.dry_run = true
	# a fresh campaign, whatever the player has won (their save is put back at the end)
	Session.campaign.progress["won"] = {}
	Session.campaign.progress["awards"] = {}
	_xp_before = Q3Profile.total_xp()
	_map = String(Session.campaign.tiers[0]["arenas"][0]["map"])
	var s := Session.campaign.setup_for(_map, 5)
	_check("campaign setup valid (%s, %d bots)" % [_map, s.bots.size()], s.validate().is_empty())
	s.set_meta("devmap", true)   # spdevmap: the test player gets give all and setviewpos
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	_game.test_input = true
	add_child(_game)
	print("source %s, g_gametype %s, g_spSkill %s" % [_game.setup.source, Q3Server.cvar("g_gametype"), Q3Server.cvar("g_spSkill")])


func _check(name: String, ok: bool) -> void:
	_checks.append(("ok   " if ok else "FAIL ") + name)
	_ok = _ok and ok


func _process(delta: float) -> void:
	if _done:
		return
	_t += delta
	if _t > 2.0 and Q3Server.running() and Q3Server.cvar("fraglimit") != "1":
		_check("single player game type", Q3Server.cvar("g_gametype") == "2")
		Q3Server.set_cvar("fraglimit", "1")
		Q3Server.set_cvar("timelimit", "2")   # a draw at worst, so the end flow always runs
		Q3Server.client_command(_game.me, "give all")
		Q3Server.client_command(_game.me, "god")
	_aim()
	if _t > 4.0 and _game.me >= 0:
		_game._select_weapon(7)
		# bg_pmove.c: fire held since the spawn doesn't shoot (PMF_RESPAWNED), so press it now
		if not Input.is_action_pressed("q3_attack"):
			Input.action_press("q3_attack")
	_hop_t -= delta
	if _hop_t <= 0.0 and _t > 4.0:
		_hop_t = 1.5
		_hop()
	if int(_t / 10.0) != int((_t - delta) / 10.0):
		var line := "t %d:" % int(_t)
		for cl in 4:
			var ps := Q3Server.player_state(cl)
			if not ps.is_empty():
				line += "  [%d hp %d score %d hits %d w %d ammo %d firing %d at %s]" % [cl, ps["stats"][0], ps["persistant"][0], ps["persistant"][1], ps["weapon"],
					ps["ammo"][ps["weapon"]], int(ps["eFlags"]) & 256, str((ps["origin"] as Vector3).round())]
		print(line, "  warmup ", Q3Server.configstring(5), " intermission ", Q3Server.configstring(22))
	if _game._results and _ended_at < 0.0:
		_ended_at = _t
		print("intermission at %.1f s" % _t)
	if _ended_at >= 0.0 and _t > _ended_at + 3.0:
		_finish()
	elif _t > 150.0:
		_check("the match ended within 150 s", false)
		_finish()


## The test player turns to the nearest live opponent and fires (the view is the usercmd's angles,
## which the server adds delta_angles to).
func _aim() -> void:
	var me := Q3Server.player_state(_game.me)
	if me.is_empty():
		return
	var eye: Vector3 = me["origin"] + Vector3(0, 0, float(me["viewheight"]))
	var best := Vector3.INF
	for cl in 8:
		if cl == _game.me or Q3Server.client_state(cl) < 4:
			continue
		var ps := Q3Server.player_state(cl)
		if ps.is_empty() or int(ps["stats"][0]) <= 0:
			continue
		var at: Vector3 = ps["origin"]
		if best == Vector3.INF or eye.distance_to(at) < eye.distance_to(best):
			best = at
	if best == Vector3.INF:
		return
	var d := best - eye
	var da: PackedInt32Array = me["delta_angles"]
	var yaw := rad_to_deg(atan2(d.y, d.x))
	var pitch := -rad_to_deg(atan2(d.z, Vector2(d.x, d.y).length()))
	_game._view = Vector3(pitch - da[0] * 360.0 / 65536.0, yaw - da[1] * 360.0 / 65536.0, 0)


## Teleports the test player 96 units from the nearest opponent (setviewpos, a cheat), from a
## different side each time until one has a clear shot.
func _hop() -> void:
	for cl in 8:
		if cl == _game.me or Q3Server.client_state(cl) < 4:
			continue
		var ps := Q3Server.player_state(cl)
		if ps.is_empty() or int(ps["stats"][0]) <= 0:
			continue
		var at: Vector3 = ps["origin"]
		_side += 1
		var a := _side * TAU / 8.0
		Q3Server.client_command(_game.me, "setviewpos %d %d %d 0" % [int(at.x + cos(a) * 96.0), int(at.y + sin(a) * 96.0), int(at.z) + 8])
		return


func _finish() -> void:
	_done = true
	Input.action_release("q3_attack")
	var rows: Array = _game.standings()
	var me_first: bool = rows.size() > 0 and rows[0]["local"]
	_check("intermission reached, results screen up", _game._ended and _game._results != null)
	if _game._results:
		_check("XP awarded (%d -> %d)" % [_xp_before, Q3Profile.total_xp()], Q3Profile.total_xp() > _xp_before)
		var won_recorded := Session.campaign.is_won(_map)
		print("standings: ", rows.map(func(r): return "%s %d" % [r["name"], r["score"]]))
		_check("result recorded (won %s, recorded %s)" % [me_first, won_recorded], won_recorded == me_first or not me_first)
		# Escape, as a player leaves the results: it's Continue (it used to leave and skip the tier movie)
		var esc := InputEventAction.new()
		esc.action = &"ui_cancel"
		esc.pressed = true
		Input.parse_input_event(esc)
		await get_tree().process_frame
		_check("Continue leaves for %s" % Session.last_request.get_file(), Session.last_request in [Session.MAIN_MENU, Session.CINEMATIC])
		_check("the menu opens on the campaign page (%s)" % Session.menu_page, Session.menu_page == "campaign")
		Session.menu_page = ""
		if me_first:
			_check("winning Training opens tier 1 and plays its movie", Session.campaign.tier_open(1) and Session.last_request == Session.CINEMATIC)
	for p in BACKUP:
		if _saved[p] == null:
			DirAccess.remove_absolute(ProjectSettings.globalize_path(p))
		else:
			var f := FileAccess.open(p, FileAccess.WRITE)
			f.store_string(_saved[p])
			f.close()
	for c in _checks:
		print(c)
	print("CAMPAIGN FLOW ", "ok" if _ok else "FAILED")
	if Q3Server.running():
		Q3Server.exec_now("killserver")
	get_tree().quit(0 if _ok else 1)
