extends PanelContainer
## The map's own panels above the inspector: Objects (the outliner) and History, one at a time under
## two tabs. The fold arrow (or clicking the open tab again) shrinks it to its tabs, leaving the
## inspector the whole column.

signal changed      ## folded or switched (the layout is saved)

var editor: Q3MapEditor
var tab := "objects"            ## objects, history
var folded := false
var height := 300.0             ## unfolded (the handle under it sets custom_minimum_size.y)
var outliner: Q3EditorOutliner
var history: Q3EditorHistory

var _tabs := {}
var _fold: Button
var _body: Control


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_STOP
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 6)
	add_child(box)
	var head := HBoxContainer.new()
	head.add_theme_constant_override("separation", 4)
	box.add_child(head)
	for t in [["OBJECTS", "objects", "Every group, brush, curve and entity in the map"], ["HISTORY", "history", "Every undo step; click one to go back to it"]]:
		var b := Button.new()
		b.text = t[0]
		b.tooltip_text = t[2]
		b.toggle_mode = true
		b.theme_type_variation = "SmallButton"
		b.focus_mode = Control.FOCUS_NONE
		b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		var id: String = t[1]
		b.pressed.connect(func():
			UiSound.move()
			if id == tab and not folded:
				set_folded(true)
			else:
				set_tab(id))
		head.add_child(b)
		_tabs[id] = b
	_fold = Button.new()
	_fold.theme_type_variation = "SmallButton"
	_fold.focus_mode = Control.FOCUS_NONE
	_fold.custom_minimum_size.x = 34
	_fold.pressed.connect(func():
		UiSound.move()
		set_folded(not folded))
	head.add_child(_fold)
	_body = VBoxContainer.new()
	_body.size_flags_vertical = Control.SIZE_EXPAND_FILL
	box.add_child(_body)
	outliner = Q3EditorOutliner.new()
	outliner.editor = editor
	outliner.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_body.add_child(outliner)
	history = Q3EditorHistory.new()
	history.editor = editor
	history.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_body.add_child(history)
	set_tab(tab)
	set_folded(folded)


func set_tab(id: String) -> void:
	tab = id
	outliner.visible = id == "objects"
	history.visible = id == "history"
	if folded:
		set_folded(false)
	_show()
	changed.emit()


func set_folded(on: bool) -> void:
	if on and not folded and custom_minimum_size.y > 0:
		height = custom_minimum_size.y
	folded = on
	_body.visible = not on
	custom_minimum_size.y = 0.0 if on else height
	_show()
	changed.emit()


func _show() -> void:
	for k in _tabs:
		_tabs[k].set_pressed_no_signal(k == tab and not folded)
	_fold.text = "▸" if folded else "▾"
	_fold.tooltip_text = "Show the %s panel" % tab if folded else "Fold away (the inspector gets the room)"
