extends Node
## The results screen's podium, windowed (3D needs a GPU): sample standings with you second, a shot when it has
## settled, then your first taunt and a shot mid-gesture.
##   godot --path . res://tests/podium_capture.tscn -- out=C:/path/shots [window=1920x1080]
## Prints "PODIUM CAPTURE ok|FAILED".

var out := ""
var results: Q3ResultsScreen
var shots := 0


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			out = a.substr(4)
	var rows := [
		{"client": 1, "name": "Xaero", "plain": "Xaero", "score": 20, "model": "xaero", "skin": "default", "bot": true, "card": {}, "level": 12},
		{"client": 0, "name": "You", "plain": "You", "score": 17, "model": "sarge", "skin": "default", "local": true, "card": Q3Profile.card(), "level": 5},
		{"client": 2, "name": "Klesk", "plain": "Klesk", "score": 9, "model": "klesk", "skin": "default", "bot": true, "card": {}, "level": 3},
		{"client": 3, "name": "Doom", "plain": "Doom", "score": 4, "model": "doom", "skin": "default", "bot": true, "card": {}, "level": 1},
	]
	# the map behind, from its intermission spot, as Gameplay shows it at the end of a match
	var map := (load(Q3MapBuilder.scene_path("q3dm17")) as PackedScene).instantiate()
	add_child(map)
	var cam := Camera3D.new()
	add_child(cam)
	if map.has_method("intermission_transform"):
		cam.global_transform = map.intermission_transform()
	cam.current = true
	results = Q3ResultsScreen.new()
	add_child(results)
	results.taunt_requested.connect(func(id): results.play_taunt(0, id))   # as the server's echo would
	results.show_results({"won": false, "title": "DEFEAT", "subtitle": "The Longest Yard  -  2nd of 4", "standings": rows,
		"medals": {"impressive": 2}, "xp": {"before": 1000, "after": 1400, "lines": [["Match complete", 100]]}, "me": 0})
	await get_tree().create_timer(3.0).timeout
	await _shot("podium.png")
	results._taunt(0)
	await get_tree().create_timer(0.8).timeout
	await _shot("podium_taunt.png")
	print("PODIUM CAPTURE ", "ok" if shots == 2 else "FAILED")
	get_tree().quit(0 if shots == 2 else 1)


func _shot(name: String) -> void:
	await RenderingServer.frame_post_draw
	var img := get_viewport().get_texture().get_image()
	if out != "" and img.save_png(out.path_join(name)) == OK:
		shots += 1
		print("saved ", out.path_join(name))
