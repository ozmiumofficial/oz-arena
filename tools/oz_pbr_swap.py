"""Oz Arena PBR texture swap: put your own PBR maps into the game's world paks, over Oz's remastered ones.

    python oz_pbr_swap.py                       the window
    python oz_pbr_swap.py --install <OzArena folder> --textures <folder> [--apply] [--restore]

Needs Python 3 with numpy and Pillow (pip install numpy pillow). Runs on an installed Oz Arena (the
release) or on the build (build/engine): the folder that holds OzArena.exe, SDL2.dll and oz/.

Name the images after id's textures. Any of these find textures/gothic_block/blocks18c:
    blocks18c_albedo.png                         (the name alone, when only one folder has it)
    gothic_block__blocks18c_albedo.png           (oz_library's names)
    textures/gothic_block/blocks18c_albedo.png   (id's folders, in the textures folder)
Suffixes (any image type Pillow reads: png, tga, jpg, tif, bmp, webp; 16-bit grey works):
    albedo     _albedo _basecolor _base_color _color _diffuse _diff _col, or none
    normal     _normal _nrm _nor _norm _normalgl _n      (OpenGL, green up: as Oz's library)
               _normaldx _normal_dx _normal_directx      (DirectX, green down)
    ORM        _orm _arm                                 (R occlusion, G roughness, B metal)
    or apart   _roughness _rough   _metallic _metalness _metal   _ao _occlusion _ambientocclusion
    height     _height _displacement _disp _bump         (white is up; used where the game has relief maps)
    specular   _s                                        (the game's own layout: R roughness, G metal, B AO)
A map left out keeps what the game has (a missing roughness keeps the game's roughness, and so on).

Each image is converted as tools/make_pk3s.py converts Oz's library: BC7 albedo with the full mip chain,
the normal map BC5 with its green flipped for the renderer, the specular map BC7 (R roughness at least
0.45, G metal x0.35, B occlusion: the game's matte rules, which can be turned off), by the GPU driver
through the game's own SDL2. An albedo without alpha takes the game's alpha where the game's has any
(grates, fences). Only maps the game already has for a texture are replaced: a texture without a normal
map in the game takes no normal map (its shader doesn't ask for one).

The paks are rewritten whole (a temp file beside them, then swapped in), so close Oz Arena first and
keep free space for the biggest pak. The first time, each pak changed is kept as <pak>.orig;
Restore puts them back. A changed pak can't join servers that check Oz's world paks; this is for
testing offline (Skirmish).
"""
import os, io, sys, ctypes, struct, shutil, zipfile, threading, argparse, traceback
import numpy as np
from PIL import Image

ROUGHNESS_FLOOR = 0.45      # tools/make_pk3s.py
METAL_SCALE = 0.35
IMAGE_EXT = (".png", ".tga", ".jpg", ".jpeg", ".tif", ".tiff", ".bmp", ".webp")

# suffix -> role; longest first when matching
SUFFIXES = {
    "albedo": "albedo", "basecolor": "albedo", "base_color": "albedo", "color": "albedo", "colour": "albedo",
    "diffuse": "albedo", "diff": "albedo", "col": "albedo",
    "normal": "normal", "normalgl": "normal", "normal_gl": "normal", "normal_opengl": "normal", "nrm": "normal",
    "nor": "normal", "norm": "normal", "n": "normal",
    "normaldx": "normaldx", "normal_dx": "normaldx", "normal_directx": "normaldx",
    "orm": "orm", "arm": "orm",
    "roughness": "rough", "rough": "rough",
    "metallic": "metal", "metalness": "metal", "metal": "metal",
    "ao": "ao", "occlusion": "ao", "ambientocclusion": "ao", "ambient_occlusion": "ao",
    "height": "height", "displacement": "height", "disp": "height", "bump": "height",
    "s": "spec",
}
ROLE_FILES = ("_nh", "_n", "_s")   # the game's own map names beside a texture's .dds


# ------------------------------------------------------------------ the GPU encoder (tools/gpu_bc.py)

class Encoder:
    """BC7 and BC5 through the GPU driver's own compressor: a hidden SDL2 window and an OpenGL context."""
    FORMATS = {"bc7": (0x8E8C, 98), "bc5": (0x8DBD, 83)}

    def __init__(self, sdl_path):
        sdl = self.sdl = ctypes.CDLL(sdl_path)
        sdl.SDL_CreateWindow.restype = ctypes.c_void_p
        sdl.SDL_GL_CreateContext.restype = ctypes.c_void_p
        sdl.SDL_GL_CreateContext.argtypes = [ctypes.c_void_p]
        sdl.SDL_GL_GetProcAddress.restype = ctypes.c_void_p
        sdl.SDL_GL_GetProcAddress.argtypes = [ctypes.c_char_p]
        if sdl.SDL_Init(0x20) != 0:
            raise RuntimeError("SDL_Init failed")
        sdl.SDL_GL_SetAttribute(17, 3)
        sdl.SDL_GL_SetAttribute(18, 3)
        self.win = sdl.SDL_CreateWindow(b"oz_pbr_swap", 0, 0, 16, 16, 0x2 | 0x8)
        self.ctx = sdl.SDL_GL_CreateContext(self.win)
        if not self.ctx:
            raise RuntimeError("no OpenGL 3.3 context (graphics driver?)")
        u, i, p = ctypes.c_uint, ctypes.c_int, ctypes.c_void_p

        def gl(name, res, *args):
            return ctypes.CFUNCTYPE(res, *args)(sdl.SDL_GL_GetProcAddress(name.encode()))
        self.glTexImage2D = gl("glTexImage2D", None, u, i, i, i, i, i, u, u, p)
        self.glGetTexLevelParameteriv = gl("glGetTexLevelParameteriv", None, u, i, u, ctypes.POINTER(i))
        self.glGetCompressedTexImage = gl("glGetCompressedTexImage", None, u, i, p)
        self.glGetError = gl("glGetError", u)
        pixelstore = gl("glPixelStorei", None, u, i)
        gen, bind = gl("glGenTextures", None, i, ctypes.POINTER(u)), gl("glBindTexture", None, u, u)
        tex = u()
        gen(1, ctypes.byref(tex))
        bind(0x0DE1, tex.value)
        pixelstore(0x0CF5, 1)
        pixelstore(0x0D05, 1)

    def compress(self, rgba, fmt):
        h, w = rgba.shape[:2]
        rgba = np.ascontiguousarray(rgba)
        self.glTexImage2D(0x0DE1, 0, self.FORMATS[fmt][0], w, h, 0, 0x1908, 0x1401, rgba.ctypes.data)
        size = ctypes.c_int()
        self.glGetTexLevelParameteriv(0x0DE1, 0, 0x86A0, ctypes.byref(size))
        buf = ctypes.create_string_buffer(size.value)
        self.glGetCompressedTexImage(0x0DE1, 0, buf)
        err = self.glGetError()
        if err:
            raise RuntimeError("OpenGL error 0x%x compressing %dx%d %s" % (err, w, h, fmt))
        return buf.raw

    def dds(self, rgba, fmt):
        """The full mip chain, each level area-filtered from the full image, as DX10 DDS bytes."""
        h, w = rgba.shape[:2]
        levels, lw, lh = [], w, h
        while True:
            lvl = rgba if (lw, lh) == (w, h) else resize(rgba, (lw, lh), Image.BOX)
            levels.append(self.compress(lvl, fmt))
            if lw == 1 and lh == 1:
                break
            lw, lh = max(1, lw // 2), max(1, lh // 2)
        flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000
        pf = struct.pack("<II4s5I", 32, 0x4, b"DX10", 0, 0, 0, 0, 0)
        caps = 0x1000 | 0x400000 | 0x8
        hdr = struct.pack("<7I44x", 124, flags, h, w, len(levels[0]), 0, len(levels)) + pf + \
            struct.pack("<5I", caps, 0, 0, 0, 0)
        return b"DDS " + hdr + struct.pack("<5I", self.FORMATS[fmt][1], 3, 0, 1, 0) + b"".join(levels)


def resize(rgba, size, resample):
    """HxWx4 uint8 -> (w, h), color and alpha filtered apart (straight alpha)."""
    rgb = Image.fromarray(np.ascontiguousarray(rgba[..., :3]), "RGB").resize(size, resample)
    alpha = Image.fromarray(np.ascontiguousarray(rgba[..., 3]), "L").resize(size, resample)
    return np.dstack([np.asarray(rgb), np.asarray(alpha)])


# ------------------------------------------------------------------ images

def load_rgba(path):
    im = Image.open(path)
    if im.mode in ("I;16", "I;16B", "I;16L", "I", "F"):
        g = gray8(path)
        return np.dstack([g, g, g, np.full_like(g, 255)])
    return np.asarray(im.convert("RGBA")).copy()


def gray8(path):
    """One channel as uint8; 16-bit and float greys scaled down, not clipped."""
    im = Image.open(path)
    if im.mode in ("I;16", "I;16B", "I;16L", "I", "F"):
        a = np.asarray(im).astype(np.float64)
        top = 65535.0 if a.max() > 255 else 255.0
        if im.mode == "F" and a.max() <= 1.0:
            top = 1.0
        return np.clip(a / top * 255.0 + 0.5, 0, 255).astype(np.uint8)
    return np.asarray(im.convert("L")).copy()


def fit(a, size):
    """A channel or image to size (w, h)."""
    if (a.shape[1], a.shape[0]) == size:
        return a
    if a.ndim == 2:
        return np.asarray(Image.fromarray(a, "L").resize(size, Image.BILINEAR))
    return resize(a, size, Image.BILINEAR)


def target_size(w, h, max_size):
    s = min(1.0, max_size / float(max(w, h)))
    return max(4, int(w * s) // 4 * 4), max(4, int(h * s) // 4 * 4)


# ------------------------------------------------------------------ the install

def find_install(start=None):
    """The folder with oz/pak_oz_world.pk3: given, beside this script, the build's, or the current one."""
    # beside the exe when frozen (PyInstaller unpacks the script into a temp folder)
    here = os.path.dirname(os.path.abspath(sys.executable if getattr(sys, "frozen", False) else __file__))
    for d in [start, here, os.path.dirname(here), os.path.join(os.path.dirname(here), "build", "engine"), os.getcwd(),
              setup_install()]:
        if d and os.path.isfile(os.path.join(d, "oz", "pak_oz_world.pk3")):
            return os.path.abspath(d)
    return start or ""


def setup_install():
    """Where OzArenaSetup.exe installed the game: its uninstall record's InstallLocation."""
    if os.name != "nt":
        return None
    import winreg
    for hive in (winreg.HKEY_CURRENT_USER, winreg.HKEY_LOCAL_MACHINE):
        for key in ("OzArena", "OzArena_is1"):
            try:
                with winreg.OpenKey(hive, "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + key, 0,
                                    winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as k:
                    d = winreg.QueryValueEx(k, "InstallLocation")[0]
                    if os.path.isfile(os.path.join(d, "OzArena.exe")):
                        return d
            except OSError:
                pass
    return None


def sdl_path(install):
    for n in ("SDL2.dll", "libSDL2-2.0.so.0", "libSDL2.so"):
        p = os.path.join(install, n)
        if os.path.isfile(p):
            return p
    return "SDL2.dll" if os.name == "nt" else "libSDL2-2.0.so.0"


def world_paks(install):
    d = os.path.join(install, "oz")
    return sorted(os.path.join(d, f) for f in os.listdir(d)
                  if f.lower().startswith("pak_oz_") and f.lower().endswith(".pk3") and f.lower() != "pak_oz_code.pk3")


class Index:
    """Every .dds in the asset paks: texture key (lower, no extension) -> (pak, entry name). The last pak
    wins, as the game loads them."""
    def __init__(self, install):
        self.entries = {}
        for pak in world_paks(install):
            with zipfile.ZipFile(pak) as z:
                for n in z.namelist():
                    if n.lower().endswith(".dds"):
                        self.entries[n[:-4].lower()] = (pak, n)
        # textures: keys that aren't another texture's _n/_nh/_s
        self.textures = set()
        for k in self.entries:
            if not any(k.endswith(r) and k[:-len(r)] in self.entries for r in ROLE_FILES):
                self.textures.add(k)
        self.by_name = {}
        for k in self.textures:
            self.by_name.setdefault(k.rsplit("/", 1)[-1], []).append(k)

    def resolve(self, parts):
        """Path parts (folders + name, '__' already split) -> (texture key, None) or (None, why)."""
        parts = [p.lower() for p in parts if p]
        name = parts[-1]
        hits = self.by_name.get(name, [])
        for n in range(len(parts), 0, -1):
            tail = "/".join(parts[-n:])
            m = [k for k in hits if k == tail or k.endswith("/" + tail)]
            if len(m) == 1:
                return m[0], None
            if len(m) > 1 and n == len(parts):
                return None, "%d textures are called %s: put it in its folder (%s)" % (
                    len(m), name, ", ".join(sorted(k.rsplit("/", 1)[0] for k in m)))
        return None, None

    def read(self, key):
        pak, n = self.entries[key]
        with zipfile.ZipFile(pak) as z:
            return z.read(n)


def decode_dds(data):
    """The game's DDS -> RGBA uint8 (Pillow decodes BC5 and BC7), or None."""
    try:
        im = Image.open(io.BytesIO(data))
        im.load()
        return np.asarray(im.convert("RGBA")).copy()
    except Exception:
        return None


# ------------------------------------------------------------------ scanning the textures folder

def scan(folder, index):
    """-> {texture key: {role: path}}, [(file, problem)]"""
    found, problems = {}, []
    for root, _, files in os.walk(folder):
        for f in sorted(files):
            stem, ext = os.path.splitext(f)
            if ext.lower() not in IMAGE_EXT:
                continue
            path = os.path.join(root, f)
            rel_dirs = [d for d in os.path.relpath(root, folder).replace("\\", "/").split("/") if d not in (".", "")]
            # the suffix: the longest one that leaves a texture the game has; no suffix = albedo
            choices = []
            low = stem.lower()
            for suf in sorted(SUFFIXES, key=len, reverse=True):
                for sep in ("_", "-", " "):
                    if low.endswith(sep + suf) and len(low) > len(suf) + 1:
                        choices.append((stem[:-len(suf) - 1], SUFFIXES[suf]))
            choices.append((stem, "albedo"))
            hit, why = None, None
            for base, role in choices:
                parts = rel_dirs + base.split("__")
                key, w = index.resolve(parts)
                if key:
                    hit = (key, role)
                    break
                why = why or w
            if not hit:
                problems.append((os.path.relpath(path, folder), why or "no texture of that name in the game's paks"))
                continue
            key, role = hit
            slot = found.setdefault(key, {})
            if role in slot:
                problems.append((os.path.relpath(path, folder), "a second %s map for %s (kept %s)"
                                 % (role, key, os.path.basename(slot[role]))))
                continue
            slot[role] = path
    return found, problems


def plan(found, index):
    """-> [(texture key, [(entry key, what, sources)], [notes])] : what each texture's files replace."""
    out = []
    for key in sorted(found):
        src, todo, notes = found[key], [], []
        if "albedo" in src:
            todo.append((key, "albedo", src))
        normal = src.get("normal") or src.get("normaldx")
        if "normal" in src and "normaldx" in src:
            notes.append("both an OpenGL and a DirectX normal map: the OpenGL one used")
        has = lambda r: key + r in index.entries
        if (normal or "height" in src) and has("_nh"):
            todo.append((key + "_nh", "normalheight", src))
            if not normal:
                notes.append("height only: the game's normal map kept")
            elif "height" not in src:
                notes.append("no height map: the game's relief kept")
        elif normal and has("_n"):
            todo.append((key + "_n", "normal", src))
            if "height" in src:
                notes.append("height map unused: this version of the game has no relief maps")
        elif normal or "height" in src:
            notes.append("normal/height unused: the game has no normal map for this texture")
        if any(r in src for r in ("orm", "rough", "metal", "ao", "spec")):
            if has("_s"):
                todo.append((key + "_s", "spec", src))
            else:
                notes.append("roughness/metal/AO unused: the game has no specular map for this texture")
        if not todo:
            notes.append("nothing to replace")
        out.append((key, todo, notes))
    return out


# ------------------------------------------------------------------ converting

def convert(enc, index, entry, what, src, opt):
    """One pak entry -> its new DDS bytes."""
    max_size = opt["max_size"]
    if what == "albedo":
        px = load_rgba(src["albedo"])
        w, h = target_size(px.shape[1], px.shape[0], max_size)
        px = fit(px, (w, h))
        if px[..., 3].min() == 255:      # no alpha of its own: the game's, where it has any
            old = decode_dds(index.read(entry))
            if old is not None and old[..., 3].min() < 250:
                px[..., 3] = fit(np.ascontiguousarray(old[..., 3]), (w, h))
        return enc.dds(px, "bc7")

    old = None
    def game():
        nonlocal old
        if old is None:
            old = decode_dds(index.read(entry))
            if old is None:
                raise RuntimeError("can't read the game's %s.dds" % entry)
        return old

    if what in ("normal", "normalheight"):
        npath = src.get("normal") or src.get("normaldx")
        if npath:
            nrm = load_rgba(npath)
            if not src.get("normal"):               # DirectX: already green down, as the renderer
                pass
            else:
                nrm[..., 1] = 255 - nrm[..., 1]     # OpenGL green up -> the renderer's green down
            w, h = target_size(nrm.shape[1], nrm.shape[0], max_size)
            nrm = fit(nrm, (w, h))
        else:
            g = game()
            w, h = g.shape[1], g.shape[0]
            if "height" in src:
                hw, hh = target_size(*Image.open(src["height"]).size, max_size)
                w, h = max(w, hw), max(h, hh)
            nrm = fit(g, (w, h))
        nrm = nrm.copy()
        if what == "normal":
            nrm[..., 2], nrm[..., 3] = 255, 255
            return enc.dds(nrm, "bc5")
        if "height" in src:
            nrm[..., 3] = fit(gray8(src["height"]), (w, h))
        else:
            nrm[..., 3] = fit(np.ascontiguousarray(game()[..., 3]), (w, h))
        return enc.dds(nrm, "bc7")

    # spec: R roughness, G metal, B occlusion
    if "spec" in src:
        px = load_rgba(src["spec"])
        w, h = target_size(px.shape[1], px.shape[0], max_size)
        px = fit(px, (w, h)).copy()
        px[..., 3] = 255
        return enc.dds(px, "bc7")
    chans = {}
    if "orm" in src:
        orm = load_rgba(src["orm"])
        chans = {"ao": orm[..., 0], "rough": orm[..., 1], "metal": orm[..., 2]}
    for r in ("rough", "metal", "ao"):
        if r in src:
            chans[r] = gray8(src[r])
    ref = max(chans.values(), key=lambda a: a.shape[0] * a.shape[1])
    w, h = target_size(ref.shape[1], ref.shape[0], max_size)
    out = np.zeros((h, w, 4), np.uint8)
    out[..., 3] = 255
    for i, r in enumerate(("rough", "metal", "ao")):
        if r in chans:
            c = fit(np.ascontiguousarray(chans[r]), (w, h)).astype(np.float64)
            if opt["matte"] and r == "rough":
                c = np.maximum(c, round(ROUGHNESS_FLOOR * 255))
            if opt["matte"] and r == "metal":
                c = c * METAL_SCALE
            out[..., i] = np.clip(c + 0.5, 0, 255).astype(np.uint8)
        else:
            out[..., i] = fit(np.ascontiguousarray(game()[..., i]), (w, h))
    return enc.dds(out, "bc7")


# ------------------------------------------------------------------ writing the paks

def check_unlocked(pak):
    """Oz Arena holds its paks open while it runs; renaming one then fails on Windows."""
    probe = pak + ".lockcheck"
    try:
        os.rename(pak, probe)
        os.rename(probe, pak)
    except OSError:
        raise RuntimeError("%s is in use: close Oz Arena and try again" % os.path.basename(pak))


def rewrite(pak, new, backup, log):
    """pak with its entries in new {entry name: bytes} replaced; a temp file swapped in."""
    tmp = pak + ".tmp"
    with zipfile.ZipFile(pak) as zin, zipfile.ZipFile(tmp, "w", allowZip64=True) as zout:
        infos = zin.infolist()
        for i, info in enumerate(infos):
            data = new[info.filename] if info.filename in new else zin.read(info)
            zi = zipfile.ZipInfo(info.filename, info.date_time)
            zi.compress_type = zipfile.ZIP_STORED if info.filename.lower().endswith(".dds") else info.compress_type
            zi.external_attr = info.external_attr
            zout.writestr(zi, data)
            if i % 200 == 0:
                log("  %s: %d / %d" % (os.path.basename(pak), i, len(infos)))
    orig = pak + ".orig"
    if backup and not os.path.isfile(orig):
        os.replace(pak, orig)      # the file moves: hard links elsewhere keep the old content
    os.replace(tmp, pak)


def restore(install, log):
    n = 0
    for pak in world_paks(install):
        orig = pak + ".orig"
        if os.path.isfile(orig):
            check_unlocked(pak)
            os.replace(orig, pak)
            log("restored " + os.path.basename(pak))
            n += 1
    for f in os.listdir(os.path.join(install, "oz")):
        if f.endswith(".pk3.orig") and not os.path.isfile(os.path.join(install, "oz", f[:-5])):
            os.replace(os.path.join(install, "oz", f), os.path.join(install, "oz", f[:-5]))
            n += 1
    log("restored %d pak(s)" % n if n else "no backups (.orig) to restore")


def apply(install, index, plans, opt, log, progress=lambda done, total: None):
    jobs = [(entry, what, src) for _, todo, _ in plans for entry, what, src in todo]
    if not jobs:
        log("nothing to replace")
        return
    paks = sorted({index.entries[e][0] for e, _, _ in jobs})
    for pak in paks:
        check_unlocked(pak)
    need = max(os.path.getsize(p) for p in paks)
    free = shutil.disk_usage(os.path.dirname(paks[0])).free
    if free < need + (64 << 20):
        raise RuntimeError("needs %.1f GB free beside the paks, has %.1f GB" % (need / 2**30, free / 2**30))
    enc = Encoder(sdl_path(install))
    new = {}
    for i, (entry, what, src) in enumerate(jobs):
        pak, name = index.entries[entry]
        log("converting %s (%s)" % (name, what))
        new.setdefault(pak, {})[name] = convert(enc, index, entry, what, src, opt)
        progress(i + 1, len(jobs) + len(paks))
    for j, pak in enumerate(paks):
        log("writing %s (%d replaced)..." % (os.path.basename(pak), len(new[pak])))
        rewrite(pak, new[pak], opt["backup"], log)
        progress(len(jobs) + j + 1, len(jobs) + len(paks))
    log("done: %d maps replaced in %s. Start Oz Arena to see them." % (len(jobs), ", ".join(os.path.basename(p) for p in paks)))


def describe(plans, problems):
    lines = []
    for key, todo, notes in plans:
        lines.append("%s: %s%s" % (key, ", ".join(os.path.basename(index_name) + ".dds" for index_name, _, _ in todo) or "-",
                                   "  (" + "; ".join(notes) + ")" if notes else ""))
    for f, why in problems:
        lines.append("SKIPPED %s: %s" % (f, why))
    return lines


# ------------------------------------------------------------------ the window

def gui():
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox

    root = tk.Tk()
    root.title("Oz Arena PBR texture swap")
    try:      # Oz's icon, bundled into the exe (engine/tools/make_pbr_swap_exe.py)
        root.iconbitmap(os.path.join(getattr(sys, "_MEIPASS", ""), "oz.ico"))
    except Exception:
        pass
    root.geometry("980x640")
    install = tk.StringVar(value=find_install())
    textures = tk.StringVar()
    backup, matte = tk.BooleanVar(value=True), tk.BooleanVar(value=True)
    max_size = tk.StringVar(value="1024")
    state = {"index": None, "plans": [], "busy": False}

    frm = ttk.Frame(root, padding=10)
    frm.pack(fill="both", expand=True)
    frm.columnconfigure(1, weight=1)

    def row(r, label, var, pick):
        ttk.Label(frm, text=label).grid(row=r, column=0, sticky="w", pady=3)
        ttk.Entry(frm, textvariable=var).grid(row=r, column=1, sticky="ew", padx=6)
        ttk.Button(frm, text="Browse...", command=pick).grid(row=r, column=2)

    row(0, "Oz Arena folder (has OzArena.exe and oz\\)", install,
        lambda: install.set(filedialog.askdirectory(initialdir=install.get() or None) or install.get()))
    row(1, "PBR textures folder", textures,
        lambda: textures.set(filedialog.askdirectory(initialdir=textures.get() or None) or textures.get()))

    opts = ttk.Frame(frm)
    opts.grid(row=2, column=0, columnspan=3, sticky="w", pady=4)
    ttk.Checkbutton(opts, text="Keep the original paks (.orig, first time only)", variable=backup).pack(side="left")
    ttk.Checkbutton(opts, text="Game's matte rules (roughness >= 0.45, metal x0.35)", variable=matte).pack(side="left", padx=12)
    ttk.Label(opts, text="Largest side").pack(side="left")
    ttk.Combobox(opts, textvariable=max_size, values=["512", "1024", "2048", "4096"], width=6,
                 state="readonly").pack(side="left", padx=4)

    cols = ("texture", "replaces", "notes")
    tree = ttk.Treeview(frm, columns=cols, show="headings", height=14)
    for c, w in zip(cols, (300, 280, 360)):
        tree.heading(c, text=c.capitalize())
        tree.column(c, width=w, anchor="w")
    tree.grid(row=3, column=0, columnspan=3, sticky="nsew", pady=6)
    frm.rowconfigure(3, weight=3)
    logbox = tk.Text(frm, height=8, state="disabled")
    logbox.grid(row=4, column=0, columnspan=3, sticky="nsew")
    frm.rowconfigure(4, weight=1)
    bar = ttk.Progressbar(frm, mode="determinate")
    bar.grid(row=5, column=0, columnspan=3, sticky="ew", pady=4)
    btns = ttk.Frame(frm)
    btns.grid(row=6, column=0, columnspan=3, sticky="e")

    # the worker thread never touches the widgets: it queues calls the window runs (tkinter isn't thread-safe)
    import queue
    calls = queue.Queue()

    def later(fn):
        calls.put(fn)

    def pump():
        while True:
            try:
                calls.get_nowait()()
            except queue.Empty:
                break
        root.after(50, pump)
    root.after(50, pump)

    def log(msg):
        def put():
            logbox.configure(state="normal")
            logbox.insert("end", msg + "\n")
            logbox.see("end")
            logbox.configure(state="disabled")
        later(put)

    def progress(done, total):
        later(lambda: bar.configure(maximum=total, value=done))

    def run(work, after=None):
        if state["busy"]:
            return
        state["busy"] = True

        def go():
            try:
                work()
            except Exception as e:
                log("ERROR: %s" % e)
                later(lambda e=e: messagebox.showerror("Oz Arena PBR texture swap", str(e)))
            finally:
                state["busy"] = False
                if after:
                    later(after)
        threading.Thread(target=go, daemon=True).start()

    def do_scan():
        inst, tex = install.get(), textures.get()
        if not os.path.isfile(os.path.join(inst, "oz", "pak_oz_world.pk3")):
            raise RuntimeError("No oz\\pak_oz_world.pk3 in that Oz Arena folder")
        if not os.path.isdir(tex):
            raise RuntimeError("Pick the folder with the PBR textures")
        log("reading the paks' contents...")
        index = Index(inst)
        found, problems = scan(tex, index)
        plans = plan(found, index)
        state.update(index=index, plans=plans)

        def show():
            tree.delete(*tree.get_children())
            for key, todo, notes in plans:
                tree.insert("", "end", values=(key, ", ".join(e.rsplit("/", 1)[-1] + ".dds" for e, _, _ in todo) or "-",
                                               "; ".join(notes)))
            for f, why in problems:
                tree.insert("", "end", values=(f, "SKIPPED", why))
        later(show)
        n = sum(len(t) for _, t, _ in plans)
        log("%d textures, %d maps to replace, %d files skipped" % (len(plans), n, len(problems)))

    def do_apply():
        if not state["plans"]:
            do_scan()
        if not sum(len(t) for _, t, _ in state["plans"]):
            log("nothing to replace")
            return
        apply(install.get(), state["index"], state["plans"],
              dict(max_size=int(max_size.get()), backup=backup.get(), matte=matte.get()), log, progress)
        state["plans"] = []

    def ask_apply():
        if messagebox.askokcancel("Oz Arena PBR texture swap",
                                  "Write the textures into the game's paks?\n\nClose Oz Arena first. "
                                  "Rewriting a pak takes a few minutes and needs free space as big as the pak."):
            run(do_apply)

    def ask_restore():
        if messagebox.askokcancel("Oz Arena PBR texture swap", "Put the original paks (.orig) back?"):
            run(lambda: restore(install.get(), log))

    ttk.Button(btns, text="Restore originals", command=ask_restore).pack(side="left", padx=4)
    ttk.Button(btns, text="Scan", command=lambda: run(do_scan)).pack(side="left", padx=4)
    ttk.Button(btns, text="Write into paks", command=ask_apply).pack(side="left", padx=4)
    log("Pick the folders, Scan to see what each file replaces, then Write into paks.")
    root.mainloop()


def main():
    if len(sys.argv) == 1:
        return gui()
    ap = argparse.ArgumentParser(description="Oz Arena PBR texture swap")
    ap.add_argument("--install", help="the Oz Arena folder (OzArena.exe, oz\\)")
    ap.add_argument("--textures", help="the folder of PBR images")
    ap.add_argument("--apply", action="store_true", help="write them into the paks (else only list)")
    ap.add_argument("--restore", action="store_true", help="put the .orig paks back")
    ap.add_argument("--no-backup", action="store_true")
    ap.add_argument("--no-matte", action="store_true", help="metal and roughness as painted")
    ap.add_argument("--max-size", type=int, default=1024)
    a = ap.parse_args()
    install = find_install(a.install)
    if a.restore:
        return restore(install, print)
    index = Index(install)
    found, problems = scan(a.textures, index)
    plans = plan(found, index)
    for line in describe(plans, problems):
        print(line)
    if a.apply:
        apply(install, index, plans, dict(max_size=a.max_size, backup=not a.no_backup, matte=not a.no_matte), print)


if __name__ == "__main__":
    if sys.stdout is None:          # the windowed exe has no console: prints go nowhere
        sys.stdout = sys.stderr = open(os.devnull, "w")
    try:
        main()
    except Exception as e:
        traceback.print_exc()
        sys.exit(1)
