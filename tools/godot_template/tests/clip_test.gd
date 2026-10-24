extends Node
## Clip export check (REPLAY_PLAN.md R8), windowed: records a few seconds of q3dm17 with bots, marks
## 3 s of it in the viewer, exports (Q3ClipExport: a second copy of the game under Movie Maker at
## 60 fps), waits for it, and reads the AVI's header: the frame count and size for that range.
##   godot --path . res://tests/clip_test.tscn -- window=1280x720
## Prints "CLIP TEST ok|FAILED"; exit code 1 on failure.

const TEST_DIR := "user://test_replays"
var _ok := true


func _ready() -> void:
	await _run()
	print("CLIP TEST ", "ok" if _ok else "FAILED")
	get_tree().quit(0 if _ok else 1)


func _check(what: String, ok: bool) -> void:
	print(("  ok    " if ok else "  FAIL  ") + what)
	if not ok:
		_ok = false


func _record() -> String:
	if Q3Host.start() != "":
		return ""
	Q3ReplayFile.dir = TEST_DIR
	DirAccess.make_dir_recursive_absolute(TEST_DIR)
	Q3Server.set_cvar("g_gametype", "0")
	Q3Server.set_cvar("fraglimit", "0")
	Q3Server.set_cvar("timelimit", "0")
	Q3Server.set_cvar("g_doWarmup", "0")
	if Q3Server.exec_now("map q3dm17") != 0:
		return ""
	for b in ["sarge", "xaero", "crash"]:
		Q3Server.exec("addbot %s 3" % b)
	Q3Server.frame(8)
	var me := Q3Server.connect_local(Q3Host.userinfo({"name": "Tester", "model": "sarge"}))
	var rec := Q3ReplayRecorder.new()
	if rec.start({"map": "q3dm17", "mode": "ffa", "title": "CLIP TEST", "local_client": me}) != "":
		return ""
	for f in 12 * 125:
		Q3Server.usercmd(me, Q3Server.time() + 8, Vector3.ZERO, 0, 0, 0, 0, 0)
		Q3Server.frame(8)
		rec.pump()
		Q3Server.next_commands(me)
	var p := rec.finish({"standings": [], "ended": "test"})
	Q3Server.exec_now("killserver")
	Q3ReplayFile.dir = Q3ReplayFile.DIR
	return p


func _run() -> void:
	var path := _record()
	_check("a replay to cut from (%s)" % path, path != "")
	if path == "":
		return
	Session.dry_run = true
	Session.watch_replay(path, 0, "replays")
	var game: Q3Gameplay = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	add_child(game)
	for i in 10:
		await get_tree().process_frame
	var f := game.replay_feed
	game.replay_playing = false
	game.seek_to(f.start_time() + 4000.0)
	await get_tree().process_frame
	game.viewer.mark(true)
	game.seek_to(f.start_time() + 7000.0)
	await get_tree().process_frame
	game.viewer.mark(false)
	_check("IN and OUT mark 3 s (%.0f to %.0f)" % [game.viewer.mark_in, game.viewer.mark_out],
		absf(game.viewer.mark_out - game.viewer.mark_in - 3000.0) < 30.0)
	game.viewer.export_clip()
	var ex: Q3ClipExport = game.viewer._export
	_check("the render starts (%s)" % (ex.path if ex else "none"), ex != null and ex.running())
	if ex == null:
		return
	var waited := 0.0
	while ex.running() and waited < 120.0:
		await get_tree().create_timer(0.5).timeout
		waited += 0.5
	for i in 4:
		await get_tree().process_frame
	var h := Q3ClipExport.avi_header(ex.path)
	print("  clip: %s, %.1f MB, %s" % [ex.path, ex.size() / 1048576.0, h])
	_check("the render finished and quit by itself (%.0f s)" % waited, not ex.running())
	var size := Q3ClipExport.movie_size()
	_check("an AVI at 60 fps, %dx%d" % [size.x, size.y], int(h.get("us_per_frame", 0)) == 16666
		and int(h.get("width", 0)) == size.x and int(h.get("height", 0)) == size.y)
	# 3 s at 60 fps, plus the few frames the copy takes to load the scene (no splash, no fades)
	var frames := int(h.get("frames", 0))
	_check("its frames are the 3 s range (%d frames, %d expected)" % [frames, ex.expected],
		frames >= ex.expected and frames <= ex.expected + 15)
	_check("the result is whole (%s)" % ex.result(), ex.result() == "")
	# a frame from the middle (the AVI is Motion JPEG): the match, not a black screen
	var img := _jpeg_frame(ex.path, 120)
	var lum := 0.0
	if img:
		for y in range(0, img.get_height(), 16):
			for x in range(0, img.get_width(), 16):
				lum += img.get_pixel(x, y).get_luminance()
		lum /= float((img.get_height() / 16) * (img.get_width() / 16))
		img.save_png(TEST_DIR.path_join("clip_frame.png"))
	_check("frame 120 is a picture of the match (mean luminance %.3f)" % lum, img != null and lum > 0.03)
	_check("the viewer says where it went", game.hud._notify.any(func(n): return String(n["text"]).begins_with("Clip saved")))
	# EXPORT again, then CANCEL: the copy is stopped and the viewer says so
	game.viewer.export_clip()
	var ex2: Q3ClipExport = game.viewer._export
	await get_tree().create_timer(2.0).timeout
	game.viewer.export_clip()
	for i in 6:
		await get_tree().process_frame
	_check("CANCEL stops a render (%s)" % ex2.result(), ex2 != ex and not ex2.running() and ex2.result() == "stopped"
		and game.hud._notify.any(func(n): return String(n["text"]).contains("stopped")))
	await get_tree().create_timer(1.0).timeout
	DirAccess.remove_absolute(ex2.path)
	game._leave()
	game.queue_free()
	Session.dry_run = false
	DirAccess.remove_absolute(ex.path)
	DirAccess.remove_absolute(path)


## The n-th JPEG in a Motion JPEG AVI (each "00db" chunk holds one, as Godot writes it), decoded.
static func _jpeg_frame(path: String, n: int) -> Image:
	var data := FileAccess.get_file_as_bytes(path)
	var seen := 0
	var i := 0
	while i < data.size() - 8:
		if data[i] == 0x30 and data[i + 1] == 0x30 and data[i + 2] == 0x64 and data[i + 3] == 0x62:   # "00db"
			var size := data.decode_u32(i + 4)
			if seen == n:
				var img := Image.new()
				return img if img.load_jpg_from_buffer(data.slice(i + 8, i + 8 + size)) == OK else null
			seen += 1
			i += 8 + size + (size & 1)
		else:
			i += 1
	return null
