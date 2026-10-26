extends Node
## Race mode, headless (RACE_PLAN.md D1): a straight corridor with Defrag's entities, compiled by
## q3map, played by q3core with g_race 1. The player runs down it and the test reads what the
## game reports ("race ..." server commands, which Q3RaceRun parses):
##   start line -> checkpoint -> multimanager + smallprint -> frags filter -> checkpoint ->
##   stop line -> a trigger_push_velocity (ADD_XY) -> a target_speed (+50 percent)
## then a second life: the run's best time is reported back at the spawn.
##   godot --headless --path . res://tests/race_test.tscn
## Prints one line per check and "RACE n/m passed"; exit code 1 on any failure.

const NAME := "ed_racetest"

var passed := 0
var failed := 0


func check(name: String, ok: bool, detail := "") -> void:
	if ok:
		passed += 1
	else:
		failed += 1
	print("  %s  race: %s%s" % ["PASS" if ok else "FAIL", name, ("  (" + detail + ")") if detail != "" else ""])


func _ready() -> void:
	_run.call_deferred()


## The corridor: x from -64 to 3200, 256 wide, 128 high. Triggers are full-height slabs.
static func corridor() -> Q3MapDoc:
	var d := Q3MapDoc.new()
	d.info["name"] = "racetest"
	var wall := "textures/gothic_block/blocks15"
	var floor_mat := "textures/base_floor/clangdark"
	d.add(Q3MapDoc.box_brush(Vector3(-80, -144, -16), Vector3(3216, 144, 0), floor_mat))
	d.add(Q3MapDoc.box_brush(Vector3(-80, -144, 128), Vector3(3216, 144, 144), wall))
	d.add(Q3MapDoc.box_brush(Vector3(-80, -144, 0), Vector3(-64, 144, 128), wall))
	d.add(Q3MapDoc.box_brush(Vector3(3200, -144, 0), Vector3(3216, 144, 128), wall))
	d.add(Q3MapDoc.box_brush(Vector3(-80, -144, 0), Vector3(3216, -128, 128), wall))
	d.add(Q3MapDoc.box_brush(Vector3(-80, 128, 0), Vector3(3216, 144, 128), wall))
	d.add(Q3MapEntities.point("info_player_deathmatch", Vector3(0, 0, 24), {"angle": "0"}))
	d.add(Q3MapEntities.point("light", Vector3(0, 0, 100), {"light": "400"}))
	d.add(Q3MapEntities.point("light", Vector3(1500, 0, 100), {"light": "400"}))
	d.add(Q3MapEntities.point("light", Vector3(3000, 0, 100), {"light": "400"}))
	# each trigger slab fires a target; the target entities sit inside the corridor
	_slab(d, 128, "trigger_multiple", "t_start", {})
	_slab(d, 500, "trigger_multiple", "t_cp1", {})
	_slab(d, 700, "trigger_multiple", "t_mm", {})
	_slab(d, 800, "trigger_multiple", "t_ff", {})
	_slab(d, 1000, "trigger_multiple", "t_cp2", {})
	_slab(d, 1400, "trigger_multiple", "t_stop", {})
	d.add(Q3MapEntities.point("target_startTimer", Vector3(140, 0, 32), {"targetname": "t_start"}))
	d.add(Q3MapEntities.point("target_checkpoint", Vector3(510, 0, 32), {"targetname": "t_cp1"}))
	d.add(Q3MapEntities.point("target_checkpoint", Vector3(1010, 0, 32), {"targetname": "t_cp2"}))
	d.add(Q3MapEntities.point("target_stopTimer", Vector3(1410, 0, 32), {"targetname": "t_stop"}))
	d.add(Q3MapEntities.point("target_multimanager", Vector3(710, 0, 32),
			{"targetname": "t_mm", "target01": "0.3:pr_a", "target02": "pr_b"}))
	d.add(Q3MapEntities.point("target_smallprint", Vector3(720, 40, 32), {"targetname": "pr_a", "message": "mm_a"}))
	d.add(Q3MapEntities.point("target_smallprint", Vector3(730, -40, 32), {"targetname": "pr_b", "message": "mm_b"}))
	d.add(Q3MapEntities.point("target_fragsFilter", Vector3(810, 0, 32), {"targetname": "t_ff", "frags": "1", "target": "ff_ok"}))
	d.add(Q3MapEntities.point("target_smallprint", Vector3(820, 40, 32), {"targetname": "ff_ok", "message": "ff_ok"}))
	# 400 ups added by a velocity pad (ADD_XY = 2), then 50 percent more by target_speed
	var push := d.add(Q3MapEntities.brush_entity("trigger_push_velocity", {"target": "pad_dir", "speed": "400", "spawnflags": "2"}))
	var pb := Q3MapDoc.box_brush(Vector3(1700, -128, 0), Vector3(1760, 128, 128), "textures/common/trigger")
	pb["entity"] = push
	d.add(pb)
	d.add(Q3MapEntities.point("target_position", Vector3(1900, 0, 32), {"targetname": "pad_dir"}))
	_slab(d, 2300, "trigger_multiple", "t_speed", {})
	d.add(Q3MapEntities.point("target_speed", Vector3(2310, 0, 32), {"targetname": "t_speed", "speed": "50", "spawnflags": "3"}))
	d.worldspawn = {"message": "Race test", "ambient": "8"}
	return d


static func _slab(d: Q3MapDoc, x: float, cn: String, target: String, keys: Dictionary) -> void:
	var k := {"target": target, "wait": "0.5"}
	k.merge(keys, true)
	var e := d.add(Q3MapEntities.brush_entity(cn, k))
	var b := Q3MapDoc.box_brush(Vector3(x, -128, 0), Vector3(x + 32, 128, 128), "textures/common/trigger")
	b["entity"] = e
	d.add(b)


func _run() -> void:
	var t0 := Time.get_ticks_msec()
	var ex := Q3MapExport.to_map(corridor())
	var c := Q3MapCompiler.new()
	add_child(c)
	var kit := Q3MapCompiler.ensure_kit()
	if kit != "":
		check("kit", false, kit)
		_finish(t0)
		return
	c.compile(NAME, ex["text"], ["bsp"])
	var r: Dictionary = await c.finished
	check("q3map compiles the corridor, no leak", r["ok"] and not r["leaked"],
			"%s%s" % [r["error"], "" if r["ok"] else "\n" + String(r["log"]).right(1200)])
	if not r["ok"]:
		_finish(t0)
		return

	var err := Q3Host.start()
	Q3Server.set_cvar("g_gametype", "0")
	Q3Server.set_cvar("g_ruleset", "classic")
	Q3Server.set_cvar("g_race", "1")
	Q3Server.set_cvar("fraglimit", "0")
	Q3Server.set_cvar("timelimit", "0")
	Q3Server.set_cvar("g_doWarmup", "0")
	var loaded := err == "" and Q3Server.exec_now("map " + NAME) == 0 and Q3Server.running()
	check("q3core loads it with g_race 1", loaded and Q3Server.cvar("g_race") == "1", err if err != "" else Q3Server.error())
	if not loaded:
		_finish(t0)
		return
	Q3Server.frame(Q3Host.FRAME_MSEC)
	var me := Q3Server.connect_local(Q3Host.userinfo({"name": "Runner", "model": "sarge"}))
	var run := Q3RaceRun.new()
	var log: PackedStringArray = []
	var cps: Array = []
	var stops: Array = []
	var prints: Array = []   # [text, server time]
	run.checkpoint.connect(func(n: int, ms: int, split: int): cps.append([n, ms, split]))
	run.finished.connect(func(ms: int, record: bool): stops.append([ms, record]))

	# life one: run the whole corridor
	var speeds: Array = []   # [x, speed] each frame
	var t_start := -1
	for f in 2400:
		var t := Q3Server.time() + Q3Host.FRAME_MSEC
		Q3Server.usercmd(me, t, Vector3.ZERO, 0, 0, 127, 0, 0)
		Q3Server.frame(Q3Host.FRAME_MSEC)
		for cmd in Q3Server.next_commands(me):
			run.feed(cmd)
			if cmd.begins_with("race") or cmd.begins_with("cp"):
				log.append("%d: %s" % [Q3Server.time(), cmd])
			if cmd.begins_with("cp"):
				prints.append([cmd.get_slice("\"", 1), Q3Server.time()])
		var ps := Q3Server.player_state(me)
		if not ps.is_empty():
			var o: Vector3 = ps["origin"]
			var v: Vector3 = ps["velocity"]
			speeds.append([o.x, Vector2(v.x, v.y).length()])
			if o.x > 3100:
				break
	print("\n".join(log))
	check("the timer starts on the start line", run.start_time > 0 and (log.size() > 0))
	check("two checkpoints are reported in order, with times", cps.size() == 2 and cps[0][0] == 1 and cps[1][0] == 2
			and cps[0][1] > 0 and cps[1][1] > cps[0][1], str(cps))
	check("the stop line ends the run once, as a record", stops.size() == 1 and cps.size() == 2 and stops[0][1] and stops[0][0] > cps[1][1], str(stops))
	var expect_ms := 0
	if stops.size() == 1:
		expect_ms = stops[0][0]
	# the run covers 1400 - 128 units (start slab to stop slab) at walking speed, so about 4 s
	check("the time is the walk from the start line to the stop line (3.5 to 5 s)", expect_ms > 3500 and expect_ms < 5000, "%d ms" % expect_ms)
	var ma := -1
	var mb := -1
	for p in prints:
		if p[0] == "mm_a":
			ma = p[1]
		elif p[0] == "mm_b":
			mb = p[1]
	check("target_multimanager fires its targets with their own delays (b at once, a 0.3 s later)", ma > 0 and mb > 0 and ma - mb >= 250 and ma - mb <= 400,
			"a %d, b %d" % [ma, mb])
	var ff_need := false
	var ff_ok := false
	for p in prints:
		ff_need = ff_need or String(p[0]).contains("more frags needed")
		ff_ok = ff_ok or p[0] == "ff_ok"
	check("target_fragsFilter holds a player with no frags and says how many are needed", ff_need and not ff_ok, str(prints))
	# speed: the pad adds 400 (friction pulls it back), then target_speed adds 50 percent
	var peak_pad := 0.0
	var best_jump := 0.0   # the biggest one-frame gain around the target_speed (friction only loses)
	for i in range(1, speeds.size()):
		var x: float = speeds[i][0]
		var sp: float = speeds[i][1]
		if x > 1700 and x < 1900:
			peak_pad = maxf(peak_pad, sp)
		if x > 2200 and x < 2340 and float(speeds[i - 1][1]) > 100.0:
			best_jump = maxf(best_jump, sp / float(speeds[i - 1][1]))
	check("trigger_push_velocity adds to the player's speed (a walk is 320)", peak_pad > 600.0, "peak %.0f" % peak_pad)
	check("target_speed with PERCENTAGE and ADD raises it by half in one frame", best_jump > 1.3 and best_jump < 1.6, "x%.2f" % best_jump)

	# life two: the spawn tells the HUD the best time
	run.reset.connect(func(): log.append("reset"))
	Q3Server.client_command(me, "kill")
	var reset_seen := false
	for f in 600:
		var t := Q3Server.time() + Q3Host.FRAME_MSEC
		Q3Server.usercmd(me, t, Vector3.ZERO, 1 if f % 20 < 10 else 0, 0, 0, 0, 0)   # attack presses respawn
		Q3Server.frame(Q3Host.FRAME_MSEC)
		for cmd in Q3Server.next_commands(me):
			run.feed(cmd)
			if cmd.begins_with("race reset"):
				reset_seen = true
				print("  ", cmd)
		if reset_seen:
			break
	check("a new life resets the run and reports the best time and the checkpoint count", reset_seen and not run.is_running()
			and run.best_ms == expect_ms and run.checkpoints_total == 2, "best %d (expected %d), checkpoints %d" % [run.best_ms, expect_ms, run.checkpoints_total])
	# g_race: a rocket into the floor costs no health, still throws the player, and the ammo never drops
	Q3Server.set_cvar("sv_cheats", "1")
	Q3Server.frame(Q3Host.FRAME_MSEC)
	Q3Server.frame(Q3Host.FRAME_MSEC)
	Q3Server.client_command(me, "give weapons")
	Q3Server.client_command(me, "give ammo")
	var hp_before := 0
	var vz_max := 0.0
	var shots := 0
	var ammo_after := -1
	for f in 400:
		var t := Q3Server.time() + Q3Host.FRAME_MSEC
		var press := f > 20 and f % 120 < 60
		Q3Server.usercmd(me, t, Vector3(90, 0, 0), 1 if press else 0, 5, 0, 0, 0)   # weapon 5: the rocket launcher
		Q3Server.frame(Q3Host.FRAME_MSEC)
		Q3Server.next_commands(me)
		var ps := Q3Server.player_state(me)
		if f == 21:
			hp_before = int((ps["stats"] as PackedInt32Array)[0])
		vz_max = maxf(vz_max, (ps["velocity"] as Vector3).z)
		if int(ps.get("weapon", 0)) == 5 and (ps.get("ammo") is PackedInt32Array):
			ammo_after = (ps["ammo"] as PackedInt32Array)[5]
	var hp_after := int((Q3Server.player_state(me)["stats"] as PackedInt32Array)[0])
	check("g_race: a rocket jump throws the player (knockback kept)", vz_max > 150.0, "peak vz %.0f" % vz_max)
	check("g_race: the player's own rocket costs no health", hp_after >= hp_before - 5 and hp_after > 0, "%d -> %d (health above 100 melts a point a second)" % [hp_before, hp_after])
	check("g_race: ammo never runs dry", ammo_after >= 200, "rockets %d" % ammo_after)
	check("Q3RaceRun formats times", Q3RaceRun.format_time(4321) == "4.321" and Q3RaceRun.format_time(62345) == "1:02.345")
	Q3Server.exec_now("killserver")
	_finish(t0)


func _finish(t0: int) -> void:
	print("RACE %d/%d passed (%.1f s)" % [passed, passed + failed, (Time.get_ticks_msec() - t0) / 1000.0])
	get_tree().quit(0 if failed == 0 else 1)
