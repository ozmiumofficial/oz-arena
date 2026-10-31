"""BC7 and BC5 DDS through the GPU driver's own texture compressor (OpenGL, via the engine's SDL2).

    enc = Encoder()                    # a hidden window and an OpenGL 3.3 context
    enc.write_dds(path, rgba, "bc7")   # rgba: HxWx4 uint8; writes the full mip chain

Uploading an uncompressed image with a BPTC or RGTC internal format makes the driver compress
it; glGetCompressedTexImage reads the blocks back. NVIDIA's BC7 measured 46-50 dB PSNR on the
world library (DXT1: 36-39 dB) at about 0.1 s for 1024x1024 with mips. Mip levels are made
here (area filter from the full image), not by glGenerateMipmap.

Images are resized with straight alpha (resize below): color and alpha filtered apart, as id's
renderer and glGenerateMipmap make mip levels. Pillow resizes RGBA premultiplied, which turns color
under zero alpha black: id's textures whose alpha is no opacity (an empty channel on opaque walls,
as q3dm1's skull_door_*, or a blend mask) lost their color below the first level.
"""
import os, ctypes, struct
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SDL2 = os.path.join(os.path.dirname(HERE), "build", "engine", "SDL2.dll")

GL_TEXTURE_2D = 0x0DE1
GL_RGBA, GL_UNSIGNED_BYTE = 0x1908, 0x1401
GL_TEXTURE_COMPRESSED_IMAGE_SIZE = 0x86A0
FORMATS = {   # name -> (GL internal format, DXGI format)
    "bc7": (0x8E8C, 98),     # GL_COMPRESSED_RGBA_BPTC_UNORM, DXGI_FORMAT_BC7_UNORM
    "bc5": (0x8DBD, 83),     # GL_COMPRESSED_RG_RGTC2, DXGI_FORMAT_BC5_UNORM
}


class Encoder:
    def __init__(self, sdl_path=SDL2):
        sdl = self.sdl = ctypes.CDLL(sdl_path)
        sdl.SDL_CreateWindow.restype = ctypes.c_void_p
        sdl.SDL_GL_CreateContext.restype = ctypes.c_void_p
        sdl.SDL_GL_CreateContext.argtypes = [ctypes.c_void_p]
        sdl.SDL_GL_GetProcAddress.restype = ctypes.c_void_p
        sdl.SDL_GL_GetProcAddress.argtypes = [ctypes.c_char_p]
        if sdl.SDL_Init(0x20) != 0:   # SDL_INIT_VIDEO
            raise RuntimeError("SDL_Init failed")
        sdl.SDL_GL_SetAttribute(17, 3)   # SDL_GL_CONTEXT_MAJOR_VERSION
        sdl.SDL_GL_SetAttribute(18, 3)   # SDL_GL_CONTEXT_MINOR_VERSION
        self.win = sdl.SDL_CreateWindow(b"gpu_bc", 0, 0, 16, 16, 0x2 | 0x8)   # OPENGL | HIDDEN
        self.ctx = sdl.SDL_GL_CreateContext(self.win)
        if not self.ctx:
            raise RuntimeError("no OpenGL context")
        u, i, p = ctypes.c_uint, ctypes.c_int, ctypes.c_void_p

        def gl(name, res, *args):
            return ctypes.CFUNCTYPE(res, *args)(sdl.SDL_GL_GetProcAddress(name.encode()))
        self.glTexImage2D = gl("glTexImage2D", None, u, i, i, i, i, i, u, u, p)
        self.glGetTexLevelParameteriv = gl("glGetTexLevelParameteriv", None, u, i, u, ctypes.POINTER(i))
        self.glGetCompressedTexImage = gl("glGetCompressedTexImage", None, u, i, p)
        self.glGetError = gl("glGetError", u)
        self.glPixelStorei = gl("glPixelStorei", None, u, i)
        gen, bind = gl("glGenTextures", None, i, ctypes.POINTER(u)), gl("glBindTexture", None, u, u)
        tex = u()
        gen(1, ctypes.byref(tex))
        bind(GL_TEXTURE_2D, tex.value)
        self.glPixelStorei(0x0CF5, 1)   # GL_UNPACK_ALIGNMENT
        self.glPixelStorei(0x0D05, 1)   # GL_PACK_ALIGNMENT

    def compress(self, rgba, fmt):
        """One level (HxWx4 uint8) -> its compressed blocks."""
        h, w = rgba.shape[:2]
        rgba = np.ascontiguousarray(rgba)
        self.glTexImage2D(GL_TEXTURE_2D, 0, FORMATS[fmt][0], w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.ctypes.data)
        size = ctypes.c_int()
        self.glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED_IMAGE_SIZE, ctypes.byref(size))
        buf = ctypes.create_string_buffer(size.value)
        self.glGetCompressedTexImage(GL_TEXTURE_2D, 0, buf)
        err = self.glGetError()
        if err:
            raise RuntimeError("OpenGL error 0x%x compressing %dx%d %s" % (err, w, h, fmt))
        return buf.raw

    def write_dds(self, path, rgba, fmt):
        """The full mip chain, each level area-filtered from the full image, as a DX10 DDS."""
        h, w = rgba.shape[:2]
        levels, lw, lh = [], w, h
        while True:
            lvl = rgba if (lw, lh) == (w, h) else resize(rgba, (lw, lh), Image.BOX)
            levels.append(self.compress(lvl, fmt))
            if lw == 1 and lh == 1:
                break
            lw, lh = max(1, lw // 2), max(1, lh // 2)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(dds_header(w, h, len(levels), len(levels[0]), FORMATS[fmt][1]))
            for data in levels:
                f.write(data)


def resize(rgba, size, resample):
    """HxWx4 uint8 -> (w, h), color and alpha filtered apart (straight alpha)."""
    rgb = Image.fromarray(np.ascontiguousarray(rgba[..., :3]), "RGB").resize(size, resample)
    alpha = Image.fromarray(np.ascontiguousarray(rgba[..., 3]), "L").resize(size, resample)
    return np.dstack([np.asarray(rgb), np.asarray(alpha)])


def dds_header(w, h, mips, linear_size, dxgi):
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000   # CAPS HEIGHT WIDTH PIXELFORMAT MIPMAPCOUNT LINEARSIZE
    pf = struct.pack("<II4s5I", 32, 0x4, b"DX10", 0, 0, 0, 0, 0)   # DDPF_FOURCC
    caps = 0x1000 | 0x400000 | 0x8                               # TEXTURE MIPMAP COMPLEX
    hdr = struct.pack("<7I44x", 124, flags, h, w, linear_size, 0, mips) + pf + struct.pack("<5I", caps, 0, 0, 0, 0)
    dx10 = struct.pack("<5I", dxgi, 3, 0, 1, 0)   # TEXTURE2D, array size 1
    return b"DDS " + hdr + dx10
