extends Node
## The menu side of online play (MULTIPLAYER_PLAN.md M5), driven through Q3MenuBridge the way the web page
## does: a host (a test identity with a Q3HostNet) lists a password-protected match; the menu browses the
## hub's directory, joins with a wrong and then the right password, plays, gets kicked (the hub bans it from
## that host for 30 minutes) and finds the rejoin refused. Needs a local hub (`cd online && npm run dev`,
## NET_DIRECT=1 in online/.dev.vars). It points the Online server setting at the hub and puts it back.
##   godot --headless --path . res://tests/net_menu_test.tscn [-- server=ws://127.0.0.1:8787]
## Prints "NET MENU ok|FAILED".

var server := "ws://127.0.0.1:8787"
var bridge: Q3MenuBridge
var posts: Array = []
var host_client: Q3OnlineClient
var host_net: Q3HostNet
var phase := "online"
var t := 0.0
var phase_t := 0.0
var host_id := ""
var checks: Array = []
var _poll_t := 0.0
var _was_server := ""
var _states: Array = []
var _frames := 0
var _play_start := -1
var _voted := false
var _kicked_line := ""
var _acc := 0.0


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("server="):
			server = a.substr(7)
	_was_server = String(Settings.get_value("network/online_server"))
	Settings.set_for_run("network/online_server", server)
	Session.dry_run = true   # joining must not change the scene
	Online.connect_now()
	var err := Q3Host.start()
	if err != "":
		_finish("start: " + err)
		return
	for cv in {"g_gametype": "0", "fraglimit": "0", "timelimit": "0", "g_doWarmup": "0"}:
		Q3Server.set_cvar(cv, {"g_gametype": "0", "fraglimit": "0", "timelimit": "0", "g_doWarmup": "0"}[cv])
	if Q3Server.exec_now("map q3dm17") != 0:
		_finish("map: " + Q3Server.error())
		return
	Q3Server.exec("addbot sarge 3")
	Q3Server.frame(Q3Host.FRAME_MSEC)
	bridge = Q3MenuBridge.new()
	bridge.post.connect(func(m): posts.append(m))
	host_client = Q3OnlineClient.new("user://test_net/menu_host_%d.key" % Time.get_ticks_usec())   # a new host each run: the hub remembers bans
	host_client.open(server, {"name": "MenuHost", "model": "sarge", "color": "#2266ff"})


func _check(ok: bool, what: String) -> void:
	checks.append([ok, what])


func _last(type: String, test := Callable()) -> Dictionary:
	for i in range(posts.size() - 1, -1, -1):
		if posts[i].get("type") == type and (test.is_null() or test.call(posts[i])):
			return posts[i]
	return {}


func _to(p: String) -> void:
	print("[menu test] %.1f s: %s" % [t, p])
	phase = p
	phase_t = 0.0
	posts.clear()


func _process(delta: float) -> void:
	t += delta
	phase_t += delta
	host_client.poll(delta)
	if t > 90.0:
		_finish("stuck in '%s' (online %s, host %s, states %s)" % [phase, Online.client.status, host_client.status, str(_states)])
		return
	if host_net != null:
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
				host_id = String(host_client.state["me"]["id"])
				host_net = Q3HostNet.new()
				add_child(host_net)
				host_net.start(host_client, {"name": "Menu test match", "mode": "ffa", "map": "q3dm17", "max": 8, "password": "pw", "version": Online.game_version()})
				_to("browse")
		"browse":
			_poll_t -= delta
			if _poll_t <= 0.0:
				_poll_t = 1.0
				bridge.handle({"type": "net_browse"})
			var m := _last("net_matches")
			for x in m.get("matches", []):
				if x["host"] == host_id:
					_check(x["name"] == "Menu test match" and x["password"] == true and x["map"] == "q3dm17" and x["max"] == 8, "the menu browses the listing (password flag, map, size)")
					_to("wrong_password")
					bridge.handle({"type": "net_join", "host": host_id, "password": "nope"})
					break
		"wrong_password":
			var f := _last("net_join_state", func(p): return p["status"] == "failed")
			if not f.is_empty():
				_check(String(f["reason"]) == "Wrong password.", "a wrong password is refused (%s)" % f["reason"])
				_to("join")
				bridge.handle({"type": "net_join", "host": host_id, "password": "pw"})
			elif phase_t > 15.0:
				_finish("no answer to the wrong password")
		"join":
			if not Session.join.is_empty():
				_check(Session.join["transport"] is Q3RtcTransport, "joining with the password hands Session.join a WebRTC transport")
				Q3Net.connect_to(Q3Host.userinfo({"name": "NetJoiner", "model": "sarge"}).replace("\\rate\\%d" % Settings.rate(), "\\rate\\90000\\snaps\\40"), 125)
				_to("play")
			elif not _last("net_join_state", func(p): return p["status"] == "failed").is_empty():
				_finish("the join failed: " + String(_last("net_join_state")["reason"]))
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
				if _play_start < 0:
					_play_start = Q3Net.time()
				var since := Q3Net.time() - _play_start
				if since > 1000 and not _voted:
					_voted = true
					Q3Net.command("callvote kick Sarge")   # Quake 3's own vote: the one human voting passes it at once, the kick follows in 3 s
				for c in Q3Net.next_commands():
					if c.contains("was kicked"):
						_kicked_line = c
			if st == 0:
				_finish("dropped before the kick: " + Q3Net.error())
			elif _play_start >= 0 and Q3Net.time() - _play_start > 7000:
				_check(not Q3Net.snapshots(Q3Net.time()).is_empty(), "the menu's join plays: snapshots arrive")
				_check(_kicked_line != "", "a vote kick called over the network passes and the server says so (%s)" % _kicked_line.strip_edges())
				_to("kick")
		"kick":
			var tr: Q3Transport = Session.join["transport"]
			if phase_t < 0.1:
				host_net.note_kick("NetJoiner")   # what Gameplay does on "NetJoiner was kicked"
			for p in tr.poll():
				Q3Net.receive(p)
			Q3Net.frame(maxi(int(delta * 1000.0), 1), false)
			for p in Q3Net.outgoing(64):
				tr.send(p)
			if phase_t > 1.5:
				Q3Net.disconnect_from()
				tr.close()
				Session.join = {}
				_check(host_net.peers.is_empty(), "the host drops the kicked player")
				_to("rejoin")
				bridge.handle({"type": "net_join", "host": host_id, "password": "pw"})
		"rejoin":
			var f := _last("net_join_state", func(p): return p["status"] == "failed")
			if not f.is_empty():
				_check(String(f["reason"]).contains("kicked") and String(f["reason"]).contains("minutes"), "the kicked player cannot rejoin (%s)" % f["reason"])
				_finish("")
			elif phase_t > 15.0:
				_finish("no answer to the rejoin")


func _finish(why: String) -> void:
	set_process(false)
	var ok := why == "" and checks.all(func(c): return c[0])
	if why != "":
		print(why)
	for c in checks:
		print("%s %s" % ["PASS" if c[0] else "FAIL", c[1]])
	print("NET MENU ", "ok" if ok else "FAILED")
	if host_net:
		host_net.stop()
		host_net.queue_free()
	host_client.close(false)
	DirAccess.remove_absolute(host_client.key_path)
	Q3Server.exec_now("killserver")
	Session.dry_run = false
	Session.join = {}
	Settings.set_value("network/online_server", _was_server)
	Settings.save()
	await get_tree().create_timer(1.0).timeout
	get_tree().quit(0 if ok else 1)
