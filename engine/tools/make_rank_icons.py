"""The ranked tier icons (the web menu's RankIcon.tsx) as RML for the native menus (ENGINE_PLAN.md P5).

python engine/tools/make_rank_icons.py

RmlUi has no SVG, so each icon is built from the web's paths. A polygon is cut into triangles, each a
zero-size element whose top border is a right triangle (border-top colored, border-right clear) that
matrix() maps onto the triangle; parts the web fills with a gradient get one color a triangle,
sampled from that gradient. The shield's body carries one smooth gradient: a box down to y 33, then
rows down the curved bottom (two circles' lens), each row a rectangle with its share of the
gradient. (Nested overflow-hidden circles would cut the lens in one piece, but the renderer doesn't
apply RmlUi's clip masks yet.)

Rewrites the marked blocks of engine/ui/pages/ranked.rml: the big Unranked icon and the six tier
cards. The icon is 1em wide and 1.25em tall (the web's 64 x 80 viewBox: a unit is 1/64 em); the
page sets the font-size. play.rcss has the classes (.rk, .rk .t, .rk .p).
"""
import math, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
PAGE = os.path.join(ROOT, "engine", "ui", "pages", "ranked.rml")

ORDER = ["bronze", "silver", "gold", "platinum", "diamond", "godlike"]
TIER_COLORS = {"bronze": "#c27a45", "silver": "#c3ccd6", "gold": "#ffc93c", "platinum": "#4fe0c8", "diamond": "#6ab8ff", "godlike": "#ff3d22"}
DARK = (10, 8, 7)            # the web's stroke, #0a0807
UNRANKED = (26, 22, 20)      # #1a1614


def em(u):
    s = ("%.4f" % (u / 64.0)).rstrip("0").rstrip(".")
    return (s if s not in ("", "-0") else "0") + "em"


def rgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))


def hexc(c):
    return "#%02x%02x%02x" % tuple(max(0, min(255, int(round(v)))) for v in c)


def mix(a, b, k):
    """k of a, the rest b (CSS color-mix)"""
    return tuple(k * x + (1 - k) * y for x, y in zip(a, b))


WHITE, BLACK = (255, 255, 255), (0, 0, 0)


def stops(c):
    """the web's metal gradient: 55% white, the color, 45% black"""
    return [(0.0, mix(c, WHITE, 0.55)), (0.45, c), (1.0, mix(c, BLACK, 0.45))]


def at(st, t):
    t = max(0.0, min(1.0, t))
    for (t0, c0), (t1, c1) in zip(st, st[1:]):
        if t <= t1:
            k = (t - t0) / (t1 - t0) if t1 > t0 else 0
            return mix(c1, c0, k)
    return st[-1][1]


def grad_t(p, box):
    """objectBoundingBox gradient from (0,0) to (0.4,1), as the web's linearGradient"""
    x0, y0, x1, y1 = box
    u, v = (p[0] - x0) / max(1e-6, x1 - x0), (p[1] - y0) / max(1e-6, y1 - y0)
    return (0.4 * u + v) / 1.16


# ---------------------------------------------------------------- paths

def parse(d, steps=8):
    """an SVG path (M L H V C Z, absolute) as a polygon"""
    toks = re.findall(r"[MLHVCZ]|-?\d*\.?\d+", d)
    pts, i, cur, cmd = [], 0, (0.0, 0.0), None
    while i < len(toks):
        t = toks[i]
        if t in "MLHVCZ":
            cmd = t
            i += 1
            if cmd == "Z":
                continue
        if cmd in "ML":
            cur = (float(toks[i]), float(toks[i + 1]))
            pts.append(cur)
            i += 2
        elif cmd == "H":
            cur = (float(toks[i]), cur[1])
            pts.append(cur)
            i += 1
        elif cmd == "V":
            cur = (cur[0], float(toks[i]))
            pts.append(cur)
            i += 1
        elif cmd == "C":
            p1 = (float(toks[i]), float(toks[i + 1]))
            p2 = (float(toks[i + 2]), float(toks[i + 3]))
            p3 = (float(toks[i + 4]), float(toks[i + 5]))
            p0 = cur
            for s in range(1, steps + 1):
                k = s / steps
                a, b, c, e = (1 - k) ** 3, 3 * (1 - k) ** 2 * k, 3 * (1 - k) * k * k, k ** 3
                pts.append((a * p0[0] + b * p1[0] + c * p2[0] + e * p3[0], a * p0[1] + b * p1[1] + c * p2[1] + e * p3[1]))
            cur = p3
            i += 6
    if len(pts) > 1 and math.dist(pts[0], pts[-1]) < 1e-6:
        pts.pop()
    return pts


def area(poly):
    return 0.5 * sum(poly[i][0] * poly[i - 1][1] - poly[i - 1][0] * poly[i][1] for i in range(len(poly)))


def offset(poly, d, limit=2.5):
    """the polygon grown by d (shrunk when negative), mitered up to limit * d"""
    flip = area(poly) > 0  # (ey, -ex) points out of a polygon that turns clockwise on screen (area() < 0)
    n = len(poly)
    out = []
    for i in range(n):
        a, p, b = poly[i - 1], poly[i], poly[(i + 1) % n]
        e1 = (p[0] - a[0], p[1] - a[1])
        e2 = (b[0] - p[0], b[1] - p[1])
        l1, l2 = math.hypot(*e1) or 1, math.hypot(*e2) or 1
        n1 = (e1[1] / l1, -e1[0] / l1)
        n2 = (e2[1] / l2, -e2[0] / l2)
        if flip:
            n1, n2 = (-n1[0], -n1[1]), (-n2[0], -n2[1])
        s = (n1[0] + n2[0], n1[1] + n2[1])
        dot = 1 + n1[0] * n2[0] + n1[1] * n2[1]
        k = d / dot if dot > 1e-6 else d
        m = (s[0] * k, s[1] * k)
        ml = math.hypot(*m)
        if ml > abs(d) * limit:
            m = (m[0] / ml * abs(d) * limit, m[1] / ml * abs(d) * limit)
        out.append((p[0] + m[0], p[1] + m[1]))
    return out


def triangulate(poly):
    """ear clipping"""
    pts = list(poly)
    if area(pts) > 0:
        pts.reverse()  # now clockwise on screen: a convex corner has a positive cross product

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    def inside(p, a, b, c):
        d1, d2, d3 = cross(a, b, p), cross(b, c, p), cross(c, a, p)
        neg = (d1 < 0) or (d2 < 0) or (d3 < 0)
        pos = (d1 > 0) or (d2 > 0) or (d3 > 0)
        return not (neg and pos)

    tris, guard = [], 0
    while len(pts) > 3 and guard < 1000:
        guard += 1
        n = len(pts)
        for i in range(n):
            a, b, c = pts[i - 1], pts[i], pts[(i + 1) % n]
            if cross(a, b, c) <= 1e-9:
                continue
            if any(inside(p, a, b, c) for p in pts if p not in (a, b, c)):
                continue
            tris.append((a, b, c))
            pts.pop(i)
            break
        else:
            break
    if len(pts) == 3:
        tris.append(tuple(pts))
    return tris


def polyline(pts, w, cap=0.0):
    """a stroked polyline (miter joins) as a polygon; cap extends the ends (square caps: w / 2)"""
    h = w / 2
    dirs = []
    for a, b in zip(pts, pts[1:]):
        l = math.dist(a, b)
        dirs.append(((b[0] - a[0]) / l, (b[1] - a[1]) / l))
    norms = [(-d[1], d[0]) for d in dirs]
    left, right = [], []
    for i, p in enumerate(pts):
        if i == 0:
            q = (p[0] - dirs[0][0] * cap, p[1] - dirs[0][1] * cap)
            n = norms[0]
            left.append((q[0] + n[0] * h, q[1] + n[1] * h))
            right.append((q[0] - n[0] * h, q[1] - n[1] * h))
        elif i == len(pts) - 1:
            q = (p[0] + dirs[-1][0] * cap, p[1] + dirs[-1][1] * cap)
            n = norms[-1]
            left.append((q[0] + n[0] * h, q[1] + n[1] * h))
            right.append((q[0] - n[0] * h, q[1] - n[1] * h))
        else:
            n1, n2 = norms[i - 1], norms[i]
            s = (n1[0] + n2[0], n1[1] + n2[1])
            dot = 1 + n1[0] * n2[0] + n1[1] * n2[1]
            m = (s[0] * h / dot, s[1] * h / dot)
            left.append((p[0] + m[0], p[1] + m[1]))
            right.append((p[0] - m[0], p[1] - m[1]))
    return left + right[::-1]


# ---------------------------------------------------------------- elements

def tri_el(a, b, c, color, grow=0.25):
    """a triangle element: the right triangle of its top border, mapped onto a b c"""
    if grow:
        g = ((a[0] + b[0] + c[0]) / 3, (a[1] + b[1] + c[1]) / 3)

        def out(p):
            l = math.dist(p, g) or 1
            return (p[0] + (p[0] - g[0]) / l * grow, p[1] + (p[1] - g[1]) / l * grow)
        a, b, c = out(a), out(b), out(c)
    m = [(b[0] - a[0]) / 64, (b[1] - a[1]) / 64, (c[0] - a[0]) / 64, (c[1] - a[1]) / 64]
    return '<i class="t" style="left: %s; top: %s; border-top-color: %s; transform: matrix(%s, 0, 0);"/>' % (
        em(a[0]), em(a[1]), hexc(color), ", ".join("%.4f" % v for v in m))


def fill(poly, color_of):
    """a polygon's triangles; color_of(centroid) colors each"""
    out = []
    for a, b, c in triangulate(poly):
        g = ((a[0] + b[0] + c[0]) / 3, (a[1] + b[1] + c[1]) / 3)
        out.append(tri_el(a, b, c, color_of(g)))
    return out


def bbox(poly):
    xs, ys = [p[0] for p in poly], [p[1] for p in poly]
    return (min(xs), min(ys), max(xs), max(ys))


def metal(poly, c, stroke=1.2, box=None):
    """a part the web fills with its metal gradient and strokes dark"""
    box = box or bbox(poly)
    st = stops(c)
    out = fill(offset(poly, stroke / 2), lambda g: DARK) if stroke else []
    return out + fill(offset(poly, -stroke / 2) if stroke else poly, lambda g: at(st, grad_t(g, box)))


def flat(poly, color, stroke=0.0, stroke_color=DARK):
    out = fill(offset(poly, stroke / 2), lambda g: stroke_color) if stroke else []
    return out + fill(offset(poly, -stroke / 2) if stroke else poly, lambda g: color)


def div(x, y, w, h, style="", inner=""):
    return '<div class="p" style="left: %s; top: %s; width: %s; height: %s;%s">%s</div>' % (em(x), em(y), em(w), em(h), (" " + style) if style else "", inner)


SHIELD = "M32 6 L54 13 V33 C54 48 44 58 32 65 C20 58 10 48 10 33 V13 Z"
SHIELD_BOX = (10, 6, 54, 65)
INNER = "M32 11 L49 16.5 V33 C49 45 41 53.5 32 59.5 C23 53.5 15 45 15 33 V16.5 Z"


def circle_fit(side_x, mid_x, tip, y0=33.0):
    """the radius of the circle through the curve's two ends: vertical at (side_x, y0), through (mid_x, tip)"""
    dx = abs(side_x - mid_x)
    dy = tip - y0
    return (dx * dx + dy * dy) / (2 * dx)


# the web's gradient runs from (0, 0) to (0.4, 1) of the shield's box: in the icon's units that is
# this direction, which CSS names 151.8deg; t along it is grad_t
GRAD = (0.4 / (SHIELD_BOX[2] - SHIELD_BOX[0]), 1.0 / (SHIELD_BOX[3] - SHIELD_BOX[1]))
GRAD_DEG = 180 - math.degrees(math.atan2(GRAD[0], GRAD[1]))


def gradient_rect(x0, y0, x1, y1, st):
    """a rectangle carrying its share of the shield's gradient: the same direction, its stops moved
    to where the rectangle's gradient line meets them (CSS runs 0% to 100% corner to corner)"""
    a = math.radians(GRAD_DEG)
    d = (math.sin(a), -math.cos(a))
    w, h = x1 - x0, y1 - y0
    half = (abs(w * d[0]) + abs(h * d[1])) / 2
    c = ((x0 + x1) / 2, (y0 + y1) / 2)
    t0 = grad_t((c[0] - d[0] * half, c[1] - d[1] * half), SHIELD_BOX)
    t1 = grad_t((c[0] + d[0] * half, c[1] + d[1] * half), SHIELD_BOX)
    parts = ["%s 0%%" % hexc(at(st, t0))]
    for t, col in st:
        if t0 < t < t1:
            parts.append("%s %.1f%%" % (hexc(col), (t - t0) / (t1 - t0) * 100))
    parts.append("%s 100%%" % hexc(at(st, t1)))
    return div(x0, y0, w, h, "decorator: linear-gradient( %.1fdeg, %s );" % (GRAD_DEG, ", ".join(parts)))


def shield_body(left, right, top, roof_apex, tip, d, st):
    """the shield grown by d (shrunk when negative) in the shield's gradient: the roof's triangles,
    a box from the roof down to y 33, then rows down the two circles' lens to the tip, each row
    a gradient rectangle"""
    r0 = circle_fit(right, 32, tip)
    r = r0 + d
    ca = right - r0          # the circle whose right edge is the shield's right side; its mirror
    l, rt = left - d, right + d
    s = (top - roof_apex) / (32 - left)
    k = math.sqrt(1 + s * s)
    base = top + d * (s - k)
    apex = (32, roof_apex - d * k)
    els = []
    # the roof: a triangle a side, each in the gradient's color at its middle
    for a, b, cc in (((l, base), apex, (32, base)), (apex, (rt, base), (32, base))):
        g = ((a[0] + b[0] + cc[0]) / 3, (a[1] + b[1] + cc[1]) / 3)
        els.append(tri_el(a, b, cc, at(st, grad_t(g, SHIELD_BOX)), grow=0.15))
    els.append(gradient_rect(l, base - 0.1, rt, 33.0, st))

    def half(y):
        """the lens's half width at y"""
        v = r * r - (y - 33) ** 2
        return math.sqrt(v) - (32 - ca) + 0.0 if v > 0 else 0.0
    y = 33.0
    end = tip + d
    while y < end - 0.05:
        hw = half(y)
        # the next row ends where the side has moved in by 1.2 units (a pixel at 1080p), 4 at most
        ny = y + 0.25
        while ny < end and hw - half(ny) < 1.2 and ny - y < 4:
            ny += 0.25
        ny = min(ny, end)
        w = (half(y) + half(ny)) / 2
        if w > 0.05:
            els.append(gradient_rect(32 - w, y - 0.05, 32 + w, ny + 0.05, st))
        y = ny
    return els


def shield_polygon(path, d):
    return offset(parse(path, 5), d)


def icon(tier):
    n = ORDER.index(tier) if tier else -1
    c = rgb(TIER_COLORS[tier]) if tier else (93, 85, 79)
    st = stops(c) if n >= 0 else [(0, UNRANKED), (1, UNRANKED)]
    els = []
    # the glow
    if n >= 0:
        a = 0.55 if n >= 3 else 0.3
        els.append(div(-2, 2, 68, 68, "decorator: radial-gradient( circle closest-side, %s%02x 0%%, %s%02x 99%%, %s00 100%% );"
                       % (hexc(c), int(a * 255), hexc(c), int(a * 0.17 * 255), hexc(c))))
    # Godlike's halo (the web's animated drop shadows, without a filter's layer)
    if n == 5:
        els.append(div(-14, -8, 92, 92, "decorator: radial-gradient( circle closest-side, #ff6a2a66 0%, #ff3c1433 55%, #ff3c1400 100% );"))
    # wings, behind the shield (Diamond, Godlike)
    if n >= 4:
        lw = parse("M12 22 C2 20 -6 30 -8 40 C-2 36 3 37 6 40 C2 44 2 50 4 54 C7 48 10 46 13 46 Z", 3)
        rw = parse("M52 22 C62 20 70 30 72 40 C66 36 61 37 58 40 C62 44 62 50 60 54 C57 48 54 46 51 46 Z", 3)
        els += metal(lw, c) + metal(rw, c)
    # blades (Platinum up)
    if n >= 3:
        els += metal(parse("M11 18 L1 12 L7 28 Z"), c) + metal(parse("M53 18 L63 12 L57 28 Z"), c)
    # the horned crown (Godlike), the spike (Gold to Diamond)
    if n == 5:
        els += metal(parse("M20 12 C14 4 15 -4 19 -8 C19 -1 22 4 27 8 Z", 3), c)
        els += metal(parse("M44 12 C50 4 49 -4 45 -8 C45 -1 42 4 37 8 Z", 3), c)
        els += metal(parse("M26 9 L32 -2 L38 9 Z"), c)
    if n in (2, 3, 4):
        els += metal(parse("M27 9 L32 0 L37 9 Z"), c)
    # the shield: its dark outline (the path grown by 1), then the body (shrunk by 1) in the gradient
    els += flat(shield_polygon(SHIELD, 1.0), DARK)
    if n >= 0:
        els += shield_body(10, 54, 13, 6, 65, -1.0, st)
    else:
        els += flat(shield_polygon(SHIELD, -1.0), UNRANKED)
    # the inner shield: black at 35% over the shield, stroked white at 18%. Drawn opaque: the stroke
    # as the shield's gradient lightened (a triangle each), the fill as the gradient darkened
    lit = [(t, mix(WHITE, col, 0.18)) for t, col in st]
    dim = [(t, mix(BLACK, col, 0.35)) for t, col in st]
    els += fill(shield_polygon(INNER, 0.5), lambda g: at(lit, grad_t(g, SHIELD_BOX)))
    if n >= 0:
        # the inner path's curves: vertical at (49, 33), through the tip (32, 59.5)
        els += shield_body(15, 49, 16.5, 11, 59.5, -0.5, dim)
    else:
        els += flat(shield_polygon(INNER, -0.5), dim[0][1])
    if n < 0:
        els.append('<div class="p q">?</div>')
    # the emblem: chevrons for the metals, a star for Platinum, a gem for Diamond, an eye for Godlike
    if 0 <= n <= 2:
        stroke = mix(c, WHITE, 0.4)
        for i in range(n + 1):
            y = 27 + i * 9 + (2 - n) * 4.5
            els += flat(polyline([(21, y), (32, y + 8), (43, y)], 4, cap=2), stroke)
    if n == 3:
        star = parse("M32 21 L35.5 31 L46 31.5 L37.8 38 L40.8 48 L32 42.2 L23.2 48 L26.2 38 L18 31.5 L28.5 31 Z")
        els += flat(star, mix(c, WHITE, 0.35), stroke=1.0)
    if n == 4:
        gem = mix(c, WHITE, 0.4)
        els += flat(parse("M32 20 L44 32 L32 50 L20 32 Z"), gem, stroke=1.0)
        els += flat(parse("M32 20.8 L27.4 32 L32 48.6 L36.6 32 Z"), mix(WHITE, gem, 0.25))
        els.append(div(20.6, 31.5, 22.8, 1, "background-color: %s;" % hexc(DARK)))
    if n == 5:
        gold, ink = (255, 207, 106), (26, 5, 3)
        eye = parse("M18 34 C24 24 40 24 46 34 C40 44 24 44 18 34 Z", 5)
        els += flat(eye, ink, stroke=1.5, stroke_color=gold)
        els.append(div(26, 28, 12, 12, "border-radius: %s; background-color: %s;" % (em(6), hexc(gold))))
        els.append(div(29.4, 31.4, 5.2, 5.2, "border-radius: %s; background-color: %s;" % (em(2.6), hexc(ink))))
        els += flat(polyline([(24, 48), (32, 54), (40, 48)], 2.5), gold)
    cls = "rk " + tier if tier else "rk"
    return '<div class="%s">%s</div>' % (cls, "".join(els))


def tier_cards():
    out = []
    for i, t in enumerate(ORDER):
        out.append('\t\t\t\t\t\t<div class="tier" data-class-mine="ranked.tier == \'%s\'" data-style-border-color="ranked.tier == \'%s\' ? \'%s\' : \'#ffffff12\'">' % (t, t, TIER_COLORS[t]))
        out.append("\t\t\t\t\t\t\t" + icon(t))
        out.append('\t\t\t\t\t\t\t<div class="q3 n" style="color: %s;">{{ ranked.tiers[%d].name }}</div>' % (TIER_COLORS[t], i))
        out.append('\t\t\t\t\t\t\t<div class="mono r">{{ ranked.tiers[%d].range }}</div>' % i)
        out.append('\t\t\t\t\t\t\t<div class="q3 you" data-if="ranked.tier == \'%s\'">You</div>' % t)
        out.append("\t\t\t\t\t\t</div>")
    return "\n".join(out)


def replace(text, name, body):
    """the text between <!-- generated: name --> and <!-- end: name -->, the end marker's indent kept"""
    begin, end = "<!-- generated: %s -->" % name, "<!-- end: %s -->" % name
    i, j = text.index(begin) + len(begin), text.index(end)
    indent = text[text.rfind("\n", 0, j) + 1:j]
    return text[:i] + "\n" + body + "\n" + indent + text[j:]


def main():
    text = open(PAGE, encoding="utf-8").read()
    text = replace(text, "unranked icon", "\t\t\t\t\t\t\t" + icon(None))
    text = replace(text, "tier cards", tier_cards())
    open(PAGE, "w", encoding="utf-8", newline="\n").write(text)
    print("wrote", PAGE, len(text), "bytes")


main()
