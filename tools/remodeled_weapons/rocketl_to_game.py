"""The remodeled rocket launcher as Oz's game files, into the remodeled weapons' pak.

    blender -b --python dump_final.py -- rocket_launcher.blend rocket_launcher final.npz
    python rocketl_to_game.py final.npz <textures folder> build/engine/oz/pak_oz_remodeled_weapons.pk3

rocket_launcher.blend and its textures are the sealed, retextured Tripo model (holes capped, voxel remesh, xatlas
UVs, baked PBR maps; the scripts in tools/weapon_swap). Settings > Experimental > Remodeled weapons draws it in
place of id's models/weapons2/rocketl/rocketl.md3 (renderer2/tr_model.c RE_RegisterModel).

models/weapons2/rocketl/rocketl_remodel.iqm   the mesh in id's weapon space (x forward, y left, z up, origin at the
                                              hand: tag_weapon), in pieces under SHADER_MAX_VERTEXES; joints
                                              tag_weapon (root) and tag_flash (the muzzle)
models/weapons2/rocketl/rocketl_remodel{,_n,_s}.dds   BC7 color, BC5 normal, BC7 specular in rend2's layout
                                              (R roughness, G metal, B AO)
scripts/oz_remodeled_weapons_rocketl.ozmtr    the shader
The pak keeps the other remodeled weapons' files already in it.
"""
import sys, os, zipfile
import numpy as np
from PIL import Image

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(TOOLS)
SDL2 = os.environ.get("OZ_SDL2", os.path.join(REPO, "build", "engine", "SDL2.dll"))   # the engine's, for the encoder
sys.path.insert(0, TOOLS)
_argv = sys.argv
sys.argv = ["make_fighters.py"]
import make_fighters as mf
sys.argv = _argv
import gpu_bc

src, texdir, out = sys.argv[1:4]
NAME = "models/weapons2/rocketl/rocketl_remodel"
SHADER_FILE = "scripts/oz_remodeled_weapons_rocketl.ozmtr"
LENGTH = 40.0                               # id's launcher is 35 long; this one has a shoulder stock
GRIP_SRC = np.array([-0.40, 0.0, 0.08])     # Tripo space (muzzle turned to +x): where the hand wraps the pistol grip
GRIP_Q3 = np.array([-4.5, 0.0, -3.0])       # where id's weapons put the grip, from tag_weapon (tools/fighter_swap)
MUZZLE_Z = 0.18                             # the bore's height in Tripo space

d = np.load(src)
flip = np.array([-1.0, -1.0, 1.0])          # the model's muzzle is at -x: half a turn about z
co = d["co"] * flip
S = LENGTH / (co[:, 0].max() - co[:, 0].min())
pos_v = (co - GRIP_SRC) * S + GRIP_Q3
tri_loops, tri_verts = d["tri_loops"], d["tri_verts"]
cn = d["cn"] * flip
uv = d["uv"].copy()
uv[:, 1] = 1.0 - uv[:, 1]                    # id's t runs down

# corners -> vertices: one per (position, normal, uv)
L = tri_loops.ravel()
key = np.hstack([tri_verts.ravel()[:, None].astype(np.float64), np.round(cn[L], 4), np.round(uv[L], 6)])
_, first, inv = np.unique(key, axis=0, return_index=True, return_inverse=True)
inv = inv.ravel()
P = pos_v[tri_verts.ravel()[first]]
N = cn[L[first]]
N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-12)
UV = uv[L[first]]
T = inv.reshape(-1, 3)[:, ::-1]                # Blender's counter-clockwise front faces -> id's clockwise
TAN = mf.tangents(P, N, UV, T)
print("verts %d tris %d  bounds %s %s" % (len(P), len(T), P.min(0).round(1), P.max(0).round(1)))
prim = dict(pos=P, nrm=N, uv=UV, tan=TAN, jw=np.ones((len(P), 1)), ji=np.zeros((len(P), 1), int))
meshes = []
for i, (lt, vs) in enumerate(mf.chunks(T)):
    meshes.append(dict(name="rocketl_%03d" % i, material=NAME, prim=prim, verts=vs, tris=lt))
print("pieces", len(meshes))

flash = np.eye(4)
flash[:3, 3] = [P[:, 0].max(), 0.0, (MUZZLE_Z - GRIP_SRC[2]) * S + GRIP_Q3[2]]
bind = np.array([np.eye(4), flash])
frames = bind[None]
lo, hi = P.min(0), P.max(0)
r = float(np.linalg.norm(np.maximum(abs(lo), abs(hi))))
iqm = mf.write_iqm(["tag_weapon", "tag_flash"], [-1, 0], bind, frames, [("idle", 0, 1)], meshes,
                   [(lo, hi, float(np.hypot(*np.maximum(abs(lo[:2]), abs(hi[:2])))), r)], "")
print("tag_flash", flash[:3, 3].round(2), "IQM %d KB" % (len(iqm) // 1024))   # tr_model_iqm.c takes up to 64 MB

# textures
enc = gpu_bc.Encoder(SDL2)
tmp = os.path.join(os.path.dirname(os.path.abspath(out)), "dds_tmp")
os.makedirs(tmp, exist_ok=True)
rd = lambda n: np.asarray(Image.open(os.path.join(texdir, n)))
base = rd("rocket_launcher_basecolor.png")
base = np.dstack([base[..., :3], np.full(base.shape[:2], 255, np.uint8)])
nrm = rd("rocket_launcher_normal.png")[..., :3].copy()
nrm[..., 1] = 255 - nrm[..., 1]              # Blender's green-up map -> rend2's t-down tangent space (as make_pk3s)
nrm = np.dstack([nrm, np.full(nrm.shape[:2], 255, np.uint8)])
orm = rd("rocket_launcher_orm.png")[..., :3]
spec = np.dstack([orm[..., 1], orm[..., 2], orm[..., 0], np.full(orm.shape[:2], 255, np.uint8)]).astype(np.uint8)
files = {}
for suffix, img, fmt in (("", base, "bc7"), ("_n", nrm, "bc5"), ("_s", spec, "bc7")):
    p = os.path.join(tmp, "t%s.dds" % suffix)
    enc.write_dds(p, np.ascontiguousarray(img), fmt)
    files[NAME + suffix + ".dds"] = open(p, "rb").read()
    os.remove(p)
    print("dds", suffix or "color", len(files[NAME + suffix + ".dds"]) // 1024, "KB")
os.rmdir(tmp)

shader = """// Settings > Experimental > Remodeled weapons: the remodeled rocket launcher (tr_model.c RE_RegisterModel)

%s
{
	{
		map %s.dds
		rgbGen lightingDiffuse
	}
	{
		stage normalMap
		map %s_n.dds
	}
	{
		stage specularMap
		map %s_s.dds
	}
}
""" % (NAME, NAME, NAME, NAME)
mine = {NAME + ".iqm", SHADER_FILE, "scripts/oz_remodeled_weapons.ozmtr"} | set(files)
with zipfile.ZipFile(out + ".part", "w") as z:
    if os.path.exists(out):     # the pak holds every remodeled weapon: keep the others' files
        with zipfile.ZipFile(out) as old:
            for i in old.infolist():
                if i.filename not in mine:
                    z.writestr(i, old.read(i))
    z.writestr(NAME + ".iqm", iqm, zipfile.ZIP_DEFLATED)
    for k, v in files.items():
        z.writestr(k, v, zipfile.ZIP_DEFLATED)
    z.writestr(SHADER_FILE, shader, zipfile.ZIP_DEFLATED)
os.replace(out + ".part", out)
print(out, os.path.getsize(out) // 1024, "KB")
