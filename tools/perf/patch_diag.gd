extends Node
## Stand-in for the Diagnostics autoload (first in the list) in tools/perf/build_perf.py's copy of a
## release build. Before any other autoload loads it:
##   - fills the copy's own user folder from the player's: settings (offline: network/online off and
##     an unreachable server, so the copy never logs in as the player) and the remembered Quake 3
##     folder. Done here, not in Python: the Microsoft Store Python redirects its AppData writes.
##   - with PATCH_FILES set (res paths, comma separated; PATCH_SRC the source root, PATCH_TMP a
##     scratch folder), packs those .gd files with .remap files pointing at them and mounts the pack
##     over the build's compiled scripts. It names no other autoload, so nothing (q3.gd) is loaded
##     before the mount. Diagnostics' API is stubbed.

const SESSIONS := "user://diagnostics/sessions"
var session_id := "perf"
var started := 0.0
var is_test := true
var context := {}
var breadcrumbs: PackedStringArray = []
var new_reports: PackedStringArray = []
var notice := ""
var logger = null


func _init() -> void:
	_user_folder()
	var src := OS.get_environment("PATCH_SRC")
	var files := OS.get_environment("PATCH_FILES").split(",", false)
	if src == "" or files.is_empty():
		return
	var tmp := OS.get_environment("PATCH_TMP")
	var pck := tmp.path_join("patch.pck")
	var p := PCKPacker.new()
	p.pck_start(pck)
	for res in files:
		var rel := String(res).trim_prefix("res://")
		var remap := tmp.path_join(rel.replace("/", "_") + ".remap")
		var f := FileAccess.open(remap, FileAccess.WRITE)
		f.store_string("[remap]\n\npath=\"%s\"\n" % res)
		f.close()
		p.add_file(res, src.path_join(rel))
		p.add_file(res + ".remap", remap)
	p.flush()
	print("PATCH mounted %s: %s" % [ProjectSettings.load_resource_pack(pck, true), ", ".join(files)])
	for res in files:
		if ResourceLoader.has_cached(res):
			print("PATCH WARNING already loaded before the mount: ", res)


func _user_folder() -> void:
	var player := OS.get_environment("APPDATA").path_join("Godot/app_userdata/Q3Remaster")
	var cfg := ConfigFile.new()
	cfg.load(player.path_join("settings.cfg"))
	cfg.set_value("network", "online", false)
	cfg.set_value("network", "online_server", "ws://127.0.0.1:1")
	cfg.save("user://settings.cfg")
	DirAccess.make_dir_recursive_absolute("user://q3")
	if FileAccess.file_exists(player.path_join("q3/ownership.cfg")):
		DirAccess.copy_absolute(player.path_join("q3/ownership.cfg"), "user://q3/ownership.cfg")


func version() -> String:
	return "perf"


func note(_text: String) -> void:
	pass


func set_context(key: String, value: Variant) -> void:
	context[key] = value


func toggle_overlay() -> void:
	pass


func state() -> Dictionary:
	return {}


func expect_stall(_on: bool) -> void:
	pass


func collect_crashes() -> PackedStringArray:
	return PackedStringArray()


func open_reports_folder() -> void:
	pass


static func reports() -> Array:
	return []
