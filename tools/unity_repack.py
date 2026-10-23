"""Repack the PBR maps for Unity URP / HDRP.

python unity_repack.py <export dir>

The tools write glTF-style ORM maps (R = AO, G = roughness, B = metal), which Godot and
Blender read directly. Unity's URP Lit wants metallic in R and smoothness (1 - roughness)
in A, with occlusion as its own map. Writes <export>/unity/:
  <stem>_BaseMap.jpg, <stem>_Normal.jpg (OpenGL style, which is what Unity expects),
  <stem>_MetallicSmoothness.png (R metal, A smoothness), <stem>_Occlusion.png,
  <stem>_Emission.jpg, and UNITY_IMPORT.txt with the material and rig settings.
"""
import os, sys, glob, shutil
import numpy as np
from PIL import Image

EXP = sys.argv[1]
TEX = os.path.join(EXP, "textures")
OUT = os.path.join(EXP, "unity")
os.makedirs(OUT, exist_ok=True)
stems = sorted({os.path.basename(f)[:-len("_orm.jpg")] for f in glob.glob(os.path.join(TEX, "*_orm.jpg"))})
for s in stems:
    orm = np.asarray(Image.open(os.path.join(TEX, s + "_orm.jpg")).convert("RGB")).astype(np.float32) / 255
    ao, rough, metal = orm[..., 0], orm[..., 1], orm[..., 2]
    ms = np.dstack([metal, metal, metal, 1 - rough])
    Image.fromarray((ms * 255 + 0.5).astype(np.uint8), "RGBA").save(os.path.join(OUT, s + "_MetallicSmoothness.png"))
    Image.fromarray((ao * 255 + 0.5).astype(np.uint8), "L").save(os.path.join(OUT, s + "_Occlusion.png"))
    for src, dst in (("albedo", "BaseMap"), ("normal", "Normal"), ("emissive", "Emission")):
        shutil.copyfile(os.path.join(TEX, "%s_%s.jpg" % (s, src)), os.path.join(OUT, "%s_%s.jpg" % (s, dst)))
open(os.path.join(OUT, "UNITY_IMPORT.txt"), "w").write("""Unity import settings (URP Lit)

Model (the .fbx in the folder above):
  Rig: Animation Type = Generic, Avatar = Create From This Model, Root = root
  Animation: every clip is a take named after animation.cfg (LEGS_RUN, TORSO_ATTACK, ...)
             set Loop Time on the looping ones (LEGS_WALK/RUN/BACK/SWIM/IDLE/IDLECR/TURN, WALKCR)
  Layering like id Tech 3: two Animator layers with Avatar Masks (Transform):
             Legs layer  = root, pelvis, thigh/calf/foot bones, waist
             Torso layer = spine and everything under it (arms, neck, head, weapon)
  Scale: the FBX is in meters (32 id units = 1 m), feet at the origin, facing +Z.

Textures (import settings):
  *_BaseMap.jpg            sRGB on
  *_Normal.jpg             Texture Type = Normal map (OpenGL style, no flip needed)
  *_MetallicSmoothness.png sRGB off; material Workflow = Metallic, Source = Metallic Alpha
  *_Occlusion.png          sRGB off
  *_Emission.jpg           sRGB on; enable Emission, HDR color white, intensity about 1-2
""")
print("unity maps for:", ", ".join(stems))
