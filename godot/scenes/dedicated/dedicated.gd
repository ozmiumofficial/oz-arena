class_name Q3Dedicated
extends Node
## A dedicated server (MULTIPLAYER_PLAN.md M7): the same executable run headless with `-- dedicated=<config.json>`
## (Session.after_splash), no local player, no window. It runs id's server on a rotation of maps, lists itself in
## the Server Browser as a dedicated server under its own identity key (so its name and bans persist), takes players
## over WebRTC through Q3HostNet, and sleeps its game loop while nobody is in it.
##
## Its files sit beside the config it was started with (`dir`; user://dedicated for the menu's config.json):
## status.json (written every 2 s: the menu's view), commands.txt (lines the local admin appends: kick, map, say ...;
## executed and cleared each second), stop (create it to shut the server down cleanly), server.key (its identity;
## the menu's server keeps user://online/server.key), server.log (when started by the menu). So a server started
## with `dedicated=/data/server.json` keeps everything in /data, and a test's server never touches the menu's.

const E := preload("res://scripts/game/q3_enums.gd")
const DIR := "user://dedicated"
const KEY := "user://online/server.key"
const INTERMISSION_WAIT := 10.0
## After a map loads it stays awake (up to this long) until its bots are in: id's game adds them only while it runs,
## and adding one loads its AI (about 0.8 s), so a server that slept until someone joined stalled under them (M8).
const BOT_FILL_WAIT := 60.0

var _map_t := 0.0   ## s since the map loaded
var dir := DIR   ## where this server keeps its files: its config's folder

const DEFAULTS := {
	"name": "Dedicated server",
	"password": "",
	"mode": "ffa",
	"rotation": [],          ## map names in order; empty is every map the mode plays
	"rules": {},             ## overrides of the mode's rules (fraglimit, timelimit, max_players ...)
	"max_players": 16,
	"bots": 0,               ## bot_minplayers: the game adds bots until this many are playing, and drops them as humans join
	"bot_skill": 3,
	"motd": "",
	"rcon_password": "",
	"online": true,          ## list in the Server Browser (needs the online service)
	"idle_sleep": true,      ## stop the game loop while no one is in it
	"udp_port": 0,           ## also answer plain UDP (Quake 3 clients, LAN queries, rcon) on this port; 0 is off. Players see each other's real addresses there.
}

var cfg: Dictionary = {}
var client: Q3OnlineClient
var host_net: Q3HostNet
var setup: Q3MatchSetup
var rotation: Array = []
var rotation_index := 0
var error := ""
var sleeping := false
var started_at := 0.0
var _t_status := 0.0
var _t_commands := 0.0
var _t_humans := 0.0
var _humans := 0
var _retry := 0.0
var _backoff := 2.0
var _end_wait := -1.0
var _acc := 0.0
var _fps_set := -1


## The config a server starts with: DEFAULTS under whatever the file holds.
static func load_config(path: String) -> Dictionary:
	var out := DEFAULTS.duplicate(true)
	var f := FileAccess.open(path, FileAccess.READ)
	if f:
		var d: Variant = JSON.parse_string(f.get_as_text())
		if d is Dictionary:
			for k in d:
				out[k] = d[k]
	return clean_config(out)


## Clamps a config's numbers; a UDP port outside 1024-65535 turns UDP off.
static func clean_config(out: Dictionary) -> Dictionary:
	out["name"] = String(out["name"]).strip_edges().left(40)
	out["max_players"] = clampi(int(out["max_players"]), 2, 32)
	out["bots"] = clampi(int(out["bots"]), 0, 16)
	out["bot_skill"] = clampi(int(out["bot_skill"]), 1, 5)
	if not out.get("rules") is Dictionary:
		out["rules"] = {}
	var port := int(out.get("udp_port", 0))
	out["udp_port"] = port if port >= 1024 and port <= 65535 else 0
	return out


static func config_path() -> String:
	return DIR.path_join("config.json")


static func status_path() -> String:
	return DIR.path_join("status.json")


## The folder a config file is in, as a user:// path when it is inside the user folder (user://dedicated for the menu's).
static func _folder_of(cfg_path: String) -> String:
	var folder := cfg_path.get_base_dir()
	var user := ProjectSettings.globalize_path("user://")
	if folder.replace("\\", "/").begins_with(user.trim_suffix("/")):
		return "user://" + folder.replace("\\", "/").substr(user.trim_suffix("/").length()).trim_prefix("/")
	return folder


static func save_config(c: Dictionary) -> void:
	DirAccess.make_dir_recursive_absolute(DIR)
	var f := FileAccess.open(config_path(), FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(c, "  "))


func _ready() -> void:
	started_at = Time.get_unix_time_from_system()
	var cfg_path := String(Session.dedicated) if Session.dedicated != "" else config_path()
	dir = _folder_of(cfg_path)
	DirAccess.make_dir_recursive_absolute(dir)
	DirAccess.remove_absolute(dir.path_join("stop"))
	cfg = load_config(cfg_path)
	error = _start()
	if error != "":
		push_error("dedicated server: " + error)
		_write_status()
		get_tree().quit(1)
		return
	print("dedicated server '%s' running %s on %s (%d players)" % [cfg["name"], cfg["mode"], rotation[rotation_index], cfg["max_players"]])
	if bool(cfg["online"]):
		client = Q3OnlineClient.new(KEY if dir == DIR else dir.path_join("server.key"))
		client.status_changed.connect(_on_status)
		host_net = Q3HostNet.new()
		add_child(host_net)
		host_net.start(client, _listing())
		_connect()
	else:
		host_net = Q3HostNet.new()   # not listed, but the same players path (a LAN or UDP server uses its own door)
		add_child(host_net)


func _listing() -> Dictionary:
	return {"name": cfg["name"], "mode": cfg["mode"], "map": rotation[rotation_index], "max": cfg["max_players"],
		"password": cfg["password"], "ranked": false, "version": Online.game_version(), "dedicated": true}


func _start() -> String:
	var err := Q3Host.start()
	if err != "":
		return err
	rotation = []
	for m in cfg["rotation"]:
		if not Session.map_info(String(m)).is_empty():
			rotation.append(String(m))
	if rotation.is_empty():
		for m in Session.maps_for(String(cfg["mode"])):
			if String(m.get("special", "")) != "custom":
				rotation.append(String(m["map"]))
	if rotation.is_empty():
		return "no maps for the mode '%s'" % cfg["mode"]
	setup = Session.new_setup(String(cfg["mode"]), rotation[0])
	setup.server_type = "dedicated"
	setup.source = "dedicated"
	setup.rules = cfg["rules"].duplicate()
	setup.rules["max_players"] = cfg["max_players"]
	var errs := setup.validate()
	if errs.size():
		return "; ".join(errs)
	var cvars := setup.game_cvars()
	cvars["g_readyUp"] = 0   # nobody is there to press F3 before the countdown
	cvars["sv_hostname"] = cfg["name"]
	cvars["bot_minplayers"] = cfg["bots"]
	cvars["g_spSkill"] = cfg["bot_skill"]
	cvars["g_motd"] = cfg["motd"]
	if String(cfg["rcon_password"]) != "":
		cvars["rconpassword"] = cfg["rcon_password"]
	for k in cvars:
		Q3Server.set_cvar(k, str(cvars[k]))
	var loaded := _load_map(rotation[0])
	if loaded == "" and int(cfg.get("udp_port", 0)) > 0 and not Q3Server.udp_open(int(cfg["udp_port"])):
		return "could not open UDP port %d" % int(cfg["udp_port"])
	return loaded


func _load_map(map: String) -> String:
	if Q3Server.exec_now("map " + map) != 0 or not Q3Server.running():
		return "map %s: %s" % [map, Q3Server.error()]
	_map_t = 0.0
	Q3Server.frame(Q3Host.FRAME_MSEC)
	return ""


func _connect() -> void:
	if client != null and client.status == "offline":
		client.open(Online.server(), {"name": cfg["name"], "model": "sarge", "color": "#888888"})


func _on_status(status: String, why: String) -> void:
	if status == "online":
		_backoff = 2.0
		_retry = -1.0
		print("dedicated server: listed with ", Online.server())
	elif status == "offline":
		print("dedicated server: offline (", why, "); retrying in ", _backoff, " s")
		_retry = _backoff
		_backoff = minf(_backoff * 2.0, 60.0)


func _process(delta: float) -> void:
	if client != null:
		client.poll(delta)
		if _retry > 0.0:
			_retry -= delta
			if _retry <= 0.0:
				_connect()
	if FileAccess.file_exists(dir.path_join("stop")):
		_shutdown()
		return
	_t_humans -= delta
	if _t_humans <= 0.0:
		_t_humans = 0.25
		_humans = human_count()
	_map_t += delta
	var awake := _humans > 0 or not bool(cfg["idle_sleep"]) or (_map_t < BOT_FILL_WAIT and bot_count() < mini(int(cfg["bots"]), int(cfg["max_players"]) - 1))
	sleeping = not awake
	_set_fps(125 if awake else 10)
	if host_net != null:
		host_net.pump_in(delta)
	if awake:
		_acc += delta * 1000.0
		var ms := int(_acc)
		_acc -= ms
		if ms > 0:
			if Q3Server.frame(ms) != 0:
				error = Q3Server.error()
				push_error("dedicated server stopped: " + error)
				_write_status()
				get_tree().quit(1)
				return
			_rotate(delta)
	if host_net != null:
		host_net.pump_out()
	_t_commands -= delta
	if _t_commands <= 0.0:
		_t_commands = 1.0
		_run_commands()
	_t_status -= delta
	if _t_status <= 0.0:
		_t_status = 2.0
		_write_status()


## Players in the server who are not bots (a connected client whose userinfo has no skill).
func bot_count() -> int:
	var n := 0
	for c in int(Q3Server.cvar("sv_maxclients")):
		if Q3Server.client_state(c) >= 2 and Q3Host.parse_info(Q3Server.configstring(E.CS_PLAYERS + c)).has("skill"):
			n += 1
	return n


func human_count() -> int:
	var n := host_net.peers.size() if host_net != null else 0
	if n > 0:
		return n
	for c in int(Q3Server.cvar("sv_maxclients")):
		if Q3Server.client_state(c) >= 2:
			var info := Q3Host.parse_info(Q3Server.configstring(E.CS_PLAYERS + c))
			if not info.has("skill"):
				n += 1
	return n


func _set_fps(fps: int) -> void:
	if fps != _fps_set:
		_fps_set = fps
		Engine.max_fps = fps


## The next map when the match ends: a few seconds at the scoreboard, then the rotation goes on.
func _rotate(delta: float) -> void:
	if Q3Server.configstring(E.CS_INTERMISSION) == "1":
		if _end_wait < 0.0:
			_end_wait = INTERMISSION_WAIT
		_end_wait -= delta
		if _end_wait <= 0.0:
			_end_wait = -1.0
			next_map()
	else:
		_end_wait = -1.0


func next_map() -> void:
	rotation_index = (rotation_index + 1) % rotation.size()
	var err := _load_map(rotation[rotation_index])
	if err != "":
		push_error("dedicated server: " + err)
		return
	print("dedicated server: next map ", rotation[rotation_index])
	if host_net != null:
		host_net.listing = _listing()


## Lines the local admin appended to commands.txt: kick, map, say, g_... cvars. Run as console commands.
func _run_commands() -> void:
	var path := dir.path_join("commands.txt")
	if not FileAccess.file_exists(path):
		return
	var text := FileAccess.get_file_as_string(path)
	DirAccess.remove_absolute(path)
	for line in text.split("\n", false):
		var l := line.strip_edges()
		if l == "" or l.begins_with("#"):
			continue
		print("dedicated server: > ", l)
		if l.begins_with("map "):
			var i := rotation.find(l.substr(4).strip_edges())
			if i >= 0:
				rotation_index = i
		Q3Server.exec_now(l)


func status() -> Dictionary:
	var players: Array = []
	if Q3Server.running():
		for c in int(Q3Server.cvar("sv_maxclients")):
			if Q3Server.client_state(c) >= 2:
				var info := Q3Host.parse_info(Q3Server.configstring(E.CS_PLAYERS + c))
				players.append({"client": c, "name": Q3Colors.strip(String(info.get("n", "?"))), "bot": info.has("skill")})
	return {"pid": OS.get_process_id(), "name": cfg.get("name", ""), "mode": cfg.get("mode", ""), "map": rotation[rotation_index] if rotation.size() else "",
		"max": cfg.get("max_players", 0), "players": players, "humans": _humans, "sleeping": sleeping, "error": error,
		"listed": client != null and client.status == "online", "uptime": int(Time.get_unix_time_from_system() - started_at),
		# ioquake3 takes the next free port (up to 9 on) when the asked one is in use
		"udp_port": int(Q3Server.cvar("net_port")) if Q3Server.udp_is_open() else 0,
		"time": Time.get_unix_time_from_system()}


func _write_status() -> void:
	var f := FileAccess.open(dir.path_join("status.json"), FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(status()))


func _shutdown() -> void:
	set_process(false)
	DirAccess.remove_absolute(dir.path_join("stop"))
	if host_net != null:
		host_net.stop()
	if client != null:
		client.close(false)
	if Q3Server.running():
		Q3Server.exec_now("killserver")
	Q3Server.udp_close()
	DirAccess.remove_absolute(dir.path_join("status.json"))
	print("dedicated server: stopped")
	get_tree().quit()
