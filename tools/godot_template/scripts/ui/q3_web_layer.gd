class_name Q3WebLayer
extends Node
## The web menu (webui/, a Next.js app) in a transparent godot_wry WebView over the game, wired
## to a Q3MenuBridge. The main menu shows it all the time; Gameplay keeps one hidden and shows it
## as the pause menu. It does what both need:
##   - the page only ever shows our bundled menu (anything that navigates it elsewhere is sent
##     back, and the bridge ignores the page meanwhile);
##   - messages for the page wait until its script runs;
##   - bind capture takes every input, and controllers drive the page through "nav" messages
##     (keyboard and mouse go to the page itself).
## Headless runs and builds without godot_wry get no web view (`web` stays null); tests drive the
## bridge directly.

const WEBUI := "res://webui/index.html"
const WEB_ORIGIN := "http://res.webui/"
## The page's images, sounds and fonts as plain files, beside an exported game's exe: Godot ships
## only its imported versions of those, which the web view can't read (tools/export_game.py).
const ASSETS_PACK := "webui_assets.pck"
static var _assets_mounted := false

signal loaded

var bridge: Q3MenuBridge
var web: Control
var shown := true
var _ready_to_post := false
var _outbox: Array = []        ## messages for the page before it can receive them
var _trusted := true
var _pad := false
var _repeat_dir := ""
var _repeat_in := 0.0


static func _mount_assets() -> void:
	if _assets_mounted or OS.has_feature("editor"):
		return
	_assets_mounted = true
	var pack := OS.get_executable_path().get_base_dir().path_join(ASSETS_PACK)
	if FileAccess.file_exists(pack) and not ProjectSettings.load_resource_pack(pack, true):
		push_warning("Q3WebLayer: couldn't load " + pack)


## The owner connects bridge.post to post() (or to its own handler that calls it).
func _init(b: Q3MenuBridge) -> void:
	bridge = b


## Creates the web view (call once the node is in the tree). Returns false when there is none.
## forward_keys: the page's input also reaches Godot. The pause menu turns it off: there an
## Escape the page acts on (resume) would arrive in Godot again after the unpause and pause anew.
func build(visible := true, forward_keys := true) -> bool:
	if DisplayServer.get_name() == "headless" or not ClassDB.class_exists("WebView"):
		return false
	if not FileAccess.file_exists(WEBUI):
		var l := Label.new()
		l.text = "The web menu isn't built: run python tools/build_webui.py"
		l.position = Vector2(60, 400)
		add_child(l)
		return false
	_mount_assets()
	web = ClassDB.instantiate("WebView")
	web.set("transparent", true)
	web.set("full_window_size", true)
	web.set("forward_input_events", forward_keys)
	web.set("focused_when_created", visible)
	web.set("zoom_hotkeys", false)
	web.set("devtools", OS.is_debug_build())
	web.set("url", WEBUI)
	web.connect("ipc_message", _on_message)
	web.connect("page_load_started", _on_navigating)
	web.connect("page_load_finished", _on_loaded)
	add_child(web)
	shown = true
	if not visible:
		set_shown(false)
	return true


## Shows or hides the page. Hidden, the game gets the keyboard back.
func set_shown(on: bool) -> void:
	shown = on
	if web == null:
		return
	web.call("set_visible", on)
	if on:
		web.call("focus")
	else:
		web.call("focus_parent")


func post(msg: Dictionary) -> void:
	if web == null:
		return
	if _ready_to_post:
		web.call("post_message", JSON.stringify(msg))
	else:
		_outbox.append(msg)


func _on_navigating(url: String) -> void:
	# (the page's live URL comes from these load signals; WebView.get_url() returns the url we set)
	_trusted = url.begins_with(WEB_ORIGIN)
	if not _trusted:
		push_warning("web menu: blocked navigation to " + url.left(120))
		web.call("load_url", WEBUI)


func _on_loaded(url: String) -> void:
	print("web menu: loaded (", url, ")")
	if shown:
		web.call("focus")
	_flush()
	loaded.emit()


## A message from the page means its script runs, so it can take ours (this can come before
## page_load_finished).
func _on_message(text: String) -> void:
	if not _trusted:
		return
	_flush()
	bridge.handle_json(text)


func _flush() -> void:
	_ready_to_post = true
	for m in _outbox:
		web.call("post_message", JSON.stringify(m))
	_outbox.clear()


func _input(ev: InputEvent) -> void:
	if not shown:
		return
	if bridge.capture_key != "":
		if bridge.capture_input(ev):
			get_viewport().set_input_as_handled()
		return
	var pad := ev is InputEventJoypadButton or (ev is InputEventJoypadMotion and absf(ev.axis_value) > 0.5)
	if pad != _pad and (pad or ev is InputEventKey or ev is InputEventMouseButton):
		_pad = pad
		post({"type": "input_mode", "pad": pad})
	if not pad:
		return
	for pair in [["ui_up", "up"], ["ui_down", "down"], ["ui_left", "left"], ["ui_right", "right"]]:
		if ev.is_action_pressed(pair[0]):
			nav(pair[1])
			_repeat_dir = pair[1]
			_repeat_in = 0.4
	if ev is InputEventJoypadButton and ev.pressed:
		match ev.button_index:
			JOY_BUTTON_A: nav("accept")
			JOY_BUTTON_B: nav("back")
			JOY_BUTTON_Y: nav("social")
			JOY_BUTTON_LEFT_SHOULDER: nav("tab_prev")
			JOY_BUTTON_RIGHT_SHOULDER: nav("tab_next")


func nav(dir: String) -> void:
	post({"type": "nav", "dir": dir})


func _process(delta: float) -> void:
	# held d-pad / stick repeats, like a held arrow key
	if _repeat_dir == "" or not shown:
		return
	if not Input.is_action_pressed("ui_" + _repeat_dir):
		_repeat_dir = ""
		return
	_repeat_in -= delta
	if _repeat_in <= 0.0:
		_repeat_in = 0.09
		nav(_repeat_dir)
