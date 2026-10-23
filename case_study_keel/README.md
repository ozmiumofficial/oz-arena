# Keel remaster study

> This is the original one-model session, kept as a case study. The generalized tools that run on every player model are in `../tools` (see `../README.md`). Paths below that start with `Documents\Keel\` now live in `Documents\Q3Remaster\case_study_keel\`.

Built 2026-10-22. Quake III Arena's Keel player model, taken from a legally owned Steam copy, turned into:

- a plain FBX with the original vertex animation as shape keys,
- a remastered version: one smoothed skinned mesh, a 21-bone skeleton recovered from the animation frames, 25 named clips, and 2K PBR texture maps,
- a web viewer comparing original and remaster side by side.

Online viewer (private to Ozmium's claude.ai account): https://claude.ai/artifact/8idHYcHtmEor4qy1ZbvWbT

The roadmap for taking this further is in [PLAN.md](PLAN.md).

## Folder map

```
Keel/
  README.md                 this file
  PLAN.md                   remaster roadmap, phases, open decisions
  q3_original/              the 28 Keel files extracted from baseq3/pak0.pk3 (md3, skins, tga, animation.cfg)
  fbx/                      simple conversion: keel.fbx (shape-key animation), keel.blend, clips.json, textures
  convert_keel.py           Blender script that made fbx/
  remaster/
    run_all.ps1             rebuilds everything below from q3_original/ in one go (about 3 minutes)
    requirements.txt        Python packages
    build_mesh_data.py      step 1: weld + subdivide the MD3 meshes
    fit_rig.py              step 2: recover skeleton + skin weights from the vertex frames
    make_pbr.py             step 3: PBR maps from the painted diffuse
    build_rigged.py         step 4: Blender assembly, exports .blend / .fbx / .glb
    pack_viewer.py          step 5: assembles the web viewer
    keel-remaster.html      viewer page source
    output/                 DELIVERABLES: keel_rigged.blend, .fbx, .glb, rig.json, textures/
    viewer/                 the web viewer, ready to serve (index.html)
    build/                  intermediate data (safe to delete; run_all.ps1 recreates it)
  tools/                    render-check scripts used to verify the FBX and the rig
  notes/
    images/                 verification renders and texture-map contact sheets from the session
    claude_memory_note.md   the condensed notes Claude keeps about this project
```

## Setting up on another PC

1. Install **Python 3.12** and the packages: `pip install -r remaster\requirements.txt`
2. Install **Blender 5.2** (5.x should work; 4.4+ is needed for the action-slot API).
3. Copy this `Keel` folder anywhere. Nothing needs Quake 3 installed: the extracted files are in `q3_original/`.
4. If Python or Blender live somewhere else, pass the paths:

```
powershell -ExecutionPolicy Bypass -File remaster\run_all.ps1 -Python "C:\path\python.exe" -Blender "C:\path\blender.exe"
```

The fit uses a fixed random seed, so a rebuild reproduces the same bones and numbers (legs 5.41 mm, torso 6.48 mm RMS).

## Viewing it

- **Online:** the artifact link above.
- **Locally:** the page fetches its data files, so it needs a web server, not a double-click:

```
cd remaster\viewer
python -m http.server 8768
```

  Then open http://localhost:8768/
- **In Blender:** open `remaster/output/keel_rigged.blend`. Each animation is an action; LEGS_IDLE is on the timeline. Textures are linked with relative paths to `output/textures/`.
- **In Unity:** import `keel_rigged.fbx` as a Generic rig. Make two avatar masks (legs bones, torso bones) on separate Animator layers to layer a legs clip with a torso clip, the way the game did. URP Lit needs the ORM map repacked: metal in R, smoothness (1 - roughness) in A, AO as its own map.

## What each step does

| Step | Script | What happens | Result |
|---|---|---|---|
| Extract | (manual) | pak0.pk3 is a zip; `models/players/keel/` copied out | 28 files |
| 1 Mesh | build_mesh_data.py | MD3 duplicates vertices on UV seams; duplicates that match in every frame are merged. Two levels of Loop subdivision, edges over 50 degrees kept creased. Stored as a sparse matrix so every frame gets the same smoothing. | 812 -> 12,960 triangles |
| 2 Rig | fit_rig.py | Skinning decomposition (the SSDR / Dem Bones idea): rigid clustering of vertex trajectories, Kabsch fits per frame, non-negative weights (max 4 bones), alternating refinement, 12 restarts. Joints = point where neighbor bones move identically; hierarchy = minimum-residual tree. Legs fitted on lower.md3 frames, torso on upper.md3 frames. | 7 leg + 10 torso bones |
| 3 PBR | make_pbr.py | Pad UV islands, 8x Lanczos + bilateral cleanup, color classes (bronze armor, dark steel, heat, fiber, skin), divide out painted lighting into AO, height from luminance + class relief, normal (OpenGL), curvature, edge wear, scratches, grunge, emissive heat cores | 2048 body, 1024 head; albedo / normal / ORM / emissive |
| 4 Assemble | build_rigged.py | One mesh, weights carried through the subdivision, waist / head / weapon bones from tag_torso / tag_head / tag_weapon, one action per animation.cfg line | 21 bones, 25 clips, .blend/.fbx/.glb |
| 5 Viewer | pack_viewer.py | Wraps binaries as base64 JSON, copies textures and previews | viewer/ |

### Bone list

root -> pelvis -> thigh/calf/foot (L, R); pelvis -> waist -> spine -> belly, chest -> neck -> head; chest -> upperarm_L -> forearm_L -> hand_L; spine -> upperarm_R -> forearm_R -> hand_R -> weapon.
Keel holds the gun two-handed, so his left forearm crosses the body center to the grip.

### Animation clips (from animation.cfg)

Legs: WALKCR, WALK, RUN, BACK, SWIM, JUMP, LAND, JUMPB, LANDB, IDLE, IDLECR, TURN.
Torso: GESTURE, ATTACK, ATTACK2, DROP, RAISE, STAND, STAND2.
Whole body: DEATH1-3, DEAD1-3.
Leg frames in lower.md3 skip the torso-only range: file frame = cfg frame - 63.

## Gotchas found along the way

- MD3 header counts start at byte 76 (name, then flags, then counts). Surface headers are 108 bytes.
- MD3 triangle winding is opposite its stored normals; flip it.
- Keel's coordinates: +X forward, +Y left, +Z up, origin 24 units above the feet; 32 units per meter matches the GAME project.
- The pip OpenCV build can't read TGA; load with Pillow.
- Blender's glTF exporter in ACTIONS mode writes keys for every bone in every clip. The viewer filters tracks to the clip's half (legs / torso) before layering; in Unity the avatar masks do the same job.
- Claude artifacts serve only text and media types, so .bin and .glb are shipped as base64 JSON (`keel_bin.json`, `keel_rigged.json` = glTF JSON with embedded buffer).
- The normal maps are OpenGL style (green up). In three.js with `flipY = false` textures this needs `normalScale.y = -1`; Blender and Unity read them as-is.

## License

Keel's mesh, textures and animation are id Software's copyrighted art. Fine for private study and reference; not for OZMIUM or any public release. The scripts are Ozmium's to use for anything. See PLAN.md for how fan remasters handle this ("bring your own pak0").
