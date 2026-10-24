extends Node
## Headless check that the settings once marked "not yet wired" drive the game: the schema has no
## unwired entries left; in a devmap q3dm17 match, Q3Server.items() reports the map's items and a
## taken Red Armor's respawn time, which HUD > Item timers counts down; the announcer stays quiet
## when off and is captioned with Subtitles; Gun field of view scales the gun; Gun position Hidden
## hides it; Toggle crouch ducks on one press; Mouse acceleration turns further on a fast swipe;
## Bright skins and Force enemy model change the bot's body; Effect intensity dims effect lights;
## Spatial audio, the rate userinfo, the lagometer, the Speedometer toggle (and an old "off" style
## carried over) and the game's audio buses. Every setting it
## touches is put back.
##   godot --headless --path . res://tests/settings_wiring_test.tscn

const E := preload("res://scripts/game/q3_enums.gd")
const TOUCHED := ["hud/item_timers", "audio/announcer", "accessibility/subtitles", "gameplay/gun_fov", "video/fov",
	"gameplay/draw_gun", "controls/toggle_crouch", "mouse/acceleration", "mouse/sensitivity", "mouse/invert",
	"gameplay/bright_skins", "gameplay/force_model", "graphics/effect_intensity", "audio/spatial", "hud/net_graph",
	"hud/show_speedometer", "hud/speedometer", "graphics/anisotropy", "graphics/preset"]   # preset last: changing anisotropy turns it to Custom

var _game: Q3Gameplay
var _t := 0.0
var _step := 0
var _ok := true
var _saved := {}
var _armor := {}


func _ready() -> void:
	for k in TOUCHED:
		_saved[k] = Settings.get_value(k)
	Settings.set_value("video/fov", 90.0)
	Settings.set_value("mouse/sensitivity", 5.0)
	Settings.set_value("mouse/invert", false)
	Settings.set_value("gameplay/draw_gun", "right")
	_schema()
	var s := Session.new_setup("ffa", "q3dm17")
	s.bots = [{"name": "Sarge", "skill": 1, "team": ""}]
	s.set_meta("devmap", true)
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	_game.test_input = true
	add_child(_game)


func _schema() -> void:
	var unwired := Settings.entries.filter(func(e): return not e.get("wired", true)).map(func(e): return e["key"])
	_check("no setting is marked unwired %s" % [unwired], unwired.is_empty())
	_check("Steam Input, raw input and voice chat are gone", not (Settings.has("controls/steam_input")
		or Settings.has("mouse/raw_input") or Settings.has("audio/voice_mode")))
	_check("Interpolation is back, now that online matches draw other players behind the clock", Settings.has("network/interp"))
	_check("Stick layout is there", Settings.options("controls/stick_layout").size() == 2)
	_check("no tip still says it waits on Gameplay", Settings.entries.all(func(e): return not String(e["tip"]).contains("once the Gameplay scene exists")))
	_check("the rate userinfo follows Connection speed", Q3Host.userinfo({}).contains("\\rate\\%d" % Settings.rate()))
	for b in ["Effects", "Announcer", "Music", "UI"]:
		_check("audio bus %s exists" % b, AudioServer.get_bus_index(b) >= 0)
	# the project setting is read only at startup, so the menu's choice has to reach the viewport
	Settings.set_value("graphics/anisotropy", 2)
	var low := get_tree().root.anisotropic_filtering_level
	Settings.set_value("graphics/anisotropy", 16)
	_check("Anisotropic filtering reaches the viewport (2x: %d, 16x: %d)" % [low, get_tree().root.anisotropic_filtering_level],
		low == Viewport.ANISOTROPY_2X and get_tree().root.anisotropic_filtering_level == Viewport.ANISOTROPY_16X)


func _process(delta: float) -> void:
	_t += delta
	match _step:
		0:
			if _t > 2.5:
				var items: Array = Q3Server.items()
				var names: PackedStringArray = ClassDB.class_call_static("Q3Core", "item_classnames")
				for it in items:
					if names[int(it["item"]) - 1] == "item_armor_body":
						_armor = it
				_check("Q3Server.items(): %d items, the Red Armor among them" % items.size(), items.size() > 10 and not _armor.is_empty())
				Settings.set_value("hud/item_timers", true)
				var o: Vector3 = _armor.get("origin", Vector3.ZERO)
				Q3Server.client_command(_game.me, "setviewpos %d %d %d 0" % [o.x, o.y, o.z + 24])
				_step = 1
		1:
			if _t > 3.5:
				var it := _find(int(_armor.get("number", -1)))
				var left := (int(it.get("respawn_time", 0)) - Q3Server.time()) / 1000.0
				_check("taken: the Red Armor comes back in %.1f s (25 s, g_items.c RESPAWN_ARMOR)" % left, it.get("taken", false) and left > 22.0 and left <= 25.0)
				var timers: Array = _game.hud._item_timers
				var red := timers.filter(func(x): return x[0] == "iconr_red")
				var mega := timers.filter(func(x): return x[0] == "iconh_mega")
				# the Quad counts too: powerups wait 30-60 s for their first spawn (FinishSpawningItem)
				_check("Item timers: %s, the armor counting down, the Mega up" % [timers], red.size() == 1 and float(red[0][1]) > 20.0
					and mega.size() == 1 and float(mega[0][1]) < 0.0)
				_announcer()
				_gun()
				Settings.set_value("controls/toggle_crouch", true)
				_tap("q3_crouch")
				_step = 2
		2:
			if _t > 4.2:
				_check("Toggle crouch: one press ducks", int(Q3Server.player_state(_game.me)["pm_flags"]) & E.PMF_DUCKED != 0)
				_tap("q3_crouch")
				_step = 3
		3:
			if _t > 4.8:
				_check("Toggle crouch: a second press stands", int(Q3Server.player_state(_game.me)["pm_flags"]) & E.PMF_DUCKED == 0)
				_mouse()
				_bodies()
				_effects()
				_speedometer()
				_check("the lagometer has samples", _game.hud._lag_frames.size() > 10)
				_step = 4
		4:
			if _t > 5.5:
				var ch := _bot_body()
				_check("Force enemy model: the bot is drawn as Keel", ch != null and ch.model == "keel")
				for k in _saved:
					Settings.set_value(k, _saved[k])
				Settings.save()
				print("settings wiring: ", "PASS" if _ok else "FAIL")
				get_tree().quit(0 if _ok else 1)


func _announcer() -> void:
	var hud := _game.hud
	Settings.set_value("audio/announcer", false)
	Settings.set_value("accessibility/subtitles", true)
	hud._captions.clear()
	hud._queue_sound("sound/feedback/impressive.wav", false)
	_check("Announcer off: nothing queued or captioned", hud._sound_queue.is_empty() and hud._captions.is_empty() and not hud._announcer.playing)
	Settings.set_value("audio/announcer", true)
	hud._queue_sound("sound/feedback/excellent.wav", false)
	_check("Announcer on: it plays on the Announcer bus", hud._announcer.playing and hud._announcer.bus == &"Announcer")
	_check("Subtitles: '%s'" % [hud._captions.map(func(c): return c["text"])], hud._captions.size() == 1 and hud._captions[0]["text"] == "Excellent!")
	hud._caption("sound/player/announce/sarge_wins.wav")
	_check("Subtitles name the winner", hud._captions[-1]["text"] == "Sarge wins!")


func _gun() -> void:
	var gun: Node3D = _game._gun
	Settings.set_value("gameplay/gun_fov", 110.0)
	_game._update_gun(Q3Server.player_state(_game.me))
	var k := tan(deg_to_rad(Settings.fov_vertical(90.0)) * 0.5) / tan(deg_to_rad(Settings.fov_vertical(110.0)) * 0.5)
	var sx := gun.transform.basis.x.length()
	_check("Gun field of view 110: the gun scales by %.3f (%.3f)" % [k, sx], gun != null and absf(sx - k) < 0.01 and absf(gun.transform.basis.z.length() - 1.0) < 0.01)
	Settings.set_value("gameplay/gun_fov", 0.0)
	_game._update_gun(Q3Server.player_state(_game.me))
	_check("Gun field of view 0: drawn at the view's", absf(gun.transform.basis.x.length() - 1.0) < 0.01)
	Settings.set_value("gameplay/draw_gun", "hidden")
	_game._update_gun(Q3Server.player_state(_game.me))
	_check("Gun position Hidden hides it", not gun.visible)
	Settings.set_value("gameplay/draw_gun", "right")


func _mouse() -> void:
	var yaw := func(accel: float) -> float:
		Settings.set_value("mouse/acceleration", accel)
		var before: float = _game._view.y
		_game._mouse = Vector2(100, 0)
		_game._mouse_move(1.0)
		return absf(_game._view.y - before)
	var plain: float = yaw.call(0.0)
	var fast: float = yaw.call(0.5)
	_check("Mouse: 100 counts at sensitivity 5 turn %.1f degrees (11)" % plain, absf(plain - 11.0) < 0.01)
	_check("Acceleration 0.5: the same swipe in 1 ms turns %.1f (121)" % fast, absf(fast - 121.0) < 0.01)


func _bodies() -> void:
	var ch := _bot_body()
	Settings.set_value("gameplay/bright_skins", true)
	var mi: MeshInstance3D = ch.body.find_children("*", "MeshInstance3D", true, false)[0] if ch else null
	var m := mi.get_surface_override_material(0) as StandardMaterial3D if mi else null
	_check("Bright skins: the bot is drawn in Enemy color", m != null and m.albedo_color == Settings.get_value("gameplay/enemy_color"))
	Settings.set_value("gameplay/bright_skins", false)
	_check("Bright skins off: its own materials again", mi != null and mi.get_surface_override_material(0) == null)
	Settings.set_value("gameplay/force_model", "keel")


func _effects() -> void:
	Settings.set_value("graphics/effect_intensity", 0.5)
	var r := Q3FX.explosion(_game.client, "rocket", Vector3.ZERO)
	var lights := r.find_children("*", "OmniLight3D", true, false)
	_check("Effect intensity 0.5: the rocket's light at half (%s)" % [lights.map(func(l): return l.light_energy)],
		lights.size() == 1 and absf(lights[0].light_energy - 1.25) < 0.01)
	var a := AudioStreamPlayer3D.new()
	Settings.set_value("audio/spatial", false)
	Settings.spatialize(a)
	var off := a.attenuation_filter_db
	Settings.set_value("audio/spatial", true)
	Settings.spatialize(a)
	_check("Spatial audio toggles the distance filter", off == 0.0 and a.attenuation_filter_db < 0.0)
	a.free()


func _speedometer() -> void:
	var sp: Q3HudSpeedometer = _game.hud.speedometer
	Settings.set_value("hud/show_speedometer", false)
	var off := sp.visible
	Settings.set_value("hud/show_speedometer", true)
	Settings.set_value("hud/speedometer", "compact")
	_check("Speedometer: off hides it, on shows it in its style", not off and sp.visible and sp.style == "compact")
	# a settings file from before the toggle, when Off was a style
	var cf := ConfigFile.new()
	cf.set_value("hud", "speedometer", "off")
	var old := OS.get_user_data_dir().path_join("speedometer_old.cfg")
	cf.save(old)
	Settings.load_from(old)
	DirAccess.remove_absolute(old)
	_check("an old Off style turns the speedometer off", Settings.get_value("hud/show_speedometer") == false)


func _bot_body() -> Q3Character:
	for num in _game.client._ents:
		var e: Dictionary = _game.client._ents[num]
		if e["kind"] == E.ET_PLAYER and e["node"] is Q3Character and is_instance_valid(e["node"]):
			return e["node"]
	return null


func _find(num: int) -> Dictionary:
	for it in Q3Server.items():
		if int(it["number"]) == num:
			return it
	return {}


func _tap(a: String) -> void:
	var ev := InputEventAction.new()
	ev.action = a
	ev.pressed = true
	Input.parse_input_event(ev)
	var up := ev.duplicate() as InputEventAction
	up.pressed = false
	Input.parse_input_event(up)


func _check(label: String, ok: bool) -> void:
	print(("ok   " if ok else "FAIL ") + label)
	_ok = _ok and ok
