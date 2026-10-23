class_name Q3HudCrosshair
extends Control
## The crosshair, from Settings > Crosshair: one of Quake 3's ten (gfx/2d/crosshaira-j) or a vector
## shape (cross, dot, circle, T) with thickness, gap, center dot and outline; per-weapon choices;
## the hit color for a moment after you hit someone; cg_crosshairHealth's color by health; and
## cg_crosshairPulse's swell when you pick something up (ITEM_BLOB_TIME 200 ms).

const S := preload("res://scenes/gameplay/hud/hud_style.gd")
const WEAPON_KEYS := ["", "gauntlet", "machinegun", "shotgun", "grenadel", "rocketl", "lightning", "railgun", "plasma", "bfg", ""]
const Q3_SHAPES := "abcdefghij"     ## Quake 3's own crosshairs; other ids are vector shapes
const HIT_TIME := 0.22
const PULSE_TIME := 0.2

var weapon := 0
var preview_weapon := 0       ## the crosshair editor's weapon tab (0: the weapon in hand)
var health := 100
var _hit := 99.0
var _pulse := 99.0
var _tex: Dictionary = {}


func hit() -> void:
	_hit = 0.0


func pulse() -> void:
	_pulse = 0.0


func _process(delta: float) -> void:
	_hit += delta
	_pulse += delta
	queue_redraw()


func _shape() -> String:
	var shape := String(Settings.get_value("crosshair/shape"))
	var held := preview_weapon if preview_weapon > 0 else weapon
	if Settings.get_value("crosshair/per_weapon") and held > 0 and held < WEAPON_KEYS.size() and WEAPON_KEYS[held] != "":
		var w := String(Settings.get_value("crosshair/weapon_" + WEAPON_KEYS[held]))
		if w != "default" and w != "":
			shape = w
	return shape


## CG_ColorForHealth: red to yellow to white as health (plus armor) rises.
func _health_color() -> Color:
	var h := float(health)
	if h <= 0:
		return Color(0, 0, 0)
	var c := Color(1, 1, 1)
	if h >= 100:
		c.b = 1.0
	elif h < 66:
		c.b = 0.0
	else:
		c.b = (h - 66) / 33.0
	if h > 60:
		c.g = 1.0
	elif h < 30:
		c.g = 0.0
	else:
		c.g = (h - 30) / 30.0
	return c


func _draw() -> void:
	var shape := _shape()
	if shape == "none" or shape == "":
		return
	var vh := get_viewport_rect().size.y
	var k := vh / 1080.0
	var sz := float(Settings.get_value("crosshair/size")) * k
	if _pulse < PULSE_TIME:
		sz *= 1.0 + _pulse / PULSE_TIME
	var color: Color = Settings.get_value("crosshair/color")
	if Settings.get_value("crosshair/health_color"):
		color = _health_color()
	if _hit < HIT_TIME:
		color = color.lerp(Settings.get_value("crosshair/hit_color"), 1.0 - _hit / HIT_TIME)
	var c := size * 0.5
	var outline: bool = Settings.get_value("crosshair/outline")
	if shape.length() == 1 and Q3_SHAPES.contains(shape):
		if not _tex.has(shape):
			var p := Q3.LIB + "ui/crosshairs/crosshair_%s.png" % shape
			_tex[shape] = load(p) if ResourceLoader.exists(p) else null
		var tex: Texture2D = _tex[shape]
		if tex == null:
			return
		var r := Rect2(c - Vector2(sz, sz) * 0.5, Vector2(sz, sz))
		if outline:
			for o in [Vector2(1, 0), Vector2(-1, 0), Vector2(0, 1), Vector2(0, -1)]:
				draw_texture_rect(tex, Rect2(r.position + o * maxf(k, 1.0), r.size), false, Color(0, 0, 0, 0.55 * color.a))
		draw_texture_rect(tex, r, false, color)
		return
	var t := maxf(float(Settings.get_value("crosshair/thickness")) * k, 1.0)
	var gap := float(Settings.get_value("crosshair/gap")) * k
	var arm := sz * 0.5
	var lines: Array = []
	match shape:
		"cross":
			lines = [[c + Vector2(gap, 0), c + Vector2(arm, 0)], [c - Vector2(gap, 0), c - Vector2(arm, 0)],
				[c + Vector2(0, gap), c + Vector2(0, arm)], [c - Vector2(0, gap), c - Vector2(0, arm)]]
		"t":
			lines = [[c + Vector2(gap, 0), c + Vector2(arm, 0)], [c - Vector2(gap, 0), c - Vector2(arm, 0)],
				[c + Vector2(0, gap), c + Vector2(0, arm)]]
	if outline:
		for l in lines:
			draw_line(l[0], l[1], Color(0, 0, 0, 0.6 * color.a), t + 2.0 * maxf(k, 1.0))
		if shape == "circle":
			draw_arc(c, arm, 0, TAU, 48, Color(0, 0, 0, 0.6 * color.a), t + 2.0 * maxf(k, 1.0), true)
	for l in lines:
		draw_line(l[0], l[1], color, t)
	if shape == "circle":
		draw_arc(c, arm, 0, TAU, 48, color, t, true)
	if shape == "dot" or Settings.get_value("crosshair/dot"):
		var d := maxf(t, 2.0 * k) * (1.6 if shape == "dot" else 1.0)
		if outline:
			draw_circle(c, d + maxf(k, 1.0), Color(0, 0, 0, 0.6 * color.a))
		draw_circle(c, d, color)
