"""World textures step 3: remastered world textures -> one Godot resource per material.

python make_godot_world.py [--work ../work/_world] [--godot ../godot]

Writes into godot/library/world/:
  materials/<path>.tres   ShaderMaterial per surface/liquid shader (path = shader name minus
                          "textures/"), FogMaterial per fog shader
  skies/<path>.tres       Sky per sky shader (farbox -> panorama, or cloud layers)
  textures/               the remastered maps the materials use; animMap frames as atlases
  world_materials.json    q3 shader name -> resource, kind, base size (pixels), light energy
                          and color, surfaceparms: what a BSP importer needs
Stochastic hex tiling is switched on only for textures without strong straight lines
(isotropic stone, rock, organic), so bricks and panels keep their pattern.
"""
import os, re, sys, json, glob, math, shutil, argparse
import numpy as np
import cv2
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ap = argparse.ArgumentParser()
ap.add_argument("--work", default=os.path.join(ROOT, "work", "_world"))
ap.add_argument("--godot", default=os.path.join(ROOT, "godot"))
a = ap.parse_args()
TEX = os.path.join(a.work, "tex")
SRC = os.path.join(a.work, "src")
LIB = os.path.join(a.godot, "library", "world")
for d in ("materials", "skies", "textures"):
    os.makedirs(os.path.join(LIB, d), exist_ok=True)
materials = json.load(open(os.path.join(a.work, "materials.json")))
textures = {t["stem"]: t for t in json.load(open(os.path.join(a.work, "textures.json")))}
UNIT = 1.0 / 32.0
WAVES = {"sin": 1, "triangle": 2, "square": 3, "sawtooth": 4, "inversesawtooth": 5}


def res(path):
    return "res://" + os.path.relpath(path, a.godot).replace("\\", "/")


def put(stem, suffix):
    """Copy one remastered map into the library; -> res:// path or None."""
    for ext in (".png", ".jpg") if suffix == "_albedo" else (".jpg",):
        src = os.path.join(TEX, stem + suffix + ext)
        if os.path.exists(src):
            dst = os.path.join(LIB, "textures", stem + suffix + ext)
            if not os.path.exists(dst) or os.path.getmtime(src) > os.path.getmtime(dst):
                shutil.copy2(src, dst)
            return res(dst)
    return None


def atlas(stems, suffix, name, size=256):
    """animMap frames side by side at size x size each."""
    frames = []
    for s in stems:
        p = next((os.path.join(TEX, s + suffix + e) for e in (".png", ".jpg") if os.path.exists(os.path.join(TEX, s + suffix + e))), None)
        if p:
            frames.append(np.array(Image.open(p).convert("RGB").resize((size, size), Image.LANCZOS)))
    if not frames:
        return None
    dst = os.path.join(LIB, "textures", name + suffix + "_atlas.jpg")
    Image.fromarray(np.concatenate(frames, axis=1)).save(dst, quality=92)
    return res(dst)


# Names of textures with a painted feature (craters, arrows, symbols, panels ...), which
# stochastic tiling would slice at its hex cells (Ozmium's q3dm1 crater report, 2026-10-22).
FEATURE_NAMES = re.compile(r"(broke|_ow|arrow|symbol|letter|sign|door|tower|eye|light|logo|skull|face|demon|button|jump|"
                           r"pad|launch|screen|comp|fin$|_e$|banner|steed|ball|bullseye|target|glow|tech|pipe|vent|"
                           r"grate|panel|center|arch)")


def isotropic(stem, name=""):
    """True when stochastic tiling can't hurt the texture: no strong axis-aligned lines, no
    localized feature (an 8x8 cell far from the median), and no feature in its name."""
    t = textures.get(stem)
    if not t or t.get("alpha") or FEATURE_NAMES.search(name.split("/")[-1]) or name.startswith("models/"):
        return False
    g = np.array(Image.open(os.path.join(SRC, t["file"])).convert("L")).astype(np.float32)
    gx, gy = np.abs(cv2.Sobel(g, cv2.CV_32F, 1, 0)), np.abs(cv2.Sobel(g, cv2.CV_32F, 0, 1))
    col, row = gx.mean(0), gy.mean(1)
    score = (col.std() / (col.mean() + 1e-6) + row.std() / (row.mean() + 1e-6)) / 2
    small = cv2.resize(g, (128, 128), interpolation=cv2.INTER_AREA)
    cells = small.reshape(8, 16, 8, 16).mean((1, 3))
    feature = float(np.abs(cells - np.median(cells)).max() / (small.std() + 1e-3))
    return score < 0.28 and feature < 1.0


def light_color(stem_list):
    for stem, suffix in stem_list:
        if not stem:
            continue
        p = next((os.path.join(TEX, stem + suffix + e) for e in (".jpg", ".png") if os.path.exists(os.path.join(TEX, stem + suffix + e))), None)
        if p:
            im = np.array(Image.open(p).convert("RGB").resize((64, 64))).reshape(-1, 3).astype(np.float32)
            lum = im.mean(1)
            sel = im[lum >= np.percentile(lum, 80)]
            c = sel.mean(0) if len(sel) else im.mean(0)
            return [round(float(x), 3) for x in c / max(c.max(), 1.0)]
    return [1.0, 1.0, 1.0]


def tcmods(mods):
    scale, rot, scroll, turb = [1.0, 1.0], 0.0, [0.0, 0.0], [0.0, 0.0, 0.0, 0.0]
    for m in mods:
        k, args = m[0], [x for x in m[1:] if isinstance(x, (int, float))]
        if k == "scale" and len(args) >= 2:
            scale = [scale[0] * args[0], scale[1] * args[1]]
        elif k == "rotate" and args:
            rot += args[0]
        elif k == "scroll" and len(args) >= 2:
            scroll = [scroll[0] + args[0], scroll[1] + args[1]]
        elif k == "turb" and len(args) >= 4:
            turb = args[:4]
    return scale, rot, scroll, turb


def wave(rgbgen):
    if rgbgen and rgbgen[0] == "wave" and len(rgbgen) >= 6:
        return [WAVES.get(rgbgen[1], 1), rgbgen[2], rgbgen[3], rgbgen[5]], rgbgen[4]
    return [0, 1.0, 0.0, 0.0], 0.0


def fmt(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return repr(round(float(v), 5))
    if isinstance(v, (list, tuple)):
        return "Vector%d(%s)" % (len(v), ", ".join(repr(round(float(x), 5)) for x in v))
    return v


def write_shader_material(path, name, shader, params, textures_, next_pass=None):
    """next_pass: (shader, params, textures) of a second material drawn after this one."""
    ext = [("Shader", shader)] + [("Texture2D", p) for p in textures_.values()]
    if next_pass:
        ext += [("Shader", next_pass[0])] + [("Texture2D", p) for p in next_pass[2].values()]
    lines = ['[gd_resource type="ShaderMaterial" load_steps=%d format=3]' % (len(ext) + 1 + bool(next_pass)), ""]
    ids = {}
    for typ, p in ext:
        if p not in ids:
            ids[p] = "%d_r" % (len(ids) + 1)
            lines.append('[ext_resource type="%s" path="%s" id="%s"]' % (typ, p, ids[p]))
    if next_pass:
        lines += ["", '[sub_resource type="ShaderMaterial" id="next_pass"]', 'shader = ExtResource("%s")' % ids[next_pass[0]]]
        lines += ['shader_parameter/%s = ExtResource("%s")' % (k, ids[p]) for k, p in next_pass[2].items()]
        lines += ["shader_parameter/%s = %s" % (k, fmt(v)) for k, v in next_pass[1].items()]
    lines += ["", "[resource]", 'resource_name = "%s"' % name, 'shader = ExtResource("%s")' % ids[shader]]
    if next_pass:
        lines.append('next_pass = SubResource("next_pass")')
    for k, p in textures_.items():
        lines.append('shader_parameter/%s = ExtResource("%s")' % (k, ids[p]))
    for k, v in params.items():
        lines.append("shader_parameter/%s = %s" % (k, fmt(v)))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "w", encoding="utf-8").write("\n".join(lines) + "\n")


# ------------------------------------------------------------------ skies
FACES = {  # tr_sky.c st_to_vec + sky_texorder: depth axis, s axis, t axis as (index, sign) in id coords
    "rt": ((0, 1), (1, -1), (2, 1)), "lf": ((0, -1), (1, 1), (2, 1)),
    "bk": ((1, 1), (0, 1), (2, 1)), "ft": ((1, -1), (0, -1), (2, 1)),
    "up": ((2, 1), (1, -1), (0, -1)), "dn": ((2, -1), (1, -1), (0, 1)),
}


def panorama(faces, out_path, W=2048, H=1024):
    u = (np.arange(W) + 0.5) / W
    v = (np.arange(H) + 0.5) / H
    uu, vv = np.meshgrid(u, v)
    theta, phi = (uu - 0.5) * 2 * math.pi, vv * math.pi  # inverse of sky_panorama.gdshader
    gx, gy, gz = np.sin(theta) * np.sin(phi), np.cos(phi), np.cos(theta) * np.sin(phi)
    q = np.stack([gz, gx, gy])  # Godot -> id Tech 3 axes: x_q = Z, y_q = X, z_q = Y
    major = np.argmax(np.abs(q), axis=0)
    out = np.zeros((H, W, 3), np.float32)
    for suf, ((di, ds), (si, ss), (ti, ts)) in FACES.items():
        if suf not in faces:
            continue
        img = np.array(Image.open(os.path.join(TEX, faces[suf] + ".jpg")).convert("RGB")).astype(np.float32)
        fh, fw = img.shape[:2]
        mask = (major == di) & (np.sign(q[di]) == ds)
        depth = np.abs(q[di]) + 1e-9
        s, t = ss * q[si] / depth, ts * q[ti] / depth
        mx = ((s + 1) * 0.5 * fw - 0.5).astype(np.float32)
        my = ((1 - (t + 1) * 0.5) * fh - 0.5).astype(np.float32)
        sample = cv2.remap(img, mx, my, cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
        out[mask] = sample[mask]
    Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)).save(out_path)


def write_sky(path, name, shader, params, textures_):
    ext = [("Shader", shader)] + [("Texture2D", p) for p in textures_.values()]
    lines = ['[gd_resource type="Sky" load_steps=%d format=3]' % (len(ext) + 2), ""]
    ids = {}
    for i, (typ, p) in enumerate(ext):
        ids[p] = "%d_r" % (i + 1)
        lines.append('[ext_resource type="%s" path="%s" id="%s"]' % (typ, p, ids[p]))
    lines += ["", '[sub_resource type="ShaderMaterial" id="ShaderMaterial_sky"]', 'resource_name = "%s"' % name,
              'shader = ExtResource("%s")' % ids[shader]]
    for k, p in textures_.items():
        lines.append('shader_parameter/%s = ExtResource("%s")' % (k, ids[p]))
    for k, v in params.items():
        lines.append("shader_parameter/%s = %s" % (k, fmt(v)))
    lines += ["", "[resource]", 'resource_name = "%s"' % name, 'sky_material = SubResource("ShaderMaterial_sky")']
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "w", encoding="utf-8").write("\n".join(lines) + "\n")


catalog, counts = {}, {}
done_panoramas = {}
for m in materials:
    name, kind = m["name"], m["kind"]
    rel = name.replace("textures/", "", 1)
    counts[kind] = counts.get(kind, 0) + 1
    entry = dict(kind=kind, parms=m.get("parms", []))
    if kind in ("hidden", "unshipped"):
        if kind == "unshipped":
            entry["missing"] = m.get("missing")
        catalog[name] = entry
        continue
    if kind == "fog":
        dst = os.path.join(LIB, "materials", rel + ".tres")
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        c, dist = m["color"], max(float(m["distance"]), 1.0)
        open(dst, "w").write('[gd_resource type="FogMaterial" format=3]\n\n[resource]\nresource_name = "%s"\n'
                             'density = %s\nalbedo = Color(%s, %s, %s, 1)\n' % (
                                 name, round(min(3.0 / (dist * UNIT), 8.0), 4), c[0], c[1], c[2]))
        entry.update(res=res(dst), color=c, distance=dist)
        catalog[name] = entry
        continue
    if kind == "sky":
        dst = os.path.join(LIB, "skies", rel + ".tres")
        energy = 1.0
        if m["farbox"]:
            key = tuple(sorted(m["farbox"].items()))
            if key not in done_panoramas:
                pano = os.path.join(LIB, "skies", (m["farbox"].get("rt") or rel).replace("__sky", "").replace("__", "_") + "_pano.png")
                panorama(m["farbox"], pano)
                done_panoramas[key] = res(pano)
            write_sky(dst, name, "res://shaders/world/sky_panorama.gdshader", dict(energy=energy),
                      dict(panorama=done_panoramas[key]))
        else:
            layers = m["layers"]
            tex_ = {}
            ch = str(m.get("cloudheight", "512"))
            params = dict(cloud_height=float(ch) if ch.replace(".", "", 1).isdigit() else 512.0, energy=energy)
            for i, l in enumerate(layers[:2]):
                src = os.path.join(TEX, l["stem"] + ".jpg")
                dstt = os.path.join(LIB, "textures", l["stem"] + ".jpg")
                shutil.copy2(src, dstt)
                tex_["layer%d" % (i + 1)] = res(dstt)
                sc, _, scr, _ = tcmods(l["tcmods"])
                params["scale%d" % (i + 1)] = sc
                params["scroll%d" % (i + 1)] = scr
                if i == 1:
                    params["layer2_add"] = l["blend"] == "add"
                if i == 0:
                    # below the horizon: the first layer's own average color, darkened (a black
                    # space sky stays black instead of the shader's default haze)
                    avg = np.asarray(Image.open(src).convert("RGB").resize((16, 16)), dtype=np.float64).mean((0, 1)) / 255.0
                    params["horizon_color"] = "Color(%.4f, %.4f, %.4f, 1)" % tuple(avg * 0.6)
            if not tex_:  # its images aren't in the retail paks
                counts["sky_unshipped"] = counts.get("sky_unshipped", 0) + 1
                entry["kind"] = "unshipped"
                catalog[name] = entry
                continue
            write_sky(dst, name, "res://shaders/world/sky_clouds.gdshader", params, tex_)
        entry.update(res=res(dst), surfacelight=m.get("surfacelight", 0))
        catalog[name] = entry
        continue

    base = m["base"]
    tex_ = {}
    if m["frames"] and len(m["frames"]) > 1:
        tex_["albedo_tex"] = atlas(m["frames"], "_albedo", base) or put(base, "_albedo")
    else:
        tex_["albedo_tex"] = put(base, "_albedo")
    tex_["normal_tex"] = put(base, "_normal")
    tex_["orm_tex"] = put(base, "_orm")
    emis = put(base, "_emissive")
    sl = float(m.get("surfacelight", 0) or 0)
    energy = min(max(sl * 0.0015, 0.3), 6.0) if sl > 0 else 1.0
    dst = os.path.join(LIB, "materials", rel + ".tres")
    if kind == "liquid":
        parms = set(m.get("parms", []))
        lk = 2 if "lava" in parms else 1 if "slime" in parms else 0
        _, _, scr, turb = tcmods(m["tcmods"])
        params = dict(kind=lk, scroll=scr if any(scr) else [0.02, 0.01], turb=turb, opacity=[0.6, 0.85, 1.0][lk],
                      emission_energy=[0.0, 1.2, 2.5][lk] * (1 if lk else 0), roughness=[0.35, 0.5, 0.65][lk])
        for d in m.get("deforms", []):
            if d and d[0] == "wave" and len(d) >= 7:
                div = max(float(d[1]), 1.0)
                params.update(wave_amp=float(d[4]) * UNIT, wave_freq=float(d[6]), wave_spread=2 * math.pi / (div * UNIT))
        write_shader_material(dst, name, "res://shaders/world/liquid.gdshader", params,
                              {k: v for k, v in tex_.items() if k in ("albedo_tex", "normal_tex") and v})
    else:
        blend, two = m["blend"], m["two_sided"]
        shader = (("world_add_lit" if m.get("lit_add") else "world_add") if blend == "add" else
                  ("world_blend_2s" if two else "world_blend") if blend == "blend" else
                  ("world_opaque_2s" if two else "world_opaque"))
        # deformVertexes autosprite / autosprite2: camera-facing quads (build_maps.py gives each its own node)
        sprite = next((d[0] for d in m.get("deforms", []) if d and d[0] in ("autosprite", "autosprite2")), None)
        if sprite:
            shader = "world_sprite_add" if blend == "add" else "world_sprite_blend"
        sc, rot, scr, turb = tcmods(m["tcmods"])
        params = dict(base_scale=sc, base_rotate=rot, base_scroll=scr, base_turb=turb, emission_energy=energy,
                      alpha_cut=0.5 if blend == "mask" else 0.0,
                      stochastic=bool(m["group"] in ("stone", "organic", "metal") and not m["frames"] and blend == "opaque"
                                      and not m.get("glows") and isotropic(base, name)))
        if m.get("under") and m["under"].get("stem") and not sprite:
            tex_["under_tex"] = put(m["under"]["stem"], "_albedo")
            usc, urot, uscr, uturb = tcmods(m["under"]["tcmods"])
            params.update(has_under=True, under_env=bool(m["under"].get("env", True)), under_scale=usc,
                          under_motion=[uscr[0], uscr[1], urot], under_turb=uturb)
        if m.get("glow_under") and not sprite:
            params["glow_under"] = True
        if m.get("base_env") and not sprite:
            params["env_map"] = True   # the base stage is a reflection (tcGen environment)
        if sprite:
            params["sprite_mode"] = "2" if sprite == "autosprite2" else "1"   # int uniform: written as is
        if m["frames"] and len(m["frames"]) > 1:
            params.update(frames=float(len(m["frames"])), fps=float(m["fps"]))
        if emis and m["group"] == "liquid":
            tex_["emission_tex"] = emis
        for i, g in enumerate(m["glows"][:2]):
            k = "glow%d" % (i + 1)
            if g.get("frames") and len(g["frames"]) > 1 and i == 0:
                tex_[k + "_tex"] = atlas(g["frames"], "_emissive", g["stem"])
                params.update(glow1_frames=float(len(g["frames"])), glow1_fps=float(g["fps"]))
            else:
                tex_[k + "_tex"] = put(g["stem"], "_emissive")
            gsc, grot, gscr, _ = tcmods(g["tcmods"])
            wv, ph = wave(g["rgbgen"])
            params.update({k + "_scale": gsc, k + "_motion": [gscr[0], gscr[1], grot], k + "_wave": wv, k + "_phase": ph})
        if sl > 0 and not m["glows"]:
            params["self_illum"] = 1.0
        nxt = None
        gl = m.get("glass")
        if gl and gl.get("stem") and tex_.get("albedo_tex"):
            # the reflection added under an alphaFunc paint layer: glass where the paint is cut
            gsc, grot, gscr, gturb = tcmods(gl["tcmods"])
            nxt = ("res://shaders/world/%s.gdshader" % ("world_add_lit" if gl.get("lit") else "world_add"),
                   dict(env_map=True, use_holes=True, base_scale=gsc, base_rotate=grot, base_scroll=gscr, base_turb=gturb),
                   {"albedo_tex": put(gl["stem"], "_albedo"), "hole_tex": tex_["albedo_tex"]})
        write_shader_material(dst, name, "res://shaders/world/%s.gdshader" % shader, params,
                              {k: v for k, v in tex_.items() if v}, nxt)
        entry.update(stochastic=params["stochastic"], shader=shader, sprite=int(params.get("sprite_mode", 0)))
    entry.update(res=res(dst), size=m.get("size"), group=m.get("group"), surfacelight=sl,
                 light_color=light_color([(m.get("lightimage"), "_emissive")] + [(g["stem"], "_emissive") for g in m["glows"]]
                                         + [(base, "_albedo")]) if sl > 0 else None)
    catalog[name] = entry

json.dump(dict(_note="q3 shader name -> remastered Godot resource. size = base image pixels (texture scale "
                     "for map UVs). surfacelight = q3map_surfacelight; light_color for placing real lights.",
               materials=catalog), open(os.path.join(LIB, "world_materials.json"), "w"), indent=1)
stoch = sum(1 for e in catalog.values() if e.get("stochastic"))
print("world materials: %s; %d with stochastic tiling; %d panoramas" % (counts, stoch, len(done_panoramas)))
