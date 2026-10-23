# Quake 3 remaster plan

Goal: see how far Quake III Arena's assets can be brought to modern standards, and build a workflow that scales past one character. Keel was the test case (2026-10-22).

## Where it stands

| Area | Status |
|---|---|
| Extraction from pak0.pk3 | Done for Keel; trivial to script for everything (pk3 = zip) |
| MD3 -> FBX (original vertex animation) | Done (`fbx/`) |
| Mesh smoothing | Done, automatic (Loop subdivision with creases) |
| Skeleton + weights from vertex frames | Done, automatic, about 6 mm average error |
| One rigged mesh with 25 layered clips | Done (.blend / .fbx / .glb) |
| PBR maps from the painted texture | Done, automatic first pass |
| Side-by-side web viewer | Done (artifact + `remaster/viewer`) |
| Unity import test | Not tried yet |
| Any other model | Not started |

## What the automated pass can't do (and the fix for each)

| Limit | Why | Fix |
|---|---|---|
| No new texture detail | upscaling only sharpens 256 painted pixels | AI upscaler for reference detail, then repaint or Substance |
| Shapes only rounder | subdivision smooths the 1999 cage | sculpt a high-poly over it, bake to a new game mesh |
| Stiff elbows / knees | old topology has no edge loops at joints | retopology with joint loops, re-skin with the recovered bones as the start |
| Some material guesses wrong | classes come from color alone | hand-paint an ID map per character (an hour or two each) |
| 15-25 fps animation | original keyframe rate | clean up at 60 fps, add IK foot locking, or retarget modern clips |

## Phases

### Phase 1: run the pipeline on every player model
The base game has 23 player models (anarki, biker, bitterman, bones, crash, doom, grunt, hunter, keel, klesk, lucy, major, mynx, orbb, ranger, razor, sarge, slash, sorlag, tankjr, uriel, visor, xaero).
- Script extraction of all of `models/players/*` (plus Team Arena's if it's installed).
- Generalize the scripts to take a model name; build a batch runner and a per-model report (fit error, bone count, class shares).
- Expect to tune the bone counts per model: Orbb (a ball), Sorlag and Klesk (non-human) need different counts and naming.
- Include red/blue team skins and the `_1` / `_2` LOD meshes.
- Extend the viewer into a gallery with a model picker.

### Phase 2: raise the automatic quality
- Local AI upscaler (Real-ESRGAN or similar) in place of Lanczos for the texture step.
- Per-model ID map (painted once by hand) to replace color classification.
- Skin shading (subsurface) for faces; separate eye/emissive handling.
- Bake geometric AO and curvature from the smoothed mesh in Blender, combined with the painted AO.
- Unity URP material set-up script (repack ORM, create materials, set up Generic rig + avatar masks + Animator layers).

### Phase 3: one fully hand-finished hero character
Pick one (Keel is the obvious choice) and take it the whole way, to measure the real effort per character:
- High-poly sculpt over the remaster silhouette (ZBrush or Blender).
- Retopology to a 20-40k triangle game mesh with joint loops.
- Bake normal / AO / curvature from the sculpt; texture in Substance Painter with the ID map.
- Map the skeleton to a Unity Humanoid avatar; keep the recovered 1999 clips as reference and add modern motion (the GAME project already has the Universal Animation Library).

### Phase 4: weapons, items, maps
- Weapon MD3s attach at `tag_weapon` (already a bone in the rig). Same mesh + PBR pipeline; they have no skeletal animation, which makes them easier.
- Items and powerups: same.
- Maps: the GAME project already imports BSPs and id Tech 3 texture packs (see the bsp-batch-import and texture-pack-import notes). The PBR texture step can run on map textures too; shader scripts (`.shader`) carry the extra layers.

### Phase 5: pick the delivery engine
Two realistic routes:
1. **ioquake3 / Quake3e with the OpenGL2 renderer.** It already supports normal maps (`_n`) and specular maps (`_s`) in a pk3, so remastered textures can ship as a texture-pack mod with the original game logic untouched. Characters would need to go back to MD3 (or IQM, which ioq3 supports and which has real skeletons).
2. **A Unity remaster** (the URP + FishNet stack from GAME). Full modern rendering and the rigged FBXs drop straight in, but the game logic has to be rebuilt. This must be a separate project from OZMIUM, which can't contain Quake naming or id assets.

## Licensing: the real constraint
id's art can't be redistributed. Fan remasters handle this with **"bring your own pak0"**: ship only the tools, and have them convert the player's own legally owned `pak0.pk3` on their machine at install time. Every script here already works that way: it reads the original files and writes new ones, with nothing of id's baked in. Anything original (new sculpts, new textures painted from scratch) is yours to ship.

## Open decisions for Ozmium
1. Which engine route (ioq3 texture-pack mod vs. separate Unity remaster)?
2. Hand-finish one hero character first (Phase 3), or batch everything automatically first (Phase 1)?
3. Is a public release a goal? If so, the tools-only "bring your own pak0" approach decides how everything gets packaged from here on.

## Resuming with Claude
Open this folder and point Claude at `README.md` and this file. The condensed session notes are in `notes/claude_memory_note.md`.
