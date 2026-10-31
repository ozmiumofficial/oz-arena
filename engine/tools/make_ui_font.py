"""The menu's display font, traced from the player's own pak0 (ENGINE_PLAN.md P5).

python engine/tools/make_ui_font.py [--baseq3 <folder>] [--out <file.ttf>]

The web menu's display face (the Godot build's q3_prop.ttf) is id's proportional menu font,
menu/art/font1_prop.tga, traced into a TrueType font. Traced from id's art, it can't be committed,
so it's made from the player's pak0: build_engine.py did that when it staged oz/zz-oz-ui.pk3;
since 2026-10-30 the engine traces it as the UI starts (src/oz/ui/oz_ui_font.cpp, a port of this
file), and this is the reference engine/tests/display_font_test.py checks it against. The tracing is the Godot build's tools/make_ui.py trace_ttf, so the glyphs are the
web menu's: each 256x256 atlas row is a fixed cell band, glyphs are its ink columns (touching
glyphs split at the thinnest column), every glyph is upscaled 8x and its outline traced. Capitals
only: lower case maps to them, as in ui_atoms.c.
"""
import os, re, io, sys, glob, zipfile, argparse

FAMILY = "OzDisplay"
# cell bands (top of each row in the atlas), cell height, baseline, gap and space as ui_atoms.c draws them
PROP = dict(file="menu/art/font1_prop.tga", cell=30, ascent=27, gap=3, space=8, rows=[
    (0, "ABCDEFGHIJKLM"), (30, "NOPQRSTUVWXY"), (60, "Z0123456897"), (90, "`-=~"),
    (120, "!@#$%^&*()"), (150, "_+[]\\/{}<>"), (180, "|;:,.'\"?")])


def find_baseq3():
    """The player's baseq3 in any Steam library (as tools/make_pk3s.py finds it), or None."""
    roots = []
    try:
        import winreg
        for hive, key, val in ((winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam", "SteamPath"),
                               (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath")):
            try:
                roots.append(winreg.QueryValueEx(winreg.OpenKey(hive, key), val)[0])
            except OSError:
                pass
    except ImportError:
        pass
    roots.append(r"C:\Program Files (x86)\Steam")
    libs = []
    for r in roots:
        vdf = os.path.join(r, "steamapps", "libraryfolders.vdf")
        if os.path.isfile(vdf):
            libs += [p.replace("\\\\", "\\") for p in re.findall(r'"path"\s+"([^"]+)"', open(vdf, encoding="utf-8").read())]
    for lib in roots + libs:
        d = os.path.join(lib, "steamapps", "common", "Quake 3 Arena", "baseq3")
        if os.path.isfile(os.path.join(d, "pak0.pk3")):
            return d
    return None


def read_pak_file(baseq3, path):
    """A file from id's paks, the last pak holding it winning, as in the engine."""
    data = None
    for p in sorted(glob.glob(os.path.join(baseq3, "pak[0-8].pk3"))):
        with zipfile.ZipFile(p) as z:
            names = {n.lower(): n for n in z.namelist()}
            if path.lower() in names:
                data = z.read(names[path.lower()])
    return data


def ink_runs(col, n, th=20):
    """Columns with ink -> exactly n [x0, x1] runs: merge the closest pairs or split the widest."""
    import numpy as np
    idx = np.where(col > th)[0]
    runs = []
    for x in idx:
        if runs and x == runs[-1][1] + 1:
            runs[-1][1] = x
        else:
            runs.append([x, x])
    while len(runs) > n:
        gaps = [runs[i + 1][0] - runs[i][1] for i in range(len(runs) - 1)]
        i = int(np.argmin(gaps))
        runs[i:i + 2] = [[runs[i][0], runs[i + 1][1]]]
    while len(runs) < n:
        i = int(np.argmax([r[1] - r[0] for r in runs]))
        x0, x1 = runs[i]
        w = x1 - x0
        lo, hi = x0 + int(w * 0.2), x1 - int(w * 0.2)
        cut = lo + int(np.argmin(col[lo:hi + 1]))
        runs[i:i + 1] = [[x0, cut - 1], [cut + 1, x1]]
    return [(int(r[0]), int(r[1])) for r in runs]


def trace(tga):
    """font1_prop.tga's bytes -> the TrueType font's bytes."""
    import numpy as np, cv2
    from PIL import Image
    from fontTools.fontBuilder import FontBuilder
    from fontTools.pens.ttGlyphPen import TTGlyphPen

    alpha = np.array(Image.open(io.BytesIO(tga)).convert("RGBA"))[..., 3]
    spec = PROP
    cell, asc = spec["cell"], spec["ascent"]
    upm, up = 1000, 8
    u = upm / cell                        # font units per atlas pixel
    order, cmap, glyphs, metrics = [".notdef", "space"], {32: "space"}, {}, {}
    glyphs[".notdef"] = TTGlyphPen(None).glyph()
    glyphs["space"] = TTGlyphPen(None).glyph()
    metrics[".notdef"] = metrics["space"] = (int(spec["space"] * u), 0)
    for top, chars in spec["rows"]:
        band = alpha[top:top + cell]
        for ch, (x0, x1) in zip(chars, ink_runs(band.max(0), len(chars))):
            w = x1 - x0 + 1
            canvas = np.zeros((cell + 2, w + 2), np.uint8)
            canvas[1:1 + cell, 1:1 + w] = alpha[top:top + cell, x0:x1 + 1]
            big = np.array(Image.fromarray(canvas).resize(((w + 2) * up, (cell + 2) * up), Image.LANCZOS))
            contours, hier = cv2.findContours((big >= 128).astype(np.uint8), cv2.RETR_CCOMP, cv2.CHAIN_APPROX_NONE)
            pen = TTGlyphPen(None)
            for ci, c in enumerate(contours):
                hole = hier[0][ci][3] != -1
                c = cv2.approxPolyDP(c, 1.2, True)[:, 0, :].astype(np.float64)
                if len(c) < 3:
                    continue
                # image pixels (8x, a 1 px border) -> font units, y up from the baseline
                pts = [((x / up - 1) * u, (asc - (y / up - 1)) * u) for x, y in c]
                cw = sum(pts[i][0] * pts[i - 1][1] - pts[i - 1][0] * pts[i][1] for i in range(len(pts))) > 0
                if cw == hole:   # TrueType: outlines clockwise, holes counter-clockwise
                    pts.reverse()
                pen.moveTo(tuple(map(round, pts[0])))
                for p in pts[1:]:
                    pen.lineTo(tuple(map(round, p)))
                pen.closePath()
            name = "g%04x" % ord(ch)
            order.append(name)
            glyphs[name] = pen.glyph()
            metrics[name] = (int(round((w + spec["gap"]) * u)), 0)
            cmap[ord(ch)] = name
            if ch.isalpha():
                cmap[ord(ch.lower())] = name
    fb = FontBuilder(upm, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=int(asc * u), descent=-int((cell - asc) * u))
    fb.setupNameTable({"familyName": FAMILY, "styleName": "Regular"})
    fb.setupOS2(sTypoAscender=int(asc * u), sTypoDescender=-int((cell - asc) * u), usWinAscent=int(asc * u),
                usWinDescent=int((cell - asc) * u))
    fb.setupPost()
    out = io.BytesIO()
    fb.save(out)
    return out.getvalue()


def make(baseq3=None):
    """-> (the font's bytes, or None, and a line saying where it came from or why not)."""
    baseq3 = baseq3 or find_baseq3()
    if not baseq3:
        return None, "no Quake III Arena install found (the display font falls back to Barlow)"
    tga = read_pak_file(baseq3, PROP["file"])
    if tga is None:
        return None, "%s has no %s" % (baseq3, PROP["file"])
    try:
        return trace(tga), "traced %s from %s" % (PROP["file"], baseq3)
    except ImportError as e:
        return None, "can't trace the display font: %s (pip install numpy pillow opencv-python fonttools)" % e


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseq3")
    ap.add_argument("--out", default="oz_display.ttf")
    a = ap.parse_args()
    data, how = make(a.baseq3)
    print(how)
    if data is None:
        sys.exit(1)
    open(a.out, "wb").write(data)
    print("wrote", a.out, len(data), "bytes")
