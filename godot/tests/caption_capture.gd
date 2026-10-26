extends Node
## Windowed shots of Accessibility > Subtitles against the HUD around them: three announcer lines
## up at once with the speedometer, a center print and the low ammo warning showing, then the same
## while dead. Settings are changed for this run only.
##   godot --path . res://tests/caption_capture.tscn -- out=C:/temp/cap preset=modern
## Writes <out>_alive.png and <out>_dead.png and prints "CAPTION CAPTURE done".

var out := "user://cap"
var _game: Q3Gameplay
var _t := 0.0
var _shots := 0
var _saving := false
var _old := {}


func _ready() -> void:
	process_priority = 1000   # after Gameplay's HUD update, so the forced state below holds
	var preset := "modern"
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			out = a.substr(4)
		if a.begins_with("preset="):
			preset = a.substr(7)
	for kv in [["hud/preset", preset], ["accessibility/subtitles", true], ["hud/show_speedometer", true]]:
		_old[kv[0]] = Settings.get_value(kv[0])
		Settings.set_value(kv[0], kv[1], false)
	Engine.max_fps = 60
	var s := Session.new_setup("ffa", "q3dm6")
	s.set_meta("devmap", true)
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	add_child(_game)


func _process(delta: float) -> void:
	var hud := _game.hud
	if hud == null or hud._ops.is_empty() or _game.map_scene == null or _game.map_scene.has_node("ProbeCover"):
		return
	_t += delta   # from when Q3Map's black probe cover is gone and its fade done
	if _t < 1.0:
		return
	if _shots == 0 and hud._captions.is_empty():
		for line in ["sound/feedback/impressive.wav", "sound/feedback/excellent.wav", "sound/player/announce/sarge_wins.wav"]:
			hud._caption(line)
		hud.center_print("You fragged Sarge\n1st place with 3")
	for c in hud._captions:
		c["t"] = 0.5   # held on screen
	hud._center_t = 0.5
	hud._low_ammo = 1
	if _shots == 1:
		hud._dead = true
		hud.killer = "Sarge"
	if _t > 1.6 + _shots * 0.6 and not _saving:
		_saving = true
		await RenderingServer.frame_post_draw
		_saving = false
		get_viewport().get_texture().get_image().save_png("%s_%s.png" % [out, "alive" if _shots == 0 else "dead"])
		_shots += 1
		if _shots == 2:
			for k in _old:
				Settings.set_value(k, _old[k], false)
			print("CAPTION CAPTURE done")
			get_tree().quit()
