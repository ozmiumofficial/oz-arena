extends Node
## Gameplay as an online host (MULTIPLAYER_PLAN.md M4): a listen server lists its match with the hub through
## the Online autoload, a second identity browses, joins over WebRTC and plays; the host's own scoreboard
## shows the newcomer; leaving frees their slot. Needs a local hub (`cd online && npm run dev`, NET_DIRECT=1
## in online/.dev.vars). It points the Online server setting at the hub and puts it back.
##   godot --headless --path . res://tests/net_host_test.tscn [-- server=ws://127.0.0.1:8787 seconds=10]
## Prints "NET HOST ok|FAILED".

var server := "ws://127.0.0.1:8787"
var seconds := 10.0
var game: Q3Gameplay
var joiner_client: Q3OnlineClient
var joiner: Q3NetJoin
var transport: Q3RtcTransport
var matches: Array = []
var phase := "online"
var t := 0.0
var phase_t := 0.0
var listed := false
var joined := false
var join_reason := ""
var frames := 0
var snaps := 0
var host_sees_joiner := false
var slot_freed := false
var was_server := ""
var _poll_t := 0.0


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("server="):
			server = a.substr(7)
		if a.begins_with("seconds="):
			seconds = float(a.substr(8))
	was_server = String(Settings.get_value("network/online_server"))
	Settings.set_value("network/online_server", server)
	Online.connect_now()
	joiner_client = Q3OnlineClient.new("user://test_net/join2.key")
	joiner_client.net.connect(func(m): if m.get("type") == "net_matches": matches = m["matches"])
	joiner_client.open(server, {"name": "NetJoiner", "model": "sarge", "color": "#ff6622"})


func _process(delta: float) -> void:
	t += delta
	phase_t += delta
	joiner_client.poll(delta)
	if phase != "play" and t > 60.0:
		_finish("stuck in '%s' (online %s, joiner %s, %s)" % [phase, Online.client.status, joiner_client.status, join_reason])
		return
	match phase:
		"online":
			if Online.client.status == "online" and joiner_client.status == "online" and joiner_client.state.has("me"):
				var s := Session.new_setup("ffa", "q3dm17")
				s.bots = [{"name": "Sarge", "skill": 3, "team": ""}, {"name": "Xaero", "skill": 3, "team": ""}]
				s.server_type = "listen"
				s.online = {"name": "Hosted by test"}
				Session.setup = s
				game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
				add_child(game)
				_to("browse")
		"browse":
			_poll_t -= delta
			if _poll_t <= 0.0:
				_poll_t = 1.0
				joiner_client.send("net_browse")
			for m in matches:
				if m["name"] == "Hosted by test":
					listed = true
					joiner = Q3NetJoin.new()
					add_child(joiner)
					joiner.finished.connect(func(ok, tr, why):
						joined = ok
						join_reason = why
						transport = tr)
					joiner.begin(joiner_client, m["host"], "", "")
					_to("join")
					break
		"join":
			if joiner.status == "failed":
				_finish("join failed: " + join_reason)
			elif transport != null:
				Q3Net.connect_to(Q3Host.userinfo({"name": "NetJoiner", "model": "sarge"}).replace("\\rate\\%d" % Settings.rate(), "\\rate\\90000\\snaps\\40"), 125)
				_to("play")
		"play":
			var ms := maxi(int(delta * 1000.0), 1)
			for p in transport.poll():
				Q3Net.receive(p)
			var st := Q3Net.frame(ms, false)
			if st == 0:
				_finish("client dropped: " + Q3Net.error())
				return
			for p in Q3Net.outgoing(64):
				transport.send(p)
			if st == 5:
				frames += 1
				Q3Net.usercmd(Q3Net.time(), Vector3(0, fmod(frames * 0.7, 360.0), 0), 0, 0, 127, 0, 0)
				if not Q3Net.snapshots(Q3Net.time()).is_empty():
					snaps += 1
				Q3Net.next_commands()
				if not host_sees_joiner:
					for r in game.standings():
						if r["plain"] == "NetJoiner":
							host_sees_joiner = true
				if phase_t > seconds and host_sees_joiner:
					Q3Net.disconnect_from()
					for p in Q3Net.outgoing(64):
						transport.send(p)
					transport.close()
					transport = null
					_to("leave")
		"leave":
			if phase_t > 2.5:
				slot_freed = not game.standings().any(func(r): return r["plain"] == "NetJoiner")
				_finish("")


func _to(p: String) -> void:
	phase = p
	phase_t = 0.0


func _finish(why: String) -> void:
	set_process(false)
	var ok := why == "" and listed and joined and snaps > 100 and host_sees_joiner and slot_freed
	if why != "":
		print(why)
	print("listed %s; joined over WebRTC %s; %d snapshots drawn; the host's scoreboard shows the joiner %s; slot freed after leaving %s" % [
		str(listed), str(joined), snaps, str(host_sees_joiner), str(slot_freed)])
	print("NET HOST ", "ok" if ok else "FAILED")
	joiner_client.close(false)
	if game:
		game._stop_server()
		game.queue_free()
	if joiner:
		joiner.queue_free()
	Settings.set_value("network/online_server", was_server)
	Settings.save()
	await get_tree().create_timer(1.0).timeout
	get_tree().quit(0 if ok else 1)
