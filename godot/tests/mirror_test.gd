extends Node
## Windowed look at a mirror in a real match: the player stands in front of q3dm0's mirror
## (or map=/pos=) and the view is saved at a few moments: the player's own body in the mirror,
## no first-person gun in it.
##   godot --path . res://tests/mirror_test.tscn -- out=C:/temp/mirror [map=q3dm0 pos=x,y,z,yaw|spawn]
## pos=spawn keeps the first spawn (q3dm0's initial spawn point faces the mirror). The view looks
## up, level, then down (pitch=0: level throughout).

const AT := [3.0, 3.6, 4.4]
var out := "user://mirror"
var map := "q3dm0"
var pos := "-1154 -1300 40 90"    ## id units and yaw: by the mirror, facing away (the teleport's
                                   ## 400 ups shove carries the player off), then turned round
var turn := 180.0
var pitch_scale := 1.0     ## pitch=0: look straight ahead
var _game: Q3Gameplay
var _t := 0.0
var _shot := 0


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			out = a.substr(4)
		elif a.begins_with("map="):
			map = a.substr(4)
		elif a == "pitch=0":
			pitch_scale = 0.0
		elif a.begins_with("pos="):
			pos = a.substr(4).replace(",", " ")
			turn = 0.0
	Engine.max_fps = 60
	var s := Session.new_setup("ffa", map)
	s.set_meta("devmap", true)
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	_game.test_input = true
	add_child(_game)


func _process(delta: float) -> void:
	_t += delta
	if _t > 1.5 and _t - delta <= 1.5 and pos != "spawn":
		Q3Server.client_command(_game.me, "setviewpos " + pos)
	if _t > 2.5:
		# the spawn's (or setviewpos') own yaw, so the test window's mouse can't turn it; looking
		# up, then down, to see the body aim in the mirror (pitch=0 keeps it level)
		_game._view = Vector3((-40.0 if _t < 3.3 else 0.0 if _t < 4.0 else 40.0) * pitch_scale, turn, 0.0)
		for move in ["q3_forward", "q3_back", "q3_left", "q3_right", "q3_jump", "q3_crouch"]:
			if InputMap.has_action(move):
				Input.action_release(move)   # keys pressed elsewhere while the window has focus
	if _t > 3.3 and _t - delta <= 3.3:
		_game._select_weapon(5)   # a switch, seen in the mirror
	if _shot < AT.size() and _t > AT[_shot]:
		_shot += 1
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png("%s_%d.png" % [out, _shot - 1])
		var portals := _game.map_scene.portals if _game.map_scene else []
		print("MIRROR t %.1f portals %d self body %s origin %s" % [_t, portals.size(), _game.client.self_body,
			Q3Server.player_state(_game.me).get("origin", Vector3.ZERO)])
	if _shot >= AT.size():
		get_tree().quit()
