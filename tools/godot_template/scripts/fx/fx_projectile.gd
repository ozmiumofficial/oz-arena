extends Node3D
## A flying projectile for demos: moves from A to B at `speed` (m/s), carries its model or
## sprite, light and trail, and calls Q3FX.explosion() on arrival.

var target := Vector3.ZERO
var speed := 900.0 * Q3.UNIT
var explode_kind := "rocket"
var trail: GPUParticles3D


func _process(delta: float) -> void:
	var to := target - global_position
	var step := speed * delta
	if to.length() <= step:
		var parent := get_parent()
		Q3FX.explosion(parent, explode_kind, target, -to.normalized() if to.length() > 0.001 else Vector3.UP)
		if trail:
			# let the smoke trail finish instead of vanishing with the rocket
			var keep := Q3FxTimed.new()
			parent.add_child(keep)
			keep.global_position = trail.global_position
			trail.emitting = false
			trail.reparent(keep)
			keep.life = 0.0
			keep.linger = trail.lifetime
		queue_free()
		return
	global_position += to.normalized() * step
	if to.length() > 0.001:
		look_at(target, Vector3.UP, true)
