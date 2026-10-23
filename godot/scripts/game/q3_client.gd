class_name Q3Client
extends Node3D
## The presentation side of a match, cgame's job: turns the game's snapshots into the remastered
## library. Players are Q3Characters (layered legs/torso clips, weapon at tag_weapon), items are
## Q3Pickups, missiles carry their Q3FX visuals, movers move the map's model_<n> nodes, and every
## entity and playerState event becomes an effect or a sound, following cg_event.c / cg_ents.c.
## Positions arrive in id units and axes; Q3.q3_to_godot() converts. A fragged player goes limp
## (Q3Character.ragdoll), stays as the corpse for 10 s after respawning, then burns away
## (Q3Character.disintegrate); a gib replaces it.

signal obituary(attacker: int, target: int, means: int)   ## EV_OBITUARY, for the kill feed
signal server_print(text: String)
signal landed(change: float)
signal local_event(ev: int, parm: int)   ## every predictable event of the local player (ps.events)   ## the local player's landing dip (cg.landChange): -8, -16 or -24 units

const E := preload("res://scripts/game/q3_enums.gd")
const WEAPON_ITEMS := ["", "weapon_gauntlet", "weapon_machinegun", "weapon_shotgun", "weapon_grenadelauncher",
	"weapon_rocketlauncher", "weapon_lightning", "weapon_railgun", "weapon_plasmagun", "weapon_bfg"]
## CG_RegisterWeapon: flashSound (one picked at random per shot), and the loops while the weapon
## is out (readySound) or fire is held (firingSound)
const FLASH_SOUNDS := [[], ["melee/fstatck"], ["machinegun/machgf1b", "machinegun/machgf2b", "machinegun/machgf3b",
	"machinegun/machgf4b"], ["shotgun/sshotf1b"], ["grenade/grenlf1a"], ["rocket/rocklf1a"], ["lightning/lg_fire"],
	["railgun/railgf1a"], ["plasma/hyprbf1a"], ["bfg/bfg_fire"], ["rocket/rocklf1a"]]
const READY_SOUNDS := {6: "melee/fsthum", 7: "railgun/rg_hum", 9: "bfg/bfg_hum", 10: "melee/fsthum"}
const FIRING_SOUNDS := {1: "melee/fstrun", 6: "lightning/lg_hum", 10: "melee/fstrun"}
const EXPLOSIONS := {E.WP_ROCKET_LAUNCHER: "rocket", E.WP_GRENADE_LAUNCHER: "grenade", E.WP_PLASMAGUN: "plasma",
	E.WP_BFG: "bfg", E.WP_RAILGUN: "rail", E.WP_MACHINEGUN: "bullet", E.WP_SHOTGUN: "shotgun_pellet",
	E.WP_LIGHTNING: "lightning"}
## missile smoke (CG_RocketTrail / CG_GrenadeTrail): a puff every 50 ms; radius, life (ms)
const TRAILS := {E.WP_ROCKET_LAUNCHER: [64.0, 2000.0], E.WP_GRENADE_LAUNCHER: [32.0, 700.0]}
const MASK_SHOT := 0x6000001          ## CONTENTS_SOLID | CONTENTS_BODY | CONTENTS_CORPSE
const MASK_WATER := 32 | 16 | 8       ## CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA
const SURF_NOIMPACT := 0x10
const ENTITYNUM_WORLD := 1022
const LIGHTNING_RANGE := 768.0
const TRACER_CHANCE := 0.4            ## cg_tracerChance

var map: Q3Map                     ## the map scene (movers live in it)
var local_client := -1
var _ents: Dictionary = {}         ## entity number -> {"node": Node3D, "es": Dictionary, "kind": int}
var _seen_events: Dictionary = {}  ## entity number -> last event value handled
var _ps_event_seq := -1
var _item_names: PackedStringArray = []
var _player_models: Dictionary = {}   ## client -> model name from CS_PLAYERS
var _sounds: Dictionary = {}       ## path -> AudioStream
var _time := 0                     ## server time of the snapshot being shown (ms)
var _corpses: Array[Node3D] = []   ## ragdolls left behind when their players respawn
var view_fx: Q3WeaponFx            ## the first-person weapon's flash and barrel (Gameplay sets it)
var eye := Vector3.ZERO            ## the camera, for effects that face it
var _now := 0.0                    ## render time (ms), cg.time
var _local_ps: Dictionary = {}
var _smoke: Q3Puffs                ## smokePuff: rocket and grenade trails
var _shotgun_smoke: Q3Puffs
var _beams: Dictionary = {}        ## entity -> Q3LightningBeam while the lightning gun fires
var _loops: Dictionary = {}        ## entity -> its weapon loop (hum while out, buzz while firing)
var self_body := false            ## draw the local player for mirrors (the map has portal views)
var _self: Dictionary = {}         ## that body's entity record
var pickup_effects := 0           ## pickup effects played (tests)
var _pickups: Array = []           ## recent pickup events: [position, time ms] (the pickup effect)
var _ext_event := 0                ## the last ps.externalEvent played
const MAX_PREDICTED_EVENTS := 16
var _predictable: PackedInt32Array = PackedInt32Array()   ## cg.predictableEvents: what each slot played
var _local_lightning := false      ## cent->pe.lightningFiring for the local player
## Quake 3 sinks bodies into the floor after 5 s (g_client.c BODY_SINK_TIME); the remaster keeps
## them longer (Ozmium, 2026-10-22), then burns them away (Q3Character.disintegrate).
const CORPSE_TIME := 10.0
const CORPSE_BURN := 1.6


func _ready() -> void:
	if ClassDB.class_exists("Q3Core"):
		_item_names = ClassDB.class_call_static("Q3Core", "item_classnames")
	_predictable.resize(MAX_PREDICTED_EVENTS)
	_smoke = Q3Puffs.create(self, "smoke_puff")
	_shotgun_smoke = Q3Puffs.create(self, "shotgun_smoke")


## One snapshot (Q3Server.snapshot()) plus the live local playerState.
func apply_snapshot(snap: Dictionary, local_ps: Dictionary) -> void:
	if snap.is_empty():
		return
	_time = int(snap["serverTime"])
	var present := {}
	for es in snap["entities"]:
		var num := int(es["number"])
		present[num] = true
		if int(es["eType"]) >= E.ET_EVENTS:
			_temp_event(num, es)
			continue
		_update_entity(num, es)
		_entity_event(num, es)
	_local_events(local_ps)   # before removals: the item the player just took leaves in this snapshot
	for num in _ents.keys():
		if not present.has(num):
			_remove(num)
	for num in _seen_events.keys():
		if not present.has(num):
			_seen_events.erase(num)


## Every frame: movers and missiles follow their trajectories at the current time.
func animate(server_time_ms: float) -> void:
	_now = server_time_ms
	for num in _ents:
		var e: Dictionary = _ents[num]
		var kind: int = e["kind"]
		if kind == E.ET_MOVER or kind == E.ET_MISSILE:
			var node: Node3D = e["node"]
			if node and is_instance_valid(node):
				node.global_position = Q3.q3_to_godot(trajectory(e["es"]["pos"], server_time_ms))
				if kind == E.ET_MOVER:
					node.basis = angles_basis(trajectory(e["es"]["apos"], server_time_ms))
				else:
					_missile_frame(e, node)


## cg_ents.c CG_Missile: the model's forward on its trajectory's direction, spun about it
## (cg.time / 4 degrees in flight, s.time at rest), and its smoke trail.
func _missile_frame(e: Dictionary, node: Node3D) -> void:
	var es: Dictionary = e["es"]
	var tr: Dictionary = es["pos"]
	var moving := int(tr.get("trType", 0)) != 0
	var delta: Vector3 = tr.get("trDelta", Vector3.ZERO)
	var z := Vector3(delta.y, delta.z, delta.x).normalized()
	if z == Vector3.ZERO:
		z = Vector3.UP
	var x0 := z.cross(Vector3.UP if absf(z.y) < 0.95 else Vector3.RIGHT).normalized()
	var x := x0.rotated(z, deg_to_rad(_now / 4.0 if moving else float(es.get("time", 0))))
	node.basis = Basis(x, z.cross(x), z)
	var trail: Array = TRAILS.get(int(es["weapon"]), [])
	if trail.is_empty():
		return
	# CG_RocketTrail: a puff at every 50 ms step of the trajectory since the last frame
	var step := 50.0
	var start: float = e.get("trail_time", float(_time))
	e["trail_time"] = _now
	if not moving:
		return
	if Q3Server.point_contents(trajectory(tr, _now), -1) & MASK_WATER:
		return
	var t := step * floorf((start + step) / step)
	while t <= _now:
		_smoke.add(Q3.q3_to_godot(trajectory(tr, t)), Vector3.ZERO, trail[0], 0.33, trail[1], t)
		t += step


# ---------------------------------------------------------------- entities

func _update_entity(num: int, es: Dictionary) -> void:
	var kind := int(es["eType"])
	var e: Dictionary = _ents.get(num, {})
	if not e.is_empty() and e["kind"] != kind:
		_remove(num)
		e = {}
	if e.is_empty():
		var node := _spawn(num, kind, es)
		e = {"node": node, "es": es, "kind": kind, "legs": -1, "torso": -1, "weapon": -1}
		_ents[num] = e
	e["es"] = es
	var node: Node3D = e["node"]
	if node == null or not is_instance_valid(node):
		return
	match kind:
		E.ET_PLAYER:
			_update_player(e, es)
		E.ET_ITEM:
			node.global_position = Q3.q3_to_godot(trajectory(es["pos"], _time))
		E.ET_GENERAL:
			node.global_position = Q3.q3_to_godot(trajectory(es["pos"], _time))


func _spawn(num: int, kind: int, es: Dictionary) -> Node3D:
	match kind:
		E.ET_PLAYER:
			if num >= E.MAX_CLIENTS:
				return null   # the body queue's copy of a dead player: our ragdoll is the corpse
			var cl := int(es["clientNum"])
			if cl == local_client and not (int(es["eFlags"]) & E.EF_DEAD):
				return null   # first person: the camera is the local player
			var ch := Q3Character.new()
			ch.model = _model_for(cl)
			add_child(ch)
			return ch
		E.ET_ITEM:
			var idx := int(es["modelindex"])
			if idx < 1 or idx > _item_names.size():
				return null
			var p := Q3Pickup.new()
			p.classname = _item_names[idx - 1]
			add_child(p)
			return p
		E.ET_MISSILE:
			var p := Q3FX.missile(self, int(es["weapon"]))
			p.global_position = Q3.q3_to_godot(trajectory(es["pos"], _time))
			return p   # the game moves it (animate) and explodes it (events)
		E.ET_MOVER:
			if map:
				return map.model_node(int(es["modelindex"]))
	return null


func _remove(num: int) -> void:
	var e: Dictionary = _ents.get(num, {})
	_ents.erase(num)
	_stop_loop(num)
	if _beams.has(num):
		(_beams[num] as Node).queue_free()
		_beams.erase(num)
	if e.is_empty():
		return
	var node: Node3D = e["node"]
	if node == null or not is_instance_valid(node) or e["kind"] == E.ET_MOVER:
		return   # movers belong to the map
	if e.get("ragdolled", false):
		# a fragged player who respawned out of sight leaves the snapshot: the body stays a corpse
		_leave_corpse(node)
		return
	if node is Q3Pickup and _picked_up_at(node.global_position):
		(node as Q3Pickup).collect()
		pickup_effects += 1
		return
	node.queue_free()


## Whether a pickup event (EV_ITEM_PICKUP) happened next to this point in the last half second:
## an item leaving the snapshot there was taken, not expired.
func _picked_up_at(pos: Vector3) -> bool:
	for p in _pickups:
		if _now - float(p[1]) < 500.0 and (p[0] as Vector3).distance_to(pos) < 2.5:
			return true
	return false


func _leave_corpse(ch: Node3D) -> void:
	if ch == null or not is_instance_valid(ch):
		return
	_corpses.append(ch)
	get_tree().create_timer(CORPSE_TIME).timeout.connect(func():
		_corpses.erase(ch)
		if is_instance_valid(ch):
			if ch.visible and ch is Q3Character:
				(ch as Q3Character).disintegrate(CORPSE_BURN)
			else:
				ch.queue_free())


## A gib (EV_GIB_PLAYER) replaces the nearest body: a ragdoll still owned by its player, or a corpse.
func _gib_body(at: Vector3) -> void:
	var best: Node3D = null
	var best_d := 2.5
	for num in _ents:
		var e: Dictionary = _ents[num]
		if e.get("ragdolled", false) and e["node"] and is_instance_valid(e["node"]):
			var d: float = (e["node"] as Node3D).global_position.distance_to(at)
			if d < best_d:
				best_d = d
				best = e["node"]
	for c in _corpses:
		if is_instance_valid(c) and c.global_position.distance_to(at) < best_d:
			best_d = c.global_position.distance_to(at)
			best = c
	if best:
		best.visible = false


func _model_for(cl: int) -> String:
	var info := Q3Host.parse_info(Q3Server.configstring(E.CS_PLAYERS + cl))
	var m := String(info.get("model", "sarge")).get_slice("/", 0).to_lower()
	_player_models[cl] = m
	return m if m in Q3.characters else "sarge"


func _update_player(e: Dictionary, es: Dictionary) -> void:
	var dead := (int(es["eFlags"]) & E.EF_DEAD) != 0
	var ch := e["node"] as Q3Character
	if e.get("ragdolled", false) and not dead:
		# respawned: the ragdoll stays behind as the corpse, a fresh character takes over
		_leave_corpse(ch)
		ch = _spawn(int(es["number"]), E.ET_PLAYER, es) as Q3Character
		e["node"] = ch
		e["ragdolled"] = false
		e.erase("armor_mat")   # the shell lives on the old body
		e["legs"] = -1
		e["torso"] = -1
		e["weapon"] = -1
	if ch == null:
		return
	if e.get("ragdolled", false):
		return   # physics owns it now
	if dead:
		# fragged: go limp, carrying the speed the body had
		var vel := Q3.q3_to_godot(es["pos"]["trDelta"])
		ch.ragdoll((vel * 0.6).limit_length(8.0))   # a shotgun's knockback can't fire limbs through floors
		e["ragdolled"] = true
		var old := ch.find_child("Attach_weapon", true, false)
		if old:
			old.queue_free()
		e.erase("wfx")
		return
	ch.global_position = Q3.q3_to_godot(trajectory(es["pos"], _time)) + Vector3.DOWN * 24.0 * Q3.UNIT   # origin is 24 above the feet
	var ang: Vector3 = es["apos"]["trBase"]
	ch.rotation = Vector3(0.0, deg_to_rad(ang.y), 0.0)
	ch.aim_pitch = ang.x   # the torso and head bend toward the aim (CG_PlayerAngles)
	var legs := int(es["legsAnim"]) & ~E.ANIM_TOGGLEBIT
	var torso := int(es["torsoAnim"]) & ~E.ANIM_TOGGLEBIT
	# the toggle bit restarts a clip that's played again
	if int(es["legsAnim"]) != e["legs"]:
		e["legs"] = int(es["legsAnim"])
		if legs < E.ANIM_NAMES.size():
			ch.play_legs(E.ANIM_NAMES[legs])
	if int(es["torsoAnim"]) != e["torso"]:
		e["torso"] = int(es["torsoAnim"])
		if torso < E.ANIM_NAMES.size() and legs > E.BOTH_DEAD3:
			ch.play_torso(E.ANIM_NAMES[torso])
	var w := int(es["weapon"])
	if w != e["weapon"]:
		e["weapon"] = w
		var old := ch.find_child("Attach_weapon", true, false)
		if old:
			old.queue_free()
		e.erase("wfx")
		if w > 0 and w < WEAPON_ITEMS.size() and not (int(es["eFlags"]) & E.EF_DEAD):
			var item: Dictionary = Q3.entities.get(WEAPON_ITEMS[w], {})
			if item.get("models", []).size():
				var prop := ch.attach(item["models"][0])
				if prop:
					e["wfx"] = Q3WeaponFx.bind(prop, w)
					(e["wfx"] as Q3WeaponFx).rail_color = client_colors(int(es["clientNum"]))[0]


# ---------------------------------------------------------------- events (cg_event.c)

## An entity's own event: fires when es.event changes (the two EV_EVENT_BITS make repeats differ).
func _entity_event(num: int, es: Dictionary) -> void:
	var ev := int(es["event"])
	if ev == 0 or _seen_events.get(num, -1) == ev:
		_seen_events[num] = ev
		return
	_seen_events[num] = ev
	if int(es["eType"]) == E.ET_PLAYER and int(es["clientNum"]) == local_client:
		return   # the local player's events come through its playerState
	_event(ev & ~E.EV_EVENT_BITS, es, Q3.q3_to_godot(trajectory(es["pos"], _time)))


## A temporary event entity (eType ET_EVENTS + event): fires once, when it first appears.
func _temp_event(num: int, es: Dictionary) -> void:
	if _seen_events.has(num):
		return
	_seen_events[num] = int(es["eType"])
	_event(int(es["eType"]) - E.ET_EVENTS, es, Q3.q3_to_godot(es["pos"]["trBase"]))


## The local player's events (ps.events and ps.externalEvent), cg_playerstate.c's CG_CheckPlayerstateEvents.
func _local_events(ps: Dictionary) -> void:
	if ps.is_empty():
		return
	# events the game added to the player outside its movement (G_AddEvent: item pickups without
	# cg_predictItems, pain, death) ride in externalEvent, played when it changes
	var ext := int(ps.get("externalEvent", 0))
	if ext != 0 and ext != _ext_event and _ps_event_seq >= 0:
		var eps := ps.duplicate()
		eps["eventParms"] = [int(ps.get("externalEventParm", 0)), int(ps.get("externalEventParm", 0))]
		_local_event(ext, eps, 0)
	_ext_event = ext
	var seq := int(ps["eventSequence"])
	if _ps_event_seq < 0:
		_ps_event_seq = seq
		return
	# CG_CheckChangedPredictableEvents: a slot already played whose event the server changed (a
	# predicted footstep where the server put the item pickup) plays the server's event too
	for i in range(seq - 2, seq):
		if i >= _ps_event_seq or i <= _ps_event_seq - MAX_PREDICTED_EVENTS:
			continue
		var raw := int(ps["events"][i & 1])
		if raw != _predictable[i & (MAX_PREDICTED_EVENTS - 1)]:
			_predictable[i & (MAX_PREDICTED_EVENTS - 1)] = raw
			_local_event(raw, ps, i)
	var from := maxi(_ps_event_seq, seq - 2)
	for i in range(from, seq):
		var raw := int(ps["events"][i & 1])
		_predictable[i & (MAX_PREDICTED_EVENTS - 1)] = raw
		_local_event(raw, ps, i)
	_ps_event_seq = seq


func _local_event(raw: int, ps: Dictionary, i: int) -> void:
	var ev := raw & ~E.EV_EVENT_BITS
	var es := {"clientNum": local_client, "weapon": ps["weapon"], "eventParm": ps["eventParms"][i & 1],
		"number": local_client, "eType": E.ET_PLAYER, "otherEntityNum": 0, "origin": ps["origin"], "origin2": Vector3.ZERO,
		"pos": {"trBase": ps["origin"]}, "apos": {"trBase": ps["viewangles"]}, "powerups": ps["powerups"][E.PW_QUAD]}
	_event(ev, es, Q3.q3_to_godot(ps["origin"]), true)
	local_event.emit(ev, int(ps["eventParms"][i & 1]))


func _event(ev: int, es: Dictionary, at: Vector3, local := false) -> void:
	var cl := int(es.get("clientNum", 0))
	match ev:
		E.EV_FIRE_WEAPON:
			_fire_weapon(es, at, local)
		E.EV_CHANGE_WEAPON:
			play_sound("sound/weapons/change.wav", at, local)
		E.EV_GRENADE_BOUNCE:
			play_sound("sound/weapons/grenade/hgrenb%da.wav" % (1 if randi() & 1 else 2), at)
		E.EV_MISSILE_HIT:
			_missile_hit_player(int(es["weapon"]), at, byte_dir(int(es["eventParm"])), int(es.get("otherEntityNum", -1)))
		E.EV_MISSILE_MISS, E.EV_MISSILE_MISS_METAL:
			missile_hit_wall(int(es["weapon"]), cl, at, byte_dir(int(es["eventParm"])))
		E.EV_RAILTRAIL:
			# es.origin2 is the muzzle, the event's position the end; 255: a no-impact surface
			var colors := client_colors(cl)
			Q3FX.rail(self, Q3.q3_to_godot(es["origin2"]), at, colors[0], colors[1])
			if int(es["eventParm"]) != 255:
				missile_hit_wall(E.WP_RAILGUN, cl, at, byte_dir(int(es["eventParm"])))
		E.EV_BULLET_HIT_WALL:
			_bullet(es["pos"]["trBase"], int(es.get("otherEntityNum", -1)), byte_dir(int(es["eventParm"])), false, ENTITYNUM_WORLD)
		E.EV_BULLET_HIT_FLESH:
			_bullet(es["pos"]["trBase"], int(es.get("otherEntityNum", -1)), Vector3.UP, true, int(es["eventParm"]))
		E.EV_SHOTGUN:
			_shotgun_fire(es)
		E.EV_PAIN:
			var hp := int(es["eventParm"])
			var which := "25" if hp < 25 else "50" if hp < 50 else "75" if hp < 75 else "100"
			play_sound("sound/player/%s/pain%s_1.wav" % [_player_models.get(cl, "sarge"), which], at, local)
		E.EV_DEATH1, E.EV_DEATH2, E.EV_DEATH3:
			play_sound("sound/player/%s/death%d.wav" % [_player_models.get(cl, "sarge"), ev - E.EV_DEATH1 + 1], at, local)
		E.EV_GIB_PLAYER:
			_gib_body(at)
			Q3FX.gibs(self, at)
			play_sound("sound/player/gibsplt1.wav", at)
		E.EV_JUMP:
			play_sound("sound/player/%s/jump1.wav" % _player_models.get(cl, "sarge"), at, local)
		E.EV_JUMP_PAD:
			play_sound("sound/world/jumppad.wav", at, local)
		E.EV_FALL_SHORT:
			play_sound("sound/player/land1.wav", at, local)
			if local:
				landed.emit(-8.0)
		E.EV_FALL_MEDIUM:
			play_sound("sound/player/%s/pain100_1.wav" % _player_models.get(cl, "sarge"), at, local)
			if local:
				landed.emit(-16.0)
		E.EV_FALL_FAR:
			play_sound("sound/player/%s/fall1.wav" % _player_models.get(cl, "sarge"), at, local)
			if local:
				landed.emit(-24.0)
		E.EV_FOOTSTEP, E.EV_FOOTSTEP_METAL:
			play_sound("sound/player/footsteps/%s%d.wav" % ["clank" if ev == E.EV_FOOTSTEP_METAL else "step", randi_range(1, 4)], at, local, -8.0)
		E.EV_ITEM_PICKUP, E.EV_GLOBAL_ITEM_PICKUP:
			_pickups.append([at, _now])
			if _pickups.size() > 8:
				_pickups.pop_front()
			var idx := int(es["eventParm"])
			if idx >= 1 and idx <= _item_names.size():
				var it: Dictionary = Q3.entities.get(_item_names[idx - 1], {})
				if it.get("pickup_sound", ""):
					play_sound(String(it["pickup_sound"]), at, local or ev == E.EV_GLOBAL_ITEM_PICKUP)
		E.EV_ITEM_RESPAWN:
			play_sound("sound/items/respawn1.wav", at)
		E.EV_PLAYER_TELEPORT_IN:
			Q3FX.teleport(self, at)
			play_sound("sound/world/telein.wav", at)
		E.EV_PLAYER_TELEPORT_OUT:
			play_sound("sound/world/teleout.wav", at)
		E.EV_GENERAL_SOUND, E.EV_GLOBAL_SOUND, E.EV_GLOBAL_TEAM_SOUND:
			var path := Q3Server.configstring(E.CS_SOUNDS + int(es["eventParm"]))
			if path != "" and not path.begins_with("*"):
				play_sound(path, at, ev != E.EV_GENERAL_SOUND)
		E.EV_OBITUARY:
			obituary.emit(int(es["otherEntityNum2"]), int(es["otherEntityNum"]), int(es["eventParm"]))


# ---------------------------------------------------------------- weapons (cg_weapons.c)

## CG_FireWeapon: the flash, the shot's sound (the lightning gun only on its first shot, then its
## loop takes over), the quad's boom, and brass for the machinegun and shotgun.
func _fire_weapon(es: Dictionary, at: Vector3, local: bool) -> void:
	var w := int(es["weapon"])
	if w <= 0 or w >= FLASH_SOUNDS.size():
		return
	var num := int(es.get("number", -1))
	var fx: Q3WeaponFx = view_fx if local else _ents.get(num, {}).get("wfx")
	if fx and fx.valid():
		fx.fire(_now)
	var firing_lightning: bool = _local_lightning if local else _ents.get(num, {}).get("lightning", false)
	if w == E.WP_LIGHTNING and firing_lightning:
		return
	var quad := int(es.get("powerups", 0)) > 0 if local else (int(es.get("powerups", 0)) & (1 << E.PW_QUAD)) != 0
	if quad:
		play_sound("sound/items/damage3.wav", at, local)
	var sounds: Array = FLASH_SOUNDS[w]
	if sounds.size():
		play_sound("sound/weapons/%s.wav" % sounds[randi() % sounds.size()], at, local)
	if w == E.WP_MACHINEGUN or w == E.WP_SHOTGUN:
		var origin: Vector3 = es["pos"]["trBase"]
		var angles: Vector3 = es.get("apos", {}).get("trBase", Vector3.ZERO)
		Q3FX.brass(self, "machinegun" if w == E.WP_MACHINEGUN else "shotgun", Q3.q3_to_godot(origin), angles_basis(angles))


## CG_MissileHitWall: the explosion and mark for the weapon, and its impact sound.
func missile_hit_wall(weapon: int, cl: int, at: Vector3, dir: Vector3) -> void:
	var kind: String = EXPLOSIONS.get(weapon, "")
	if kind == "":
		return
	var snd := ""
	match weapon:
		E.WP_LIGHTNING:
			var r := randi() & 3
			snd = "lightning/lg_hit2" if r < 2 else "lightning/lg_hit" if r == 2 else "lightning/lg_hit3"
		E.WP_GRENADE_LAUNCHER, E.WP_ROCKET_LAUNCHER, E.WP_BFG:
			snd = "rocket/rocklx1a"
		E.WP_RAILGUN, E.WP_PLASMAGUN:
			snd = "plasma/plasmx1a"
		E.WP_MACHINEGUN:
			var r := randi() & 3
			snd = "machinegun/ric1" if r == 0 else "machinegun/ric2" if r == 1 else "machinegun/ric3"
	if snd != "":
		play_sound("sound/weapons/%s.wav" % snd, at)
	if weapon == E.WP_RAILGUN:
		# the flash's shader ignores color1 (rgbGen wave); the mark takes color2
		Q3FX.explosion(self, kind, at, dir, Color.WHITE, client_colors(cl)[1])
	else:
		Q3FX.explosion(self, kind, at, dir)


## CG_MissileHitPlayer: blood, and for grenades and rockets the blast as well.
func _missile_hit_player(weapon: int, at: Vector3, dir: Vector3, target: int) -> void:
	_bleed(at, target)
	if weapon == E.WP_GRENADE_LAUNCHER or weapon == E.WP_ROCKET_LAUNCHER:
		missile_hit_wall(weapon, 0, at, dir)


## CG_Bleed: the blood puff, except your own in first person.
func _bleed(at: Vector3, target: int) -> void:
	if target == local_client:
		return
	Q3FX.blood(self, at)


## CG_Bullet: a tracer from the shooter's muzzle now and then, then the wall hit or blood.
## end is in id units; normal is Godot's.
func _bullet(end: Vector3, source: int, normal: Vector3, flesh: bool, flesh_ent: int) -> void:
	var at := Q3.q3_to_godot(end)
	if source >= 0 and randf() < TRACER_CHANCE:
		var start := _muzzle_point(source)
		if start != Vector3.INF:
			var a := Q3.q3_to_godot(start)
			Q3FX.tracer(self, a, at, eye)
			play_sound("sound/weapons/machinegun/buletby1.wav", (a + at) * 0.5)
	if flesh:
		_bleed(at, flesh_ent)
	else:
		missile_hit_wall(E.WP_MACHINEGUN, 0, at, normal)


## CG_CalcMuzzlePoint (id units): the eye plus 14 units forward.
func _muzzle_point(num: int) -> Vector3:
	if num == local_client and not _local_ps.is_empty():
		var m: Vector3 = _local_ps["origin"]
		m.z += float(_local_ps["viewheight"])
		return m + forward_of(_local_ps["viewangles"]) * 14.0
	var e: Dictionary = _ents.get(num, {})
	if e.is_empty():
		return Vector3.INF
	var es: Dictionary = e["es"]
	var m: Vector3 = es["pos"]["trBase"]
	var anim := int(es.get("legsAnim", 0)) & ~E.ANIM_TOGGLEBIT
	m.z += 12.0 if anim == E.LEGS_WALKCR or anim == E.LEGS_IDLECR else float(E.DEFAULT_VIEWHEIGHT)
	return m + forward_of(es["apos"]["trBase"]) * 14.0


## CG_ShotgunFire: a smoke puff in front of the muzzle, then every pellet of the server's own
## pattern (the same seed) traced through the world.
func _shotgun_fire(es: Dictionary) -> void:
	var origin: Vector3 = es["pos"]["trBase"]
	var dir: Vector3 = (es["origin2"] as Vector3).normalized()
	if not (Q3Server.point_contents(origin, 0) & 32):
		# (id's code measures from origin2 - origin, but origin2 is a direction: the puff goes 32 units along it)
		_shotgun_smoke.add(Q3.q3_to_godot(origin + dir * 32.0), Vector3(0, 8, 0) * Q3.UNIT, 32.0, 0.33, 900.0, _now, false)
	var right := perpendicular(dir)
	var up := dir.cross(right)
	var seed := [int(es["eventParm"])]
	for i in 11:   # DEFAULT_SHOTGUN_COUNT; DEFAULT_SHOTGUN_SPREAD 700 * 16
		var r := q_crandom(seed) * 700.0 * 16.0
		var u := q_crandom(seed) * 700.0 * 16.0
		var end := origin + dir * (8192.0 * 16.0) + right * r + up * u
		_shotgun_pellet(origin, end, int(es.get("otherEntityNum", -1)))


func _shotgun_pellet(start: Vector3, end: Vector3, skip: int) -> void:
	var tr := Q3Server.trace(start, end, Vector3.ZERO, Vector3.ZERO, skip, MASK_SHOT)
	if tr.is_empty() or int(tr["surfaceFlags"]) & SURF_NOIMPACT:
		return
	var at := Q3.q3_to_godot(tr["endpos"])
	var n: Vector3 = tr["normal"]
	var hit := int(tr["entityNum"])
	if _ents.get(hit, {}).get("kind", -1) == E.ET_PLAYER:
		_bleed(at, hit)   # CG_MissileHitPlayer(WP_SHOTGUN): blood only
	else:
		missile_hit_wall(E.WP_SHOTGUN, 0, at, Vector3(n.y, n.z, n.x))


## Every frame after the camera moves: the weapons' flashes and barrels, the lightning beams,
## the weapon loops, and the smoke.
func present(local_ps: Dictionary, eye_pos: Vector3) -> void:
	_local_ps = local_ps
	eye = eye_pos
	if self_body:
		_update_self(local_ps)
	for num in _ents:
		var e: Dictionary = _ents[num]
		if e["kind"] != E.ET_PLAYER or e.get("ragdolled", false):
			continue
		var es: Dictionary = e["es"]
		var firing := (int(es["eFlags"]) & E.EF_FIRING) != 0 and not (int(es["eFlags"]) & E.EF_DEAD)
		var fx: Q3WeaponFx = e.get("wfx")
		if fx and fx.valid():
			fx.update(_now, firing)
			fx.rail_charge(1.0, _now)
		e["lightning"] = _weapon_loop(num, int(es["weapon"]), firing, e["node"], false)
		var start := _muzzle_point(num)
		_lightning(num, int(es["weapon"]) == E.WP_LIGHTNING and firing and fx != null and fx.valid(),
			fx.muzzle() if fx and fx.valid() else Vector3.ZERO, start, forward_of(es["apos"]["trBase"]))
	if not local_ps.is_empty() and local_client >= 0:
		var alive := int(local_ps["pm_type"]) != E.PM_DEAD and int(local_ps["pm_type"]) != E.PM_INTERMISSION \
			and int(local_ps["pm_type"]) != E.PM_SPECTATOR
		var firing := alive and (int(local_ps["eFlags"]) & E.EF_FIRING) != 0
		if view_fx and view_fx.valid():
			view_fx.update(_now, firing)
			# the railgun recharges through its reload (weaponstate WEAPON_FIRING, weaponTime 1500 down to 0)
			var reload := int(local_ps["weaponstate"]) == 3 and int(local_ps["weapon"]) == E.WP_RAILGUN
			view_fx.rail_charge(1.0 - float(local_ps["weaponTime"]) / 1500.0 if reload else 1.0, _now)
		_local_lightning = _weapon_loop(local_client, int(local_ps["weapon"]) if alive else 0, firing, null, true)
		_lightning(local_client, alive and firing and int(local_ps["weapon"]) == E.WP_LIGHTNING and view_fx != null and view_fx.valid(),
			view_fx.muzzle() if view_fx and view_fx.valid() else Vector3.ZERO, _muzzle_point(local_client),
			forward_of(local_ps["viewangles"]))
	_smoke.step(_now, eye)
	_shotgun_smoke.step(_now, eye)


## The local player's own body, for mirrors and portal views only: cgame draws the predicted
## player with RF_THIRD_PERSON ("don't draw through eyes, only mirrors"). Built from the
## playerState the way BG_PlayerStateToEntityState makes the entity others see.
func _update_self(ps: Dictionary) -> void:
	if ps.is_empty() or local_client < 0:
		return
	if _self.is_empty():
		var body := Q3Character.new()
		body.model = _model_for(local_client)
		add_child(body)
		_self = {"node": body, "kind": E.ET_PLAYER, "legs": -1, "torso": -1, "weapon": -1}
	var ch: Q3Character = _self["node"]
	var pm := int(ps["pm_type"])
	ch.visible = pm == E.PM_NORMAL or pm == E.PM_FREEZE or pm == E.PM_NOCLIP
	if not ch.visible:
		return
	var weapon: int = _self["weapon"]
	_update_player(_self, {"number": local_client, "clientNum": local_client, "eType": E.ET_PLAYER,
		"eFlags": int(ps["eFlags"]) & ~E.EF_DEAD, "event": 0, "weapon": int(ps["weapon"]),
		"legsAnim": int(ps["legsAnim"]), "torsoAnim": int(ps["torsoAnim"]),
		"pos": {"trType": 0, "trBase": ps["origin"], "trDelta": ps["velocity"]},
		"apos": {"trType": 0, "trBase": ps["viewangles"]}})
	if weapon != _self["weapon"] or not _self.has("layered"):
		_self["layered"] = true
		for g in ch.find_children("*", "GeometryInstance3D", true, false):
			(g as GeometryInstance3D).layers = Q3Portal.MIRROR_LAYER
			(g as GeometryInstance3D).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF


## CG_LightningBolt: from the eye 14 units forward, LIGHTNING_RANGE along the aim; the beam is
## drawn from the weapon's flash to where that trace stops.
func _lightning(num: int, on: bool, flash: Vector3, start: Vector3, fwd: Vector3) -> void:
	var beam: Q3LightningBeam = _beams.get(num)
	if not on or start == Vector3.INF:
		if beam:
			beam.visible = false
		return
	if beam == null:
		beam = Q3LightningBeam.new()
		add_child(beam)
		_beams[num] = beam
	beam.visible = true
	var tr := Q3Server.trace(start, start + fwd * LIGHTNING_RANGE, Vector3.ZERO, Vector3.ZERO, num, MASK_SHOT)
	if tr.is_empty():
		beam.visible = false
		return
	beam.aim(flash, Q3.q3_to_godot(tr["endpos"]), float(tr["fraction"]) < 1.0)


## CG_AddPlayerWeapon's loops: the firing sound while fire is held, else the ready hum. Returns
## whether the firing loop plays (cent->pe.lightningFiring).
func _weapon_loop(num: int, weapon: int, firing: bool, owner: Node3D, flat: bool) -> bool:
	var path := ""
	var firing_loop := false
	if firing and FIRING_SOUNDS.has(weapon):
		path = FIRING_SOUNDS[weapon]
		firing_loop = true
	elif READY_SOUNDS.has(weapon):
		path = READY_SOUNDS[weapon]
	var cur: Dictionary = _loops.get(num, {})
	if cur.get("path", "") == path and (path == "" or is_instance_valid(cur.get("player"))):
		return firing_loop
	_stop_loop(num)
	if path == "":
		return firing_loop
	var res := "res://library/sounds/weapons/%s.wav" % path
	if not ResourceLoader.exists(res):
		return firing_loop
	var player: Node
	if flat:
		var a := AudioStreamPlayer.new()
		a.stream = Q3FX.looped(load(res))
		a.volume_db = -4.0
		player = a
	else:
		var a := AudioStreamPlayer3D.new()
		a.stream = Q3FX.looped(load(res))
		a.unit_size = 10.0
		a.max_distance = 40.0
		player = a
	player.set("bus", "SFX" if AudioServer.get_bus_index("SFX") >= 0 else "Master")
	if owner and is_instance_valid(owner):
		owner.add_child(player)
	else:
		add_child(player)
	player.call("play")
	_loops[num] = {"path": path, "player": player}
	return firing_loop


func _stop_loop(num: int) -> void:
	var cur: Dictionary = _loops.get(num, {})
	if cur.has("player") and is_instance_valid(cur["player"]):
		(cur["player"] as Node).queue_free()
	_loops.erase(num)


## The rail colors from a player's userinfo (c1, c2), CG_ColorFromString: 1 blue, 2 green, 4 red.
func client_colors(cl: int) -> Array:
	var info := Q3Host.parse_info(Q3Server.configstring(E.CS_PLAYERS + cl))
	return [_color_from(String(info.get("c1", "4"))), _color_from(String(info.get("c2", "5")))]


static func _color_from(v: String) -> Color:
	var n := v.to_int()
	if n < 1 or n > 7:
		return Color.WHITE
	return Color(1.0 if n & 4 else 0.0, 1.0 if n & 2 else 0.0, 1.0 if n & 1 else 0.0)


## AngleVectors' forward for id angles (pitch, yaw, roll), id axes.
static func forward_of(a: Vector3) -> Vector3:
	var p := deg_to_rad(a.x)
	var y := deg_to_rad(a.y)
	return Vector3(cos(p) * cos(y), cos(p) * sin(y), -sin(p))


## q_math.c PerpendicularVector: the smallest axis projected off `src`, normalized.
static func perpendicular(src: Vector3) -> Vector3:
	var pos := 0
	var minelem := 1.0
	for i in 3:
		if absf(src[i]) < minelem:
			pos = i
			minelem = absf(src[i])
	var t := Vector3.ZERO
	t[pos] = 1.0
	var inv := 1.0 / src.dot(src)
	return (t - src * (src.dot(t) * inv)).normalized()


## Q_crandom on an int seed (Q_rand: seed = 69069 * seed + 1), -1..1. seed is a one-int Array.
static func q_crandom(seed: Array) -> float:
	seed[0] = (69069 * int(seed[0]) + 1) & 0xFFFFFFFF
	return 2.0 * (float(int(seed[0]) & 0xffff) / 65536.0 - 0.5)


# ---------------------------------------------------------------- hit feedback

var _numbers: Dictionary = {}   ## target entity -> its Q3DamageNumber while the combo is open
const ARMOR_SHELL := preload("res://shaders/armor_shell.gdshader")
const ARMOR_COLOR := Color(1.0, 0.78, 0.28)
const ARMOR_LOW_COLOR := Color(1.0, 0.12, 0.08)
const ARMOR_LOW := 50


## "dmg <target> <amount> <armor>": a hit you landed. Numbers on the same target add up.
func damage(target: int, amount: int) -> void:
	if not Settings.get_value("hud/damage_numbers"):
		return
	var head := _head_of(target)
	if head == Vector3.INF:
		return
	var prev: Variant = _numbers.get(target)   # a finished number has freed itself
	var n: Q3DamageNumber = prev if is_instance_valid(prev) and (prev as Q3DamageNumber).combo_open() else null
	if n == null:
		n = Q3DamageNumber.new()
		add_child(n)
		_numbers[target] = n
	n.hit(amount, head)


## "armorhit <target> <armor left>": armor took part of a hit. The shell flashes in the armor
## color, red when armor is low, and not at all once it's gone.
func armor_hit(target: int, armor: int) -> void:
	if armor <= 0:
		return
	var e: Dictionary = _ents.get(target, {})
	var ch := e.get("node") as Q3Character
	if ch == null or not is_instance_valid(ch) or e.get("ragdolled", false):
		return
	var mat: ShaderMaterial = e.get("armor_mat")
	if mat == null:
		mat = ShaderMaterial.new()
		mat.shader = ARMOR_SHELL
		e["armor_mat"] = mat
		for mi in ch.find_children("*", "MeshInstance3D", true, false):
			if not String(mi.get_path()).contains("Attach_"):
				(mi as MeshInstance3D).material_overlay = mat
	mat.set_shader_parameter("color", ARMOR_LOW_COLOR if armor < ARMOR_LOW else ARMOR_COLOR)
	mat.set_shader_parameter("flash", 1.0)
	var tw := create_tween()
	tw.tween_method(func(v: float): mat.set_shader_parameter("flash", v), 1.0, 0.0, 0.4)


func _head_of(target: int) -> Vector3:
	var e: Dictionary = _ents.get(target, {})
	var node := e.get("node") as Node3D
	if node and is_instance_valid(node):
		return node.global_position + Vector3.UP * 2.0
	return Vector3.INF


# ---------------------------------------------------------------- sound

## Plays a Quake 3 sound path (sound/...wav) from library/sounds; positional unless `flat`.
func play_sound(path: String, at: Vector3, flat := false, volume_db := 0.0) -> void:
	var p := "res://library/sounds/" + path.trim_prefix("sound/")
	if not _sounds.has(p):
		_sounds[p] = load(p) if ResourceLoader.exists(p) else null
	var stream: AudioStream = _sounds[p]
	if stream == null:
		return
	var player: Node
	if flat:
		var a := AudioStreamPlayer.new()
		a.stream = stream
		a.volume_db = volume_db
		a.bus = "SFX" if AudioServer.get_bus_index("SFX") >= 0 else "Master"
		player = a
	else:
		var a := AudioStreamPlayer3D.new()
		a.stream = stream
		a.volume_db = volume_db
		a.unit_size = 12.0
		a.max_distance = 60.0
		a.bus = "SFX" if AudioServer.get_bus_index("SFX") >= 0 else "Master"
		player = a
	add_child(player)
	if player is Node3D:
		(player as Node3D).global_position = at
	player.finished.connect(player.queue_free)
	player.play()


# ---------------------------------------------------------------- math (bg_misc.c, q_math.c)

## BG_EvaluateTrajectory: a trajectory at a time (ms), id units.
static func trajectory(tr: Dictionary, at_ms: float) -> Vector3:
	var base: Vector3 = tr.get("trBase", Vector3.ZERO)
	var delta: Vector3 = tr.get("trDelta", Vector3.ZERO)
	var t0 := float(tr.get("trTime", 0))
	match int(tr.get("trType", 0)):
		2:   # TR_LINEAR
			return base + delta * ((at_ms - t0) * 0.001)
		3:   # TR_LINEAR_STOP
			var t := at_ms
			if t > t0 + float(tr["trDuration"]):
				t = t0 + float(tr["trDuration"])
			return base + delta * (maxf(t - t0, 0.0) * 0.001)
		4:   # TR_SINE
			var phase := sin((at_ms - t0) / maxf(float(tr["trDuration"]), 1.0) * TAU)
			return base + delta * phase
		5:   # TR_GRAVITY (DEFAULT_GRAVITY 800)
			var dt := (at_ms - t0) * 0.001
			var p := base + delta * dt
			p.z -= 0.5 * 800.0 * dt * dt
			return p
	return base   # TR_STATIONARY, TR_INTERPOLATE


## AnglesToAxis for id angles (pitch, yaw, roll) as a Godot basis.
static func angles_basis(a: Vector3) -> Basis:
	return Basis(Vector3.UP, deg_to_rad(a.y)) * Basis(Vector3.RIGHT, deg_to_rad(a.x)) * Basis(Vector3.BACK, deg_to_rad(a.z))


## ByteToDir: an event's direction byte as a Godot normal.
static func byte_dir(b: int) -> Vector3:
	if b < 0 or b >= Q3Enums.BYTEDIRS.size():
		return Vector3.UP
	var d: Vector3 = Q3Enums.BYTEDIRS[b]
	return Vector3(d.y, d.z, d.x)
