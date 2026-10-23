extends Node
## Windowed check that quitting from the main menu ends the process (a live web view used to hang
## or crash Godot's exit). Opens the main menu, quits after 8 s through Session.quit_game, or
## through a bare get_tree().quit() with plain=1. The process should exit within a few seconds.
##   godot --path . res://tests/quit_test.tscn [-- plain=1]

var _t := 0.0
var _plain := false


func _ready() -> void:
	_plain = "plain=1" in OS.get_cmdline_user_args()
	add_child(load(Session.MAIN_MENU).instantiate())


func _process(delta: float) -> void:
	_t += delta
	if _t > 8.0 and _t - delta <= 8.0:
		print("QUIT requested (%s)" % ("plain" if _plain else "Session.quit_game"))
		if _plain:
			get_tree().quit()
		else:
			Session.quit_game()
