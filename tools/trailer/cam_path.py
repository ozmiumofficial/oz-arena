"""Camera paths for the trailer: the .cam format, its spline, and a check against the map's walls.

python tools/trailer/cam_path.py check <path> [<path> ...] | --all   [--margin 8]

A path is tools/trailer/paths/<name>.cam. The first line names the map ("// map q3dm17"); then one
key per line: time  x y z  pitch yaw roll  fov  (seconds, id units, degrees; fov horizontal).
eval() is the same cubic Hermite spline as q3cam's cgame (tools/trailer/q3cam/src/cg_q3cam.c):
Catmull-Rom tangents inside, flat at both ends. The remaster's reader has to match it too.

check samples every frame at 60 fps and reports frames whose camera is within --margin units of
the inside of a solid brush, which would show the void in both games. Curves and misc_models are
not brushes, so the captures still need a look.
"""
import os, re, sys, glob, zipfile, argparse
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import q3bsp, q3common

PATHS = os.path.join(HERE, "paths")
FPS = 60
CONTENTS_SOLID = 1


def load(name):
    """(map, keys) with keys an array of rows [time, x, y, z, pitch, yaw, roll, fov]."""
    text = open(os.path.join(PATHS, name + ".cam")).read()
    m = re.match(r"\s*//\s*map\s+(\S+)", text)
    if not m:
        raise ValueError(f"{name}.cam: the first line must be // map <name>")
    rows = []
    for line in text.splitlines():
        line = line.split("//")[0].strip()
        if line:
            v = [float(x) for x in line.split()]
            if len(v) != 8:
                raise ValueError(f"{name}.cam: a key needs 8 numbers: {line}")
            rows.append(v)
    keys = np.array(rows)
    if len(keys) == 0 or np.any(np.diff(keys[:, 0]) <= 0):
        raise ValueError(f"{name}.cam: keys must exist and go forward in time")
    return m.group(1), keys


def _tangent(keys, i):
    if i <= 0 or i >= len(keys) - 1:
        return np.zeros(7)
    return (keys[i + 1, 1:] - keys[i - 1, 1:]) / (keys[i + 1, 0] - keys[i - 1, 0])


def evaluate(keys, t):
    """[x, y, z, pitch, yaw, roll, fov] at time t, as CG_CamEval."""
    if len(keys) == 1 or t <= keys[0, 0]:
        return keys[0, 1:].copy()
    if t >= keys[-1, 0]:
        return keys[-1, 1:].copy()
    i = 0
    while i < len(keys) - 2 and t >= keys[i + 1, 0]:
        i += 1
    a, b = keys[i], keys[i + 1]
    dt = b[0] - a[0]
    u = (t - a[0]) / dt
    u2, u3 = u * u, u * u * u
    return ((2 * u3 - 3 * u2 + 1) * a[1:] + (u3 - 2 * u2 + u) * dt * _tangent(keys, i)
            + (-2 * u3 + 3 * u2) * b[1:] + (u3 - u2) * dt * _tangent(keys, i + 1))


def frames(keys, fps=FPS):
    """Every frame's view, both ends included, as q3cam flies it."""
    n = int(round((keys[-1, 0] - keys[0, 0]) * fps)) + 1
    return np.array([evaluate(keys, keys[0, 0] + f / fps) for f in range(n)])


def read_map(name):
    p = os.path.join(ROOT, "extracted", "baseq3", "maps", name + ".bsp")
    if os.path.isfile(p):
        return q3bsp.read_bsp(open(p, "rb").read())
    for base in q3common.DEFAULT_Q3:
        for pk3 in sorted(glob.glob(os.path.join(base, "*.pk3")), reverse=True):
            with zipfile.ZipFile(pk3) as z:
                if f"maps/{name}.bsp" in z.namelist():
                    return q3bsp.read_bsp(z.read(f"maps/{name}.bsp"))
    raise FileNotFoundError(f"maps/{name}.bsp not found")


def solid_frames(bsp, points, margin=8.0):
    """Indices of points within margin of the inside of a solid world brush."""
    m0 = bsp.models[0]
    hit = np.zeros(len(points), bool)
    for bi in range(int(m0["first_brush"]), int(m0["first_brush"]) + int(m0["num_brushes"])):
        brush = bsp.brushes[bi]
        if not q3bsp.brush_contents(bsp, brush) & CONTENTS_SOLID:
            continue
        n, d = q3bsp.brush_planes(bsp, brush)
        hit |= np.all(points @ n.T - d < margin, axis=1)
    return np.nonzero(hit)[0]


def check(names, margin):
    bad = 0
    cache = {}
    for name in names:
        mp, keys = load(name)
        if mp not in cache:
            cache[mp] = read_map(mp)
        f = frames(keys)
        hits = solid_frames(cache[mp], f[:, :3], margin)
        if len(hits):
            bad += 1
            runs = np.split(hits, np.nonzero(np.diff(hits) > 1)[0] + 1)
            spans = ", ".join(f"{r[0] / FPS:.2f}-{r[-1] / FPS:.2f} s" for r in runs)
            print(f"{name}: in a wall at {spans} (e.g. {np.round(f[hits[0], :3]).astype(int).tolist()})")
        else:
            print(f"{name}: clear ({len(f)} frames, {mp})")
    return bad


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["check"])
    ap.add_argument("paths", nargs="*")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--margin", type=float, default=8.0)
    a = ap.parse_args()
    names = sorted(os.path.splitext(os.path.basename(p))[0] for p in glob.glob(os.path.join(PATHS, "*.cam"))) \
        if a.all else a.paths
    sys.exit(1 if check(names, a.margin) else 0)
