class_name Q3ResultsScreen
extends CanvasLayer
## End of match: VICTORY / DEFEAT, the standings as player cards, the medals you earned (Quake 3's
## award icons), then the XP you gained line by line and the XP bar filling, with a flash for every
## level crossed. Continue (Enter, Space, click) emits `finished`.
##   show_results({won, title, subtitle, standings: [{name, score, card, level, local}],
##                 medals: {impressive: n, ...}, xp: Q3Profile.award_match() result})

signal finished
signal watch_requested   ## Watch replay (R): the match's replay, now

## Quake 3's medal art (library/ui/hud: menu/medals at 2x)
const MEDALS := {"impressive": "medal_impressive", "excellent": "medal_excellent", "gauntlet": "medal_gauntlet",
	"defend": "medal_defend", "assist": "medal_assist", "captures": "medal_capture", "accuracy": "medal_accuracy",
	"frags": "medal_frags", "perfect": "medal_victory"}

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


func _init() -> void:
	layer = 30


func show_results(r: Dictionary) -> void:
	_root = Control.new()
	_root.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(_root)
	var bg := ColorRect.new()
	bg.color = Color(0.02, 0.015, 0.015, 0.0)
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	_root.add_child(bg)
	create_tween().tween_property(bg, "color:a", 0.86, 0.6)

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
	col.position = Vector2(view.x * 0.06, view.y * 0.25)
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
		card.custom_minimum_size = Vector2(view.x * 0.32, view.y * 0.075)
		card.highlight = s.get("local", false)
		row.add_child(card)
		var score := _label(str(s.get("score", 0)), int(view.y * 0.045), Color.WHITE)
		score.custom_minimum_size = Vector2(view.y * 0.1, 0)
		score.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		score.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		row.add_child(score)
		row.modulate.a = 0.0
		col.add_child(row)
		var t := create_tween()
		t.tween_interval(0.35 + i * 0.12)
		t.tween_property(row, "modulate:a", 1.0, 0.25)
		i += 1

	# medals
	var medals := HBoxContainer.new()
	medals.add_theme_constant_override("separation", 18)
	medals.position = Vector2(view.x * 0.56, view.y * 0.25)
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
	_xp_lines.position = Vector2(view.x * 0.56, view.y * 0.42)
	_xp_lines.custom_minimum_size = Vector2(view.x * 0.36, 0)
	_root.add_child(_xp_lines)
	var bar_y := view.y * 0.78
	_level_label = _label("", int(view.y * 0.05), Color.WHITE)
	_level_label.position = Vector2(view.x * 0.56, bar_y - view.y * 0.075)
	_root.add_child(_level_label)
	_xp_label = _label("", int(view.y * 0.022), Color(1, 1, 1, 0.75))
	_xp_label.position = Vector2(view.x * 0.56, bar_y + view.y * 0.035)
	_root.add_child(_xp_label)
	_bar_back = ColorRect.new()
	_bar_back.color = Color(1, 1, 1, 0.08)
	_bar_back.position = Vector2(view.x * 0.56, bar_y)
	_bar_back.size = Vector2(view.x * 0.36, view.y * 0.025)
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


func _label(text: String, size: int, color: Color) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_override("font", Q3Fonts.prop())
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_color", color)
	l.add_theme_color_override("font_outline_color", Color.BLACK)
	l.add_theme_constant_override("outline_size", 4)
	return l


func _unhandled_input(event: InputEvent) -> void:
	if _done or _root == null:
		return
	var key := event as InputEventKey
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
		_done = true
		UiSound.play("move")
		finished.emit()
