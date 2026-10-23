extends Node
## Plays Session.cinematic_queue full screen, then loads Session.cinematic_then (the main menu
## after the launch intro; later, the menu or the next arena after a campaign tier movie).


func _ready() -> void:
	var p := Q3CinematicPlayer.play(self, Session.cinematic_queue)
	p.finished.connect(func(): Session._go(Session.cinematic_then))
