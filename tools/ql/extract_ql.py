"""Quake Live's rules and item amounts, read from the player's Quake Live install.

python tools/ql/extract_ql.py [--ql <Quake Live baseq3>] [--dump work/ql]

  tools/ql/ql_rules.json   every cvar default from the game DLL's cvar table, the item table (pickup
                           amounts), the factories (per-mode overrides), and "standard_movement": the
                           movement this remaster plays (QUAKE_LIVE_PLAN.md decision 1)
  <dump>/                  the raw files for reading (factories.txt, mappools, cvar list)

Quake Live's game code is closed, so its numbers come from data in its own files:
  bin.pk3/qagamex86.dll   the cvarTable (24 bytes an entry: vmCvar*, name*, default*, flags, two
                          more fields) and bg_itemlist (72 bytes: classname*, pickup sound*, 8 model
                          slots, icon*, pickup name*, quantity, giType, giTag, 3 more fields)
  pak00.pk3               scripts/factories.txt (JSON with // comments)
Both tables are found by walking out from a name that must be in them, one entry at a time, until
an entry no longer holds valid strings.
"""
import os, re, json, glob, struct, zipfile, hashlib, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

DEFAULT_QL = [
    r"C:\Program Files (x86)\Steam\steamapps\common\Quake Live\baseq3",
    r"C:\Program Files\Steam\steamapps\common\Quake Live\baseq3",
    r"D:\SteamLibrary\steamapps\common\Quake Live\baseq3",
    r"E:\SteamLibrary\steamapps\common\Quake Live\baseq3",
    os.path.expanduser("~/.steam/steam/steamapps/common/Quake Live/baseq3"),
]
OUT_RULES = os.path.join(HERE, "ql_rules.json")

CVAR_STRIDE = 24
ITEM_STRIDE = 72
GI_TYPES = ["bad", "weapon", "ammo", "armor", "health", "powerup", "holdable", "persistant_powerup", "team", "key"]


# --- the game DLL -----------------------------------------------------------------------------

class PE:
    """Just enough of a 32-bit PE to follow pointers into its sections."""

    def __init__(self, data: bytes):
        self.d = data
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        nsec = struct.unpack_from("<H", data, pe + 6)[0]
        optsz = struct.unpack_from("<H", data, pe + 20)[0]
        self.base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
        self.secs = []
        o = pe + 24 + optsz
        for _ in range(nsec):
            vsize, va, rsize, raw = struct.unpack_from("<IIII", data, o + 8)
            self.secs.append((va, max(vsize, rsize), raw, rsize))
            o += 40

    def offset(self, v):
        r = v - self.base
        for va, size, raw, rsize in self.secs:
            if va <= r < va + size:
                return raw + r - va if r - va < rsize else None
        return None

    def address(self, off):
        for va, size, raw, rsize in self.secs:
            if raw <= off < raw + rsize:
                return self.base + va + off - raw
        return None

    def string(self, v):
        """The printable C string at address v, or None."""
        o = self.offset(v) if v else None
        if o is None:
            return None
        e = self.d.find(b"\0", o)
        s = self.d[o:e]
        if len(s) > 255 or any(c < 32 or c > 126 for c in s):
            return None
        return s.decode("ascii")

    def refs(self, s):
        """File offsets of every dword pointing at the string s."""
        hit = self.d.find(b"\0" + s.encode() + b"\0")
        if hit < 0:
            raise SystemExit("not in the DLL: " + s)
        p = struct.pack("<I", self.address(hit + 1))
        return [i for i in range(0, len(self.d) - 3, 4) if self.d[i:i + 4] == p]

    def dwords(self, off, n):
        return struct.unpack_from("<%dI" % n, self.d, off)


def walk(pe, anchor, stride, valid):
    """Entry offsets of the table holding the entry at `anchor`, in order."""
    start = anchor
    while start - stride >= 0 and valid(start - stride):
        start -= stride
    out, o = [], start
    while o + stride <= len(pe.d) and valid(o):
        out.append(o)
        o += stride
    return out


CVAR_NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


def cvar_table(pe):
    anchor = next(o - 4 for o in pe.refs("pmove_JumpVelocity") if pe.string(pe.dwords(o + 4, 1)[0]) == "275.0f")

    def valid(o):
        w = pe.dwords(o, 4)
        name = pe.string(w[1])
        return name is not None and CVAR_NAME.fullmatch(name) and pe.string(w[2]) is not None

    out = {}
    for o in walk(pe, anchor, CVAR_STRIDE, valid):
        w = pe.dwords(o, 4)
        out[pe.string(w[1])] = {"default": pe.string(w[2]), "flags": w[3]}
    return out


def item_table(pe):
    anchor = pe.refs("item_armor_shard")[0]

    def valid(o):
        w = pe.dwords(o, 15)
        return pe.string(w[0]) is not None and w[13] < len(GI_TYPES)

    out = {}
    for o in walk(pe, anchor, ITEM_STRIDE, valid):
        w = pe.dwords(o, 15)
        models = [m for m in (pe.string(x) for x in w[2:10]) if m]
        out[pe.string(w[0])] = {"name": pe.string(w[11]), "quantity": w[12], "type": GI_TYPES[w[13]], "tag": w[14],
                                "models": models, "icon": pe.string(w[10])}
    return out


# --- main -------------------------------------------------------------------------------------

def find_ql(cands):
    for c in cands:
        if c and os.path.isfile(os.path.join(c, "pak00.pk3")):
            return c
    raise SystemExit("Quake Live not found; pass its baseq3 folder with --ql")


def sha1(path):
    return hashlib.sha1(open(path, "rb").read()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--ql", help="Quake Live's baseq3 folder")
    ap.add_argument("--dump", default=os.path.join(ROOT, "work", "ql"), help="where the raw files go")
    a = ap.parse_args()
    ql = find_ql([a.ql] + DEFAULT_QL)
    pak00, binpk3 = os.path.join(ql, "pak00.pk3"), os.path.join(ql, "bin.pk3")
    zp, zb = zipfile.ZipFile(pak00), zipfile.ZipFile(binpk3)

    pe = PE(zb.read("qagamex86.dll"))
    cvars = cvar_table(pe)
    items = item_table(pe)
    factories = {}
    for f in json.loads(re.sub(r"//[^\n]*", "", zp.read("scripts/factories.txt").decode("latin1"))):
        factories[f["id"]] = {"title": f["title"], "basegt": f["basegt"], "cvars": f["cvars"]}

    # decision 1 (Ozmium, 2026-10-23): the duel factory's movement set in every mode
    duel_moves = {k: v for k, v in factories["duel"]["cvars"].items() if k.startswith("pmove_")}
    standard = {k: v["default"] for k, v in cvars.items() if k.startswith("pmove_")}
    standard.update(duel_moves)

    rules = {
        "_note": "Read from Quake Live's own files by tools/ql/extract_ql.py; see tools/ql/QL_CATALOG.md for what each "
                 "value means and QUAKE_LIVE_PLAN.md for which ones the remaster uses.",
        "source": {"pak00.pk3": {"bytes": os.path.getsize(pak00), "sha1": sha1(pak00)},
                   "bin.pk3": {"bytes": os.path.getsize(binpk3), "sha1": sha1(binpk3)}},
        "standard_movement": {"_note": "QL's pmove defaults with the duel factory's movement set, in every mode "
                                       "(QUAKE_LIVE_PLAN.md decision 1)", "from_duel": sorted(duel_moves),
                              "cvars": dict(sorted(standard.items()))},
        "cvars": dict(sorted(cvars.items(), key=lambda kv: kv[0].lower())),
        "items": items,
        "factories": factories,
    }
    with open(OUT_RULES, "w", encoding="utf-8", newline="\n") as f:
        json.dump(rules, f, indent=1)
        f.write("\n")

    # raw files for reading
    os.makedirs(a.dump, exist_ok=True)
    for name in ["scripts/factories.txt", "scripts/arenas.txt", "default.cfg"]:
        with open(os.path.join(a.dump, os.path.basename(name)), "wb") as f:
            f.write(zp.read(name))
    for p in glob.glob(os.path.join(ql, "mappool*.txt")) + [os.path.join(ql, "server.cfg")]:
        with open(p, "rb") as src, open(os.path.join(a.dump, os.path.basename(p)), "wb") as f:
            f.write(src.read())
    with open(os.path.join(a.dump, "qagame_cvars.txt"), "w", encoding="utf-8") as f:
        for k, v in rules["cvars"].items():
            f.write("%-40s %-24r flags=%#x\n" % (k, v["default"], v["flags"]))
    print("%d cvars, %d items, %d factories -> %s" % (len(cvars), len(items), len(factories), os.path.relpath(OUT_RULES, ROOT)))


if __name__ == "__main__":
    main()
