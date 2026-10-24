"""Reflection probe boxes for a Quake 3 map, from its BSP (build_maps.py writes them to the map
JSON as "probes"; Q3Map adds a box-projected ReflectionProbe for each at runtime).

reflection_probes(bsp) -> [{"mins", "maxs", "origin"}] in id units and axes.

The open space is sampled on a 64-unit grid: a point is open when the world's BSP tree puts it in
a leaf with a cluster (q3map gives solid and outside leaves cluster -1). The open grid is then cut
into boxes, roomiest seeds first: a box grows one layer at a time on every side while the new layer
is mostly open, unclaimed and reached from the box with no wall between (walls thinner than the
grid fall between cell centers, so the points between are checked too), up to PROBE_MAX on a side,
so a box ends where a room's walls are. Detail brushes sit in open leaves and read as open.
Small leftovers (door frames, thin gaps) join the neighbor box they fit best, or are dropped. The
boxes holding the most play space (open cells just above a floor) are kept, up to PROBE_LIMIT; a
point outside every probe falls back to the sky's reflection, as everything did before probes.
"""
import numpy as np
from scipy import ndimage

PROBE_CELL = 64.0     # grid step (id units)
PROBE_MAX = 1536.0    # longest box side
PROBE_FILL = 0.6      # share of a new layer that must be open and unclaimed (0.8 left a quarter of
                      # q3ctf3's play space outside every probe; the reflections are matte and blurry,
                      # so a box that's a little loose around pillars and slopes doesn't show)
PROBE_MIN = 18        # open cells a box needs to stand alone (3 x 3 x 2)
PROBE_LIMIT = 64      # most probes per map (the reflection atlas holds 80, project.godot)
MERGE_FILL = 0.55     # share of a merged box that must be open
PASS_STEP = 8.0       # spacing of the points checked between neighbor cells (thinnest wall seen)
PLAY_HEIGHT = 4       # cells above a floor that count as play space


def leaf_clusters(bsp, pts):
    """The cluster of the leaf each point (id units) falls in, through model 0's node tree."""
    nodes = np.frombuffer(bsp.nodes, "<i4").reshape(-1, 9)
    leafs = np.frombuffer(bsp.leafs, "<i4").reshape(-1, 12)
    normals = bsp.planes["normal"].astype(np.float64)
    dists = bsp.planes["dist"].astype(np.float64)
    idx = np.zeros(len(pts), np.int64)
    active = np.ones(len(pts), bool)
    while active.any():
        n = idx[active]
        pl = nodes[n, 0]
        d = (pts[active] * normals[pl]).sum(1) - dists[pl]
        idx[active] = np.where(d >= 0, nodes[n, 1], nodes[n, 2])
        active = idx >= 0
    return leafs[-(idx + 1), 0]


def _is_open(bsp, pts, chunk=1 << 20):
    return np.concatenate([leaf_clusters(bsp, pts[i:i + chunk]) >= 0 for i in range(0, len(pts), chunk)])


def open_grid(bsp):
    m0 = bsp.models[0]
    lo = np.floor(m0["mins"].astype(np.float64) / PROBE_CELL) * PROBE_CELL
    hi = np.ceil(m0["maxs"].astype(np.float64) / PROBE_CELL) * PROBE_CELL
    shape = np.maximum(((hi - lo) / PROBE_CELL).astype(int), 1)
    ii = np.indices(shape).reshape(3, -1).T
    pts = lo + (ii + 0.5) * PROBE_CELL
    return lo, _is_open(bsp, pts).reshape(shape)


def passages(bsp, lo, open_):
    """passages[ax][i, j, k]: open cell (i, j, k) reaches its open neighbor one step along axis ax
    with every point between their centers open (PASS_STEP apart). A wall thinner than the grid
    can fall between two cell centers, so this is what stops a box growing through it."""
    out = []
    for ax in range(3):
        a = [slice(None)] * 3
        b = [slice(None)] * 3
        a[ax] = slice(0, -1)
        b[ax] = slice(1, None)
        both = open_[tuple(a)] & open_[tuple(b)]
        cells = np.argwhere(both)
        ok = np.ones(len(cells), bool)
        step = np.zeros(3)
        step[ax] = PASS_STEP
        for t in range(1, int(PROBE_CELL / PASS_STEP)):
            ok &= _is_open(bsp, lo + (cells + 0.5) * PROBE_CELL + step * t)
        p = np.zeros(open_.shape, bool)
        p[tuple(cells[ok].T)] = True
        out.append(p)   # True at the lower cell of each passable pair
    return out


def play_space(open_):
    """Open cells with a floor at most PLAY_HEIGHT cells below: where players and items are, as
    opposed to the void around a space map, which no surface sits in."""
    solid = ~open_
    near = np.zeros_like(open_)
    for dz in range(1, PLAY_HEIGHT + 1):
        near[:, :, dz:] |= solid[:, :, :-dz]
    return open_ & near


def _grow(avail, passes, seed, cap):
    """The box (lo, hi cell indices, hi exclusive) grown from one seed cell. A new layer's cell
    counts when it's unclaimed and reached from the box's cell beside it (passages)."""
    lo = np.array(seed)
    hi = lo + 1
    grew = True
    while grew:
        grew = False
        for ax in range(3):
            for side in (0, 1):
                if hi[ax] - lo[ax] >= cap:
                    continue
                at = lo[ax] - 1 if side == 0 else hi[ax]
                if at < 0 or at >= avail.shape[ax]:
                    continue
                sl = [slice(lo[i], hi[i]) for i in range(3)]
                sl[ax] = slice(at, at + 1)
                link = list(sl)   # passages are stored at the lower cell of each pair
                link[ax] = slice(at, at + 1) if side == 0 else slice(at - 1, at)
                if (avail[tuple(sl)] & passes[ax][tuple(link)]).mean() >= PROBE_FILL:
                    if side == 0:
                        lo[ax] -= 1
                    else:
                        hi[ax] += 1
                    grew = True
    return lo, hi


def _boxes(open_, passes):
    """Cut the open cells into boxes; returns [(lo, hi, open cells claimed)]."""
    avail = open_.copy()
    room = ndimage.distance_transform_edt(open_)   # roomiest seeds first
    order = np.argsort(-room, axis=None)
    cap = int(PROBE_MAX / PROBE_CELL)
    out = []
    for flat in order:
        seed = np.unravel_index(flat, open_.shape)
        if not avail[seed]:
            continue
        lo, hi = _grow(avail, passes, seed, cap)
        sl = tuple(slice(lo[i], hi[i]) for i in range(3))
        claimed = int(avail[sl].sum())
        avail[sl] = False
        out.append([lo, hi, claimed])
    return out


def _merge_small(boxes, open_):
    """Each box under PROBE_MIN joins the neighbor whose bounding union stays the most open
    (at least MERGE_FILL, sides within PROBE_MAX), or is dropped."""
    cap = int(PROBE_MAX / PROBE_CELL)
    big = [b for b in boxes if b[2] >= PROBE_MIN]
    for lo, hi, n in (b for b in boxes if b[2] < PROBE_MIN):
        best, best_fill = None, MERGE_FILL
        for b in big:
            if np.any(lo > b[1]) or np.any(hi < b[0]):   # not touching
                continue
            ulo, uhi = np.minimum(lo, b[0]), np.maximum(hi, b[1])
            if np.any(uhi - ulo > cap):
                continue
            fill = open_[tuple(slice(ulo[i], uhi[i]) for i in range(3))].mean()
            if fill >= best_fill:
                best, best_fill = b, fill
        if best is not None:
            best[0], best[1], best[2] = np.minimum(lo, best[0]), np.maximum(hi, best[1]), best[2] + n
    return big


def reflection_probes(bsp):
    lo, open_ = open_grid(bsp)
    if not open_.any():
        return []
    play = play_space(open_)
    boxes = _merge_small(_boxes(open_, passages(bsp, lo, open_)), open_)
    # the boxes holding the most play space win; boxes of void alone are dropped
    score = [int(play[tuple(slice(b[0][i], b[1][i]) for i in range(3))].sum()) for b in boxes]
    ranked = [b for s, b in sorted(zip(score, boxes), key=lambda sb: -sb[0]) if s > 0]
    out = []
    for blo, bhi, _ in ranked[:PROBE_LIMIT]:
        sl = tuple(slice(blo[i], bhi[i]) for i in range(3))
        # capture from the open cell nearest the box's middle
        cells = np.argwhere(open_[sl]) + blo
        mid = (blo + bhi) * 0.5
        c = cells[np.argmin(((cells + 0.5 - mid) ** 2).sum(1))]
        out.append({"mins": (lo + blo * PROBE_CELL).tolist(), "maxs": (lo + bhi * PROBE_CELL).tolist(),
                    "origin": (lo + (c + 0.5) * PROBE_CELL).tolist()})
    return out


def coverage(bsp, probes):
    """Share of the play space inside at least one probe box (for the report). `probes` in id
    units, as reflection_probes returns them (not the meters build_maps.py stores)."""
    lo, open_ = open_grid(bsp)
    play = play_space(open_)
    inside = np.zeros_like(open_)
    for p in probes:
        a = np.round((np.array(p["mins"]) - lo) / PROBE_CELL).astype(int)
        b = np.round((np.array(p["maxs"]) - lo) / PROBE_CELL).astype(int)
        inside[a[0]:b[0], a[1]:b[1], a[2]:b[2]] = True
    return float((inside & play).sum()) / max(int(play.sum()), 1)
