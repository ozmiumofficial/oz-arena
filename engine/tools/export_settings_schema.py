"""Export the Godot build's settings schema to engine/ui/settings.json, once (ENGINE_PLAN.md P4 step 4).

python engine/tools/export_settings_schema.py [path/to/schema.gd]

The Godot build (F:/quake-3-arena-remastered-fan-project, reference only) keeps every setting as
data in godot/scripts/settings/schema.gd. This reads it without Godot: the file's constants and its
build() body are plain enough to run as Python after a few textual fixes (var, true/false, Color,
to_lower). The result is the same list the Godot menu shows, with what the engine needs added:

- cvar: the cvar that holds it. A setting an engine or cgame cvar already holds maps onto that cvar
  (ENGINE below), with option values, a scale or the cvar's own default where they differ. The rest
  get an oz_<tab>_<name> cvar of their own; "wired": false says nothing reads it yet.
- create: the engine creates the cvar (archived) at startup. Cvars the engine registers itself are
  only bound; cgame ones are created with the cgame's default, so the page works before a match.
- command and binds (type "bind"): the console command and its default keys in the engine's names.
- preview: crosshair (the crosshair tab's live preview), cm360 (mouse travel per turn), confirm
  (the 15 s "keep these display settings?" check).

Player-visible text gets no game title (CLAUDE.md), so the schema's references are reworded here.
The output is committed; run this again only to pick up a schema change.
"""
import os, re, sys, json

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), "ui", "settings.json")
SCHEMA = sys.argv[1] if len(sys.argv) > 1 else "F:/quake-3-arena-remastered-fan-project/godot/scripts/settings/schema.gd"

# settings an existing cvar holds: the engine's own (bound only) or the cgame's (created with its default)
ENGINE = {
    "video/vsync": {"cvar": "r_swapInterval", "values": {"off": "0", "on": "1", "adaptive": "-1"}, "drop": ["mailbox"],
                    "restart": True},
    "video/max_fps": {"cvar": "com_maxfps", "default": 125},
    "video/fov": {"cvar": "cg_fov", "create": True, "default": 90.0},
    "video/brightness": {"cvar": "r_gamma"},
    "graphics/anisotropy": {"cvar": "r_ext_max_anisotropy", "default": 8},
    "graphics/texture_quality": {"cvar": "r_picmip", "values": {"low": "2", "medium": "1", "high": "0"}, "restart": True},
    "graphics/decals": {"cvar": "cg_marks", "create": True},
    "graphics/gibs": {"cvar": "cg_gibs", "create": True},
    "audio/vol_master": {"cvar": "s_volume", "scale": 0.01, "default": 80},
    "audio/vol_music": {"cvar": "s_musicVolume", "scale": 0.01, "default": 25},
    "audio/mute_unfocused": {"cvar": "s_muteWhenUnfocused"},
    "mouse/sensitivity": {"cvar": "sensitivity"},
    "mouse/acceleration": {"cvar": "cl_mouseAccel"},
    "mouse/invert": {"cvar": "m_pitch", "values": {"false": "0.022", "true": "-0.022"}},
    "crosshair/size": {"cvar": "cg_crosshairSize", "create": True},
    "crosshair/health_color": {"cvar": "cg_crosshairHealth", "create": True, "default": True},
    "hud/chat_time": {"cvar": "con_notifyTime", "default": 3},
    "hud/fps": {"cvar": "cg_drawFPS", "create": True},
    "hud/net_graph": {"cvar": "cg_lagometer", "create": True, "default": True},
    "hud/team_chats_only": {"cvar": "cg_teamChatsOnly", "create": True},
    "profile/name": {"cvar": "name", "default": "UnnamedPlayer"},
    "profile/model": {"cvar": "model", "default": "sarge"},
}

# controls: the console command of each action (replay ones wait for P10's replay viewer)
COMMANDS = {
    "forward": "+forward", "back": "+back", "moveleft": "+moveleft", "moveright": "+moveright",
    "jump": "+moveup", "crouch": "+movedown", "walk": "+speed", "attack": "+attack", "zoom": "+zoom",
    "weapnext": "weapnext", "weapprev": "weapprev", "use_item": "+button2", "gesture": "+button3",
    "scores": "+scores", "chat": "messagemode", "teamchat": "messagemode2", "voice": "+voiprecord",
    "screenshot": "screenshotJPEG", "console": "toggleconsole",
}
for n in range(1, 10):
    COMMANDS["weapon%d" % n] = "weapon %d" % n

# Godot's key names -> the engine's (qcommon/keys.c); Godot's joypad buttons are SDL's order
KEYS = {"Up": "UPARROW", "Down": "DOWNARROW", "Left": "LEFTARROW", "Right": "RIGHTARROW", "Space": "SPACE",
        "Ctrl": "CTRL", "Shift": "SHIFT", "Enter": "ENTER", "Tab": "TAB", "QuoteLeft": "`", "Pause": "PAUSE",
        "Minus": "-", "Equal": "=", "Comma": ",", "Period": ".", "BracketLeft": "[", "BracketRight": "]",
        "Home": "HOME", "End": "END"}
MOUSE = {"1": "MOUSE1", "2": "MOUSE2", "3": "MOUSE3", "4": "MWHEELUP", "5": "MWHEELDOWN"}
PAD = ["A", "B", "X", "Y", "BACK", "GUIDE", "START", "LEFTSTICK_CLICK", "RIGHTSTICK_CLICK", "LEFTSHOULDER",
       "RIGHTSHOULDER", "DPAD_UP", "DPAD_DOWN", "DPAD_LEFT", "DPAD_RIGHT"]
AXES = {"4:1": "PAD0_LEFTTRIGGER", "5:1": "PAD0_RIGHTTRIGGER"}

# no game title in player-visible text; checked below
REWORD = [
    ("Modern (Quake Live style)", "Modern"), ("Quake Live beam", "Thick beam"), ("Quake Live is one thick glowing beam",
    "Thick beam is one thick glowing beam"), ("Quake 3 classic", "Classic"), ("Quake 3's ten crosshairs", "The ten classic crosshairs"),
    ("Quake 3's id logo and intro", "The id logo and intro"), ("Quake 3's soundtrack", "The original soundtrack"),
    ("Quake 3's menu had no music", "The original menu had no music"), ("Quake 3's cg_crosshairHealth", "Like cg_crosshairHealth"),
    ("Quake 3's cl_mouseAccel", "Like cl_mouseAccel"), ("Quake 3's lagometer", "The lagometer"), ("like Quake 3's cg_fov", "as cg_fov is"),
    ("Like Quake 3's cg_fov", "As cg_fov is"), ("Quake 3 units", "Classic units"), ("Quake 3's tier movie", "the original tier movie"),
    ("as in Quake 3 (CG_AddPainTwitch)", "as in the original (CG_AddPainTwitch)"), ("Respawning works as in Quake 3", "Respawning works as it always has"),
    ("Quake 3's", "the original"), ("Quake 3", "the original"), ("Quake Live", "modern"),
]


def gd_to_py(src):
    """The schema's constants and build() body as Python source."""
    py = ""
    for m in re.finditer(r"^const (\w+) := ", src, re.M):
        i, depth = m.end(), 0
        while True:   # to the closing bracket of the literal
            c = src[i]
            depth += (c in "[{(") - (c in "]})")
            i += 1
            if depth == 0 and c in "]})":
                break
        py += "%s = %s\n" % (m.group(1), src[m.end():i])
    body = src[src.index("var L: Array = []"):src.index("_list = L")]
    lines = []
    for line in body.splitlines():
        line = line[1:] if line.startswith("\t") else line   # build()'s own indent
        line = re.sub(r"\bvar (\w+)\s*(:\s*\w+)?\s*:?=", r"\1 =", line)
        lines.append(line)
    py += "\n".join(lines)
    py = py.replace(".to_lower()", ".lower()").replace("true", "True").replace("false", "False")
    return py


def color_hex(r, g, b, a=1.0):
    return "#%02x%02x%02x" % tuple(int(round(max(0.0, min(1.0, c)) * 255)) for c in (r, g, b))


def entry(key, label, type_, default, tip, group, extra=None):
    d = {"key": key, "tab": key.split("/")[0], "label": label, "type": type_, "default": default, "tip": tip,
         "group": group, "wired": True, "restart": False}
    d.update(extra or {})
    return d


def load_schema():
    env = {"Color": color_hex, "_e": entry, "L": []}
    exec(gd_to_py(open(SCHEMA, encoding="utf-8").read()), env)
    return env["TABS"], env["PRESETS"], env["L"]


def godot_key(s):
    """key:W / mouse:1 / joy:0 / joyaxis:5:1 -> the engine's key name."""
    if not s:
        return ""
    kind, _, rest = s.partition(":")
    if kind == "key":
        return KEYS.get(rest, rest.lower() if len(rest) == 1 else rest.upper())
    if kind == "mouse":
        return MOUSE[rest]
    if kind == "joy":
        return "PAD0_" + PAD[int(rest)]
    if kind == "joyaxis":
        return AXES[rest]
    raise ValueError(s)


def reword(s):
    upper = s[:1].isupper()
    s = re.sub(r"^Quake 3 ([A-J])$", r"Classic \1", s)   # the crosshairs
    for a, b in REWORD:
        s = s.replace(a, b)
    return s[0].upper() + s[1:] if s and upper else s


def cvar_string(e, value):
    """A setting's value as its cvar holds it."""
    if e["type"] == "bool":
        v = "true" if value else "false"
        return e["values"][v] if "values" in e else ("1" if value else "0")
    if e["type"] == "enum":
        return e.get("values", {}).get(str(value), str(value))
    if e["type"] in ("int", "float"):
        v = value * e.get("scale", 1)
        return ("%g" % v) if e["type"] == "float" or "scale" in e else str(int(v))
    return str(value)


def main():
    tabs, presets, schema = load_schema()
    out = []
    for e in schema:
        key = e["key"]
        e["label"] = reword(e["label"])
        e["tip"] = reword(e["tip"])
        e["group"] = reword(e["group"])
        if "options" in e:
            e["options"] = [[o[0], reword(o[1])] for o in e["options"]]
        m = ENGINE.get(key)
        if e["type"] == "bind":
            action = key.split("/")[1]
            e["command"] = COMMANDS.get(action, "oz_" + action)
            e["wired"] = action in COMMANDS
            e["binds"] = [godot_key(b) for b in e["default"]]
            e["default"] = e["binds"]
        elif m:
            e["cvar"] = m["cvar"]
            e["create"] = m.get("create", False)
            for k in ("values", "scale", "restart"):
                if k in m:
                    e[k] = m[k]
            if "drop" in m:
                e["options"] = [o for o in e["options"] if o[0] not in m["drop"]]
            if "default" in m:
                e["default"] = m["default"]
        else:
            e["cvar"] = "oz_" + key.replace("/", "_")
            e["create"] = True
            e["wired"] = False
        if "cvar" in e:
            e["cvar_default"] = cvar_string(e, e["default"])
        if key.startswith("crosshair/"):
            e["preview"] = "crosshair"
        elif key in ("mouse/sensitivity", "mouse/dpi"):
            e["preview"] = "cm360"
        elif key in ("video/display_mode", "video/resolution", "video/monitor"):
            e["preview"] = "confirm"
        else:
            e["preview"] = ""
        out.append(e)

    text = json.dumps({"_note": "Exported once from the Godot build's godot/scripts/settings/schema.gd by "
                                "engine/tools/export_settings_schema.py (its docstring explains the fields). "
                                "The engine creates the cvars and generates the Settings page from it.",
                       "tabs": [[t[0], reword(t[1])] for t in tabs],
                       "presets": presets,
                       "settings": out}, indent=1, ensure_ascii=False)
    visible = json.dumps([[e["label"], e["tip"], e["group"], e.get("options")] for e in out])
    if "Quake" in visible:
        sys.exit("player-visible text still names the game: " + ", ".join(sorted(set(re.findall(r"[^\"]{0,30}Quake[^\"]{0,20}", visible)))))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, "w", encoding="utf-8", newline="\n").write(text + "\n")
    by_type = {}
    for e in out:
        by_type[e["type"]] = by_type.get(e["type"], 0) + 1
    print("%d settings (%s), %d on existing cvars, %d oz_ cvars, %d binds -> %s" % (
        len(out), ", ".join("%d %s" % (n, t) for t, n in sorted(by_type.items())),
        sum(1 for e in out if e["key"] in ENGINE), sum(1 for e in out if e.get("cvar", "").startswith("oz_")),
        by_type.get("bind", 0), OUT))


main()
