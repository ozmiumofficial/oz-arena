class_name Q3HudStyle
extends RefCounted
## The in-game HUD's look, shared by its widgets: the web menu's palette (globals.css: near-black
## glass, warm off-white ink, Quake 3's red, amber), Barlow for numbers (tabular figures, so they
## don't jitter) and Quake 3's own font for labels, slanted glass panels with a red edge.

const INK := Color("#e6ddd2")
const INK_HOT := Color("#fff3e6")
const DIM := Color("#8d847c")
const FAINT := Color("#5d554f")
const RED := Color("#db2410")
const GLOW := Color("#ff4a1c")
const AMBER := Color("#ffad33")
const GLASS := Color(0.04, 0.03, 0.027, 0.62)
const GLASS_EDGE := Color(1.0, 0.92, 0.85, 0.08)
const MEGA := Color("#9fd8ff")          ## health over 100
const TEAM_RED := Color(1.0, 0.28, 0.2)
const TEAM_BLUE := Color(0.32, 0.55, 1.0)
const SKEW := 0.26                      ## panel slant: horizontal shift per unit of height

static var _num: Font
static var _num_medium: Font
static var _icons: Dictionary = {}


## Barlow SemiBold with tabular figures.
static func num() -> Font:
	if _num == null:
		_num = _barlow("res://fonts/barlow_semibold.woff2")
	return _num


static func num_medium() -> Font:
	if _num_medium == null:
		_num_medium = _barlow("res://fonts/barlow_medium.woff2")
	return _num_medium


static func _barlow(path: String) -> Font:
	var base: Font = load(path) if ResourceLoader.exists(path) else ThemeDB.fallback_font
	var v := FontVariation.new()
	v.base_font = base
	v.opentype_features = {"tnum": 1}
	return v


## Quake 3's prop font, for labels.
static func label() -> Font:
	var f := Q3Fonts.prop()
	return f if f else ThemeDB.fallback_font


## A HUD icon from library/ui/hud (icons/*.tga and menu/medals at 2x), or null.
static func icon(name: String) -> Texture2D:
	if not _icons.has(name):
		var p := Q3.LIB + "ui/hud/%s.png" % name
		_icons[name] = load(p) if ResourceLoader.exists(p) else null
	return _icons[name]


## Weapon icon for a weapon number (iconw_*).
static func weapon_icon(w: int) -> Texture2D:
	const NAMES := ["", "gauntlet", "machinegun", "shotgun", "grenade", "rocket", "lightning", "railgun", "plasma", "bfg", "grapple"]
	return icon("iconw_" + NAMES[w]) if w > 0 and w < NAMES.size() else null


## A slanted panel (parallelogram leaning right) filling `r`.
static func panel(ci: CanvasItem, r: Rect2, color := GLASS, accent := Color(0, 0, 0, 0), accent_w := 0.0) -> void:
	var s := r.size.y * SKEW
	var pts := PackedVector2Array([r.position + Vector2(s, 0), Vector2(r.end.x, r.position.y),
		Vector2(r.end.x - s, r.end.y), Vector2(r.position.x, r.end.y)])
	ci.draw_colored_polygon(pts, color)
	ci.draw_polyline(PackedVector2Array([pts[0], pts[1], pts[2], pts[3], pts[0]]), GLASS_EDGE, 1.0, true)
	if accent.a > 0.0 and accent_w > 0.0:
		ci.draw_line(pts[3] + Vector2(0, -accent_w * 0.5), pts[3] + Vector2(minf(r.size.x * 0.35, r.size.x - s), -accent_w * 0.5), accent, accent_w)


## A slanted bar segment from x0 to x1 (fractions of r's width) in r.
static func bar(ci: CanvasItem, r: Rect2, f0: float, f1: float, color: Color) -> void:
	if f1 <= f0:
		return
	var s := r.size.y * SKEW
	var x0 := r.position.x + f0 * (r.size.x - s)
	var x1 := r.position.x + f1 * (r.size.x - s)
	ci.draw_colored_polygon(PackedVector2Array([Vector2(x0 + s, r.position.y), Vector2(x1 + s, r.position.y),
		Vector2(x1, r.end.y), Vector2(x0, r.end.y)]), color)


## Text with a soft dark outline so it reads over bright walls. pos is the baseline's left end
## (or center / right end with align).
static func text(ci: CanvasItem, font: Font, pos: Vector2, s: String, size: int, color: Color,
		align := HORIZONTAL_ALIGNMENT_LEFT, width := -1.0, outline := 0.35) -> void:
	var p := pos
	if align != HORIZONTAL_ALIGNMENT_LEFT and width < 0.0:
		var w := font.get_string_size(s, HORIZONTAL_ALIGNMENT_LEFT, -1, size).x
		p.x -= w if align == HORIZONTAL_ALIGNMENT_RIGHT else w * 0.5
		align = HORIZONTAL_ALIGNMENT_LEFT
	if outline > 0.0:
		ci.draw_string_outline(font, p, s, align, width, size, maxi(int(size * 0.12), 2), Color(0, 0, 0, outline * color.a))
	ci.draw_string(font, p, s, align, width, size, color)


static func text_width(font: Font, s: String, size: int) -> float:
	return font.get_string_size(s, HORIZONTAL_ALIGNMENT_LEFT, -1, size).x


## A plus (health) drawn as vector art in r.
static func cross_icon(ci: CanvasItem, r: Rect2, color: Color) -> void:
	var c := r.get_center()
	var t := r.size.x * 0.3
	ci.draw_rect(Rect2(c.x - t * 0.5, r.position.y, t, r.size.y), color)
	ci.draw_rect(Rect2(r.position.x, c.y - t * 0.5, r.size.x, t), color)


## A shield (armor) drawn as vector art in r.
static func shield_icon(ci: CanvasItem, r: Rect2, color: Color) -> void:
	var x := r.position.x
	var y := r.position.y
	var w := r.size.x
	var h := r.size.y
	var pts := PackedVector2Array([Vector2(x + w * 0.5, y), Vector2(x + w, y + h * 0.16), Vector2(x + w * 0.94, y + h * 0.58),
		Vector2(x + w * 0.5, y + h), Vector2(x + w * 0.06, y + h * 0.58), Vector2(x, y + h * 0.16)])
	ci.draw_colored_polygon(pts, color)
	var inner := PackedVector2Array()
	for p in pts:
		inner.append(r.get_center() + (p - r.get_center()) * 0.55)
	ci.draw_colored_polygon(inner, Color(0, 0, 0, 0.35))


## CG_PlaceString: 1st, 2nd, 3rd, 4th ... with "Tied for" when the rank carries RANK_TIED_FLAG.
static func place(rank: int) -> String:
	var tied := (rank & 0x4000) != 0
	var r := (rank & ~0x4000) + 1
	var s: String
	if r == 1:
		s = "1st"
	elif r == 2:
		s = "2nd"
	elif r == 3:
		s = "3rd"
	elif r == 11 or r == 12 or r == 13:
		s = "%dth" % r
	elif r % 10 == 1:
		s = "%dst" % r
	elif r % 10 == 2:
		s = "%dnd" % r
	elif r % 10 == 3:
		s = "%drd" % r
	else:
		s = "%dth" % r
	return ("Tied for " + s) if tied else s
