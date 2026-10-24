"""Player card catalog: every card part, achievement and showcase stat, as data.

The structure is ported from trac3r (C:/gAME/TRAC3R/web/src/game/cosmetics.ts and achievements.ts):
  - Parts are pure data {id, name, unlock, ...}, shared by the game, the menu and (later) the ranked
    backend, which re-checks every card it serves (enforce_unlocks).
  - Achievements are computed from career stats, never stored as flags, so they can't drift between
    devices. Each has a target, a progress rule and a bronze/silver/gold medal; collections list what's
    still missing.
  - The level track pays something at every level up to 10 and at least every 5 levels after that
    (trac3r 1.12.17 found levels 61-99 empty and had to backfill them).
The content is Quake III Arena's: arenas, skies, the 29 fighters and their skins, weapons, items,
medals, materials, the campaign and the skill levels.

build(src) -> dict, written to cards.json by build_cards.py.

Unlock rules (one per part; Q3Cards.is_unlocked evaluates them):
  {"free": true}                        everyone, from the start (everything Quake 3 gave away)
  {"level": N}                          the level track
  {"achievement": id}                   only through that achievement
  {"stat": "a.b", "min": N}             a career stat ("wins_map.q3dm17", "pickups.quad")
  {"campaign": [maps], "skill": S}      every listed campaign arena won at skill S or higher
  {"any": [rules]}                      any one of them

Achievement progress rules:
  {"stat": "a.b"}                       the stat's value
  {"set": "a", "of": [keys], "min": N}  how many keys of dict stat "a" reached N (default 1); `missing` names the rest
  {"max": "a"}                          the highest value in dict stat "a"
  {"campaign": [maps], "skill": S}      how many of the arenas are won at skill S or higher
"""

# ---------------------------------------------------------------- vocabulary

WEAPONS = [  # id, display name, icon, stat key
    ("gauntlet", "Gauntlet", "iconw_gauntlet"),
    ("machinegun", "Machinegun", "iconw_machinegun"),
    ("shotgun", "Shotgun", "iconw_shotgun"),
    ("grenade", "Grenade Launcher", "iconw_grenade"),
    ("rocket", "Rocket Launcher", "iconw_rocket"),
    ("lightning", "Lightning Gun", "iconw_lightning"),
    ("railgun", "Railgun", "iconw_railgun"),
    ("plasma", "Plasma Gun", "iconw_plasma"),
    ("bfg", "BFG10K", "iconw_bfg"),
]
ITEMS = [  # id, display name, icon
    ("quad", "Quad Damage", "quad"),
    ("regen", "Regeneration", "regen"),
    ("haste", "Haste", "haste"),
    ("battlesuit", "Battle Suit", "envirosuit"),
    ("invis", "Invisibility", "invis"),
    ("flight", "Flight", "flight"),
    ("mega", "Mega Health", "iconh_mega"),
    ("armor_red", "Heavy Armor", "iconr_red"),
    ("armor_yellow", "Armor", "iconr_yellow"),
    ("medkit", "Medkit", "medkit"),
    ("teleporter", "Personal Teleporter", "teleporter"),
]
POWERUPS = ["quad", "regen", "haste", "battlesuit", "invis", "flight"]
MEDALS = [  # stat key under medal., display name, icon (menu/medals)
    ("excellent", "Excellent", "medal_excellent"),
    ("impressive", "Impressive", "medal_impressive"),
    ("gauntlet", "Humiliation", "medal_gauntlet"),
    ("capture", "Capture", "medal_capture"),
    ("assist", "Assist", "medal_assist"),
    ("defend", "Defense", "medal_defend"),
]
MODES = ["ffa", "duel", "tdm", "ctf", "ca", "ft", "instagib"]
SKILLS = ["I Can Win", "Bring It On", "Hurt Me Plenty", "Hardcore", "Nightmare!"]
# Ranked tiers (PLAN.md, Ozmium 2026-10-23): Bronze to Godlike. Floors in rating (MMR) points.
RANKED_TIERS = [("bronze", "Bronze", 0), ("silver", "Silver", 1000), ("gold", "Gold", 1300),
                ("platinum", "Platinum", 1600), ("diamond", "Diamond", 1900), ("godlike", "Godlike", 2200)]
RANKED_DIVISION = 100   # III -> II -> I every 100 points inside a tier; Godlike is open-ended
RANKED_PLACEMENTS = 10  # matches before a rank shows

# Career stats Q3Progress keeps (cards.json "stats_doc"; dotted keys are dictionaries).
STATS_DOC = {
    "matches": "matches played to the end", "wins": "matches won", "frags": "frags", "deaths": "deaths",
    "playtime": "seconds in matches",
    "wins_mode.<mode>": "wins per game mode (%s)" % ", ".join(MODES),
    "wins_map.<map>": "wins per arena", "matches_model.<model>": "matches played as each character",
    "matches_skin.<model>/<skin>": "matches played in each skin (red/blue for team skins)",
    "frags_weapon.<weapon>": "frags per weapon (%s)" % ", ".join(w[0] for w in WEAPONS),
    "frags_powerup.<powerup>": "frags while holding each powerup",
    "medal.<award>": "Quake 3 awards: excellent, impressive, gauntlet, capture, assist, defend, perfect",
    "pickups.<item>": "item pickups (%s)" % ", ".join(i[0] for i in ITEMS),
    "deaths_by.<cause>": "deaths by lava, slime, fall, drown, telefrag, crush",
    "telefrags": "telefrags", "rocket_jumps": "rocket jumps", "plasma_climbs": "plasma climbs",
    "air_rocket_frags": "rocket frags while airborne after a rocket jump",
    "flag_returns": "flags returned", "ca_clutches": "Clan Arena rounds won as the last one standing",
    "thaws": "Freeze Tag thaws", "flawless_ffa": "Free For All wins with no deaths",
    "trifectas": "matches with an Excellent, an Impressive and a Gauntlet",
    "best.speed": "top horizontal speed (ups)", "best.rail_streak": "most rail hits in a row",
    "best.rail_accuracy": "best railgun accuracy in a match (%, 10+ shots)",
    "duel_wins": "Tournament wins", "ranked_matches": "ranked matches", "ranked_peak": "highest ranked rating",
    "ranked_rating": "current ranked rating (MMR, set by official ranked servers)", "ranked_wins": "ranked wins",
}

# ---------------------------------------------------------------- hand-written parts

# Level ranks (placeholders in PLAN.md "Open decisions"). The Vadrigar are the Arena's masters.
LEVEL_TITLES = [(1, "fresh_meat", "Fresh Meat"), (5, "initiate", "Initiate"), (10, "fragger", "Fragger"),
                (15, "contender", "Contender"), (20, "gladiator", "Gladiator"), (30, "veteran", "Arena Veteran"),
                (40, "warlord", "Warlord"), (50, "champion", "Champion"), (60, "immortal", "Immortal"),
                (70, "vadrigars_chosen", "Vadrigar's Chosen"), (80, "deathbringer", "Deathbringer"),
                (90, "arena_lord", "Arena Lord"), (100, "xaeros_equal", "Xaero's Equal")]

# Frames: a world material for the border (its albedo is baked as a swatch), tinted.
LEVEL_FRAMES = [  # level, id, name, material, tint
    (10, "rusted_iron", "Rusted Iron", "gothic_trim/pitted_rust3", "#b06a3a"),
    (20, "riveted_steel", "Riveted Steel", "gothic_trim/chrome_rivet", "#c8ccd0"),
    (30, "gothic_stone", "Gothic Stone", "gothic_block/blocks15_ow", "#a89c8c"),
    (40, "base_tech", "Base Tech", "base_trim/panel_shiny", "#9fb4c8"),
    (50, "bloodstained", "Bloodstained", "gothic_block/blocks18cblood", "#a01818"),
    (60, "bronze", "Bronze", "gothic_trim/chrome_rivet", "#cd7f32"),
    (70, "silver", "Silver", "gothic_trim/chrome_rivet", "#e6e8ee"),
    (80, "gold", "Gold", "gothic_wall/goldbrick", "#ffd24a"),
    (90, "arena_master", "Arena Master", "gothic_trim/skullsvertgray02a", "#ff4a1a"),
]
# Powerup frames are the powerup shells (Q3FX.shell_material), animated on the live card.
POWERUP_FRAMES = [("quad", "Quad Damage"), ("battlesuit", "Battle Suit"), ("regen", "Regeneration"),
                  ("haste", "Haste"), ("invis", "Invisibility")]

# Material banners: world materials rendered as a lit wall (Q3's own scale), some animated.
MATERIAL_BANNERS = [  # id, name, material, animated
    ("q3a_banner", "Arena Banner", "base_wall/main_q3abanner", True),
    ("proto_banner", "Proto Banner", "base_wall/protobanner", True),
    ("gothic_blocks", "Gothic Blocks", "gothic_block/blocks15_ow", False),
    ("bloody_blocks", "Bloody Blocks", "gothic_block/blocks17bloody", False),
    ("skull_arch", "Skull Arch", "gothic_door/skullarch_b", False),
    ("pentagram", "Pentagram", "gothic_light/pentagram_light1", False),
    ("flesh", "Flesh Wall", "skin/proto_skin2", False),
    ("rusty_grate", "Rusty Grate", "base_floor/proto_rustygrate", False),
    ("blue_metal", "Blue Metal", "base_wall/bluemetal1b_chrome", False),
    ("jump_pad", "Jump Pad", "sfx/xq1metalbig_jumppad", True),
    ("glow_floor", "Glow Floor", "gothic_floor/glowfloor1", True),
    ("lava", "Lava", "liquids/lavahell", True),
    ("slime", "Slime", "liquids/slime1", True),
    ("hellfire", "Hellfire", "sfx/flame2", True),
    ("electric_slime", "Electric Slime", "sfx/electricslime", True),
    ("ornate_door", "Ornate Door", "gothic_door/door02_i_ornate5_fin", False),
    ("red_flag", "Red Flag", "ctf/ctf_redflag", True),
    ("blue_flag", "Blue Flag", "ctf/ctf_blueflag", True),
    ("blue_pentagram", "Blue Pentagram", "sfx/pentagramfloor_blue3glowtest", True),
    ("lava_grate", "Lava Grate", "base_floor/clang_floor3blava", True),
    ("bloody_bridge", "Bloody Bridge", "gothic_floor/metalbridge06b_blood", False),
    ("arena_tower", "Arena Tower", "gothic_trim/km_arena1tower4fx", True),
    ("red_light", "Red Light", "base_light/proto_lightred", False),
    ("hell_rock", "Hell Rock", "organics/pjrock7d", False),
    ("head_wall", "Head Wall", "gothic_block/blocks18cbloodhead", False),
    ("slime_blocks", "Slime Blocks", "gothic_block/blocks17slime", True),
    ("electric_wall", "Electric Wall", "base_trim/electricwall", True),
]

ACCENTS_FREE = [  # Quake 3's ^1-^7 name colors
    ("red", "Red", "#ff2020"), ("green", "Green", "#20ff20"), ("yellow", "Yellow", "#ffff20"),
    ("blue", "Blue", "#3050ff"), ("cyan", "Cyan", "#20ffff"), ("magenta", "Magenta", "#ff20ff"),
    ("white", "White", "#ffffff"),
]
ACCENTS_EARNED = [("rust", "Rust", "#b0502a"), ("bronze", "Bronze", "#cd7f32"), ("chrome", "Chrome", "#c8d2dc"),
                  ("plasma", "Plasma", "#5ab4ff"), ("slime", "Slime", "#8cff3a"), ("quad", "Quad Blue", "#4a6bff"),
                  ("lava", "Lava", "#ff6a1a"), ("gold", "Gold", "#ffd24a")]

# Rail trails: Quake 3's color1 1-7 stay free; more colors and patterns unlock.
RAIL_FREE = [("red", "Red", "#ff0000"), ("green", "Green", "#00ff00"), ("yellow", "Yellow", "#ffff00"),
             ("blue", "Blue", "#0000ff"), ("cyan", "Cyan", "#00ffff"), ("magenta", "Magenta", "#ff00ff"),
             ("white", "White", "#ffffff")]
RAIL_EARNED = [("orange", "Orange", "#ff8000", None), ("purple", "Purple", "#8020ff", None),
               ("pink", "Pink", "#ff70b0", None), ("lime", "Lime", "#b0ff00", None),
               ("pulse", "Pulsing Core", "#ffffff", "pulse"), ("team", "Team Colored", "team", "team"),
               ("gold", "Gold", "#ffd24a", None), ("rainbow", "Rainbow Spiral", "spectrum", "spectrum")]

CARD_STATS = [  # showcase stats: id, label, how (a stat key or a derived value Q3Cards computes)
    ("frags", "FRAGS", "frags"), ("wins", "WINS", "wins"), ("kd", "K/D", "=kd"),
    ("excellents", "EXCELLENTS", "medal.excellent"), ("impressives", "IMPRESSIVES", "medal.impressive"),
    ("gauntlets", "HUMILIATIONS", "medal.gauntlet"), ("captures", "CAPTURES", "medal.capture"),
    ("duel_wins", "DUEL WINS", "duel_wins"), ("rail_acc", "BEST RAIL ACC", "best.rail_accuracy"),
    ("top_speed", "TOP SPEED", "best.speed"), ("ranked_peak", "RANKED PEAK", "ranked_peak"),
    ("arenas_won", "ARENAS WON", "=arenas_won"), ("achievements", "ACHIEVEMENTS", "=achievements"),
    ("hours", "HOURS", "=hours"),
]


# ---------------------------------------------------------------- achievements

def _ach(out, id, name, desc, medal, target, progress, icon, hidden=False):
    out.append(dict(id=id, name=name, desc=desc, medal=medal, target=target, progress=progress, icon=icon,
                    hidden=hidden))


def achievements(campaign, maps, characters):
    """About 80 achievements, all Quake 3. `campaign` = [(tier name, [maps])], maps = every arena."""
    A = []
    # medals
    for key, name, icon in MEDALS:
        steps = [(10, "bronze"), (100, "silver"), (1000, "gold")] if key in ("excellent", "impressive", "gauntlet") \
            else [(10, "bronze"), (100, "silver")]
        for n, medal in steps:
            _ach(A, "medal_%s_%d" % (key, n), "%s x%d" % (name, n), "Earn %d %s medals" % (n, name), medal, n,
                 {"stat": "medal." + key}, "emblem:" + icon)
    # weapons (gauntlet frags are the Humiliation medal above)
    for key, name, icon in WEAPONS[1:]:
        for n, medal, word in [(100, "bronze", "Blooded"), (1000, "silver", "Master")]:
            _ach(A, "weapon_%s_%d" % (key, n), "%s %s" % (name, word), "%d frags with the %s" % (n, name), medal, n,
                 {"stat": "frags_weapon." + key}, "emblem:" + icon)
    _ach(A, "arsenal", "Arsenal", "100 frags with every weapon", "gold", len(WEAPONS),
         {"set": "frags_weapon", "of": [w[0] for w in WEAPONS], "min": 100}, "emblem:iconw_bfg")
    # powerups
    for key in POWERUPS:
        name = dict((i[0], i[1]) for i in ITEMS)[key]
        _ach(A, "powerup_" + key, name + " Fragger", "25 frags while holding %s" % name, "bronze", 25,
             {"stat": "frags_powerup." + key}, "emblem:" + dict((i[0], i[2]) for i in ITEMS)[key])
    _ach(A, "every_powerup", "Juiced", "A frag under every powerup", "silver", len(POWERUPS),
         {"set": "frags_powerup", "of": POWERUPS}, "emblem:quad")
    _ach(A, "quad_god", "Quad God", "250 frags with Quad Damage", "gold", 250, {"stat": "frags_powerup.quad"},
         "emblem:quad")
    # feats
    _ach(A, "perfect", "Perfect", "Win a match without dying (the postgame Perfect)", "silver", 1,
         {"stat": "medal.perfect"}, "emblem:medal_victory")
    _ach(A, "untouchable", "Untouchable", "Win Free For All with no deaths", "gold", 1, {"stat": "flawless_ffa"},
         "emblem:medal_victory")
    _ach(A, "rail_streak", "The Longest Yard", "Five rail hits in a row", "gold", 5, {"stat": "best.rail_streak"},
         "emblem:iconw_railgun")
    _ach(A, "rail_accuracy", "Deadeye", "50% railgun accuracy in a match (10+ shots)", "silver", 50,
         {"stat": "best.rail_accuracy"}, "emblem:medal_accuracy")
    _ach(A, "air_rocket", "Air Rocket", "A rocket frag while flying from your own rocket jump", "silver", 1,
         {"stat": "air_rocket_frags"}, "emblem:iconw_rocket")
    _ach(A, "telefragger", "Telefragger", "Telefrag 10 players", "bronze", 10, {"stat": "telefrags"},
         "emblem:teleporter")
    _ach(A, "trifecta", "Show-Off", "An Excellent, an Impressive and a Gauntlet in one match", "silver", 1,
         {"stat": "trifectas"}, "emblem:medal_impressive")
    # movement
    _ach(A, "speed_600", "Strafe Jumper", "Reach 600 ups", "bronze", 600, {"stat": "best.speed"}, "emblem:haste")
    _ach(A, "speed_800", "Speed Demon", "Reach 800 ups", "silver", 800, {"stat": "best.speed"}, "emblem:haste")
    _ach(A, "speed_1000", "Light Speed", "Reach 1000 ups", "gold", 1000, {"stat": "best.speed"}, "emblem:haste")
    _ach(A, "rocket_jumper", "Rocket Jumper", "100 rocket jumps", "bronze", 100, {"stat": "rocket_jumps"},
         "emblem:iconw_rocket")
    _ach(A, "plasma_climber", "Plasma Climber", "25 plasma climbs", "silver", 25, {"stat": "plasma_climbs"},
         "emblem:iconw_plasma")
    # accidents (the obituary titles)
    for key, n, id, name, desc, medal in [("lava", 50, "lava_swimmer", "Lava Swimmer", "Die in lava 50 times", "bronze"),
                                          ("lava", 100, "lava_bath", "Does a Back Flip into the Lava", "Die in lava 100 times", "gold"),
                                          ("fall", 50, "cratered", "Cratered", "Fall to your death 50 times", "bronze"),
                                          ("drown", 25, "sank", "Sank Like a Rock", "Drown 25 times", "bronze"),
                                          ("telefrag", 10, "telefragged", "Wrong Place", "Get telefragged 10 times", "bronze")]:
        _ach(A, id, name, desc, medal, n, {"stat": "deaths_by." + key}, "emblem:medal_frags", hidden=True)
    # modes
    _ach(A, "duelist", "Duelist", "Win 25 Tournament matches", "bronze", 25, {"stat": "duel_wins"}, "emblem:medal_victory")
    _ach(A, "duel_100", "Tournament Champion", "Win 100 Tournament matches", "gold", 100, {"stat": "duel_wins"},
         "emblem:medal_victory")
    _ach(A, "flag_returns", "Defender of the Flag", "Return 50 flags", "bronze", 50, {"stat": "flag_returns"},
         "emblem:medal_defend")
    _ach(A, "clutch", "Last One Standing", "Win a Clan Arena round alone", "silver", 1, {"stat": "ca_clutches"},
         "emblem:medal_victory")
    _ach(A, "thaws", "Thawer", "Thaw 100 teammates in Freeze Tag", "bronze", 100, {"stat": "thaws"},
         "emblem:medal_assist")
    _ach(A, "instagib", "Instagibber", "Win 25 Instagib matches", "bronze", 25, {"stat": "wins_mode.instagib"},
         "emblem:iconw_railgun")
    # career
    for n, medal in [(10, "bronze"), (100, "silver"), (1000, "gold")]:
        _ach(A, "matches_%d" % n, "Arena Regular" if n == 10 else "Arena Fixture" if n == 100 else "Arena Eternal",
             "Play %d matches to the end" % n, medal, n, {"stat": "matches"}, "emblem:medal_frags")
        _ach(A, "wins_%d" % n, "Victor x%d" % n, "Win %d matches" % n, medal, n, {"stat": "wins"}, "emblem:medal_victory")
    _ach(A, "frags_1000", "Fragmaster", "1,000 frags", "silver", 1000, {"stat": "frags"}, "emblem:medal_frags")
    _ach(A, "frags_10000", "Fraglord", "10,000 frags", "gold", 10000, {"stat": "frags"}, "emblem:medal_frags")
    # campaign
    names = {0: "Arena Eligible"}
    for i, (tier, arenas) in enumerate(campaign):
        if tier in ("Training", "Final"):
            continue
        _ach(A, "campaign_%s" % tier.lower().replace(" ", ""), "%s Cleared" % tier, "Win every arena of %s" % tier,
             "bronze", len(arenas), {"campaign": arenas, "skill": 1}, "emblem:medal_victory")
    train = next(a for t, a in campaign if t == "Training")
    final = next(a for t, a in campaign if t == "Final")
    every = [m for _, a in campaign for m in a]
    _ach(A, "campaign_training", names[0], "Finish Training", "bronze", len(train), {"campaign": train, "skill": 1},
         "emblem:medal_victory")
    _ach(A, "campaign_final", "Xaero Slayer", "Beat Xaero in the Final", "silver", 1, {"campaign": final, "skill": 1},
         "emblem:head_xaero")
    _ach(A, "campaign_hardcore", "Arena Eternal", "Win every campaign arena on Hardcore", "silver", len(every),
         {"campaign": every, "skill": 4}, "emblem:medal_victory")
    _ach(A, "campaign_nightmare", "Nightmare Walker", "Win every campaign arena on Nightmare!", "gold", len(every),
         {"campaign": every, "skill": 5}, "emblem:medal_victory")
    # collections
    _ach(A, "tourist", "Tourist", "Win on every original arena", "gold", len(maps),
         {"set": "wins_map", "of": maps}, "emblem:medal_victory")
    _ach(A, "home_turf", "Home Turf", "Win 25 matches on one arena", "silver", 25, {"max": "wins_map"},
         "emblem:medal_victory")
    _ach(A, "roster", "Full Roster", "Play a match as every character", "silver", len(characters),
         {"set": "matches_model", "of": characters}, "emblem:head_sarge")
    _ach(A, "medal_collector", "Medal Collector", "Earn every kind of medal", "silver", len(MEDALS) + 1,
         {"set": "medal", "of": [m[0] for m in MEDALS] + ["perfect"]}, "emblem:medal_excellent")
    # ranked
    for tid, name, floor in RANKED_TIERS[2:]:
        _ach(A, "ranked_" + tid, name, "Reach %s in ranked (%d)" % (name, floor),
             {"gold": "bronze", "platinum": "silver"}.get(tid, "gold"), floor, {"stat": "ranked_peak"},
             "emblem:medal_victory")
    return A


# ---------------------------------------------------------------- the catalog

def _level_track(fixed, ordered, top=100):
    """Assign levels: `fixed` {id: level} stay put; `ordered` ids fill levels 2-10 first, then spread
    evenly over 11..top so no 5-level stretch is empty."""
    taken = set(fixed.values())
    early = [l for l in range(2, 11) if l not in taken]
    out = dict(fixed)
    rest = list(ordered)
    for l in early:
        if rest:
            out[rest.pop(0)] = l
    n = len(rest)
    for i, pid in enumerate(rest):
        out[pid] = 11 + round(i * (top - 11) / max(n - 1, 1))
    return out


def campaign_tiers(maps):
    """Q3Campaign's tiers: the 'single' arenas in file order, Training first, Final last, tiers of 4."""
    single = [m for m in maps if "single" in m.get("types", [])]
    training = [m["map"] for m in single if m.get("special", "").lower() == "training"]
    final = [m["map"] for m in single if m.get("special", "").lower() == "final"]
    rest = [m["map"] for m in single if not m.get("special")]
    tiers = [("Training", training)] if training else []
    tiers += [("Tier %d" % (i // 4 + 1), rest[i:i + 4]) for i in range(0, len(rest), 4)]
    return tiers + ([("Final", final)] if final else [])


def build(src):
    """src: maps (maps.json), bots, characters, skins {model: [skin]}, skies [(id, name)],
    materials (set of shader names that exist), icons (set of baked emblem ids), art {part id: path}."""
    maps, bots, chars = src["maps"], src["bots"], src["characters"]
    art = src["art"]
    tiers = campaign_tiers(maps)
    arena_maps = [m["map"] for m in maps]
    achs = achievements(tiers, arena_maps, chars)
    ach_ids = {a["id"] for a in achs}

    def P(id, name, unlock, **kw):
        d = dict(id=id, name=name, unlock=unlock)
        d.update({k: v for k, v in kw.items() if v is not None})
        if id in art:
            d["art"] = art[id]
        return d

    # --- level track
    sky_ids = ["sky_" + s for s, _ in src["skies"]]
    mat_ids = ["mat_" + b[0] for b in MATERIAL_BANNERS if b[2] in src["materials"]]
    track = []
    # interleave skies and materials so both kinds keep coming
    for i in range(max(len(sky_ids), len(mat_ids))):
        track += sky_ids[i:i + 1] + mat_ids[i:i + 1]
    accents = ["accent_" + a[0] for a in ACCENTS_EARNED[:-1]]  # gold is an achievement
    rails = ["rail_" + r[0] for r in RAIL_EARNED if r[0] in ("orange", "purple", "pink", "lime", "pulse")]
    # spread accents and rails through the track
    for i, pid in enumerate(accents + rails):
        track.insert(min(len(track), 2 + i * 4), pid)
    fixed = {"title_" + t[1]: t[0] for t in LEVEL_TITLES if t[0] > 1}
    fixed.update({"frame_" + f[1]: f[0] for f in LEVEL_FRAMES})
    levels = _level_track(fixed, track)
    L = lambda pid: {"level": levels[pid]}

    # --- banners
    banners = [P("void", "The Void", {"free": True}, kind="pattern", desc="Smoke drifting in the dark")]
    for m in maps:
        banners.append(P("map_" + m["map"], m["name"], {"stat": "wins_map." + m["map"], "min": 1}, kind="image",
                         desc="Win a match on %s" % m["name"]))
    for s, name in src["skies"]:
        banners.append(P("sky_" + s, name, L("sky_" + s), kind="image", fx="pan"))
    in_campaign = {}
    for m in maps:
        if "single" in m.get("types", []):
            for b in m.get("bots", []):
                in_campaign.setdefault(b.lower(), []).append(m["map"])
    for c in chars:
        name = next((b["name"] for b in bots if b["model"] == c and b.get("skin", "default") == "default"), c.capitalize())
        rules = [{"stat": "matches_model." + c, "min": 25}]
        arenas = in_campaign.get(name.lower()) or in_campaign.get(c)
        if arenas:
            rules.insert(0, {"any": [{"campaign": [a], "skill": 1} for a in arenas]} if len(arenas) > 1
                         else {"campaign": arenas, "skill": 1})
        banners.append(P("char_" + c, name, {"any": rules} if len(rules) > 1 else rules[0], kind="image",
                         desc=("Beat %s in the campaign, or play 25 matches as %s" % (name, name)) if len(rules) > 1
                         else "Play 25 matches as %s" % name))
    for bid, name, mat, animated in MATERIAL_BANNERS:
        if mat in src["materials"]:
            banners.append(P("mat_" + bid, name, L("mat_" + bid), kind="image", material="textures/" + mat,
                             fx="animated" if animated else None))
    for bid, name, ach, desc in [
            ("trophy_wall", "Trophy Wall", "medal_collector", "Your three best medal counts"),
            ("weapon_mastery", "Weapon Mastery", "arsenal", "Your best weapon and its frags"),
            ("favorite_arena", "Favorite Arena", "home_turf", "The arena you've won most"),
            ("rank_crest", "Rank Crest", "ranked_gold", "Your ranked tier's colors"),
            ("season", "Season", "ranked_platinum", "A new design every ranked season"),
            ("campaign_map", "Campaign Map", "campaign_training", "Every campaign arena and the skill you won it at")]:
        banners.append(P(bid, name, {"achievement": ach}, kind="stat", desc=desc))
    for bid, name, ach, desc, base in [
            ("quad_damage", "Quad Damage", "quad_god", "The card pulses with the Quad shell", "map_q3dm17"),
            ("lava_bath", "Lava Bath", "lava_bath", "Your favorite arena sinks into rising lava", "mat_lava"),
            ("longest_yard", "The Longest Yard", "rail_streak", "Rail trails cross the void", "map_q3dm17"),
            ("gibbed", "Gibbed", "medal_gauntlet_1000", "A gauntlet kill's gib fountain", "mat_bloody_blocks"),
            ("nightmare", "Nightmare!", "campaign_nightmare", "Xaero above the final arena", "char_xaero")]:
        banners.append(P(bid, name, {"achievement": ach}, kind="prestige", fx="animated", base=base, desc=desc))

    # --- emblems
    emblems = []
    for c in chars:
        for skin in src["skins"].get(c, []):
            eid = "head_%s" % c if skin == "default" else "head_%s_%s" % (c, skin)
            if eid not in src["icons"]:
                continue
            if skin == "default":
                rule = {"free": True}
            else:
                rule = {"stat": "matches_skin.%s/%s" % (c, skin), "min": 10}
            label = c.capitalize() if skin == "default" else "%s (%s)" % (c.capitalize(), skin.capitalize())
            emblems.append(P(eid, label, rule, kind="image", group="heads" if skin in ("default", "red", "blue") else "skins"))
    for key, name, icon in WEAPONS:
        if icon in src["icons"]:
            rule = {"achievement": "medal_gauntlet_100"} if key == "gauntlet" else {"achievement": "weapon_%s_100" % key}
            emblems.append(P(icon, name, rule, kind="image", group="weapons"))
    for key, name, icon in ITEMS:
        if icon in src["icons"]:
            emblems.append(P(icon, name, {"stat": "pickups." + key, "min": 50}, kind="image", group="items",
                             desc="Pick up %s 50 times" % name))
    for icon, name, rule in [("iconf_red", "Red Flag", {"achievement": "medal_capture_10"}),
                             ("iconf_blu", "Blue Flag", {"achievement": "flag_returns"})]:
        if icon in src["icons"]:
            emblems.append(P(icon, name, rule, kind="image", group="items"))
    medal_rule = {m[2]: {"achievement": "medal_%s_100" % m[0]} for m in MEDALS}
    medal_rule.update({"medal_accuracy": {"achievement": "rail_accuracy"}, "medal_frags": {"achievement": "frags_1000"},
                       "medal_victory": {"achievement": "wins_100"}})
    for icon, rule in medal_rule.items():
        if icon in src["icons"]:
            emblems.append(P(icon, icon[6:].capitalize(), rule, kind="image", group="medals"))

    # --- frames
    frames = [P("plain", "Plain", {"free": True}, style="plain")]
    for lvl, fid, name, mat, tint in LEVEL_FRAMES:
        frames.append(P("frame_" + fid, name, L("frame_" + fid), style="material", material="textures/" + mat, tint=tint))
    for key, name in POWERUP_FRAMES:
        frames.append(P("frame_" + key, name, {"achievement": "powerup_" + key}, style="shell", shell=key, fx="animated"))
    for tier, arenas in tiers:
        frames.append(P("frame_" + tier.lower().replace(" ", ""), tier, {"campaign": arenas, "skill": 1},
                        style="campaign", desc="Win every arena of %s" % tier))
    frames.append(P("frame_nightmare", "Nightmare!", {"achievement": "campaign_nightmare"}, style="campaign", fx="animated"))
    for tid, name, floor in RANKED_TIERS[1:]:
        frames.append(P("frame_ranked_" + tid, name, {"stat": "ranked_peak", "min": floor}, style="ranked",
                        desc="Reach %s in ranked" % name))

    # --- titles
    titles = [P("title_" + t[1], t[2], {"free": True} if t[0] == 1 else L("title_" + t[1])) for t in LEVEL_TITLES]
    for tid, name, ach in [
            ("excellent", "Excellent!", "medal_excellent_100"), ("impressive", "Impressive!", "medal_impressive_100"),
            ("humiliator", "Humiliator", "medal_gauntlet_100"), ("flag_runner", "Flag Runner", "medal_capture_100"),
            ("defender", "Defender", "medal_defend_100"), ("team_player", "Team Player", "medal_assist_100"),
            ("rail_god", "Rail God", "weapon_railgun_1000"), ("rocket_scientist", "Rocket Scientist", "weapon_rocket_1000"),
            ("plasma_junkie", "Plasma Junkie", "weapon_plasma_1000"), ("shaft_master", "Shaft Master", "weapon_lightning_1000"),
            ("buckshot", "Buckshot", "weapon_shotgun_1000"), ("grenadier", "Grenadier", "weapon_grenade_1000"),
            ("pea_shooter", "Pea Shooter", "weapon_machinegun_1000"), ("bfg_bandit", "BFG Bandit", "weapon_bfg_100"),
            ("strafe_jumper", "Strafe Jumper", "speed_600"), ("speed_demon", "Speed Demon", "speed_800"),
            ("rocket_jumper", "Rocket Jumper", "rocket_jumper"), ("plasma_climber", "Plasma Climber", "plasma_climber"),
            ("xaero_slayer", "Xaero Slayer", "campaign_final"), ("arena_eternal", "Arena Eternal", "campaign_hardcore"),
            ("nightmare_walker", "Nightmare Walker", "campaign_nightmare"), ("recruit", "Recruit", "campaign_training"),
            ("cratered", "Cratered", "cratered"), ("lava_swimmer", "Lava Swimmer", "lava_swimmer"),
            ("telefragged", "Telefragged", "telefragged"), ("sank", "Sank Like a Rock", "sank"),
            ("perfect", "Perfect", "perfect"), ("untouchable", "Untouchable", "untouchable"),
            ("telefragger", "Telefragger", "telefragger"), ("tourist", "Tourist", "tourist"),
            ("collector", "Full Roster", "roster"), ("show_off", "Show-Off", "trifecta"),
            ("duelist", "Duelist", "duelist"), ("clutch", "Last One Standing", "clutch"),
            ("quad_god", "Quad God", "quad_god"), ("deadeye", "Deadeye", "rail_accuracy")]:
        titles.append(P("title_" + tid, name, {"achievement": ach}))
    for tid, name, floor in RANKED_TIERS[1:]:
        titles.append(P("title_ranked_" + tid, name, {"stat": "ranked_peak", "min": floor}, desc="Reach %s in ranked" % name))

    # --- accents, rail trails, taunts
    accents = [P("accent_" + a[0], a[1], {"free": True}, value=a[2]) for a in ACCENTS_FREE]
    for aid, name, value in ACCENTS_EARNED:
        accents.append(P("accent_" + aid, name, {"achievement": "wins_1000"} if aid == "gold" else L("accent_" + aid),
                         value=value))
    rails = [P("rail_" + r[0], r[1], {"free": True}, value=r[2]) for r in RAIL_FREE]
    for rid, name, value, fx in RAIL_EARNED:
        rule = {"team": {"achievement": "medal_capture_10"}, "gold": {"achievement": "weapon_railgun_1000"},
                "rainbow": {"achievement": "rail_streak"}}.get(rid) or L("rail_" + rid)
        rails.append(P("rail_" + rid, name, rule, value=value, fx=fx))
    taunts = [P("taunt_" + c, next((b["name"] for b in bots if b["model"] == c), c.capitalize()), {"free": True},
                sound="sound/player/%s/taunt.wav" % c) for c in chars]

    parts = dict(banners=banners, emblems=emblems, frames=frames, titles=titles, accents=accents, rails=rails,
                 taunts=taunts)
    # every achievement-unlocked part points at a real achievement
    for kind, lst in parts.items():
        for p in lst:
            rules = p["unlock"].get("any", [p["unlock"]])
            for r in rules:
                if "achievement" in r and r["achievement"] not in ach_ids:
                    raise ValueError("%s %s unlocks from unknown achievement %s" % (kind, p["id"], r["achievement"]))
    return {
        "_note": "Player card catalog (tools/card_catalog.py, baked by build_cards.py). Structure from trac3r's "
                 "cosmetics.ts/achievements.ts; see PLAN.md 'Player cards, progression and ranked'.",
        "version": 1,
        "levels": {"max": 100, "xp_base": 1000, "xp_step": 25,
                   "_note": "XP to go from level L to L+1 = xp_base + xp_step * (L - 1)"},
        "default_card": {"banner": "void", "emblem": "head_sarge", "frame": "plain", "title": "title_fresh_meat",
                         "accent": "accent_red", "taunt": "taunt_sarge", "rail": "rail_red",
                         "stats": ["frags", "wins", "excellents"], "badges": []},
        "card_stats": [dict(id=s[0], label=s[1], value=s[2]) for s in CARD_STATS],
        "medal_colors": {"bronze": "#e08a4f", "silver": "#c9d6df", "gold": "#ffd24a"},
        "ranked_tiers": [dict(id=t[0], name=t[1], min=t[2]) for t in RANKED_TIERS],
        "ranked": {"division": RANKED_DIVISION, "placements": RANKED_PLACEMENTS, "start": 1000},
        "skills": SKILLS,
        "stats_doc": STATS_DOC,
        "parts": parts,
        "achievements": achs,
    }
