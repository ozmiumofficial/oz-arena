class_name Q3HudAmmo
extends Control
## The held weapon, bottom right: its icon, its ammo, its name under the panel, and the holdable
## item (teleporter, medkit) beside it. As in cg_draw.c the count goes gray while a slow weapon
## reloads (weaponTime over 100 ms); the remaster also turns it red when about two seconds of
## fire are left and blinks it on every shot.

const S := preload("res://scenes/gameplay/hud/hud_style.gd")
## the remaster's low marks: about two seconds of fire (the gauntlet has no ammo)
const LOW := [0, 0, 20, 3, 3, 3, 30, 3, 20, 3, 0]
const NAMES := ["", "GAUNTLET", "MACHINEGUN", "SHOTGUN", "GRENADE LAUNCHER", "ROCKET LAUNCHER", "LIGHTNING GUN",
	"RAILGUN", "PLASMA GUN", "BFG10K", "GRAPPLING HOOK"]

var hud: Q3Hud
var compact := false
var weapon := 0
var ammo := 0
var holdable := ""     ## icon name of the holdable item, or ""
var reloading := false
var _last_ammo := -1
var _flash := 0.0


func set_values(w: int, a: int, hold: String, reload: bool) -> void:
	reloading = reload
	if w == weapon and a < _last_ammo:
		_flash = 1.0
	weapon = w
	ammo = a
	_last_ammo = a
	holdable = hold


func _process(delta: float) -> void:
	_flash = move_toward(_flash, 0.0, delta * 8.0)
	queue_redraw()


func _draw() -> void:
	var u := hud.u
	if weapon <= 0:
		return
	var r := Rect2(Vector2.ZERO, Vector2(200, 92) * u if not compact else Vector2(160, 66) * u)
	r.position.x = size.x - r.size.x
	if not compact:
		S.panel(self, r, S.GLASS, S.RED, 3.0 * u)
	var ic := S.weapon_icon(weapon)
	var icon_r := Rect2(r.position + Vector2(r.size.x * 0.12, r.size.y * 0.18), Vector2.ONE * (44.0 if not compact else 34.0) * u)
	if ic:
		draw_texture_rect(ic, icon_r, false)
	var infinite := ammo < 0 or weapon == 1
	var low: bool = not infinite and weapon < LOW.size() and ammo <= LOW[weapon]
	var color := S.FAINT if reloading else (S.RED if low else S.INK_HOT)
	color = color.lerp(Color.WHITE, _flash * 0.5)
	var size_px := int((56.0 if not compact else 46.0) * u)
	var txt := "-" if infinite else str(ammo)
	S.text(self, S.num(), Vector2(r.end.x - 22 * u, icon_r.get_center().y + size_px * 0.36), txt, size_px, color, HORIZONTAL_ALIGNMENT_RIGHT)
	if not compact:
		S.text(self, S.label(), Vector2(r.end.x - 22 * u, r.end.y - 10 * u), NAMES[weapon] if weapon < NAMES.size() else "",
			int(15 * u), Color(S.DIM, 0.9), HORIZONTAL_ALIGNMENT_RIGHT)
	if holdable != "":
		var tex := S.icon(holdable)
		var hr := Rect2(Vector2(r.position.x - 64 * u, r.position.y + r.size.y * 0.2), Vector2.ONE * 50 * u)
		S.panel(self, hr.grow(6 * u), S.GLASS)
		if tex:
			draw_texture_rect(tex, hr, false)
