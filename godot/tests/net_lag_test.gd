extends Node
## Two processes over a bad link (MULTIPLAYER_PLAN.md M8): a dedicated server runs as a child process with bots, this
## game finds it in a local hub's Server Browser, joins it over WebRTC with Q3LagTransport adding latency, jitter
## and loss, and plays in the Gameplay scene (running and firing, then standing still). Needs a local hub (`cd online
## && npm run dev`, NET_DIRECT=1 in online/.dev.vars); the Online server setting is pointed at it for the run.
##   godot --headless --path . res://tests/net_lag_test.tscn [-- latency=60 jitter=20 loss=0.05 seconds=25 snaps=42]
## Checks: it joins and builds the view; bots are drawn from snapshots; the scoreboard has everyone; the player ran;
## mispredictions stay rare; other players are mostly drawn between two snapshots; the ping fits the link; at rest
## the prediction sits on the server's state; leaving frees the slot. On this link the client makes about 4-35
## small mispredictions a minute (worst about 20 units; they swing with the fighting) and draws others ahead of the
## newest snapshot on 1-5% of frames (MULTIPLAYER_PLAN.md M8); the limits are set to catch breakage, not that spread.
## Prints "NET LAG ok|FAILED" and a line of numbers (work/net_lag.json).

const DIR := "user://test_lag"

var server := "ws://127.0.0.1:8787"
var latency := 60
var jitter := 20
var loss := 0.05
var seconds := 25.0
var snaps := -1   # -1: the game's default (Q3NetFeed.SNAPS)
var interp := -1  # -1: the setting as it is (Settings > Network > Interpolation)
var _was_interp: Variant = null
var pid := -1
var client: Q3OnlineClient
var joiner: Q3NetJoin
var lag: Q3LagTransport
var game: Q3Gameplay
var matches: Array = []
var phase := "spawn"
var t := 0.0
var phase_t := 0.0
var checks: Array = []
var _was_server := ""
var _status: Dictionary = {}
var _poll_t := 0.0
var _start := Vector3.ZERO
var _moved := 0.0
var _frames := 0
var _extrap := 0
var _errors := 0
var _worst_error := 0.0
var _join_error := 0.0
const JOIN_FRAMES := 300   # about the first 2.5 s in the match
var _error_sizes: Array = []
var _last_error_time := 0
var _rows := 0
var _drawn := 0
var _pings: Array = []
var _clock_jumps: Array = []   # [clock step - frame step, frame ms] where they differ by over 50 ms
var _last_clock := 0
var _hitch := 0.0
var _cmd_gaps: Array = []   # the clock minus the newest snapshot's commandTime: how far the server trails our commands


func _ready() -> void:
	process_priority = 100   # after Gameplay: this frame's clock, snapshots and prediction
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=")
		if kv.size() != 2:
			continue
		match kv[0]:
			"server": server = kv[1]
			"latency": latency = int(kv[1])
			"jitter": jitter = int(kv[1])
			"loss": loss = float(kv[1])
			"seconds": seconds = float(kv[1])
			"snaps": snaps = int(kv[1])
			"interp": interp = int(kv[1])
	_was_server = String(Settings.get_value("network/online_server"))
	Settings.set_value("network/online_server", server)
	if interp >= 0:
		_was_interp = Settings.get_value("network/interp")
		Settings.set_value("network/interp", interp)
	Settings.save()
	DirAccess.make_dir_recursive_absolute(DIR)
	for f in ["status.json", "stop", "commands.txt"]:
		DirAccess.remove_absolute(DIR.path_join(f))
	var name := "Lag test %d" % OS.get_process_id()
	var cfg := {"name": name, "mode": "ffa", "rotation": ["q3dm17"], "max_players": 8, "bots": 4,   # players in all: 3 bots beside this one
		"rules": {"fraglimit": 0, "timelimit": 0, "warmup": 0}, "idle_sleep": true, "online": true}
	var cf := FileAccess.open(DIR.path_join("config.json"), FileAccess.WRITE)
	cf.store_string(JSON.stringify(cfg))
	cf.close()
	if OS.get_cmdline_user_args().has("debug=1"):   # the server's developer prints (why it ignores a client message)
		FileAccess.open(DIR.path_join("commands.txt"), FileAccess.WRITE).store_string("developer 1\n")
	var args := PackedStringArray(["--headless", "--path", ProjectSettings.globalize_path("res://"), "--log-file",
		ProjectSettings.globalize_path("res://").path_join("../work/net_lag_child.log").simplify_path(), "--",
		"dedicated=" + ProjectSettings.globalize_path(DIR.path_join("config.json"))])
	pid = OS.create_process(OS.get_executable_path(), args)
	client = Q3OnlineClient.new("user://test_net/lag_joiner.key")
	client.net.connect(func(m): if m.get("type") == "net_matches": matches = m["matches"])
	client.open(server, {"name": "LagJoiner", "model": "sarge", "color": "#22cc66"})


func _check(ok: bool, what: String) -> void:
	checks.append([ok, what])


func _to(p: String) -> void:
	print("[lag test] %.1f s: %s" % [t, p])
	phase = p
	phase_t = 0.0


func _process(delta: float) -> void:
	t += delta
	phase_t += delta
	client.poll(delta)
	_poll_t -= delta
	if _poll_t <= 0.0:
		_poll_t = 0.5
		var text := FileAccess.get_file_as_string(DIR.path_join("status.json"))
		var d: Variant = JSON.parse_string(text) if text != "" else null
		_status = d if d is Dictionary else {}
	if t > seconds + 120.0:
		_finish("stuck in '%s' (status %s)" % [phase, str(_status)])
		return
	match phase:
		"spawn":
			if bool(_status.get("listed", false)) and client.status == "online":
				_to("browse")
		"browse":
			if fmod(phase_t, 1.0) < delta:
				client.send("net_browse")
			for m in matches:
				if String(m.get("name", "")) == String(_status.get("name", "?")):
					joiner = Q3NetJoin.new()
					add_child(joiner)
					joiner.finished.connect(_on_joined)
					joiner.begin(client, String(m["host"]), "", Online.game_version())
					_to("join")
					break
		"join":
			if phase_t > 45.0:
				_finish("never joined: " + joiner.reason)
		"run", "rest":
			_watch(delta)


func _on_joined(ok: bool, tr: Q3Transport, why: String) -> void:
	if not ok:
		_finish("the join failed: " + why)
		return
	lag = Q3LagTransport.new(tr, latency, jitter, loss, 1234)
	Session.join = {"transport": lag, "player": {"name": "LagJoiner", "model": "sarge", "skin": "default"}}
	if snaps > 0:
		Session.join["snaps"] = snaps
	game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	game.test_input = true
	add_child(game)
	Input.action_press("q3_forward")
	Input.action_press("q3_attack")
	_to("run")


func _watch(delta: float) -> void:
	var feed := game.net_feed
	if feed == null or feed.state() != 5 or game._joining:
		if phase_t > 40.0:
			_finish("never got going (state %d, error '%s')" % [feed.state() if feed else -1, game._error])
		return
	game._view.y += delta * 40.0
	var ps := feed.player_state(game.me)
	if ps.is_empty():
		return
	_frames += 1
	if _frames <= 400 and OS.get_cmdline_user_args().has("trace=1"):
		var srv0 := feed.server_state()
		print("trace %d: dt %d clock %d newest %s cmdTime %s pred %s err %.1f@%d pm %s" % [_frames, int(delta * 1000.0), Q3Net.time(),
			str(Q3Net.snapshots(1 << 30).get("snap", {}).get("serverTime", -1)), str(srv0.get("commandTime", -1)),
			str(Vector3(ps["origin"]).round()), feed._error_vec.length(), feed._error_time, str(srv0.get("pm_type", -1))])
	if _start == Vector3.ZERO:
		_start = ps["origin"]
	_moved = maxf(_moved, Vector3(ps["origin"]).distance_to(_start))
	if feed._extrapolating:
		_extrap += 1
	if feed._error_time != _last_error_time:
		_last_error_time = feed._error_time
		if feed._error_vec.length() > 1.0 and _frames < JOIN_FRAMES:
			_join_error = maxf(_join_error, feed._error_vec.length())
		elif feed._error_vec.length() > 1.0:
			_errors += 1
			_worst_error = maxf(_worst_error, feed._error_vec.length())
			# with what the server said then: a big error at a death (pm_type 3) or a respawn is the server's doing
			_error_sizes.append([snappedf(feed._error_vec.length(), 0.1), int(feed.server_state().get("pm_type", -1)), snappedf(phase_t, 0.1)])
	_rows = maxi(_rows, game.standings().size())
	if game.client:
		_drawn = maxi(_drawn, game.client.player_nodes().size())
	if feed.ping() > 0:
		_pings.append(feed.ping())
	var clk := Q3Net.time()
	if _last_clock != 0 and absi(clk - _last_clock - int(delta * 1000.0)) > 50:
		_clock_jumps.append([clk - _last_clock - int(delta * 1000.0), int(delta * 1000.0), snappedf(phase_t, 0.1)])
	_last_clock = clk
	_hitch = maxf(_hitch, delta)
	var srv := feed.server_state()
	if srv.has("commandTime"):
		_cmd_gaps.append(Q3Net.time() - int(srv["commandTime"]))
	if phase == "run" and phase_t >= seconds:
		# stand still so the prediction can settle on the server's state
		Input.action_release("q3_forward")
		Input.action_release("q3_attack")
		_to("rest")
	elif phase == "rest" and phase_t >= 4.0:
		_leave()


func _leave() -> void:
	var feed := game.net_feed
	var pred := Vector3(feed.player_state(game.me)["origin"])
	var srv: Dictionary = feed.server_state()
	var drift := pred.distance_to(Vector3(srv.get("origin", pred)))
	var stats := Q3Net.stats()
	_pings.sort()
	var ping: int = _pings[_pings.size() / 2] if _pings.size() else 0
	var run_s := seconds + 4.0
	var mispredict_per_min := _errors / run_s * 60.0
	var extrap_share := float(_extrap) / maxf(_frames, 1)
	_check(game._error == "" and game.map_scene != null and game.me >= 0, "it joins the dedicated server over WebRTC and builds the view")
	_check(_drawn >= 3 and _rows >= 4, "bots are drawn from snapshots (%d), the scoreboard has everyone (%d rows)" % [_drawn, _rows])
	_check(_moved > 150.0, "the player ran (%.0f units)" % _moved)
	_check(mispredict_per_min < 60.0, "mispredictions over 1 unit stay rare: %.1f a minute (worst %.1f units; %.0f units right after joining, not counted)" % [mispredict_per_min, _worst_error, _join_error])
	_check(extrap_share < 0.15, "other players are drawn between snapshots on %.1f%% of frames (%.1f%% guessed ahead)" % [100.0 - extrap_share * 100.0, extrap_share * 100.0])
	_check(ping >= latency * 2 and ping < latency * 2 + jitter + 120, "the ping fits the link: %d ms on %d ms + up to %d ms jitter" % [ping, latency * 2, jitter * 2])
	_check(drift < 1.0, "at rest the prediction sits on the server's state (%.2f units off)" % drift)
	var row := {"latency": latency, "jitter": jitter, "loss": loss, "snaps": int(Session.join.get("snaps", Q3NetFeed.SNAPS)), "interp": game.net_feed.interp,
		"ping": ping, "frames": _frames, "extrapolated": extrap_share, "mispredictions_per_min": mispredict_per_min,
		"worst_error": _worst_error, "join_error": _join_error, "error_sizes": _error_sizes, "cmd_gap": _gap_stats(), "clock_jumps": _clock_jumps, "worst_frame_ms": int(_hitch * 1000.0), "snapshots": stats["snapshots"], "dropped": stats["dropped"], "lag_dropped": lag.dropped}
	print("numbers: ", JSON.stringify(row))
	var f := FileAccess.open(ProjectSettings.globalize_path("res://").path_join("../work/net_lag.json").simplify_path(), FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(row, "  "))
	game._stop_server()
	_to("left")
	await get_tree().create_timer(4.0).timeout
	_check(int(_status.get("humans", 1)) == 0, "leaving frees the slot (the server counts %d players)" % int(_status.get("humans", -1)))
	_finish("")


func _gap_stats() -> Dictionary:
	if _cmd_gaps.is_empty():
		return {}
	var g := _cmd_gaps.duplicate()
	g.sort()
	return {"median": g[g.size() / 2], "p95": g[int(g.size() * 0.95)], "max": g.back(), "min": g[0]}


func _finish(why: String) -> void:
	set_process(false)
	var ok := why == "" and checks.all(func(c): return c[0])
	if why != "":
		print(why)
	for c in checks:
		print("%s %s" % ["PASS" if c[0] else "FAIL", c[1]])
	print("NET LAG ", "ok" if ok else "FAILED")
	if game != null and is_instance_valid(game):
		game._stop_server()   # closes the data channel: a live one keeps libdatachannel's threads, and the process, alive
	elif lag != null:
		lag.close()
	if pid > 0 and OS.is_process_running(pid):
		FileAccess.open(DIR.path_join("stop"), FileAccess.WRITE)
		await get_tree().create_timer(6.0).timeout
		if OS.is_process_running(pid):
			OS.kill(pid)
	client.close(false)
	if joiner:
		joiner.queue_free()
	Settings.set_value("network/online_server", _was_server)
	if _was_interp != null:
		Settings.set_value("network/interp", _was_interp)
	Settings.save()
	await get_tree().create_timer(1.0).timeout
	get_tree().quit(0 if ok else 1)
