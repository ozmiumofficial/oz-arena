class_name Q3Console
extends CanvasLayer
## The developer console: Quake 3's drop-down, over every scene (Diagnostics makes it). The key
## is Settings > Controls > Console (` by default); with Shift it opens full height, Escape closes.
## Scrollback holds the console's own lines, the game engine's output (Com_Printf, through a
## Logger that picks up the "q3core: " lines) and what the game prints to you and the chat.
## Up and Down walk the history (kept in user://console_history.txt), Tab completes, PageUp and
## PageDown scroll, Ctrl+L clears. Commands are Q3ConsoleCommands' (with Q3ConsoleClient's: every
## command Quake 3 had); this node pumps its buffer every frame (wait), runs autoexec.cfg once, and
## in a match runs the console binds when their keys go down (and their "-" halves when they come up).
## While it's open the match carries on, but your usercmd only holds BUTTON_TALK (cl_input.c does
## the same with any key catcher up) and Gameplay ignores keys and the mouse (open_now).

signal toggled(open: bool)

const RING := 2000
const HISTORY_FILE := "user://console_history.txt"
const HISTORY_MAX := 128
const FONT_SIZE := 15
const SLIDE := 0.14
const TEXT := Color(0.86, 0.86, 0.86)
const ECHO := Color(1.0, 0.68, 0.2)

static var open_now := false    ## Gameplay and friends stand aside while this is true

var commands := Q3ConsoleCommands.new()
var host: Object:               ## the Gameplay scene, while a match is up
	set(v):
		host = v
		commands.host = v
		if v == null:
			commands.client.release_all()   # a bound +forward must not outlive the match

var history_file := HISTORY_FILE        ## tests point this elsewhere
var _tap := Tap.new()
var _built := false
var _panel: Control
var _view: Control
var _edit: LineEdit
var _font: Font
var _lines: PackedStringArray = []      ## everything printed, oldest first
var _rows: Array = []                   ## the lines wrapped to the view: runs for each row
var _wrap_w := 0.0
var _scroll := 0                        ## rows up from the newest
var _history: PackedStringArray = []
var _hist_at := -1
var _draft := ""
var _full := false
var _prev_mouse := Input.MOUSE_MODE_VISIBLE
var _slide: Tween
var _hid_pages: Array = []                ## the web layers open() hid, shown again by close()
var _partial := ""                      ## engine output that hasn't had its newline yet
var _partial_age := 0.0
var _autoexec_done := false
var _tabbed := ""                   ## the line a Tab already listed matches for


func _init() -> void:
	layer = 124
	process_mode = Node.PROCESS_MODE_ALWAYS
	visible = false


func _enter_tree() -> void:
	OS.add_logger(_tap)


func _exit_tree() -> void:
	OS.remove_logger(_tap)
	open_now = false


func _ready() -> void:
	commands.printer = print_line
	commands.clear_requested.connect(clear)
	commands.console_requested.connect(_on_request)
	commands.client.viewport = get_viewport()


## What the commands ask of the console itself: toggleconsole, a close for messagemode, condump.
func _on_request(what: String, arg: Variant) -> void:
	match what:
		"toggle": toggle()
		"close": close()
		"condump":
			var path := "user://%s.txt" % String(arg).get_file().trim_suffix(".txt")
			var f := FileAccess.open(path, FileAccess.WRITE)
			if f:
				for l in _lines:
					f.store_line(Q3Colors.strip(l))
				print_line("Dumped console text to " + ProjectSettings.globalize_path(path))
			else:
				print_line("^1Couldn't write " + path)


# ---------------------------------------------------------------- output

## One or more lines of output (Quake 3 color codes work).
func print_line(text: String) -> void:
	for l in text.split("\n"):
		_add_line(l)


func clear() -> void:
	_lines = PackedStringArray()
	_rows = []
	_scroll = 0
	if _view:
		_view.queue_redraw()


func _add_line(l: String) -> void:
	_lines.append(l)
	if _lines.size() > RING:
		_lines = _lines.slice(_lines.size() - RING)
		_rows = []
		_wrap_w = 0.0   # rewrapped on the next draw
	elif _built and _wrap_w > 0.0:
		var added := Q3Colors.wrap(_font, l, FONT_SIZE, _wrap_w, TEXT)
		if added.is_empty():
			added = [[]]
		_rows.append_array(added)
		if _scroll > 0:
			_scroll += added.size()   # a scrolled-back view stays where it is
	if _view and visible:
		_view.queue_redraw()


func _process(delta: float) -> void:
	var tapped := _tap.drain()
	if ClassDB.class_exists("Q3Server") and ClassDB.class_has_method("Q3Server", "console_take"):
		_feed(String(ClassDB.class_call_static("Q3Server", "console_take")), delta)
	else:
		for l in tapped:   # a q3core built before console_take: the log's lines, newlines lost
			_add_line(l)
	if not _autoexec_done:
		# Com_Init's "exec autoexec.cfg", through this console so its binds and client cvars work
		_autoexec_done = true
		if commands.read_config("autoexec.cfg") != null:
			commands.run("exec autoexec.cfg")
	commands.pump()   # what a "wait" held back
	commands.client.process(delta)


## The engine's raw text: Com_Printf pieces without newlines join up, and a line the engine hasn't
## ended is shown after a moment anyway (a prompt, a progress line).
func _feed(text: String, delta: float) -> void:
	if text == "":
		if _partial != "":
			_partial_age += delta
			if _partial_age > 0.3:
				_add_line(_partial)
				_partial = ""
		return
	_partial_age = 0.0
	var parts := (_partial + text.replace("\r", "")).split("\n")
	_partial = parts[parts.size() - 1]
	for i in parts.size() - 1:
		if parts[i].strip_edges() != "" or _lines.is_empty() or _lines[_lines.size() - 1].strip_edges() != "":
			_add_line(parts[i])


# ---------------------------------------------------------------- opening

func is_open() -> bool:
	return open_now


func toggle(full := false) -> void:
	if open_now:
		close()
	else:
		open(full)


func open(full := false) -> void:
	if open_now:
		return
	_build()
	_full = full
	open_now = true
	# the menus' web view is a native window over everything Godot draws: it steps aside while the
	# console is open (the main menu's, or the pause menu when it's up), and takes the keyboard back after
	_hid_pages.clear()
	for layer in get_tree().get_nodes_in_group(Q3WebLayer.GROUP):
		if (layer as Q3WebLayer).shown:
			(layer as Q3WebLayer).set_shown(false)
			_hid_pages.append(layer)
	_prev_mouse = Input.mouse_mode
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	visible = true
	_layout()
	_scroll = 0
	_tabbed = ""
	_edit.text = ""
	_hist_at = -1
	if _slide:
		_slide.kill()
	_panel.position.y = -_panel.size.y
	_slide = create_tween().set_pause_mode(Tween.TWEEN_PAUSE_PROCESS).set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_OUT)
	_slide.tween_property(_panel, "position:y", 0.0, SLIDE)
	_edit.grab_focus()
	toggled.emit(true)


func close() -> void:
	if not open_now:
		return
	open_now = false
	_edit.release_focus()
	for layer in _hid_pages:
		if is_instance_valid(layer):
			(layer as Q3WebLayer).set_shown(true)
	_hid_pages.clear()
	if Input.mouse_mode == Input.MOUSE_MODE_VISIBLE:
		Input.mouse_mode = _prev_mouse
	if _slide:
		_slide.kill()
	_slide = create_tween().set_pause_mode(Tween.TWEEN_PAUSE_PROCESS).set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_IN)
	_slide.tween_property(_panel, "position:y", -_panel.size.y, SLIDE)
	_slide.tween_callback(func(): visible = open_now)
	toggled.emit(false)


## The console key, also as the menus' web view forwards it: key code only, no physical key, so the
## bind (a physical key) never matched it and ` did nothing in the main menu (2026-10-25).
func _console_key(event: InputEvent) -> bool:
	if not InputMap.has_action("q3_console") or event is InputEventMouseMotion:
		return false
	if event.is_action_pressed("q3_console"):
		return true
	var k := event as InputEventKey
	if k == null or not k.pressed or k.echo or k.physical_keycode != KEY_NONE or k.keycode == KEY_NONE:
		return false
	for b in InputMap.action_get_events("q3_console"):
		if b is InputEventKey and k.keycode in [(b as InputEventKey).physical_keycode, (b as InputEventKey).keycode]:
			return true
	return false


func _input(event: InputEvent) -> void:
	if _console_key(event):
		toggle(Input.is_key_pressed(KEY_SHIFT))
		get_viewport().set_input_as_handled()
	elif open_now and event.is_action_pressed("ui_cancel") and not event is InputEventMouseButton:
		close()
		get_viewport().set_input_as_handled()
	elif not open_now and host != null and (event is InputEventKey or event is InputEventMouseButton):
		_bound_key(event)


## `bind <key> <command>`: a key in a match runs its line; a "+command" gets its "-" when the key comes up.
func _bound_key(event: InputEvent) -> void:
	if event.is_echo() or get_viewport().gui_get_focus_owner() is LineEdit:
		return   # chat or the pause menu has the keyboard
	var client := commands.client
	if event.is_pressed():
		var line := client.bound(event)
		if line != "":
			client.pressed(event, line)
			commands.run(line)
	else:
		var line := client.released(event)
		if line != "":
			for c in Q3ConsoleCommands.split_commands(line):
				if c.begins_with("+"):
					commands.run("-" + c.substr(1))


# ---------------------------------------------------------------- building

func _build() -> void:
	if _built:
		return
	_built = true
	var sys := SystemFont.new()
	sys.font_names = PackedStringArray(["Consolas", "Cascadia Mono", "Lucida Console", "Courier New", "monospace"])
	_font = sys
	_panel = Control.new()
	_panel.mouse_filter = Control.MOUSE_FILTER_STOP
	_panel.draw.connect(_draw_panel)
	add_child(_panel)
	_view = Control.new()
	_view.mouse_filter = Control.MOUSE_FILTER_PASS
	_view.clip_contents = true
	_view.draw.connect(_draw_view)
	_view.gui_input.connect(_view_input)
	_panel.add_child(_view)
	_edit = LineEdit.new()
	_edit.add_theme_font_override("font", _font)
	_edit.add_theme_font_size_override("font_size", FONT_SIZE)
	_edit.add_theme_color_override("font_color", Color.WHITE)
	_edit.add_theme_color_override("caret_color", ECHO)
	_edit.add_theme_color_override("selection_color", Color(ECHO, 0.35))
	var flat := StyleBoxEmpty.new()
	for s in ["normal", "focus", "read_only"]:
		_edit.add_theme_stylebox_override(s, flat)
	_edit.context_menu_enabled = false
	_edit.caret_blink = true
	_edit.select_all_on_focus = false
	_edit.keep_editing_on_text_submit = true
	_edit.text_submitted.connect(_submit)
	_edit.gui_input.connect(_edit_input)
	_panel.add_child(_edit)
	get_viewport().size_changed.connect(_layout)
	_load_history()
	_add_line("^3Q3Remaster console^7 · %s" % Diagnostics.version())
	_add_line("Type ^3help^7 for what it does. Tab completes.")


func _layout() -> void:
	if not _built:
		return
	var vp := get_viewport().get_visible_rect().size
	_panel.size = Vector2(vp.x, vp.y * (1.0 if _full else 0.5))
	if not (_slide and _slide.is_running()):
		_panel.position = Vector2(0, 0 if open_now else -_panel.size.y)
	var lh := _line_h()
	_view.position = Vector2(14, 8)
	_view.size = Vector2(_panel.size.x - 28, _panel.size.y - 16 - lh - 12)
	_edit.position = Vector2(14 + _prompt_w(), _panel.size.y - lh - 10)
	_edit.size = Vector2(_panel.size.x - 28 - _prompt_w(), lh + 4)
	_wrap_w = 0.0
	_panel.queue_redraw()
	_view.queue_redraw()


func _line_h() -> float:
	return _font.get_height(FONT_SIZE) + 1.0


func _prompt_w() -> float:
	return _font.get_string_size("] ", HORIZONTAL_ALIGNMENT_LEFT, -1, FONT_SIZE).x


func _draw_panel() -> void:
	var r := Rect2(Vector2.ZERO, _panel.size)
	_panel.draw_rect(r, Color(0.035, 0.03, 0.03, 0.93))
	_panel.draw_rect(Rect2(0, r.size.y - 3, r.size.x, 3), Color(ECHO, 0.9))
	var lh := _line_h()
	_panel.draw_string(_font, Vector2(14, r.size.y - 10 - 3), "]", HORIZONTAL_ALIGNMENT_LEFT, -1, FONT_SIZE, ECHO)
	_panel.draw_line(Vector2(14, r.size.y - lh - 16), Vector2(r.size.x - 14, r.size.y - lh - 16), Color(1, 1, 1, 0.12))


func _rewrap() -> void:
	_wrap_w = _view.size.x
	_rows = []
	for l in _lines:
		var w := Q3Colors.wrap(_font, l, FONT_SIZE, _wrap_w, TEXT)
		if w.is_empty():
			w = [[]]
		_rows.append_array(w)


func _draw_view() -> void:
	if _wrap_w != _view.size.x:
		_rewrap()
	var lh := _line_h()
	var visible_rows := int(_view.size.y / lh)
	_scroll = clampi(_scroll, 0, maxi(_rows.size() - visible_rows, 0))
	var last := _rows.size() - 1 - _scroll
	var y := _view.size.y - 4.0 - (lh - _font.get_ascent(FONT_SIZE) + 1.0) * 0.5
	var i := last
	while i >= 0 and y > -lh:
		Q3Colors.draw_runs(_view, _font, Vector2(0, y), _rows[i], FONT_SIZE)
		y -= lh
		i -= 1
	if _scroll > 0:
		_view.draw_string(_font, Vector2(_view.size.x - 120, _view.size.y - 6), "^ ^ ^  (%d)" % _scroll,
			HORIZONTAL_ALIGNMENT_LEFT, -1, FONT_SIZE, Color(ECHO, 0.8))


# ---------------------------------------------------------------- keys

func _view_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			_scroll_by(3)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_scroll_by(-3)


func _scroll_by(rows: int) -> void:
	_scroll = maxi(_scroll + rows, 0)
	_view.queue_redraw()


func _page() -> int:
	return maxi(int(_view.size.y / _line_h()) - 1, 1)


func _edit_input(event: InputEvent) -> void:
	if not event is InputEventKey or not event.pressed:
		return
	var k := event as InputEventKey
	match k.keycode:
		KEY_UP:
			_history_step(1)
		KEY_DOWN:
			_history_step(-1)
		KEY_TAB:
			_complete()
		KEY_PAGEUP:
			_scroll_by(_page())
		KEY_PAGEDOWN:
			_scroll_by(-_page())
		KEY_HOME:
			if not k.ctrl_pressed:
				return
			_scroll_by(RING * 8)
		KEY_END:
			if not k.ctrl_pressed:
				return
			_scroll = 0
			_view.queue_redraw()
		KEY_L:
			if not k.ctrl_pressed:
				return
			clear()
		_:
			return
	_edit.accept_event()


func _submit(line: String) -> void:
	var text := line.strip_edges()
	_edit.text = ""
	_hist_at = -1
	_scroll = 0
	_tabbed = ""
	if text == "":
		return
	_add_line("^3]^7 " + text)
	if _history.is_empty() or _history[_history.size() - 1] != text:
		_history.append(text)
		if _history.size() > HISTORY_MAX:
			_history = _history.slice(_history.size() - HISTORY_MAX)
		_save_history()
	commands.run(text)


func _history_step(dir: int) -> void:
	if _history.is_empty():
		return
	if _hist_at == -1:
		if dir < 0:
			return
		_draft = _edit.text
	_hist_at = clampi(_hist_at + dir, -1, _history.size() - 1)
	_edit.text = _draft if _hist_at == -1 else _history[_history.size() - 1 - _hist_at]
	_edit.caret_column = _edit.text.length()


func _complete() -> void:
	var r := commands.complete(_edit.text)
	_edit.text = r["text"]
	_edit.caret_column = _edit.text.length()
	var matches: Array = r["matches"]
	if matches.size() > 1 and _tabbed != _edit.text:
		_add_line("^3]^7 " + _edit.text)
		_add_line("  " + "  ".join(matches))
	_tabbed = _edit.text


func _load_history() -> void:
	if FileAccess.file_exists(history_file):
		for l in FileAccess.get_file_as_string(history_file).split("\n", false):
			_history.append(l)
		_history = _history.slice(maxi(_history.size() - HISTORY_MAX, 0))


func _save_history() -> void:
	var f := FileAccess.open(history_file, FileAccess.WRITE)
	if f:
		f.store_string("\n".join(_history))


# ---------------------------------------------------------------- the engine's output

## Picks the game engine's printed lines out of Godot's log (q3core's print hook writes them as
## "q3core: <text>"). Called from any thread.
class Tap extends Logger:
	var _mutex := Mutex.new()
	var _pending: PackedStringArray = []

	func _log_message(message: String, error: bool) -> void:
		if message.begins_with("q3core: "):
			_push(message, error)

	func _log_error(_function: String, _file: String, _line: int, code: String, rationale: String,
			_editor_notify: bool, _error_type: int, _script_backtraces: Array[ScriptBacktrace]) -> void:
		var text := rationale if rationale != "" else code
		if text.begins_with("q3core: "):
			_push(text, true)

	func _push(message: String, error: bool) -> void:
		var text := message.substr(8).strip_edges(false, true)
		_mutex.lock()
		if _pending.size() < 5000:
			for l in text.split("\n"):
				# Com_Printf("\n") arrives as an empty message (the hook trims the newline), so blanks are noise
				if l.strip_edges() != "":
					_pending.append(("^1" + l) if error else l)
		_mutex.unlock()

	func drain() -> PackedStringArray:
		_mutex.lock()
		var out := _pending
		_pending = PackedStringArray()
		_mutex.unlock()
		return out
