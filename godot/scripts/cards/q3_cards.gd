class_name Q3Cards
extends RefCounted
## The player card catalog (library/cards/cards.json, written by tools/build_cards.py from
## tools/card_catalog.py) and the rules that read it. Ported from trac3r's cosmetics.ts and
## achievements.ts:
##   - every part is data with exactly one unlock rule (see card_catalog.py for the rule kinds)
##   - achievements are computed from career stats, never stored as flags, so they can't drift
##   - enforce_unlocks() puts any part a profile hasn't earned back to the default before a card
##     is shown to anyone, so a hand-edited profile can't show locked parts
## A profile here is {"level": int, "stats": {career stats}, "campaign": {map: best skill won}}.
## Stats use dotted keys into nested dictionaries: "wins_map.q3dm17", "frags_weapon.railgun".

const PATH := "res://library/cards/cards.json"
## card slot -> part list
const SLOTS := {"banner": "banners", "emblem": "emblems", "frame": "frames", "title": "titles",
	"accent": "accents", "taunt": "taunts", "rail": "rails"}
const SHOWCASE := 3   ## stats and badges on a card

static var _cat: Dictionary = {}
static var _parts: Dictionary = {}   ## kind -> {id: part}
static var _achs: Dictionary = {}    ## id -> achievement


static func catalog() -> Dictionary:
	if _cat.is_empty():
		var d: Variant = Q3.load_json(PATH) if FileAccess.file_exists(PATH) else {}
		_cat = d if d is Dictionary else {}
		for kind in _cat.get("parts", {}):
			var m := {}
			for p in _cat["parts"][kind]:
				m[p["id"]] = p
			_parts[kind] = m
		for a in _cat.get("achievements", []):
			_achs[a["id"]] = a
	return _cat


static func parts(kind: String) -> Array:
	return catalog().get("parts", {}).get(kind, [])


static func part(kind: String, id: String) -> Dictionary:
	catalog()
	return _parts.get(kind, {}).get(id, {})


static func achievements() -> Array:
	return catalog().get("achievements", [])


static func achievement(id: String) -> Dictionary:
	catalog()
	return _achs.get(id, {})


static func default_card() -> Dictionary:
	return catalog().get("default_card", {}).duplicate(true)


static func new_profile() -> Dictionary:
	return {"level": 1, "xp": 0, "stats": {}, "campaign": {}}


# ---------------------------------------------------------------- stats

## A career stat by dotted key; a missing stat is 0.
static func stat(stats: Dictionary, key: String) -> float:
	var v: Variant = stats
	for k in key.split("."):
		if not v is Dictionary or not (v as Dictionary).has(k):
			return 0.0
		v = v[k]
	return float(v) if (v is int or v is float) else 0.0


static func _dict_stat(stats: Dictionary, key: String) -> Dictionary:
	var v: Variant = stats
	for k in key.split("."):
		if not v is Dictionary or not (v as Dictionary).has(k):
			return {}
		v = v[k]
	return v if v is Dictionary else {}


static func _campaign_count(profile: Dictionary, arenas: Array, skill: int) -> int:
	var won: Dictionary = profile.get("campaign", {})
	return arenas.filter(func(m): return int(won.get(m, 0)) >= skill).size()


# ---------------------------------------------------------------- achievements

## How far along an achievement is (compare with its "target").
static func progress(a: Dictionary, profile: Dictionary) -> float:
	var p: Dictionary = a.get("progress", {})
	var stats: Dictionary = profile.get("stats", {})
	if p.has("stat"):
		return stat(stats, p["stat"])
	if p.has("set"):
		var d := _dict_stat(stats, p["set"])
		var need := float(p.get("min", 1))
		return float(Array(p["of"]).filter(func(k): return float(d.get(k, 0)) >= need).size())
	if p.has("max"):
		var best := 0.0
		for v in _dict_stat(stats, p["max"]).values():
			best = maxf(best, float(v))
		return best
	if p.has("campaign"):
		return float(_campaign_count(profile, p["campaign"], int(p.get("skill", 1))))
	return 0.0


static func is_achieved(a: Dictionary, profile: Dictionary) -> bool:
	return progress(a, profile) >= float(a.get("target", 1))


## Collections: the keys still needed, so the last one isn't a mystery (trac3r's `missing`).
static func missing(a: Dictionary, profile: Dictionary) -> PackedStringArray:
	var p: Dictionary = a.get("progress", {})
	var out: PackedStringArray = []
	if p.has("set"):
		var d := _dict_stat(profile.get("stats", {}), p["set"])
		for k in p["of"]:
			if float(d.get(k, 0)) < float(p.get("min", 1)):
				out.append(k)
	elif p.has("campaign"):
		var won: Dictionary = profile.get("campaign", {})
		for m in p["campaign"]:
			if int(won.get(m, 0)) < int(p.get("skill", 1)):
				out.append(m)
	return out


## Every achievement this profile holds, as a set (id -> true).
static func achieved(profile: Dictionary) -> Dictionary:
	var out := {}
	for a in achievements():
		if is_achieved(a, profile):
			out[a["id"]] = true
	return out


# ---------------------------------------------------------------- unlocks

static func is_unlocked(rule: Dictionary, profile: Dictionary, done: Variant = null) -> bool:
	if rule.get("free", false):
		return true
	if rule.has("level"):
		return int(profile.get("level", 1)) >= int(rule["level"])
	if rule.has("achievement"):
		var held: Dictionary = done if done is Dictionary else achieved(profile)
		return held.has(rule["achievement"])
	if rule.has("stat"):
		return stat(profile.get("stats", {}), rule["stat"]) >= float(rule.get("min", 1))
	if rule.has("campaign"):
		return _campaign_count(profile, rule["campaign"], int(rule.get("skill", 1))) >= Array(rule["campaign"]).size()
	if rule.has("any"):
		for r in rule["any"]:
			if is_unlocked(r, profile, done):
				return true
	return false


## What unlocks a part, for the locker: "LEVEL 40", "ACHIEVEMENT: RAIL GOD", or the part's own text.
static func unlock_text(p: Dictionary) -> String:
	var rule: Dictionary = p.get("unlock", {})
	if rule.get("free", false):
		return "FREE"
	if rule.has("level"):
		return "LEVEL %d" % int(rule["level"])
	if rule.has("achievement"):
		return "ACHIEVEMENT: %s" % String(achievement(rule["achievement"]).get("name", rule["achievement"])).to_upper()
	return String(p.get("desc", "")).to_upper()


## The card as anyone else may see it: every part the profile hasn't earned goes back to the
## default, unknown stats and badges are dropped (trac3r's enforceUnlocks).
static func enforce_unlocks(card: Dictionary, profile: Dictionary) -> Dictionary:
	var def := default_card()
	var done := achieved(profile)
	var out := def.duplicate(true)
	for slot in SLOTS:
		var p := part(SLOTS[slot], String(card.get(slot, "")))
		if not p.is_empty() and is_unlocked(p["unlock"], profile, done):
			out[slot] = p["id"]
	var ids := (catalog().get("card_stats", []) as Array).map(func(s): return s["id"])
	var stats: Array = Array(card.get("stats", [])).filter(func(s): return ids.has(s))
	out["stats"] = stats.slice(0, SHOWCASE) if stats.size() else def["stats"]
	out["badges"] = Array(card.get("badges", [])).filter(func(b): return done.has(b)).slice(0, SHOWCASE)
	return out


# ---------------------------------------------------------------- levels and showcase

static func xp_to_next(level: int) -> int:
	var l: Dictionary = catalog().get("levels", {})
	return int(l.get("xp_base", 1000)) + int(l.get("xp_step", 25)) * (level - 1)


## Level and the XP into it, from total career XP.
static func level_for(total_xp: int) -> Dictionary:
	var top := int(catalog().get("levels", {}).get("max", 100))
	var level := 1
	var left := total_xp
	while level < top and left >= xp_to_next(level):
		left -= xp_to_next(level)
		level += 1
	return {"level": level, "xp": left, "next": xp_to_next(level)}


## A showcase stat as the card prints it.
static func stat_value(id: String, profile: Dictionary) -> String:
	var s: Dictionary = {}
	for e in catalog().get("card_stats", []):
		if e["id"] == id:
			s = e
	var how := String(s.get("value", ""))
	var stats: Dictionary = profile.get("stats", {})
	match how:
		"=kd":
			return "%.2f" % (stat(stats, "frags") / maxf(stat(stats, "deaths"), 1.0))
		"=arenas_won":
			return str(_dict_stat(stats, "wins_map").values().filter(func(v): return float(v) >= 1.0).size())
		"=achievements":
			return "%d/%d" % [achieved(profile).size(), achievements().size()]
		"=hours":
			return str(int(stat(stats, "playtime") / 3600.0))
		"best.speed":
			return "%d UPS" % int(stat(stats, how))
		"best.rail_accuracy":
			return "%d%%" % int(stat(stats, how))
		"":
			return "-"
	return str(int(stat(stats, how)))
