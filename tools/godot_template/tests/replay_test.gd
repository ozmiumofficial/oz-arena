extends Node
## Headless replay check (REPLAY_PLAN.md R1, R2, R9): records a bot match with Q3ReplayRecorder
## while keeping what the server held at every recorded frame (every entity, every player's
## playerState, the local player's commands), stops the server, opens the file with Q3Replay and
## compares every frame with the live run. Then random seeks (from keyframes), the timeline's
## frags against the deaths, traces in playback with only the map's collision loaded, the size
## per minute, and the replay library (list, favorite, rename, auto-clean).
##   godot --headless --path . res://tests/replay_test.tscn [-- map=q3dm17 seconds=60 bots=7 compare=1]
## A size run: -- seconds=600 bots=7 compare=0 (10 minutes, 8 players).
## Prints "REPLAY TEST ok|FAILED"; exit code 1 on failure.

const BOTS := ["sarge", "xaero", "crash", "klesk", "anarki", "doom", "hunter", "major", "orbb", "slash"]
## playerState fields msg.c never sends (MSG_WriteDeltaPlayerstate): a network client doesn't see
## them either
const PS_UNSENT := ["ping", "externalEventTime"]
const TEST_DIR := "user://test_replays"
## msg.c's field widths (entityStateFields, playerStateFields; arrays as MSG_WriteShort, negative =
## signed): the replay holds what a network client receives, so the live values are cut to these
const ES_BITS := {"event": 10, "eType": 8, "torsoAnim": 8, "eventParm": 8, "legsAnim": 8, "groundEntityNum": 10,
	"eFlags": 19, "otherEntityNum": 10, "weapon": 8, "clientNum": 8, "solid": 24, "powerups": 16, "modelindex": 8,
	"otherEntityNum2": 10, "loopSound": 8, "generic1": 8, "modelindex2": 8, "frame": 16, "trType": 8}
const PS_BITS := {"bobCycle": 8, "weaponTime": -16, "legsTimer": 8, "pm_time": -16, "eventSequence": 16, "torsoAnim": 8,
	"movementDir": 4, "events": 8, "legsAnim": 8, "pm_flags": 16, "groundEntityNum": 10, "weaponstate": 4, "eFlags": 16,
	"externalEvent": 10, "gravity": 16, "speed": 16, "delta_angles": 16, "externalEventParm": 8, "viewheight": -8,
	"damageEvent": 8, "damageYaw": 8, "damagePitch": 8, "damageCount": 8, "generic1": 8, "pm_type": 8, "torsoTimer": 12,
	"eventParms": 8, "clientNum": 8, "weapon": 5, "jumppad_ent": 10, "loopSound": 16, "stats": -16, "persistant": -16,
	"ammo": -16}

var _ok := true
var _lines: PackedStringArray = []


func _ready() -> void:
	var args := {}
	for a in OS.get_cmdline_user_args():
		if a.contains("="):
			args[a.get_slice("=", 0)] = a.get_slice("=", 1)
	var r: Dictionary = await run(String(args.get("map", "q3dm17")), int(args.get("seconds", "60")), int(args.get("bots", "7")),
		args.get("compare", "1") == "1", args.get("playback", "1") == "1")
	print("\n".join(r["lines"]))
	print("REPLAY TEST ", "ok" if r["ok"] else "FAILED")
	get_tree().quit(0 if r["ok"] else 1)


func _check(what: String, ok: bool) -> void:
	_lines.append(("  ok    " if ok else "  FAIL  ") + what)
	if not ok:
		_ok = false


## Records `seconds` of a match on `map` with `bots` bots and a local player, then plays it back.
## Returns {ok, lines, path, bytes_per_minute}.
func run(map: String, seconds: int, bots: int, compare := true, playback := true) -> Dictionary:
	var err := Q3Host.start()
	if err != "":
		return {"ok": false, "lines": ["start: " + err]}
	var old_dir := Q3ReplayFile.dir
	Q3ReplayFile.dir = TEST_DIR
	DirAccess.make_dir_recursive_absolute(TEST_DIR)
	for f in DirAccess.get_files_at(TEST_DIR):
		DirAccess.remove_absolute(TEST_DIR.path_join(f))
	var r := _record_and_play(map, seconds, bots, compare)
	if playback and String(r.get("path", "")) != "":
		await _playback(String(r["path"]))
	_library()
	Q3ReplayFile.dir = old_dir
	r["ok"] = _ok
	r["lines"] = _lines
	return r


func _record_and_play(map: String, seconds: int, bots: int, compare: bool) -> Dictionary:
	Q3Server.set_cvar("g_gametype", "0")
	Q3Server.set_cvar("fraglimit", "0")
	Q3Server.set_cvar("timelimit", "0")
	Q3Server.set_cvar("g_doWarmup", "0")
	Q3Server.set_cvar("sv_maxclients", str(maxi(bots + 1, 8)))
	if Q3Server.exec_now("map " + map) != 0 or not Q3Server.running():
		_check("map %s: %s" % [map, Q3Server.error()], false)
		return {}
	for i in bots:
		Q3Server.exec("addbot %s 3" % BOTS[i % BOTS.size()])
	Q3Server.frame(Q3Host.FRAME_MSEC)
	var me := Q3Server.connect_local(Q3Host.userinfo({"name": "Tester", "model": "sarge"}))
	_check("the local player joins", me >= 0)
	Q3Server.frame(Q3Host.FRAME_MSEC)
	Q3Server.next_commands(me)   # what came before the recording isn't in it
	var rec := Q3ReplayRecorder.new()
	var rerr := rec.start({"map": map, "mode": "ffa", "title": "REPLAY TEST", "local_client": me})
	_check("recording starts (%s)" % rerr, rerr == "")
	if rerr != "":
		return {}

	# the live run: what the server holds at every recorded frame
	var live: Array = []        ## [time, {number: entity}, {client: ps}]
	var live_cmds: PackedStringArray = []
	var frames := seconds * 1000 / Q3Host.FRAME_MSEC
	var t0 := Time.get_ticks_msec()
	var count := 0
	for f in frames:
		var t := Q3Server.time() + Q3Host.FRAME_MSEC
		Q3Server.usercmd(me, t, Vector3(0, fmod(f * 0.5, 360.0), 0), 1 if f % 50 < 10 else 0, 0, 127, 0, 127 if f % 90 == 0 else 0)
		if Q3Server.frame(Q3Host.FRAME_MSEC) != 0:
			_check("frame %d: %s" % [f, Q3Server.error()], false)
			return {}
		if f == 200:
			# chat with a percent sign and letters outside ASCII must come back as said
			Q3Server.client_command(me, "say \"100% sür été\"")
		rec.pump()
		for c in Q3Server.next_commands(me):
			# what the recorder leaves out (q3_replay.c skip_command): configstrings travel apart
			if not (c.begins_with("cs ") or c.begins_with("bcs") or c.begins_with("scores ") or c.begins_with("disconnect")):
				live_cmds.append(c)
		if Q3Server.record_frames() != count:
			count = Q3Server.record_frames()
			if compare:
				var ents := {}
				for e in Q3Server.entities():
					ents[int(e["number"])] = _wire(e, ES_BITS)
				var pss := {}
				for cl in 64:
					if Q3Server.client_state(cl) == 4:
						pss[cl] = _wire(Q3Server.player_state(cl), PS_BITS)
				live.append([Q3Server.time(), ents, pss])
	var path := rec.finish({"standings": [], "ended": "test"})
	var total := Q3Server.record_frames()   # (stopping adds a frame when commands came after the last one)
	var rec_ms := Time.get_ticks_msec() - t0
	var bytes := FileAccess.get_file_as_bytes(path).size() if path != "" else 0
	var per_min := bytes / maxf(seconds / 60.0, 0.01)
	_lines.append("%s, %d players, %d s of game time recorded in %d ms: %d frames, %.0f KB (%.2f MB a minute)" %
		[map, bots + 1, seconds, rec_ms, count, bytes / 1024.0, per_min / 1048576.0])
	_check("the file was written", path != "" and bytes > 0)
	_check("about 40 frames a second (%d frames in %d s)" % [count, seconds], absf(count / float(seconds) - 40.0) < 2.0)
	var checksum := Q3Server.cvar("sv_mapChecksum")
	Q3Server.exec_now("killserver")
	_check("the server stopped", not Q3Server.running())

	# playback
	var rp := Q3Replay.new()
	var oerr := rp.open(path)
	_check("the replay opens (%s)" % oerr, oerr == "")
	if oerr != "":
		return {"path": path, "bytes_per_minute": per_min}
	var head := rp.header()
	_check("header and trailer read (complete, map %s, %d frames)" % [head.get("map"), int(head.get("frames", 0))],
		bool(head.get("complete")) and head.get("map") == map and int(head.get("frames", 0)) == total and total - count <= 1)
	_check("every frame decodes (%d of %d)" % [rp.frame_count(), total], rp.frame_count() == total)
	var info := Q3ReplayFile.read_info(path)
	_check("the browser's quick read sees the same header", info.get("map") == map and int(info.get("frames", 0)) == total)
	var merr := Q3Replay.load_map(map)
	_check("the map's collision loads without a server (%s)" % merr, merr == "")
	_check("same map checksum as recorded (%d / %s)" % [Q3Replay.map_checksum(), checksum], str(Q3Replay.map_checksum()) == checksum)

	if compare:
		var bad := _compare_all(rp, live)
		_check("every frame's entities and playerStates match the live run (%d frames, %d mismatches)" % [live.size(), bad.size()],
			bad.is_empty() and live.size() == count)
		for b in bad.slice(0, 8):
			_lines.append("      " + b)
		# seeking: random frames out of order (keyframe + forward decode) give the same frames
		var seek_bad := 0
		var rng := RandomNumberGenerator.new()
		rng.seed = 7
		var seeks := 60
		var t_seek := Time.get_ticks_usec()
		for i in seeks:
			var fi := rng.randi_range(0, count - 1)
			if not rp.seek(fi) or not _frame_diffs(rp, live[fi]).is_empty():
				seek_bad += 1
		var us := (Time.get_ticks_usec() - t_seek) / seeks
		_check("%d random seeks land on the exact frame (%d wrong, %.1f ms each)" % [seeks, seek_bad, us / 1000.0], seek_bad == 0)

	# the timeline
	var deaths := 0
	rp.seek(rp.frame_count() - 1)
	for cl in rp.clients():
		deaths += int(rp.player_state(cl)["persistant"][8])   # PERS_KILLED
	var obits := rp.events().filter(func(e): return int(e["type"]) == 1).size()
	_check("the timeline has an obituary per death (%d obituaries, %d deaths)" % [obits, deaths], obits == deaths and deaths > 0)
	var cmds := rp.commands(-1, rp.frame_count() - 1, me)
	_check("the local player's commands come back exactly, in order (%d recorded, %d live)" % [cmds.size(), live_cmds.size()],
		cmds.size() > 0 and cmds == live_cmds)
	_check("chat keeps the percent sign and accented letters", Array(cmds).any(func(c): return String(c).contains("100% sür été")))
	# traces with only the map's collision: straight down from a player hits the floor under them
	var hits := 0
	var tried := 0
	for cl in rp.clients():
		var ps := rp.player_state(cl)
		if int(ps["pm_type"]) != 0:
			continue
		tried += 1
		var o: Vector3 = ps["origin"]
		var tr := rp.trace(o, o - Vector3(0, 0, 4096), Vector3.ZERO, Vector3.ZERO, cl, 1)
		if not tr.is_empty() and float(tr["fraction"]) < 1.0:
			hits += 1
	_check("traces hit the world in playback (%d of %d)" % [hits, tried], tried > 0 and hits == tried)
	var o2 := rp.player_state(rp.clients()[0])["origin"] as Vector3
	_check("point contents answers in playback", rp.point_contents(o2, -1) >= 0)
	rp.close()
	Q3Replay.unload_map()
	_check("a server starts again after playback", Q3Server.exec_now("map " + map) == 0 and Q3Server.running())
	Q3Server.exec_now("killserver")
	return {"path": path, "bytes_per_minute": per_min}


func _compare_all(rp: Q3Replay, live: Array) -> PackedStringArray:
	var bad: PackedStringArray = []
	for i in live.size():
		if not rp.seek(i):
			bad.append("frame %d: seek failed" % i)
			break
		bad.append_array(_frame_diffs(rp, live[i]))
		if bad.size() > 50:
			break
	return bad


## What differs between the decoded frame and the live one.
func _frame_diffs(rp: Q3Replay, l: Array) -> PackedStringArray:
	var out: PackedStringArray = []
	var i := rp.current()
	if rp.frame_time(i) != int(l[0]):
		out.append("frame %d: time %d, live %d" % [i, rp.frame_time(i), l[0]])
	var snap := rp.snapshot(-2)   # a client no filter matches: every entity
	var ents := {}
	for e in snap["entities"]:
		ents[int(e["number"])] = e
	var live_ents: Dictionary = l[1]
	if ents.size() != live_ents.size():
		out.append("frame %d: %d entities, live %d" % [i, ents.size(), live_ents.size()])
	for n in live_ents:
		if not ents.has(n):
			out.append("frame %d: entity %d missing" % [i, n])
			continue
		var d := _diff(ents[n], live_ents[n], [])
		if d != "":
			out.append("frame %d: entity %d %s" % [i, n, d])
	var pss: Dictionary = l[2]
	var cls := rp.clients()
	if cls.size() != pss.size():
		out.append("frame %d: %d players, live %d" % [i, cls.size(), pss.size()])
	for cl in pss:
		var d := _diff(rp.player_state(cl), pss[cl], PS_UNSENT)
		if d != "":
			out.append("frame %d: client %d %s" % [i, cl, d])
	return out


## A live entity or playerState as msg.c sends it: each field cut to its width.
static func _wire(d: Dictionary, bits: Dictionary) -> Dictionary:
	var out := {}
	for k in d:
		var v = d[k]
		if v is Dictionary:
			out[k] = _wire(v, bits)
		elif bits.has(k) and v is PackedInt32Array:
			var a := PackedInt32Array()
			for x in v:
				a.append(_cut(x, bits[k]))
			out[k] = a
		elif bits.has(k) and v is int:
			out[k] = _cut(v, bits[k])
		else:
			out[k] = v
	return out


static func _cut(v: int, bits: int) -> int:
	var n := absi(bits)
	var m := v & ((1 << n) - 1)
	if bits < 0 and m & (1 << (n - 1)):
		m -= 1 << n
	return m


static func _diff(a: Dictionary, b: Dictionary, skip: Array) -> String:
	for k in b:
		if k in skip:
			continue
		if not a.has(k):
			return "%s missing" % k
		var x = a[k]
		var y = b[k]
		if x is Dictionary:
			var d := _diff(x, y, [])
			if d != "":
				return "%s.%s" % [k, d]
		elif x != y:
			return "%s %s, live %s" % [k, x, y]
	return ""


## The library: listing, favorites, renames and auto-clean (favorites survive).
func _library() -> void:
	var files := Q3ReplayFile.list()
	if files.is_empty():
		_check("the library lists the replay", false)
		return
	var keep: String = files[0]["file"]
	Q3ReplayFile.set_favorite(keep, true)
	Q3ReplayFile.rename(keep, "Test run")
	# copies stand in for older matches
	for i in 4:
		var copy := TEST_DIR.path_join("2000-01-0%d_000000_test_ffa.q3replay" % (i + 1))
		DirAccess.copy_absolute(TEST_DIR.path_join(keep), copy)
	var listed := Q3ReplayFile.list()
	var mine: Array = listed.filter(func(r): return r["file"] == keep)
	_check("the library lists every replay with its name and favorite (%d)" % listed.size(),
		listed.size() == 5 and mine.size() == 1 and mine[0]["name"] == "Test run" and mine[0]["favorite"])
	var removed := Q3ReplayFile.clean(2)
	var left := Q3ReplayFile.list()
	_check("auto-clean keeps the newest non-favorites and every favorite (%d removed, %d left)" % [removed, left.size()],
		removed == 2 and left.size() == 3 and left.any(func(r): return r["file"] == keep))
	_check("delete refuses paths outside the folder", not Q3ReplayFile.delete("../settings.cfg"))
	# the menu's Replays page, through the bridge (R4)
	var bridge := Q3MenuBridge.new()
	var got: Array = []
	bridge.post.connect(func(m: Dictionary): got.append(m))
	bridge.handle({"type": "replays"})
	var list: Array = got[-1].get("list", []) if got.size() and got[-1].get("type") == "replays" else []
	var row: Dictionary = list.filter(func(r): return r["file"] == keep)[0] if list.any(func(r): return r["file"] == keep) else {}
	_check("the Replays page gets the list (%d, with names, map and length)" % list.size(),
		list.size() == 3 and row.get("name") == "Test run" and row.get("map") != "" and int(row.get("length_ms", 0)) > 0)
	bridge.handle({"type": "replay_favorite", "file": keep, "on": false})
	bridge.handle({"type": "replay_rename", "file": keep, "name": "Renamed"})
	var after: Array = got[-1].get("list", [])
	_check("favorite and rename from the page", after.any(func(r): return r["file"] == keep and not r["favorite"] and r["name"] == "Renamed"))
	bridge.handle({"type": "replay_watch", "file": "../../settings.cfg"})
	_check("watching refuses a path that isn't a replay", got[-1].get("type") == "start_failed")
	Session.dry_run = true
	bridge.handle({"type": "replay_watch", "file": keep})
	_check("watching from the page opens Gameplay with the replay", Session.last_request == Session.GAMEPLAY and
		String(Session.replay.get("path", "")).get_file() == keep and Session.replay.get("back") == "replays")
	Session.replay = {}
	Session.setup = null
	Session.dry_run = false
	var other: String = list.filter(func(r): return r["file"] != keep)[0]["file"]
	bridge.handle({"type": "replay_delete", "file": other})
	_check("delete from the page", got[-1].get("list", []).size() == 2)
	for f in DirAccess.get_files_at(TEST_DIR):
		DirAccess.remove_absolute(TEST_DIR.path_join(f))


## The replay in the Gameplay scene (R2, R3): Q3ReplayFeed under Q3Client, the HUD and the viewer.
## Plays at 4x, checks that what Q3Client draws matches the frame, jumps back (a muted rebuild),
## switches cameras and players, steps frames, jumps to a frag and plays to the end.
func _playback(path: String) -> void:
	Session.dry_run = true   # leaving goes nowhere
	Session.watch_replay(path, 0, "replays")
	var game: Q3Gameplay = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	add_child(game)
	await _frames(3)
	_check("Gameplay plays the replay (%s)" % game._error, game._error == "" and game.replay_feed != null and game.viewer != null)
	if game._error != "" or game.replay_feed == null:
		game.queue_free()
		Session.dry_run = false
		return
	var f := game.replay_feed
	game.replay_speed = 4.0
	var t0 := f.playhead
	await _frames(40)
	_check("the playhead moves at the chosen speed (%.0f ms in)" % (f.playhead - t0), f.playhead > t0 + 500.0)
	_check("Q3Client is on the playhead's frame (%d / %d)" % [f.frame, f.target_frame()], f.frame == f.target_frame())
	_check_drawn(game, "while playing")
	# a jump back: the drawing is rebuilt from that frame
	game.seek_to(f.start_time() + 2000.0)
	await _frames(2)
	_check("a jump back lands on its frame (%d / %d)" % [f.frame, f.target_frame()], f.frame == f.target_frame())
	_check_drawn(game, "after a jump")
	# the cameras
	var ps := game.feed.player_state(game.me)
	var eye := Q3.q3_to_godot(Vector3(ps["origin"]) + Vector3(0, 0, float(ps["viewheight"])))
	_check("first person sits at the watched player's eye (%.2f m off)" % game.cam.global_position.distance_to(eye),
		game.cam.global_position.distance_to(eye) < 0.6)
	game.replay_playing = false
	game.set_cam_mode("chase")
	await _frames(2)
	ps = game.feed.player_state(game.me)
	eye = Q3.q3_to_godot(Vector3(ps["origin"]) + Vector3(0, 0, float(ps["viewheight"])))
	var d := game.cam.global_position.distance_to(eye)
	_check("the chase camera is behind them, no further than its distance (%.2f m)" % d, d > 0.1 and d < 4.0)
	_check("chase draws the watched player's body", game.client.player_nodes().has(game.me) or int(ps["pm_type"]) != 0)
	var before := game.cam.global_position
	game.set_cam_mode("free")
	await _frames(2)
	_check("the free camera starts where the view was", game.cam.global_position.distance_to(before) < 0.05)
	game.set_cam_mode("orbit")
	await _frames(2)
	_check("orbit circles the player", game.cam.global_position.distance_to(eye) < 6.0)
	game.set_cam_mode("cinematic")
	game.replay_playing = true
	await _frames(30)
	game.replay_playing = false
	var lens: Vector3 = game._cine.lens if game._cine else Vector3.INF
	_check("the cinematic camera frames the player from clear air (%s shot, %.2f m)" % [game._cine.shot_name() if game._cine else "no",
		Q3.q3_to_godot(lens).distance_to(eye) if game._cine else -1.0], game._cine != null and game._cine.shot_name() != ""
		and game.feed.point_contents(lens, -1) & 1 == 0 and Q3.q3_to_godot(lens).distance_to(game.cam.global_position) < 0.05)
	game.set_cam_mode("first")
	# whose eyes
	var others := Array(f.clients()).filter(func(c): return c != game.me)
	if others.size():
		game.watch(others[0])
		await _frames(2)
		_check("watching another player", game.me == others[0] and game.client.local_client == others[0])
	# frames, keys and frags
	var fr := f.frame
	game.step_frame(1)
	await _frames(1)
	_check("frame forward steps one frame, paused (%d -> %d)" % [fr, f.frame], f.frame == fr + 1 and not game.replay_playing)
	var ev := InputEventAction.new()
	ev.action = &"q3_replay_play"
	ev.pressed = true
	_check("the play key plays", game.viewer.handle_input(ev) and game.replay_playing)
	var obits := f.replay.events().filter(func(e): return int(e["type"]) == 1)
	if obits.size():
		var at := f.playhead
		game.viewer.frag(1)
		var want := -1.0
		for o in obits:
			if float(o["time"]) - Q3ReplayViewer.FRAG_LEAD_MS > at + 50.0:
				want = float(o["time"]) - Q3ReplayViewer.FRAG_LEAD_MS
				break
		_check("next frag lands just before it", want < 0.0 or absf(f.playhead - want) < 1.0)
	game.viewer.names = true
	game.viewer.set_outlines(true)
	await _frames(3)
	_check("outlines go on the players", game.viewer._outlined.size() > 0)
	game.viewer.set_outlines(false)
	# to the end
	game.replay_speed = 4.0
	game.seek_to(f.end_time() - 1500.0)
	game.replay_playing = true
	await _frames(60)
	_check("it stops at the end", f.at_end() and not game.replay_playing)
	game._leave()
	_check("leaving goes back to the menu's replays page", Session.last_request == Session.MAIN_MENU and Session.menu_page == "replays")
	Session.menu_page = ""
	game.queue_free()
	await _frames(2)
	Session.dry_run = false


## What Q3Client draws matches the frame: every player in the snapshot but the watched one in
## first person has a body (or lies dead), and every item and missile has its node.
func _check_drawn(game: Q3Gameplay, when: String) -> void:
	var snap := game.feed.snapshot(game._snap_client())
	var missing: Array = []
	for es in snap.get("entities", []):
		var n := int(es["number"])
		var kind := int(es["eType"])
		if kind == 1 and n < 64 and not (n == game.me and game.cam_mode == "first"):   # ET_PLAYER
			if not game.client._ents.has(n):
				missing.append(n)
		elif kind == 2 or kind == 3:   # ET_ITEM, ET_MISSILE
			if not game.client._ents.has(n):
				missing.append(n)
	_check("Q3Client draws every player, item and missile of the frame %s (%d missing)" % [when, missing.size()], missing.is_empty())


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame
