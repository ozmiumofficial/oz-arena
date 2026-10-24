"""Props step 1: every non-player MD3 (weapons, items, powerups, ammo, flags, gibs,
projectiles, weapon hits, map objects) -> welded + subdivided mesh data.

python build_props.py <extracted baseq3> <props work dir> [--target 1024]

Writes into <props work>:
  props.json     one entry per model: id (path under models/), category, frames, tags, materials
  materials.json one entry per (texture, blend mode, two-sided) combination
  textures.json  textures to remaster (same format the character step uses, plus kind/glow)
  src/           original images (+ orig_<stem>.png), cage/<id>.npz + <id>_S.npz, cage/uv_<stem>.png

LOD meshes (name_1.md3, name_2.md3 next to name.md3) are skipped; Godot generates LODs.
Material kinds: paint (painted texture, opaque/mask/blend), add (additive energy layer),
env (reflection-only chrome item), envadd (reflection-only glowing glass).
"""
import os, sys, re, json, glob, shutil, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import scipy.sparse as sp
from PIL import Image
import q3common as q3
import meshops

ap = argparse.ArgumentParser()
ap.add_argument("root")
ap.add_argument("work")
ap.add_argument("--target", type=int, default=1024, help="long edge of remastered prop textures")
ap.add_argument("--crease", type=float, default=50.0)
a = ap.parse_args()
for d in ("src", "cage"):
    os.makedirs(os.path.join(a.work, d), exist_ok=True)
MODELS = os.path.join(a.root, "models")

textures, tex_index, materials, mat_index, masks = [], {}, [], {}, {}


def register_texture(path, kind, glow, under=None):
    full = q3.resolve_texture(a.root, path)
    key = (full or "missing", kind, under)
    if key not in tex_index:
        stem = os.path.splitext(os.path.basename(full))[0] if full else "missing"
        if kind in ("env", "envadd", "add"):
            stem += "_" + kind
        while any(t["stem"] == stem for t in textures):
            stem += "_b"
        if full:
            ext = os.path.splitext(full)[1]
            shutil.copyfile(full, os.path.join(a.work, "src", stem + ext))
            img = Image.open(full)
        else:
            ext = ".png"
            img = Image.new("RGB", (64, 64), (128, 128, 128))
            img.save(os.path.join(a.work, "src", stem + ext))
        img.convert("RGBA").save(os.path.join(a.work, "src", "orig_%s.png" % stem))
        w, h = img.size
        has_alpha = img.mode in ("RGBA", "LA") and img.getchannel("A").getextrema()[0] < 250
        gfile = None
        if glow:
            gfull = q3.resolve_texture(a.root, glow)
            if gfull:
                gfile = "glow_%s.png" % stem
                Image.open(gfull).convert("RGB").save(os.path.join(a.work, "src", gfile))
        ufile = None
        if under:
            ufull = q3.resolve_texture(a.root, under)
            if ufull:
                ufile = "under_%s.png" % stem
                Image.open(ufull).convert("RGB").save(os.path.join(a.work, "src", ufile))
        tex_index[key] = len(textures)
        textures.append(dict(stem=stem, file=stem + ext, source=path, w=w, h=h, kind=kind, alpha=has_alpha,
                             glow=gfile, under=ufile, up=q3.upscale_factor(w, h, a.target), parts=["prop"]))
    return tex_index[key]


def material_for(surface_shader):
    info = q3.shader_info(a.root, surface_shader)
    if info is None:  # plain image named directly by the md3
        info = dict(map=surface_shader, blend="opaque", two_sided=False, env=False, env_only=False, glow=None, under=None)
    if info["env_only"]:
        kind = "envadd" if info["blend"] == "add" else "env"
    else:
        kind = "add" if info["blend"] == "add" else "paint"
    ti = register_texture(info["map"], kind, info["glow"], info["under"] if kind == "paint" else None)
    blend = info["blend"] if kind == "paint" else ("add" if kind in ("add", "envadd") else "opaque")
    key = (ti, blend, info["two_sided"])
    if key not in mat_index:
        flags = "" if blend == "opaque" else "__" + blend
        if info["two_sided"]:
            flags += "__2s"
        mat_index[key] = len(materials)
        materials.append(dict(name=textures[ti]["stem"] + flags, texture=ti, blend=blend,
                              two_sided=info["two_sided"], env=info["env"]))
    return mat_index[key]


files = sorted(f for f in glob.glob(os.path.join(MODELS, "**", "*.md3"), recursive=True)
               if os.sep + "players" + os.sep not in f)
props, lods, tag_only = [], {}, {}
for f in files:
    rel = os.path.relpath(f, MODELS).replace("\\", "/")[:-4]
    m = re.match(r"(.+)_(\d)$", rel)
    if m and os.path.isfile(os.path.join(MODELS, m.group(1) + ".md3")):
        lods.setdefault(m.group(1), []).append(rel)
        continue
    nfr, tags, surfs = q3.read_md3(f)
    if not surfs:  # tag-only md3, e.g. weapon *_hand.md3 = first-person weapon placement
        tag_only[rel] = {k: [list(map(float, fr)) for fr in v] for k, v in tags.items()}
        continue
    xyz = np.concatenate([s["xyz"] for s in surfs], axis=1)
    st = np.concatenate([s["st"] for s in surfs])
    tris, mats, base = [], [], 0
    for s in surfs:
        mi = material_for(s["shaders"][0] if s["shaders"] else "")
        tris.append(s["tris"] + base)
        mats += [mi] * len(s["tris"])
        base += len(s["st"])
    tris = np.concatenate(tris)[:, [0, 2, 1]]  # md3 winding is opposite its normals
    mats = np.array(mats, np.int32)
    weld, wpos = meshops.weld(xyz)
    F = weld[tris]
    keep = (F[:, 0] != F[:, 1]) & (F[:, 1] != F[:, 2]) & (F[:, 2] != F[:, 0])
    F, Fmat, Ftri = F[keep], mats[keep], tris[keep]
    FUV = st[Ftri].astype(np.float64)
    all_add = all(materials[m_]["blend"] == "add" for m_ in set(Fmat.tolist()))
    levels = 0 if all_add else (2 if len(F) <= 1200 else 1)  # effect quads stay flat
    crease = meshops.dihedral_creases(wpos[0].astype(np.float64), F, a.crease)
    S, Fh, UVh, Mh, Ch = meshops.subdivide(wpos.shape[1], F, FUV, Fmat, crease, levels)
    UVh = meshops.subdivide_uv(wpos[0].astype(np.float64), F, FUV, S, Fh, UVh, levels)
    for ti_, tri in zip(Fmat, Ftri):
        t = textures[materials[ti_]["texture"]]
        mk = masks.setdefault(t["stem"], np.zeros((t["h"] * t["up"], t["w"] * t["up"]), np.uint8))
        meshops.raster_uv(mk, st, tri)
    safe = rel.replace("/", "__")
    sp.save_npz(os.path.join(a.work, "cage", safe + "_S.npz"), S)
    np.savez(os.path.join(a.work, "cage", safe + ".npz"), wpos=wpos, Fh=Fh, UVh=UVh, Mh=Mh,
             crease_h=np.array(sorted(Ch), np.int32).reshape(-1, 2),
             **{"tag__" + k: np.array(v, np.float32) for k, v in tags.items()})
    props.append(dict(id=rel, file=safe, category=rel.split("/")[0], frames=nfr, tags=sorted(tags),
                      materials=sorted(set(int(x) for x in Fmat)), tris=int(len(F)), hiTris=int(len(Fh)),
                      levels=levels))
for p in props:
    p["lods"] = lods.get(p["id"], [])

json.dump(props, open(os.path.join(a.work, "props.json"), "w"), indent=1)
json.dump(materials, open(os.path.join(a.work, "materials.json"), "w"), indent=1)
# tag-only models: origin + 3 axes per frame, id Tech 3 units/axes (see q3common)
json.dump(tag_only, open(os.path.join(a.work, "tag_models.json"), "w"), indent=1)
json.dump(textures, open(os.path.join(a.work, "textures.json"), "w"), indent=1)
for stem, mk in masks.items():
    Image.fromarray(mk).save(os.path.join(a.work, "cage", "uv_%s.png" % stem))
anim = [p["id"] for p in props if p["frames"] > 1]
print("%d props (%d LOD files skipped), %d materials, %d textures; animated: %s" % (
    len(props), sum(len(v) for v in lods.values()), len(materials), len(textures), ", ".join(anim) or "none"))
kinds = {}
for t in textures:
    kinds[t["kind"]] = kinds.get(t["kind"], 0) + 1
print("texture kinds:", kinds)
