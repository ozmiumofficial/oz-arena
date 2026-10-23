# Map editor scene: plan (approved by Ozmium, 2026-10-23, with every recommended choice below)

The built-in editor from PLAN.md "Built-in map editor", rebuilt from OZMIUM's (`C:\gAME\GAME`, Unity). It replaces the stub in `scenes/map_editor/map_editor.tscn`. Starting point on the OZMIUM side: `Docs/MapEditor-Handoff-2026-09-04.md`.

## What OZMIUM's editor is (surveyed 2026-10-23)

About 14-16k lines for the editor itself, 20k with bots, sync and importers.

| Part | OZMIUM files | Lines |
|---|---|---|
| Geometry kernel: planes are the truth, double-precision winding clip, convex hull, CSG, Bezier patches, meshing | `Scripts/Maps/Brush*.cs`, `BrushCsg.cs`, `Patch*.cs` | 1,800 |
| Map runtime: file I/O, chunked meshes (16 m cells, dirty rebuild), trace world, loaders | rest of `Scripts/Maps` | 3,500 |
| Editor core: `MapDocument` (selection, groups, hide/lock, snapshot undo), `MapEditorController` (one `Execute(command)`), scene view, picking (analytic, no physics), gizmos, keymap | `Scripts/MapEditor` | 6,200 |
| Tools: create (B), select, face (T), vertex (V), entity (P/J/K/I/O/N), light (L), prefab | `MapEditor/Tools` | 2,300 |
| UI: React (OneJS) TopBar, Explorer (materials, entities, prefabs, lights, skies, map, files), Inspector, Outliner, dialogs, F1 sheet | `Scenes/MapEditor/UI/~` + `EditorUiBridge.cs` | 3,700 |
| Collaboration: host-authoritative object-diff sync at 20 Hz, soft locks, presence | `Net/Sync`, `Net/Presence` | 1,500 |
| Bot nav bake, BSP importer, Workshop, tests | | 10,000 |

**Where the spec in PLAN.md is out of date** (OZMIUM changed since):
- The perimeter groove and fill modes, and the X drag-box subtract, were removed on 2026-09-13. Only Ctrl+Shift+X CSG subtract is left.
- Play-test is **F5**. F6 returns to the editor, F7 toggles markers, F8 dev materials, F3 a third-person picture-in-picture.
- There is no autosave timer. `_autosave` is written on play-test and on leaving.
- OZMIUM has no text `.map` importer. Radiant `.map` import and export are new work here.

## What changes for Quake 3

OZMIUM's game runs on its own trace world and nav. Ours runs id's code in q3core, which only plays compiled `.bsp` files. That shapes the whole port:

- **Play-test compiles the map.** Editor document -> `.map` -> **q3map** (BSP stage only, about a second for an arena) -> `user://q3/baseq3/maps/<name>.bsp` -> `spmap`/`devmap` in q3core. Same pmove, same collision, same weapons as a real match. id's q3map and bspc are already in the pinned `id-q3a` source (`q3map/`, `code/bspc/`, GPL).
- **No trace world port.** Editor picking is analytic (as in OZMIUM); play uses q3core. That drops about 400 lines of OZMIUM and any risk of the feel drifting.
- **"Bake bots" is bspc.** The compiled BSP goes through bspc to an `.aas`, which id's botlib reads. OZMIUM's 2,500-line nav baker isn't ported.
- **Entities are id's classes** with key/value pairs, so imported maps keep everything (movers, triggers, targets) even where the editor has no special tool yet. The friendly tools (spawn, jump pad, teleporter, pickups, speakers, lights) write the same classes.
- **Materials** are the 1,415 world materials and 49 skies already in `library/world`, by Quake 3 shader name.

## Phases

Hours of Claude working time, scaled to this project's pace. Each phase ends with self-test checks and a capture in the live editor.

| # | Phase | What it delivers | Hours |
|---|---|---|---|
| E0 | **Kernel and document** | Brushes as planes (double precision), winding clip, convex hull, CSG subtract, patches (biquadratic, bevel/curve/cylinder/arch); per-material meshing in 16 m chunks with dirty rebuild; the map document (ids, selection, groups, hide/lock, snapshot undo with shared unchanged objects); the save format; geometry self-test (OZMIUM's 547-line suite as the checklist) | 3 |
| E1 | **Viewport and core editing** | Scene shell: fly camera, grid and snap, analytic picking, gizmos; select, box select, move, nudge, rotate (R exact 90, Ctrl+R 15), mirror, duplicate/copy/paste, group, hide/lock/isolate; box create (B); the keymap table with user overrides and the F1 sheet; Map editor wired from the main menu, with leave-to-menu and the unsaved check | 4 |
| E2 | **Face, vertex, CSG, curves** | Face tool (T): push/pull, paint, eyedropper, UV shift/scale/rotate, align; vertex tool (V): hull rebuild, delete corners for wedges, Shift+V snap; Ctrl+Shift+X subtract with inherited faces; patch creation and control-net editing | 3.5 |
| E3 | **Texturing and materials** | Material browser over `world_materials.json` with thumbnails, favorites and recents, skies tab; per-face UV axes, texture lock (Ctrl+T) carried through moves/rotates/mirrors; per-face tint; real-time preview lighting | 2.5 |
| E4 | **Entities, lights, prefabs** | Entities tab with rendered tiles for every pickup (the catalog's renders); P/J/K/I/O/N/L tools; jump-pad arcs drawn with id's trajectory; key/value inspector for any id class; thumbnail camera; prefabs with thumbnails (Ctrl+P) | 2.5 |
| E5 | **Compile, play-test, bots** | q3map and bspc built by `build_q3core.py` as separate executables (a compiler `exit()` or crash can't take the game down); `.map` export; F5 compile and play in q3core with the real HUD, F6 back to the same camera and selection, F7/F8/F3; Bake bots; compile errors (leaks with the leak line drawn) shown in the editor | 4 |
| E6 | **Import and export** | BSP import from any pak (q3core's file system): brush/side/plane lumps to editable brushes, patches from surfaces, OZMIUM's least-squares UV fit, every entity; Radiant `.map` import and export (GtkRadiant, NetRadiant, TrenchBroom round trips); batch audit over all 35 stock maps (every drawn surface has an aligned face) | 3 |
| E7 | **Workflow and polish** | Save/Open/Recent, thumbnails, `_autosave`, Publish; the status bar, toasts, outliner, history panel; perf pass on the biggest import (q3dm12-sized) | 1.5 |
| E8 | **Tests and docs** | Headless smoke suite (OZMIUM's 189 checks as the list) and input suite (84) as `tests/map_editor_*.tscn`; captures; README, CLAUDE.md, PLAN.md | 2 |
| | **Total** | | **about 26** |

**Later, after match multiplayer exists:** collaborative editing over the same WebRTC connections (OZMIUM's host-authoritative object diffs, soft locks, presence), about 4 h; the Workshop on Cloudflare R2, about 2 h.

Order: E0 -> E1 -> E2/E3/E4 (any order) -> E5 -> E6 -> E7 -> E8. The first usable point is after E1 plus E3 (build and texture a room); the first playable point is after E5.

## Progress

**E0 done (2026-10-23): kernel, document, meshing.**
- **Kernel** (`tools/q3core/src/map/brush_kernel.*`, plain C++ in doubles): planes are the truth (n . p = d, n out). It does:
  - Winding clip with q3map's base winding; points on axial planes are put exactly on them, and near-integers snap, so axial brushes stay on integers.
  - Validity checks, corners, bounds, the convex hull (vertex tool), affine transforms (planes by M^-T, so mirrors stay outward), split, and CSG subtract. Cut faces take the attributes of the most nearly parallel target face.
  - Ray picking, Quake 3's default texture axes, face tangents, and biquadratic patches subdivided to a chord tolerance.
- **Godot face:** `Q3MapGeo` in q3core (`src/q3mapgeo.*`) takes and returns PackedFloat64Arrays in id space. Its mesh builders return Godot surface arrays (Godot axes, meters, Godot's clockwise front faces).
- **Document** (`scripts/map/q3_map_doc.gd`, `Q3MapDoc`):
  - One id pool for brushes, patches and entities. Stored objects are never changed in place, so an undo snapshot is a shallow copy that shares every unchanged object (100 steps).
  - Faces are either Quake 3 world projection (`tex`: shift, rotate, scale, through q3map's QuakeTextureVecs) or explicit texel axes (`uv`). Texture lock turns a face explicit and carries it through moves, rotations and mirrors.
  - Entities keep id's key/values as they are.
- **File:** `<name>.q3map.json`, written atomically. Number lists stay readable JSON when they're all whole numbers (axial brushes). Otherwise they're stored as exact doubles (`"f64:"` + base64), because Godot's JSON parser can read a double back one bit off.
- **Mesher** (`scripts/map/q3_map_mesher.gd`, `Q3MapMesher`): 16 m cells, one surface per material, and only changed cells are rebuilt. A mesh per patch. Materials come from `world_materials.json` by full shader name (`Q3MapMaterials`).
- **Verified:**
  - `tests/map_geo_test.tscn`: 25 checks, also in the self-test, which now passes 64/64. They cover exact 90-degree rotations, hull/chamfer/wedge volumes, subtract (including a rotated cutter), texture lock through move, rotate and mirror, and an exact save/load round trip.
  - Our face tangents match Godot's own mikktspace tangents, sign included.
  - 2,000 brushes, a third of them rotated, mesh in about 100 ms.
  - Captures of `tests/map_geo_room.tscn` show a room with a subtracted doorway, a rotated pillar, a texture-locked block and a curved patch. They're in `work/_map_editor/`.
- **Confirmed:** Godot 4.7 can't bake LightmapGI in an exported game ("Lightmap baking functionality is only available in the editor", class reference). Player maps get q3map's light stage (decision 1).
- **Found along the way:**
  - `textures/base_wall/concrete4` is id's lava shader despite its name. New brushes default to `base_wall/concrete`.
  - The world shaders ignore vertex color, so per-face tint needs tint-aware shader variants (E3).
  - Patch facing follows du x dv for now. E6 checks it against the vertex normals of BSP patches.

## Decisions for Ozmium (decided 2026-10-23: every recommended choice)

1. **Lighting for player-made maps.** Godot can only bake LightmapGI in editor builds; an exported game can't (to confirm in E0). Ozmium's rule was "real-time lights in the editor, bake on save", so the bake needs a route:
   - **Recommended: q3map's own light stage** (`-light`, the same GPL source): Quake 3's lightmaps, the light model we already calibrated the Godot bakes against. The world shaders gain a BSP-lightmap input. About +1.5 h.
   - A custom Godot export template with the lightmapper compiled in: identical look to the stock maps, but we'd build Godot from source instead of the Steam build. About +3 h plus long builds.
   - Real-time only (SDFGI and shadows), no bake: simplest, but leaks in tight rooms and costs frame rate.
2. **Kernel language:**
   - **Recommended: C++ inside q3core** (it already builds with MSVC): double precision (Godot's Vector3 is 32-bit in the Steam build), and fast on 1,000-brush imports.
   - GDScript with our own double math: no rebuilds, slower.
3. **UI tech:**
   - **Recommended: native Godot Controls** themed like the web menu (PLAN.md's choice). The 3D viewport needs every mouse event, and the web view swallows input over its whole rectangle.
   - The web menu (React, so OZMIUM's TSX ports nearly line for line), needing a hole-punched web view or several, plus the capture limits in CLAUDE.md.
4. **Save format:** **recommended: our own JSON** (`<name>.q3map.json`: planes, per-face UV axes, tint, groups, editor camera, thumbnail, entities as id key/values), with `.map` as a lossless import/export. Or Radiant `.map` (Valve 220 UVs) as the native file, which can't hold tint, groups or prefabs.
5. **Keys:** follow OZMIUM as it is now (F5 play-test, F6 back), or PLAN.md's F6?
