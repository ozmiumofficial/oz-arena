extends Node
## Headless check of the in-game chat and Quake 3's color codes (events fed through Godot's input):
## Q3Colors parses codes as q_shared.h does; T opens the chat and the line edit takes the keys;
## while typing you stand still and the game sets EF_TALK; a line with color codes goes through
## id's G_Say and comes back as "chat" with your colored name and ^2; the Enter that sent it isn't
## Use item; Escape closes the chat without pausing; Y sends say_team (a plain say in FFA).
##   godot --headless --path . res://tests/chat_test.tscn

const E := preload("res://scripts/game/q3_enums.gd")
const NAME := "^4Tes^1ter"

var _game: Q3Gameplay
var _t := 0.0
var _step := 0
var _ok := true
var _origin := Vector3.ZERO


func _ready() -> void:
	_colors()
	var s := Session.new_setup("ffa", "q3dm17")
	s.bots = [{"name": "Sarge", "skill": 1, "team": ""}]
	s.player["name"] = NAME
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	_game.test_input = true
	add_child(_game)


func _colors() -> void:
	var r := Q3Colors.runs("^1Sar^7ge", Color(0.5, 0.5, 0.5, 0.5))
	_check("runs: red then white", r.size() == 2 and r[0][0] == "Sar" and r[0][1] == Color(1, 0, 0, 0.5) and r[1][1] == Color(1, 1, 1, 0.5))
	_check("strip", Q3Colors.strip("^1Sar^7ge^") == "Sarge^")
	_check("^^ isn't a code", Q3Colors.strip("a^^1b") == "a^b")
	_check("letters wrap to the eight", Q3Colors.runs("^ax")[0][1] == Q3Colors.runs("^1x")[0][1] and Q3Colors.runs("^Ax")[0][1] == Q3Colors.runs("^1x")[0][1])
	var font := ThemeDB.fallback_font
	var lines := Q3Colors.wrap(font, "^1" + "word ".repeat(30), 16, 120.0)
	var red := true
	for l in lines:
		for run in l:
			red = red and run[1] == Color(1, 0, 0)
	_check("wrap: %d lines, the color carries over" % lines.size(), lines.size() > 3 and red)


func _key(code: Key, unicode := 0) -> void:
	var ev := InputEventKey.new()
	ev.keycode = code
	ev.physical_keycode = code
	ev.unicode = unicode
	ev.pressed = true
	Input.parse_input_event(ev)
	var up := ev.duplicate() as InputEventKey
	up.pressed = false
	Input.parse_input_event(up)


func _action(a: String) -> void:
	var ev := InputEventAction.new()
	ev.action = a
	ev.pressed = true
	Input.parse_input_event(ev)
	var up := ev.duplicate() as InputEventAction
	up.pressed = false
	Input.parse_input_event(up)


func _ps() -> Dictionary:
	return Q3Server.player_state(_game.me)


func _last_line() -> String:
	var l := _game.hud.chat.lines()
	return String(l[-1]["text"]) if not l.is_empty() else ""


func _process(delta: float) -> void:
	_t += delta
	var chat := _game.hud.chat if _game and _game.hud else null
	match _step:
		0:
			if _t > 3.0:
				_action("q3_chat")
				_step = 1
		1:
			if _t > 3.2:
				_check("T opens the chat", chat.typing and not chat.team)
				_key(KEY_H, "h".unicode_at(0))
				_key(KEY_I, "i".unicode_at(0))
				Input.action_press("q3_forward")
				_origin = _ps()["origin"]
				_step = 2
		2:
			if _t > 3.8:
				_check("the line edit takes the keys: '%s'" % chat._edit.text, chat._edit.text == "hi")
				var ps := _ps()
				_check("typing: EF_TALK is set", int(ps["eFlags"]) & E.EF_TALK != 0)
				var moved := Vector2((ps["origin"] as Vector3).x - _origin.x, (ps["origin"] as Vector3).y - _origin.y).length()
				_check("typing: forward held doesn't move you (%.1f units)" % moved, moved < 1.0)
				Input.action_release("q3_forward")
				chat._edit.text = "hi ^1red \"quoted\""
				Input.action_press("q3_use_item")   # the Enter that sends it
				chat._edit.text_submitted.emit(chat._edit.text)
				_step = 3
		3:
			if _t > 4.3:
				_check("Enter closes the chat", not chat.typing)
				_check("the held Enter isn't Use item", not _game._held(&"q3_use_item"))
				Input.action_release("q3_use_item")
				_check("let go, it counts again", not _game._chat_held.has(&"q3_use_item") or not _game._held(&"q3_use_item"))
				var line := _last_line()
				_check("G_Say's line comes back: '%s'" % line, line == NAME + "^7: ^2hi ^1red 'quoted'")
				var runs := Q3Colors.runs(line)
				_check("it draws blue, red, white, green, red", runs.map(func(r): return r[1]) == [Q3Colors.TABLE[4], Q3Colors.TABLE[1],
					Q3Colors.TABLE[7], Q3Colors.TABLE[2], Q3Colors.TABLE[1]])
				_check("not typing: EF_TALK clears", int(_ps()["eFlags"]) & E.EF_TALK == 0)
				_action("q3_chat")
				_step = 4
		4:
			if _t > 4.6:
				_check("open again", chat.typing)
				_key(KEY_ESCAPE)
				_step = 5
		5:
			if _t > 4.9:
				_check("Escape closes it and doesn't pause", not chat.typing and not _game._paused)
				_action("q3_teamchat")
				_step = 6
		6:
			if _t > 5.1:
				_check("Y opens team chat", chat.typing and chat.team)
				chat._edit.text = "team line"
				chat._edit.text_submitted.emit(chat._edit.text)
				_step = 7
		7:
			if _t > 5.6:
				_check("say_team in FFA comes back as a say: '%s'" % _last_line(), _last_line() == NAME + "^7: ^2team line")
				_check("the talk balloon texture loads", load(Q3.LIB + "fx/textures/talk_balloon.png") is Texture2D)
				print("CHAT %s" % ("ok" if _ok else "FAILED"))
				Q3Server.exec_now("killserver")
				get_tree().quit(0 if _ok else 1)
				_step = 8


func _check(label: String, ok: bool) -> void:
	print(("ok   " if ok else "FAIL ") + label)
	_ok = _ok and ok
