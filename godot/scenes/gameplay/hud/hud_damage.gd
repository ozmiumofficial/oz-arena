class_name Q3HudDamage
extends Control
## Taking damage (cg_playerstate.c CG_DamageFeedback): an arc around the crosshair pointing at the
## attacker, from the playerState's damage yaw and pitch, and a red flash at the screen edges that
## grows with the damage. Damage with no direction (falling, lava) flashes all round. Health of 25
## or less keeps a slow red pulse at the edges.

const S := preload("res://scenes/gameplay/hud/hud_style.gd")
const LIFE := 1.1

var low_health := false
var _hits: Array[Dictionary] = []    ## {angle (screen radians, 0 = up, clockwise), strength, t, all}
var _flash := 0.0
var _t := 0.0


## angle: the attacker's direction on screen (0 ahead/up, positive to the right), or NAN for none.
func damage(angle: float, count: int) -> void:
	var strength := clampf(count / 50.0, 0.25, 1.0)
	_hits.append({"angle": angle, "s": strength, "t": 0.0})
	while _hits.size() > 8:
		_hits.remove_at(0)
	_flash = maxf(_flash, strength)


func _process(delta: float) -> void:
	_t += delta
	for h in _hits:
		h["t"] = float(h["t"]) + delta
	_hits = _hits.filter(func(h): return float(h["t"]) < LIFE)
	_flash = move_toward(_flash, 0.0, delta * 2.2)
	queue_redraw()


func _draw() -> void:
	var vs := size
	var c := vs * 0.5
	var k := vs.y / 1080.0
	# edge flash and low-health pulse
	var edge := _flash * 0.45
	if low_health:
		edge = maxf(edge, 0.12 + 0.08 * sin(_t * TAU * 1.2))
	if edge > 0.005:
		var band := vs.y * 0.16
		var col := Color(S.RED, edge)
		var clear := Color(S.RED, 0.0)
		draw_polygon(PackedVector2Array([Vector2.ZERO, Vector2(vs.x, 0), Vector2(vs.x, band), Vector2(0, band)]),
			PackedColorArray([col, col, clear, clear]))
		draw_polygon(PackedVector2Array([Vector2(0, vs.y - band), Vector2(vs.x, vs.y - band), vs, Vector2(0, vs.y)]),
			PackedColorArray([clear, clear, col, col]))
		draw_polygon(PackedVector2Array([Vector2.ZERO, Vector2(band, 0), Vector2(band, vs.y), Vector2(0, vs.y)]),
			PackedColorArray([col, clear, clear, col]))
		draw_polygon(PackedVector2Array([Vector2(vs.x - band, 0), Vector2(vs.x, 0), vs, Vector2(vs.x - band, vs.y)]),
			PackedColorArray([clear, col, col, clear]))
	# direction arcs
	var r := 130.0 * k
	for h in _hits:
		var t: float = h["t"]
		var a := clampf(1.0 - t / LIFE, 0.0, 1.0)
		a = a * a * float(h["s"])
		var ang: float = h["angle"]
		if is_nan(ang):
			draw_arc(c, r, 0, TAU, 64, Color(S.RED, a * 0.6), 6.0 * k, true)
			continue
		var mid := ang - PI * 0.5           # screen: 0 = right, so "up" is -PI/2
		var spread := 0.42
		draw_arc(c, r, mid - spread, mid + spread, 20, Color(S.GLOW, a), 9.0 * k, true)
		draw_arc(c, r + 10.0 * k, mid - spread * 0.6, mid + spread * 0.6, 14, Color(S.RED, a * 0.7), 4.0 * k, true)
