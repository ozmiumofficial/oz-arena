"""Real-ESRGAN x4plus on the GPU, for tools/make_textures.py: 4x upscales of id's images.

    net = Net()                         # loads build/sr_models/realesrgan_x4plus/RealESRGAN_x4plus.pth
    big = net.up4(bgr)                  # HxWx3 uint8 BGR -> 4H x 4W x 3 float32 (0..255)

The network is basicsr's RRDBNet (23 blocks, 64 features, BSD-3, as Real-ESRGAN ships it), written out
here so nothing gets installed; the weights load strictly, so a mismatch fails. The .pth is a pickle:
it's read through an unpickler that only rebuilds tensors and dicts. Large images run in tiles, each
with its neighbors' texels around it (reflected at the image's edges), so 8 GB of VRAM is enough.

The weights (67,040,989 bytes, sha256 4fa0d389...) come from github.com/xinntao/Real-ESRGAN, release
v0.1.0, into the git-ignored build/sr_models/ (Ozmium approved the download, 2026-10-30).
"""
import os, pickle
import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

HERE = os.path.dirname(os.path.abspath(__file__))
WEIGHTS = os.path.join(os.path.dirname(HERE), "build", "sr_models", "realesrgan_x4plus", "RealESRGAN_x4plus.pth")


class RDB(nn.Module):
    def __init__(self, nf=64, gc=32):
        super().__init__()
        self.conv1 = nn.Conv2d(nf, gc, 3, 1, 1)
        self.conv2 = nn.Conv2d(nf + gc, gc, 3, 1, 1)
        self.conv3 = nn.Conv2d(nf + 2 * gc, gc, 3, 1, 1)
        self.conv4 = nn.Conv2d(nf + 3 * gc, gc, 3, 1, 1)
        self.conv5 = nn.Conv2d(nf + 4 * gc, nf, 3, 1, 1)
        self.lrelu = nn.LeakyReLU(0.2, inplace=True)

    def forward(self, x):
        x1 = self.lrelu(self.conv1(x))
        x2 = self.lrelu(self.conv2(torch.cat((x, x1), 1)))
        x3 = self.lrelu(self.conv3(torch.cat((x, x1, x2), 1)))
        x4 = self.lrelu(self.conv4(torch.cat((x, x1, x2, x3), 1)))
        return self.conv5(torch.cat((x, x1, x2, x3, x4), 1)) * 0.2 + x


class RRDB(nn.Module):
    def __init__(self, nf=64, gc=32):
        super().__init__()
        self.rdb1, self.rdb2, self.rdb3 = RDB(nf, gc), RDB(nf, gc), RDB(nf, gc)

    def forward(self, x):
        return self.rdb3(self.rdb2(self.rdb1(x))) * 0.2 + x


class RRDBNet(nn.Module):
    def __init__(self, nf=64, nb=23, gc=32):
        super().__init__()
        self.conv_first = nn.Conv2d(3, nf, 3, 1, 1)
        self.body = nn.Sequential(*[RRDB(nf, gc) for _ in range(nb)])
        self.conv_body = nn.Conv2d(nf, nf, 3, 1, 1)
        self.conv_up1 = nn.Conv2d(nf, nf, 3, 1, 1)
        self.conv_up2 = nn.Conv2d(nf, nf, 3, 1, 1)
        self.conv_hr = nn.Conv2d(nf, nf, 3, 1, 1)
        self.conv_last = nn.Conv2d(nf, 3, 3, 1, 1)
        self.lrelu = nn.LeakyReLU(0.2, inplace=True)

    def forward(self, x):
        feat = self.conv_first(x)
        feat = feat + self.conv_body(self.body(feat))
        feat = self.lrelu(self.conv_up1(F.interpolate(feat, scale_factor=2, mode="nearest")))
        feat = self.lrelu(self.conv_up2(F.interpolate(feat, scale_factor=2, mode="nearest")))
        return self.conv_last(self.lrelu(self.conv_hr(feat)))


class TensorsOnly(pickle.Unpickler):
    """torch.load's pickle, allowed to build only tensors, storages and dicts."""
    ALLOWED = {("collections", "OrderedDict"), ("torch._utils", "_rebuild_tensor_v2"),
               ("torch._utils", "_rebuild_parameter"), ("torch", "FloatStorage"), ("torch", "HalfStorage")}

    def find_class(self, module, name):
        if (module, name) not in self.ALLOWED:
            raise pickle.UnpicklingError("refused %s.%s" % (module, name))
        return super().find_class(module, name)


class SafePickle:
    Unpickler = TensorsOnly
    load = staticmethod(lambda f, **kw: TensorsOnly(f, **kw).load())


class Net:
    def __init__(self, weights=WEIGHTS):
        if not os.path.isfile(weights):
            raise SystemExit("no %s: the Real-ESRGAN x4plus weights (see tools/sr_model.py)" % weights)
        sd = torch.load(weights, map_location="cpu", pickle_module=SafePickle)
        sd = sd.get("params_ema", sd.get("params", sd))
        self.net = RRDBNet()
        self.net.load_state_dict(sd, strict=True)
        self.net = self.net.eval().cuda()

    @torch.no_grad()
    def _run(self, bgr):
        x = torch.from_numpy(np.ascontiguousarray(bgr[..., ::-1]).astype(np.float32) / 255.0).permute(2, 0, 1)[None].cuda()
        y = self.net(x).clamp(0, 1)[0].permute(1, 2, 0).cpu().numpy()[..., ::-1]
        return y * 255.0

    def up4(self, bgr, tile=192, pad=16):
        """HxWx3 uint8 BGR -> (4H, 4W, 3) float32, in tiles of `tile` texels read with `pad` texels around."""
        h, w = bgr.shape[:2]
        src = np.pad(bgr, ((pad, pad), (pad, pad), (0, 0)), mode="reflect")
        out = np.zeros((h * 4, w * 4, 3), np.float32)
        for y in range(0, h, tile):
            for x in range(0, w, tile):
                th, tw = min(tile, h - y), min(tile, w - x)
                part = self._run(src[y:y + th + 2 * pad, x:x + tw + 2 * pad])
                out[4 * y:4 * (y + th), 4 * x:4 * (x + tw)] = part[4 * pad:4 * (pad + th), 4 * pad:4 * (pad + tw)]
        return out
