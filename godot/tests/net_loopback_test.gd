extends Node
## Loopback network test (MULTIPLAYER_PLAN.md M1-M2): id's server and the net client in one process,
## talking through packets only, with fake latency and loss between them.
##   godot --headless --path . res://tests/net_loopback_test.tscn [-- map=q3dm17 seconds=20 bots=3 latency=40 loss=0.05]
## Checks: the handshake completes and the server gives the client a slot; every snapshot the client
## decodes is exactly the one the server built (as the network carries it); the client's clock follows
## the server's; its prediction lands where the server later puts the player (same command time); server
## commands and configstrings arrive; and disconnecting frees the slot. Prints "NET LOOPBACK ok|FAILED".

const PEER := 7
const BOTS := ["sarge", "xaero", "crash", "klesk", "anarki", "doom"]


func _ready() -> void:
	var args := {}
	for a in OS.get_cmdline_user_args():
		if a.contains("="):
			args[a.get_slice("=", 0)] = a.get_slice("=", 1)
	var r := run(String(args.get("map", "q3dm17")), int(args.get("seconds", "20")), int(args.get("bots", "3")),
		int(args.get("latency", "40")), float(args.get("loss", "0.05")), int(args.get("snaps", str(Q3NetFeed.SNAPS))), args.get("verbose", "0") == "1")
	print(r["report"])
	print("NET LOOPBACK ", "ok" if r["ok"] else "FAILED")
	get_tree().quit(0 if r["ok"] else 1)


static func run(map: String, seconds: int, bots: int, latency: int, loss: float, snaps := Q3NetFeed.SNAPS, verbose := false) -> Dictionary:
	var lines: PackedStringArray = []
	var err := Q3Host.start()
	if err != "":
		return {"ok": false, "report": "start: " + err}
	if not ClassDB.class_exists("Q3Net"):
		return {"ok": false, "report": "Q3Net isn't in this build of q3core"}
	for cv in {"g_gametype": "0", "fraglimit": "0", "timelimit": "0", "g_doWarmup": "0"}:
		Q3Server.set_cvar(cv, {"g_gametype": "0", "fraglimit": "0", "timelimit": "0", "g_doWarmup": "0"}[cv])
	if verbose:   # a line for every packet
		Q3Server.set_cvar("developer", "1")
		Q3Server.set_cvar("showdrop", "1")
		Q3Server.set_cvar("showpackets", "1")
	if Q3Server.exec_now("map " + map) != 0 or not Q3Server.running():
		return {"ok": false, "report": "map %s: %s" % [map, Q3Server.error()]}
	Q3Server.exec_now("kick allbots")   # bots outlive a map change (the self-test's match check leaves three)
	# knockback is the server's alone (no client predicts it), so a bot's rocket would read as a prediction failure
	var knockback := Q3Server.cvar("g_knockback")
	Q3Server.set_cvar("g_knockback", "0")
	for i in bots:
		Q3Server.exec("addbot %s 3" % BOTS[i % BOTS.size()])
	Q3Server.frame(Q3Host.FRAME_MSEC)

	var rng := RandomNumberGenerator.new()
	rng.seed = 1234
	# a fast link, so the 14 KB gamestate isn't paced by wall-clock rate control while the test runs far faster than real time
	var info := Q3Host.userinfo({"name": "Remote", "model": "sarge"}).replace("\\rate\\%d" % Settings.rate(), "\\rate\\90000\\snaps\\%d" % snaps)
	Q3Net.connect_to(info, 125)

	# packets in flight: [deliver_at_ms, bytes]
	var to_server: Array = []
	var to_client: Array = []
	var now := 0
	var step := Q3Host.FRAME_MSEC
	var frames := seconds * 1000 / step
	var connected_at := -1
	var snaps_checked := 0
	var snaps_equal := 0
	var first_diff := ""
	var history := {}        # server commandTime -> [origin, velocity] of the remote player
	var predicted := 0
	var matched := 0
	var worst := 0.0
	var commands := 0
	var kinds := {}
	var slot := -1
	var dropped_pkts := 0
	var clock_err := 0
	var clock_n := 0
	var checked_msg := -1
	var last_state := -1
	var pending := {}
	var f := 0
	while f < frames:
		now += step
		# 1. the client's frame, its packets onto the wire
		var st := Q3Net.frame(step, false)
		if st != last_state:
			print("[net test] %d ms: client state %d -> %d %s" % [now, last_state, st, Q3Net.error()])
			last_state = st
		if st == 0:
			return {"ok": false, "report": "client dropped at %d ms: %s" % [now, Q3Net.error()]}
		if st == 5:
			if connected_at < 0:
				connected_at = now
			var a := Vector3(0, fmod(f * 0.7, 360.0), 0)
			Q3Net.usercmd(Q3Net.time(), a, 1 if f % 60 < 8 else 0, 0, 127, 0, 127 if f % 100 == 0 else 0)
		for p in Q3Net.outgoing(64):
			if rng.randf() < loss:
				dropped_pkts += 1
			else:
				to_server.append([now + latency, p])
		# 2. deliver what has arrived at the server
		while to_server.size() > 0 and to_server[0][0] <= now:
			var pk: PackedByteArray = to_server.pop_front()[1]
			var rv := Q3Server.net_receive(PEER, pk)
			if verbose and now < 400 and pk.decode_s32(0) != -1:
				print("[net test] sequenced packet to server: %d bytes %s -> %s" % [pk.size(), pk.hex_encode(), str(rv)])
		# 3. the server's frame; its packets onto the wire
		if Q3Server.frame(step) != 0:
			return {"ok": false, "report": "server frame: " + Q3Server.error()}
		slot = Q3Server.net_peer_client(PEER)
		for pk in Q3Server.net_outgoing(256):
			if pk["peer"] != PEER:
				continue
			if rng.randf() < loss:
				dropped_pkts += 1
			else:
				to_client.append([now + latency, pk["data"]])
		while to_client.size() > 0 and to_client[0][0] <= now:
			Q3Net.receive(to_client.pop_front()[1])
		# 4. checks
		if slot >= 0:
			var ps := Q3Server.player_state(slot)
			if not ps.is_empty() and Q3Server.client_state(slot) == 4:
				history[int(ps["commandTime"])] = [ps["origin"], ps["velocity"]]
		for c in Q3Net.next_commands():
			commands += 1
			kinds[c.get_slice(" ", 0)] = kinds.get(c.get_slice(" ", 0), 0) + 1
		if st == 5 and slot >= 0:
			var pair := Q3Net.snapshots(Q3Net.time())
			if not pair.is_empty():
				var m: int = pair["newest_message"]
				if m != checked_msg:
					checked_msg = m
					var mine := Q3Net.snapshots(1 << 30)   # the newest
					var theirs := Q3Net.server_frame(slot, int(mine["message"]))
					if not theirs.is_empty():
						snaps_checked += 1
						var same: bool = mine["snap"]["ps"] == theirs["ps"] and mine["snap"]["entities"] == theirs["entities"]
						if same:
							snaps_equal += 1
						elif first_diff == "":
							first_diff = _diff(mine["snap"], theirs, int(mine["message"]))
				clock_n += 1
				clock_err += absi(Q3Net.time() - Q3Server.time())
			var pred := Q3Net.predict()
			if not pred.is_empty():
				pending[int(pred["commandTime"])] = pred["origin"]
		# the server's state at each command time it has run, against what the client predicted for it
		for ct in pending.keys():
			if history.has(ct):
				predicted += 1
				var d := Vector3(pending[ct]).distance_to(history[ct][0])
				worst = maxf(worst, d)
				if d < 0.02:
					matched += 1
				pending.erase(ct)
			elif ct < int(Q3Server.time()) - 2000:
				pending.erase(ct)
		f += 1

	var cs_bad := 0
	var cs_n := 0
	for i in 1024:
		var s := Q3Server.configstring(i)
		if s != "":
			cs_n += 1
			if i < 32 and Q3Net.configstring(i) != s:
				cs_bad += 1
			elif i >= 32 and Q3Net.configstring(i) != s:
				cs_bad += 1
	Q3Server.set_cvar("g_knockback", knockback)
	var stats := Q3Net.stats()
	# remote console over the same packets (Q3Net.rcon): the right password runs `status`, a wrong one is refused
	Q3Server.set_cvar("rconpassword", "loopback")
	var rcon_ok := _rcon("loopback", step).contains("map:")
	var rcon_refused := _rcon("nope", step).contains("Bad rcon")
	Q3Net.disconnect_from()
	for i in 20:
		for p in Q3Net.outgoing(64):
			Q3Server.net_receive(PEER, p)
		Q3Server.frame(step)
		for pk in Q3Server.net_outgoing(256):
			pass
	var out_slot := Q3Server.net_peer_client(PEER)
	var freed := out_slot < 0 or Q3Server.client_state(out_slot) < 2
	# the first packets sent by the client after the disconnect reach the server through net_receive
	var avg_clock := float(clock_err) / maxf(clock_n, 1)
	var active_s := (now - connected_at) / 1000.0 if connected_at >= 0 else 0.0
	# prediction: 92-95% of compared moves land within 0.02 units on a 5% loss link all through M8 (worst 10-40 units)
	var ok := connected_at >= 0 and slot >= 0 and snaps_checked > active_s * 10 and snaps_equal == snaps_checked \
		and predicted > active_s * 40 and matched >= predicted * 0.9 and commands > 0 and cs_bad == 0 and freed \
		and rcon_ok and rcon_refused
	lines.append("%s: %d s over a %d ms, %.0f%% loss link, connected at %d ms, slot %d" % [map, seconds, latency * 2, loss * 100.0, connected_at, slot])
	lines.append("bandwidth: snaps %d, %d bots: down %.1f KB/s, up %.1f KB/s per player" % [snaps, bots, stats["bytes_in"] / 1024.0 / maxf(active_s, 1.0), stats["bytes_out"] / 1024.0 / maxf(active_s, 1.0)])
	lines.append("snapshots: %d checked, %d identical to the server's%s" % [snaps_checked, snaps_equal, "" if first_diff == "" else "\n  first difference: " + first_diff])
	lines.append("prediction: %d/%d land within 0.02 units of the server's state at that command time (worst %.3f)" % [matched, predicted, worst])
	lines.append("clock: the client's server time is %.1f ms from the server's on average" % avg_clock)
	lines.append("server commands: %d %s; configstrings: %d of %d differ" % [commands, str(kinds), cs_bad, cs_n])
	lines.append("rcon: status answered %s, wrong password refused %s" % [str(rcon_ok), str(rcon_refused)])
	lines.append("traffic: client sent %d packets (%d bytes), received %d (%d bytes), %d snapshots dropped on the way, %d link drops; slot freed after disconnect: %s" % [
		stats["packets_out"], stats["bytes_out"], stats["packets_in"], stats["bytes_in"], stats["dropped"], dropped_pkts, str(freed)])
	return {"ok": ok, "report": "\n".join(lines)}


## One rcon command over a lossless link; what came back within half a second of game frames.
static func _rcon(password: String, step: int) -> String:
	Q3Net.rcon(password, "status")
	var out := ""
	for i in 500 / step:
		Q3Net.frame(step, false)
		for p in Q3Net.outgoing(64):
			Q3Server.net_receive(PEER, p)
		Q3Server.frame(step)
		for pk in Q3Server.net_outgoing(256):
			if pk["peer"] == PEER:
				Q3Net.receive(pk["data"])
		out += Q3Net.rcon_output()
	return out


## The first thing that differs between two snapshots, for the report.
static func _diff(mine: Dictionary, theirs: Dictionary, message: int) -> String:
	for k in mine["ps"]:
		if mine["ps"][k] != theirs["ps"].get(k):
			return "message %d, ps.%s: client %s, server %s" % [message, k, str(mine["ps"][k]), str(theirs["ps"].get(k))]
	var a: Array = mine["entities"]
	var b: Array = theirs["entities"]
	if a.size() != b.size():
		return "message %d, %d entities against the server's %d" % [message, a.size(), b.size()]
	for i in a.size():
		if a[i] != b[i]:
			for k in a[i]:
				if a[i][k] != b[i].get(k):
					return "message %d, entity %d .%s: client %s, server %s" % [message, a[i]["number"], k, str(a[i][k]), str(b[i].get(k))]
	return "message %d" % message
