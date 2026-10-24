extends Node
## Autoload "Settings": every setting from Q3SettingsSchema, stored in user://settings.cfg.
## set_value() applies live, emits `changed`, and saves shortly after. Scenes with their own
## 3D environment call apply_environment() and re-apply on `environment_changed`.

signal changed(key: String, value: Variant)
signal environment_changed

const PATH := "user://settings.cfg"
const M_YAW := 0.022            ## Quake 3 degrees per mouse count at sensitivity 1
const BUSES := {"audio/vol_master": "Master", "audio/vol_music": "Music", "audio/vol_effects": "Effects",
	"audio/vol_voice": "Voice", "audio/vol_announcer": "Announcer", "audio/vol_ui": "UI"}

var entries: Array = []
var path := PATH
var _by_key: Dictionary = {}
var _values: Dictionary = {}
var _save_timer: Timer
var _in_preset := false
var _headless := false
var _colorblind: CanvasLayer


func _ready() -> void:
	_headless = DisplayServer.get_name() == "headless"
	entries = Q3SettingsSchema.build()
	for e in entries:
		_by_key[e["key"]] = e
		_values[e["key"]] = _copy(e["default"])
	_save_timer = Timer.new()
	_save_timer.one_shot = true
	_save_timer.wait_time = 0.5
	_save_timer.timeout.connect(save)
	add_child(_save_timer)
	load_from(PATH)
	_register_ui_actions()
	for e in entries:
		_apply(e["key"], _values[e["key"]])
	get_window().focus_exited.connect(_on_focus.bind(false))
	get_window().focus_entered.connect(_on_focus.bind(true))


# ---------------------------------------------------------------- values

func has(key: String) -> bool:
	return _by_key.has(key)


func entry(key: String) -> Dictionary:
	return _by_key.get(key, {})


func get_value(key: String) -> Variant:
	return _values.get(key)


func default_of(key: String) -> Variant:
	return _copy(_by_key[key]["default"]) if _by_key.has(key) else null


func is_default(key: String) -> bool:
	return _same(_values.get(key), _by_key[key]["default"])


func set_value(key: String, value: Variant, save_soon := true) -> void:
	if not _by_key.has(key):
		push_warning("Settings: unknown key " + key)
		return
	value = _coerce(_by_key[key], value)
	if _same(_values[key], value):
		return
	_values[key] = value
	_apply(key, value)
	if key.begins_with("graphics/") and key != "graphics/preset" and not _in_preset:
		_match_preset()
	changed.emit(key, value)
	if save_soon:
		_save_timer.start()


func reset(key: String) -> void:
	set_value(key, default_of(key))


func reset_tab(tab: String) -> void:
	for e in entries:
		if e["tab"] == tab:
			set_value(e["key"], _copy(e["default"]))


func reset_all() -> void:
	for e in entries:
		set_value(e["key"], _copy(e["default"]))


func tab_entries(tab: String) -> Array:
	return entries.filter(func(e): return e["tab"] == tab)


## Settings whose label, tip, group or key contain every word of `text`.
func search(text: String) -> Array:
	var words := text.to_lower().split(" ", false)
	if words.is_empty():
		return []
	return entries.filter(func(e):
		if e["tab"] == "profile":
			return false
		var hay := ("%s %s %s %s" % [e["label"], e["tip"], e["group"], e["key"]]).to_lower()
		for w in words:
			if not hay.contains(w):
				return false
		return true)


## Options for an enum, including the ones only known at runtime (devices, characters).
func options(key: String) -> Array:
	var e: Dictionary = _by_key[key]
	var opts: Array = e.get("options", []).duplicate()
	match e.get("dynamic", ""):
		"output_devices":
			for d in AudioServer.get_output_device_list():
				if d != "Default":
					opts.append([d, d])
		"input_devices":
			for d in AudioServer.get_input_device_list():
				if d != "Default":
					opts.append([d, d])
		"characters", "characters_only":
			for c in Q3.characters:
				opts.append([c, c.capitalize()])
	return opts


# ---------------------------------------------------------------- storage

func save(to := "") -> void:
	var cf := ConfigFile.new()
	for k in _values:
		cf.set_value(k.get_slice("/", 0), k.get_slice("/", 1), _values[k])
	cf.save(to if to != "" else path)


func load_from(from: String) -> bool:
	var cf := ConfigFile.new()
	if cf.load(from) != OK:
		return false
	for k: String in _by_key:
		var sec := k.get_slice("/", 0)
		var name := k.get_slice("/", 1)
		if cf.has_section_key(sec, name):
			_values[k] = _coerce(_by_key[k], cf.get_value(sec, name))
	return true


## Every non-default setting as a shareable code (prefix filters, e.g. "crosshair/").
func export_code(prefix := "") -> String:
	var d := {}
	for k in _values:
		if k.begins_with(prefix) and not k.begins_with("profile/") and not is_default(k):
			d[k] = var_to_str(_values[k])
	return Marshalls.utf8_to_base64(JSON.stringify(d))


func import_code(code: String) -> int:
	var d: Variant = JSON.parse_string(Marshalls.base64_to_utf8(code.strip_edges()))
	if not d is Dictionary:
		return -1
	var n := 0
	for k in d:
		if _by_key.has(k) and not String(k).begins_with("profile/"):
			set_value(k, str_to_var(String(d[k])))
			n += 1
	return n


# ---------------------------------------------------------------- derived values

## Quake 3's cg_fov is horizontal degrees at 4:3. Keeping the 4:3 vertical angle makes every wider
## screen see more at the sides (Hor+). Returns Godot's vertical FOV (Camera3D.KEEP_HEIGHT).
func fov_vertical(fov_4x3 := -1.0) -> float:
	var h := deg_to_rad(fov_4x3 if fov_4x3 > 0 else float(_values["video/fov"]))
	return rad_to_deg(2.0 * atan(tan(h * 0.5) * 0.75))


## Mouse travel for a full turn, in cm.
func cm_per_360() -> float:
	var deg_per_count: float = float(_values["mouse/sensitivity"]) * M_YAW
	return 360.0 / deg_per_count / float(_values["mouse/dpi"]) * 2.54


func particle_ratio() -> float:
	var r: float = {"low": 0.35, "medium": 0.65, "high": 1.0}[_values["graphics/particles"]]
	return r * (0.4 if _values["accessibility/reduced_flashing"] else 1.0)


## Network > Connection speed as Quake 3's rate userinfo (bytes per second).
func rate() -> int:
	return {"slow": 25000, "medium": 50000, "fast": 90000, "lan": 99999}.get(_values["network/rate"], 25000)


## Audio > Spatial audio for a positional sound: distance muffling and a sharper left and right,
## or (off) Quake 3's plain stereo panning.
func spatialize(a: AudioStreamPlayer3D) -> void:
	var on: bool = _values["audio/spatial"]
	a.attenuation_filter_db = -24.0 if on else 0.0
	a.panning_strength = 1.5 if on else 1.0


func shadows_on() -> bool:
	return _values["graphics/shadows"] != "off"


## Applies the graphics settings to a scene's environment.
func apply_environment(env: Environment) -> void:
	if env == null:
		return
	env.ssr_enabled = _values["graphics/reflections"]
	env.ssao_enabled = _values["graphics/ao"] != "off"
	env.sdfgi_enabled = _values["graphics/gi"] == "sdfgi"
	env.glow_enabled = _values["graphics/bloom"]
	env.volumetric_fog_enabled = _values["graphics/volumetric_fog"]
	env.adjustment_enabled = true
	env.adjustment_brightness = _values["video/brightness"]
	env.adjustment_contrast = _values["video/contrast"]


# ---------------------------------------------------------------- keybinds

## "key:W", "mouse:1", "joy:0", "joyaxis:5:1" <-> InputEvent
static func event_from_string(s: String) -> InputEvent:
	var p := s.split(":")
	match p[0]:
		"key":
			var k := InputEventKey.new()
			k.physical_keycode = OS.find_keycode_from_string(p[1])
			return k if k.physical_keycode != KEY_NONE else null
		"mouse":
			var m := InputEventMouseButton.new()
			m.button_index = int(p[1]) as MouseButton
			return m
		"joy":
			var j := InputEventJoypadButton.new()
			j.button_index = int(p[1]) as JoyButton
			return j
		"joyaxis":
			var a := InputEventJoypadMotion.new()
			a.axis = int(p[1]) as JoyAxis
			a.axis_value = float(p[2])
			return a
	return null


static func event_to_string(ev: InputEvent) -> String:
	if ev is InputEventKey:
		var code: Key = ev.physical_keycode if ev.physical_keycode != KEY_NONE else ev.keycode
		return "key:" + OS.get_keycode_string(code)
	if ev is InputEventMouseButton:
		return "mouse:%d" % ev.button_index
	if ev is InputEventJoypadButton:
		return "joy:%d" % ev.button_index
	if ev is InputEventJoypadMotion and absf(ev.axis_value) > 0.5:
		return "joyaxis:%d:%d" % [ev.axis, 1 if ev.axis_value > 0 else -1]
	return ""


const _MOUSE := {1: "Mouse 1", 2: "Mouse 2", 3: "Mouse 3", 4: "Wheel up", 5: "Wheel down", 6: "Wheel left", 7: "Wheel right", 8: "Mouse 4", 9: "Mouse 5"}
const _PAD := {0: "Pad A", 1: "Pad B", 2: "Pad X", 3: "Pad Y", 4: "Back", 5: "Guide", 6: "Start", 7: "Left stick", 8: "Right stick",
	9: "Left bumper", 10: "Right bumper", 11: "D-pad up", 12: "D-pad down", 13: "D-pad left", 14: "D-pad right"}
const _AXES := {0: "Left stick X", 1: "Left stick Y", 2: "Right stick X", 3: "Right stick Y", 4: "Left trigger", 5: "Right trigger"}


static func bind_label(s: String) -> String:
	if s == "":
		return "-"
	var p := s.split(":")
	match p[0]:
		"key": return p[1]
		"mouse": return _MOUSE.get(int(p[1]), "Mouse %s" % p[1])
		"joy": return _PAD.get(int(p[1]), "Pad %s" % p[1])
		"joyaxis":
			var n: String = _AXES.get(int(p[1]), "Axis %s" % p[1])
			return n if int(p[1]) >= 4 else "%s %s" % [n, "+" if int(p[2]) > 0 else "-"]
	return s


## Other bind settings that share one of this one's inputs.
func bind_conflicts(key: String) -> PackedStringArray:
	var out: PackedStringArray = []
	var mine: Array = (_values[key] as Array).filter(func(x): return x != "")
	for e in entries:
		if e["type"] != "bind" or e["key"] == key:
			continue
		for b in _values[e["key"]]:
			if b != "" and mine.has(b):
				out.append(e["key"])
				break
	return out


func set_bind(key: String, slot: int, ev_string: String) -> void:
	var v: Array = (_values[key] as Array).duplicate()
	v[slot] = ev_string
	set_value(key, v)


func _apply_bind(key: String, binds: Array) -> void:
	var action := "q3_" + key.get_slice("/", 1)
	if not InputMap.has_action(action):
		InputMap.add_action(action, 0.5)
	InputMap.action_erase_events(action)
	for b in binds:
		var ev: InputEvent = event_from_string(b) if b != "" else null
		if ev:
			InputMap.action_add_event(action, ev)


## Menu navigation for controllers: left stick and shoulder buttons alongside the d-pad, and
## Q/E or the bumpers to switch tabs. Mouse 2 goes back, as in Quake 3 (K_MOUSE2 pops the menu).
func _register_ui_actions() -> void:
	var adds := {"ui_up": ["joyaxis:1:-1"], "ui_down": ["joyaxis:1:1"], "ui_left": ["joyaxis:0:-1"], "ui_right": ["joyaxis:0:1"],
		"ui_cancel": ["mouse:2"], "menu_tab_prev": ["key:Q", "joy:9"], "menu_tab_next": ["key:E", "joy:10"]}
	for action in adds:
		if not InputMap.has_action(action):
			InputMap.add_action(action, 0.5)
		for s in adds[action]:
			InputMap.action_add_event(action, event_from_string(s))


# ---------------------------------------------------------------- live application

func _apply(key: String, v: Variant) -> void:
	var e: Dictionary = _by_key[key]
	if e["type"] == "bind":
		_apply_bind(key, v)
		return
	if BUSES.has(key):
		var bus := AudioServer.get_bus_index(BUSES[key])
		if bus >= 0:
			AudioServer.set_bus_volume_db(bus, linear_to_db(maxf(float(v) / 100.0, 0.0001)))
			AudioServer.set_bus_mute(bus, int(v) == 0)
		return
	var vp := get_tree().root
	match key:
		"graphics/preset":
			if v != "custom":
				_in_preset = true
				var i: int = Q3SettingsSchema.PRESET_NAMES.find(v)
				for k in Q3SettingsSchema.PRESETS:
					set_value(k, Q3SettingsSchema.PRESETS[k][i])
				_in_preset = false
		"graphics/shadows":
			var size: int = {"off": 512, "low": 1024, "medium": 2048, "high": 4096, "ultra": 8192}[v]
			var q: int = {"off": 0, "low": 1, "medium": 2, "high": 3, "ultra": 4}[v]
			RenderingServer.directional_shadow_atlas_set_size(size, true)
			RenderingServer.directional_soft_shadow_filter_set_quality(q as RenderingServer.ShadowQuality)
			RenderingServer.positional_soft_shadow_filter_set_quality(q as RenderingServer.ShadowQuality)
			environment_changed.emit()
		"graphics/ao":
			if v != "off":
				var q: int = {"low": 1, "medium": 2, "high": 3}[v]
				RenderingServer.environment_set_ssao_quality(q as RenderingServer.EnvironmentSSAOQuality, true, 0.5, 2, 50, 300)
			environment_changed.emit()
		"graphics/gi", "graphics/reflections", "graphics/volumetric_fog", "graphics/bloom", "video/brightness", "video/contrast":
			environment_changed.emit()
		"graphics/particles":
			environment_changed.emit()
		"graphics/antialiasing":
			vp.msaa_3d = {"msaa2": Viewport.MSAA_2X, "msaa4": Viewport.MSAA_4X, "msaa8": Viewport.MSAA_8X}.get(v, Viewport.MSAA_DISABLED)
			vp.screen_space_aa = Viewport.SCREEN_SPACE_AA_FXAA if v == "fxaa" else Viewport.SCREEN_SPACE_AA_DISABLED
			vp.use_taa = v == "taa"
		"graphics/anisotropy":
			# the project setting is read only at startup (it stayed at its 4x default whatever the
			# menu said); the viewport's own level changes live. The project setting still seeds
			# viewports made later (portals, mirrors).
			var level: int = {2: 1, 4: 2, 8: 3, 16: 4}[int(v)]
			vp.anisotropic_filtering_level = level as Viewport.AnisotropicFiltering
			ProjectSettings.set_setting("rendering/textures/default_filters/anisotropic_filtering_level", level)
		"graphics/texture_quality":
			vp.texture_mipmap_bias = {"low": 1.0, "medium": 0.5, "high": 0.0}[v]
		"graphics/detail":
			vp.mesh_lod_threshold = {"low": 4.0, "medium": 2.0, "high": 1.0}[v]
		"video/render_scale":
			vp.scaling_3d_scale = float(v)
		"video/upscaler":
			vp.scaling_3d_mode = {"bilinear": Viewport.SCALING_3D_MODE_BILINEAR, "fsr": Viewport.SCALING_3D_MODE_FSR, "fsr2": Viewport.SCALING_3D_MODE_FSR2}[v]
		"video/max_fps":
			Engine.max_fps = int(v)
		"video/display_mode", "video/resolution", "video/monitor", "video/vsync":
			_apply_display.call_deferred()
		"accessibility/ui_scale":
			vp.content_scale_factor = float(v)
		"accessibility/reduced_flashing":
			environment_changed.emit()
		"audio/menu_ambient":
			if has_node("/root/UiSound"):
				get_node("/root/UiSound").set_ambient(v)
		"audio/output_device":
			if not _headless:
				AudioServer.output_device = v if AudioServer.get_output_device_list().has(v) else "Default"
		"audio/input_device":
			if not _headless:
				AudioServer.input_device = v if AudioServer.get_input_device_list().has(v) else "Default"
		"controls/pad_deadzone":
			# the menus' stick directions; Gameplay reads the sticks itself with the same deadzone
			for a in ["ui_up", "ui_down", "ui_left", "ui_right"]:
				if InputMap.has_action(a):
					InputMap.action_set_deadzone(a, clampf(float(v), 0.05, 0.9))
		"accessibility/colorblind":
			_set_colorblind(String(v))


## Accessibility > Colorblind mode: shaders/colorblind.gdshader on a layer over everything Godot
## draws, in every scene (the web menu is its own window and keeps its colors).
func _set_colorblind(mode: String) -> void:
	var i: int = ["off", "protanopia", "deuteranopia", "tritanopia"].find(mode)
	if _colorblind == null:
		if i <= 0 or _headless:
			return
		_colorblind = CanvasLayer.new()
		_colorblind.layer = 120
		var rect := ColorRect.new()
		rect.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
		rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
		var mat := ShaderMaterial.new()
		mat.shader = load("res://shaders/colorblind.gdshader")
		rect.material = mat
		_colorblind.add_child(rect)
		add_child(_colorblind)
	_colorblind.visible = i > 0
	((_colorblind.get_child(0) as ColorRect).material as ShaderMaterial).set_shader_parameter("mode", maxi(i, 0))


func _apply_display() -> void:
	if _headless:
		return
	var w := get_window()
	var mode: String = _values["video/display_mode"]
	var res: String = _values["video/resolution"]
	var forced := _forced_window()
	if forced != "":
		mode = "windowed"
		res = forced
	DisplayServer.window_set_vsync_mode({"off": DisplayServer.VSYNC_DISABLED, "on": DisplayServer.VSYNC_ENABLED,
		"adaptive": DisplayServer.VSYNC_ADAPTIVE, "mailbox": DisplayServer.VSYNC_MAILBOX}[_values["video/vsync"]])
	var screen := clampi(int(_values["video/monitor"]), 0, DisplayServer.get_screen_count() - 1)
	match mode:
		"windowed":
			w.mode = Window.MODE_WINDOWED
			w.borderless = false
			var r: PackedStringArray = res.split("x")
			var size := Vector2i(int(r[0]), int(r[1]))
			var usable := DisplayServer.screen_get_usable_rect(screen)
			size = size.min(usable.size)
			if w.size != size or w.current_screen != screen:
				w.current_screen = screen
				w.size = size
				w.position = usable.position + (usable.size - size) / 2
		"borderless":
			w.current_screen = screen
			w.mode = Window.MODE_FULLSCREEN
		"fullscreen", "exclusive":
			w.current_screen = screen
			w.mode = Window.MODE_EXCLUSIVE_FULLSCREEN if mode == "exclusive" else Window.MODE_FULLSCREEN


## `-- window=1920x1080` runs in a window of that size whatever the saved display settings say, and
## saves nothing (screenshots and captures).
func _forced_window() -> String:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("window=") and a.substr(7).split("x").size() == 2:
			return a.substr(7)
	return ""


## The values the 15 s "keep these display settings?" confirmation reverts to.
func display_snapshot() -> Dictionary:
	var d := {}
	for k in ["video/display_mode", "video/resolution", "video/monitor"]:
		d[k] = _values[k]
	return d


func _on_focus(focused: bool) -> void:
	var bus := AudioServer.get_bus_index("Master")
	AudioServer.set_bus_mute(bus, (not focused and _values["audio/mute_unfocused"]) or int(_values["audio/vol_master"]) == 0)


func _match_preset() -> void:
	var cur: String = _values["graphics/preset"]
	var i: int = Q3SettingsSchema.PRESET_NAMES.find(cur)
	if i < 0:
		return
	for k in Q3SettingsSchema.PRESETS:
		if not _same(_values[k], Q3SettingsSchema.PRESETS[k][i]):
			_values["graphics/preset"] = "custom"
			changed.emit("graphics/preset", "custom")
			return


# ---------------------------------------------------------------- helpers

func _coerce(e: Dictionary, v: Variant) -> Variant:
	match e["type"]:
		"bool":
			return bool(v)
		"int":
			return clampi(int(v), int(e.get("min", -2147483648)), int(e.get("max", 2147483647)))
		"float":
			return clampf(float(v), float(e.get("min", -INF)), float(e.get("max", INF)))
		"color":
			return v if v is Color else Color(str(v)) if Color.html_is_valid(str(v)) else e["default"]
		"string":
			return str(v).substr(0, int(e.get("max", 256)))
		"bind":
			var a: Array = v if v is Array else []
			var out: Array = []
			for i in 3:
				out.append(String(a[i]) if i < a.size() else "")
			return out
		"enum":
			if e.get("dynamic", "") != "":
				return v   # device and character lists change between machines; keep the stored value
			for o in e["options"]:
				if _same(o[0], v):
					return o[0]
			return e["default"]
	return v


static func _same(a: Variant, b: Variant) -> bool:
	if typeof(a) == TYPE_FLOAT or typeof(b) == TYPE_FLOAT:
		return (typeof(a) in [TYPE_INT, TYPE_FLOAT]) and (typeof(b) in [TYPE_INT, TYPE_FLOAT]) and is_equal_approx(float(a), float(b))
	if a is Color and b is Color:
		return a.is_equal_approx(b)
	return typeof(a) == typeof(b) and a == b


static func _copy(v: Variant) -> Variant:
	return v.duplicate() if v is Array or v is Dictionary else v
