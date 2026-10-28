extends Control
## The setup screen the splash shows until the player's own Quake III Arena is verified
## (Q3Ownership): drag pak0.pk3 (or its baseq3 folder) onto the window, or browse to it. Without a
## retail pak0 the game goes no further; Quit is the only other way out. Emits `verified` once it's
## accepted.

signal verified

var _status: Label
var _drop: PanelContainer
var _drop_style: StyleBoxFlat
var _dialog: FileDialog
var _done := false


func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_build()
	get_window().files_dropped.connect(_on_files)
	UiSound.play_in()


func _exit_tree() -> void:
	if get_window().files_dropped.is_connected(_on_files):
		get_window().files_dropped.disconnect(_on_files)


func _build() -> void:
	var center := CenterContainer.new()
	center.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(center)
	var box := VBoxContainer.new()
	box.custom_minimum_size = Vector2(900, 0)
	box.add_theme_constant_override("separation", 18)
	center.add_child(box)

	var head := Label.new()
	head.text = "QUAKE III ARENA REQUIRED"
	head.add_theme_font_override("font", Q3Theme.banner_font(6))
	head.add_theme_font_size_override("font_size", 34)
	head.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	box.add_child(head)
	var rule := ColorRect.new()
	rule.color = Color(Q3Theme.RED, 0.7)
	rule.custom_minimum_size = Vector2(0, 2)
	box.add_child(rule)
	box.add_child(_text("Oz Arena runs on your own copy of Quake III Arena. Drag pak0.pk3 from the game's baseq3 folder onto this window.", 24, Q3Theme.TEXT))

	_drop = PanelContainer.new()
	_drop_style = StyleBoxFlat.new()
	_drop_style.bg_color = Color(0.06, 0.03, 0.02, 0.85)
	_drop_style.border_color = Color(Q3Theme.RED, 0.8)
	_drop_style.set_border_width_all(2)
	_drop_style.set_corner_radius_all(4)
	_drop_style.content_margin_top = 34
	_drop_style.content_margin_bottom = 34
	_drop.add_theme_stylebox_override("panel", _drop_style)
	_drop.custom_minimum_size = Vector2(0, 150)
	var inner := VBoxContainer.new()
	inner.alignment = BoxContainer.ALIGNMENT_CENTER
	inner.add_theme_constant_override("separation", 8)
	# the theme's text font: Quake 3's prop font has no digits or period, so it would read "PAKO-PK3"
	var big := _text("Drop pak0.pk3 here", 34, Q3Theme.TEXT_HOT)
	inner.add_child(big)
	inner.add_child(_text("or the baseq3 folder, or the whole Quake III Arena folder", 18, Q3Theme.DIM))
	_drop.add_child(inner)
	box.add_child(_drop)

	var found := Q3Ownership.find_install()
	var hint := ("Found an install at %s." % found.replace("/", "\\")) if found != "" \
		else "On Steam it's in Steam\\steamapps\\common\\Quake 3 Arena\\baseq3."
	box.add_child(_text(hint, 18, Q3Theme.DIM))

	var row := HBoxContainer.new()
	row.alignment = BoxContainer.ALIGNMENT_CENTER
	row.add_theme_constant_override("separation", 16)
	if found != "":
		row.add_child(_button("OPEN THAT FOLDER", func(): OS.shell_open(found)))
	row.add_child(_button("BROWSE...", _browse))
	row.add_child(_button("QUIT", func(): UiSound.out(); get_tree().quit()))
	box.add_child(row)

	_status = _text("", 22, Q3Theme.AMBER)
	_status.custom_minimum_size = Vector2(0, 60)
	box.add_child(_status)
	box.add_child(_text("Nothing is copied or uploaded. The game remembers where the file is and checks it each time it starts.", 16, Q3Theme.DIM))


func _text(s: String, size: int, color: Color) -> Label:
	var l := Label.new()
	l.text = s
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	l.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_color", color)
	return l


func _button(label: String, action: Callable) -> Button:
	var b := Button.new()
	b.text = label
	b.custom_minimum_size = Vector2(200, 46)
	b.add_theme_font_override("font", Q3Fonts.prop())
	b.add_theme_font_size_override("font_size", 18)
	b.mouse_entered.connect(UiSound.move)
	b.pressed.connect(func(): UiSound.move(); action.call())
	return b


func _browse() -> void:
	if _dialog == null:
		_dialog = FileDialog.new()
		_dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILE
		_dialog.access = FileDialog.ACCESS_FILESYSTEM
		_dialog.use_native_dialog = true
		_dialog.title = "Find pak0.pk3 in Quake III Arena's baseq3 folder"
		_dialog.filters = PackedStringArray(["pak0.pk3 ; Quake III Arena pak0", "*.pk3 ; Quake III Arena paks"])
		var found := Q3Ownership.find_install()
		if found != "":
			_dialog.current_dir = found
		_dialog.file_selected.connect(func(p): _on_files(PackedStringArray([p])))
		add_child(_dialog)
	_dialog.popup_centered_ratio(0.6)


func _on_files(files: PackedStringArray) -> void:
	if _done or files.is_empty():
		return
	_status.add_theme_color_override("font_color", Q3Theme.DIM)
	_status.text = "Checking..."
	await get_tree().process_frame
	var r := Q3Ownership.accept(files[0])
	_status.text = String(r["message"])
	if not r["ok"]:
		UiSound.buzz()
		_status.add_theme_color_override("font_color", Q3Theme.GLOW)
		_flash(Q3Theme.GLOW)
		return
	_done = true
	UiSound.move()
	_status.add_theme_color_override("font_color", Q3Theme.AMBER)
	_drop_style.border_color = Q3Theme.AMBER
	get_tree().create_timer(1.6 if int(r["paks"]) < Q3Ownership.PAK_CHECKSUMS.size() else 0.9).timeout.connect(verified.emit)


func _flash(c: Color) -> void:
	_drop_style.border_color = c
	var tw := create_tween()
	tw.tween_property(_drop_style, "border_color", Color(Q3Theme.RED, 0.8), 0.6)
