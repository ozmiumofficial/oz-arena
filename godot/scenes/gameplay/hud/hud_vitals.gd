class_name Q3HudVitals
extends Control
## Health and armor, bottom left: a vector icon, the number and a slanted segmented bar each.
## Health past 100 refills the bar in a cooler tone (mega and regeneration), a red ghost trails
## every loss for a moment, and 25 or less pulses red (Quake 3 flashes the number then too).
## Armor is amber, red once it drops under 50 like the armor shell. `compact` drops the bars.

const S := preload("res://scenes/gameplay/hud/hud_style.gd")

var hud: Q3Hud
var compact := false
var health := 100
var armor := 0
var _ghost_h := 100.0
var _ghost_a := 0.0
var _t := 0.0


func set_values(h: int, a: int) -> void:
	if h > health:
		_ghost_h = float(h)
	if a > armor:
		_ghost_a = float(a)
	health = h
	armor = a


func _process(delta: float) -> void:
	_t += delta
	_ghost_h = move_toward(_ghost_h, float(health), delta * 160.0)
	_ghost_a = move_toward(_ghost_a, float(armor), delta * 160.0)
	queue_redraw()


func _draw() -> void:
	var u := hud.u
	var block := Vector2(250, 92) * u
	if compact:
		block = Vector2(190, 66) * u
	var low := health <= 25 and health > 0
	var pulse := 0.5 + 0.5 * sin(_t * TAU * 2.0)
	var hc := S.MEGA if health > 100 else (S.RED.lerp(S.GLOW, pulse) if low else S.INK_HOT)
	var ac := S.RED if armor > 0 and armor < 50 else S.AMBER
	_block(Rect2(Vector2.ZERO, block), true, health, hc, _ghost_h, low)
	_block(Rect2(Vector2(block.x + 14 * u, 0), block), false, armor, ac, _ghost_a, false)


func _block(r: Rect2, is_health: bool, value: int, color: Color, ghost: float, low: bool) -> void:
	var u := hud.u
	if not compact:
		S.panel(self, r, S.GLASS, S.RED if is_health else S.AMBER, 3.0 * u)
	var pad := 14.0 * u
	var icon := Rect2(r.position + Vector2(pad + 6 * u, r.size.y * (0.2 if not compact else 0.22)), Vector2.ONE * (30.0 if compact else 34.0) * u)
	var dim := value <= 0 and not is_health
	var ic := color if not dim else S.FAINT
	if is_health:
		S.cross_icon(self, icon, ic)
	else:
		S.shield_icon(self, icon, ic)
	var size := int((46.0 if compact else 56.0) * u)
	var x := icon.end.x + 12 * u
	S.text(self, S.num(), Vector2(x, icon.position.y + icon.size.y * 0.5 + size * 0.36), str(maxi(value, 0)), size,
		color if not dim else S.FAINT)
	if low:
		# Quake 3 flashes low health; the HUD adds a soft glow behind the number
		var w := S.text_width(S.num(), str(value), size)
		draw_rect(Rect2(x - 6 * u, icon.position.y - 4 * u, w + 12 * u, icon.size.y + 8 * u), Color(S.RED, 0.12 + 0.1 * sin(_t * TAU * 2.0)))
	if compact:
		return
	# the bar: 10 segments per 100, the second hundred over the first in a cooler tone
	var bar := Rect2(r.position + Vector2(pad, r.size.y - 22 * u), Vector2(r.size.x - pad * 2.0, 9 * u))
	S.bar(self, bar, 0.0, 1.0, Color(1, 1, 1, 0.07))
	var base := clampf(value / 100.0, 0.0, 1.0)
	var over := clampf((value - 100) / 100.0, 0.0, 1.0)
	var g := clampf(ghost / 100.0, 0.0, 1.0)
	if g > base:
		S.bar(self, bar, base, g, Color(S.RED, 0.75))
	S.bar(self, bar, 0.0, base, Color(color, 0.92) if value <= 100 else Color(S.INK_HOT, 0.9))
	if over > 0.0:
		var top := Rect2(bar.position + Vector2(0, -4 * u), Vector2(bar.size.x, 4 * u))
		S.bar(self, top, 0.0, over, (S.MEGA if is_health else S.AMBER.lightened(0.3)))
	# segment gaps
	var s := bar.size.y * S.SKEW
	for i in range(1, 10):
		var fx := bar.position.x + (i / 10.0) * (bar.size.x - s)
		draw_line(Vector2(fx + s, bar.position.y - 4 * u), Vector2(fx, bar.end.y), Color(0, 0, 0, 0.55), 2.0 * u)
