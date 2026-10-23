class_name Q3Colors
extends RefCounted
## Quake 3's color codes: "^" and any character but another "^" picks g_color_table[(c - '0') & 7]
## for the text after it (q_shared.h Q_IsColorString, ColorIndex), so ^1 is red and ^7 white, and
## letters wrap round to the same eight. Names, chat and server prints all carry them. Black and
## blue are lifted a little, as the web menu does (lib/social.ts): id's pure values vanish on the
## dark glass.

const TABLE := [Color("#5a5a5a"), Color("#ff0000"), Color("#00ff00"), Color("#ffff00"), Color("#4a6bff"),
	Color("#00ffff"), Color("#ff00ff"), Color("#ffffff")]


## The text cut into colored runs: [[text, color], ...]. Text before the first code takes `base`.
## Codes keep base's alpha, so a fading line fades every run.
static func runs(s: String, base := Color.WHITE) -> Array:
	var out: Array = []
	var color := base
	var buf := ""
	var i := 0
	while i < s.length():
		if s[i] == "^" and i + 1 < s.length() and s[i + 1] != "^":
			if buf != "":
				out.append([buf, color])
				buf = ""
			color = Color(TABLE[(s.unicode_at(i + 1) - 48) & 7], base.a)
			i += 2
			continue
		buf += s[i]
		i += 1
	if buf != "":
		out.append([buf, color])
	return out


## The text without its codes (Q_CleanStr's color part).
static func strip(s: String) -> String:
	if not s.contains("^"):
		return s
	var out := ""
	for r in runs(s):
		out += r[0]
	return out


static func has_codes(s: String) -> bool:
	return strip(s) != s


## Draws colored text with its left baseline end at pos. With max_width > 0 it stops there and
## ends in an ellipsis. Returns the width drawn.
static func draw(ci: CanvasItem, font: Font, pos: Vector2, s: String, size: int, base := Color.WHITE, max_width := -1.0) -> float:
	var parts := runs(s, base)
	if max_width > 0.0 and width(font, s, size) > max_width:
		parts = fit(font, parts, size, max_width)
	var x := pos.x
	for r in parts:
		ci.draw_string(font, Vector2(x, pos.y), r[0], HORIZONTAL_ALIGNMENT_LEFT, -1, size, r[1])
		x += font.get_string_size(r[0], HORIZONTAL_ALIGNMENT_LEFT, -1, size).x
	return x - pos.x


static func width(font: Font, s: String, size: int) -> float:
	return font.get_string_size(strip(s), HORIZONTAL_ALIGNMENT_LEFT, -1, size).x


## The text broken into lines no wider than max_width, at spaces where it can: an Array of lines,
## each an Array of runs. A color carries on to the next line.
static func wrap(font: Font, s: String, size: int, max_width: float, base := Color.WHITE) -> Array:
	var lines: Array = []
	var line: Array = []
	var w := 0.0
	var re := RegEx.create_from_string("\\S+|\\s+")
	for r in runs(s, base):
		for m in re.search_all(r[0]):
			var tok := m.get_string()
			var space := tok.strip_edges() == ""
			var tw := font.get_string_size(tok, HORIZONTAL_ALIGNMENT_LEFT, -1, size).x
			if w + tw > max_width and w > 0.0 and not space:
				lines.append(line)
				line = []
				w = 0.0
			if space and line.is_empty():
				continue
			while tw > max_width and not space:
				# a word longer than the line: as much as fits, then the rest on the next
				var n := tok.length()
				while n > 1 and w + font.get_string_size(tok.left(n), HORIZONTAL_ALIGNMENT_LEFT, -1, size).x > max_width:
					n -= 1
				_add_run(line, tok.left(n), r[1])
				lines.append(line)
				line = []
				w = 0.0
				tok = tok.substr(n)
				tw = font.get_string_size(tok, HORIZONTAL_ALIGNMENT_LEFT, -1, size).x
			_add_run(line, tok, r[1])
			w += tw
	if not line.is_empty():
		lines.append(line)
	return lines


static func _add_run(line: Array, t: String, c: Color) -> void:
	if t == "":
		return
	if not line.is_empty() and line[-1][1] == c:
		line[-1][0] += t
	else:
		line.append([t, c])


## Draws runs from wrap() (or runs()) with the left baseline end at pos.
static func draw_runs(ci: CanvasItem, font: Font, pos: Vector2, parts: Array, size: int, alpha := 1.0) -> void:
	var x := pos.x
	for r in parts:
		ci.draw_string(font, Vector2(x, pos.y), r[0], HORIZONTAL_ALIGNMENT_LEFT, -1, size, Color(r[1], r[1].a * alpha))
		x += font.get_string_size(r[0], HORIZONTAL_ALIGNMENT_LEFT, -1, size).x


## Runs cut down to max_width with an ellipsis in the last run's color.
static func fit(font: Font, parts: Array, size: int, max_width: float) -> Array:
	var room := max_width - font.get_string_size("…", HORIZONTAL_ALIGNMENT_LEFT, -1, size).x
	var out: Array = []
	var used := 0.0
	for r in parts:
		var t: String = r[0]
		var w := font.get_string_size(t, HORIZONTAL_ALIGNMENT_LEFT, -1, size).x
		if used + w <= room:
			out.append(r)
			used += w
			continue
		while t != "" and used + font.get_string_size(t, HORIZONTAL_ALIGNMENT_LEFT, -1, size).x > room:
			t = t.left(-1)
		out.append([t.strip_edges(false, true) + "…", r[1]])
		break
	return out
