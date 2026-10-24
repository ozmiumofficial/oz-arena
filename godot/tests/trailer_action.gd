extends Node
## Trailer footage (TRAILER_PLAN.md): cinematic action filmed from a bot-match replay
## (tests/trailer_replay_rec.tscn records one). For every frag by a real weapon it films a shot:
## from PRE seconds before the kill at full speed, a ramp to SLOW speed around the hit, then full
## speed again, with a camera that is placed for drama rather than following anyone:
##   side        low and to the side of the exchange, looking across it
##   low_behind  over the killer's shoulder, low, looking at the victim
##   reverse     beyond the victim, looking back at the killer (the shot comes at the lens)
##   orbit       circling the victim through the slow motion
## The styles take turns. Cameras track smoothly, pull in from walls (the replay's own traces) and
## close their field of view a little into the slow motion. No HUD. Windowed (needs the GPU);
## tools/trailer/capture_remaster.py --action runs it:
##   godot --path . --fixed-fps 60 res://tests/trailer_action.tscn -- replay=C:/x/match.q3replay
##     max=14 window=1920x1080 out=C:/x/action.mp4 ffmpeg=C:/x/ffmpeg.exe
## Prints "TRAILER ACTION shot <i> at <frame> frames <n> <weapon> <style> kill <k>" per shot (k: the
## frame of the kill, counted from the shot's first) and
## tests/trailer_capture.gd records it all as one video.

const FPS := 60.0
const Capture := preload("res://tests/trailer_capture.gd")
const WEAPONS := {1: "shotgun", 2: "gauntlet", 4: "grenade", 5: "grenade", 6: "rocket", 7: "rocket",
	8: "plasma", 9: "plasma", 10: "railgun", 11: "lightning", 12: "bfg", 13: "bfg"}
## how much a kill is worth filming: rockets, rails and the gauntlet read best on screen
const PRIORITY := {"rocket": 3, "railgun": 3, "gauntlet": 3, "lightning": 2, "shotgun": 2, "bfg": 2, "grenade": 1, "plasma": 1}
const STYLES := ["side", "low_behind", "reverse", "orbit"]
const PRE := 1.8        ## game seconds shown before the kill
const SLOW_IN := 0.35   ## game seconds before the kill the slow motion starts
const SLOW_OUT := 0.45  ## game seconds after the kill it ends
const SLOW := 0.22      ## its speed
const POST := 0.8       ## game seconds after it, at full speed
const EYE := 22.0       ## where the camera aims on a body: about chest height above the origin
const MASK_SOLID := 1
const FOV := 52.0       ## vertical degrees; 46 at the heart of the slow motion

var _game: Q3Gameplay
var _rec: Capture
var _shots: Array = []   ## {t (ms), a, b, weapon, style, start (frame), frames}
var _shot := -1
var _cam := Vector3.ZERO     ## id units
var _look := Vector3.ZERO
var _started := false
var _wait := 0


func _ready() -> void:
	process_priority = 10   # after Gameplay: our camera and playhead win
	var path := ""
	var most := 14
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
		push_error("trailer_action: can't open %s" % path)
		get_tree().quit(1)
		return
	_pick_shots(most)
	if _shots.is_empty():
		push_error("trailer_action: no frags by a real weapon in %s" % path)
		get_tree().quit(1)
		return
	var total := 0
	for s in _shots:
		s["start"] = total
		total += _shot_frames()
	_rec.frames = total
	_game.replay_playing = false
	_game.set_cam_mode("free")
	_rec.use_window()   # begun in _process, once the forced window size has taken


## Frags by a real weapon (not the machinegun, falls or suicides), the best `most` by PRIORITY,
## in match order, each its own shot with the styles in turn.
func _pick_shots(most: int) -> void:
	var feed := _game.replay_feed
	var last := -1e9
	var found: Array = []
	for e in feed.replay.events():
		if int(e["type"]) != 1:
			continue
		var w := int(e["c"])
		var a := int(e["a"])
		var b := int(e["b"])
		var t := float(e["time"])
		if not WEAPONS.has(w) or a == b or a < 0 or a >= 64 or b < 0 or b >= 64:
			continue
		if t - PRE * 1000.0 < feed.start_time() or t + (SLOW_OUT + POST) * 1000.0 > feed.end_time() or t - last < 1500.0:
			continue
		last = t
		found.append({"t": t, "a": a, "b": b, "weapon": WEAPONS[w]})
	found.sort_custom(func(x: Dictionary, y: Dictionary) -> bool: return PRIORITY[x["weapon"]] > PRIORITY[y["weapon"]])
	found = found.slice(0, most)
	found.sort_custom(func(x: Dictionary, y: Dictionary) -> bool: return float(x["t"]) < float(y["t"]))
	for i in found.size():
		found[i]["style"] = STYLES[i % STYLES.size()]
	_shots = found


func _shot_frames() -> int:
	return roundi((PRE - SLOW_IN) * FPS) + roundi((SLOW_IN + SLOW_OUT) / SLOW * FPS) + roundi(POST * FPS)


## Game time (ms) of frame k of a shot, and how slow it is there (1 = full speed).
func _shot_time(s: Dictionary, k: int) -> Vector2:
	var n1 := roundi((PRE - SLOW_IN) * FPS)
	var n2 := roundi((SLOW_IN + SLOW_OUT) / SLOW * FPS)
	var t := float(s["t"]) / 1000.0
	if k < n1:
		return Vector2((t - PRE + k / FPS) * 1000.0, 1.0)
	if k < n1 + n2:
		return Vector2((t - SLOW_IN + (k - n1) / FPS * SLOW) * 1000.0, SLOW)
	return Vector2((t + SLOW_OUT + (k - n1 - n2) / FPS) * 1000.0, 1.0)


func _process(delta: float) -> void:
	if _game == null or _rec == null or _shots.is_empty():
		return
	_game.hud.visible = false
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
	var f := maxi(_rec.tick(), 0)   # the preroll holds the first shot's first frame
	var i := 0
	while i + 1 < _shots.size() and f >= int(_shots[i + 1]["start"]):
		i += 1
	var s: Dictionary = _shots[i]
	var k := f - int(s["start"])
	var cut := i != _shot
	if cut:
		_shot = i
		print("TRAILER ACTION shot %d at %d frames %d %s %s kill %d" % [i, int(s["start"]), _shot_frames(), s["weapon"], s["style"],
			roundi((PRE - SLOW_IN) * FPS) + roundi(SLOW_IN / SLOW * FPS)])
	_frame_camera(s, k, cut, delta)
	# the next frame's moment: Gameplay draws it before we run again
	var next := _shot_time(s, k + 1) if k + 1 < _shot_frames() or i + 1 >= _shots.size() else _shot_time(_shots[i + 1], 0)
	_game.seek_to(next.x)


func _frame_camera(s: Dictionary, k: int, cut: bool, delta: float) -> void:
	var feed := _game.replay_feed
	var pa: Dictionary = feed.player_state(int(s["a"]))
	var pb: Dictionary = feed.player_state(int(s["b"]))
	if pa.is_empty() or pb.is_empty():
		return
	var a: Vector3 = (pa["origin"] as Vector3) + Vector3(0, 0, EYE)
	var b: Vector3 = (pb["origin"] as Vector3) + Vector3(0, 0, EYE)
	var d := Vector3(b.x - a.x, b.y - a.y, 0.0)
	if d.length() < 1.0:
		d = Vector3(1, 0, 0)
	var dist := d.length()
	d = d.normalized()
	var side := Vector3(-d.y, d.x, 0.0)
	var mid := (a + b) * 0.5
	var tk := _shot_time(s, k)
	var slow := tk.y < 1.0
	var want := Vector3.ZERO
	var look := Vector3.ZERO
	match String(s["style"]):
		"side":
			want = b.lerp(a, 0.3) + side * clampf(dist * 0.4, 120.0, 260.0) + Vector3(0, 0, -12.0)
			look = mid.lerp(b, 0.7)
		"low_behind":
			want = a - d * 110.0 + side * 34.0 + Vector3(0, 0, -6.0)
			look = b
		"reverse":
			want = b + d * 170.0 - side * 50.0 + Vector3(0, 0, 8.0)
			look = b.lerp(a, 0.4)
		_:   # orbit: around the victim, faster through the slow motion
			var ang := atan2(-d.y, -d.x) + 0.7 + k * (0.004 if not slow else 0.011)
			want = b + Vector3(cos(ang), sin(ang), 0.0) * 190.0 + Vector3(0, 0, 18.0)
			look = b
	want = _clear(look, want)
	if cut:
		_cam = want
		_look = look
	else:
		_cam = _cam.lerp(want, 1.0 - exp(-delta * 5.0))
		_look = _look.lerp(look, 1.0 - exp(-delta * 9.0))
	var cam := _game.cam
	cam.global_position = Q3.q3_to_godot(_cam)
	var at := Q3.q3_to_godot(_look)
	if cam.global_position.distance_to(at) > 0.05:
		cam.look_at(at, Vector3.UP)
	var t := float(s["t"])
	var into := clampf(1.0 - absf(tk.x - t) / (SLOW_IN * 1000.0 + 1.0), 0.0, 1.0)
	cam.fov = lerpf(FOV, FOV - 6.0, smoothstep(0.0, 1.0, into))


## The camera pulled in from walls: a box trace from what it looks at out to where it wants to be.
func _clear(from: Vector3, to: Vector3) -> Vector3:
	var box := Vector3(8, 8, 8)
	var tr: Dictionary = _game.replay_feed.trace(from, to, -box, box, -1, MASK_SOLID)
	if tr.is_empty() or float(tr["fraction"]) >= 1.0:
		return to
	var hit: Vector3 = tr["endpos"]
	return hit - (to - from).normalized() * 6.0
