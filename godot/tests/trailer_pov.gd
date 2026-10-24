extends Node
## Trailer footage (TRAILER_PLAN.md): first-person gameplay from a bot-match replay
## (tests/trailer_replay_rec.tscn records one). Each clip watches a killer through their own eyes,
## HUD and gun, at full speed from PRE seconds before a frag to POST after it. Frags that come in
## a run (more by the same player close by) and rockets, rails and the gauntlet go first; clips
## never overlap, and each new clip watches someone else where it can. Windowed (needs the GPU):
##   godot --path . --fixed-fps 60 res://tests/trailer_pov.tscn -- replay=C:/x/match.q3replay
##     max=6 window=1920x1080 out=C:/x/pov.mp4 ffmpeg=C:/x/ffmpeg.exe
## Prints "TRAILER POV clip <i> at <frame> frames <n> <weapon> client <c>" per clip and
## tests/trailer_capture.gd records it all as one video.

const FPS := 60.0
const Capture := preload("res://tests/trailer_capture.gd")
const Action := preload("res://tests/trailer_action.gd")
const PRE := 3.5    ## seconds before the frag
const POST := 1.5   ## seconds after it
const RUN := 4.0    ## seconds either side in which the killer's other frags count toward a run

var _game: Q3Gameplay
var _rec: Capture
var _clips: Array = []   ## {t (ms), a, weapon, start (frame)}
var _clip := -1
var _started := false
var _wait := 0


func _ready() -> void:
	process_priority = 10   # after Gameplay: our playhead wins
	var path := ""
	var most := 6
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			match kv[0]:
				"replay": path = kv[1]
				"max": most = int(kv[1])
	Capture.quality()
	_rec = Capture.new()
	add_child(_rec)
	_rec.read_args()
	_rec.tag = path.get_file().get_basename()
	Session.dry_run = true
	Session.watch_replay(path, 0, "replays")
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	add_child(_game)
	if _game.replay_feed == null or _game.replay_feed.replay == null:
		push_error("trailer_pov: can't open %s" % path)
		get_tree().quit(1)
		return
	_pick_clips(most)
	if _clips.is_empty():
		push_error("trailer_pov: no frags by a real weapon in %s" % path)
		get_tree().quit(1)
		return
	for i in _clips.size():
		_clips[i]["start"] = i * _clip_frames()
	_rec.frames = _clips.size() * _clip_frames()
	_game.replay_playing = false
	_game.set_cam_mode("first")
	_rec.use_window()


func _clip_frames() -> int:
	return roundi((PRE + POST) * FPS)


## Frags by a real weapon, scored by the weapon and by the killer's other frags around it; the best
## `most` that don't overlap, preferring a killer not seen yet, in match order.
func _pick_clips(most: int) -> void:
	var feed := _game.replay_feed
	var frags: Array = []
	for e in feed.replay.events():
		var w := int(e["c"])
		var a := int(e["a"])
		var b := int(e["b"])
		if int(e["type"]) != 1 or not Action.WEAPONS.has(w) or a == b or a < 0 or a >= 64 or b < 0 or b >= 64:
			continue
		var t := float(e["time"])
		if t - PRE * 1000.0 < feed.start_time() or t + POST * 1000.0 > feed.end_time():
			continue
		frags.append({"t": t, "a": a, "weapon": Action.WEAPONS[w]})
	for f in frags:
		var run := 0
		for g in frags:
			if g != f and g["a"] == f["a"] and absf(float(g["t"]) - float(f["t"])) < RUN * 1000.0:
				run += 1
		f["score"] = Action.PRIORITY[f["weapon"]] + 2 * run
	frags.sort_custom(func(x: Dictionary, y: Dictionary) -> bool: return x["score"] > y["score"])
	var seen := {}
	for pass_ in 2:   # killers not seen yet first, then anyone
		for f in frags:
			if _clips.size() >= most or _clips.has(f) or (pass_ == 0 and seen.has(f["a"])):
				continue
			var clear := _clips.all(func(c: Dictionary) -> bool: return absf(float(c["t"]) - float(f["t"])) > (PRE + POST) * 1000.0)
			if clear:
				_clips.append(f)
				seen[f["a"]] = true
	_clips.sort_custom(func(x: Dictionary, y: Dictionary) -> bool: return float(x["t"]) < float(y["t"]))


## Game time (ms) of frame k of a clip.
func _clip_time(c: Dictionary, k: int) -> float:
	return float(c["t"]) - PRE * 1000.0 + k * 1000.0 / FPS


func _process(_delta: float) -> void:
	if _game == null or _rec == null or _clips.is_empty():
		return
	_game.scoreboard.visible = false
	if _game.viewer:
		_game.viewer.chrome = false
		_game.viewer.bar_shown = false
		_game.viewer.names = false
	if not _started:
		_wait += 1
		if not _rec.size_ok() and _wait < 120:
			return
		if not _rec.begin():
			get_tree().quit(1)
			return
		_started = true
	var f := maxi(_rec.tick(), 0)   # the preroll holds the first clip's first frame
	var i := mini(f / _clip_frames(), _clips.size() - 1)
	var c: Dictionary = _clips[i]
	var k := f - int(c["start"])
	if i != _clip:
		_clip = i
		_game.watch(int(c["a"]))
		print("TRAILER POV clip %d at %d frames %d %s client %d" % [i, int(c["start"]), _clip_frames(), c["weapon"], int(c["a"])])
	# the next frame's moment: Gameplay draws it before we run again
	var nxt: Dictionary = c if k + 1 < _clip_frames() or i + 1 >= _clips.size() else _clips[i + 1]
	_game.seek_to(_clip_time(nxt, k + 1 if nxt == c else 0))
