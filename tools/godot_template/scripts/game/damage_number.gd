class_name Q3DamageNumber
extends Label3D
## A damage number over a player you hit (the "dmg" server command). Hits on the same player close
## together add up instead of spawning new numbers: each one re-launches the same label off the
## player's head with the running total, bigger and hotter as it climbs (white, yellow, orange,
## red), and it fades once the hits stop.

const COMBO_TIME := 1.0     ## seconds between hits that still add to the same number
const FADE := 0.35
const GRAVITY := 9.0

var total := 0
var _vel := Vector3.ZERO
var _since := 0.0


func _init() -> void:
	billboard = BaseMaterial3D.BILLBOARD_ENABLED
	no_depth_test = true
	fixed_size = true
	pixel_size = 0.0011
	outline_size = 14
	outline_modulate = Color(0, 0, 0, 0.85)
	font_size = 44


## One hit of `amount` from a head position: adds up and pops off again.
func hit(amount: int, at: Vector3) -> void:
	total += amount
	text = str(total)
	global_position = at
	_vel = Vector3(randf_range(-1.2, 1.2), 3.2, randf_range(-1.2, 1.2))
	_since = 0.0
	var heat := clampf(total / 150.0, 0.0, 1.0)
	modulate = Color.WHITE.lerp(Color(1.0, 0.85, 0.2), clampf(heat * 3.0, 0.0, 1.0)).lerp(Color(1.0, 0.45, 0.1), clampf(heat * 2.0 - 0.5, 0.0, 1.0)).lerp(Color(1.0, 0.12, 0.08), clampf(heat * 2.0 - 1.0, 0.0, 1.0))
	font_size = 40 + int(minf(total, 200.0) * 0.18)
	scale = Vector3.ONE * 1.45
	create_tween().tween_property(self, "scale", Vector3.ONE, 0.18).set_trans(Tween.TRANS_BACK).set_ease(Tween.EASE_OUT)


func combo_open() -> bool:
	return _since < COMBO_TIME


func _process(delta: float) -> void:
	_since += delta
	_vel.y -= GRAVITY * delta
	global_position += _vel * delta
	if _since > COMBO_TIME:
		modulate.a = clampf(1.0 - (_since - COMBO_TIME) / FADE, 0.0, 1.0)
		if modulate.a <= 0.0:
			queue_free()
