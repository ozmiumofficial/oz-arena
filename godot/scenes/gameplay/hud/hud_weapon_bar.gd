class_name Q3HudWeaponBar
extends Control
## Every weapon you hold along the bottom (Settings > HUD > Ammo bar): its icon and ammo, the held
## one raised and underlined in red, empty ones dimmed with Quake 3's no-ammo mark. The classic
## preset shows it only for a moment after switching, as Quake 3's weapon select does.

const S := preload("res://scenes/gameplay/hud/hud_style.gd")

var hud: Q3Hud
var owned := 0          ## STAT_WEAPONS bits
var ammo: PackedInt32Array = []
var weapon := 0
var fade_out := false   ## classic: show for 1.4 s after a change (WEAPON_SELECT_TIME)
var _shown_t := 99.0
var _raise: Dictionary = {}


func set_values(bits: int, a: PackedInt32Array, w: int) -> void:
	if w != weapon or bits != owned:
		_shown_t = 0.0
	owned = bits
	ammo = a
	weapon = w


func _process(delta: float) -> void:
	_shown_t += delta
	for i in range(1, 10):
		var target := 1.0 if i == weapon else 0.0
		_raise[i] = move_toward(float(_raise.get(i, 0.0)), target, delta * 8.0)
	queue_redraw()


func _draw() -> void:
	var u := hud.u
	var alpha := 1.0
	if fade_out:
		alpha = clampf((1.4 - _shown_t) / 0.3, 0.0, 1.0)
		if alpha <= 0.0:
			return
	var list: Array[int] = []
	for i in range(1, 10):
		if owned & (1 << i):
			list.append(i)
	if list.is_empty():
		return
	var cell := Vector2(58, 64) * u
	var gap := 6.0 * u
	var total := list.size() * cell.x + (list.size() - 1) * gap
	var x := (size.x - total) * 0.5
	for w in list:
		var k: float = _raise.get(w, 0.0)
		var r := Rect2(Vector2(x, size.y - cell.y - k * 8 * u), cell)
		var held := w == weapon
		S.panel(self, r, Color(S.GLASS, S.GLASS.a * alpha * (1.0 if held else 0.75)))
		var empty := w > 1 and w < ammo.size() and ammo[w] == 0
		var tex := S.weapon_icon(w)
		var ir := Rect2(r.position + Vector2(cell.x * 0.5 - 16 * u, 7 * u), Vector2.ONE * 32 * u)
		if tex:
			draw_texture_rect(tex, ir, false, Color(1, 1, 1, alpha * (0.35 if empty else (1.0 if held else 0.7))))
		if empty:
			var na := S.icon("noammo")
			if na:
				draw_texture_rect(na, ir, false, Color(1, 1, 1, alpha * 0.8))
		var count := "" if w == 1 or w >= ammo.size() or ammo[w] < 0 else str(ammo[w])
		var c := S.INK_HOT if held else S.DIM
		S.text(self, S.num_medium(), Vector2(r.get_center().x, r.end.y - 8 * u), count, int(17 * u), Color(c, alpha), HORIZONTAL_ALIGNMENT_CENTER)
		if held:
			S.bar(self, Rect2(Vector2(r.position.x + 4 * u, r.end.y + 3 * u), Vector2(cell.x - 8 * u, 3 * u)), 0.0, 1.0, Color(S.RED, alpha))
		x += cell.x + gap
