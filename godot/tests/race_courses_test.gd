extends Node
## The starter courses (RACE_PLAN.md D6), headless: Maps/race_starter.pk3 as the menu and the game see
## it, and every course run with scripted input, as a player would: hurdles jumped, gaps jumped, four
## buttons shot, four shards collected. Also what must not work: running the gaps without jumping (the
## pit kills), and finishing the accuracy and level courses without the frags.
##   godot --headless --path . res://tests/race_courses_test.tscn
## Prints one line per check and "RACE COURSES n/m passed"; exit code 1 on any failure.

const COURSES := ["race_warmup", "race_gaps", "race_targets", "race_shards"]
const HURDLES := [500, 900, 1300]
const EDGES := [400, 820, 1220, 1660]                         ## the gap runner's platform ends
const BUTTONS := [[400, 90.0], [600, 270.0], [800, 90.0], [1000, 270.0]]   ## x, the yaw that faces the wall
const SHARDS := [[500, -80.0], [800, 80.0], [1100, -80.0], [1400, 80.0]]

var passed := 0
var failed := 0
var _me := -1


func check(name: String, ok: bool, detail := "") -> void:
	if ok:
		passed += 1
	else:
		failed += 1
	print("  %s  race courses: %s%s" % ["PASS" if ok else "FAIL", name, ("  (" + detail + ")") if detail != "" else ""])


func _ready() -> void:
	_run.call_deferred()


## One frame of input for a course: {angles, buttons, forward, right, up}.
func _drive(id: String, o: Vector3, f: int, state: Dictionary, no_jump := false, no_shoot := false) -> Dictionary:
	var cmd := {"angles": Vector3.ZERO, "buttons": 0, "forward": 127, "right": 0, "up": 0}
	var x := o.x
	match id:
		"race_warmup", "race_gaps":
			var marks: Array = HURDLES if id == "race_warmup" else EDGES
			var lead: float = 100.0 if id == "race_warmup" else -3.0   # hurdles are jumped early, gaps at the edge
			for m in marks:
				var at: float = float(m) - lead if id == "race_warmup" else float(m) + lead
				if x >= at and not state.has(m):
					state[m] = f
				if state.has(m) and f - int(state[m]) < 3 and not no_jump:
					cmd["up"] = 127
		"race_targets":
			var yaw := 0.0
			var cur := -1
			for i in BUTTONS.size():
				if x < float(BUTTONS[i][0]) + 40.0:
					cur = i
					break
			if cur >= 0:
				yaw = float(BUTTONS[cur][1])
				cmd["forward"] = 0
				cmd["right"] = 127 if yaw == 90.0 else -127
				if absf(x - float(BUTTONS[cur][0])) < 30.0 and not no_shoot:
					cmd["buttons"] = 1
			cmd["angles"] = Vector3(0, yaw, 0)
		"race_shards":
			var ty := 0.0
			for s in SHARDS:
				if x < float(s[0]) + 30.0:
					ty = float(s[1])
					break
			var dy := ty - o.y
			if ty != 0.0 and absf(dy) > 8.0 and not no_shoot:   # no_shoot: stay in the middle (collect nothing)
				cmd["right"] = -127 if dy > 0 else 127
	return cmd


## Runs a course from the spawn; returns {run, died, cps, messages, x}.
func _play(id: String, seconds := 30.0, no_jump := false, no_shoot := false) -> Dictionary:
	Q3Server.exec_now("killserver")
	var loaded := Q3Server.exec_now("map " + id) == 0 and Q3Server.running()
	if not loaded:
		return {"error": Q3Server.error()}
	Q3Server.frame(Q3Host.FRAME_MSEC)
	_me = Q3Server.connect_local(Q3Host.userinfo({"name": "Runner", "model": "sarge"}))
	var run := Q3RaceRun.new()
	var messages: PackedStringArray = []
	var died := false
	var state := {}
	var x := 0.0
	for f in int(seconds * 125):
		var ps := Q3Server.player_state(_me)
		var o: Vector3 = ps["origin"] if not ps.is_empty() else Vector3.ZERO
		x = o.x
		if not ps.is_empty() and (ps["stats"] as PackedInt32Array)[0] <= 0:
			died = true
			break
		var cmd := _drive(id, o, f, state, no_jump, no_shoot)
		Q3Server.usercmd(_me, Q3Server.time() + Q3Host.FRAME_MSEC, cmd["angles"], cmd["buttons"], 2, cmd["forward"], cmd["right"], cmd["up"])
		Q3Server.frame(Q3Host.FRAME_MSEC)
		for c in Q3Server.next_commands(_me):
			run.feed(c)
			if c.begins_with("cp "):
				messages.append(c)
		if run.state == Q3RaceRun.State.FINISHED or o.x > 2600.0:
			break
	return {"run": run, "died": died, "cps": run.splits.size(), "messages": messages, "x": x}


func _run() -> void:
	var t0 := Time.get_ticks_msec()
	Q3MapPacks.dir_override = Q3RaceCourses.out_dir()
	Session.refresh_custom_maps()
	var listed := Session.maps_for("race")
	var by_map := {}
	for m in listed:
		by_map[m["map"]] = m
	check("the repo's Maps folder lists the four courses as race maps, with their names, styles and level shots",
			COURSES.all(func(c): return by_map.has(c) and by_map[c]["types"] == ["race"] and String(by_map[c]["levelshot"]).begins_with("data:image"))
			and by_map["race_warmup"]["race"]["style"] == "training" and by_map["race_gaps"]["race"]["style"] == "run"
			and by_map["race_targets"]["race"]["style"] == "accuracy" and by_map["race_shards"]["race"]["style"] == "level"
			and by_map["race_gaps"]["name"] == "Gap Runner" and by_map["race_shards"]["race"]["author"] == "Q3Remaster",
			str(by_map.keys()))

	var err := Q3Host.start()
	check("q3core starts", err == "", err)
	Q3Server.set_cvar("g_gametype", "0")
	Q3Server.set_cvar("g_ruleset", "classic")
	Q3Server.set_cvar("g_race", "1")
	Q3Server.set_cvar("fraglimit", "0")
	Q3Server.set_cvar("timelimit", "0")
	Q3Server.set_cvar("g_doWarmup", "0")
	var cp_expected := {"race_warmup": 1, "race_gaps": 2, "race_targets": 0, "race_shards": 0}
	for id in COURSES:
		var r := _play(id)
		if r.has("error"):
			check("%s loads" % id, false, String(r["error"]))
			continue
		var run: Q3RaceRun = r["run"]
		var ms := run.last_ms
		check("%s: run start to finish with the scripted input, %d checkpoints, 3 to 12 s" % [id, cp_expected[id]],
				run.state == Q3RaceRun.State.FINISHED and int(r["cps"]) == cp_expected[id] and ms > 3000 and ms < 12000 and not r["died"],
				"state %d, %d cps, %d ms, x %.0f, died %s" % [run.state, r["cps"], ms, r["x"], r["died"]])
		check("%s: the game told the HUD the course's checkpoint count" % id, run.checkpoints_total == cp_expected[id], str(run.checkpoints_total))

	# what must not work
	var r1 := _play("race_gaps", 12.0, true)
	check("Gap Runner: running it without jumping ends in the pit", r1.get("died", false) and (r1["run"] as Q3RaceRun).state != Q3RaceRun.State.FINISHED,
			"x %.0f" % float(r1.get("x", 0)))
	var r2 := _play("race_targets", 12.0, false, true)
	check("Target Practice: reaching the finish without shooting doesn't stop the timer",
			(r2["run"] as Q3RaceRun).state == Q3RaceRun.State.RUNNING and not r2["died"] and float(r2["x"]) > 1500.0, "x %.0f" % float(r2["x"]))
	var r3 := _play("race_shards", 12.0, false, true)
	check("Shard Hunt: reaching the finish without the shards doesn't stop the timer",
			(r3["run"] as Q3RaceRun).state == Q3RaceRun.State.RUNNING and float(r3["x"]) > 1560.0, "x %.0f" % float(r3["x"]))
	Q3Server.exec_now("killserver")
	Q3MapPacks.dir_override = ""
	print("RACE COURSES %d/%d passed (%.1f s)" % [passed, passed + failed, (Time.get_ticks_msec() - t0) / 1000.0])
	get_tree().quit(0 if failed == 0 else 1)
