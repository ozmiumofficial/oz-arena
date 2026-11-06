"""python pack.py build/engine/oz/pak_oz_remodeled_<name>.pk3 name=work_dir,config.json [...]
A remodeled weapons pack (Settings > Experimental > Remodeled weapons, r_ozRemodeledWeapons: RE_RegisterModel
draws <model>_remodel.iqm from any pak in place of id's md3). Each weapon in a pak of its own beside
tools/remodeled_weapons' pak_oz_remodeled_weapons.pk3 (the rocket launcher), so neither tool rewrites the other's;
the engine, the installer and the Linux launcher take every pak_oz_*.pk3. For each weapon: its IQM beside id's md3,
its maps as DDS (tools/gpu_bc.py, the GPU driver's BC7/BC5) and its shader, scripts/oz_remodeled_weapons_<name>.ozmtr,
which only the oz renderer reads. Files already in the pak that this run doesn't write are kept.

Maps (work_dir/pbr, texture.py) -> <shader>.dds and suffixes:
  .dds        base color, BC7                     _n.dds   normal, BC5, green flipped (rend2's t runs down)
  _s.dds      rend2's specular layout, BC7: R roughness (floor 0.45, Oz's matte rule), G metal (x 0.35, as Oz's
              other maps: rend2 takes a metal's diffuse away), B occlusion (make_pk3s.py's layout)
  _tint.dds   the red parts, rgb = alpha = mask      _trim.dds, _cell.dds   the glows

The shader (one per weapon): the painted stage lit by rend2 with the normal and specular maps; then the red parts
take the player's rail color (rgbGen entity: color1, as id's railgun's glow strips do; the cgame dims it after a
shot and brings it back over 1.5 s): dst * (1 - mask + mask * color), a GL_DST_COLOR GL_ONE_MINUS_SRC_ALPHA blend;
then the cells glow steadily, breathing slowly, and the trim pulses: additive stages in the rail color whose
strength is an alphaGen sine wave, above white for bloom (ozGlow, which also keeps them on rend2's generic program).
"""
import os, sys, json, zipfile
import numpy as np, cv2

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import gpu_bc

ROUGHNESS_FLOOR = 0.45
METAL_SCALE = 0.35
SDL2 = os.environ.get("OZ_SDL2", gpu_bc.SDL2)
if not os.path.exists(SDL2):     # a worktree has no engine build: the main checkout's
    SDL2 = os.path.join(os.path.expanduser("~"), "Documents", "Q3Remaster", "build", "engine", "SDL2.dll")

SHADER = """{shader}
{{
	// Oz Arena: the remodeled {name} (tools/weapon_swap): rend2's lit stage, then the red parts in the rail color
	{{
		map {shader}.dds
		rgbGen lightingDiffuse
	}}
	{{
		stage normalMap
		map {shader}_n.dds
	}}
	{{
		stage specularMap
		map {shader}_s.dds
	}}
	{{
		map {shader}_tint.dds
		blendFunc GL_DST_COLOR GL_ONE_MINUS_SRC_ALPHA
		rgbGen entity
		alphaGen const 1.0
		ozGlow 1
	}}
	{{
		map {shader}_cell.dds
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen entity
		alphaGen wave sin 0.6 0.15 0 0.2
		ozGlow 1.6
	}}
	{{
		map {shader}_trim.dds
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen entity
		alphaGen wave sin 0.5 0.45 0 0.45
		ozGlow 2.5
	}}
}}
"""


def rd(path):
    im = cv2.imread(path, cv2.IMREAD_UNCHANGED)
    if im.ndim == 3:
        im = im[..., ::-1]   # RGB
    return im


def rgba(img, alpha=None):
    if img.ndim == 2:
        img = np.repeat(img[..., None], 3, 2)
    img = img[..., :3]
    a = np.full(img.shape[:2], 255, np.uint8) if alpha is None else alpha
    return np.dstack([img, a]).astype(np.uint8)


def fit(img, size):
    if img.shape[0] == size:
        return img
    return cv2.resize(img, (size, size), interpolation=cv2.INTER_AREA)


def main():
    out = sys.argv[1]
    enc = gpu_bc.Encoder(SDL2)
    stage = os.path.join(os.path.dirname(os.path.abspath(out)), "weapon_swap_stage")
    files, scripts = [], {}
    for arg in sys.argv[2:]:
        name, rest = arg.split("=", 1)
        work, cfgp = rest.split(",")
        cfg = json.load(open(cfgp))
        S = int(cfg.get("texture", 4096))
        pd = os.path.join(work, "pbr")
        sh = cfg["shader"]
        base = rd(os.path.join(pd, "basecolor.png"))
        nrm = cv2.imread(os.path.join(pd, "normal.png"), cv2.IMREAD_UNCHANGED)[..., ::-1].astype(np.float32) / 65535
        nrm8 = (np.clip(nrm, 0, 1) * 255 + 0.5).astype(np.uint8)
        nrm8[..., 1] = 255 - nrm8[..., 1]
        orm = rd(os.path.join(pd, "orm.png"))
        spec = np.stack([np.maximum(orm[..., 1], int(round(ROUGHNESS_FLOOR * 255))),
                         (orm[..., 2] * METAL_SCALE + 0.5).astype(np.uint8), orm[..., 0]], -1)
        tint = rd(os.path.join(pd, "tint.png"))
        tint = tint[..., 0] if tint.ndim == 3 else tint
        maps = {".dds": ("bc7", rgba(fit(base, S))),
                "_n.dds": ("bc5", rgba(fit(nrm8, S))),
                "_s.dds": ("bc7", rgba(fit(spec, S // 2))),
                "_tint.dds": ("bc7", rgba(fit(tint, S), fit(tint, S))),
                "_trim.dds": ("bc7", rgba(fit(rd(os.path.join(pd, "glow_trim.png")), S))),
                "_cell.dds": ("bc7", rgba(fit(rd(os.path.join(pd, "glow_cell.png")), S // 2)))}
        for suf, (fmt, im) in maps.items():
            p = os.path.join(stage, sh + suf)
            enc.write_dds(p, im, fmt)
            files.append((p, sh + suf))
            print("  %s%s %dx%d %s" % (sh, suf, im.shape[1], im.shape[0], fmt))
        files.append((os.path.join(work, os.path.basename(cfg["model"]) + "_remodel.iqm"), cfg["model"] + "_remodel.iqm"))
        scripts["scripts/oz_remodeled_weapons_%s.ozmtr" % name] = SHADER.format(shader=sh, name=name)
    mine = {arc for _, arc in files} | set(scripts)
    with zipfile.ZipFile(out + ".part", "w", zipfile.ZIP_DEFLATED) as z:
        if os.path.exists(out):                       # the other weapons' files stay
            with zipfile.ZipFile(out) as old:
                for info in old.infolist():
                    if info.filename not in mine:
                        z.writestr(info, old.read(info.filename))
        for src, arc in files:
            z.write(src, arc)
        for arc, text in scripts.items():
            z.writestr(arc, text)
    os.replace(out + ".part", out)
    print("wrote", out, "%.1f MB" % (os.path.getsize(out) / 1e6))


if __name__ == "__main__":
    main()
