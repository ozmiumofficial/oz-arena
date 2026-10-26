extends Node3D
## What a respawn's new body costs, step by step (match_perf's 100 ms stalls at every respawn).
##   godot --path . res://tests/char_spawn_perf.tscn -- models=sarge,xaero rounds=4
## Each round builds every model's body as Q3Client._spawn does, then frees it. The first round is
## a first load; later rounds are what a match pays at each respawn.

var models := ["sarge", "xaero", "visor", "grunt", "hunter"]
var rounds := 4
var bright := false   ## bright=1: Gameplay > Bright skins on


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("models="):
			models = Array(a.substr(7).split(","))
		elif a.begins_with("rounds="):
			rounds = int(a.substr(7))
		elif a == "bright=1":
			bright = true
	Q3.hold_models(true)
	var cam := Camera3D.new()
	add_child(cam)
	cam.position = Vector3(0, 1, 4)
	await get_tree().process_frame
	for r in rounds:
		for m in models:
			await _one(r, m)
	Q3.hold_models(false)
	get_tree().quit(0)


func _one(r: int, m: String) -> void:
	var t := {}
	var t0 := Time.get_ticks_usec()
	var scene := Q3.character_scene(m)
	t["load"] = Time.get_ticks_usec()
	var body := scene.instantiate()
	t["instantiate"] = Time.get_ticks_usec()
	body.free()
	var ch := Q3Character.new()
	ch.model = m
	ch.skin = "default"
	ch.turn = true
	ch.match_clock = true
	var t1 := Time.get_ticks_usec()
	add_child(ch)   # _ready -> load_model: instantiate, tree, aim, skin
	t["add_child"] = Time.get_ticks_usec()
	ch.bright_skin(Color(0.2, 1.0, 0.2) if bright else Color(0, 0, 0, 0))
	t["bright"] = Time.get_ticks_usec()
	ch.advance(8.0)
	t["advance1"] = Time.get_ticks_usec()
	ch.advance(8.0)
	t["advance2"] = Time.get_ticks_usec()
	ch.attach("weapons2/rocketl/rocketl")
	t["attach"] = Time.get_ticks_usec()
	await get_tree().process_frame
	t["frame"] = Time.get_ticks_usec()
	var parts: Array = []
	var last := t0
	for k in ["load", "instantiate"]:
		parts.append("%s %.1f" % [k, (t[k] - last) / 1000.0])
		last = t[k]
	last = t1
	for k in ["add_child", "bright", "advance1", "advance2", "attach", "frame"]:
		parts.append("%s %.1f" % [k, (t[k] - last) / 1000.0])
		last = t[k]
	print("SPAWN r%d %-8s total %.1f ms: %s" % [r, m, (t["attach"] - t1) / 1000.0, ", ".join(parts)])
	ch.queue_free()
	await get_tree().process_frame
