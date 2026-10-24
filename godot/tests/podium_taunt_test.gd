extends Node
## Podium taunts through id's game (patch 0009): "ptaunt <id>" from a player comes back to every client as
## "ptaunt <client> <id>", also during the intermission (where every other command is chat); ids that aren't
## the catalog's form are dropped, and a player gets one every 1.5 s.
##   godot --headless --path . res://tests/podium_taunt_test.tscn
## Prints "PODIUM TAUNT ok|FAILED".

var a := -1
var b := -1
var t := 0
var fails: Array = []


func _ready() -> void:
	var err := Q3Host.start()
	if err != "":
		_done(["start: " + err])
		return
	for cv in {"fraglimit": "0", "g_readyUp": "0", "g_doWarmup": "0"}:   # no warmup: the clock runs at once
		Q3Server.set_cvar(cv, {"fraglimit": "0", "g_readyUp": "0", "g_doWarmup": "0"}[cv])
	if Q3Server.exec_now("map q3dm17") != 0 or not Q3Server.running():
		_done(["map: " + Q3Server.error()])
		return
	a = Q3Server.connect_local(Q3Host.userinfo({"name": "Alpha", "model": "sarge"}))
	b = Q3Server.connect_local(Q3Host.userinfo({"name": "Bravo", "model": "xaero"}))
	_run(1.0)
	_expect(a, "taunt_sarge", true, "a taunt reaches every player")
	_expect(a, "taunt_xaero", false, "a second within 1.5 s is dropped")
	_run(1.6)
	_expect(a, "Bad;Id", false, "an id that isn't the catalog's form is dropped")
	# the intermission: every other command is chat there
	Q3Server.exec_now("set fraglimit 1")   # the first frag ends it: two bots fight
	Q3Server.exec("addbot sarge 5")
	Q3Server.exec("addbot klesk 5")
	var t0 := Time.get_ticks_msec()
	while Q3Server.configstring(22).is_empty() or Q3Server.configstring(22) == "0":   # CS_INTERMISSION
		_run(0.1)
		if Time.get_ticks_msec() - t0 > 60000:
			fails.append("the match never reached its intermission")
			break
	_run(1.6)
	print("  intermission reached: ", Q3Server.configstring(22))
	_expect(b, "taunt_doom", true, "and during the intermission")
	_done(fails)


func _run(seconds: float) -> void:
	for i in int(seconds * 125):
		t += 8
		for c in [a, b]:
			Q3Server.usercmd(c, t, Vector3.ZERO, 0, 0, 0, 0, 0)
		Q3Server.frame(8)


func _expect(who: int, id: String, arrives: bool, what: String) -> void:
	for c in [a, b]:
		Q3Server.next_commands(c)   # what came before
	Q3Server.client_command(who, "ptaunt " + id)
	_run(0.1)
	var want := "ptaunt %d %s" % [who, id]
	for c in [a, b]:
		var got := Array(Q3Server.next_commands(c)).any(func(x): return String(x).strip_edges() == want)
		if got != arrives:
			fails.append("%s: client %d %s '%s'" % [what, c, "got" if got else "didn't get", want])
	print("  %s: %s" % [what, "ok" if not fails.any(func(f): return String(f).begins_with(what)) else "FAIL"])


func _done(f: Array) -> void:
	for x in f:
		print(x)
	print("PODIUM TAUNT ", "ok" if f.is_empty() else "FAILED")
	get_tree().quit(0 if f.is_empty() else 1)
