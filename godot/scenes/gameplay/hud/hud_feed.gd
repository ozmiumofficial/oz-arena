class_name Q3HudFeed
extends Control
## The kill feed, top right (Settings > HUD > Kill feed): "attacker [weapon] victim" for every
## EV_OBITUARY, newest at the top, each row for 6 s. The weapon is its Quake 3 icon; deaths with
## no weapon (lava, falling, crushing ...) show a short tag instead. You are amber; in team modes
## names take their team's color.

const S := preload("res://scenes/gameplay/hud/hud_style.gd")
const LIFE := 6.0
const MAX_ROWS := 6
## means of death -> weapon number for its icon (bg_public.h meansOfDeath_t)
const MOD_WEAPON := {1: 3, 2: 1, 3: 2, 4: 4, 5: 4, 6: 5, 7: 5, 8: 8, 9: 8, 10: 7, 11: 6, 12: 9, 13: 9, 23: 10}
const MOD_TAG := {0: "DIED", 14: "DROWNED", 15: "SLIME", 16: "LAVA", 17: "CRUSHED", 18: "TELEFRAG", 19: "FELL",
	20: "SUICIDE", 21: "LASER", 22: "KILLED"}

var hud: Q3Hud
var _rows: Array[Dictionary] = []


## attacker: "" for the world or a suicide. *_team: 0 none, 1 red, 2 blue. *_me: the local player.
func add(attacker: String, victim: String, mod: int, attacker_me: bool, victim_me: bool, attacker_team := 0, victim_team := 0) -> void:
	_rows.push_front({"a": attacker, "v": victim, "mod": mod, "am": attacker_me, "vm": victim_me,
		"at": attacker_team, "vt": victim_team, "t": 0.0})
	while _rows.size() > MAX_ROWS:
		_rows.pop_back()


func _process(delta: float) -> void:
	for r in _rows:
		r["t"] = float(r["t"]) + delta
	_rows = _rows.filter(func(r): return float(r["t"]) < LIFE)
	queue_redraw()


func _name_color(me: bool, team: int) -> Color:
	if me:
		return S.AMBER
	if team == 1:
		return S.TEAM_RED
	if team == 2:
		return S.TEAM_BLUE
	return S.INK


func _draw() -> void:
	var u := hud.u
	var h := 34.0 * u
	var y := 0.0
	var fs := int(19 * u)
	var font := S.num_medium()
	for row in _rows:
		var t: float = row["t"]
		var alpha := clampf((LIFE - t) / 0.6, 0.0, 1.0) * clampf(t / 0.12, 0.0, 1.0)
		var slide := (1.0 - clampf(t / 0.18, 0.0, 1.0)) * 40.0 * u
		var a: String = row["a"]
		var v: String = row["v"]
		var mod: int = row["mod"]
		var icon_w := 30.0 * u
		var tag: String = "" if MOD_WEAPON.has(mod) else String(MOD_TAG.get(mod, "KILLED"))
		var mid_w := icon_w if tag == "" else S.text_width(S.label(), tag, int(13 * u)) + 6 * u
		var aw := S.text_width(font, a, fs) if a != "" else 0.0
		var vw := S.text_width(font, v, fs)
		var gap := 10.0 * u
		var w := aw + (gap if a != "" else 0.0) + mid_w + gap + vw + 36.0 * u
		var r := Rect2(Vector2(size.x - w + slide, y), Vector2(w, h))
		var involved: bool = row["am"] or row["vm"]
		S.panel(self, r, Color(S.GLASS, S.GLASS.a * alpha * (1.0 if involved else 0.8)))
		if involved:
			S.bar(self, Rect2(r.position + Vector2(0, h - 3 * u), Vector2(r.size.x, 3 * u)), 0.0, 1.0,
				Color(S.AMBER if row["am"] else S.RED, 0.8 * alpha))
		var x := r.position.x + 18.0 * u
		var base := y + h * 0.5 + fs * 0.36
		if a != "":
			S.text(self, font, Vector2(x, base), a, fs, Color(_name_color(row["am"], row["at"]), alpha))
			x += aw + gap
		if tag == "":
			var tex := S.weapon_icon(int(MOD_WEAPON[mod]))
			if tex:
				draw_texture_rect(tex, Rect2(Vector2(x, y + (h - icon_w) * 0.5), Vector2.ONE * icon_w), false, Color(1, 1, 1, alpha))
		else:
			S.text(self, S.label(), Vector2(x, y + h * 0.5 + 5 * u), tag, int(13 * u), Color(S.RED, alpha))
		x += mid_w + gap
		S.text(self, font, Vector2(x, base), v, fs, Color(_name_color(row["vm"], row["vt"]), alpha))
		y += h + 5.0 * u
