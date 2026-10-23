class_name Q3Hud
extends CanvasLayer
## The in-game HUD: a modern, native layer over the match, laid out by Settings > HUD (preset,
## scale, opacity, elements) and fed by the playerState every frame. The widgets draw themselves
## (hud_*.gd); this layer places them and does cgame's local feedback:
##   cg_playerstate.c CG_CheckLocalSounds: hit sounds, awards (medal + announcer), lead changes,
##     frag-limit and time-limit warnings; CG_DamageFeedback: the damage arc and the view kick;
##   cg_draw.c: center prints (3 s), rewards (3 s each, queued), the item pickup (3 s), powerups
##     with their time left, the low-ammo warning (CG_CheckAmmo), warmup and the countdown with
##     its "three, two, one" and "FIGHT!".
## Gameplay calls update() each frame and the event methods as things happen.

const S := preload("res://scenes/gameplay/hud/hud_style.gd")
const E := preload("res://scripts/game/q3_enums.gd")
const CENTER_TIME := 3.0          ## cg_centertime
const REWARD_TIME := 3.0
const PICKUP_TIME := 3.0
const DAMAGE_DEFLECT_TIME := 100.0
const DAMAGE_RETURN_TIME := 400.0
const RANK_TIED := 0x4000
const WEAPON_FIRING := 3          ## weaponstate_t
## bg_public.h PERS_PLAYEREVENTS bits
const PLAYEREVENT_DENIEDREWARD := 1
const PLAYEREVENT_GAUNTLETREWARD := 2
const PLAYEREVENT_HOLYSHIT := 4
## powerups with their HUD icons (PW_QUAD .. PW_FLIGHT, then the flags)
const POWERUP_ICONS := {1: "quad", 2: "envirosuit", 3: "haste", 4: "invis", 5: "regen", 6: "flight", 7: "iconf_red", 8: "iconf_blu"}

var u := 1.0                      ## pixels per HUD unit: 1080p = 1, times Settings' HUD scale
var preset := "modern"
var root: Control
var vitals: Q3HudVitals
var ammo_panel: Q3HudAmmo
var weapon_bar: Q3HudWeaponBar
var speedometer: Q3HudSpeedometer
var feed: Q3HudFeed
var crosshair: Q3HudCrosshair
var damage_fx: Q3HudDamage
var status: Q3HudStatus
var _msg: Control                 ## center prints, rewards, pickups, powerups, warmup, notices
var _notify: Array[Dictionary] = []   ## console prints and chat, top left
var _fatal := ""
var _center := ""
var _center_t := 99.0
var _center_y := 0.3
var _center_big := false
var _rewards: Array[Dictionary] = []   ## {medal, count, sound}
var _reward_t := 99.0
var _reward: Dictionary = {}
var _pickup: Dictionary = {}
var _pickup_t := 99.0
var _ops: Dictionary = {}         ## the previous frame's playerState
var _sound_queue: Array[String] = []
var _announcer: AudioStreamPlayer
var _low_ammo := 0                ## cg.lowAmmoWarning: 0, 1 low, 2 empty
var _warmup_count := -1
var _warmup_state := 0
var _time_warnings := 0
var _frag_warnings := 0
var _dmg_time := -100000.0
var _dmg_pitch := 0.0
var _dmg_roll := 0.0
var _dead := false
var killer := ""
var _powerups: Array = []         ## [icon, seconds] for the HUD's list
var _warmup_text := ""
var _fps_t := 0.0
var _fps := 0


func _init() -> void:
	layer = 10


func _ready() -> void:
	root = Control.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(root)
	damage_fx = _add(Q3HudDamage.new())
	crosshair = _add(Q3HudCrosshair.new())
	status = _add(Q3HudStatus.new())
	feed = _add(Q3HudFeed.new())
	speedometer = _add(Q3HudSpeedometer.new())
	weapon_bar = _add(Q3HudWeaponBar.new())
	vitals = _add(Q3HudVitals.new())
	ammo_panel = _add(Q3HudAmmo.new())
	_msg = _add(Control.new())
	_msg.draw.connect(_draw_messages)
	_announcer = AudioStreamPlayer.new()
	_announcer.bus = "SFX" if AudioServer.get_bus_index("SFX") >= 0 else "Master"
	add_child(_announcer)
	_announcer.finished.connect(_next_sound)
	get_viewport().size_changed.connect(layout)
	Settings.changed.connect(func(_k, _v): layout())
	layout()


func _add(c: Control) -> Control:
	c.mouse_filter = Control.MOUSE_FILTER_IGNORE
	if "hud" in c:
		c.set("hud", self)
	root.add_child(c)
	return c


## Places every widget for the preset and the current window size.
func layout() -> void:
	var vs := root.get_viewport_rect().size
	preset = String(Settings.get_value("hud/preset"))
	var scale := float(Settings.get_value("hud/scale")) * (0.88 if preset == "competitive" else 1.0)
	u = vs.y / 1080.0 * scale
	root.modulate.a = float(Settings.get_value("hud/opacity"))
	var m := 34.0 * u
	var compact := preset != "modern"
	vitals.compact = compact
	ammo_panel.compact = compact
	var speed_style := String(Settings.get_value("hud/speedometer"))
	speedometer.style = "compact" if preset in ["competitive", "classic"] and speed_style == "arc" else speed_style
	speedometer.visible = speed_style != "off"
	var bar_on: bool = Settings.get_value("hud/ammo_bar") and preset != "minimal"
	# classic: Quake 3's weapon select, shown for a moment after a switch
	weapon_bar.visible = bar_on or preset == "classic"
	weapon_bar.fade_out = preset == "classic"
	feed.visible = Settings.get_value("hud/kill_feed")
	for c in [damage_fx, crosshair, _msg]:
		c.position = Vector2.ZERO
		c.size = vs
	status.position = Vector2(0, 18 * u)
	status.size = Vector2(vs.x, 90 * u)
	feed.size = Vector2(560 * u, 260 * u)
	feed.position = Vector2(vs.x - feed.size.x - m, m)
	var vit := Vector2(530 * u, 92 * u) if not compact else Vector2(400 * u, 66 * u)
	var amm := Vector2(330 * u, 92 * u) if not compact else Vector2(260 * u, 66 * u)
	weapon_bar.size = Vector2(vs.x * 0.5, 84 * u)
	weapon_bar.position = Vector2(vs.x * 0.25, vs.y - weapon_bar.size.y - 14 * u)
	var bar_top := weapon_bar.position.y if bar_on else vs.y - m * 0.5
	speedometer.size = Vector2(300 * u, 170 * u) if speedometer.style == "arc" else Vector2(200 * u, 56 * u)
	if preset == "classic":
		# Quake 3's status bar: ammo, health and armor in one row along the bottom middle
		var row := Vector2(vit.x + amm.x + 20 * u, vit.y)
		var x0 := (vs.x - row.x) * 0.5
		ammo_panel.size = amm
		ammo_panel.position = Vector2(x0, vs.y - row.y - m * 0.6)
		vitals.size = vit
		vitals.position = Vector2(x0 + amm.x + 20 * u, vs.y - row.y - m * 0.6)
		weapon_bar.position.y = ammo_panel.position.y - weapon_bar.size.y - 10 * u
		speedometer.position = Vector2((vs.x - speedometer.size.x) * 0.5, weapon_bar.position.y - speedometer.size.y)
	else:
		vitals.size = vit
		vitals.position = Vector2(m, vs.y - vit.y - m)
		ammo_panel.size = amm
		ammo_panel.position = Vector2(vs.x - amm.x - m, vs.y - amm.y - m)
		speedometer.position = Vector2((vs.x - speedometer.size.x) * 0.5, bar_top - speedometer.size.y - 4 * u)


# ---------------------------------------------------------------- per frame

## ps: the live local playerState; info: {now (ms), level_time (ms), gametype, standings: Callable}
func update(ps: Dictionary, info: Dictionary) -> void:
	if ps.is_empty():
		return
	var stats: PackedInt32Array = ps["stats"]
	var pers: PackedInt32Array = ps["persistant"]
	var ammo: PackedInt32Array = ps["ammo"]
	var w := int(ps["weapon"])
	var level_time := int(info.get("level_time", 0))
	var spectator := int(pers[E.PERS_TEAM]) == E.TEAM_SPECTATOR
	_dead = int(ps["pm_type"]) == E.PM_DEAD
	var playing := not spectator and int(ps["pm_type"]) != E.PM_INTERMISSION
	for c in [vitals, ammo_panel, weapon_bar]:
		c.modulate.a = 0.0 if not playing else (0.45 if _dead else 1.0)
	crosshair.visible = playing and not _dead
	vitals.set_values(int(stats[E.STAT_HEALTH]), int(stats[E.STAT_ARMOR]))
	var hold := int(stats[E.STAT_HOLDABLE_ITEM])
	ammo_panel.set_values(w, int(ammo[w]) if w < ammo.size() else 0, _item_icon(hold) if hold > 0 else "",
		int(ps["weaponstate"]) == WEAPON_FIRING and int(ps["weaponTime"]) > 100)
	weapon_bar.set_values(int(stats[E.STAT_WEAPONS]), ammo, w)
	var vel: Vector3 = ps["velocity"]
	speedometer.set_speed(Vector2(vel.x, vel.y).length())
	crosshair.weapon = w
	# CG_ColorForHealth counts the armor that would still protect
	var h := int(stats[E.STAT_HEALTH])
	var arm := mini(int(stats[E.STAT_ARMOR]), int(h * 0.66 / 0.34))
	crosshair.health = h + arm if h > 0 else 0
	damage_fx.low_health = playing and h > 0 and h <= 25
	_update_powerups(ps, level_time)
	_update_status(ps, info, level_time)
	if not _ops.is_empty():
		_check_local_sounds(ps, _ops, info)
		if int(ps["damageEvent"]) != int(_ops["damageEvent"]) and int(ps["damageCount"]) > 0:
			_damage_feedback(ps)
	_check_ammo(ps)
	_ops = ps.duplicate(true)
	_msg.queue_redraw()


func _process(delta: float) -> void:
	_center_t += delta
	_pickup_t += delta
	_reward_t += delta
	if _reward_t >= REWARD_TIME and not _rewards.is_empty():
		_reward = _rewards.pop_front()
		_reward_t = 0.0
		_queue_sound(String(_reward.get("sound", "")), true)
	elif _reward_t >= REWARD_TIME:
		_reward = {}
	for n in _notify:
		n["t"] = float(n["t"]) + delta
	_notify = _notify.filter(func(n): return float(n["t"]) < 6.0)
	_fps_t += delta
	if _fps_t >= 0.5:
		_fps = Engine.get_frames_per_second()
		_fps_t = 0.0
	_msg.queue_redraw()


func _update_powerups(ps: Dictionary, level_time: int) -> void:
	_powerups.clear()
	var pw: PackedInt32Array = ps["powerups"]
	for i in POWERUP_ICONS:
		if i < pw.size() and int(pw[i]) > 0:
			var left := (int(pw[i]) - level_time) / 1000.0
			if i >= 7:
				left = -1.0   # flags have no timer
			elif left <= 0.0:
				continue
			_powerups.append([POWERUP_ICONS[i], left])


var _standings_t := 0.0
var _standings: Array = []


func _update_status(ps: Dictionary, info: Dictionary, level_time: int) -> void:
	var gt := int(info.get("gametype", 0))
	var pers: PackedInt32Array = ps["persistant"]
	status.teams = gt >= E.GT_TEAM
	var start := Q3Server.configstring(E.CS_LEVEL_START_TIME).to_int()
	var elapsed := maxi(level_time - start, 0)
	var tl := Q3Server.cvar("timelimit").to_int()
	var warmup := Q3Server.configstring(E.CS_WARMUP)
	var ms := elapsed
	if tl > 0:
		ms = maxi(tl * 60000 - elapsed, 0)
	var secs := ms / 1000
	status.clock = "%d:%02d" % [secs / 60, secs % 60]
	status.warn = tl > 0 and ms < 60000
	if warmup != "" and warmup != "0":
		status.clock = "WARMUP" if warmup.to_int() < 0 else "0:00"
		status.warn = false
	var fl := Q3Server.cvar("fraglimit").to_int()
	var cl := Q3Server.cvar("capturelimit").to_int()
	status.limit = ("FIRST TO %d" % cl) if gt == E.GT_CTF and cl > 0 else (("FRAG LIMIT %d" % fl) if fl > 0 and gt != E.GT_CTF else "")
	status.my_score = int(pers[E.PERS_SCORE])
	status.my_place = S.place(int(pers[E.PERS_RANK]))
	status.my_team = int(pers[E.PERS_TEAM])
	status.red = Q3Server.configstring(E.CS_SCORES1).to_int()
	status.blue = Q3Server.configstring(E.CS_SCORES2).to_int()
	_standings_t -= get_process_delta_time()
	if _standings_t <= 0.0 and info.has("standings"):
		_standings = (info["standings"] as Callable).call()
		_standings_t = 0.25
	status.other_name = ""
	for s in _standings:
		if not s.get("local", false):
			status.other_name = String(s["name"])
			status.other_score = int(s["score"])
			break
	_warmup_text = ""
	if warmup != "" and warmup != "0":
		var wv := warmup.to_int()
		if wv < 0:
			_warmup_text = "WARMUP"
			_warmup_count = -1
			_warmup_state = -1
		else:
			if _warmup_state <= 0:
				_queue_sound("sound/feedback/prepare.wav", false)   # CG_ParseWarmup: countPrepareSound
				_warmup_state = 1
			var sec := (wv - level_time) / 1000
			if sec != _warmup_count:
				_warmup_count = sec
				match sec:
					0: _play_now("sound/feedback/one.wav")
					1: _play_now("sound/feedback/two.wav")
					2: _play_now("sound/feedback/three.wav")
			_warmup_text = "STARTS IN %d" % (sec + 1)
	else:
		_warmup_state = 0


# ---------------------------------------------------------------- cgame's local feedback

func _check_local_sounds(ps: Dictionary, ops: Dictionary, info: Dictionary) -> void:
	var p: PackedInt32Array = ps["persistant"]
	var o: PackedInt32Array = ops["persistant"]
	if p[E.PERS_TEAM] != o[E.PERS_TEAM]:
		return
	if p[E.PERS_HITS] > o[E.PERS_HITS]:
		_play_now("sound/feedback/hit.wav")
		crosshair.hit()
	elif p[E.PERS_HITS] < o[E.PERS_HITS]:
		_play_now("sound/feedback/hit_teammate.wav")
	var reward := false
	var awards := [[E.PERS_CAPTURES, "sound/teamplay/flagcapture_yourteam.wav", "medal_capture"],
		[E.PERS_IMPRESSIVE_COUNT, "sound/feedback/impressive.wav", "medal_impressive"],
		[E.PERS_EXCELLENT_COUNT, "sound/feedback/excellent.wav", "medal_excellent"],
		[E.PERS_GAUNTLET_FRAG_COUNT, "sound/feedback/humiliation.wav", "medal_gauntlet"],
		[E.PERS_DEFEND_COUNT, "sound/feedback/defense.wav", "medal_defend"],
		[E.PERS_ASSIST_COUNT, "sound/feedback/assist.wav", "medal_assist"]]
	for a in awards:
		if p[a[0]] != o[a[0]]:
			_rewards.append({"sound": a[1], "medal": a[2], "count": int(p[a[0]])})
			reward = true
	if p[E.PERS_PLAYEREVENTS] != o[E.PERS_PLAYEREVENTS]:
		var changed := int(p[E.PERS_PLAYEREVENTS]) ^ int(o[E.PERS_PLAYEREVENTS])
		if changed & PLAYEREVENT_DENIEDREWARD:
			_play_now("sound/feedback/denied.wav")
		elif changed & PLAYEREVENT_GAUNTLETREWARD:
			_play_now("sound/feedback/humiliation.wav")
		elif changed & PLAYEREVENT_HOLYSHIT:
			_play_now("sound/feedback/voc_holyshit.wav")
		reward = true
	var gt := int(info.get("gametype", 0))
	var warmup := Q3Server.configstring(E.CS_WARMUP)
	var in_warmup := warmup != "" and warmup != "0"
	if not reward and not in_warmup and p[E.PERS_RANK] != o[E.PERS_RANK] and gt < E.GT_TEAM:
		if p[E.PERS_RANK] == 0:
			_queue_sound("sound/feedback/takenlead.wav", false)
		elif p[E.PERS_RANK] == RANK_TIED:
			_queue_sound("sound/feedback/tiedlead.wav", false)
		elif (int(o[E.PERS_RANK]) & ~RANK_TIED) == 0:
			_queue_sound("sound/feedback/lostlead.wav", false)
	# time and frag limit warnings
	var tl := Q3Server.cvar("timelimit").to_int()
	if tl > 0 and not in_warmup:
		var msec := int(info.get("level_time", 0)) - Q3Server.configstring(E.CS_LEVEL_START_TIME).to_int()
		if not (_time_warnings & 4) and msec > (tl * 60 + 2) * 1000:
			_time_warnings |= 7
			_play_now("sound/feedback/sudden_death.wav")
		elif not (_time_warnings & 2) and msec > (tl - 1) * 60000:
			_time_warnings |= 3
			_play_now("sound/feedback/1_minute.wav")
		elif tl > 5 and not (_time_warnings & 1) and msec > (tl - 5) * 60000:
			_time_warnings |= 1
			_play_now("sound/feedback/5_minute.wav")
	var fl := Q3Server.cvar("fraglimit").to_int()
	if fl > 0 and gt < E.GT_CTF:
		var high := Q3Server.configstring(E.CS_SCORES1).to_int()
		if not (_frag_warnings & 4) and high == fl - 1:
			_frag_warnings |= 7
			_queue_sound("sound/feedback/1_frag.wav", false)
		elif fl > 2 and not (_frag_warnings & 2) and high == fl - 2:
			_frag_warnings |= 3
			_queue_sound("sound/feedback/2_frags.wav", false)
		elif fl > 3 and not (_frag_warnings & 1) and high == fl - 3:
			_frag_warnings |= 1
			_queue_sound("sound/feedback/3_frags.wav", false)


## CG_DamageFeedback: the arc toward the attacker, and the view kick Gameplay adds to the camera.
func _damage_feedback(ps: Dictionary) -> void:
	var stats: PackedInt32Array = ps["stats"]
	var health := int(stats[E.STAT_HEALTH])
	var count := int(ps["damageCount"])
	var scale := 1.0 if health < 40 else 40.0 / maxf(health, 1.0)
	var kick := clampf(count * scale, 5.0, 10.0)
	var yaw_b := int(ps["damageYaw"])
	var pitch_b := int(ps["damagePitch"])
	if yaw_b == 255 and pitch_b == 255:
		_dmg_pitch = -kick
		_dmg_roll = 0.0
		damage_fx.damage(NAN, count)
	else:
		# the direction the damage came from, against the view axes
		var dir := -Q3Client.forward_of(Vector3(pitch_b / 255.0 * 360.0, yaw_b / 255.0 * 360.0, 0))
		var view: Vector3 = ps["viewangles"]
		var vf := Q3Client.forward_of(view)
		var yaw := deg_to_rad(view.y)
		var vl := Vector3(-sin(yaw), cos(yaw), 0)
		var front := dir.dot(vf)
		var left := dir.dot(vl)
		_dmg_roll = kick * left
		_dmg_pitch = -kick * front
		damage_fx.damage(atan2(-left, front), count)
	_dmg_time = Time.get_ticks_msec()


## The damage kick (pitch, roll in degrees) to add to the view now (CG_OffsetFirstPersonView).
func view_kick() -> Vector2:
	var ratio := float(Time.get_ticks_msec()) - _dmg_time
	if ratio < DAMAGE_DEFLECT_TIME:
		ratio /= DAMAGE_DEFLECT_TIME
	else:
		ratio = 1.0 - (ratio - DAMAGE_DEFLECT_TIME) / DAMAGE_RETURN_TIME
	if ratio <= 0.0:
		return Vector2.ZERO
	return Vector2(_dmg_pitch, _dmg_roll) * ratio


## CG_CheckAmmo: about five seconds of fire left is low; none is empty. The change clicks.
func _check_ammo(ps: Dictionary) -> void:
	var stats: PackedInt32Array = ps["stats"]
	var ammo: PackedInt32Array = ps["ammo"]
	var weapons := int(stats[E.STAT_WEAPONS])
	var total := 0
	for i in range(E.WP_MACHINEGUN, E.WP_NUM_WEAPONS):
		if not (weapons & (1 << i)):
			continue
		var n := int(ammo[i]) if i < ammo.size() else 0
		if n < 0:
			total = 5000
		elif i in [E.WP_ROCKET_LAUNCHER, E.WP_GRENADE_LAUNCHER, E.WP_RAILGUN, E.WP_SHOTGUN]:
			total += n * 1000
		else:
			total += n * 200
		if total >= 5000:
			_low_ammo = 0
			return
	var prev := _low_ammo
	_low_ammo = 2 if total == 0 else 1
	if _low_ammo != prev:
		_play_now("sound/weapons/noammo.wav")


# ---------------------------------------------------------------- events from Gameplay

func center_print(text: String, y := 0.3, big := false) -> void:
	_center = text
	_center_t = 0.0
	_center_y = y
	_center_big = big


func fatal(text: String) -> void:
	_fatal = text
	_msg.queue_redraw()


func notify(text: String, chat := false) -> void:
	_notify.append({"text": text, "chat": chat, "t": 0.0})
	while _notify.size() > 5:
		_notify.remove_at(0)


func obituary(attacker: String, victim: String, mod: int, attacker_me: bool, victim_me: bool, at := 0, vt := 0) -> void:
	feed.add(attacker, victim, mod, attacker_me, victim_me, at, vt)
	if victim_me:
		killer = attacker


## EV_ITEM_PICKUP for you: its icon and name for 3 s, and the crosshair pulse.
func pickup(classname: String) -> void:
	var it: Dictionary = Q3.entities.get(classname, {})
	_pickup = {"name": String(it.get("name", classname)), "icon": String(it.get("icon", "")).get_file()}
	_pickup_t = 0.0
	crosshair.pulse()


## EV_JUMP for you.
func jumped(ups: float) -> void:
	speedometer.jumped(ups)


## map_restart after a warmup: "FIGHT!"
func fight() -> void:
	_play_now("sound/feedback/fight.wav")
	center_print("FIGHT!", 0.3, true)


func _item_icon(item_index: int) -> String:
	var names: PackedStringArray = ClassDB.class_call_static("Q3Core", "item_classnames") if ClassDB.class_exists("Q3Core") else PackedStringArray()
	if item_index < 1 or item_index > names.size():
		return ""
	return String(Q3.entities.get(names[item_index - 1], {}).get("icon", "")).get_file()


# ---------------------------------------------------------------- sound

func _stream(path: String) -> AudioStream:
	var p := "res://library/sounds/" + path.trim_prefix("sound/")
	return load(p) if ResourceLoader.exists(p) else null


## A local sound right away (trap_S_StartLocalSound).
func _play_now(path: String) -> void:
	var s := _stream(path)
	if s == null:
		return
	var a := AudioStreamPlayer.new()
	a.stream = s
	a.bus = _announcer.bus
	add_child(a)
	a.finished.connect(a.queue_free)
	a.play()


## CG_AddBufferedSound: announcer lines one after another. `front` jumps the queue (rewards).
func _queue_sound(path: String, front: bool) -> void:
	if path == "":
		return
	if front:
		_sound_queue.push_front(path)
	else:
		_sound_queue.append(path)
	if not _announcer.playing:
		_next_sound()


func _next_sound() -> void:
	while not _sound_queue.is_empty():
		var s := _stream(_sound_queue.pop_front())
		if s:
			_announcer.stream = s
			_announcer.play()
			return


# ---------------------------------------------------------------- messages

func _draw_messages() -> void:
	var vs := _msg.size
	var font := S.label()
	if _fatal != "":
		var lines := _fatal.split("\n")
		for i in lines.size():
			S.text(_msg, S.num_medium(), Vector2(vs.x * 0.5, vs.y * 0.4 + i * 34 * u), lines[i], int(26 * u), S.INK_HOT, HORIZONTAL_ALIGNMENT_CENTER)
		return
	# console prints and chat, top left
	var y := 40.0 * u
	for n in _notify:
		var a := clampf((6.0 - float(n["t"])) / 0.5, 0.0, 1.0)
		S.text(_msg, S.num_medium(), Vector2(34 * u, y), String(n["text"]), int(18 * u), Color(S.AMBER if n["chat"] else S.INK, a))
		y += 24 * u
	# the center print
	if _center != "" and _center_t < CENTER_TIME:
		var a := clampf((CENTER_TIME - _center_t) / 0.5, 0.0, 1.0)
		var big := _center_big
		var lines := _center.split("\n")
		var size := int((64.0 if big else 30.0) * u)
		var pop := 1.0 + (1.0 - clampf(_center_t / 0.15, 0.0, 1.0)) * 0.25
		for i in lines.size():
			var col := S.INK_HOT if i == 0 else S.DIM
			S.text(_msg, S.label(), Vector2(vs.x * 0.5, vs.y * _center_y + i * size * 1.2), lines[i],
				int(size * pop * (1.0 if i == 0 else 0.7)), Color(col, a), HORIZONTAL_ALIGNMENT_CENTER)
	# warmup
	if _warmup_text != "":
		S.text(_msg, font, Vector2(vs.x * 0.5, vs.y * 0.2), _warmup_text, int(40 * u), S.INK_HOT, HORIZONTAL_ALIGNMENT_CENTER)
		if _warmup_text == "WARMUP":
			var sub := "PRESS F3 WHEN YOU'RE READY" if Q3Server.cvar("g_readyUp") == "1" else "WAITING FOR PLAYERS"
			S.text(_msg, font, Vector2(vs.x * 0.5, vs.y * 0.2 + 34 * u), sub, int(18 * u), S.DIM, HORIZONTAL_ALIGNMENT_CENTER)
	# the reward on show
	if not _reward.is_empty() and _reward_t < REWARD_TIME:
		var a := clampf((REWARD_TIME - _reward_t) / 0.4, 0.0, 1.0)
		var pop := 1.0 + (1.0 - clampf(_reward_t / 0.2, 0.0, 1.0)) * 0.6
		var tex := S.icon(String(_reward["medal"]))
		var sz := 72.0 * u * pop
		var r := Rect2(Vector2(vs.x * 0.5 - sz * 0.5, vs.y * 0.13), Vector2(sz, sz))
		if tex:
			_msg.draw_texture_rect(tex, r, false, Color(1, 1, 1, a))
		if int(_reward["count"]) > 1:
			S.text(_msg, S.num(), Vector2(r.end.x + 8 * u, r.end.y - 8 * u), "x%d" % int(_reward["count"]), int(26 * u), Color(S.AMBER, a))
	# the pickup, above the vitals
	if not _pickup.is_empty() and _pickup_t < PICKUP_TIME:
		var a := clampf((PICKUP_TIME - _pickup_t) / 0.5, 0.0, 1.0)
		var p := vitals.position + Vector2(8 * u, -46 * u)
		var tex := S.icon(String(_pickup["icon"]))
		if tex:
			_msg.draw_texture_rect(tex, Rect2(p, Vector2.ONE * 36 * u), false, Color(1, 1, 1, a))
		S.text(_msg, S.num_medium(), p + Vector2(46 * u, 26 * u), String(_pickup["name"]), int(21 * u), Color(S.INK_HOT, a))
	# powerups, above the ammo
	var py := ammo_panel.position.y - 56 * u
	for pw in _powerups:
		var tex := S.icon(String(pw[0]))
		var x := ammo_panel.position.x + ammo_panel.size.x - 110 * u
		if tex:
			_msg.draw_texture_rect(tex, Rect2(Vector2(x, py), Vector2.ONE * 44 * u), false)
		if float(pw[1]) >= 0.0:
			var left := ceili(float(pw[1]))
			S.text(_msg, S.num(), Vector2(x + 54 * u, py + 34 * u), str(left), int(32 * u), S.RED if left <= 5 else S.INK_HOT)
		py -= 52 * u
	# CG_DrawAmmoWarning
	if _low_ammo > 0 and not _dead:
		S.text(_msg, font, Vector2(vs.x * 0.5, vs.y * 0.62), "OUT OF AMMO" if _low_ammo == 2 else "LOW AMMO WARNING", int(20 * u),
			Color(S.RED, 0.85), HORIZONTAL_ALIGNMENT_CENTER)
	# dead: who did it, and how to come back
	if _dead:
		if killer != "":
			S.text(_msg, S.num_medium(), Vector2(vs.x * 0.5, vs.y * 0.7), "Fragged by " + killer, int(26 * u), S.INK_HOT, HORIZONTAL_ALIGNMENT_CENTER)
		S.text(_msg, font, Vector2(vs.x * 0.5, vs.y * 0.7 + 34 * u), "FIRE TO RESPAWN", int(16 * u), S.DIM, HORIZONTAL_ALIGNMENT_CENTER)
	if Settings.get_value("hud/fps"):
		S.text(_msg, S.num_medium(), Vector2(vs.x - 34 * u, vs.y * 0.5 - 140 * u), "%d FPS" % _fps, int(16 * u), S.DIM, HORIZONTAL_ALIGNMENT_RIGHT)
