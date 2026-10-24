"""Team and alternate skins: every .skin set a player model ships besides default (red, blue, and
the bots' own, such as Daemia, Phobos or Stripe), remastered like the default set.

python build_skins.py [model ...] [--jobs 4] [--from pbr] [--godot ../godot]

For each model and skin: build_mesh_data.py --skin <skin> into work/<model>/skins/<skin> (its
textures and UV masks), then make_pbr.py on the images the default set doesn't already have. Out:
  work/<model>/skins/tex/<model>_<stem>_<map>.jpg   the new maps, named the way Godot names the
                                                    images it pulls out of the model's .glb
  work/<model>/skins/skins.json                     {skin: {"materials": {default stem: skin stem},
                                                    "material_modes": {skin stem: {blend, two_sided}}}}
The maps are copied into <godot>/library/characters and "skins" is written into the model's
rig.json there (and into work/<model>/export/rig.json when that exists), for Q3Character.set_skin.
Run tools/flag_3d_textures.py afterwards so Godot imports the new maps as 3D textures.

A skin counts when Quake 3 would load it: lower, upper and head .skin files and an icon
(cg_players.c CG_RegisterClientModelname), and at least one image of its own that ships (Hunter's
harpy names images no pak has; Tim's red skin has no icon). A surface whose image is missing keeps
the default material (Razor's patriot goggles).
"""
import os, sys, json, glob, shutil, argparse, subprocess
from multiprocessing import Pool

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import q3common as q3
from rig_extras import material_modes

ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument("models", nargs="*")
ap.add_argument("--extracted", default=os.path.join(ROOT, "extracted", "baseq3"))
ap.add_argument("--work", default=os.path.join(ROOT, "work"))
ap.add_argument("--godot", default=os.path.join(ROOT, "godot"))
ap.add_argument("--jobs", type=int, default=4, help="parallel workers (4 is safe on 16 GB)")
ap.add_argument("--from", dest="start", choices=["mesh", "pbr", "install"], default="mesh",
                help="start at this step and reuse earlier results")
a = ap.parse_args()
PARTS = ("lower", "upper", "head")
MAPS = ("albedo", "normal", "orm", "emissive")


def skins_of(model):
    """The skins Quake 3 would load for a model, default first."""
    md = os.path.join(a.extracted, "models", "players", model)
    out = []
    for f in sorted(glob.glob(os.path.join(md, "upper_*.skin"))):
        s = os.path.basename(f)[len("upper_"):-len(".skin")]
        if all(os.path.isfile(os.path.join(md, "%s_%s.skin" % (p, s))) for p in PARTS) and \
                any(os.path.isfile(os.path.join(md, "icon_%s.%s" % (s, e))) for e in ("tga", "skin")):
            out.append(s)
    return sorted(out, key=lambda s: (s != "default", s))


def surfaces(model, skin):
    """(part, surface) -> resolved image path (None when missing), as build_mesh_data picks them."""
    md = os.path.join(a.extracted, "models", "players", model)
    out = {}
    for p in PARTS:
        sk = q3.read_skin(os.path.join(md, "%s_%s.skin" % (p, skin)))
        for s in q3.read_md3(os.path.join(md, p + ".md3"))[2]:
            path = sk.get(s["name"]) or (s["shaders"][0] if s["shaders"] else "")
            out[(p, s["name"])] = q3.resolve_texture(a.extracted, path)
    return out


def run(args, log):
    p = subprocess.run([sys.executable] + args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    with open(log, "a", encoding="utf-8") as f:
        f.write("\n$ " + " ".join(args) + "\n" + p.stdout)
    if p.returncode != 0 or "Traceback" in p.stdout:
        raise RuntimeError("%s failed (see %s)" % (os.path.basename(args[0]), log))


def mesh_job(job):
    model, skin = job
    W = os.path.join(a.work, model, "skins", skin)
    os.makedirs(W, exist_ok=True)
    run([os.path.join(HERE, "build_mesh_data.py"), a.extracted, model, W, "--skin", skin], os.path.join(W, "pipeline.log"))
    return job


def pbr_job(job):
    model, skin, stems, band = job
    W = os.path.join(a.work, model, "skins", skin)
    args = [os.path.join(HERE, "make_pbr.py"), W, "--only", ",".join(stems)]
    if band is not None:
        args += ["--face-band", str(band)]
    run(args, os.path.join(W, "pipeline.log"))
    return job


def plan(model):
    """-> (skins.json data, [(skin, local stem, global stem, default image it replaces)] to convert)."""
    W = os.path.join(a.work, model, "skins")
    if len(skins_of(model)) < 2:
        return {}, []
    rig = json.load(open(os.path.join(a.godot, "library", "characters", model + ".rig.json")))
    full = lambda t: q3.resolve_texture(a.extracted, t["source"]) or "missing"
    base = json.load(open(os.path.join(W, "default", "textures.json")))
    names = {full(t): t["stem"] for t in base}   # image -> stem, the default set's first
    if not set(rig["materials"]) <= set(names.values()):
        raise RuntimeError("%s: default stems %s don't match rig.json %s" % (model, sorted(names.values()), rig["materials"]))
    used = set(names.values())
    default = surfaces(model, "default")
    base_stems = set(names.values())
    data, todo = {}, []
    for skin in skins_of(model)[1:]:
        texs = json.load(open(os.path.join(W, skin, "textures.json")))
        local = {full(t): t for t in texs}
        mapping, modes = {}, []
        for k, img in surfaces(model, skin).items():
            if img is None or img == default[k]:
                continue   # missing image: the default material stays
            if img not in names:
                stem = os.path.splitext(os.path.basename(img))[0].lower()
                while stem in used:
                    stem += "_b"
                names[img] = stem
                used.add(stem)
                todo.append((skin, local[img]["stem"], stem, default[k]))
            mapping[names[default[k] or "missing"]] = names[img]
            t = local[img]
            if names[img] not in base_stems and names[img] not in [m["stem"] for m in modes]:
                modes.append(dict(t, stem=names[img]))
        if mapping:
            data[skin] = {"materials": dict(sorted(mapping.items())), "material_modes": material_modes(modes)}
    return data, todo


if __name__ == "__main__":   # Windows starts the pool's workers by importing this file
    models = a.models or [m for m in q3.player_models(a.extracted)
                          if os.path.isfile(os.path.join(a.godot, "library", "characters", m + ".rig.json"))]
    settings = json.load(open(os.path.join(HERE, "models.json")))
    jobs = [(m, s) for m in models for s in skins_of(m) if len(skins_of(m)) > 1]
    if a.start == "mesh":
        print("mesh data for %d skins of %d models" % (len(jobs), len(models)), flush=True)
        with Pool(a.jobs) as pool:
            for m, s in pool.imap_unordered(mesh_job, jobs):
                print("  %s/%s" % (m, s), flush=True)
    plans = {m: plan(m) for m in models}
    if a.start in ("mesh", "pbr"):
        pj = []
        for m, (_, todo) in plans.items():
            by_skin = {}
            for skin, lstem, _, ref in todo:
                by_skin.setdefault(skin, []).append(lstem)
                # its paint glows only where the default image's does (make_pbr.py "heat_from")
                tj = os.path.join(a.work, m, "skins", skin, "textures.json")
                texs = json.load(open(tj))
                for t in texs:
                    if t["stem"] != lstem:
                        continue
                    t.pop("heat_from", None)
                    if ref:
                        t["heat_from"] = "heatref_" + os.path.basename(ref)
                        shutil.copyfile(ref, os.path.join(a.work, m, "skins", skin, "src", t["heat_from"]))
                json.dump(texs, open(tj, "w"), indent=1)
            pj += [(m, s, st, settings.get(m, {}).get("face_band")) for s, st in by_skin.items()]
        print("PBR maps for %d images" % sum(len(j[2]) for j in pj), flush=True)
        with Pool(a.jobs) as pool:
            for m, s, st, _ in pool.imap_unordered(pbr_job, pj):
                print("  %s/%s: %s" % (m, s, ", ".join(st)), flush=True)
    installed = 0
    for m, (data, todo) in plans.items():
        W = os.path.join(a.work, m, "skins")
        out = os.path.join(W, "tex")
        os.makedirs(out, exist_ok=True)
        for skin, lstem, stem, _ in todo:
            for mp in MAPS:
                src = next((p for p in (os.path.join(W, skin, "tex", "%s_%s%s" % (lstem, mp, e)) for e in (".png", ".jpg"))
                            if os.path.isfile(p)), None)
                if src is None:
                    raise RuntimeError("%s/%s: no %s map for %s" % (m, skin, mp, lstem))
                shutil.copyfile(src, os.path.join(out, "%s_%s_%s%s" % (m, stem, mp, os.path.splitext(src)[1])))
        json.dump(data, open(os.path.join(W, "skins.json"), "w"), indent=1)
        lib = os.path.join(a.godot, "library", "characters")
        for f in sorted(os.listdir(out)):
            dst = os.path.join(lib, f)
            if not os.path.isfile(dst) or open(dst, "rb").read() != open(os.path.join(out, f), "rb").read():
                shutil.copyfile(os.path.join(out, f), dst)
                installed += 1
        for rp in (os.path.join(lib, m + ".rig.json"), os.path.join(a.work, m, "export", "rig.json")):
            if os.path.isfile(rp):
                rig = json.load(open(rp))
                rig["skins"] = data
                json.dump(rig, open(rp, "w"), indent=1)
        print("%-10s %s" % (m, " ".join("%s(%d)" % (s, len(d["materials"])) for s, d in data.items()) or "no skins"))
    print("%d maps installed in %s" % (installed, os.path.join(a.godot, "library", "characters")))
