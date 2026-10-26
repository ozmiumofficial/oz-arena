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
## every EV_OBITUARY, also while a replay rebuilds quietly (the death cam's card needs the last one)
signal death_noted(attacker: int, target: int, means: int)
signal server_print(text: String)
signal landed(change: float)   ## the local player's landing dip (cg.landChange): -8, -16 or -24 units
signal stepped(change: float)  ## the local player stepped up 4 to 16 units (EV_STEP_n): the view eases up after it
signal team_sound(path: String)   ## an EV_GLOBAL_TEAM_SOUND announcer line (CG_AddBufferedSound)
signal local_event(ev: int, parm: int)   ## every predictable event of the local player (ps.events)

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
## Graphics > Missile trails > Enhanced (the remaster's, after WolfcamQL's trail options): glowing
## puffs behind each missile: step (ms), radius (units), life (ms), color
const GLOW_TRAILS := {E.WP_ROCKET_LAUNCHER: [10.0, 9.0, 170.0, Color(1.0, 0.55, 0.2)],
	E.WP_GRENADE_LAUNCHER: [16.0, 5.0, 220.0, Color(1.0, 0.4, 0.12)],
	E.WP_PLASMAGUN: [6.0, 6.0, 160.0, Color(0.35, 0.5, 1.0)],
	E.WP_BFG: [10.0, 16.0, 320.0, Color(0.3, 1.0, 0.3)]}
const MASK_SHOT := 0x6000001          ## CONTENTS_SOLID | CONTENTS_BODY | CONTENTS_CORPSE
const MASK_WATER := 32 | 16 | 8       ## CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA
const CONTENTS_WATER := 32
const CONTENTS_SOLID := 1
## the remaster's impact sparks (Q3Sparks) per weapon hit: count, speed (units/s), life (s), spread
const SPARKS := {E.WP_MACHINEGUN: [5, 340.0, 0.45, 0.8], E.WP_SHOTGUN: [2, 300.0, 0.4, 0.8],
	E.WP_RAILGUN: [10, 420.0, 0.6, 0.9], E.WP_ROCKET_LAUNCHER: [16, 520.0, 0.9, 1.0],
	E.WP_GRENADE_LAUNCHER: [16, 520.0, 0.9, 1.0], E.WP_LIGHTNING: [1, 260.0, 0.3, 0.9]}
const SURF_NOIMPACT := 0x10
const SURF_NOMARKS := 0x20
const ENTITYNUM_WORLD := 1022
const LIGHTNING_RANGE := 768.0
const TRACER_CHANCE := 0.4            ## cg_tracerChance

var map: Q3Map                     ## the map scene (movers live in it)
var feed: Q3Feed = Q3LiveFeed.shared()   ## the match: live, or a replay (Q3ReplayFeed)
var local_client := -1
var _ents: Dictionary = {}         ## entity number -> {"node": Node3D, "es": Dictionary, "kind": int}
var _seen_events: Dictionary = {}  ## entity number -> last event value handled
var _last_seen: Dictionary = {}    ## entity number -> server time of the last snapshot it was in (cent->snapShotTime)
const EVENT_VALID_MSEC := 300
## the player whose team EV_GLOBAL_TEAM_SOUND speaks for: the replay's player in every camera
## (cgs.clientinfo[cg.clientNum]); -1 is local_client
var team_client := -1
var _ps_event_seq := -1
var _item_names: PackedStringArray = []
var _pain_time: Dictionary = {}    ## client -> last pain sound (cent->pe.painTime), ms
var _pain_dir: Dictionary = {}     ## client -> which way the pain twitch rolls (cent->pe.painDirection)
var _sounds: Dictionary = {}       ## path -> AudioStream
var _time := 0                     ## server time of the snapshot being shown (ms)
var _corpses: Array[Node3D] = []   ## ragdolls left behind when their players respawn
var view_fx: Q3WeaponFx            ## the first-person weapon's flash and barrel (Gameplay sets it)
var eye := Vector3.ZERO            ## the camera, for effects that face it
var _now := 0.0                    ## render time (ms), cg.time
var _anim_time := -1.0             ## the match time the bodies have animated to (ms; -1: not yet)
var _anim_step := 0.0              ## how far they animate this frame (ms)
var _local_ps: Dictionary = {}
var _preload_t := -1000000         ## server time of the last look through the players for models to preload
var _warmed: Dictionary = {}       ## "model/skin/bright color" -> true: a body built once (_preload_models)
var _smoke: Q3Puffs                ## smokePuff: rocket and grenade trails
var _shotgun_smoke: Q3Puffs
var _bubbles: Q3Puffs              ## CG_BubbleTrail: rockets, grenades, bullets and pellets under water
var _glow_trail: Q3Puffs           ## the remaster's glowing missile trails (GLOW_TRAILS)
var _sparks: Q3Sparks
var _burns: Q3Burns                ## the lightning gun's burn lines
var _beams: Dictionary = {}        ## entity -> Q3LightningBeam while the lightning gun fires
var _loops: Dictionary = {}        ## entity -> its weapon loop (hum while out, buzz while firing)
var self_body := false            ## draw the local player for mirrors (the map has portal views)
## draw the local player's body for the main camera too: a spectator's chase camera on the followed
## player, whose entity the server never sends (cgame builds it from the playerState)
var self_visible := false
var _self: Dictionary = {}         ## that body's entity record
var pickup_effects := 0           ## pickup effects played (tests)
var _pickups: Array = []           ## recent pickup events: [position, time ms] (the pickup effect)
var _ext_event := 0                ## the last ps.externalEvent played
const MAX_PREDICTED_EVENTS := 16
var _predictable: PackedInt32Array = PackedInt32Array()   ## cg.predictableEvents: what each slot played
var _local_lightning := false      ## cent->pe.lightningFiring for the local player
var _next: Dictionary = {}         ## replays: entity number -> its state in the next frame (cg.nextSnap)
var _frac := 0.0                   ## how far between this frame and the next (cg.frameInterpolation)
var muted := false                 ## a replay rebuilding after a jump: events are noted, not played
var _helpers: Array = []           ## the puff, spark and burn renderers made in _ready
## The death cam (REPLAY_PLAN.md R6): your own body goes limp where you die. Quake 3 never drew it
## (you were the camera, and the server never sends your own entity), so it's built from the
## playerState the moment you die, carries your speed, and stays as a corpse when you respawn.
var own_death := false
const PLAYER_FILL := 1.2  ## energy of each of the two player fill lights
var own_body: Q3Character
var _own_seen_alive := false       ## a body is only built for a death seen happening, not one already over
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
	_sparks = Q3Sparks.create(self)
	_burns = Q3Burns.create(self)
	_bubbles = Q3Puffs.create(self, "bubble")
	_glow_trail = Q3Puffs.create(self, "flare", true)
	Settings.changed.connect(_on_setting)
	_add_fill_lights()
	_helpers = get_children()   # what reset() keeps
	Q3FX.warm_up(self)
	load_footsteps()


## Players never go dark (remaster only): two soft lights from opposite sides, unshadowed, that
## light only Q3Character.FILL_LAYER, so no player drops below a floor of brightness in an unlit
## corridor. They stay out of the bake and the volumetric fog, and the map's own lighting still
## decides everything above the floor.
func _add_fill_lights() -> void:
	for yaw in [35.0, 215.0]:
		var l := DirectionalLight3D.new()
		l.name = "PlayerFill"
		l.light_cull_mask = Q3Character.FILL_LAYER
		l.light_energy = PLAYER_FILL
		l.light_color = Color(0.9, 0.93, 1.0)
		l.light_bake_mode = Light3D.BAKE_DISABLED
		l.light_volumetric_fog_energy = 0.0
		l.shadow_enabled = false
		l.rotation_degrees = Vector3(-40.0, yaw, 0.0)
		add_child(l)


## While a match draws, the models it loads stay loaded: a respawn or an explosion doesn't read its
## model again.
func _enter_tree() -> void:
	Q3.hold_models(true)


func _exit_tree() -> void:
	Q3.hold_models(false)


## One snapshot (Q3Server.snapshot()) plus the live local playerState.
func apply_snapshot(snap: Dictionary, local_ps: Dictionary) -> void:
	if snap.is_empty():
		return
	_time = int(snap["serverTime"])
	_local_ps = local_ps   # cg.snap->ps: the events below see this snapshot's playerState, not last frame's
	if absi(_time - _preload_t) >= 500:
		_preload_t = _time
		_preload_models()
	var present := {}
	for es in snap["entities"]:
		var num := int(es["number"])
		present[num] = true
		# CG_ResetEntity: an entity back within EVENT_VALID_MSEC still carries the event already played
		if _time - int(_last_seen.get(num, -1000000)) > EVENT_VALID_MSEC:
			_seen_events.erase(num)
		_last_seen[num] = _time
		if int(es["eType"]) >= E.ET_EVENTS:
			_temp_event(num, es)
			continue
		_update_entity(num, es)
		_entity_event(num, es)
	_local_events(local_ps)   # before removals: the item the player just took leaves in this snapshot
	for num in _ents.keys():
		if not present.has(num):
			_remove(num)
	for num in _last_seen.keys():
		if not present.has(num) and _time - int(_last_seen[num]) > EVENT_VALID_MSEC:
			_last_seen.erase(num)
			_seen_events.erase(num)


## Replays: the next recorded frame and how far the playhead is toward it. Players are drawn
## between the two (cg_ents.c CG_InterpolateEntityPosition); without a next frame they stand
## where the frame puts them.
func set_next(next_snap: Dictionary, frac: float) -> void:
	_next.clear()
	_frac = frac
	for es in next_snap.get("entities", []):
		_next[int(es["number"])] = es


## Every player drawn and standing (not a ragdoll): client number -> their Q3Character. The replay
## viewer's name tags and outlines, the spectator HUD and the death cam read it.
func player_nodes() -> Dictionary:
	var out := {}
	for num in _ents:
		var e: Dictionary = _ents[num]
		var ch := e.get("node") as Q3Character
		if e["kind"] == E.ET_PLAYER and ch and is_instance_valid(ch) and not e.get("ragdolled", false) and num < E.MAX_CLIENTS:
			out[num] = ch
	return out


## The ragdoll of a fragged player still lying where they fell (the death cam follows it), or null.
func body_of(cl: int) -> Q3Character:
	var e: Dictionary = _ents.get(cl, {})
	var ch := e.get("node") as Q3Character
	if e.get("ragdolled", false) and ch and is_instance_valid(ch) and ch.visible:
		return ch
	return null


## A spectator follows someone else: their body goes (their eyes are the camera now), the one left
## comes back from the next snapshot, and their playerState's events start over. Everything else
## (corpses, marks, effects) stays, unlike reset().
func set_view_client(cl: int) -> void:
	var old := local_client
	local_client = cl
	# the one left behind: their ragdoll, if they're down, becomes their entity's body again
	var left: Q3Character = own_body if own_body and is_instance_valid(own_body) else null
	own_body = null
	_own_seen_alive = false
	var taken: Node3D = null
	for n in [old, cl]:
		var e: Dictionary = _ents.get(n, {})
		if _beams.has(n):
			(_beams[n] as Node).queue_free()
			_beams.erase(n)
		_burns.lift(n)
		_stop_loop(n)
		_seen_events.erase(n)
		_last_seen.erase(n)
		if e.is_empty():
			continue
		_free_effects(e)
		var node: Node3D = e["node"]
		_ents.erase(n)
		if n == cl and e.get("ragdolled", false) and node is Q3Character and is_instance_valid(node):
			taken = node   # the new one is down: their ragdoll is the death cam's body
		elif e.get("ragdolled", false):
			_leave_corpse(node)
		elif node and is_instance_valid(node):
			node.queue_free()
	if left and old >= 0 and old < E.MAX_CLIENTS:
		_ents[old] = {"node": left, "es": {}, "kind": E.ET_PLAYER, "legs": -1, "torso": -1, "weapon": -1, "ragdolled": true}
	elif left:
		_leave_corpse(left)
	if taken:
		own_body = taken as Q3Character
	_ps_event_seq = -1
	_ext_event = 0
	_predictable.fill(0)
	_last_health_client = -1
	_clear_local_holder()
	if not _self.is_empty() and is_instance_valid(_self["node"]):
		(_self["node"] as Node).queue_free()
	_self = {}


## A replay jumped (or changed whose eyes it shows): everything drawn goes, to be rebuilt from the
## next snapshot with `muted` set. Corpses from before the jump aren't recreated.
func reset() -> void:
	for num in _ents.keys():
		var e: Dictionary = _ents[num]
		var node: Node3D = e["node"]
		if e["kind"] != E.ET_MOVER and node and is_instance_valid(node):
			node.queue_free()
		_free_effects(e)
	_ents.clear()
	_last_seen.clear()
	_last_health_client = -1
	_clear_local_holder()
	for num in _loops.keys():
		_stop_loop(num)
	for b in _beams.values():
		(b as Node).queue_free()
	_beams.clear()
	for c in _corpses:
		if is_instance_valid(c):
			c.queue_free()
	_corpses.clear()
	_seen_events.clear()
	_ps_event_seq = -1
	_ext_event = 0
	_predictable.fill(0)
	_pickups.clear()
	_numbers.clear()
	_haste_time.clear()
	_pain_time.clear()
	_next.clear()
	_anim_time = -1.0
	if own_body and is_instance_valid(own_body):
		own_body.queue_free()
	own_body = null
	_own_seen_alive = false
	_smoke.clear()
	_shotgun_smoke.clear()
	_bubbles.clear()
	_glow_trail.clear()
	_sparks.clear()
	_burns.clear()
	# the mirror body is rebuilt for whoever is watched now
	if not _self.is_empty() and is_instance_valid(_self["node"]):
		(_self["node"] as Node).queue_free()
	_self = {}
	# effects and sounds still playing from before the jump (the helpers made in _ready stay)
	for child in get_children():
		if not child in _helpers:
			child.queue_free()


## cg_servercmds.c CG_MapRestart (CG_InitLocalEntities, CG_InitMarkPolys, CG_ClearParticles): the
## restarted match starts in a clean arena, without the warmup's marks, gibs, brass, smoke or
## corpses. The entities stay; the next snapshot updates them.
func clear_local() -> void:
	for c in _corpses:
		if is_instance_valid(c):
			c.queue_free()
	_corpses.clear()
	if own_body and is_instance_valid(own_body):
		own_body.queue_free()
	own_body = null
	_own_seen_alive = false
	for num in _ents.keys():
		var e: Dictionary = _ents[num]
		if e.get("ragdolled", false):
			_stop_loop(num)
			_free_effects(e)
			if is_instance_valid(e["node"]):
				(e["node"] as Node).queue_free()
			_ents.erase(num)
	_smoke.clear()
	_shotgun_smoke.clear()
	_bubbles.clear()
	_glow_trail.clear()
	_sparks.clear()
	_burns.clear()
	for child in get_children():
		if child is Q3FxTimed or child is RigidBody3D:   # explosions, marks, blood, rails; gibs, brass
			child.queue_free()
	_pickups.clear()
	Q3FX.clear_marks()


## Every frame: movers and missiles follow their trajectories at the current time; in a replay,
## players move between recorded frames. Players animate on the match's clock: as far as it moved
## since last frame (it steps back a hair between server frames; that doesn't count).
func animate(server_time_ms: float) -> void:
	_now = server_time_ms
	_anim_step = 0.0
	if _anim_time < 0.0 or server_time_ms < _anim_time - 1000.0:
		_anim_time = server_time_ms
	elif server_time_ms > _anim_time:
		_anim_step = server_time_ms - _anim_time
		_anim_time = server_time_ms
	for num in _ents:
		var e: Dictionary = _ents[num]
		var kind: int = e["kind"]
		if kind == E.ET_PLAYER:
			if _next.has(num):
				_interpolate_player(e, _next[num])
			_advance_body(e)
		elif kind == E.ET_ITEM and int(e["es"]["pos"].get("trType", 0)) != 0:
			var item: Node3D = e["node"]
			if item and is_instance_valid(item):
				item.global_position = Q3.q3_to_godot(trajectory(e["es"]["pos"], server_time_ms))
		if kind == E.ET_MOVER:
			# CG_Mover: the inline model, and its model2 (a map object riding it), at lerpOrigin/lerpAngles
			var p := Q3.q3_to_godot(trajectory(e["es"]["pos"], server_time_ms))
			var b := angles_basis(trajectory(e["es"]["apos"], server_time_ms))
			for n in [e["node"], e.get("model2")]:
				if n and is_instance_valid(n):
					(n as Node3D).global_position = p
					(n as Node3D).basis = b
		elif kind == E.ET_MISSILE:
			var node: Node3D = e["node"]
			if node and is_instance_valid(node):
				node.global_position = Q3.q3_to_godot(trajectory(e["es"]["pos"], server_time_ms))
				_missile_frame(e, node)


## CG_InterpolateEntityPosition for a player: between this frame's origin and angles and the next
## frame's, unless they teleported (EF_TELEPORT_BIT toggled) or died in between.
func _interpolate_player(e: Dictionary, nx: Dictionary) -> void:
	var ch := e["node"] as Q3Character
	if ch == null or not is_instance_valid(ch) or e.get("ragdolled", false):
		return
	var es: Dictionary = e["es"]
	if (int(es["eFlags"]) ^ int(nx["eFlags"])) & 4 or int(nx["eFlags"]) & E.EF_DEAD:
		return
	var a: Vector3 = es["pos"]["trBase"]
	ch.global_position = Q3.q3_to_godot(a.lerp(nx["pos"]["trBase"], _frac)) + Vector3.DOWN * 24.0 * Q3.UNIT
	var aa: Vector3 = es["apos"]["trBase"]
	var ab: Vector3 = nx["apos"]["trBase"]
	ch.aim_yaw = Q3ReplayFeed.lerp_angle_deg(aa.y, ab.y, _frac)
	ch.aim_pitch = Q3ReplayFeed.lerp_angle_deg(aa.x, ab.x, _frac)


## A standing player's body steps on by the match clock's move this frame.
func _advance_body(e: Dictionary) -> void:
	var ch := e.get("node") as Q3Character
	if ch and is_instance_valid(ch) and ch.match_clock and not e.get("ragdolled", false):
		ch.advance(_anim_step)


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
	var weapon := int(es["weapon"])
	var trail: Array = TRAILS.get(weapon, [])
	var glow: Array = GLOW_TRAILS.get(weapon, []) if Settings.get_value("graphics/missile_trails") == "enhanced" else []
	if trail.is_empty() and glow.is_empty():
		return
	# CG_RocketTrail: a puff at every 50 ms step of the trajectory since the last frame
	var step := 50.0
	var start: float = e.get("trail_time", float(_time))
	e["trail_time"] = _now
	if not moving:
		return
	var here := trajectory(tr, _now)
	var contents := feed.point_contents(here, -1)
	if contents & MASK_WATER:
		# under water: CG_RocketTrail's bubbles instead of smoke, while it was in water last frame too
		if not trail.is_empty() and contents & feed.point_contents(trajectory(tr, start), -1) & CONTENTS_WATER:
			_bubble_trail(trajectory(tr, start), here, 8.0)
		return
	if not trail.is_empty():
		var t := step * floorf((start + step) / step)
		while t <= _now:
			_smoke.add(Q3.q3_to_godot(trajectory(tr, t)), Vector3.ZERO, trail[0], 0.33, trail[1], t)
			t += step
	if not glow.is_empty():
		var gs: float = glow[0]
		var t := gs * floorf((start + gs) / gs)
		while t <= _now:
			_glow_trail.add(Q3.q3_to_godot(trajectory(tr, t)), Vector3.ZERO, glow[1], 1.0, glow[2], t, false, glow[3])
			t += gs


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
	_entity_effects(e, es)
	if kind == E.ET_MOVER and int(es.get("modelindex2", 0)) != int(e.get("model2_index", 0)):
		_mover_model2(e, int(es.get("modelindex2", 0)))
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
			var ch := _body_for(cl)
			ch.turn = true
			ch.match_clock = true
			add_child(ch)
			_bright(ch, cl)
			return ch
		E.ET_ITEM:
			var idx := int(es["modelindex"])
			if idx < 1 or idx > _item_names.size():
				return null
			var p := Q3Pickup.new()
			p.classname = _item_names[idx - 1]
			p.number = num   # its bob rate (cg_ents.c CG_Item)
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


## cg_ents.c CG_EntityEffects and CG_Speaker: an entity's loopSound plays where it is (the map's
## looped target_speakers: wind, machinery, fire), its constantLight glows, and a speaker that
## triggers itself (wait and random keys) plays its sound every wait +- random seconds.
func _entity_effects(e: Dictionary, es: Dictionary) -> void:
	var at := Q3.q3_to_godot(trajectory(es["pos"], _time))
	var loop := int(es.get("loopSound", 0))
	if int(e.get("ambient_sound", 0)) != loop:
		e["ambient_sound"] = loop
		if is_instance_valid(e.get("ambient")):
			(e["ambient"] as Node).queue_free()
		e.erase("ambient")
		var stream := _load_sound(feed.configstring(E.CS_SOUNDS + loop)) if loop != 0 else null
		if stream:
			var a := AudioStreamPlayer3D.new()
			a.stream = Q3FX.looped(stream)
			a.unit_size = 12.0
			a.max_distance = 60.0
			a.bus = &"Effects"
			Settings.spatialize(a)
			add_child(a)
			a.play()
			e["ambient"] = a
	if is_instance_valid(e.get("ambient")):
		(e["ambient"] as Node3D).global_position = at
	var cl := int(es.get("constantLight", 0))
	if int(e.get("glow_value", 0)) != cl:
		e["glow_value"] = cl
		if is_instance_valid(e.get("glow")):
			(e["glow"] as Node).queue_free()
		e.erase("glow")
		if cl != 0:
			var l := OmniLight3D.new()
			l.light_color = Color8(cl & 255, (cl >> 8) & 255, (cl >> 16) & 255)
			l.omni_range = float(((cl >> 24) & 255) * 4) * Q3.UNIT
			l.light_energy = 2.0
			l.shadow_enabled = false
			add_child(l)
			e["glow"] = l
	if is_instance_valid(e.get("glow")):
		(e["glow"] as Node3D).global_position = at
	if int(es["eType"]) == E.ET_SPEAKER and int(es.get("clientNum", 0)) != 0 and not muted:
		if float(_time) >= float(e.get("speaker_time", 0.0)):
			play_sound(feed.configstring(E.CS_SOUNDS + int(es["eventParm"])), at)
			e["speaker_time"] = float(_time) + int(es.get("frame", 0)) * 100.0 + int(es["clientNum"]) * 100.0 * randf_range(-1.0, 1.0)


## cg_ents.c CG_Mover's second model: a map object riding the mover (q3dm0's hologram and x-ray
## skeleton, q3dm19's jets, q3tourney6's pipes), from CS_MODELS. It belongs to the client, not the
## map, and animate() moves it with the mover.
func _mover_model2(e: Dictionary, index: int) -> void:
	if is_instance_valid(e.get("model2")):
		(e["model2"] as Node).queue_free()
	e.erase("model2")
	e["model2_index"] = index
	if index == 0:
		return
	var path := feed.configstring(E.CS_MODELS + index)   # "models/mapobjects/bitch/fembotbig.md3"
	var n := Q3.instance_prop(path.trim_prefix("models/").get_basename().to_lower()) if path.begins_with("models/") else null
	if n == null:
		return
	add_child(n)
	e["model2"] = n
	n.global_position = Q3.q3_to_godot(trajectory(e["es"]["pos"], _time))
	n.basis = angles_basis(trajectory(e["es"]["apos"], _time))


## What an entity added next to its own node: its loop sound, its light, a mover's second model.
func _free_effects(e: Dictionary) -> void:
	for k in ["ambient", "glow", "model2"]:
		if is_instance_valid(e.get(k)):
			(e[k] as Node).queue_free()
		e.erase(k)
	e.erase("ambient_sound")
	e.erase("glow_value")
	e.erase("model2_index")


func _remove(num: int) -> void:
	var e: Dictionary = _ents.get(num, {})
	_ents.erase(num)
	_stop_loop(num)
	_free_effects(e)
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
	# a weak reference: a lambda that captured a node freed meanwhile (a replay's jump clears the
	# corpses) makes Godot print "Lambda capture ... was freed" even though it checks
	var ref: WeakRef = weakref(ch)
	get_tree().create_timer(CORPSE_TIME).timeout.connect(func():
		var body := ref.get_ref() as Node3D
		if body == null:
			return   # a replay's jump already cleared it
		_corpses.erase(body)
		if body.visible and body is Q3Character:
			(body as Q3Character).disintegrate(CORPSE_BURN)
		else:
			body.queue_free())


## The body a gib (EV_GIB_PLAYER) is for: the nearest ragdoll still owned by its player, or corpse.
func _body_near(at: Vector3) -> Node3D:
	var best: Node3D = null
	var best_d := 2.5
	for num in _ents:
		var e: Dictionary = _ents[num]
		if e.get("ragdolled", false) and e["node"] and is_instance_valid(e["node"]):
			var d: float = (e["node"] as Node3D).global_position.distance_to(at)
			if d < best_d:
				best_d = d
				best = e["node"]
	var bodies: Array[Node3D] = _corpses.duplicate()
	if own_body:
		bodies.append(own_body)
	for c in bodies:
		if is_instance_valid(c) and c.global_position.distance_to(at) < best_d:
			best_d = c.global_position.distance_to(at)
			best = c
	return best


## The model and skin a player is drawn with, picked as cg_players.c does (CG_NewClientInfo,
## CG_LoadClientInfo, CG_FindClientModelFile): CS_PLAYERS "model" is "model/skin". Gameplay > Force
## enemy model (cg_forceModel) gives enemies that model in its default skin. In team games the skin
## is the team's, "<skin>_red" or else "red" by "t" (blue for the blue team, red otherwise), and a
## model without one is Sarge in it (id's DEFAULT_TEAM_MODEL fallback: Brandon, Xian). Otherwise
## it's the player's skin, or the model's default when it has no such skin. Sounds stay the model's.
func _look(cl: int) -> PackedStringArray:
	var info := Q3Host.parse_info(feed.configstring(E.CS_PLAYERS + cl))
	var spec := String(info.get("model", "sarge")).to_lower()
	var m := spec.get_slice("/", 0)
	var s := spec.get_slice("/", 1) if spec.contains("/") else "default"
	var forced := String(Settings.get_value("gameplay/force_model"))
	if forced in Q3.characters and cl != local_client and not _teammate(cl):
		m = forced
		s = "default"
	if not m in Q3.characters:
		m = "sarge"
		s = "default"
	var skins := Q3Character.skins_of(m)
	if feed.cvar("g_gametype").to_int() >= E.GT_TEAM:
		var team := "blue" if String(info.get("t", "")).to_int() == E.TEAM_BLUE else "red"
		if s + "_" + team in skins:
			s += "_" + team
		elif team in skins:
			s = team
		else:
			m = "sarge"
			s = team
	elif not s in skins:
		s = "default"
	return PackedStringArray([m, s])


## CG_LoadClientInfo's early load: every connected player's model starts loading on a loader thread
## (twice a second), so the first sight of them doesn't read a glTF mid-fight. Once a model is
## in, a body is built out of sight in the player's skin and bright color and freed:
## its bright materials stay (Q3.character_material) with their shaders compiled, which the first
## body of a model otherwise did mid-fight (about 100 ms, 2026-10-25).
func _preload_models() -> void:
	Q3.poll_preloads()
	for cl in E.MAX_CLIENTS:
		if feed.configstring(E.CS_PLAYERS + cl) == "" or cl == local_client:
			continue
		var look := _look(cl)
		Q3.preload_character(look[0])
		var key := "%s/%s/%s" % [look[0], look[1], _bright_color(cl).to_html()]
		if _warmed.has(key) or not Q3.has_character(look[0]):
			continue
		_warmed[key] = true
		var ch := _body_for(cl)
		add_child(ch)
		ch.global_position = Vector3(0.0, -1000.0, 0.0)   # far below any map
		_bright(ch, cl)
		if ch.body:
			for mi in ch.body.find_children("*", "MeshInstance3D", true, false):
				(mi as MeshInstance3D).material_overlay = _armor_shell()
		ch.free()


## A new body for a player, in their model and skin (not yet in the tree).
func _body_for(cl: int) -> Q3Character:
	var look := _look(cl)
	var ch := Q3Character.new()
	ch.model = look[0]
	ch.skin = look[1]
	return ch


## CG_NewClientInfo: when a player's CS_PLAYERS string changes (a team switch, a new model) or the
## local player's does (which side counts as teammates), the body standing takes the new skin and
## bright color. Returns false when the model changed: the body has to be built again.
func _restyle(e: Dictionary, cl: int) -> bool:
	var cs := feed.configstring(E.CS_PLAYERS + cl)
	if local_client >= 0:
		cs += "\n" + feed.configstring(E.CS_PLAYERS + local_client)
	var seen: String = e.get("cs", cs)
	e["cs"] = cs
	var ch := e.get("node") as Q3Character
	if seen == cs or ch == null or not is_instance_valid(ch) or e.get("ragdolled", false):
		return true
	var look := _look(cl)
	if look[0] != ch.model:
		return false
	ch.set_skin(look[1])
	_bright(ch, cl)
	return true


## A standing player's body built again (a new model): the old one goes with its gun and shells.
func _new_body(num: int, e: Dictionary) -> Q3Character:
	var old := e.get("node") as Node
	if old and is_instance_valid(old):
		old.queue_free()
	var ch := _spawn(num, E.ET_PLAYER, e["es"]) as Q3Character
	e["node"] = ch
	for k in ["armor_mat", "wfx", "gun", "tele"]:
		e.erase(k)
	e["legs"] = -1
	e["torso"] = -1
	e["weapon"] = -1
	return ch


## Whether a player is on the local player's team (team modes only).
func _teammate(cl: int) -> bool:
	if local_client < 0 or feed.cvar("g_gametype").to_int() < E.GT_TEAM:
		return false
	var team := func(c: int) -> String: return String(Q3Host.parse_info(feed.configstring(E.CS_PLAYERS + c)).get("t", ""))
	return team.call(cl) == team.call(local_client)


## Gameplay > Bright skins: enemies in Enemy color, teammates in Team color.
func _bright(ch: Q3Character, cl: int) -> void:
	ch.bright_skin(_bright_color(cl))


## A player's bright skin color; alpha 0 when bright skins are off.
func _bright_color(cl: int) -> Color:
	if Settings.get_value("gameplay/bright_skins"):
		return Settings.get_value("gameplay/team_color" if _teammate(cl) else "gameplay/enemy_color")
	return Color(0, 0, 0, 0)


## Player look settings changed (from the pause menu): redo the bodies standing in the match.
func _on_setting(key: String, _v: Variant) -> void:
	if not key in ["gameplay/bright_skins", "gameplay/enemy_color", "gameplay/team_color", "gameplay/force_model"]:
		return
	for num in _ents:
		var e: Dictionary = _ents[num]
		var ch := e.get("node") as Q3Character
		if e["kind"] != E.ET_PLAYER or ch == null or not is_instance_valid(ch) or e.get("ragdolled", false):
			continue
		var cl := int(e["es"]["clientNum"])
		if key == "gameplay/force_model":
			_new_body(num, e)
		else:
			_bright(ch, cl)


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
		e.erase("tele")
		e["legs"] = -1
		e["torso"] = -1
		e["weapon"] = -1
	if ch == null:
		return
	if e.get("ragdolled", false):
		return   # physics owns it now
	if e != _self and not _restyle(e, int(es["clientNum"])):
		ch = _new_body(int(es["number"]), e)
		if ch == null:
			return
	if dead and muted:
		# a body from before a replay's jump: not recreated as a ragdoll (its hum, lights and flag go)
		_powerups(int(es["number"]), ch, ch.body, null, 0, 0)
		ch.visible = false
		e["ragdolled"] = true
		return
	if dead:
		# fragged: the powerups go (the game drops them), then the body goes limp with its speed
		_powerups(int(es["number"]), ch, ch.body, null, 0, 0)
		var vel := Q3.q3_to_godot(es["pos"]["trDelta"])
		ch.ragdoll((vel * 0.6).limit_length(8.0))   # a shotgun's knockback can't fire limbs through floors
		e["ragdolled"] = true
		_talk_balloon(ch, false, false)
		var old := ch.find_child("Attach_weapon", true, false)
		if old:
			old.queue_free()
		e.erase("wfx")
		return
	ch.global_position = Q3.q3_to_godot(trajectory(es["pos"], _time)) + Vector3.DOWN * 24.0 * Q3.UNIT   # origin is 24 above the feet
	_player_sprite(ch, int(es["eFlags"]), e == _self)
	# CG_PlayerAngles: the legs, torso and head turn toward the aim and the way they move, and the
	# legs lean into their speed
	var ang: Vector3 = es["apos"]["trBase"]
	ch.aim_yaw = ang.y
	ch.aim_pitch = ang.x
	ch.move_dir = int((es.get("angles2", Vector3.ZERO) as Vector3).y)
	ch.velocity = es["pos"].get("trDelta", Vector3.ZERO)
	var legs := int(es["legsAnim"]) & ~E.ANIM_TOGGLEBIT
	var torso := int(es["torsoAnim"]) & ~E.ANIM_TOGGLEBIT
	ch.aim_center = legs != E.LEGS_IDLE or torso != E.TORSO_STAND
	# CG_AddPainTwitch: each pain rolls the torso 20 degrees, alternating sides, easing back over 200 ms
	# (Gameplay > Hit twitch, off by default: it read as a distracting jerk)
	var since_pain := _now - float(_pain_time.get(int(es["clientNum"]), -100000.0))
	ch.aim_roll = 0.0 if since_pain < 0.0 or since_pain >= 200.0 or not Settings.get_value("gameplay/pain_twitch") \
		else (20.0 if _pain_dir.get(int(es["clientNum"]), false) else -20.0) * (1.0 - since_pain / 200.0)
	# the toggle bit restarts a clip that's played again
	if int(es["legsAnim"]) != e["legs"]:
		e["legs"] = int(es["legsAnim"])
		if legs < E.ANIM_NAMES.size():
			ch.play_legs(E.ANIM_NAMES[legs])
	if int(es["torsoAnim"]) != e["torso"]:
		e["torso"] = int(es["torsoAnim"])
		if torso < E.ANIM_NAMES.size() and legs > E.BOTH_DEAD3:
			ch.play_torso(E.ANIM_NAMES[torso])
	# CG_ResetPlayerEntity: just in view, or teleported (EF_TELEPORT_BIT toggled; respawns too)
	var tele := int(es["eFlags"]) & E.EF_TELEPORT_BIT
	if tele != int(e.get("tele", -1)):
		e["tele"] = tele
		ch.reset_motion()
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
				e["gun"] = prop   # its powerup shells (_powerups)
				if prop:
					e["wfx"] = Q3WeaponFx.bind(prop, w)
					(e["wfx"] as Q3WeaponFx).rail_color = client_colors(int(es["clientNum"]))[0]


## cg_players.c CG_PlayerSprites, in its order: the first flag set picks the sprite over the head.
## (EF_CONNECTION's icon and the team-mate marker have no art in the library.)
const PLAYER_SPRITES := [[E.EF_TALK, "fx/textures/talk_balloon.png"],
	[E.EF_AWARD_IMPRESSIVE, "ui/hud/medal_impressive.png"], [E.EF_AWARD_EXCELLENT, "ui/hud/medal_excellent.png"],
	[E.EF_AWARD_GAUNTLET, "ui/hud/medal_gauntlet.png"], [E.EF_AWARD_DEFEND, "ui/hud/medal_defend.png"],
	[E.EF_AWARD_ASSIST, "ui/hud/medal_assist.png"], [E.EF_AWARD_CAP, "ui/hud/medal_capture.png"]]


## The sprite a player's eFlags float over them: typing in the chat, or the medal of an award just
## earned (the game keeps EF_AWARD_* on for REWARD_SPRITE_TIME, 2 s).
func _player_sprite(ch: Q3Character, eflags: int, mine: bool) -> void:
	for s in PLAYER_SPRITES:
		if eflags & int(s[0]):
			_talk_balloon(ch, true, mine, s[1])
			return
	_talk_balloon(ch, false, mine)


## CG_PlayerFloatSprite: a sprite 48 units over the origin, radius 10 (the chat balloon unless
## `tex` says otherwise). Your own shows only in mirrors.
func _talk_balloon(ch: Q3Character, on: bool, mine: bool, tex := "fx/textures/talk_balloon.png") -> void:
	var b := ch.get_node_or_null("TalkBalloon") as Sprite3D
	if b == null:
		if not on:
			return
		b = Sprite3D.new()
		b.name = "TalkBalloon"
		b.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		b.shaded = false
		b.alpha_cut = SpriteBase3D.ALPHA_CUT_DISABLED
		b.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		b.position = Vector3.UP * (24.0 + 48.0) * Q3.UNIT
		if mine and not self_visible:
			b.layers = Q3Portal.MIRROR_LAYER
		ch.add_child(b)
	if on and String(b.get_meta("tex", "")) != tex:
		b.set_meta("tex", tex)
		b.texture = _sprite_texture(tex)
		if b.texture:
			b.pixel_size = 20.0 * Q3.UNIT / b.texture.get_width()
	b.visible = on


static var _sprite_textures: Dictionary = {}   ## path -> a mipmapped copy for 3D


## A head sprite's texture. The medals are the HUD's 2D imports, which have no mipmaps: drawn small
## and far off in 3D they'd shimmer, so they get a mipmapped copy, made once.
static func _sprite_texture(tex: String) -> Texture2D:
	var t := Q3.held(Q3.LIB + tex) as Texture2D
	if t == null or tex.begins_with("fx/"):
		return t
	if not _sprite_textures.has(tex):
		var img := t.get_image()
		if img and not img.has_mipmaps():
			if img.is_compressed():
				img.decompress()
			img.generate_mipmaps()
			_sprite_textures[tex] = ImageTexture.create_from_image(img)
		else:
			_sprite_textures[tex] = t
	return _sprite_textures[tex]


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
	_event(ev & ~E.EV_EVENT_BITS, es, _sound_origin(es))


## cg_ents.c CG_SetEntitySoundPosition: a brush model's sounds (doors, plats, buttons) come from its
## middle; its origin is often the map's (0, 0, 0) plus its travel. A mover with a loop sound keeps
## lerpOrigin, as the loop overwrites the sound origin in id's sound code.
func _sound_origin(es: Dictionary) -> Vector3:
	var at := Q3.q3_to_godot(trajectory(es["pos"], _time))
	if int(es.get("solid", 0)) == E.SOLID_BMODEL and int(es.get("loopSound", 0)) == 0 and map:
		at += _model_mid(int(es.get("modelindex", 0)))
	return at


var _mids: Dictionary = {}    ## inline model -> its bounds' middle relative to its origin (cgs.inlineModelMidpoints)
var _mids_map: Q3Map


func _model_mid(n: int) -> Vector3:
	if _mids_map != map:
		_mids.clear()
		_mids_map = map
	if not _mids.has(n):
		var mid := Vector3.ZERO
		var found := false
		for m in map.info().get("models", []):
			if int(m["index"]) == n:
				mid = (Q3Map._vec3(m["mins"]) + Q3Map._vec3(m["maxs"])) * 0.5   # Godot meters, model space
				found = true
				break
		if not found:   # an editor play-test map has no JSON: its mesh's bounds
			var node := map.model_node(n)
			for mi in (node.find_children("*", "MeshInstance3D", true, false) if node else []):
				mid = (node.global_transform.affine_inverse() * (mi as MeshInstance3D).global_transform * (mi as MeshInstance3D).get_aabb()).get_center()
				break
		_mids[n] = mid
	return _mids[n]


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
	_local_pain(ps)


var _last_health := 0
var _last_health_client := -1   ## whose health that was (a follow switch starts over: *ops = *ps)
var _last_health_team := -1


## cg_playerstate.c CG_CheckLocalSounds: your own pain sounds come from your health dropping by more
## than 1 (still alive), not from the server's EV_PAIN, which the event code skips for you. Not for
## spectators or at the intermission; the same 500 ms gate (and EV_FALL_FAR's) as everyone's.
func _local_pain(ps: Dictionary) -> void:
	var hp := int(ps["stats"][E.STAT_HEALTH])
	var cn := int(ps.get("clientNum", local_client))
	var team := int(ps["persistant"][E.PERS_TEAM])
	if not muted and cn == _last_health_client and team == _last_health_team and team != E.TEAM_SPECTATOR \
			and int(ps["pm_type"]) != E.PM_INTERMISSION and hp < _last_health - 1 and hp > 0:
		_pain(local_client, hp, Q3.q3_to_godot(ps["origin"]), true)
	_last_health = hp
	_last_health_client = cn
	_last_health_team = team


func _local_event(raw: int, ps: Dictionary, i: int) -> void:
	var ev := raw & ~E.EV_EVENT_BITS
	var es := {"clientNum": local_client, "weapon": ps["weapon"], "eventParm": ps["eventParms"][i & 1],
		"number": local_client, "eType": E.ET_PLAYER, "otherEntityNum": 0, "origin": ps["origin"], "origin2": Vector3.ZERO,
		"pos": {"trBase": ps["origin"]}, "apos": {"trBase": ps["viewangles"]}, "powerups": ps["powerups"][E.PW_QUAD]}
	_event(ev, es, Q3.q3_to_godot(ps["origin"]), true)
	local_event.emit(ev, int(ps["eventParms"][i & 1]))


func _event(ev: int, es: Dictionary, at: Vector3, local := false) -> void:
	if ev == E.EV_OBITUARY:
		death_noted.emit(int(es["otherEntityNum2"]), int(es["otherEntityNum"]), int(es["eventParm"]))
	if muted:
		return
	var cl := int(es.get("clientNum", 0))
	# CG_CustomSound's client: the entity's number (a player's own event), or the player a temp
	# event entity was made for (EF_PLAYER_EVENT); anything else speaks with client 0's voice
	var who := int(es.get("number", cl))
	if int(es.get("eFlags", 0)) & E.EF_PLAYER_EVENT:
		who = int(es.get("otherEntityNum", who))
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
			if not local:   # yours come from your health dropping (_local_pain)
				_pain(who, int(es["eventParm"]), at, local)
		E.EV_DEATH1, E.EV_DEATH2, E.EV_DEATH3:
			play_sound(custom_sound(who, "*death%d.wav" % (ev - E.EV_DEATH1 + 1)), at, local)
		E.EV_GIB_PLAYER:
			# Settings > Graphics > Gibs is cg_gibs; with it off the body burns away instead
			var body := _body_near(at)
			if Settings.get_value("graphics/gibs"):
				if body:
					body.visible = false
				Q3FX.gibs(self, at)
			elif body is Q3Character and not body.has_meta("burning"):
				body.set_meta("burning", true)
				(body as Q3Character).disintegrate(CORPSE_BURN)
			play_sound("sound/player/gibsplt1.wav", at)
		E.EV_JUMP:
			play_sound(custom_sound(who, "*jump1.wav"), at, local)
		E.EV_JUMP_PAD:
			# the boing at the pad, the player's jump grunt, and a puff of smoke rising off it
			_smoke.add(at, Vector3.UP * Q3.UNIT, 32.0, 0.33, 1000.0, _now, false)
			play_sound("sound/world/jumppad.wav", at, local)
			play_sound(custom_sound(who, "*jump1.wav"), at, local)
		E.EV_TAUNT:
			play_sound(custom_sound(who, "*taunt.wav"), at, local)
		E.EV_FALL_SHORT:
			play_sound("sound/player/land1.wav", at, local)
			if local:
				landed.emit(-8.0)
		E.EV_FALL_MEDIUM:
			play_sound(custom_sound(who, "*pain100_1.wav"), at, local)
			if local:
				landed.emit(-16.0)
		E.EV_FALL_FAR:
			play_sound(custom_sound(who, "*fall1.wav"), at, local)
			_pain_time[who] = _now   # don't play a pain sound right after this
			if local:
				landed.emit(-24.0)
		E.EV_STEP_4, E.EV_STEP_8, E.EV_STEP_12, E.EV_STEP_16:
			if local:
				stepped.emit(4.0 * (ev - E.EV_STEP_4 + 1))
		E.EV_FOOTSTEP, E.EV_FOOTSTEP_METAL, E.EV_FOOTSPLASH, E.EV_FOOTWADE, E.EV_SWIM:
			# the model's own footsteps (animation.cfg), metal on metal floors, splashes in water
			var kind := footstep_kind(_sound_model(who))
			if ev == E.EV_FOOTSTEP_METAL:
				kind = "clank"
			elif ev != E.EV_FOOTSTEP:
				kind = "splash"
			play_sound("sound/player/footsteps/%s%d.wav" % [kind, randi_range(1, 4)], at, local, -8.0)
		E.EV_WATER_TOUCH:
			play_sound("sound/player/watr_in.wav", at, local)
		E.EV_WATER_LEAVE:
			play_sound("sound/player/watr_out.wav", at, local)
		E.EV_WATER_UNDER:
			play_sound("sound/player/watr_un.wav", at, local)
		E.EV_WATER_CLEAR:
			play_sound(custom_sound(who, "*gasp.wav"), at, local)
		E.EV_ITEM_PICKUP, E.EV_GLOBAL_ITEM_PICKUP:
			_pickups.append([at, _now])
			if _pickups.size() > 8:
				_pickups.pop_front()
			var idx := int(es["eventParm"])
			if idx >= 1 and idx <= _item_names.size():
				var it: Dictionary = Q3.entities.get(_item_names[idx - 1], {})
				if ev == E.EV_ITEM_PICKUP and String(it.get("type", "")) in ["powerup", "team"]:
					# their own sound is the global pickup's; this one is the plain health chime
					play_sound("sound/items/n_health.wav", at, local)
				elif it.get("pickup_sound", ""):
					play_sound(String(it["pickup_sound"]), at, local or ev == E.EV_GLOBAL_ITEM_PICKUP)
		E.EV_ITEM_RESPAWN:
			var back: Variant = _ents.get(int(es.get("number", -1)), {}).get("node")
			if is_instance_valid(back) and back is Q3Pickup:
				(back as Q3Pickup).respawned()
			play_sound("sound/items/respawn1.wav", at)
		E.EV_PLAYER_TELEPORT_IN, E.EV_PLAYER_TELEPORT_OUT:
			# CG_SpawnEffect where they appear and where they vanish, standing on the floor (the
			# event is at the origin, 24 units over the feet)
			if Settings.get_value("gameplay/spawn_effect"):
				Q3FX.teleport(self, at + Vector3.DOWN * 24.0 * Q3.UNIT)
			play_sound("sound/world/telein.wav" if ev == E.EV_PLAYER_TELEPORT_IN else "sound/world/teleout.wav", at)
		E.EV_USE_ITEM0, E.EV_USE_ITEM1, E.EV_USE_ITEM2, E.EV_USE_ITEM3, E.EV_USE_ITEM4, E.EV_USE_ITEM5, E.EV_USE_ITEM6, E.EV_USE_ITEM7:
			# CG_UseItem: the medkit's sound; the teleporter's are its teleport events; nothing to use buzzes
			var item := ev - E.EV_USE_ITEM0
			if item == E.HI_MEDKIT:
				play_sound("sound/items/use_medkit.wav", at, local)
			elif item != E.HI_TELEPORTER:
				play_sound("sound/items/use_nothing.wav", at, local)
		E.EV_POWERUP_QUAD:
			play_sound("sound/items/damage3.wav", at, local)   # a quad hit lands
		E.EV_POWERUP_BATTLESUIT:
			play_sound("sound/items/protect3.wav", at, local)   # the battle suit takes a hit
		E.EV_POWERUP_REGEN:
			play_sound("sound/items/regen.wav", at, local)   # a regeneration tick
		E.EV_GENERAL_SOUND, E.EV_GLOBAL_SOUND:
			# "*name.wav" is the player's own voice: a pit's target_speaker screams *falling1.wav
			var path := feed.configstring(E.CS_SOUNDS + int(es["eventParm"]))
			if path != "":
				play_sound(custom_sound(who, path), at, local or ev == E.EV_GLOBAL_SOUND)
		E.EV_GLOBAL_TEAM_SOUND:
			_team_sound(int(es["eventParm"]))
		E.EV_OBITUARY:
			obituary.emit(int(es["otherEntityNum2"]), int(es["otherEntityNum"]), int(es["eventParm"]))


## CG_PainEvent: at most two pain sounds a second, by how hurt the player is.
func _pain(who: int, health: int, at: Vector3, local: bool) -> void:
	if _now - float(_pain_time.get(who, -100000.0)) < 500.0:
		return
	_pain_time[who] = _now
	_pain_dir[who] = not _pain_dir.get(who, false)   # the twitch alternates sides (painDirection)
	var which := "25" if health < 25 else "50" if health < 50 else "75" if health < 75 else "100"
	play_sound(custom_sound(who, "*pain%s_1.wav" % which), at, local)


## bg_public.h global_team_sound_t
enum { GTS_RED_CAPTURE, GTS_BLUE_CAPTURE, GTS_RED_RETURN, GTS_BLUE_RETURN, GTS_RED_TAKEN, GTS_BLUE_TAKEN,
	GTS_REDOBELISK_ATTACKED, GTS_BLUEOBELISK_ATTACKED, GTS_REDTEAM_SCORED, GTS_BLUETEAM_SCORED,
	GTS_REDTEAM_TOOK_LEAD, GTS_BLUETEAM_TOOK_LEAD, GTS_TEAMS_ARE_TIED }


## EV_GLOBAL_TEAM_SOUND's announcer lines (cg_event.c), for the local player's team. A flag they
## took themselves is Q3Hud's "You have the flag" instead.
func _team_sound(gts: int) -> void:
	var tc := team_client if team_client >= 0 else local_client
	var team := String(Q3Host.parse_info(feed.configstring(E.CS_PLAYERS + tc)).get("t", "")) if tc >= 0 else ""
	var red := team == "1"
	var blue := team == "2"
	var tp := "sound/teamplay/"
	# their flag, from this snapshot (cg.snap->ps): outside first person in a replay, their own state
	var bits := powerup_bits(_local_ps if not _local_ps.is_empty() else feed.player_state(tc) if tc >= 0 else {})
	match gts:
		GTS_RED_CAPTURE:
			team_sound.emit(tp + ("flagcapture_yourteam.wav" if red else "flagcapture_opponent.wav"))
		GTS_BLUE_CAPTURE:
			team_sound.emit(tp + ("flagcapture_yourteam.wav" if blue else "flagcapture_opponent.wav"))
		GTS_RED_RETURN:
			team_sound.emit(tp + ("flagreturn_yourteam.wav" if red else "flagreturn_opponent.wav"))
			team_sound.emit(tp + "voc_blue_returned.wav")
		GTS_BLUE_RETURN:
			team_sound.emit(tp + ("flagreturn_yourteam.wav" if blue else "flagreturn_opponent.wav"))
			team_sound.emit(tp + "voc_red_returned.wav")
		GTS_RED_TAKEN:
			if not bits & (1 << E.PW_BLUEFLAG) and (red or blue):
				team_sound.emit(tp + ("voc_enemy_flag.wav" if blue else "voc_team_flag.wav"))
		GTS_BLUE_TAKEN:
			if not bits & (1 << E.PW_REDFLAG) and (red or blue):
				team_sound.emit(tp + ("voc_enemy_flag.wav" if red else "voc_team_flag.wav"))
		GTS_REDTEAM_SCORED:
			team_sound.emit(tp + "voc_red_scores.wav")
		GTS_BLUETEAM_SCORED:
			team_sound.emit(tp + "voc_blue_scores.wav")
		GTS_REDTEAM_TOOK_LEAD:
			team_sound.emit("sound/feedback/redleads.wav")
		GTS_BLUETEAM_TOOK_LEAD:
			team_sound.emit("sound/feedback/blueleads.wav")
		GTS_TEAMS_ARE_TIED:
			team_sound.emit("sound/feedback/teamstied.wav")


## CG_CustomSound: "*name.wav" in a player's own voice (sound/player/<model>/name.wav), Sarge's
## (DEFAULT_MODEL) where their model has none; any other path as it is.
func custom_sound(cl: int, name: String) -> String:
	if not name.begins_with("*"):
		return name
	if cl < 0 or cl >= E.MAX_CLIENTS:
		cl = 0
	var own := "sound/player/%s/%s" % [_sound_model(cl), name.substr(1)]
	if ResourceLoader.exists("res://library/sounds/" + own.trim_prefix("sound/")):
		return own
	return "sound/player/sarge/" + name.substr(1)


## The model whose voice and footsteps a player has: their userinfo's, before any skin, whatever
## Gameplay > Force enemy model draws them as (ci->modelName).
func _sound_model(cl: int) -> String:
	if cl < 0 or cl >= E.MAX_CLIENTS:
		cl = 0
	var info := Q3Host.parse_info(feed.configstring(E.CS_PLAYERS + cl))
	return String(info.get("model", "sarge")).get_slice("/", 0).to_lower()


static var _footsteps: Dictionary = {}   ## model -> footstep sound prefix, read once from the paks


## CG_ParseAnimationFile's "footsteps" (default, normal, boot, flesh, mech, energy) as the sound
## file's prefix. The remaster ships no animation.cfg, so it's read from the player's own paks.
static func footstep_kind(model: String) -> String:
	load_footsteps()
	return String(_footsteps.get(model, "step"))


## Reads every player model's footsteps from the paks, once a run, at match load (Q3Client._ready),
## as cgame does while loading client info: opening every pak mid-match would stall a frame.
static func load_footsteps() -> void:
	if _footsteps.is_empty():
		_footsteps["-"] = "step"   # read once, even when there are no paks
		var paks := Q3MapCompiler.pak_dir()
		if paks != "" and DirAccess.dir_exists_absolute(paks):
			var names: Array = []
			for f in DirAccess.get_files_at(paks):
				if f.to_lower().ends_with(".pk3"):
					names.append(f)
			names.sort()   # later paks win, as Quake 3 loads them
			for f in names:
				var z := ZIPReader.new()
				if z.open(paks.path_join(f)) != OK:
					continue
				for e in z.get_files():
					var low := e.to_lower()
					if low.begins_with("models/players/") and low.ends_with("/animation.cfg"):
						var kind := "step"
						for line in z.read_file(e).get_string_from_ascii().split("\n"):
							var tok := line.strip_edges().replace("\t", " ").split(" ", false)
							if tok.size() >= 2 and tok[0].to_lower() == "footsteps":
								var k := tok[1].to_lower()
								kind = k if k in ["boot", "flesh", "mech", "energy"] else "step"
						_footsteps[low.get_slice("/", 2)] = kind
				z.close()


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
		var kind := "machinegun" if w == E.WP_MACHINEGUN else "shotgun"
		# where the shell comes out (the brass offset in the view axes), back in id units, for the
		# water check (waterScale)
		var b := angles_basis(angles)
		var o: Array = Q3.fx["brass"][kind]["offset"]
		var g := Q3.q3_to_godot(origin) + (b.z * float(o[0]) + b.x * float(o[1]) + b.y * float(o[2])) * Q3.UNIT
		var water := (feed.point_contents(Vector3(g.z, g.x, g.y) / Q3.UNIT, -1) & CONTENTS_WATER) != 0
		Q3FX.brass(self, kind, Q3.q3_to_godot(origin), b, water)


## CG_MissileHitWall: the explosion and mark for the weapon, and its impact sound. The mark only
## lands on the world, never on a nomarks surface or a mover (`mark` -1: traced here).
func missile_hit_wall(weapon: int, cl: int, at: Vector3, dir: Vector3, mark := -1) -> void:
	# its switch's default: falls into WP_LIGHTNING in baseq3, and the lightning gun's wall hit
	# (g_weapon.c Weapon_LightningFire) is sent with weapon 0
	if not EXPLOSIONS.has(weapon):
		weapon = E.WP_LIGHTNING
	var kind: String = EXPLOSIONS.get(weapon, "")
	if mark < 0:
		mark = int(_marks_surface(at, dir))
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
		Q3FX.explosion(self, kind, at, dir, Color.WHITE, client_colors(cl)[1], mark == 1)
	else:
		Q3FX.explosion(self, kind, at, dir, Color.WHITE, Color.WHITE, mark == 1)
	var sp: Array = SPARKS.get(weapon, [])
	if not sp.is_empty():
		_sparks.burst(at, dir, int(sp[0]), float(sp[1]), float(sp[2]), float(sp[3]))


## Whether an impact at `at` (Godot) facing `dir` takes a mark: tr_marks.c R_MarkFragments marks
## only the world's surfaces, and none whose shader is nomarks or noimpact (jump pads, lights, sky).
func _marks_surface(at: Vector3, dir: Vector3) -> bool:
	var p := Vector3(at.z, at.x, at.y) / Q3.UNIT
	var n := Vector3(dir.z, dir.x, dir.y)
	var tr := feed.trace(p + n * 4.0, p - n * 20.0, Vector3.ZERO, Vector3.ZERO, -1, CONTENTS_SOLID)
	return not tr.is_empty() and float(tr["fraction"]) < 1.0 and int(tr["entityNum"]) == ENTITYNUM_WORLD \
		and not int(tr["surfaceFlags"]) & (SURF_NOIMPACT | SURF_NOMARKS)


## CG_BubbleTrail (id units): bubbles every `spacing` units from a random start, drifting up.
func _bubble_trail(start: Vector3, end: Vector3, spacing: float) -> void:
	var v := end - start
	var length := v.length()
	if length <= 0.0:
		return
	v /= length
	var i := float(randi() % int(spacing))
	var p := start + v * i
	while i < length:
		var vel := Vector3(randf_range(-5.0, 5.0), randf_range(-5.0, 5.0), randf_range(-5.0, 5.0) + 6.0)
		_bubbles.add(Q3.q3_to_godot(p), Q3.q3_to_godot(vel), 3.0, 1.0, 1000.0 + randf() * 250.0, _now, false)
		p += v * spacing
		i += spacing


## CG_Bullet and CG_ShotgunPellet's bubbles: a shot through water, into it or out of it leaves a
## trail through the water part (id units).
func _shot_bubbles(start: Vector3, end: Vector3) -> void:
	var sc := feed.point_contents(start, 0)
	var dc := feed.point_contents(end, 0)
	if sc == dc and sc & CONTENTS_WATER:
		_bubble_trail(start, end, 32.0)
	elif sc & CONTENTS_WATER:
		var w := feed.trace(end, start, Vector3.ZERO, Vector3.ZERO, -1, CONTENTS_WATER)
		if not w.is_empty():
			_bubble_trail(start, w["endpos"], 32.0)
	elif dc & CONTENTS_WATER:
		var w := feed.trace(start, end, Vector3.ZERO, Vector3.ZERO, -1, CONTENTS_WATER)
		if not w.is_empty():
			_bubble_trail(w["endpos"], end, 32.0)


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
	if source >= 0:
		var start := _muzzle_point(source)
		if start != Vector3.INF:
			_shot_bubbles(start, end)
			if randf() < TRACER_CHANCE:
				# CG_Tracer: the whizz only with a streak drawn, from its middle
				var mid := Q3FX.tracer(self, Q3.q3_to_godot(start), at, eye)
				if mid != Vector3.INF:
					play_sound("sound/weapons/machinegun/buletby1.wav", mid)
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
	if not (feed.point_contents(origin, 0) & 32):
		# (id's code measures from origin2 - origin, but origin2 is a direction: the puff goes 32 units along it)
		_shotgun_smoke.add(Q3.q3_to_godot(origin + dir * 32.0), Vector3(0, 8, 0) * Q3.UNIT, 32.0, 0.33, 900.0, _now, false)
	var right := perpendicular(dir)
	var up := dir.cross(right)
	var skip := int(es.get("otherEntityNum", -1))
	# Quake Live's ruleset (g_ruleset, patch 0005) fires a fixed pattern; replays and demos from
	# before it have no g_ruleset, and were 1.32's
	if not feed.cvar("g_ruleset").to_lower() in ["", "classic"]:
		for i in QL_SHOTGUN_COUNT:
			var p := ql_shotgun_pellet(i)
			_shotgun_pellet(origin, origin + dir * (8192.0 * 16.0) + right * p.x + up * p.y, skip)
		return
	var seed := [int(es["eventParm"])]
	for i in 11:   # DEFAULT_SHOTGUN_COUNT; DEFAULT_SHOTGUN_SPREAD 700 * 16
		var r := q_crandom(seed) * 700.0 * 16.0
		var u := q_crandom(seed) * 700.0 * 16.0
		var end := origin + dir * (8192.0 * 16.0) + right * r + up * u
		_shotgun_pellet(origin, end, skip)


## BG_QLShotgunPellet (patch 0005): pellet i of Quake Live's fixed pattern as (right, up) offsets at
## 8192 * 16 units out: 6 every 60 degrees at 4000, 6 between them at 8000, 8 every 45 at 12000.
const QL_SHOTGUN_COUNT := 20

static func ql_shotgun_pellet(i: int) -> Vector2:
	var angle := i * 60.0 if i < 6 else ((i - 6) * 60.0 + 30.0 if i < 12 else (i - 12) * 45.0)
	var radius := 4000.0 if i < 6 else (8000.0 if i < 12 else 12000.0)
	return Vector2(radius * cos(deg_to_rad(angle)), radius * sin(deg_to_rad(angle)))   # one rounding, as in C


func _shotgun_pellet(start: Vector3, end: Vector3, skip: int) -> void:
	var tr := feed.trace(start, end, Vector3.ZERO, Vector3.ZERO, skip, MASK_SHOT)
	if tr.is_empty():
		return
	_shot_bubbles(start, tr["endpos"])
	if int(tr["surfaceFlags"]) & SURF_NOIMPACT:
		return
	var at := Q3.q3_to_godot(tr["endpos"])
	var n: Vector3 = tr["normal"]
	var hit := int(tr["entityNum"])
	if _ents.get(hit, {}).get("kind", -1) == E.ET_PLAYER:
		_bleed(at, hit)   # CG_MissileHitPlayer(WP_SHOTGUN): blood only
	else:
		var marks := hit == ENTITYNUM_WORLD and not int(tr["surfaceFlags"]) & SURF_NOMARKS
		missile_hit_wall(E.WP_SHOTGUN, 0, at, Vector3(n.y, n.z, n.x), int(marks))


## Every frame after the camera moves: the weapons' flashes and barrels, the lightning beams,
## the weapon loops, and the smoke.
func present(local_ps: Dictionary, eye_pos: Vector3) -> void:
	_local_ps = local_ps
	eye = eye_pos
	if (own_death or own_body) and local_client >= 0:
		_update_own_body(local_ps)
	if self_body or self_visible:
		_update_self(local_ps)
	elif not _self.is_empty():
		(_self["node"] as Node3D).visible = false
	for num in _ents:
		var e: Dictionary = _ents[num]
		if e["kind"] != E.ET_PLAYER or e.get("ragdolled", false):
			continue
		var es: Dictionary = e["es"]
		var ch := e["node"] as Q3Character
		if ch and is_instance_valid(ch):
			var gun: Variant = e.get("gun")
			_powerups(num, ch, ch.body, gun if is_instance_valid(gun) else null, int(es.get("powerups", 0)), int(es["legsAnim"]))
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
		_local_powerups(local_ps)
	else:
		_clear_local_holder()   # nobody's eyes (a replay's chase, orbit or free camera): nothing left behind
	_smoke.step(_now, eye)
	_shotgun_smoke.step(_now, eye)
	_bubbles.step(_now, eye)
	_glow_trail.step(_now, eye)
	_sparks.step(get_process_delta_time(), eye)


# ---------------------------------------------------------------- powerups (cg_players.c)

const SHELLS := [[E.PW_QUAD, "quad"], [E.PW_REGEN, "regen"], [E.PW_BATTLESUIT, "battlesuit"]]
const WEAPON_SHELLS := [[E.PW_BATTLESUIT, "battlesuit"], [E.PW_QUAD, "quad"]]   ## CG_AddWeaponWithPowerups: no regen
var _haste_time: Dictionary = {}   ## client -> next haste puff (cent->trailTime), ms
var _local_holder: Node3D          ## where the local player's powerup lights, sound and flag hang


## es.powerups' bits from a playerState (BG_PlayerStateToEntityState: any powerup with time left).
static func powerup_bits(ps: Dictionary) -> int:
	var bits := 0
	var pw: Variant = ps.get("powerups", PackedInt32Array())   # a PackedInt32Array
	for i in mini(pw.size(), E.PW_NUM_POWERUPS):
		if int(pw[i]) != 0:
			bits |= 1 << i
	return bits


## The shells for a set of powerup bits, in id's drawing order.
static func shell_kinds(bits: int, table := SHELLS) -> PackedStringArray:
	var out := PackedStringArray()
	for s in table:
		if bits & (1 << int(s[0])):
			out.append(s[1])
	return out


## What a player's powerups show (CG_PlayerPowerups, CG_AddRefEntityWithPowerups): the quad, regen
## and battle suit shells over the body and the held gun (regen flashes one tenth of every second;
## invisibility leaves only its own shader), the quad's blue light, a carried flag trailing behind
## (CG_TrailItem: Quake 3's own models have no flag animations) with its team's light, flight's
## hum, and haste's smoke at the feet while running. `holder` carries the lights, sound and flag:
## the flag shows in first person too (CG_TrailItem sets no RF_THIRD_PERSON).
func _powerups(cl: int, holder: Node3D, body: Node3D, gun: Node3D, bits: int, legs_anim: int) -> void:
	var invis := bits & (1 << E.PW_INVIS) != 0
	if holder is Q3Character:
		(holder as Q3Character).set_anim_speed(1.5 if bits & (1 << E.PW_HASTE) else 1.0)
	if body and is_instance_valid(body):
		Q3FX.powerup_shells(body, shell_kinds(bits), invis)
		Q3FX.shell_visible(body, "regen", int(_now / 100.0) % 10 == 1)
	if gun and is_instance_valid(gun):
		Q3FX.powerup_shells(gun, shell_kinds(bits, WEAPON_SHELLS), invis, true)
	_powerup_light(holder, "QuadLight", bits & (1 << E.PW_QUAD) != 0, Color(0.2, 0.2, 1.0))
	_powerup_light(holder, "RedFlagLight", bits & (1 << E.PW_REDFLAG) != 0, Color(1.0, 0.2, 0.2))
	_powerup_light(holder, "BlueFlagLight", bits & (1 << E.PW_BLUEFLAG) != 0, Color(0.2, 0.2, 1.0))
	_trail_flag(holder, "team_CTF_redflag", bits & (1 << E.PW_REDFLAG) != 0)
	_trail_flag(holder, "team_CTF_blueflag", bits & (1 << E.PW_BLUEFLAG) != 0)
	var hum := holder.get_node_or_null("FlightLoop") as AudioStreamPlayer3D
	if bits & (1 << E.PW_FLIGHT) and hum == null:
		var res := "res://library/sounds/items/flight.wav"
		if ResourceLoader.exists(res):
			hum = AudioStreamPlayer3D.new()
			hum.name = "FlightLoop"
			hum.stream = Q3FX.looped(Q3.held(res))
			hum.unit_size = 10.0
			hum.max_distance = 40.0
			hum.bus = &"Effects"
			Settings.spatialize(hum)
			holder.add_child(hum)
			hum.play()
	elif not bits & (1 << E.PW_FLIGHT) and hum:
		hum.queue_free()
	# CG_HasteTrail: a puff 16 units under the origin every 100 ms while running forward or back
	var anim := legs_anim & ~E.ANIM_TOGGLEBIT
	if bits & (1 << E.PW_HASTE) and (anim == E.LEGS_RUN or anim == E.LEGS_BACK):
		var t: float = _haste_time.get(cl, 0.0)
		if t <= _now:
			_haste_time[cl] = maxf(t + 100.0, _now)
			_smoke.add(holder.global_position + Vector3.UP * 8.0 * Q3.UNIT, Vector3.ZERO, 8.0, 1.0, 500.0, _now)


## trap_R_AddLightToScene(origin, 200 + (rand() & 31), color) while `on`.
func _powerup_light(holder: Node3D, light_name: String, on: bool, color: Color) -> void:
	var l := holder.get_node_or_null(light_name) as OmniLight3D
	if not on:
		if l:
			l.queue_free()
		return
	if l == null:
		l = OmniLight3D.new()
		l.name = light_name
		l.light_color = color
		l.shadow_enabled = false
		l.position = Vector3.UP * 24.0 * Q3.UNIT   # the origin, 24 units over the feet
		holder.add_child(l)
	l.omni_range = (200.0 + float(randi() & 31)) * Q3.UNIT
	l.light_energy = 2.0 * Q3FX.gain()


## CG_TrailItem: the flag model 16 units behind the origin and 16 over it, turned 90 degrees.
func _trail_flag(holder: Node3D, classname: String, on: bool) -> void:
	var node_name := "Carried_" + classname
	var flag := holder.get_node_or_null(node_name) as Node3D
	if not on:
		if flag:
			flag.queue_free()
		return
	if flag == null:
		var models: Array = Q3.entities.get(classname, {}).get("models", [])
		flag = Q3.instance_prop(models[0]) if models.size() else null
		if flag == null:
			return
		flag.name = node_name
		holder.add_child(flag)
		flag.position = Vector3(0.0, 40.0, -16.0) * Q3.UNIT
		flag.basis = Basis(Vector3.UP, deg_to_rad(90.0))


## The local player's powerups: their body is only drawn in mirrors (or by a spectator's chase
## camera), but the quad's light, the flight hum and the haste smoke are there in first person too.
func _local_powerups(ps: Dictionary) -> void:
	if _local_holder == null or not is_instance_valid(_local_holder):
		_local_holder = Node3D.new()
		_local_holder.name = "LocalPowerups"
		add_child(_local_holder)
		_helpers.append(_local_holder)   # reset() keeps it
	_local_holder.global_position = Q3.q3_to_godot(ps["origin"]) + Vector3.DOWN * 24.0 * Q3.UNIT
	_local_holder.rotation = Vector3(0.0, deg_to_rad(float((ps["viewangles"] as Vector3).y)), 0.0)
	var body: Node3D = null
	var gun: Node3D = null
	if not _self.is_empty() and is_instance_valid(_self["node"]):
		body = (_self["node"] as Q3Character).body
		var g: Variant = _self.get("gun")
		gun = g if is_instance_valid(g) else null
	var pm := int(ps["pm_type"])
	var bits := powerup_bits(ps) if pm != E.PM_DEAD and pm != E.PM_SPECTATOR else 0
	_powerups(local_client, _local_holder, body, gun, bits, int(ps["legsAnim"]))
	if body:
		(_self["node"] as Q3Character).set_anim_speed(1.5 if bits & (1 << E.PW_HASTE) else 1.0)
	# CG_EntityEffects for the predicted player: ps.loopSound, the sizzle in lava or slime. A replay
	# carries the watched player's own entity, whose loop already plays.
	var loop := 0 if _ents.has(local_client) else int(ps.get("loopSound", 0))
	if int(_local_holder.get_meta("loop_sound", 0)) != loop:
		_local_holder.set_meta("loop_sound", loop)
		var old := _local_holder.get_node_or_null("LoopSound")
		if old:
			old.queue_free()
		var stream := _load_sound(feed.configstring(E.CS_SOUNDS + loop)) if loop != 0 else null
		if stream:
			var a := AudioStreamPlayer3D.new()
			a.name = "LoopSound"
			a.stream = Q3FX.looped(stream)
			a.unit_size = 12.0
			a.max_distance = 60.0
			a.bus = &"Effects"
			Settings.spatialize(a)
			a.position = Vector3.UP * 24.0 * Q3.UNIT   # the origin (the holder is at the feet)
			_local_holder.add_child(a)
			a.play()


## Everything the local player's powerups and loop hung on the holder goes (a replay camera that
## isn't anyone's eyes, a follow switch, a replay jump): CG_PlayerPowerups adds them per frame.
func _clear_local_holder() -> void:
	if _local_holder == null or not is_instance_valid(_local_holder):
		return
	for c in _local_holder.get_children():
		_local_holder.remove_child(c)
		c.queue_free()
	_local_holder.set_meta("loop_sound", 0)


## The local player's own body, for mirrors and portal views only: cgame draws the predicted
## player with RF_THIRD_PERSON ("don't draw through eyes, only mirrors"). Built from the
## playerState the way BG_PlayerStateToEntityState makes the entity others see.
func _update_self(ps: Dictionary) -> void:
	if ps.is_empty() or local_client < 0:
		return
	if not _self.is_empty() and not _restyle(_self, local_client):
		(_self["node"] as Node).queue_free()   # a new model: built again below
		_self = {}
	if _self.is_empty():
		var body := _body_for(local_client)
		body.turn = true
		body.match_clock = true
		add_child(body)
		_bright(body, local_client)
		_self = {"node": body, "kind": E.ET_PLAYER, "legs": -1, "torso": -1, "weapon": -1}
	var ch: Q3Character = _self["node"]
	var pm := int(ps["pm_type"])
	ch.visible = pm == E.PM_NORMAL or pm == E.PM_FREEZE or pm == E.PM_NOCLIP
	if not ch.visible:
		_self.erase("tele")   # back in view: CG_ResetPlayerEntity
		return
	var weapon: int = _self["weapon"]
	_update_player(_self, {"number": local_client, "clientNum": local_client, "eType": E.ET_PLAYER,
		"eFlags": int(ps["eFlags"]) & ~E.EF_DEAD, "event": 0, "weapon": int(ps["weapon"]),
		"legsAnim": int(ps["legsAnim"]), "torsoAnim": int(ps["torsoAnim"]), "powerups": powerup_bits(ps),
		"angles2": Vector3(0.0, float(ps.get("movementDir", 0)), 0.0),
		"pos": {"trType": 0, "trBase": ps["origin"], "trDelta": ps["velocity"]},
		"apos": {"trType": 0, "trBase": ps["viewangles"]}})
	_advance_body(_self)
	if weapon != _self["weapon"] or _self.get("layered", -1) != int(self_visible):
		_self["layered"] = int(self_visible)
		for g in ch.find_children("*", "GeometryInstance3D", true, false):
			(g as GeometryInstance3D).layers = (1 if self_visible else Q3Portal.MIRROR_LAYER) | Q3Character.FILL_LAYER
			(g as GeometryInstance3D).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON if self_visible 				else GeometryInstance3D.SHADOW_CASTING_SETTING_OFF


## CG_LightningBolt: from the eye 14 units forward, LIGHTNING_RANGE along the aim; the beam is
## drawn from the weapon's flash to where that trace stops.
func _lightning(num: int, on: bool, flash: Vector3, start: Vector3, fwd: Vector3) -> void:
	var beam: Q3LightningBeam = _beams.get(num)
	if not on or start == Vector3.INF:
		if beam:
			beam.visible = false
		_burns.lift(num)
		return
	if beam == null:
		beam = Q3LightningBeam.new()
		add_child(beam)
		_beams[num] = beam
	beam.visible = true
	var tr := feed.trace(start, start + fwd * LIGHTNING_RANGE, Vector3.ZERO, Vector3.ZERO, num, MASK_SHOT)
	if tr.is_empty():
		beam.visible = false
		_burns.lift(num)
		return
	beam.aim(flash, Q3.q3_to_godot(tr["endpos"]), float(tr["fraction"]) < 1.0)
	# the remaster's burn line where the beam touches the map (not movers, sky or nomarks surfaces)
	if float(tr["fraction"]) < 1.0 and int(tr["entityNum"]) == ENTITYNUM_WORLD \
			and not int(tr["surfaceFlags"]) & (SURF_NOIMPACT | SURF_NOMARKS):
		var n: Vector3 = tr["normal"]
		_burns.touch(num, Q3.q3_to_godot(tr["endpos"]), Vector3(n.y, n.z, n.x))
	else:
		_burns.lift(num)


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
		a.stream = Q3FX.looped(Q3.held(res))
		a.volume_db = -4.0
		player = a
	else:
		var a := AudioStreamPlayer3D.new()
		a.stream = Q3FX.looped(Q3.held(res))
		a.unit_size = 10.0
		a.max_distance = 40.0
		Settings.spatialize(a)
		player = a
	player.set("bus", &"Effects")
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
## Your own rail can be one color1 can't carry (a card rail past Quake 3's seven); it draws that.
func client_colors(cl: int) -> Array:
	var info := Q3Host.parse_info(feed.configstring(E.CS_PLAYERS + cl))
	var out := [_color_from(String(info.get("c1", "4"))), _color_from(String(info.get("c2", "5")))]
	if cl == local_client and not feed.is_replay():
		var own := Q3Profile.rail_colors(int(info.get("t", "0")))
		for i in own.size():
			out[i] = own[i]
	return out


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
	if ch == null or not is_instance_valid(ch) or e.get("ragdolled", false) or ch.body == null:
		return
	if int(e.get("es", {}).get("powerups", 0)) & (1 << E.PW_INVIS):
		return   # an invisible body shows only the invisibility shader (CG_AddRefEntityWithPowerups)
	var meshes: Array = e.get("armor_mat", [])
	if meshes.is_empty():
		# the body only: not a carried flag hanging off the character
		for mi in ch.body.find_children("*", "MeshInstance3D", true, false):
			if not String(mi.get_path()).contains("Attach_") and not Q3FX.is_shell(mi):
				var m := mi as MeshInstance3D
				if m.material_overlay:   # a replay's outline stays, after the shell: a material of its own
					var own := ShaderMaterial.new()
					own.shader = ARMOR_SHELL
					own.next_pass = m.material_overlay
					m.material_overlay = own
				else:
					m.material_overlay = _armor_shell()
				meshes.append(m)
		e["armor_mat"] = meshes
	var color := ARMOR_LOW_COLOR if armor < ARMOR_LOW else ARMOR_COLOR
	var flash := func(v: float) -> void:
		for m in meshes:
			if is_instance_valid(m):
				(m as MeshInstance3D).set_instance_shader_parameter("flash", v)
	for m in meshes:
		if is_instance_valid(m):
			(m as MeshInstance3D).set_instance_shader_parameter("color", color)
	flash.call(1.0)
	create_tween().tween_method(flash, 1.0, 0.0, 0.4)


static var _shell: ShaderMaterial


## The armor shell every body shares (its color and flash are instance uniforms).
static func _armor_shell() -> ShaderMaterial:
	if _shell == null:
		_shell = ShaderMaterial.new()
		_shell.shader = ARMOR_SHELL
	return _shell


func _head_of(target: int) -> Vector3:
	var e: Dictionary = _ents.get(target, {})
	var node := e.get("node") as Node3D
	if node and is_instance_valid(node):
		return node.global_position + Vector3.UP * 2.0
	return Vector3.INF


# ---------------------------------------------------------------- sound

## Plays a Quake 3 sound path (sound/...wav) from library/sounds; positional unless `flat`.
func play_sound(path: String, at: Vector3, flat := false, volume_db := 0.0) -> void:
	var stream := _load_sound(path)
	if stream == null:
		return
	var player: Node
	if flat:
		var a := AudioStreamPlayer.new()
		a.stream = stream
		a.volume_db = volume_db
		a.bus = &"Effects"
		player = a
	else:
		var a := AudioStreamPlayer3D.new()
		a.stream = stream
		a.volume_db = volume_db
		a.unit_size = 12.0
		a.max_distance = 60.0
		a.bus = &"Effects"
		Settings.spatialize(a)
		player = a
	add_child(player)
	if player is Node3D:
		(player as Node3D).global_position = at
	player.finished.connect(player.queue_free)
	player.play()


## A Quake 3 sound path's library stream, loaded once; null when the library has none.
func _load_sound(path: String) -> AudioStream:
	if path == "":
		return null
	var p := "res://library/sounds/" + path.trim_prefix("sound/")
	if not _sounds.has(p):
		_sounds[p] = load(p) if ResourceLoader.exists(p) else null
	return _sounds[p]


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


## Your own body while you're dead (own_death): made and ragdolled when the playerState goes
## PM_DEAD, left behind as a corpse (burning away like any other) when you're back.
func _update_own_body(ps: Dictionary) -> void:
	var dead := not ps.is_empty() and int(ps["pm_type"]) == E.PM_DEAD
	if not dead:
		_own_seen_alive = not ps.is_empty()
	# gibbed (health at GIB_HEALTH -40 or under, as BG_PlayerStateToEntityState hides the body): no
	# body, only the gibs
	var gibbed := dead and int(ps["stats"][E.STAT_HEALTH]) <= -40
	if dead and own_body == null and own_death and not muted and _own_seen_alive and not gibbed:
		_own_seen_alive = false
		var ch := _body_for(local_client)
		add_child(ch)
		_bright(ch, local_client)
		ch.global_position = Q3.q3_to_godot(ps["origin"]) + Vector3.DOWN * 24.0 * Q3.UNIT
		ch.rotation = Vector3(0.0, deg_to_rad(float((ps["viewangles"] as Vector3).y)), 0.0)
		ch.ragdoll((Q3.q3_to_godot(ps["velocity"]) * 0.6).limit_length(8.0))
		own_body = ch
	elif not dead and own_body:
		_leave_corpse(own_body)
		own_body = null
