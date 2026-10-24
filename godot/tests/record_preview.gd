extends Node
## Preview footage: a free-for-all of bots, watched in first person through id's follow spectator
## (team spectator, then follow), cutting to the killer whenever the followed player is fragged.
## Run it under Movie Maker for a fixed-rate video with sound:
##   godot --path . --write-movie C:/temp/preview.avi --fixed-fps 60 res://tests/record_preview.tscn
##     --resolution 1920x1080 -- window=1920x1080 map=q3dm6 bots=5 skill=4 seconds=50 warmup=60
## Prints "RECORD PREVIEW done" and quits.

const BOTS := ["Sarge", "Xaero", "Major", "Visor", "Anarki", "Klesk", "Doom", "Hunter"]

var _game: Q3Gameplay
var _t := 0.0
var _seconds := 50.0
var _warmup := 60.0      ## match seconds run before the first frame, unrecorded
var _followed := -1
var _cut_at := -1.0      ## when to follow _cut_to (a beat after the frag, so the death shows)
var _cut_to := -1


func _ready() -> void:
	process_priority = 10   # after Gameplay, so the scoreboard it shows for the dead stays hidden
	var map := "q3dm6"
	var count := 5
	var skill := 4
	for a in OS.get_cmdline_user_args():
		if a.begins_with("map="):
			map = a.substr(4)
		if a.begins_with("bots="):
			count = int(a.substr(5))
		if a.begins_with("skill="):
			skill = int(a.substr(6))
		if a.begins_with("seconds="):
			_seconds = float(a.substr(8))
		if a.begins_with("warmup="):
			_warmup = float(a.substr(7))
	var s := Session.new_setup("ffa", map)
	s.rules = {"warmup": 0, "fraglimit": 0, "timelimit": 0}
	for i in count:
		s.bots.append({"name": BOTS[i % BOTS.size()], "skill": skill, "team": ""})
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	add_child(_game)
	_game.client.obituary.connect(_on_obituary)
	Q3Server.client_command(_game.me, "team spectator")
	# skip the first minute (bots armed, scores up); its server commands, join messages included,
	# are read and dropped so the reliable queue doesn't overflow
	for i in int(_warmup * 1000.0 / Q3Host.FRAME_MSEC):
		Q3Server.frame(Q3Host.FRAME_MSEC)
		Q3Server.next_commands(_game.me)
	_game._client_time = Q3Server.time()


func _process(delta: float) -> void:
	_t += delta
	if _game.me < 0:
		return
	if _followed < 0 and _t > 1.0:
		_follow(0)
	if _cut_at > 0.0 and _t >= _cut_at:
		_cut_at = -1.0
		_follow(_cut_to)
	var ps := Q3Server.player_state(_game.me)
	if not ps.is_empty() and int(ps["clientNum"]) != _game.me:
		_game.client.local_client = int(ps["clientNum"])   # the followed player's events and gun are "ours"
	_game.scoreboard.visible = false
	if _t >= _seconds:
		print("RECORD PREVIEW done")
		get_tree().quit()


func _follow(cl: int) -> void:
	_followed = cl
	Q3Server.client_command(_game.me, "follow %d" % cl)


func _on_obituary(attacker: int, target: int, _means: int) -> void:
	if target != _followed or _cut_at > 0.0:
		return
	_cut_to = attacker if attacker >= 0 and attacker < 64 and attacker != target and attacker != _game.me \
		else (target + 1) % (_game.me)
	_cut_at = _t + 1.2
