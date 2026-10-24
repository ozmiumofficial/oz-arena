class_name Q3ChaseCam
extends RefCounted
## A third-person camera that moves only the camera, never the player (cg_view.c
## CG_OffsetThirdPersonView): from a focus point (the eye, or a body) it backs off along the view
## direction and up, and a box trace through the world (MASK_SOLID) pulls it in where a wall is in
## the way. Pulling in is immediate; easing back out takes a moment, so it doesn't pump in corridors.
## The replay viewer's chase and orbit cameras, spectators' chase cam (R5) and the death cam (R6)
## all use it. Points are id units in, a Godot transform out.

const MASK_SOLID := 1          ## CONTENTS_SOLID
const BOX := 8.0               ## the camera's own size, so it never sits in a wall's surface
const OUT_RATE := 4.0          ## how fast it eases back out (1/s)

var distance := 110.0          ## id units behind the focus
var height := 20.0             ## id units above it
var _dist := -1.0


## The camera for `focus` (id units) seen from `yaw`/`pitch` degrees (id angles: pitch positive
## looks down). `look_ahead` aims past the focus along the view (0 looks straight at it).
## `pass_entity` is skipped by the trace (the watched player).
func place(feed: Q3Feed, focus: Vector3, yaw: float, pitch: float, delta: float, pass_entity: int, look_ahead := 64.0) -> Transform3D:
	var fwd := Q3Client.forward_of(Vector3(pitch, yaw, 0.0))
	var want := focus - fwd * distance + Vector3(0, 0, height)
	var reach := (want - focus).length()
	var tr := feed.trace(focus, want, Vector3(-BOX, -BOX, -BOX), Vector3(BOX, BOX, BOX), pass_entity, MASK_SOLID)
	var hit := reach
	if not tr.is_empty():
		hit = reach * float(tr["fraction"])
	if _dist < 0.0 or hit < _dist:
		_dist = hit
	else:
		_dist = lerpf(_dist, hit, 1.0 - exp(-OUT_RATE * delta))
	var pos := focus + (want - focus).normalized() * _dist if reach > 0.0 else focus
	var target := focus + fwd * look_ahead
	return look(pos, target)


## Forgets the eased distance (a new subject, or a jump in a replay).
func reset() -> void:
	_dist = -1.0


## A Godot transform at `from` looking at `to` (both id units).
static func look(from: Vector3, to: Vector3) -> Transform3D:
	var a := Q3.q3_to_godot(from)
	var b := Q3.q3_to_godot(to)
	var t := Transform3D(Basis.IDENTITY, a)
	if a.distance_to(b) > 0.001:
		var up := Vector3.UP if absf((b - a).normalized().y) < 0.99 else Vector3.BACK
		t = t.looking_at(b, up)
	return t
