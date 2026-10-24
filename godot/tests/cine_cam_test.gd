extends Node
## Headless cinematic camera check (Q3CineCam, REPLAY_PLAN.md "Cinematic camera"): a long bot
## match on a map of rooms and corridors, joined as a spectator with Settings > Gameplay >
## Spectator camera on Cinematic. Every frame the lens must be in clear air (no solid, lava,
## slime or water at it, and a 4-unit box there isn't stuck); the subject must be in sight on
## nearly every frame; the shots must vary, ease and cut, and the auto-director must move between
## players. The match is recorded, and the same camera then runs over the replay (Q3ReplayFeed
## and its traces) with the same checks.
##   godot --headless --fixed-fps 60 --path . res://tests/cine_cam_test.tscn -- map=q3dm6 seconds=240 bots=5
## (--fixed-fps 60 runs game time faster than real time.) Prints "CINE CAM TEST ok|FAILED";
## exit code 1 on failure.

const E := preload("res://scripts/game/q3_enums.gd")
const TOUCHED := ["gameplay/spec_view", "gameplay/spec_director", "gameplay/record_replays"]
const BOTS := ["Sarge", "Xaero", "Crash", "Anarki", "Hunter", "Major", "Visor"]

var _game: Q3Gameplay
var _ok := true
var _saved := {}
var _args := {}


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.contains("="):
			_args[a.get_slice("=", 0)] = a.get_slice("=", 1)
	for k in TOUCHED:
		_saved[k] = Settings.get_value(k)
	Settings.set_value("gameplay/spec_view", "cinematic")
	Settings.set_value("gameplay/spec_director", false)
	Settings.set_value("gameplay/record_replays", true)
	var s := Session.new_setup("ffa", String(_args.get("map", "q3dm6")))
	for i in clampi(int(_args.get("bots", "5")), 1, BOTS.size()):
		s.bots.append({"name": BOTS[i], "skill": 3, "team": ""})
	s.rules = {"fraglimit": 0, "timelimit": 0, "warmup": 0}
	s.spectate = true
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	_game.test_input = true
	add_child(_game)
	await _run()
	for k in TOUCHED:
		Settings.set_value(k, _saved[k])
	Settings.save()
	if Q3Server.running():
		Q3Server.exec_now("killserver")
	print("CINE CAM TEST ", "ok" if _ok else "FAILED")
	get_tree().quit(0 if _ok else 1)


func _check(what: String, ok: bool) -> void:
	print(("  ok    " if ok else "  FAIL  ") + what)
	if not ok:
		_ok = false


## Whether the lens at `p` (id units) is in clear air: nothing solid, wet or burning there, and a
## small box placed there isn't stuck in anything.
static func lens_clear(feed: Q3Feed, p: Vector3) -> bool:
	if feed.point_contents(p, -1) & Q3CineCam.MASK_LENS:
		return false
	var tr := feed.trace(p, p, Vector3(-4, -4, -4), Vector3(4, 4, 4), -1, Q3CineCam.MASK_SOLID)
	return not tr.is_empty() and not tr["startsolid"] and not tr["allsolid"]


## Whether the subject's middle can be seen from the lens.
static func in_sight(feed: Q3Feed, lens: Vector3, origin: Vector3, dead: bool, cl: int) -> bool:
	var focus := origin + Vector3(0, 0, -14.0 if dead else 12.0)
	var tr := feed.trace(lens, focus, Vector3.ZERO, Vector3.ZERO, cl, Q3CineCam.MASK_SOLID)
	return not tr.is_empty() and float(tr["fraction"]) >= 1.0


func _run() -> void:
	var seconds := int(_args.get("seconds", "240"))
	for i in 30:
		await get_tree().process_frame
	_check("the match runs (%s)" % _game._error, _game._error == "" and Q3Server.running())
	_check("the setting starts the cinematic camera with the auto-director", _game.spec_view == "cinematic" and _game.director)
	var frames := 0
	var cine_frames := 0
	var bad := 0
	var hidden := 0
	var off_lens := 0
	var close := 0
	var shots := {}
	var subjects := {}
	var last_subject := -1
	var switches := 0
	var bad_at: Array = []
	var until := Q3Server.time() + seconds * 1000
	var t0 := Time.get_ticks_msec()
	while Q3Server.time() < until and Q3Server.running():
		await get_tree().process_frame
		frames += 1
		var ps: Dictionary = Q3Server.player_state(_game.me)
		if not Q3Gameplay.following(ps) or _game._cine == null:
			continue
		cine_frames += 1
		var cine := _game._cine
		var cl := int(ps["clientNum"])
		if cl != last_subject:
			if last_subject >= 0:
				switches += 1
			last_subject = cl
		subjects[cl] = true
		shots[cine.shot_name()] = int(shots.get(cine.shot_name(), 0)) + 1
		if not lens_clear(_game.feed, cine.lens):
			bad += 1
			if bad_at.size() < 5:
				bad_at.append("%s at %s (%s)" % [Q3Server.time(), cine.lens, cine.shot_name()])
		if not in_sight(_game.feed, cine.lens, ps["origin"], int(ps["pm_type"]) == E.PM_DEAD, cl):
			hidden += 1
		if cine.lens.distance_to(Vector3(ps["origin"]) + Vector3(0, 0, 12)) < 32.0:
			close += 1
			if OS.has_environment("CINE_DEBUG"):
				print("close %d %s t=%.2f dead=%s d=%.0f fb=%s" % [Q3Server.time(), cine.shot_name(), cine._t,
					int(ps["pm_type"]) == E.PM_DEAD, cine.lens.distance_to(Vector3(ps["origin"]) + Vector3(0, 0, 12)), cine._fallback])
		if Q3Gameplay._godot_to_id(_game.cam.global_position).distance_to(cine.lens) > 0.5:
			off_lens += 1
	var cine := _game._cine
	print("  %d s of game time in %d ms, %d frames (%d cinematic): %d cuts, %d eases, shots %s, why %s, %d subjects, %d switches" % [
		seconds, Time.get_ticks_msec() - t0, frames, cine_frames, cine.cuts if cine else 0, cine.eases if cine else 0, shots,
		cine.reasons if cine else {}, subjects.size(), switches])
	_check("the cinematic camera ran nearly the whole match (%d of %d frames)" % [cine_frames, frames], cine_frames > frames * 0.9)
	_check("the lens never sat in solid, lava, slime or water (%d frames) %s" % [bad, bad_at], bad == 0)
	_check("the camera is the lens (%d frames off)" % off_lens, off_lens == 0)
	_check("the subject was in sight (hidden %.2f%% of frames)" % (100.0 * hidden / maxf(cine_frames, 1)), hidden < cine_frames * 0.05)
	_check("the lens stays out of the subject's body (within 32 units %.2f%% of frames)" % (100.0 * close / maxf(cine_frames, 1)),
		close < cine_frames * 0.01)
	_check("every shot kind was used (%s)" % [shots.keys()], shots.size() == Q3CineCam.NAMES.size())
	_check("shots changed by cuts and by easing", cine != null and cine.cuts > 5 and cine.eases > 3)
	var changes := 0
	for k in cine.reasons:
		if k in ["timed", "ahead", "blocked", "cramped", "solid"]:
			changes += int(cine.reasons[k])
	var mean := cine_frames / 60.0 / maxf(changes + 1, 1)
	_check("shots last (%.1f s on average between changes the camera made itself)" % mean, mean >= 3.0)
	_check("the auto-director moved between players (%d switches, %d players)" % [switches, subjects.size()], switches >= 3 and subjects.size() >= 2)
	_check("the HUD says CINEMATIC CAMERA", String(_game.hud.spectator.get("mode", "")) == "cinematic")
	_check("no crosshair or view gun", not _game.hud.crosshair.visible and (_game._gun == null or not _game._gun.visible))
	# Use item: first person, chase, cinematic
	var ev := InputEventAction.new()
	ev.action = &"q3_use_item"
	ev.pressed = true
	_game._unhandled_input(ev)
	await get_tree().process_frame
	_check("Use item: cinematic -> first person, the auto-director as it was (off)", _game.spec_view == "first" and not _game.director)
	_game._unhandled_input(ev)
	_game._unhandled_input(ev)
	_check("... -> chase -> cinematic again, the auto-director on", _game.spec_view == "cinematic" and _game.director)
	await _replay_pass()


## The same camera over the recorded match: Q3ReplayFeed's interpolated playerStates and its
## traces (CM_LoadMap, no server).
func _replay_pass() -> void:
	_game._finish_recording({"ended": "test"})
	var path := _game.replay_path
	_check("the match was recorded (%s)" % path, path != "")
	Q3Server.exec_now("killserver")
	if path == "":
		return
	var feed := Q3ReplayFeed.new()
	var err := feed.open(path)
	_check("the replay opens (%s)" % err, err == "")
	if err != "":
		return
	var cine := Q3CineCam.new()
	var cls := feed.clients().duplicate()
	cls.erase(_game.me)
	var bad := 0
	var hidden := 0
	var n := 0
	var shots := {}
	var ms := float(feed.start_time())
	while ms < feed.end_time():
		ms += 1000.0 / 60.0
		feed.playhead = ms
		while feed.frame < feed.target_frame() and feed.step():
			pass
		var cl: int = cls[int((ms - feed.start_time()) / 20000.0) % cls.size()]   # someone new every 20 s
		var ps := feed.player_state(cl)
		if ps.is_empty() or int(ps["persistant"][E.PERS_TEAM]) == E.TEAM_SPECTATOR:
			continue
		var dead := int(ps["pm_type"]) == E.PM_DEAD
		var others: Array = []
		for es in feed.snapshot(-1).get("entities", []):
			if int(es["eType"]) == E.ET_PLAYER and int(es["number"]) != cl and not (int(es["eFlags"]) & E.EF_DEAD):
				others.append(Vector3(es["pos"]["trBase"]) + Vector3(0, 0, 10))
		cine.update(feed, {"client": cl, "origin": ps["origin"], "vel": ps["velocity"], "yaw": float((ps["viewangles"] as Vector3).y),
			"dead": dead, "look": Vector3(ps["origin"]) + Vector3(0, 0, 10), "others": others}, ms)
		n += 1
		shots[cine.shot_name()] = true
		if not lens_clear(feed, cine.lens):
			bad += 1
		if not in_sight(feed, cine.lens, ps["origin"], dead, cl):
			hidden += 1
	feed.close()
	_check("replay: %d frames, the lens never in solid (%d), the subject in sight (hidden %.2f%%), shots %s" % [n, bad,
		100.0 * hidden / maxf(n, 1), shots.keys()], n > 1000 and bad == 0 and hidden < n * 0.05 and shots.size() >= 4)
	DirAccess.remove_absolute(path)
