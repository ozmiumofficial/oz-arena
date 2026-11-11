"""The map editor's icons -> engine/ui/fonts/OzIcons.ttf and engine/src/oz/ui/oz_editor_icons.h.

Each icon is a line drawing on a 24 x 24 grid, written here as SVG path data (Oz's own drawings, from
the editor's design mockups). The font is how the page draws them: RmlUi rasterizes a glyph at the
size it's shown, so an icon stays sharp at every window size and menu scale, and it takes the text's
color (`color`, hover and all) like a letter. The lines become filled outlines here: a quad along
each segment and a round cap at each point, all wound the same way, so TrueType's non-zero fill
joins them. A one-instruction prep program keeps FreeType from auto-hinting the outlines (it hints
a TrueType font that has no instructions at all), which pulls thin lines onto the pixel grid.

The icons sit on the private use area from U+E000, in the order below; the header maps each name to
its code point (oz_ui_editor.cpp's Icon()). Both outputs are committed: run this again after adding
or changing an icon (needs fontTools).
"""
import math
import os
import re
import sys

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import newTable
from fontTools.ttLib.tables import ttProgram

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.dirname(HERE)
FONT = os.path.join(ENGINE, "ui", "fonts", "OzIcons.ttf")
HEADER = os.path.join(ENGINE, "src", "oz", "ui", "oz_editor_icons.h")

FIRST = 0xE000
EM = 1000
SCALE = EM / 24.0      # the drawings' 24 units fill the em
DESCENT = -150         # the drawing sits from -150 to 850: centered on a line of Barlow's text
STROKE = 1.8           # line width in the drawings' units

# name: path data (24 x 24, y down). New icons go at the end: the code points follow this order.
ICONS = [
    ("select", "M5 3.5l13.5 6.8-6 1.9-2.2 6.3z"),
    ("brush", "M12 2.8l8 4.6v9.2L12 21.2l-8-4.6V7.4zM4 7.4l8 4.6 8-4.6M12 12v9.2"),
    ("clip", "M3.5 6.5a2.5 2.5 0 1 0 5 0a2.5 2.5 0 1 0 -5 0M3.5 17.5a2.5 2.5 0 1 0 5 0a2.5 2.5 0 1 0 -5 0M8.2 8l11.3 10M8.2 16l11.3-10"),
    ("faces", "M12 2.8l8 4.6v9.2L12 21.2l-8-4.6V7.4zM4 7.4l8 4.6 8-4.6M12 12v9.2M14.5 14.6l3-1.7M14.5 17.6l3-1.7"),
    ("entity", "M12 3l8.5 9-8.5 9-8.5-9zM12 9v6M9 12h6"),
    ("light", "M9.5 18h5M10.5 21h3M12 3a6 6 0 0 0-3.8 10.6c.8.7 1.3 1.6 1.3 2.6v.3h5v-.3c0-1 .5-1.9 1.3-2.6A6 6 0 0 0 12 3z"),
    ("jump_pad", "M3 20h18M6.5 20C6.5 10 17.5 9 17.5 16.5M15 14.5l2.5 2.5 2.5-2.5"),
    ("teleport", "M5 18a7 2.5 0 1 0 14 0a7 2.5 0 1 0 -14 0M12 15V4M8.5 7.5L12 4l3.5 3.5"),
    ("new", "M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8zM14 3v5h5M12 11.5v6M9 14.5h6"),
    ("open", "M3 7.5a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v7.5a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"),
    ("save", "M5 3.5h11l3.5 3.5v12a1.5 1.5 0 0 1-1.5 1.5H5a1.5 1.5 0 0 1-1.5-1.5V5A1.5 1.5 0 0 1 5 3.5zM7.5 3.5v4.5h7.5V3.5M7 20.5v-6.5h10v6.5"),
    ("undo", "M9 14L4 9l5-5M4 9h10.5a5.5 5.5 0 0 1 0 11H11"),
    ("redo", "M15 14l5-5-5-5M20 9H9.5a5.5 5.5 0 0 0 0 11H13"),
    ("duplicate", "M10 8h9a1.5 1.5 0 0 1 1.5 1.5v9A1.5 1.5 0 0 1 19 20h-9a1.5 1.5 0 0 1-1.5-1.5v-9A1.5 1.5 0 0 1 10 8z"
                  "M15.5 8V5.5A1.5 1.5 0 0 0 14 4H5a1.5 1.5 0 0 0-1.5 1.5v9A1.5 1.5 0 0 0 5 16h3.5"),
    ("delete", "M4 7h16M10 11v6M14 11v6M6 7l1 12a2 2 0 0 0 2 2h6a2 2 0 0 0 2-2l1-12M9 7V4.5h6V7"),
    ("turn", "M4 12a8 8 0 1 0 2.3-5.7M4 4v5h5"),
    ("mirror", "M12 3v18M9 7L3.5 17H9zM15 7l5.5 10H15z"),
    ("hollow", "M4 4h16v16H4zM8 8h8v8H8z"),
    ("subtract", "M4 4h11v11H4zM15 9h5v11H9v-5"),
    ("merge", "M7 4v5a5 5 0 0 0 10 0V4M12 14v6"),
    ("eyedropper", "M14.5 4.5l5 5M17.2 2.8a2.2 2.2 0 0 1 3.1 3.1L18 8.2 15.8 6zM15.5 7L5 17.5V20h2.5L18 9.5"),
    ("paint", "M5 11l7-7 8 8-7 7a2.1 2.1 0 0 1-3 0l-5-5a2.1 2.1 0 0 1 0-3zM5 11h15M20 15.5s1.5 2 1.5 3a1.5 1.5 0 0 1-3 0c0-1 1.5-3 1.5-3z"),
    ("lock", "M6 11h12a1.5 1.5 0 0 1 1.5 1.5v7A1.5 1.5 0 0 1 18 21H6a1.5 1.5 0 0 1-1.5-1.5v-7A1.5 1.5 0 0 1 6 11zM8 11V7.5a4 4 0 0 1 8 0V11"),
    ("views", "M4 4h7v7H4zM13 4h7v7h-7zM4 13h7v7H4zM13 13h7v7h-7z"),
    ("frame", "M4 9V5a1 1 0 0 1 1-1h4M15 4h4a1 1 0 0 1 1 1v4M20 15v4a1 1 0 0 1-1 1h-4M9 20H5a1 1 0 0 1-1-1v-4"
              "M9.5 12a2.5 2.5 0 1 0 5 0a2.5 2.5 0 1 0 -5 0"),
    ("search", "M4 11a7 7 0 1 0 14 0a7 7 0 1 0 -14 0M20 20l-4-4"),
    ("sun", "M8 12a4 4 0 1 0 8 0a4 4 0 1 0 -8 0M12 2.5v2M12 19.5v2M2.5 12h2M19.5 12h2M5.3 5.3l1.4 1.4M17.3 17.3l1.4 1.4"
            "M5.3 18.7l1.4-1.4M17.3 6.7l1.4-1.4"),
    ("hammer", "M15.5 3.5l5 5-2.5 2.5-5-5zM14.3 7.8L4.5 17.6a1.6 1.6 0 0 0 2.3 2.3l9.8-9.8"),
    ("stop", "M6.5 6.5h11v11h-11z"),
    ("play", "M7 4.5v15l12.5-7.5z"),
    ("warning", "M12 3.5L2.5 20h19zM12 10v4.5M12 17.2v.1"),
    ("leak", "M12 3.5s6 6.6 6 10.5a6 6 0 0 1-12 0c0-3.9 6-10.5 6-10.5z"),
    ("exit", "M14 4h4a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2h-4M9 16l-4-4 4-4M5 12h10"),
    ("eye", "M2.5 12S6 5.5 12 5.5 21.5 12 21.5 12 18 18.5 12 18.5 2.5 12 2.5 12zM9.5 12a2.5 2.5 0 1 0 5 0a2.5 2.5 0 1 0 -5 0"),
    ("plus", "M12 5v14M5 12h14"),
    ("minus", "M5 12h14"),
    ("check", "M5 12.5l4.5 4.5L19 7.5"),
    ("close", "M6 6l12 12M18 6L6 18"),
    ("align_left", "M4 4v16M8 7h12M8 12h7M8 17h10"),
    ("align_right", "M20 4v16M4 7h12M9 12h7M6 17h10"),
    ("align_top", "M4 4h16M7 8v12M12 8v7M17 8v10"),
    ("align_bottom", "M4 20h16M7 4v12M12 9v7M17 6v10"),
    ("align_center", "M12 3v18M6 7h12M8 12h8M5 17h14"),
    ("flip_across", "M12 3v18M8 7l-5 5 5 5M16 7l5 5-5 5"),
    ("flip_down", "M3 12h18M7 8l5-5 5 5M7 16l5 5 5-5"),
    ("reset", "M3.5 12a8.5 8.5 0 1 0 2.5-6M3.5 3.5V8H8"),
    ("uv", "M4 4h16v16H4zM4 12h16M12 4v16"),
    ("grid", "M4 9h16M4 15h16M9 4v16M15 4v16"),
    ("chevron", "M6 9l6 6 6-6"),
    ("keys", "M3.5 7h17v10h-17zM7 10.5h.1M10.5 10.5h.1M14 10.5h.1M17 10.5h.1M8 14h8"),
    ("guide", "M4 5.5a1.5 1.5 0 0 1 1.5-1.5H11v16H5.5A1.5 1.5 0 0 1 4 18.5zM20 5.5A1.5 1.5 0 0 0 18.5 4H13v16h5.5a1.5 1.5 0 0 0 1.5-1.5z"),
    ("star", "M12 3.5l2.6 5.4 5.9.8-4.3 4.1 1 5.8L12 16.8l-5.2 2.8 1-5.8-4.3-4.1 5.9-.8z"),
    ("roller", "M4.5 3.5h11a1.5 1.5 0 0 1 1.5 1.5v2a1.5 1.5 0 0 1-1.5 1.5h-11A1.5 1.5 0 0 1 3 7V5a1.5 1.5 0 0 1 1.5-1.5zM17 6h2.5v5.5H11v3M9.5 14.5h3V21h-3z"),
]

NUM = re.compile(r"-?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?")
TOKEN = re.compile(r"[MmLlHhVvCcSsQqAaZz]|-?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?")


def arc_points(p0, rx, ry, phi, large, sweep, p1, steps_per_rad=6.0):
    """SVG's endpoint arc as points after p0 (the spec's F.6.5 conversion to the center form)."""
    x1, y1 = p0
    x2, y2 = p1
    if rx == 0 or ry == 0 or (x1 == x2 and y1 == y2):
        return [p1]
    rx, ry = abs(rx), abs(ry)
    c, s = math.cos(math.radians(phi)), math.sin(math.radians(phi))
    dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
    xp, yp = c * dx + s * dy, -s * dx + c * dy
    lam = (xp * xp) / (rx * rx) + (yp * yp) / (ry * ry)
    if lam > 1:
        rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
    num = rx * rx * ry * ry - rx * rx * yp * yp - ry * ry * xp * xp
    den = rx * rx * yp * yp + ry * ry * xp * xp
    coef = math.sqrt(max(0.0, num / den)) * (-1 if large == sweep else 1)
    cxp, cyp = coef * rx * yp / ry, -coef * ry * xp / rx
    cx = c * cxp - s * cyp + (x1 + x2) / 2
    cy = s * cxp + c * cyp + (y1 + y2) / 2

    def ang(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a

    t1 = ang(1, 0, (xp - cxp) / rx, (yp - cyp) / ry)
    dt = ang((xp - cxp) / rx, (yp - cyp) / ry, (-xp - cxp) / rx, (-yp - cyp) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    elif sweep and dt < 0:
        dt += 2 * math.pi
    n = max(2, int(math.ceil(abs(dt) * steps_per_rad * max(rx, ry) / 4 + 2)))
    out = []
    for i in range(1, n + 1):
        t = t1 + dt * i / n
        ex, ey = rx * math.cos(t), ry * math.sin(t)
        out.append((c * ex - s * ey + cx, s * ex + c * ey + cy))
    out[-1] = p1
    return out


def bezier_points(p0, c1, c2, p1):
    length = math.dist(p0, c1) + math.dist(c1, c2) + math.dist(c2, p1)
    n = max(3, int(length * 1.5))
    out = []
    for i in range(1, n + 1):
        t = i / n
        u = 1 - t
        out.append((u * u * u * p0[0] + 3 * u * u * t * c1[0] + 3 * u * t * t * c2[0] + t * t * t * p1[0],
                    u * u * u * p0[1] + 3 * u * u * t * c1[1] + 3 * u * t * t * c2[1] + t * t * t * p1[1]))
    return out


def parse(d):
    """Path data -> [(points, closed)], curves and arcs flattened."""
    toks = TOKEN.findall(d)
    i = 0
    paths = []
    pts = []
    cur = (0.0, 0.0)
    start = cur
    last_c2 = None
    cmd = None

    def num():
        nonlocal i
        v = float(toks[i])
        i += 1
        return v

    def flush(closed):
        nonlocal pts
        if len(pts) >= 1:
            paths.append((pts, closed))
        pts = []

    while i < len(toks):
        if re.match(r"[A-Za-z]", toks[i]):
            cmd = toks[i]
            i += 1
            if cmd in "Zz":
                flush(True)
                cur = start
                last_c2 = None
                continue
        rel = cmd.islower()
        c = cmd.upper()
        ox, oy = cur if rel else (0.0, 0.0)
        if c == "M":
            flush(False)
            cur = (ox + num(), oy + num())
            start = cur
            pts = [cur]
            cmd = "l" if rel else "L"  # coordinates after a move are lines
            last_c2 = None
        elif c == "L":
            cur = (ox + num(), oy + num())
            pts.append(cur)
            last_c2 = None
        elif c == "H":
            cur = ((cur[0] if rel else 0.0) + num(), cur[1])
            pts.append(cur)
            last_c2 = None
        elif c == "V":
            cur = (cur[0], (cur[1] if rel else 0.0) + num())
            pts.append(cur)
            last_c2 = None
        elif c == "C":
            c1 = (ox + num(), oy + num())
            c2 = (ox + num(), oy + num())
            p1 = (ox + num(), oy + num())
            pts.extend(bezier_points(cur, c1, c2, p1))
            cur, last_c2 = p1, c2
        elif c == "S":
            c1 = (2 * cur[0] - last_c2[0], 2 * cur[1] - last_c2[1]) if last_c2 else cur
            c2 = (ox + num(), oy + num())
            p1 = (ox + num(), oy + num())
            pts.extend(bezier_points(cur, c1, c2, p1))
            cur, last_c2 = p1, c2
        elif c == "A":
            rx, ry, phi = num(), num(), num()
            large, sweep = int(num()), int(num())
            p1 = (ox + num(), oy + num())
            pts.extend(arc_points(cur, rx, ry, phi, large, sweep, p1))
            cur = p1
            last_c2 = None
        else:
            sys.exit("unsupported path command %s" % cmd)
    flush(False)
    return paths


def to_font(p):
    return (p[0] * SCALE, (24.0 - p[1]) * SCALE + DESCENT)


def clockwise(poly):
    area = 0.0
    for k in range(len(poly)):
        x0, y0 = poly[k]
        x1, y1 = poly[(k + 1) % len(poly)]
        area += x0 * y1 - x1 * y0
    return poly[::-1] if area > 0 else poly  # TrueType's filled contours run clockwise (y up)


def outline(d):
    """The drawing's lines as filled contours in font units."""
    r = STROKE / 2 * SCALE
    contours = []
    for pts, closed in parse(d):
        fp = [to_font(p) for p in pts]
        # drop repeated points, keep a lone dot
        clean = [fp[0]]
        for q in fp[1:]:
            if math.dist(q, clean[-1]) > 0.5:
                clean.append(q)
        if closed and len(clean) > 1 and math.dist(clean[0], clean[-1]) < 0.5:
            clean.pop()
        segs = list(zip(clean, clean[1:]))
        if closed and len(clean) > 2:
            segs.append((clean[-1], clean[0]))
        for a, b in segs:
            dx, dy = b[0] - a[0], b[1] - a[1]
            ln = math.hypot(dx, dy)
            nx, ny = -dy / ln * r, dx / ln * r
            contours.append(clockwise([(a[0] + nx, a[1] + ny), (b[0] + nx, b[1] + ny), (b[0] - nx, b[1] - ny), (a[0] - nx, a[1] - ny)]))
        # round caps and joins
        for q in clean:
            n = 16
            contours.append(clockwise([(q[0] + r * math.cos(2 * math.pi * k / n), q[1] + r * math.sin(2 * math.pi * k / n)) for k in range(n)]))
    return [[(int(round(x)), int(round(y))) for x, y in c] for c in contours]


def main():
    order = [".notdef", "space"] + [n for n, _ in ICONS]
    cmap = {0x20: "space"}
    glyphs = {}
    metrics = {}
    empty = TTGlyphPen(None)
    glyphs[".notdef"] = empty.glyph()
    glyphs["space"] = TTGlyphPen(None).glyph()
    metrics[".notdef"] = (EM, 0)
    metrics["space"] = (EM // 2, 0)
    for k, (name, d) in enumerate(ICONS):
        cmap[FIRST + k] = name
        pen = TTGlyphPen(None)
        cs = outline(d)
        for c in cs:
            pen.moveTo(c[0])
            for q in c[1:]:
                pen.lineTo(q)
            pen.closePath()
        glyphs[name] = pen.glyph()
        metrics[name] = (EM, min(x for c in cs for x, _ in c))

    fb = FontBuilder(EM, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=EM + DESCENT, descent=DESCENT)
    fb.setupNameTable({"familyName": "OzIcons", "styleName": "Regular",
                       "copyright": "Oz Arena's map editor icons, GPL-2.0 with the game",
                       "uniqueFontIdentifier": "OzIcons-Regular", "fullName": "OzIcons Regular",
                       "psName": "OzIcons-Regular"})
    fb.setupOS2(sTypoAscender=EM + DESCENT, sTypoDescender=DESCENT, sTypoLineGap=0,
                usWinAscent=EM + DESCENT, usWinDescent=-DESCENT, fsType=0)
    fb.setupPost()
    prep = newTable("prep")
    prep.program = ttProgram.Program()
    prep.program.fromBytecode(b"\xb0\x00\x21")  # PUSHB 0, POP: an instruction, so FreeType doesn't auto-hint
    fb.font["prep"] = prep
    gasp = newTable("gasp")
    gasp.version = 1
    gasp.gaspRange = {0xFFFF: 0x000A}  # gray and symmetric smoothing at every size
    fb.font["gasp"] = gasp
    os.makedirs(os.path.dirname(FONT), exist_ok=True)
    fb.save(FONT)

    lines = ["// Made by engine/tools/make_editor_icons.py: the map editor's icons in ui/fonts/OzIcons.ttf (family OzIcons),",
             "// each name's code point. Run that again after changing its icons; don't edit this by hand.",
             "#pragma once", "",
             "static const struct { const char *name; unsigned code; } OZ_EDITOR_ICONS[] = {"]
    for k, (name, _) in enumerate(ICONS):
        lines.append('\t{ "%s", 0x%04X },' % (name, FIRST + k))
    lines.append("};")
    with open(HEADER, "w", newline="\r\n") as f:
        f.write("\n".join(lines) + "\n")
    print("%d icons -> %s (%d bytes), %s" % (len(ICONS), FONT, os.path.getsize(FONT), HEADER))


if __name__ == "__main__":
    main()
