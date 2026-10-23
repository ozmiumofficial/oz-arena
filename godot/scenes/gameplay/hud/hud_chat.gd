class_name Q3HudChat
extends Control
## The chat, bottom left. T opens Quake 3's messagemode ("say") and Y messagemode2 ("say_team";
## outside team modes the game makes it a say). The game itself formats what everyone sees
## (g_cmds.c G_Say: "Name^7: ^2text", "(Name^7): ^5text" for the team) and sends it back as
## "chat" / "tchat", so bots' lines and other players' arrive the same way and keep their colors.
## Lines stay Settings > HUD > Chat time seconds, newest at the bottom; while typing, the panel
## shows the recent history. Up and Down recall what you sent, Tab completes a player's name, and
## a message with color codes shows a preview of how it will look.

signal sent(text: String, team: bool)
signal closed

const S := preload("res://scenes/gameplay/hud/hud_style.gd")
const HISTORY := 100
const MAX_SAY := 150              ## MAX_SAY_TEXT: the game cuts longer lines
const OPEN_LINES := 10            ## history shown while typing
const SAY_COLOR := Color(0, 1, 0)     ## COLOR_GREEN for say
const TEAM_COLOR := Color(0, 1, 1)    ## COLOR_CYAN for say_team

var hud: Q3Hud
var typing := false
var team := false
var names: Callable               ## -> the players' names, for Tab completion
var _lines: Array[Dictionary] = []   ## {text, team, at (ms)}
var _edit: LineEdit
var _sent: PackedStringArray = []
var _recall := -1


func _ready() -> void:
	_edit = LineEdit.new()
	_edit.visible = false
	_edit.max_length = MAX_SAY
	_edit.context_menu_enabled = false
	_edit.caret_blink = true
	for st in ["normal", "focus", "read_only"]:
		_edit.add_theme_stylebox_override(st, StyleBoxEmpty.new())
	_edit.add_theme_font_override("font", S.num_medium())
	_edit.add_theme_color_override("font_color", S.INK_HOT)
	_edit.add_theme_color_override("caret_color", S.AMBER)
	_edit.add_theme_color_override("selection_color", Color(S.AMBER, 0.3))
	add_child(_edit)
	_edit.text_submitted.connect(_submit)
	_edit.gui_input.connect(_edit_input)
	_edit.text_changed.connect(func(_t): queue_redraw())


## A chat line from the game ("chat" or "tchat").
func add(text: String, team_line := false) -> void:
	_lines.append({"text": text, "team": team_line, "at": Time.get_ticks_msec()})
	if _lines.size() > HISTORY:
		_lines.pop_front()


func lines() -> Array[Dictionary]:
	return _lines


func open(team_mode: bool) -> void:
	typing = true
	team = team_mode
	_recall = -1
	_edit.text = ""
	_edit.visible = true
	_place_edit()
	_edit.grab_focus()
	_edit.edit()
	queue_redraw()


func close() -> void:
	if not typing:
		return
	typing = false
	_edit.release_focus()
	_edit.visible = false
	queue_redraw()
	closed.emit()


func _submit(text: String) -> void:
	var t := text.strip_edges()
	if Q3Colors.strip(t).strip_edges() != "":
		if _sent.is_empty() or _sent[-1] != t:
			_sent.append(t)
		sent.emit(t, team)
	close()


func _edit_input(event: InputEvent) -> void:
	if not (event is InputEventKey and event.pressed):
		return
	match (event as InputEventKey).keycode:
		KEY_ESCAPE:
			close()
		KEY_TAB:
			_complete()
		KEY_UP:
			_recall_sent(-1)
		KEY_DOWN:
			_recall_sent(1)
		_:
			return
	_edit.accept_event()


## Up and Down step through what you've sent this match.
func _recall_sent(dir: int) -> void:
	if _sent.is_empty() or (_recall < 0 and dir > 0):
		return
	_recall = _sent.size() - 1 if _recall < 0 else clampi(_recall + dir, 0, _sent.size())
	_edit.text = _sent[_recall] if _recall < _sent.size() else ""
	_edit.caret_column = _edit.text.length()


## Tab: the word before the caret becomes the first player whose name starts with it.
func _complete() -> void:
	if not names.is_valid():
		return
	var before := _edit.text.left(_edit.caret_column)
	var start := before.rfind(" ") + 1
	var word := before.substr(start).to_lower()
	if word == "":
		return
	for n in names.call():
		var plain := Q3Colors.strip(String(n))
		if plain.to_lower().begins_with(word):
			_edit.text = before.left(start) + plain + _edit.text.substr(_edit.caret_column)
			_edit.caret_column = start + plain.length()
			return


func _process(_delta: float) -> void:
	if typing:
		_place_edit()
	queue_redraw()


## The input row along the bottom, after its SAY: label.
func _place_edit() -> void:
	var u := hud.u
	var fs := int(19 * u)
	var label := "SAY TEAM:" if team else "SAY:"
	var lx := 14.0 * u + S.text_width(S.label(), label, int(14 * u)) + 10 * u
	var h := 36.0 * u
	_edit.position = Vector2(lx, size.y - h)
	_edit.size = Vector2(size.x - lx - 14.0 * u, h)
	if _edit.get_theme_font_size("font_size") != fs:
		_edit.add_theme_font_size_override("font_size", fs)


func _draw() -> void:
	var u := hud.u
	var fs := int(19 * u)
	var lh := 25.0 * u
	var font := S.num_medium()
	var pad := 14.0 * u
	var wrap_w := size.x - pad * 2.0
	var now := Time.get_ticks_msec()
	var life := float(Settings.get_value("hud/chat_time")) * 1000.0
	var max_lines := int(Settings.get_value("hud/chat_lines"))
	# the newest lines, wrapped, bottom up
	var shown: Array = []   ## [runs, alpha]
	for i in range(_lines.size() - 1, -1, -1):
		var l: Dictionary = _lines[i]
		var age := float(now - int(l["at"]))
		var alpha := 1.0
		if not typing:
			if age >= life:
				break
			alpha = clampf((life - age) / 600.0, 0.0, 1.0)
		var wrapped := Q3Colors.wrap(font, String(l["text"]), fs, wrap_w, S.INK)
		for j in range(wrapped.size() - 1, -1, -1):
			shown.append([wrapped[j], alpha])
		if shown.size() >= (OPEN_LINES if typing else max_lines):
			break
	shown = shown.slice(0, OPEN_LINES if typing else max_lines)
	var input_h := 36.0 * u if typing else 0.0
	var preview: Array = []
	if typing and Q3Colors.has_codes(_edit.text):
		preview = Q3Colors.wrap(font, _edit.text, fs, wrap_w, TEAM_COLOR if team else SAY_COLOR)
	var body_h := (shown.size() + preview.size()) * lh
	var bottom := size.y - input_h
	if typing:
		var r := Rect2(Vector2(0, bottom - body_h - pad * 0.6), Vector2(size.x, body_h + pad * 0.6 + input_h))
		draw_rect(r, Color(S.GLASS, 0.72))
		draw_rect(Rect2(r.position, Vector2(3 * u, r.size.y)), Color(TEAM_COLOR if team else S.RED, 0.85))
		var ir := Rect2(Vector2(0, size.y - input_h), Vector2(size.x, input_h))
		draw_rect(ir, Color(0, 0, 0, 0.35))
		var label := "SAY TEAM:" if team else "SAY:"
		var lfs := int(14 * u)
		S.text(self, S.label(), Vector2(pad, ir.get_center().y + lfs * 0.38), label, lfs, Color(TEAM_COLOR if team else S.AMBER, 0.95))
		# the preview: what the message will look like, just above the input
		var py := bottom - (preview.size() - 1) * lh - 7 * u
		for p in preview:
			_line(font, Vector2(pad, py), p, fs, 0.9)
			py += lh
		bottom -= preview.size() * lh
	var y := bottom - 7 * u
	for s in shown:
		_line(font, Vector2(pad, y), s[0], fs, float(s[1]))
		y -= lh


func _line(font: Font, pos: Vector2, parts: Array, fs: int, alpha: float) -> void:
	var plain := ""
	for p in parts:
		plain += p[0]
	draw_string_outline(font, pos, plain, HORIZONTAL_ALIGNMENT_LEFT, -1, fs, maxi(int(fs * 0.14), 2), Color(0, 0, 0, 0.7 * alpha))
	Q3Colors.draw_runs(self, font, pos, parts, fs, alpha)
