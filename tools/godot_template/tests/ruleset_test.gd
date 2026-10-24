extends Node
## Quake Live's rules (patch 0005, QUAKE_LIVE_PLAN.md Q3) against id 1.32's, one ruleset per run:
##   godot --headless --path . res://tests/ruleset_test.tscn -- [ruleset=ql|classic]
## ql: every Quake Live cvar the game registers has tools/ql/ql_rules.json's default; BFG refire
## 300 ms on the server and in q3core's prediction; rockets 1000 ups; the machinegun drops on
## death; the dead respawn by themselves after g_respawn_delay_max + the /kill penalty (3.4 s).
## classic: 200 ms, 900 ups, no machinegun drop, still dead after 5 s (g_forcerespawn is 20 s).
## Both: the HUD's respawn line counts down the ruleset's delay (2.1 s, or 1.7 s).

const E := preload("res://scripts/game/q3_enums.gd")
const RULES_JSON := "../tools/ql/ql_rules.json"
const OURS := ["g_damage_", "g_splashdamage_", "g_splashradius_", "g_velocity_", "g_knockback_", "g_max_knockback",
	"weapon_reload_", "g_startingAmmo_", "g_startingHealth", "g_startingArmor", "g_startingWeapons", "g_respawn_delay_",
	"g_spawnDelay", "g_battleSuitDampen", "g_allowKill", "g_dropPowerups"]
const MG_ITEM := 10       # weapon_machinegun's index in bg_itemlist (an ET_ITEM's modelindex)

var ruleset := "ql"
var _game: Q3Gameplay
var _t := 0.0
var _fails: Array = []
var _notes: Array = []
var _bfg_time := 0          ## the most weaponTime seen after the BFG shot
var _rocket_speed := 0.0
var _mg_dropped := false
var _died_at := -1.0
var _respawned_at := -1.0
var _hud_text := ""          ## the HUD's respawn line half a second after death


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("ruleset="):
			ruleset = a.substr(8)
	Engine.max_fps = 60
	if Q3Host.start() != "":
		_finish(["engine: " + Q3Server.error()])
		return
	Q3Server.set_cvar("g_ruleset", ruleset)   # before the map: the game registers it with "ql"
	var s := Session.new_setup("ffa", "q3dm17")
	s.set_meta("devmap", true)
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	_game.test_input = true
	add_child(_game)


func _ps() -> Dictionary:
	return Q3Server.player_state(_game.me)


func _at(t: float, delta: float) -> bool:
	return _t >= t and _t - delta < t


func _process(delta: float) -> void:
	_t += delta
	if _game == null or _game.me < 0:
		return
	var ql := ruleset != "classic"
	if _at(2.0, delta):
		_check_cvars(ql)
		Q3Server.client_command(_game.me, "give all")
	if _at(2.5, delta):
		_game._select_weapon(E.WP_BFG)
	if _at(3.5, delta):
		_check_prediction(ql)
		Input.action_press("q3_attack")
	if _at(3.55, delta):
		Input.action_release("q3_attack")
	if _t > 3.5 and _t < 4.0:
		_bfg_time = maxi(_bfg_time, int(_ps().get("weaponTime", 0)))
	if _at(4.5, delta):
		_game._select_weapon(E.WP_ROCKET_LAUNCHER)
	if _at(5.5, delta):
		Input.action_press("q3_attack")
	if _at(5.55, delta):
		Input.action_release("q3_attack")
	if _t > 5.5 and _t < 5.9:
		for e in Q3Server.entities():
			if int(e["eType"]) == E.ET_MISSILE and int(e["weapon"]) == E.WP_ROCKET_LAUNCHER:
				_rocket_speed = maxf(_rocket_speed, (e["pos"]["trDelta"] as Vector3).length())
	if _at(6.5, delta):
		_game._select_weapon(E.WP_MACHINEGUN)
	if _at(7.5, delta):
		Q3Server.client_command(_game.me, "kill")
		_died_at = _t
	if _t > 7.6 and _t < 8.5:
		for e in Q3Server.entities():
			if int(e["eType"]) == E.ET_ITEM and int(e["modelindex"]) == MG_ITEM:
				_mg_dropped = true
	if _died_at > 0 and _respawned_at < 0 and _t > _died_at + 0.2 and int(_ps()["stats"][E.STAT_HEALTH]) > 0:
		_respawned_at = _t
	if _died_at > 0 and _at(_died_at + 0.5, delta):
		_hud_text = _game.hud.respawn_text
	if _t > 13.0:
		_judge(ql)


func _check_cvars(ql: bool) -> void:
	var f := FileAccess.open(ProjectSettings.globalize_path("res://").path_join(RULES_JSON), FileAccess.READ)
	if f == null:
		_fails.append("can't read " + RULES_JSON)
		return
	var cvars: Dictionary = JSON.parse_string(f.get_as_text())["cvars"]
	var checked := 0
	var unused: Array = []
	for name in cvars:
		if not OURS.any(func(p): return String(name).begins_with(p)):
			continue
		var v := Q3Server.cvar(name)
		if v == "":
			unused.append(name)      # Quake Live has it, the remaster doesn't use it (HMG, hook, falloff...)
			continue
		checked += 1
		var want := String(cvars[name]["default"])
		if ql and not is_equal_approx(v.to_float(), want.trim_suffix("f").to_float()):
			_fails.append("%s is %s, Quake Live's default is %s" % [name, v, want])
	if checked < 50:
		_fails.append("only %d Quake Live cvars registered" % checked)
	_notes.append("%d cvars match ql_rules.json; not used: %s" % [checked, ", ".join(unused)])
	if Q3Server.cvar("g_ruleset") != ruleset:
		_fails.append("g_ruleset is '%s'" % Q3Server.cvar("g_ruleset"))


## q3core's prediction (Q3Server.predict, BG_QLRules from the same cvars) fires the BFG with the
## ruleset's refire time.
func _check_prediction(ql: bool) -> void:
	var ps := _ps()
	if int(ps["weapon"]) != E.WP_BFG or int(ps["weaponTime"]) > 0:
		_fails.append("prediction: BFG not ready (weapon %d, weaponTime %d)" % [ps["weapon"], ps["weaponTime"]])
		return
	var t := int(ps["commandTime"]) + 8
	var pred := Q3Server.predict(_game.me, [{"serverTime": t, "buttons": 1, "weapon": E.WP_BFG}])
	var want := 300 if ql else 200
	if int(pred.get("weaponTime", -1)) != want:
		_fails.append("prediction: BFG weaponTime %s, want %d" % [pred.get("weaponTime", "none"), want])
	else:
		_notes.append("prediction: BFG weaponTime %d" % want)


func _judge(ql: bool) -> void:
	var bfg := 300 if ql else 200
	if _bfg_time < bfg - 20 or _bfg_time > bfg:
		_fails.append("server: BFG weaponTime peaked at %d, want about %d" % [_bfg_time, bfg])
	var speed := 1000.0 if ql else 900.0
	if absf(_rocket_speed - speed) > 2.0:
		_fails.append("rocket speed %.1f, want %d" % [_rocket_speed, speed])
	if _mg_dropped != ql:
		_fails.append("machinegun %s on death" % ("dropped" if _mg_dropped else "not dropped"))
	if ql:
		var after := _respawned_at - _died_at
		if _respawned_at < 0 or absf(after - 3.4) > 0.15:
			_fails.append("respawned %.2f s after /kill, want 3.4 (2.4 + 1 s penalty)" % after)
	elif _respawned_at > 0:
		_fails.append("respawned by itself %.2f s after /kill" % (_respawned_at - _died_at))
	# the HUD counts down the ruleset's delay (it can't know about the /kill penalty: nothing in the
	# remaster sends /kill, the test does)
	var want_hud := 1.6 if ql else 1.2
	if not _hud_text.begins_with("RESPAWN IN ") or absf(_hud_text.substr(11).to_float() - want_hud) > 0.15:
		_fails.append("HUD says '%s' 0.5 s after death, want 'RESPAWN IN %.1f'" % [_hud_text, want_hud])
	_notes.append("BFG %d ms, rocket %.0f ups, MG dropped %s, respawn %s" % [_bfg_time, _rocket_speed, _mg_dropped,
		"%.2f s" % (_respawned_at - _died_at) if _respawned_at > 0 else "none"])
	_finish(_fails)


func _finish(fails: Array) -> void:
	for n in _notes:
		print("  ", n)
	print("RULESET %s %s%s" % [ruleset, "OK" if fails.is_empty() else "FAIL", "" if fails.is_empty() else ": " + "; ".join(fails)])
	set_process(false)
	Q3Server.exec_now("killserver")
	get_tree().quit(0 if fails.is_empty() else 1)
