class_name Q3MatchStats
extends RefCounted
## Your match so far, for the scoreboard's left panel (Ozmium, 2026-10-23): damage and accuracy
## per weapon, distance traveled and the medals.
##   Shots: every EV_FIRE_WEAPON of yours, as g_weapon.c FireWeapon counts accuracy_shots (the
##   gauntlet and grapple aren't counted).
##   Hits and damage: the game's "dmg <target> <amount> <armor> <means> <time>" (q3core's damage
##   numbers patch). Hits count once per weapon per server frame, as id's accuracy_hits counts a
##   shot once however many pellets or splash victims it hurt.
##   Distance: your origin from frame to frame while alive, without teleports or respawns.
##   Medals: your playerState's award counts.

const E := preload("res://scripts/game/q3_enums.gd")
const EF_TELEPORT_BIT := 4
## bg_public.h meansOfDeath_t -> the weapon that did it
const MOD_WEAPON := {E.MOD_SHOTGUN: E.WP_SHOTGUN, E.MOD_GAUNTLET: E.WP_GAUNTLET, E.MOD_MACHINEGUN: E.WP_MACHINEGUN,
	E.MOD_GRENADE: E.WP_GRENADE_LAUNCHER, E.MOD_GRENADE_SPLASH: E.WP_GRENADE_LAUNCHER, E.MOD_ROCKET: E.WP_ROCKET_LAUNCHER,
	E.MOD_ROCKET_SPLASH: E.WP_ROCKET_LAUNCHER, E.MOD_PLASMA: E.WP_PLASMAGUN, E.MOD_PLASMA_SPLASH: E.WP_PLASMAGUN,
	E.MOD_RAILGUN: E.WP_RAILGUN, E.MOD_LIGHTNING: E.WP_LIGHTNING, E.MOD_BFG: E.WP_BFG, E.MOD_BFG_SPLASH: E.WP_BFG,
	E.MOD_GRAPPLE: E.WP_GRAPPLING_HOOK}
const WEAPON_LABELS := ["", "Gauntlet", "Machinegun", "Shotgun", "Grenade L.", "Rocket L.", "Lightning", "Railgun",
	"Plasma Gun", "BFG10K", "Grapple"]
## the medals the panel lists: [label, persistant index, icon]
const MEDALS := [["Impressive", E.PERS_IMPRESSIVE_COUNT, "medal_impressive"], ["Excellent", E.PERS_EXCELLENT_COUNT, "medal_excellent"],
	["Humiliation", E.PERS_GAUNTLET_FRAG_COUNT, "medal_gauntlet"], ["Defend", E.PERS_DEFEND_COUNT, "medal_defend"],
	["Assist", E.PERS_ASSIST_COUNT, "medal_assist"], ["Capture", E.PERS_CAPTURES, "medal_capture"]]

var _shots: Dictionary = {}        ## weapon -> shots
var _hits: Dictionary = {}         ## weapon -> hits (shots that hurt someone)
var _damage: Dictionary = {}       ## weapon -> damage dealt (armor included)
var _hit_at: Dictionary = {}       ## weapon -> the server time of its last hit
var _distance := 0.0               ## id units
var _last := Vector3.INF
var _last_flags := 0


func reset() -> void:
	_shots.clear()
	_hits.clear()
	_damage.clear()
	_hit_at.clear()
	_distance = 0.0
	_last = Vector3.INF


func fired(weapon: int) -> void:
	if weapon > E.WP_NONE and weapon < E.WP_NUM_WEAPONS:
		_shots[weapon] = int(_shots.get(weapon, 0)) + 1


## A "dmg" command's numbers. Old commands without the means of death count for the weapon held.
func damaged(amount: int, means: int, time: int, held: int) -> void:
	var w: int = MOD_WEAPON.get(means, held if means < 0 else 0)
	if w <= 0:
		return   # telefrags, the world
	_damage[w] = int(_damage.get(w, 0)) + amount
	if time < 0 or int(_hit_at.get(w, -1)) != time:
		_hit_at[w] = time
		_hits[w] = int(_hits.get(w, 0)) + 1


## Every frame with your playerState: the distance you covered.
func moved(ps: Dictionary) -> void:
	if ps.is_empty():
		return
	var o: Vector3 = ps["origin"]
	var flags := int(ps["eFlags"])
	var alive := int(ps["pm_type"]) == E.PM_NORMAL
	if alive and _last != Vector3.INF and not ((flags ^ _last_flags) & EF_TELEPORT_BIT):
		var step := o.distance_to(_last)
		if step < 128.0:   # more is a teleport or a respawn
			_distance += step
	_last = o if alive else Vector3.INF
	_last_flags = flags


## What the panel shows: {weapons: [{weapon, name, shots, hits, damage, accuracy (-1: not
## counted)}], damage, accuracy, distance_m, frags, deaths, medals: [{name, icon, count}]}.
func summary(ps: Dictionary) -> Dictionary:
	var rows: Array = []
	var total_damage := 0
	var shots := 0
	var hits := 0
	for w in range(1, E.WP_NUM_WEAPONS):
		var s := int(_shots.get(w, 0))
		var h := int(_hits.get(w, 0))
		var d := int(_damage.get(w, 0))
		if s == 0 and d == 0:
			continue
		var counted := w != E.WP_GAUNTLET and w != E.WP_GRAPPLING_HOOK
		var acc := clampf(float(h) / float(s), 0.0, 1.0) if counted and s > 0 else -1.0
		rows.append({"weapon": w, "name": WEAPON_LABELS[w], "shots": s, "hits": h, "damage": d, "accuracy": acc})
		total_damage += d
		if counted:
			shots += s
			hits += mini(h, s)
	var medals: Array = []
	var pers: PackedInt32Array = ps.get("persistant", PackedInt32Array())
	for m in MEDALS:
		var n := int(pers[m[1]]) if pers.size() > int(m[1]) else 0
		medals.append({"name": m[0], "icon": m[2], "count": n})
	return {"weapons": rows, "damage": total_damage, "accuracy": float(hits) / float(shots) if shots > 0 else -1.0,
		"distance_m": _distance * Q3.UNIT, "frags": int(pers[E.PERS_SCORE]) if pers.size() else 0,
		"deaths": int(pers[E.PERS_KILLED]) if pers.size() else 0, "medals": medals}
