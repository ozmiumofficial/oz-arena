class_name Q3MatchSetup
extends Resource
## Everything the Gameplay scene needs to start a match. The menu, a dedicated server's config
## and the map editor's play-test all build one of these, so every entry point runs the same rules.

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
@export var source := "menu"              ## menu, editor (play-test), dedicated, training, launch (map= argument)
@export var training := ""                ## training preset id, when source == "training"


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
	if info.is_empty():
		errs.append("Unknown map '%s'." % map)
	elif not m.accepts_map(info) and not source in ["training", "campaign", "launch"]:   # these may use any arena (q3dm0)
		errs.append("%s isn't a %s map." % [info.get("name", map), m.display_name])
	if not SERVER_TYPES.has(server_type):
		errs.append("Unknown server type '%s'." % server_type)
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
	return errs


func to_dict() -> Dictionary:
	return {"mode": mode, "map": map, "rules": rules, "modifiers": modifiers, "bots": bots,
		"server_type": server_type, "player": player, "source": source, "training": training}


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
