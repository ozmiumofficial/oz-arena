extends Node
## The warmup's end and the pause menu's Restart, both "map_restart 0" (g_main.c CheckTournament,
## gameplay.gd). ioquake3's SV_MapRestart_f does nothing while com_frameTime == sv.serverId (one
## restart per frame), so q3core's host has to advance com_frameTime every frame as Com_Frame does;
## if it doesn't, the game waits at level.restarted forever and the match freezes after the warmup.
## A headless duel on q3tourney2, one bot, a 2 s warmup, no ready-up:
##   godot --headless --path . res://tests/warmup_restart_test.tscn
## Checks: CS_WARMUP counts down and clears, g_restarted goes back to 0, CS_LEVEL_START_TIME moves,
## the client gets "map_restart" (the HUD's FIGHT!), the level clock keeps going (the player's
## commandTime keeps up with the server); then Restart sets a suicide's -1 back to 0 and the
## warmup runs and ends again. Prints "WARMUP RESTART ok|FAIL: ...".

const E := preload("res://scripts/game/q3_enums.gd")
const WARMUP := 2
const LIMIT := 40.0           ## seconds of real time before giving up

var _game: Q3Gameplay
var _t := 0.0
var _fails: Array = []
var _notes: Array = []
var _stage := 0               ## 0 warmup, 1 match, 2 suicide done, 3 restarted, 4 second warmup over
var _stage_t := 0.0
var _counted := false         ## CS_WARMUP held a countdown time
var _start_time := ""         ## CS_LEVEL_START_TIME when the stage began
var _fights := 0              ## FIGHT! center prints seen
var _fight_shown := false
var _worst_gap := 0           ## the most the player's commandTime fell behind the server, in a match stage


func _ready() -> void:
	Engine.max_fps = 60
	if Q3Host.start() != "":
		_finish(["engine: " + Q3Server.error()])
		return
	var s := Session.new_setup("duel", "q3tourney2")
	s.rules = {"warmup": WARMUP, "timelimit": 0, "fraglimit": 0}
	s.bots = [{"name": "Sarge", "skill": 1, "team": ""}]
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	_game.test_input = true
	add_child(_game)


func _process(delta: float) -> void:
	_t += delta
	if _game == null or _game.me < 0:
		if _t > 10.0:
			_finish(["the match didn't start: " + String(_game._error if _game else "")])
		return
	if _t < 0.1:
		Q3Server.set_cvar("g_readyUp", "0")   # Gameplay turns it on with a warmup; no one presses F3 here
		_start_time = Q3Server.configstring(E.CS_LEVEL_START_TIME)
	var warmup := Q3Server.configstring(E.CS_WARMUP)
	if warmup.to_int() > 0:
		_counted = true
	var fight: bool = _game.hud != null and _game.hud._center == "FIGHT!" and _game.hud._center_t < 1.0
	if fight and not _fight_shown:
		_fights += 1
	_fight_shown = fight
	var ps := Q3Server.player_state(_game.me)
	var since := _t - _stage_t
	match _stage:
		0, 3:   # a warmup: wait for its map_restart
			if warmup == "" and Q3Server.cvar("g_restarted") == "0" and _counted and since > 0.5:
				var st := Q3Server.configstring(E.CS_LEVEL_START_TIME)
				if st == _start_time:
					_fails.append("stage %d: CS_LEVEL_START_TIME didn't move (%s)" % [_stage, st])
				_notes.append("stage %d: warmup over after %.1f s, level start %s -> %s" % [_stage, since, _start_time, st])
				_next(1 if _stage == 0 else 4)
			elif since > 15.0:
				_fails.append("stage %d: the warmup never ended: CS_WARMUP '%s', g_restarted %s, counted %s" % [
					_stage, warmup, Q3Server.cvar("g_restarted"), _counted])
				_finish(_fails)
		1:   # the match runs: the clock goes on; then a suicide (-1)
			_gap(ps)
			if since > 3.0:
				Q3Server.client_command(_game.me, "kill")
				_next(2)
		2:
			if since > 1.0:
				var score := int(ps["persistant"][E.PERS_SCORE])
				if score != -1:
					_fails.append("score %d after /kill, want -1" % score)
				_start_time = Q3Server.configstring(E.CS_LEVEL_START_TIME)
				_counted = false
				Q3Server.exec("map_restart 0")   # the pause menu's Restart
				_next(3)
		4:   # after the second warmup: score back to 0, the clock still going
			_gap(ps)
			if since > 3.0:
				var score := int(ps["persistant"][E.PERS_SCORE])
				if score != 0:
					_fails.append("score %d after Restart, want 0" % score)
				if _worst_gap > 300:
					_fails.append("the level clock stopped: commandTime fell %d ms behind the server" % _worst_gap)
				if _fights != 2:
					_fails.append("FIGHT! shown %d times, want 2 (once per warmup)" % _fights)
				_notes.append("commandTime at most %d ms behind; FIGHT! %d times; server time %d" % [_worst_gap, _fights, Q3Server.time()])
				_finish(_fails)
	if _t > LIMIT:
		_fails.append("ran out of time in stage %d" % _stage)
		_finish(_fails)


func _next(stage: int) -> void:
	_stage = stage
	_stage_t = _t


## How far the local player's last command is behind the server: id's ClientThink holds a command
## to level.time + 200, so a stopped level clock shows up here.
func _gap(ps: Dictionary) -> void:
	if ps.is_empty() or _t - _stage_t < 0.5:
		return
	_worst_gap = maxi(_worst_gap, Q3Server.time() - int(ps["commandTime"]))


func _finish(fails: Array) -> void:
	for n in _notes:
		print("  ", n)
	print("WARMUP RESTART %s%s" % ["ok" if fails.is_empty() else "FAIL", "" if fails.is_empty() else ": " + "; ".join(fails)])
	set_process(false)
	if Q3Server.running():
		Q3Server.exec_now("killserver")
	get_tree().quit(0 if fails.is_empty() else 1)
