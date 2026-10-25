class_name Q3EditorInspector
extends PanelContainer
## The map editor's inspector (OZMIUM's Inspector): what's selected, in sections that fold (click a
## heading; the choice holds for the session): its material and tint, the face mapping (shift,
## rotate, scale, alignment, texture lock; folded unless faces are picked), the objects' position and
## rotation, and for entities their class, typed fields for the class's keys (a target picked from
## the map's names, numbers with id's defaults, the spawnflags by name, a light's color, a sound) and
## every raw key/value (folded). With nothing selected it edits the map's settings (worldspawn).
## Sections are rebuilt when the selection changes; values refresh after every edit.

var editor: Q3MapEditor

var _root: VBoxContainer        ## the scroll's column
var _box: VBoxContainer         ## the section being built (the column itself before the first)
var _shape := ""                ## which sections are built (rebuild when it changes)
var _fields := {}               ## name -> control
var _busy := false              ## refreshing values: changes aren't edits
var _player: AudioStreamPlayer  ## the sound field's preview
var _queued := false            ## a refresh is waiting for the end of the frame
var _defaults := {}             ## num:<key> -> the value shown while the key is absent
static var _folded := {}        ## section -> folded, as the player left it this session


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_STOP
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	add_child(scroll)
	_root = VBoxContainer.new()
	_root.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_root.add_theme_constant_override("separation", 6)
	scroll.add_child(_root)
	_box = _root
	editor.selection_changed.connect(_queue_refresh)
	editor.status_changed.connect(_queue_refresh)
	_refresh()


func _shape_now() -> String:
	var t := editor.paint_targets()
	var ents := ""
	for id in editor.entity_targets():
		var e := editor.doc.get_obj(id)
		var keys: Array = e["keys"].keys()
		keys.sort()
		ents += "%d:%s:%s;" % [id, e["classname"], ",".join(keys)]
	if editor.selection.is_empty() and editor.faces.is_empty():
		var wk: Array = editor.doc.worldspawn.keys()
		wk.sort()
		ents = "world:" + ",".join(wk)
	return "%d|%d|%d|%s|%s" % [editor.selection.size(), t[0].size(), t[1].size(), editor.faces.size() > 0, ents]


## Edits signal once per stored object (a placed prefab, a CSG cut), so the inspector refreshes once,
## at the end of the frame.
func _queue_refresh() -> void:
	if not _queued:
		_queued = true
		_refresh.call_deferred()


func _refresh() -> void:
	_queued = false
	if not is_inside_tree():
		return
	var shape := _shape_now()
	if shape != _shape:
		_shape = shape
		_build()
	_values()


# ---------------------------------------------------------------- building

func _build() -> void:
	for c in _root.get_children():
		c.queue_free()
	_fields.clear()
	_defaults.clear()
	_box = _root
	var t := editor.paint_targets()
	var face_keys: Array = t[0]
	var patches: Array = t[1]
	_fields["title"] = _label(_box, "", "TitleLabel")
	_fields["sub"] = _label(_box, "", "DimLabel")
	if face_keys.is_empty() and patches.is_empty() and editor.selection.is_empty():
		_fields["sub"].text = "Select brushes, curves, entities or faces (T) to edit them. Pick a material on the left to make it the current one."
		_build_world()
		return
	var ent_ids := editor.entity_targets()
	if ent_ids.size():
		_build_entity(ent_ids)
	if face_keys.size() or patches.size():
		_build_material(face_keys)
	if editor.selection.size():
		_build_transform()


## A section: a heading that folds it, and its body, which the next fields go into. folded: how it
## starts until the player folds or opens it.
func _group(text: String, folded := false) -> void:
	var head := Button.new()
	head.flat = true
	head.focus_mode = Control.FOCUS_NONE
	head.alignment = HORIZONTAL_ALIGNMENT_LEFT
	head.add_theme_font_override("font", Q3Fonts.prop())
	head.add_theme_font_size_override("font_size", 15)
	for c in ["font_color", "font_hover_color", "font_pressed_color", "font_hover_pressed_color"]:
		head.add_theme_color_override(c, Q3Theme.RED.lightened(0.25 if c == "font_color" else 0.45))
	_root.add_child(head)
	var body := VBoxContainer.new()
	body.add_theme_constant_override("separation", 6)
	_root.add_child(body)
	var key := text.trim_prefix("%d " % text.to_int())   # "3 ENTITIES" folds with "ENTITY"
	var shut: bool = _folded.get(key, folded)
	body.visible = not shut
	head.text = ("▸  " if shut else "▾  ") + text
	head.pressed.connect(func():
		UiSound.move()
		body.visible = not body.visible
		_folded[key] = not body.visible
		head.text = ("▾  " if body.visible else "▸  ") + text)
	_box = body


func _build_material(face_keys: Array) -> void:
	_group("MATERIAL")
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 10)
	_box.add_child(row)
	var thumb := TextureRect.new()
	thumb.custom_minimum_size = Vector2(56, 56)
	thumb.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	thumb.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
	row.add_child(thumb)
	_fields["thumb"] = thumb
	var mat := _label(row, "", "")
	mat.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	mat.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_fields["material"] = mat
	var buttons := HBoxContainer.new()
	_box.add_child(buttons)
	_button(buttons, "Apply current", "Paint the current material (the swatch at the bottom of the tool strip) onto the selection",
			func(): editor.apply_material(editor.material))
	_button(buttons, "Make current", "Make the selection's material the current one, for new brushes and Paint (Alt+click a face does it too)", func():
		var m := _material_of(editor.paint_targets())
		if m != "" and m != "mixed":
			editor.material = m
			editor.status_changed.emit())
	var trow := HBoxContainer.new()
	_box.add_child(trow)
	_cell(trow, "Tint")
	var picker := ColorPickerButton.new()
	picker.custom_minimum_size = Vector2(90, 30)
	picker.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	picker.edit_alpha = false
	picker.tooltip_text = "Multiplies the material's color"
	# applied when the picker closes: one undo step, not one per drag of the wheel
	picker.popup_closed.connect(func(): editor.set_tint(picker.color))
	trow.add_child(picker)
	_fields["tint"] = picker
	var clear := _button(trow, "Clear", "Remove the tint", func(): editor.set_tint(Color.WHITE))
	clear.size_flags_horizontal = Control.SIZE_FILL
	# face mapping: open when faces are picked, folded for whole objects
	if face_keys.is_empty():
		return
	_group("TEXTURE MAPPING", editor.faces.is_empty())
	var grid := GridContainer.new()
	grid.columns = 2
	_box.add_child(grid)
	var specs := [["Shift S", 0, -4096, 4096, 1.0], ["Shift T", 1, -4096, 4096, 1.0], ["Rotate", 2, -360, 360, 1.0],
			["Scale S", 3, -16, 16, 0.05], ["Scale T", 4, -16, 16, 0.05]]
	for s in specs:
		_cell(grid, s[0])
		var sb := SpinBox.new()
		sb.min_value = s[2]
		sb.max_value = s[3]
		sb.step = s[4]
		sb.custom_arrow_step = s[4] * (8.0 if s[1] < 2 else (15.0 if s[1] == 2 else 2.0))
		sb.allow_greater = true
		sb.allow_lesser = true
		sb.select_all_on_focus = true
		sb.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		var idx: int = s[1]
		sb.value_changed.connect(func(v):
			if not _busy:
				editor.edit_faces("field", idx, v))
		grid.add_child(sb)
		_fields["tex%d" % idx] = sb
	_fields["explicit"] = _label(_box, "Explicit mapping (texture lock or an import): use the arrows, the alignment buttons or Reset.", "DimLabel")
	var align := GridContainer.new()
	align.columns = 3
	_box.add_child(align)
	for a in [["Fit", "fit"], ["Left", "left"], ["Right", "right"], ["Top", "top"], ["Bottom", "bottom"], ["Center", "center"],
			["Flip U", "flipU"], ["Flip V", "flipV"], ["Reset", ""]]:
		var how: String = a[1]
		_button(align, a[0], "Reset to Quake 3's world projection" if how == "" else "Align: " + a[0].to_lower(), func():
			if how == "":
				editor.edit_faces("reset")
			else:
				editor.edit_faces("align", how))
	var lock := CheckBox.new()
	lock.text = "Texture lock (Ctrl+T)"
	lock.tooltip_text = "A locked texture moves with its brush"
	lock.focus_mode = Control.FOCUS_NONE
	lock.toggled.connect(func(on):
		if not _busy:
			editor.set_lock(on))
	_box.add_child(lock)
	_fields["lock"] = lock


func _build_transform() -> void:
	_group("TRANSFORM")
	var pg := GridContainer.new()
	pg.columns = 2
	_box.add_child(pg)
	for i in 3:
		_cell(pg, ["X", "Y", "Z"][i])
		var sb := SpinBox.new()
		sb.min_value = -65536
		sb.max_value = 65536
		sb.step = 1
		sb.select_all_on_focus = true
		sb.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		sb.tooltip_text = "The selection's lowest corner on %s (units)" % ["X", "Y", "Z"][i]
		var axis := i
		sb.value_changed.connect(func(v):
			if not _busy:
				var d := Vector3.ZERO
				d[axis] = v - editor.selection_bounds().position[axis]
				editor.move_selection(d))
		pg.add_child(sb)
		_fields["pos%d" % i] = sb
	_fields["size"] = _label(_box, "", "DimLabel")
	var rrow := HBoxContainer.new()
	_box.add_child(rrow)
	_cell(rrow, "Turn")
	var ang := SpinBox.new()
	ang.min_value = -360
	ang.max_value = 360
	ang.step = 1
	ang.value = 45
	ang.suffix = "°"
	ang.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	rrow.add_child(ang)
	var turn := _button(rrow, "Rotate", "Turn the selection about the vertical (R does 90°, Ctrl+R 15°)", func(): editor.rotate_selection(ang.value))
	turn.size_flags_horizontal = Control.SIZE_FILL


# ---------------------------------------------------------------- entities (E4)

func _build_entity(ids: Array) -> void:
	var e := editor.doc.get_obj(ids[0])
	var cn: String = e["classname"]
	_group("ENTITY" if ids.size() == 1 else "%d ENTITIES" % ids.size())
	var head := _label(_box, cn, "")
	head.add_theme_font_size_override("font_size", 17)
	head.add_theme_color_override("font_color", Q3MapEntities.color(cn).lightened(0.35))
	_label(_box, Q3MapEntities.description(cn), "DimLabel")
	# the class, where it has relatives to switch between
	var family := PackedStringArray()
	if Q3MapEntities.is_item(cn):
		family = Q3MapEntities.weapons() if cn in Q3MapEntities.weapons() else Q3MapEntities.items()
	elif Q3MapEntities.category(cn) == "spawn":
		for c in Q3.map_entities:
			if Q3MapEntities.category(c) == "spawn":
				family.append(c)
	if family.size() > 1:
		var ob := OptionButton.new()
		ob.focus_mode = Control.FOCUS_NONE
		for c in family:
			ob.add_item(Q3MapEntities.description(c) if Q3MapEntities.is_item(c) else c)
		ob.select(family.find(cn))
		ob.item_selected.connect(func(i): editor.set_keys(ids, {"classname": family[i]}, "Change class"))
		_box.add_child(ob)
	var grid := GridContainer.new()
	grid.columns = 2
	_box.add_child(grid)
	if Q3MapEntities.faces(cn):
		_spin(grid, "Angle", "angle", 0, 360, 1, 15, "°", func(v): _turn(ids, fposmod(v, 360.0)))
	match cn:
		"light":
			_spin(grid, "Intensity", "light", 0, 100000, 5, 25, "", func(v): editor.set_keys(ids, {"light": Q3MapEditor._num(v)}, "Light intensity"))
			_cell(grid, "Color")
			var picker := ColorPickerButton.new()
			picker.custom_minimum_size = Vector2(120, 30)
			picker.edit_alpha = false
			picker.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			picker.popup_closed.connect(func():
				var c := picker.color
				editor.set_keys(ids, {"_color": "%s %s %s" % [String.num(c.r, 3), String.num(c.g, 3), String.num(c.b, 3)]}, "Light color"))
			grid.add_child(picker)
			_fields["light_color"] = picker
			if e["keys"].has("target"):
				_spin(grid, "Spot radius", "radius", 1, 4096, 1, 8, "", func(v): editor.set_keys(ids, {"radius": Q3MapEditor._num(v)}, "Spot radius"))
		"target_speaker":
			var sounds := Q3MapEntities.sounds()
			_cell(grid, "Sound")
			var srow := HBoxContainer.new()
			srow.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			grid.add_child(srow)
			var ob := OptionButton.new()
			ob.focus_mode = Control.FOCUS_NONE
			ob.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			ob.fit_to_longest_item = false
			for snd in sounds:
				ob.add_item(String(snd).trim_prefix("sound/world/"))
			ob.item_selected.connect(func(i): editor.set_keys(ids, {"noise": sounds[i]}, "Sound"))
			srow.add_child(ob)
			_fields["noise"] = ob
			var listen := _button(srow, "▶", "Listen", func(): _listen(String(editor.doc.get_obj(ids[0])["keys"].get("noise", ""))))
			listen.size_flags_horizontal = Control.SIZE_FILL
		"trigger_push", "target_push":
			_fields["flight"] = _label(_box, "", "DimLabel")
	# the class's other keys, typed
	for spec in Q3MapEntities.key_specs(cn):
		_typed(grid, ids, spec)
	var flags := Q3MapEntities.flags(cn)
	if flags.size():
		var fg := GridContainer.new()
		fg.columns = 2
		_box.add_child(fg)
		for f in flags:
			var bit: int = f[0]
			var cb := CheckBox.new()
			cb.text = f[1]
			cb.focus_mode = Control.FOCUS_NONE
			cb.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			cb.tooltip_text = "spawnflags %d" % bit
			cb.toggled.connect(func(on):
				if not _busy:
					var fl := int(editor.doc.get_obj(ids[0])["keys"].get("spawnflags", "0"))
					fl = (fl | bit) if on else (fl & ~bit)
					editor.set_keys(ids, {"spawnflags": str(fl) if fl != 0 else ""}, cb.text))
			fg.add_child(cb)
			_fields["flag:%d" % bit] = cb
	if e["keys"].has("target") or e["keys"].has("targetname"):
		_fields["links"] = _label(_box, "", "DimLabel")
	# every key/value, as they're stored
	_group("ALL KEYS", true)
	_kv_rows(func(): return editor.doc.get_obj(ids[0]).get("keys", {}), func(ch: Dictionary): editor.set_keys(ids, ch))


## One typed field from Q3MapEntities.KEY_SPECS: [key, kind, default].
func _typed(grid: GridContainer, ids: Array, spec: Array) -> void:
	var key: String = spec[0]
	var label := key.capitalize()
	match spec[1]:
		"num":
			var sb := _spin(grid, label, key, -100000, 100000, 0.1, 1, "", func(v): editor.set_keys(ids, {key: Q3MapEditor._num(v)}, label))
			sb.allow_lesser = true
			sb.tooltip_text = "%s (id's default %s)" % [key, Q3MapEditor._num(spec[2])]
			_defaults["num:" + key] = float(spec[2])
		"text", "name":
			_cell(grid, label)
			var le := LineEdit.new()
			le.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			le.placeholder_text = "a name others target" if spec[1] == "name" else ""
			_commit_on(le, func(t): editor.set_keys(ids, {key: t.strip_edges()}, label))
			grid.add_child(le)
			_fields["txt:" + key] = le
		"target":
			_cell(grid, label)
			var ob := OptionButton.new()
			ob.focus_mode = Control.FOCUS_NONE
			ob.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			ob.fit_to_longest_item = false
			ob.tooltip_text = "The entity this one fires or aims at: one with that targetname"
			ob.item_selected.connect(func(i):
				if not _busy:
					var n := ob.get_item_text(i)
					editor.set_keys(ids, {key: "" if i == 0 else n.get_slice("  ", 0)}, "Target"))
			grid.add_child(ob)
			_fields["target:" + key] = ob
		"bool":
			_cell(grid, "")
			var cb := CheckBox.new()
			cb.text = {"notfree": "Not in FFA / duel", "notteam": "Not in team games", "notsingle": "Not in single player",
					"nobots": "No bots spawn here", "nohumans": "No humans spawn here"}.get(key, label)
			cb.focus_mode = Control.FOCUS_NONE
			cb.toggled.connect(func(on):
				if not _busy:
					editor.set_keys(ids, {key: "1" if on else ""}, cb.text))
			grid.add_child(cb)
			_fields["bool:" + key] = cb


## Map settings: worldspawn's friendly fields and every key.
func _build_world() -> void:
	_group("MAP SETTINGS")
	var grid := GridContainer.new()
	grid.columns = 2
	_box.add_child(grid)
	_cell(grid, "Message")
	var msg := LineEdit.new()
	msg.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	msg.placeholder_text = "the map's title"
	_commit_on(msg, func(t): editor.set_world({"message": t}))
	grid.add_child(msg)
	_fields["world:message"] = msg   # its own name: the KEYS rows below use kv:message
	_cell(grid, "Music")
	var tracks := _music()
	var ob := OptionButton.new()
	ob.focus_mode = Control.FOCUS_NONE
	ob.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	ob.add_item("(none)")
	for tr in tracks:
		ob.add_item(String(tr).get_slice(" ", 0).get_file().get_basename().trim_suffix("_intro"))
	ob.item_selected.connect(func(i): editor.set_world({"music": "" if i == 0 else tracks[i - 1]}))
	grid.add_child(ob)
	_fields["music"] = ob
	_fields["music_tracks"] = tracks
	_spin(grid, "Gravity", "gravity", 0, 5000, 1, 50, "", func(v): editor.set_world({"gravity": Q3MapEditor._num(v)}))
	_spin(grid, "Ambient", "ambient", 0, 255, 1, 5, "", func(v): editor.set_world({"ambient": Q3MapEditor._num(v)}))
	_group("ALL KEYS", true)
	_kv_rows(func(): return editor.doc.worldspawn, func(ch: Dictionary): editor.set_world(ch))


## Quake 3's soundtrack as worldspawn wants it: "intro loop" pairs, or one track.
static func _music() -> PackedStringArray:
	var out := PackedStringArray()
	var d := DirAccess.open("res://library/music")
	if d == null:
		return out
	var names := {}
	for f in d.get_files():
		var n := f.trim_suffix(".import").trim_suffix(".remap")
		if n.get_extension() == "ogg":
			names[n.get_basename()] = true
	var keys := names.keys()
	keys.sort()
	for n in keys:
		if n.ends_with("_loop"):
			continue
		if n.ends_with("_intro") and names.has(n.trim_suffix("_intro") + "_loop"):
			out.append("music/%s.wav music/%s_loop.wav" % [n, n.trim_suffix("_intro")])
		else:
			out.append("music/%s.wav" % n)
	return out


## A labeled SpinBox for a key's number; the value is set on change (not while refreshing).
func _spin(grid: Control, label: String, key: String, lo: float, hi: float, step: float, arrow: float, suffix: String, apply: Callable) -> SpinBox:
	_cell(grid, label)
	var sb := SpinBox.new()
	sb.min_value = lo
	sb.max_value = hi
	sb.step = step
	sb.custom_arrow_step = arrow
	sb.allow_greater = true
	sb.suffix = suffix
	sb.select_all_on_focus = true
	sb.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	sb.value_changed.connect(func(v):
		if not _busy:
			apply.call(v))
	grid.add_child(sb)
	_fields["num:" + key] = sb
	return sb


## Key/value rows: each value edits in place, x removes the key, and the last row adds one.
## get_keys() -> Dictionary; apply(changes) sets ("" removes).
func _kv_rows(get_keys: Callable, apply: Callable) -> void:
	var grid := GridContainer.new()
	grid.columns = 3
	_box.add_child(grid)
	var keys: Array = (get_keys.call() as Dictionary).keys()
	keys.sort()
	for k in keys:
		if k == "classname":
			continue
		var l := _cell(grid, k)
		l.clip_text = true
		l.custom_minimum_size.x = 96
		l.tooltip_text = k
		l.mouse_filter = Control.MOUSE_FILTER_PASS
		var v := LineEdit.new()
		v.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		var key: String = k
		_commit_on(v, func(t): apply.call({key: t}))
		grid.add_child(v)
		_fields["kv:" + key] = v
		var x := Button.new()
		x.text = "✕"
		x.tooltip_text = "Remove " + key
		x.theme_type_variation = "SmallButton"
		x.focus_mode = Control.FOCUS_NONE
		x.pressed.connect(func():
			UiSound.move()
			apply.call({key: ""}))
		grid.add_child(x)
	var nk := LineEdit.new()
	nk.placeholder_text = "key"
	nk.custom_minimum_size.x = 96
	grid.add_child(nk)
	var nv := LineEdit.new()
	nv.placeholder_text = "value"
	nv.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	grid.add_child(nv)
	var add := func():
		var k := nk.text.strip_edges()
		if k == "" or nv.text == "":
			UiSound.buzz()
			return
		apply.call({k: nv.text})
	nv.text_submitted.connect(func(_t): add.call())
	var b := Button.new()
	b.text = "ADD"
	b.theme_type_variation = "SmallButton"
	b.focus_mode = Control.FOCUS_NONE
	b.pressed.connect(func():
		UiSound.move()
		add.call())
	grid.add_child(b)


## A LineEdit that applies its text on Enter or when it loses focus, if it changed.
func _commit_on(le: LineEdit, apply: Callable) -> void:
	var go := func():
		if not _busy and le.text != String(le.get_meta("shown", "")):
			le.set_meta("shown", le.text)
			apply.call(le.text)
	le.text_submitted.connect(func(_t):
		go.call()
		le.release_focus())
	le.focus_exited.connect(go)


## The Angle field: "angles" keeps its pitch and roll, else "angle" is set.
func _turn(ids: Array, yaw: float) -> void:
	var e := editor.doc.get_obj(ids[0])
	if e["keys"].has("angles"):
		var v := Q3MapEntities.vec(String(e["keys"]["angles"]))
		editor.set_keys(ids, {"angles": Q3MapEntities.vec_key(Vector3(v.x, yaw, v.z))}, "Turn")
	else:
		editor.set_keys(ids, {"angle": Q3MapEditor._num(yaw)}, "Turn")


func _listen(noise: String) -> void:
	var stream := Q3MapEntities.sound_stream(noise)
	if stream == null:
		editor.notify("No sound " + noise, true)
		return
	if _player == null:
		_player = AudioStreamPlayer.new()
		add_child(_player)
	_player.stream = stream
	_player.play()


func _label(parent: Control, text: String, variation: String) -> Label:
	var l := Label.new()
	l.text = text
	if variation != "":
		l.theme_type_variation = variation
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	parent.add_child(l)
	return l


## A grid's name column: one line, a fixed width (a wrapping label there shrinks to a letter wide).
func _cell(parent: Control, text: String) -> Label:
	var l := Label.new()
	l.text = text
	l.theme_type_variation = "DimLabel"
	l.custom_minimum_size.x = 80
	parent.add_child(l)
	return l


func _button(parent: Control, text: String, tip: String, cb: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.tooltip_text = tip
	b.theme_type_variation = "SmallButton"
	b.focus_mode = Control.FOCUS_NONE
	b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	b.pressed.connect(func():
		UiSound.move()
		cb.call())
	parent.add_child(b)
	return b


# ---------------------------------------------------------------- values

## The material all targets share, "mixed", or "".
func _material_of(t: Array) -> String:
	var m := ""
	for k: Vector2i in t[0]:
		var f: Dictionary = editor.doc.get_obj(k.x)["faces"][k.y]
		if m == "":
			m = f["material"]
		elif m != f["material"]:
			return "mixed"
	for id in t[1]:
		var p := editor.doc.get_obj(id)
		if m == "":
			m = p["material"]
		elif m != p["material"]:
			return "mixed"
	return m


func _values() -> void:
	_busy = true
	var t := editor.paint_targets()
	var nb := 0
	var np := 0
	var ne := 0
	for id in editor.selection:
		match editor.doc.get_obj(id).get("type", ""):
			"brush": nb += 1
			"patch": np += 1
			"entity": ne += 1
	if _fields.has("title"):
		if editor.faces.size():
			_fields["title"].text = "%d FACE%s" % [editor.faces.size(), "" if editor.faces.size() == 1 else "S"]
		elif editor.selection.size():
			var parts := []
			for x in [[nb, "brush", "brushes"], [np, "curve", "curves"], [ne, "entity", "entities"]]:
				if x[0] > 0:
					parts.append("%d %s" % [x[0], x[1] if x[0] == 1 else x[2]])
			_fields["title"].text = " · ".join(parts).to_upper()
		else:
			_fields["title"].text = "INSPECTOR"
	if _fields.has("material"):
		var m := _material_of(t)
		_fields["material"].text = Q3MapMaterials.short_name(m) if m != "" else "-"
		_fields["thumb"].texture = Q3MapMaterials.thumbnail(m) if m != "" and m != "mixed" else null
	if _fields.has("tint"):
		var c := Color.WHITE
		if t[0].size():
			c = editor.doc.get_obj(t[0][0].x)["faces"][t[0][0].y].get("color", Color.WHITE)
		elif t[1].size():
			c = editor.doc.get_obj(t[1][0]).get("color", Color.WHITE)
		_set_color(_fields["tint"], c)
	if _fields.has("tex0") and t[0].size():
		var first: Dictionary = editor.doc.get_obj(t[0][0].x)["faces"][t[0][0].y]
		var all_tex: bool = t[0].all(func(k): return editor.doc.get_obj(k.x)["faces"][k.y].has("tex"))
		var tex: Array = first.get("tex", Q3MapDoc.DEFAULT_TEX)
		for i in 5:
			var sb: SpinBox = _fields["tex%d" % i]
			sb.editable = all_tex
			if not sb.get_line_edit().has_focus():
				sb.set_value_no_signal(float(tex[i]))
		_fields["explicit"].visible = not all_tex
		(_fields["lock"] as CheckBox).set_pressed_no_signal(t[0].all(func(k): return editor.doc.get_obj(k.x)["faces"][k.y].get("lock", false)))
	if _fields.has("pos0") and editor.selection.size():
		var bx := editor.selection_bounds()
		for i in 3:
			var sb: SpinBox = _fields["pos%d" % i]
			sb.custom_arrow_step = editor.grid.units()
			if not sb.get_line_edit().has_focus():
				sb.set_value_no_signal(bx.position[i])
		_fields["size"].text = "Size %s × %s × %s" % [Q3MapEditor._num(bx.size.x), Q3MapEditor._num(bx.size.y), Q3MapEditor._num(bx.size.z)]
	_entity_values()
	_busy = false


const NUMBER_DEFAULTS := {"light": 300.0, "gravity": 800.0, "ambient": 0.0, "angle": 0.0, "radius": 64.0}


## The entity and map settings fields, from the first target entity's keys (or worldspawn's).
func _entity_values() -> void:
	var ids := editor.entity_targets()
	var world := editor.selection.is_empty() and editor.faces.is_empty()
	if ids.is_empty() and not world:
		return
	var e := {} if world else editor.doc.get_obj(ids[0])
	var keys: Dictionary = editor.doc.worldspawn if world else e["keys"]
	for f in _fields:
		if not _fields[f] is Control:
			continue
		var c: Control = _fields[f]
		if c.has_focus() or (c is SpinBox and (c as SpinBox).get_line_edit().has_focus()):
			continue
		var k: String = f.substr(f.find(":") + 1)
		if f.begins_with("kv:") or f.begins_with("world:") or f.begins_with("txt:"):
			var v := String(keys.get(k, ""))
			(c as LineEdit).text = v
			c.set_meta("shown", v)
		elif f.begins_with("num:"):
			var v: float = float(keys[k]) if keys.has(k) else _defaults.get(f, NUMBER_DEFAULTS.get(k, 0.0))
			if k == "angle" and not world:
				v = Q3MapEntities.angle(e)
			(c as SpinBox).set_value_no_signal(v)
			# a key left at id's default reads dim
			c.modulate = Color(1, 1, 1, 1.0 if keys.has(k) or k == "angle" else 0.6)
		elif f.begins_with("bool:"):
			(c as CheckBox).set_pressed_no_signal(String(keys.get(k, "0")) not in ["", "0"])
		elif f.begins_with("flag:"):
			(c as CheckBox).set_pressed_no_signal(int(keys.get("spawnflags", "0")) & int(k) != 0)
		elif f.begins_with("target:"):
			_fill_targets(c as OptionButton, String(keys.get(k, "")), String(keys.get("targetname", "")))
	if _fields.has("light_color"):
		_set_color(_fields["light_color"], Q3MapEntities.light_params(keys)["color"])
	if _fields.has("noise"):
		(_fields["noise"] as OptionButton).select(Q3MapEntities.sounds().find(String(keys.get("noise", ""))))
	if _fields.has("music"):
		var tracks: PackedStringArray = _fields["music_tracks"]
		(_fields["music"] as OptionButton).select(tracks.find(String(keys.get("music", ""))) + 1)
	if _fields.has("flight"):
		_fields["flight"].text = _flight(e)
	if _fields.has("links"):
		var names := Q3MapEntities.targetnames(editor.doc)
		var parts := []
		if keys.has("target"):
			var n: int = names.get(String(keys["target"]), []).size()
			parts.append("Targets '%s': %s" % [keys["target"], "nothing yet" if n == 0 else "%d entit%s" % [n, "y" if n == 1 else "ies"]])
		if keys.has("targetname"):
			var by := 0
			for id in editor.doc.objects:
				var o: Dictionary = editor.doc.objects[id]
				if o["type"] == "entity" and String(o["keys"].get("target", "")) == String(keys["targetname"]):
					by += 1
			parts.append("Named '%s': targeted by %d" % [keys["targetname"], by])
		_fields["links"].text = "\n".join(parts)


## The target menu: (none), then every targetname in the map but this entity's own (the current one
## marked when nothing has it yet).
func _fill_targets(ob: OptionButton, current: String, own: String) -> void:
	var names: Array = Q3MapEntities.targetnames(editor.doc).keys()
	names.erase(own)
	names.sort()
	ob.clear()
	ob.add_item("(none)")
	var at := 0
	for n in names:
		ob.add_item(n)
		if n == current:
			at = ob.item_count - 1
	if current != "" and at == 0:
		ob.add_item(current + "  (nothing has this name)")
		at = ob.item_count - 1
	ob.select(at)


## A jump pad's flight from AimAtTarget: launch speed, how high, how long to the top, how far.
func _flight(e: Dictionary) -> String:
	var t := Q3MapEntities.targets_of(editor.doc, e)
	if t.is_empty():
		return "No target: give it a target_position to aim at (the top of the arc)."
	var from := Q3MapEntities.center(editor.doc, e)
	var to := Q3MapEntities.center(editor.doc, editor.doc.get_obj(t[0]))
	var g := Q3MapEntities.gravity(editor.doc)
	var v := Q3MapEntities.push_velocity(from, to, g)
	if v == Vector3.ZERO:
		return "The target is below the pad, so Quake 3 removes this jump pad."
	var flat := Vector2(to.x - from.x, to.y - from.y).length()
	return "Launch %d u/s (%.1f m/s), %d units up, %.2f s to the top, %d units out" % [
			int(v.length()), v.length() / 32.0, int(to.z - from.z), v.z / g, int(flat)]


func _set_color(p: ColorPickerButton, c: Color) -> void:
	if p.color != c:
		p.color = c
