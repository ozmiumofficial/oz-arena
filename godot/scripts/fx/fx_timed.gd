class_name Q3FxTimed
extends Node3D
## Runs one effect over its life: shader "progress" 0..1, light fade, decal fade, then frees
## itself. Particles and decals can outlive the flash through `linger`.

var life := 1.0                       ## seconds the sprite/beam runs
var linger := 0.0                     ## extra seconds before freeing (smoke, sparks, decals)
var materials: Array[ShaderMaterial] = []
var lights: Array[Light3D] = []
var light_energy: Array[float] = []
var light_range: Array[float] = []
var light_hold := false               ## CG_AddExplosion: full for the first half, then shrinks to 0
var decals: Array[Decal] = []
var decal_fade := 1.0                 ## seconds of fade at the end of a decal's life
var t := 0.0


func add_light(l: Light3D) -> void:
	lights.append(l)
	light_energy.append(l.light_energy)
	light_range.append(l.omni_range if l is OmniLight3D else 0.0)


func _process(delta: float) -> void:
	t += delta
	var p := clampf(t / maxf(life, 0.0001), 0.0, 1.0)
	for m in materials:
		m.set_shader_parameter("progress", p)
	for i in lights.size():
		if light_hold:
			var k := 1.0 if p < 0.5 else 1.0 - (p - 0.5) * 2.0
			lights[i].light_energy = light_energy[i] * k
			if lights[i] is OmniLight3D:
				(lights[i] as OmniLight3D).omni_range = maxf(light_range[i] * k, 0.01)
		else:
			lights[i].light_energy = light_energy[i] * (1.0 - p) * (1.0 - p)
	var end := life + linger
	for d in decals:
		if is_instance_valid(d):
			d.modulate.a = clampf((end - t) / decal_fade, 0.0, 1.0)
	if t >= end:
		queue_free()
