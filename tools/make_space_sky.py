"""Procedural space panorama for the space maps' skies (q3dm10, q3dm17, q3dm19, q3ctf4, q3tourney6...).

Quake 3's space skybox faces are tiny (256 px), so the 2048x1024 panorama made from them is soft and
blocky. This draws a new equirectangular one with real detail: a star field with color and a few
bright stars, a galactic band of layered nebula, dark dust lanes and a distant planet.

The mapping is the one sky_panorama.gdshader reads: u = atan(x, z) / TAU + 0.5, v = acos(y) / PI,
with Godot's y up. The noise is 3D on the sphere, so the seam and the poles are clean.

    python tools/make_space_sky.py [--out PATH] [--width 4096] [--seed 7]
"""
import argparse
import os
import sys

import cv2
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_OUT = os.path.join(ROOT, "godot", "library", "world", "skies", "env_space1_rt_pano.png")


def _hash3(ix, iy, iz, seed):
    """Lattice hash -> float in [0, 1)."""
    h = (ix.astype(np.uint32) * np.uint32(374761393)
         + iy.astype(np.uint32) * np.uint32(668265263)
         + iz.astype(np.uint32) * np.uint32(2147483647)
         + np.uint32((seed * 1274126177) & 0xFFFFFFFF))
    h = (h ^ (h >> np.uint32(13))) * np.uint32(1274126177)
    h = h ^ (h >> np.uint32(16))
    return (h & np.uint32(0xFFFFFF)).astype(np.float32) / np.float32(0x1000000)


def value_noise(p, seed):
    """Trilinear value noise for points p (..., 3), in [0, 1]."""
    i = np.floor(p).astype(np.int64)
    f = (p - i).astype(np.float32)
    f = f * f * (3.0 - 2.0 * f)
    out = 0.0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = ((f[..., 0] if dx else 1 - f[..., 0])
                     * (f[..., 1] if dy else 1 - f[..., 1])
                     * (f[..., 2] if dz else 1 - f[..., 2]))
                out = out + w * _hash3(i[..., 0] + dx, i[..., 1] + dy, i[..., 2] + dz, seed)
    return out


def fbm(p, seed, octaves=6, lacunarity=2.03, gain=0.5):
    total = np.zeros(p.shape[:-1], np.float32)
    amp, norm = 1.0, 0.0
    for o in range(octaves):
        total += amp * value_noise(p * (lacunarity ** o) + o * 17.3, seed + o)
        norm += amp
        amp *= gain
    return total / norm


def unit(v):
    v = np.asarray(v, np.float32)
    return v / np.linalg.norm(v)


def build(width, seed):
    height = width // 2
    rng = np.random.default_rng(seed)
    img = np.zeros((height, width, 3), np.float32)
    behind = np.zeros((height, width), np.float32)   # the planet's disc: no stars show through it

    band_normal = unit([0.25, 1.0, 0.35])
    planet_dir = unit([0.62, 0.22, -0.75])
    planet_ang = 0.20   # radians
    sun_dir = unit([-0.7, 0.35, 0.6])
    star_light = unit([0.9, 0.5, 0.35])   # lights the planet from the side: a big crescent

    chunk = 256
    for y0 in range(0, height, chunk):
        v = (np.arange(y0, min(y0 + chunk, height), dtype=np.float32) + 0.5) / height
        u = (np.arange(width, dtype=np.float32) + 0.5) / width
        phi = (u - 0.5) * (2.0 * np.pi)
        theta = v * np.pi
        st, ct = np.sin(theta)[:, None], np.cos(theta)[:, None]
        x = st * np.sin(phi)[None, :]
        z = st * np.cos(phi)[None, :]
        y = np.broadcast_to(ct, x.shape)
        d = np.stack([x, y, z], axis=-1)

        # the galactic band: strongest at the plane, fading by about 40 degrees
        lat = np.abs(d @ band_normal)
        band = np.exp(-(lat / 0.34) ** 2).astype(np.float32)

        # domain-warped nebula
        warp = np.stack([fbm(d * 1.6 + k * 11.0, seed + 100 + k, 4) for k in range(3)], axis=-1) - 0.5
        q = d * 2.2 + warp * 1.7
        n1 = fbm(q, seed + 1, 6)
        n2 = fbm(q * 1.7 + 5.0, seed + 2, 5)
        n3 = fbm(d * 3.4 + warp * 2.4 + 9.0, seed + 3, 5)
        dust = fbm(d * 5.0 + warp * 1.2 + 21.0, seed + 4, 5)

        gas = np.clip((n1 - 0.42) * 2.6, 0, 1) * (0.25 + 0.75 * band)
        gas2 = np.clip((n2 - 0.45) * 2.8, 0, 1) * (0.2 + 0.8 * band)
        glow = np.clip((n3 - 0.5) * 3.0, 0, 1) * band

        blue = np.array([0.10, 0.16, 0.55], np.float32)
        violet = np.array([0.42, 0.12, 0.62], np.float32)
        rose = np.array([0.80, 0.22, 0.34], np.float32)
        teal = np.array([0.08, 0.42, 0.52], np.float32)
        col = (gas[..., None] * blue
               + gas2[..., None] * violet
               + (gas * gas2)[..., None] * 1.2 * rose
               + glow[..., None] * 0.55 * teal)

        # dark dust lanes cutting through the band
        lanes = np.clip((dust - 0.50) * 3.2, 0, 1) * band
        col *= (1.0 - 0.8 * lanes)[..., None]

        # a faint warm haze toward one side of the band
        warm = np.clip(d @ sun_dir, 0, 1) ** 3
        col += (warm * band * 0.10)[..., None] * np.array([0.9, 0.5, 0.25], np.float32)

        # the planet: shaded disc with a lit limb from sun_dir
        cosang = d @ planet_dir
        ang = np.arccos(np.clip(cosang, -1, 1))
        edge = planet_ang
        disc = np.clip((edge - ang) / (edge * 0.012), 0, 1)
        if disc.max() > 0:
            # position on the disc as a sphere normal
            r = np.clip(ang / edge, 0, 1)
            local = d - cosang[..., None] * planet_dir
            ln = np.linalg.norm(local, axis=-1, keepdims=True)
            local = local / np.maximum(ln, 1e-6)
            nrm = local * r[..., None] + planet_dir * np.sqrt(np.clip(1 - r ** 2, 0, 1))[..., None]
            lit = np.clip(nrm @ star_light, 0, 1)
            surf = fbm(nrm * 5.0, seed + 6, 6)
            bands = 0.5 + 0.5 * np.sin(nrm[..., 1] * 14.0 + surf * 4.0)
            base = (np.array([0.55, 0.36, 0.26], np.float32) * (0.6 + 0.4 * bands)[..., None]
                    + np.array([0.20, 0.12, 0.10], np.float32) * (surf - 0.5)[..., None] * 2.0)
            shade = (0.035 + 0.95 * lit ** 0.8)[..., None]
            planet = base * shade
            col = col * (1 - disc[..., None]) + planet * disc[..., None]
        # atmosphere rim just outside the disc
        rim = np.exp(-np.clip(ang - edge, 0, None) / (edge * 0.05)) * (ang > edge)
        rim *= np.clip((d @ star_light) * 0.5 + 0.55, 0.15, 1)
        col += (rim * 0.5)[..., None] * np.array([0.35, 0.5, 0.9], np.float32)

        img[y0:y0 + chunk] = col
        behind[y0:y0 + chunk] = disc
        print(f"nebula rows {y0}/{height}", end="\r", file=sys.stderr)
    print(file=sys.stderr)

    # stars: random points on the sphere, denser along the band, splatted and softened
    n_stars = int(width * 9)
    s = rng.normal(size=(n_stars * 4, 3)).astype(np.float32)
    s /= np.linalg.norm(s, axis=1, keepdims=True)
    keep = rng.random(len(s)) < (0.25 + 0.75 * np.exp(-((s @ band_normal) / 0.4) ** 2))
    s = s[keep][:n_stars]
    mag = rng.random(len(s)) ** 5.0   # most are faint
    temp = rng.random(len(s))
    tint = (np.array([0.65, 0.78, 1.0], np.float32)[None] * (1 - temp)[:, None]
            + np.array([1.0, 0.82, 0.60], np.float32)[None] * temp[:, None])
    tint = tint * 0.55 + 0.45   # keep most of them near white
    px = ((np.arctan2(s[:, 0], s[:, 2]) / (2 * np.pi) + 0.5) * width).astype(np.int64) % width
    py = (np.arccos(np.clip(s[:, 1], -1, 1)) / np.pi * height).astype(np.int64).clip(0, height - 1)
    stars = np.zeros_like(img)
    for i in range(len(s)):
        stars[py[i], px[i]] += tint[i] * (0.18 + 1.6 * mag[i])
    soft = cv2.GaussianBlur(stars, (0, 0), 0.75)
    keep_sky = (1.0 - cv2.GaussianBlur(behind, (0, 0), 1.5))[..., None]
    img += soft * 1.5 * keep_sky
    # a few big bright stars with a halo
    big = rng.choice(len(s), size=90, replace=False)
    halo = np.zeros_like(img)
    for i in big:
        halo[py[i], px[i]] += tint[i] * (2.5 + 3.0 * rng.random())
    img += (cv2.GaussianBlur(halo, (0, 0), 1.6) * 7.0 + cv2.GaussianBlur(halo, (0, 0), 7.0) * 2.5) * keep_sky

    # to display values: a soft shoulder, then dither against banding
    img = img / (1.0 + img * 0.55)
    img = np.clip(img, 0, 1) ** (1 / 1.15)
    img += (rng.random(img.shape, dtype=np.float32) - 0.5) / 255.0
    return (np.clip(img, 0, 1) * 255.0 + 0.5).astype(np.uint8)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--width", type=int, default=4096)
    ap.add_argument("--seed", type=int, default=7)
    a = ap.parse_args()
    rgb = build(a.width, a.seed)
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    cv2.imwrite(a.out, rgb[..., ::-1])
    print("wrote", a.out, rgb.shape[1], "x", rgb.shape[0], os.path.getsize(a.out) // 1024, "KB")


if __name__ == "__main__":
    main()
