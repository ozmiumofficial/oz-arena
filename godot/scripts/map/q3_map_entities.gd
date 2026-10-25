class_name Q3MapEntities
extends RefCounted
## id's entity classes for the map editor (MAP_EDITOR_PLAN.md E4): what each class is (point or
## brush, its box, its editor color), the lists the pickup tools cycle through, and the bits of id's
## code the editor shows: g_trigger.c AimAtTarget's jump pad launch and q3map's light falloff.
## Entities keep id's key/values as they are ("origin" "x y z", "angle", "target" ...); everything
## here reads or writes those.

const VIEWHEIGHT := 26.0        ## bg_public.h DEFAULT_VIEWHEIGHT: the eye above the player's origin
const SPAWN_LIFT := 9.0         ## g_client.c SelectSpawnPoint: a spawned player starts 9 units up
const DEFAULT_GRAVITY := 800.0  ## g_gravity; worldspawn "gravity" sets it (SP_worldspawn)
const POINT_PHOTONS := 7500.0   ## q3map light.c: a point light of intensity L sends L * 7500
const UNIT := 1.0 / 32.0
const PAD := 64.0               ## the jump pad and teleporter tools' footprint (units)
const SOUNDS_DIR := "res://library/sounds/"

## Editor colors by category (after Radiant's entities.def, which colors spawns magenta, items
## blue, lights green).
const COLORS := {
	"spawn": Color(1.0, 0.3, 0.95), "teleport": Color(1.0, 0.3, 0.25), "movement": Color(0.3, 1.0, 0.55),
	"marker": Color(0.35, 0.75, 0.35), "audio": Color(0.3, 0.85, 0.85), "world": Color(0.4, 1.0, 0.3),
	"logic": Color(0.95, 0.85, 0.3), "hazard": Color(1.0, 0.45, 0.2), "mover": Color(0.4, 0.6, 1.0),
	"decoration": Color(0.8, 0.7, 0.55), "render": Color(0.7, 0.5, 1.0), "item": Color(0.35, 0.5, 1.0),
}

## The Entities tab's sections: [label, filter] (filter: an item type, or a map entity category).
const SECTIONS := [
	["Weapons", "weapon"], ["Ammo", "ammo"], ["Health", "health"], ["Armor", "armor"],
	["Powerups and holdables", "powerup|holdable"], ["Flags", "team"],
	["Spawns", "spawn"], ["Jump pads and pushers", "movement"], ["Teleporters", "teleport"],
	["Lights and sound", "world|audio"], ["Markers", "marker"], ["Movers (brushes)", "mover"],
	["Triggers and logic", "logic"], ["Hazards", "hazard"], ["Models and portals", "decoration|render"],
]

## The inspector's typed fields: [key, kind, default]; kind is "num", "text", "target" (another
## entity's targetname), "name" (this one's), or "bool" ("1" or absent). Defaults are the spawn
## functions' (g_mover.c, g_trigger.c, g_target.c, g_items.c, g_client.c). "*item" covers every pickup.
const KEY_SPECS := {
	"*item": [["team", "text", ""], ["wait", "num", 0.0], ["random", "num", 0.0], ["count", "num", 0.0], ["target", "target", ""],
			["targetname", "name", ""], ["notfree", "bool", ""], ["notteam", "bool", ""], ["notsingle", "bool", ""]],
	"info_player_deathmatch": [["target", "target", ""], ["nobots", "bool", ""], ["nohumans", "bool", ""]],
	"info_player_start": [["target", "target", ""]],
	"info_player_intermission": [["target", "target", ""]],
	"info_notnull": [["targetname", "name", ""]],
	"func_door": [["speed", "num", 400.0], ["wait", "num", 2.0], ["lip", "num", 8.0], ["dmg", "num", 2.0], ["health", "num", 0.0],
			["team", "text", ""], ["targetname", "name", ""]],
	"func_button": [["target", "target", ""], ["speed", "num", 40.0], ["wait", "num", 1.0], ["lip", "num", 4.0], ["health", "num", 0.0]],
	"func_plat": [["speed", "num", 200.0], ["lip", "num", 8.0], ["height", "num", 0.0], ["dmg", "num", 2.0], ["targetname", "name", ""]],
	"func_bobbing": [["speed", "num", 4.0], ["height", "num", 32.0], ["phase", "num", 0.0], ["dmg", "num", 2.0]],
	"func_pendulum": [["speed", "num", 30.0], ["phase", "num", 0.0], ["dmg", "num", 2.0]],
	"func_rotating": [["speed", "num", 100.0], ["dmg", "num", 2.0]],
	"func_train": [["target", "target", ""], ["speed", "num", 100.0], ["dmg", "num", 2.0]],
	"func_static": [["targetname", "name", ""]],
	"func_timer": [["target", "target", ""], ["targetname", "name", ""], ["wait", "num", 1.0], ["random", "num", 1.0]],
	"trigger_multiple": [["target", "target", ""], ["wait", "num", 0.5], ["random", "num", 0.0]],
	"trigger_hurt": [["dmg", "num", 5.0], ["targetname", "name", ""]],
	"trigger_push": [["target", "target", ""]],
	"trigger_teleport": [["target", "target", ""]],
	"trigger_always": [["target", "target", ""]],
	"target_push": [["target", "target", ""], ["targetname", "name", ""], ["speed", "num", 1000.0]],
	"target_teleporter": [["target", "target", ""], ["targetname", "name", ""]],
	"target_position": [["targetname", "name", ""]],
	"misc_teleporter_dest": [["targetname", "name", ""]],
	"target_delay": [["target", "target", ""], ["targetname", "name", ""], ["wait", "num", 1.0], ["random", "num", 0.0]],
	"target_relay": [["target", "target", ""], ["targetname", "name", ""]],
	"target_print": [["message", "text", ""], ["targetname", "name", ""]],
	"target_give": [["target", "target", ""], ["targetname", "name", ""]],
	"target_score": [["count", "num", 1.0], ["targetname", "name", ""]],
	"target_kill": [["targetname", "name", ""]],
	"target_laser": [["target", "target", ""], ["targetname", "name", ""], ["dmg", "num", 100.0]],
	"target_location": [["message", "text", ""]],
	"target_speaker": [["targetname", "name", ""], ["wait", "num", 0.0], ["random", "num", 0.0]],
	"target_remove_powerups": [["targetname", "name", ""]],
	"shooter_rocket": [["target", "target", ""], ["targetname", "name", ""], ["random", "num", 0.0]],
	"shooter_plasma": [["target", "target", ""], ["targetname", "name", ""], ["random", "num", 0.0]],
	"shooter_grenade": [["target", "target", ""], ["targetname", "name", ""], ["random", "num", 0.0]],
	"misc_model": [["model", "text", ""], ["modelscale", "num", 1.0]],
	"misc_portal_surface": [["target", "target", ""]],
	"misc_portal_camera": [["targetname", "name", ""], ["roll", "num", 0.0]],
	"light": [["target", "target", ""], ["targetname", "name", ""]],
}

## spawnflags bits by class, as the inspector names them: [[bit, label], ...].
const FLAGS := {
	"*item": [[1, "Suspended (floats in the air)"]],
	"info_player_deathmatch": [[1, "Initial (the first spawn)"]],
	"func_door": [[1, "Start open"], [4, "Crusher"]],
	"func_bobbing": [[1, "X axis"], [2, "Y axis"]],
	"func_rotating": [[4, "X axis"], [8, "Y axis"]],
	"func_train": [[1, "Start on"], [2, "Toggle"], [4, "Block stops"]],
	"func_timer": [[1, "Start on"]],
	"trigger_hurt": [[1, "Start off"], [4, "Silent"], [8, "No protection"], [16, "Slow"]],
	"trigger_multiple": [[1, "Red team only"], [2, "Blue team only"]],
	"target_push": [[1, "Bounce pad sound"]],
	"target_speaker": [[1, "Looping, on"], [2, "Looping, off"], [4, "Global"], [8, "Activator only"]],
	"target_relay": [[1, "Red team only"], [2, "Blue team only"], [4, "One at random"]],
	"target_print": [[1, "Red team"], [2, "Blue team"], [4, "Private"]],
	"target_laser": [[1, "Start on"]],
	"misc_portal_camera": [[1, "Slow rotate"], [2, "Fast rotate"], [4, "Swing"]],
	"light": [[1, "Linear falloff"], [2, "No angle"]],
}

static var _sounds: PackedStringArray = []


## The class's typed fields and flags (KEY_SPECS, FLAGS), pickups sharing one set.
static func key_specs(cn: String) -> Array:
	return KEY_SPECS.get("*item" if is_item(cn) else cn, [])


static func flags(cn: String) -> Array:
	return FLAGS.get("*item" if is_item(cn) else cn, [])


# ---------------------------------------------------------------- classes

## Brush entities own brushes (doors, triggers); everything else is a point with an origin.
static func is_brush_class(cn: String) -> bool:
	if cn in ["func_timer", "trigger_always"]:   # g_spawn.c: these have no model
		return false
	return cn.begins_with("func_") or cn.begins_with("trigger_")


static func is_item(cn: String) -> bool:
	return Q3.entities.has(cn)


## The class's category: an item's type ("weapon", "ammo" ...) or a map class's category.
static func category(cn: String) -> String:
	if Q3.entities.has(cn):
		return String(Q3.entities[cn].get("type", "item"))
	return String(Q3.map_entities.get(cn, {}).get("category", "logic"))


static func description(cn: String) -> String:
	if Q3.entities.has(cn):
		return String(Q3.entities[cn].get("name", cn))
	return String(Q3.map_entities.get(cn, {}).get("description", "Not one of Quake 3's classes"))


static func color(cn: String) -> Color:
	return COLORS["item"] if Q3.entities.has(cn) else COLORS.get(category(cn), Color(0.8, 0.8, 0.8))


## The box a point entity takes around its origin (Radiant's sizes): players 32 x 32 x 56 from 24
## below the origin, items 32 cubed, lights and markers 16.
static func bounds(cn: String) -> AABB:
	if category(cn) == "spawn" or cn == "misc_teleporter_dest":
		return AABB(Vector3(-16, -16, -24), Vector3(32, 32, 56))
	if Q3.entities.has(cn) or cn in ["misc_model", "info_player_intermission"]:
		return AABB(Vector3(-16, -16, -16), Vector3(32, 32, 32))
	return AABB(Vector3(-8, -8, -8), Vector3(16, 16, 16))


## Classes that face somewhere ("angle" is their yaw).
static func faces(cn: String) -> bool:
	return category(cn) == "spawn" or cn in ["misc_teleporter_dest", "misc_model", "info_player_intermission"]


## Every class the Entities tab offers, by section: [[label, [classname ...]], ...].
static func catalog() -> Array:
	var out := []
	for s in SECTIONS:
		var kinds := String(s[1]).split("|")
		var names := []
		for cn in Q3.entities:
			if String(Q3.entities[cn].get("type", "")) in kinds:
				names.append(cn)
		for cn in Q3.map_entities:
			if cn != "worldspawn" and String(Q3.map_entities[cn].get("category", "")) in kinds:
				names.append(cn)
		if names.size():
			out.append([s[0], names])
	return out


## The weapon tool's list (I) and the item tool's (O): health, armor, ammo, powerups, holdables.
static func weapons() -> PackedStringArray:
	return _items_of(["weapon"])


static func items() -> PackedStringArray:
	return _items_of(["health", "armor", "ammo", "powerup", "holdable"])


static func _items_of(types: Array) -> PackedStringArray:
	var out := PackedStringArray()
	for cn in Q3.entities:
		if String(Q3.entities[cn].get("type", "")) in types:
			out.append(cn)
	return out


## The next (step 1) or previous (-1) class in whichever pickup list cn is in.
static func cycle(cn: String, step: int) -> String:
	for list in [weapons(), items()]:
		var i: int = list.find(cn)
		if i >= 0:
			return list[posmod(i + step, list.size())]
	return cn


# ---------------------------------------------------------------- keys

static func origin(e: Dictionary) -> Vector3:
	return vec(String(e.get("keys", {}).get("origin", "0 0 0")))


static func vec(s: String) -> Vector3:
	var p := s.split(" ", false)
	return Vector3(float(p[0]), float(p[1]), float(p[2])) if p.size() == 3 else Vector3.ZERO


static func vec_key(v: Vector3) -> String:
	return "%s %s %s" % [Q3MapEditor._num(v.x), Q3MapEditor._num(v.y), Q3MapEditor._num(v.z)]


static func angle(e: Dictionary) -> float:
	var k: Dictionary = e.get("keys", {})
	if k.has("angles"):
		return vec(String(k["angles"])).y
	return float(k.get("angle", "0"))


## A point entity: {type: "entity", classname, keys} with the origin and any other keys.
static func point(cn: String, at: Vector3, keys := {}) -> Dictionary:
	var k := {"origin": vec_key(at)}
	k.merge(keys, true)
	return {"type": "entity", "id": 0, "group": 0, "classname": cn, "keys": k}


static func brush_entity(cn: String, keys := {}) -> Dictionary:
	return {"type": "entity", "id": 0, "group": 0, "classname": cn, "keys": keys.duplicate()}


## Every targetname in the map: name -> [entity ids].
static func targetnames(doc: Q3MapDoc) -> Dictionary:
	var out := {}
	for id in doc.objects:
		var o: Dictionary = doc.objects[id]
		if o["type"] == "entity" and o["keys"].has("targetname"):
			var n := String(o["keys"]["targetname"])
			if not out.has(n):
				out[n] = []
			out[n].append(id)
	return out


## prefix + the first number nobody uses (and that isn't in taken).
static func unique_name(doc: Q3MapDoc, prefix: String, taken := {}) -> String:
	var used := targetnames(doc)
	var n := 1
	while used.has(prefix + str(n)) or taken.has(prefix + str(n)):
		n += 1
	return prefix + str(n)


## The entities a source entity's "target" names.
static func targets_of(doc: Q3MapDoc, e: Dictionary, names := {}) -> Array:
	var t := String(e.get("keys", {}).get("target", ""))
	if t == "":
		return []
	if names.is_empty():
		names = targetnames(doc)
	return names.get(t, [])


## The brushes and patches a brush entity owns: entity id -> [ids].
static func owned(doc: Q3MapDoc) -> Dictionary:
	var out := {}
	for id in doc.objects:
		var e := int(doc.objects[id].get("entity", 0))
		if e != 0:
			if not out.has(e):
				out[e] = []
			out[e].append(id)
	return out


## Where a brush entity is: the middle of its brushes' bounds (q3's absmin + absmax over two), or
## a point entity's origin.
static func center(doc: Q3MapDoc, e: Dictionary, own := {}) -> Vector3:
	if not is_brush_class(String(e.get("classname", ""))) or e["keys"].has("origin"):
		return origin(e)
	if own.is_empty():
		own = owned(doc)
	var ids: Array = own.get(e["id"], [])
	if ids.is_empty():
		return origin(e)
	return Q3MapEditor.bounds_in(doc, ids).get_center()


static func gravity(doc: Q3MapDoc) -> float:
	var g := float(doc.worldspawn.get("gravity", str(DEFAULT_GRAVITY)))
	return g if g > 0 else DEFAULT_GRAVITY


# ---------------------------------------------------------------- id's numbers

## g_trigger.c AimAtTarget: a trigger_push throws the player from its center so that the target is
## the top of the arc. The launch velocity (units/s), or Vector3.ZERO when the target isn't above.
static func push_velocity(from: Vector3, target: Vector3, g: float) -> Vector3:
	var height := target.z - from.z
	if height <= 0.0:
		return Vector3.ZERO
	var time := sqrt(height / (0.5 * g))
	var flat := Vector3(target.x - from.x, target.y - from.y, 0)
	var dist := flat.length()
	var v := flat / dist * (dist / time) if dist > 0.0 else Vector3.ZERO
	v.z = time * g
	return v


## Where to put a pad's target so the arc it makes comes down on `landing`: the top of the arc
## clears the higher end by 64 units plus a third of the distance, and sits where the climb and the
## fall split the horizontal distance by their times.
## `ceiling` keeps the top 40 units under a roof (a target outside the map's hull is a leak).
static func apex_for(from: Vector3, landing: Vector3, g: float, ceiling := INF) -> Vector3:
	var flat := Vector2(landing.x - from.x, landing.y - from.y)
	var top := maxf(from.z, landing.z) + 64.0 + flat.length() / 3.0
	top = maxf(minf(top, ceiling - 40.0), maxf(from.z, landing.z) + 16.0)
	var up := sqrt(2.0 * (top - from.z) / g)
	var down := sqrt(2.0 * (top - landing.z) / g)
	var f := up / (up + down)
	return Vector3(from.x + flat.x * f, from.y + flat.y * f, top)


## The target for a jump pad standing on the floor at `pad` (the tools' 64-unit pad: a 4-unit bounce
## pad under a 36-unit trigger) so that a player launched from it lands on the floor at `landing`.
## AimAtTarget aims from the trigger's center (18 up), but the player's origin starts 10 higher
## (24 above the pad's top) and comes down 24 above the floor, so the trigger's arc is solved down
## to 14 above the landing floor.
static func pad_apex(pad: Vector3, landing: Vector3, g: float, ceiling := INF) -> Vector3:
	return apex_for(pad + Vector3(0, 0, 18), landing + Vector3(0, 0, 14), g, ceiling)


## q3map's point light as tools/build_maps.py turns it into a Godot light: energy E with E / d^2 at
## d meters (photons / 255 / 32^2) and a range where it fades out. {energy, range, color}
static func light_params(keys: Dictionary) -> Dictionary:
	var intensity := float(keys.get("light", keys.get("_light", "300")))
	var photons := absf(intensity) * POINT_PHOTONS
	var c := vec(String(keys.get("_color", "1 1 1")))
	var m := maxf(c.x, maxf(c.y, c.z))
	var col := Color(c.x / m, c.y / m, c.z / m) if m > 0 else Color.WHITE
	return {"energy": photons / 255.0 * UNIT * UNIT * (1.0 if intensity >= 0 else 0.0),
			"range": minf(sqrt(photons) * UNIT, 96.0), "color": col}


## World sounds for target_speaker ("sound/world/..."), from the library.
static func sounds() -> PackedStringArray:
	if _sounds.is_empty():
		_scan_sounds("world/")
		_sounds.sort()
	return _sounds


static func _scan_sounds(rel: String) -> void:
	var d := DirAccess.open(SOUNDS_DIR + rel)
	if d == null:
		return
	for f in d.get_files():
		# an exported game lists the import remaps, not the .wav itself
		var n := f.trim_suffix(".import").trim_suffix(".remap")
		if n.get_extension() in ["wav", "ogg"] and not _sounds.has("sound/" + rel + n):
			_sounds.append("sound/" + rel + n)
	for sub in d.get_directories():
		_scan_sounds(rel + sub + "/")


## The library stream for an id sound path ("sound/world/x.wav"), or null.
static func sound_stream(path: String) -> AudioStream:
	var p := SOUNDS_DIR + path.trim_prefix("sound/")
	return load(p) as AudioStream if ResourceLoader.exists(p) else null
