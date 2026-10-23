class_name Q3Profile
extends RefCounted
## The player's career: total XP (level from Q3Cards.level_for), career stats in the shape
## Q3Cards' achievements read ("frags", "wins", "wins_map.q3dm17", "frags_weapon.railgun" ...),
## and the card they show. Stored in user://profile.json. Matches award XP through award_match().
##
## XP for a match (shown line by line on the results screen):
##   finished the match 100, each frag 10, a win 150 (campaign wins x skill / 3),
##   each Impressive / Excellent / Gauntlet / Defend / Assist medal 25, a Perfect 100.

const PATH := "user://profile.json"
## Ranked opens at this level (Ozmium, 2026-10-22). The online hub must check it too once ranked
## queues exist; the menu only shows it.
const RANKED_LEVEL := 30
const XP := {"finished": 100, "frag": 10, "win": 150, "medal": 25, "perfect": 100}

static var _data: Dictionary = {}


static func data() -> Dictionary:
	if _data.is_empty():
		var d: Variant = Q3.load_json(PATH) if FileAccess.file_exists(PATH) else null
		_data = d if d is Dictionary else Q3Cards.new_profile()
		_data["xp"] = int(_data.get("xp", 0))
		if not _data.has("card"):
			_data["card"] = Q3Cards.default_card()
	return _data


static func save() -> void:
	var f := FileAccess.open(PATH, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(data(), " "))


static func total_xp() -> int:
	return int(data()["xp"])


static func level() -> Dictionary:
	return Q3Cards.level_for(total_xp())


## The card to show for the local player, with any part they haven't earned put back to default.
static func card() -> Dictionary:
	return Q3Cards.enforce_unlocks(data()["card"], data())


## Adds one match to the career. `r`: {map, mode, won, frags, deaths, medals: {impressive, ...},
## perfect, campaign_skill (0 if not campaign), extra: [[label, xp]] (the campaign's progress XP,
## added after the match's own)}. Returns {lines: [[label, xp]], before, after}.
static func award_match(r: Dictionary) -> Dictionary:
	var d := data()
	var lines: Array = []
	lines.append(["Match complete", XP["finished"]])
	var frags := int(r.get("frags", 0))
	if frags > 0:
		lines.append(["%d frags" % frags, frags * XP["frag"]])
	if r.get("won", false):
		var skill := int(r.get("campaign_skill", 0))
		lines.append(["Victory" + (" (skill %d)" % skill if skill > 0 else ""),
			int(XP["win"] * (maxf(skill, 3.0) / 3.0 if skill > 0 else 1.0))])
	for m in r.get("medals", {}):
		var n := int(r["medals"][m])
		if n > 0:
			lines.append(["%s x%d" % [String(m).capitalize(), n], n * XP["medal"]])
	if r.get("perfect", false):
		lines.append(["Perfect", XP["perfect"]])
	lines.append_array(r.get("extra", []))
	var gain := 0
	for l in lines:
		gain += int(l[1])
	var before := total_xp()
	d["xp"] = before + gain
	var st: Dictionary = d["stats"]
	_add(st, "matches", 1)
	_add(st, "frags", frags)
	_add(st, "deaths", int(r.get("deaths", 0)))
	if r.get("won", false):
		_add(st, "wins", 1)
		_add_in(st, "wins_map", String(r.get("map", "")), 1)
	for m in r.get("medals", {}):
		_add(st, String(m) + "s", int(r["medals"][m]))
	if r.get("perfect", false):
		_add(st, "perfects", 1)
	d["level"] = int(Q3Cards.level_for(d["xp"])["level"])
	save()
	return {"lines": lines, "gain": gain, "before": before, "after": d["xp"]}


static func _add(st: Dictionary, k: String, n: int) -> void:
	st[k] = int(st.get(k, 0)) + n


static func _add_in(st: Dictionary, k: String, sub: String, n: int) -> void:
	if not st.has(k) or not st[k] is Dictionary:
		st[k] = {}
	st[k][sub] = int(st[k].get(sub, 0)) + n


## A card for a bot: its own head as the emblem, a banner and title picked from its name, and a
## level that reads its skill, so every bot looks like a person on the scoreboard.
static func bot_card(name: String, model: String, skill: int) -> Dictionary:
	var c := Q3Cards.default_card()
	var h := absi(name.hash())
	var emblem := "head_" + model
	if not Q3Cards.part("emblems", emblem).is_empty():
		c["emblem"] = emblem
	var banners: Array = Q3Cards.parts("banners")
	if banners.size():
		c["banner"] = banners[h % banners.size()]["id"]
	var titles: Array = Q3Cards.parts("titles")
	if titles.size():
		c["title"] = titles[(h / 7) % titles.size()]["id"]
	c["level"] = clampi(skill * 12 + h % 9, 1, 100)
	return c
