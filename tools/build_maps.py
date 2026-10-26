"""Quake 3's maps -> Godot: geometry as GLB plus a map description for the scene builder.

python build_maps.py [<extracted baseq3>] [<work maps dir>] [--maps q3dm17 q3dm1 ...] [--godot ../godot]

For every maps/*.bsp in the player's paks (extract_pk3.py --prefix maps/ puts them in extracted/):
  work/_maps/<map>/<map>.glb    drawn surfaces in Godot axes and meters (x = Y, y = Z, z = X, 1/32 m):
                                world_<cx>_<cy>_<cz>   model 0 cut into 1024-unit cells, one primitive per shader
                                model_<n>              each inline model (doors, plats, buttons ...) at its own
                                                       origin, moved by the game's entity state at runtime
                                Material names are the Quake 3 shader names; the import script swaps in the
                                remastered world materials (library/world/world_materials.json).
  work/_maps/<map>/<map>.json   worldspawn, entities, lights, sky, fog volumes, collision hulls (solid and
                                playerclip brushes, convex point sets) and patch collision triangles,
                                inline model bounds, mirrors and portal views ("portals", see portals()),
                                reflection probe boxes ("probes", map_probes.py), the shader report
  work/_maps/maps_report.json   every map: surfaces placed, triangles, shaders resolved / missing / skipped
With --godot, the GLB and the JSON are copied to <godot>/library/maps/<map>/. Godot imports the
GLB on its next scan; tools/bake_maps.gd configure() then switches its import to static lightmaps
(UV2) and the builder makes the scene (Godot ignores hand-written .import files it didn't create).

Patches are re-tessellated finely (smooth curves, about 0.25 units of chord error) instead of
q3map's coarse grids. Flares, nodraw, sky, fog and hidden surfaces aren't drawn: the sky is the
environment's background, fog is a FogVolume. The game itself (q3core) reads the .bsp from the paks
for collision and movement, so nothing here changes how a map plays.
"""
import os, re, sys, json, glob, shutil, struct, argparse, subprocess
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import q3bsp
import q3shader
import q3common as q3
import map_probes

UNIT = 1.0 / 32.0
CELL = 1024.0   # world chunk size in id units (frustum culling and lightmap granularity)

# q3map's light model (light.c in id's GPL q3map): a point light of intensity L sends L * 7500
# "photons"; a surface light of value S sends S * area * 0.25 from every lightsubdivide patch;
# brightness at distance d (units) is photons / d^2 on the 0-255 lightmap scale. In Godot an
# omni light of energy E gives E / d^2 at d meters, so E = photons / 255 / 32^2, times LIGHT_SCALE,
# the one calibration knob. Checked on q3dm17 (2026-10-22): at 1.0 (with the builder's SKY_SCALE
# 4) the bake's mean light is 0.23 against 0.18 for Quake 3's own lightmaps (report "q3_lightmap";
# tools/bake_maps.gd measure() gives the bake's side).
POINT_SCALE = 7500.0
AREA_SCALE = 0.25
SUBDIVIDE = 128.0       # surface light patch size (q3map's default lightsubdivide is 120)
Q3MAP_SUBDIVIDE = 999.0   # id 1.32 q3map's defaultLightSubdivide (light.c): what decides the backsplash
LIGHT_SCALE = 1.0
# per-map corrections from measuring the first bake against the original lightmaps
MAP_LIGHTING = os.path.join(HERE, "map_lighting.json")
SURFACE_LIGHT = {}   # shader -> q3map's light settings (q3shader): two-sided, backsplash, subdivide; filled in main()


def q3_lightmap_stats(bsp):
    """Mean / median of the original lightmaps as linear light (x2 overbright, gamma 2.2), the
    brightness target for the relit maps."""
    if not len(bsp.lightmaps):
        return {}
    lm = np.frombuffer(bsp.lightmaps, np.uint8).reshape(-1, 128, 128, 3).astype(np.float64)
    vals = []
    for s in bsp.surfaces:
        if s["lm_num"] >= 0 and int(s["type"]) != q3bsp.MST_FLARE:
            vals.append(lm[s["lm_num"], s["lm_y"]:s["lm_y"] + s["lm_h"], s["lm_x"]:s["lm_x"] + s["lm_w"]].reshape(-1, 3))
    if not vals or not sum(len(v) for v in vals):
        # q3map2 keeps the rectangles in the vertices' lightmap coordinates, not the surfaces' fields:
        # take the pages' lit pixels instead
        flat = lm.reshape(-1, 3)
        vals = [flat[flat.max(1) > 0]]
        if not len(vals[0]):
            return {}
    lin = ((np.concatenate(vals).mean(1) * 2.0 / 255.0) ** 2.2)
    return {"mean": round(float(lin.mean()), 4), "median": round(float(np.median(lin)), 4)}

ap = argparse.ArgumentParser()
ap.add_argument("root", nargs="?", default=os.path.join(ROOT, "extracted", "baseq3"))
ap.add_argument("work", nargs="?", default=os.path.join(ROOT, "work", "_maps"))
ap.add_argument("--maps", nargs="*")
ap.add_argument("--godot", help="Godot project to copy the maps into (library/maps)")
ap.add_argument("--q3", help="baseq3 folder with the paks (default: the usual install paths)")
ap.add_argument("--json-only", action="store_true",
                help="with --godot, copy only the JSON (the GLB is unchanged, so its baked lightmaps stay)")
a = ap.parse_args()


def to_godot(p):
    """id (x fwd, y left, z up) -> Godot (x = y, y = z, z = x), meters. A rotation, so winding holds."""
    p = np.asarray(p, dtype=np.float64)
    return np.stack([p[..., 1], p[..., 2], p[..., 0]], -1) * UNIT


def to_godot_dir(n):
    n = np.asarray(n, dtype=np.float64)
    return np.stack([n[..., 1], n[..., 2], n[..., 0]], -1)


# ---------------------------------------------------------------- GLB writer

class Glb:
    """Minimal glTF 2.0 binary writer: nodes with one mesh each, primitives with POSITION,
    NORMAL, TEXCOORD_0 and uint32 indices; materials by name only."""

    def __init__(self):
        self.bin = bytearray()
        self.views, self.accessors, self.meshes, self.nodes, self.materials = [], [], [], [], []
        self.mat_index = {}

    def _view(self, data, target):
        while len(self.bin) % 4:
            self.bin.append(0)
        self.views.append({"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data), "target": target})
        self.bin += data
        return len(self.views) - 1

    def _accessor(self, arr, comp, typ, target, minmax=False):
        v = self._view(arr.tobytes(), target)
        acc = {"bufferView": v, "componentType": comp, "count": int(arr.shape[0]), "type": typ}
        if minmax:
            acc["min"] = [float(x) for x in arr.min(0)]
            acc["max"] = [float(x) for x in arr.max(0)]
        self.accessors.append(acc)
        return len(self.accessors) - 1

    def material(self, name):
        if name not in self.mat_index:
            self.mat_index[name] = len(self.materials)
            self.materials.append({"name": name, "doubleSided": False,
                                   "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 0.8}})
        return self.mat_index[name]

    def mesh_node(self, name, prims, translation=None, rotation=None):
        """prims: [(material name, pos[n,3] f32, nrm[n,3] f32, uv[n,2] f32, idx[m*3] u32)]"""
        out = []
        for mat, pos, nrm, uv, idx in prims:
            out.append({"attributes": {
                "POSITION": self._accessor(pos.astype(np.float32), 5126, "VEC3", 34962, True),
                "NORMAL": self._accessor(nrm.astype(np.float32), 5126, "VEC3", 34962),
                "TEXCOORD_0": self._accessor(uv.astype(np.float32), 5126, "VEC2", 34962)},
                "indices": self._accessor(idx.astype(np.uint32), 5125, "SCALAR", 34963),
                "material": self.material(mat), "mode": 4})
        self.meshes.append({"name": name, "primitives": out})
        node = {"name": name, "mesh": len(self.meshes) - 1}
        if translation is not None:
            node["translation"] = [float(x) for x in translation]
        if rotation is not None:
            node["rotation"] = [float(x) for x in rotation]   # quaternion x, y, z, w
        self.nodes.append(node)

    def save(self, path):
        gltf = {"asset": {"version": "2.0", "generator": "Q3Remaster build_maps.py"},
                "scene": 0, "scenes": [{"nodes": list(range(len(self.nodes)))}],
                "nodes": self.nodes, "meshes": self.meshes, "materials": self.materials,
                "accessors": self.accessors, "bufferViews": self.views,
                "buffers": [{"byteLength": len(self.bin)}]}
        js = json.dumps(gltf, separators=(",", ":")).encode()
        js += b" " * (-len(js) % 4)
        binp = bytes(self.bin) + b"\0" * (-len(self.bin) % 4)
        with open(path, "wb") as f:
            f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(binp)))
            f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
            f.write(struct.pack("<II", len(binp), 0x004E4942) + binp)


# ---------------------------------------------------------------- one map

def load_world_materials():
    for p in (os.path.join(a.godot or os.path.join(ROOT, "godot"), "library", "world", "world_materials.json"),
              os.path.join(ROOT, "godot", "library", "world", "world_materials.json")):
        if os.path.exists(p):
            return json.load(open(p))["materials"]
    sys.exit("world_materials.json not found: build the world materials first (build_library.py)")


def classify(bsp, si, wm):
    """-> 'draw', 'sky', 'fog', 'skip' (+ the material kind for the report)"""
    name = bsp.shader_names[si]
    sflags = int(bsp.shaders[si]["surface_flags"])
    cflags = int(bsp.shaders[si]["content_flags"])
    w = wm.get(name)
    if w is not None and w["kind"] == "sky" or sflags & q3bsp.SURF_SKY:
        return "sky", "sky"
    if w is not None and w["kind"] == "fog" or cflags & q3bsp.CONTENTS_FOG:
        return "fog", "fog"
    if sflags & q3bsp.SURF_NODRAW or (w is not None and w["kind"] == "hidden"):
        return "skip", "hidden"
    if w is None:
        return "draw", "missing"
    return "draw", w["kind"]


# ---------------------------------------------------------------- mirrors and portal views

# How each portal shader the stock maps use is drawn over its view (the stages after the view):
# its look for Q3Portal and the stage images, copied to library/maps/portal/.
PORTAL_SFX = ["textures/sfx/portal_sfx3.tga", "textures/sfx/portal_sfx1.tga", "textures/sfx/portal_sfx.jpg",
              "textures/sfx/portalfog.jpg"]
PORTAL_LOOKS = {
    "textures/common/mirror1": ("mirror", ["textures/common/mirror1.tga"]),
    "textures/common/mirror2": ("mirror", ["textures/common/mirror1.tga", "textures/sfx/mirror.jpg"]),
    "textures/sfx/bugmirror": ("bugmirror", ["textures/common/mirror1.tga", "textures/sfx/mirrorkc.tga"]),
    "textures/sfx/portal_sfx": ("portal_sfx", PORTAL_SFX),
    "textures/sfx/portal2_sfx": ("portal_sfx", PORTAL_SFX),
}
BYTEDIRS = np.array(json.load(open(os.path.join(HERE, "q3_bytedirs.json"))))   # q_math.c bytedirs


def perpendicular(src):
    """q_math.c PerpendicularVector: the smallest axis projected onto the plane of src."""
    pos = int(np.argmin(np.abs(src)))
    t = np.zeros(3)
    t[pos] = 1.0
    d = t - np.dot(src, t) / np.dot(src, src) * src
    return d / np.linalg.norm(d)


def portals(bsp, ents, wm):
    """Every mirror and portal view, the way the renderer finds them (tr_main.c
    R_GetPortalOrientations) from the game's ET_PORTAL entities (g_misc.c misc_portal_surface and
    locateCamera, cg_ents.c CG_Portal). Surfaces whose shader has the "portal" keyword are grouped
    by shader and plane; each takes the misc_portal_surface within 64 units of its plane. Without
    a target that's a mirror, else the view comes from the misc_portal_camera it targets: facing
    its own target (or its angles), the direction byte-packed as the game sends it, rolled by
    "roll" and, unless spawnflags 4, swinging 4 degrees either way (1: turning 25, 2: 75 degrees a
    second).
    -> [{shader, look, textures, mirror, range, surface: {origin, axes}, camera: {origin, axes,
    roll, swing, speed} | None, rect: [u0, v0, u1, v1] along surface axes 1 and 2, in_glb, verts,
    normals, uvs, indices}] in Godot axes and meters (axes are id's axis vectors turned to Godot)."""
    bodies = q3.shader_bodies(a.root)
    is_portal = {}
    groups = {}
    for s in bsp.surfaces:
        name = bsp.shader_names[int(s["shader"])]
        if name not in is_portal:
            is_portal[name] = bool(re.search(r"(^|\n)\s*portal\s*(\n|$)", bodies.get(name, "")))
        if not is_portal[name] or int(s["type"]) == q3bsp.MST_FLARE:
            continue
        m = q3bsp.surface_mesh(bsp, s)
        if m is None:
            continue
        pos, nrm, st, tris = m
        # R_PlaneForSurface: a face's own plane, else the plane of the first triangle
        n = np.asarray(s["lm_vecs"][2], dtype=np.float64)
        if int(s["type"]) != q3bsp.MST_PLANAR or np.linalg.norm(n) < 0.5:
            t = pos[tris[0]]
            n = np.cross(t[1] - t[0], t[2] - t[0])
        n = n / np.linalg.norm(n)
        dist = float(np.dot(pos[tris[0][0]], n))
        # one view per shader, plane and patch of touching surfaces
        key = (name, tuple(np.round(n, 3)), round(dist, 1))
        lo, hi = pos.min(0) - 16.0, pos.max(0) + 16.0
        g = next((g for g in groups.setdefault(key, []) if np.all(lo <= g["hi"]) and np.all(hi >= g["lo"])), None)
        if g is None:
            g = {"n": n, "dist": dist, "pos": [], "nrm": [], "st": [], "tris": [], "count": 0, "lo": lo, "hi": hi}
            groups[key].append(g)
        g["lo"], g["hi"] = np.minimum(g["lo"], lo), np.maximum(g["hi"], hi)
        g["pos"].append(pos)
        g["nrm"].append(nrm)
        g["st"].append(st)
        g["tris"].append(tris + g["count"])
        g["count"] += len(pos)
    targets = {e["targetname"]: e for e in ents if "targetname" in e}
    spots = [e for e in ents if e.get("classname") == "misc_portal_surface" and "origin" in e]
    out = []
    for (name, _, _), g in [(k, g) for k, gs in groups.items() for g in gs]:
        n, dist = g["n"], g["dist"]
        ent = next((e for e in spots if abs(np.dot(vec(e["origin"]), n) - dist) <= 64.0), None)
        if ent is None:
            continue   # the renderer can't place a view without one
        pos = np.concatenate(g["pos"])
        s_axes = np.array([n, perpendicular(n), np.zeros(3)])
        s_axes[2] = np.cross(s_axes[0], s_axes[1])
        cam = targets.get(ent.get("target", ""))
        camera = None
        if cam is None or "origin" not in cam:
            s_origin = n * dist   # a mirror reflects about the plane
        else:
            eo = vec(ent["origin"])
            s_origin = eo - (np.dot(eo, n) - dist) * n
            co = vec(cam["origin"])
            aim = targets.get(cam.get("target", ""))
            if aim is not None and "origin" in aim:
                d = vec(aim["origin"]) - co
            else:
                ang = [float(x) for x in cam["angles"].split()] if "angles" in cam else [0.0, float(cam.get("angle", "0")), 0.0]
                p, yaw = np.radians(ang[0]), np.radians(ang[1])
                d = np.array([np.cos(yaw) * np.cos(p), np.sin(yaw) * np.cos(p), -np.sin(p)])
            d = d / np.linalg.norm(d)
            d = BYTEDIRS[int(np.argmax(BYTEDIRS @ d))]   # DirToByte / ByteToDir
            ax1 = -perpendicular(d)   # "negating this tends to get the directions like they want"
            c_axes = np.array([-d, -ax1, np.cross(d, ax1)])   # the renderer negates axes 0 and 1
            flags = int(cam.get("spawnflags", "0"))
            roll_byte = int(float(cam.get("roll", "0")) / 360.0 * 256)
            camera = {"origin": to_godot(co).round(4).tolist(), "axes": to_godot_dir(c_axes).round(6).tolist(),
                      "roll": roll_byte / 256.0 * 360.0, "swing": not (flags & 4),
                      "speed": 25.0 if flags & 1 else (75.0 if flags & 2 else 0.0)}
        rel = pos - s_origin
        u, v = rel @ s_axes[1], rel @ s_axes[2]
        look, tex = PORTAL_LOOKS.get(name, ("mirror", ["textures/common/mirror1.tga"]))
        rng = re.search(r"alphagen\s+portal\s+([\d.]+)", bodies.get(name, ""))
        out.append({
            "shader": name, "look": look, "textures": [os.path.splitext(os.path.basename(t))[0] for t in tex],
            "mirror": camera is None, "range": float(rng.group(1) if rng else 256.0) * UNIT,
            "surface": {"origin": to_godot(s_origin).round(4).tolist(), "axes": to_godot_dir(s_axes).round(6).tolist()},
            "camera": camera,
            "rect": [round(float(u.min()) * UNIT, 4), round(float(v.min()) * UNIT, 4),
                     round(float(u.max()) * UNIT, 4), round(float(v.max()) * UNIT, 4)],
            "in_glb": classify(bsp, bsp.shader_names.index(name), wm)[0] == "draw",
            "verts": to_godot(pos).round(4).ravel().tolist(),
            "normals": to_godot_dir(np.concatenate(g["nrm"])).round(4).ravel().tolist(),
            "uvs": np.concatenate(g["st"]).round(5).ravel().tolist(),
            "indices": np.concatenate(g["tris"]).astype(int).ravel().tolist(),
        })
    return out


def copy_portal_textures(dst):
    """The stage images PORTAL_LOOKS names, as PNG (alpha kept) for Q3Portal."""
    from PIL import Image
    os.makedirs(dst, exist_ok=True)
    for _, tex in PORTAL_LOOKS.values():
        for t in tex:
            out = os.path.join(dst, os.path.splitext(os.path.basename(t))[0] + ".png")
            src = q3.resolve_texture(a.root, t)
            if src and not os.path.exists(out):
                Image.open(src).convert("RGBA").save(out)


def light_of(e):
    """A light entity -> a Godot light (energy, range in meters) with q3map's falloff."""
    color = [float(x) for x in e.get("_color", "1 1 1").split()[:3]] if e.get("_color") else [1.0, 1.0, 1.0]
    m = max(color) or 1.0
    intensity = float(e.get("light", e.get("_light", "300")))
    photons = abs(intensity) * POINT_SCALE
    d = {"kind": "point", "origin": to_godot(vec(e["origin"])).round(4).tolist(),
         "energy": round(photons / 255.0 * UNIT * UNIT * LIGHT_SCALE * (1 if intensity >= 0 else 0), 4),
         "range": round(min(np.sqrt(photons) * UNIT, 96.0), 3),
         "color": [c / m for c in color], "target": e.get("target", ""), "intensity": intensity}
    if "radius" in e:
        d["radius"] = float(e["radius"]) * UNIT
    return d


def surface_lights(pos, nrm, tris, value, color, two_sided=False, backsplash=(0.05, 24.0), subdivide=Q3MAP_SUBDIVIDE):
    """q3map's surface lights for one drawn surface (light.c CreateSurfaceLights): one emitter per
    SUBDIVIDE cell of its triangles, at the cell's area-weighted center, 2 units off the face. A
    cull none shader (q3map's twoSided) emits from both faces: console03 lights the recess behind
    its glass. A surface q3map doesn't subdivide (no wider than the shader's q3map_lightsubdivide,
    999 by default) also gets the backsplash: a point light with the shader's q3map_backsplash share
    of its photons (5%) that far out (24 units) (SubdivideAreaLight). The emitters themselves stay
    on 128-unit cells, which the bake is calibrated on."""
    a, b, c = pos[tris[:, 0]], pos[tris[:, 1]], pos[tris[:, 2]]
    cross = np.cross(b - a, c - a)
    area = np.linalg.norm(cross, axis=1) * 0.5
    keep = area > 1e-3
    if not keep.any():
        return []
    cen = ((a + b + c) / 3.0)[keep]
    area = area[keep]
    n = nrm[tris[keep, 0]] + nrm[tris[keep, 1]] + nrm[tris[keep, 2]]
    cells = {}
    for k, key in enumerate(map(tuple, np.floor(cen / SUBDIVIDE).astype(int))):
        cells.setdefault(key, []).append(k)
    patches = []
    for idx in cells.values():
        w = area[idx]
        tot = float(w.sum())
        p = (cen[idx] * w[:, None]).sum(0) / tot
        nn = n[idx].sum(0)
        nn /= max(np.linalg.norm(nn), 1e-9)
        patches.append((p, nn, value * tot * AREA_SCALE))
    frac, dist = backsplash
    splash = frac > 0 and bool(np.all(pos.max(0) - pos.min(0) <= subdivide))
    out = []
    for side in ((1.0, -1.0) if two_sided else (1.0,)):
        for p, nn, photons in patches:
            nn = nn * side
            out.append({"kind": "surface", "origin": to_godot(p + nn * 2.0).round(4).tolist(),
                        "normal": to_godot_dir(nn).round(4).tolist(),
                        "energy": round(photons / 255.0 * UNIT * UNIT * LIGHT_SCALE, 4),
                        "range": round(min(np.sqrt(photons) * UNIT, 96.0), 3), "color": color})
            if splash:
                bp = photons * frac
                out.append({"kind": "point", "origin": to_godot(p + nn * (1.0 + dist)).round(4).tolist(),
                            "energy": round(bp / 255.0 * UNIT * UNIT * LIGHT_SCALE, 4),
                            "range": round(min(np.sqrt(bp) * UNIT, 96.0), 3), "color": color,
                            "target": "", "intensity": 0.0, "backsplash": True})
    return out


def sky_light(root, sky_names):
    """q3map_sun (r g b intensity yaw elevation) and q3map_surfacelight of the map's sky shader."""
    bodies = q3.shader_bodies(root)
    for name in sky_names:
        body = bodies.get(name, "")
        sun = re.search(r"q3map_sun(?:ext)?\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)", body)
        sl = re.search(r"q3map_surfacelight\s+([-\d.]+)", body)
        # q3map2's q3map_skyLight <value> <iterations> is the sky's ambient light: the sky as a surface light
        skyl = re.search(r"q3map_skylight\s+([-\d.]+)", body)
        out = {"shader": name, "surfacelight": float(sl.group(1)) if sl else (float(skyl.group(1)) if skyl else 0.0)}
        if sun:
            r, g, b, inten, yaw, elev = (float(x) for x in sun.groups())
            m = max(r, g, b) or 1.0
            y, e = np.radians(yaw), np.radians(elev)
            toward = np.array([np.cos(y) * np.cos(e), np.sin(y) * np.cos(e), np.sin(e)])   # id axes, toward the sun
            out["sun"] = {"color": [r / m, g / m, b / m], "energy": round(inten / 255.0 * LIGHT_SCALE, 4),
                          "direction": to_godot_dir(-toward).round(4).tolist()}   # the way the light travels
        return out
    return {}


def quads(tris):
    """Groups triangles into the 4-vertex quads autosprite surfaces are made of."""
    parent = {}

    def find(x):
        while parent.setdefault(x, x) != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for t in tris:
        for v in t[1:]:
            parent[find(int(v))] = find(int(t[0]))
    groups = {}
    for t in tris:
        groups.setdefault(find(int(t[0])), set()).update(int(v) for v in t)
    return [sorted(g) for g in groups.values() if len(g) == 4]


def sprite_nodes(pos, st, mode):
    """One autosprite quad -> (center, rotation quaternion, local verts, uvs, triangles) in Godot
    terms. Mode 1 (autosprite) is a camera-facing square of the quad's size with st 0..1 (Quake 3
    ignores the quad's own st); mode 2 (autosprite2) keeps its st and puts its long axis on local Y."""
    g = to_godot(pos)
    c = g.mean(0)
    if mode == 1:
        r = float(np.linalg.norm(g[0] - c)) * 0.707
        loc = np.array([[-r, r, 0], [r, r, 0], [r, -r, 0], [-r, -r, 0]])
        uv = np.array([[0, 0], [1, 0], [1, 1], [0, 1]], dtype=np.float64)
        return c, None, loc, uv, np.array([[0, 3, 2], [0, 2, 1]])
    # the long axis runs between the midpoints of the two shortest edges (tr_shade_calc.c AutoSprite2)
    order = [0, 1, 2, 3]
    best = None
    for i in range(4):
        for j in range(i + 1, 4):
            d = np.linalg.norm(g[i] - g[j])
            if best is None or d > best[0]:
                best = (d, i, j)
    # the far pair is a diagonal; pair each vertex with its nearest other vertex = short edges
    near = {}
    for i in order:
        others = sorted((np.linalg.norm(g[i] - g[j]), j) for j in order if j != i)
        near[i] = others[0][1]
    e1 = (0, near[0])
    rest = [k for k in order if k not in e1]
    m1, m2 = g[list(e1)].mean(0), g[rest].mean(0)
    axis = m2 - m1
    length = np.linalg.norm(axis)
    if length < 1e-6:
        return None
    y = axis / length
    n = np.cross(g[1] - g[0], g[2] - g[0])
    n = n / max(np.linalg.norm(n), 1e-9)
    x = np.cross(y, n)
    x /= max(np.linalg.norm(x), 1e-9)
    z = np.cross(x, y)
    basis = np.stack([x, y, z], 1)   # columns
    loc = (g - c) @ basis
    from scipy.spatial.transform import Rotation
    q = Rotation.from_matrix(basis).as_quat()
    tri = np.array([[0, 1, 2], [0, 2, 3]])
    return c, q, loc, st, tri


def vec(s):
    return np.array([float(x) for x in s.split()[:3]])


CONTENTS_TRANSLUCENT = 0x20000000
SURF_ALPHASHADOW = 0x10000


def cell_name(key):
    return "world_%d_%d_%d" % key[:3] + ("_ns" if len(key) > 3 else "")


def build_map(name, bsp_path, wm, out_dir):
    global LIGHT_SCALE
    per_map = json.load(open(MAP_LIGHTING)) if os.path.exists(MAP_LIGHTING) else {}
    entry = per_map.get(name, 1.0)
    # a number scales the whole map; {"scale": x, "shaders": {shader: factor}} also scales single
    # surface-light shaders (q3dm12's ceiling lamps, once their beams stopped boxing their light in)
    map_scale = float(entry.get("scale", 1.0) if isinstance(entry, dict) else entry)
    shader_scale = entry.get("shaders", {}) if isinstance(entry, dict) else {}
    LIGHT_SCALE = 1.0 * map_scale
    bsp = q3bsp.read_bsp(open(bsp_path, "rb").read())
    os.makedirs(out_dir, exist_ok=True)
    glb = Glb()
    report = {"map": name, "surfaces": 0, "drawn": 0, "triangles": 0, "skipped": {}, "missing": {},
              "patches": 0, "inline_models": len(bsp.models) - 1}
    sky_shaders, fog_shaders = set(), set()

    emitters = []
    sprites = []

    def gather(first, count, cell_of, collect_lights=True):
        """surfaces -> {key: {material: [pos, nrm, uv, idx lists]}}"""
        groups = {}
        for s in bsp.surfaces[first:first + count]:
            report["surfaces"] += 1
            if int(s["type"]) == q3bsp.MST_FLARE:
                report["skipped"]["flare"] = report["skipped"].get("flare", 0) + 1
                continue
            what, kind = classify(bsp, int(s["shader"]), wm)
            sname = bsp.shader_names[s["shader"]]
            if what == "sky":
                sky_shaders.add(sname)
            if what == "fog":
                fog_shaders.add(sname)
            if what != "draw":
                report["skipped"][kind] = report["skipped"].get(kind, 0) + 1
                continue
            m = q3bsp.surface_mesh(bsp, s)
            if m is None:
                continue
            pos, nrm, st, tris = m
            sprite = int(wm.get(sname, {}).get("sprite", 0) or 0)
            if sprite and collect_lights:
                for q in quads(tris):
                    out = sprite_nodes(pos[q], st[q], sprite)
                    if out is not None:
                        sprites.append((sname, out))
                report["drawn"] += 1
                report["sprites"] = report.get("sprites", 0) + 1
                continue
            sl = float(wm.get(sname, {}).get("surfacelight", 0) or 0) * float(shader_scale.get(sname, 1.0))
            if sl > 0 and collect_lights:
                emitters.extend(surface_lights(pos, nrm, tris, sl, wm[sname].get("light_color") or [1.0, 1.0, 1.0],
                                               **SURFACE_LIGHT.get(sname, {})))
            if int(s["type"]) == q3bsp.MST_PATCH:
                report["patches"] += 1
            if kind == "missing":
                report["missing"][sname] = report["missing"].get(sname, 0) + 1
            key = cell_of(pos)
            # q3map traces light through translucent surfaces (surfaceparm trans) unless they're
            # alphashadow (q3map2: cut-out shadows, leaves). They go on their own nodes (world_..._ns),
            # which the builder sets not to cast; otherwise Rustgrad's sun sprite and cliff backdrop,
            # wrapped around the map, shadowed all of it
            sh = bsp.shaders[int(s["shader"])]
            if (isinstance(key, tuple) and int(sh["content_flags"]) & CONTENTS_TRANSLUCENT
                    and not int(sh["surface_flags"]) & SURF_ALPHASHADOW):
                key = key + ("ns",)
            g = groups.setdefault(key, {}).setdefault(sname, [[], [], [], [], 0])
            g[0].append(to_godot(pos))
            g[1].append(to_godot_dir(nrm))
            g[2].append(st)
            g[3].append(tris + g[4])
            g[4] += len(pos)
            report["drawn"] += 1
            report["triangles"] += len(tris)
        return groups

    def prims(mats, offset=None):
        out = []
        for mat, (P, N, S, T, _) in sorted(mats.items()):
            pos = np.concatenate(P)
            if offset is not None:
                pos = pos - offset
            out.append((mat, pos, np.concatenate(N), np.concatenate(S), np.concatenate(T).ravel()))
        return out

    # model 0: the static world, cut into cells
    m0 = bsp.models[0]
    world = gather(int(m0["first_surface"]), int(m0["num_surfaces"]),
                   lambda pos: tuple(np.floor(pos.mean(0) / CELL).astype(int).tolist()))
    for key in sorted(world, key=lambda k: (k[:3], len(k))):
        glb.mesh_node(cell_name(key), prims(world[key]))
    # camera-facing quads (deformVertexes autosprite/autosprite2), each centered on its own node
    for k, (sname, (c, q, loc, uv, tri)) in enumerate(sprites):
        nrm = np.tile([0.0, 0.0, 1.0], (4, 1))
        glb.mesh_node("sprite_%d" % k, [(sname, loc, nrm, uv, tri.ravel())], translation=c, rotation=q)

    # inline models: geometry relative to the model's own origin (its bounds' center for movers
    # without an origin brush is fine too: the entity state carries where it is)
    models = []
    owner = {e["model"]: e for e in bsp.entity_list if e.get("model", "").startswith("*")}
    for mi in range(1, len(bsp.models)):
        mm = bsp.models[mi]
        g = gather(int(mm["first_surface"]), int(mm["num_surfaces"]), lambda pos: 0, collect_lights=False)
        e = owner.get("*%d" % mi, {})
        origin = to_godot(vec(e["origin"])) if "origin" in e else np.zeros(3)   # an origin brush's spot
        models.append({"index": mi, "classname": e.get("classname", ""), "origin": origin.round(4).tolist(),
                       "mins": to_godot(mm["mins"]).round(4).tolist(), "maxs": to_godot(mm["maxs"]).round(4).tolist(),
                       "drawn": bool(g)})
        if g:
            glb.mesh_node("model_%d" % mi, prims(g[0]), translation=origin)
    glb.save(os.path.join(out_dir, name + ".glb"))

    # collision for Godot physics (ragdolls, gibs): convex hulls of the brushes, per model
    def hulls(first, count):
        solid, clip = [], []
        for br in bsp.brushes[first:first + count]:
            c = q3bsp.brush_contents(bsp, br)
            if not c & (q3bsp.CONTENTS_SOLID | q3bsp.CONTENTS_PLAYERCLIP):
                continue
            pts = q3bsp.brush_hull(bsp, br)
            if len(pts) < 4:
                continue
            (solid if c & q3bsp.CONTENTS_SOLID else clip).append(to_godot(pts).round(4).ravel().tolist())
        return solid, clip
    solid, clip = hulls(int(m0["first_brush"]), int(m0["num_brushes"]))
    for md in models:
        mm = bsp.models[md["index"]]
        if md["classname"].startswith("trigger_"):   # volumes, not walls
            md["solid"], md["clip"] = [], []
            continue
        md["solid"], md["clip"] = hulls(int(mm["first_brush"]), int(mm["num_brushes"]))
    # patches collide in the game through their own grid; here the drawn tessellation stands in
    patch_tris = []
    for s in bsp.surfaces[int(m0["first_surface"]):int(m0["first_surface"]) + int(m0["num_surfaces"])]:
        if int(s["type"]) != q3bsp.MST_PATCH or not int(bsp.shaders[s["shader"]]["content_flags"]) & q3bsp.CONTENTS_SOLID:
            continue
        pos, nrm, st, tris = q3bsp.surface_mesh(bsp, s, max_error=2.0, max_steps=8)
        patch_tris.append(to_godot(pos[tris.ravel()]).round(4).ravel().tolist())

    # fog volumes: the brushes the fogs lump points at
    fogs = []
    for f in bsp.fogs:
        if 0 <= int(f["brush"]) < len(bsp.brushes):
            pts = to_godot(q3bsp.brush_hull(bsp, bsp.brushes[int(f["brush"])]))
            if len(pts):
                fogs.append({"shader": f["shader"].split(b"\0")[0].decode("latin-1").lower(),
                             "mins": pts.min(0).round(4).tolist(), "maxs": pts.max(0).round(4).tolist()})

    ents = bsp.entity_list
    targets = {e["targetname"]: e for e in ents if "targetname" in e}
    lights = []
    for e in ents:
        if e.get("classname") == "light" and "origin" in e:
            l = light_of(e)
            if l["target"] in targets and "origin" in targets[l["target"]]:
                l["spot_target"] = to_godot(vec(targets[l["target"]]["origin"])).round(4).tolist()
            lights.append(l)
    lights.extend(emitters)
    report["q3_lightmap"] = q3_lightmap_stats(bsp)
    portal_list = portals(bsp, ents, wm)
    report["portals"] = len(portal_list)
    probes = [{k: to_godot(v).round(4).tolist() for k, v in p.items()} for p in map_probes.reflection_probes(bsp)]
    report["probes"] = len(probes)
    info = {
        "map": name, "bsp_version": bsp.version, "light_scale": map_scale,
        "bake": per_map.get("_bake", {}).get(name, {}), "worldspawn": ents[0] if ents else {},
        "entities": ents, "lights": lights, "sky": sorted(sky_shaders), "sky_light": sky_light(a.root, sorted(sky_shaders)), "fogs": fogs,
        "models": models, "collision": {"solid": solid, "clip": clip, "patches": patch_tris}, "portals": portal_list,
        "probes": probes, "bounds": [to_godot(m0["mins"]).round(4).tolist(), to_godot(m0["maxs"]).round(4).tolist()],
        "report": report,
    }
    json.dump(info, open(os.path.join(out_dir, name + ".json"), "w"), separators=(",", ":"))
    report["lights"] = len(lights) - len(emitters)
    report["surface_lights"] = len(emitters)
    report["brush_hulls"] = len(solid) + len(clip)
    report["fog_volumes"] = len(fogs)
    report["sky"] = sorted(sky_shaders)
    return report




def main():
    maps_dir = os.path.join(a.root, "maps")
    if not glob.glob(os.path.join(maps_dir, "*.bsp")):
        q3dir = next((c for c in [a.q3] + q3.DEFAULT_Q3 if c and os.path.isfile(os.path.join(c, "pak0.pk3"))), None)
        if not q3dir:
            sys.exit("No maps in %s and no Quake 3 install found (pass --q3 <baseq3>)" % maps_dir)
        subprocess.run([sys.executable, os.path.join(HERE, "extract_pk3.py"), q3dir, a.root, "--prefix", "maps/"], check=True)
    wm = load_world_materials()
    # q3map's twoSided is only cull none (shaders.c); backsplash and lightsubdivide are per shader
    SURFACE_LIGHT.update({n: dict(two_sided=sh.cull.lower() == "none", backsplash=sh.backsplash,
                                  subdivide=sh.lightsubdivide or Q3MAP_SUBDIVIDE)
                          for n, sh in q3shader.parse_all(a.root).items()})
    names = a.maps or sorted(os.path.splitext(os.path.basename(p))[0] for p in glob.glob(os.path.join(maps_dir, "*.bsp")))
    names = [n for n in names if n != "test_bigbox"]
    reports = []
    for n in names:
        r = build_map(n, os.path.join(maps_dir, n + ".bsp"), wm, os.path.join(a.work, n))
        reports.append(r)
        print("%-16s %5d surfaces, %5d drawn, %7d triangles, %3d patches, %3d models, %4d lights + %4d surface lights, %4d hulls%s" % (
            n, r["surfaces"], r["drawn"], r["triangles"], r["patches"], r["inline_models"], r["lights"], r["surface_lights"], r["brush_hulls"],
            (", %d surfaces with missing materials" % sum(r["missing"].values())) if r["missing"] else ""), flush=True)
        if a.godot:
            dst = os.path.join(a.godot, "library", "maps", n)
            os.makedirs(dst, exist_ok=True)
            for ext in ((".json",) if a.json_only else (".glb", ".json")):
                shutil.copy2(os.path.join(a.work, n, n + ext), os.path.join(dst, n + ext))
    if a.godot:
        copy_portal_textures(os.path.join(a.godot, "library", "maps", "portal"))
    missing = {}
    for r in reports:
        for k, v in r["missing"].items():
            missing[k] = missing.get(k, 0) + v
    json.dump({"maps": reports, "missing_materials": missing}, open(os.path.join(a.work, "maps_report.json"), "w"), indent=1)
    print("%d maps, %d triangles; %d shaders without a material" % (
        len(reports), sum(r["triangles"] for r in reports), len(missing)))


if __name__ == "__main__":
    main()
