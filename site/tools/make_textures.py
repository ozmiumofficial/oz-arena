"""The site's textures, generated (no photos, no game art): seamless tiles for site/img/.

python site/tools/make_textures.py

  tex-iron.jpg   dark steel plate, rust bloom, pitting and scratches (panels)
  tex-stone.jpg  near-black stone with cracks and damp stains (the page)
  tex-grime.png  transparent grime, streaks and scratches (laid over anything)
Every noise is filtered with wrap-around edges, so each tile repeats without a seam.
"""
import os
import numpy as np
from PIL import Image
from scipy import ndimage as ndi

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "img")
N = 512
rng = np.random.default_rng(1913)


def noise(scales, weights, n=N):
    out = np.zeros((n, n), np.float32)
    for s, w in zip(scales, weights):
        r = ndi.gaussian_filter(rng.standard_normal((n, n)).astype(np.float32), s, mode="wrap")
        out += w * r / (r.std() + 1e-6)
    return out


def scratches(count, length, spread, n=N):
    img = np.zeros((n, n), np.float32)
    for _ in range(count):
        x, y = rng.uniform(0, n, 2)
        a = rng.normal(-0.5, spread)
        steps = int(rng.uniform(0.3, 1) * length)
        v = rng.uniform(0.3, 1)
        for t in range(steps):
            xi, yi = int(x + np.cos(a) * t) % n, int(y + np.sin(a) * t) % n
            img[yi, xi] = max(img[yi, xi], v)
    return ndi.gaussian_filter(img, 0.5, mode="wrap")


def cracks(count, n=N):
    img = np.zeros((n, n), np.float32)
    for _ in range(count):
        x, y = rng.uniform(0, n, 2)
        a = rng.uniform(0, 6.28)
        for t in range(int(rng.uniform(60, 220))):
            a += rng.normal(0, 0.35)
            x, y = x + np.cos(a), y + np.sin(a)
            img[int(y) % n, int(x) % n] = 1
            if rng.random() < 0.01:   # a branch
                bx, by, ba = x, y, a + rng.choice([-1, 1]) * 0.9
                for _ in range(int(rng.uniform(15, 60))):
                    ba += rng.normal(0, 0.4)
                    bx, by = bx + np.cos(ba), by + np.sin(ba)
                    img[int(by) % n, int(bx) % n] = 0.7
    return ndi.gaussian_filter(img, 0.6, mode="wrap")


def save(arr, name, alpha=None):
    rgb = np.clip(arr, 0, 255).astype(np.uint8)
    if alpha is None:
        Image.fromarray(rgb, "RGB").save(os.path.join(OUT, name), quality=86, optimize=True, progressive=True)
    else:
        a = np.clip(alpha * 255, 0, 255).astype(np.uint8)
        Image.fromarray(np.dstack([rgb, a]), "RGBA").save(os.path.join(OUT, name), optimize=True)
    print("wrote", name)


# -------------------------------------------------------------- iron
base = noise((1, 3, 10, 40), (0.15, 0.25, 0.3, 0.3))
brushed = ndi.gaussian_filter1d(rng.standard_normal((N, N)).astype(np.float32), 30, axis=1, mode="wrap")
brushed /= brushed.std()
steel = np.array([34, 31, 30], np.float32) * (1 + 0.16 * base + 0.05 * brushed)[..., None]
rust_m = np.clip((noise((4, 14, 50), (0.25, 0.4, 0.35)) - 0.35) * 0.9, 0, 0.95)
rust_fine = 1 + 0.35 * noise((1, 2), (0.6, 0.4))
rust_c = np.array([66, 30, 16], np.float32) * rust_fine[..., None]
iron = steel * (1 - rust_m[..., None]) + rust_c * rust_m[..., None]
pits = np.clip(noise((0.8,), (1,)) - 2.2, 0, 1)
iron *= (1 - 0.6 * pits)[..., None]
sc = scratches(110, 60, 0.6)
iron += sc[..., None] * np.array([26, 22, 20])
save(iron, "tex-iron.jpg")

# -------------------------------------------------------------- stone
st = noise((2, 8, 30, 90), (0.2, 0.3, 0.3, 0.2))
stone = np.array([19, 15, 14], np.float32) * (1 + 0.3 * st)[..., None]
damp = np.clip((noise((20, 60), (0.5, 0.5)) - 0.4) * 0.8, 0, 1)
stone = stone * (1 - 0.45 * damp[..., None]) + np.array([24, 10, 7], np.float32) * 0.45 * damp[..., None]
cr = cracks(18)
stone *= (1 - 0.75 * cr)[..., None]
stone += ndi.shift(cr, (1, 1), mode="wrap")[..., None] * 6
save(stone, "tex-stone.jpg")

# -------------------------------------------------------------- grime overlay
g = np.clip((noise((3, 12, 40), (0.3, 0.4, 0.3)) - 0.2) * 0.5, 0, 1)
streak = np.clip(ndi.gaussian_filter(rng.standard_normal((N, N)).astype(np.float32), (2, 6), mode="wrap") * 4 - 1.2, 0, None)
streak = ndi.gaussian_filter1d(streak, 45, axis=0, mode="wrap")
streak = np.clip(streak / (streak.max() + 1e-6) * 1.6, 0, 1)
dark_a = np.clip(0.55 * g + 0.35 * streak, 0, 0.85)
sc2 = scratches(45, 60, 0.7)
light_a = np.clip(sc2 * 0.18, 0, 0.18)
alpha = np.maximum(dark_a, light_a)
col = np.where((light_a > dark_a)[..., None], np.array([190, 160, 130], np.float32), np.array([6, 4, 3], np.float32))
col = np.broadcast_to(col, (N, N, 3))
save(col, "tex-grime.png", alpha)
