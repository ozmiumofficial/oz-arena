extends Node
## Headless check of the ownership gate (Q3Ownership): MD4 against RFC 1320's vectors, every retail
## pak in the local install against id's checksum table, and each kind of drop: pak0.pk3, another
## pak, the baseq3 folder, the game folder, a fake pak0, a wrong file. The player's own remembered
## folder is put back afterwards.
##   godot --headless --path . res://tests/ownership_test.tscn
## Prints "OWNERSHIP <passed>/<total>".

var _pass := 0
var _total := 0


func _ready() -> void:
	var saved := Q3Ownership.remembered()
	_run()
	Q3Ownership.forget()
	if saved != "":
		Q3Ownership.accept(saved)
	print("OWNERSHIP %d/%d" % [_pass, _total])
	get_tree().quit(0 if _pass == _total else 1)


func _run() -> void:
	_check(Q3Ownership.md4(PackedByteArray()).hex_encode() == "31d6cfe0d16ae931b73c59d7e0c089c0", "md4 of nothing")
	_check(Q3Ownership.md4("abc".to_utf8_buffer()).hex_encode() == "a448017aaf21d8525fc10ae87aa6729d", "md4 of abc")
	var long := "12345678901234567890123456789012345678901234567890123456789012345678901234567890"
	_check(Q3Ownership.md4(long.to_utf8_buffer()).hex_encode() == "e33b4ddc9c38f2199c3e7b164fcc0536", "md4 of 80 digits")

	# a fake pak0: a zip that isn't id's
	var fake := ProjectSettings.globalize_path("user://ownership_test/baseq3")
	DirAccess.make_dir_recursive_absolute(fake)
	var zp := ZIPPacker.new()
	zp.open(fake.path_join("pak0.pk3"))
	zp.start_file("maps/q3dm17.bsp")
	zp.write_file("not really".to_utf8_buffer())
	zp.close_file()
	zp.close()
	Q3Ownership.forget()
	var r := Q3Ownership.accept(fake.path_join("pak0.pk3"))
	_check(not r["ok"] and "retail" in String(r["message"]), "a fake pak0 is refused: " + String(r["message"]))
	_check(Q3Ownership.verified() == "", "nothing verified after a refused drop")
	r = Q3Ownership.accept(fake.get_base_dir().path_join("readme.txt"))
	_check(not r["ok"], "a file that isn't a pak is refused")
	r = Q3Ownership.accept(ProjectSettings.globalize_path("user://ownership_test"))
	_check(not r["ok"], "a folder with a fake pak0 is refused")
	r = Q3Ownership.accept(ProjectSettings.globalize_path("user://"))
	_check(not r["ok"] and "no pak0" in String(r["message"]), "a folder without pak0 is refused")

	var real := Q3Ownership.find_install()
	if real == "":
		print("  (no Quake III Arena install found here; the retail checks are skipped)")
		return
	for i in Q3Ownership.PAK_CHECKSUMS.size():
		var p := real.path_join("pak%d.pk3" % i)
		if FileAccess.file_exists(p):
			_check(Q3Ownership.pak_checksum(p) == Q3Ownership.PAK_CHECKSUMS[i], "pak%d.pk3 is retail" % i)
	for drop in [real.path_join("pak0.pk3"), real.path_join("pak3.pk3"), real, real.get_base_dir()]:
		Q3Ownership.forget()
		r = Q3Ownership.accept(drop)
		_check(r["ok"] and r["baseq3"] == real and Q3Ownership.verified() == real, "drop %s" % drop.get_file())
	_check(int(r["paks"]) == Q3Ownership.retail_paks(real), "pak count reported")
	# a remembered folder whose pak0 is gone no longer counts
	var cfg := ConfigFile.new()
	cfg.set_value("quake3", "baseq3", fake.get_base_dir().path_join("gone"))
	cfg.save(Q3Ownership.CONFIG)
	_check(Q3Ownership.verified() == "", "a missing pak0 brings the setup back")


func _check(ok: bool, what: String) -> void:
	_total += 1
	if ok:
		_pass += 1
	print("  %s %s" % ["ok  " if ok else "FAIL", what])
