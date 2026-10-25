class_name Q3EditorBrowser
extends PanelContainer
## The map editor's browser (OZMIUM's Explorer): three tabs of tiles with search, in sections.
##   Materials   world textures, skies, or clip & caulk (q3map's tool shaders), picked under the tab;
##               favorites (right-click) and recents first, then a section per folder (the models'
##               own shaders last); a click makes it current and paints the selection
##   Entities    every pickup (rendered by the thumbnail camera) and map class, by section; a click
##               arms the entity tool, or makes the selected brushes into a brush entity
##   Prefabs     saved prefabs with their thumbnails; a click arms the prefab tool, right-click deletes
## It's docked along the bottom, under the view (Unity's Project panel), its controls in one row. S M L
## sets the tile size; the columns follow the panel's width. Thumbnails load as their tiles scroll
## into view. The map's own panels (Objects, History) are on the right, above the inspector.

const TILES := {"S": Vector2(76, 96), "M": Vector2(104, 126), "L": Vector2(148, 170)}
const LOADS_PER_FRAME := 8
const TABS := [["MATERIALS", "surface"], ["ENTITIES", "entity"], ["PREFABS", "prefab"]]
const KINDS := [["TEXTURES", "surface", "Walls, floors, trims and liquids"], ["SKIES", "sky", "Sky shaders: a brush face of one shows the sky"],
		["CLIP & CAULK", "tool", "q3map's tool shaders: caulk, clip, hint, trigger, nodraw ..."]]
const RECENT_SHOWN := 12

var editor: Q3MapEditor
var tab := "surface"            ## surface, sky, tool, entity, prefab
var folder := ""                ## "" all, "*fav", "*recent", a folder, or an Entities section
var tile_size := "M"

var _search: LineEdit
var _folder: OptionButton
var _scroll: ScrollContainer
var _list: VBoxContainer        ## the sections: a heading and a grid each
var _grids: Array = []
var _tabs := {}
var _kinds := {}
var _kind_row: HBoxContainer
var _sizes := {}
var _tiles: Array = []          ## [{name, button, rect, loaded}]
var _pending := false           ## tiles in view still without a thumbnail
var _settle := 0                ## frames to keep looking after a rebuild
var _count: Label
var _reveal := ""               ## a material to scroll to once the tiles are laid out
static var _swatches := {}      ## color -> Texture2D (map class tiles)


func _ready() -> void:
	custom_minimum_size.y = 250
	mouse_filter = Control.MOUSE_FILTER_STOP
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 6)
	add_child(box)
	# one row across the bottom: the tabs, the material kinds, search, folder, the count and tile size
	var head := HBoxContainer.new()
	head.add_theme_constant_override("separation", 4)
	box.add_child(head)
	for t in TABS:
		var b := _toggle(t[0], "")
		b.size_flags_horizontal = Control.SIZE_FILL
		b.custom_minimum_size.x = 104
		var id: String = t[1]
		b.pressed.connect(func():
			UiSound.move()
			set_tab(tab if id == "surface" and tab in ["surface", "sky", "tool"] else id))
		head.add_child(b)
		_tabs[id] = b
	head.add_child(VSeparator.new())
	# beside Materials: which kind
	_kind_row = HBoxContainer.new()
	_kind_row.add_theme_constant_override("separation", 2)
	head.add_child(_kind_row)
	for k in KINDS:
		var b := _toggle(k[0], k[2])
		b.size_flags_horizontal = Control.SIZE_FILL
		b.add_theme_font_size_override("font_size", 13)
		var id: String = k[1]
		b.pressed.connect(func():
			UiSound.move()
			set_tab(id))
		_kind_row.add_child(b)
		_kinds[id] = b
	_search = LineEdit.new()
	_search.clear_button_enabled = true
	_search.custom_minimum_size.x = 200
	_search.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_search.text_changed.connect(func(_t): rebuild())
	head.add_child(_search)
	_folder = OptionButton.new()
	_folder.focus_mode = Control.FOCUS_NONE
	_folder.custom_minimum_size.x = 190
	_folder.fit_to_longest_item = false
	_folder.item_selected.connect(func(i):
		folder = String(_folder.get_item_metadata(i))
		rebuild())
	head.add_child(_folder)
	_count = Label.new()
	_count.theme_type_variation = "DimLabel"
	_count.add_theme_font_size_override("font_size", 13)
	_count.clip_text = true
	_count.mouse_filter = Control.MOUSE_FILTER_PASS
	_count.custom_minimum_size.x = 120
	_count.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	head.add_child(_count)
	for z in ["S", "M", "L"]:
		var b := _toggle(z, "Tile size")
		b.custom_minimum_size.x = 28
		b.size_flags_horizontal = Control.SIZE_FILL
		var size_id: String = z
		b.pressed.connect(func():
			UiSound.move()
			set_tile_size(size_id)
			editor.ui._save_layout())
		head.add_child(b)
		_sizes[z] = b
	_scroll = ScrollContainer.new()
	_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	_scroll.get_v_scroll_bar().value_changed.connect(func(_v): _pending = true)
	visibility_changed.connect(func(): _settle = 3)
	box.add_child(_scroll)
	_list = VBoxContainer.new()
	_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_list.add_theme_constant_override("separation", 4)
	_scroll.add_child(_list)
	resized.connect(_fit_columns)
	_scroll.resized.connect(_fit_columns)   # the first layout gives it a width only after the tiles are made
	editor.status_changed.connect(_show_current)
	editor.thumbs.rendered.connect(_on_rendered)
	editor.prefabs_changed.connect(func():
		if tab == "prefab":
			rebuild())
	_show_sizes()
	set_tab("surface")


func _toggle(text: String, tip: String) -> Button:
	var b := Button.new()
	b.text = text
	b.tooltip_text = tip
	b.toggle_mode = true
	b.theme_type_variation = "SmallButton"
	b.focus_mode = Control.FOCUS_NONE
	b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	return b


func set_tab(id: String) -> void:
	tab = id
	var materials := id in ["surface", "sky", "tool"]
	for k in _tabs:
		_tabs[k].set_pressed_no_signal(k == id or (k == "surface" and materials))
	for k in _kinds:
		_kinds[k].set_pressed_no_signal(k == id)
	_kind_row.visible = materials
	_folder.clear()
	_folder.add_item("All")
	_folder.set_item_metadata(0, "")
	match id:
		"surface", "sky", "tool":
			for extra in [["★ Favorites", "*fav"], ["Recent", "*recent"]]:
				_folder.add_item(extra[0])
				_folder.set_item_metadata(_folder.item_count - 1, extra[1])
			for f in _folders():
				_folder.add_item(f)
				_folder.set_item_metadata(_folder.item_count - 1, f)
		"entity":
			for s in Q3MapEntities.catalog():
				_folder.add_item(s[0])
				_folder.set_item_metadata(_folder.item_count - 1, s[0])
	_folder.visible = id != "prefab"
	_search.placeholder_text = {"entity": "Search entities", "prefab": "Search prefabs", "sky": "Search skies"}.get(id, "Search materials")
	folder = ""
	_folder.select(0)
	rebuild()


## The tab's folders: world texture folders by name, the models' own shaders after them.
func _folders() -> Array:
	var folders := {}
	for n in Q3MapMaterials.names(tab):
		folders[Q3MapMaterials.folder(n)] = true
	var fl := folders.keys()
	var late := ["models", "sprites", "gfx"]
	fl.sort_custom(func(a, b): return (a in late) == (b in late) and a < b or (b in late and not a in late))
	return fl


func set_tile_size(id: String) -> void:
	if not TILES.has(id):
		return
	tile_size = id
	_show_sizes()
	if _list != null:
		rebuild()


func _show_sizes() -> void:
	for k in _sizes:
		_sizes[k].set_pressed_no_signal(k == tile_size)


## The names on show for the tab, folder and search (material shader names, entity classnames or
## prefab names).
func visible_names() -> PackedStringArray:
	var out := PackedStringArray()
	var seen := {}
	for s in _sections():
		for n in s[1]:
			if not seen.has(n):   # favorites and recents show in their folders too
				seen[n] = true
				out.append(n)
	return out


## [[heading, names], ...]: with nothing narrowed, favorites and recents first, then a section per
## folder (or Entities section); otherwise one section without a heading.
func _sections() -> Array:
	var q := _search.text.strip_edges().to_lower()
	match tab:
		"entity":
			var out := []
			for s in Q3MapEntities.catalog():
				if folder != "" and s[0] != folder:
					continue
				var names := PackedStringArray()
				for cn in s[1]:
					if q == "" or cn.to_lower().contains(q) or Q3MapEntities.description(cn).to_lower().contains(q):
						names.append(cn)
				if names.size():
					out.append([s[0] if folder == "" else "", names])
			return out
		"prefab":
			var names := PackedStringArray()
			for p in Q3MapPrefabs.list():
				if q == "" or String(p["name"]).to_lower().contains(q):
					names.append(p["name"])
			return [["", names]]
	var all := Q3MapMaterials.names(tab)
	var ok := func(n: String) -> bool: return q == "" or n.to_lower().contains(q)
	match folder:
		"*fav", "*recent":
			var names := PackedStringArray()
			for n in Q3MapMaterials.prefs()["favorites" if folder == "*fav" else "recent"]:
				if n in all and ok.call(n):
					names.append(n)
			return [["", names]]
		"":
			if q != "":
				var names := PackedStringArray()
				for n in all:
					if ok.call(n):
						names.append(n)
				return [["", names]]
			var out := []
			var fav := PackedStringArray()
			for n in Q3MapMaterials.prefs()["favorites"]:
				if n in all:
					fav.append(n)
			if fav.size():
				out.append(["★ FAVORITES", fav])
			var rec := PackedStringArray()
			for n in Q3MapMaterials.prefs()["recent"]:
				if n in all and rec.size() < RECENT_SHOWN:
					rec.append(n)
			if rec.size():
				out.append(["RECENT", rec])
			var by := {}
			for n in all:
				var f := Q3MapMaterials.folder(n)
				if not by.has(f):
					by[f] = PackedStringArray()
				by[f].append(n)
			for f in _folders():
				var names: PackedStringArray = by[f]
				names.sort()
				out.append([f.to_upper(), names])
			return out
	var names := PackedStringArray()
	for n in all:
		if Q3MapMaterials.folder(n) == folder and ok.call(n):
			names.append(n)
	names.sort()
	return [["", names]]


func rebuild() -> void:
	for c in _list.get_children():
		c.queue_free()
	_tiles.clear()
	_grids.clear()
	var k := 0
	for s in _sections():
		if s[1].is_empty():
			continue
		if s[0] != "":
			var h := Label.new()
			h.text = s[0]
			h.theme_type_variation = "GroupLabel"
			h.add_theme_font_size_override("font_size", 13)
			_list.add_child(h)
		var grid := GridContainer.new()
		grid.add_theme_constant_override("h_separation", 4)
		grid.add_theme_constant_override("v_separation", 4)
		_list.add_child(grid)
		_grids.append(grid)
		for n in s[1]:
			_tiles.append(_tile(n, grid))
		k += s[1].size()
	_fit_columns()
	var shown := _tiles.size()
	match tab:
		"entity":
			_count.text = "%d entit%s" % [shown, "y" if shown == 1 else "ies"]
			_count.tooltip_text = "Click to place · brush classes (func_, trigger_) make the selected brushes into one"
		"prefab":
			_count.text = "%d prefab%s" % [shown, "" if shown == 1 else "s"] if shown else "No prefabs yet: Ctrl+P saves the selection as one"
			_count.tooltip_text = "Click to place · Ctrl+P saves the selection · right-click: delete"
		_:
			_count.text = "%d material%s" % [shown, "" if shown == 1 else "s"]
			_count.tooltip_text = "Click: paint the selection and make it current · right-click: favorite"
	_scroll.scroll_vertical = 0
	_settle = 3
	_show_current()


## Columns that fill the panel's width.
func _fit_columns() -> void:
	if _scroll == null:
		return
	var w := _scroll.size.x - 16.0
	var cols := maxi(1, int((w + 4.0) / (TILES[tile_size].x + 4.0)))
	for g in _grids:
		if is_instance_valid(g):
			g.columns = cols
	_settle = 3


func _tile(n: String, grid: GridContainer) -> Dictionary:
	var ts: Vector2 = TILES[tile_size]
	var b := Button.new()
	b.custom_minimum_size = ts
	b.focus_mode = Control.FOCUS_NONE
	b.toggle_mode = tab in ["surface", "sky", "tool"]
	var v := VBoxContainer.new()
	v.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT, Control.PRESET_MODE_MINSIZE, 4)
	v.mouse_filter = Control.MOUSE_FILTER_IGNORE
	b.add_child(v)
	var r := TextureRect.new()
	r.custom_minimum_size = Vector2(ts.x - 8, ts.x - 8)
	r.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	r.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
	r.mouse_filter = Control.MOUSE_FILTER_IGNORE
	v.add_child(r)
	var l := Label.new()
	l.clip_text = true
	l.custom_minimum_size.x = ts.x - 8
	l.add_theme_font_size_override("font_size", 12 if tile_size != "L" else 13)
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
	grid.add_child(b)
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


## Shows a material: its kind's tab, its folder, scrolled to its tile.
func show_material(m: String) -> void:
	if not visible:
		editor.ui.toggle_panels()
	var k := Q3MapMaterials.kind(m)
	set_tab("sky" if k == "sky" else ("tool" if k in ["hidden", "fog"] else "surface"))
	var f := Q3MapMaterials.folder(m)
	for i in _folder.item_count:
		if _folder.get_item_metadata(i) == f:
			_folder.select(i)
			folder = f
			rebuild()
			break
	_reveal = m


func _process(_delta: float) -> void:
	if _settle > 0:
		_settle -= 1     # new tiles have no place on screen until the containers lay them out
		_pending = true
		if _settle == 0 and _reveal != "":
			for t in _tiles:
				if t["name"] == _reveal:
					_scroll.ensure_control_visible(t["button"])
					break
			_reveal = ""
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


## The current material's tiles show pressed.
func _show_current() -> void:
	var m := editor.material
	for t in _tiles:
		(t["button"] as Button).set_pressed_no_signal(t["name"] == m)
