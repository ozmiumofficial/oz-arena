class_name Q3EditorBrowser
extends PanelContainer
## The map editor's browser (OZMIUM's Explorer): tabs of tiles with search.
##   Materials, Skies, Tools  every remastered world material, by folder, with favorites (right-click)
##                            and recents; a click makes it current and paints the selection
##   Entities                 every pickup (rendered by the thumbnail camera) and map class, by
##                            section; a click arms the entity tool, or makes the selected brushes
##                            into a brush entity
##   Prefabs                  saved prefabs with their thumbnails; a click arms the prefab tool,
##                            right-click deletes
## Thumbnails load as their tiles scroll into view.

const TILE := Vector2(100, 122)
const LOADS_PER_FRAME := 8
const TABS := [["MATERIALS", "surface"], ["SKIES", "sky"], ["TOOLS", "tool"], ["ENTITIES", "entity"], ["PREFABS", "prefab"],
		["OBJECTS", "objects"], ["HISTORY", "history"]]

var editor: Q3MapEditor
var tab := "surface"            ## surface, sky, tool, entity, prefab, objects, history
var outliner: Q3EditorOutliner
var history: Q3EditorHistory
var folder := ""                ## "" all, "*fav", "*recent", a folder, or an Entities section

var _search: LineEdit
var _folder: OptionButton
var _scroll: ScrollContainer
var _grid: GridContainer
var _tabs := {}
var _tiles: Array = []          ## [{name, button, rect, loaded}]
var _pending := false           ## tiles in view still without a thumbnail
var _settle := 0                ## frames to keep looking after a rebuild
var _current_row: Control
var _current: Label
var _current_rect: TextureRect
var _count: Label
static var _swatches := {}      ## color -> Texture2D (map class tiles)


func _ready() -> void:
	custom_minimum_size.x = 460   # wider than any tab's contents, so switching tabs never moves the view
	mouse_filter = Control.MOUSE_FILTER_STOP
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 6)
	add_child(box)
	# three rows of tabs: the material kinds; entities and prefabs; the map's objects and history
	for row_tabs in [TABS.slice(0, 3), TABS.slice(3, 5), TABS.slice(5)]:
		var tabs := HBoxContainer.new()
		box.add_child(tabs)
		for t in row_tabs:
			var b := Button.new()
			b.text = t[0]
			b.toggle_mode = true
			b.theme_type_variation = "SmallButton"
			b.focus_mode = Control.FOCUS_NONE
			b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			var id: String = t[1]
			b.pressed.connect(func():
				UiSound.move()
				set_tab(id))
			tabs.add_child(b)
			_tabs[id] = b
	_search = LineEdit.new()
	_search.clear_button_enabled = true
	_search.text_changed.connect(func(_t): rebuild())
	box.add_child(_search)
	_folder = OptionButton.new()
	_folder.focus_mode = Control.FOCUS_NONE
	_folder.item_selected.connect(func(i):
		folder = String(_folder.get_item_metadata(i))
		rebuild())
	box.add_child(_folder)
	_count = Label.new()
	_count.theme_type_variation = "DimLabel"
	_count.add_theme_font_size_override("font_size", 13)
	_count.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	box.add_child(_count)
	_scroll = ScrollContainer.new()
	_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	_scroll.get_v_scroll_bar().value_changed.connect(func(_v): _pending = true)
	visibility_changed.connect(func(): _settle = 3)
	box.add_child(_scroll)
	_grid = GridContainer.new()
	_grid.columns = 4
	_grid.add_theme_constant_override("h_separation", 4)
	_grid.add_theme_constant_override("v_separation", 4)
	_scroll.add_child(_grid)
	outliner = Q3EditorOutliner.new()
	outliner.editor = editor
	outliner.size_flags_vertical = Control.SIZE_EXPAND_FILL
	outliner.visible = false
	box.add_child(outliner)
	history = Q3EditorHistory.new()
	history.editor = editor
	history.size_flags_vertical = Control.SIZE_EXPAND_FILL
	history.visible = false
	box.add_child(history)
	var cur := HBoxContainer.new()
	cur.add_theme_constant_override("separation", 8)
	box.add_child(cur)
	_current_row = cur
	_current_rect = TextureRect.new()
	_current_rect.custom_minimum_size = Vector2(40, 40)
	_current_rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_current_rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
	cur.add_child(_current_rect)
	_current = Label.new()
	_current.add_theme_font_size_override("font_size", 14)
	_current.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_current.clip_text = true
	cur.add_child(_current)
	editor.status_changed.connect(_show_current)
	editor.thumbs.rendered.connect(_on_rendered)
	editor.prefabs_changed.connect(func():
		if tab == "prefab":
			rebuild())
	set_tab("surface")


func set_tab(id: String) -> void:
	tab = id
	for k in _tabs:
		_tabs[k].set_pressed_no_signal(k == id)
	_folder.clear()
	_folder.add_item("All")
	_folder.set_item_metadata(0, "")
	match id:
		"surface":
			for extra in [["★ Favorites", "*fav"], ["Recent", "*recent"]]:
				_folder.add_item(extra[0])
				_folder.set_item_metadata(_folder.item_count - 1, extra[1])
			var folders := {}
			for n in Q3MapMaterials.names("surface"):
				folders[Q3MapMaterials.folder(n)] = true
			var fl := folders.keys()
			fl.sort()
			for f in fl:
				_folder.add_item(f)
				_folder.set_item_metadata(_folder.item_count - 1, f)
		"entity":
			for s in Q3MapEntities.catalog():
				_folder.add_item(s[0])
				_folder.set_item_metadata(_folder.item_count - 1, s[0])
	_folder.visible = id in ["surface", "entity"]
	_current_row.visible = id in ["surface", "sky", "tool"]
	# the Objects and History tabs are panels of their own in place of the tiles
	var panel := id in ["objects", "history"]
	for c in [_search, _count, _scroll]:
		c.visible = not panel
	outliner.visible = id == "objects"
	history.visible = id == "history"
	if panel:
		folder = ""
		return
	_search.placeholder_text = {"entity": "Search entities", "prefab": "Search prefabs"}.get(id, "Search materials")
	folder = ""
	_folder.select(0)
	rebuild()


## The names on show for the tab, folder and search (material shader names, entity classnames or
## prefab names).
func visible_names() -> PackedStringArray:
	var q := _search.text.strip_edges().to_lower()
	var out := PackedStringArray()
	match tab:
		"entity":
			for s in Q3MapEntities.catalog():
				if folder != "" and s[0] != folder:
					continue
				for cn in s[1]:
					if q == "" or cn.to_lower().contains(q) or Q3MapEntities.description(cn).to_lower().contains(q):
						out.append(cn)
			return out
		"prefab":
			for p in Q3MapPrefabs.list():
				if q == "" or String(p["name"]).to_lower().contains(q):
					out.append(p["name"])
			return out
	var src: PackedStringArray
	match folder:
		"*fav": src = PackedStringArray(Q3MapMaterials.prefs()["favorites"])
		"*recent": src = PackedStringArray(Q3MapMaterials.prefs()["recent"])
		_: src = Q3MapMaterials.names(tab)
	for n in src:
		if folder != "" and not folder.begins_with("*") and Q3MapMaterials.folder(n) != folder:
			continue
		if q != "" and not n.to_lower().contains(q):
			continue
		out.append(n)
	return out


func rebuild() -> void:
	for c in _grid.get_children():
		c.queue_free()
	_tiles.clear()
	var names := visible_names()
	for n in names:
		_tiles.append(_tile(n))
	var k := names.size()
	match tab:
		"entity":
			_count.text = "%d entit%s · click to place · brush classes (func_, trigger_) make the selected brushes into one" % [k, "y" if k == 1 else "ies"]
		"prefab":
			_count.text = "%d prefab%s · click to place · Ctrl+P saves the selection · right-click: delete" % [k, "" if k == 1 else "s"] \
					if k else "No prefabs yet: select part of the map and press Ctrl+P to save it as one."
		_:
			_count.text = "%d material%s · right-click: favorite" % [k, "" if k == 1 else "s"]
	_scroll.scroll_vertical = 0
	_settle = 3
	_show_current()


func _tile(n: String) -> Dictionary:
	var b := Button.new()
	b.custom_minimum_size = TILE
	b.focus_mode = Control.FOCUS_NONE
	b.toggle_mode = tab in ["surface", "sky", "tool"]
	var v := VBoxContainer.new()
	v.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT, Control.PRESET_MODE_MINSIZE, 4)
	v.mouse_filter = Control.MOUSE_FILTER_IGNORE
	b.add_child(v)
	var r := TextureRect.new()
	r.custom_minimum_size = Vector2(92, 92)
	r.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	r.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
	r.mouse_filter = Control.MOUSE_FILTER_IGNORE
	v.add_child(r)
	var l := Label.new()
	l.clip_text = true
	l.custom_minimum_size.x = 92
	l.add_theme_font_size_override("font_size", 12)
	l.mouse_filter = Control.MOUSE_FILTER_IGNORE
	v.add_child(l)
	match tab:
		"entity":
			var item := Q3MapEntities.is_item(n)
			l.text = Q3MapEntities.description(n) if item else n
			b.tooltip_text = "%s\n%s" % [n, Q3MapEntities.description(n)]
			if not item:
				# map classes: a swatch in the class color with its short name over it
				r.texture = _swatch(Q3MapEntities.color(n))
				var tag := Label.new()
				tag.text = n.get_slice("_", n.get_slice_count("_") - 1).to_upper()
				tag.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
				tag.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
				tag.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
				tag.add_theme_font_size_override("font_size", 13)
				tag.add_theme_color_override("font_outline_color", Color.BLACK)
				tag.add_theme_constant_override("outline_size", 5)
				tag.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
				r.add_child(tag)
			b.pressed.connect(func():
				UiSound.move()
				editor.use_class(n))
		"prefab":
			l.text = n
			b.tooltip_text = n
			b.pressed.connect(func():
				UiSound.move()
				editor.start_prefab(n))
			b.gui_input.connect(func(ev):
				if ev is InputEventMouseButton and ev.pressed and ev.button_index == MOUSE_BUTTON_RIGHT:
					b.accept_event()
					editor.ui.confirm("DELETE PREFAB", "Delete the prefab %s? This can't be undone." % n, "DELETE",
							func(): editor.delete_prefab(n)))
		_:
			l.text = ("★ " if Q3MapMaterials.is_favorite(n) else "") + n.get_file()
			b.tooltip_text = "%s\n%s · %d × %d" % [n, Q3MapMaterials.kind(n), Q3MapMaterials.size(n).x, Q3MapMaterials.size(n).y]
			b.pressed.connect(func():
				UiSound.move()
				editor.apply_material(n))
			b.gui_input.connect(func(ev):
				if ev is InputEventMouseButton and ev.pressed and ev.button_index == MOUSE_BUTTON_RIGHT:
					Q3MapMaterials.toggle_favorite(n)
					l.text = ("★ " if Q3MapMaterials.is_favorite(n) else "") + n.get_file()
					UiSound.move()
					b.accept_event())
	_grid.add_child(b)
	var loaded := tab == "entity" and not Q3MapEntities.is_item(n)
	return {"name": n, "button": b, "rect": r, "loaded": loaded}


static func _swatch(c: Color) -> Texture2D:
	if not _swatches.has(c):
		var img := Image.create(32, 32, false, Image.FORMAT_RGBA8)
		for y in 32:
			for x in 32:
				img.set_pixel(x, y, c.darkened(0.55 + 0.25 * float(y) / 31.0))
		_swatches[c] = ImageTexture.create_from_image(img)
	return _swatches[c]


func _process(_delta: float) -> void:
	if _settle > 0:
		_settle -= 1     # new tiles have no place on screen until the containers lay them out
		_pending = true
	if not _pending or not is_visible_in_tree():
		return
	var view := _scroll.get_global_rect()
	var loads := 0
	_pending = false
	for t in _tiles:
		if t["loaded"]:
			continue
		var b: Button = t["button"]
		if not b.is_inside_tree() or not view.intersects(b.get_global_rect()):
			continue
		if loads >= LOADS_PER_FRAME:
			_pending = true
			break
		match tab:
			"entity": t["rect"].texture = editor.thumbs.item(t["name"])
			"prefab": t["rect"].texture = Q3MapPrefabs.thumbnail(t["name"])
			_: t["rect"].texture = Q3MapMaterials.thumbnail(t["name"])
		t["loaded"] = true
		loads += 1


## A thumbnail the camera just finished: put it on its tile.
func _on_rendered(key: String, tex: Texture2D) -> void:
	var want := ("item:" if tab == "entity" else "prefab:") + "%s"
	for t in _tiles:
		if want % t["name"] == key:
			t["rect"].texture = tex


func _show_current() -> void:
	if _current == null:
		return
	var m := editor.material
	_current.text = "Current: " + Q3MapMaterials.short_name(m)
	_current_rect.texture = Q3MapMaterials.thumbnail(m)
	for t in _tiles:
		(t["button"] as Button).set_pressed_no_signal(t["name"] == m)
