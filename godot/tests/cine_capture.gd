extends Node
## A windowed capture of the cinematic spectator camera (Q3CineCam): a bot free-for-all joined as a
## spectator with Spectator camera on Cinematic, so the auto-director picks who to watch and the
## camera picks its shots, filmed with tests/trailer_capture.gd (Ultra preset, this run only):
##   godot --path . --fixed-fps 60 res://tests/cine_capture.tscn -- map=q3dm6 bots=6 seconds=45 warmup=40
##     hud=1 window=1920x1080 out=C:/x/cine.mp4 ffmpeg=C:/x/ffmpeg.exe [stills=60,600 stills_dir=C:/x]
## It prints each shot as it starts ("CINE <seconds> <shot> <who>").

const BOTS := ["Sarge", "Xaero", "Major", "Visor", "Anarki", "Klesk", "Doom", "Hunter"]
const FPS := 60.0
const Capture := preload("res://tests/trailer_capture.gd")

var _game: Q3Gameplay
var _rec: Capture
var _hud := true
var _wait := 0
var _started := false
var _last := ""


func _ready() -> void:
	process_priority = 10   # after Gameplay
	var map := "q3dm6"
	var count := 6
	var seconds := 45.0
	var warmup := 40.0
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			match kv[0]:
				"map": map = kv[1]
				"bots": count = int(kv[1])
				"seconds": seconds = float(kv[1])
				"warmup": warmup = float(kv[1])
				"hud": _hud = kv[1] != "0"
	Capture.quality()
	Settings.set_value("gameplay/spec_view", "cinematic", false)
	Settings.set_value("gameplay/record_replays", false, false)   # (tests' matches record into user://test_replays)
	_rec = Capture.new()
	add_child(_rec)
	_rec.read_args()
	_rec.tag = "cine_" + map
	_rec.frames = roundi(seconds * FPS)
	var s := Session.new_setup("ffa", map)
	s.rules = {"warmup": 0, "fraglimit": 0, "timelimit": 0}
	s.spectate = true
	for i in count:
		s.bots.append({"name": BOTS[i % BOTS.size()], "skill": 4, "team": ""})
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	add_child(_game)
	# skip the start (bots armed, scores up); its server commands are read and dropped so the
	# reliable queue doesn't overflow
	for i in int(warmup * 1000.0 / Q3Host.FRAME_MSEC):
		Q3Server.frame(Q3Host.FRAME_MSEC)
		Q3Server.next_commands(_game.me)
	_game._client_time = Q3Server.time()
	_rec.use_window()


func _process(_delta: float) -> void:
	if _game == null or _game.me < 0 or _rec == null:
		return
	if not _started:
		_wait += 1
		if not _rec.size_ok() and _wait < 120:
			return
		if not _rec.begin():
			get_tree().quit(1)
			return
		_started = true
	var f := _rec.tick()
	_game.scoreboard.visible = false
	_game.hud.visible = _hud
	var c := _game._cine
	if c and f >= 0:
		var now := "%s %d" % [c.shot_name(), c._subject]
		if now != _last:
			_last = now
			print("CINE %.2f %s %s" % [f / FPS, c.shot_name(), Q3Colors.strip(_game._name(c._subject))])
