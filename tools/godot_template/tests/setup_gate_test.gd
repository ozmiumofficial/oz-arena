extends Node
## Windowed check of the launch gate: with no verified Quake III Arena the splash shows the setup
## screen and goes no further; a fake pak0.pk3 dropped on the window is refused; the real one (from
## the local install) is accepted and the splash moves on. Drops go through the window's
## files_dropped signal, as a drag from Explorer does. Frames are saved at each step.
##   godot --path . res://tests/setup_gate_test.tscn -- window=1600x900 out=C:/temp/gate
## Prints "SETUP GATE <passed>/<total>". The player's remembered folder is put back afterwards.

var out := "user://gate"
var _pass := 0
var _total := 0
var _splash: Node


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			out = a.substr(4)
	var saved := Q3Ownership.remembered()
	Q3Ownership.forget()
	await _run()
	Q3Ownership.forget()
	if saved != "":
		Q3Ownership.accept(saved)
	print("SETUP GATE %d/%d" % [_pass, _total])
	get_tree().quit(0 if _pass == _total else 1)


func _run() -> void:
	_splash = load(Session.SPLASH).instantiate()
	add_child(_splash)
	await _wait(7.0)
	_check(_splash._gate != null, "no verified copy: the setup screen shows")
	await _shot("1_setup")
	await _wait(10.0)
	_check(not _splash._leaving and _splash._gate != null, "and the game doesn't go on without one")

	var fake := ProjectSettings.globalize_path("user://gate_test/baseq3")
	DirAccess.make_dir_recursive_absolute(fake)
	var zp := ZIPPacker.new()
	zp.open(fake.path_join("pak0.pk3"))
	zp.start_file("maps/q3dm17.bsp")
	zp.write_file("not really".to_utf8_buffer())
	zp.close_file()
	zp.close()
	get_window().files_dropped.emit(PackedStringArray([fake.path_join("pak0.pk3")]))
	await _wait(0.5)
	_check(_splash._gate != null and not _splash._leaving and Q3Ownership.verified() == "", "a fake pak0 is refused")
	await _shot("2_refused")

	var real := Q3Ownership.find_install()
	if real == "":
		print("  (no Quake III Arena install found here; the accept step is skipped)")
		return
	get_window().files_dropped.emit(PackedStringArray([real.path_join("pak0.pk3")]))
	await _wait(0.4)
	_check(Q3Ownership.verified() == real, "the real pak0 is accepted")
	await _shot("3_verified")
	await _wait(0.7)   # the screen holds 0.9 s, then the splash fades for 0.5 s and changes scene
	_check(_splash._gate == null and _splash._leaving, "and the splash goes on to the game")


func _wait(s: float) -> void:
	await get_tree().create_timer(s).timeout


func _shot(name: String) -> void:
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png("%s_%s.png" % [out, name])


func _check(ok: bool, what: String) -> void:
	_total += 1
	if ok:
		_pass += 1
	print("  %s %s" % ["ok  " if ok else "FAIL", what])
