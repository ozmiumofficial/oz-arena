extends PanelContainer
## The map editor's tool strip down the left edge of the view (Photoshop's, Radiant's toolbar): one
## drawn icon per tool with its key in the corner, the curves menu, the entity tools, and at the
## bottom the current material (what new brushes and Paint use; a click shows it in the browser).

const K = preload("res://scenes/map_editor/q3_editor_keymap.gd")
const SIZE := 40

var editor: Q3MapEditor

var _buttons := {}        ## command -> Button
var _swatch: TextureRect
var _swatch_button: Button


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_STOP
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.03, 0.028, 0.028, 0.98)
	sb.border_color = Color(0.2, 0.17, 0.16)
	sb.border_width_right = 1
	sb.content_margin_left = 4
	sb.content_margin_right = 4
	sb.content_margin_top = 6
	sb.content_margin_bottom = 6
	add_theme_stylebox_override("panel", sb)
	# it scrolls when the window is too short for every tool (the browser docked under it)
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_SHOW_NEVER
	add_child(scroll)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 3)
	box.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.add_child(box)
	for t in [["select", "tool.select", "Select and move"], ["create", "tool.create", "Create brush"],
			["face", "tool.face", "Face / texture tool"], ["vertex", "tool.vertex", "Vertex tool: corners and curve control points"]]:
		box.add_child(_tool_button(t[0], t[1], t[2]))
	box.add_child(_curves_button())
	box.add_child(_rule())
	for t in [["spawn", "Spawn point"], ["jumppad", "Jump pad (click the pad, then where to land)"],
			["teleporter", "Teleporter (click the pad, then the exit)"], ["weapon", "Weapon pickup (, and . change it)"],
			["item", "Item pickup: health, armor, ammo, powerups (, and . change it)"], ["sound", "Ambient sound"],
			["light", "Light (Shift+click: a spot light aimed at the surface)"]]:
		box.add_child(_tool_button(t[0], "tool." + t[0], t[1]))
	var spacer := Control.new()
	spacer.size_flags_vertical = Control.SIZE_EXPAND_FILL
	spacer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	box.add_child(spacer)
	_swatch_button = Button.new()
	_swatch_button.custom_minimum_size = Vector2(SIZE, SIZE)
	_swatch_button.focus_mode = Control.FOCUS_NONE
	_swatch_button.pressed.connect(func():
		UiSound.move()
		editor.ui.browser.show_material(editor.material))
	_swatch = TextureRect.new()
	_swatch.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT, Control.PRESET_MODE_MINSIZE, 4)
	_swatch.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_swatch.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
	_swatch.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_swatch_button.add_child(_swatch)
	box.add_child(_swatch_button)
	editor.status_changed.connect(_refresh)
	_refresh()


func _tool_button(id: String, command: String, tip: String) -> Button:
	var b := Button.new()
	b.custom_minimum_size = Vector2(SIZE, SIZE)
	b.toggle_mode = true
	b.focus_mode = Control.FOCUS_NONE
	var k := K.key_for(command)
	b.tooltip_text = tip + (" (" + k + ")" if k != "" else "")
	b.pressed.connect(func():
		UiSound.move()
		editor.execute(command))
	b.add_child(_icon(id, k))
	_buttons[command] = b
	return b


func _curves_button() -> MenuButton:
	var m := MenuButton.new()
	m.custom_minimum_size = Vector2(SIZE, SIZE)
	m.focus_mode = Control.FOCUS_NONE
	m.flat = false
	m.tooltip_text = "Drop a curved patch at the cursor"
	var pop := m.get_popup()
	for k in ["Bevel (quarter circle)", "Curve (half circle)", "Cylinder", "Arch"]:
		pop.add_item(k)
	pop.id_pressed.connect(func(i):
		UiSound.move()
		editor.execute("patch." + Q3PatchFactory.KINDS[i]))
	m.add_child(_icon("curves", ""))
	return m


func _rule() -> Control:
	var r := ColorRect.new()
	r.color = Color(0.25, 0.22, 0.2)
	r.custom_minimum_size = Vector2(SIZE - 12, 1)
	r.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	r.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return r


func _icon(id: String, key: String) -> Control:
	var c := Control.new()
	c.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	c.mouse_filter = Control.MOUSE_FILTER_IGNORE
	c.draw.connect(func(): _draw_icon(c, id, key))
	return c


func _refresh() -> void:
	if not is_inside_tree():
		return
	var et: Q3EntityTool = editor.tools["entity"]
	for cmd in _buttons:
		var on := false
		if cmd.begins_with("tool.") and editor.tools.has(cmd.substr(5)):
			on = editor.tool == editor.tools[cmd.substr(5)]
		else:
			on = editor.tool == et and et.kind == cmd.substr(5)
		(_buttons[cmd] as Button).set_pressed_no_signal(on)
	_swatch.texture = Q3MapMaterials.thumbnail(editor.material)
	_swatch_button.tooltip_text = "Current material: %s\nNew brushes and Paint use it · click to find it in the browser" % Q3MapMaterials.short_name(editor.material)


# ---------------------------------------------------------------- icons

## The icons, drawn in a 24-unit box in the middle of the button.
func _draw_icon(c: Control, id: String, key: String) -> void:
	var s := minf(c.size.x, c.size.y) / 34.0
	var o := (c.size - Vector2(24, 24) * s) * 0.5 - Vector2(0, 2 * s if key != "" else 0)
	var col := Q3Theme.TEXT_HOT
	var p := func(x: float, y: float) -> Vector2: return o + Vector2(x, y) * s
	var w := 2.0
	match id:
		"select":
			var pts := PackedVector2Array()
			for v in [[6, 2], [6, 19], [10, 15.5], [13, 22], [16, 20.5], [13, 14.5], [18.5, 14.5], [6, 2]]:
				pts.append(p.call(v[0], v[1]))
			c.draw_colored_polygon(pts, Color(col, 0.18))
			c.draw_polyline(pts, col, w, true)
		"create", "face":
			var top := [p.call(12, 3), p.call(21, 7.5), p.call(12, 12), p.call(3, 7.5)]
			if id == "face":
				c.draw_colored_polygon(PackedVector2Array(top), Color(0.45, 0.85, 1.0, 0.85))
			var hull := PackedVector2Array([p.call(12, 3), p.call(21, 7.5), p.call(21, 17), p.call(12, 21.5), p.call(3, 17), p.call(3, 7.5), p.call(12, 3)])
			c.draw_polyline(hull, col, w, true)
			c.draw_polyline(PackedVector2Array([p.call(3, 7.5), p.call(12, 12), p.call(21, 7.5)]), col, w, true)
			c.draw_line(p.call(12, 12), p.call(12, 21.5), col, w, true)
		"vertex":
			c.draw_polyline(PackedVector2Array([p.call(5, 5), p.call(19, 5), p.call(19, 19), p.call(5, 19), p.call(5, 5)]), Color(col, 0.5), w * 0.75, true)
			for v in [[5, 5], [19, 5], [19, 19], [5, 19]]:
				c.draw_rect(Rect2(p.call(v[0] - 2.5, v[1] - 2.5), Vector2(5, 5) * s), Color(1.0, 0.8, 0.3))
		"curves":
			c.draw_arc(p.call(12, 19), 8.5 * s, PI, TAU, 24, col, w, true)
			c.draw_arc(p.call(12, 19), 4.0 * s, PI, TAU, 16, col, w, true)
			c.draw_line(p.call(3.5, 19), p.call(8, 19), col, w, true)
			c.draw_line(p.call(16, 19), p.call(20.5, 19), col, w, true)
			# a little triangle: it opens a menu
			c.draw_colored_polygon(PackedVector2Array([Vector2(c.size.x - 12, c.size.y - 10), Vector2(c.size.x - 5, c.size.y - 10),
					Vector2(c.size.x - 8.5, c.size.y - 6)]), Color(col, 0.6))
		"spawn":
			var m := Q3MapEntities.color("info_player_deathmatch").lightened(0.2)
			c.draw_circle(p.call(12, 5), 3.0 * s, m)
			c.draw_line(p.call(12, 8), p.call(12, 15), m, w, true)
			c.draw_polyline(PackedVector2Array([p.call(6, 11), p.call(12, 10), p.call(18, 11)]), m, w, true)
			c.draw_polyline(PackedVector2Array([p.call(8, 21), p.call(12, 15), p.call(16, 21)]), m, w, true)
		"jumppad":
			var m := Q3MapEntities.COLORS["movement"]
			var arc := PackedVector2Array()
			for i in 13:
				var t := float(i) / 12.0
				arc.append(p.call(3 + 17 * t, 20 - 60 * t * (1 - t)))
			c.draw_polyline(arc, m, w, true)
			c.draw_colored_polygon(PackedVector2Array([p.call(20, 21), p.call(16.5, 17), p.call(22, 16.5)]), m)
			c.draw_line(p.call(1, 21.5), p.call(8, 21.5), col, w, true)
		"teleporter":
			var m := Q3MapEntities.COLORS["teleport"]
			c.draw_arc(p.call(12, 12), 9.0 * s, 0, TAU, 32, m, w, true)
			c.draw_arc(p.call(12, 12), 5.0 * s, 0, TAU, 24, Color(m, 0.7), w, true)
			c.draw_circle(p.call(12, 12), 1.8 * s, m)
		"weapon":
			var m := Q3MapEntities.COLORS["item"].lightened(0.3)
			c.draw_rect(Rect2(p.call(2, 9), Vector2(19, 5) * s), m)
			c.draw_rect(Rect2(p.call(19, 8), Vector2(3, 7) * s), m.darkened(0.2))
			c.draw_colored_polygon(PackedVector2Array([p.call(7, 14), p.call(11, 14), p.call(10, 20), p.call(6.5, 20)]), m)
		"item":
			var m := Color(0.4, 1.0, 0.5)
			c.draw_rect(Rect2(p.call(9, 3), Vector2(6, 18) * s), m)
			c.draw_rect(Rect2(p.call(3, 9), Vector2(18, 6) * s), m)
		"sound":
			var m := Q3MapEntities.COLORS["audio"]
			c.draw_colored_polygon(PackedVector2Array([p.call(3, 9), p.call(7, 9), p.call(12, 4.5), p.call(12, 19.5), p.call(7, 15), p.call(3, 15)]), m)
			c.draw_arc(p.call(12, 12), 4.5 * s, -PI / 3, PI / 3, 10, m, w, true)
			c.draw_arc(p.call(12, 12), 8.5 * s, -PI / 3, PI / 3, 14, m, w, true)
		"light":
			var m := Color(1.0, 0.85, 0.35)
			c.draw_circle(p.call(12, 9.5), 6.5 * s, m)
			c.draw_rect(Rect2(p.call(9, 16.5), Vector2(6, 4.5) * s), m.darkened(0.35))
			for a in [-0.9, -0.45, 0.0, 0.45, 0.9]:
				var d := Vector2(sin(a), -cos(a))
				c.draw_line(p.call(12, 9.5) + d * 8.5 * s, p.call(12, 9.5) + d * 11.0 * s, Color(m, 0.7), 1.5, true)
	if key != "":
		var font := get_theme_font("font", "Label")
		var fs := 11
		var ts := font.get_string_size(key, HORIZONTAL_ALIGNMENT_LEFT, -1, fs)
		c.draw_string(font, Vector2(c.size.x - ts.x - 3, c.size.y - 3), key, HORIZONTAL_ALIGNMENT_LEFT, -1, fs, Color(col, 0.55))
