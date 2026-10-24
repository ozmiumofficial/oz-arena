extends Node
## net_loopback_test over real WebRTC (MULTIPLAYER_PLAN.md M4): the host lists a match with a local hub
## (`cd online && npm run dev`, with NET_DIRECT=1 in online/.dev.vars so two clients on one PC can connect
## without the relay), a second identity finds it in the directory, joins through the hub's signaling,
## and plays on id's server over a WebRTC data channel.
##   godot --headless --path . res://tests/rtc_loopback_test.tscn [-- server=ws://127.0.0.1:8787 seconds=12]
## Checks: both players reach the hub; the listing is browsable; the join, the offer and answer and the
## data channel work; the client's snapshots equal the server's; its prediction lands where the server
## puts the player; leaving frees the slot and the host's peer; the listing goes with the host.
## Prints "RTC LOOPBACK ok|FAILED".

const BOTS := ["sarge", "xaero", "crash"]

var server := "ws://127.0.0.1:8787"
var seconds := 12.0
var host_client: Q3OnlineClient
var join_client: Q3OnlineClient
var host_net: Q3HostNet
var joiner: Q3NetJoin
var transport: Q3RtcTransport
var phase := "hubs"
var t := 0.0
var phase_t := 0.0
var joined := false
var join_reason := ""
var listing_seen := false
var matches: Array = []
var frames := 0
var checked := 0
var equal := 0
var first_diff := ""
var predicted := 0
var matched := 0
var history := {}
var pending := {}
var slot := -1
var ice_seen := {}
var last_msg := -1
var gone_ok := false
var listing_gone := false
var out: PackedStringArray = []
var _acc := 0.0
var _checked_msg := -1
var _started := false
var _renew_slot := -2      # the slot when the handover started
var host_renewed := 0
var renew_checked := 0     # snapshots checked after the handover


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("server="):
			server = a.substr(7)
		if a.begins_with("seconds="):
			seconds = float(a.substr(8))
	var err := Q3Host.start()
	if err != "":
		_finish("start: " + err)
		return
	if not ClassDB.class_exists("WebRTCPeerConnection") or WebRTCPeerConnection.new().initialize({}) != OK:
		_finish("WebRTC isn't available (the webrtc_native addon)")
		return
	for cv in {"g_gametype": "0", "fraglimit": "0", "timelimit": "0", "g_doWarmup": "0"}:
		Q3Server.set_cvar(cv, {"g_gametype": "0", "fraglimit": "0", "timelimit": "0", "g_doWarmup": "0"}[cv])
	if Q3Server.exec_now("map q3dm17") != 0 or not Q3Server.running():
		_finish("map: " + Q3Server.error())
		return
	for i in 3:
		Q3Server.exec("addbot %s 3" % BOTS[i])
	Q3Server.frame(Q3Host.FRAME_MSEC)
	host_client = Q3OnlineClient.new("user://test_net/host.key")
	join_client = Q3OnlineClient.new("user://test_net/join.key")
	host_client.net.connect(func(m): if m.get("type") == "net_matches": matches = m["matches"])
	join_client.net.connect(func(m): if m.get("type") == "net_matches": matches = m["matches"])
	host_client.open(server, {"name": "RtcHost", "model": "sarge", "color": "#2266ff"})
	join_client.open(server, {"name": "RtcJoiner", "model": "sarge", "color": "#ff6622"})


func _process(delta: float) -> void:
	t += delta
	phase_t += delta
	host_client.poll(delta)
	join_client.poll(delta)
	if phase != "play" and t > 60.0:
		_finish("stuck in phase '%s' (host %s, joiner %s, join: %s)" % [phase, host_client.status, join_client.status, join_reason])
		return
	match phase:
		"hubs":
			if host_client.status == "online" and join_client.status == "online" and host_client.state.has("me") and join_client.state.has("me"):
				host_net = Q3HostNet.new()
				add_child(host_net)
				host_net.renewed.connect(func(_pid): host_renewed += 1)
				host_net.start(host_client, {"name": "RTC test", "mode": "ffa", "map": "q3dm17", "max": 16, "version": "t"})
				_to("browse")
		"browse":
			if phase_t > 0.5 and fmod(phase_t, 1.0) < delta:
				join_client.send("net_browse")
			for m in matches:
				if m["host"] == host_client.state["me"]["id"]:
					listing_seen = m["name"] == "RTC test" and m["map"] == "q3dm17" and int(m["max"]) == 16
					joiner = Q3NetJoin.new()
					add_child(joiner)
					joiner.finished.connect(func(ok, tr, why):
						joined = ok
						join_reason = why
						transport = tr)
					joiner.begin(join_client, host_client.state["me"]["id"], "", "t")
					_to("join")
					break
		"join":
			_server_step(delta)
			if joiner.status == "failed" or transport != null:
				if transport == null:
					_finish("the join failed: " + join_reason)
					return
				Q3Net.connect_to(Q3Host.userinfo({"name": "RtcJoiner", "model": "sarge"}).replace("\\rate\\%d" % Settings.rate(), "\\rate\\90000\\snaps\\40"), 125)
				_to("play")
		"play":
			_play(delta)
		"leave":
			_server_step(delta)
			host_net.pump_in(delta)
			if phase_t > 1.0 and not gone_ok:
				var s := Q3Server.net_peer_client(host_net.peers.values()[0]["id"]) if host_net.peers.size() else -1
				gone_ok = host_net.peers.is_empty() and (slot < 0 or Q3Server.client_state(slot) < 2)
			if gone_ok or phase_t > 5.0:
				host_net.stop()
				host_client.send("net_unhost")
				_to("unlisted")
		"unlisted":
			if fmod(phase_t, 0.5) < delta:
				join_client.send("net_browse")
			if phase_t > 1.5:
				listing_gone = not matches.any(func(m): return m["host"] == host_client.state["me"]["id"])
				_finish("")


func _to(p: String) -> void:
	print("[rtc test] %.1f s: phase %s" % [t, p])
	phase = p
	phase_t = 0.0


func _server_step(delta: float) -> void:
	_acc += delta * 1000.0
	var ms := int(_acc)
	_acc -= ms
	if ms <= 0:
		return
	host_net.pump_in(delta)
	if Q3Server.frame(ms) != 0:
		_finish("server: " + Q3Server.error())
		return
	host_net.pump_out()


func _play(delta: float) -> void:
	_server_step(delta)
	var ms := maxi(int(delta * 1000.0), 1)
	for p in transport.poll():
		Q3Net.receive(p)
	var st := Q3Net.frame(ms, false)
	if st == 0:
		_finish("the client dropped: " + Q3Net.error())
		return
	for p in Q3Net.outgoing(64):
		transport.send(p)
	slot = host_net.peers.values()[0]["id"] if host_net.peers.size() and host_net.peers.values()[0]["id"] >= 0 else -1
	if slot >= 0:
		slot = Q3Server.net_peer_client(slot)
	if slot >= 0:
		var ps := Q3Server.player_state(slot)
		if not ps.is_empty() and Q3Server.client_state(slot) == 4:
			history[int(ps["commandTime"])] = ps["origin"]
	if st == 5:
		if not _started:
			_started = true
			phase_t = 0.0
		frames += 1
		# the credential handover, forced halfway: a second connection, then the swap, with the match going on
		if phase_t >= seconds / 2.0 and _renew_slot == -2:
			_renew_slot = slot
			transport.renew_now()
		Q3Net.usercmd(Q3Net.time(), Vector3(0, fmod(frames * 0.7, 360.0), 0), 1 if frames % 60 < 8 else 0, 0, 127, 0, 127 if frames % 100 == 0 else 0)
		var pair := Q3Net.snapshots(1 << 30)
		if not pair.is_empty() and slot >= 0:
			var m: int = pair["message"]
			if m != _checked_msg:
				_checked_msg = m
				var theirs := Q3Net.server_frame(slot, m)
				if not theirs.is_empty():
					checked += 1
					if pair["snap"]["ps"] == theirs["ps"] and pair["snap"]["entities"] == theirs["entities"]:
						equal += 1
						if transport.renewals > 0:
							renew_checked += 1
					elif first_diff == "":
						first_diff = "message %d differs" % m
		var pr := Q3Net.predict()
		if not pr.is_empty():
			pending[int(pr["commandTime"])] = pr["origin"]
		for ct in pending.keys():
			if history.has(ct):
				predicted += 1
				if Vector3(pending[ct]).distance_to(history[ct]) < 0.02:
					matched += 1
				pending.erase(ct)
		Q3Net.next_commands()
		if phase_t >= seconds:
			Q3Net.disconnect_from()
			for p in Q3Net.outgoing(64):
				transport.send(p)
			transport.close()
			_to("leave")


func _finish(why: String) -> void:
	set_process(false)
	var ok := why == "" and listing_seen and joined and checked > 40 and equal == checked and predicted > 100 and matched >= predicted * 0.95 \
		and gone_ok and listing_gone and transport != null and transport.renewals == 1 and host_renewed == 1 and renew_checked > 20 		and _renew_slot >= 0
	if why != "":
		out.append(why)
	out.append("hub: listing browsable %s; joined over WebRTC %s (%s)" % [str(listing_seen), str(joined), join_reason])
	out.append("snapshots: %d checked, %d identical to the server's%s" % [checked, equal, "" if first_diff == "" else " (" + first_diff + ")"])
	out.append("prediction: %d/%d land within 0.02 units" % [matched, predicted])
	out.append("handover: joiner swapped %d, host swapped %d, same slot %d, %d exact snapshots after it" % [
		transport.renewals if transport else -1, host_renewed, _renew_slot, renew_checked])
	out.append("leaving: slot and peer freed %s; listing gone from the directory %s" % [str(gone_ok), str(listing_gone)])
	for l in out:
		print(l)
	print("RTC LOOPBACK ", "ok" if ok else "FAILED")
	host_client.close(false)
	join_client.close(false)
	Q3Server.exec_now("killserver")
	# let libdatachannel's threads finish with the closed connections before the process ends
	if host_net:
		host_net.stop()
		host_net.queue_free()
	if joiner:
		joiner.queue_free()
	transport = null
	await get_tree().create_timer(1.0).timeout
	get_tree().quit(0 if ok else 1)
