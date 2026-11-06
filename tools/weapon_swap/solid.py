"""python solid.py orig.npz high.npz voxel thick [face,face,...]
A watertight surface for Tripo's mesh without capping its holes. The solid is where the mesh's generalized
winding number is over 1/2 (robust to holes, overlapping shells and stray inside faces), joined with the mesh's
surface thickened by `thick` (so open sheets, which enclose nothing, stay as thin plates). Its signed distance
(exact distance to the triangles, sign from the winding number) is sampled on a voxel grid, exactly in a narrow
band around the surface and from a coarse grid elsewhere, and its zero surface taken with marching cubes.

Capping holes before a voxel remesh, the earlier way, stretched fans across tangled open loops: they cut false
pockets and left spiky membranes in the cell cut-outs and the side vent (Tony, 11/6/26). Thickening every face
instead of only the open sheets is on purpose: without it the vent's bars came apart (Tripo's bars are half open).
It grows the surface by `thick` (0.0006 of a 1-unit model: 0.02 game units)."""
import sys, time
import numpy as np
import igl
from skimage import measure
from scipy import ndimage

src, dst, h, thick = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4])
# faces of prep_orig.py's triangles to leave out, found by eye: a model's stray flaps the rules below miss
by_hand = [int(x) for x in sys.argv[5].split(",")] if len(sys.argv) > 5 and sys.argv[5] else []
d = np.load(src)
V, F = d["v"].astype(np.float64), d["f"].astype(np.int64)
if by_hand:
    F = np.delete(F, by_hand, axis=0)
    print("left out %d faces by hand" % len(by_hand))

# open-sheet slivers go: a face with the winding number under 1/2 on both sides encloses nothing, and when it is
# also a sliver (thinner than SLIVER) it is one of Tripo's stray flaps (the fins at the top of the side vent),
# which the thickening below would keep as a spike
SLIVER = 0.004
a, b, c = V[F[:, 0]], V[F[:, 1]], V[F[:, 2]]
n = np.cross(b - a, c - a)
ar = np.linalg.norm(n, axis=1)
n /= np.maximum(ar, 1e-12)[:, None]
longest = np.max([np.linalg.norm(b - a, axis=1), np.linalg.norm(c - b, axis=1), np.linalg.norm(a - c, axis=1)], 0)
alt = ar / np.maximum(longest, 1e-12)
ctr = (a + b + c) / 3
wp = igl.fast_winding_number(V, F, ctr + n * 2 * h)
wm = igl.fast_winding_number(V, F, ctr - n * 2 * h)
drop = (wp < 0.5) & (wm < 0.5) & (alt < SLIVER)
F = F[~drop]
print("dropped %d open-sheet slivers" % drop.sum())
B = 4                                  # fine voxels per coarse voxel
H = h * B
lo = V.min(0) - 3 * H
hi = V.max(0) + 3 * H
nc = np.ceil((hi - lo) / H).astype(int) + 1
hi = lo + (nc - 1) * H
nf = (nc - 1) * B + 1
print("coarse", nc, "fine", nf, "%.0fM fine voxels" % (np.prod(nf) / 1e6))


def sdf(P):
    s = igl.signed_distance(P, V, F, sign_type=igl.SIGNED_DISTANCE_TYPE_FAST_WINDING_NUMBER)[0]
    return np.minimum(s, np.abs(s) - thick)


t = time.time()
g = [lo[i] + H * np.arange(nc[i]) for i in range(3)]
P = np.stack(np.meshgrid(*g, indexing="ij"), -1).reshape(-1, 3)
Sc = sdf(P).reshape(nc).astype(np.float32)
print("coarse sdf %.1fs" % (time.time() - t))

# fine grid: the coarse field, upsampled (right in sign away from the surface), then exact in the band
t = time.time()
idx = np.stack(np.meshgrid(*[np.arange(n, dtype=np.float32) / B for n in nf], indexing="ij"), 0)
vol = ndimage.map_coordinates(Sc, idx, order=1).astype(np.float32)
del idx
band = np.abs(Sc) < 1.9 * H
band = ndimage.binary_dilation(band, np.ones((2, 2, 2), bool), origin=0) | band
cells = np.argwhere(band[:-1, :-1, :-1])
print("band cells", len(cells))
off = np.stack(np.meshgrid(*[np.arange(B + 1)] * 3, indexing="ij"), -1).reshape(-1, 3)
CH = 20000
done = np.zeros(nf, bool)
for i in range(0, len(cells), CH):
    c = cells[i:i + CH]
    fi = (c[:, None, :] * B + off[None]).reshape(-1, 3)
    fi = np.unique(fi, axis=0)
    fi = fi[~done[fi[:, 0], fi[:, 1], fi[:, 2]]]
    if not len(fi):
        continue
    vol[fi[:, 0], fi[:, 1], fi[:, 2]] = sdf(lo + fi * h)
    done[fi[:, 0], fi[:, 1], fi[:, 2]] = True
    print("  band %d/%d  %.0fs" % (min(i + CH, len(cells)), len(cells), time.time() - t), flush=True)
print("fine sdf %.1fs (%d exact)" % (time.time() - t, done.sum()))
del done

# fins: anything thinner than about 2 * FIN goes (Tripo's stray flaps, kept by the thickening: the spike at the
# top of the side vent). Only the voxels an opening of the solid removes are set outside, so every other part of
# the surface keeps its exact, sub-voxel place. A FIN of 2 voxels on a 1-unit model is 0.05 game units.
t = time.time()
FIN = 2
inside = vol < 0
r = np.arange(-FIN, FIN + 1)
ball = (r[:, None, None] ** 2 + r[None, :, None] ** 2 + r[None, None, :] ** 2) <= FIN * FIN + 0.5
opened = ndimage.binary_opening(inside, ball)
fin = inside & ~opened
del inside
# the opening also shaves every sharp edge by a voxel or so: only what stands out more than 1.5 voxels from the
# opened solid is cut, and the field there rises with that distance, so the cut is smooth, not voxel steps
lab, nlab = ndimage.label(fin, np.ones((3, 3, 3), bool))
cutm = np.zeros(vol.shape, bool)
for sl in ndimage.find_objects(lab):
    box = tuple(slice(max(0, q.start - 4), q.stop + 4) for q in sl)
    dist = ndimage.distance_transform_edt(~opened[box])
    m = fin[box] & (dist > 1.5)
    if m.any():
        sub = vol[box]
        sub[m] = np.maximum(sub[m], (dist[m] - 1.5) * h)
        cutm[box] |= m
del fin, lab, opened
# then a smooth brush around each cut, fading out within a few voxels: the fin's stub and the pits it leaves go
lab, ncut = ndimage.label(cutm, np.ones((3, 3, 3), bool))
for sl in ndimage.find_objects(lab):
    box = tuple(slice(max(0, q.start - 8), q.stop + 8) for q in sl)
    sub = vol[box]
    w = np.clip(ndimage.gaussian_filter(cutm[box].astype(np.float32), 2.0) * 6, 0, 1)
    sub[:] = sub * (1 - w) + ndimage.gaussian_filter(sub, 1.5) * w
print("fins: %d voxels cut in %d places (%.1fs)" % (cutm.sum(), ncut, time.time() - t))
del cutm, lab

t = time.time()
verts, faces, _, _ = measure.marching_cubes(vol, level=0.0, spacing=(h, h, h), allow_degenerate=False)
verts += lo
print("marching cubes %.1fs: %d verts %d tris" % (time.time() - t, len(verts), len(faces)))
np.savez(dst, v=verts.astype(np.float32), f=faces.astype(np.int32))
