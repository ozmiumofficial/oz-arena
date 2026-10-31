"""P3's effect and media assets -> build/engine/oz/pak_oz_fx.pk3 and pak_oz_video.pk3.

python tools/make_fx_pk3.py [--baseq3 <dir>] [--library build/oz_library] [--no-video] [--report]

Reads the effect textures (--library: tools/make_textures.py's build/oz_library, made from the player's own
id images, or the Godot build's godot/library until that's made), the Godot build's cinematics (read only)
and id's paks:
- pak_oz_fx.pk3
  - The remastered effect textures (godot/library/fx: fx.json "textures"), upscales of id's own
    images. Each flipbook frame becomes a BC7 DDS at its id image path, which only the oz renderer
    loads. Marks that darken (GL_ZERO GL_ONE_MINUS_SRC_COLOR) get their darkening back in color.
  - scripts/oz_fx.ozmtr: id's effect shaders naming those DDS, its sprites soft against walls (ozSoft). id's scripts name .tga, and the
    renderer takes the .dds beside an image only for a name without an extension; the oz renderer
    reads .ozmtr scripts last, so these win. World, player and map-object shaders are left to
    pak_oz_world and pak_oz_fighters.
  - sound/feedback/oz_hit_1..4.wav: id's hit beep pitched for four damage steps (Oz's cgame plays
    them when oz_audio_hit_sound is "pitch"): higher for small hits, lower for big ones.
  - gfx/oz/burn.png: Oz's own, made here: the lightning gun's burn line (Oz's cgame, shader
    oz/fx/burn in game/assets).
- pak_oz_video.pk3: the upscaled cinematics (godot/library/cinematics) as video/<id>.ogv, Theora
  with Vorbis sound, for cl_cin.c.
id's files and their upscales are build output, never committed.
"""
import os, io, re, sys, json, wave, zipfile, argparse
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import q3shader
from q3paks import find_baseq3, Paks

GODOT = r"F:\quake-3-arena-remastered-fan-project"
LIB = os.path.join(GODOT, "godot", "library")
OWN = os.path.join(ROOT, "build", "oz_library")     # tools/make_textures.py's, from the player's id images
OUT = os.path.join(ROOT, "build", "engine", "oz")
STAGE = os.path.join(ROOT, "build", "oz_assets", "fx")

# Each remastered texture's frames, at id's image paths (the Godot build's tools/make_fx.py SOURCES):
# a flipbook's frames sit left to right in its atlas.
FRAMES = {
    "rocket_explosion": ["models/weaphits/rlboom/rlboom_%d" % i for i in range(1, 9)],
    "grenade_explosion": ["models/weaphits/glboom/glboom_%d" % i for i in range(1, 4)],
    "bfg_explosion": ["models/weaphits/bfgboom/bfgboom_%d" % i for i in range(1, 4)],
    "bullet_explosion": ["models/weaphits/bullet%d" % i for i in range(1, 4)],
    "rail_explosion": ["models/weaphits/ring02_%d" % i for i in range(1, 5)],
    "blood_explosion": ["models/weaphits/blood20%d" % i for i in range(1, 6)],
    "plasma_explosion": ["models/weaphits/plasmaboom"],
    "smoke_puff": ["gfx/misc/smokepuff3"],
    "shotgun_smoke": ["gfx/misc/smokepuff2b"],
    "rail_core": ["gfx/misc/railcorethin_mono"],
    "rail_disc": ["gfx/misc/raildisc_mono2"],
    "lightning_bolt": ["gfx/misc/lightning3new"],
    "teleport": ["gfx/misc/teleportEffect2"],
    "bubble": ["sprites/bubble"],
    "talk_balloon": ["sprites/balloon4"],
    "tracer": ["gfx/misc/tracer2"],
    "plasma_ball": ["sprites/plasmaa"],
    "bfg_ball": ["models/weaphits/bfg01"],
    "electric": ["models/weaphits/electric"],
    "flare": ["gfx/misc/flare"],
    "splash": ["sprites/splash"],
    "mark_bullet": ["gfx/damage/bullet_mrk"],
    "mark_burn": ["gfx/damage/burn_med_mrk"],
    "mark_hole": ["gfx/damage/hole_lg_mrk"],
    "mark_energy": ["gfx/damage/plasma_mrk"],
    "mark_blood": ["gfx/damage/blood_stain"],
    "shell_quad": ["textures/effects/quadmap2"],
    "shell_battlesuit": ["textures/effects/envmapgold2"],
    "shell_regen": ["textures/effects/regenmap2"],
    "shell_invis": ["textures/effects/invismap"],
    "blood_screen": ["gfx/damage/blood_screen"],
}
# shaders other paks rewrite (the world library, the fighters), or that no effect uses
SKIP_PREFIX = ("textures/", "models/mapobjects/", "models/players/")
# id's sprites that stand in the world: soft against the scene behind them on the oz renderer
# ("ozSoft <units>", renderer2's tr_shader.c), so smoke and fireballs don't cut a line through walls
SOFT = {"smokepuff": 16, "shotgunsmokepuff": 16, "bloodexplosion": 12, "rocketexplosion": 12,
        "grenadeexplosion": 12, "bfgexplosion": 12, "hastesmokepuff": 16}
# id's hit beep, pitched: (most damage in the step, pitch). Oz's cgame (cg_playerstate.c) picks
# the step from the damage the "dmg" commands reported since the last beep.
HIT_STEPS = [(25, 1.25), (50, 1.1), (75, 0.95), (1 << 30, 0.8)]

ap = argparse.ArgumentParser()
ap.add_argument("--baseq3", help="the player's baseq3 (found in the Steam libraries by default)")
ap.add_argument("--library", default=OWN if os.path.isfile(os.path.join(OWN, "fx", "made.json")) else LIB,
                help="the folder holding fx/fx.json and its flipbooks")
ap.add_argument("--no-video", action="store_true", help="leave pak_oz_video.pk3 as it is")
ap.add_argument("--report", action="store_true", help="list what would be written, convert nothing")
# parsed when run; imported by make_props.py (FRAMES, shaders), the defaults
a = ap.parse_args() if __name__ == "__main__" else ap.parse_args([])


def textures(fx):
    """{id image path (no extension): RGBA uint8} for every remastered frame."""
    out = {}
    for name, frames in FRAMES.items():
        t = fx["textures"].get(name)
        if not t:
            print("not in fx.json:", name)
            continue
        atlas = np.asarray(Image.open(os.path.join(a.library, "fx", t["file"])).convert("RGBA"))
        fw, fh = t["frame_size"]
        if t["frames"] != len(frames) or atlas.shape[1] != fw * len(frames):
            sys.exit("%s: %d frames of %d px in a %d px atlas, expected %d" % (name, t["frames"], fw, atlas.shape[1], len(frames)))
        for i, path in enumerate(frames):
            px = atlas[:, i * fw:(i + 1) * fw].copy()
            if t["mode"] == "darken":
                # make_fx.py turned id's darkening amount (its color) into the decal's alpha
                px[..., 0] = px[..., 1] = px[..., 2] = px[..., 3]
                px[..., 3] = 255
            out[path.lower()] = px
    return out


def shaders(paks, images):
    """scripts/oz_fx.ozmtr: every id shader outside SKIP_PREFIX that names a remastered image,
    with those images as .dds."""
    ref = re.compile(r"([\w/\-.]+?)\.(tga|jpg)\b", re.I)
    found = {}
    for f in sorted(n for n in paks.files if n.startswith("scripts/") and n.endswith(".shader")):
        text = paks.read(f).decode("latin-1")
        for sh in q3shader.parse_text(text, f):
            name = sh.name
            if name.lower().startswith(SKIP_PREFIX):
                continue
            hits = [m for m in ref.finditer(sh.text) if m.group(1).lower() in images]
            if hits:
                text = ref.sub(lambda m: m.group(1) + ".dds" if m.group(1).lower() in images else m.group(0), sh.text)
                if name.lower() in SOFT:
                    text = re.sub(r"(?im)^([ \t]*)(blendfunc\b[^\r\n]*)", lambda m: "%s%s\n%sozSoft %d" % (
                        m.group(1), m.group(2), m.group(1), SOFT[name.lower()]), text)
                found[name.lower()] = text
    return found


def pitched(data, pitch):
    """A WAV's samples resampled so it plays `pitch` times higher at the same rate."""
    src = wave.open(io.BytesIO(data))
    ch, width, rate, n = src.getnchannels(), src.getsampwidth(), src.getframerate(), src.getnframes()
    dtype = {1: np.uint8, 2: np.int16}[width]
    s = np.frombuffer(src.readframes(n), dtype).reshape(-1, ch).astype(np.float64)
    m = int(n / pitch)
    x = np.arange(m) * pitch
    out = np.stack([np.interp(x, np.arange(n), s[:, c]) for c in range(ch)], 1)
    buf = io.BytesIO()
    w = wave.open(buf, "wb")
    w.setnchannels(ch)
    w.setsampwidth(width)
    w.setframerate(rate)
    w.writeframes(np.round(out).astype(dtype).tobytes())
    w.close()
    return buf.getvalue()


def burn_line():
    """gfx/oz/burn.png: the burn line across t, its round ends in s. The middle half of s is the line
    (the same in every column), each outer quarter half a disc (a stroke's ends). Alpha is the scorch
    (after the Godot build's fx_burns.gd), color the glow, white at its core and orange at its rim; the
    cgame tints and fades both by the vertex colors."""
    w, h = 64, 32
    x = (np.arange(w) + 0.5)
    y = (np.arange(h) + 0.5)
    dx = (x - np.clip(x, w / 4, w * 3 / 4)) / (h / 2)
    dy = (y - h / 2) / (h / 2)
    r = np.sqrt(dx[None, :] ** 2 + dy[:, None] ** 2)
    edge = np.clip((r - 0.75) / 0.25, 0, 1)
    edge = 1 - edge * edge * (3 - 2 * edge)
    glow = np.exp(-r * r * 3.5) * edge
    core = np.exp(-r * r * 14)
    scorch = np.exp(-r * r * 3) * edge
    rgba = np.stack([glow, (0.7 + 0.3 * core) * glow, (0.35 + 0.65 * core) * glow, scorch], axis=-1)
    buf = io.BytesIO()
    Image.fromarray((rgba * 255 + 0.5).astype(np.uint8), "RGBA").save(buf, "PNG")
    return buf.getvalue()


def write_pk3(path, files):
    """files: {name in the pk3: bytes or a file path}. DDS and video are stored, the rest deflated."""
    with zipfile.ZipFile(path + ".part", "w") as z:
        for name in sorted(files):
            v = files[name]
            how = zipfile.ZIP_STORED if name.endswith((".dds", ".ogv")) else zipfile.ZIP_DEFLATED
            if isinstance(v, bytes):
                z.writestr(name, v, how)
            else:
                z.write(v, name, how)
    os.replace(path + ".part", path)
    print("%-28s %4d files %8.1f MB" % (os.path.basename(path), len(files), os.path.getsize(path) / 1e6))


def main():
    paks = Paks(a.baseq3 or find_baseq3())
    fx = json.load(open(os.path.join(a.library, "fx", "fx.json")))
    tex = textures(fx)
    sh = shaders(paks, set(tex))
    print("%d remastered frames, %d shaders rewritten" % (len(tex), len(sh)))
    if a.report:
        for name in sorted(sh):
            print("  ", name)
        return
    files = {}
    import gpu_bc
    enc = gpu_bc.Encoder()
    os.makedirs(STAGE, exist_ok=True)
    for path, px in sorted(tex.items()):
        dst = os.path.join(STAGE, path + ".dds")
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        enc.write_dds(dst, px, "bc7")
        files[path + ".dds"] = dst
    files["scripts/oz_fx.ozmtr"] = ("// Oz: id's effect shaders with the remastered images (tools/make_fx_pk3.py)\n\n" +
                                    "\n\n".join(sh[n] for n in sorted(sh)) + "\n").encode("latin-1")
    hit = paks.read("sound/feedback/hit.wav")
    for i, (_, pitch) in enumerate(HIT_STEPS):
        files["sound/feedback/oz_hit_%d.wav" % (i + 1)] = pitched(hit, pitch)
    files["gfx/oz/burn.png"] = burn_line()
    os.makedirs(OUT, exist_ok=True)
    write_pk3(os.path.join(OUT, "pak_oz_fx.pk3"), files)
    if not a.no_video:
        cine = json.load(open(os.path.join(LIB, "cinematics", "cinematics.json")))["cinematics"]
        write_pk3(os.path.join(OUT, "pak_oz_video.pk3"),
                  {"video/" + c["file"].lower(): os.path.join(LIB, "cinematics", c["file"]) for c in cine})


if __name__ == "__main__":
    main()
