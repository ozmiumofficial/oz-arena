class_name Q3ResultsScreen
extends CanvasLayer
## End of match: VICTORY / DEFEAT, the standings as player cards, the medals you earned (Quake 3's
## award icons), then the XP you gained line by line and the XP bar filling, with a flash for every
## level crossed. Continue (Enter, Space, click) emits `finished`. In the middle, the podium (Q3PodiumStage) with
## the top three on it (Q3PodiumStage, in the map in front of `camera`); if you're one of them, the podium's three taunts are buttons under it (and keys 1-3), which
## emit `taunt_requested` for Gameplay to send, and every podium taunt comes back through play_taunt().
##   show_results({won, title, subtitle, standings: [{client, name, score, card, level, local, model, skin, bot}],
##                 medals: {impressive: n, ...}, xp: Q3Profile.award_match() result, me: local client, camera})

signal finished
signal watch_requested   ## Watch replay (R): the match's replay, now
signal taunt_requested(id: String)   ## a podium taunt button (or 1-3): Gameplay sends "ptaunt <id>"

## Quake 3's medal art (library/ui/hud: menu/medals at 2x)
const MEDALS := {"impressive": "medal_impressive", "excellent": "medal_excellent", "gauntlet": "medal_gauntlet",
	"defend": "medal_defend", "assist": "medal_assist", "captures": "medal_capture", "accuracy": "medal_accuracy",
	"frags": "medal_frags", "perfect": "medal_victory"}

## the right column (medals and XP), as fractions of the screen's width
const RIGHT := 0.715
const RIGHT_W := 0.26

var _root: Control
var _bar: ColorRect
var _bar_back: ColorRect
var _level_label: Label
var _xp_label: Label
var _xp_lines: VBoxContainer
var _done := false
var _can_continue := false
var _replay := ""                 ## the match's replay file, "" if it wasn't recorded
var _saved := false
var _watch_button: Button
var _save_button: Button
var _podium: Q3PodiumStage
var _taunts: Array = []          ## the podium taunts' ids (Q3PodiumStage.TAUNTS), when you're on the podium
var _taunt_wait := 0.0


func _init() -> void:
	layer = 30


func show_results(r: Dictionary) -> void:
	_root = Control.new()
	_root.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(_root)
	# the map shows through: the camera is at its intermission spot (Quake 3 moves everyone there), under a light
	# tint, with darker panels behind the standings and the XP so they stay readable
	var bg := ColorRect.new()
	bg.color = Color(0.02, 0.015, 0.015, 0.0)
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	_root.add_child(bg)
	create_tween().tween_property(bg, "color:a", 0.3, 0.6)
	var view0 := _root.get_viewport_rect().size
	for side in [[0.0, 0.36, false], [0.64, 0.36, true]]:   # [x, width, fades toward the left]
		var g := Gradient.new()
		g.set_color(0, Color(0.02, 0.015, 0.015, 0.0 if side[2] else 0.7))
		g.set_color(1, Color(0.02, 0.015, 0.015, 0.7 if side[2] else 0.0))
		g.add_point(0.75 if not side[2] else 0.25, Color(0.02, 0.015, 0.015, 0.6))
		var tex := GradientTexture2D.new()
		tex.gradient = g
		tex.width = 256
		tex.height = 4
		var panel := TextureRect.new()
		panel.texture = tex
		panel.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		panel.stretch_mode = TextureRect.STRETCH_SCALE
		panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
		panel.position = Vector2(view0.x * side[0], 0.0)
		panel.size = Vector2(view0.x * side[1], view0.y)
		panel.modulate.a = 0.0
		_root.add_child(panel)
		create_tween().tween_property(panel, "modulate:a", 1.0, 0.6)

	var view := _root.get_viewport_rect().size
	var won: bool = r.get("won", false)
	var head := Label.new()
	head.text = String(r.get("title", "VICTORY" if won else "DEFEAT"))
	head.add_theme_font_override("font", Q3Fonts.banner())
	head.add_theme_font_size_override("font_size", int(view.y * 0.11))
	head.add_theme_color_override("font_color", Color(1.0, 0.82, 0.3) if won else Color(0.85, 0.12, 0.08))
	head.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	head.position = Vector2(0, view.y * 0.04)
	head.size = Vector2(view.x, view.y * 0.13)
	head.pivot_offset = head.size * 0.5
	head.scale = Vector2(1.4, 1.4)
	head.modulate.a = 0.0
	_root.add_child(head)
	var tw := create_tween().set_parallel()
	tw.tween_property(head, "scale", Vector2.ONE, 0.45).set_trans(Tween.TRANS_BACK).set_ease(Tween.EASE_OUT)
	tw.tween_property(head, "modulate:a", 1.0, 0.3)

	var sub := _label(String(r.get("subtitle", "")), int(view.y * 0.028), Color(1, 1, 1, 0.75))
	sub.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	sub.position = Vector2(0, view.y * 0.17)
	sub.size = Vector2(view.x, view.y * 0.04)
	_root.add_child(sub)

	# standings: one card per player, sliding in one after another
	var col := VBoxContainer.new()
	col.add_theme_constant_override("separation", int(view.y * 0.01))
	col.position = Vector2(view.x * 0.03, view.y * 0.25)
	_root.add_child(col)
	var i := 0
	for s in r.get("standings", []):
		var row := HBoxContainer.new()
		row.add_theme_constant_override("separation", 14)
		var place := _label("%d" % (i + 1), int(view.y * 0.04), Color(1, 1, 1, 0.8))
		place.custom_minimum_size = Vector2(view.y * 0.05, 0)
		place.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		row.add_child(place)
		var card := Q3CardView.new().setup(s.get("card", {}), String(s.get("name", "")), int(s.get("level", 1)))
		card.custom_minimum_size = Vector2(view.x * 0.19, view.y * 0.075)
		card.highlight = s.get("local", false)
		row.add_child(card)
		var score := _label(str(s.get("score", 0)), int(view.y * 0.045), Color.WHITE)
		score.custom_minimum_size = Vector2(view.y * 0.07, 0)
		score.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		score.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		row.add_child(score)
		row.modulate.a = 0.0
		col.add_child(row)
		var t := create_tween()
		t.tween_interval(0.35 + i * 0.12)
		t.tween_property(row, "modulate:a", 1.0, 0.25)
		i += 1

	_build_podium(r, view)

	# medals
	var medals := HBoxContainer.new()
	medals.add_theme_constant_override("separation", 18)
	medals.position = Vector2(RIGHT * view.x, view.y * 0.25)
	_root.add_child(medals)
	var m_i := 0
	for k in r.get("medals", {}):
		var n := int(r["medals"][k])
		if n <= 0 or not MEDALS.has(k):
			continue
		var box := VBoxContainer.new()
		var icon := TextureRect.new()
		var path := Q3.LIB + "ui/hud/%s.png" % MEDALS[k]
		if ResourceLoader.exists(path):
			icon.texture = load(path)
		icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		icon.custom_minimum_size = Vector2.ONE * view.y * 0.08
		box.add_child(icon)
		var cnt := _label(("Frags %d" % n) if k == "frags" else ("%s x%d" % [String(k).capitalize(), n]), int(view.y * 0.02), Color(1, 0.85, 0.4))
		cnt.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		box.add_child(cnt)
		box.scale = Vector2.ZERO
		medals.add_child(box)
		var t := create_tween()
		t.tween_interval(0.9 + m_i * 0.15)
		t.tween_property(box, "scale", Vector2.ONE, 0.3).set_trans(Tween.TRANS_BACK).set_ease(Tween.EASE_OUT)
		m_i += 1

	# XP: the lines, then the bar
	_xp_lines = VBoxContainer.new()
	_xp_lines.position = Vector2(RIGHT * view.x, view.y * 0.42)
	_xp_lines.custom_minimum_size = Vector2(view.x * RIGHT_W, 0)
	_root.add_child(_xp_lines)
	var bar_y := view.y * 0.78
	_level_label = _label("", int(view.y * 0.05), Color.WHITE)
	_level_label.position = Vector2(RIGHT * view.x, bar_y - view.y * 0.075)
	_root.add_child(_level_label)
	_xp_label = _label("", int(view.y * 0.022), Color(1, 1, 1, 0.75))
	_xp_label.position = Vector2(RIGHT * view.x, bar_y + view.y * 0.035)
	_root.add_child(_xp_label)
	_bar_back = ColorRect.new()
	_bar_back.color = Color(1, 1, 1, 0.08)
	_bar_back.position = Vector2(RIGHT * view.x, bar_y)
	_bar_back.size = Vector2(view.x * RIGHT_W, view.y * 0.025)
	_root.add_child(_bar_back)
	_bar = ColorRect.new()
	_bar.color = Color(1.0, 0.7, 0.15)
	_bar.position = _bar_back.position
	_bar.size = Vector2(0, _bar_back.size.y)
	_root.add_child(_bar)
	var hint := _label("ENTER  CONTINUE", int(view.y * 0.022), Color(1, 1, 1, 0.5))
	hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	hint.position = Vector2(0, view.y * 0.92)
	hint.size = Vector2(view.x, view.y * 0.04)
	_root.add_child(hint)
	_replay = String(r.get("replay", ""))
	if _replay != "":
		_replay_buttons(view)
	_animate_xp(r.get("xp", {}))


## The podium (the top three of the standings) in the middle, and the local player's taunts under it if they're on it.
func _build_podium(r: Dictionary, view: Vector2) -> void:
	var rows: Array = (r.get("standings", []) as Array).slice(0, 3)
	var cam: Camera3D = r.get("camera")
	if rows.is_empty() or cam == null:
		return
	_podium = Q3PodiumStage.new()   # in the map, in front of the match camera (it frames the podium from now on)
	add_child(_podium)
	_podium.setup(rows, cam)
	# drawn on top of the map (no wall can cut it), under the screen's text
	var vp := _podium.overlay(cam.get_world_3d(), Vector2i(get_viewport().get_visible_rect().size))
	add_child(vp)
	var over := TextureRect.new()
	over.texture = vp.get_texture()
	over.set_anchors_preset(Control.PRESET_FULL_RECT)
	over.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_root.add_child(over)
	_root.move_child(over, 3)   # after the tint and its two side panels
	var me := int(r.get("me", -1))
	if not _podium.has_player(me):
		return
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", int(view.y * 0.02))
	_root.add_child(row)
	for id in Q3PodiumStage.TAUNTS:
		_taunts.append(id)
		var b := _button("%d  %s" % [_taunts.size(), String(Q3PodiumStage.TAUNTS[id]).to_upper()], view)
		var n := _taunts.size() - 1
		b.pressed.connect(func(): _taunt(n))
		row.add_child(b)
	row.position = Vector2((view.x - row.get_combined_minimum_size().x) * 0.5, view.y * 0.805)


## Taunt n (0-2) of the local player's three: Gameplay sends it, and it comes back for everyone (play_taunt).
func _taunt(n: int) -> void:
	if n >= _taunts.size() or _taunt_wait > 0.0:
		return
	_taunt_wait = 1.5   # the server takes one every 1.5 s
	taunt_requested.emit(String(_taunts[n]))


## Holds the camera on the podium while the results show (Gameplay calls it after placing the camera itself).
func frame_camera(cam: Camera3D) -> bool:
	if _podium == null or not is_instance_valid(_podium):
		return false
	_podium.frame_camera(cam)
	return true


## A podium taunt from anyone (the "ptaunt <client> <id>" server command).
func play_taunt(client: int, id: String) -> void:
	if _podium:
		_podium.play_taunt(client, id)


func _process(delta: float) -> void:
	_taunt_wait = maxf(_taunt_wait - delta, 0.0)


## The match's replay (Gameplay > Record replays): watch it now, or keep it for good (a favorite,
## which auto-clean never deletes).
func _replay_buttons(view: Vector2) -> void:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", int(view.y * 0.03))
	_root.add_child(row)
	_watch_button = _button("R  WATCH REPLAY", view)
	_watch_button.pressed.connect(_watch)
	row.add_child(_watch_button)
	_save_button = _button("S  SAVE REPLAY", view)
	_save_button.pressed.connect(_save)
	row.add_child(_save_button)
	row.position = Vector2((view.x - row.get_combined_minimum_size().x) * 0.5, view.y * 0.865)


func _button(text: String, view: Vector2) -> Button:
	var b := Button.new()
	b.text = text
	b.flat = true
	b.add_theme_font_override("font", Q3Fonts.prop())
	b.add_theme_font_size_override("font_size", int(view.y * 0.026))
	b.add_theme_color_override("font_color", Color(1, 1, 1, 0.75))
	b.add_theme_color_override("font_hover_color", Color(1.0, 0.82, 0.3))
	b.focus_mode = Control.FOCUS_NONE
	b.mouse_entered.connect(func(): UiSound.play("move"))
	return b


func _watch() -> void:
	if _done:
		return
	_done = true
	UiSound.play("move")
	watch_requested.emit()


func _save() -> void:
	if _saved:
		return
	_saved = true
	Q3ReplayFile.set_favorite(_replay.get_file(), true)
	_save_button.text = "SAVED  -  KEPT FOREVER"
	UiSound.play_in()


func _animate_xp(xp: Dictionary) -> void:
	var before := int(xp.get("before", 0))
	var after := int(xp.get("after", before))
	_show_level(before)
	var t := create_tween()
	t.tween_interval(1.2)
	var running := before
	for l in xp.get("lines", []):
		var label := String(l[0])
		var amount := int(l[1])
		t.tween_callback(func(): _add_line(label, amount))
		t.tween_interval(0.35)
	# the bar fills through every level it crosses
	t.tween_method(_show_level, before, after, clampf(float(after - before) / 400.0, 0.8, 3.0)).set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_OUT)
	t.tween_callback(func(): _can_continue = true)


func _add_line(text: String, amount: int) -> void:
	var row := HBoxContainer.new()
	var a := _label(text, int(_root.get_viewport_rect().size.y * 0.026), Color(1, 1, 1, 0.85))
	a.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(a)
	var b := _label("+%d XP" % amount, int(_root.get_viewport_rect().size.y * 0.026), Color(1.0, 0.75, 0.2))
	row.add_child(b)
	row.modulate.a = 0.0
	_xp_lines.add_child(row)
	create_tween().tween_property(row, "modulate:a", 1.0, 0.2)
	UiSound.play("move")


var _shown_level := -1


func _show_level(total) -> void:
	var lv := Q3Cards.level_for(int(total))
	if _shown_level >= 0 and int(lv["level"]) > _shown_level:
		_level_up(int(lv["level"]))
	_shown_level = int(lv["level"])
	_level_label.text = "LEVEL %d" % int(lv["level"])
	_xp_label.text = "%d / %d XP" % [int(lv["xp"]), int(lv["next"])]
	_bar.size.x = _bar_back.size.x * clampf(float(lv["xp"]) / maxf(float(lv["next"]), 1.0), 0.0, 1.0)


func _level_up(lv: int) -> void:
	UiSound.play_in()
	var flash := _label("LEVEL UP  %d" % lv, int(_root.get_viewport_rect().size.y * 0.04), Color(1.0, 0.85, 0.3))
	flash.position = _level_label.position + Vector2(_bar_back.size.x * 0.45, 0)
	_root.add_child(flash)
	var t := create_tween().set_parallel()
	t.tween_property(flash, "position:y", flash.position.y - 40.0, 1.0)
	t.tween_property(flash, "modulate:a", 0.0, 1.0).set_delay(0.4)
	t.chain().tween_callback(flash.queue_free)
	_bar.color = Color.WHITE
	create_tween().tween_property(_bar, "color", Color(1.0, 0.7, 0.15), 0.5)


## The ranked result, when the hub's answer arrives (a few seconds after the match): "Rating 1000 -> 1020 (+20)".
func rank_note(text: String, good: bool) -> void:
	if _root == null:
		return
	var l := _label(text, 28, Color(0.55, 1.0, 0.65) if good else Color(1.0, 0.55, 0.45))
	l.set_anchors_preset(Control.PRESET_CENTER_BOTTOM)
	l.grow_horizontal = Control.GROW_DIRECTION_BOTH
	l.position.y -= 120.0
	_root.add_child(l)


func _label(text: String, size: int, color: Color) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_override("font", Q3Fonts.prop())
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_color", color)
	l.add_theme_color_override("font_outline_color", Color.BLACK)
	l.add_theme_constant_override("outline_size", 4)
	return l


## Continue, from outside (Gameplay's Escape): the same way out as Enter, so a campaign tier's movie
## still plays.
func continue_now() -> void:
	if _done:
		return
	_done = true
	UiSound.play("move")
	finished.emit()


func _unhandled_input(event: InputEvent) -> void:
	if _done or _root == null:
		return
	var key := event as InputEventKey
	if key and key.pressed and not key.echo and key.keycode >= KEY_1 and key.keycode <= KEY_3 and not _taunts.is_empty():
		_taunt(key.keycode - KEY_1)
		return
	if _replay != "" and key and key.pressed and not key.echo:
		if key.keycode == KEY_R:
			_watch()
			return
		if key.keycode == KEY_S:
			_save()
			return
	var go: bool = event.is_action_pressed("ui_accept") or (event is InputEventKey and event.pressed and event.keycode == KEY_SPACE) \
		or (event is InputEventMouseButton and event.pressed)
	if go:
		if not _can_continue:
			_can_continue = true   # a second press skips the count-up
			for t in get_tree().get_processed_tweens():
				t.custom_step(10.0)
			return
		continue_now()
