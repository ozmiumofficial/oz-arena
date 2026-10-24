class_name Q3NetFeed
extends Q3Feed
## A match played over the network (MULTIPLAYER_PLAN.md M3): everything Gameplay, Q3Client and the HUD
## read comes from Q3Net, the net client in q3core, which is a client of id's server speaking id's own
## protocol through a Q3Transport. Where the live feed reads the server's memory, this one has only
## what a network client has, as cgame does:
##   - the world is snapshots: the one at or before the render time and the one after it, so players
##     can be drawn between them (set_next / fraction, as a replay does);
##   - your own playerState is predicted: the newest snapshot's with Pmove run over the commands
##     the server hasn't acknowledged, the misprediction eased out over 100 ms (cg_errorDecay);
##   - other players' scores come from the "scores" command (cg_servercmds.c CG_ParseScores), their
##     names, models and teams from configstrings;
##   - cvars are the server's serverinfo and systeminfo.
## Values are id units and axes, as Q3Server gives them.

const E := preload("res://scripts/game/q3_enums.gd")
## The snaps userinfo a joining player sends. The server runs 8 ms frames, so a snapshot goes out on the first
## frame at least 1000 / snaps ms after the last: 40 asks for 25 ms and gets 32 (31 Hz); 42 asks for 23 and gets 24,
## Quake Live's 40 Hz. Others are drawn about a snapshot interval + 5 ms + `interp` behind the newest snapshot
## (CL_AdjustTimeDelta), so 42 shows them 8 ms fresher than 40. Through the relay it costs about 56 MB a
## player-hour in a 4-player match and 72 in an 8-player one, 12% more than 40 (tests/relay_bandwidth.tscn, M8).
const SNAPS := 42
## cl_maxpackets: packets a second at most to the server. Commands wait for the next packet, about 10 ms on
## average here; 30 would save 13% of the relay bytes and add about 7 ms, 125 would cost 19% more.
const MAXPACKETS := 60
const ERROR_DECAY := 100.0           ## cg_errorDecay, ms
const SCORE_EVERY := 1.0             ## s between "score" requests

var transport: Q3Transport
var me := -1
var error := ""                       ## why the connection ended, once it has
var interp := 0                       ## ms other players are drawn behind (Settings > Network > Interpolation)

var _pair: Dictionary = {}
var _ps: Dictionary = {}
var _error_vec := Vector3.ZERO
var _error_time := 0
var _commands: PackedStringArray = PackedStringArray()
var _scores: Dictionary = {}          ## client -> {score, ping, time, powerups, accuracy ...}
var _score_t := 0.0
var _extrapolating := false
var _state := 0
var _info_cache: Dictionary = {}      ## configstring text -> parsed


func is_replay() -> bool:
	return false


## 0 disconnected, 1 connecting, 2 challenging, 3 connected, 4 gamestate in, 5 playing.
func state() -> int:
	return _state


func time() -> int:
	return Q3Net.time()


## The time the world is drawn at: the server's clock, interp ms behind.
func render_time() -> int:
	return Q3Net.time() - interp


func running() -> bool:
	return _state == 5


func configstring(index: int) -> String:
	return Q3Net.configstring(index)


## A server cvar: serverinfo and systeminfo (what the server tells every client).
func cvar(name: String) -> String:
	for cs in [E.CS_SYSTEMINFO, E.CS_SERVERINFO]:
		var info := _info(Q3Net.configstring(cs))
		if info.has(name):
			return String(info[name])
	return ""


func _info(text: String) -> Dictionary:
	if _info_cache.has(text):
		return _info_cache[text]
	if _info_cache.size() > 16:
		_info_cache.clear()
	var parsed := Q3Host.parse_info(text)
	_info_cache[text] = parsed
	return parsed


## Moves the transport's packets and the net client's clock. Call once per frame, before update().
func pump(ms: int) -> int:
	if transport != null:
		if not transport.is_open() and _state != 0:
			Q3Net.disconnect_from()
			error = "The connection was lost"
			_state = 0
			return 0
		for p in transport.poll():
			Q3Net.receive(p)
	_state = Q3Net.frame(ms, false)   # the clock judges its own extrapolation (CL_SetCGameTime); this draws `interp` behind it
	if transport != null:
		for p in Q3Net.outgoing(64):
			transport.send(p)
	if _state == 0 and error == "":
		error = Q3Net.error()
	return _state


## After pump(): picks this frame's snapshots, predicts the local player, reads the server commands.
func update(delta: float) -> void:
	if _state < 4:
		return
	me = Q3Net.client_num()
	var snaps := Q3Net.snapshots(render_time())
	_pair = snaps
	_extrapolating = snaps.has("snap") and not snaps.has("next")
	var pr := Q3Net.predict()
	if not pr.is_empty():
		_error_vec = pr["error"]
		_error_time = int(pr["error_time"])
		_ps = pr
		# the view eases out a misprediction (CG_OffsetFirstPersonView): the error decays over 100 ms
		var f := 1.0 - float(Q3Net.time() - _error_time) / ERROR_DECAY
		if f > 0.0 and f < 1.0 and _error_vec != Vector3.ZERO:
			_ps["origin"] = Vector3(pr["origin"]) + _error_vec * f
	elif snaps.has("snap"):
		_ps = snaps["snap"]["ps"]
	for c in Q3Net.next_commands():
		if c.begins_with("scores "):
			_parse_scores(c)
		_commands.append(c)
	_score_t -= delta
	if _score_t <= 0.0 and _state == 5:
		_score_t = SCORE_EVERY
		Q3Net.command("score")


## cg_servercmds.c CG_ParseScores: "scores <n> <red> <blue>" then 14 numbers a client (client, score,
## ping, minutes, flags, powerups, accuracy, impressive, excellent, gauntlet, defend, assist, perfect, captures).
func _parse_scores(cmd: String) -> void:
	var a := cmd.split(" ", false)
	if a.size() < 4:
		return
	var n := int(a[1])
	_scores.clear()
	for i in n:
		var o := 4 + i * 14
		if o + 13 >= a.size():
			break
		_scores[int(a[o])] = {"score": int(a[o + 1]), "ping": int(a[o + 2]), "time": int(a[o + 3]), "powerups": int(a[o + 5]),
			"accuracy": int(a[o + 6]), "impressive": int(a[o + 7]), "excellent": int(a[o + 8]), "gauntlet": int(a[o + 9]),
			"defend": int(a[o + 10]), "assist": int(a[o + 11]), "perfect": int(a[o + 12]), "captures": int(a[o + 13])}


func player_state(client: int) -> Dictionary:
	if client == me or client < 0:
		return _ps
	return _other_state(client)


## Someone else, as much as a client knows: their team from CS_PLAYERS, their score and ping from the
## last "scores", their powerups (a flag carried) from it as well.
func _other_state(cl: int) -> Dictionary:
	var info := _info(Q3Net.configstring(E.CS_PLAYERS + cl))
	if info.is_empty():
		return {}
	var sc: Dictionary = _scores.get(cl, {})
	var pers := PackedInt32Array()
	pers.resize(16)
	pers[E.PERS_SCORE] = int(sc.get("score", 0))
	pers[E.PERS_TEAM] = int(info.get("t", "0"))
	var pw := PackedInt32Array()
	pw.resize(16)
	var bits := int(sc.get("powerups", 0))
	for i in E.PW_NUM_POWERUPS:
		if bits & (1 << i):
			pw[i] = 1
	return {"clientNum": cl, "pm_type": 0, "pm_flags": 0, "persistant": pers, "powerups": pw, "ping": int(sc.get("ping", 0)),
		"stats": PackedInt32Array([100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]), "weapon": 0, "origin": Vector3.ZERO}


func client_state(client: int) -> int:
	if client == me:
		return 4
	return 4 if Q3Net.configstring(E.CS_PLAYERS + client) != "" else 0


## The snapshot at the render time. Gameplay hands it to Q3Client every frame; events play once.
func snapshot(_client: int) -> Dictionary:
	return _pair.get("snap", {})


## The snapshot after it, and how far the render time is toward it (Q3Client.set_next).
func next_snapshot() -> Dictionary:
	return _pair.get("next", {})


func fraction() -> float:
	if not _pair.has("next"):
		return 0.0
	var a := float(_pair["snap"]["serverTime"])
	var b := float(_pair["next"]["serverTime"])
	return clampf((float(render_time()) - a) / maxf(b - a, 1.0), 0.0, 1.0)


## The snapshot's own playerState (what the server says, unpredicted), for ping and tests.
func server_state() -> Dictionary:
	return _pair["snap"]["ps"] if _pair.has("snap") else {}


## The round trip the client measured, ms.
func ping() -> int:
	return int(_pair.get("ping", 0))


func next_commands(_client: int) -> PackedStringArray:
	var out := _commands
	_commands = PackedStringArray()
	return out


func trace(start: Vector3, end: Vector3, mins: Vector3, maxs: Vector3, pass_entity: int, content_mask: int) -> Dictionary:
	return Q3Net.trace(start, end, mins, maxs, pass_entity, content_mask)


func point_contents(point: Vector3, pass_entity: int) -> int:
	return Q3Net.point_contents(point, pass_entity)


func usercmd(server_time: int, angles: Vector3, buttons: int, weapon: int, forward: int, right: int, up: int) -> void:
	Q3Net.usercmd(server_time, angles, buttons, weapon, forward, right, up)


func command(text: String) -> void:
	Q3Net.command(text)


func disconnect_now() -> void:
	if _state != 0:
		Q3Net.disconnect_from()
		if transport != null:
			for p in Q3Net.outgoing(64):
				transport.send(p)
		_state = 0
	if transport != null:
		transport.close()
