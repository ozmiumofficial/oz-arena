class_name Q3Host
extends RefCounted
## Starts id's game code (the q3core extension: ioquake3's server, game module and botlib) inside
## Godot, once per process. Game data (.bsp collision, .aas bot navigation, botfiles, scripts)
## comes from the player's own pak0-8.pk3: the baseq3 folder the setup screen verified
## (Q3Ownership), handed to ioquake3 as fs_steampath. Without one, ioquake3 looks for the Steam
## install itself. Either way it checks pak0 again (FS_CheckPak0).
##
## Q3Server is the extension's raw API; this class adds the paths, the 125 Hz setup and helpers.

const BIN := "res://addons/q3core/bin"     ## holds baseq3/qagamex86_64.dll (fs_basepath)
const EXPORTED_BIN := "q3core"             ## the same next to an exported game's exe (tools/export_game.py)
const HOME := "user://q3"                  ## configs and logs (fs_homepath)
const FRAME_MSEC := 8                      ## sv_fps 125, pmove_msec 8

## Cvars every match starts with. pmove_fixed 1 + pmove_msec 8 gives the 125 fps movement feel at
## any frame rate; sv_fps 125 runs the game frame and snapshots every 8 ms.
const BOOT_CVARS := {
	"dedicated": "1", "net_enabled": "0", "sv_pure": "0", "vm_game": "0", "com_hunkmegs": "128",
	"sv_fps": "125", "pmove_fixed": "1", "pmove_msec": "8", "bot_enable": "1", "sv_maxclients": "16",
}


static func available() -> bool:
	return ClassDB.class_exists("Q3Server")


## Starts the engine; "" on success, else the reason. `q3_path` (a baseq3 folder) overrides the
## folder the setup screen verified, which overrides the Steam auto-detection.
static func start(q3_path := "", extra := {}) -> String:
	if not available():
		return "The q3core extension isn't built (python tools/build_q3core.py)."
	if Q3Server.is_ready():
		return ""
	if q3_path == "":
		q3_path = Q3Ownership.remembered()
	var home := ProjectSettings.globalize_path(HOME)
	DirAccess.make_dir_recursive_absolute(home)
	var cvars := BOOT_CVARS.duplicate()
	cvars["fs_basepath"] = bin_path()
	cvars["fs_homepath"] = home
	if q3_path != "":
		cvars["fs_steampath"] = q3_path.get_base_dir() if q3_path.ends_with("baseq3") else q3_path
	cvars.merge(extra, true)
	var line := ""
	for k in cvars:
		line += "+set %s \"%s\" " % [k, String(cvars[k]).replace("\\", "/")]
	if Q3Server.start(line) != 0:
		return Q3Server.error()
	return ""


## Where the game module lives: the addon folder in the editor, beside the exe once exported (a
## .pck holds no DLLs the engine could load).
static func bin_path() -> String:
	if OS.has_feature("editor"):
		return ProjectSettings.globalize_path(BIN)
	return OS.get_executable_path().get_base_dir().path_join(EXPORTED_BIN)


## Quake 3's userinfo string for a local player.
static func userinfo(player: Dictionary) -> String:
	var info := {"name": player.get("name", "Player"), "model": "%s/%s" % [player.get("model", "sarge"), player.get("skin", "default")],
		"headmodel": "%s/%s" % [player.get("model", "sarge"), player.get("skin", "default")], "handicap": "100",
		"color1": "4", "color2": "5", "sex": "male", "rate": "25000", "team": player.get("team", "")}
	var s := ""
	for k in info:
		s += "\\%s\\%s" % [k, info[k]]
	return s


## Parses a Quake 3 info string ("\key\value\...") into a Dictionary.
static func parse_info(s: String) -> Dictionary:
	var parts := s.trim_prefix("\\").split("\\")
	var out := {}
	for i in range(0, parts.size() - 1, 2):
		out[parts[i]] = parts[i + 1]
	return out
