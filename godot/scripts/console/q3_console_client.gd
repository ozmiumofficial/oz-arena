class_name Q3ConsoleClient
extends RefCounted
## Quake 3's client-side console commands and cvars: everything id 1.32 registered in the client
## (cl_*.c), the renderer (tr_init.c), cgame (cg_consolecmds.c), q3_ui (ui_atoms.c) and the win32
## layer, which the game engine here lacks because Godot is the client. Each one does what it did
## in Quake 3, through the remaster's own pieces:
##   +forward / +attack ...    the Controls actions (Input.action_press), +buttonN the usercmd bits,
##                             +left / +lookup ... turn at cl_yawspeed (CL_AdjustAngles)
##   bind / unbind / bindlist  the Controls binds themselves for commands they have (bind w +forward),
##                             otherwise a console bind run by Q3Console when the key goes down
##   cg_* / s_* / r_* cvars    the menu settings they correspond to; cl_yawspeed, cg_thirdPerson and
##                             the other client-only ones are kept here (user://console_cvars.cfg)
##   demo / record / connect   the replay library, the recorder, the hub's match directory
## Q3ConsoleCommands asks this layer after its own table. The cgame commands Team Arena added
## (#ifdef MISSIONPACK: the order menu, scoresUp ...) were never in baseq3, so they aren't here.

const E := preload("res://scripts/game/q3_enums.gd")

## +name: held Controls actions.
const HELD := {"forward": "q3_forward", "back": "q3_back", "moveleft": "q3_moveleft", "moveright": "q3_moveright",
	"moveup": "q3_jump", "movedown": "q3_crouch", "attack": "q3_attack", "speed": "q3_walk", "zoom": "q3_zoom",
	"scores": "q3_scores"}
## +name: CL_AdjustAngles / CL_KeyMove keys, read every frame by process() and Gameplay.
const LOOK := ["left", "right", "lookup", "lookdown", "strafe", "mlook"]
## Settings > Controls actions as the Quake 3 command each one is (bind w +forward lands on Move forward).
const ACTION_CMD := {"forward": "+forward", "back": "+back", "moveleft": "+moveleft", "moveright": "+moveright",
	"jump": "+moveup", "crouch": "+movedown", "walk": "+speed", "attack": "+attack", "zoom": "+zoom",
	"weapnext": "weapnext", "weapprev": "weapprev", "weapon1": "weapon 1", "weapon2": "weapon 2", "weapon3": "weapon 3",
	"weapon4": "weapon 4", "weapon5": "weapon 5", "weapon6": "weapon 6", "weapon7": "weapon 7", "weapon8": "weapon 8",
	"weapon9": "weapon 9", "use_item": "+button2", "gesture": "+button3", "scores": "+scores", "chat": "messagemode",
	"teamchat": "messagemode2", "screenshot": "screenshot", "console": "toggleconsole"}
## keys.c's names that aren't Godot's.
const KEY_NAMES := {"uparrow": "Up", "downarrow": "Down", "leftarrow": "Left", "rightarrow": "Right", "ins": "Insert",
	"del": "Delete", "pgup": "PageUp", "pgdn": "PageDown", "backspace": "BackSpace", "semicolon": "Semicolon",
	"capslock": "CapsLock", "enter": "Enter", "escape": "Escape", "space": "Space", "tab": "Tab", "alt": "Alt",
	"ctrl": "Ctrl", "shift": "Shift", "home": "Home", "end": "End", "pause": "Pause", "kp_enter": "Kp Enter",
	";": "Semicolon", ",": "Comma", ".": "Period", "-": "Minus", "=": "Equal", "[": "BracketLeft", "]": "BracketRight",
	"/": "Slash", "\\": "BackSlash", "'": "Apostrophe", "`": "QuoteLeft", "~": "QuoteLeft"}
const MOUSE_NAMES := {1: "MOUSE1", 2: "MOUSE2", 3: "MOUSE3", 8: "MOUSE4", 9: "MOUSE5", 4: "MWHEELUP", 5: "MWHEELDOWN"}

## Client cvars that are menu settings: lowercase name -> [shown name, setting key, kind]. Kinds:
## num, pct (0-1 in Quake 3, 0-100 here), bool, invert (m_pitch below 0), on:<value> (a bool whose
## Quake 3 "on" is that number), enum:<options by number>, autoswitch, rate.
const SETTING_CVARS := {
	"sensitivity": ["sensitivity", "mouse/sensitivity", "num"], "m_pitch": ["m_pitch", "mouse/invert", "invert"],
	"cl_mouseaccel": ["cl_mouseAccel", "mouse/acceleration", "num"],
	"cg_fov": ["cg_fov", "video/fov", "num"], "com_maxfps": ["com_maxfps", "video/max_fps", "num"],
	"r_gamma": ["r_gamma", "video/brightness", "num"], "r_swapinterval": ["r_swapInterval", "video/vsync", "enum:off,on"],
	"r_fullscreen": ["r_fullscreen", "video/display_mode", "enum:windowed,fullscreen"],
	"r_picmip": ["r_picmip", "graphics/texture_quality", "enum:high,medium,low"],
	"cg_drawfps": ["cg_drawFPS", "hud/fps", "bool"], "cg_lagometer": ["cg_lagometer", "hud/net_graph", "bool"],
	"cg_drawcrosshair": ["cg_drawCrosshair", "crosshair/shape", "enum:none,a,b,c,d,e,f,g,h,i,j"],
	"cg_crosshairsize": ["cg_crosshairSize", "crosshair/size", "num"],
	"cg_drawgun": ["cg_drawGun", "gameplay/draw_gun", "enum:hidden,right,center,left"],
	"cg_bobup": ["cg_bobup", "gameplay/view_bob", "on:0.005"], "cg_gibs": ["cg_gibs", "graphics/gibs", "bool"],
	"cg_marks": ["cg_marks", "graphics/decals", "bool"], "com_blood": ["com_blood", "graphics/blood", "enum:off,classic,heavy"],
	"cg_autoswitch": ["cg_autoswitch", "gameplay/autoswitch", "autoswitch"],
	"cg_teamchatsonly": ["cg_teamChatsOnly", "hud/team_chats_only", "bool"],
	"con_notifytime": ["con_notifytime", "hud/chat_time", "num"],
	"s_volume": ["s_volume", "audio/vol_effects", "pct"], "s_musicvolume": ["s_musicvolume", "audio/vol_music", "pct"],
	"rate": ["rate", "network/rate", "rate"], "name": ["name", "profile/name", "raw"],
}
## Client-only cvars kept by the console: lowercase name -> [shown name, default, archived, cheat].
## A cheat cvar keeps its default in a match other players share (cg_thirdPerson shows around corners).
const LOCAL_CVARS := {
	"cl_yawspeed": ["cl_yawspeed", "140", true, false], "cl_pitchspeed": ["cl_pitchspeed", "140", true, false],
	"cl_anglespeedkey": ["cl_anglespeedkey", "1.5", true, false],
	"cg_thirdperson": ["cg_thirdPerson", "0", false, true], "cg_thirdpersonrange": ["cg_thirdPersonRange", "40", false, true],
	"cg_thirdpersonangle": ["cg_thirdPersonAngle", "0", false, true], "cg_cameraorbit": ["cg_cameraOrbit", "0", false, true],
	"cg_cameraorbitdelay": ["cg_cameraOrbitDelay", "50", true, false], "cg_viewsize": ["cg_viewsize", "100", true, false],
	"rconpassword": ["rconPassword", "", false, false],
}
const RATES := {"slow": 25000, "medium": 50000, "fast": 100000, "lan": 1000000}

## ui_teamorders.c's orders (q3_ui's team orders menu sends them as team chat to a bot or everyone).
const TEAM_ORDERS := ["I am the leader", "Defend the base", "Follow me", "Get enemy flag", "Camp here", "Report",
	"I relinquish command"]

var out := Callable()               ## prints a line (Q3ConsoleCommands._out)
var run_line := Callable()          ## (text): runs console text (vstr-like: a bind's line)
var forward := Callable()           ## (text): a command for the game, sent next frame (Q3ConsoleCommands._forward)
var restricted := Callable()        ## () -> why the shared game is locked, or ""
var host_fn := Callable()           ## () -> the Gameplay scene or null
var request := Callable()           ## (what, arg): toggle, close, condump; things only the console can do
var viewport: Viewport
var binds_file := "user://console_binds.cfg"
var cvars_file := "user://console_cvars.cfg"

var buttons := 0                    ## +buttonN held: usercmd bits Gameplay adds (BUTTON_ATTACK is 1 << 0)
var _look := {}                     ## LOOK key -> held
var _local := {}                    ## LOCAL_CVARS values
var _binds: Dictionary = {}         ## console binds: event string ("key:W", "mouse:1") -> command line
var _loaded := false
var _down: Dictionary = {}          ## event string -> the line a key ran when it went down
var _orbit_ms := 0.0
var _browse_wanted := false
var _browsed: Array = []            ## the hub's last match list (globalservers), for connect by number
var _test: Node3D                   ## testmodel / testgun
var _test_gun := false
var _test_skin := 0
var _music: Q3Music                 ## music outside a match
var _rcon_ms := 0.0                 ## how much longer to print what a server's rcon answers


func _out(text: String) -> void:
	if out.is_valid():
		out.call(text)


func _host() -> Object:
	var h: Object = host_fn.call() if host_fn.is_valid() else null
	return h if h != null and is_instance_valid(h) else null


## The Gameplay scene with you in it, or null (and says so).
func _match() -> Object:
	var h := _host()
	if h == null or int(h.get("me")) < 0:
		_out("Not in a match.")
		return null
	return h


func _ask(what: String, arg: Variant = null) -> void:
	if request.is_valid():
		request.call(what, arg)


# ---------------------------------------------------------------- the list

## Every command this layer answers to, as id spelled them.
func command_names() -> Array:
	var names: Array = []
	for k in HELD.keys() + LOOK:
		names.append("+" + k)
		names.append("-" + k)
	for i in 15:
		names.append("+button%d" % i)
		names.append("-button%d" % i)
	names.append_array(["bind", "bindlist", "centerview", "cinematic", "clear", "clientinfo", "cmd", "condump",
		"configstrings", "connect", "demo", "disconnect", "fs_openedList", "fs_referencedList", "globalservers",
		"localservers", "messagemode", "messagemode2", "messagemode3", "messagemode4", "model", "music", "ping", "play",
		"rcon", "reconnect", "record", "s_info", "s_list", "s_stop", "serverstatus", "showip", "snd_restart",
		"stoprecord", "toggleconsole", "unbind", "unbindall", "vid_restart",
		"gfxinfo", "imagelist", "modelist", "modellist", "screenshot", "screenshotJPEG", "shaderlist", "skinlist",
		"in_restart", "midiinfo",
		"levelselect", "postgame", "ui_cache", "ui_cinematics", "ui_teamOrders", "iamacheater", "iamamonkey", "ui_cdkey",
		"testgun", "testmodel", "nextframe", "prevframe", "nextskin", "prevskin", "viewpos", "sizeup", "sizedown",
		"weapnext", "weapprev", "weapon", "tell_target", "tell_attacker", "vtell_target", "vtell_attacker", "tcmd",
		"startOrbit", "loaddeferred", "loaddefered"])
	return names


func has_command(name: String) -> bool:
	for n in command_names():
		if String(n).to_lower() == name:
			return true
	return false


## Runs one (`name` lowercase; `text` the whole command for the ones that pass their arguments on).
func run(name: String, args: PackedStringArray, text: String) -> void:
	if name.begins_with("+") or name.begins_with("-"):
		_key(name)
		return
	var rest := text.strip_edges().substr(text.strip_edges().find(" ") + 1).strip_edges() if text.strip_edges().contains(" ") else ""
	match name:
		"bind": _bind(args)
		"unbind": _unbind(args)
		"unbindall": _unbindall()
		"bindlist": _bindlist()
		"centerview": _centerview()
		"cinematic": _cinematic(args)
		"clear": _ask("clear")
		"clientinfo": _clientinfo()
		"cmd": _cmd(rest)
		"condump":
			if args.is_empty():
				_out("usage: condump <filename>")
			else:
				_ask("condump", args[0])
		"configstrings": _configstrings()
		"connect": _connect(args)
		"demo": _demo(args)
		"disconnect": _disconnect()
		"fs_openedlist": _paks(false)
		"fs_referencedlist": _paks(true)
		"globalservers", "localservers": _browse()
		"messagemode": _message("say")
		"messagemode2": _message("team")
		"messagemode3": _message("target")
		"messagemode4": _message("attacker")
		"model": _model(args)
		"music": _play_music(args)
		"ping": _ping(args)
		"play": _play(args)
		"rcon": _rcon(rest)
		"reconnect": _reconnect()
		"record": _record(args)
		"stoprecord": _stoprecord()
		"s_info": _s_info()
		"s_list": _s_list()
		"s_stop": _s_stop()
		"serverstatus": _serverstatus(args)
		"showip": _showip()
		"snd_restart": _snd_restart()
		"toggleconsole": _ask("toggle")
		"vid_restart": _vid_restart()
		"gfxinfo": _gfxinfo()
		"imagelist": _imagelist()
		"modelist": _modelist()
		"modellist": _modellist()
		"screenshot": _screenshot("png", args)
		"screenshotjpeg": _screenshot("jpg", args)
		"shaderlist": _shaderlist()
		"skinlist": _skinlist()
		"in_restart": release_all()
		"midiinfo": _midiinfo()
		"levelselect": _menu_page("campaign")
		"ui_cinematics": _menu_page("cinematics")
		"postgame": _out("postgame is what the game sends at the end of a single-player match; the campaign reads it.")
		"ui_cache": _out("Nothing to cache: the menu's art loads with the menu.")
		"ui_teamorders": _team_orders()
		"iamacheater": _unlock_campaign()
		"iamamonkey": _out("^3iamamonkey^7 would hand out every medal, and medals earn achievements and card parts here, so it's off.")
		"ui_cdkey": _cdkey()
		"testmodel": _testmodel(args, false)
		"testgun": _testmodel(args, true)
		"nextframe": _test_frame(1)
		"prevframe": _test_frame(-1)
		"nextskin": _test_skin_step(1)
		"prevskin": _test_skin_step(-1)
		"viewpos": _viewpos()
		"sizeup": _viewsize(10)
		"sizedown": _viewsize(-10)
		"weapnext": _cycle(1)
		"weapprev": _cycle(-1)
		"weapon": _weapon(args)
		"tell_target": _tell(crosshair_player(), "tell", rest)
		"tell_attacker": _tell(last_attacker(), "tell", rest)
		"vtell_target": _tell(crosshair_player(), "vtell", rest)
		"vtell_attacker": _tell(last_attacker(), "vtell", rest)
		"tcmd": _tcmd(args)
		"startorbit": _start_orbit()
		"loaddeferred", "loaddefered": pass   # every player model is loaded as it's needed: nothing waits


# ---------------------------------------------------------------- keys held by commands

func _key(name: String) -> void:
	var down := name.begins_with("+")
	var k := name.substr(1)
	if HELD.has(k):
		if down:
			Input.action_press(HELD[k])
		else:
			Input.action_release(HELD[k])
	elif LOOK.has(k):
		_look[k] = down
	elif k.begins_with("button") and k.substr(6).is_valid_int() and int(k.substr(6)) < 15:
		var bit := 1 << int(k.substr(6))
		buttons = (buttons | bit) if down else (buttons & ~bit)


func held(k: String) -> bool:
	return bool(_look.get(k, false))


## Lets go of everything a command or a bound key is holding (in_restart, the match ending).
func release_all() -> void:
	for a in HELD.values():
		Input.action_release(a)
	_look.clear()
	buttons = 0
	_down.clear()


## CL_KeyMove's +strafe: +left / +right move sideways instead of turning. -1 .. 1.
func strafe_side() -> float:
	if not held("strafe"):
		return 0.0
	return (1.0 if held("right") else 0.0) - (1.0 if held("left") else 0.0)


## Every frame: a server's rcon answer, CL_AdjustAngles for the turning keys, cg_cameraOrbit's swing.
func process(delta: float) -> void:
	if _rcon_ms > 0.0:
		_rcon_ms -= delta * 1000.0
		var said := String(ClassDB.class_call_static("Q3Net", "rcon_output"))
		for l in said.rstrip("\n").split("\n", false):
			_out(l)
	var h := _host()
	if h == null:
		return
	var turning := (held("left") or held("right")) and not held("strafe")
	if turning or held("lookup") or held("lookdown"):
		var speed := delta * (cvar_float("cl_anglespeedkey") if Input.is_action_pressed("q3_walk") else 1.0)
		var v: Vector3 = h.get("_view")
		if not held("strafe"):
			v.y += speed * cvar_float("cl_yawspeed") * ((1.0 if held("left") else 0.0) - (1.0 if held("right") else 0.0))
		v.x += speed * cvar_float("cl_pitchspeed") * ((1.0 if held("lookdown") else 0.0) - (1.0 if held("lookup") else 0.0))
		h.set("_view", v)
	var orbit := cvar_float("cg_cameraorbit")
	if orbit != 0.0:
		_orbit_ms += delta * 1000.0
		var step := maxf(cvar_float("cg_cameraorbitdelay"), 1.0)
		while _orbit_ms >= step:
			_orbit_ms -= step
			_local["cg_thirdpersonangle"] = str(fmod(cvar_float("cg_thirdpersonangle") + orbit, 360.0))


# ---------------------------------------------------------------- bind

## keys.c's key names to Settings' event strings ("key:W", "mouse:1"); "" if it isn't a key.
static func parse_key(name: String) -> String:
	var n := name.to_lower()
	for b in MOUSE_NAMES:
		if MOUSE_NAMES[b].to_lower() == n:
			return "mouse:%d" % b
	if n.begins_with("joy") and n.substr(3).is_valid_int() and int(n.substr(3)) >= 1:
		return "joy:%d" % (int(n.substr(3)) - 1)
	var s := String(KEY_NAMES.get(n, name))
	var code := OS.find_keycode_from_string(s)
	if code == KEY_NONE:
		code = OS.find_keycode_from_string(s.capitalize())
	if code == KEY_NONE:
		return ""
	return "key:" + OS.get_keycode_string(code)


## An event string as keys.c would name it (Key_KeynumToString): "w", "SPACE", "MOUSE1".
static func key_name(ev: String) -> String:
	var kind := ev.get_slice(":", 0)
	var v := ev.get_slice(":", 1)
	if kind == "mouse":
		return String(MOUSE_NAMES.get(int(v), "MOUSE%s" % v))
	if kind == "joy":
		return "JOY%d" % (int(v) + 1)
	if kind == "key":
		if v.length() == 1:
			return v.to_lower()
		for q in KEY_NAMES:
			if String(KEY_NAMES[q]) == v and String(q).length() > 1:
				return String(q).to_upper()
		return v.to_upper()
	return ev


static func _norm(cmd: String) -> String:
	return " ".join(Q3ConsoleCommands.tokenize(cmd)).to_lower()


## The Controls action a command line is, or "".
static func action_for(cmd: String) -> String:
	var c := _norm(cmd)
	for a in ACTION_CMD:
		if ACTION_CMD[a] == c:
			return a
	return ""


## What a key runs: a Controls action's command, or its console bind, or "".
func binding(ev: String) -> String:
	_load()
	for a in ACTION_CMD:
		var v: Variant = Settings.get_value("controls/" + a)
		if v is Array and (v as Array).has(ev):
			return ACTION_CMD[a]
	return String(_binds.get(ev, ""))


## The console bind a key event runs (Controls binds run themselves through the InputMap), or "".
func bound(ev: InputEvent) -> String:
	_load()
	return String(_binds.get(Settings.event_to_string(ev), ""))


func pressed(ev: InputEvent, line: String) -> void:
	_down[Settings.event_to_string(ev)] = line


## The line a key ran when it went down, once (its "-" halves are due now).
func released(ev: InputEvent) -> String:
	var k := Settings.event_to_string(ev)
	var line := String(_down.get(k, ""))
	_down.erase(k)
	return line


## Takes a key off every Controls action and the console binds (Q3: a key runs one command).
func _clear_key(ev: String) -> void:
	for a in ACTION_CMD:
		var v: Variant = Settings.get_value("controls/" + a)
		if v is Array:
			for slot in (v as Array).size():
				if v[slot] == ev:
					Settings.set_bind("controls/" + a, slot, "")
	_binds.erase(ev)


func _bind(args: PackedStringArray) -> void:
	_load()
	if args.is_empty():
		_out("bind <key> [command] : attach a command to a key")
		return
	var ev := parse_key(args[0])
	if ev == "":
		_out("\"%s\" isn't a valid key" % args[0])
		return
	if args.size() == 1:
		var now := binding(ev)
		_out("\"%s\" = \"%s\"" % [args[0], now] if now != "" else "\"%s\" is not bound" % args[0])
		return
	var line := " ".join(args.slice(1))
	_clear_key(ev)
	# a Controls action's command lands in a free slot of that action; with its slots taken (Quake 3
	# had no limit) it's a console bind, which runs the same command, so no key you had is lost
	var action := action_for(line)
	var slot := -1
	if action != "":
		var v: Array = (Settings.get_value("controls/" + action) as Array)
		for s in ([2] if ev.begins_with("joy") else [0, 1]):
			if slot < 0 and String(v[s]) == "":
				slot = s
	if slot >= 0:
		Settings.set_bind("controls/" + action, slot, ev)
	else:
		_binds[ev] = line
	_save_binds()


func _unbind(args: PackedStringArray) -> void:
	_load()
	if args.is_empty():
		_out("unbind <key> : remove commands from a key")
		return
	var ev := parse_key(args[0])
	if ev == "":
		_out("\"%s\" isn't a valid key" % args[0])
		return
	_clear_key(ev)
	_save_binds()


## Clears every bind, as Quake 3's does; the console key stays, as Quake 3's was never a bind.
func _unbindall() -> void:
	_load()
	for a in ACTION_CMD:
		if a == "console":
			continue
		var v: Variant = Settings.get_value("controls/" + a)
		if v is Array:
			for slot in (v as Array).size():
				if v[slot] != "":
					Settings.set_bind("controls/" + a, slot, "")
	_binds.clear()
	_save_binds()


func bind_lines() -> PackedStringArray:
	_load()
	var lines := PackedStringArray()
	for a in ACTION_CMD:
		var v: Variant = Settings.get_value("controls/" + a)
		if v is Array:
			for ev in v:
				if ev != "":
					lines.append("%s \"%s\"" % [key_name(ev), ACTION_CMD[a]])
	for ev in _binds:
		lines.append("%s \"%s\"" % [key_name(ev), _binds[ev]])
	return lines


func _bindlist() -> void:
	for l in bind_lines():
		_out(l)


# ---------------------------------------------------------------- cvars

func is_cvar(name: String) -> bool:
	var n := name.to_lower()
	return SETTING_CVARS.has(n) or LOCAL_CVARS.has(n)


## Shown names of every client cvar.
func cvar_names() -> Array:
	var names: Array = []
	for n in SETTING_CVARS:
		names.append(SETTING_CVARS[n][0])
	for n in LOCAL_CVARS:
		names.append(LOCAL_CVARS[n][0])
	return names


func cvar_get(name: String) -> String:
	var n := name.to_lower()
	if LOCAL_CVARS.has(n):
		_load()
		if LOCAL_CVARS[n][3] and restricted.is_valid() and String(restricted.call()) != "":
			return LOCAL_CVARS[n][1]
		return String(_local.get(n, LOCAL_CVARS[n][1]))
	var spec: Array = SETTING_CVARS[n]
	return _as_cvar(spec[2], Settings.get_value(spec[1]))


## A setting's value as its Quake 3 cvar reads.
static func _as_cvar(kind: String, v: Variant) -> String:
	match kind:
		"bool": return "1" if v else "0"
		"invert": return "-0.022" if v else "0.022"
		"pct": return _num(float(v) / 100.0)
		"autoswitch": return "0" if v == "never" else "1"
		"rate": return str(RATES.get(v, 25000))
	if kind.begins_with("on:"):
		return kind.substr(3) if v else "0"
	if kind.begins_with("enum:"):
		return str(maxi(kind.substr(5).split(",").find(str(v)), 0))
	if v is float or v is int:
		return _num(float(v))
	return str(v)


func cvar_float(name: String) -> float:
	return cvar_get(name).to_float()


func cvar_default(name: String) -> String:
	var n := name.to_lower()
	if LOCAL_CVARS.has(n):
		return LOCAL_CVARS[n][1]
	var spec: Array = SETTING_CVARS[n]
	return _as_cvar(spec[2], Settings.default_of(spec[1]))


## Sets one; returns what it reads back, or "" with a reason printed if it can't be set now.
func cvar_set(name: String, text: String) -> String:
	var n := name.to_lower()
	if LOCAL_CVARS.has(n):
		_load()
		var why := String(restricted.call()) if restricted.is_valid() else ""
		if LOCAL_CVARS[n][3] and why != "":
			_out("^1%s is locked: %s." % [LOCAL_CVARS[n][0], why])
			return ""
		_local[n] = text
		_save_cvars()
		return cvar_get(n)
	var spec: Array = SETTING_CVARS[n]
	var kind: String = spec[2]
	var value: Variant
	if kind == "bool" or kind.begins_with("on:"):
		value = text.to_float() != 0.0 if text.is_valid_float() else text.to_lower() in Q3ConsoleCommands.TRUE_WORDS
	elif kind == "invert":
		value = text.to_float() < 0.0
	elif kind == "pct":
		value = roundi(clampf(text.to_float(), 0.0, 1.0) * 100.0)
	elif kind == "autoswitch":
		value = "never" if text.to_float() == 0.0 else ("always" if Settings.get_value(spec[1]) == "never" else Settings.get_value(spec[1]))
	elif kind == "rate":
		value = "slow"
		for r in RATES:
			if text.to_int() >= RATES[r]:
				value = r
	elif kind.begins_with("enum:"):
		var opts := kind.substr(5).split(",")
		value = opts[clampi(text.to_int(), 0, opts.size() - 1)]
	elif kind == "num":
		value = text.to_float()
	else:
		value = text
	Settings.set_value(spec[1], value)
	return cvar_get(n)


## Archived client cvars as "seta" lines, for writeconfig.
func archived_lines() -> PackedStringArray:
	_load()
	var lines := PackedStringArray()
	for n in SETTING_CVARS:
		lines.append("seta %s \"%s\"" % [SETTING_CVARS[n][0], cvar_get(n)])
	for n in LOCAL_CVARS:
		if LOCAL_CVARS[n][2]:
			lines.append("seta %s \"%s\"" % [LOCAL_CVARS[n][0], cvar_get(n)])
	return lines


static func _num(f: float) -> String:
	return str(int(f)) if is_equal_approx(f, roundf(f)) else String.num(f, 4)


func _load() -> void:
	if _loaded:
		return
	_loaded = true
	if FileAccess.file_exists(binds_file):
		for l in FileAccess.get_file_as_string(binds_file).split("\n", false):
			var i := l.find("|")
			if i > 0:
				_binds[l.left(i)] = l.substr(i + 1).strip_edges()
	if FileAccess.file_exists(cvars_file):
		for l in FileAccess.get_file_as_string(cvars_file).split("\n", false):
			var i := l.find("|")
			if i > 0 and LOCAL_CVARS.has(l.left(i)):
				_local[l.left(i)] = l.substr(i + 1).strip_edges()


func _save_binds() -> void:
	var f := FileAccess.open(binds_file, FileAccess.WRITE)
	if f:
		for ev in _binds:
			f.store_line("%s|%s" % [ev, _binds[ev]])


func _save_cvars() -> void:
	var f := FileAccess.open(cvars_file, FileAccess.WRITE)
	if f:
		for n in _local:
			if LOCAL_CVARS[n][2]:
				f.store_line("%s|%s" % [n, _local[n]])


## Tests point the files elsewhere and start over.
func reload() -> void:
	_loaded = false
	_binds.clear()
	_local.clear()


# ---------------------------------------------------------------- the client

func _centerview() -> void:
	var h := _host()
	if h != null:
		var v: Vector3 = h.get("_view")
		h.set("_view", Vector3(0.0, v.y, v.z))


func _cinematic(args: PackedStringArray) -> void:
	if args.is_empty():
		_out("cinematic <name>")
		return
	var want := args[0].get_file().get_basename().to_lower()
	for c in Session.cinematics:
		if String(c["id"]) == want:
			Session.play_cinematics(PackedStringArray([want]))
			return
	_out("No cinematic \"%s\". There are: %s" % [args[0], ", ".join(Session.cinematics.map(func(c): return String(c["id"])))])


## CL_Clientinfo_f.
func _clientinfo() -> void:
	var h := _host()
	_out("--------- Client Information ---------")
	_out("State: %s" % ("active" if h != null and int(h.get("me")) >= 0 else "disconnected"))
	_out("Server: %s" % ("someone else's match" if h != null and h.get("net_feed") != null else ("this machine" if h != null else "")))
	_out("User info settings:")
	var player: Dictionary = Session.setup.player if Session.setup != null else {
		"name": Settings.get_value("profile/name"), "model": Settings.get_value("profile/model"), "skin": Settings.get_value("profile/skin")}
	var parts := Q3Host.userinfo(player).split("\\", false)
	for i in range(0, parts.size() - 1, 2):
		_out("%-20s%s" % [parts[i], parts[i + 1]])
	_out("--------------------------------------")


## cmd: the rest of the line to the server as it is (CL_ForwardToServer_f).
func _cmd(rest: String) -> void:
	var h := _match()
	if h != null and rest != "":
		forward.call(rest)


func _configstrings() -> void:
	var h := _host()
	var feed: Object = h.get("feed") if h != null else null
	if feed == null:
		_out("Not connected to a server.")
		return
	for i in 1024:
		var s := String(feed.call("configstring", i))
		if s != "":
			_out("%4d: %s" % [i, s])


## connect <host id | number from globalservers> [password]: the hub's directory joins over WebRTC.
func _connect(args: PackedStringArray) -> void:
	if args.is_empty():
		_out("usage: connect <match number from globalservers, or a host's id> [password]")
		return
	if not Online.enabled():
		_out("^1Online play is off (Settings > Network > Online services).")
		return
	var host := args[0].trim_prefix("match:")
	if host.is_valid_int() and int(host) >= 1 and int(host) <= _browsed.size():
		host = String(_browsed[int(host) - 1].get("host", ""))
	_out("Connecting to %s..." % host)
	Online.net_join(host, args[1] if args.size() > 1 else "")


func _demo(args: PackedStringArray) -> void:
	if args.is_empty():
		_out("demo <demoname>")
		for r in Q3ReplayFile.list().slice(0, 20):
			_out("  " + String(r.get("file", "")).get_basename())
		return
	var want := args[0].get_file().get_basename().to_lower()
	for r in Q3ReplayFile.list():
		if String(r.get("file", "")).get_basename().to_lower() == want:
			Session.watch_replay(String(r["path"]), 0, "")
			return
	# a demo inside the paks (pak8's demos/four.dm_68), copied out for the replay player
	var data := _engine_file("demos/%s.dm_68" % want)
	if not data.is_empty():
		DirAccess.make_dir_recursive_absolute(Q3ReplayFile.dir)
		var path := Q3ReplayFile.dir.path_join(want + ".dm_68")
		var f := FileAccess.open(path, FileAccess.WRITE)
		if f:
			f.store_buffer(data)
			f.close()
			Session.watch_replay(path, 0, "")
			return
	_out("couldn't open %s" % args[0])


func _disconnect() -> void:
	if _host() == null:
		_out("Not connected to a server.")
		return
	Session.to_main_menu()


func _paks(referenced: bool) -> void:
	if not ClassDB.class_has_method("Q3Server", "pak_names"):
		_out("^1This q3core build can't list the paks.")
		return
	var names := String(ClassDB.class_call_static("Q3Server", "pak_names", referenced))
	_out(("Referenced PK3 Names: " if referenced else "Opened PK3 Names: ") + names)


func _browse() -> void:
	if not Online.enabled():
		_out("^1Online play is off (Settings > Network > Online services).")
		return
	if not _browse_wanted:
		_browse_wanted = true
		Online.matches.connect(_on_matches, CONNECT_ONE_SHOT)
	if not Online.net_browse():
		_out("^1Not connected to the hub.")
		return
	_out("Asking the hub for matches...")


func _on_matches(m: Dictionary) -> void:
	_browse_wanted = false
	_browsed = m.get("matches", [])
	_out("%d matches (%s first):" % [_browsed.size(), m.get("region", "your region")])
	for i in _browsed.size():
		var l: Dictionary = _browsed[i]
		_out("%3d  %-28s %-10s %-10s %d/%d %s%s" % [i + 1, Q3Colors.strip(String(l.get("name", ""))).left(28), l.get("map", ""),
			l.get("mode", ""), int(l.get("players", 0)), int(l.get("max", 0)), l.get("region", ""),
			"  password" if l.get("password", false) else ""])
	if _browsed.size():
		_out("connect <number> joins one.")


## messagemode 1-4 (CL_MessageMode_f): chat to everyone, the team, the crosshair target, the attacker.
func _message(to: String) -> void:
	var h := _match()
	if h == null:
		return
	var chat: Object = h.get("hud").get("chat") if h.get("hud") != null else null
	if chat == null:
		return
	var target := -1
	if to == "target" or to == "attacker":
		target = crosshair_player() if to == "target" else last_attacker()
		if target < 0:
			_out("No %s to talk to." % ("player under the crosshair" if to == "target" else "attacker"))
			return
	_ask("close")
	chat.set("tell_to", target)
	chat.call_deferred("open", to == "team")


func _model(args: PackedStringArray) -> void:
	if args.is_empty():
		_out("model is:\"%s/%s^7\"" % [Settings.get_value("profile/model"), Settings.get_value("profile/skin")])
		return
	var parts := args[0].to_lower().split("/")
	if not Q3.characters.has(parts[0]):
		_out("No player model \"%s\"." % parts[0])
		return
	Settings.set_value("profile/model", parts[0])
	Settings.set_value("profile/skin", parts[1] if parts.size() > 1 else "default")


func _play_music(args: PackedStringArray) -> void:
	if args.is_empty():
		_out("music <musicfile> [loopfile]")
		return
	var h := _host()
	var m: Q3Music = h.get("music") if h != null else null
	if m == null:
		if _music == null:
			_music = Q3Music.new()
			viewport.get_tree().root.add_child(_music)
		m = _music
		_menu_music_paused(true)   # the menu's shuffle steps aside, as it does for a cinematic
	m.play(" ".join(args))


func _menu_music_paused(on: bool) -> void:
	for n in viewport.get_tree().get_nodes_in_group("menu_music"):
		if n.has_method("set_paused"):
			n.call("set_paused", on)


func _ping(args: PackedStringArray) -> void:
	_out("ping: matches are reached through the hub and Cloudflare's relay, so there's no address to ping.%s" % (
		" The scoreboard shows each player's ping." if _host() != null else " Play > Browse matches shows them by region."))
	if args.size():
		_out("(%s)" % args[0])


func _play(args: PackedStringArray) -> void:
	if args.is_empty():
		_out("play <soundfile> [soundfile] ...")
		return
	for a in args:
		var p := a.replace("\\", "/").trim_prefix("sound/")
		if p.get_extension() == "":
			p += ".wav"
		var res := "res://library/sounds/" + p
		if not ResourceLoader.exists(res):
			_out("^3can't find sound/%s" % p)
			continue
		var player := AudioStreamPlayer.new()
		player.stream = load(res)
		player.bus = &"Effects"
		player.finished.connect(player.queue_free)
		viewport.get_tree().root.add_child(player)
		player.play()


## CL_Rcon_f: the command and rconPassword to the server you joined (a dedicated server's
## rconpassword); its console's answer prints as the packets come back.
func _rcon(rest: String) -> void:
	var h := _host()
	if h == null or h.get("net_feed") == null or not ClassDB.class_exists("Q3Net"):
		_out("You must be connected to issue rcon commands (on your own match, type them here directly).")
		return
	if cvar_get("rconPassword") == "":
		_out("You must set 'rconPassword' before issuing an rcon command.")
		return
	ClassDB.class_call_static("Q3Net", "rcon", cvar_get("rconPassword"), rest)
	_rcon_ms = 5000.0


func _reconnect() -> void:
	if Session.setup == null:
		_out("Can't reconnect to localhost.")
		return
	Session.start_match(Session.setup)


func _record(args: PackedStringArray) -> void:
	var h := _match()
	if h == null:
		return
	if h.get("recorder") != null:
		_out("Already recording.")
		return
	h.call("_start_recording")
	if h.get("recorder") != null:
		_out("recording to %s." % (args[0] if args.size() else "the replay library"))
	else:
		_out("^1Couldn't start recording.")


func _stoprecord() -> void:
	var h := _match()
	if h == null:
		return
	if h.get("recorder") == null:
		_out("Not recording a demo.")
		return
	h.call("_finish_recording", {"ended": "stoprecord"})
	_out("Stopped demo. It's in Play > Replays.")


func _s_info() -> void:
	_out("----- Sound Info -----")
	_out("%5d mix rate" % AudioServer.get_mix_rate())
	_out("%5.0f ms output latency" % (AudioServer.get_output_latency() * 1000.0))
	_out("      device: %s" % AudioServer.output_device)
	for i in AudioServer.bus_count:
		_out("      bus %s: %.1f dB%s" % [AudioServer.get_bus_name(i), AudioServer.get_bus_volume_db(i), " (muted)" if AudioServer.is_bus_mute(i) else ""])
	_out("----------------------")


func _s_list() -> void:
	var n := 0
	for p in _players():
		var s: AudioStream = p.get("stream")
		if s != null:
			_out("%s %s" % ["playing" if p.call("is_playing") else "       ", s.resource_path.trim_prefix("res://library/")])
			n += 1
	_out("Total sounds: %d" % n)


func _players() -> Array:
	if viewport == null:
		return []
	var all: Array = viewport.get_tree().root.find_children("*", "AudioStreamPlayer", true, false)
	all.append_array(viewport.get_tree().root.find_children("*", "AudioStreamPlayer3D", true, false))
	return all


func _s_stop() -> void:
	for p in _players():
		p.call("stop")
	if _music != null:
		_music.stop()
		_menu_music_paused(false)


## The match on its own, or one from the last globalservers by number (CL_ServerStatus_f).
func _serverstatus(args: PackedStringArray) -> void:
	if args.size():
		var i := args[0].to_int()
		if i < 1 or i > _browsed.size():
			_out("serverstatus <number from globalservers>")
			return
		for k in _browsed[i - 1]:
			_out("%-16s%s" % [k, _browsed[i - 1][k]])
		return
	var h := _host()
	if h == null or not ClassDB.class_exists("Q3Server") or not Q3Server.running():
		_out("Not connected to a server.")
		return
	_out("Server settings:")
	for k in ["sv_hostname", "mapname", "g_gametype", "fraglimit", "timelimit", "capturelimit", "sv_maxclients", "g_ruleset", "version"]:
		_out("%-16s%s" % [k, Q3Server.cvar(k)])
	_out("Players:")
	var rows: Array = h.call("standings") if h.has_method("standings") else []
	_out("num score ping name")
	for r in rows:
		_out("%3d %5d %4d %s" % [int(r.get("client", 0)), int(r.get("score", 0)), int(r.get("ping", 0)), r.get("name", "")])


## NET_ShowIP: this machine's own addresses. Other players never see them; matches go through the relay.
func _showip() -> void:
	for a in IP.get_local_addresses():
		if not String(a).begins_with("fe80") and String(a) != "::1":
			_out("IP: %s" % a)


func _snd_restart() -> void:
	_s_stop()
	for k in ["audio/vol_master", "audio/vol_music", "audio/vol_effects", "audio/vol_voice", "audio/vol_announcer", "audio/vol_ui", "audio/output_device"]:
		if Settings.has(k):
			Settings._apply(k, Settings.get_value(k))
	_out("Sound restarted.")


## vid_restart: every video and graphics setting applied again (the match keeps going; Quake 3 reloaded).
func _vid_restart() -> void:
	for e in Settings.entries:
		var k := String(e["key"])
		if k.begins_with("video/") or k.begins_with("graphics/"):
			Settings._apply(k, Settings.get_value(k))
	Settings.environment_changed.emit()
	_out("Video restarted.")


func _gfxinfo() -> void:
	_out("GL_VENDOR: %s" % RenderingServer.get_video_adapter_vendor())
	_out("GL_RENDERER: %s" % RenderingServer.get_video_adapter_name())
	_out("GL_VERSION: %s (%s)" % [RenderingServer.get_video_adapter_api_version(), ProjectSettings.get_setting("rendering/rendering_device/driver", "")])
	var size := DisplayServer.window_get_size()
	_out("MODE: %d x %d %s" % [size.x, size.y, Settings.get_value("video/display_mode")])
	_out("VSYNC: %s, max fps %s" % [Settings.get_value("video/vsync"), Settings.get_value("video/max_fps")])
	_out("video memory: %d MB" % int(Performance.get_monitor(Performance.RENDER_VIDEO_MEM_USED) / 1048576.0))


## R_ImageList_f: every image the scene's materials draw with, and the texture memory in use.
func _imagelist() -> void:
	var seen := {}
	if viewport:
		for gi in viewport.get_tree().root.find_children("*", "GeometryInstance3D", true, false):
			var mats: Array = [(gi as GeometryInstance3D).material_override]
			if gi is MeshInstance3D and (gi as MeshInstance3D).mesh:
				var mi := gi as MeshInstance3D
				for s in mi.mesh.get_surface_count():
					mats.append(mi.get_active_material(s))
			for m in mats:
				_textures_of(m, seen)
	var paths := seen.keys()
	paths.sort()
	for p in paths:
		var t: Texture2D = seen[p]
		_out("%5d x %-5d %s" % [t.get_width(), t.get_height(), String(p).trim_prefix("res://")])
	_out(" ---------")
	_out(" %d total images, %.1f MB of textures" % [paths.size(),
		RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TEXTURE_MEM_USED) / 1048576.0])


func _textures_of(m: Material, seen: Dictionary) -> void:
	if m == null:
		return
	if m is BaseMaterial3D:
		var b := m as BaseMaterial3D
		for t in [b.albedo_texture, b.normal_texture, b.orm_texture, b.emission_texture, b.roughness_texture, b.metallic_texture]:
			if t != null and t.resource_path != "":
				seen[t.resource_path] = t
	elif m is ShaderMaterial and (m as ShaderMaterial).shader != null:
		var sm := m as ShaderMaterial
		for u in sm.shader.get_shader_uniform_list():
			var v: Variant = sm.get_shader_parameter(String(u["name"]))
			if v is Texture2D and (v as Texture2D).resource_path != "":
				seen[(v as Texture2D).resource_path] = v
	_textures_of(m.next_pass, seen)


func _modelist() -> void:
	_out("Display modes (Settings > Video > Resolution):")
	var opts: Array = Settings.entry("video/resolution").get("options", [])
	for i in opts.size():
		_out("Mode %2d: %s" % [i, opts[i][1] if opts[i] is Array else str(opts[i])])


func _modellist() -> void:
	var models: Array = Q3.get("_held_models").keys()
	for m in models:
		_out(String(m).trim_prefix(Q3.LIB))
	_out("%d total models" % models.size())


func _shaderlist() -> void:
	var seen := {}
	var h := _host()
	var root: Node = h.get("map_scene") if h != null else null
	if root == null:
		_out("No map loaded.")
		return
	for mi in root.find_children("*", "MeshInstance3D", true, false):
		var mesh: Mesh = (mi as MeshInstance3D).mesh
		if mesh == null:
			continue
		for s in mesh.get_surface_count():
			var m := mesh.surface_get_material(s)
			if m != null and m.resource_path != "":
				seen[m.resource_path.trim_prefix(Q3.LIB)] = true
	for k in seen.keys():
		_out(k)
	_out("%d total shaders" % seen.size())


func _skinlist() -> void:
	var n := 0
	for m in Q3.get("_held_models").keys():
		if String(m).contains("/characters/"):
			_out(String(m).get_file().get_basename())
			n += 1
	_out("%d skins" % n)


func _screenshot(ext: String, args: PackedStringArray) -> void:
	if viewport == null:
		return
	DirAccess.make_dir_recursive_absolute("user://screenshots")
	var name := args[0].get_basename() if args.size() and args[0] != "silent" else "shot-%s" % Time.get_datetime_string_from_system().replace(":", "-")
	var path := "user://screenshots/%s.%s" % [name, ext]
	var img := viewport.get_texture().get_image()
	if img == null or img.is_empty():
		_out("^1Couldn't read the screen.")
		return
	if (img.save_jpg(path, 0.9) if ext == "jpg" else img.save_png(path)) == OK:
		if not args.has("silent"):
			_out("Wrote %s" % ProjectSettings.globalize_path(path))
	else:
		_out("^1Couldn't write %s" % path)


func _midiinfo() -> void:
	OS.open_midi_inputs()
	var inputs := OS.get_connected_midi_inputs()
	_out("%d MIDI inputs" % inputs.size())
	for i in inputs:
		_out("  " + i)


func _menu_page(page: String) -> void:
	Session.to_main_menu()
	Session.menu_page = page   # the menu opens there (main_menu.gd reads it once)


func _team_orders() -> void:
	_out("Team orders (q3_ui): say_team \"<bot name> <order>\", or the order alone for every bot:")
	for o in TEAM_ORDERS:
		_out("  " + o)


## q3_ui UI_SPUnlock_f: every single-player arena playable. Nothing is marked won, so no XP comes of it.
func _unlock_campaign() -> void:
	Session.campaign.progress["unlocked"] = true
	Session.campaign.save()
	_out("All levels unlocked.")


func _cdkey() -> void:
	var dir := Q3Ownership.verified()
	_out("The game files are verified from %s" % dir if dir != "" else "^1No verified game files. The game asks for one at start-up.")


# ---------------------------------------------------------------- cgame

## cg_consolecmds.c CG_Viewpos_f.
func _viewpos() -> void:
	var h := _match()
	if h == null:
		return
	var ps: Dictionary = h.get("feed").call("player_state", int(h.get("me")))
	var o: Vector3 = ps.get("origin", Vector3.ZERO)
	var v: Vector3 = h.get("_view")
	_out("(%d %d %d) : %d" % [int(o.x), int(o.y), int(o.z), int(v.y)])


func _viewsize(step: int) -> void:
	_local["cg_viewsize"] = str(clampi(int(cvar_float("cg_viewsize")) + step, 30, 100))
	_save_cvars()
	_out("cg_viewsize %s: the view always fills the screen here; Settings > HUD > Scale sizes the HUD." % _local["cg_viewsize"])


func _cycle(dir: int) -> void:
	var h := _match()
	if h != null:
		h.call("_cycle_weapon", dir)


func _weapon(args: PackedStringArray) -> void:
	var h := _match()
	if h == null:
		return
	var n := args[0].to_int() if args.size() else 0
	if n >= 1 and n <= 9:
		h.call("_select_weapon", n)


## CG_CrosshairPlayer: the player last under the crosshair, for a second.
func crosshair_player() -> int:
	var h := _host()
	if h == null or Q3.game_ms - float(h.get("crosshair_ms")) > 1000.0:
		return -1
	return int(h.get("crosshair_client"))


## CG_LastAttacker: ps.persistant[PERS_ATTACKER], once someone has hurt you.
func last_attacker() -> int:
	var h := _host()
	if h == null or int(h.get("me")) < 0:
		return -1
	var ps: Dictionary = h.get("feed").call("player_state", int(h.get("me")))
	var pers: PackedInt32Array = ps.get("persistant", PackedInt32Array())
	if pers.size() <= E.PERS_ATTACKER:
		return -1
	var a := int(pers[E.PERS_ATTACKER])
	return a if a >= 0 and a < E.MAX_CLIENTS and a != int(h.get("me")) else -1


func _tell(target: int, verb: String, rest: String) -> void:
	var h := _match()
	if h != null and target >= 0:
		forward.call("%s %d %s" % [verb, target, rest])


## CG_TargetCommand_f: "gc <target> <command>" to the player under the crosshair.
func _tcmd(args: PackedStringArray) -> void:
	var h := _match()
	var target := crosshair_player()
	if h != null and target >= 0:
		forward.call("gc %d %d" % [target, args[0].to_int() if args.size() else 0])


## CG_StartOrbit_f: with developer on, the third-person camera swinging round you, or back.
func _start_orbit() -> void:
	if not ClassDB.class_exists("Q3Server") or Q3Server.cvar("developer").to_int() == 0:
		return
	if cvar_float("cg_cameraorbit") != 0.0:
		cvar_set("cg_cameraOrbit", "0")
		cvar_set("cg_thirdPerson", "0")
	else:
		cvar_set("cg_cameraOrbit", "5")
		cvar_set("cg_thirdPerson", "1")
		cvar_set("cg_thirdPersonAngle", "0")
		cvar_set("cg_thirdPersonRange", "100")


## testmodel / testgun <model>: a library model in front of you (or held as the gun), then
## nextframe / prevframe step its animation. A library id ("powerups/armor/armor_red") or the md3
## path ("models/powerups/armor/armor_red.md3"); a player model by its name ("sarge").
func _testmodel(args: PackedStringArray, gun: bool) -> void:
	var h := _match()
	if h == null:
		return
	if _test != null and is_instance_valid(_test):
		_test.queue_free()
	_test = null
	if args.is_empty():
		return
	var id := args[0].replace("\\", "/").trim_prefix("models/").get_basename()
	var node: Node3D
	if Q3.characters.has(id.trim_prefix("players/").get_slice("/", 0)):
		var s := Q3.character_scene(id.trim_prefix("players/").get_slice("/", 0))
		node = s.instantiate() if s else null
	elif Q3.props.has(id):
		node = Q3.instance_prop(id)
	if node == null:
		_out("Can't register model %s" % args[0])
		return
	_test = node
	_test_gun = gun
	_test_skin = 0
	var cam: Camera3D = h.get("cam")
	if gun:
		# CG_TestGun_f: drawn as the view weapon, at the eye
		cam.add_child(node)
		node.transform = Transform3D.IDENTITY
	else:
		# CG_TestModel_f: 100 units ahead, turned to face you
		(h as Node).add_child(node)
		var fwd := -cam.global_basis.z
		fwd.y = 0.0
		node.global_position = cam.global_position + fwd.normalized() * 100.0 * Q3.UNIT
		var eye := Vector3(cam.global_position.x, node.global_position.y, cam.global_position.z)
		if not eye.is_equal_approx(node.global_position):
			node.look_at(eye, Vector3.UP)


func _test_frame(step: int) -> void:
	if _test == null or not is_instance_valid(_test):
		return
	var ap: AnimationPlayer = null
	for c in _test.find_children("*", "AnimationPlayer", true, false):
		ap = c
		break
	if ap == null or ap.get_animation_list().is_empty():
		_out("frame 0")
		return
	if ap.current_animation == "":
		ap.play(ap.get_animation_list()[0])
		ap.pause()
	var fps := 15.0
	ap.seek(clampf(ap.current_animation_position + step / fps, 0.0, ap.current_animation_length), true)
	_out("frame %d" % roundi(ap.current_animation_position * fps))


func _test_skin_step(step: int) -> void:
	if _test == null or not is_instance_valid(_test):
		return
	_test_skin = maxi(_test_skin + step, 0)
	_out("skin %d" % _test_skin)


# ---------------------------------------------------------------- helpers

## A file through the engine's search path (fs_homepath, then the paks).
func _engine_file(path: String) -> PackedByteArray:
	if path.contains("..") or not ClassDB.class_exists("Q3Server") or not Q3Server.is_ready() \
			or not ClassDB.class_has_method("Q3Server", "read_file"):
		return PackedByteArray()
	return ClassDB.class_call_static("Q3Server", "read_file", path)
