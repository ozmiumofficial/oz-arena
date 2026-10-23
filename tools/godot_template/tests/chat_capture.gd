extends Node
## Windowed look at the chat: a few lines through id's G_Say (your colored name, a bot's), a server
## print, the chat open with a color-code preview, then a bot seen from the side with the talk
## balloon on (forced on for the shot; bots raise it themselves before they chat).
##   godot --path . res://tests/chat_capture.tscn -- out=C:/temp/chat
## Writes <out>_open.png and <out>_balloon.png and prints "CHAT CAPTURE done".

var _game: Q3Gameplay
var _t := 0.0
var _step := 0
var _out := "user://chat"
var _cam: Camera3D
var _bot: Node3D


func _ready() -> void:
	process_priority = 100   # after Gameplay, so the forced balloon survives its snapshot
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			_out = a.substr(4)
	Engine.max_fps = 60
	var s := Session.new_setup("ffa", "q3dm17")
	s.bots = [{"name": "Sarge", "skill": 1, "team": ""}, {"name": "Xaero", "skill": 1, "team": ""}]
	s.player["name"] = "^1Tony^3 Q"
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	_game.test_input = true
	add_child(_game)


func _process(delta: float) -> void:
	_t += delta
	match _step:
		0:
			if _t > 3.0:
				var bot := -1
				for r in _game.standings():
					if r["bot"]:
						bot = int(r["client"])
				Q3Server.client_command(_game.me, "say \"anyone up for a ^1rail^7 duel?\"")
				if bot >= 0:
					Q3Server.client_command(bot, "say \"you're on\"")   # bots aren't local clients: ignored if refused
				_game.hud.chat.add(_game._name(bot) + "^7: ^2you'll need more than ^3luck")
				_game.hud.notify("^5Xaero^7 entered the game")
				_step = 1
		1:
			if _t > 3.6:
				_game.hud.chat.open(false)
				_game.hud.chat._edit.text = "gg ^1red ^4blue ^3gold"
				_game.hud.chat._edit.caret_column = 999
				_step = 2
		2:
			if _t > 4.2:
				_save("_open")
				_game.hud.chat.close()
				_step = 3
		3:
			if _t > 4.4:
				for num in _game.client._ents:
					var e: Dictionary = _game.client._ents[num]
					if e["kind"] == 1 and e["node"] is Q3Character and not e.get("ragdolled", false):
						_bot = e["node"]
						break
				if _bot:
					_cam = Camera3D.new()
					add_child(_cam)
					_cam.make_current()
				_step = 4
		4:
			for num in _game.client._ents:
				var e: Dictionary = _game.client._ents[num]
				if e["kind"] == 1 and e["node"] is Q3Character and not e.get("ragdolled", false):
					_bot = e["node"]
					break
			if _bot and is_instance_valid(_bot):
				_game.client._talk_balloon(_bot, true, false)
				var at := _bot.global_position + Vector3.UP * 1.6
				_cam.global_position = at + _bot.global_basis.x * 3.2 + Vector3.UP * 0.4
				_cam.look_at(at)
			if _t > 5.2:
				var b := _bot.get_node_or_null("TalkBalloon") as Sprite3D if _bot and is_instance_valid(_bot) else null
				print("balloon: ", b != null, " ", b.is_visible_in_tree() if b else false, " ", b.global_position if b else Vector3.ZERO,
					" tex ", b.texture.get_size() if b and b.texture else Vector2.ZERO, " bot ", _bot.global_position if _bot and is_instance_valid(_bot) else Vector3.ZERO,
					" cam ", _cam.global_position if _cam else Vector3.ZERO)
				_save("_balloon")
				print("CHAT CAPTURE done")
				Q3Server.exec_now("killserver")
				get_tree().quit()
				_step = 5


func _save(suffix: String) -> void:
	var img := get_viewport().get_texture().get_image()
	img.save_png(_out + suffix + ".png")
	print("saved ", _out + suffix + ".png")
