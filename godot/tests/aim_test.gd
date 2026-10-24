extends Node3D
## Windowed side view of three characters aiming up, level and down (aim_pitch -40, 0, 40):
## the torso and head bend as CG_PlayerAngles turns them. Saves <out>.png. Then <out>_turn.png:
## four match bodies (turn on) facing the camera, seen from above: strafing left (legs 45 degrees
## off and leaning left), running forward (leaning forward), running back and right, and one
## standing whose aim jumps 70 degrees (its legs shuffle round with LEGS_TURN, then stand). Prints
## each one's angles and "AIM TEST ok|FAILED".
##   godot --path . res://tests/aim_test.tscn -- out=C:/temp/aim [model=sarge]

var out := "user://aim"
var model := "sarge"
var _t := 0.0
var _pitch: Array[Q3Character] = []
var _turn: Array[Q3Character] = []
var _cam: Camera3D
var _stage := 0
var _shuffled := false   ## the standing one played LEGS_TURN while its legs swung round
var _ok := true


func _ready() -> void:
	Engine.max_fps = 60
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			out = a.substr(4)
		elif a.begins_with("model="):
			model = a.substr(6)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(0.18, 0.2, 0.24)
	env.environment.ambient_light_color = Color.WHITE
	env.environment.ambient_light_energy = 0.6
	add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-40, -30, 0)
	add_child(sun)
	var gun: Array = Q3.entities.get("weapon_railgun", {}).get("models", [])
	for i in 3:
		var ch := Q3Character.new()
		ch.model = model
		add_child(ch)
		ch.position = Vector3((i - 1) * 1.6, 0, 0)
		ch.rotation_degrees = Vector3(0, 90, 0)   # facing +X: the camera sees them side on
		ch.aim_pitch = [-40.0, 0.0, 40.0][i]
		if gun.size():
			ch.attach(gun[0])
		_pitch.append(ch)
	# [legs clip, move_dir, velocity (id units/s: x forward, y left)]
	var moves := [["LEGS_RUN", 2, Vector3(0, 320, 0)], ["LEGS_RUN", 0, Vector3(320, 0, 0)],
		["LEGS_BACK", 5, Vector3(-226, -226, 0)], ["LEGS_IDLE", 0, Vector3.ZERO]]
	for i in moves.size():
		var ch := Q3Character.new()
		ch.model = model
		ch.legs_clip = moves[i][0]
		ch.turn = true
		ch.match_clock = true
		add_child(ch)
		ch.position = Vector3((i - 1.5) * 1.6, 0, 0)
		ch.move_dir = moves[i][1]
		ch.velocity = moves[i][2]
		ch.aim_center = moves[i][0] != "LEGS_IDLE"
		ch.reset_motion()
		if gun.size():
			ch.attach(gun[0])
		ch.visible = false
		_turn.append(ch)
	_cam = Camera3D.new()
	_cam.position = Vector3(0, 1.0, 5.5)
	add_child(_cam)
	_cam.make_current()


func _process(delta: float) -> void:
	_t += delta
	for ch in _turn:
		ch.advance(delta * 1000.0)
	var stand := _turn[3]
	if _t > 3.0 and stand.aim_yaw == 0.0:
		stand.aim_yaw = 70.0   # past the legs' 40 degree tolerance: they swing round
	if stand._shown_legs == "LEGS_TURN":
		_shuffled = true
	if _stage == 0 and _t > 2.0:
		_stage = 1
		await _shot(".png")
		for ch in _pitch:
			ch.visible = false
		for ch in _turn:
			ch.visible = true
		_cam.position = Vector3(0, 3.2, 4.6)
		_cam.look_at(Vector3(0, 0.6, 0))
	elif _stage == 1 and _t > 6.0:
		_stage = 2
		_report()
		await _shot("_turn.png")
		print("AIM TEST ", "ok" if _ok else "FAILED")
		get_tree().quit(0 if _ok else 1)


func _shot(suffix: String) -> void:
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(out + suffix)


func _report() -> void:
	var names := ["strafe left", "run forward", "back right", "standing, aim +70"]
	for i in _turn.size():
		var ch := _turn[i]
		print("  %-18s legs yaw %6.1f  torso %+6.1f  head %+6.1f  lean pitch %+5.1f roll %+5.1f  legs %s" % [names[i],
			rad_to_deg(ch.rotation.y), ch.torso_turn(), ch.head_turn(), ch._lean.x, ch._lean.y, ch._shown_legs])
	var near := func(a: float, b: float) -> bool: return absf(a - b) < 0.1
	_check("strafing left turns the legs 45 left", near.call(rad_to_deg(_turn[0].rotation.y), 45.0))
	_check("and the torso a quarter of that", near.call(_turn[0].torso_turn(), -45.0 * 0.75))
	# 16 degrees at 320 units/s, split between the turned legs' forward and left
	_check("and leans left and forward 11.3 degrees", near.call(_turn[0]._lean.y, -16.0 * sqrt(0.5)) and near.call(_turn[0]._lean.x, 16.0 * sqrt(0.5)))
	_check("running forward leans forward 16 degrees", near.call(_turn[1]._lean.x, 16.0) and near.call(_turn[1]._lean.y, 0.0))
	_check("backing right turns the legs 22", near.call(rad_to_deg(_turn[2].rotation.y), 22.0))
	_check("the head looks where they aim", near.call(_turn[0].head_turn(), -45.0) and near.call(_turn[2].head_turn(), -22.0))
	_check("a standing body's legs shuffle round with LEGS_TURN", _shuffled)
	_check("and stand again facing the aim", near.call(rad_to_deg(_turn[3].rotation.y), 70.0) and _turn[3]._shown_legs == "LEGS_IDLE")


func _check(what: String, ok: bool) -> void:
	print(("  ok    " if ok else "  FAIL  ") + what)
	if not ok:
		_ok = false
