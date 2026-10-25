"""World textures step 1: every world shader + every textures/ image -> material list and the
texture jobs for make_pbr.py.

python build_world.py <extracted baseq3> <world work dir> [--target 1024]

Writes into <world work>:
  materials.json   one entry per material (q3 shader name, or the image path for images no
                   shader owns): kind (surface/liquid/sky/fog/hidden), blend, two_sided, base
                   texture stem, glow layers with their tcMod/rgbGen motion, flipbook frames,
                   surface light, liquid/deform data, sky layers, group, original pixel size
  textures.json    make_pbr jobs: kind tile (painted, tiling), glow (additive layer, tiling),
                   additive (an additive shader's base layer: light, black kept black),
                   sky (sky faces / cloud layers, upscale only); group drives the PBR rules
  src/             the source images
Shaders without stages fall back to the image named like the shader, as the engine does.
Map objects: q3map bakes misc_models into the .bsp, so the stock maps also draw shaders from
models/ (torches, lamps, teleporter pads, statues). Every such shader a maps/*.bsp uses is
included as a world material too (build_maps.py resolves map surfaces by shader name).
"""
import os, sys, json, glob, shutil, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image
import q3common as q3
import q3shader

ap = argparse.ArgumentParser()
ap.add_argument("root")
ap.add_argument("work")
ap.add_argument("--target", type=int, default=1024)
ap.add_argument("--only-from", help="a root holding only some files (community maps' pk3s, extracted): the "
                "materials, images and maps to emit are the ones that root has; `root` stays the full lookup "
                "(id's textures first, these over them)")
a = ap.parse_args()
SRC = os.path.join(a.work, "src")
os.makedirs(SRC, exist_ok=True)

# folder -> PBR group (make_pbr tile rules)
GROUPS = {"base_wall": "metal", "base_trim": "metal", "base_floor": "metal", "base_support": "metal",
          "base_door": "metal", "base_button": "metal", "base_object": "metal", "base_light": "light",
          "gothic_wall": "stone", "gothic_block": "stone", "gothic_floor": "stone", "gothic_trim": "stone",
          "gothic_door": "stone", "gothic_cath": "stone", "gothic_ceiling": "stone", "gothic_light": "light",
          "gothic_button": "stone", "stone": "stone", "organics": "organic", "skin": "organic", "hell": "organic",
          "liquids": "liquid", "skies": "sky", "sfx": "metal", "ctf": "metal", "effects": "metal", "tim": "metal",
          "eerie": "stone", "shrine": "stone", "common": "hidden"}

textures, tex_index = [], {}


# community maps keep their textures in folders of their own: the PBR group comes from the name
KEYWORDS = (("sky", ("sky", "cloud")), ("light", ("lamp", "light", "glow", "beam", "lit", "neon")),
            ("organic", ("grass", "leaf", "ivy", "fern", "vine", "wood", "crate", "straw", "trunk", "plant", "dirt", "moss")),
            ("metal", ("metal", "steel", "iron", "chrome", "grate", "grill", "pipe", "plate", "rivet", "vent", "fan",
                       "bolt", "nut", "electrical", "jump", "tele", "support", "beam", "girder", "rail")),
            ("stone", ("concrete", "brick", "stone", "rock", "cliff", "plaster", "ground", "tile", "floor_paint", "grunge", "pillar")))


def group_of(path):
    parts = path.split("/")
    folder = parts[1] if len(parts) > 2 and parts[0] == "textures" else ""
    if folder in GROUPS:
        return GROUPS[folder]
    low = path.lower()
    # anything else is painted or bare masonry: a metal default made lightmapped floors and walls
    # nearly black (metals take little diffuse light; Rustgrad, 2026-10-24)
    return next((g for g, words in KEYWORDS if any(w in low for w in words)), "stone")


def register(path, kind, group, target=None):
    """Texture job for an image path (no extension). -> stem or None if the file is missing."""
    if not path:
        return None
    full = q3.resolve_texture(a.root, path)
    if not full:
        return None
    key = (full, kind)
    if a.only_from and group not in ("sky", "liquid"):
        # a community texture's own path says what it is (a lamp image used by a ceiling shader is still a lamp)
        group = group_of(os.path.splitext(os.path.relpath(full, a.root).replace("\\", "/"))[0])
    if key not in tex_index:
        rel = os.path.relpath(full, a.root).replace("\\", "/")
        stem = os.path.splitext(rel)[0].replace("textures/", "", 1).replace("/", "__")
        if kind != "tile":
            stem += "__" + kind
        ext = os.path.splitext(full)[1]
        shutil.copyfile(full, os.path.join(SRC, stem + ext))
        img = Image.open(full)
        w, h = img.size
        has_alpha = img.mode in ("RGBA", "LA") and img.getchannel("A").getextrema()[0] < 250
        tex_index[key] = len(textures)
        textures.append(dict(stem=stem, file=stem + ext, source=rel, w=w, h=h, kind=kind, group=group,
                             alpha=has_alpha, up=q3.upscale_factor(w, h, target or a.target), parts=["world"],
                             previews="light"))
    return textures[tex_index[key]]["stem"]


def size_of(stem):
    t = next((t for t in textures if t["stem"] == stem), None)
    return [t["w"], t["h"]] if t else None


shaders = q3shader.parse_all(a.root)
only_scripts = (set(os.path.basename(f).lower() for f in glob.glob(os.path.join(a.only_from, "scripts", "*.shader")))
                if a.only_from else None)
# a community pk3 can redefine an id shader (hub3aeroq3's gratelamp_flare): id's stays
id_names = set(q3shader.parse_all(os.path.join(os.path.dirname(a.root.rstrip("/\\")), "retail"))) if a.only_from else set()
world = {k: v for k, v in shaders.items() if k.startswith("textures/") and k not in id_names
         and (only_scripts is None or v.source.lower() in only_scripts)}
import q3bsp
map_shaders = set()
for bsp_file in glob.glob(os.path.join(a.only_from or a.root, "maps", "*.bsp")):
    b = q3bsp.read_bsp(open(bsp_file, "rb").read())
    map_shaders.update(n for n in b.shader_names if not n.startswith("textures/") and n != "noshader")
map_implicit = sorted(n for n in map_shaders if n not in shaders)
world.update({n: shaders[n] for n in map_shaders if n in shaders and n not in id_names and (only_scripts is None or shaders[n].source.lower() in only_scripts)})
if only_scripts is not None:
    map_implicit = [n for n in map_implicit if q3.resolve_texture(a.only_from, n)]
images = sorted(os.path.splitext(os.path.relpath(f, a.root).replace("\\", "/"))[0]
                for f in glob.glob(os.path.join(a.root, "textures", "**", "*.*"), recursive=True)
                if f.lower().endswith((".tga", ".jpg"))
                and (not a.only_from or os.path.isfile(os.path.join(a.only_from, os.path.relpath(f, a.root)))))
materials = []

for name, sh in sorted(world.items()):
    d = sh.describe(a.root)
    group = group_of(name)
    if d["kind"] == "hidden":
        materials.append(dict(name=name, kind="hidden", parms=d["parms"]))
        continue
    if d["kind"] == "fog":
        fp = d["fogparms"] or ((0.5, 0.5, 0.5), 512.0)
        materials.append(dict(name=name, kind="fog", color=list(fp[0]), distance=fp[1], parms=d["parms"]))
        continue
    if d["kind"] == "sky":
        far = d["skyparms"][0] if d["skyparms"] else None
        faces = {}
        if far:
            for suf in ("rt", "bk", "lf", "ft", "up", "dn"):
                s = register("%s_%s" % (far, suf), "sky", "sky", 1024)
                if s:
                    faces[suf] = s
        layers = [dict(stem=register(l["map"], "sky", "sky", 1024), tcmods=l["tcmods"], blend=l["blend"])
                  for l in d["sky_layers"]]
        materials.append(dict(name=name, kind="sky", farbox=faces, layers=[l for l in layers if l["stem"]],
                              cloudheight=d["skyparms"][1] if d["skyparms"] else "512",
                              surfacelight=d["surfacelight"], parms=d["parms"]))
        continue
    base_path = d["base"] or (name if q3.resolve_texture(a.root, name) else None)
    if d["kind"] == "liquid":
        group = "liquid"
    # an additive base layer is light, not paint: its black must stay black (kind "additive"), or
    # every glow, beam and teleporter ring draws as a faint square (2026-10-23)
    base_kind = "additive" if d["blend"] == "add" else "tile"
    base = register(base_path, base_kind, group)
    if base is None:
        # id shipped shader definitions for images cut from the retail paks; no map uses them
        materials.append(dict(name=name, kind="unshipped", missing=base_path, parms=d["parms"], source=sh.source))
        continue
    frames = [register(f, base_kind, group) for f in d["anim"][1]] if d["anim"] else []
    glows = []
    for g in d["glows"]:
        gs = register(g["map"], "glow", group)
        if gs:
            gf = [register(f, "glow", group) for f in g["anim"][1]] if g["anim"] else []
            glows.append(dict(stem=gs, frames=[f for f in gf if f], fps=g["anim"][0] if g["anim"] else 0,
                              tcmods=g["tcmods"], rgbgen=g["rgbgen"]))
    materials.append(dict(
        name=name, kind=d["kind"], group=group, blend=d["blend"], two_sided=d["two_sided"], base=base,
        size=size_of(base), frames=[f for f in frames if f], fps=d["anim"][0] if d["anim"] else 0,
        under=(dict(stem=register(d["under"]["map"], "tile", group), tcmods=d["under"]["tcmods"], env=d["under"]["env"])
               if d.get("under") else None),
        glow_under=d.get("glow_under", False),
        glow_lit=d.get("glow_lit", False),
        glass=(dict(stem=register(d["glass"]["map"], "tile", group), tcmods=d["glass"]["tcmods"], lit=d["glass"]["lit"])
               if d.get("glass") else None),
        tcmods=d["tcmods"], glows=glows, env=d["env"], base_env=d["base_env"], lit_add=d["lit_add"], surfacelight=d["surfacelight"],
        lightimage=register(d["lightimage"], "glow", group) if d["lightimage"] else None,
        parms=d["parms"], deforms=d["deforms"], source=sh.source))

# images no shader owns: the engine draws them as image x lightmap
owned = {m["name"] for m in materials}
implicit = 0
for img in images + map_implicit:
    if img in owned or img.startswith("textures/common/"):
        continue
    group = group_of(img)
    if group == "sky":
        continue
    base = register(img, "tile", group)
    if base:
        materials.append(dict(name=img, kind="surface", group=group, blend="opaque", two_sided=False, base=base,
                              size=size_of(base), frames=[], fps=0, tcmods=[], glows=[], env=False,
                              surfacelight=0.0, lightimage=None, parms=[], deforms=[], source="(no shader)"))
        implicit += 1

json.dump(materials, open(os.path.join(a.work, "materials.json"), "w"), indent=1)
json.dump(textures, open(os.path.join(a.work, "textures.json"), "w"), indent=1)
kinds, tk = {}, {}
for m in materials:
    kinds[m["kind"]] = kinds.get(m["kind"], 0) + 1
for t in textures:
    tk[t["kind"]] = tk.get(t["kind"], 0) + 1
print("%d materials (%d from shaders, %d implicit): %s" % (len(materials), len(materials) - implicit, implicit, kinds))
print("%d texture jobs: %s" % (len(textures), tk))
