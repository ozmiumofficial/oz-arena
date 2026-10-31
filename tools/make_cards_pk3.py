"""P12's player card art -> build/engine/oz/zz-oz-cards.pk3.

python tools/make_cards_pk3.py [--library <godot/library>] [--out <pk3>] [--report]

The Godot build's card art (godot/library/cards, read only): what its tools/build_cards.py made from
id's icons, level shots, characters, skies and world materials. Each part in the catalog
(engine/ui/cards/catalog.json, the same library's cards.json) names its art as cards/<kind>/<id>.<ext>;
the pak keeps that path under ui/ (ui/cards/banners/map_q3dm17.jpg ...), where the native UI and the
HUD look for it. The engine mounts it like zz-oz-ui.pk3: under every mod and on pure servers, for ui/
names only, in no pure or reference list.

It's derived from id's art, so it's build output, never committed; engine/build_engine.py stages it
with the UI pak.
"""
import argparse, json, os, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
LIBRARY = r"F:/quake-3-arena-remastered-fan-project/godot/library"
OUT = os.path.join(ROOT, "build", "engine", "oz", "zz-oz-cards.pk3")
CATALOG = os.path.join(ROOT, "engine", "ui", "cards", "catalog.json")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--library", default=LIBRARY)
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--report", action="store_true")
    a = ap.parse_args()
    cards = os.path.join(a.library, "cards")
    if not os.path.isdir(cards):
        print("make_cards_pk3: no card art at %s; the cards draw from the player's paks" % cards)
        return 1
    cat = json.load(open(CATALOG, encoding="utf-8"))
    wanted, missing = [], []
    for kind, parts in cat["parts"].items():
        for p in parts:
            art = p.get("art")
            if not art:
                continue
            (wanted if os.path.isfile(os.path.join(a.library, *art.split("/"))) else missing).append(art)
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    tmp = a.out + ".tmp"
    size = 0
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_STORED) as z:   # JPG and PNG are compressed already
        for art in sorted(set(wanted)):
            src = os.path.join(a.library, *art.split("/"))
            z.write(src, "ui/" + art)
            size += os.path.getsize(src)
    os.replace(tmp, a.out)
    print("%s: %d images, %.1f MB" % (a.out, len(set(wanted)), size / 1e6))
    if missing:
        print("  %d parts name art the library doesn't have (they draw from the player's paks): %s" % (len(missing), ", ".join(missing[:8])))
    if a.report:
        for art in sorted(set(wanted)):
            print("  ui/" + art)
    return 0


if __name__ == "__main__":
    sys.exit(main())
