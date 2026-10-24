class_name Q3CineCam
extends RefCounted
## The cinematic camera (REPLAY_PLAN.md "Cinematic camera"): broadcast-style shots of one player
## that move on their own. It moves only the camera, never the player, like Q3ChaseCam.
##   orbit  circles the subject slowly (a quick, low one after they frag someone)
##   track  dollies alongside them as they run
##   lead   runs ahead, looking back at them
##   wide   a crane high in the room that drifts after them, framing the fight, slowly pushing in
##   low    a hero shot from near the floor, craning up (a powerup)
## A shot lasts 4-7.5 s, then the next one eases in from where the lens is (a critically damped
## spring, so position and speed stay smooth) or, when that path isn't clear, cuts. A new subject,
## a teleport, an event (frag, powerup, flag), a subject hidden behind a wall and a lens crammed
## against them cut, and so does a wall about to come between the wide shot and the subject;
## apart from a hidden subject, nothing but a new subject cuts a shot in its first MIN_SHOT.
## The moving shots ride on the subject: their offset springs toward the shot's as yaw, pitch and
## distance (so a change of shot swings around the subject, never through them), around a point
## that follows the subject closely sideways and loosely up and down (a jump doesn't bob the
## camera). Walls: every shot's lens is checked from the subject at its start (a box trace,
## MASK_SOLID, and no lava, slime or water there); a moving shot's lens is always the end of a box
## trace from the subject, pulled in at once and eased back out as Q3ChaseCam's is, so it always
## sees them, and a wall pulling it in hard swings the shot round the subject (_whisker); the wide
## shot's lens only moves by a box sweep from where it was, and a sweep that
## starts in solid (a mover) cuts. Timed by game time (Q3.game_ms) or a replay's playhead, never
## wall time: it holds still while a replay is paused and follows its speed.
## Points are id units in, a Godot transform out.

const MASK_SOLID := 1                  ## CONTENTS_SOLID
const MASK_LENS := 1 | 8 | 16 | 32     ## solid, lava, slime, water: never where the lens sits
const BOX := 8.0                       ## the lens's own size (Q3ChaseCam.BOX)
const BLOCK_CUT := 0.25                ## s the subject may be hidden before the camera cuts away
const AHEAD := 0.3                     ## s ahead the subject's path is checked: a wall coming between ...
const AHEAD_CUT := 0.12                ## ... for this long (s) changes the shot before they vanish
const CRAMPED := 56.0                  ## a moving shot's lens pulled in closer than this ...
const CRAMPED_CUT := 0.5               ## ... for this long (s) changes the shot
const SQUEEZED := 36.0                 ## a moving shot's lens this close changes the shot at once
const CROWD := 64.0                    ## a new shot's lens never starts this close to another player
const MIN_REACH := 90.0                ## a moving shot sized down to its room is at least this far out
const OUT_RATE := 3.0                  ## how fast a pulled-in lens eases back out (1/s)
const FALL_SPEED := 300.0              ## ups up or down past a jump's (JUMP_VELOCITY 270): followed closely
const TELEPORT := 300.0                ## the subject moved this far in one frame: a teleport
const WIDE_REACH := 560.0              ## the wide shot's lens is at most this far from the subject
const WIDE_FOLLOW := 0.35              ## how much of the subject's move the wide shot's lens follows
const MIN_SHOT := 0.8                  ## s a shot stays before an event or a cramped or wide view changes it
const EASE_TIME := 0.9                 ## the spring's smoothing (s) when a shot eases in
const ORBIT := 0
const TRACK := 1
const LEAD := 2
const WIDE := 3
const LOW := 4
const NAMES := ["orbit", "track", "lead", "wide", "low"]

var fov := 55.0                ## vertical degrees, for Camera3D.fov
var shot := -1                 ## ORBIT .. LOW
var cuts := 0                  ## hard cuts so far (tests)
var eases := 0                 ## eased changes of shot so far (tests)
var reasons := {}              ## why shots changed: fresh, timed, event, ahead, blocked, cramped, solid (tests)
var lens := Vector3.ZERO       ## where the lens is (id units)
var _dry := Vector3.ZERO       ## the last place the lens was clear of liquids

var _p := {}                   ## the shot's parameters
var _t := 0.0                  ## seconds into the shot
var _len := 6.0
var _cam := Vector3.ZERO       ## the wide shot's lens as its spring has it
var _cam_vel := Vector3.ZERO
var _pol := Vector3.ZERO       ## a moving shot's lens from the anchor as (yaw, pitch, distance), sprung
var _pol_vel := Vector3.ZERO
var _swing := Vector2.ZERO     ## yaw and pitch (degrees) added to the shot's to get its lens out from a wall
var _anchor := Vector3.ZERO    ## the point moving shots ride on: the subject, smoothed
var _anchor_vel := Vector3.ZERO
var _dist := -1.0              ## how far out a moving shot's lens is (pulled in by walls)
var _aim := Vector3.ZERO
var _aim_vel := Vector3.ZERO
var _smooth := 0.35            ## the lens spring's time now (eases back down after a shot eases in)
var _blocked := 0.0
var _ahead := 0.0
var _cramped := 0.0
var _last_ms := -1.0
var _subject := -1
var _last_focus := Vector3.INF
var _dir := Vector3(1, 0, 0)   ## the subject's heading, smoothed (horizontal)
var _pending := -1             ## a shot an event asked for, and when
var _pending_at := 0.0
var _hold_until := 0.0         ## no timed change of shot before this (ms): a death plays out
var _hero := false             ## the asked-for orbit is the quick one after a frag
var _fallback := false         ## the shot is the close orbit taken when nothing else fit
var _rng := RandomNumberGenerator.new()


func shot_name() -> String:
	return NAMES[shot] if shot >= 0 else ""


## Forgets everything: the next update starts a new shot with a cut.
func reset() -> void:
	_last_ms = -1.0
	_subject = -1
	_pending = -1
	_hold_until = 0.0


## Something happened to the subject: "frag" (they fragged someone: a quick hero orbit), "powerup"
## (a low shot), "flag" (they took a flag: a tracking shot) or "died" (the shot holds on the body).
func hint(what: String, now_ms: float) -> void:
	match what:
		"frag":
			_ask(ORBIT, now_ms + 600.0)
			_hero = true
		"powerup":
			_ask(LOW, now_ms + 250.0)
			_hero = false
		"flag":
			_ask(TRACK, now_ms + 250.0)
			_hero = false
		"died":
			_pending = -1
			_hold_until = now_ms + 2500.0


func _ask(kind: int, at: float) -> void:
	_pending = kind
	_pending_at = at


## The camera for this frame. `s`: {client, origin (id units), vel, yaw (degrees), dead,
## look (where to aim; a ragdoll's middle), others: [id points of the other players in view]}.
func update(feed: Q3Feed, s: Dictionary, now_ms: float) -> Transform3D:
	var focus := _focus(s)
	var dt := (now_ms - _last_ms) / 1000.0
	if _last_ms >= 0.0 and dt < 0.0 and dt > -0.1:
		# a clock stepping back a hair (server time plus a frame's leftover ms) isn't a jump
		dt = 0.0
		now_ms = _last_ms
	var fresh := _last_ms < 0.0 or dt < 0.0 or dt > 0.5 or int(s["client"]) != _subject \
		or (_last_focus != Vector3.INF and focus.distance_to(_last_focus) > TELEPORT)
	_last_ms = now_ms
	_last_focus = focus
	dt = clampf(dt, 0.0, 0.1)
	var vel: Vector3 = s.get("vel", Vector3.ZERO)
	var flat := Vector3(vel.x, vel.y, 0.0)
	if fresh:
		_subject = int(s["client"])
		var y := deg_to_rad(float(s.get("yaw", 0.0)))
		_dir = flat.normalized() if flat.length() > 80.0 else Vector3(cos(y), sin(y), 0.0)
		_rng.seed = hash([_subject, int(now_ms / 1000.0)])   # the same moment gets the same shots
		_anchor = focus
		_anchor_vel = Vector3.ZERO
		_pending = -1   # (an event of the last subject's)
		_hero = false
		# an establishing wide shot, unless they're running somewhere alone (it'd lose them)
		var fight := (s.get("others", []) as Array).any(func(o): return (o as Vector3).distance_to(focus) < 900.0)
		_new_shot(feed, s, WIDE if fight or flat.length() < 250.0 else -1, true, "fresh")
	else:
		if flat.length() > 80.0:
			_dir = _dir.slerp(flat.normalized(), 1.0 - exp(-1.5 * dt)).normalized()
		_t += dt
		if _pending >= 0 and now_ms >= _pending_at and _t >= MIN_SHOT:
			var k := _pending
			_pending = -1
			_new_shot(feed, s, k, true, "event")
		elif _t >= _len and now_ms >= _hold_until and not s.get("dead", false):
			_new_shot(feed, s, -1, false, "timed")
	# the anchor: close behind the subject sideways, loosely up and down through a jump (a fall or
	# a jump pad, faster than a jump's 270 ups, is followed closely)
	var falling := absf(vel.z) > FALL_SPEED
	var ax := _spring(_anchor, _anchor_vel, focus, 0.08, dt)
	var az := _spring(_anchor, _anchor_vel, focus, 0.12 if falling else 0.4, dt)
	_anchor = Vector3(ax[0].x, ax[0].y, az[0].z)
	_anchor_vel = Vector3(ax[1].x, ax[1].y, az[1].z)
	_smooth = lerpf(_smooth, 0.35, 1.0 - exp(-1.2 * dt))
	if shot == WIDE:
		# a crane: sprung toward its spot, moved only by a sweep from where it was
		var r := _spring(_cam, _cam_vel, _raw_lens(focus, _t), _smooth, dt)
		_cam = r[0]
		_cam_vel = r[1]
		var tr := feed.trace(lens, _cam, Vector3(-BOX, -BOX, -BOX), Vector3(BOX, BOX, BOX), _subject, MASK_SOLID)
		if not tr.is_empty() and (tr["startsolid"] or tr["allsolid"]):
			_new_shot(feed, s, -1, true, "solid")   # something moved into it (a door, a platform)
		elif not tr.is_empty():
			lens = tr["endpos"]
			if float(tr["fraction"]) < 1.0:
				_cam = lens
				_cam_vel = Vector3.ZERO
	else:
		_ride(feed, focus, dt)
	# never into lava, slime or water (the traces stop only at solid): the lens holds its last clear
	# spot (a hidden subject cuts from there)
	if feed.point_contents(lens, _subject) & MASK_LENS:
		lens = _dry
		_cam = lens
		_cam_vel = Vector3.ZERO
	else:
		_dry = lens
	# the subject hidden, or the lens crammed against them: cut away; a wall about to come between
	# them (where they'll be in AHEAD s): change the shot first
	_blocked = _blocked + dt if not _sees(feed, focus) else 0.0
	var ahead := focus
	if shot == WIDE and not s.get("dead", false) and flat.length() > 100.0:
		var tr2 := feed.trace(focus, focus + flat * AHEAD, Vector3(-BOX, -BOX, -BOX), Vector3(BOX, BOX, BOX), _subject, MASK_SOLID)
		if not tr2.is_empty():
			ahead = tr2["endpos"]
	_ahead = _ahead + dt if ahead != focus and not _sees(feed, ahead) else 0.0
	var near := lens.distance_to(focus)
	_cramped = _cramped + dt if shot != WIDE and near < CRAMPED else 0.0
	var squeezed := shot != WIDE and near < SQUEEZED and _t >= 0.25   # right against them: out at once
	if _blocked > BLOCK_CUT:
		_new_shot(feed, s, -1, true, "blocked")
	elif not _fallback and (squeezed or (_cramped > CRAMPED_CUT and _t >= MIN_SHOT)):
		_new_shot(feed, s, -1, true, "cramped")   # a cut: easing out from there stays inside them a while
	elif _ahead > AHEAD_CUT and _t >= MIN_SHOT:
		_new_shot(feed, s, -1, true, "ahead")
	# the aim, sprung too: the subject stays framed without the lens jerking
	var st := 0.22 if shot != WIDE else 0.4
	var r2 := _spring(_aim, _aim_vel, _shot_aim(s, focus), st, dt)
	var r3 := _spring(_aim, _aim_vel, _shot_aim(s, focus), 0.06 if falling else st * 0.5, dt)   # up and down: tighter
	_aim = Vector3(r2[0].x, r2[0].y, r3[0].z)
	_aim_vel = Vector3(r2[1].x, r2[1].y, r3[1].z)
	var f := clampf(_t / _len, 0.0, 1.0)
	fov = lerpf(float(_p.get("fov0", 55.0)), float(_p.get("fov1", _p.get("fov0", 55.0))), f * f * (3.0 - 2.0 * f))
	return Q3ChaseCam.look(lens, _aim)


## A moving shot's lens: the offset sprung toward the shot's, from the anchor, then as far out
## as a box trace from the subject gets.
func _ride(feed: Q3Feed, focus: Vector3, dt: float) -> void:
	var t := _polar(_raw_lens(focus, _t) - focus) + Vector3(_swing.x, _swing.y, 0.0)
	t.y = clampf(t.y, -30.0, 70.0)
	t.x = _pol.x + wrapf(t.x - _pol.x, -180.0, 180.0)   # the short way round
	_whisker(feed, focus, t)
	var r := _spring(_pol, _pol_vel, t, _smooth, dt)
	_pol = r[0]
	_pol_vel = r[1]
	var want := _anchor + _unpolar(_pol)
	var reach := want - focus
	var tr := feed.trace(focus, want, Vector3(-BOX, -BOX, -BOX), Vector3(BOX, BOX, BOX), _subject, MASK_SOLID)
	if tr.is_empty() or tr["startsolid"] or reach.length() < 0.01:
		lens = focus   # (no map, or squeezed into a vent): inside their own box, until the next shot
		_dist = 0.0
	else:
		var hit := reach.length() * float(tr["fraction"])
		_dist = hit if _dist < 0.0 or hit < _dist else lerpf(_dist, hit, 1.0 - exp(-OUT_RATE * dt))
		lens = focus + reach.normalized() * _dist
	_cam = lens
	_cam_vel = Vector3.ZERO


## A wall pulling the shot's lens in hard (under 60% of the way, and close): try swinging it round
## the subject and up, and keep the nearest way that's clear (else the roomiest, if it doubles the
## room) for the rest of the shot; the spring carries the lens there.
func _whisker(feed: Q3Feed, focus: Vector3, t: Vector3) -> void:
	var want := _anchor + _unpolar(t)
	var tr := feed.trace(focus, want, Vector3(-BOX, -BOX, -BOX), Vector3(BOX, BOX, BOX), _subject, MASK_SOLID)
	if tr.is_empty() or tr["startsolid"] or float(tr["fraction"]) >= 0.6 or t.z * float(tr["fraction"]) > 120.0:
		return
	var now := t.z * float(tr["fraction"])
	var best := Vector2.ZERO
	var best_reach := maxf(90.0, now * 2.0)   # worth swinging for: twice the room, and room enough
	for d in [Vector2(35, 0), Vector2(-35, 0), Vector2(0, 30), Vector2(70, 0), Vector2(-70, 0), Vector2(35, 30),
			Vector2(-35, 30), Vector2(0, 55), Vector2(110, 10), Vector2(-110, 10), Vector2(180, 15), Vector2(180, 45)]:
		var p := t + Vector3(d.x, d.y, 0.0)
		p.y = clampf(p.y, -30.0, 70.0)
		var tr2 := feed.trace(focus, _anchor + _unpolar(p), Vector3(-BOX, -BOX, -BOX), Vector3(BOX, BOX, BOX), _subject, MASK_SOLID)
		if tr2.is_empty() or tr2["startsolid"]:
			continue
		var reach := t.z * float(tr2["fraction"])
		if reach > best_reach:
			best_reach = reach
			best = d
			if float(tr2["fraction"]) >= 0.85:
				break   # clear enough: the nearest such way round
	if best != Vector2.ZERO:
		_swing += best
		_swing.x = wrapf(_swing.x, -180.0, 180.0)


## An offset as (yaw, pitch, distance), degrees and id units; pitch positive is up.
static func _polar(v: Vector3) -> Vector3:
	var l := v.length()
	if l < 0.001:
		return Vector3(0, 0, 0)
	return Vector3(rad_to_deg(atan2(v.y, v.x)), rad_to_deg(asin(clampf(v.z / l, -1.0, 1.0))), l)


static func _unpolar(p: Vector3) -> Vector3:
	var y := deg_to_rad(p.x)
	var pi := deg_to_rad(clampf(p.y, -85.0, 85.0))
	return Vector3(cos(pi) * cos(y), cos(pi) * sin(y), sin(pi)) * maxf(p.z, 0.0)


func _sees(feed: Q3Feed, p: Vector3) -> bool:
	var tr := feed.trace(lens, p, Vector3.ZERO, Vector3.ZERO, _subject, MASK_SOLID)
	return tr.is_empty() or float(tr["fraction"]) >= 1.0


## The point traces start from: inside the subject's box, alive (crouched too) or dead.
static func _focus(s: Dictionary) -> Vector3:
	var o: Vector3 = s["origin"]
	return o + Vector3(0, 0, -14.0 if s.get("dead", false) else 12.0)


func _side() -> Vector3:
	return Vector3(-_dir.y, _dir.x, 0.0) * float(_p.get("side", 1.0))


## The shot's lens at `t` seconds before any wall pulls it in.
func _raw_lens(focus: Vector3, t: float) -> Vector3:
	var p := _p
	match shot:
		ORBIT:
			var a := deg_to_rad(float(p["a0"]) + float(p["rate"]) * t)
			return focus + Vector3(cos(a), sin(a), 0.0) * float(p["r"]) + Vector3(0, 0, float(p["h"]))
		TRACK:
			return focus + _side() * float(p["d"]) - _dir * float(p["back"]) + Vector3(0, 0, float(p["h"]))
		LEAD:
			return focus + _dir * float(p["d"]) + _side() * float(p["off"]) + Vector3(0, 0, float(p["h"]))
		LOW:
			var a := deg_to_rad(float(p["a"]))
			var f := clampf(t / _len, 0.0, 1.0)
			var h := lerpf(float(p["h0"]), float(p["h1"]), f * f * (3.0 - 2.0 * f))
			return focus + Vector3(cos(a), sin(a), 0.0) * float(p["r"]) + Vector3(0, 0, h)
		WIDE:
			# a crane that drifts a third of the way after the subject (the sweep keeps it off walls)
			return p["pos"] + (focus - p["from"]) * WIDE_FOLLOW
	return focus


func _shot_aim(s: Dictionary, focus: Vector3) -> Vector3:
	var look: Vector3 = s.get("look", focus)
	if s.get("dead", false):
		return look
	match shot:
		TRACK:
			var vel: Vector3 = s.get("vel", Vector3.ZERO)
			return look + _dir * clampf(Vector2(vel.x, vel.y).length() * 0.12, 0.0, 50.0)
		WIDE:
			# the fight: the subject, pulled a little toward whoever is close to them
			var near := Vector3.ZERO
			var n := 0
			for o in s.get("others", []):
				if (o as Vector3).distance_to(focus) < 700.0:
					near += o
					n += 1
			return look if n == 0 else look.lerp(near / n, 0.3)
	return look


## Starts a shot: `kind` first (-1: a weighted pick), re-rolling its parameters and then the other
## kinds until one has a clear lens. `cut` places the lens at once; otherwise it eases over when the
## way there is clear.
func _new_shot(feed: Q3Feed, s: Dictionary, kind: int, cut: bool, why: String) -> void:
	reasons[why] = int(reasons.get(why, 0)) + 1
	var focus := _focus(s)
	var old := shot
	var hero := _hero and kind == ORBIT and why == "event"
	if why == "event":
		_hero = false
	var order: Array = []
	if kind >= 0:
		order.append(kind)
	for k in _weighted(s, focus, old):
		if not k in order:
			order.append(k)
	var chosen := false
	for k in order:
		for attempt in (1 if k == WIDE else 4):
			if _try(feed, s, focus, k, hero and k == ORBIT):
				chosen = true
				break
		if chosen:
			break
	_fallback = not chosen
	if not chosen:
		# nowhere clear (a tight corridor): a close orbit, pulled in by its trace like the chase cam
		shot = ORBIT
		_p = {"a0": float(s.get("yaw", 0.0)) + 180.0, "rate": 6.0, "r": 90.0, "h": 24.0, "fov0": 62.0}
		_len = 3.0
	_t = 0.0
	_swing = Vector2.ZERO
	_blocked = 0.0
	_ahead = 0.0
	_cramped = 0.0
	var start := _raw_lens(focus, 0.0)
	var ease := false
	if not cut and old >= 0:
		# a moving shot swings over around the subject (its traces keep it clear); a wide one only
		# when the straight way to its spot is clear
		ease = shot != WIDE
		if shot == WIDE and lens.distance_to(start) < 700.0:
			var tr := feed.trace(lens, start, Vector3(-BOX, -BOX, -BOX), Vector3(BOX, BOX, BOX), _subject, MASK_SOLID)
			ease = not tr.is_empty() and float(tr["fraction"]) >= 1.0 and not tr["startsolid"]
		if not ease:
			reasons["no_ease"] = int(reasons.get("no_ease", 0)) + 1
	if ease:
		_smooth = EASE_TIME   # the spring carries the lens over, keeping its speed
		if old == WIDE:
			_pol = _polar(lens - _anchor)
			_pol_vel = Vector3.ZERO
			_dist = -1.0
		eases += 1
	else:
		var tr := feed.trace(focus, start, Vector3(-BOX, -BOX, -BOX), Vector3(BOX, BOX, BOX), _subject, MASK_SOLID)
		lens = start if tr.is_empty() else tr["endpos"]
		if feed.point_contents(lens, _subject) & MASK_LENS == 0:
			_dry = lens
		elif feed.point_contents(_dry, _subject) & MASK_LENS or _dry.distance_to(focus) > 1200.0:
			_dry = focus   # (the subject's own space: dry unless they swim)
		_cam = lens
		_cam_vel = Vector3.ZERO
		_anchor = focus
		_anchor_vel = Vector3.ZERO
		_pol = _polar(start - focus)
		_pol_vel = Vector3.ZERO
		_dist = -1.0
		_aim = _shot_aim(s, focus)
		_aim_vel = Vector3.ZERO
		_smooth = 0.35
		cuts += 1


## The kinds to try, best first: varied (never the one just shown), running shots for a runner,
## the wide shot when there's a fight, a slow orbit for someone standing.
func _weighted(s: Dictionary, focus: Vector3, old: int) -> Array:
	var vel: Vector3 = s.get("vel", Vector3.ZERO)
	var speed := Vector2(vel.x, vel.y).length()
	var fight := 0
	for o in s.get("others", []):
		if (o as Vector3).distance_to(focus) < 900.0:
			fight += 1
	var w := {ORBIT: 2.0 + (1.5 if speed < 200.0 else 0.0), TRACK: 1.0 + (2.5 if speed > 250.0 else 0.0),
		LEAD: 0.8 + (1.5 if speed > 250.0 else 0.0), WIDE: (0.6 if speed > 300.0 else 1.0) + (1.5 if fight > 0 else 0.0), LOW: 0.8}
	w.erase(old)
	var out: Array = []
	while not w.is_empty():
		var total := 0.0
		for k in w:
			total += w[k]
		var x := _rng.randf() * total
		var pick: int = w.keys()[0]
		for k in w:
			x -= w[k]
			if x <= 0.0:
				pick = k
				break
		out.append(pick)
		w.erase(pick)
	if old >= 0:
		out.append(old)
	return out


## Rolls a shot of `kind` and keeps it when its lens is clear at the start and halfway through.
func _try(feed: Q3Feed, s: Dictionary, focus: Vector3, kind: int, hero: bool) -> bool:
	var r := _rng
	var yaw := rad_to_deg(atan2(_dir.y, _dir.x))
	var side := 1.0 if r.randf() < 0.5 else -1.0
	var p := {}
	var length := r.randf_range(5.0, 7.5)
	match kind:
		ORBIT:
			if hero:
				p = {"a0": yaw + side * r.randf_range(40.0, 100.0), "rate": side * 16.0, "r": r.randf_range(115.0, 140.0),
					"h": r.randf_range(-4.0, 12.0), "fov0": 58.0, "fov1": 52.0}
				length = 4.0
			else:
				p = {"a0": yaw + 180.0 + side * r.randf_range(40.0, 140.0), "rate": side * r.randf_range(7.0, 12.0),
					"r": r.randf_range(140.0, 200.0), "h": r.randf_range(24.0, 70.0), "fov0": 52.0}
		TRACK:
			p = {"side": side, "d": r.randf_range(170.0, 240.0), "back": r.randf_range(-40.0, 60.0), "h": r.randf_range(16.0, 48.0),
				"fov0": 56.0}
		LEAD:
			p = {"side": side, "d": r.randf_range(190.0, 260.0), "off": r.randf_range(0.0, 60.0), "h": r.randf_range(0.0, 30.0),
				"fov0": 52.0}
			length = r.randf_range(4.0, 6.0)
		LOW:
			p = {"a": yaw + side * r.randf_range(20.0, 70.0), "r": r.randf_range(110.0, 150.0), "h0": -30.0, "h1": 22.0,
				"fov0": 62.0, "fov1": 56.0}
			length = r.randf_range(4.5, 6.0)
		WIDE:
			var pos := _wide_spot(feed, s, focus)
			if pos == Vector3.INF:
				return false
			p = {"pos": pos, "from": focus, "fov0": 64.0, "fov1": 54.0}
	var keep := {"shot": shot, "p": _p, "len": _len}
	shot = kind
	_p = p
	_len = length
	if kind != WIDE:
		# sized to the room: a wall short of the lens brings the whole shot in, down to MIN_REACH
		var f := _reach(feed, s, focus, 0.0)
		if f >= 0.0 and f < 0.98:
			var k := f * 0.92
			var main := float(p.get("r", p.get("d", 0.0)))
			if main * k >= MIN_REACH:
				for key in ["r", "d", "back", "off"]:
					if p.has(key):
						p[key] = float(p[key]) * k
				f = _reach(feed, s, focus, 0.0)
		var fits := f >= 0.98 and not _crowded(s, _raw_lens(focus, 0.0))
		for q in [0.25, 0.5, 0.75, 1.0]:   # (where the subject is now: they move, but rooms are rooms)
			fits = fits and _reach(feed, s, focus, length * q) >= 0.7
		if not fits:
			shot = keep["shot"]
			_p = keep["p"]
			_len = keep["len"]
			return false
	return true


## Whether another player stands where a lens at `p` would be (a body filling the frame).
static func _crowded(s: Dictionary, p: Vector3) -> bool:
	for o in s.get("others", []):
		if (o as Vector3).distance_to(p) < CROWD:
			return true
	return false


## How far out the shot's lens at `t` gets: the fraction of the box trace from the subject, or
## -1 when it starts stuck or ends in lava, slime or water.
func _reach(feed: Q3Feed, s: Dictionary, focus: Vector3, t: float) -> float:
	var tr := feed.trace(focus, _raw_lens(focus, t), Vector3(-BOX, -BOX, -BOX), Vector3(BOX, BOX, BOX), int(s["client"]), MASK_SOLID)
	if tr.is_empty() or tr["startsolid"]:
		return -1.0
	return float(tr["fraction"]) if feed.point_contents(tr["endpos"], int(s["client"])) & MASK_LENS == 0 else -1.0


## The wide shot's lens: rays up and out from the subject; a spot must see where the subject is
## heading (where a runner will be in 0.8 and 1.6 s), and the best is far (but not too far),
## high, sees the others close by, and sits across the line to the nearest one, so both are in
## frame. Vector3.INF when none is 240 units out.
func _wide_spot(feed: Q3Feed, s: Dictionary, focus: Vector3) -> Vector3:
	var cl := int(s["client"])
	var nearest := Vector3.INF
	var others: Array = []
	for o in s.get("others", []):
		if (o as Vector3).distance_to(focus) < 1200.0:
			others.append(o)
			if nearest == Vector3.INF or (o as Vector3).distance_to(focus) < nearest.distance_to(focus):
				nearest = o
	others.sort_custom(func(a, b): return (a as Vector3).distance_to(focus) < (b as Vector3).distance_to(focus))
	others = others.slice(0, 4)
	var best := Vector3.INF
	var best_score := -1e9
	var base := _rng.randf_range(0.0, 30.0)
	var box := Vector3(12, 12, 12)
	# where they'll be in a second or two, so the shot keeps them
	var vel: Vector3 = s.get("vel", Vector3.ZERO)
	var heading: Array = []
	for t in [0.8, 1.6]:
		var tr := feed.trace(focus, focus + Vector3(vel.x, vel.y, 0.0) * t, Vector3(-BOX, -BOX, -BOX), Vector3(BOX, BOX, BOX), cl, MASK_SOLID)
		if not tr.is_empty() and (tr["endpos"] as Vector3).distance_to(focus) > 64.0:
			heading.append(tr["endpos"])
	for i in 12:
		var y := deg_to_rad(base + i * 30.0 + _rng.randf_range(-8.0, 8.0))
		var up := deg_to_rad(_rng.randf_range(12.0, 38.0))
		var dir := Vector3(cos(up) * cos(y), cos(up) * sin(y), sin(up))
		var tr := feed.trace(focus, focus + dir * WIDE_REACH, -box, box, cl, MASK_SOLID)
		if tr.is_empty() or tr["startsolid"]:
			continue
		var d := WIDE_REACH * float(tr["fraction"]) - 24.0
		if d < 240.0:
			continue
		var pos := focus + dir * d
		if feed.point_contents(pos, cl) & MASK_LENS or _crowded(s, pos):
			continue
		var keeps := true
		for h in heading:
			var see := feed.trace(pos, h, Vector3.ZERO, Vector3.ZERO, cl, MASK_SOLID)
			keeps = keeps and not see.is_empty() and float(see["fraction"]) >= 1.0
		if not keeps:
			continue   # they'd be gone behind a wall in a second
		var score := d + (pos.z - focus.z) * 0.3
		for o in others:
			var see := feed.trace(pos, o, Vector3.ZERO, Vector3.ZERO, cl, MASK_SOLID)
			if not see.is_empty() and float(see["fraction"]) >= 1.0:
				score += 120.0
		if nearest != Vector3.INF:
			var to := Vector2(nearest.x - focus.x, nearest.y - focus.y).normalized()
			score += 80.0 * (1.0 - absf(to.dot(Vector2(dir.x, dir.y).normalized())))
		if score > best_score:
			best_score = score
			best = pos
	return best


## A critically damped spring (Game Programming Gems 4, 1.10): `cur` and its `vel` toward
## `target` in about `smooth_time` seconds. Returns [position, velocity].
static func _spring(cur: Vector3, vel: Vector3, target: Vector3, smooth_time: float, dt: float) -> Array:
	var omega := 2.0 / maxf(smooth_time, 0.01)
	var x := omega * dt
	var e := 1.0 / (1.0 + x + 0.48 * x * x + 0.235 * x * x * x)
	var change := cur - target
	var temp := (vel + omega * change) * dt
	return [target + (change + temp) * e, (vel - omega * temp) * e]
