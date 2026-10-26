class_name Q3ConsoleCommands
extends RefCounted
## What the developer console does with text: Quake 3's command buffer (cmd.c). Lines split at
## newlines and ";" (quotes keep one together, "//" ends a line), run in order until a "wait",
## which holds the rest for a frame (pump()). exec and vstr put their text at the front, as
## Cbuf_InsertText does. Each command goes to the first of these that has it:
##   1. the console's own: the buffer's commands (exec, vstr, wait, echo, set ...), map and friends
##      (a match runs inside Gameplay, so the engine's own map would pull it out from under it),
##      cmdlist / cvarlist over everything, and the remaster's setting, maps, cheats, help;
##   2. Quake 3's client commands and cvars (Q3ConsoleClient: +attack, bind, weapon, cg_fov ...);
##   3. the game's player commands (g_cmds.c: give, noclip, team, say ...), as you, in a match;
##   4. the engine's console commands (Q3Server.engine_commands(): kick, map_restart, status, dir
##      ...) and the game's server commands (g_svcmds.c: addbot, botlist ...), queued for its next frame;
##   5. an engine cvar: its name alone says its value, with a value sets it;
##   6. in a match anything else goes to the server, as Quake 3's client forwards what it doesn't know.
## While this player hosts for others, has joined someone else's match or plays a matchmade one, 4
## and the setters of 5 are locked: the console mustn't change a game other players share or a
## ranked result. Player commands still go, and the game itself refuses cheats without sv_cheats.

signal clear_requested
signal console_requested(what: String, arg: Variant)   ## toggle, close, condump: things only the console can do

## id 1.32's engine commands (common.c, cmd.c, cvar.c, files.c, sv_ccmds.c, win32): what's known
## before the engine is up to answer for itself.
const SERVER_VERBS := ["banclient", "banuser", "changevectors", "clientkick", "crash", "cvar_restart", "dir", "dumpuser",
	"error", "fdir", "freeze", "heartbeat", "hunklog", "hunksmalllog", "kick", "killserver", "map_restart", "meminfo",
	"net_restart", "path", "say", "sectorlist", "serverinfo", "setenv", "status", "systeminfo", "touchfile", "unset",
	"vminfo", "vmprofile", "zonelog"]
## What the game answers at the server console (g_svcmds.c ConsoleCommand), through the engine.
const GAME_SERVER := ["abort_podium", "addbot", "addip", "botlist", "entitylist", "forceteam", "game_memory", "listip",
	"removeip"]
## What the game takes from a player (g_cmds.c ClientCommand).
const GAME_CLIENT := ["callteamvote", "callvote", "follow", "follownext", "followprev", "gc", "give", "god", "kill",
	"levelshot", "noclip", "notarget", "say", "say_team", "score", "setviewpos", "stats", "team", "teamtask", "teamvote",
	"tell", "vosay", "vosay_team", "vote", "votell", "vsay", "vsay_team", "vtaunt", "vtell", "where"]
const TRUE_WORDS := ["1", "true", "on", "yes"]
const MAX_PER_FRAME := 10000   ## a script that execs itself stops here instead of hanging the game
const CVAR_ARCHIVE := 1
const CVAR_NONEXISTENT := -1   ## (0xFFFFFFFF as an int)

var printer := Callable()       ## called with each output line (Q3Console.print_line)
var host: Object                ## the Gameplay scene, when a match is up
var client := Q3ConsoleClient.new()
var config_dir := "user://q3/baseq3"   ## where exec and writeconfig look first (the engine's fs_homepath/baseq3)

var _lines := PackedStringArray()
var _table := {}                ## name -> {call, help, usage}
var _cbuf := PackedStringArray()
var _wait := 0
var _outgoing := PackedStringArray()   ## commands for the game, sent after the server's next frame
var _outgoing_time := -1
var _engine_cmds := PackedStringArray()
var _engine_cvars := PackedStringArray()


func _init() -> void:
	client.out = _out
	client.run_line = func(t: String) -> void: run(t)
	client.forward = _forward
	client.restricted = restricted
	client.host_fn = func() -> Object: return host
	client.request = func(what: String, arg: Variant) -> void:
		if what == "clear":
			clear_requested.emit()
		else:
			console_requested.emit(what, arg)
	_add("help", "Says what the console does, or what one command is.", "help [command]", _cmd_help)
	_add("cmdlist", "Lists every command, or those starting with some text.", "cmdlist [text]", _cmd_cmdlist)
	_add("cvarlist", "Lists every cvar with its value, or those starting with some text.", "cvarlist [text]", _cmd_cvarlist)
	_add("echo", "Prints its arguments.", "echo <text>", _cmd_echo)
	_add("exec", "Runs a script file (.cfg) from your Quake 3 folder or the paks.", "exec <filename>", _cmd_exec)
	_add("execq", "exec without the \"execing\" line.", "execq <filename>", _cmd_execq)
	_add("vstr", "Runs a cvar's value as commands.", "vstr <cvar>", _cmd_vstr)
	_add("wait", "Holds the rest of the commands for a frame (or n frames).", "wait [n]", _cmd_wait)
	_add("set", "Sets a cvar, making it if there's none.", "set <name> <value>", _cmd_set)
	_add("seta", "set, and kept by writeconfig.", "seta <name> <value>", func(a: PackedStringArray) -> void: _cmd_set(a, "seta"))
	_add("sets", "set, and shown in serverinfo.", "sets <name> <value>", func(a: PackedStringArray) -> void: _cmd_set(a, "sets"))
	_add("setu", "set, and sent in userinfo.", "setu <name> <value>", func(a: PackedStringArray) -> void: _cmd_set(a, "setu"))
	_add("toggle", "Flips a cvar between 0 and 1.", "toggle <cvar>", _cmd_toggle)
	_add("reset", "Puts a cvar back to its default.", "reset <cvar>", _cmd_reset)
	_add("cvar_restart", "Puts every cvar back to its default.", "cvar_restart", _cmd_cvar_restart)
	_add("writeconfig", "Writes your binds and archived cvars to a script exec can run.", "writeconfig <filename>", _cmd_writeconfig)
	_add("map", "Starts a match on a map: the current mode, or free-for-all from the menu.", "map <mapname>", _cmd_map)
	_add("devmap", "map with cheats on.", "devmap <mapname>", _cmd_devmap)
	_add("spmap", "map (the campaign itself starts from Single Player).", "spmap <mapname>", _cmd_map)
	_add("spdevmap", "devmap.", "spdevmap <mapname>", _cmd_devmap)
	_add("quit", "Quits the game.", "quit", _cmd_quit)
	_add("version", "The game's version.", "version", _cmd_version)
	_add("maps", "Lists the maps (remaster).", "maps [text]", _cmd_maps)
	_add("cheats", "Shows or sets sv_cheats (remaster shorthand).", "cheats [on|off]", _cmd_cheats)
	_add("setting", "Reads or changes a menu setting by its key (remaster). 'setting list [text]' lists them, 'setting reset <key>' restores one.",
		"setting <key> [value] | setting list [text] | setting reset <key>", _cmd_setting)


# ---------------------------------------------------------------- the buffer

## Runs text now (as far as any "wait"); returns what was printed (also sent to `printer`).
func run(text: String) -> PackedStringArray:
	_lines = PackedStringArray()
	insert(text)
	execute()
	return _lines


## Cbuf_InsertText: the text's commands before whatever is waiting.
func insert(text: String) -> void:
	var cmds := split_commands(text)
	cmds.append_array(_cbuf)
	_cbuf = cmds


## Every frame: last frame's commands for the game go out, a wait counts down, then the buffer runs on.
func pump() -> void:
	_send_outgoing()
	if _wait > 0:
		_wait -= 1
		return
	if not _cbuf.is_empty():
		execute()


func execute() -> void:
	var n := 0
	while not _cbuf.is_empty() and _wait == 0 and n < MAX_PER_FRAME:
		var cmd := _cbuf[0]
		_cbuf.remove_at(0)
		_run_one(cmd)
		n += 1
	if n >= MAX_PER_FRAME:
		_cbuf.clear()
		_out("^1Too many commands in one frame: the rest were dropped (a script that runs itself?).")


func pending() -> bool:
	return not _cbuf.is_empty()


## Text cut into commands: at newlines and at ";" outside quotes; "//" outside quotes ends a line.
static func split_commands(text: String) -> PackedStringArray:
	var out := PackedStringArray()
	for line in text.replace("\r", "").split("\n"):
		var cur := ""
		var quoted := false
		var i := 0
		while i < line.length():
			var ch := line[i]
			if ch == "\"":
				quoted = not quoted
			if not quoted and ch == "/" and i + 1 < line.length() and line[i + 1] == "/":
				break
			if ch == ";" and not quoted:
				if cur.strip_edges() != "":
					out.append(cur.strip_edges())
				cur = ""
			else:
				cur += ch
			i += 1
		if cur.strip_edges() != "":
			out.append(cur.strip_edges())
	return out


## Words split at spaces; "double quotes" keep spaces in one word.
static func tokenize(text: String) -> PackedStringArray:
	var out := PackedStringArray()
	var cur := ""
	var quoted := false
	var has := false
	for ch in text:
		if ch == "\"":
			quoted = not quoted
			has = true
		elif (ch == " " or ch == "\t") and not quoted:
			if has:
				out.append(cur)
			cur = ""
			has = false
		else:
			cur += ch
			has = true
	if has:
		out.append(cur)
	return out


func _run_one(text: String) -> void:
	var args := tokenize(text)
	if args.is_empty():
		return
	var name := args[0].to_lower()
	var rest := args.slice(1)
	if _table.has(name):
		(_table[name]["call"] as Callable).call(rest)
	elif client.has_command(name):
		client.run(name, rest, text)
	elif client.is_cvar(name):
		_client_cvar(args[0], rest)
	elif GAME_CLIENT.has(name) and _in_match():
		_forward(text)
	elif _is_engine_command(name):
		_server(text)
	elif _cvar(args[0], rest):
		pass
	elif _in_match():
		_forward(text)   # CL_ForwardCommandToServer: the game says if it doesn't know it
	else:
		_out("Unknown command \"%s\"" % args[0])


func _add(name: String, help: String, usage: String, fn: Callable) -> void:
	_table[name] = {"call": fn, "help": help, "usage": usage}


func _out(text: String) -> void:
	for l in text.split("\n"):
		_lines.append(l)
		if printer.is_valid():
			printer.call(l)


# ---------------------------------------------------------------- where commands go

## Why the console may not touch the shared game, or "".
func restricted() -> String:
	if host != null and is_instance_valid(host):
		if host.get("host_net") != null:
			return "you are hosting this match for other players"
		if host.get("net_feed") != null:
			return "this is someone else's match"
	if not Session.matchmade.is_empty():
		return "this is a matchmade match"
	return ""


func _engine_ready() -> bool:
	return ClassDB.class_exists("Q3Server") and Q3Server.is_ready()


func _in_match() -> bool:
	return host != null and is_instance_valid(host) and int(host.get("me")) >= 0


## A command from you to the game (Gameplay._cmd: this process's server or the one you joined). It
## goes on the next frame, as Quake 3's rode the next packet: by then the server has run a frame, so
## "set sv_cheats 1; noclip" works as it did there (the game reads cvars once a frame).
func _forward(text: String) -> void:
	if _outgoing.is_empty():
		_outgoing_time = _server_time()
	_outgoing.append(text)


## The local server's clock, or -1 when this process runs no match (a joined match: send at once).
func _server_time() -> int:
	return Q3Server.time() if ClassDB.class_exists("Q3Server") and Q3Server.running() else -1


func _send_outgoing() -> void:
	if _outgoing.is_empty():
		return
	var now := _server_time()
	if now >= 0 and now <= _outgoing_time:
		return   # no server frame yet since they were typed (a frame can be shorter than sv_fps' 8 ms)
	var cmds := _outgoing
	_outgoing = PackedStringArray()
	if host != null and is_instance_valid(host) and int(host.get("me")) >= 0:
		for c in cmds:
			host.call("_cmd", c)


func _server(text: String) -> void:
	var why := restricted()
	if why != "":
		_out("^1Locked: %s." % why)
	elif not _engine_ready():
		_out("^1The game engine isn't running yet (it starts with the first match).")
	else:
		Q3Server.exec(text)


## `name` alone prints an engine cvar, `name value` sets it. True if it's a cvar.
func _cvar(name: String, rest: PackedStringArray) -> bool:
	if not _engine_ready() or _flags(name) == CVAR_NONEXISTENT:
		return false
	if rest.is_empty():
		_out("\"%s\" is:\"%s^7\"" % [name, Q3Server.cvar(name)])
		return true
	var why := restricted()
	if why != "":
		_out("^1Locked: %s." % why)
		return true
	# Cvar_Command, as typed: read-only and latched cvars say so (sv_cheats: devmap turns it on)
	Q3Server.exec_now("%s \"%s\"" % [name.replace("\"", ""), " ".join(rest).replace("\"", "'")])
	return true


func _client_cvar(name: String, rest: PackedStringArray) -> void:
	if rest.is_empty():
		_out("\"%s\" is:\"%s^7\" default:\"%s^7\"" % [name, client.cvar_get(name), client.cvar_default(name)])
	else:
		client.cvar_set(name, " ".join(rest))


func _flags(name: String) -> int:
	if ClassDB.class_has_method("Q3Server", "cvar_flags"):
		return int(ClassDB.class_call_static("Q3Server", "cvar_flags", name))
	return 0 if Q3Server.cvar(name) != "" else CVAR_NONEXISTENT


func _engine_lists() -> void:
	if not _engine_ready():
		return
	if _engine_cmds.is_empty() and ClassDB.class_has_method("Q3Server", "engine_commands"):
		_engine_cmds = ClassDB.class_call_static("Q3Server", "engine_commands")
	if ClassDB.class_has_method("Q3Server", "engine_cvars"):
		_engine_cvars = ClassDB.class_call_static("Q3Server", "engine_cvars")   # the game registers more as it loads


func _is_engine_command(name: String) -> bool:
	if SERVER_VERBS.has(name) or GAME_SERVER.has(name):
		return true
	_engine_lists()
	for c in _engine_cmds:
		if c.to_lower() == name:
			return true
	return false


# ---------------------------------------------------------------- lists and completion

## Every command, as id spelled them, sorted without case.
func names() -> Array:
	_engine_lists()
	var seen := {}
	var all: Array = []
	for group in [_table.keys(), client.command_names(), GAME_CLIENT, SERVER_VERBS, GAME_SERVER, Array(_engine_cmds)]:
		for n in group:
			var lower := String(n).to_lower()
			if not seen.has(lower):
				seen[lower] = true
				all.append(String(n))
	all.sort_custom(func(a, b): return a.to_lower() < b.to_lower())
	return all


## Every cvar: the engine's, and the client's.
func cvar_names() -> Array:
	_engine_lists()
	var all: Array = Array(_engine_cvars)
	for c in client.cvar_names():
		if not all.has(c):
			all.append(c)
	all.sort_custom(func(a, b): return String(a).to_lower() < String(b).to_lower())
	return all


func cvar_value(name: String) -> String:
	if client.is_cvar(name):
		return client.cvar_get(name)
	return Q3Server.cvar(name) if _engine_ready() else ""


## Tab completion (Field_CompleteCommand): {"text": the line filled in as far as it's certain,
## "matches": every candidate when there's more than one}. Commands and cvars complete without case.
func complete(text: String) -> Dictionary:
	var parts := text.split(" ")
	var last := parts[parts.size() - 1]
	var first := parts[0].to_lower()
	var pool: Array = []
	if parts.size() == 1:
		pool = names()
		pool.append_array(cvar_names())
	elif parts.size() == 2 and first in ["set", "seta", "sets", "setu", "toggle", "reset", "vstr", "unset"]:
		pool = cvar_names()
	elif parts.size() == 2 and first in ["map", "devmap", "spmap", "spdevmap"]:
		for m in Session.maps:
			pool.append(String(m.get("map", "")))
	elif parts.size() == 2 and first == "setting":
		pool = ["list", "reset"] + _setting_keys()
	elif parts.size() == 3 and first == "setting" and parts[1] == "reset":
		pool = _setting_keys()
	elif parts.size() == 2 and first == "help":
		pool = names()
	var found: Array = []
	var seen := {}
	for p in pool:
		var s := String(p)
		if s.to_lower().begins_with(last.to_lower()) and not seen.has(s.to_lower()):
			seen[s.to_lower()] = true
			found.append(s)
	if found.is_empty():
		return {"text": text, "matches": []}
	found.sort_custom(func(a, b): return a.to_lower() < b.to_lower())
	var prefix: String = found[0]
	for f in found:
		while not String(f).to_lower().begins_with(prefix.to_lower()):
			prefix = prefix.left(-1)
	var head := text.left(text.length() - last.length())
	if found.size() == 1:
		return {"text": head + prefix + " ", "matches": []}
	return {"text": head + (prefix if prefix.length() >= last.length() else last), "matches": found}


# ---------------------------------------------------------------- the console's own commands

func _cmd_help(args: PackedStringArray) -> void:
	if args.is_empty():
		_out("Quake 3's console: every command and cvar Quake 3 had (cmdlist, cvarlist), and a few of the remaster's.")
		_out("Examples: map q3dm6 | devmap q3dm17 | addbot sarge 4 | give all | noclip | bind f +zoom | cg_fov 110 | exec my.cfg")
		_out("Tab completes, Up/Down is the history, PageUp/PageDown scrolls, Shift + the console key opens it full height.")
		return
	var name := args[0].to_lower()
	if _table.has(name):
		_out("^3%s^7  %s" % [_table[name]["usage"], _table[name]["help"]])
	elif client.has_command(name):
		_out("^3%s^7 is one of Quake 3's client commands." % args[0])
	elif GAME_CLIENT.has(name):
		_out("^3%s^7 goes to the game from you (in a match)." % args[0])
	elif _is_engine_command(name):
		_out("^3%s^7 is a command of the game engine's own console." % args[0])
	elif client.is_cvar(name) or (_engine_ready() and _flags(args[0]) != CVAR_NONEXISTENT):
		_out("^3%s^7 is a cvar: \"%s\"" % [args[0], cvar_value(args[0])])
	else:
		_out("No such command or cvar.")


func _cmd_cmdlist(args: PackedStringArray) -> void:
	var match_ := args[0].to_lower() if args.size() else ""
	var n := 0
	for c in names():
		if match_ == "" or String(c).to_lower().begins_with(match_):
			_out(String(c))
			n += 1
	_out("%d commands" % n)


func _cmd_cvarlist(args: PackedStringArray) -> void:
	var match_ := args[0].to_lower() if args.size() else ""
	var n := 0
	for c in cvar_names():
		if match_ == "" or String(c).to_lower().begins_with(match_):
			var flags := _flags(c) if not client.is_cvar(c) and _engine_ready() else 0
			_out("%s %s \"%s\"" % ["A" if flags != CVAR_NONEXISTENT and flags & CVAR_ARCHIVE else " ", c, cvar_value(c)])
			n += 1
	_out("%d total cvars" % n)


func _cmd_echo(args: PackedStringArray) -> void:
	_out(" ".join(args))


func _cmd_exec(args: PackedStringArray, quiet := false) -> void:
	if args.is_empty():
		_out("exec <filename> : execute a script file")
		return
	var file := args[0].replace("\\", "/")
	if file.get_extension() == "":
		file += ".cfg"
	var text: Variant = read_config(file)
	if text == null:
		_out("couldn't exec %s" % file)
		return
	if not quiet:
		_out("execing %s" % file)
	insert(text)


func _cmd_execq(args: PackedStringArray) -> void:
	_cmd_exec(args, true)


## A script by its name: the Quake 3 folder the engine writes to, then the paks. null if none.
func read_config(file: String) -> Variant:
	if file.contains(".."):
		return null
	var path := config_dir.path_join(file)
	if FileAccess.file_exists(path):
		return FileAccess.get_file_as_string(path)
	if _engine_ready() and ClassDB.class_has_method("Q3Server", "read_file"):
		var data: PackedByteArray = ClassDB.class_call_static("Q3Server", "read_file", file)
		if not data.is_empty():
			return data.get_string_from_utf8()
	return null


func _cmd_vstr(args: PackedStringArray) -> void:
	if args.is_empty():
		_out("vstr <variablename> : execute a variable command")
		return
	insert(cvar_value(args[0]))


func _cmd_wait(args: PackedStringArray) -> void:
	_wait = maxi(args[0].to_int(), 1) if args.size() else 1


## set / seta / sets / setu: a client cvar goes to its setting, any other to the engine.
func _cmd_set(args: PackedStringArray, verb := "set") -> void:
	if args.size() < 2:
		_out("usage: %s <variable> <value>" % verb)
		return
	if client.is_cvar(args[0]):
		client.cvar_set(args[0], " ".join(args.slice(1)))
		return
	var why := restricted()
	if why != "":
		_out("^1Locked: %s." % why)
	elif not _engine_ready():
		_out("^1The game engine isn't running yet (it starts with the first match).")
	else:
		# the engine's own set / seta / sets / setu, at once, so the cvar gets its flag (seta: archived)
		Q3Server.exec_now("%s \"%s\" \"%s\"" % [verb, args[0].replace("\"", ""), " ".join(args.slice(1)).replace("\"", "'")])


func _cmd_toggle(args: PackedStringArray) -> void:
	if args.is_empty():
		_out("usage: toggle <variable>")
		return
	var now := cvar_value(args[0])
	_cmd_set(PackedStringArray([args[0], "0" if now.to_float() != 0.0 else "1"]))


func _cmd_reset(args: PackedStringArray) -> void:
	if args.is_empty():
		_out("usage: reset <variable>")
	elif client.is_cvar(args[0]):
		client.cvar_set(args[0], client.cvar_default(args[0]))
	else:
		_server("reset " + args[0])


func _cmd_cvar_restart(_args: PackedStringArray) -> void:
	for c in client.cvar_names():
		client.cvar_set(c, client.cvar_default(c))
	_server("cvar_restart")


## Cvar_WriteVariables + Key_WriteBindings: what exec needs to put your setup back.
func _cmd_writeconfig(args: PackedStringArray) -> void:
	if args.is_empty():
		_out("Usage: writeconfig <filename>")
		return
	var file := args[0].replace("\\", "/")
	if file.contains(".."):
		return
	if file.get_extension() == "":
		file += ".cfg"
	DirAccess.make_dir_recursive_absolute(config_dir)
	var f := FileAccess.open(config_dir.path_join(file), FileAccess.WRITE)
	if f == null:
		_out("^1Couldn't write %s." % file)
		return
	f.store_line("// generated by quake, do not modify")
	f.store_line("unbindall")
	for l in client.bind_lines():
		f.store_line("bind " + l)
	for l in client.archived_lines():
		f.store_line(l)
	_engine_lists()
	for c in _engine_cvars:
		var flags := _flags(c)
		if flags != CVAR_NONEXISTENT and flags & CVAR_ARCHIVE:
			f.store_line("seta %s \"%s\"" % [c, Q3Server.cvar(c)])
	_out("Writing %s." % file)


func _cmd_map(args: PackedStringArray, dev := false) -> void:
	if args.is_empty():
		_out("USAGE: map <map name>")
		return
	var why := restricted()
	if why != "":
		_out("^1Locked: %s." % why)
		return
	var s: Q3MatchSetup
	if Session.setup != null:
		s = Session.setup.duplicate()
		s.map = args[0].to_lower()
	else:
		s = Session.new_setup("ffa", args[0].to_lower())
	if dev:
		s.set_meta("devmap", true)   # Gameplay starts it with "devmap": cheats on
	elif s.has_meta("devmap"):
		s.remove_meta("devmap")
	var errs := Session.start_match(s)
	for e in errs:
		_out("^1" + e)


func _cmd_devmap(args: PackedStringArray) -> void:
	_cmd_map(args, true)


func _cmd_quit(_args: PackedStringArray) -> void:
	Session.quit_game()


func _cmd_version(_args: PackedStringArray) -> void:
	_out(Diagnostics.version())


func _cmd_maps(args: PackedStringArray) -> void:
	var filter := args[0].to_lower() if args.size() else ""
	var ids := PackedStringArray()
	for m in Session.maps:
		var id := String(m.get("map", ""))
		if id != "" and (filter == "" or id.contains(filter)):
			ids.append(id)
	ids.sort()
	_out(("  " + "  ".join(ids)) if ids.size() else "No maps match.")


func _cmd_cheats(args: PackedStringArray) -> void:
	if args.is_empty():
		_out("Cheats are %s." % ("on" if _engine_ready() and Q3Server.cvar("sv_cheats") == "1" else "off"))
		return
	var why := restricted()
	if why != "":
		_out("^1Locked: %s." % why)
	elif not _engine_ready():
		_out("^1The game engine isn't running yet (it starts with the first match).")
	else:
		# sv_cheats is read only to set and to typing it, as in Quake 3; this does what devmap would
		Q3Server.set_cvar("sv_cheats", "1" if String(args[0]).to_lower() in TRUE_WORDS else "0")
		_out("Cheats are %s." % ("on" if Q3Server.cvar("sv_cheats") == "1" else "off"))


func _cmd_setting(args: PackedStringArray) -> void:
	var sub := args[0] if args.size() else "list"
	if sub == "list":
		var filter := args[1].to_lower() if args.size() > 1 else ""
		var n := 0
		for key in _setting_keys():
			if filter == "" or String(key).to_lower().contains(filter):
				_out("  %s = %s" % [key, _show(Settings.get_value(key))])
				n += 1
		_out("%d settings" % n)
		return
	if sub == "reset":
		if args.size() < 2 or not Settings.has(args[1]):
			_out("setting reset <key>")
			return
		Settings.set_value(args[1], Settings.default_of(args[1]))
		_out("%s = %s" % [args[1], _show(Settings.get_value(args[1]))])
		return
	if not Settings.has(sub):
		var near: Array = _setting_keys().filter(func(k): return String(k).contains(sub))
		_out("^1No setting \"%s\".%s" % [sub, (" Did you mean: " + ", ".join(near.slice(0, 8))) if near.size() else " 'setting list' shows them."])
		return
	if args.size() == 1:
		_out("%s = %s  (default %s)  %s" % [sub, _show(Settings.get_value(sub)), _show(Settings.default_of(sub)), Settings.entry(sub).get("label", "")])
		return
	Settings.set_value(sub, _parse_like(Settings.get_value(sub), " ".join(args.slice(1))))
	_out("%s = %s" % [sub, _show(Settings.get_value(sub))])


# ---------------------------------------------------------------- helpers

static func _setting_keys() -> Array:
	var keys: Array = []
	for e in Settings.entries:
		keys.append(String(e["key"]))
	return keys


static func _show(v: Variant) -> String:
	if v is Color:
		return "#" + (v as Color).to_html(false)
	return str(v)


## A typed word as the same kind of value as the setting it's for.
static func _parse_like(current: Variant, text: String) -> Variant:
	match typeof(current):
		TYPE_BOOL: return text.to_lower() in TRUE_WORDS
		TYPE_INT: return text.to_int()
		TYPE_FLOAT: return text.to_float()
		TYPE_COLOR: return Color.html(text)
	return text
