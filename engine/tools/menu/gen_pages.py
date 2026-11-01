"""Builds engine/ui/pages/{offline_bots,create_match,campaign}.rml from tpl/*.rml.in: the long data
expressions (RmlUi's expressions have no loops, so the checks over an arena's types and bots are
written out) replace @@NAME@@ tokens."""
import os, re

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))), "engine", "ui", "pages")
GT, LT, AND = "&gt;", "&lt;", "&amp;&amp;"
NTYPES, NBOTS, NFILL, NSLOTS = 6, 7, 18, 11


def has_type(m, t):
    """the arena expression m lists type t (an expression) among its first NTYPES types; ternaries,
    so the checks stop at the first match or the last type (RmlUi's || evaluates both sides)"""
    out = "false"
    for k in reversed(range(NTYPES)):
        out = f"({m}.types.size {GT} {k} ? ({m}.types[{k}] == {t} ? true : {out}) : false)"
    return out


def any_type(m):
    """the arena is made for one of the pages' modes (ffa, tourney, team, ctf)"""
    out = "false"
    for k in reversed(range(NTYPES)):
        x = f"{m}.types[{k}]"
        out = f"({m}.types.size {GT} {k} ? ({x} == 'ffa' || {x} == 'tourney' || {x} == 'team' || {x} == 'ctf' ? true : {out}) : false)"
    return out


def first_mode(m):
    """the first of the modes (ffa, duel, tdm, ctf) arena m is made for"""
    return f"{has_type(m, chr(39) + 'ffa' + chr(39))} ? 0 : {has_type(m, chr(39) + 'tourney' + chr(39))} ? 1 : {has_type(m, chr(39) + 'team' + chr(39))} ? 2 : 3"


def tokens(P, Q, nmodes, m="m", i="i", show=None):
    """P: the page's state (mi, the mode), Q: what its map browser shows (fit, the mode's map type, and
    mapi, the arena): a top-level key of its own, so that the other choices don't make RmlUi check
    every card again. m and i: a card's arena and its index in maps; show: whether the card shows at all
    (Skirmish's search)"""
    MD = f"create_match.modes[{P}.mi]"
    T = f"{Q}.fit"
    SEL = f"maps[{Q}.mapi]"
    fits_sel = f"(maps.size {GT} {Q}.mapi ? {has_type(SEL, T)} : false)"
    fix = f"{Q}.fit = {MD}.fit; {Q}.mapi = {fits_sel} ? {Q}.mapi : {MD}.pick"
    shown = (lambda x: f"{show} ? ({x}) : false") if show else (lambda x: x)
    return {
        "MINE": shown(has_type(m, T)),
        "OTHER": shown(f"{has_type(m, T)} ? false : {any_type(m)}"),
        "PICK_OTHER": f"{Q}.mapi = {i}; {P}.mi = {first_mode(m)}; {Q}.fit = {MD}.fit",
        "FITS_SEL": fits_sel,
        "NOT_FITS_SEL": f"!{fits_sel}",
        "FIX": fix,
        "MODE_PREV": f"{P}.mi = {P}.mi {GT} 0 ? {P}.mi - 1 : {nmodes - 1}; {fix}",
        "MODE_NEXT": f"{P}.mi = {P}.mi {LT} {nmodes - 1} ? {P}.mi + 1 : 0; {fix}",
        "MODE_OPTIONS": "|".join(str(i) for i in range(nmodes)),
    }


# the modifiers a custom match may have changed, back to the original game's (the map's worldspawn sets g_gravity)
RESET = "cmd set g_quadfactor 3; cmd set g_weaponrespawn 5; cmd set g_friendlyFire 0"


def skirmish():
    """Skirmish: its cards are skirmish_view's (v.i: the arena's index in maps, v.show: the search
    shows it), and its mod is skirmish.mod (oz_ui_skirmish.cpp): under CPMA startmatch plays the match
    under CPMA's game, under Defrag the race action races the picked race map"""
    P = "create_match.skirmish"
    Q = "create_match_pick.skirmish"
    M = f"maps[{Q}.mapi]"
    MD = f"create_match.modes[{P}.mi]"
    prog = [f"{P}.mod = skirmish.mod", f"{P}.map = {M}.map", f"{P}.n = {P}.mi == 1 ? 1 : {P}.count * 1", f"{P}.nmb = {M}.bots.size"]
    prog += [f"{P}.k{k} = {P}.nmb {GT} {k} ? to_lower({M}.bots[{k}]) : '-'" for k in range(NBOTS)]
    prog += [f"{P}.bots.m{k} = {P}.n {GT} {k} {AND} {P}.nmb {GT} {k} ? {M}.bots[{k}] : ''" for k in range(NBOTS)]
    prog += [f"{P}.nm = {P}.nmb {GT} {P}.n ? {P}.n : {P}.nmb", f"{P}.c = 0"]
    for j in range(NFILL):
        dup = " || ".join(f"to_lower(bots[{j}].name) == {P}.k{k}" for k in range(NBOTS))
        prog += [
            f"{P}.d = bots.size {GT} {j} ? ({dup} ? 1 : 0) : 1",
            f"{P}.bots.f{j} = {P}.d == 0 {AND} {P}.n - {P}.nm {GT} {j} - {P}.c ? bots[{j}].name : ''",
            f"{P}.c = {P}.c + {P}.d",
        ]
    prog += [
        f"{P}.gametype = {MD}.gametype", f"{P}.fraglimit = {MD}.rules.fraglimit", f"{P}.timelimit = {MD}.rules.timelimit",
        f"{P}.capturelimit = {MD}.rules.capturelimit",
        f"{P}.maxplayers = {P}.n + 1 {GT} {MD}.rules.max_players ? {P}.n + 1 : {MD}.rules.max_players",
    ]
    t = tokens(P, Q, 4, m="maps[v.i]", i="v.i", show="v.show")
    W = f"{MD}.rules.warmup"
    t["PROGRAM"] = "; ".join(prog)
    t["START"] = (f"!skirmish.ready ? 'sound buzz; toast ' + skirmish.mod_name + ' has to be imported first' : "
                  f"skirmish.mod == 'defrag' ? 'race' : "
                  f"{t['FITS_SEL']} ? '{RESET}; cmd set g_doWarmup ' + ({W} {GT} 0 ? 1 : 0) + '; cmd set g_warmup ' + {W} + "
                  f"'; startmatch create_match.skirmish' + ({MD}.team ? '; append wait 5; append team auto' : '') : "
                  f"'sound buzz; toast ' + {MD}.name + ' needs an arena made for it'")
    return t


def custom():
    P = "create_match.custom"
    Q = "create_match_pick.custom"
    M = f"maps[{Q}.mapi]"
    MD = f"create_match.modes[{P}.mi]"
    S = lambda k: f"{P}.slots[{k}]"
    prog = [f"{P}.map = {M}.map", f"{P}.gametype = {MD}.gametype", f"{P}.fraglimit = {MD}.rules.fraglimit",
            f"{P}.timelimit = {MD}.rules.timelimit", f"{P}.capturelimit = {MD}.rules.capturelimit", f"{P}.maxplayers = {MD}.rules.max_players"]
    for k in range(NSLOTS):
        prog += [
            f"{P}.bots.b{k}.name = {S(k)}.on {AND} bots.size {GT} {S(k)}.i ? bots[{S(k)}.i].name : ''",
            f"{P}.bots.b{k}.skill = {S(k)}.skill",
            f"{P}.bots.b{k}.team = {MD}.team ? to_lower({S(k)}.team) : ''",
        ]
    skill = " : ".join(f"{S(k)}.on ? {S(k)}.skill" for k in range(NSLOTS)) + " : 3"
    prog.append(f"{P}.skill = {skill}")
    t = tokens(P, Q, 7)
    W = f"{MD}.rules.warmup"
    modv = lambda k: f"create_match.modifiers[{k}].values[create_match.modifiers[{k}].v]"
    # g_gravity after the map has loaded (its worldspawn sets it), and only when changed (a map may have its own);
    # Online hosts the same match (host: a listen server, listed by oz_host, P8)
    go = (f"'cmd set g_quadfactor ' + {modv(5)} + '; cmd set g_weaponrespawn ' + {modv(7)} + "
          f"'; cmd set g_friendlyFire ' + {modv(8)} + '; cmd set g_doWarmup ' + ({W} {GT} 0 ? 1 : 0) + '; cmd set g_warmup ' + {W} + "
          f"'; ' + ({P}.server == 'Offline' ? 'startmatch' : 'host') + ' create_match.custom' + "
          f"(create_match.modifiers[6].v == 0 ? '' : '; append set g_gravity ' + {modv(6)}) + "
          f"({P}.you == 'Play' ? ({MD}.team ? '; append wait 5; append team auto' : '') : '; append wait 150; append team spectator')")
    t["PROGRAM"] = "; ".join(prog)
    t["START"] = (f"{MD}.soon ? 'sound buzz; toast ' + {MD}.name + ' is not in the game code yet' : "
                  f"{P}.server == 'LAN (your local network)' ? 'soon lan' : "
                  f"!{t['FITS_SEL']} ? 'sound buzz; toast ' + {MD}.name + ' needs an arena made for it' : {go}")
    count = " + ".join(f"({S(k)}.on ? 1 : 0)" for k in range(NSLOTS))
    first_off = " : ".join(f"!{S(k)}.on ? {k}" for k in range(NSLOTS - 1)) + f" : {NSLOTS - 1}"
    t["ADD_BOT"] = (f"{count} + 1 {GT}= {MD}.rules.max_players || {count} {GT}= {NSLOTS} ? 'sound buzz' : "
                    f"'set create_match.custom.slots.' + ({first_off}) + '.on true'")
    return t


def campaign():
    A = "maps[a.mi]"
    in_arena = " || ".join(f"({A}.bots.size {GT} {k} ? to_lower(b.name) == to_lower({A}.bots[{k}]) : false)" for k in range(NBOTS))
    # the game's own single player (oz_ui_profile.cpp "campaign"): the arena's bots, limits and the postgame's awards
    start = f"t.open ? 'campaign ' + {A}.map : 'sound buzz'"
    return {"IN_ARENA": in_arena, "START": start}


def build(name, t):
    src = open(os.path.join(HERE, "tpl", name + ".rml.in"), encoding="utf-8").read()
    out = re.sub(r"@@(\w+)@@", lambda m: t[m.group(1)], src)
    left = re.findall(r"@@\w+@@", out)
    assert not left, left
    path = os.path.join(OUT, name + ".rml")
    open(path, "w", encoding="utf-8", newline="\n").write(out)
    print(path, len(out))


build("offline_bots", skirmish())
build("create_match", custom())
build("campaign", campaign())
