extends Node
## Trailer footage (TRAILER_PLAN.md): a real bot match, filmed until the choreographed fights are
## (F1-F7). Bots play a free-for-all; the recording follows one of them through id's follow
## spectator and cuts to the killer a beat after each frag, as tests/record_preview.tscn does.
## view=chase: the spectator chase camera behind the fighter, no HUD (cinematic). view=first:
## their eyes with the HUD and gun (the "feel" beats). Windowed (needs the GPU);
## tools/trailer/capture_remaster.py --matches runs it from tools/trailer/matches.json:
##   godot --path . --fixed-fps 60 res://tests/trailer_match.tscn -- map=q3dm6 bots=6 skill=5
##     seconds=50 warmup=60 view=chase follow=0 window=1920x1080 out=C:/x/match.mp4 ffmpeg=C:/x/ffmpeg.exe
## tests/trailer_capture.gd records the window (Ultra preset, this run only).

const BOTS := ["Sarge", "Xaero", "Major", "Visor", "Anarki", "Klesk", "Doom", "Hunter"]
const CUT_DELAY := 1.2   ## seconds after a frag before cutting to the killer, so the death shows
const FPS := 60.0
const Capture := preload("res://tests/trailer_capture.gd")

var _game: Q3Gameplay
var _rec: Capture
var _view := "chase"
var _first_follow := 0
var _t := 0.0
var _followed := -1
var _cut_at := -1.0
var _cut_to := -1
var _wait := 0         ## frames waited for the window to reach 1920x1080
var _started := false


func _ready() -> void:
	process_priority = 10   # after Gameplay, so the scoreboard it shows for the dead stays hidden
	var map := "q3dm6"
	var count := 6
	var skill := 5
	var seconds := 50.0
	var warmup := 60.0
	var tag := ""
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			match kv[0]:
				"map": map = kv[1]
				"bots": count = int(kv[1])
				"skill": skill = int(kv[1])
				"seconds": seconds = float(kv[1])
				"warmup": warmup = float(kv[1])
				"view": _view = kv[1]
				"follow": _first_follow = int(kv[1])
				"name": tag = kv[1]
	Capture.quality()
	_rec = Capture.new()
	add_child(_rec)
	_rec.read_args()
	_rec.tag = tag if tag != "" else map
	_rec.frames = roundi(seconds * FPS)
	var s := Session.new_setup("ffa", map)
	s.rules = {"warmup": 0, "fraglimit": 0, "timelimit": 0}
	for i in count:
		s.bots.append({"name": BOTS[i % BOTS.size()], "skill": skill, "team": ""})
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	add_child(_game)
	_game.client.obituary.connect(_on_obituary)
	Q3Server.client_command(_game.me, "team spectator")
	# skip the first minute (bots armed, scores up); its server commands are read and dropped so
	# the reliable queue doesn't overflow
	for i in int(warmup * 1000.0 / Q3Host.FRAME_MSEC):
		Q3Server.frame(Q3Host.FRAME_MSEC)
		Q3Server.next_commands(_game.me)
	_game._client_time = Q3Server.time()
	_game.spec_chase = _view == "chase"
	_rec.use_window()   # begun in _process, once the forced window size has taken


func _process(delta: float) -> void:
	_t += delta
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
	_rec.tick()
	if _followed < 0:
		_follow(_first_follow)
	if _cut_at > 0.0 and _t >= _cut_at:
		_cut_at = -1.0
		_follow(_cut_to)
	var ps := Q3Server.player_state(_game.me)
	if not ps.is_empty() and int(ps["clientNum"]) != _game.me:
		_game.client.local_client = int(ps["clientNum"])   # the followed player's events and gun are "ours"
	_game.scoreboard.visible = false
	if _game.hud:
		_game.hud.visible = _view == "first"


func _follow(cl: int) -> void:
	_followed = cl
	Q3Server.client_command(_game.me, "follow %d" % cl)


func _on_obituary(attacker: int, target: int, _means: int) -> void:
	if target != _followed or _cut_at > 0.0:
		return
	_cut_to = attacker if attacker >= 0 and attacker < 64 and attacker != target and attacker != _game.me \
		else (target + 1) % (_game.me)
	_cut_at = _t + CUT_DELAY
