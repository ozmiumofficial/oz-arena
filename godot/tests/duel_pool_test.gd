extends Node
## The duel pool (DUEL_MAPS_PLAN.md D1): 15 entries, built ones resolve, ranked setups accept only the pool.
##   godot --headless --path . res://tests/duel_pool_test.tscn --quit-after 600

func _ready() -> void:
	var fails: Array = []
	var entries: Array = Session.duel_pool_entries()
	if entries.size() != 15:
		fails.append("pool has %d entries, want 15" % entries.size())
	var built := Session.ranked_maps()
	var names := built.map(func(m): return String(m["map"]))
	for want in ["pro-q3dm6", "pro-q3dm13", "pro-q3tourney4"]:
		if not names.has(want):
			fails.append("%s missing from ranked maps" % want)
	if built.size() < 3:
		fails.append("%d built maps, want at least 3" % built.size())
	var duel := Session.maps_for("duel")
	for i in built.size():
		if not names.has(String(duel[i]["map"])):
			fails.append("duel list doesn't lead with the pool")
	var s := Session.new_setup("duel", "pro-q3dm6")
	s.online = {"ranked": true}
	if s.validate().size():
		fails.append("ranked pool map refused: %s" % ", ".join(s.validate()))
	s.map = "q3tourney1"
	if s.validate().size() != 1:
		fails.append("ranked q3tourney1 should be refused")
	s = Session.new_setup("ffa", "pro-q3dm6")
	s.online = {"ranked": true}
	if s.validate().size() != 1:
		fails.append("ranked ffa should be refused")
	print("duel_pool_test: ", "PASS" if fails.is_empty() else "FAIL ", fails)
	get_tree().quit(0 if fails.is_empty() else 1)
