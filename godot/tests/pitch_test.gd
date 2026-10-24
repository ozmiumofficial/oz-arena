extends Node
## Looking straight down after a teleport (the "can't aim at the floor" bug, 2026-10-24): a teleport (setviewpos)
## sets delta_angles to cancel the pitch you had, so the real view is the usercmd's pitch plus that offset. With
## Gameplay's old ±89° limit on the usercmd's pitch, someone looking 60° down when teleported could only look about
## 29° down after it. The server limits the real view itself (PM_UpdateViewAngles), so the client mustn't.
##   godot --headless --path . res://tests/pitch_test.tscn
## Prints "PITCH ok|FAILED".

var me := -1
var t := 0


func _ready() -> void:
	var err := Q3Host.start()
	if err != "":
		_done("start: " + err, false)
		return
	Q3Server.set_cvar("sv_cheats", "1")
	if Q3Server.exec_now("devmap q3dm17") != 0 or not Q3Server.running():
		_done("map: " + Q3Server.error(), false)
		return
	me = Q3Server.connect_local(Q3Host.userinfo({"name": "Tester", "model": "sarge"}))
	t = Q3Server.time()
	_run(1.0, 0.0)
	_run(0.5, 60.0)                       # look 60° down
	Q3Server.client_command(me, "setviewpos 0 0 600 0")   # a teleport: delta_angles cancels that pitch
	_run(0.5, 60.0)
	var after_teleport := _pitch()
	var old_way := _reach(89.0)           # the old client limit on the usercmd's pitch
	var new_way := _reach(170.0)          # no client limit: push the mouse on down
	var ok := new_way > 85.0 and old_way < 40.0
	_done("after the teleport the view is at %.1f°; with the usercmd's pitch held to 89° it reaches %.1f° down, without the limit %.1f°" % [
		after_teleport, old_way, new_way], ok)


func _run(seconds: float, pitch: float) -> void:
	for i in int(seconds * 125):
		t += 8
		Q3Server.usercmd(me, t, Vector3(pitch, 0, 0), 0, 0, 0, 0, 0)
		Q3Server.frame(8)


## Pushes the usercmd's pitch up to `limit` (as the mouse would) and returns the view's pitch.
func _reach(limit: float) -> float:
	var p := 60.0
	while p < limit:
		p = minf(p + 2.0, limit)
		_run(0.016, p)
	_run(0.2, limit)
	return _pitch()


func _pitch() -> float:
	var a: Vector3 = Q3Server.player_state(me).get("viewangles", Vector3.ZERO)
	return wrapf(a.x, -180.0, 180.0)


func _done(msg: String, ok: bool) -> void:
	print(msg)
	print("PITCH ", "ok" if ok else "FAILED")
	get_tree().quit(0 if ok else 1)
