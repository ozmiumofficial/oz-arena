"""python segment.py bake_dir uv.npz K : chart coverage mask, miss fill, k-means material clusters (preview + npz)"""
import sys, os
import numpy as np, cv2
bd, uvp, K = sys.argv[1], sys.argv[2], int(sys.argv[3])
base = cv2.imread(os.path.join(bd, "bake_base.png"), cv2.IMREAD_COLOR)[:, :, ::-1].copy()   # RGB
S = base.shape[0]
uv = np.load(uvp)["uv"]
cover = np.zeros((S, S), np.uint8)
pts = np.round(np.stack([uv[..., 0] * S - 0.5, (1 - uv[..., 1]) * S - 0.5], -1)).astype(np.int32)
cv2.fillPoly(cover, list(pts), 1)
# a texel bake ray missed stays exactly black
miss = (cover == 1) & (base.max(2) == 0)
print("coverage %.3f  misses %.4f of covered" % (cover.mean(), miss.sum() / cover.sum()))


def fill(img, known, iters=64):
    """push known texels outward (and into misses): repeated masked 3x3 averaging"""
    img = img.astype(np.float32).copy()
    k = known.astype(np.float32)
    for _ in range(iters):
        if k.min() > 0:
            break
        s = cv2.blur(img * k[..., None], (3, 3))
        w = cv2.blur(k, (3, 3))
        grow = (k == 0) & (w > 0)
        img[grow] = s[grow] / w[grow][:, None]
        k[grow] = 1
    return img


known = (cover == 1) & ~miss
base_f = fill(base, known, 256)
np.savez_compressed(os.path.join(bd, "seg_in.npz"), base=base_f.astype(np.uint8), cover=cover, known=known)
lab = cv2.cvtColor(base_f.astype(np.uint8), cv2.COLOR_RGB2LAB).astype(np.float32)
samp = lab[known][np.random.default_rng(1).choice(known.sum(), 300000, replace=False)]
crit = (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 50, 0.2)
_, lab_s, centers = cv2.kmeans(samp, K, None, crit, 4, cv2.KMEANS_PP_CENTERS)
order = np.argsort(centers[:, 0])
centers = centers[order]
d = ((lab.reshape(-1, 1, 3) - centers[None]) ** 2).sum(-1)
label = d.argmin(1).reshape(S, S).astype(np.uint8)
share = np.bincount(label[known], minlength=K) / known.sum()
for i, c in enumerate(centers):
    rgb = cv2.cvtColor(c.reshape(1, 1, 3).astype(np.uint8), cv2.COLOR_LAB2RGB)[0, 0]
    print("cluster %d L %.0f a %.0f b %.0f  rgb %s  share %.3f" % (i, c[0] * 100 / 255, c[1] - 128, c[2] - 128, rgb, share[i]))
np.savez_compressed(os.path.join(bd, "seg.npz"), label=label, centers=centers)
pal = np.array([cv2.cvtColor(c.reshape(1, 1, 3).astype(np.uint8), cv2.COLOR_LAB2RGB)[0, 0] for c in centers])
vis = pal[label]
vis[cover == 0] = 255
cv2.imwrite(os.path.join(bd, "seg_preview.jpg"), cv2.resize(vis[:, :, ::-1], (1400, 1400), interpolation=cv2.INTER_NEAREST))
cv2.imwrite(os.path.join(bd, "base_filled_preview.jpg"), cv2.resize(base_f.astype(np.uint8)[:, :, ::-1], (1400, 1400)))
