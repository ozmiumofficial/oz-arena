extends Node
## Windowed bot match, watched as a spectator, for the players' body motion (CG_PlayerAngles): the
## camera picks a bot strafing or running diagonally and films it from the side, so its legs turned
## off its aim (22 or 45 degrees), its torso a quarter of that, and its lean show. Saves
## <out>_<n>.png and prints how many strafing bodies' legs faced their aim plus the move offset.
##   godot --path . res://tests/body_motion_capture.tscn -- out=C:/temp/motion [map=q3dm17] [bots=5] [shots=8]

const E := preload("res://scripts/game/q3_enums.gd")
const MOVE_OFFSETS := [0.0, 22.0, 45.0, -22.0, 0.0, 22.0, -45.0, -22.0]

var out := "user://motion"
var shots := 8
var _game: Node
var _t := 0.0
var _target := -1
var _lock_t := 0.0
var _cam_off := Vector3.ZERO   ## the camera beside the target (id units), riding along with it
var _taken := 0
var _errors: PackedFloat32Array = []   ## strafing bodies' legs: degrees off aim + move offset
var _signed := 0    ## frames whose legs were turned the way of the move offset


func _ready() -> void:
	var map := "q3dm17"
	var bots := 5
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			out = a.substr(4)
		elif a.begins_with("map="):
			map = a.substr(4)
		elif a.begins_with("bots="):
			bots = int(a.substr(5))
		elif a.begins_with("shots="):
			shots = int(a.substr(6))
	var s := Session.new_setup("ffa", map)
	s.bots = []
	var names := ["Sarge", "Xaero", "Crash", "Visor", "Grunt", "Hunter", "Bones", "Orbb"]
	for i in bots:
		s.bots.append({"name": names[i % names.size()], "skill": 4, "team": ""})
	s.rules = {"fraglimit": 0, "timelimit": 0, "warmup": 0}
	s.spectate = true
	Session.setup = s
	process_priority = 1000   # after Gameplay: the camera is ours
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	_game.test_input = true
	add_child(_game)


func _process(delta: float) -> void:
	_t += delta
	if _t < 4.0 or not Q3Server.running():
		return
	var ents := {}
	for es in _game.feed.snapshot(_game.me).get("entities", []):
		if int(es["eType"]) == E.ET_PLAYER and int(es["number"]) < E.MAX_CLIENTS:
			ents[int(es["number"])] = es
	var bodies: Dictionary = _game.client.player_nodes()
	_tally(ents, bodies)
	_lock_t += delta
	if _target < 0 or not ents.has(_target) or _lock_t > 1.6:
		_pick(ents, bodies)
	if _target < 0:
		return
	var es: Dictionary = ents[_target]
	var o: Vector3 = es["pos"]["trBase"]
	_game.cam.global_position = Q3.q3_to_godot(o + _cam_off)
	_game.cam.look_at(Q3.q3_to_godot(o + Vector3(0, 0, 4)))
	if _lock_t > 0.3 and _lock_t - delta <= 0.3 and _strafing(es):
		_taken += 1
		var ch: Q3Character = bodies.get(_target)
		print("  shot %d: client %d dir %d legs %s yaw off aim %+.1f torso %+.1f lean %+.1f/%+.1f" % [_taken, _target,
			_dir(es), ch.legs_clip, -ch.head_turn(), ch.torso_turn(), ch._lean.x, ch._lean.y])
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png("%s_%d.png" % [out, _taken])
		if _taken >= shots:
			_errors.sort()
			var median := _errors[_errors.size() / 2] if _errors.size() else 999.0
			print("  strafing bodies' legs off aim + move offset: median %.1f, 90th percentile %.1f degrees; turned the move's way in %d of %d frames"
				% [median, _errors[int(_errors.size() * 0.9)] if _errors.size() else 999.0, _signed, _errors.size()])
			print("BODY MOTION ", "ok" if median < 6.0 and _signed >= _errors.size() * 0.75 else "FAILED")
			Q3Server.exec_now("killserver")
			get_tree().quit(0)
	if _t > 240.0:
		print("BODY MOTION FAILED: only %d shots" % _taken)
		get_tree().quit(1)


static func _dir(es: Dictionary) -> int:
	return int((es.get("angles2", Vector3.ZERO) as Vector3).y)


## Running or walking with a sideways part (movementDir 1-3, 5-7).
static func _strafing(es: Dictionary) -> bool:
	var legs := int(es["legsAnim"]) & ~E.ANIM_TOGGLEBIT
	var v: Vector3 = es["pos"]["trDelta"]
	return _dir(es) % 4 != 0 and legs in [E.LEGS_RUN, E.LEGS_BACK, E.LEGS_WALK, E.LEGS_BACKWALK] \
		and Vector2(v.x, v.y).length() > 150.0 and not (int(es["eFlags"]) & E.EF_DEAD)


## Every strafing body: how far its legs are from the aim turned by the move offset, and whether
## they're turned that way at all. They swing after it (CG_SwingAngles, 0.15 to 0.6 degrees a
## millisecond), so a bot sweeping its aim keeps them a few degrees behind.
func _tally(ents: Dictionary, bodies: Dictionary) -> void:
	for cl in ents:
		var es: Dictionary = ents[cl]
		var ch: Q3Character = bodies.get(cl)
		if ch == null or not _strafing(es):
			continue
		var aim := float((es["apos"]["trBase"] as Vector3).y)
		var off := float(MOVE_OFFSETS[_dir(es)])
		var legs := rad_to_deg(ch.rotation.y)
		_errors.append(absf(wrapf(legs - aim - off, -180.0, 180.0)))
		if signf(wrapf(legs - aim, -180.0, 180.0)) == signf(off):
			_signed += 1


## A strafing bot and a clear spot beside it (a side of its path, else ahead of it).
func _pick(ents: Dictionary, bodies: Dictionary) -> void:
	_target = -1
	for cl in ents:
		var es: Dictionary = ents[cl]
		if not bodies.has(cl) or not _strafing(es):
			continue
		var o: Vector3 = es["pos"]["trBase"]
		var v: Vector3 = es["pos"]["trDelta"]
		var along := Vector3(v.x, v.y, 0).normalized()
		var side := Vector3(-along.y, along.x, 0)
		for off in [side * 100.0, -side * 100.0, along * 140.0]:
			var spot: Vector3 = o + off + Vector3(0, 0, 30)
			var tr: Dictionary = Q3Server.trace(o, spot, Vector3(-6, -6, -6), Vector3(6, 6, 6), int(cl), 1)
			if not tr.is_empty() and float(tr["fraction"]) >= 1.0:
				_target = int(cl)
				_cam_off = spot - o
				_lock_t = 0.0
				return
