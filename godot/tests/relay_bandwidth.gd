extends Node
## What a relayed player costs (MULTIPLAYER_PLAN.md M8): a host and a joiner in this process connect through
## Cloudflare's TURN relay (the live hub, relay only) with tools/perf/turn_meter.py between each of them and the
## relay, and play on id's server while the snapshot rate, the packet rate and the number of players change.
## Each window reports the game's own packets and what the relay sent each side (Cloudflare bills that, its
## egress to each TURN client, framing included), per second and per player-hour.
##   python tools/perf/turn_meter.py --listen 34780 34781
##   godot --headless --path . res://tests/relay_bandwidth.tscn [-- seconds=45 fps=144 server=wss://...]
## The listing has a password and the two test identities are reused, so no one else joins and no new players
## are made. Credentials are fetched once a side. Writes work/relay_bandwidth.json. Prints "RELAY BANDWIDTH ok|FAILED".

const JOIN_PORT := 34780
const HOST_PORT := 34781
const BOTS := ["sarge", "xaero", "crash", "klesk", "anarki", "doom", "hunter", "major", "mynx", "orbb", "slash"]
const WINDOWS := [
	{"snaps": 40, "mp": 60, "bots": 3},
	{"snaps": 20, "mp": 60, "bots": 3},
	{"snaps": 30, "mp": 60, "bots": 3},
	{"snaps": 42, "mp": 60, "bots": 3},
	{"snaps": 63, "mp": 60, "bots": 3},
	{"snaps": 40, "mp": 30, "bots": 3},
	{"snaps": 40, "mp": 125, "bots": 3},
	{"snaps": 40, "mp": 60, "bots": 7},
	{"snaps": 42, "mp": 60, "bots": 7},
	{"snaps": 40, "mp": 60, "bots": 11},
]
const SETTLE := 6.0   # sv_floodprotect holds a userinfo change up to 5 s

var server := Online.DEFAULT_SERVER
var seconds := 45.0
var meter_path := ProjectSettings.globalize_path("res://").path_join("../work/turn_meter.json").simplify_path()
var host_client: Q3OnlineClient
var join_client: Q3OnlineClient
var host_net: Q3HostNet
var joiner: Q3NetJoin
var transport: Q3RtcTransport
var matches: Array = []
var phase := "hubs"
var t := 0.0
var phase_t := 0.0
var bots := 0
var window := -1
var settled := false
var frames := 0
var start_meter: Dictionary = {}
var start_stats: Dictionary = {}
var start_t := 0.0
var rows: Array = []
var _acc := 0.0
var _info := ""


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("server="):
			server = a.substr(7)
		if a.begins_with("seconds="):
			seconds = float(a.substr(8))
		if a.begins_with("fps="):
			Engine.max_fps = int(a.substr(4))
	if Engine.max_fps == 0:
		Engine.max_fps = 144   # a player's frame rate paces their packets (maxpackets is a ceiling)
	if _meter().is_empty():
		_finish("no meter: start python tools/perf/turn_meter.py --listen %d %d first" % [JOIN_PORT, HOST_PORT])
		return
	var err := Q3Host.start()
	if err != "":
		_finish("start: " + err)
		return
	for cv in {"g_gametype": "0", "fraglimit": "0", "timelimit": "0", "g_doWarmup": "0"}:
		Q3Server.set_cvar(cv, {"g_gametype": "0", "fraglimit": "0", "timelimit": "0", "g_doWarmup": "0"}[cv])
	if Q3Server.exec_now("map q3dm17") != 0 or not Q3Server.running():
		_finish("map: " + Q3Server.error())
		return
	_set_bots(WINDOWS[0]["bots"])
	Q3Server.frame(Q3Host.FRAME_MSEC)
	Q3RtcPeer.turn_rewrite = func(url: String, offerer: bool) -> String:
		return "turn:127.0.0.1:%d?transport=udp" % (JOIN_PORT if offerer else HOST_PORT)
	host_client = Q3OnlineClient.new("user://test_net/host.key")
	join_client = Q3OnlineClient.new("user://test_net/join.key")
	join_client.net.connect(func(m): if m.get("type") == "net_matches": matches = m["matches"])
	host_client.open(server, {"name": "RtcHost", "model": "sarge", "color": "#2266ff"})
	join_client.open(server, {"name": "RtcJoiner", "model": "sarge", "color": "#ff6622"})


func _set_bots(n: int) -> void:
	while bots < n:
		Q3Server.exec("addbot %s 3" % BOTS[bots])
		bots += 1


func _meter() -> Dictionary:
	var d: Variant = JSON.parse_string(FileAccess.get_file_as_string(meter_path))
	return d if d is Dictionary and Time.get_unix_time_from_system() - float(d.get("time", 0)) < 3.0 else {}


func _process(delta: float) -> void:
	t += delta
	phase_t += delta
	host_client.poll(delta)
	join_client.poll(delta)
	if phase != "play" and t > 90.0:
		_finish("stuck in '%s' (host %s, joiner %s)" % [phase, host_client.status, join_client.status])
		return
	match phase:
		"hubs":
			if host_client.status == "online" and join_client.status == "online" and host_client.state.has("me") and join_client.state.has("me"):
				host_net = Q3HostNet.new()
				add_child(host_net)
				host_net.start(host_client, {"name": "Bandwidth test", "mode": "ffa", "map": "q3dm17", "max": 16, "password": "meter", "version": "meter"})
				_to("browse")
		"browse":
			_server_step(delta)
			if phase_t > 0.5 and fmod(phase_t, 1.0) < delta:
				join_client.send("net_browse")
			for m in matches:
				if m["host"] == host_client.state["me"]["id"]:
					joiner = Q3NetJoin.new()
					add_child(joiner)
					joiner.finished.connect(func(ok, tr, why):
						if ok:
							transport = tr
						else:
							_finish("the join failed: " + why))
					joiner.begin(join_client, host_client.state["me"]["id"], "meter", "meter")
					_to("join")
					break
		"join":
			_server_step(delta)
			if transport != null:
				var w: Dictionary = WINDOWS[0]
				_info = Q3Host.userinfo({"name": "RtcJoiner", "model": "sarge"}).replace("\\rate\\%d" % Settings.rate(), "\\rate\\90000")
				Q3Net.connect_to(_info + "\\snaps\\%d" % w["snaps"], w["mp"])
				_to("play")
		"play":
			_play(delta)


func _to(p: String) -> void:
	print("[relay test] %.1f s: %s" % [t, p])
	phase = p
	phase_t = 0.0


func _server_step(delta: float) -> void:
	_acc += delta * 1000.0
	var ms := int(_acc)
	_acc -= ms
	if ms <= 0 or host_net == null:
		return
	host_net.pump_in(delta)
	if Q3Server.frame(ms) != 0:
		_finish("server: " + Q3Server.error())
		return
	host_net.pump_out()


func _play(delta: float) -> void:
	_server_step(delta)
	for p in transport.poll():
		Q3Net.receive(p)
	var st := Q3Net.frame(maxi(int(round(delta * 1000.0)), 1), false)
	if st == 0:
		_finish("the client dropped: " + Q3Net.error())
		return
	for p in Q3Net.outgoing(64):
		transport.send(p)
	Q3Net.next_commands()
	if st != 5:
		return
	frames += 1
	# a player running, turning, firing and now and then jumping
	Q3Net.usercmd(Q3Net.time(), Vector3(0, fmod(frames * 0.7, 360.0), 0), 1 if frames % 60 < 8 else 0, 0, 127, 0, 127 if frames % 100 == 0 else 0)
	if window < 0:
		_begin_window(0)
	elif not settled and phase_t >= SETTLE:
		settled = true
		start_meter = _meter()
		start_stats = Q3Net.stats()
		start_t = t
	elif settled and t - start_t >= seconds:
		_end_window()
		if window + 1 < WINDOWS.size():
			_begin_window(window + 1)
		else:
			_finish("")


func _begin_window(i: int) -> void:
	window = i
	settled = false
	phase_t = 0.0
	var w: Dictionary = WINDOWS[i]
	_set_bots(w["bots"])
	Q3Net.set_maxpackets(w["mp"])
	Q3Net.command('userinfo "%s"' % (_info + "\\snaps\\%d" % w["snaps"]))
	print("[relay test] %.1f s: window %d: snaps %d, maxpackets %d, %d bots" % [t, i, w["snaps"], w["mp"], w["bots"]])


func _leg(m0: Dictionary, m1: Dictionary, port: int, key: String) -> Array:
	var a: Array = m0.get(str(port), {}).get(key, [0, 0])
	var b: Array = m1.get(str(port), {}).get(key, [0, 0])
	return [float(b[0]) - float(a[0]), float(b[1]) - float(a[1])]


func _end_window() -> void:
	var m1 := _meter()
	var s1 := Q3Net.stats()
	var dur := t - start_t
	if m1.is_empty() or start_meter.is_empty():
		_finish("the meter stopped writing")
		return
	var w: Dictionary = WINDOWS[window]
	var r := {"snaps": w["snaps"], "maxpackets": w["mp"], "bots": w["bots"], "players": w["bots"] + 1, "seconds": dur,
		"fps": Engine.max_fps}
	r["game_in"] = [(float(s1["packets_in"]) - float(start_stats["packets_in"])) / dur, (float(s1["bytes_in"]) - float(start_stats["bytes_in"])) / dur]
	r["game_out"] = [(float(s1["packets_out"]) - float(start_stats["packets_out"])) / dur, (float(s1["bytes_out"]) - float(start_stats["bytes_out"])) / dur]
	for side in [["to_player", JOIN_PORT], ["to_host", HOST_PORT]]:
		var d := {}
		for k in ["down", "down_data", "down_indication", "down_control", "up"]:
			var v := _leg(start_meter, m1, side[1], k)
			d[k] = [v[0] / dur, v[1] / dur]
		r[side[0]] = d
	r["billed_bytes_per_s"] = r["to_player"]["down"][1] + r["to_host"]["down"][1]
	r["billed_mb_per_hour"] = r["billed_bytes_per_s"] * 3600.0 / 1e6
	rows.append(r)
	print("  snaps %d maxpackets %d, %d players: game %.1f pk/s %.2f KB/s in, %.1f pk/s %.2f KB/s out | relay to player %.1f pk/s %.2f KB/s, to host %.1f pk/s %.2f KB/s | billed %.1f MB per player-hour" % [
		w["snaps"], w["mp"], r["players"], r["game_in"][0], r["game_in"][1] / 1024.0, r["game_out"][0], r["game_out"][1] / 1024.0,
		r["to_player"]["down"][0], r["to_player"]["down"][1] / 1024.0, r["to_host"]["down"][0], r["to_host"]["down"][1] / 1024.0,
		r["billed_mb_per_hour"]])


func _finish(why: String) -> void:
	set_process(false)
	Q3RtcPeer.turn_rewrite = Callable()
	var ok := why == "" and rows.size() == WINDOWS.size() and rows.all(func(r): return r["to_player"]["down_data"][1] > 0.0)
	if why != "":
		print(why)
	var f := FileAccess.open(meter_path.get_base_dir().path_join("relay_bandwidth.json"), FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify({"rows": rows, "server": server}, "  "))
	print("RELAY BANDWIDTH ", "ok" if ok else "FAILED")
	if Q3Net.state() != 0:
		Q3Net.disconnect_from()
		if transport:
			for p in Q3Net.outgoing(64):
				transport.send(p)
	if transport:
		transport.close()
	if host_net:
		host_net.stop()
		host_client.send("net_unhost")
	if host_client:
		host_client.close(false)
		join_client.close(false)
	Q3Server.exec_now("killserver")
	if host_net:
		host_net.queue_free()
	if joiner:
		joiner.queue_free()
	transport = null
	await get_tree().create_timer(1.0).timeout
	get_tree().quit(0 if ok else 1)
