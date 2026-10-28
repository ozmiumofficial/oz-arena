extends Node
## Ready-up in a duel against a bot (g_main.c CheckTournament, patch 0003). F3 ("ready") starts the
## countdown, and its map_restart clears everyone's ready; the duel used to see nobody ready right
## after and go back to "press F3 when you're ready" for good. A headless duel on q3tourney2, one bot,
## a 2 s warmup, ready-up on:
##   godot --headless --path . res://tests/readyup_duel_test.tscn
## Checks: the warmup waits (CS_WARMUP -1) until "ready", then counts down, restarts and clears, and
## stays clear for 4 s of match. Prints "READYUP DUEL ok|FAIL: ...".

const E := preload("res://scripts/game/q3_enums.gd")
const WARMUP := 2
const LIMIT := 40.0

var _game: Q3Gameplay
var _t := 0.0
var _stage := 0               ## 0 waiting, 1 ready sent, 2 match running
var _stage_t := 0.0
var _counted := false
var _fails: Array = []


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
	var warmup := Q3Server.configstring(E.CS_WARMUP)
	var since := _t - _stage_t
	match _stage:
		0:   # both players in, nobody ready: the duel waits
			if since > 3.0:
				if Q3Server.cvar("g_readyUp") != "1":
					_fails.append("g_readyUp is %s, want 1 (Gameplay turns it on with a warmup)" % Q3Server.cvar("g_readyUp"))
				if warmup != "-1":
					_fails.append("before ready: CS_WARMUP '%s', want -1 (waiting)" % warmup)
				Q3Server.client_command(_game.me, "ready")   # F3
				_next(1)
		1:   # counting down, then the restart clears it
			if warmup.to_int() > 0:
				_counted = true
			if _counted and warmup == "" and Q3Server.cvar("g_restarted") == "0" and since > 0.5:
				_next(2)
			elif since > 15.0:
				_fails.append("after ready: the warmup never ended (CS_WARMUP '%s', counted %s)" % [warmup, _counted])
				_finish(_fails)
		2:   # the match must stay a match
			if warmup != "":
				_fails.append("%.1f s into the match CS_WARMUP went back to '%s'" % [since, warmup])
				_finish(_fails)
			elif since > 4.0:
				_finish(_fails)
	if _t > LIMIT:
		_fails.append("ran out of time in stage %d" % _stage)
		_finish(_fails)


func _next(stage: int) -> void:
	_stage = stage
	_stage_t = _t


func _finish(fails: Array) -> void:
	print("READYUP DUEL %s%s" % ["ok" if fails.is_empty() else "FAIL", "" if fails.is_empty() else ": " + "; ".join(fails)])
	set_process(false)
	if Q3Server.running():
		Q3Server.exec_now("killserver")
	get_tree().quit(0 if fails.is_empty() else 1)
