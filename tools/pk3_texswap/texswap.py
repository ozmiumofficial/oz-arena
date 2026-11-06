#!/usr/bin/env python3
"""texswap: drop an image into a folder and it replaces the texture of the same name inside the game's PK3s.

    python texswap.py apply   [--config texswap.json] [--dry-run] [--only <file> ...]
    python texswap.py watch   [--config texswap.json] [--dry-run]
    python texswap.py list    [--config texswap.json]          what each drop would replace (no writes)
    python texswap.py restore [--config texswap.json]          put every <pak>.texswap-orig back

Python 3.8+, standard library only; Windows, Linux and macOS. See README.md for the strategy.

Matching: a drop's file name without its extension (wall01 from wall01.png) against every image entry in
the configured PK3s whose leaf name without extension is the same, case-insensitively, whatever the
entry's extension. A drop inside subfolders of the watch folder (textures/gothic_block/wall01.png) only
matches entries whose path ends with those folders (path-aware).

Writing: the engine picks an image's decoder from the entry's extension, never from its bytes, so a
.png's bytes under a .tga name would fail to load. Each matched entry is therefore removed and the new
bytes are written at the same folder and stem with the drop's extension (wall01.tga -> wall01.png).
The engine still finds it: a shader asking for wall01.tga that misses tries .png, .tga, .jpg ... next.
"""
import os, sys, json, time, zlib, shutil, struct, zipfile, argparse, datetime

IMAGE_EXTS = (".png", ".jpg", ".jpeg", ".tga", ".dds", ".bmp", ".pcx")
STORED_EXTS = (".png", ".jpg", ".jpeg", ".dds")       # already compressed: deflate gains nothing
ID_PAKS = {"pak%d.pk3" % i for i in range(9)}         # id's baseq3 pak0..8 (pure servers check them)
BACKUP_SUFFIX = ".texswap-orig"
TMP_SUFFIX = ".texswap-tmp"
IGNORED = (".part", ".tmp", ".crdownload", ".download", "~")

DEFAULTS = {
    "game_dirs": [],            # folders whose *.pk3 are searched, lowest priority first (baseq3, then a mod)
    "pk3s": [],                 # extra PK3 files, after (above) the game_dirs' ones
    "watch_folder": "texture_replacements",
    "match": "all",             # "all": every PK3 with the texture; "first": only the one the game loads it from
    "path_aware": True,         # subfolders in the watch folder narrow the match
    "extensions": list(IMAGE_EXTS),
    "dry_run": False,
    "backup": True,             # keep each PK3's original as <pak>.texswap-orig the first time it's changed
    "protect_id_paks": True,    # never write baseq3/pak0..8.pk3
    "poll_seconds": 1.0,
    "settle_seconds": 1.5,      # a drop must stop changing this long before it's applied
    "log_file": "texswap.log",  # relative to the watch folder; "" for none
}


# ------------------------------------------------------------------ config and log

class Config:
    def __init__(self, path, overrides):
        data = dict(DEFAULTS)
        self.path = os.path.abspath(path) if path else None
        if self.path:
            with open(self.path, encoding="utf-8") as f:
                data.update(json.load(f))
        data.update({k: v for k, v in overrides.items() if v is not None})
        base = os.path.dirname(self.path) if self.path else os.getcwd()
        rel = lambda p: os.path.normpath(os.path.join(base, os.path.expanduser(p)))
        self.game_dirs = [rel(d) for d in data["game_dirs"]]
        self.pk3s = [rel(p) for p in data["pk3s"]]
        self.watch = rel(data["watch_folder"])
        self.match = data["match"]
        if self.match not in ("all", "first"):
            raise SystemExit("config: match must be \"all\" or \"first\"")
        self.path_aware = bool(data["path_aware"])
        self.exts = tuple(e.lower() if e.startswith(".") else "." + e.lower() for e in data["extensions"])
        self.dry_run = bool(data["dry_run"])
        self.backup = bool(data["backup"])
        self.protect_id = bool(data["protect_id_paks"])
        self.poll = float(data["poll_seconds"])
        self.settle = float(data["settle_seconds"])
        self.log_path = os.path.join(self.watch, data["log_file"]) if data["log_file"] else None

    def paks(self):
        """Every PK3, lowest priority first: as the game searches, a later one wins."""
        out = []
        for d in self.game_dirs:
            if not os.path.isdir(d):
                log("WARNING: game dir not found: %s" % d)
                continue
            out += sorted((os.path.join(d, f) for f in os.listdir(d) if f.lower().endswith(".pk3")),
                          key=lambda p: os.path.basename(p).lower())
        for p in self.pk3s:
            if os.path.isfile(p):
                out.append(p)
            else:
                log("WARNING: pk3 not found: %s" % p)
        return out

    def is_id_pak(self, pak):
        return os.path.basename(pak).lower() in ID_PAKS and \
            os.path.basename(os.path.dirname(pak)).lower() == "baseq3"


_log_file = None


def log(msg):
    line = "%s  %s" % (datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"), msg)
    print(line, flush=True)
    if _log_file:
        try:
            with open(_log_file, "a", encoding="utf-8") as f:
                f.write(line + "\n")
        except OSError:
            pass


# ------------------------------------------------------------------ the PK3s' contents

class PakIndex:
    """Image entries of every PK3, by leaf stem (lowercase). Re-read when a PK3 changes on disk."""
    def __init__(self, cfg):
        self.cfg = cfg
        self.cache = {}          # pak -> (mtime, size, [entry names])

    def entries(self, pak):
        st = os.stat(pak)
        hit = self.cache.get(pak)
        if not hit or hit[:2] != (st.st_mtime_ns, st.st_size):
            try:
                with zipfile.ZipFile(pak) as z:
                    names = [n for n in z.namelist() if os.path.splitext(n)[1].lower() in self.cfg.exts]
            except (zipfile.BadZipFile, OSError) as e:
                log("WARNING: can't read %s: %s" % (pak, e))
                names = []
            hit = self.cache[pak] = (st.st_mtime_ns, st.st_size, names)
        return hit[2]

    def find(self, rel_stem):
        """rel_stem: 'wall01' or, path-aware, 'textures/gothic_block/wall01' (lowercase, '/').
        -> [(pak, entry)], lowest priority first."""
        leaf = rel_stem.rsplit("/", 1)[-1]
        out = []
        for pak in self.cfg.paks():
            for n in self.entries(pak):
                stem = os.path.splitext(n)[0].lower()
                if stem.rsplit("/", 1)[-1] != leaf:
                    continue
                if "/" in rel_stem and not (stem == rel_stem or stem.endswith("/" + rel_stem)):
                    continue
                out.append((pak, n))
        return out


# ------------------------------------------------------------------ drops

def drops(cfg):
    """Image files in the watch folder -> {abs path: rel stem}."""
    out = {}
    if not os.path.isdir(cfg.watch):
        return out
    for root, dirs, files in os.walk(cfg.watch):
        dirs[:] = [d for d in dirs if not d.startswith(".")]
        for f in files:
            if f.startswith(".") or f.lower().endswith(IGNORED):
                continue
            stem, ext = os.path.splitext(f)
            if ext.lower() not in cfg.exts:
                continue
            p = os.path.join(root, f)
            rel = os.path.relpath(os.path.join(root, stem), cfg.watch).replace("\\", "/").lower()
            out[p] = rel if cfg.path_aware else stem.lower()
    return out


def file_crc(path):
    crc, size = 0, 0
    with open(path, "rb") as f:
        while True:
            b = f.read(1 << 20)
            if not b:
                return crc & 0xFFFFFFFF, size
            crc, size = zlib.crc32(b, crc), size + len(b)


# ------------------------------------------------------------------ planning

def plan(cfg, index, files):
    """-> {pak: {"remove": {old entry}, "add": {new entry: drop path}, "log": [...]}}, [warnings]"""
    by_pak, notes = {}, []
    claimed = {}                                      # (pak, new entry lower) -> drop: two drops, one name
    for path, rel in sorted(files.items()):
        hits = index.find(rel)
        if not hits:
            notes.append("no match: %s (no image named %s.* in the PK3s; the file is kept)"
                         % (os.path.relpath(path, cfg.watch), rel))
            continue
        if cfg.match == "first":
            hits = [hits[-1]]                         # the highest-priority PK3's copy: what the game loads
        dirs = sorted({os.path.dirname(n).lower() for _, n in hits})
        if len(dirs) > 1:
            notes.append("%s matches %d folders (%s): all replaced; put it in a subfolder to narrow it"
                         % (os.path.basename(path), len(dirs), ", ".join(d or "/" for d in dirs)))
        ext = os.path.splitext(path)[1].lower()
        crc, size = file_crc(path)
        for pak, old in hits:
            if cfg.protect_id and cfg.is_id_pak(pak):
                notes.append("skipped %s in %s: id's paks are protected (protect_id_paks)"
                             % (old, os.path.basename(pak)))
                continue
            new = os.path.splitext(old)[0] + ext       # same folder and stem, the drop's format
            key = (pak, new.lower())
            if key in claimed and claimed[key] != path:
                notes.append("conflict: %s and %s both become %s in %s; kept the first"
                             % (os.path.basename(claimed[key]), os.path.basename(path), new, os.path.basename(pak)))
                continue
            claimed[key] = path
            p = by_pak.setdefault(pak, {"remove": set(), "add": {}, "log": [], "same": []})
            p["remove"].add(old)
            p["add"][new] = path
            p["log"].append((path, old, new))
            p["same"].append((old, new, crc, size))
    # drop a PK3 whose entries already hold exactly these bytes under these names
    for pak in list(by_pak):
        p = by_pak[pak]
        try:
            with zipfile.ZipFile(pak) as z:
                info = {i.filename: i for i in z.infolist()}
        except (zipfile.BadZipFile, OSError):
            continue
        if all(old == new and new in info and info[new].CRC == crc and info[new].file_size == size
               for old, new, crc, size in p["same"]):
            del by_pak[pak]
    return by_pak, notes


# ------------------------------------------------------------------ writing a PK3

def copy_raw(zin, info, zout):
    """Copy one entry's compressed bytes as they are (no inflate and deflate again)."""
    fp = zin.fp
    fp.seek(info.header_offset)
    h = fp.read(30)
    if h[:4] != b"PK\x03\x04":
        raise zipfile.BadZipFile("bad local header for %s" % info.filename)
    nlen, xlen = struct.unpack("<HH", h[26:30])
    fp.seek(info.header_offset + 30 + nlen + xlen)
    zi = zipfile.ZipInfo(info.filename, info.date_time)
    zi.compress_type = info.compress_type
    zi.CRC, zi.compress_size, zi.file_size = info.CRC, info.compress_size, info.file_size
    zi.external_attr, zi.create_system = info.external_attr, info.create_system
    zi.flag_bits = info.flag_bits & ~0x08              # sizes go in the header: no data descriptor
    zi.header_offset = zout.fp.tell()
    zout.fp.write(zi.FileHeader())
    left = info.compress_size
    while left:
        b = fp.read(min(left, 1 << 20))
        if not b:
            raise zipfile.BadZipFile("short data for %s" % info.filename)
        zout.fp.write(b)
        left -= len(b)
    zout.filelist.append(zi)
    zout.NameToInfo[zi.filename] = zi
    zout.start_dir = zout.fp.tell()
    zout._didModify = True


def rewrite(cfg, pak, p):
    """pak with p["remove"] taken out and p["add"] written in, through a temp file; validated, then swapped in."""
    tmp = pak + TMP_SUFFIX
    remove = {n for n in p["remove"]}
    adds = dict(p["add"])
    expect = set()
    try:
        with zipfile.ZipFile(pak) as zin, zipfile.ZipFile(tmp, "w", allowZip64=True) as zout:
            written = set()
            for info in zin.infolist():
                if info.filename in remove or info.filename in adds:
                    # the new entry takes the place of the first old one it replaces
                    for new, src in adds.items():
                        if new in written:
                            continue
                        olds = [o for (s, o, n) in p["log"] if n == new]
                        if info.filename in olds or info.filename == new:
                            zi = zipfile.ZipInfo(new, time.localtime(os.path.getmtime(src))[:6])
                            zi.compress_type = zipfile.ZIP_STORED if os.path.splitext(new)[1].lower() in STORED_EXTS \
                                else zipfile.ZIP_DEFLATED
                            zi.external_attr = 0o644 << 16
                            with open(src, "rb") as f, zout.open(zi, "w", force_zip64=False) as w:
                                shutil.copyfileobj(f, w, 1 << 20)
                            written.add(new)
                            expect.add(new)
                    continue
                copy_raw(zin, info, zout)
                expect.add(info.filename)
        # validate: every entry's CRC, and exactly the names expected
        with zipfile.ZipFile(tmp) as z:
            bad = z.testzip()
            if bad:
                raise zipfile.BadZipFile("CRC error in %s after writing" % bad)
            if set(z.namelist()) != expect:
                raise zipfile.BadZipFile("entries differ from the plan after writing")
            for new, src in adds.items():
                crc, size = file_crc(src)
                i = z.getinfo(new)
                if (i.CRC, i.file_size) != (crc, size):
                    raise zipfile.BadZipFile("%s doesn't hold %s's bytes after writing" % (new, src))
    except Exception:
        if os.path.exists(tmp):
            os.remove(tmp)
        raise
    backup = pak + BACKUP_SUFFIX
    moved = False
    try:
        if cfg.backup and not os.path.exists(backup):
            os.replace(pak, backup)                 # a rename: instant, and hard links elsewhere keep the old file
            moved = True
        os.replace(tmp, pak)                        # atomic on one volume
    except OSError as e:
        if moved and not os.path.exists(pak):
            os.replace(backup, pak)                 # put the original back
        if os.path.exists(tmp):
            os.remove(tmp)
        raise OSError("couldn't replace %s (is the game running?): %s" % (pak, e))


def apply(cfg, index, only=None):
    files = drops(cfg)
    if only:
        want = {os.path.abspath(o) for o in only}
        files = {p: r for p, r in files.items() if os.path.abspath(p) in want}
    if not files:
        log("no images in %s" % cfg.watch)
        return 0
    by_pak, notes = plan(cfg, index, files)
    for n in notes:
        log("WARNING: " + n)
    if not by_pak:
        log("nothing to change (%d drop%s checked)" % (len(files), "" if len(files) == 1 else "s"))
        return 0
    swaps = 0
    for pak in sorted(by_pak):
        p = by_pak[pak]
        verb = "would swap" if cfg.dry_run else "swap"
        for src, old, new in sorted(p["log"]):
            log("%s: %s -> %s : %s -> %s" % (verb, os.path.relpath(src, cfg.watch), pak, old, new))
        if cfg.dry_run:
            continue
        try:
            t = time.time()
            rewrite(cfg, pak, p)
            swaps += len(p["log"])
            log("wrote %s (%d entr%s, %.1f s)%s" % (os.path.basename(pak), len(p["log"]),
                                                     "y" if len(p["log"]) == 1 else "ies", time.time() - t,
                                                     ", original kept as " + os.path.basename(pak) + BACKUP_SUFFIX
                                                     if cfg.backup else ""))
        except Exception as e:
            log("ERROR: %s left unchanged: %s" % (os.path.basename(pak), e))
    return swaps


def list_drops(cfg, index):
    files = drops(cfg)
    for path, rel in sorted(files.items()):
        hits = index.find(rel)
        print("%s  (%s)" % (os.path.relpath(path, cfg.watch), rel))
        if cfg.match == "first" and hits:
            hits = [hits[-1]]
        for pak, n in hits:
            tag = "  [id pak, protected]" if cfg.protect_id and cfg.is_id_pak(pak) else ""
            print("    %s : %s -> %s%s" % (pak, n, os.path.splitext(n)[0] + os.path.splitext(path)[1].lower(), tag))
        if not hits:
            print("    no match")


def restore(cfg):
    n = 0
    for d in cfg.game_dirs + [os.path.dirname(p) for p in cfg.pk3s]:
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            if f.endswith(BACKUP_SUFFIX):
                b = os.path.join(d, f)
                pak = b[:-len(BACKUP_SUFFIX)]
                if cfg.dry_run:
                    log("would restore %s" % pak)
                    continue
                os.replace(b, pak)
                log("restored %s" % pak)
                n += 1
    if not n and not cfg.dry_run:
        log("no %s backups found" % BACKUP_SUFFIX)


def watch(cfg, index):
    os.makedirs(cfg.watch, exist_ok=True)
    log("watching %s (%s, every %.1f s); Ctrl+C stops" % (cfg.watch, "dry run" if cfg.dry_run else "writing",
                                                          cfg.poll))
    apply(cfg, index)                               # whatever is already there (no-op when applied before)
    seen = {p: _stat(p) for p in drops(cfg)}
    pending = {}                                    # path -> (stat, first time it was seen at that stat)
    while True:
        time.sleep(cfg.poll)
        now = time.time()
        current = drops(cfg)
        for p in current:
            st = _stat(p)
            if st is None or st == seen.get(p):
                continue
            if p not in pending or pending[p][0] != st:
                pending[p] = (st, now)              # changed: start (again) waiting for it to settle
        ready = [p for p, (st, t) in pending.items() if now - t >= cfg.settle and _stat(p) == st]
        for p in list(pending):
            if p not in current:
                del pending[p]
        if ready:
            for p in ready:
                seen[p] = pending.pop(p)[0]
                log("new or changed: %s" % os.path.relpath(p, cfg.watch))
            apply(cfg, index, ready)


def _stat(p):
    try:
        st = os.stat(p)
        return (st.st_size, st.st_mtime_ns)
    except OSError:
        return None


def main():
    ap = argparse.ArgumentParser(description="Replace textures inside PK3s with dropped images (matched by name).")
    ap.add_argument("command", choices=["apply", "watch", "list", "restore"])
    ap.add_argument("--config", default="texswap.json", help="config file (default: texswap.json)")
    ap.add_argument("--dry-run", action="store_true", default=None, help="log what would change, write nothing")
    ap.add_argument("--only", nargs="*", help="apply: only these dropped files")
    a = ap.parse_args()
    if not os.path.isfile(a.config):
        raise SystemExit("no config at %s (copy texswap.example.json)" % a.config)
    cfg = Config(a.config, {"dry_run": a.dry_run})
    global _log_file
    if cfg.log_path:
        os.makedirs(os.path.dirname(cfg.log_path), exist_ok=True)
        _log_file = cfg.log_path
    index = PakIndex(cfg)
    if a.command == "apply":
        apply(cfg, index, a.only)
    elif a.command == "watch":
        try:
            watch(cfg, index)
        except KeyboardInterrupt:
            log("stopped")
    elif a.command == "list":
        list_drops(cfg, index)
    else:
        restore(cfg)


if __name__ == "__main__":
    main()
