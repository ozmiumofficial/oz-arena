extends Node
## Map download from the host (Q3MapFetch): two processes, because in one the joiner shares the host's
## engine and always has the map.
##   host:  godot --headless --path . res://tests/map_fetch_test.tscn -- role=host [map=rustgrad seconds=120]
##          lists a listen server on a Maps folder map (Online autoload, the player's identity)
##   join:  godot --headless --path . res://tests/map_fetch_test.tscn -- role=join maps_dir=<empty folder>
##          [map=rustgrad]: its Maps folder is the empty one, its identity user://test_net/fetch.key; it joins
##          through Gameplay's own join, so q3core finds the map missing, the pk3 comes over the files
##          channel, the paks are read again and the joiner enters the match
## Both need a local hub (`cd online && npm run dev`, NET_DIRECT=1 in online/.dev.vars) at server=
## (default ws://127.0.0.1:8787). direct=1|0 (each role, default 1) is that side's "Allow direct map
## downloads" for this run (not saved). The join prints "MAP FETCH ok|FAILED": with expect=ok (default),
## in the match with a pk3 in its Maps folder whose md5 is the host's, and under 64 KB of it through the
## relay (the file goes over the direct link); with expect=refused, the join stops with the message
## saying who has to allow it, and no file was saved.

const NAME := "Map fetch test"

var role := "host"
var map := "rustgrad"
var server := "ws://127.0.0.1:8787"
var seconds := 120.0
var maps_dir := ""
var game: Node
var t := 0.0
var phase := "online"
var was_server := ""
var client: Q3OnlineClient
var joiner: Q3NetJoin
var matches: Array = []
var _poll := 0.0
var _saw_download := false
var _best := 0.0
var direct := true
var expect := "ok"
var _relay_bytes := 0


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := String(a).split("=", true, 1)
		if kv.size() == 2:
			match kv[0]:
				"role": role = kv[1]
				"map": map = kv[1]
				"server": server = kv[1]
				"seconds": seconds = float(kv[1])
				"maps_dir": maps_dir = kv[1]
				"direct": direct = kv[1] != "0"
				"expect": expect = kv[1]
	Settings.set_value("network/direct_downloads", direct, false)
	if role == "host":
		was_server = String(Settings.get_value("network/online_server"))
		Settings.set_for_run("network/online_server", server)
		Online.connect_now()
	else:
		Q3MapPacks.dir_override = maps_dir
		Session.refresh_custom_maps()
		Online._clip_render = true   # the host process holds this PC's identity: this one stays off it
		Online.connect_now()
		client = Q3OnlineClient.new("user://test_net/fetch.key")
		client.net.connect(func(m): if m.get("type") == "net_matches": matches = m["matches"])
		client.open(server, {"name": "Fetcher", "model": "sarge", "color": "#22aaff"})


func _process(delta: float) -> void:
	t += delta
	if role == "host":
		_host()
	else:
		_join(delta)


func _host() -> void:
	if phase == "online" and Online.client.status == "online":
		var s := Session.new_setup("ffa", map)
		s.bots = [{"name": "Sarge", "skill": 1, "team": ""}]
		s.server_type = "listen"
		s.online = {"name": NAME}
		Session.setup = s
		game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
		add_child(game)
		phase = "hosting"
		print("hosting %s" % map)
	if t > seconds:
		print("host done")
		_quit(0)


func _join(delta: float) -> void:
	client.poll(delta)
	if t > seconds:
		_finish("timed out in '%s'" % phase)
		return
	match phase:
		"online":
			if client.status == "online" and client.state.has("me"):
				phase = "browse"
		"browse":
			_poll -= delta
			if _poll <= 0.0:
				_poll = 1.0
				client.send("net_browse")
			for m in matches:
				if m["name"] == NAME:
					joiner = Q3NetJoin.new()
					add_child(joiner)
					joiner.finished.connect(_on_joined)
					joiner.begin(client, m["host"], "", Online.game_version())
					phase = "join"
					break
		"play":
			if game._map_fetch != null:
				_saw_download = true
				_best = maxf(_best, game._map_fetch.progress())
				if game._map_fetch._peer != null:
					_relay_bytes = game._map_fetch._peer.files_bytes
			if game._error != "":
				if expect == "refused":
					_refused(game._error)
				else:
					_finish("gameplay: " + game._error)
			elif game.net_feed != null and not game._joining and Q3Net.state() == 5:
				_finish("")


func _on_joined(ok: bool, transport: Q3RtcTransport, why: String) -> void:
	if not ok:
		_finish("join failed: " + why)
		return
	Session.join = {"transport": transport, "player": {"name": "Fetcher", "model": "sarge", "skin": "default"}}
	game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	add_child(game)
	phase = "play"


func _finish(why: String) -> void:
	set_process(false)
	var host_dir := ProjectSettings.globalize_path("res://").trim_suffix("/").get_base_dir().path_join(Q3MapPacks.DIR_NAME)
	var host_pk3 := ""
	for f in DirAccess.get_files_at(host_dir):
		if f.get_basename().to_lower() == map:
			host_pk3 = host_dir.path_join(f)
	var got := ""
	for f in DirAccess.get_files_at(maps_dir):
		if f.to_lower().begins_with(map) and f.to_lower().ends_with(".pk3"):
			got = maps_dir.path_join(f)
	var same := got != "" and host_pk3 != "" and FileAccess.get_md5(got) == FileAccess.get_md5(host_pk3)
	var ok := why == "" and _saw_download and same and _relay_bytes < 65536
	if why != "":
		print(why)
	print("downloaded %s (%s, %d%% seen); same md5 as the host's %s; %d bytes of it through the relay; in the match %s" % [
		got.get_file() if got != "" else "nothing", "watched" if _saw_download else "never seen",
		roundi(_best * 100.0), str(same), _relay_bytes, str(why == "")])
	print("MAP FETCH ", "ok" if ok else "FAILED")
	_quit(0 if ok else 1)


## expect=refused: the join must stop with the message, and nothing may have been saved.
func _refused(message: String) -> void:
	set_process(false)
	var saved := DirAccess.get_files_at(maps_dir).size()
	var ok := message.to_lower().contains("direct map downloads") and saved == 0
	print("refused: %s" % message.replace("
", " "))
	print("files saved: %d; relay bytes: %d" % [saved, _relay_bytes])
	print("MAP FETCH ", "ok" if ok else "FAILED")
	_quit(0 if ok else 1)


func _quit(code: int) -> void:
	set_process(false)
	if game and game.has_method("_stop_server"):
		game._stop_server()
	if client:
		client.close(false)
	if role == "host":
		Settings.set_value("network/online_server", was_server)
		Settings.save()
	await get_tree().create_timer(1.0).timeout
	get_tree().quit(code)
