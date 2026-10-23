class_name Q3HudSpeedometer
extends Control
## The speedometer (Settings > HUD > Speedometer): horizontal speed in units per second, Quake 3's
## own measure.
##   arc      a sweeping gauge, 0-1000 ups, a bold tick at 320 (the ground run speed, g_speed). The
##            fill heats from ink through amber (past run speed) to red (600+) and glows while you
##            gain speed; a peak marker holds your top speed for a moment; a faint trace behind the
##            digits shows the last two seconds. Under it, the strafe-jump readout: your speed at
##            each jump's takeoff, and what you gained or lost against the jump before.
##   compact  the digits and a thin bar.

const S := preload("res://scenes/gameplay/hud/hud_style.gd")
const MAX_UPS := 1000.0
const RUN := 320.0
const FAST := 600.0
const A0 := PI + 0.42                ## arc start (lower left), screen angles grow clockwise
const A1 := TAU - 0.42
const HISTORY := 120                 ## samples (two seconds at 60 Hz)

var hud: Q3Hud
var style := "arc"
var speed := 0.0                     ## shown speed (eased)
var _target := 0.0
var _peak := 0.0
var _peak_hold := 0.0
var _gaining := 0.0
var _history: PackedFloat32Array = []
var _jump := -1.0                    ## last takeoff speed
var _jump_delta := 0.0
var _jump_t := 99.0


func set_speed(ups: float) -> void:
	_target = ups


## EV_JUMP: the takeoff speed, compared with the previous takeoff in this chain.
func jumped(ups: float) -> void:
	_jump_delta = ups - _jump if _jump >= 0.0 and _jump_t < 1.5 else 0.0
	_jump = ups
	_jump_t = 0.0


func _process(delta: float) -> void:
	var before := speed
	speed = lerpf(speed, _target, clampf(delta * 18.0, 0.0, 1.0))
	_gaining = clampf(_gaining + (0.6 if speed > before + 0.5 and speed > RUN else -1.2) * delta * 4.0, 0.0, 1.0)
	if speed >= _peak:
		_peak = speed
		_peak_hold = 1.2
	else:
		_peak_hold -= delta
		if _peak_hold <= 0.0:
			_peak = maxf(speed, _peak - 450.0 * delta)
	_history.append(speed)
	while _history.size() > HISTORY:
		_history.remove_at(0)
	_jump_t += delta
	queue_redraw()


static func heat(ups: float) -> Color:
	if ups <= RUN:
		return S.INK_HOT
	if ups <= FAST:
		return S.INK_HOT.lerp(S.AMBER, (ups - RUN) / (FAST - RUN))
	return S.AMBER.lerp(S.GLOW, clampf((ups - FAST) / 300.0, 0.0, 1.0))


func _angle(ups: float) -> float:
	return lerpf(A0, A1, clampf(ups / MAX_UPS, 0.0, 1.0))


func _draw() -> void:
	if style == "compact":
		_draw_compact()
	else:
		_draw_arc()


func _draw_arc() -> void:
	var u := hud.u
	var c := Vector2(size.x * 0.5, size.y - 30 * u)
	var r := 92.0 * u
	var th := 7.0 * u
	# backdrop: a glass band under the arc and a plate under the digits
	_band(c, r - th * 2.0, r + th * 2.6, Color(0.03, 0.025, 0.02, 0.55))
	draw_arc(c, r + th * 2.6, A0, A1, 48, S.GLASS_EDGE, 1.0, true)
	# track and ticks
	draw_arc(c, r, A0, A1, 64, Color(1, 1, 1, 0.13), th, true)
	for v in range(0, int(MAX_UPS) + 1, 100):
		var a := _angle(v)
		var d := Vector2(cos(a), sin(a))
		var major := v % 500 == 0
		draw_line(c + d * (r + th * 0.75), c + d * (r + th * (2.1 if major else 1.45)), Color(S.INK, 0.75 if major else 0.4), 1.5 * u, true)
	# the run speed mark
	var ar := _angle(RUN)
	var dr := Vector2(cos(ar), sin(ar))
	draw_line(c + dr * (r - th * 0.9), c + dr * (r + th * 1.9), Color(S.RED, 0.9), 2.5 * u, true)
	S.text(self, S.num_medium(), c + dr * (r + th * 4.0) + Vector2(0, 5 * u), "320", int(12 * u), Color(S.INK, 0.8), HORIZONTAL_ALIGNMENT_CENTER)
	# the fill, heating with speed
	var steps := 48
	var f := clampf(speed / MAX_UPS, 0.0, 1.0)
	for i in steps:
		var t0 := float(i) / steps
		if t0 >= f:
			break
		var t1 := minf(float(i + 1) / steps, f)
		var col := heat(t1 * MAX_UPS)
		if _gaining > 0.01 and t1 * MAX_UPS > RUN:
			draw_arc(c, r, lerpf(A0, A1, t0), lerpf(A0, A1, t1), 4, Color(col, 0.16 * _gaining), th * 3.2, true)
		draw_arc(c, r, lerpf(A0, A1, t0), lerpf(A0, A1, t1), 4, col, th, true)
	# the tip
	var at := _angle(speed)
	var dt := Vector2(cos(at), sin(at))
	draw_line(c + dt * (r - th * 1.1), c + dt * (r + th * 1.1), Color.WHITE, 3.0 * u, true)
	# peak hold
	if _peak > speed + 8.0:
		var ap := _angle(_peak)
		var dp := Vector2(cos(ap), sin(ap))
		var tip := c + dp * (r + th * 0.9)
		var side := Vector2(-dp.y, dp.x) * 5.0 * u
		draw_colored_polygon(PackedVector2Array([tip, tip + dp * 9 * u + side, tip + dp * 9 * u - side]), Color(heat(_peak), 0.9))
	# the last two seconds, faint, behind the digits
	if _history.size() > 2:
		var pts := PackedVector2Array()
		var w := r * 1.2
		var h := r * 0.4
		var base := c + Vector2(-w * 0.5, -r * 0.62 + h)
		for i in _history.size():
			pts.append(base + Vector2(w * i / float(HISTORY - 1), -h * clampf(_history[i] / MAX_UPS, 0.0, 1.0)))
		draw_polyline(pts, Color(heat(speed), 0.22), 1.5 * u, true)
	# digits
	var big := int(40 * u)
	S.text(self, S.num(), c + Vector2(0, -r * 0.18), str(roundi(speed)), big, heat(speed), HORIZONTAL_ALIGNMENT_CENTER)
	S.text(self, S.label(), c + Vector2(0, -r * 0.18 + 17 * u), "UPS", int(12 * u), Color(S.INK, 0.6), HORIZONTAL_ALIGNMENT_CENTER)
	# the strafe-jump readout
	if _jump >= 0.0:
		var alpha := clampf(1.0 - (_jump_t - 2.5) / 0.8, 0.0, 1.0)
		if alpha > 0.0:
			var y := c.y + 22 * u
			S.text(self, S.label(), Vector2(c.x - 12 * u, y), "JUMP", int(12 * u), Color(S.DIM, alpha), HORIZONTAL_ALIGNMENT_RIGHT)
			S.text(self, S.num_medium(), Vector2(c.x - 64 * u, y), str(roundi(_jump)), int(17 * u), Color(heat(_jump), alpha), HORIZONTAL_ALIGNMENT_RIGHT)
			if absf(_jump_delta) >= 1.0:
				var gain := _jump_delta > 0.0
				S.text(self, S.num_medium(), Vector2(c.x + 12 * u, y), ("+%d" if gain else "%d") % roundi(_jump_delta), int(17 * u),
					Color(S.MEGA if gain else S.RED, alpha))


func _draw_compact() -> void:
	var u := hud.u
	var c := Vector2(size.x * 0.5, size.y - 18 * u)
	S.text(self, S.num(), c, str(roundi(speed)), int(30 * u), heat(speed), HORIZONTAL_ALIGNMENT_CENTER)
	var bar := Rect2(Vector2(size.x * 0.5 - 60 * u, size.y - 10 * u), Vector2(120 * u, 4 * u))
	draw_rect(bar, Color(1, 1, 1, 0.1))
	draw_rect(Rect2(bar.position, Vector2(bar.size.x * clampf(speed / MAX_UPS, 0.0, 1.0), bar.size.y)), heat(speed))
	var rx := bar.position.x + bar.size.x * RUN / MAX_UPS
	draw_line(Vector2(rx, bar.position.y - 3 * u), Vector2(rx, bar.end.y + 3 * u), S.RED, 2.0 * u)


## A filled band (annulus sector) between two radii over the gauge's sweep.
func _band(c: Vector2, r0: float, r1: float, color: Color) -> void:
	var pts := PackedVector2Array()
	var n := 40
	for i in n + 1:
		var a := lerpf(A0, A1, float(i) / n)
		pts.append(c + Vector2(cos(a), sin(a)) * r1)
	for i in range(n, -1, -1):
		var a := lerpf(A0, A1, float(i) / n)
		pts.append(c + Vector2(cos(a), sin(a)) * r0)
	draw_colored_polygon(pts, color)
