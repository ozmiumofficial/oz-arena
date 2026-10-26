extends Node
## Headless check of the developer console (Q3Console, Q3ConsoleCommands, Q3ConsoleClient):
## the buffer's parsing, key names, completion, then a real match (one bot on q3dm17):
##   - every command id's 1.32 registered (client, renderer, qcommon, server, win32, q3_ui, cgame,
##     the game's player and server commands) is one the console takes;
##   - the console key opens it, you stand still with EF_TALK while it's open, Escape closes it
##     without pausing;
##   - echo, a cvar read, the engine's output (status), an unknown command the game answers, the history;
##   - wait holds the rest for a frame, exec runs a .cfg (comments, binds, seta), vstr, writeconfig;
##   - client cvars on the settings (cg_fov, s_volume, toggle, reset), cl_yawspeed with +left,
##     +strafe, +buttonN, bind onto Controls and a console bind run by its key (+zoom held, then let go);
##   - cg_thirdPerson moves the eye behind you and draws your body;
##   - stoprecord ends the replay, viewpos, configstrings, clientinfo, fs_openedList, gfxinfo;
##   - hosting for others locks the engine commands, setters and cg_thirdPerson.
## Your settings, binds and replays are left as they were (everything it writes goes to scratch files).
##   godot --headless --path . res://tests/console_test.tscn

const E := preload("res://scripts/game/q3_enums.gd")
const SCRATCH := "user://console_test"

## Everything id's 1.32 code registered with Cmd_AddCommand / trap_AddCommand, or answers in
## ClientCommand / ConsoleCommand / UI_ConsoleCommand (baseq3: no MISSIONPACK).
const ID_COMMANDS := [
	# client (cl_*.c)
	"+attack", "+back", "+button0", "+button1", "+button2", "+button3", "+button4", "+button5", "+button6", "+button7",
	"+button8", "+button9", "+button10", "+button11", "+button12", "+button13", "+button14", "+forward", "+left",
	"+lookdown", "+lookup", "+mlook", "+movedown", "+moveleft", "+moveright", "+moveup", "+right", "+speed", "+strafe",
	"-attack", "-back", "-button0", "-button14", "-forward", "-left", "-lookdown", "-lookup", "-mlook", "-movedown",
	"-moveleft", "-moveright", "-moveup", "-right", "-speed", "-strafe", "bind", "bindlist", "centerview", "cinematic",
	"clear", "clientinfo", "cmd", "condump", "configstrings", "connect", "demo", "disconnect", "fs_openedList",
	"fs_referencedList", "globalservers", "localservers", "messagemode", "messagemode2", "messagemode3", "messagemode4",
	"model", "music", "ping", "play", "rcon", "reconnect", "record", "s_info", "s_list", "s_stop", "serverstatus",
	"setenv", "showip", "snd_restart", "stoprecord", "toggleconsole", "unbind", "unbindall", "vid_restart",
	# renderer
	"gfxinfo", "imagelist", "modelist", "modellist", "screenshot", "screenshotJPEG", "shaderlist", "skinlist",
	# qcommon
	"changeVectors", "cmdlist", "crash", "cvar_restart", "cvarlist", "dir", "echo", "error", "exec", "fdir", "freeze",
	"hunklog", "hunksmalllog", "meminfo", "path", "quit", "reset", "set", "seta", "sets", "setu", "toggle", "touchFile",
	"vminfo", "vmprofile", "vstr", "wait", "writeconfig", "zonelog",
	# server
	"banClient", "banUser", "clientkick", "devmap", "dumpuser", "heartbeat", "kick", "killserver", "map", "map_restart",
	"say", "sectorlist", "serverinfo", "spdevmap", "spmap", "status", "systeminfo",
	# win32
	"in_restart", "midiinfo", "net_restart",
	# q3_ui
	"levelselect", "postgame", "ui_cache", "ui_cinematics", "ui_teamOrders", "iamacheater", "iamamonkey", "ui_cdkey",
	# cgame
	"testgun", "testmodel", "nextframe", "prevframe", "nextskin", "prevskin", "viewpos", "+scores", "-scores", "+zoom",
	"-zoom", "sizeup", "sizedown", "weapnext", "weapprev", "weapon", "tell_target", "tell_attacker", "vtell_target",
	"vtell_attacker", "tcmd", "startOrbit", "loaddeferred",
	# game: ClientCommand and ConsoleCommand
	"say_team", "tell", "vsay", "vsay_team", "vtell", "vosay", "vosay_team", "votell", "vtaunt", "score", "give", "god",
	"notarget", "noclip", "kill", "teamtask", "levelshot", "follow", "follownext", "followprev", "team", "where",
	"callvote", "vote", "callteamvote", "teamvote", "gc", "setviewpos", "stats",
	"entitylist", "forceteam", "game_memory", "addbot", "botlist", "abort_podium", "addip", "removeip", "listip",
]

var _game: Q3Gameplay
var _con: Q3Console
var _cmds: Q3ConsoleCommands
var _t := 0.0
var _step := 0
var _at := 0.0              ## when the current step began
var _ok := true
var _origin := Vector3.ZERO
var _yaw := 0.0
var _saved := {}            ## settings the test changes, to put back
var _replays := ""


func _ready() -> void:
	DirAccess.make_dir_recursive_absolute(SCRATCH)
	_con = Diagnostics.console
	_cmds = _con.commands
	_con.history_file = SCRATCH + "/history.txt"
	_cmds.config_dir = SCRATCH
	_cmds.client.binds_file = SCRATCH + "/binds.cfg"
	_cmds.client.cvars_file = SCRATCH + "/cvars.cfg"
	_cmds.client.reload()
	for e in Settings.entries:
		var k := String(e["key"])
		if k.begins_with("controls/") or k in ["video/fov", "hud/fps", "audio/vol_effects", "mouse/sensitivity"]:
			_saved[k] = Settings.get_value(k)
	_replays = Q3ReplayFile.dir
	Q3ReplayFile.dir = SCRATCH + "/replays"
	_parsing()
	var s := Session.new_setup("ffa", "q3dm17")
	s.bots = [{"name": "Sarge", "skill": 1, "team": ""}]
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	_game.test_input = true
	add_child(_game)


func _parsing() -> void:
	var C := Q3ConsoleCommands
	_check("split at ; and newlines", C.split_commands("echo a; echo b ;;echo c\necho d") == PackedStringArray(["echo a", "echo b", "echo c", "echo d"]))
	_check("quotes hold a ;", C.split_commands("say \"a;b\"; kill") == PackedStringArray(["say \"a;b\"", "kill"]))
	_check("// ends a line, not in quotes", C.split_commands("seta a 1 // note\nsay \"x//y\"") == PackedStringArray(["seta a 1", "say \"x//y\""]))
	_check("tokenize", C.tokenize("say \"hello  world\" x") == PackedStringArray(["say", "hello  world", "x"]))
	var K := Q3ConsoleClient
	_check("key names in", K.parse_key("MOUSE1") == "mouse:1" and K.parse_key("w") == "key:W" and K.parse_key("UPARROW") == "key:Up"
		and K.parse_key("MWHEELDOWN") == "mouse:5" and K.parse_key("F4") == "key:F4" and K.parse_key("nosuchkey") == "")
	_check("key names out", K.key_name("key:W") == "w" and K.key_name("key:Up") == "UPARROW" and K.key_name("mouse:4") == "MWHEELUP"
		and K.key_name("key:Space") == "SPACE")
	_check("commands that are Controls actions", K.action_for("+forward") == "forward" and K.action_for("weapon  5") == "weapon5"
		and K.action_for("echo hi") == "")
	var r := _cmds.complete("cmdl")
	_check("tab completes a lone match", r["text"] == "cmdlist " and r["matches"].is_empty())
	r = _cmds.complete("KIL")
	_check("tab lists several, without case (%s)" % [r["matches"]], r["matches"].has("kill") and r["matches"].has("killserver")
		and String(r["text"]).to_lower() == "kill")


func _action(a: String) -> void:
	var ev := InputEventAction.new()
	ev.action = a
	ev.pressed = true
	Input.parse_input_event(ev)
	var up := ev.duplicate() as InputEventAction
	up.pressed = false
	Input.parse_input_event(up)


func _key(code: Key, down: bool) -> void:
	var ev := InputEventKey.new()
	ev.keycode = code
	ev.physical_keycode = code
	ev.pressed = down
	Input.parse_input_event(ev)


func _type(line: String) -> void:
	_con._edit.text = line
	_con._edit.text_submitted.emit(line)


func _has(needle: String) -> bool:
	for l in _con._lines:
		if String(l).contains(needle):
			return true
	return false


func _run(line: String) -> String:
	return "\n".join(_cmds.run(line))


func _ps() -> Dictionary:
	return Q3Server.player_state(_game.me)


## Moves on after `secs`.
func _after(secs: float) -> bool:
	if _t - _at < secs:
		return false
	_at = _t
	_step += 1
	return true


func _process(delta: float) -> void:
	_t += delta
	match _step:
		0:
			if _t > 3.0 and _after(0.0):
				_action("q3_console")
				Input.action_press("q3_forward")
				_origin = _ps()["origin"]
		1:
			if _after(0.6):
				_check("the console key opens it", Q3Console.open_now and _con.visible)
				var ps := _ps()
				var moved := Vector2((ps["origin"] as Vector3).x - _origin.x, (ps["origin"] as Vector3).y - _origin.y).length()
				_check("open: forward held doesn't move you (%.1f units)" % moved, moved < 1.0)
				_check("open: EF_TALK is set", int(ps["eFlags"]) & E.EF_TALK != 0)
				Input.action_release("q3_forward")
				_every_command()
				_type("echo hello console")
				_type("sv_fps")
				_type("foobarbaz")
				_type("status")
				_type("set wait_test 1; wait; set wait_test 2")
				_check("wait holds the rest for a frame", Q3Server.cvar("wait_test") == "1")
		2:
			if _after(0.6):
				_check("and then it runs", Q3Server.cvar("wait_test") == "2")
				_check("echo prints, your line is echoed", _has("hello console") and _has("^3]^7 echo hello console"))
				_check("an engine cvar's value", _has("\"sv_fps\" is:\"125"))
				_check("an unknown command goes to the game, which says so", _has("foobarbaz"))
				_check("the engine's output reaches the scrollback", _has("map: q3dm17"))
				_check("the history keeps the lines", _con._history.size() >= 5 and _con._history[-1] == "set wait_test 1; wait; set wait_test 2")
				_scripts()
				_client_cvars()
				_type("cheats on; noclip")
		3:
			if _after(0.4):
				_check("cheats on, and noclip reaches the game (pm_type %d)" % int(_ps()["pm_type"]), int(_ps()["pm_type"]) == 1)
				_run("noclip")
				_key(KEY_ESCAPE, true)
				_key(KEY_ESCAPE, false)
		4:
			if _after(0.3):
				_check("Escape closes it and doesn't pause", not Q3Console.open_now and not _game._paused)
				_check("noclip again turns it off", int(_ps()["pm_type"]) == 0)
				_run("cheats off; sv_cheats 1; set sv_cheats 1")
				_check("sv_cheats stays read only to typing and set, as in Quake 3", Q3Server.cvar("sv_cheats") == "0")
				_yaw = _game._view.y
				_run("cl_yawspeed 280; +left")
		5:
			if _after(0.5):
				_run("-left")
				var turned := absf(_game._view.y - _yaw)
				_check("+left turns at cl_yawspeed (%.0f degrees in 0.5 s)" % turned, turned > 90.0 and turned < 190.0)
				_run("+strafe; +right; +button3")
				_check("+strafe makes +right a side move", _cmds.client.strafe_side() == 1.0)
				_check("+button3 is the gesture bit", _cmds.client.buttons == E.BUTTON_GESTURE)
				_run("-right; -strafe; -button3; reset cl_yawspeed")
				_check("all let go", _cmds.client.strafe_side() == 0.0 and _cmds.client.buttons == 0)
				_binds()
				_key(KEY_F, true)
		6:
			if _after(0.3):
				_check("a console bind runs when its key goes down: +zoom held", Input.is_action_pressed("q3_zoom"))
				_key(KEY_F, false)
		7:
			if _after(0.3):
				_check("and its key coming up sends -zoom", not Input.is_action_pressed("q3_zoom"))
				_run("cg_thirdPerson 1")
		8:
			if _after(0.4):
				var eye := Q3.q3_to_godot((_ps()["origin"] as Vector3) + Vector3(0, 0, float(_ps()["viewheight"])))
				var back := _game.cam.global_position.distance_to(eye) / Q3.UNIT
				_check("cg_thirdPerson puts the eye behind you (%.0f units) and draws your body" % back, back > 20.0 and _game.client.self_visible)
				_run("cg_thirdPerson 0")
		9:
			if _after(0.3):
				_check("cg_thirdPerson 0 is back in your eyes", not _game.client.self_visible)
				_reports()
				_locks()
				_finish()


## Every one of id's commands is the console's.
func _every_command() -> void:
	var names := {}
	for n in _cmds.names():
		names[String(n).to_lower()] = true
	var missing: Array = []
	for c in ID_COMMANDS:
		if not names.has(String(c).to_lower()):
			missing.append(c)
	_check("every Quake 3 command is there (%d names; missing %s)" % [names.size(), missing], missing.is_empty())
	var cv := _cmds.cvar_names()
	_check("cvars: the engine's and the client's (%d)" % cv.size(), cv.has("sv_cheats") and cv.has("cg_fov") and cv.has("cl_yawspeed"))


## exec (with comments, a bind and seta), vstr, writeconfig.
func _scripts() -> void:
	var f := FileAccess.open(SCRATCH + "/test_exec.cfg", FileAccess.WRITE)
	f.store_string("// a script\nseta exec_test 5 // set from a script\nset vstr_test \"echo vstr ran\"\nbind h \"echo from h\"\n")
	f.close()
	var out := _run("exec test_exec")
	_check("exec runs a .cfg: %s" % out, out.contains("execing test_exec.cfg") and Q3Server.cvar("exec_test") == "5")
	_check("its bind took", _run("bind h").contains("echo from h"))
	_check("vstr runs a cvar's text", _run("vstr vstr_test").contains("vstr ran"))
	_check("exec of nothing says so", _run("exec nosuchfile").contains("couldn't exec nosuchfile.cfg"))
	_run("writeconfig test_written")
	var text := FileAccess.get_file_as_string(SCRATCH + "/test_written.cfg")
	_check("writeconfig writes binds and archived cvars", text.contains("unbindall") and text.contains("bind h \"echo from h\"")
		and text.contains("seta cl_yawspeed") and text.contains("seta exec_test \"5\""))
	_run("unbind h")
	_check("unbind", _run("bind h").contains("is not bound"))


## The client cvars that are settings, and the client-only ones.
func _client_cvars() -> void:
	_run("cg_fov 100")
	_check("cg_fov sets Field of view", is_equal_approx(float(Settings.get_value("video/fov")), 100.0))
	_check("and reads back, with its default", _run("cg_fov").contains("\"cg_fov\" is:\"100^7\" default:\"90"))
	_run("s_volume 0.5")
	_check("s_volume is Effects volume (%s)" % Settings.get_value("audio/vol_effects"), int(Settings.get_value("audio/vol_effects")) == 50)
	_run("set cg_drawFPS 1")
	_check("set works on them", Settings.get_value("hud/fps") == true)
	_run("toggle cg_drawFPS")
	_check("toggle flips them", Settings.get_value("hud/fps") == false)
	_run("cg_fov 110; reset cg_fov")
	_check("reset puts one back", is_equal_approx(float(Settings.get_value("video/fov")), float(Settings.default_of("video/fov"))))


## bind onto Controls, unbind, and a console bind.
func _binds() -> void:
	var fwd: Array = (Settings.get_value("controls/forward") as Array).duplicate()
	_run("bind j +forward")
	_check("bind j +forward: j runs +forward, and Move forward keeps %s" % [fwd.slice(0, 2)],
		_cmds.client.binding("key:J") == "+forward" and (Settings.get_value("controls/forward") as Array).slice(0, 2) == fwd.slice(0, 2))
	_check("bindlist shows it", _run("bindlist").contains("j \"+forward\""))
	_run("bind j weapon 7")
	_check("a key runs one command: j left +forward for Railgun's free slot", _cmds.client.binding("key:J") == "weapon 7"
		and (Settings.get_value("controls/weapon7") as Array).has("key:J"))
	_run("unbind j")
	_check("unbind takes it off", _cmds.client.binding("key:J") == "" and not (Settings.get_value("controls/weapon7") as Array).has("key:J"))
	_check("a bad key is refused", _run("bind nosuchkey x").contains("isn't a valid key"))
	_run("bind f +zoom")
	_check("bind f +zoom lands on Zoom", (Settings.get_value("controls/zoom") as Array).has("key:F"))
	# a console bind for the key test: a command line no Controls action is
	_run("unbind f; bind f \"+zoom; echo zoomed\"")
	_check("a line that isn't one action is a console bind", _cmds.client.binding("key:F") == "+zoom; echo zoomed")


## Commands that print what they found.
func _reports() -> void:
	var vp := _run("viewpos")
	_check("viewpos: %s" % vp, vp.begins_with("(") and vp.contains(") : "))
	_check("weapon 2 runs quietly", _run("weapon 2") == "")
	_check("cvarlist finds sv_fps", _run("cvarlist sv_fps").contains("sv_fps \"125\""))
	_check("configstrings lists them", _run("configstrings").contains("   0: "))
	_check("clientinfo shows your userinfo", _run("clientinfo").contains("User info settings:"))
	_check("fs_openedList names pak0", _run("fs_openedList").contains("pak0"))
	_check("gfxinfo names the renderer", _run("gfxinfo").contains("GL_RENDERER"))
	_check("s_info lists the buses", _run("s_info").contains("bus Effects"))
	_check("ui_cdkey: the verified install", _run("ui_cdkey").contains("verified from"))
	_check("rcon wants a server you joined", _run("rcon status").contains("You must be connected"))
	var rec := _game.recorder != null
	_run("stoprecord")
	_check("stoprecord ends the replay (it was %s)" % ("recording" if rec else "off"), _game.recorder == null)
	_run("condump console_test_dump")
	_check("condump writes the scrollback", FileAccess.get_file_as_string("user://console_test_dump.txt").contains("echo hello console"))
	DirAccess.remove_absolute("user://console_test_dump.txt")


## Hosting for others: the engine's commands, setters and cheat cvars are locked; player commands aren't.
func _locks() -> void:
	_check("not hosting: nothing locked", _cmds.restricted() == "")
	_run("cg_thirdPerson 1")
	_game.host_net = Q3HostNet.new()
	var out := _cmds.run("addbot sarge 1; sv_cheats 1; set sv_cheats 1; toggle sv_cheats; map q3dm6; devmap q3dm6; kick sarge; cheats on")
	_check("hosting locks the engine's commands and setters (%d lines)" % out.size(), out.size() == 8
		and Array(out).all(func(l): return String(l).contains("Locked")))
	_check("and cg_thirdPerson reads 0 while it's locked", _cmds.client.cvar_get("cg_thirdPerson") == "0")
	_check("nothing changed", Q3Server.cvar("sv_cheats") == "0")
	_game.host_net.free()
	_game.host_net = null
	_run("cg_thirdPerson 0")


func _finish() -> void:
	_cmds.client.release_all()
	for k in _saved:
		Settings.set_value(k, _saved[k], false)
	Settings.save()
	Q3ReplayFile.dir = _replays
	_rm(SCRATCH)
	print("CONSOLE %s" % ("ok" if _ok else "FAILED"))
	Q3Server.exec_now("killserver")
	get_tree().quit(0 if _ok else 1)


func _rm(dir: String) -> void:
	for d in DirAccess.get_directories_at(dir):
		_rm(dir.path_join(d))
	for f in DirAccess.get_files_at(dir):
		DirAccess.remove_absolute(dir.path_join(f))
	DirAccess.remove_absolute(dir)


func _check(label: String, cond: bool) -> void:
	print("%s %s" % ["PASS" if cond else "FAIL", label])
	if not cond:
		_ok = false
