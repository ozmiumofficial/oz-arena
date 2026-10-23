---
name: keel-remaster-pipeline
description: "2026-10-22 id Tech 3 player remaster study (Keel) — MD3 -> subdivided skinned mesh + recovered skeleton + PBR maps, scripts in Documents/Keel/remaster, artifact viewer"
metadata:
  node_type: memory
  type: project
  originSessionId: aea43c0f-99cb-41fc-ba65-ae394203ea8d
  modified: 2026-10-22T20:40:37.578Z
---

Ozmium owns Quake III Arena on Steam (`C:\Program Files (x86)\Steam\steamapps\common\Quake 3 Arena\baseq3\pak0.pk3`); Keel was extracted to `Documents/Keel/q3_original`. A plain MD3->FBX (shape-key frames) is in `Documents/Keel/fbx`. Remaster study built 2026-10-22, scripts in `Documents/Keel/remaster/`, run in order: `build_mesh_data.py` (weld seam dups, 2x Loop subdivision with 50 deg creases as a sparse matrix), `fit_rig.py` (skinning decomposition: 7 leg bones / 10 torso bones fitted on lower/upper frames separately, 12 k-means restarts, 5.4 / 6.5 mm RMS; side-chain naming), `build_rigged.py` (Blender: one mesh, 21 bones incl. waist/head/weapon tags, 25 actions; leg clips key only leg bones so they layer), `make_pbr.py` (pad, 8x upscale, color classes, de-light into AO, height->normal, wear, ORM, emissive). Deliverables in `remaster/output`, viewer copy in `remaster/viewer`. Artifact: https://claude.ai/artifact/8idHYcHtmEor4qy1ZbvWbT

Gotchas: MD3 header counts start at byte 76 (flags first); MD3 winding is opposite its normals; this OpenCV build can't read TGA (use PIL); glTF exporter in ACTIONS mode keys every bone in every clip, so filter tracks per half in three.js; artifacts don't serve .bin/.glb, ship base64 JSON / glTF JSON; maps are OpenGL-style, so three.js with flipY=false needs normalScale.y = -1.

**Why:** Ozmium wants to explore a modern-standards Q3 remaster workflow; id's art is reference only, never shipped in OZMIUM (see [[quake3-player-setup]] no-Quake-naming rule).
**How to apply:** reuse the scripts for other player models; keep outputs in Documents per [[feedback-working-files-not-in-assets]].
