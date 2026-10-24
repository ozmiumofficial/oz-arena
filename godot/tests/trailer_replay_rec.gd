extends Node
## Trailer footage (TRAILER_PLAN.md): records a bot free-for-all as a replay, headless and fast, for
## tests/trailer_action.tscn to film. The bots warm up unrecorded (armed, the map's items in play),
## then `seconds` of the match go into the replay. The recording player is a spectator.
##   godot --headless --path . res://tests/trailer_replay_rec.tscn -- map=q3dm6 bots=sarge,xaero,klesk,doom,anarki,visor
##     skill=5 seconds=180 warmup=45 dir=C:/x/replays
## Prints "TRAILER REPLAY <path>" and quits.

var _map := "q3dm6"
var _bots: PackedStringArray = ["sarge", "xaero", "klesk", "doom", "anarki", "visor"]
var _skill := 5
var _seconds := 180.0
var _warmup := 45.0
var _dir := "user://trailer_replays"


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			match kv[0]:
				"map": _map = kv[1]
				"bots": _bots = kv[1].split(",", false)
				"skill": _skill = int(kv[1])
				"seconds": _seconds = float(kv[1])
				"warmup": _warmup = float(kv[1])
				"dir": _dir = kv[1]
	var path := _record()
	if path == "":
		print("TRAILER REPLAY FAILED")
		get_tree().quit(1)
		return
	print("TRAILER REPLAY %s" % ProjectSettings.globalize_path(path))
	get_tree().quit()


func _record() -> String:
	if Q3Host.start() != "":
		return ""
	DirAccess.make_dir_recursive_absolute(_dir)
	Q3ReplayFile.dir = _dir
	for kv in [["g_gametype", "0"], ["fraglimit", "0"], ["timelimit", "0"], ["g_doWarmup", "0"]]:
		Q3Server.set_cvar(kv[0], kv[1])
	if Q3Server.exec_now("map " + _map) != 0:
		return ""
	for b in _bots:
		Q3Server.exec("addbot %s %d" % [b, _skill])
	Q3Server.frame(8)
	var me := Q3Server.connect_local(Q3Host.userinfo({"name": "Director", "model": "sarge"}))
	Q3Server.client_command(me, "team spectator")
	var step := func() -> void:
		Q3Server.usercmd(me, Q3Server.time() + 8, Vector3.ZERO, 0, 0, 0, 0, 0)
		Q3Server.frame(8)
		Q3Server.next_commands(me)
	for f in int(_warmup * 125):
		step.call()
	var rec := Q3ReplayRecorder.new()
	if rec.start({"map": _map, "mode": "ffa", "title": "TRAILER  -  " + _map.to_upper(), "local_client": me}) != "":
		return ""
	for f in int(_seconds * 125):
		step.call()
		rec.pump()
	var p := rec.finish({"standings": [], "ended": "trailer"})
	Q3Server.exec_now("killserver")
	Q3ReplayFile.dir = Q3ReplayFile.DIR
	return p
