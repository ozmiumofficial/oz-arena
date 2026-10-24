class_name Q3EditorOutliner
extends VBoxContainer
## The browser's Objects tab (OZMIUM's Outliner): every group, brush, curve and entity in the map.
## Groups come first, folded, with their members inside; then everything else in id order, a page
## at a time. Click selects (Shift / Ctrl add), double-click frames, right-click names the row. The
## Show and Lock columns hide and lock (a group's rows do all its members), each one undo step.

const PAGE := 100   ## entries a page (a group is one entry)

var editor: Q3MapEditor

var _head: Label
var _filter: LineEdit
var _tree: Tree
var _pager: HBoxContainer
var _page_label: Label
var _page := 0
var _drawn_key := []        ## [doc, revision, selection, filter, page] last drawn
var _syncing := false       ## selecting rows to match the editor, not the other way
var _dirty := true


func _ready() -> void:
	add_theme_constant_override("separation", 6)
	_head = Label.new()
	_head.theme_type_variation = "DimLabel"
	_head.add_theme_font_size_override("font_size", 13)
	_head.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	add_child(_head)
	var row := HBoxContainer.new()
	add_child(row)
	for b in [["ISOLATE", "view.isolate", "Hide everything but the selection (again: show it all)"],
			["UNHIDE ALL", "edit.unhideAll", "Show every hidden object"], ["UNLOCK ALL", "edit.unlockAll", "Unlock every locked object"]]:
		var btn := Button.new()
		btn.text = b[0]
		btn.tooltip_text = b[2]
		btn.theme_type_variation = "SmallButton"
		btn.focus_mode = Control.FOCUS_NONE
		btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		var cmd: String = b[1]
		btn.pressed.connect(func():
			UiSound.move()
			editor.execute(cmd))
		row.add_child(btn)
	_filter = LineEdit.new()
	_filter.placeholder_text = "Filter by name, class or material"
	_filter.clear_button_enabled = true
	_filter.text_changed.connect(func(_t):
		_page = 0
		refresh())
	add_child(_filter)
	_tree = Tree.new()
	_tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_tree.columns = 3
	_tree.hide_root = true
	_tree.select_mode = Tree.SELECT_MULTI
	_tree.column_titles_visible = true
	_tree.set_column_title(0, "Object")
	_tree.set_column_title(1, "Show")
	_tree.set_column_title(2, "Lock")
	for c in [1, 2]:
		_tree.set_column_expand(c, false)
		_tree.set_column_custom_minimum_width(c, 52)
	# the theme has no box for an unchecked cell, so the Lock column looked empty: drawn here
	_tree.add_theme_icon_override("checked", _box_icon(true))
	_tree.add_theme_icon_override("unchecked", _box_icon(false))
	_tree.multi_selected.connect(func(_item, _col, _sel):
		if not _syncing:
			_pick_rows.call_deferred())
	_tree.item_activated.connect(_frame_row)
	_tree.item_edited.connect(_on_edited)
	_tree.allow_rmb_select = true   # without it the Tree never reports a right click
	_tree.item_mouse_selected.connect(func(pos, button):
		if button == MOUSE_BUTTON_RIGHT:
			_rename_row(_tree.get_item_at_position(pos)))
	add_child(_tree)
	_pager = HBoxContainer.new()
	add_child(_pager)
	var prev := Button.new()
	prev.text = "◀"
	prev.theme_type_variation = "SmallButton"
	prev.focus_mode = Control.FOCUS_NONE
	prev.pressed.connect(func():
		_page = maxi(_page - 1, 0)
		refresh())
	_pager.add_child(prev)
	_page_label = Label.new()
	_page_label.theme_type_variation = "DimLabel"
	_page_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_page_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_pager.add_child(_page_label)
	var next := Button.new()
	next.text = "▶"
	next.theme_type_variation = "SmallButton"
	next.focus_mode = Control.FOCUS_NONE
	next.pressed.connect(func():
		_page += 1
		refresh())
	_pager.add_child(next)
	editor.status_changed.connect(func(): _dirty = true)
	editor.selection_changed.connect(func(): _dirty = true)
	visibility_changed.connect(func(): _dirty = true)


func _process(_delta: float) -> void:
	# redrawn at most once a frame, and only while the tab is showing
	if _dirty and is_visible_in_tree():
		_dirty = false
		refresh()


## A 16 px check box: an outline, filled with the theme's glow when checked.
static func _box_icon(on: bool) -> Texture2D:
	var img := Image.create(16, 16, false, Image.FORMAT_RGBA8)
	img.fill(Color(0, 0, 0, 0))
	var edge := Color(0.75, 0.72, 0.7)
	for i in range(1, 15):
		for p in [Vector2i(i, 1), Vector2i(i, 14), Vector2i(1, i), Vector2i(14, i)]:
			img.set_pixelv(p, edge)
	if on:
		img.fill_rect(Rect2i(4, 4, 8, 8), Q3Theme.GLOW)
	return ImageTexture.create_from_image(img)


## What a row says for an object: its name, else what it is, with its id.
static func label(doc: Q3MapDoc, id: int) -> String:
	var o := doc.get_obj(id)
	var name := String(o.get("name", ""))
	var what := ""
	match o.get("type", ""):
		"brush":
			var m := String(o["faces"][0]["material"]).get_file() if o["faces"].size() else ""
			what = "Brush #%d · %s" % [id, m]
		"patch":
			what = "Curve #%d · %s" % [id, String(o["material"]).get_file()]
		"entity":
			var tn := String(o["keys"].get("targetname", ""))
			what = "%s #%d%s" % [o["classname"], id, (" · " + tn) if tn != "" else ""]
	return name + "   (" + what + ")" if name != "" else what


## The rows on show: [[group id or 0, [ids]]], groups first, filtered.
func entries() -> Array:
	var doc := editor.doc
	var q := _filter.text.strip_edges().to_lower() if _filter else ""
	var by_group := {}
	var loose := []
	var ids := doc.objects.keys()
	ids.sort()
	for id in ids:
		var g := int(doc.objects[id].get("group", 0))
		if g != 0 and doc.groups.has(g):
			if not by_group.has(g):
				by_group[g] = []
			by_group[g].append(id)
		else:
			loose.append(id)
	var out := []
	var gids := by_group.keys()
	gids.sort()
	for g in gids:
		var members: Array = by_group[g]
		var gname := String(doc.groups[g].get("name", "Group %d" % g))
		if q == "" or gname.to_lower().contains(q) or members.any(func(i): return label(doc, i).to_lower().contains(q)):
			out.append([g, members])
	for id in loose:
		if q == "" or label(doc, id).to_lower().contains(q):
			out.append([0, [id]])
	return out


func refresh() -> void:
	if editor == null or _tree == null:
		return
	var doc := editor.doc
	var key := [doc, editor.doc_revision(), editor.selection.hash(), doc.groups.hash(), _filter.text, _page]
	if key == _drawn_key:
		return
	_drawn_key = key
	var all := entries()
	var pages := maxi(1, ceili(all.size() / float(PAGE)))
	_page = clampi(_page, 0, pages - 1)
	_head.text = "%d objects · %d group%s · %d hidden · %d locked" % [doc.objects.size(), doc.groups.size(),
			"" if doc.groups.size() == 1 else "s", doc.hidden.size(), doc.locked.size()]
	var open := {}   # groups left unfolded stay so
	var root := _tree.get_root()
	if root:
		for it in root.get_children():
			if it.get_metadata(0) is Dictionary and not it.collapsed:
				open[int(it.get_metadata(0)["group"])] = true
	_syncing = true
	_tree.clear()
	root = _tree.create_item()
	var first := _page * PAGE
	for e in all.slice(first, first + PAGE):
		var g: int = e[0]
		if g != 0:
			var gi := _tree.create_item(root)
			var members: Array = e[1]
			gi.set_text(0, "▣ %s  (%d)" % [String(doc.groups[g].get("name", "Group %d" % g)), members.size()])
			gi.set_metadata(0, {"group": g, "ids": members})
			gi.collapsed = not open.has(g) and not members.any(func(i): return editor.selection.has(i))
			_flags(gi, members)
			# a huge group (an imported map's brush entity) shows its first PAGE members and any selected
			var shown := 0
			for id in members:
				if shown < PAGE or editor.selection.has(id):
					_row(gi, id)
					shown += 1
			if shown < members.size():
				var more := _tree.create_item(gi)
				more.set_text(0, "… %d more (the filter finds them)" % (members.size() - shown))
				more.set_selectable(0, false)
				more.set_custom_color(0, Color(0.55, 0.55, 0.55))
		else:
			_row(root, e[1][0])
	_page_label.text = "rows %d–%d of %d" % [mini(first + 1, all.size()), mini(first + PAGE, all.size()), all.size()] if all.size() else "nothing matches"
	_pager.visible = all.size() > PAGE
	_syncing = false


func _row(parent: TreeItem, id: int) -> void:
	var it := _tree.create_item(parent)
	var o := editor.doc.get_obj(id)
	var glyph: String = {"brush": "■", "patch": "◪", "entity": "◆"}.get(o.get("type", ""), "·")
	it.set_text(0, glyph + " " + label(editor.doc, id))
	it.set_metadata(0, id)
	_flags(it, [id])
	if editor.selection.has(id):
		it.select(0)


## The Show and Lock checkboxes for these objects (a group's: checked when all of them are).
func _flags(it: TreeItem, ids: Array) -> void:
	for c in [1, 2]:
		it.set_cell_mode(c, TreeItem.CELL_MODE_CHECK)
		it.set_editable(c, true)
	it.set_checked(1, ids.all(func(i): return not editor.doc.hidden.has(i)))
	it.set_checked(2, ids.any(func(i): return editor.doc.locked.has(i)))


static func _ids_of(it: TreeItem) -> Array:
	var m: Variant = it.get_metadata(0)
	if m == null:
		return []   # the "… more" row
	return m["ids"] if m is Dictionary else [m]


## The tree's selected rows become the editor's selection.
func _pick_rows() -> void:
	var ids := []
	var it := _tree.get_next_selected(null)
	while it:
		for id in _ids_of(it):
			if not ids.has(id) and not editor.doc.hidden.has(id) and not editor.doc.locked.has(id):
				ids.append(id)
		it = _tree.get_next_selected(it)
	editor.select(ids)


func _frame_row() -> void:
	var it := _tree.get_selected()
	if it == null:
		return
	var ids := _ids_of(it)
	editor.select(ids)
	editor.execute("view.focus")


func _on_edited() -> void:
	var it := _tree.get_edited()
	var col := _tree.get_edited_column()
	if it == null:
		return
	set_flag(_ids_of(it), "show" if col == 1 else "lock", it.is_checked(col))
	UiSound.move()


## A row's Show or Lock box: flag "show" or "lock", on or off for these objects, one undo step.
func set_flag(ids: Array, flag: String, on: bool) -> void:
	var flags: Dictionary = editor.doc.hidden if flag == "show" else editor.doc.locked
	if ids.all(func(i): return flags.has(i) == (on if flag == "lock" else not on)):
		return   # already so: no empty undo step
	editor.doc.checkpoint(("Show" if on else "Hide") if flag == "show" else ("Lock" if on else "Unlock"))
	if flag == "show":
		editor.doc.set_hidden(ids, not on)
	else:
		editor.doc.set_locked(ids, on)
	if (flag == "show") != on:
		# hidden or locked things can't stay selected
		editor.select(editor.selected_ids().filter(func(i): return not ids.has(i)))


## Right-click: names the row's object or group (an empty name clears it), one undo step.
func _rename_row(it: TreeItem) -> void:
	if it == null:
		return
	var m: Variant = it.get_metadata(0)
	var doc := editor.doc
	var current := String(doc.groups[m["group"]].get("name", "")) if m is Dictionary else String(doc.get_obj(m).get("name", ""))
	editor.ui.name_dialog("NAME " + ("GROUP" if m is Dictionary else "OBJECT"), current, func(n: String):
		doc.checkpoint("Rename")
		if m is Dictionary:
			doc.groups[m["group"]]["name"] = n if n != "" else "Group %d" % m["group"]
			doc.dirty = true
			editor.status_changed.emit()
		else:
			var o := doc.edit(m)
			if n == "":
				o.erase("name")
			else:
				o["name"] = n
			doc.put(o))
