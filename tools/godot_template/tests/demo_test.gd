extends Node
## Headless Quake 3 demo check (REPLAY_PLAN.md R7): id's own demos/four.dm_68 (protocol 68, in
## the player's pak8.pk3; read from there, never copied into the project) opened with Q3Replay,
## which parses it as cl_parse.c does and turns it into replay frames. Checks the header from its
## configstrings, every snapshot decoding with the recording player's playerState and entities,
## seeking, the timeline, the browser's quick read, and a few seconds in the Gameplay scene.
##   godot --headless --path . res://tests/demo_test.tscn [-- demo=<file.dm_68>]
## Prints "DEMO TEST ok|FAILED"; exit code 1 on failure.

const E := preload("res://scripts/game/q3_enums.gd")
const TEST_DIR := "user://test_demos"   # its own: the auto-clean check mustn't touch other tests' files

var _ok := true


func _ready() -> void:
	var path := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("demo="):
			path = a.substr(5)
	await _run(path)
	print("DEMO TEST ", "ok" if _ok else "FAILED")
	get_tree().quit(0 if _ok else 1)


func _check(what: String, ok: bool) -> void:
	print(("  ok    " if ok else "  FAIL  ") + what)
	if not ok:
		_ok = false


## four.dm_68 out of the player's pak8.pk3, into the test folder.
func _extract() -> String:
	var base := Q3Ownership.remembered()
	var paks: Array = []
	if base != "":
		paks.append(base.path_join("pak8.pk3"))
	for root in Q3Ownership.LIKELY:
		paks.append(String(root).path_join("baseq3/pak8.pk3"))
	for pak in paks:
		if not FileAccess.file_exists(pak):
			continue
		var z := ZIPReader.new()
		if z.open(pak) != OK:
			continue
		var bytes := z.read_file("demos/four.dm_68")
		z.close()
		if bytes.size():
			DirAccess.make_dir_recursive_absolute(TEST_DIR)
			var out := TEST_DIR.path_join("four.dm_68")
			var f := FileAccess.open(out, FileAccess.WRITE)
			f.store_buffer(bytes)
			f.close()
			return out
	return ""


func _run(path: String) -> void:
	if path == "":
		path = _extract()
	_check("found a protocol 68 demo (%s)" % path, path != "")
	if path == "":
		return
	var err := Q3Host.start()
	_check("the engine starts (%s)" % err, err == "")
	if Q3Server.running():
		Q3Server.exec_now("killserver")
	var rp := Q3Replay.new()
	var t0 := Time.get_ticks_msec()
	err = rp.open(path)
	_check("the demo opens (%s, %d ms)" % [err, Time.get_ticks_msec() - t0], err == "")
	if err != "":
		return
	var h := rp.header()
	var length := float(h.get("length_ms", 0)) / 1000.0
	print("  demo: map %s, mode %s, recorded by %s, %d frames, %.1f s" % [h.get("map"), h.get("mode"), h.get("player"),
		rp.frame_count(), length])
	_check("the header comes from its configstrings (map %s, player %s)" % [h.get("map"), h.get("player")],
		String(h.get("map", "")) != "" and String(h.get("player", "")) != "" and bool(h.get("demo", false)))
	_check("a demo's worth of snapshots (%d over %.1f s)" % [rp.frame_count(), length], rp.frame_count() > 100 and length > 10.0)
	# every frame: the recording player's playerState, entities, rising time
	var me := int(h.get("local_client", -1))
	var bad := 0
	var most := 0
	var last := -1
	var events := 0     # frames where the player's eventSequence went up by one
	var on_time := 0    # ... and their body (built from the playerState) played that same event
	var last_seq := -1
	var last_ev := -1
	var quiet := 0      # frames since the playerState last held an external event
	for i in rp.frame_count():
		if not rp.seek(i):
			bad += 1
			break
		var ps := rp.player_state(me)
		var snap := rp.snapshot(me)
		most = maxi(most, snap.get("entities", []).size())
		if ps.is_empty() or int(snap["serverTime"]) <= last:
			bad += 1
			continue
		last = int(snap["serverTime"])
		var seq := int(ps["eventSequence"])
		var ev := -1
		for e in rp.snapshot(-2).get("entities", []):
			if int(e["number"]) == int(ps["clientNum"]):
				ev = int(e["event"])
		# an external event (pain, pickups) holds the body for its 300 ms, on the server too, and the
		# queued ones follow a frame each: only frames clear of that are counted
		quiet = 0 if int(ps["externalEvent"]) != 0 else quiet + 1
		if last_seq >= 0 and seq == last_seq + 1 and quiet >= 3:
			events += 1
			if ev != last_ev and (ev & 0xff) == int(ps["events"][(seq - 1) & 1]) and (ev >> 8) & 3 == (seq - 1) & 3:
				on_time += 1
		last_seq = seq
		last_ev = ev
	_check("every frame decodes with the player's view (%d bad, up to %d entities)" % [bad, most], bad == 0 and most > 0)
	_check("the player's body plays their events as they happen (%d of %d)" % [on_time, events],
		events > 20 and on_time >= events * 0.9)
	var seeks := 0
	for i in 20:
		var f := (i * 7919) % rp.frame_count()
		if rp.seek(f) and rp.current() == f and not rp.player_state(me).is_empty():
			seeks += 1
	_check("random seeks land (%d of 20)" % seeks, seeks == 20)
	rp.seek(rp.frame_count() - 1)
	var players := 0
	for cl in 64:
		if rp.configstring(E.CS_PLAYERS + cl) != "":
			players += 1
	_check("its players are in the configstrings (%d)" % players, players >= 2)
	var obits := rp.events().filter(func(e): return int(e["type"]) == 1).size()
	var cmds := rp.commands(-1, rp.frame_count() - 1, me).size()
	_check("the timeline and the server commands (%d frags, %d commands)" % [obits, cmds], cmds > 0)
	rp.close()
	var info := Q3Replay.demo_info(path)
	_check("the browser's quick read (%s)" % info.get("map", info.get("error", "")), info.get("map", "") == h.get("map"))
	if path.begins_with(TEST_DIR):
		var old_dir := Q3ReplayFile.dir
		Q3ReplayFile.dir = TEST_DIR
		var listed := Q3ReplayFile.list().filter(func(r): return r["file"] == path.get_file())
		_check("the Replays page lists a demo dropped in the folder", listed.size() == 1 and listed[0].get("demo", false)
			and listed[0]["map"] == h.get("map") and Q3ReplayFile.path_of(path.get_file()) != "")
		_check("auto-clean never deletes a demo", Q3ReplayFile.clean(0) == 0 and FileAccess.file_exists(path))
		Q3ReplayFile.dir = old_dir
	# a few seconds in Gameplay
	Session.dry_run = true
	Session.watch_replay(path, 5000, "replays")
	var game: Q3Gameplay = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	add_child(game)
	for i in 60:
		await get_tree().process_frame
	_check("Gameplay plays it (%s)" % game._error, game._error == "" and game.replay_feed != null and game.replay_feed.frame > 0)
	game.set_cam_mode("chase")
	for i in 20:
		await get_tree().process_frame
	_check("the others are drawn from what the player saw (%d bodies)" % game.client.player_nodes().size(),
		game.client.player_nodes().size() > 0)
	var pov := game.feed.player_state(game.me)
	_check("the demo's own player has a body in the chase camera (built from their playerState)",
		game.client.player_nodes().has(game.me) or int(pov.get("pm_type", 0)) != 0)
	game._leave()
	game.queue_free()
	await get_tree().process_frame
	Session.dry_run = false
	if path.begins_with(TEST_DIR):
		DirAccess.remove_absolute(path)
