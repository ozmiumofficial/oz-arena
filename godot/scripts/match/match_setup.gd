class_name Q3MatchSetup
extends Resource
## Everything the Gameplay scene needs to start a match. The menu, a dedicated server's config
## and the map editor's play-test all build one of these, so every entry point runs the same rules.

## g_gametype for each mode (bg_public.h gametype_t). Clan Arena, Freeze Tag and Instagib play as their base types until
## their rules land in the game module.
const GAMETYPES := {"ffa": 0, "duel": 1, "tdm": 3, "ctf": 4, "clan_arena": 3, "freeze_tag": 3, "instagib": 0, "race": 0}
const SERVER_TYPES := ["offline", "listen", "dedicated", "matchmaking"]
## Quake 3's bot skill names (ui_startserver.c)
const SKILLS := ["I Can Win", "Bring It On", "Hurt Me Plenty", "Hardcore", "Nightmare!"]

@export var mode := "ffa"
@export var map := "q3dm17"
@export var rules: Dictionary = {}        ## overrides of the mode's rule defaults
@export var modifiers: Dictionary = {}    ## overrides of the modifier defaults
@export var bots: Array = []              ## {name, skill 1-5, team "" / "red" / "blue"}
@export var server_type := "offline"
@export var player: Dictionary = {}       ## name, model, skin, team_color (from the Profile)
@export var spectate := false             ## join as a spectator (id's team spectator): watch the bots
@export var matchmade: Dictionary = {}   ## the hub's match_found when the matchmaker made this match (never saved)
@export var online: Dictionary = {}      ## a listen server's listing: name, password, ranked, version (Q3HostNet)
@export var source := "menu"              ## menu, editor (play-test), dedicated, training, launch (map= argument)
@export var training := ""                ## training preset id, when source == "training"


## The match the hub's matchmaker made for this host: the mode and map it picked, the mode's own rules (a short warmup
## that starts by itself), no modifiers, no bots, listed privately (only its players can join).
static func from_found(m: Dictionary) -> Q3MatchSetup:
	var s := Session.new_setup(String(m.get("mode", "ffa")), String(m.get("map", "")))
	s.server_type = "listen"
	s.source = "matchmaking"
	s.rules = {"warmup": 10}
	s.matchmade = m
	var ranked := bool(m.get("ranked", false))
	s.online = {"name": "%s %s" % ["Ranked" if ranked else "Quick", String(m.get("mode", "match")).capitalize()], "password": "", "ranked": ranked}
	return s


## The cvars the game starts with for this setup (what Gameplay and the dedicated server set before "map").
## `sp_skill`: the campaign's skill, only used for single player.
func game_cvars(sp_skill := 3) -> Dictionary:
	var rules := effective_rules()
	var mods := effective_modifiers()
	var campaign := source in ["campaign", "training"]
	var cvars := {
		"g_gametype": 2 if campaign else GAMETYPES.get(mode, 0),   # 2: GT_SINGLE_PLAYER
		"g_spSkill": sp_skill,
		"fraglimit": rules.get("fraglimit", 20), "timelimit": rules.get("timelimit", 10),
		"capturelimit": rules.get("capturelimit", 8),
		"g_doWarmup": 1 if int(rules.get("warmup", 0)) > 0 else 0, "g_warmup": rules.get("warmup", 0),
		# ready-up (tools/q3core/patches/0003): with a warmup, the countdown waits for F3 from every human
		"g_readyUp": 1 if int(rules.get("warmup", 0)) > 0 and not campaign else 0,
		"g_gravity": mods.get("gravity", 800), "g_quadfactor": mods.get("quad_factor", 3),
		"g_weaponrespawn": mods.get("weapon_respawn", 5), "g_friendlyfire": 1 if mods.get("friendly_fire", false) else 0,
		"sv_maxclients": maxi(int(rules.get("max_players", 16)), bots.size() + 1),
	}
	if mode == "race":   # RACE_PLAN.md D2: id 1.32's movement (Classic) and the game's race rules; Gameplay restores the defaults after
		cvars["g_ruleset"] = "classic"
		cvars["g_race"] = 1
	return cvars


func game_mode() -> Q3GameMode:
	return Session.mode(mode)


## The mode's rules with this setup's overrides on top.
func effective_rules() -> Dictionary:
	var m := game_mode()
	var out: Dictionary = m.rules.duplicate() if m else {}
	for k in rules:
		if out.has(k):
			out[k] = rules[k]
	return out


## Modifier defaults, then this setup's choices, then whatever the mode forces.
func effective_modifiers() -> Dictionary:
	var out := {}
	for d in Session.modifiers:
		out[d["id"]] = d["default"]
	for k in modifiers:
		out[k] = modifiers[k]
	var m := game_mode()
	if m:
		for k in m.modifiers:
			out[k] = m.modifiers[k]
	return out


## Problems that stop this setup from starting; empty when it's good to go.
func validate() -> PackedStringArray:
	var errs: PackedStringArray = []
	var m := game_mode()
	if m == null:
		return PackedStringArray(["Unknown game mode '%s'." % mode])
	var info := Session.map_info(map)
	if source == "editor":
		pass   # a play-test runs the map the editor just compiled, which isn't in arenas.txt
	elif info.is_empty():
		errs.append("Unknown map '%s'." % map)
	elif not m.accepts_map(info) and not source in ["training", "campaign", "launch"]:   # these may use any arena (q3dm0)
		errs.append("%s isn't a %s map." % [info.get("name", map), m.display_name])
	if bool(online.get("ranked", false)):   # ranked duels play the duel pool's maps only (DUEL_MAPS_PLAN.md)
		if mode != "duel":
			errs.append("Ranked matches are duels.")
		elif not Session.in_duel_pool(map):
			errs.append("%s isn't in the ranked duel pool." % info.get("name", map))
	if not SERVER_TYPES.has(server_type):
		errs.append("Unknown server type '%s'." % server_type)
	if server_type == "listen" and source not in ["editor"] and (not Online.enabled() or Online.client.status != "online"):
		errs.append("Hosting an online match needs a connection to the online service (Settings, Network).")
	var r := effective_rules()
	for k in r:
		var spec: Dictionary = Q3GameMode.RULES.get(k, {})
		if spec and (int(r[k]) < int(spec["min"]) or int(r[k]) > int(spec["max"])):
			errs.append("%s must be %d-%d." % [spec["label"], spec["min"], spec["max"]])
	if bots.size() + 1 > int(r.get("max_players", 64)):
		errs.append("Too many bots: %d players allowed." % int(r.get("max_players", 64)))
	for b in bots:
		var skill := int(b.get("skill", 3))
		if skill < 1 or skill > 5:
			errs.append("Bot %s: skill must be 1-5." % b.get("name", "?"))
		if m.team_based and not ["red", "blue"].has(String(b.get("team", ""))):
			errs.append("Bot %s needs a team (red or blue)." % b.get("name", "?"))
	if spectate and bots.size() < (2 if mode == "duel" else 1):
		errs.append("Spectating needs bots to watch (a duel needs two).")
	return errs


func to_dict() -> Dictionary:
	return {"mode": mode, "map": map, "rules": rules, "modifiers": modifiers, "bots": bots,
		"server_type": server_type, "player": player, "source": source, "training": training, "spectate": spectate}


static func from_dict(d: Dictionary) -> Q3MatchSetup:
	var s := Q3MatchSetup.new()
	s.mode = String(d.get("mode", s.mode))
	s.map = String(d.get("map", s.map))
	s.rules = d.get("rules", {})
	s.modifiers = d.get("modifiers", {})
	s.bots = d.get("bots", [])
	s.server_type = String(d.get("server_type", s.server_type))
	s.player = d.get("player", {})
	s.source = String(d.get("source", s.source))
	s.training = String(d.get("training", ""))
	s.spectate = bool(d.get("spectate", false))
	var o: Variant = d.get("online", {})
	if o is Dictionary:   # a listen server's listing; the password never goes into the saved setup (to_dict)
		s.online = {"name": String(o.get("name", "")).strip_edges().left(40), "password": String(o.get("password", "")).strip_edges().left(40),
			"ranked": bool(o.get("ranked", false))}
	return s


## One line per fact, for the Gameplay stub and the "continue" tooltip.
func summary() -> String:
	var m := game_mode()
	var lines: PackedStringArray = []
	lines.append("%s on %s" % [m.display_name if m else mode, Session.map_info(map).get("name", map)])
	var r := effective_rules()
	var parts: PackedStringArray = []
	for k in r:
		parts.append("%s %s%s" % [Q3GameMode.RULES[k]["label"].to_lower(), r[k], Q3GameMode.RULES[k]["unit"]])
	lines.append(", ".join(parts))
	var mods: PackedStringArray = []
	var em := effective_modifiers()
	for d in Session.modifiers:
		if em[d["id"]] != d["default"]:
			mods.append("%s: %s" % [d["label"], Session.modifier_label(d["id"], em[d["id"]])])
	if mods.size():
		lines.append("Modifiers: " + ", ".join(mods))
	if bots.size():
		var names: PackedStringArray = []
		for b in bots:
			names.append("%s (%d%s)" % [b["name"], int(b["skill"]), (", " + String(b["team"])) if String(b.get("team", "")) != "" else ""])
		lines.append("Bots: " + ", ".join(names))
	lines.append("Server: %s" % server_type)
	return "\n".join(lines)
