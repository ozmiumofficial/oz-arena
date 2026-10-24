extends Node
## Windowed captures of a replay in the viewer (REPLAY_PLAN.md R3), Godot-drawn so the viewport
## image shows everything:
##   godot --path . res://tests/replay_capture.tscn -- out=C:/temp/replay [path=<file.q3replay>] [map=q3dm17] [window=1920x1080]
## Without path= it first records 40 s of q3dm17 with five bots into user://test_replays. Writes
## <out>_first.png (first person with the bar), _chase.png (names and outlines on), _players.png
## (the player list over a free camera) and _orbit.png.

var out := "user://replay_capture"
var path := ""
var map := "q3dm17"


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			match kv[0]:
				"out": out = kv[1]
				"path": path = kv[1]
				"map": map = kv[1]
	Engine.max_fps = 60
	if path == "":
		path = _record()
	if path == "":
		print("REPLAY CAPTURE FAILED: recording")
		get_tree().quit(1)
		return
	Session.dry_run = true
	Session.watch_replay(path, 22000, "replays")
	var game: Q3Gameplay = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	add_child(game)
	await _frames(10)
	# the leader (a bot) is more to look at than the test player walking into the void
	var rows := game.standings()
	if rows.size():
		game.watch(int(rows[0]["client"]))
	await _frames(80)
	await _shot("first")
	game.set_cam_mode("chase")
	game.viewer.names = true
	game.viewer.set_outlines(true)
	await _frames(60)
	await _shot("chase")
	game.set_cam_mode("free")
	game.viewer.picking = true
	game.replay_playing = false
	await _frames(20)
	await _shot("players")
	game.viewer.picking = false
	game.set_cam_mode("orbit")
	game.replay_playing = true
	await _frames(60)
	await _shot("orbit")
	print("REPLAY CAPTURE ok")
	Session.dry_run = false
	get_tree().quit(0)


func _record() -> String:
	if Q3Host.start() != "":
		return ""
	Q3ReplayFile.dir = "user://test_replays"
	Q3Server.set_cvar("g_gametype", "0")
	Q3Server.set_cvar("fraglimit", "0")
	Q3Server.set_cvar("timelimit", "0")
	Q3Server.set_cvar("g_doWarmup", "0")
	if Q3Server.exec_now("map " + map) != 0:
		return ""
	for b in ["sarge", "xaero", "crash", "klesk", "anarki"]:
		Q3Server.exec("addbot %s 3" % b)
	Q3Server.frame(8)
	var me := Q3Server.connect_local(Q3Host.userinfo({"name": "Tester", "model": "sarge"}))
	var rec := Q3ReplayRecorder.new()
	if rec.start({"map": map, "mode": "ffa", "title": "FREE FOR ALL  -  CAPTURE", "local_client": me}) != "":
		return ""
	for f in 40 * 125:
		Q3Server.usercmd(me, Q3Server.time() + 8, Vector3(0, fmod(f * 0.4, 360.0), 0), 0, 0, 127, 0, 0)
		Q3Server.frame(8)
		rec.pump()
		Q3Server.next_commands(me)
	var p := rec.finish({"standings": [], "ended": "capture"})
	Q3Server.exec_now("killserver")
	Q3ReplayFile.dir = Q3ReplayFile.DIR
	return p


func _shot(name: String) -> void:
	await RenderingServer.frame_post_draw
	var img := get_viewport().get_texture().get_image()
	var file := "%s_%s.png" % [out, name]
	img.save_png(file)
	print("saved ", file)


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame
