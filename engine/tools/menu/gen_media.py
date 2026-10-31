"""Stand-in data for the replays and cinematics pages (P5), from the Godot build's dev_state.json and mock.ts."""
import json, re, sys, datetime, os

DEV = r"F:\quake-3-arena-remastered-fan-project\webui\out\q3\dev_state.json"
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))), "engine", "ui", "data")
d = json.load(open(DEV, encoding="utf-8"))



# replays: webui/lib/mock.ts's makeReplays, its times as the web capture shows them
BOTS = ["Sarge", "Xaero", "^1Klesk", "Anarki", "Crash", "Hunter", "Doom"]
RMAPS = [["q3dm17", "The Longest Yard", "ffa", "Free for All"], ["q3tourney2", "The Proving Grounds", "duel", "Duel"],
         ["q3dm6", "The Camping Grounds", "ffa", "Free for All"], ["q3ctf1", "Dueling Keeps", "ctf", "Capture the Flag"],
         ["q3dm7", "Temple of Retribution", "tdm", "Team Deathmatch"]]
WHEN = ["Oct 29  06:12 AM", "Oct 29  04:12 AM", "Oct 29  02:12 AM", "Oct 29  12:12 AM", "Oct 28  10:12 PM"]
rl = []
for i, (mp, mname, mode, mode_name) in enumerate(RMAPS):
    n = 1 if mode == "duel" else 3 + (i % 4)
    players = [{"name": "^3Ozmium", "score": 20 - i * 3, "local": True, "bot": False}]
    players += [{"name": b, "score": 18 - k * 4 - i, "local": False, "bot": True} for k, b in enumerate(BOTS[:n])]
    players.sort(key=lambda p: -p["score"])  # stable, as JS's sort
    place = next(k for k, p in enumerate(players) if p["local"]) + 1
    for k, p in enumerate(players):
        p["n"] = k + 1
    complete = i != 4
    length = 0 if i == 4 else (6 + i * 2) * 60000 + 13000
    result = "" if i == 4 else ("VICTORY" if place == 1 else "DEFEAT")
    name = "That rail" if i == 1 else ""
    s = round(length / 1000)
    b = (6 + i * 2) * 900000
    r = {
        "id": "r%d" % (i + 1), "map": mp, "shot": "/levelshots/" + mp, "map_name": mname, "mode_name": mode_name,
        "name": name, "favorite": i == 1, "when": WHEN[i],
        "sub": ("%s, " % mname if name else "") + "%s  -  %d %s" % (mode_name, len(players), "player" if len(players) == 1 else "players"),
        "length": "%d:%02d" % (s // 60, s % 60) if length else "--:--",
        "badge": result or "UNFINISHED", "tone": "amber" if result == "VICTORY" else "red" if result == "DEFEAT" else "dim",
        "result": "%s (#%d)" % (result, place) if result else ("Left early" if complete else "Unfinished"),
        "size": "%.1f MB" % (b / 1048576) if b >= 1048576 else "%d KB" % max(1, round(b / 1024)),
        "complete": complete, "players": players,
    }
    rl.append(r)
replays = {
    "_note": "Replays (ReplaysPage.tsx): stand-in matches as the web's mock (webui/lib/mock.ts) makes them, until the replay system (P10). pick: the replay shown on the right.",
    "replays": {"tab": "all", "pick": "r1", "first": "r1", "first_fav": next(r["id"] for r in rl if r["favorite"]), "kept": 50, "list": rl},
}

# cinematics: the Godot build's list (dev_state.json), each played from the player's pak0 (video/<id>.RoQ)
SHOTS = {"intro": "q3dm0", "tier1": "q3tourney1", "tier2": "q3tourney2", "tier3": "q3tourney3", "tier4": "q3tourney4",
         "tier5": "q3tourney5", "tier6": "q3dm19", "tier7": "q3tourney6"}
ROQ = {"idlogo", "intro", "tier1", "tier2", "tier3", "tier4", "tier5", "tier6", "tier7", "end"}  # pak0.pk3's video/*.RoQ


def lock_text(u):
    if u == "final":
        return "Win the final duel"
    if u.startswith("tier:"):
        n = int(u.split(":")[1])
        return "Finish Training" if n == 1 else "Finish Tier %d" % (n - 1)
    return ""


movies = []
for i, c in enumerate(d["cinematics"]):
    sec = c["seconds"]
    play = "close; cmd set nextmap \"{cinematics.list.%d.back}\"; cmd cinematic %s.RoQ" % (i, c["id"]) if c["id"] in ROQ else "soon cinematics"
    movies.append({
        "id": c["id"], "title": c["title"].replace("Quake III Arena", "the original game"),
        "length": "%d:%02d" % (sec // 60, round(sec % 60)),
        "shot": ("/levelshots/" + SHOTS[c["id"]]) if c["id"] in SHOTS else "",
        "unlocked": bool(c["unlocked"]), "lock": lock_text(c["unlock"]),
        "do": play if c["unlocked"] else "sound buzz",
        "back": "ui_page menu; ui_run sub extras; ui_page cinematics; ui_focus cin_%s" % c["id"],
    })
cin = {
    "_note": "Cinematics (CinematicsPage.tsx): the Godot build's list (dev_state.json). Play runs the original RoQ movie from the player's pak0; the upscaled .ogv movies come with the Theora player (P3). unlocked comes from the campaign's progress with P12 (all open until then). back: what runs when the movie ends (the cvar nextmap, as q3_ui's cinematics menu does): this page again, on that movie. credits: the Credits page's state (Extras, as the cinematics).",
    "cinematics": {"list": movies},
    "credits": {"paused": False},
}

for name, obj in (("replays", replays), ("cinematics", cin)):
    with open(os.path.join(OUT, name + ".json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(obj, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print(name, os.path.getsize(os.path.join(OUT, name + ".json")))
