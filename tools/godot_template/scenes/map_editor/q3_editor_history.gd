class_name Q3EditorHistory
extends VBoxContainer
## The browser's History tab (OZMIUM's history in its Map tab): every undo step by what it did,
## oldest first from "Start", the current state marked ▶, steps that redo would bring back in grey
## below. Clicking a row undoes or redoes to it in one go. 100 steps are kept (Q3MapDoc.UNDO_LIMIT);
## opening or starting a map clears them.

var editor: Q3MapEditor

var _list: ItemList
var _foot: Label
var _rows := 0        ## steps applied: the row the document is at (0: Start)
var _drawn := []
var _dirty := true


func _ready() -> void:
	add_theme_constant_override("separation", 6)
	var row := HBoxContainer.new()
	add_child(row)
	for b in [["UNDO", "edit.undo"], ["REDO", "edit.redo"]]:
		var btn := Button.new()
		btn.text = b[0]
		btn.theme_type_variation = "SmallButton"
		btn.focus_mode = Control.FOCUS_NONE
		btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		var cmd: String = b[1]
		btn.pressed.connect(func():
			UiSound.move()
			editor.execute(cmd))
		row.add_child(btn)
	_list = ItemList.new()
	_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_list.item_clicked.connect(func(i, _pos, button):
		if button == MOUSE_BUTTON_LEFT:
			go_to(i))
	add_child(_list)
	_foot = Label.new()
	_foot.theme_type_variation = "DimLabel"
	_foot.add_theme_font_size_override("font_size", 13)
	_foot.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	add_child(_foot)
	editor.status_changed.connect(func(): _dirty = true)
	visibility_changed.connect(func(): _dirty = true)


func _process(_delta: float) -> void:
	if _dirty and is_visible_in_tree():
		_dirty = false
		refresh()


func refresh() -> void:
	var h := editor.doc.history()
	var done: Array = h["done"]
	var undone: Array = h["undone"]
	var key := [editor.doc, done, undone]
	if key == _drawn:
		return
	_drawn = key
	_list.clear()
	_rows = done.size()
	var labels := ["Start"] + done + undone
	for i in labels.size():
		var here := i == _rows
		var idx := _list.add_item(("▶ " if here else ("↻ " if i > _rows else "   ")) + String(labels[i]))
		if here:
			_list.set_item_custom_fg_color(idx, Q3Theme.TEXT_HOT)
		elif i > _rows:
			_list.set_item_custom_fg_color(idx, Color(0.55, 0.55, 0.55))
	_list.ensure_current_is_visible()
	if _rows < _list.item_count:
		_list.select(_rows)
		_list.ensure_current_is_visible()
	_foot.text = "%d of %d steps applied · click a step to go back or forward to it · %d kept at most" % [
			done.size(), done.size() + undone.size(), Q3MapDoc.UNDO_LIMIT]


## Undoes or redoes to row i (0: Start), as one change.
func go_to(i: int) -> void:
	var delta := i - _rows
	if delta == 0:
		return
	editor.clear_selection()
	var n := editor.doc.step(delta)
	UiSound.move()
	editor.notify("%s %d step%s" % ["Undid" if delta < 0 else "Redid", n, "" if n == 1 else "s"])
	editor.status_changed.emit()
	refresh()   # a second click before the next frame must step from here
