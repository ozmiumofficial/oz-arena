extends Node3D
## A short FFA (fraglimit 1, three skill-5 bots) for capturing the end-of-match results screen:
##   godot --path . res://tests/capture.tscn -- scene=res://tests/match_end_preview.tscn frames=1800
func _ready() -> void:
	var s := Session.new_setup("ffa", "q3dm17")
	s.rules = {"fraglimit": 1, "timelimit": 0}
	s.bots = [{"name": "Sarge", "skill": 5, "team": ""}, {"name": "Xaero", "skill": 5, "team": ""}, {"name": "Crash", "skill": 5, "team": ""}]
	Session.setup = s
	add_child(load("res://scenes/gameplay/gameplay.tscn").instantiate())
