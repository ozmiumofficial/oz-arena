"""Generates engine/ui/data/leaderboards.json (and, until P12, profile.json): stand-in data for the
Leaderboards page from the Godot build's dev_state.json (profile_demo, cards) and mock.ts's board.
Only names, ids and numbers the pages draw; art paths point at the player's own paks."""
import json, os, re, zipfile, glob

GODOT = r"F:/quake-3-arena-remastered-fan-project"
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
d = json.load(open(GODOT + "/webui/out/q3/dev_state.json", encoding="utf-8"))
cat = d["cards"]
prof = d["profile_demo"]
U = {k: set(v) for k, v in prof["unlocked"].items()}

PAK = set()
SHADER_MAPS = {}  # shader name -> its image stages (the paks' scripts/*.shader)
for p in sorted(glob.glob(r"C:/steam/steamapps/common/Quake 3 Arena/baseq3/pak[0-8].pk3")):
    z = zipfile.ZipFile(p)
    for n in z.namelist():
        PAK.add(n.lower())
        if n.lower().startswith("scripts/") and n.lower().endswith(".shader"):
            text = re.sub(r"//[^\n]*", "", z.read(n).decode("latin-1"))
            for m in re.finditer(r"([\w/\-]+)\s*\{((?:[^{}]|\{[^{}]*\})*)\}", text):
                maps = re.findall(r"\b(?:map|clampmap|animmap\s+[\d.]+)\s+([\w/\-.]+)", m.group(2), re.I)
                SHADER_MAPS.setdefault(m.group(1).lower(), [re.sub(r"\.(tga|jpg)$", "", x, flags=re.I) for x in maps if not x.startswith("$")])


def img(path):
    """a pak image without its extension, or '' when the paks don't have it"""
    if not path:
        return ""
    for ext in (".tga", ".jpg", ".png"):
        if path.lower() + ext in PAK:
            return "/" + path
    return ""


def material(name):
    """a material's look in the paks: its own image, a stage of its shader named like it, or its image
    without the shader's suffix (blocks15_ow -> blocks15, km_arena1tower4fx -> km_arena1tower4)"""
    if not name:
        return ""
    if img(name):
        return img(name)
    base = name.rsplit("/", 1)[-1].lower()
    for m in SHADER_MAPS.get(name.lower(), []):
        b = m.rsplit("/", 1)[-1].lower()
        if "." not in b and img(m) and len(os.path.commonprefix([b, base])) >= 5:  # not the glow layers (x.blend.tga)
            return img(m)
    for cand in (re.sub(r"_[a-z0-9]+$", "", name), re.sub(r"fx$", "", name)):
        if cand != name and img(cand):
            return img(cand)
    return ""


def fmt(n):
    return "{:,}".format(int(round(n)))


def part(kind, pid):
    for p in cat["parts"][kind]:
        if p["id"] == pid:
            return p
    return None


def rgb(hexc):
    h = hexc.lstrip("#")[:6]
    return [int(h[i:i + 2], 16) for i in (0, 2, 4)]


def lighten(hexc, t=0.3):
    return "#%02x%02x%02x" % tuple(int(c + (255 - c) * t) for c in rgb(hexc))


def darken(hexc, t=0.55):
    return "#%02x%02x%02x" % tuple(int(c * (1 - t)) for c in rgb(hexc))


TIER = {"bronze": "#c27a45", "silver": "#c3ccd6", "gold": "#ffc93c", "platinum": "#4fe0c8", "diamond": "#6ab8ff", "godlike": "#ff3d22"}
MEDAL = cat["medal_colors"]
NONE = "none"
FALLBACK_ICON = "/menu/medals/medal_frags"

# ---------------------------------------------------------------- banners as decorators

SMOKE = "radial-gradient(ellipse at 70% 60%, #2a1512 0%, #070605 70%)"
PLATE = "repeating-linear-gradient(135deg, #1a1412 0dp, #1a1412 7dp, #14100e 7dp, #14100e 14dp)"
SKY = {"sky_hellsky": "#7a1a0a", "sky_hellsky2bright": "#a8401a", "sky_kc_dm10sky": "#5a3a6a", "sky_kcbasesky_arena1_sky": "#3a4a6a",
       "sky_killsky": "#6a2a1a", "sky_nightsky_xian_dm1": "#1a2a5a", "sky_skybox": "#4a5a7a"}
SKYTEX = {"sky_hellsky": "textures/skies/intelredclouds", "sky_hellsky2bright": "textures/skies/intelredclouds", "sky_kc_dm10sky": "textures/skies/inteldimclouds",
          "sky_kcbasesky_arena1_sky": "textures/skies/pjbasesky", "sky_killsky": "textures/skies/killsky_1", "sky_nightsky_xian_dm1": "textures/skies/bluedimclouds",
          "sky_skybox": "textures/skies/topclouds"}
MATC = {"lava": "#ff6a1a", "slime": "#6acf2a", "blue": "#3a6aff", "red": "#c81a0a", "gold": "#ffc83a", "electric": "#5ab4ff", "flesh": "#8a2a2a"}


def mat_color(p):
    n = p["id"] + " " + p.get("material", "")
    for k, c in MATC.items():
        if k in n:
            return c
    return "#6a5a4a"


def banner(pid):
    """(bg, fg) decorators for a banner part: bg fills the 4:1 box, fg (or none) lies over it"""
    p = part("banners", pid)
    if not p or pid == "void":
        return SMOKE, NONE
    if pid.startswith("map_"):
        return "image(/levelshots/%s cover)" % pid[4:], NONE
    if pid.startswith("sky_"):
        tex = img(SKYTEX.get(pid, ""))
        c = SKY.get(pid, "#4a3a3a")
        if tex:
            return "image(%s cover)" % tex, "linear-gradient(90deg, %s99 0%%, %s33 100%%)" % (c, c)
        return "linear-gradient(120deg, %s 0%%, %s 50%%, %s 100%%)" % (darken(c, 0.7), c, darken(c, 0.8)), NONE
    if pid.startswith("char_"):
        icon = img("models/players/%s/icon_default" % pid[5:])
        return "linear-gradient(90deg, #070605 0%, #2a1512 60%, #3a1a12 100%)", ("image(%s contain right)" % icon if icon else NONE)
    if pid.startswith("mat_"):
        tex = material(p.get("material", ""))
        if tex:
            return "image(%s cover)" % tex, NONE
        c = mat_color(p)
        return "linear-gradient(120deg, %s 0%%, %s 55%%, %s 100%%)" % (darken(c, 0.75), darken(c, 0.35), darken(c, 0.85)), NONE
    if p.get("kind") == "stat":
        if pid == "favorite_arena":
            return "image(/levelshots/q3dm17 cover)", NONE
        if pid == "weapon_mastery":
            return PLATE, "image(/icons/iconw_rocket contain left)"
        if pid == "trophy_wall":
            return PLATE, "image(/menu/medals/medal_excellent contain left)"
        if pid == "rank_crest":
            c = TIER["platinum"]
            return "linear-gradient(110deg, #0a0807 30%%, %s55 55%%, #0a0807 56%%, #0a0807 62%%, %s33 63%%, #0a0807 80%%)" % (c, c), NONE
        if pid == "campaign_map":
            return PLATE, "repeating-linear-gradient(90deg, #ffad33cc 0dp, #db2410cc 5dp, #00000000 5dp, #00000000 9dp)"
        return "linear-gradient(120deg, #1a1408 0%, #6a4a12 50%, #140f06 100%)", "repeating-linear-gradient(120deg, #00000000 0dp, #00000000 16dp, #ffffff0a 16dp, #ffffff0a 17dp)"
    if p.get("kind") == "prestige":
        base, _ = banner(p.get("base", "void"))
        over = {"quad_damage": "radial-gradient(ellipse at 50% 50%, #466effb3 0%, #283cff40 60%, #00000000 100%)",
                "lava_bath": "linear-gradient(180deg, #00000000 0%, #ff6e1480 45%, #ff3c00d9 70%, #3a0800 100%)",
                "longest_yard": "linear-gradient(90deg, #00000000 0%, #3060ff80 60%, #ffffffb3 100%)",
                "gibbed": "radial-gradient(ellipse at 50% 90%, #8a0a04cc 0%, #00000000 60%)",
                "nightmare": "radial-gradient(ellipse at 50% 40%, #00000000 30%, #b400008c 100%)"}.get(pid, NONE)
        return base, over
    return SMOKE, NONE


# ---------------------------------------------------------------- frames, emblems, rails

CAMPAIGN_FRAME = {"frame_training": "#6b625b", "frame_tier1": "#7a5a3a", "frame_tier2": "#8a6a3a", "frame_tier3": "#9a7a3a", "frame_tier4": "#a8562a",
                  "frame_tier5": "#b8402a", "frame_tier6": "#c02a1a", "frame_final": "#e0b040", "frame_nightmare": "#d01010"}
SHELL = {"frame_quad": "#4a6bff", "frame_battlesuit": "#ffcc33", "frame_regen": "#ff3030", "frame_haste": "#ff9a30", "frame_invis": "#b8d8ff"}


def frame(fid, accent):
    """(decorator, padding) for a frame part around a card of that accent"""
    f = part("frames", fid)
    if not f or f.get("style") == "plain":
        return "linear-gradient(160deg, %saa 0%%, #2a2320 40%%, #141110 100%%)" % accent, "0.14em"
    if f.get("style") == "material":
        c = f.get("tint", accent)
        tex = material(f.get("material", ""))
        if tex:  # the texture under a bevel: lit top edge, shaded foot (frameStyle's inset shadows)
            return ("linear-gradient(180deg, #ffffff2e 0%%, #ffffff00 6%%, #00000000 80%%, #000000a6 100%%), "
                    "linear-gradient(160deg, #00000026 0%%, #00000073 100%%), image(%s cover)" % tex), "0.42em"
        return "linear-gradient(160deg, %s 0%%, %s 30%%, %s 60%%, %s 100%%)" % (darken(c, 0.2), darken(c, 0.7), darken(c, 0.5), darken(c, 0.8)), "0.42em"
    if f.get("style") == "shell":
        c = SHELL.get(fid, accent)
        return "linear-gradient(160deg, %s 0%%, %s 50%%, %s 100%%)" % (lighten(c, 0.2), darken(c, 0.5), c), "0.42em"
    c = TIER.get(fid.replace("frame_ranked_", ""), accent) if f.get("style") == "ranked" else CAMPAIGN_FRAME.get(fid, accent)
    return "linear-gradient(160deg, %s 0%%, #1a1412 45%%, %s88 100%%)" % (c, c), "0.36em"


def emblem_art(eid):
    if eid.startswith("head_"):
        rest = eid[5:]
        m, skin = rest.split("_", 1) if "_" in rest else (rest, "default")
        return img("models/players/%s/icon_%s" % (m, skin))
    if eid.startswith("iconw_"):
        return img("icons/" + eid)
    if eid.startswith("medal_"):
        return img("menu/medals/" + eid)
    return img("icons/" + eid)


def rail_deco(v):
    if v == "spectrum":
        return "linear-gradient(90deg, #ff3030 0%, #ffb020 17%, #f0ff30 33%, #30ff60 50%, #30c0ff 67%, #6040ff 83%, #ff30d0 100%)", "#ffffff"
    if v == "team":
        return "linear-gradient(90deg, #ff2a2a 0%, #ff2a2a 50%, #2a5aff 50%, #2a5aff 100%)", "#ffffff"
    return "linear-gradient(90deg, %s 0%%, %s 100%%)" % (v, v), v


def accent_of(aid):
    p = part("accents", aid)
    return p["value"] if p else "#ff2020"


# ---------------------------------------------------------------- the part grids

def group_of(kind, p):
    if kind == "banners":
        if p.get("kind") == "stat":
            return "Career"
        if p.get("kind") == "prestige":
            return "Prestige"
        return {"map": "Arenas", "sky": "Skies", "char": "Characters", "mat": "Materials"}.get(p["id"].split("_")[0], "Default")
    if kind == "emblems":
        return {"heads": "Characters", "skins": "Skins", "weapons": "Weapons", "items": "Items", "medals": "Medals"}.get(p.get("group", ""), "Other")
    if kind == "frames":
        return {"plain": "Default", "material": "Level track", "shell": "Powerups", "campaign": "Campaign", "ranked": "Ranked"}.get(p.get("style", ""), "Other")
    if kind in ("titles", "accents", "rails"):
        return "Free" if p.get("free") else "Level track" if p.get("level") else "Earned"
    return "Characters"


def sentence(s):
    return s[:1] + s[1:].lower() if s else s


def how_of(p):
    h = p.get("how") or ""
    if not h and p["id"].startswith("head_"):
        rest = p["id"][5:]
        m, skin = rest.split("_", 1) if "_" in rest else (rest, "")
        h = "PLAY 10 MATCHES AS %s IN THE %s SKIN" % (m.upper(), skin.upper())
    return h


SLOT_KIND = {"banner": "banners", "emblem": "emblems", "frame": "frames", "title": "titles", "accent": "accents", "rail": "rails", "taunt": "taunts"}


def grid(slot):
    kind = SLOT_KIND[slot]
    parts = list(cat["parts"][kind])
    if kind in ("titles", "accents", "rails"):
        parts.sort(key=lambda p: 0 if p["id"] in U[kind] else 1)
    groups = []
    for p in parts:
        g = group_of(kind, p)
        hit = next((x for x in groups if x["name"] == g), None)
        if not hit:
            hit = {"name": g, "open": 0, "total": 0, "items": []}
            groups.append(hit)
        is_open = p["id"] in U[kind]
        how = how_of(p)
        item = {"id": p["id"], "name": p["name"], "open": is_open,
                "level": "L%d" % p["level"] if p.get("level") and not is_open else "",
                "info": (p.get("desc") or "Unlocked") if is_open else how,
                "toast": "%s: %s" % (p["name"], sentence(how))}
        if kind == "banners":
            item["bg"], item["fg"] = banner(p["id"])
        elif kind == "emblems":
            item["art"] = emblem_art(p["id"])
        elif kind == "taunts":
            item["art"] = emblem_art(p["id"].replace("taunt_", "head_"))
        elif kind == "frames":
            item["deco"], item["pad"] = frame(p["id"], "#db2410")
        elif kind == "titles":
            item["sub"] = "Free" if p.get("free") else (p.get("desc") or sentence(how))
        elif kind == "accents":
            item["value"] = p["value"]
        elif kind == "rails":
            item["beam"], c = rail_deco(p.get("value", "#0050ff"))
            item["spiral"] = "repeating-linear-gradient(110deg, #00000000 0dp, #00000000 6dp, %saa 6dp, %saa 8dp)" % (c, c)
        if kind in ("emblems", "taunts") and not item["art"]:
            continue  # nothing in the player's paks to draw
        hit["items"].append(item)
        hit["total"] += 1
        hit["open"] += 1 if is_open else 0
    return [g for g in groups if g["items"]]


# ---------------------------------------------------------------- achievements and badges

ach_state = {a["id"]: a for a in prof["achievements"]}
GROUPS = [
    ("Medals", lambda i: i.startswith("medal_") and i != "medal_collector"),
    ("Weapons", lambda i: i.startswith("weapon_") or i == "arsenal"),
    ("Powerups", lambda i: i.startswith("powerup_") or i in ("every_powerup", "quad_god")),
    ("Feats", lambda i: i in ("perfect", "untouchable", "rail_streak", "rail_accuracy", "air_rocket", "telefragger", "trifecta")),
    ("Movement", lambda i: i.startswith("speed_") or i in ("rocket_jumper", "plasma_climber")),
    ("Accidents", lambda i: i in ("lava_swimmer", "lava_bath", "cratered", "sank", "telefragged")),
    ("Modes", lambda i: i in ("duelist", "duel_100", "flag_returns", "clutch", "thaws", "instagib")),
    ("Career", lambda i: re.match(r"^(matches|wins|frags)_\d+$", i) is not None),
    ("Campaign", lambda i: i.startswith("campaign_")),
    ("Collections", lambda i: i in ("tourist", "home_turf", "roster", "medal_collector")),
    ("Ranked", lambda i: i.startswith("ranked_")),
]


def ach_group(i):
    return next((g for g, f in GROUPS if f(i)), "Other")


def icon_of(icon):
    kind, _, iid = icon.partition(":")
    return emblem_art(iid) if kind == "emblem" else ""


map_names = {m["map"]: m["name"] for m in d["maps"]}
achievements = []
for a in cat["achievements"]:
    s = ach_state.get(a["id"], {"done": False, "progress": 0, "missing": []})
    done = bool(s.get("done"))
    secret = bool(a.get("hidden")) and not done
    prog = s.get("progress") or 0
    target = a.get("target") or 1
    pays = []
    for k, plist in cat["parts"].items():
        for p in plist:
            if p.get("ach") == a["id"]:
                t = "%s %s" % (p["name"], k.rstrip("s"))
                if t not in pays:
                    pays.append(t)
    icon = icon_of(a.get("icon", ""))
    missing = s.get("missing") or []
    achievements.append({
        "id": a["id"], "group": ach_group(a["id"]), "state": "Earned" if done else "In progress" if prog > 0 else "Not started", "done": done, "secret": secret,
        "name": "Hidden achievement" if secret else a["name"], "desc": "Keep playing to find out." if secret else a["desc"],
        "medal": a["medal"], "color": MEDAL[a["medal"]], "icon": icon or FALLBACK_ICON, "has_icon": bool(icon) and not secret,
        "bar": (not done and not secret and target > 1), "pct": "%.1f%%" % (min(1, prog / max(1, target)) * 100),
        "progress": "%s / %s" % (fmt(min(prog, target)), fmt(target)),
        "missing": ("Still to go: " + ", ".join(map_names.get(m, m) for m in missing)) if (not done and not secret and 0 < len(missing) <= 8) else "",
        "pays": ("Pays: " + ", ".join(pays)) if pays and not secret else "",
    })
ach_done = sum(1 for a in achievements if a["done"])
ach_groups = ["All"] + [g for g, _ in GROUPS if any(a["group"] == g for a in achievements)] + (["Other"] if any(a["group"] == "Other" for a in achievements) else [])
medal_totals = [{"name": m, "n": sum(1 for a in achievements if a["medal"] == m and a["done"]), "of": sum(1 for a in achievements if a["medal"] == m), "color": MEDAL[m]}
                for m in ("bronze", "silver", "gold")]

card = prof["card"]
badges_pick = []
for a in achievements:
    if a["secret"]:
        continue
    slot = card["badges"].index(a["id"]) + 1 if a["id"] in card["badges"] else 0
    badges_pick.append({"id": a["id"], "name": a["name"], "open": a["done"], "slot": slot, "icon": a["icon"], "color": a["color"],
                        "info": ("Badge %d on your card" % slot) if slot else a["desc"],
                        "toast": "%s: earn this achievement to wear its medal." % a["name"]})

stats_pick = []
for s in cat["card_stats"]:
    slot = card["stats"].index(s["id"]) + 1 if s["id"] in card["stats"] else 0
    stats_pick.append({"id": s["id"], "label": s["label"], "value": prof["stats"].get(s["id"], "-"), "slot": slot,
                       "info": ("Slot %d on your card" % slot) if slot else "Select to show it on your card"})


# ---------------------------------------------------------------- the card

def card_of(c, level, stats, xp=None, ranked=None, ident="LOCAL"):
    accent = accent_of(c["accent"])
    deco, pad = frame(c["frame"], accent)
    bg, fg = banner(c["banner"])
    tp = part("titles", c["title"])
    out = {
        "level": level, "accent": accent, "title_color": lighten(accent, 0.3), "title": tp["name"] if tp else "",
        "frame": deco, "pad": pad, "banner_bg": bg, "banner_fg": fg,
        "emblem": emblem_art(c["emblem"]) or "/models/players/sarge/icon_default",
        "trim": "linear-gradient(90deg, %s 0%%, %s55 60%%, %s00 100%%)" % (accent, accent, accent),
        "glow": "%s66" % accent,
        "stats": [], "badges": [], "id": ident,
        "xp_pct": "0%", "xp_text": "", "xp_bar": "linear-gradient(90deg, %saa 0%%, %s 100%%)" % (accent, accent),
        "ranked": ranked is not None, "rank": "", "rank_color": "#8d847c", "mmr": "", "pips": [],
        "rank_deco": "linear-gradient(165deg, #2a2320 0%, #1a1614 100%)", "rank_mark": "#8d847c",
    }
    for i in range(3):
        sid = c["stats"][i] if i < len(c["stats"]) else ""
        lab = next((s["label"] for s in cat["card_stats"] if s["id"] == sid), "")
        out["stats"].append({"v": stats.get(sid, "-") if sid else "", "l": lab})
    for i in range(3):
        bid = c["badges"][i] if i < len(c["badges"]) else ""
        a = next((x for x in achievements if x["id"] == bid), None)
        out["badges"].append({"on": a is not None, "icon": a["icon"] if a else FALLBACK_ICON, "color": a["color"] if a else "#ffffff14"})
    if xp:
        out["xp_pct"] = "%.1f%%" % (min(1, xp[0] / max(1, xp[1])) * 100)
        out["xp_text"] = "%s / %s XP" % (fmt(xp[0]), fmt(xp[1]))
    if ranked:
        r = ranked["rank"]
        col = TIER[r["id"]]
        out["rank"] = r["label"].upper()
        out["rank_color"] = col
        out["mmr"] = "%s MMR" % fmt(ranked["rating"])
        out["pips"] = [{"c": col}] * (4 - r["division"] if r["division"] else 0)
        out["rank_deco"] = "linear-gradient(165deg, %s 0%%, %s 45%%, %s 100%%)" % (lighten(col, 0.45), col, darken(col, 0.55))
        out["rank_mark"] = lighten(col, 0.65)
    return out


SLOTS = [("banner", "Banner"), ("emblem", "Emblem"), ("frame", "Frame"), ("title", "Title"), ("accent", "Accent"), ("rail", "Rail"), ("taunt", "Taunt"), ("stats", "Stats"), ("badges", "Badges")]
TIPS = {
    "banner": "Wide art across the top of your card. Arena banners come from winning there, characters from beating them in the campaign.",
    "emblem": "The portrait on your card. Every default head is free; skins, weapons, items and medals are earned.",
    "frame": "The border around your card. The level track pays one every 10 levels; powerup frames pulse.",
    "title": "The line under your name.",
    "accent": "Your card's trim, glow and XP bar. The original game's seven colors are free.",
    "rail": "Your rail trail's color in matches. Forced rail colors and competitive settings override it for whoever turns them on.",
    "taunt": "The voice line when your card appears on the duel versus screen. Selecting one plays it.",
    "stats": "Three career stats shown on your card, in the order you pick them.",
    "badges": "Up to three achievement medals shown on your card.",
}

grids = {s: grid(s) for s in SLOT_KIND}
slots = []
for sid, label in SLOTS:
    if sid in grids:
        count = "%d/%d" % (sum(g["open"] for g in grids[sid]), sum(g["total"] for g in grids[sid]))
    else:
        count = "%d/3" % len(card[sid])
    slots.append({"id": sid, "label": label, "count": count, "tip": TIPS[sid]})

# ---------------------------------------------------------------- career

c = prof["career"]
WEAPONS = ["gauntlet", "machinegun", "shotgun", "grenade", "rocket", "lightning", "railgun", "plasma", "bfg"]
top = max(1, *[c["frags_weapon"].get(w, 0) for w in WEAPONS])
fav = sorted(c["matches_model"].items(), key=lambda x: -x[1])[0]
hours = c["playtime"] / 3600
nxt = []
for k, plist in cat["parts"].items():
    for p in plist:
        if p.get("level", 0) > prof["level"]:
            nxt.append((p["level"], list(cat["parts"]).index(k), k, p))
nxt.sort(key=lambda x: (x[0], x[1]))
next_unlocks = []
for lvl, _, k, p in nxt[:6]:
    e = {"level": "L%d" % lvl, "name": p["name"], "kind": k.rstrip("s").upper(), "swatch": "text", "deco": NONE, "color": "#8d847c"}
    if k == "banners":
        e["swatch"], e["deco"] = "banner", banner(p["id"])[0]
    elif k == "frames":
        e["swatch"], e["deco"] = "frame", frame(p["id"], "#db2410")[0]
    elif k == "accents":
        e["swatch"], e["color"] = "dot", p["value"]
    elif k == "rails":
        e["swatch"], e["deco"] = "rail", rail_deco(p["value"])[0]
    next_unlocks.append(e)

MODE_NAMES = [("ffa", "Free For All"), ("duel", "Tournament"), ("tdm", "Team DM"), ("ctf", "CTF"), ("ca", "Clan Arena"), ("ft", "Freeze Tag"), ("instagib", "Instagib")]
MEDALS = ["excellent", "impressive", "gauntlet", "capture", "assist", "defend", "perfect"]
career = {
    "level": prof["level"], "max": prof["max_level"], "total": fmt(prof["total"]),
    "xp": "%s / %s" % (fmt(prof["xp"]), fmt(prof["next"])), "to_next": "%s to level %d" % (fmt(prof["next"] - prof["xp"]), min(prof["max_level"], prof["level"] + 1)),
    "pct": "%.1f%%" % (prof["xp"] / max(1, prof["next"]) * 100),
    "next": next_unlocks,
    "rows": [{"k": k, "v": v} for k, v in [
        ("Matches", fmt(c["matches"])), ("Wins", fmt(c["wins"])), ("Win rate", "%d%%" % round(c["wins"] / c["matches"] * 100) if c["matches"] else "-"),
        ("Frags", fmt(c["frags"])), ("Deaths", fmt(c["deaths"])), ("K/D", "%.2f" % (c["frags"] / max(1, c["deaths"]))),
        ("Time played", "%.1f h" % hours if hours >= 1 else "%d min" % round(c["playtime"] / 60)), ("Favorite character", "%s (%d)" % fav)]],
    "weapons": [{"name": "BFG" if w == "bfg" else w.capitalize(), "icon": "/icons/iconw_" + w, "n": fmt(c["frags_weapon"].get(w, 0)),
                 "pct": "%.1f%%" % (c["frags_weapon"].get(w, 0) / top * 100)} for w in WEAPONS],
    "medals": [{"label": "HUMILIATION" if m == "gauntlet" else m.upper(), "icon": "/menu/medals/medal_" + ("victory" if m == "perfect" else m),
                "n": fmt(c["medal"].get(m, 0)), "zero": not c["medal"].get(m, 0)} for m in MEDALS],
    "modes": [{"name": n, "n": fmt(c["wins_mode"].get(i, 0))} for i, n in MODE_NAMES],
}

profile = {
    "_note": "Stand-in data for the Profile page (pages/profile.rml) until progression and player cards (P12): the Godot build's mid-career demo profile (dev_state.json profile_demo) and the parts of its card catalog the page draws. Art comes from the player's own paks (levelshots, player icons, weapon, item and medal icons, textures); gradients for the rest.",
    "tab": "card", "slot": "banner", "locked": True,
    "hover_on": False, "hover_name": "", "hover_info": "", "hover_locked": False, "name_dirty": False,
    "level": prof["level"], "ach_done": ach_done, "ach_total": len(achievements),
    "cur": {k: card[k] for k in ("banner", "emblem", "frame", "title", "accent", "rail", "taunt")},
    "card": card_of(card, prof["level"], prof["stats"], xp=(prof["xp"], prof["next"]), ranked=prof["ranked"]),
    "slots": slots,
    "grid": grids,
    "stats": stats_pick,
    "badges": badges_pick,
    "skin": "Default",
    "colors": ["#db2410", "#ff6a00", "#ffcc00", "#7dff3a", "#22e0c8", "#2f8cff", "#5a3bff", "#d23cff", "#ff4f9a", "#ffffff", "#a0a0a0", "#000000"],
    "codes": [{"code": "^%d" % i, "color": col} for i, col in enumerate(["#5a5a5a", "#ff0000", "#00ff00", "#ffff00", "#4a6bff", "#00ffff", "#ff00ff", "#ffffff"])],
    "ach": {"group": "All", "show": "All", "groups": ach_groups, "list": achievements, "done": ach_done, "total": len(achievements),
            "pct": "%.1f%%" % (ach_done / max(1, len(achievements)) * 100), "medals": medal_totals},
    "career": career,
}

# ---------------------------------------------------------------- leaderboards (mock.ts mockBoard)

TITLES = ["title_xaeros_equal", "title_warlord", "title_gladiator", "title_contender", "title_fragger", "title_initiate"]
BANNERS = ["map_q3dm17", "sky_hellsky", "mat_lava", "char_xaero", "map_q3tourney2", "mat_skull_arch", "void"]
ACCENTS = ["accent_gold", "accent_plasma", "accent_red", "accent_bronze"]
PODIUM = ["#ffd24a", "#c9d6df", "#e08a4f"]


def board(scope, names):
    rows = []
    for i, (name, model) in enumerate(names):
        xp = round(190000 / (1 + i * 0.55))
        level, left = 1, xp
        while level < 100 and left >= 1000 + 25 * (level - 1):
            left -= 1000 + 25 * (level - 1)
            level += 1
        mine = name == "^1Ranger"
        cardd = {"banner": BANNERS[i % len(BANNERS)], "emblem": "head_" + model, "frame": "frame_gold" if i < 3 else "plain", "title": TITLES[i % len(TITLES)],
                 "accent": ACCENTS[i % 4], "stats": ["frags", "wins", "kd"], "badges": []}
        stats = {"frags": str(9000 - i * 600), "wins": str(700 - i * 50), "kd": "%.2f" % (2.1 - i * 0.1)}
        cd = card_of(cardd, level, stats)
        rows.append({"scope": scope, "id": "me" if mine else "p%d" % i, "rank": i + 1, "name": name, "level": level, "xp": fmt(xp),
                     "online": i % 3 != 2, "mine": mine, "podium": i < 3, "rank_color": PODIUM[i] if i < 3 else "#8d847c",
                     "glow": (PODIUM[i] + "88") if i < 3 else "#00000000",
                     "accent": cd["accent"], "title": cd["title"], "emblem": cd["emblem"], "banner": cd["banner_bg"], "card": cd})
    return rows


GLOBAL = [["^1F^7atal", "xaero"], ["^4Visor", "visor"], ["Strafe^3God", "sarge"], ["Klesk", "klesk"], ["^5Railz", "slash"], ["^1Ranger", "ranger"],
          ["Mynx", "mynx"], ["^2Doom", "doom"], ["^3Anarki", "anarki"], ["Orbb", "orbb"], ["Hunter", "hunter"], ["Crash", "crash"]]
FRIENDS = [["^4Visor", "visor"], ["Klesk", "klesk"], ["^1Ranger", "ranger"], ["^3Anarki", "anarki"], ["Orbb", "orbb"]]


rows_g, rows_f = board("global", GLOBAL), board("friends", FRIENDS)
me_g = next(r for r in rows_g if r["mine"])
me_f = next(r for r in rows_f if r["mine"])

leaderboards = {
    "_note": "Stand-in data for the Leaderboards page (pages/leaderboards.rml) until the online services (P7): mock.ts's pretend level board (the mock friends and strangers, highest XP first) in both scopes and an empty ranked board.",
    "tab": "level", "scope": "global", "pick": "me",
    "rows": rows_g + rows_f,
    "count": {"global": "%d players" % len(rows_g), "friends": "%d players" % len(rows_f)},
    "you": {"global": "You: #%d" % me_g["rank"], "friends": "You: #%d" % me_f["rank"]},
    "tiers": [{"id": t["id"], "name": t["name"], "color": TIER[t["id"]], "deco": "linear-gradient(165deg, %s 0%%, %s 45%%, %s 100%%)" % (lighten(TIER[t["id"]], 0.45), TIER[t["id"]], darken(TIER[t["id"]], 0.55)),
               "mark": lighten(TIER[t["id"]], 0.65)} for t in cat["ranked_tiers"]],
}


def leaf(v):
    return not isinstance(v, (dict, list))


def flat(v):
    if leaf(v):
        return True
    if isinstance(v, list):
        return all(leaf(x) for x in v) or (all(isinstance(x, dict) and all(leaf(y) for y in x.values()) for x in v) and len(json.dumps(v)) < 120)
    return all(flat(x) for x in v.values()) and len(json.dumps(v, ensure_ascii=False)) < 600


def dump(v, ind=0):
    """JSON with one line per leaf object, so a part or a row reads as one line"""
    pad = "\t" * (ind + 1)
    if isinstance(v, dict):
        if ind > 0 and flat(v):
            return json.dumps(v, ensure_ascii=False)
        return "{\n" + ",\n".join(pad + json.dumps(k) + ": " + dump(x, ind + 1) for k, x in v.items()) + "\n" + "\t" * ind + "}"
    if isinstance(v, list):
        if flat(v):
            return json.dumps(v, ensure_ascii=False)
        return "[\n" + ",\n".join(pad + dump(x, ind + 1) for x in v) + "\n" + "\t" * ind + "]"
    return json.dumps(v, ensure_ascii=False)


out = os.path.join(REPO, "engine", "ui", "data")
# profile.json is the engine's since P12 (oz_ui_profile.cpp fills it from the career): only the boards' stand-in
for name, obj in (("leaderboards", leaderboards),):
    path = os.path.join(out, name + ".json")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(dump({name: obj}) + "\n")
    print(path, os.path.getsize(path))
