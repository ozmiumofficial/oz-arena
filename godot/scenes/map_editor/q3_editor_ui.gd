class_name Q3EditorUi
extends Control
## The map editor's screen furniture in native controls, in the menu's theme (Q3Theme) with Barlow on
## buttons and fields: the top bar (File, Edit, View, Play, undo, grid, snap), the browser docked under
## the view, the tool strip down the view's edge, the Objects / History panel and the inspector on the right
## (both docks resize from their handles and fold away; Tab hides them), the status bar, toasts, the
## box-select rectangle, the dialogs (unsaved changes, save as, open) and the F1 hotkey sheet, which
## rebinds keys too. The 3D view gets every event these don't take.

const K = preload("res://scenes/map_editor/q3_editor_keymap.gd")
const ToolStrip = preload("res://scenes/map_editor/q3_editor_toolstrip.gd")
const Splitter = preload("res://scenes/map_editor/q3_editor_splitter.gd")
const SidePanel = preload("res://scenes/map_editor/q3_editor_side.gd")
const BAR_H := 40
const UI_FONT := "res://fonts/barlow_semibold.woff2"

static var layout_path := "user://map_editor/layout.json"   ## tests point it elsewhere

var editor: Q3MapEditor

var _undo: Button
var _redo: Button
var _grid_label: Label
var _snap: Button
var _hint: Label
var _sel_label: Label
var _cursor_label: Label
var _file_label: Label
var _flags_label: Label
var _count_key := []
var _count_text := ""
var _toast: Label
var _toast_tween: Tween
var _rect: Panel
var _canvas: Control           ## where tools draw 2D marks over the view (vertex handles)
var browser: Q3EditorBrowser
var inspector: Q3EditorInspector
var strip: Control              ## the tool strip
var side: Control               ## Objects / History, above the inspector
var _right: VBoxContainer       ## the right dock: side + its handle + inspector
var _handles: Array = []        ## the left, right and side resize handles
var _view: Control              ## the open middle between the panels (the 3D view shows through)
var _modal: Control
var _file_dialog: FileDialog   ## the system's .map picker while it's open
var _sheet: Control
var _sheet_version := -1
var _rebinding := -1          ## the binding waiting for a key in the sheet
var _hint_override := ""
var _loading_layout := false
var _grid2d: Control            ## a 2D view's grid and coordinates, over the open view
var _view_buttons := {}         ## 3d, top, front, side -> the switcher's buttons


func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	theme = _editor_theme()
	_canvas = Control.new()
	_canvas.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_canvas.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_canvas.draw.connect(func(): editor.tool.draw_2d(_canvas))
	add_child(_canvas)
	# top bar / ((tools | the open view) over the browser | handle | objects, inspector) / status bar;
	# the open middle lets the mouse through to the 3D view
	var layout := VBoxContainer.new()
	layout.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	layout.mouse_filter = Control.MOUSE_FILTER_IGNORE
	layout.add_theme_constant_override("separation", 0)
	add_child(layout)
	_build_top(layout)
	var middle := HBoxContainer.new()
	middle.size_flags_vertical = Control.SIZE_EXPAND_FILL
	middle.mouse_filter = Control.MOUSE_FILTER_IGNORE
	middle.add_theme_constant_override("separation", 0)
	layout.add_child(middle)
	# the view and its tool strip over the browser; the right dock runs the full height beside them
	var main := VBoxContainer.new()
	main.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	main.mouse_filter = Control.MOUSE_FILTER_IGNORE
	main.add_theme_constant_override("separation", 0)
	middle.add_child(main)
	var work := HBoxContainer.new()
	work.size_flags_vertical = Control.SIZE_EXPAND_FILL
	work.mouse_filter = Control.MOUSE_FILTER_IGNORE
	work.add_theme_constant_override("separation", 0)
	main.add_child(work)
	strip = ToolStrip.new()
	strip.editor = editor
	work.add_child(strip)
	_view = Control.new()
	_view.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_view.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_view.clip_contents = true
	work.add_child(_view)
	_grid2d = Control.new()
	_grid2d.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_grid2d.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_grid2d.draw.connect(func(): _draw_grid2d(_grid2d))
	_view.add_child(_grid2d)
	_view.add_child(_view_switcher())
	browser = Q3EditorBrowser.new()
	browser.editor = editor
	main.add_child(_handle(browser, true, -1.0, 140, 800))
	main.add_child(browser)
	_right = VBoxContainer.new()
	_right.add_theme_constant_override("separation", 0)
	_right.custom_minimum_size.x = 340
	middle.add_child(_handle(_right, false, -1.0, 280, 700))
	middle.add_child(_right)
	side = SidePanel.new()
	side.editor = editor
	_right.add_child(side)
	var side_handle := _handle(side, true, 1.0, 120, 900)
	_right.add_child(side_handle)
	side.changed.connect(func():
		side_handle.visible = not side.folded
		_save_layout())
	inspector = Q3EditorInspector.new()
	inspector.editor = editor
	inspector.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_right.add_child(inspector)
	_build_status(layout)
	_toast = Label.new()
	_toast.set_anchors_and_offsets_preset(Control.PRESET_TOP_WIDE)
	_toast.offset_top = BAR_H + 40
	_toast.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_toast.add_theme_font_size_override("font_size", 20)
	_toast.add_theme_color_override("font_outline_color", Color.BLACK)
	_toast.add_theme_constant_override("outline_size", 6)
	_toast.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_toast.modulate.a = 0
	add_child(_toast)
	_rect = Panel.new()
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.45, 0.85, 1.0, 0.08)
	sb.border_color = Color(0.45, 0.85, 1.0, 0.9)
	sb.set_border_width_all(1)
	_rect.add_theme_stylebox_override("panel", sb)
	_rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_rect.visible = false
	add_child(_rect)
	_load_layout()
	editor.status_changed.connect(_refresh)
	_refresh()


## Q3Theme, with Barlow on buttons, fields, lists and body text (Quake 3's prop font stays on titles
## and section headings): the prop font's capitals were hard to read at the editor's sizes.
func _editor_theme() -> Theme:
	var t := Q3Theme.build(Settings.get_value("accessibility/high_contrast"))
	# solid panels: the menu's glass let the map, and a 2D view's bright lines, show through the text
	var glass := t.get_stylebox("panel", "PanelContainer").duplicate() as StyleBoxFlat
	if glass:
		glass.bg_color = Color(0.035, 0.032, 0.032, 0.98)
		t.set_stylebox("panel", "PanelContainer", glass)
	var f: Font = load(UI_FONT) if ResourceLoader.exists(UI_FONT) else null
	if f == null:
		return t
	for type in ["Button", "OptionButton", "MenuButton", "ColorPickerButton", "CheckBox", "CheckButton"]:
		t.set_font("font", type, f)
		t.set_font_size("font_size", type, 15)
	t.set_font_size("font_size", "SmallButton", 14)
	for type in ["LineEdit", "PopupMenu", "ItemList", "Tree", "TooltipLabel", "Label", "DimLabel"]:
		t.set_font("font", type, f)
	t.set_font_size("font_size", "LineEdit", 15)
	t.set_font_size("font_size", "PopupMenu", 15)
	t.set_font_size("font_size", "Tree", 14)
	t.set_font_size("title_button_font_size", "Tree", 13)
	t.set_font("title_button_font", "Tree", f)
	t.set_font_size("font_size", "ItemList", 14)
	t.set_font_size("font_size", "TooltipLabel", 14)
	t.set_font_size("font_size", "Label", 15)
	t.set_font_size("font_size", "DimLabel", 14)
	t.set_font_size("font_size", "GroupLabel", 15)
	t.set_font_size("font_size", "TitleLabel", 24)
	return t


func _handle(target: Control, vertical: bool, toward: float, lo: float, hi: float) -> Control:
	var h := Splitter.new()
	h.target = target
	h.vertical = vertical
	h.side = toward
	h.lo = lo
	h.hi = hi
	h.changed.connect(_save_layout)
	_handles.append(h)
	return h


func _bar_button(text: String, tip: String, cb: Callable, toggle := false) -> Button:
	var b := Button.new()
	b.text = text
	b.tooltip_text = tip
	b.theme_type_variation = "SmallButton"
	b.focus_mode = Control.FOCUS_NONE
	b.toggle_mode = toggle
	b.pressed.connect(func():
		UiSound.move()
		cb.call())
	return b


## A toolbar menu of commands: [[label, command], ...]; an empty label is a separator. Each item shows
## its key, read again as it opens (keys can be rebound).
func _menu(text: String, tip: String, entries: Array) -> MenuButton:
	var m := MenuButton.new()
	m.text = text
	m.tooltip_text = tip
	m.theme_type_variation = "SmallButton"
	m.focus_mode = Control.FOCUS_NONE
	m.flat = false
	var pop := m.get_popup()
	for i in entries.size():
		if entries[i][0] == "":
			pop.add_separator()
		else:
			pop.add_item(entries[i][0], i)
	pop.about_to_popup.connect(func():
		for i in entries.size():
			if entries[i][0] != "":
				var k := K.key_for(entries[i][1])
				pop.set_item_text(pop.get_item_index(i), entries[i][0] + ("    " + k if k != "" else "")))
	pop.id_pressed.connect(func(i):
		UiSound.move()
		editor.execute(entries[i][1]))
	return m


func _tip(label: String, command: String) -> String:
	var k := K.key_for(command)
	return label + (" (" + k + ")" if k != "" else "")


func _build_top(parent: Control) -> void:
	var panel := PanelContainer.new()
	panel.custom_minimum_size.y = BAR_H
	parent.add_child(panel)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 4)
	panel.add_child(row)
	var title := Label.new()
	title.text = "MAP EDITOR"
	title.add_theme_font_override("font", Q3Theme.banner_font(4))
	title.add_theme_font_size_override("font_size", 18)
	title.add_theme_color_override("font_color", Q3Theme.GLOW)
	row.add_child(title)
	row.add_child(VSeparator.new())
	# the commands live in four menus, so the bar stays one short row; the tools are down the view's edge
	row.add_child(_menu("FILE", "New, open, save, import and export maps", [["New map", "file.new"], ["Open map", "file.open"],
			["Save", "file.save"], ["Save as", "file.saveAs"], ["Publish (play it from Create Match)", "file.publish"], ["", ""],
			["Import a Quake 3 map", "file.importQ3"], ["Import a .bsp or .pk3 file", "file.importBsp"],
			["Import .map", "file.importMap"], ["", ""], ["Export .map (Radiant)", "file.exportMap"], ["Export .map (TrenchBroom)", "file.exportValve"],
			["Export compiled .bsp / .pk3 (plays in Quake 3)", "file.exportBsp"],
			["", ""], ["Leave to the main menu", "session.leave"]]))
	row.add_child(_menu("EDIT", "Copy, group, turn, mirror, hide and lock the selection", [["Undo", "edit.undo"], ["Redo", "edit.redo"], ["", ""],
			["Copy", "edit.copy"], ["Paste at the cursor", "edit.paste"], ["Duplicate", "edit.duplicate"], ["Delete", "edit.delete"],
			["Select all", "select.all"], ["", ""],
			["Group", "edit.group"], ["Ungroup", "edit.ungroup"], ["", ""],
			["Rotate 90°", "edit.rotate"], ["Rotate 15°", "edit.rotate15"], ["Mirror front-back", "edit.mirrorX"], ["Mirror left-right", "edit.mirrorY"],
			["Mirror upside down", "edit.mirrorZ"], ["CSG subtract (carve)", "csg.subtract"], ["Snap corners to the grid", "edit.snapVertices"], ["", ""],
			["Hide selection", "edit.hide"], ["Unhide all", "edit.unhideAll"], ["Isolate selection", "view.isolate"],
			["Lock selection", "edit.lock"], ["Unlock all", "edit.unlockAll"], ["", ""],
			["Save selection as prefab", "prefab.save"]]))
	row.add_child(_view_menu())
	var play := _menu("PLAY", _tip("Play the map", "map.playtest"), [["Play-test", "map.playtest"],
			["Full compile (check for leaks)", "map.compile"], ["Bots in play-tests", "map.bots"], ["Start at the camera", "map.fromCamera"]])
	var ppop := play.get_popup()
	ppop.about_to_popup.connect(func():
		ppop.set_item_text(2, "Bots in play-tests: %s    %s" % ["%d" % editor.play_bots if editor.play_bots > 0 else "off", K.key_for("map.bots")])
		ppop.set_item_text(3, "Start at the camera: %s    %s" % ["on" if editor.play_from_camera else "off (a spawn point)", K.key_for("map.fromCamera")]))
	row.add_child(play)
	row.add_child(_menu("RACE", "Defrag courses: start and finish lines, checkpoints, and a check of the course",
			[["Start line", "tool.startline"], ["Finish line", "tool.finishline"], ["Checkpoint", "tool.checkpoint"], ["", ""],
			["Check the course", "race.check"], ["Course style and author", "race.settings"]]))
	row.add_child(VSeparator.new())
	_undo = _bar_button("UNDO", "Undo (Ctrl+Z)", func(): editor.execute("edit.undo"))
	_redo = _bar_button("REDO", "Redo (Ctrl+Y)", func(): editor.execute("edit.redo"))
	row.add_child(_undo)
	row.add_child(_redo)
	row.add_child(VSeparator.new())
	var g := Label.new()
	g.text = "GRID"
	g.theme_type_variation = "DimLabel"
	row.add_child(g)
	row.add_child(_bar_button("−", _tip("Smaller grid", "grid.smaller"), func(): editor.execute("grid.smaller")))
	_grid_label = Label.new()
	_grid_label.custom_minimum_size.x = 110
	_grid_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	row.add_child(_grid_label)
	row.add_child(_bar_button("+", _tip("Bigger grid", "grid.bigger"), func(): editor.execute("grid.bigger")))
	_snap = _bar_button("SNAP", _tip("Grid snapping: moves, new brushes, faces, corners and entities land on the grid", "grid.snap"),
			func(): editor.execute("grid.snap"), true)
	row.add_child(_snap)
	var spacer := Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(spacer)
	row.add_child(_bar_button("KEYS", "Hotkey sheet (F1)", func(): toggle_sheet()))
	row.add_child(_bar_button("MAIN MENU", _tip("Leave to the main menu", "session.leave"), func(): editor.execute("session.leave")))


## VIEW: the filters (checked while they hide something), show all, and the lighting preview.
func _view_menu() -> MenuButton:
	var m := MenuButton.new()
	m.text = "VIEW"
	m.tooltip_text = "Hide classes of things from the view (Radiant's filters) and the lighting preview"
	m.theme_type_variation = "SmallButton"
	m.focus_mode = Control.FOCUS_NONE
	m.flat = false
	var pop := m.get_popup()
	var cmds := []
	for f in Q3MapEditor.FILTERS:
		pop.add_check_item("Hide " + String(f[1]).to_lower(), cmds.size())
		cmds.append("view.filter." + f[0])
	pop.add_item("Show everything", cmds.size())
	cmds.append("view.filter.none")
	pop.add_separator()
	pop.add_check_item("Lights-only preview", cmds.size())
	cmds.append("view.lighting")
	pop.add_separator()
	for v in [["3D view", "view.3d"], ["Top view (2D)", "view.top"], ["Front view (2D)", "view.front"], ["Side view (2D)", "view.side"], ["", ""],
			["Frame selection", "view.focus"], ["Reset camera", "view.home"], ["Show / hide the grid", "grid.toggle"],
			["Show / hide the panels", "view.explorer"]]:
		if v[0] == "":
			pop.add_separator()
			continue
		pop.add_item(v[0], cmds.size())
		cmds.append(v[1])
	pop.about_to_popup.connect(func():
		for i in cmds.size():
			var idx := pop.get_item_index(i)
			var k := K.key_for(cmds[i])
			var base := pop.get_item_text(idx).split("    ")[0]
			pop.set_item_text(idx, base + ("    " + k if k != "" else ""))
			if i < Q3MapEditor.FILTERS.size():
				pop.set_item_checked(idx, editor.filters.has(Q3MapEditor.FILTERS[i][0]))
			elif cmds[i] == "view.lighting":
				pop.set_item_checked(idx, editor.lights_only))
	pop.id_pressed.connect(func(i):
		UiSound.move()
		editor.execute(cmds[i]))
	return m


func _build_status(parent: Control) -> void:
	var panel := PanelContainer.new()
	panel.custom_minimum_size.y = 32
	parent.add_child(panel)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 24)
	panel.add_child(row)
	_hint = Label.new()
	_hint.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_hint.clip_text = true
	_hint.add_theme_font_size_override("font_size", 15)
	row.add_child(_hint)
	for l in ["_flags_label", "_sel_label", "_cursor_label", "_file_label"]:
		var lab := Label.new()
		lab.theme_type_variation = "DimLabel"
		row.add_child(lab)
		set(l, lab)
	# isolate and the view filters: things are missing from the view, so it says so in amber
	_flags_label.add_theme_color_override("font_color", Color(1.0, 0.72, 0.25))


func _refresh() -> void:
	if not is_inside_tree():
		return
	for k in _view_buttons:
		_view_buttons[k].set_pressed_no_signal(k == editor.view)
	_grid2d.queue_redraw()
	_undo.disabled = not editor.doc.can_undo()
	_redo.disabled = not editor.doc.can_redo()
	_undo.tooltip_text = "Undo %s (Ctrl+Z)" % editor.doc.undo_label() if editor.doc.can_undo() else "Nothing to undo"
	_redo.tooltip_text = "Redo %s (Ctrl+Y)" % editor.doc.redo_label() if editor.doc.can_redo() else "Nothing to redo"
	_grid_label.text = editor.grid.label()
	_snap.set_pressed_no_signal(editor.grid.snapping)
	var n := editor.selection.size()
	if n == 0:
		# counting a big map's objects on every status change adds up: once per document change
		var key := [editor.doc, editor.doc_revision()]
		if _count_key != key:
			_count_key = key
			_count_text = _counts(editor.doc.objects.keys())
		_sel_label.text = _count_text
	else:
		var s := editor.selection_bounds().size
		_sel_label.text = "%s selected · %s × %s × %s" % [_counts(editor.selection.keys()), Q3MapEditor._num(s.x), Q3MapEditor._num(s.y), Q3MapEditor._num(s.z)]
	var flags := PackedStringArray()
	if editor.is_isolated():
		flags.append("isolated · Shift+I shows all")
	if not editor.grid.snapping:
		flags.append("snap off · %s turns it on" % K.key_for("grid.snap"))
	if editor.filters.size():
		flags.append("filtered: %s · Alt+0 shows all" % ", ".join(PackedStringArray(editor.filters.keys())))
	_flags_label.text = "   ".join(flags)
	_flags_label.visible = flags.size() > 0
	# a new map is "untitled"; an imported one goes by the map it came from until it's saved
	_file_label.text = String(editor.doc.info.get("name", "untitled")) + (" *" if editor.doc.dirty else "")
	if _hint_override == "":
		_hint.text = editor.tool.hint()


## "12 brushes · 3 curves · 5 entities" (the kinds there are).
func _counts(ids: Array) -> String:
	var by := {"brush": 0, "patch": 0, "entity": 0}
	for id in ids:
		var t := String(editor.doc.get_obj(id).get("type", ""))
		if by.has(t):
			by[t] += 1
	var parts := PackedStringArray()
	for k in [["brush", "brush", "brushes"], ["patch", "curve", "curves"], ["entity", "entity", "entities"]]:
		if by[k[0]] > 0:
			parts.append("%d %s" % [by[k[0]], k[1] if by[k[0]] == 1 else k[2]])
	return " · ".join(parts) if parts.size() else "empty map"


func _process(_delta: float) -> void:
	if editor.camera.ortho != "":
		_grid2d.queue_redraw()   # it pans and zooms with the camera
	if editor.tool.wants_2d() or _canvas.get_meta("drawn", false):
		_canvas.queue_redraw()
		_canvas.set_meta("drawn", editor.tool.wants_2d())


## Tab: the browser and the right dock (Objects, inspector) both show or both hide, with their handles;
## the tool strip stays.
func toggle_panels() -> void:
	var on := not panels_shown()
	browser.visible = on
	_right.visible = on
	for h in _handles:
		if h.target != side:
			h.visible = on
	_save_layout()


func panels_shown() -> bool:
	return browser.visible or _right.visible


# ---------------------------------------------------------------- 2D views

## The view switcher in the view's corner: 3D, Top, Front, Side.
func _view_switcher() -> Control:
	var row := HBoxContainer.new()
	row.position = Vector2(10, 10)
	row.mouse_filter = Control.MOUSE_FILTER_IGNORE
	row.add_theme_constant_override("separation", 2)
	for v in [["3D", "3d", "The 3D view"], ["TOP", "top", "Top view: X right, Y up"], ["FRONT", "front", "Front view: X right, Z up"],
			["SIDE", "side", "Side view: Y right, Z up"]]:
		var id: String = v[1]
		var b := _bar_button(v[0], _tip(v[2], "view." + id), func(): editor.execute("view." + id), true)
		b.custom_minimum_size.x = 52
		b.add_theme_font_size_override("font_size", 13)
		row.add_child(b)
		_view_buttons[id] = b
	return row


## A 2D view's grid (the grid size, coarser when its lines would crowd), the axes in their colors
## (X red, Y green, Z blue) and coordinates on every major line.
func _draw_grid2d(c: Control) -> void:
	var cam := editor.camera
	if cam.ortho == "" or not editor.grid.visible:
		return
	var axes: Array = {"top": [0, 1], "front": [0, 2], "side": [1, 2]}[cam.ortho]
	var colors := [Color(0.95, 0.3, 0.25, 0.75), Color(0.3, 0.85, 0.35, 0.75), Color(0.35, 0.55, 1.0, 0.75)]
	var ppu := get_viewport().get_visible_rect().size.y / (cam.size * 32.0)   # pixels per unit
	var o := editor.screen_of(Vector3.ZERO) - c.global_position              # the id origin on screen
	var step := float(editor.grid.units())
	while step * ppu < 7.0:
		step *= 8.0
	var minor := Color(0.6, 0.57, 0.55, 0.13)
	var major := Color(0.75, 0.7, 0.65, 0.3)
	var font := get_theme_font("font", "Label")
	for i in range(floori(-o.x / ppu / step), ceili((c.size.x - o.x) / ppu / step) + 1):
		var x := o.x + i * step * ppu
		c.draw_line(Vector2(x, 0), Vector2(x, c.size.y), colors[axes[1]] if i == 0 else (major if i % 8 == 0 else minor), 1.0)
		if i % 8 == 0:
			c.draw_string(font, Vector2(x + 3, c.size.y - 6), str(int(i * step)), HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color(1, 1, 1, 0.45))
	for j in range(floori((o.y - c.size.y) / ppu / step), ceili(o.y / ppu / step) + 1):
		var y := o.y - j * step * ppu
		c.draw_line(Vector2(0, y), Vector2(c.size.x, y), colors[axes[0]] if j == 0 else (major if j % 8 == 0 else minor), 1.0)
		if j % 8 == 0:
			c.draw_string(font, Vector2(c.size.x - 52, y - 3), str(int(j * step)), HORIZONTAL_ALIGNMENT_RIGHT, 46, 11, Color(1, 1, 1, 0.45))
	var names := ["X", "Y", "Z"]
	c.draw_string(font, Vector2(c.size.x - 70, 26), "%s →   %s ↑" % [names[axes[0]], names[axes[1]]], HORIZONTAL_ALIGNMENT_LEFT, -1, 13, Color(1, 1, 1, 0.6))


# ---------------------------------------------------------------- layout

## The docks' sizes, the Objects panel's height and fold, the browser's tile size and grid snapping, kept between
## sessions (hidden panels always come back: a panel that stayed away would look lost).
func _save_layout() -> void:
	if _loading_layout or not is_inside_tree():
		return
	var data := {"bottom": browser.custom_minimum_size.y, "right": _right.custom_minimum_size.x, "snap": editor.grid.snapping,
			"side_height": side.custom_minimum_size.y if not side.folded else side.height,
			"side_folded": side.folded, "side_tab": side.tab, "tile": browser.tile_size}
	DirAccess.make_dir_recursive_absolute(layout_path.get_base_dir())
	var f := FileAccess.open(layout_path, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(data, "\t"))


func _load_layout() -> void:
	var d: Variant = JSON.parse_string(FileAccess.get_file_as_string(layout_path)) if FileAccess.file_exists(layout_path) else null
	if not d is Dictionary:
		return
	_loading_layout = true
	browser.custom_minimum_size.y = clampf(float(d.get("bottom", browser.custom_minimum_size.y)), 140, 800)
	editor.grid.snapping = bool(d.get("snap", true))
	_right.custom_minimum_size.x = clampf(float(d.get("right", _right.custom_minimum_size.x)), 280, 700)
	side.height = clampf(float(d.get("side_height", side.height)), 120, 900)
	side.set_tab(String(d.get("side_tab", "objects")))
	side.set_folded(bool(d.get("side_folded", false)))
	browser.set_tile_size(String(d.get("tile", browser.tile_size)))
	_loading_layout = false


## A temporary hint from a tool (a drag in progress); "" goes back to the tool's own.
func set_hint(text: String) -> void:
	_hint_override = text
	_hint.text = text if text != "" else editor.tool.hint()


func cursor_moved() -> void:
	var p: Variant = editor.hover.get("point") if editor.hover.size() else editor.plane_point(editor.mouse_pos, Vector3.ZERO, Vector3(0, 0, 1))
	if p == null:
		_cursor_label.text = ""
	else:
		var s := editor.grid.snap_vec(p)
		_cursor_label.text = "%d %d %d" % [int(s.x), int(s.y), int(s.z)]


func toast(text: String, error := false) -> void:
	_toast.text = text
	_toast.add_theme_color_override("font_color", Q3Theme.GLOW if error else Q3Theme.TEXT_HOT)
	if _toast_tween:
		_toast_tween.kill()
	_toast.modulate.a = 1.0
	_toast_tween = create_tween()
	_toast_tween.tween_interval(1.6)
	_toast_tween.tween_property(_toast, "modulate:a", 0.0, 0.6)


func show_rect(r: Rect2) -> void:
	_rect.position = r.position
	_rect.size = r.size
	_rect.visible = true


func hide_rect() -> void:
	_rect.visible = false


# ---------------------------------------------------------------- dialogs

func modal_open() -> bool:
	return _modal != null or _rebinding >= 0


## A centered dialog over a dimmed screen. buttons: [[label, callable], ...]; the last one is what
## Escape does.
func _show_modal(title: String, text: String, buttons: Array, body: Control = null) -> void:
	close_modal()
	_modal = ColorRect.new()
	(_modal as ColorRect).color = Color(0, 0, 0, 0.55)
	_modal.set_anchors_preset(Control.PRESET_FULL_RECT)
	_modal.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(_modal)
	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	_modal.add_child(center)
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(560, 0)
	center.add_child(panel)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 14)
	panel.add_child(box)
	var head := Label.new()
	head.text = title
	head.theme_type_variation = "TitleLabel"
	box.add_child(head)
	if text != "":
		var lab := Label.new()
		lab.text = text
		lab.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		box.add_child(lab)
	if body:
		box.add_child(body)
	var row := HBoxContainer.new()
	row.alignment = BoxContainer.ALIGNMENT_END
	row.add_theme_constant_override("separation", 10)
	box.add_child(row)
	var first: Button
	for b in buttons:
		var cb: Callable = b[1]
		var btn := Button.new()
		btn.text = b[0]
		btn.pressed.connect(func():
			UiSound.move()
			close_modal()
			cb.call())
		row.add_child(btn)
		if first == null:
			first = btn
	_modal.set_meta("cancel", buttons.back()[1] if buttons.size() else Callable())
	if body is LineEdit:
		(body as LineEdit).grab_focus.call_deferred()
	elif first:
		first.grab_focus.call_deferred()
	UiSound.play_in()


func close_modal() -> void:
	if _modal:
		_modal.queue_free()
		_modal = null


func _input(ev: InputEvent) -> void:
	if _rebinding >= 0 and ev is InputEventKey and ev.pressed and not ev.echo:
		get_viewport().set_input_as_handled()
		if ev.keycode in [KEY_CTRL, KEY_SHIFT, KEY_ALT, KEY_META]:
			return
		var idx := _rebinding
		_rebinding = -1
		if ev.keycode == KEY_ESCAPE:
			_fill_sheet()
			return
		var clash := K.rebind(idx, ev.keycode, K.mods_of(ev))
		if clash != "":
			toast("Also bound to: " + clash, true)
		_fill_sheet()
		return
	if _modal and ev is InputEventKey and ev.pressed and ev.keycode == KEY_ESCAPE:
		get_viewport().set_input_as_handled()
		var cb: Callable = _modal.get_meta("cancel")
		close_modal()
		UiSound.out()
		if cb.is_valid():
			cb.call()
	elif _sheet and _sheet.visible and ev is InputEventKey and ev.pressed and ev.keycode in [KEY_ESCAPE, KEY_F1]:
		get_viewport().set_input_as_handled()
		toggle_sheet()


## Runs then now if the map is saved; otherwise asks Save / Don't save / Cancel first.
func ask_unsaved(then: Callable) -> void:
	if not editor.doc.dirty:
		then.call()
		return
	var name: String = editor.doc.info.get("name", "untitled") if editor.path != "" else "this map"
	_show_modal("UNSAVED CHANGES", "Save the changes to %s first?" % name, [
		["SAVE", func():
			if editor.path == "" or editor.path == Q3MapEditor.map_path(Q3MapEditor.AUTOSAVE):
				save_as_dialog(then)
			elif editor.save_map() == OK:
				then.call()],
		["DON'T SAVE", then],
		["CANCEL", func(): pass],
	])


## Map names become file names (and later BSP names): lowercase letters, digits, - and _.
static func clean_name(s: String) -> String:
	var out := ""
	for ch in s.strip_edges().to_lower().replace(" ", "_"):
		if (ch >= "a" and ch <= "z") or (ch >= "0" and ch <= "9") or ch == "_" or ch == "-":
			out += ch
	return out


func save_as_dialog(then := Callable()) -> void:
	var field := LineEdit.new()
	field.placeholder_text = "map name"
	var current := String(editor.doc.info.get("name", ""))
	field.text = current if current != "untitled" and current != Q3MapEditor.AUTOSAVE else ""
	var go := func():
		var n := clean_name(field.text)
		if n == "" or n == Q3MapEditor.AUTOSAVE:
			toast("Give the map a name (letters, digits, - and _)", true)
			return
		if editor.save_map(n) == OK and then.is_valid():
			then.call()
	field.text_submitted.connect(func(_t):
		close_modal()
		go.call())
	_show_modal("SAVE MAP AS", "Saved in your maps folder.", [["SAVE", go], ["CANCEL", func(): pass]], field)


## Asks for a free-text name (the outliner's); then gets it trimmed ("" clears).
func name_dialog(title: String, current: String, then: Callable) -> void:
	var field := LineEdit.new()
	field.text = current
	field.placeholder_text = "name (empty: none)"
	field.select_all_on_focus = true
	var go := func():
		then.call(field.text.strip_edges())
	field.text_submitted.connect(func(_t):
		close_modal()
		go.call())
	_show_modal(title, "", [["OK", go], ["CANCEL", func(): pass]], field)


## Ctrl+P: asks for a name and saves the selection as a prefab.
func save_prefab_dialog() -> void:
	if editor.selection.is_empty():
		toast("Select something to save as a prefab", true)
		return
	var field := LineEdit.new()
	field.placeholder_text = "prefab name"
	var go := func():
		var n := clean_name(field.text)
		if n == "":
			toast("Give the prefab a name (letters, digits, - and _)", true)
			return
		editor.save_prefab(n)
	field.text_submitted.connect(func(_t):
		close_modal()
		go.call())
	_show_modal("SAVE PREFAB", "The selection, saved by name to stamp into any map from the browser's Prefabs tab. A prefab with the same name is replaced.",
			[["SAVE", go], ["CANCEL", func(): pass]], field)


## A compile's outcome: what happened, and q3map's own output to read (and copy).
func show_compile_log(title: String, summary: String, log_text: String) -> void:
	var box := TextEdit.new()
	box.text = log_text if log_text != "" else "(no output)"
	box.editable = false
	box.custom_minimum_size = Vector2(760, 320)
	box.wrap_mode = TextEdit.LINE_WRAPPING_BOUNDARY
	box.add_theme_font_size_override("font_size", 13)
	box.scroll_vertical = 1e9
	_show_modal(title, summary, [["COPY LOG", func(): DisplayServer.clipboard_set(log_text)], ["OK", func(): pass]], box)
	UiSound.buzz()


## A yes / cancel question; then runs on yes.
func confirm(title: String, text: String, yes: String, then: Callable) -> void:
	_show_modal(title, text, [[yes, then], ["CANCEL", func(): pass]])


## A dialog that stays up while background work runs; cancel runs if the player presses Cancel.
func show_progress(title: String, text: String, cancel: Callable) -> void:
	_show_modal(title, text, [["CANCEL", cancel]])
	_modal.set_meta("progress", true)


func close_progress() -> void:
	if _modal and _modal.has_meta("progress"):
		close_modal()


## Quake 3's maps as the player's paks hold them (the retail game and point releases, and any
## others), by arena name; the one picked is imported from its .bsp.
func import_q3_dialog() -> void:
	var paks := Q3BspImport.maps_in_paks()
	if paks.is_empty():
		toast("No maps found in your Quake 3 paks", true)
		return
	var names: Array = paks.keys()
	names.sort()
	var list := ItemList.new()
	list.custom_minimum_size = Vector2(520, 360)
	for n in names:
		var long := String(Session.map_info(n).get("name", ""))
		list.add_item("%s    %s" % [n, long] if long != "" else n)
	list.select(0)
	var go := func():
		var sel := list.get_selected_items()
		if sel.size():
			editor.import_map("bsp", names[sel[0]])
	list.item_activated.connect(func(_i):
		close_modal()
		go.call())
	_show_modal("IMPORT A QUAKE 3 MAP", "Its brushes, curves and entities become an editable map, aligned as q3map drew it. Save it under a name of your own.",
			[["IMPORT", go], ["CANCEL", func(): pass]], list)
	list.grab_focus.call_deferred()


## Picks one of `items` from a list; then gets it.
func pick_dialog(title: String, text: String, items: Array, then: Callable, ok_label := "IMPORT") -> void:
	var list := ItemList.new()
	list.custom_minimum_size = Vector2(520, 320)
	for it in items:
		list.add_item(String(it))
	list.select(0)
	var go := func():
		var sel := list.get_selected_items()
		if sel.size():
			then.call(items[sel[0]])
	list.item_activated.connect(func(_i):
		close_modal()
		go.call())
	_show_modal(title, text, [[ok_label, go], ["CANCEL", func(): pass]], list)
	list.grab_focus.call_deferred()


## The system's file picker for a .map: opening one when save_name is "", else saving under it.
func map_file_dialog(save_name: String, then: Callable) -> void:
	file_dialog(save_name, PackedStringArray(["*.map ; Radiant maps"]), "Export .map" if save_name != "" else "Import .map", then)


## The system's file picker: opening a file when save_name is "", else saving under it.
func file_dialog(save_name: String, filters: PackedStringArray, title: String, then: Callable) -> void:
	if is_instance_valid(_file_dialog):
		return   # one picker at a time
	var fd := FileDialog.new()
	_file_dialog = fd
	fd.access = FileDialog.ACCESS_FILESYSTEM
	fd.file_mode = FileDialog.FILE_MODE_SAVE_FILE if save_name != "" else FileDialog.FILE_MODE_OPEN_FILE
	fd.filters = filters
	fd.use_native_dialog = true
	fd.title = title
	fd.current_dir = OS.get_system_dir(OS.SYSTEM_DIR_DOCUMENTS)
	if save_name != "":
		fd.current_file = save_name
	fd.file_selected.connect(func(p: String):
		fd.queue_free()
		then.call(p))
	fd.canceled.connect(fd.queue_free)
	add_child(fd)
	fd.popup_centered_ratio(0.6)


## Recent maps first, then every saved map, newest first, each with its thumbnail and when it was
## saved; the autosave (written on leaving and play-tests) is listed as such. Delete asks first.
func open_dialog() -> void:
	var maps := Q3MapEditor.list_maps()
	if maps.is_empty():
		toast("No saved maps yet")
		return
	var by_path := {}
	for m in maps:
		by_path[m["path"]] = m
	var rows := []   # the map each list row opens ({} for a heading)
	var list := ItemList.new()
	list.custom_minimum_size = Vector2(640, 420)
	list.fixed_icon_size = Vector2i(96, 54)
	list.icon_mode = ItemList.ICON_MODE_LEFT
	var section := func(title: String, entries: Array) -> void:
		if entries.is_empty():
			return
		var h := list.add_item(title)
		list.set_item_disabled(h, true)
		list.set_item_selectable(h, false)
		list.set_item_custom_fg_color(h, Q3Theme.GLOW)
		rows.append({})
		for m in entries:
			var name: String = m["name"]
			var label := ("Autosave (from leaving or a play-test)" if name == Q3MapEditor.AUTOSAVE else name)
			var i := list.add_item("%s   ·   %s%s" % [label, Time.get_datetime_string_from_unix_time(m["time"], true).replace("T", "  "),
					"   · open now" if m["path"] == editor.path else ""], _thumb(name))
			rows.append(m)
			if m["path"] == editor.path:
				list.set_item_custom_fg_color(i, Q3Theme.TEXT_HOT)
	var recent := []
	for p in Q3MapEditor.recent_maps():
		if by_path.has(p):
			recent.append(by_path[p])
	section.call("RECENT", recent)
	section.call("ALL MAPS (%d)" % maps.size(), maps)
	for i in rows.size():
		if not rows[i].is_empty():
			list.select(i)
			break
	var picked := func() -> Dictionary:
		var sel := list.get_selected_items()
		return rows[sel[0]] if sel.size() else {}
	var go := func():
		var m: Dictionary = picked.call()
		if m.size():
			editor.open_map(m["path"])
	var delete := func():
		var m: Dictionary = picked.call()
		if m.is_empty():
			return
		confirm("DELETE MAP", "Delete %s for good? Its file and thumbnail go; this can't be undone." % m["name"], "DELETE", func():
			editor.delete_map(m["path"])
			if Q3MapEditor.list_maps().size():
				open_dialog())
	list.item_activated.connect(func(i):
		if not rows[i].is_empty():
			close_modal()
			go.call())
	_show_modal("OPEN MAP", "", [["OPEN", go], ["DELETE", delete], ["CANCEL", func(): pass]], list)
	list.grab_focus.call_deferred()


## A saved map's thumbnail, or null when it has none (headless saves, older maps).
func _thumb(name: String) -> Texture2D:
	var p := Q3MapEditor.thumb_path(name)
	if not FileAccess.file_exists(p):
		return null
	var img := Image.load_from_file(ProjectSettings.globalize_path(p))
	return ImageTexture.create_from_image(img) if img and not img.is_empty() else null


# ---------------------------------------------------------------- F1 sheet

func toggle_sheet() -> void:
	if _sheet == null:
		_sheet = PanelContainer.new()
		_sheet.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT, Control.PRESET_MODE_KEEP_SIZE, 0)
		_sheet.offset_left = 120
		_sheet.offset_right = -120
		_sheet.offset_top = BAR_H + 30
		_sheet.offset_bottom = -60
		_sheet.visible = false
		add_child(_sheet)
	_sheet.visible = not _sheet.visible
	if _sheet.visible:
		_fill_sheet()
		UiSound.play_in()
	else:
		_rebinding = -1
		UiSound.out()


func _fill_sheet() -> void:
	for c in _sheet.get_children():
		c.queue_free()
	_sheet_version = K.version
	var outer := VBoxContainer.new()
	outer.add_theme_constant_override("separation", 10)
	_sheet.add_child(outer)
	var head := HBoxContainer.new()
	outer.add_child(head)
	var title := Label.new()
	title.text = "HOTKEYS"
	title.theme_type_variation = "TitleLabel"
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	head.add_child(title)
	var note := Label.new()
	note.text = "Click a key to change it · Esc closes"
	note.theme_type_variation = "DimLabel"
	head.add_child(note)
	head.add_child(_bar_button("RESET KEYS", "Back to the default keys", func():
		K.reset()
		_fill_sheet()))
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	outer.add_child(scroll)
	var cols := HBoxContainer.new()
	cols.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	cols.add_theme_constant_override("separation", 40)
	scroll.add_child(cols)
	var by_cat := {}
	var order := []
	var eff := K.effective()
	for i in eff.size():
		var cat: String = eff[i][3]
		if not by_cat.has(cat):
			by_cat[cat] = []
			order.append(cat)
		by_cat[cat].append(i)
	var col_boxes := []
	for c in 2:
		var v := VBoxContainer.new()
		v.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		v.add_theme_constant_override("separation", 4)
		cols.add_child(v)
		col_boxes.append(v)
	var left := 0
	var right := 0
	for cat in order:
		var target: VBoxContainer = col_boxes[0] if left <= right else col_boxes[1]
		if left <= right:
			left += by_cat[cat].size() + 2
		else:
			right += by_cat[cat].size() + 2
		var gl := Label.new()
		gl.text = cat.to_upper()
		gl.theme_type_variation = "GroupLabel"
		target.add_child(gl)
		for i in by_cat[cat]:
			var b: Array = eff[i]
			var row := HBoxContainer.new()
			row.add_theme_constant_override("separation", 12)
			target.add_child(row)
			var keyb := Button.new()
			keyb.custom_minimum_size.x = 150
			keyb.theme_type_variation = "SmallButton"
			keyb.focus_mode = Control.FOCUS_NONE
			keyb.text = "press a key…" if i == _rebinding else K.key_text(b)
			if b[0] == "":
				keyb.disabled = true
			else:
				var idx: int = i
				keyb.pressed.connect(func():
					_rebinding = idx
					_fill_sheet())
				if K.conflict(i):
					keyb.add_theme_color_override("font_color", Q3Theme.GLOW)
			row.add_child(keyb)
			var lab := Label.new()
			lab.text = b[4]
			lab.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
			lab.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			lab.add_theme_font_size_override("font_size", 15)
			row.add_child(lab)
