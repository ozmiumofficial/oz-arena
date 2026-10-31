"""Writes engine/ui/data/create_match.json and campaign.json (the P5 "Play: offline" pages' data)."""
import json, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))


def dump(obj, indent=0):
    """JSON with short objects and lists on one line, like menu.json."""
    flat = json.dumps(obj, ensure_ascii=False)
    pad = "  " * indent
    if len(flat) + len(pad) <= 150 or not isinstance(obj, (dict, list)):
        return flat
    if isinstance(obj, list):
        return "[\n" + ",\n".join(pad + "  " + dump(v, indent + 1) for v in obj) + "\n" + pad + "]"
    return "{\n" + ",\n".join(pad + "  " + json.dumps(k) + ": " + dump(v, indent + 1) for k, v in obj.items()) + "\n" + pad + "}"


RULE_KEYS = ["fraglimit", "capturelimit", "roundlimit", "max_players", "timelimit", "warmup"]


PICK = {"ffa": 21, "tourney": 7, "team": 6, "ctf": 26}  # the arena a mode starts on: q3dm17, q3tourney2, q3dm6, q3ctf1 (the web menu's picks)


def mode(id, name, desc, fit, gametype, team, soon, rules, forced=None):
    full = {"fraglimit": 0, "capturelimit": 0, "roundlimit": 0, "max_players": 12, "timelimit": 0, "warmup": 0}
    full.update(rules)
    return {
        "id": id, "name": name, "description": desc, "fit": fit, "pick": PICK[fit], "gametype": gametype, "team": team, "soon": soon,
        "rules": full, "show": {k: k in rules for k in RULE_KEYS},
    }


modes = [
    mode("ffa", "Free For All", "Every player for themselves. Most frags wins.", "ffa", 0, False, False,
         {"fraglimit": 20, "max_players": 12, "timelimit": 15, "warmup": 0}),
    mode("duel", "Duel", "One on one (the original game's Tournament). The rest wait in the queue.", "tourney", 1, False, False,
         {"fraglimit": 0, "max_players": 2, "timelimit": 10, "warmup": 10}),
    mode("tdm", "Team Deathmatch", "Red against blue. The team with the most frags wins.", "team", 3, True, False,
         {"fraglimit": 0, "max_players": 8, "timelimit": 20, "warmup": 10}),
    mode("ctf", "Capture the Flag", "Take the enemy flag to your own base while yours is home.", "ctf", 4, True, False,
         {"capturelimit": 8, "max_players": 10, "timelimit": 20, "warmup": 10}),
    mode("clan_arena", "Clan Arena", "Rounds, no pickups, everyone spawns with every weapon and full armor. Last team standing wins the round.", "team", 3, True, True,
         {"max_players": 8, "roundlimit": 10, "warmup": 10}),
    mode("freeze_tag", "Freeze Tag", "Kills freeze instead of kill; teammates thaw you by standing next to you. Freeze the whole enemy team to win the round.", "team", 3, True, True,
         {"max_players": 10, "roundlimit": 10, "warmup": 10}),
    mode("instagib", "Instagib", "Railguns only, one hit kills, no pickups.", "ffa", 0, False, True,
         {"fraglimit": 30, "max_players": 12, "timelimit": 10, "warmup": 0}),
]

rules = {
    "fraglimit": {"label": "Frag limit", "min": 0, "max": 999, "unit": "", "tip": "Frags to win. 0 = no limit."},
    "capturelimit": {"label": "Capture limit", "min": 0, "max": 99, "unit": "", "tip": "Flag captures to win. 0 = no limit."},
    "roundlimit": {"label": "Round limit", "min": 1, "max": 99, "unit": "", "tip": "Rounds a team must win."},
    "max_players": {"label": "Max players", "min": 2, "max": 64, "unit": "", "tip": "Players and bots together."},
    "timelimit": {"label": "Time limit", "min": 0, "max": 999, "unit": "min", "tip": "Match length in minutes. 0 = no limit."},
    "warmup": {"label": "Warmup", "min": 0, "max": 120, "unit": "s", "tip": "Warmup before the match starts. 0 = none."},
}


def modifier(id, label, tip, options, default, cvar="", soon=True):
    return {
        "id": id, "label": label, "tip": tip, "options": [o[1] for o in options], "values": [o[0] for o in options],
        "v": [o[0] for o in options].index(default), "idx": "|".join(str(i) for i in range(len(options))), "cvar": cvar, "soon": soon,
    }


modifiers = [
    modifier("weapon_set", "Weapons", "Which weapons spawn and what players start with.",
             [["standard", "Standard pickups"], ["all_weapons", "Spawn with every weapon"], ["railgun_only", "Railgun only"], ["rockets_only", "Rockets only"], ["no_bfg", "No BFG"]], "standard"),
    modifier("item_set", "Items", "Which items spawn on the map.",
             [["standard", "Standard"], ["no_powerups", "No powerups"], ["no_health", "No health"], ["no_items", "No items"]], "standard"),
    modifier("physics", "Physics", "Movement rules. VQ3 is the original game's own bg_pmove.c; CPM adds air control and double jumps.",
             [["vq3", "VQ3 (original)"], ["cpm", "CPM (Promode)"]], "vq3"),
    modifier("spawn_rules", "Spawns", "How spawn points are picked. The original game picks randomly among the points farthest from the killer.",
             [["far_random", "Random far (original)"], ["random", "Fully random"], ["farthest", "Always farthest"]], "far_random"),
    modifier("damage", "Damage", "Instagib: any hit kills.", [["normal", "Normal"], ["instagib", "Instagib"]], "normal"),
    modifier("quad_factor", "Quad factor", "Quad Damage multiplier (g_quadfactor).", [[3, "x3 (original)"], [4, "x4"], [2, "x2"]], 3, "g_quadfactor", False),
    modifier("gravity", "Gravity", "g_gravity in units/s^2.", [[800, "800 (original)"], [400, "400 (low)"], [1200, "1200 (heavy)"]], 800, "g_gravity", False),
    modifier("weapon_respawn", "Weapon respawn", "Seconds before a picked-up weapon returns (g_weaponrespawn; team modes use 30).",
             [[5, "5 s (original)"], [30, "30 s"], [1, "1 s"]], 5, "g_weaponrespawn", False),
    modifier("friendly_fire", "Friendly fire", "Team modes: teammates can hurt each other.", [[0, "Off"], [1, "On"]], 0, "g_friendlyFire", False),
    modifier("overtime", "Overtime", "What happens on a tie when time runs out.",
             [["sudden_death", "Sudden death"], ["extra_time", "Two minutes extra"], ["draw", "Draw"]], "sudden_death"),
]

# the match objects startmatch reads; the keys after them are the Fight button's working values
skirmish = {
    "mi": 0, "count": 5, "skill": 3,
    "map": "", "gametype": 0, "fraglimit": 20, "timelimit": 15, "capturelimit": 8, "maxplayers": 12,
    "bots": dict([("m%d" % k, "") for k in range(7)] + [("f%d" % j, "") for j in range(18)]),
    "n": 5, "nmb": 0, "nm": 0, "c": 0, "d": 0,
}
for k in range(7):
    skirmish["k%d" % k] = "-"

custom = {
    "mi": 0, "server": "Offline", "you": "Play",
    "slots": [{"on": False, "i": k + 1, "skill": 3, "team": "Red" if k % 2 == 0 else "Blue"} for k in range(11)],
    "map": "", "gametype": 0, "fraglimit": 20, "timelimit": 15, "capturelimit": 8, "maxplayers": 12, "skill": 3,
    "bots": {"b%d" % k: {"name": "", "skill": 3, "team": ""} for k in range(11)},
}

create_match = {
    "create_match": {
        "_note": "Skirmish (offline_bots.rml) and Custom Match (create_match.rml): the Godot build's modes, rules, modifiers and skills (its dev_state), the pages' choices and the match objects their Fight buttons hand to startmatch. The arenas are the engine's maps; the chosen one and the mode's map type are in create_match_pick. Modes with soon are shown and refused; modifiers with soon show their default; the others set their cvar. ranges[n] is the spinner options 0..n-1 (a bot's index in bots).",
        "modes": modes,
        "rules": rules,
        "modifiers": modifiers,
        "skills": ["I Can Win", "Bring It On", "Hurt Me Plenty", "Hardcore", "Nightmare!"],
        "skirmish": skirmish,
        "custom": custom,
        "ranges": ["|".join(str(i) for i in range(n)) for n in range(65)],
    },
    "create_match_pick": {
        "_note": "What the two pages' map browsers show: fit, the chosen mode's map type (modes[mi].fit), and mapi, the chosen arena's index in the engine's maps (21 is the original game's q3dm17 in its arenas.txt order). Apart from create_match so that the pages' other choices don't make every card be checked again.",
        "skirmish": {"fit": "ffa", "mapi": 21},
        "custom": {"fit": "ffa", "mapi": 21},
    },
}

# the campaign: the original game's tiers (the Godot build's dev_state campaign layout); each arena is
# its map and its index in the engine's maps (arenas.txt order), which give its name, shot and bots
tiers = [
    ("Training", ["q3dm0"]),
    ("Tier 1", ["q3dm1", "q3dm2", "q3dm3", "q3tourney1"]),
    ("Tier 2", ["q3dm4", "q3dm5", "q3dm6", "q3tourney2"]),
    ("Tier 3", ["q3dm7", "q3dm8", "q3dm9", "q3tourney3"]),
    ("Tier 4", ["q3dm10", "q3dm11", "q3dm12", "q3tourney4"]),
    ("Tier 5", ["q3dm13", "q3dm14", "q3dm15", "q3tourney5"]),
    ("Tier 6", ["q3dm16", "q3dm17", "q3dm18", "q3dm19"]),
    ("Final", ["q3tourney6"]),
]
order = ["q3dm1", "q3dm2", "q3dm3", "q3tourney1", "q3dm4", "q3dm5", "q3dm6", "q3tourney2", "q3dm7", "q3dm8", "q3dm9", "q3tourney3",
         "q3dm10", "q3dm11", "q3dm12", "q3tourney4", "q3dm13", "q3dm14", "q3dm15", "q3tourney5", "q3dm16", "q3dm17", "q3dm18", "q3dm19",
         "q3dm0", "q3tourney6"]
campaign = {
    "campaign": {
        "_note": "The campaign page (campaign.rml): its choices (tier, skill), the medals and the arenas won, and campaign_tiers, the tiers and their arenas (a key of its own, so a choice doesn't make every arena be checked again). Progress comes with single player and progression (P12): won, the medals and the open tiers are stand-in progress (the web menu's mock: every tier won at skill 3). An arena's mi is its index in the engine's maps (arenas.txt order), which gives its name, level shot and bots. Start plays the arena as a free-for-all against its bots on the chosen skill (start is the match object startmatch reads); nothing is recorded yet.",
        "tier": 7, "skill": 3, "won": 26, "total": 26,
        "tier_options": "|".join(str(i) for i in range(len(tiers))),
        "medals": [
            {"id": "accuracy", "label": "Accuracy", "img": "/menu/medals/medal_accuracy", "count": 4},
            {"id": "impressive", "label": "Impressive", "img": "/menu/medals/medal_impressive", "count": 20},
            {"id": "excellent", "label": "Excellent", "img": "/menu/medals/medal_excellent", "count": 47},
            {"id": "gauntlet", "label": "Humiliation", "img": "/menu/medals/medal_gauntlet", "count": 0},
            {"id": "frags", "label": "Frags", "img": "/menu/medals/medal_frags", "count": 260},
            {"id": "perfect", "label": "Perfect", "img": "/menu/medals/medal_victory", "count": 2},
        ],
        "start": {"map": "", "gametype": 0, "fraglimit": 20, "timelimit": 0, "capturelimit": 0, "maxplayers": 8, "skill": 3,
                  "bots": {"b%d" % k: "" for k in range(7)}},
    },
    "campaign_tiers": [
        {"name": n, "open": True, "complete": True, "arenas": [{"map": m, "mi": order.index(m), "won": 3} for m in arenas]}
        for n, arenas in tiers
    ],
}

for name, obj in (("create_match", create_match), ("campaign", campaign)):
    path = os.path.join(ROOT, "engine", "ui", "data", name + ".json")
    text = dump(obj) + "\n"
    json.loads(text)
    open(path, "w", encoding="utf-8", newline="\n").write(text)
    print(path, len(text))
