class_name Q3GameMode
extends Resource
## A game mode is data: the rules the Gameplay scene loads, never a scene of its own.
## Modifiers (game/modifiers.json) stack on top; a new mode is a new .tres in game/modes/.

## Every rule a mode can use: label, range, unit. A mode lists the ones it uses with its defaults.
const RULES := {
	"fraglimit": {"label": "Frag limit", "min": 0, "max": 999, "unit": "", "tip": "Frags to win. 0 = no limit."},
	"timelimit": {"label": "Time limit", "min": 0, "max": 999, "unit": "min", "tip": "Match length in minutes. 0 = no limit."},
	"capturelimit": {"label": "Capture limit", "min": 0, "max": 99, "unit": "", "tip": "Flag captures to win. 0 = no limit."},
	"roundlimit": {"label": "Round limit", "min": 1, "max": 99, "unit": "", "tip": "Rounds a team must win."},
	"max_players": {"label": "Max players", "min": 2, "max": 64, "unit": "", "tip": "Players and bots together."},
	"warmup": {"label": "Warmup", "min": 0, "max": 120, "unit": "s", "tip": "Warmup before the match starts. 0 = none."},
}

@export var id := ""
@export var display_name := ""
@export_multiline var description := ""
@export var team_based := false
@export var rounds := false                       ## round-based (Clan Arena, Freeze Tag)
@export var map_types: PackedStringArray = []     ## arenas.txt types this mode plays on (ffa, tourney, team, ctf)
@export var rules: Dictionary = {}                ## rule -> default value (keys from RULES)
@export var modifiers: Dictionary = {}            ## modifier -> value the mode forces (Instagib: railgun only)


func accepts_map(map: Dictionary) -> bool:
	for t in map.get("types", []):
		if map_types.has(t):
			return true
	return false
