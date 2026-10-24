class_name Q3Theme
extends RefCounted
## The menu look: Quake 3's gothic metal made modern. Dark glass, warm off-white text, a red
## accent and glow (q3_ui's color_red), Quake 3's own fonts for buttons and headings.
## Type variations: HeaderLabel (banner font), TitleLabel, GroupLabel, DimLabel, BadgeLabel,
## SmallButton, ListButton.

const RED := Color(0.86, 0.12, 0.06)
const GLOW := Color(1.0, 0.24, 0.1)
const TEXT := Color(0.88, 0.84, 0.8)
const TEXT_HOT := Color(1.0, 0.95, 0.88)
const DIM := Color(0.55, 0.52, 0.5)
const METAL := Color(0.34, 0.3, 0.28)
const AMBER := Color(1.0, 0.68, 0.2)
const PANEL := Color(0.035, 0.03, 0.03, 0.82)


static func build(high_contrast := false) -> Theme:
	var t := Theme.new()
	var body := ThemeDB.fallback_font
	var prop := Q3Fonts.prop()
	var text := Color.WHITE if high_contrast else TEXT
	var panel := Color(0.02, 0.018, 0.018, 0.97) if high_contrast else PANEL
	t.default_font = body
	t.default_font_size = 18

	t.set_color("font_color", "Label", text)
	t.set_color("font_shadow_color", "Label", Color(0, 0, 0, 0.6))
	t.set_constant("shadow_offset_x", "Label", 1)
	t.set_constant("shadow_offset_y", "Label", 1)
	_variation(t, "HeaderLabel", "Label", Q3Fonts.banner(), 44, Color.WHITE)
	_variation(t, "TitleLabel", "Label", prop, 30, TEXT_HOT)
	_variation(t, "GroupLabel", "Label", prop, 19, RED.lightened(0.25))
	_variation(t, "DimLabel", "Label", body, 15, Color(0.8, 0.78, 0.75) if high_contrast else DIM)
	_variation(t, "BadgeLabel", "Label", prop, 12, AMBER)

	# buttons: flat dark metal, red edge when hot
	var normal := _box(Color(0.07, 0.06, 0.06, 0.75 if not high_contrast else 1.0), METAL.darkened(0.3), [0, 0, 0, 1])
	var hover := _box(Color(0.16, 0.06, 0.04, 0.9), RED, [3, 0, 0, 1])
	var pressed := _box(Color(0.3, 0.07, 0.04, 0.95), GLOW, [3, 0, 0, 1])
	var disabled := _box(Color(0.05, 0.05, 0.05, 0.5), Color(0.2, 0.2, 0.2), [0, 0, 0, 1])
	var focus := _box(Color(0, 0, 0, 0), GLOW, [2, 2, 2, 2])
	for type in ["Button", "OptionButton", "MenuButton", "ColorPickerButton"]:
		t.set_stylebox("normal", type, normal)
		t.set_stylebox("hover", type, hover)
		t.set_stylebox("pressed", type, pressed)
		t.set_stylebox("hover_pressed", type, pressed)
		t.set_stylebox("disabled", type, disabled)
		t.set_stylebox("focus", type, focus)
		t.set_font("font", type, prop)
		t.set_font_size("font_size", type, 19)
		t.set_color("font_color", type, text)
		t.set_color("font_hover_color", type, TEXT_HOT)
		t.set_color("font_focus_color", type, TEXT_HOT)
		t.set_color("font_pressed_color", type, Color.WHITE)
		t.set_color("font_hover_pressed_color", type, Color.WHITE)
		t.set_color("font_disabled_color", type, Color(0.4, 0.38, 0.36))
	t.set_type_variation("SmallButton", "Button")
	t.set_font_size("font_size", "SmallButton", 15)
	t.set_type_variation("ListButton", "Button")
	t.set_stylebox("normal", "ListButton", _box(Color(0, 0, 0, 0), Color(0, 0, 0, 0), [0, 0, 0, 0]))
	t.set_constant("h_separation", "Button", 8)

	for type in ["CheckButton", "CheckBox"]:
		t.set_stylebox("normal", type, StyleBoxEmpty.new())
		t.set_stylebox("hover", type, StyleBoxEmpty.new())
		t.set_stylebox("pressed", type, StyleBoxEmpty.new())
		t.set_stylebox("hover_pressed", type, StyleBoxEmpty.new())
		t.set_stylebox("focus", type, focus)
		t.set_font("font", type, prop)
		t.set_font_size("font_size", type, 17)
		t.set_color("font_color", type, text)
		t.set_color("font_pressed_color", type, TEXT_HOT)
		t.set_color("font_hover_color", type, TEXT_HOT)
		t.set_color("font_focus_color", type, TEXT_HOT)
		t.set_color("font_hover_pressed_color", type, TEXT_HOT)

	var field := _box(Color(0.02, 0.02, 0.02, 0.85), METAL, [1, 1, 1, 1])
	var field_focus := _box(Color(0.04, 0.02, 0.02, 0.95), GLOW, [1, 1, 1, 2])
	t.set_stylebox("normal", "LineEdit", field)
	t.set_stylebox("focus", "LineEdit", field_focus)
	t.set_stylebox("read_only", "LineEdit", field)
	t.set_color("font_color", "LineEdit", Color.WHITE)
	t.set_color("caret_color", "LineEdit", GLOW)
	t.set_color("selection_color", "LineEdit", Color(RED, 0.5))
	t.set_color("font_placeholder_color", "LineEdit", DIM)
	t.set_stylebox("normal", "TextEdit", field)
	t.set_stylebox("focus", "TextEdit", field_focus)

	# sliders: thin track, red fill
	var track := _box(Color(0.12, 0.11, 0.1), Color(0, 0, 0, 0), [0, 0, 0, 0])
	track.content_margin_top = 3
	track.content_margin_bottom = 3
	var fill := _box(RED.darkened(0.15), Color(0, 0, 0, 0), [0, 0, 0, 0])
	fill.content_margin_top = 3
	fill.content_margin_bottom = 3
	var fill_hot := fill.duplicate() as StyleBoxFlat
	fill_hot.bg_color = GLOW
	t.set_stylebox("slider", "HSlider", track)
	t.set_stylebox("grabber_area", "HSlider", fill)
	t.set_stylebox("grabber_area_highlight", "HSlider", fill_hot)
	t.set_stylebox("focus", "HSlider", focus)
	t.set_icon("grabber", "HSlider", _dot(16, TEXT))
	t.set_icon("grabber_highlight", "HSlider", _dot(16, Color.WHITE))

	var glass := StyleBoxFlat.new()
	glass.bg_color = panel
	glass.border_color = METAL.darkened(0.2)
	glass.set_border_width_all(1)
	glass.border_width_top = 2
	glass.border_color = Color(RED, 0.8)
	glass.set_content_margin_all(24)
	t.set_stylebox("panel", "PanelContainer", glass)
	t.set_stylebox("panel", "Panel", glass)

	var tip := _box(Color(0.03, 0.025, 0.025, 0.97), RED, [0, 0, 0, 0])
	tip.border_width_left = 3
	tip.set_content_margin_all(10)
	t.set_stylebox("panel", "TooltipPanel", tip)
	t.set_color("font_color", "TooltipLabel", Color.WHITE)
	t.set_font_size("font_size", "TooltipLabel", 16)

	var popup := _box(Color(0.05, 0.045, 0.045, 0.98), METAL, [1, 1, 1, 1])
	popup.set_content_margin_all(4)
	t.set_stylebox("panel", "PopupMenu", popup)
	t.set_stylebox("hover", "PopupMenu", _box(Color(0.3, 0.06, 0.03), Color(0, 0, 0, 0), [0, 0, 0, 0]))
	t.set_font("font", "PopupMenu", body)
	t.set_font_size("font_size", "PopupMenu", 17)
	t.set_color("font_color", "PopupMenu", text)
	t.set_color("font_hover_color", "PopupMenu", Color.WHITE)

	var grab := _box(Color(0.35, 0.3, 0.28, 0.8), Color(0, 0, 0, 0), [0, 0, 0, 0])
	grab.set_corner_radius_all(3)
	var grab_hot := grab.duplicate() as StyleBoxFlat
	grab_hot.bg_color = RED
	var scroll_bg := _box(Color(0, 0, 0, 0.25), Color(0, 0, 0, 0), [0, 0, 0, 0])
	scroll_bg.content_margin_left = 4
	scroll_bg.content_margin_right = 4
	t.set_stylebox("scroll", "VScrollBar", scroll_bg)
	t.set_stylebox("grabber", "VScrollBar", grab)
	t.set_stylebox("grabber_highlight", "VScrollBar", grab_hot)
	t.set_stylebox("grabber_pressed", "VScrollBar", grab_hot)

	t.set_stylebox("separator", "HSeparator", _box(Color(METAL, 0.5), Color(0, 0, 0, 0), [0, 0, 0, 0]))
	t.set_constant("separation", "HSeparator", 12)
	t.set_constant("separation", "VBoxContainer", 6)
	t.set_constant("separation", "HBoxContainer", 10)
	t.set_stylebox("panel", "ItemList", field)
	# ItemList draws its focus box over the items, so it may only be a border
	var list_focus := _box(Color(0, 0, 0, 0), GLOW, [1, 1, 1, 2])
	list_focus.draw_center = false
	t.set_stylebox("focus", "ItemList", list_focus)
	t.set_stylebox("selected", "ItemList", _box(Color(0.3, 0.06, 0.03), Color(0, 0, 0, 0), [0, 0, 0, 0]))
	t.set_stylebox("selected_focus", "ItemList", _box(Color(0.4, 0.08, 0.04), GLOW, [2, 0, 0, 0]))
	t.set_stylebox("hovered", "ItemList", _box(Color(0.12, 0.05, 0.04), Color(0, 0, 0, 0), [0, 0, 0, 0]))
	t.set_color("font_color", "ItemList", text)
	return t


static func _variation(t: Theme, name: String, base: String, font: Font, size: int, color: Color) -> void:
	t.set_type_variation(name, base)
	t.set_font("font", name, font)
	t.set_font_size("font_size", name, size)
	t.set_color("font_color", name, color)


## widths: left, top, right, bottom
static func _box(bg: Color, border: Color, widths: Array) -> StyleBoxFlat:
	var s := StyleBoxFlat.new()
	s.bg_color = bg
	s.border_color = border
	s.border_width_left = widths[0]
	s.border_width_top = widths[1]
	s.border_width_right = widths[2]
	s.border_width_bottom = widths[3]
	s.content_margin_left = 12
	s.content_margin_right = 12
	s.content_margin_top = 6
	s.content_margin_bottom = 6
	return s


static func _dot(size: int, c: Color) -> ImageTexture:
	var img := Image.create(size, size, false, Image.FORMAT_RGBA8)
	var r := size * 0.5
	for y in size:
		for x in size:
			var d := Vector2(x + 0.5 - r, y + 0.5 - r).length()
			img.set_pixel(x, y, Color(c, clampf(r - 1.0 - d, 0.0, 1.0)))
	return ImageTexture.create_from_image(img)


## A label in the banner font with wide tracking, e.g. "REMASTERED" under the logo.
static func banner_font(spacing := 6) -> FontVariation:
	var v := FontVariation.new()
	v.base_font = Q3Fonts.banner()
	v.spacing_glyph = spacing
	return v
