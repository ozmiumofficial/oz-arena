class_name Q3ReplayViewer
extends CanvasLayer
## The replay viewer's controls (REPLAY_PLAN.md R3), drawn by Godot in the HUD's style rather than
## the web menu, so the keyboard never changes hands. Gameplay (in replay mode) passes it every
## input first (handle_input).
##   Transport bar: play/pause, speed 0.25x to 4x, frame step, back/forward 5 s, previous/next
##   frag, and a timeline to click or drag. Its markers: the watched player's frags (amber) and
##   deaths (red), everyone else's frags (faint), flag events (team colored), medals (white).
##   Cameras: first person (their eyes, HUD and gun), chase, free fly (right mouse to look, the
##   movement keys to fly), orbit (right mouse to turn it) and cinematic (Q3CineCam: shots that
##   move and cut by themselves; an exported clip picks its own from the range's start).
##   The player list (also the bar's name button) picks who to watch; toggles for the HUD, name
##   tags over players and outlines around them.
##   Clip export (R8): IN and OUT mark a range (Home, End), EXPORT (F9) renders it from the camera
##   on show to user://clips at 60 fps with Q3ClipExport; the range is shaded on the timeline.
##   While it renders the button (and F9) is CANCEL.
## Every key is in Settings > Controls > Replays.

const S := preload("res://scenes/gameplay/hud/hud_style.gd")
const E := preload("res://scripts/game/q3_enums.gd")
const SPEEDS := [0.25, 0.5, 1.0, 2.0, 4.0]
const CAMS := [["first", "FIRST PERSON"], ["chase", "CHASE"], ["free", "FREE"], ["orbit", "ORBIT"], ["cinematic", "CINEMATIC"]]
const SKIP_MS := 5000.0
const FRAG_LEAD_MS := 1500.0     ## previous/next frag lands this long before it, so you see it happen
const OUTLINE := preload("res://shaders/outline.gdshader")
const BAR_HEIGHT := 128.0        ## the transport bar, 1080p units
const MARGIN := 28.0
const STRIP := 56.0              ## the clip strip over the bar's right end

var game: Q3Gameplay
var picking := false             ## the player list is open
var names := false
var outlines := false
var bar_shown := true
var chrome := true               ## the panels and the bar (a clip render draws only name tags)
var _root: Control
var _buttons: Array = []         ## [Rect2, Callable] this frame
var _rows: Array = []            ## the player list: [Rect2, client]
var _timeline := Rect2()
var _dragging := false
var _mouse := Vector2(-1, -1)
var _outline_mats: Dictionary = {}   ## color html -> ShaderMaterial
var _outlined: Array = []        ## meshes wearing an outline
var _outline_of: Dictionary = {} ## body instance id -> the outline it wears
var _events: Array = []
var mark_in := -1.0              ## the clip's start and end (server ms), -1 unset
var mark_out := -1.0
var _export: Q3ClipExport
var _export_noted := true          ## the replay's timeline events (Q3Replay.events), read once


func _init() -> void:
	layer = 12


func _ready() -> void:
	_root = Control.new()
	_root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_root)
	_root.draw.connect(_draw_viewer)


func _process(_delta: float) -> void:
	if outlines:
		_apply_outlines()
	if _export and not _export_noted and not _export.running():
		_export_noted = true
		var why := _export.result()
		game.hud.notify(("Clip saved: %s (%.1f MB)" % [_export.path, _export.size() / 1048576.0]) if why == ""
			else "The clip didn't finish: %s." % why)
	# the HUD's bottom row moves up over the bar
	var u := _root.get_viewport_rect().size.y / 1080.0
	var inset := (BAR_HEIGHT + 2.0 * MARGIN + STRIP) * u if bar_shown else 0.0
	if game and game.hud and absf(game.hud.bottom_inset - inset) > 0.5:
		game.hud.bottom_inset = inset
		game.hud.layout()
	_root.queue_redraw()


# ---------------------------------------------------------------- actions

func toggle_play() -> void:
	var f := game.replay_feed
	if not game.replay_playing and f.at_end():
		game.seek_to(f.start_time())
	game.replay_playing = not game.replay_playing
	UiSound.play("move")


func skip(ms: float) -> void:
	game.seek_to(game.replay_feed.playhead + ms)


func change_speed(dir: int) -> void:
	var i := SPEEDS.find(game.replay_speed)
	i = clampi((i if i >= 0 else 2) + dir, 0, SPEEDS.size() - 1)
	if SPEEDS[i] == game.replay_speed:
		UiSound.play("buzz")
		return
	game.replay_speed = SPEEDS[i]
	UiSound.play("move")


## The previous or next frag in the match (anyone's), landing just before it.
func frag(dir: int) -> void:
	var now := game.replay_feed.playhead
	var times: Array = []
	if _events.is_empty():
		_events = game.replay_feed.replay.events()
	for e in _events:
		if int(e["type"]) == 1:
			times.append(float(e["time"]))
	var pick := -1.0
	if dir > 0:
		for t in times:
			if t - FRAG_LEAD_MS > now + 50.0:
				pick = t
				break
	else:
		for i in range(times.size() - 1, -1, -1):
			if times[i] - FRAG_LEAD_MS < now - 600.0:
				pick = times[i]
				break
	if pick < 0.0:
		UiSound.play("buzz")
		return
	game.seek_to(pick - FRAG_LEAD_MS)
	UiSound.play("move")


func next_camera() -> void:
	var i := CAMS.map(func(c): return c[0]).find(game.cam_mode)
	game.set_cam_mode(CAMS[(i + 1) % CAMS.size()][0])
	UiSound.play("move")


func next_player(dir: int) -> void:
	var cls := Array(game.replay_feed.clients())
	if cls.is_empty():
		return
	var i := cls.find(game.me)
	game.watch(cls[(i + dir + cls.size()) % cls.size()])
	UiSound.play("move")


## Every input reaches here first; true when the viewer used it.
func handle_input(event: InputEvent) -> bool:
	if (event is InputEventKey or event is InputEventAction or event is InputEventJoypadButton) and event.is_pressed() and not event.is_echo():
		var acts := {"q3_replay_play": toggle_play, "q3_replay_back": skip.bind(-SKIP_MS),
			"q3_replay_forward": skip.bind(SKIP_MS), "q3_replay_slower": change_speed.bind(-1),
			"q3_replay_faster": change_speed.bind(1), "q3_replay_step_back": game.step_frame.bind(-1),
			"q3_replay_step_forward": game.step_frame.bind(1), "q3_replay_prev_frag": frag.bind(-1),
			"q3_replay_next_frag": frag.bind(1), "q3_replay_camera": next_camera,
			"q3_replay_prev_player": next_player.bind(-1), "q3_replay_next_player": next_player.bind(1),
			"q3_replay_players": func(): picking = not picking,
			"q3_replay_hud": func(): game.hud.visible = not game.hud.visible,
			"q3_replay_names": func(): names = not names,
			"q3_replay_outlines": func(): set_outlines(not outlines),
			"q3_replay_bar": func(): bar_shown = not bar_shown,
			"q3_replay_mark_in": mark.bind(true), "q3_replay_mark_out": mark.bind(false), "q3_replay_export": export_clip}
		for a in acts:
			if InputMap.has_action(a) and event.is_action_pressed(a):
				(acts[a] as Callable).call()
				return true
		if picking and event.is_action_pressed("ui_cancel"):
			picking = false
			return true
		return false
	if event is InputEventMouseMotion:
		_mouse = event.position
		if _dragging:
			_seek_at(_mouse.x)
			return true
		return false
	var mb := event as InputEventMouseButton
	if mb and mb.button_index == MOUSE_BUTTON_LEFT:
		if not mb.pressed:
			var was := _dragging
			_dragging = false
			return was
		_mouse = mb.position
		if picking:
			for r in _rows:
				if (r[0] as Rect2).has_point(_mouse):
					game.watch(int(r[1]))
					picking = false
					UiSound.play("move")
					return true
		if bar_shown and _timeline.grow(6.0).has_point(_mouse):
			_dragging = true
			_seek_at(_mouse.x)
			return true
		for b in _buttons:
			if (b[0] as Rect2).has_point(_mouse):
				(b[1] as Callable).call()
				return true
	return false


func _seek_at(x: float) -> void:
	var f := game.replay_feed
	var k := clampf((x - _timeline.position.x) / maxf(_timeline.size.x, 1.0), 0.0, 1.0)
	game.seek_to(f.start_time() + k * (f.end_time() - f.start_time()))


# ---------------------------------------------------------------- clip export (R8)

## Marks the clip's start or end at the playhead (an end before the start moves the start back).
func mark(start: bool) -> void:
	var f := game.replay_feed
	var t := f.playhead
	if start:
		mark_in = t
		if mark_out >= 0.0 and mark_out <= t:
			mark_out = -1.0
	else:
		mark_out = t
		if mark_in < 0.0 or mark_in >= t:
			mark_in = f.start_time()
	UiSound.play("move")


## Renders the marked range (all of it when nothing is marked) from the camera on show; while one
## renders, stops it.
func export_clip() -> void:
	if _export and _export.running():
		_export.cancel()
		UiSound.play("out")
		return
	var f := game.replay_feed
	var a := mark_in if mark_in >= 0.0 else float(f.start_time())
	var b := mark_out if mark_out >= 0.0 else float(f.end_time())
	_export = Q3ClipExport.new()
	var err := _export.start(String(Session.replay.get("path", "")), int(a), int(b), game.cam_mode, game.me, game.hud.visible,
		game.clip_view().merged({"names": names, "outlines": outlines}))
	if err != "":
		game.hud.notify("Clip: " + err)
		UiSound.buzz()
		_export = null
		return
	_export_noted = false
	game.replay_playing = false   # the render plays it in its own window
	game.hud.notify("Rendering the clip (%s) to %s" % [_clock(b - a), _export.path])
	UiSound.play_in()


# ---------------------------------------------------------------- name tags and outlines

func set_outlines(on: bool) -> void:
	outlines = on
	if not on:
		for mi in _outlined:
			if not is_instance_valid(mi):
				continue
			var g := mi as GeometryInstance3D
			if g.material_overlay in _outline_mats.values():
				g.material_overlay = null
			elif g.material_overlay and g.material_overlay.next_pass in _outline_mats.values():
				g.material_overlay.next_pass = null
		_outlined.clear()
		_outline_of.clear()


## Team colors in team modes; otherwise amber, with the watched player in white.
func _player_color(cl: int) -> Color:
	if game.feed.cvar("g_gametype").to_int() >= E.GT_TEAM:
		var t := String(Q3Host.parse_info(game.feed.configstring(E.CS_PLAYERS + cl)).get("t", "0"))
		return S.TEAM_RED if t == "1" else S.TEAM_BLUE if t == "2" else S.INK
	return Color.WHITE if cl == game.me else S.AMBER


## The outline is the body's overlay; where the overlay is already taken (the armor shell's
## flash, Q3Client.armor_hit), it rides as that material's next pass.
func _apply_outlines() -> void:
	var nodes := game.client.player_nodes()
	for cl in nodes:
		var ch: Q3Character = nodes[cl]
		var c := _player_color(cl)
		var mat: ShaderMaterial = _outline_mats.get(c.to_html())
		if mat == null:
			mat = ShaderMaterial.new()
			mat.shader = OUTLINE
			mat.set_shader_parameter("color", c)
			_outline_mats[c.to_html()] = mat
		if _outline_of.get(ch.get_instance_id()) == mat:
			continue
		_outline_of[ch.get_instance_id()] = mat
		for mi in ch.find_children("*", "MeshInstance3D", true, false):
			var g := mi as GeometryInstance3D
			if String(mi.get_path()).contains("Attach_"):
				continue
			if g.material_overlay == null or g.material_overlay in _outline_mats.values():
				g.material_overlay = mat
			else:
				g.material_overlay.next_pass = mat
			_outlined.append(g)
	_outlined = _outlined.filter(func(g): return is_instance_valid(g))


func _draw_names(u: float) -> void:
	var cam := game.cam
	var nodes := game.client.player_nodes()
	for cl in nodes:
		if cl == game.me and game.cam_mode == "first":
			continue
		var at: Vector3 = (nodes[cl] as Node3D).global_position + Vector3.UP * 2.05
		if cam.is_position_behind(at):
			continue
		var p := cam.unproject_position(at)
		var name := String(Q3Host.parse_info(game.feed.configstring(E.CS_PLAYERS + cl)).get("n", "?"))
		var size := int(clampf(22.0 * u * 12.0 / maxf(cam.global_position.distance_to(at), 4.0), 12.0 * u, 24.0 * u))
		S.ctext(_root, S.label(), p, name, size, _player_color(cl), HORIZONTAL_ALIGNMENT_CENTER, 0.6)


# ---------------------------------------------------------------- drawing

static func _clock(ms: float) -> String:
	var s := int(maxf(ms, 0.0) / 1000.0)
	return "%d:%02d" % [s / 60, s % 60]


func _button(r: Rect2, label: String, action: Callable, on := false, size := 0) -> void:
	var hover := r.has_point(_mouse)
	var u := r.size.y / 40.0
	S.panel(_root, r, Color(1, 1, 1, 0.12) if on else (Color(1, 1, 1, 0.07) if hover else Color(0, 0, 0, 0.0)))
	S.text(_root, S.label(), Vector2(r.get_center().x, r.position.y + r.size.y * 0.68), label, size if size > 0 else int(17 * u),
		S.INK_HOT if hover or on else S.INK, HORIZONTAL_ALIGNMENT_CENTER)
	_buttons.append([r, action])


func _draw_viewer() -> void:
	_buttons.clear()
	_rows.clear()
	if game == null or game.replay_feed == null or game.replay_feed.replay == null:
		return
	var vs := _root.get_viewport_rect().size
	var u := vs.y / 1080.0
	var f := game.replay_feed
	var m := MARGIN * u
	if names:
		_draw_names(u)
	if not chrome:
		return
	# who and how, top left
	var who := String(Q3Host.parse_info(game.feed.configstring(E.CS_PLAYERS + game.me)).get("n", "?"))
	var cam_name: String = CAMS[CAMS.map(func(c): return c[0]).find(game.cam_mode)][1]
	var top := Rect2(Vector2(m, m), Vector2(560 * u, 74 * u))
	S.panel(_root, top, S.GLASS, S.GLOW, 3 * u)
	S.text(_root, S.label(), top.position + Vector2(28 * u, 30 * u), "REPLAY  -  " + String(f.header.get("title", "")), int(18 * u), S.DIM)
	S.ctext(_root, S.label(), top.position + Vector2(28 * u, 60 * u), "WATCHING %s^7   %s" % [who, cam_name], int(22 * u), S.INK_HOT)
	if picking:
		_draw_players(u, Vector2(m, top.end.y + 14 * u))
	if not bar_shown:
		return
	# the clip strip over the bar's right end
	var strip := Rect2(Vector2(vs.x - m - 470 * u, vs.y - BAR_HEIGHT * u - m - STRIP * u), Vector2(470 * u, STRIP * u - 8 * u))
	S.panel(_root, strip, Color(0.04, 0.03, 0.027, 0.78))
	var rendering := _export != null and _export.running()
	var label := "RENDERING..." if rendering else "CLIP  " + (_clock((mark_out if mark_out >= 0.0 else float(f.end_time())) - (mark_in if mark_in >= 0.0 else float(f.start_time()))))
	S.text(_root, S.label(), strip.position + Vector2(26 * u, 31 * u), label, int(16 * u), S.AMBER if rendering else S.DIM)
	var bx := strip.end.x - 18 * u
	for b in [["CANCEL" if rendering else "EXPORT", export_clip, false], ["OUT", mark.bind(false), mark_out >= 0.0],
			["IN", mark.bind(true), mark_in >= 0.0]]:
		var bw := S.text_width(S.label(), b[0], int(16 * u)) + 30 * u
		bx -= bw
		_button(Rect2(Vector2(bx, strip.position.y + 4 * u), Vector2(bw, strip.size.y - 8 * u)), b[0], b[1], b[2], int(16 * u))
		bx -= 6 * u
	# the transport bar
	var bar := Rect2(Vector2(m, vs.y - BAR_HEIGHT * u - m), Vector2(vs.x - 2 * m, BAR_HEIGHT * u))
	S.panel(_root, bar, Color(0.04, 0.03, 0.027, 0.78))
	_timeline = Rect2(Vector2(bar.position.x + 60 * u, bar.position.y + 26 * u), Vector2(bar.size.x - 120 * u, 10 * u))
	_draw_timeline(u)
	var y := bar.position.y + 58 * u
	var h := 44 * u
	var x := bar.position.x + 48 * u
	var w := 64 * u
	var transport := [["|<<", frag.bind(-1)], ["-5s", skip.bind(-SKIP_MS)], ["<", game.step_frame.bind(-1)],
		["PAUSE" if game.replay_playing else ("AGAIN" if f.at_end() else "PLAY"), toggle_play], [">", game.step_frame.bind(1)],
		["+5s", skip.bind(SKIP_MS)], [">>|", frag.bind(1)]]
	for b in transport:
		var bw: float = w * (1.6 if String(b[0]).length() > 3 else 1.0)
		_button(Rect2(Vector2(x, y), Vector2(bw, h)), b[0], b[1], false, int(19 * u))
		x += bw + 6 * u
	x += 10 * u
	_button(Rect2(Vector2(x, y), Vector2(w * 1.1, h)), ("%sx" % str(game.replay_speed)).replace(".0x", "x"),
		func(): change_speed(1 if game.replay_speed < SPEEDS[-1] else -(SPEEDS.size() - 1)), false, int(19 * u))
	x += w * 1.1 + 18 * u
	S.text(_root, S.num_medium(), Vector2(x, y + h * 0.7), "%s / %s" % [_clock(f.playhead - f.start_time()),
		_clock(f.end_time() - f.start_time())], int(24 * u), S.INK)
	# cameras, the player list and the toggles, right
	var rx := bar.end.x - 48 * u
	var toggles := [["OUTLINES", func(): set_outlines(not outlines), outlines], ["NAMES", func(): names = not names, names],
		["HUD", func(): game.hud.visible = not game.hud.visible, game.hud.visible],
		["PLAYERS", func(): picking = not picking, picking]]
	for t in toggles:
		var tw := S.text_width(S.label(), t[0], int(17 * u)) + 34 * u
		rx -= tw
		_button(Rect2(Vector2(rx, y), Vector2(tw, h)), t[0], t[1], t[2])
		rx -= 6 * u
	rx -= 18 * u
	for i in range(CAMS.size() - 1, -1, -1):
		var c: Array = CAMS[i]
		var cw := S.text_width(S.label(), c[1], int(17 * u)) + 34 * u
		rx -= cw
		_button(Rect2(Vector2(rx, y), Vector2(cw, h)), c[1], game.set_cam_mode.bind(c[0]), game.cam_mode == c[0])
		rx -= 6 * u


func _draw_timeline(u: float) -> void:
	var f := game.replay_feed
	var tl := _timeline
	var span := maxf(f.end_time() - f.start_time(), 1.0)
	_root.draw_rect(tl, Color(1, 1, 1, 0.10))
	var k := clampf((f.playhead - f.start_time()) / span, 0.0, 1.0)
	_root.draw_rect(Rect2(tl.position, Vector2(tl.size.x * k, tl.size.y)), Color(S.GLOW, 0.75))
	if mark_in >= 0.0 or mark_out >= 0.0:
		# the clip's range
		var a := clampf(((mark_in if mark_in >= 0.0 else float(f.start_time())) - f.start_time()) / span, 0.0, 1.0)
		var b := clampf(((mark_out if mark_out >= 0.0 else float(f.end_time())) - f.start_time()) / span, 0.0, 1.0)
		var cr := Rect2(tl.position.x + tl.size.x * a, tl.position.y - 4 * u, tl.size.x * (b - a), tl.size.y + 8 * u)
		_root.draw_rect(cr, Color(S.AMBER, 0.18))
		_root.draw_rect(cr, Color(S.AMBER, 0.8), false, 1.5 * u)
	if _events.is_empty():
		_events = f.replay.events()
	for e in _events:
		var x := tl.position.x + tl.size.x * clampf((float(e["time"]) - f.start_time()) / span, 0.0, 1.0)
		match int(e["type"]):
			1:   # obituary: a = attacker, b = target
				var mine := int(e["a"]) == game.me and int(e["b"]) != game.me
				var died := int(e["b"]) == game.me
				var c := S.AMBER if mine else (S.RED if died else Color(1, 1, 1, 0.22))
				var hgt := (12.0 if mine or died else 6.0) * u
				_root.draw_rect(Rect2(Vector2(x - 1.5 * u, tl.position.y - hgt - 2 * u), Vector2(3 * u, hgt)), c)
			2:   # flag events: even GTS_ values are red's, odd blue's
				var c := S.TEAM_RED if int(e["a"]) % 2 == 0 else S.TEAM_BLUE
				_root.draw_colored_polygon(PackedVector2Array([Vector2(x, tl.end.y + 2 * u), Vector2(x + 5 * u, tl.end.y + 7 * u),
					Vector2(x, tl.end.y + 12 * u), Vector2(x - 5 * u, tl.end.y + 7 * u)]), c)
			3:   # the watched player's medals
				if int(e["a"]) == game.me:
					_root.draw_circle(Vector2(x, tl.end.y + 7 * u), 3.5 * u, Color.WHITE)
	var hx := tl.position.x + tl.size.x * k
	_root.draw_rect(Rect2(Vector2(hx - 2 * u, tl.position.y - 6 * u), Vector2(4 * u, tl.size.y + 12 * u)), S.INK_HOT)
	if tl.grow(6.0).has_point(_mouse):
		S.text(_root, S.num_medium(), Vector2(_mouse.x, tl.position.y - 18 * u),
			_clock((_mouse.x - tl.position.x) / tl.size.x * span), int(18 * u), S.INK_HOT, HORIZONTAL_ALIGNMENT_CENTER)


func _draw_players(u: float, at: Vector2) -> void:
	var rows := game.standings()
	var r := Rect2(at, Vector2(420 * u, (rows.size() * 40 + 56) * u))
	S.panel(_root, r, Color(0.04, 0.03, 0.027, 0.86))
	S.text(_root, S.label(), at + Vector2(28 * u, 34 * u), "WATCH WHO  (%s)" % _bind_name("replay_players"), int(17 * u), S.DIM)
	var y := at.y + 50 * u
	for row in rows:
		var rr := Rect2(Vector2(at.x + 14 * u, y), Vector2(r.size.x - 40 * u, 36 * u))
		var on: bool = int(row["client"]) == game.me
		if on or rr.has_point(_mouse):
			S.panel(_root, rr, Color(1, 1, 1, 0.12 if on else 0.06))
		S.ctext(_root, S.label(), rr.position + Vector2(18 * u, 26 * u), String(row["name"]), int(21 * u), S.INK_HOT if on else S.INK)
		S.text(_root, S.num_medium(), Vector2(rr.end.x - 12 * u, rr.position.y + 26 * u), str(row["score"]), int(21 * u), S.INK,
			HORIZONTAL_ALIGNMENT_RIGHT)
		_rows.append([rr, int(row["client"])])
		y += 40 * u


static func _bind_name(id: String) -> String:
	var b = Settings.get_value("controls/" + id)
	return String(b[0]).get_slice(":", 1) if b is Array and b.size() and String(b[0]) != "" else ""
