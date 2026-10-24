class_name Q3Scoreboard
extends CanvasLayer
## The Tab scoreboard, in the HUD's style (hud_style.gd): a glass board with the map's level shot
## as its header (mode, map, clock, limit). On the left, your match (Q3MatchStats): score, deaths,
## damage, accuracy, each weapon's damage and accuracy, the distance you covered and your medals.
## On the right every player ranked: place (gold, silver, bronze), their card, score, frags
## against deaths and ping bars; your own row glows amber.
## Team modes split red and blue, each under its team's score. Rows come from
## Gameplay.standings() (the server's own playerStates and configstrings), so the board is always
## the live game's.
## While Tab is held, the right mouse button frees the mouse (`set_interactive`): rows light up
## under it, and clicking one opens that player's panel: their card large, this match's numbers,
## and their career (yours from your profile; a bot's skill and model), in the left column.

signal interactive_changed(on: bool)
signal picked(client: int)   ## a row clicked (a spectator follows that player)

const S := preload("res://scenes/gameplay/hud/hud_style.gd")
const PLACE_COLORS := [Color("#ffc94d"), Color("#d6dde6"), Color("#d08a4e")]

var interactive := false
var _root: Control
var _board: Control
var _standings: Array = []
var _selected := -1               ## client number of the open player panel, or -1
var _hover := -1                  ## client under the mouse
var _header := ""
var _team_scores := Vector2i(-1, -1)
var _info: Dictionary = {}        ## {map, clock, limit}
var _rows: Array = []             ## [{rect: Rect2, client: int, s, place}] for drawing and the mouse
var _levelshot: Texture2D
var _levelshot_map := ""
var _stats: Dictionary = {}       ## Q3MatchStats.summary(): the left panel ({} hides it)
var _stats_title := "YOUR MATCH"


func _init() -> void:
	layer = 20
	visible = false


func _ready() -> void:
	_root = Control.new()
	_root.set_anchors_preset(Control.PRESET_FULL_RECT)
	_root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_root)
	_board = Control.new()
	_board.set_anchors_preset(Control.PRESET_FULL_RECT)
	_board.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_board.draw.connect(_draw_board)
	_board.gui_input.connect(_on_board_input)
	_root.add_child(_board)


## Mouse mode for the board (Gameplay toggles it on right click, off when Tab is released).
func set_interactive(on: bool) -> void:
	if on == interactive:
		return
	interactive = on
	if not on:
		_selected = -1
		_hover = -1
	_board.mouse_filter = Control.MOUSE_FILTER_STOP if on else Control.MOUSE_FILTER_IGNORE
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE if on else Input.MOUSE_MODE_CAPTURED
	if on:
		UiSound.play_in()
	else:
		UiSound.play("out")
	interactive_changed.emit(on)
	if not _standings.is_empty():
		refresh(_standings, _header, _team_scores, _info, _stats)


## standings: Gameplay.standings(); header: the match title; info: {map, clock, limit, stats_title};
## stats: Q3MatchStats.summary() for the left panel.
func refresh(standings: Array, header: String, team_scores := Vector2i(-1, -1), info := {}, stats := {}) -> void:
	_standings = standings
	_stats = stats
	_stats_title = String(info.get("stats_title", "YOUR MATCH"))
	_header = header
	_team_scores = team_scores
	_info = info
	var map := String(info.get("map", ""))
	if map != _levelshot_map:
		_levelshot_map = map
		var p := Q3.LIB + "ui/levelshots/%s.jpg" % map
		_levelshot = load(p) if map != "" and ResourceLoader.exists(p) else null
	for c in _board.get_children():
		c.queue_free()
	_layout_rows()
	if _selected >= 0:
		var row: Dictionary = {}
		for s in standings:
			if int(s.get("client", -1)) == _selected:
				row = s
		if row.is_empty():
			_selected = -1
		else:
			_board.add_child(_player_panel(row, _board.get_viewport_rect().size))
	_board.queue_redraw()
	_board.queue_redraw()


# ---------------------------------------------------------------- layout and drawing

func _u() -> float:
	return _board.get_viewport_rect().size.y / 1080.0


func _panel_rect() -> Rect2:
	var vs := _board.get_viewport_rect().size
	var u := _u()
	var w := minf(vs.x * 0.92, 1780.0 * u)
	return Rect2((vs.x - w) * 0.5, 70.0 * u, w, vs.y - 150.0 * u)


## The left column: your match, or the clicked player's panel.
func _left_rect() -> Rect2:
	var r := _panel_rect()
	var u := _u()
	return Rect2(r.position.x + 24.0 * u, r.position.y + 156.0 * u, r.size.x * 0.33, r.size.y - 180.0 * u)


## The player list, right of the left column.
func _list_rect() -> Rect2:
	var r := _panel_rect()
	var l := _left_rect()
	var u := _u()
	var x := l.end.x + 28.0 * u
	return Rect2(x, r.position.y, r.end.x - 24.0 * u - x, r.size.y)


## The cards are controls; everything else is drawn. Rows remember their rects for the mouse.
func _layout_rows() -> void:
	_rows.clear()
	var u := _u()
	var r := _list_rect()
	var teams := _team_scores.x >= 0
	var cols: Array = [_standings] if not teams else [_standings.filter(func(s): return s.get("team", 0) == 1),
		_standings.filter(func(s): return s.get("team", 0) == 2)]
	var gap := 24.0 * u
	var cw := (r.size.x - gap * (cols.size() - 1)) / cols.size()
	var top := r.position.y + 170.0 * u + (64.0 * u if teams else 0.0)
	var rh := 64.0 * u
	for c in cols.size():
		var x := r.position.x + c * (cw + gap)
		var y := top
		for i in cols[c].size():
			var s: Dictionary = cols[c][i]
			var row := Rect2(x, y, cw, rh)
			_rows.append({"rect": row, "client": int(s.get("client", -1)), "s": s, "place": i})
			var card := Q3CardView.new().setup(s.get("card", {}), String(s.get("name", "")), int(s.get("level", 1)))
			card.position = Vector2(x + 64.0 * u, y + 6.0 * u)
			card.size = Vector2(cw * 0.5, rh - 12.0 * u)
			card.highlight = s.get("local", false)
			card.mouse_filter = Control.MOUSE_FILTER_IGNORE
			_board.add_child(card)
			y += rh + 6.0 * u


func _draw_board() -> void:
	if _standings.is_empty() and _header == "":
		return
	var vs := _board.get_viewport_rect().size
	var u := _u()
	_board.draw_rect(Rect2(Vector2.ZERO, vs), Color(0, 0, 0, 0.45))   # the match behind dims
	var r := _panel_rect()
	var teams := _team_scores.x >= 0
	var bottom := r.position.y + 170.0 * u + (64.0 * u if teams else 0.0)
	for row in _rows:
		bottom = maxf(bottom, (row["rect"] as Rect2).end.y)
	if not _stats.is_empty() or _selected >= 0:
		bottom = maxf(bottom, _left_rect().position.y + _stats_height(u))
	var panel := Rect2(r.position, Vector2(r.size.x, bottom - r.position.y + 70.0 * u))
	_board.draw_rect(panel, Color(0.03, 0.025, 0.022, 0.86))
	_board.draw_rect(panel, S.GLASS_EDGE, false, 1.0)
	# header: the level shot, darkened toward the text
	var head := Rect2(panel.position, Vector2(panel.size.x, 132.0 * u))
	if _levelshot:
		var ts := _levelshot.get_size()
		var crop_h := ts.x * head.size.y / head.size.x
		_board.draw_texture_rect_region(_levelshot, head, Rect2(0, (ts.y - crop_h) * 0.45, ts.x, crop_h), Color(1.1, 1.05, 1.0))
	_board.draw_polygon(PackedVector2Array([head.position, Vector2(head.end.x, head.position.y), head.end, Vector2(head.position.x, head.end.y)]),
		PackedColorArray([Color(0, 0, 0, 0.82), Color(0, 0, 0, 0.12), Color(0, 0, 0, 0.45), Color(0, 0, 0, 0.9)]))
	_board.draw_rect(Rect2(head.position, Vector2(head.size.x, 3.0 * u)), S.RED)
	var title := _header.get_slice("  -  ", 0)
	var map_name := _header.get_slice("  -  ", 1) if _header.contains("  -  ") else ""
	S.text(_board, S.label(), head.position + Vector2(32 * u, 62 * u), title, int(44 * u), S.INK_HOT)
	S.text(_board, S.label(), head.position + Vector2(34 * u, 100 * u), map_name, int(20 * u), Color(S.AMBER, 0.95))
	S.text(_board, S.num(), Vector2(head.end.x - 32 * u, head.position.y + 70 * u), String(_info.get("clock", "")), int(46 * u), S.INK_HOT,
		HORIZONTAL_ALIGNMENT_RIGHT)
	S.text(_board, S.label(), Vector2(head.end.x - 34 * u, head.position.y + 102 * u), String(_info.get("limit", "")), int(15 * u),
		Color(S.DIM, 0.95), HORIZONTAL_ALIGNMENT_RIGHT)
	# your match (the left column, unless a player's panel is open there)
	if _selected < 0 and not _stats.is_empty():
		_draw_stats(_left_rect(), u)
	# column labels and team headers
	var lr := _list_rect()
	var cols := 2 if teams else 1
	var gap := 24.0 * u
	var cw := (lr.size.x - gap * (cols - 1)) / cols
	for c in cols:
		var x := lr.position.x + c * (cw + gap)
		var y := head.end.y + 30.0 * u
		if teams:
			var tc := S.TEAM_RED if c == 0 else S.TEAM_BLUE
			var tr := Rect2(x, y - 8 * u, cw, 54 * u)
			S.panel(_board, tr, Color(tc.darkened(0.7), 0.85))
			S.bar(_board, Rect2(tr.position.x, tr.end.y - 4 * u, tr.size.x, 4 * u), 0.0, 1.0, tc)
			S.text(_board, S.label(), tr.position + Vector2(26 * u, 36 * u), "RED TEAM" if c == 0 else "BLUE TEAM", int(24 * u), tc.lightened(0.2))
			S.text(_board, S.num(), Vector2(tr.end.x - 24 * u, tr.position.y + 42 * u), str(_team_scores[c]), int(44 * u), S.INK_HOT,
				HORIZONTAL_ALIGNMENT_RIGHT)
			y += 64 * u
		var ly := y + 4 * u
		for h in [[0.0, "#"], [64.0 * u, "PLAYER"]]:
			S.text(_board, S.label(), Vector2(x + h[0] + 8 * u, ly), h[1], int(13 * u), Color(S.DIM, 0.8))
		for h in [[0.67, "SCORE"], [0.81, "K / D"], [0.93, "PING"]]:
			S.text(_board, S.label(), Vector2(x + cw * h[0], ly), h[1], int(13 * u), Color(S.DIM, 0.8), HORIZONTAL_ALIGNMENT_CENTER)
	for row in _rows:
		_draw_row(row, u)
	# footer
	var fy := panel.end.y - 26.0 * u
	var hint := "CLICK A PLAYER FOR THEIR CARD   -   RELEASE TAB TO PLAY" if interactive else "RIGHT CLICK   PLAYER CARDS"
	S.text(_board, S.label(), Vector2(panel.get_center().x, fy), hint, int(14 * u), Color(S.DIM, 0.9), HORIZONTAL_ALIGNMENT_CENTER)
	_board.draw_rect(Rect2(panel.position.x, panel.end.y - 3.0 * u, panel.size.x, 3.0 * u), Color(S.RED, 0.5))


func _draw_row(row: Dictionary, u: float) -> void:
	var rr: Rect2 = row["rect"]
	var s: Dictionary = row["s"]
	var place: int = row["place"]
	var me: bool = s.get("local", false)
	var hot: bool = interactive and (int(row["client"]) == _hover or int(row["client"]) == _selected)
	var bg := Color(1, 1, 1, 0.07 if place % 2 == 0 else 0.035)
	if me:
		bg = Color(S.AMBER, 0.13)
	if hot:
		bg = Color(1, 1, 1, 0.13)
	S.panel(_board, rr, bg)
	if me or hot:
		_board.draw_rect(Rect2(rr.position.x, rr.position.y, 4 * u, rr.size.y), S.AMBER if me else S.RED)
	var pc: Color = PLACE_COLORS[place] if place < 3 else Color(S.DIM, 0.9)
	S.text(_board, S.num(), Vector2(rr.position.x + 34 * u, rr.get_center().y + 13 * u), str(place + 1), int(36 * u), pc,
		HORIZONTAL_ALIGNMENT_CENTER)
	var base := rr.get_center().y + 12 * u
	S.text(_board, S.num(), Vector2(rr.position.x + rr.size.x * 0.67, base + 2 * u), str(s.get("score", 0)), int(36 * u), S.INK_HOT,
		HORIZONTAL_ALIGNMENT_CENTER)
	S.text(_board, S.num_medium(), Vector2(rr.position.x + rr.size.x * 0.81, base - 1 * u), "%d / %d" % [s.get("frags", 0), s.get("deaths", 0)],
		int(24 * u), Color(S.INK, 0.85), HORIZONTAL_ALIGNMENT_CENTER)
	var px := rr.position.x + rr.size.x * 0.93
	if s.get("bot", false):
		var chip := Rect2(px - 26 * u, rr.get_center().y - 12 * u, 52 * u, 24 * u)
		_board.draw_rect(chip, Color(1, 1, 1, 0.08))
		_board.draw_rect(chip, Color(1, 1, 1, 0.2), false, 1.0)
		S.text(_board, S.label(), Vector2(chip.get_center().x, chip.get_center().y + 6 * u), "BOT", int(14 * u), S.DIM,
			HORIZONTAL_ALIGNMENT_CENTER)
	else:
		_ping_bars(Vector2(px - 30 * u, rr.get_center().y + 10 * u), int(s.get("ping", 0)), u)
		S.text(_board, S.num_medium(), Vector2(px + 26 * u, base - 2 * u), _ping(int(s.get("ping", 0))), int(20 * u),
			Color(S.INK, 0.85), HORIZONTAL_ALIGNMENT_CENTER)


## Four bars, green to red with the ping (none lit offline).
func _ping_bars(at: Vector2, ping: int, u: float) -> void:
	var lit := 0 if ping >= 999 else (4 if ping < 50 else 3 if ping < 100 else 2 if ping < 180 else 1)
	var col := Color("#5fe08a") if lit >= 3 else (S.AMBER if lit == 2 else S.RED)
	for i in 4:
		var h := (6.0 + i * 4.0) * u
		_board.draw_rect(Rect2(at.x + i * 7 * u, at.y - h, 5 * u, h), col if i < lit else Color(1, 1, 1, 0.15))


func _on_board_input(ev: InputEvent) -> void:
	if not interactive:
		return
	if ev is InputEventMouseMotion:
		var h := -1
		for row in _rows:
			if (row["rect"] as Rect2).has_point(ev.position):
				h = int(row["client"])
		if h != _hover:
			_hover = h
			if h >= 0:
				UiSound.play("move")
			_board.queue_redraw()
	elif ev is InputEventMouseButton and ev.pressed and ev.button_index == MOUSE_BUTTON_LEFT:
		var hit := -1
		for row in _rows:
			if (row["rect"] as Rect2).has_point(ev.position):
				hit = int(row["client"])
		if hit < 0:
			return
		picked.emit(hit)
		if not interactive:
			return   # a spectator went to follow them
		_selected = -1 if _selected == hit else hit
		if _selected >= 0:
			UiSound.play_in()
		else:
			UiSound.play("out")
		refresh(_standings, _header, _team_scores, _info)


# ---------------------------------------------------------------- the player panel

## A player's panel: the card large, an XP bar (yours), this match, and their career or bot details.
func _player_panel(s: Dictionary, view: Vector2) -> Control:
	var u := view.y / 1080.0
	var left := _left_rect()
	var box := Control.new()
	box.position = left.position
	box.size = Vector2(left.size.x, minf(left.size.y, _stats_height(u)))
	box.mouse_filter = Control.MOUSE_FILTER_STOP
	var pad := 28.0 * u
	var card := Q3CardView.new().setup(s.get("card", {}), String(s.get("name", "")), int(s.get("level", 1)))
	card.position = Vector2(pad, pad + 4 * u)
	card.size = Vector2(box.size.x - pad * 2, 132 * u)
	card.highlight = s.get("local", false)
	card.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var lines: Array = []
	var deaths := int(s.get("deaths", 0))
	var frags := int(s.get("frags", 0))
	lines.append(["THIS MATCH", ""])
	lines.append(["Score", str(s.get("score", 0))])
	lines.append(["Frags / deaths", "%d / %d" % [frags, deaths]])
	lines.append(["Ratio", "%.2f" % (float(frags) / maxf(deaths, 1.0))])
	if not s.get("bot", false):
		lines.append(["Ping", _ping(int(s.get("ping", 0))) + (" ms" if int(s.get("ping", 0)) < 999 else "")])
	lines.append(["Character", String(s.get("model", "")).capitalize()])
	var level_frac := -1.0
	if s.get("bot", false):
		lines.append(["BOT", ""])
		lines.append(["Skill", "%d  %s" % [int(s.get("skill", 3)), String(Q3MatchSetup.SKILLS[clampi(int(s.get("skill", 3)) - 1, 0, 4)])]])
	elif s.get("local", false):
		var st: Dictionary = Q3Profile.data().get("stats", {})
		var lv := Q3Profile.level()
		level_frac = float(lv["xp"]) / maxf(float(lv["next"]), 1.0)
		lines.append(["CAREER", ""])
		lines.append(["Level", "%d   %d / %d XP" % [int(lv["level"]), int(lv["xp"]), int(lv["next"])]])
		lines.append(["Matches / wins", "%d / %d" % [int(st.get("matches", 0)), int(st.get("wins", 0))]])
		var cf := int(st.get("frags", 0))
		var cd := int(st.get("deaths", 0))
		lines.append(["Frags / deaths", "%d / %d   (%.2f)" % [cf, cd, float(cf) / maxf(cd, 1.0)]])
		var medals := []
		for m in ["impressive", "excellent", "gauntlet", "defend", "assist", "perfect"]:
			var n := int(Q3Cards.stat(st, "medal." + m))
			if n > 0:
				medals.append("%s %d" % [m.capitalize(), n])
		lines.append(["Medals", ", ".join(medals) if medals.size() else "-"])
	box.draw.connect(func():
		var r := Rect2(Vector2.ZERO, box.size)
		box.draw_rect(r, Color(0.03, 0.025, 0.022, 0.92))
		box.draw_rect(r, S.GLASS_EDGE, false, 1.0)
		box.draw_rect(Rect2(0, 0, r.size.x, 3 * u), S.RED)
		var y := card.position.y + card.size.y + 34 * u
		if level_frac >= 0.0:
			# the XP bar toward the next level
			var bar := Rect2(pad, card.position.y + card.size.y + 10 * u, r.size.x - pad * 2, 7 * u)
			S.bar(box, bar, 0.0, 1.0, Color(1, 1, 1, 0.08))
			S.bar(box, bar, 0.0, level_frac, S.AMBER)
			y += 10 * u
		for l in lines:
			if l[1] == "":
				y += 12 * u
				S.text(box, S.label(), Vector2(pad, y), l[0], int(17 * u), S.RED)
				box.draw_line(Vector2(pad + S.text_width(S.label(), l[0], int(17 * u)) + 12 * u, y - 6 * u), Vector2(r.size.x - pad, y - 6 * u),
					Color(S.RED, 0.35), 1.0)
				y += 30 * u
				continue
			S.text(box, S.num_medium(), Vector2(pad, y), l[0], int(21 * u), Color(S.INK, 0.7))
			S.text(box, S.num(), Vector2(r.size.x - pad, y), l[1], int(22 * u), S.INK_HOT, HORIZONTAL_ALIGNMENT_RIGHT)
			y += 32 * u)
	box.add_child(card)
	return box


## The offline player has no network link (the server reports 999): show a dash.
static func _ping(p: int) -> String:
	return "-" if p >= 999 else str(p)


# ---------------------------------------------------------------- your match

const STATS_ROW := 34.0   ## a weapon row, 1080p units


## How tall the left column's content is (the board grows to fit it).
func _stats_height(u: float) -> float:
	var weapons: Array = _stats.get("weapons", [])
	return (176.0 + 48.0 + maxf(weapons.size(), 1) * STATS_ROW + 70.0 + 52.0 + 112.0) * u


## Your match: the four big numbers, each weapon's damage and accuracy, distance, medals.
func _draw_stats(r: Rect2, u: float) -> void:
	var box := Rect2(r.position, Vector2(r.size.x, _stats_height(u)))
	_board.draw_rect(box, Color(1, 1, 1, 0.035))
	_board.draw_rect(Rect2(box.position, Vector2(4 * u, box.size.y)), Color(S.AMBER, 0.8))
	var x := box.position.x + 24 * u
	var w := box.size.x - 44 * u
	var y := box.position.y + 40 * u
	S.text(_board, S.label(), Vector2(x, y), _stats_title, int(22 * u), S.INK_HOT)
	# the big four
	y += 26 * u
	var acc := float(_stats.get("accuracy", -1.0))
	var big := [["SCORE", str(_stats.get("frags", 0))], ["DEATHS", str(_stats.get("deaths", 0))],
		["DAMAGE", str(_stats.get("damage", 0))], ["ACCURACY", ("%d%%" % roundi(acc * 100.0)) if acc >= 0.0 else "-"]]
	for i in big.size():
		var bx := x + w * i / 4.0
		S.text(_board, S.num(), Vector2(bx, y + 52 * u), big[i][1], int(40 * u), S.INK_HOT)
		S.text(_board, S.label(), Vector2(bx + 2 * u, y + 76 * u), big[i][0], int(13 * u), Color(S.DIM, 0.9))
	y += 120 * u
	# weapons
	_section(Vector2(x, y), w, "WEAPONS", u)
	y += 22 * u
	for h in [[0.52, "DAMAGE"], [0.72, "HITS / SHOTS"], [0.93, "ACCURACY"]]:
		S.text(_board, S.label(), Vector2(x + w * h[0], y + 4 * u), h[1], int(12 * u), Color(S.DIM, 0.8), HORIZONTAL_ALIGNMENT_CENTER)
	y += 14 * u
	var weapons: Array = _stats.get("weapons", [])
	if weapons.is_empty():
		S.text(_board, S.num_medium(), Vector2(x, y + 24 * u), "No shots yet", int(19 * u), Color(S.DIM, 0.9))
		y += STATS_ROW * u
	for wr in weapons:
		var ry := y + STATS_ROW * u
		var tex := S.weapon_icon(int(wr["weapon"]))
		if tex:
			_board.draw_texture_rect(tex, Rect2(x, ry - 24 * u, 26 * u, 26 * u), false)
		S.text(_board, S.num_medium(), Vector2(x + 36 * u, ry - 4 * u), String(wr["name"]), int(19 * u), S.INK)
		S.text(_board, S.num_medium(), Vector2(x + w * 0.52, ry - 4 * u), str(wr["damage"]), int(20 * u), S.INK_HOT, HORIZONTAL_ALIGNMENT_CENTER)
		var a := float(wr["accuracy"])
		S.text(_board, S.num_medium(), Vector2(x + w * 0.72, ry - 4 * u), "%d / %d" % [wr["hits"], wr["shots"]] if a >= 0.0 else str(wr["hits"]),
			int(18 * u), Color(S.INK, 0.8), HORIZONTAL_ALIGNMENT_CENTER)
		if a >= 0.0:
			var bar := Rect2(x + w * 0.85, ry - 15 * u, w * 0.15, 6 * u)
			S.bar(_board, bar, 0.0, 1.0, Color(1, 1, 1, 0.08))
			S.bar(_board, bar, 0.0, a, S.AMBER if a >= 0.5 else S.INK)
			S.text(_board, S.num_medium(), Vector2(x + w * 0.93, ry - 18 * u), "%d%%" % roundi(a * 100.0), int(15 * u), S.INK_HOT,
				HORIZONTAL_ALIGNMENT_CENTER)
		y += STATS_ROW * u
	# distance
	y += 28 * u
	_section(Vector2(x, y), w, "DISTANCE", u)
	var m := float(_stats.get("distance_m", 0.0))
	S.text(_board, S.num(), Vector2(x, y + 44 * u), ("%.2f km" % (m / 1000.0)) if m >= 1000.0 else ("%d m" % roundi(m)), int(30 * u), S.INK_HOT)
	y += 74 * u
	# medals
	_section(Vector2(x, y), w, "MEDALS", u)
	var mx := x
	for md in _stats.get("medals", []):
		var n := int(md["count"])
		var tex := S.icon(String(md["icon"]))
		var col := Color(1, 1, 1, 1.0 if n > 0 else 0.22)
		if tex:
			_board.draw_texture_rect(tex, Rect2(mx, y + 18 * u, 46 * u, 46 * u), false, col)
		S.text(_board, S.num_medium(), Vector2(mx + 23 * u, y + 86 * u), str(n), int(18 * u), S.INK_HOT if n > 0 else Color(S.DIM, 0.6),
			HORIZONTAL_ALIGNMENT_CENTER)
		S.text(_board, S.label(), Vector2(mx + 23 * u, y + 104 * u), String(md["name"]).to_upper(), int(10 * u),
			Color(S.DIM, 0.85 if n > 0 else 0.5), HORIZONTAL_ALIGNMENT_CENTER)
		mx += w / 6.0


func _section(at: Vector2, w: float, title: String, u: float) -> void:
	S.text(_board, S.label(), at, title, int(15 * u), S.RED)
	var tw := S.text_width(S.label(), title, int(15 * u))
	_board.draw_line(Vector2(at.x + tw + 10 * u, at.y - 5 * u), Vector2(at.x + w, at.y - 5 * u), Color(S.RED, 0.35), 1.0)
