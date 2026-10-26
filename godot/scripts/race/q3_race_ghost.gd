class_name Q3RaceGhost
extends Node3D
## The ghost of your best run (RACE_PLAN.md D4): your model and skin, faded, running the best run
## beside you from each start. Samples come from Q3RaceGhostRecorder's file. It follows the run
## clock (the server time since the timer started), so it is where you were at the same moment.

const E := preload("res://scripts/game/q3_enums.gd")
const FIELDS := Q3RaceGhostRecorder.FIELDS
const FADE := 0.6   ## GeometryInstance3D.transparency

var ms := 0                    ## the best run's time
var samples := PackedFloat32Array()
var model := "sarge"
var skin := "default"
var _ch: Q3Character
var _legs := -1
var _torso := -1


## The map's ghost, or null (none yet, or a file that isn't one).
static func load_for(map: String) -> Q3RaceGhost:
	var path := Q3RaceRecords.ghost_path(map)
	if not FileAccess.file_exists(path):
		return null
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return null
	var raw_size := f.get_32()
	var body := f.get_buffer(f.get_length() - 4)
	if raw_size <= 0 or raw_size > 64 * 1024 * 1024:
		return null
	var v: Variant = bytes_to_var(body.decompress(raw_size, FileAccess.COMPRESSION_ZSTD))
	if not (v is Array) or v.size() != 2 or not (v[0] is Dictionary) or not (v[1] is PackedFloat32Array):
		return null
	var head: Dictionary = v[0]
	if head.get("magic", "") != Q3RaceGhostRecorder.MAGIC or int(head.get("fields", 0)) != FIELDS or (v[1] as PackedFloat32Array).size() < FIELDS * 2:
		return null
	var g := Q3RaceGhost.new()
	g.name = "RaceGhost"
	g.ms = int(head.get("ms", 0))
	g.model = String(head.get("model", "sarge"))
	g.skin = String(head.get("skin", "default"))
	g.samples = v[1]
	return g


func _ready() -> void:
	_ch = Q3Character.new()
	_ch.model = model
	_ch.skin = skin
	_ch.turn = true
	add_child(_ch)
	_ch.bright_skin(Color(0.35, 0.8, 1.0))   # a cool tint, so it reads as a ghost
	_fade()
	visible = false


func _fade() -> void:
	for m in _ch.find_children("*", "MeshInstance3D", true, false):
		(m as MeshInstance3D).transparency = FADE
	_ch.child_entered_tree.connect(func(n: Node) -> void:
		if n is MeshInstance3D:
			(n as MeshInstance3D).transparency = FADE)


func hide_ghost() -> void:
	visible = false


func restart() -> void:
	_legs = -1
	_torso = -1
	if _ch:
		_ch.reset_motion()


## run_ms: the run clock (-1: no run, nothing shown).
func update(run_ms: int) -> void:
	if _ch == null:
		return
	if run_ms < 0 or samples.is_empty():
		visible = false
		return
	var n := samples.size() / FIELDS
	# the sample at or before run_ms (binary search over the times)
	var lo := 0
	var hi := n - 1
	while lo < hi:
		var mid := (lo + hi + 1) / 2
		if samples[mid * FIELDS] <= run_ms:
			lo = mid
		else:
			hi = mid - 1
	var nxt := mini(lo + 1, n - 1)
	var t0 := samples[lo * FIELDS]
	var t1 := samples[nxt * FIELDS]
	var f := 0.0 if t1 <= t0 else clampf((run_ms - t0) / (t1 - t0), 0.0, 1.0)
	# after the end the ghost has finished: it stands at the line for a moment, then goes
	if run_ms > ms + 1500:
		visible = false
		return
	visible = true
	var a := lo * FIELDS
	var b := nxt * FIELDS
	var o0 := Vector3(samples[a + 1], samples[a + 2], samples[a + 3])
	var o1 := Vector3(samples[b + 1], samples[b + 2], samples[b + 3])
	var o := o0.lerp(o1, f)
	global_position = Q3.q3_to_godot(o) + Vector3.DOWN * 24.0 * Q3.UNIT
	_ch.velocity = (o1 - o0) / maxf((t1 - t0) * 0.001, 0.001) if t1 > t0 else Vector3.ZERO
	_ch.aim_pitch = samples[a + 4]
	_ch.aim_yaw = lerp_angle(deg_to_rad(samples[a + 5]), deg_to_rad(samples[b + 5]), f) * 180.0 / PI
	_ch.move_dir = int(samples[a + 8])
	var legs := int(samples[a + 6]) & ~E.ANIM_TOGGLEBIT
	var torso := int(samples[a + 7]) & ~E.ANIM_TOGGLEBIT
	_ch.aim_center = legs != E.LEGS_IDLE or torso != E.TORSO_STAND
	if int(samples[a + 6]) != _legs:
		_legs = int(samples[a + 6])
		if legs < E.ANIM_NAMES.size():
			_ch.play_legs(E.ANIM_NAMES[legs])
	if int(samples[a + 7]) != _torso:
		_torso = int(samples[a + 7])
		if torso < E.ANIM_NAMES.size() and legs > E.BOTH_DEAD3:
			_ch.play_torso(E.ANIM_NAMES[torso])
