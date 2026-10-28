extends Node
## The first autoload (UPDATES_PLAN.md U3): mounts the release's patch pack over Q3Remaster.pck before
## any other script loads. A release ships the pack of its base release plus patch.pck, every file
## changed since that base (godot --export-patch), so an update downloads megabytes, not the 1.1 GB pack.
##
## It mounts in _init: the main scene and the other autoloads load after it, so they read the patched
## files, and Godot picks up the patch's class list, so new class_name scripts work too (checked
## 2026-10-27). project.godot is read before any of this, so a change to it needs a new base;
## export_game.py --release cuts one by itself. Never change this script in a patch either.
##
## The version every release bumps would be such a change, so the build's version comes from
## game/version.txt (export_game.py writes it from project.godot), which a patch carries, and goes
## over application/config/version here, where everything reads it.


func _init() -> void:
	if OS.has_feature("editor"):
		return
	var patch := OS.get_executable_path().get_base_dir().path_join("patch.pck")
	var mounted := FileAccess.file_exists(patch) and ProjectSettings.load_resource_pack(patch, true)
	if FileAccess.file_exists(patch) and not mounted:
		push_error("UpdateBoot: couldn't mount " + patch)
	var version := FileAccess.get_file_as_string("res://game/version.txt").strip_edges()
	if version != "":
		ProjectSettings.set_setting("application/config/version", version)
	print("[update] Q3Remaster %s%s" % [ProjectSettings.get_setting("application/config/version", "?"), " (patch.pck)" if mounted else ""])
