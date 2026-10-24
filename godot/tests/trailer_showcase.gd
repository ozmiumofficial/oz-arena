extends Node
## Trailer footage (TRAILER_PLAN.md): a fighter in the main menu's showcase (Q3ShowcaseStage: the
## dark red mist, embers, key and rim lights, the drifting camera), centered for the trailer and
## playing its taunt (TORSO_GESTURE) at set moments. Windowed (needs the GPU);
## tools/trailer/capture_remaster.py runs it from tools/trailer/showcase.json:
##   godot --path . --fixed-fps 60 res://tests/trailer_showcase.tscn -- model=sarge framing=1
##     seconds=5 gesture=1.2 [yaw=-20] [closer=1.0] [seed=1] [shell=quad] [name=sarge_taunt]
##     out=C:/x/sarge_taunt.mp4 ffmpeg=C:/x/ffmpeg.exe [preroll=120]
## framing: Q3ShowcaseStage.FRAMINGS (0 three-quarter, 1 low hero, 2 close, 3 wide). gesture: seconds
## into the shot, comma-separated. yaw: the fighter's turn at the start, in degrees (it keeps
## turning slowly, as in the menu). The weapon is the stage's pick from `seed`; no power-up shell
## unless shell= names one. tests/trailer_capture.gd records it.

const FPS := 60.0
const Capture := preload("res://tests/trailer_capture.gd")

var model := "sarge"
var framing := 0
var seconds := 5.0
var gestures: PackedFloat64Array = [1.2]
var yaw := 0.0
var closer := 1.0
var shot_seed := 1   ## seeds the stage's pick (the weapon)
var shell := ""
var _rec: Capture
var _stage: Q3ShowcaseStage
var _stand_at := -1   ## recorded frame at which the taunt ends


func _ready() -> void:
	var tag := ""
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			match kv[0]:
				"model": model = kv[1]
				"framing": framing = int(kv[1])
				"seconds": seconds = float(kv[1])
				"gesture": gestures = PackedFloat64Array(kv[1].split_floats(",", false))
				"yaw": yaw = float(kv[1])
				"closer": closer = float(kv[1])
				"seed": shot_seed = int(kv[1])
				"shell": shell = kv[1]
				"name": tag = kv[1]
	if not model in Q3.characters:
		push_error("trailer_showcase: no character %s" % model)
		get_tree().quit(1)
		return
	Capture.quality()
	_rec = Capture.new()
	add_child(_rec)
	_rec.read_args()
	_rec.tag = tag if tag != "" else model
	_rec.frames = roundi(seconds * FPS)
	var vp := _rec.make_viewport()
	Session.set_meta("first_subject", "char:" + model)
	_stage = Q3ShowcaseStage.new()
	_stage.locked = true
	_stage.centered = true
	_stage.fixed_framing = clampi(framing, 0, Q3ShowcaseStage.FRAMINGS.size() - 1)
	vp.add_child(_stage)
	Session.remove_meta("first_subject")
	# rebuilt with a seeded pick (weapon), the same on every take
	var rng := RandomNumberGenerator.new()
	rng.seed = hash(model) + shot_seed
	_stage.set("_rng", rng)
	_stage.show_subject("char:" + model, true)
	for m in _stage.subject.find_children("*", "MeshInstance3D", true, false):
		(m as MeshInstance3D).material_overlay = Q3FX.shell_material(shell) if shell != "" else null
	_stage.subject.rotation.y = deg_to_rad(yaw)
	_stage.call("_next_framing", true)
	_stage.set("_closer", closer)
	_stage.set("_closer_goal", closer)
	_stage.set("_gesture_in", 1e9)   # taunts only when the shot says
	if not _rec.begin():
		get_tree().quit(1)


func _process(_delta: float) -> void:
	if _rec == null or _stage == null:
		return
	var f := _rec.tick()
	if f < 0:
		_stage.subject.rotation.y = deg_to_rad(yaw)   # holds its pose through the preroll
		return
	var ch := _stage.character()
	if ch == null or ch.player == null or not ch.player.has_animation("TORSO_GESTURE"):
		return
	for g in gestures:
		if f == roundi(g * FPS):
			ch.play_torso("TORSO_GESTURE")
			_stand_at = f + roundi(ch.player.get_animation("TORSO_GESTURE").length * FPS)
	if f == _stand_at:
		ch.play_torso("TORSO_STAND")
