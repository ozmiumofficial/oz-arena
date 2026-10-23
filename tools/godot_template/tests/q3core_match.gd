extends Node
## Headless match test for q3core: id's server, game module and botlib run q3dm17 with 3 bots and
## a local player for 60 s of game time, as fast as the machine can.
##   godot --headless --path . res://tests/q3core_match.tscn [-- map=q3dm17 seconds=60 bots=3]
## Prints the scores and "Q3CORE MATCH ok|FAILED"; exit code 1 on failure. The self-test runs
## the same thing through run_match().

const BOTS := ["sarge", "xaero", "crash", "klesk", "anarki", "doom"]


func _ready() -> void:
	var args := {}
	for a in OS.get_cmdline_user_args():
		if a.contains("="):
			args[a.get_slice("=", 0)] = a.get_slice("=", 1)
	var r := run_match(String(args.get("map", "q3dm17")), int(args.get("seconds", "60")), int(args.get("bots", "3")))
	print(r["report"])
	print("Q3CORE MATCH ", "ok" if r["ok"] else "FAILED")
	get_tree().quit(0 if r["ok"] else 1)


## Starts the engine if needed, plays one match, returns {ok, report, frags, deaths, msec, commands}.
## ok: client-side prediction lands where the server puts the player, snapshots every frame, server commands arrive, every bot moved, someone died, and (with
## require_frags) someone scored a frag. Short runs (the self-test) skip the frag requirement,
## since a frag in 20 s is likely but not certain.
static func run_match(map: String, seconds: int, bots: int, require_frags := true) -> Dictionary:
	var lines: PackedStringArray = []
	var err := Q3Host.start()
	if err != "":
		return {"ok": false, "report": "start: " + err}
	Q3Server.set_cvar("g_gametype", "0")
	Q3Server.set_cvar("fraglimit", "0")
	Q3Server.set_cvar("timelimit", "0")
	Q3Server.set_cvar("g_doWarmup", "0")
	if Q3Server.exec_now("map " + map) != 0 or not Q3Server.running():
		return {"ok": false, "report": "map %s: %s" % [map, Q3Server.error()]}
	for i in bots:
		Q3Server.exec("addbot %s 3" % BOTS[i % BOTS.size()])
	Q3Server.frame(Q3Host.FRAME_MSEC)
	var me := Q3Server.connect_local(Q3Host.userinfo({"name": "Tester", "model": "sarge"}))
	if me < 0:
		return {"ok": false, "report": "connect: " + Q3Server.error()}

	var start_pos := {}
	for cl in 16:
		if cl != me and Q3Server.client_state(cl) == 4:
			start_pos[cl] = Q3Server.player_state(cl)["origin"]

	var t0 := Time.get_ticks_msec()
	var frames := seconds * 1000 / Q3Host.FRAME_MSEC
	var commands := 0
	var kinds := {}
	var snaps := 0
	var max_ents := 0
	var predicted := 0
	var matched := 0
	for f in frames:
		# the local player walks forward and turns slowly, firing now and then
		var t := Q3Server.time() + Q3Host.FRAME_MSEC
		var cmd := {"serverTime": t, "angles": Vector3(0, fmod(f * 0.5, 360.0), 0), "buttons": 1 if f % 50 < 10 else 0,
			"weapon": 0, "forward": 127, "right": 0, "up": 127 if f % 90 == 0 else 0}
		# prediction (cgame's Pmove over the newest snapshot) must land where the server puts us
		var pred := Q3Server.predict(me, [cmd])
		Q3Server.usercmd(me, t, cmd["angles"], cmd["buttons"], 0, cmd["forward"], 0, cmd["up"])
		if not pred.is_empty():
			predicted += 1
			var live := Q3Server.player_state(me)
			if Vector3(pred["origin"]).distance_to(live["origin"]) < 0.01 and Vector3(pred["velocity"]).distance_to(live["velocity"]) < 0.01:
				matched += 1
		if Q3Server.frame(Q3Host.FRAME_MSEC) != 0:
			return {"ok": false, "report": "frame %d: %s" % [f, Q3Server.error()]}
		for c in Q3Server.next_commands(me):
			commands += 1
			kinds[c.get_slice(" ", 0)] = kinds.get(c.get_slice(" ", 0), 0) + 1
		var snap := Q3Server.snapshot(me)
		if not snap.is_empty():
			snaps += 1
			max_ents = maxi(max_ents, snap["entities"].size())
	var msec := Time.get_ticks_msec() - t0

	var frags := 0
	var deaths := 0
	var moved := 0
	var names: PackedStringArray = []
	for cl in 16:
		var ps := Q3Server.player_state(cl)
		if ps.is_empty() or Q3Server.client_state(cl) < 4:
			continue
		var info := Q3Host.parse_info(Q3Server.configstring(544 + cl))   # CS_PLAYERS
		var score: int = ps["persistant"][0]   # PERS_SCORE
		frags += maxi(score, 0)
		deaths += int(ps["persistant"][8])   # PERS_KILLED
		if start_pos.has(cl) and Vector3(ps["origin"]).distance_to(start_pos[cl]) > 64.0:
			moved += 1
		names.append("%s %d" % [info.get("n", "?"), score])
	var ok := matched >= predicted * 0.98 and predicted > frames / 2 and snaps > frames / 2 and commands > 0 and moved >= bots and deaths > 0 and (frags > 0 or not require_frags)
	lines.append("%s: %d s of game time in %d ms (%.1fx real time), %d snapshots (up to %d entities)" %
		[map, seconds, msec, seconds * 1000.0 / maxf(msec, 1), snaps, max_ents])
	lines.append("scores: %s (%d frags, %d deaths, %d/%d bots moved)" % [", ".join(names), frags, deaths, moved, bots])
	lines.append("server commands: %d %s" % [commands, str(kinds)])
	lines.append("prediction: %d/%d moves match the server exactly" % [matched, predicted])
	return {"ok": ok, "report": "\n".join(lines), "frags": frags, "deaths": deaths, "predicted": predicted, "matched": matched, "msec": msec, "commands": commands}
