class_name Q3Campaign
extends RefCounted
## Quake III Arena's single-player campaign (q3_ui ui_splevel.c / ui_gameinfo.c), as data:
## the "single" arenas of scripts/arenas.txt in file order. The Training arena comes first, the
## Final arena last, and the rest form tiers of four. As in UI_GetCurrentGame, the campaign goes in
## order: Training must be won first, and a tier opens once every arena before it is won (first
## place, ties included, at any skill); the Final opens after the last tier.
## Each arena is a match setup like any other: Free For All (Tournament on q3tourney maps) against
## the arena's own bots at the chosen skill, with its frag limit.
## Progress lives in user://campaign.json. Gameplay reports a result with record_result().

const PATH := "user://campaign.json"
const TIER_SIZE := 4
## q3_ui's single-player awards (ui_sppostgame.c), in its order
const AWARDS := ["accuracy", "impressive", "excellent", "gauntlet", "frags", "perfect"]
## Career XP for campaign progress (Ozmium, 2026-10-22), on top of the match's own XP: the first win
## of an arena (times skill / 3), a win at a higher skill than before, a finished tier of four,
## and the whole campaign the first time the Final is won.
const XP := {"arena": 250, "skill_up": 100, "tier": 1000, "campaign": 5000}

var path := PATH            ## the self-test points this elsewhere
var tiers: Array = []        ## [{name, arenas: [map info + "tier"]}]
var progress := {"skill": 3, "won": {}, "awards": {}}   ## won: map -> best skill won at
var last_result: Dictionary = {}   ## what the last record_result() changed: first_win, skill_up, tier, campaign


func _init() -> void:
	var single: Array = Session.maps.filter(func(m): return m.get("types", []).has("single"))
	var training := single.filter(func(m): return String(m.get("special", "")).to_lower() == "training")
	var final := single.filter(func(m): return String(m.get("special", "")).to_lower() == "final")
	var rest := single.filter(func(m): return String(m.get("special", "")) == "")
	if training.size():
		tiers.append({"name": "Training", "arenas": training})
	for i in range(0, rest.size(), TIER_SIZE):
		tiers.append({"name": "Tier %d" % (i / TIER_SIZE + 1), "arenas": rest.slice(i, i + TIER_SIZE)})
	if final.size():
		tiers.append({"name": "Final", "arenas": final})
	load_progress()


func load_progress() -> void:
	var d: Variant = Q3.load_json(path) if FileAccess.file_exists(path) else {}
	if d is Dictionary:
		progress.merge(d, true)


func save() -> void:
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(progress, " "))


func reset() -> void:
	progress = {"skill": progress.get("skill", 3), "won": {}, "awards": {}}
	save()


func is_won(map: String) -> bool:
	return progress["won"].has(map)


func tier_complete(i: int) -> bool:
	return tiers[i]["arenas"].all(func(m): return is_won(m["map"]))


## q3_ui's level select: everything up to the first arena not yet won is open, so Training first,
## then each tier once every tier before it is complete.
func tier_open(i: int) -> bool:
	for t in i:
		if not tier_complete(t):
			return false
	return true


func arena_tier(map: String) -> int:
	for i in tiers.size():
		for m in tiers[i]["arenas"]:
			if m["map"] == map:
				return i
	return -1


## The match setup for an arena at a skill (1-5).
func setup_for(map: String, skill: int) -> Q3MatchSetup:
	var info := Session.map_info(map)
	var tourney := map.begins_with("q3tourney")
	var s := Session.new_setup("duel" if tourney else "ffa", map)
	s.source = "campaign"
	s.training = "campaign"
	if int(info.get("fraglimit", 0)) > 0:
		s.rules = {"fraglimit": int(info["fraglimit"]), "timelimit": 0}
	for name in info.get("bots", []):
		var bot := _bot(String(name))
		if bot != "":
			s.bots.append({"name": bot, "skill": clampi(skill, 1, 5), "team": ""})
	return s


func _bot(name: String) -> String:
	for b in Session.bots:
		if String(b["name"]).to_lower() == name.to_lower():
			return b["name"]
	return ""


## Gameplay calls this when a campaign match ends. Returns the cinematic the win unlocks, the way
## q3_ui plays one when a tier is finished: tier1 after Training ... tier7 after the last tier,
## the ending after the final. Empty when nothing new.
## `awards` are ui_sppostgame.c's: accuracy (1 when 50% or better), impressive, excellent, gauntlet,
## frags (the match score; every 100 in the career is a medal), perfect. last_result says what
## changed, for the campaign XP.
func record_result(map: String, skill: int, won: bool, awards := {}) -> String:
	var tier := arena_tier(map)
	var was_complete := tier >= 0 and tier_complete(tier)
	var before := int(progress["won"].get(map, 0))
	last_result = {"first_win": won and before == 0, "skill_up": won and before > 0 and skill > before, "skill": skill,
		"tier": "", "campaign": false}
	if won:
		progress["won"][map] = maxi(before, skill)
	for k in awards:
		progress["awards"][k] = int(progress["awards"].get(k, 0)) + int(awards[k])
	save()
	if tier < 0 or was_complete or not tier_complete(tier):
		return ""
	last_result["tier"] = String(tiers[tier]["name"])
	last_result["campaign"] = tier == tiers.size() - 1
	return "end" if tier == tiers.size() - 1 else "tier%d" % (tier + 1)


## The campaign XP lines for last_result: [[label, xp], ...].
func xp_lines() -> Array:
	var r := last_result
	var out: Array = []
	if r.get("first_win", false):
		out.append(["Arena cleared", int(XP["arena"] * maxf(float(r.get("skill", 3)), 1.0) / 3.0)])
	if r.get("skill_up", false):
		out.append(["Cleared at a harder skill", int(XP["skill_up"])])
	if String(r.get("tier", "")) not in ["", "Training"] and not r.get("campaign", false):
		out.append(["%s complete" % r["tier"], int(XP["tier"])])
	if r.get("campaign", false):
		out.append(["Campaign complete", int(XP["campaign"])])
	return out


## Everything the web menu's Single Player page draws.
func to_dict() -> Dictionary:
	var out: Array = []
	for i in tiers.size():
		var arenas: Array = []
		for m in tiers[i]["arenas"]:
			var bots: Array = []
			for name in m.get("bots", []):
				var full := _bot(String(name))
				var b: Dictionary = {}
				for x in Session.bots:
					if x["name"] == full:
						b = x
				bots.append({"name": full if full != "" else String(name).capitalize(), "model": b.get("model", "")})
			arenas.append({"map": m["map"], "name": m.get("name", m["map"]), "levelshot": m.get("levelshot", ""),
				"fraglimit": m.get("fraglimit", 0), "bots": bots, "won": int(progress["won"].get(m["map"], 0))})
		out.append({"name": tiers[i]["name"], "open": tier_open(i), "complete": tier_complete(i), "arenas": arenas})
	return {"tiers": out, "skill": int(progress.get("skill", 3)), "awards": progress.get("awards", {}), "award_names": AWARDS}
