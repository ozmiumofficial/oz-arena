extends Node
## Plays a networked match in the Gameplay scene (MULTIPLAYER_PLAN.md M3): id's server runs in this
## process with bots, and Gameplay joins it as a network client over a Q3NetPipe with fake latency
## and loss, so everything it shows has come through packets. The local player runs and fires.
##   godot --headless --path . res://tests/net_play_test.tscn [-- seconds=20 map=q3dm17 latency=40 loss=0.03]
## Checks: the join completes and builds the view; other players and items are drawn from snapshots;
## the scoreboard knows everyone (scores command + configstrings); the player ran (prediction), and once
## still the prediction sits where the server has the player; the measured ping fits the link; leaving
## frees the server's slot. Prints "NET PLAY ok|FAILED".

const PEER := 7
const BOTS := ["sarge", "xaero", "crash", "klesk"]

var seconds := 20.0
var latency := 40
var loss := 0.03
var _game: Q3Gameplay
var _pipe: Q3NetPipe
var _t := 0.0
var _acc := 0.0
var _start := Vector3.ZERO
var _phase := 0
var _moved := 0.0
var _still_t := 0.0
var _max_rows := 0
var _snaps_seen := 0


func _ready() -> void:
	var map := "q3dm17"
	for a in OS.get_cmdline_user_args():
		if a.begins_with("seconds="):
			seconds = float(a.substr(8))
		if a.begins_with("map="):
			map = a.substr(4)
		if a.begins_with("latency="):
			latency = int(a.substr(8))
		if a.begins_with("loss="):
			loss = float(a.substr(5))
	var err := Q3Host.start()
	if err != "":
		_fail("start: " + err)
		return
	for cv in {"g_gametype": "0", "fraglimit": "0", "timelimit": "0", "g_doWarmup": "0"}:
		Q3Server.set_cvar(cv, {"g_gametype": "0", "fraglimit": "0", "timelimit": "0", "g_doWarmup": "0"}[cv])
	if Q3Server.exec_now("map " + map) != 0 or not Q3Server.running():
		_fail("map: " + Q3Server.error())
		return
	for i in 3:
		Q3Server.exec("addbot %s 3" % BOTS[i % BOTS.size()])
	Q3Server.frame(Q3Host.FRAME_MSEC)
	_pipe = Q3NetPipe.new(PEER, latency, loss)
	Session.join = {"transport": _pipe, "player": {"name": "Remote", "model": "sarge", "skin": "default"}, "snaps": 40, "maxpackets": 60}
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	_game.test_input = true
	add_child(_game)
	Input.action_press("q3_forward")
	Input.action_press("q3_attack")


func _process(delta: float) -> void:
	if _game == null:
		return
	# the server side: its frames, and the packets between it and the pipe
	_acc += delta * 1000.0
	var ms := int(_acc)
	_acc -= ms
	if ms > 0:
		if Q3Server.frame(ms) != 0:
			_fail("server: " + Q3Server.error())
			return
		Q3NetPipe.service(ms)
	_t += delta
	var feed := _game.net_feed
	if feed.state() == 5 and not _game._joining:
		_game._view.y += delta * 40.0
		var ps := feed.player_state(_game.me)
		if _phase == 0 and not ps.is_empty():
			_start = ps["origin"]
			_phase = 1
		if _phase == 1:
			_moved = maxf(_moved, Vector3(ps["origin"]).distance_to(_start))
		if _game.client:
			_snaps_seen = maxi(_snaps_seen, _game.client.player_nodes().size())
		_max_rows = maxi(_max_rows, _game.standings().size())
	if _phase == 0 or (_phase == 1 and _t < seconds - 4.0):
		if _t >= seconds + 20.0:
			_fail("never got going (state %d, joining %s, phase %d, error '%s')" % [feed.state(), str(_game._joining), _phase, _game._error])
		return
	if _phase == 1:
		# stand still so the prediction can settle to the server's state
		Input.action_release("q3_forward")
		Input.action_release("q3_attack")
		_phase = 2
		_still_t = _t
		return
	if _t >= _still_t + 4.0:
		_finish()


func _finish() -> void:
	set_process(false)
	var feed := _game.net_feed
	var client: Q3Client = _game.client
	var players := 0
	var items := 0
	for c in client.get_children():
		if c is Q3Character:
			players += 1
		elif c is Q3Pickup:
			items += 1
	var pred := Vector3(feed.player_state(_game.me)["origin"])
	var live_ps := Q3Server.player_state(_game.me)
	var live := Vector3(live_ps["origin"]) if not live_ps.is_empty() else pred
	var drift := pred.distance_to(live)
	var ping := feed.ping()
	var stats := Q3Net.stats()
	var me: int = _game.me
	var gone_ok := false
	var rows: Array = _game.standings()
	var names := PackedStringArray()
	for r in rows:
		names.append("%s %d" % [r["plain"], r["score"]])
	_game._stop_server()
	for i in 20:
		Q3NetPipe.service(8)
		Q3Server.frame(8)
	gone_ok = Q3Server.client_state(me) < 2
	var ok: bool = _game._error == "" and _game.map_scene != null and me >= 0 and players >= 1 and items > 0 \
		and _moved > 150.0 and drift < 1.0 and ping > 0 and ping < latency * 2 + 120 and _max_rows >= 4 and gone_ok
	print("joined as client %d; state %d, error '%s'; %d snapshots decoded" % [me, feed.state(), _game._error, stats["snapshots"]])
	print("drawn: %d players, %d items; standings: %s" % [players, items, ", ".join(names)])
	print("ran %.0f units; at rest the predicted origin is %.2f units from the server's" % [_moved, drift])
	print("ping %d ms on a %d ms link with %.0f%% loss (pipe: %d sent, %d dropped, %d received); client dropped %d snapshots" % [
		ping, latency * 2, loss * 100.0, _pipe.sent, _pipe.dropped, _pipe.received, stats["dropped"]])
	print("slot freed on leaving: ", gone_ok)
	print("NET PLAY ", "ok" if ok else "FAILED")
	Q3Server.exec_now("killserver")
	get_tree().quit(0 if ok else 1)


func _fail(why: String) -> void:
	print(why)
	print("NET PLAY FAILED")
	get_tree().quit(1)
