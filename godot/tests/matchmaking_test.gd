extends Node
## Matchmaking from the game's side (MULTIPLAYER_PLAN.md M6): this game (the Online autoload) queues for a casual duel
## through Q3MenuBridge, a test identity queues too and is elected host (lower round trip), the hub sends both
## match_found, the host lists the private match, and the game joins it by itself over WebRTC and plays. Needs a
## local hub (`cd online && npm run dev`, NET_DIRECT=1 in online/.dev.vars).
##   godot --headless --path . res://tests/matchmaking_test.tscn [-- server=ws://127.0.0.1:8787]
## Prints "MATCHMAKING ok|FAILED".

var server := "ws://127.0.0.1:8787"
var bridge: Q3MenuBridge
var posts: Array = []
var host_client: Q3OnlineClient
var host_net: Q3HostNet
var found_host: Dictionary = {}
var phase := "online"
var t := 0.0
var phase_t := 0.0
var checks: Array = []
var _was_server := ""
var _frames := 0
var _acc := 0.0
var _host_started := false


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("server="):
			server = a.substr(7)
	_was_server = String(Settings.get_value("network/online_server"))
	Settings.set_for_run("network/online_server", server)
	Session.dry_run = true
	Session.matchmade = {}
	Session.join = {}
	Online.connect_now()
	var err := Q3Host.start()
	if err != "":
		_finish("start: " + err)
		return
	for cv in {"g_gametype": "1", "fraglimit": "0", "timelimit": "0", "g_doWarmup": "0"}:
		Q3Server.set_cvar(cv, {"g_gametype": "1", "fraglimit": "0", "timelimit": "0", "g_doWarmup": "0"}[cv])
	if Q3Server.exec_now("map q3tourney2") != 0:
		_finish("map: " + Q3Server.error())
		return
	Q3Server.frame(Q3Host.FRAME_MSEC)
	bridge = Q3MenuBridge.new()
	bridge.post.connect(func(m): posts.append(m))
	host_client = Q3OnlineClient.new("user://test_net/mm_host_%d.key" % Time.get_ticks_usec())
	host_client.queue.connect(_on_host_queue)
	host_client.open(server, {"name": "MmHost", "model": "sarge", "color": "#2266ff"})


func _check(ok: bool, what: String) -> void:
	checks.append([ok, what])


func _on_host_queue(m: Dictionary) -> void:
	if String(m.get("type", "")) == "match_found" and found_host.is_empty():
		found_host = m


func _last(type: String, test := Callable()) -> Dictionary:
	for i in range(posts.size() - 1, -1, -1):
		if posts[i].get("type") == type and (test.is_null() or test.call(posts[i])):
			return posts[i]
	return {}


func _to(p: String) -> void:
	print("[mm test] %.1f s: %s" % [t, p])
	phase = p
	phase_t = 0.0


func _process(delta: float) -> void:
	t += delta
	phase_t += delta
	host_client.poll(delta)
	if t > 90.0:
		_finish("stuck in '%s' (online %s, host %s)" % [phase, Online.client.status, host_client.status])
		return
	if _host_started:
		_acc += delta * 1000.0
		var ms := int(_acc)
		_acc -= ms
		if ms > 0:
			host_net.pump_in(delta)
			Q3Server.frame(ms)
			host_net.pump_out()
	match phase:
		"online":
			if Online.client.status == "online" and host_client.status == "online" and host_client.state.has("me"):
				# the menu's Play button for a casual duel; the test host queues with the better connection
				bridge.handle({"type": "queue_join", "modes": ["duel"], "ranked": false})
				host_client.send("queue_join", {"modes": ["duel"], "ranked": false, "region": "auto", "rtt": 1})
				_to("queued")
		"queued":
			var q := _last("queue")
			if phase_t > 0.5 and not found_host.is_empty():
				_check(String(Online.queue_state.get("status", "")) == "found" or not Session.matchmade.is_empty(), "the menu's queue gets a match")
				_check(bool(found_host["you_host"]) and String(found_host["mode"]) == "duel", "the test host is elected (lower round trip) for a duel")
				_check(Session.matchmade.get("match", "") == found_host["match"] and not bool(Session.matchmade.get("you_host", true)), "this game is told it is not the host, same match")
				host_net = Q3HostNet.new()
				add_child(host_net)
				host_net.start(host_client, {"name": "Quick Duel", "mode": "duel", "map": "q3tourney2", "max": 2, "match": found_host["match"], "version": Online.game_version()})
				_host_started = true
				_to("joining")
		"joining":
			if not Session.join.is_empty():
				_check(Session.join["transport"] is Q3RtcTransport, "this game joins the private match by itself over WebRTC")
				_check(String(Session.join.get("teampref", "")) in ["auto", "red", "blue"], "the join carries its team preference (%s)" % Session.join.get("teampref", ""))
				Online.match_joined(String(Session.matchmade.get("match", "")))   # what Gameplay does once it is in
				host_client.send("match_joined", {"match": found_host["match"]})
				Q3Net.connect_to(Q3Host.userinfo({"name": "MmJoiner", "model": "sarge"}).replace("\\rate\\%d" % Settings.rate(), "\\rate\\90000\\snaps\\40"), 125)
				_to("play")
			elif phase_t > 40.0:
				_finish("never joined (%s)" % str(_last("net_join_state")))
		"play":
			var tr: Q3Transport = Session.join["transport"]
			for p in tr.poll():
				Q3Net.receive(p)
			var st := Q3Net.frame(maxi(int(delta * 1000.0), 1), false)
			for p in Q3Net.outgoing(64):
				tr.send(p)
			if st == 5:
				_frames += 1
				Q3Net.usercmd(Q3Net.time(), Vector3.ZERO, 0, 0, 0, 0, 0)
			if _frames > 150:
				_check(not Q3Net.snapshots(Q3Net.time()).is_empty(), "the matchmade match plays: snapshots arrive")
				Q3Net.disconnect_from()
				tr.close()
				_finish("")


func _finish(why: String) -> void:
	set_process(false)
	var ok := why == "" and checks.all(func(c): return c[0])
	if why != "":
		print(why)
	for c in checks:
		print("%s %s" % ["PASS" if c[0] else "FAIL", c[1]])
	print("MATCHMAKING ", "ok" if ok else "FAILED")
	if host_net:
		host_net.stop()
		host_net.queue_free()
	Online.queue_leave()
	host_client.close(false)
	DirAccess.remove_absolute(host_client.key_path)
	Q3Server.exec_now("killserver")
	Session.dry_run = false
	Session.join = {}
	Session.matchmade = {}
	Settings.set_value("network/online_server", _was_server)
	Settings.save()
	await get_tree().create_timer(1.0).timeout
	get_tree().quit(0 if ok else 1)
