class_name Q3DedicatedLauncher
extends RefCounted
## Starts, watches and stops this PC's dedicated server (scenes/dedicated, MULTIPLAYER_PLAN.md M7) from the menu. The
## server is the same executable run again headless, so it keeps running after the game closes; the menu and the
## server talk through files in user://dedicated (Q3Dedicated documents them): config.json to the server, status.json
## back, a `stop` file to end it, commands.txt for the local admin's console commands.

const RUN_KEY := "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"
const RUN_NAME := "Q3RemasterDedicatedServer"
## A status older than this means the server is gone (it writes one every 2 s).
const FRESH := 8.0


static func status() -> Dictionary:
	var f := FileAccess.open(Q3Dedicated.status_path(), FileAccess.READ)
	if f == null:
		return {"running": false}
	var d: Variant = JSON.parse_string(f.get_as_text())
	if not d is Dictionary:
		return {"running": false}
	d["running"] = Time.get_unix_time_from_system() - float(d.get("time", 0)) < FRESH
	return d


## The command line that runs the server on `config_path` (an absolute path): the project in the editor, the exported
## exe otherwise.
static func command_line(config_path: String) -> PackedStringArray:
	var args := PackedStringArray(["--headless"])
	if OS.has_feature("editor"):
		args.append_array(["--path", ProjectSettings.globalize_path("res://")])
	args.append_array(["--log-file", ProjectSettings.globalize_path(Q3Dedicated.DIR.path_join("server.log")), "--", "dedicated=" + config_path])
	return args


## Saves the config and starts the server; "" on success, else why not.
static func start(config: Dictionary) -> String:
	if status().get("running", false):
		return "A dedicated server is already running."
	if Q3Ownership.remembered() == "" and Q3Ownership.find_install() == "":
		return "The game files aren't set up."
	Q3Dedicated.save_config(config)
	DirAccess.remove_absolute(Q3Dedicated.DIR.path_join("stop"))
	DirAccess.remove_absolute(Q3Dedicated.status_path())
	var pid := OS.create_process(OS.get_executable_path(), command_line(ProjectSettings.globalize_path(Q3Dedicated.config_path())))
	return "" if pid > 0 else "Couldn't start the server process."


## Asks it to shut down (it checks every frame, so within a moment).
static func stop() -> void:
	FileAccess.open(Q3Dedicated.DIR.path_join("stop"), FileAccess.WRITE)


## A console command for the running server (kick <name>, map q3dm6, say hello ...).
static func command(line: String) -> void:
	var l := line.replace("\n", " ").strip_edges()
	if l == "" or not status().get("running", false):
		return
	var path := Q3Dedicated.DIR.path_join("commands.txt")
	var f := FileAccess.open(path, FileAccess.READ_WRITE if FileAccess.file_exists(path) else FileAccess.WRITE)
	if f:
		f.seek_end()
		f.store_line(l)


## "Start with Windows": a Run key for this user that starts the server when they sign in. Windows only; the menu asks
## first, since it changes the PC's startup list.
static func autostart_command() -> String:
	var parts := PackedStringArray(["\"%s\"" % OS.get_executable_path()])
	for a in command_line(ProjectSettings.globalize_path(Q3Dedicated.config_path())):
		parts.append("\"%s\"" % a if a.contains(" ") else a)
	return " ".join(parts)


static func autostart_enabled() -> bool:
	if OS.get_name() != "Windows":
		return false
	var out := []
	return OS.execute("reg", ["query", RUN_KEY, "/v", RUN_NAME], out, true) == 0


static func set_autostart(on: bool) -> bool:
	if OS.get_name() != "Windows":
		return false
	var out := []
	if on:
		return OS.execute("reg", ["add", RUN_KEY, "/v", RUN_NAME, "/t", "REG_SZ", "/d", autostart_command(), "/f"], out, true) == 0
	return OS.execute("reg", ["delete", RUN_KEY, "/v", RUN_NAME, "/f"], out, true) == 0
