extends Node
## A real dedicated server (MULTIPLAYER_PLAN.md M7): the game is run again as a headless child process with
## `-- dedicated=<config>`, it lists itself in a local hub's directory under its own identity, sleeps while empty,
## wakes when this test joins it over WebRTC, fills with bots, rotates its maps when a match ends, sleeps again
## when the player leaves, and shuts down cleanly when asked. Needs a local hub (`cd online && npm run dev`,
## NET_DIRECT=1 in online/.dev.vars). Settings > Network > Online server is pointed at the hub for the run.
##   godot --headless --path . res://tests/dedicated_test.tscn [-- server=ws://127.0.0.1:8787]
## Prints "DEDICATED ok|FAILED".

const DIR := "user://test_dedicated"   # its own folder: the server keeps its files beside its config, never the menu's

var server := "ws://127.0.0.1:8787"
var phase := "spawn"
var t := 0.0
var phase_t := 0.0
var pid := -1
var client: Q3OnlineClient
var joiner: Q3NetJoin
var transport: Q3RtcTransport
var matches: Array = []
var host_id := ""
var checks: Array = []
var _was_server := ""
var _poll_t := 0.0
var _frames := 0
var _first_map := ""
var _status: Dictionary = {}
var _rotated := false
var _woke := false
var _menu_config := ""


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("server="):
			server = a.substr(7)
	var err := Q3Host.start()   # this process is the client: Q3Net needs the engine up (collision, netchan)
	if err != "":
		_finish("start: " + err)
		return
	_was_server = String(Settings.get_value("network/online_server"))
	Settings.set_value("network/online_server", server)
	Settings.save()
	DirAccess.make_dir_recursive_absolute(DIR)
	for f in ["status.json", "stop", "commands.txt"]:
		DirAccess.remove_absolute(DIR.path_join(f))
	var cfg := {"name": "Test dedicated", "mode": "ffa", "rotation": ["q3dm17", "q3dm6"], "max_players": 8, "bots": 3,
		"rules": {"fraglimit": 1, "timelimit": 0, "warmup": 0}, "idle_sleep": true, "online": true}
	_menu_config = FileAccess.get_file_as_string(Q3Dedicated.config_path())
	var cf := FileAccess.open(DIR.path_join("config.json"), FileAccess.WRITE)
	cf.store_string(JSON.stringify(cfg))
	cf.close()
	var cfg_path := ProjectSettings.globalize_path(DIR.path_join("config.json"))
	var args := PackedStringArray(["--headless", "--path", ProjectSettings.globalize_path("res://"), "--log-file",
		ProjectSettings.globalize_path("res://").path_join("../work/dedicated_child.log").simplify_path(), "--", "dedicated=" + cfg_path])
	pid = OS.create_process(OS.get_executable_path(), args)
	client = Q3OnlineClient.new("user://test_net/ded_browser_%d.key" % Time.get_ticks_usec())
	client.net.connect(func(m): if m.get("type") == "net_matches": matches = m["matches"])
	client.open(server, {"name": "DedBrowser", "model": "sarge", "color": "#2266ff"})


func _check(ok: bool, what: String) -> void:
	checks.append([ok, what])


func _read_status() -> Dictionary:
	var f := FileAccess.open(DIR.path_join("status.json"), FileAccess.READ)
	if f == null:
		return {}
	var d: Variant = JSON.parse_string(f.get_as_text())
	return d if d is Dictionary else {}


func _to(p: String) -> void:
	print("[ded test] %.1f s: %s" % [t, p])
	phase = p
	phase_t = 0.0


func _process(delta: float) -> void:
	t += delta
	phase_t += delta
	client.poll(delta)
	_poll_t -= delta
	if _poll_t <= 0.0:
		_poll_t = 0.5
		_status = _read_status()
	if t > 150.0:
		_finish("stuck in '%s' (status %s)" % [phase, str(_status)])
		return
	match phase:
		"spawn":
			# the child starts the engine (a few seconds), lists itself, stays awake until its bots are in, and sleeps
			if not _status.is_empty() and bool(_status.get("listed", false)) and client.status == "online" and bool(_status.get("sleeping", false)):
				_check(true, "an empty dedicated server sleeps its game loop (after adding its bots)")
				_check(String(_status.get("name", "")) == "Test dedicated" and _status.get("pid", 0) != 0, "it runs under the config (name, pid %s)" % str(_status.get("pid", 0)))
				_first_map = String(_status["map"])
				_to("browse")
		"browse":
			_poll_t = minf(_poll_t, 0.5)
			if fmod(phase_t, 1.0) < delta:
				client.send("net_browse")
			for m in matches:
				if String(m.get("name", "")) == "Test dedicated":
					host_id = String(m["host"])
					_check(bool(m.get("dedicated", false)) and int(m["max"]) == 8 and String(m["mode"]) == "ffa", "the Server Browser lists it as a dedicated server")
					joiner = Q3NetJoin.new()
					add_child(joiner)
					joiner.finished.connect(func(ok, tr, _why): transport = tr if ok else null)
					joiner.begin(client, host_id, "", Online.game_version())
					_to("join")
					break
		"join":
			if transport != null:
				Q3Net.connect_to(Q3Host.userinfo({"name": "DedJoiner", "model": "sarge"}).replace("\\rate\\%d" % Settings.rate(), "\\rate\\90000\\snaps\\40"), 125)
				_to("play")
			elif phase_t > 45.0:
				_finish("never joined: " + (joiner.reason if joiner else ""))
		"play":
			_pump()
			var st := Q3Net.state()
			if st == 5:
				_frames += 1
				Q3Net.usercmd(Q3Net.time(), Vector3.ZERO, 0, 0, 0, 0, 0)
				Q3Net.next_commands()
			# status.json is rewritten every 2 s: wait for one written since the join
			if not _woke and _frames >= 100 and (not bool(_status.get("sleeping", true)) and int(_status.get("humans", 0)) >= 1 or phase_t > 12.0):
				_woke = true
				_check(not bool(_status.get("sleeping", true)) and int(_status.get("humans", 0)) >= 1, "it wakes when a player joins")
				_check(not Q3Net.snapshots(Q3Net.time()).is_empty(), "and the player is in the match")
			if _frames > 100 and String(_status.get("map", "")) != "" and String(_status["map"]) != _first_map and not _rotated:
				_rotated = true
				_check(true, "the rotation goes on to the next map after a match (%s -> %s)" % [_first_map, _status["map"]])
				_to("rotated")
			elif phase_t > 80.0:
				_finish("the maps never rotated (status %s)" % str(_status))
		"rotated":
			_pump()
			if Q3Net.state() == 5:
				Q3Net.usercmd(Q3Net.time(), Vector3.ZERO, 0, 0, 0, 0, 0)
			if phase_t > 4.0:
				_check(Q3Net.state() == 5, "the player stays connected through the map change")
				Q3Net.disconnect_from()
				transport.close()
				_to("empty")
		"empty":
			if bool(_status.get("sleeping", false)):
				_check(true, "it goes back to sleep when the player leaves")
				_to("stop")
				FileAccess.open(DIR.path_join("stop"), FileAccess.WRITE)
			elif phase_t > 20.0:
				_finish("it never slept again (status %s)" % str(_status))
		"stop":
			if not OS.is_process_running(pid):
				_check(true, "the stop file shuts it down cleanly")
				_check(not FileAccess.file_exists(DIR.path_join("status.json")), "and removes its status")
				_check(FileAccess.get_file_as_string(Q3Dedicated.config_path()) == _menu_config, "the menu's Dedicated Server settings are left alone")
				_finish("")
			elif phase_t > 20.0:
				_finish("it did not stop")


func _pump() -> void:
	for p in transport.poll():
		Q3Net.receive(p)
	Q3Net.frame(maxi(int(get_process_delta_time() * 1000.0), 1), false)
	for p in Q3Net.outgoing(64):
		transport.send(p)


func _finish(why: String) -> void:
	set_process(false)
	var ok := why == "" and checks.all(func(c): return c[0])
	if why != "":
		print(why)
	for c in checks:
		print("%s %s" % ["PASS" if c[0] else "FAIL", c[1]])
	print("DEDICATED ", "ok" if ok else "FAILED")
	if pid > 0 and OS.is_process_running(pid):
		FileAccess.open(DIR.path_join("stop"), FileAccess.WRITE)
		await get_tree().create_timer(6.0).timeout
		if OS.is_process_running(pid):
			OS.kill(pid)
	client.close(false)
	DirAccess.remove_absolute(client.key_path)
	if joiner:
		joiner.queue_free()
	Settings.set_value("network/online_server", _was_server)
	Settings.save()
	await get_tree().create_timer(1.0).timeout
	get_tree().quit(0 if ok else 1)
