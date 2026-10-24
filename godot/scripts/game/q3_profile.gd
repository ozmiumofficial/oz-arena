class_name Q3Profile
extends RefCounted
## The player's career: total XP (level from Q3Cards.level_for), career stats in the shape
## Q3Cards' achievements read ("frags", "wins", "wins_map.q3dm17", "frags_weapon.railgun" ...),
## and the card they show. Stored in user://profile.json. Matches award XP through award_match().
##
## XP for a match (shown line by line on the results screen):
##   finished the match 100, each frag 10, a win 150 (campaign wins x skill / 3),
##   each Impressive / Excellent / Gauntlet / Defend / Assist medal 25, a Perfect 100.
## The menu's Profile page edits the card through set_card() / set_showcase(), which refuse parts
## the career hasn't earned, and draws everything from menu_payload().

const PATH := "user://profile.json"
## Ranked opens at this level (Ozmium, 2026-10-22). The online hub must check it too once ranked
## queues exist; the menu only shows it.
const RANKED_LEVEL := 30
const XP := {"finished": 100, "frag": 10, "win": 150, "medal": 25, "perfect": 100}
## Session's mode ids -> the short ids card_catalog.py's wins_mode stats use
const MODE_STAT := {"clan_arena": "ca", "freeze_tag": "ft"}
## Medals as the match reports them -> the stat under medal. (card_catalog.py MEDALS)
const MEDAL_STAT := {"captures": "capture"}

static var _data: Dictionary = {}


static func data() -> Dictionary:
	if _data.is_empty():
		var d: Variant = Q3.load_json(PATH) if FileAccess.file_exists(PATH) else null
		_data = d if d is Dictionary else Q3Cards.new_profile()
		_data["xp"] = int(_data.get("xp", 0))
		if not _data.has("card"):
			_data["card"] = Q3Cards.default_card()
		_migrate(_data)
	return _data


## Profiles written before 2026-10-23 kept medals as flat "excellents", "impressives" ... which no
## achievement or showcase stat reads; they move under medal.<award> as card_catalog.py names them.
static func _migrate(d: Dictionary) -> void:
	var st: Dictionary = d.get("stats", {})
	d["stats"] = st
	var old := {"impressives": "impressive", "excellents": "excellent", "gauntlets": "gauntlet", "defends": "defend",
		"assists": "assist", "capturess": "capture", "perfects": "perfect", "accuracys": "accuracy"}
	for k in old:
		if st.has(k):
			_add_in(st, "medal", old[k], int(st[k]))
			st.erase(k)
	st.erase("fragss")
	# the catalog's default rail was blue until 2026-10-23; Quake 3's own default (color1 4) is red
	if int(d.get("card_v", 1)) < 2:
		if d.get("card", {}).get("rail", "") == "rail_blue":
			d["card"]["rail"] = "rail_red"
		d["card_v"] = 2


static func save() -> void:
	var f := FileAccess.open(PATH, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(data(), " "))
	# the level leaderboard follows the career (Online is an autoload; tests may run without it)
	var tree := Engine.get_main_loop() as SceneTree
	var online: Node = tree.root.get_node_or_null("Online") if tree else null
	if online:
		online.career_changed()


static func total_xp() -> int:
	return int(data()["xp"])


static func level() -> Dictionary:
	return Q3Cards.level_for(total_xp())


## The card to show for the local player, with any part they haven't earned put back to default.
static func card() -> Dictionary:
	return Q3Cards.enforce_unlocks(data()["card"], data())


## Adds one match to the career. `r`: {map, mode, won, frags, deaths, medals: {impressive, ...},
## perfect, campaign_skill (0 if not campaign), model, skin, seconds, extra: [[label, xp]] (the
## campaign's progress XP, added after the match's own)}. Returns {lines: [[label, xp]], before,
## after, unlocked: [{kind, id, name}]}; the parts it unlocked also wait in "new" for the Profile page.
static func award_match(r: Dictionary) -> Dictionary:
	var d := data()
	var had := unlocked_ids()
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
		if n <= 0:
			continue
		if m == "frags":
			# the campaign's frags medal carries the milestone (100, 200 ...), not a count
			lines.append(["Frags medal (%d)" % n, XP["medal"]])
		else:
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
	var mode := String(r.get("mode", ""))
	mode = MODE_STAT.get(mode, mode)
	var model := String(r.get("model", ""))
	_add(st, "matches", 1)
	_add(st, "frags", frags)
	_add(st, "deaths", int(r.get("deaths", 0)))
	_add(st, "playtime", int(r.get("seconds", 0)))
	if model != "":
		_add_in(st, "matches_model", model, 1)
		_add_in(st, "matches_skin", "%s/%s" % [model, String(r.get("skin", "default"))], 1)
	if r.get("won", false):
		_add(st, "wins", 1)
		_add_in(st, "wins_map", String(r.get("map", "")), 1)
		if mode != "":
			_add_in(st, "wins_mode", mode, 1)
		if mode == "duel":
			_add(st, "duel_wins", 1)
	for m in r.get("medals", {}):
		if m != "frags":
			_add_in(st, "medal", MEDAL_STAT.get(m, m), int(r["medals"][m]))
	if r.get("perfect", false):
		_add_in(st, "medal", "perfect", 1)
	d["level"] = int(Q3Cards.level_for(d["xp"])["level"])
	var fresh: Array = []
	var news: Array = d.get("new", [])
	for key in unlocked_ids():
		if not had.has(key):
			var kind := String(key).get_slice(":", 0)
			var id := String(key).get_slice(":", 1)
			fresh.append({"kind": kind, "id": id, "name": Q3Cards.part(kind, id).get("name", id)})
			if not news.has(key):
				news.append(key)
	d["new"] = news
	save()
	return {"lines": lines, "gain": gain, "before": before, "after": d["xp"], "unlocked": fresh}


## Ranked as the card and the Ranked page show it. The rating (MMR) is only ever set by official
## ranked servers (PLAN.md "Trust"); until RANKED_PLACEMENTS matches are played there's no rank.
static func ranked() -> Dictionary:
	var st: Dictionary = data().get("stats", {})
	var cfg: Dictionary = Q3Cards.catalog().get("ranked", {})
	var matches := int(Q3Cards.stat(st, "ranked_matches"))
	var rating := int(Q3Cards.stat(st, "ranked_rating")) if st.has("ranked_rating") else int(cfg.get("start", 1000))
	var peak := int(Q3Cards.stat(st, "ranked_peak"))
	var placements := int(cfg.get("placements", 10))
	return {"open": int(level()["level"]) >= RANKED_LEVEL, "matches": matches, "placements": placements,
		"placed": matches >= placements, "rating": rating, "peak": peak, "rank": Q3Cards.rank(rating),
		"peak_rank": Q3Cards.rank(peak) if peak > 0 else {}, "wins": int(Q3Cards.stat(st, "ranked_wins"))}


## The ranked backend's word on the duel ladder (the headline queue): the only writer of the ranked stats
## (PLAN.md "Trust"). `q` is the hub's {rating, rated, wins, losses, peak}.
static func set_ranked(q: Dictionary) -> void:
	var st: Dictionary = data()["stats"]
	var changed := int(st.get("ranked_rating", -1)) != int(q.get("rating", 1000)) or int(st.get("ranked_matches", -1)) != int(q.get("rated", 0))
	st["ranked_rating"] = int(q.get("rating", 1000))
	st["ranked_matches"] = int(q.get("rated", 0))
	st["ranked_wins"] = int(q.get("wins", 0))
	st["ranked_peak"] = int(q.get("peak", 1000))
	if changed:
		save()


## Every part the career has earned, as a set of "kind:id".
static func unlocked_ids() -> Dictionary:
	var d := data()
	var done := Q3Cards.achieved(d)
	var out := {}
	for kind in Q3Cards.SLOTS.values():
		for p in Q3Cards.parts(kind):
			if Q3Cards.is_unlocked(p["unlock"], d, done):
				out["%s:%s" % [kind, p["id"]]] = true
	return out


# ---------------------------------------------------------------- the card editor (menu Profile page)

## Puts a part in a card slot. Refuses (false) an unknown slot or part, or one not yet earned.
static func set_card(slot: String, id: String) -> bool:
	if not Q3Cards.SLOTS.has(slot):
		return false
	var p := Q3Cards.part(Q3Cards.SLOTS[slot], id)
	if p.is_empty() or not Q3Cards.is_unlocked(p["unlock"], data()):
		return false
	data()["card"][slot] = id
	save()
	return true


## The card's three showcase stats ("stats") or medal badges ("badges"), in order. Unknown stats
## and achievements not yet earned are refused (false).
static func set_showcase(kind: String, ids: Array) -> bool:
	if ids.size() > Q3Cards.SHOWCASE:
		return false
	if kind == "stats":
		var known := (Q3Cards.catalog().get("card_stats", []) as Array).map(func(s): return s["id"])
		if not ids.all(func(i): return known.has(i)):
			return false
	elif kind == "badges":
		var done := Q3Cards.achieved(data())
		if not ids.all(func(i): return done.has(i)):
			return false
	else:
		return false
	data()["card"][kind] = ids.map(func(i): return String(i))
	save()
	return true


## Quake 3's color1 digits (CG_ColorFromString: 4 red, 2 green, 1 blue): the seven free rails.
const RAIL_COLOR1 := {"rail_blue": 1, "rail_green": 2, "rail_cyan": 3, "rail_red": 4, "rail_magenta": 5, "rail_yellow": 6, "rail_white": 7}


## The card's rail as color1 for the userinfo. Every other rail sends the nearest of Quake 3's
## seven, so anyone who can't draw the remaster's rails still sees something close.
static func rail_color1() -> int:
	var id := String(card().get("rail", ""))
	if RAIL_COLOR1.has(id):
		return RAIL_COLOR1[id]
	var v := String(Q3Cards.part("rails", id).get("value", ""))
	if not Color.html_is_valid(v):
		return 7   # team and rainbow rails: white underneath
	var c := Color(v)
	var n := (4 if c.r > 0.5 else 0) | (2 if c.g > 0.5 else 0) | (1 if c.b > 0.5 else 0)
	return n if n > 0 else 7


## Your own rail's colors [core] or [core, spiral] when the card's rail is one color1 can't carry
## (an unlocked color, team colored, rainbow); empty for Quake 3's seven. `team` is TEAM_RED 1 /
## TEAM_BLUE 2. Others see these only once cards travel online; until then they see rail_color1().
static func rail_colors(team: int) -> Array:
	var id := String(card().get("rail", ""))
	if RAIL_COLOR1.has(id):
		return []
	var v := String(Q3Cards.part("rails", id).get("value", ""))
	if v == "team":
		return [Color(0.2, 0.35, 1.0) if team == 2 else Color(1.0, 0.15, 0.1)]
	if v == "spectrum":
		var h := randf()
		return [Color.from_hsv(h, 0.9, 1.0), Color.from_hsv(fmod(h + 0.5, 1.0), 0.9, 1.0)]
	return [Color(v)] if Color.html_is_valid(v) else []


## Parts the Profile page has shown since they unlocked stop being NEW.
static func mark_seen(keys: Array) -> void:
	var news: Array = data().get("new", [])
	var left := news.filter(func(k): return not keys.has(k))
	if left.size() != news.size():
		data()["new"] = left
		save()


## Everything the Profile page draws: level and XP, the stored card and the card others see, every
## showcase stat's value, which parts are earned, each achievement's progress, and what the
## stat-driven banners need (favorite arena, best weapon, top medals, campaign tiers cleared).
static func menu_payload() -> Dictionary:
	var d := data()
	var lv := level()
	var done := Q3Cards.achieved(d)
	var st: Dictionary = d.get("stats", {})
	var stats := {}
	for s in Q3Cards.catalog().get("card_stats", []):
		stats[s["id"]] = Q3Cards.stat_value(s["id"], d)
	var unlocked := {}
	for key in unlocked_ids():
		var kind := String(key).get_slice(":", 0)
		if not unlocked.has(kind):
			unlocked[kind] = []
		unlocked[kind].append(String(key).get_slice(":", 1))
	var achs: Array = []
	for a in Q3Cards.achievements():
		achs.append({"id": a["id"], "progress": Q3Cards.progress(a, d), "done": done.has(a["id"]),
			"missing": Array(Q3Cards.missing(a, d))})
	return {"level": int(lv["level"]), "xp": int(lv["xp"]), "next": int(lv["next"]), "total": total_xp(),
		"max_level": int(Q3Cards.catalog().get("levels", {}).get("max", 100)),
		"card": d["card"], "shown": card(), "stats": stats, "unlocked": unlocked, "achievements": achs,
		"new": d.get("new", []), "ranked_level": RANKED_LEVEL, "ranked": ranked(),
		"career": {"matches": int(Q3Cards.stat(st, "matches")), "wins": int(Q3Cards.stat(st, "wins")),
			"frags": int(Q3Cards.stat(st, "frags")), "deaths": int(Q3Cards.stat(st, "deaths")),
			"playtime": int(Q3Cards.stat(st, "playtime")), "frags_weapon": st.get("frags_weapon", {}),
			"medal": st.get("medal", {}), "wins_mode": st.get("wins_mode", {}),
			"matches_model": st.get("matches_model", {})},
		"banner_ctx": _banner_ctx(d)}


static func _banner_ctx(d: Dictionary) -> Dictionary:
	var st: Dictionary = d.get("stats", {})
	var best := func(key: String) -> String:
		var top := ""
		var n := 0.0
		var m: Variant = st.get(key, {})
		if m is Dictionary:
			for k in m:
				if float(m[k]) > n:
					n = float(m[k])
					top = String(k)
		return top
	var medals: Array = []
	var md: Variant = st.get("medal", {})
	if md is Dictionary:
		medals = (md as Dictionary).keys().filter(func(k): return float(md[k]) > 0 and not Q3Cards.part("emblems", "medal_" + k).is_empty())
		medals.sort_custom(func(a, b): return float(md[a]) > float(md[b]))
	var won: Dictionary = d.get("campaign", {})
	return {"favorite_map": best.call("wins_map"), "best_weapon": best.call("frags_weapon"),
		"medals": medals.slice(0, 3).map(func(k): return {"id": k, "n": int(md[k])}),
		"campaign_won": won.size(), "ranked_peak": int(Q3Cards.stat(st, "ranked_peak"))}


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
