class_name Q3HudStatus
extends Control
## The match status, top center: the clock (counting down when there's a time limit, up otherwise,
## like cg_drawTimer), and the scores as cg_draw.c CG_DrawScores shows them. In free-for-all and
## duel it's your place and score beside the leader's (or the runner-up's when you lead); in team
## modes it's red against blue. The limit sits under the clock.

const S := preload("res://scenes/gameplay/hud/hud_style.gd")

var hud: Q3Hud
var teams := false
var clock := "0:00"
var limit := ""
var my_place := ""
var my_score := 0
var other_name := ""
var other_score := 0
var other_leads := true
var red := 0
var blue := 0
var my_team := 0
var warn := false            ## the last minute: the clock turns red


func _process(_delta: float) -> void:
	queue_redraw()


func _draw() -> void:
	var u := hud.u
	var cx := size.x * 0.5
	var clock_r := Rect2(Vector2(cx - 62 * u, 0), Vector2(124 * u, 46 * u))
	S.panel(self, clock_r, S.GLASS)
	S.text(self, S.num(), Vector2(cx, clock_r.get_center().y + 11 * u), clock, int(31 * u), S.RED if warn else S.INK_HOT,
		HORIZONTAL_ALIGNMENT_CENTER)
	if limit != "":
		S.text(self, S.label(), Vector2(cx, clock_r.end.y + 16 * u), limit, int(12 * u), Color(S.DIM, 0.9), HORIZONTAL_ALIGNMENT_CENTER)
	var bw := 172.0 * u
	var left := Rect2(Vector2(clock_r.position.x - bw - 8 * u, 4 * u), Vector2(bw, 38 * u))
	var right := Rect2(Vector2(clock_r.end.x + 8 * u, 4 * u), Vector2(bw, 38 * u))
	if teams:
		_score_box(left, "RED", red, S.TEAM_RED, my_team == 1)
		_score_box(right, "BLUE", blue, S.TEAM_BLUE, my_team == 2)
	else:
		_score_box(left, my_place.to_upper(), my_score, S.AMBER, true)
		if other_name != "":
			_score_box(right, other_name.to_upper(), other_score, S.INK, false)


func _score_box(r: Rect2, label: String, score: int, color: Color, mine: bool) -> void:
	var u := hud.u
	S.panel(self, r, Color(color.darkened(0.75), 0.7) if mine else S.GLASS)
	S.bar(self, Rect2(Vector2(r.position.x, r.end.y - 3 * u), Vector2(r.size.x, 3 * u)), 0.0, 1.0, Color(color, 0.85))
	S.text(self, S.label(), Vector2(r.position.x + 22 * u, r.get_center().y + 5 * u), label, int(13 * u), Color(color, 0.95),
		HORIZONTAL_ALIGNMENT_LEFT, r.size.x - 80 * u)
	S.text(self, S.num(), Vector2(r.end.x - 18 * u, r.get_center().y + 10 * u), str(score), int(28 * u), S.INK_HOT, HORIZONTAL_ALIGNMENT_RIGHT)
