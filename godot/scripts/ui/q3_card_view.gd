class_name Q3CardView
extends Control
## A player card drawn natively for the in-game UI (scoreboard rows, results screen): the banner
## art behind, the emblem on the left, the frame around, name, title and level, in the card's
## accent color. Parts come from Q3Cards (library/cards/cards.json); the web menu draws the same
## cards on the Profile page.
##   var v := Q3CardView.new(); v.setup(card, "Name", level); v.custom_minimum_size = Vector2(420, 84)

var card: Dictionary = {}
var player_name := ""
var level := 1
var highlight := false     ## the local player's row
var _banner: Texture2D
var _emblem: Texture2D
var _frame: Texture2D
var _accent := Color(1, 0.13, 0.13)
var _title := ""


func setup(c: Dictionary, name: String, lvl: int) -> Q3CardView:
	card = c
	player_name = name
	level = lvl
	_banner = _art("banners", c.get("banner", ""))
	_emblem = _art("emblems", c.get("emblem", ""))
	_frame = _art("frames", c.get("frame", ""))
	var acc := Q3Cards.part("accents", c.get("accent", ""))
	if acc.has("value"):
		_accent = Color(acc["value"])
	_title = String(Q3Cards.part("titles", c.get("title", "")).get("name", ""))
	queue_redraw()
	return self


func _art(kind: String, id: String) -> Texture2D:
	var p: Dictionary = Q3Cards.part(kind, id)
	var path := Q3.LIB + String(p.get("art", ""))
	return load(path) if p.has("art") and ResourceLoader.exists(path) else null


func _draw() -> void:
	var r := Rect2(Vector2.ZERO, size)
	# banner: its art cropped to the card's shape, darkened toward the text side
	draw_rect(r, Color(0.04, 0.035, 0.035))
	if _banner:
		var ts := _banner.get_size()
		var crop_h := ts.x * r.size.y / r.size.x
		draw_texture_rect_region(_banner, r, Rect2(0, (ts.y - crop_h) * 0.5, ts.x, crop_h), Color(0.75, 0.75, 0.75))
	# darker toward the right, where the text sits (vertex colors: no texture to wait for)
	draw_polygon(PackedVector2Array([r.position, Vector2(r.end.x, r.position.y), r.end, Vector2(r.position.x, r.end.y)]),
		PackedColorArray([Color(0, 0, 0, 0.15), Color(0, 0, 0, 0.8), Color(0, 0, 0, 0.8), Color(0, 0, 0, 0.15)]))
	# emblem in a square on the left
	var e := Rect2(Vector2(6, 6), Vector2(r.size.y - 12, r.size.y - 12))
	draw_rect(e, Color(0, 0, 0, 0.6))
	if _emblem:
		draw_texture_rect(_emblem, e, false)
	draw_rect(e, _accent, false, 2.0)
	# frame: the frame's material art as a border, else a thin accent line
	var bw := 4.0
	if _frame:
		for side in [Rect2(0, 0, r.size.x, bw), Rect2(0, r.size.y - bw, r.size.x, bw), Rect2(0, 0, bw, r.size.y), Rect2(r.size.x - bw, 0, bw, r.size.y)]:
			draw_texture_rect(_frame, side, true)
	else:
		draw_rect(r, _accent.darkened(0.3), false, 2.0)
	if highlight:
		draw_rect(r.grow(-1), Color(1, 1, 1, 0.9), false, 2.0)
	# text
	var font := Q3Fonts.prop() if Q3Fonts.prop() else get_theme_default_font()
	var x := e.end.x + 14
	var h := r.size.y
	var text_w := r.size.x - x - maxf(50.0, h * 0.75) - 20.0
	draw_string(font, Vector2(x, h * 0.46), player_name.to_upper(), HORIZONTAL_ALIGNMENT_LEFT, text_w, int(h * 0.32), Color.WHITE)
	if _title != "":
		draw_string(font, Vector2(x, h * 0.80), _title, HORIZONTAL_ALIGNMENT_LEFT, text_w, int(h * 0.2), _accent.lightened(0.35))
	# level badge on the right, growing with the card so three-digit levels fit at any size
	var lv := str(level)
	var badge_w := maxf(50.0, h * 0.75)
	draw_string(font, Vector2(r.size.x - badge_w - 10, h * 0.42), "LVL", HORIZONTAL_ALIGNMENT_CENTER, badge_w, int(h * 0.16), Color(1, 1, 1, 0.6))
	draw_string(font, Vector2(r.size.x - badge_w - 10, h * 0.82), lv, HORIZONTAL_ALIGNMENT_CENTER, badge_w, int(h * 0.34), Color.WHITE)
