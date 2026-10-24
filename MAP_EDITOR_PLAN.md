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
| Bot nav bake, BSP importer, tests (OZMIUM also had a Workshop; ours is planned as M9 in MULTIPLAYER_PLAN.md) | | 10,000 |

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

**Later, after match multiplayer exists:** collaborative editing over the same WebRTC connections (OZMIUM's host-authoritative object diffs, soft locks, presence), about 4 h. (A Workshop for sharing maps is planned as M9 in `MULTIPLAYER_PLAN.md`, on Cloudflare R2.)

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

**E1 done (2026-10-23): the Map editor scene opens from the main menu and edits brushes.**
- **Scene** (`scenes/map_editor/`): `map_editor.gd` (`Q3MapEditor`) owns the document, camera, grid, tools and selection. Every command runs through `execute(cmd)`, which the keymap, the toolbar and the tests all call. It opens the last map (`user://map_editor/recent.json`), or a new map with a 1,024-unit floor. Maps live in `user://maps/<name>.q3map.json`, and leaving writes `_autosave`.
- **View:**
  - Fly camera as OZMIUM's: hold the right mouse button to look, W A S D and Q/E (Shift fast, Ctrl slow), the wheel dollies, F frames the selection, Home resets.
  - Ground grid with the X axis in red and Y in green; 1 to 256 units with keys 1-9, `[` `]` or Ctrl+wheel.
  - A preview sun with shadows.
  - The selection is drawn on its own (real materials, an orange tint, its edges, faint through walls), so a drag moves one node instead of rebuilding cells. The brush under the cursor gets a hover outline.
- **Tools** (`scenes/map_editor/tools/`):
  - Select: click (a group takes the whole group; double-click or Ctrl reaches inside), Shift adds, Ctrl toggles, and drag on empty space box selects. A drag moves on the grid, landing the selection's min corner on it (Alt vertical, Ctrl no snap).
  - Create brush (B): drag a footprint on any surface or the grid, set the height with the mouse (it remembers the last one), then click or press Enter.
- **Commands:** undo/redo, delete, duplicate, copy/paste at the cursor, select all, rotate (R 90°, Ctrl+R 15°, about the grid-snapped center), mirror on three axes, nudges along the camera's nearest axis (arrows, Page Up/Down), hide/unhide, lock/unlock, isolate, group/ungroup, and CSG subtract (Ctrl+Shift+X, through every overlapping brush, as one undo step).
- **Keymap** (`q3_editor_keymap.gd`): OZMIUM's table with id axes; rebinds save to `user://map_editor/keymap.json`. The F1 sheet is drawn from the table, with click-to-rebind, conflicts in red, and Reset.
- **UI** (`q3_editor_ui.gd`): native controls in the menu's theme. It has a top bar (file, tools, undo/redo, grid, keys, main menu), a status bar (tool hint, selection size, cursor position, map name with `*` when unsaved) and toasts. The dialogs are unsaved changes (Save / Don't save / Cancel), Save as and Open.
- **Verified:**
  - `tests/map_editor_smoke.tscn`: 27 checks, headless, in a scratch folder. It runs the real scene through tool calls at screen points and key events, covering everything above plus save/new/open, rebinding, and the unsaved check on leaving.
  - The self-test still passes 64/64.
  - Captures in `work/_map_editor/` (`editor_main`, `editor_sheet`, `editor_dialog`) through `tests/map_editor_shot.tscn`.
  - All 17 editor scripts compile from disk in the live editor with no console errors.
- **Not yet:** the face, vertex and patch tools (E2), materials and the inspector (E3), and entities (E4). Selecting and moving entities and patches works in code, but nothing creates them yet.

**E2 done (2026-10-23): faces, vertices, curves.** (CSG subtract was already in E1.)
- **Face tool (T, `tools/q3_face_tool.gd`):**
  - Click picks faces (Shift adds, Ctrl toggles); they're tinted cyan and outlined.
  - Alt+click is the eyedropper: the face's material and alignment go onto the selected faces, or the material becomes the current one.
  - Dragging a face pushes or pulls it along its normal on the grid (all selected faces, each along its own normal), stopping before a brush would turn inside out.
  - Arrows shift the texture by the grid; Ctrl+Up/Down scales it (×1.1), Ctrl+Left/Right rotates it 15°. World-projected faces change their shift/rotate/scale, explicit ones their axes.
- **Texture lock (Ctrl+T):** on the selected faces, or every face of the selected brushes. Locking turns a face's mapping explicit; Ctrl+T again frees it.
- **Vertex tool (V, `tools/q3_vertex_tool.gd`):**
  - Square handles (2D, drawn on the UI's canvas) on the selected brushes' corners and the selected patches' control points.
  - Drags run on the axis plane that faces the camera best (Alt vertical, Ctrl no snap); arrows nudge the selected handles.
  - Brushes are rebuilt as the convex hull (`Q3MapEditor.rebuild_from_points`), and each new face takes the old face pointing the same way. Moved corners stay selected.
  - Delete removes corners: one gives a chamfer, two on an edge a wedge. Refused if a brush would flatten.
  - Shift+V vertex snap lands a dragged corner on another brush's corner within 14 px. Ctrl+Shift+G snaps corners to the grid.
- **Curves (top bar > CURVES, `scripts/map/q3_patch_factory.gd`):**
  - Bevel (3×3), curve (5×3), cylinder (9×3) and arch (5×3), as OZMIUM's PatchFactory builds them, with texture coordinates by distance at Quake 3's scale.
  - Placed at the cursor (radius 64, height 128) facing outward, selected, in the vertex tool.
  - Patches can be picked (ray against the cached tessellation, `Q3MapDoc.patch_tess`), box selected, moved, duplicated and undone like brushes.
  - Quake 3 culls their back faces, so the editor draws backs as a dim see-through shell; curves stay findable from behind.
- **Verified:**
  - `tests/map_editor_smoke.tscn`: 40 checks (13 new), covering face picking, push/pull, texture shift/scale/rotate, the eyedropper, Ctrl+T, corner drag, chamfer and wedge, grid snap, each curve's shape and facing, picking a patch, dragging a control point, and undoing all of it.
  - The self-test passes 64/64, and the live editor's console stays clean.
  - Captures: `work/_map_editor/editor_e2_vertex_0.png` and `editor_e2_face_0.png`.
- **Next (E3):** the material browser and inspector, so faces can take any of the 1,415 remastered materials. Today only the eyedropper and the default material set them.

**E3 done (2026-10-23): materials, tint, the inspector.**
- **Screen layout:** containers, not fixed offsets. The top bar, then the browser | the open 3D view | the inspector, then the status bar. Tab hides or shows both panels.
- **Material browser** (left, `q3_editor_browser.gd`):
  - Three tabs. Materials holds 1,469 surfaces and liquids, filtered by folder (base_wall, gothic_block …) plus ★ Favorites and Recent, with search. Skies holds 49. Tools holds the 26 q3map tool shaders and 46 fogs.
  - Thumbnails are the remastered albedo, read from each material file without loading its other maps. They load only as tiles scroll into view (8 a frame).
  - A click makes a material current and paints it onto the face tool's faces, or every face of the selected brushes and patches. Right-click toggles a favorite.
  - Favorites and recents are kept in `user://map_editor/materials.json`.
- **Material kinds** (`Q3MapMaterials`):
  - Sky faces show the map's sky through the brush (`editor_sky_face.gdshader`, the same direction mappings as the cloud and panorama sky shaders).
  - Tool shaders take Radiant's colors: caulk solid pink; clip, trigger, hint and the other tools see-through.
  - Fogs are see-through boxes in the fog's color. Unshipped names stay out.
- **Tint:**
  - A `tint` uniform (white by default, so stock maps are unchanged) in `world_surface.gdshaderinc` and `liquid.gdshader`.
  - A tinted face draws with a cached tinted copy of its material; the mesher keys its surfaces by material and tint.
- **Inspector** (right, `q3_editor_inspector.gd`):
  - What's selected; the material (thumbnail, Paint current, Pick); and the tint (applied when the picker closes, so one undo step).
  - Texture mapping: shift, rotate and scale fields for world-projected faces, plus Fit, Left, Right, Top, Bottom, Center, Flip U, Flip V and Reset, all of which also work on explicit (locked or imported) mappings. A texture lock checkbox.
  - The selection's position (min corner, arrows step by the grid) and size, and a rotation by any angle.
- **Face tool:** Shift+T switches between single faces and whole objects (a brush, or its group). Leaving the face tool drops its picked faces, so painting from the select tool reaches whole brushes.
- **Verified:**
  - `tests/map_editor_smoke.tscn`: 51 checks (11 new), covering the browser counts and search, thumbnails loading only in view, painting faces and whole brushes, favorites and recents saved, tint, the inspector's fields, Fit and Right on world and explicit mappings, Reset, Shift+T, the sky and tool materials, Tab, and undo.
  - The self-test passes 64/64.
  - The capture `work/_map_editor/editor_e3_0.png` shows a cloud sky through a ceiling brush, caulk, clip, lava, a tinted block, and both panels.
  - The changed shaders compile in the live editor's renderer, and its console stays clean.
- **Next (E4):** entities (spawns, jump pads, teleporters, pickups, sounds, lights), and prefabs. The Entities and Prefabs tabs join the browser.

**E4 done (2026-10-23): entities, lights, prefabs.**
- **Entities are id's classes and key/values** (`Q3MapEntities` reads the catalog in `library/entities.json`: 35 items, 47 map classes). A brush entity (`func_*`, `trigger_*` except `trigger_always`) owns brushes through their `entity` field; everything else is a point with an `origin`.
- **Tools** (`q3_entity_tool.gd`, one tool with kinds), after OZMIUM's EntityTool and LightTool. Each shows a ghost of what it will place.
  - **P** spawn on the floor (origin 24 up, facing the camera's heading to 15°); **Shift+P** a spawn that puts the player's eye where the camera is (SelectSpawnPoint's 9-unit lift plus the 26-unit view height).
  - **J** jump pad: click the pad, then where to land. It makes a 64×64 bounce-pad brush, a `trigger_push` over it (a trigger-shader brush) and a `target_position`, grouped and linked by a fresh targetname. The target is the top of the arc, as AimAtTarget wants, placed so the flight comes down on the second click.
  - **K** teleporter: a `trigger_teleport` and its `misc_teleporter_dest` (facing the camera's way).
  - **I** weapons and **O** items (health, armor, ammo, powerups, holdables) on the floor; **,** and **.** step through them, or change the selected pickups.
  - **N** a looping `target_speaker`; **L** a light one grid step off the surface, **Shift+click** a spot aimed through an `info_null`, as q3map wants.
  - Clicking an entity with these tools selects it instead.
- **Drawing** (`q3_editor_entities.gd`): pickups are `Q3Pickup` (spinning and bobbing as in the game); misc_models are their prop; spawns are player-sized capsules; other classes are boxes in their class color with their name.
  - Lines show targets, facings and every jump pad's arc on id's trajectory. An arc falls on until it meets a brush; the landing test runs one arc a frame and is cached until brushes change.
  - Lights are real Godot lights with q3map's falloff (`tools/build_maps.py`'s numbers). **Shift+L** shows the map lit by its own lights only: preview sun off, worldspawn's ambient, the lights casting shadows.
- **Editing:** entities pick by their box (select tool and entity tools only), box-select, move, turn and mirror; "angle" follows turns and mirrors. Copies (duplicate, paste, prefabs) keep their links inside themselves: copied brushes follow their copied entity, and targetnames already in the map get new names with the copies' targets renamed. Deleting a brush entity takes its brushes; deleting its last brush takes the entity.
- **Browser:** two new tabs.
  - **Entities:** every pickup, its tile rendered by the thumbnail camera (cached in `user://map_editor/thumbs`), and every map class, by section with search. A point class arms the tool. A brush class turns the selected brushes into that entity, and triggers get the trigger shader; `trigger_push` and `trigger_teleport` with nothing selected start the J and K tools.
  - **Prefabs:** saved prefabs with their thumbnails; right-click deletes (after a confirm).
- **Inspector:** for entities, the class (switchable among pickups or spawn classes), angle, a light's intensity, color and spot radius, a speaker's sound with Listen and Looping, a jump pad's flight (launch speed, height, time to the top, distance), what targets what, and every key/value (edit, remove, add). With nothing selected it edits the map settings (worldspawn): message, music, gravity (which reshapes the arcs), ambient, and every key.
- **Prefabs** (`Q3MapPrefabs`, `q3_prefab_tool.gd`): **Ctrl+P** saves the selection, with the entities its brushes belong to, to `user://map_editor/prefabs/<name>.q3prefab.json` (a map file holding just those objects) plus a thumbnail. The prefab tool shows a ghost in real materials, bottom center on the cursor and corner on the grid. **R** / **Shift+R** turns it 90°, a click places a copy in one group, Esc returns to the select tool.
- **Thumbnail camera** (`Q3EditorThumbs`): a SubViewport with a world of its own, rendering one job at a time and framing on the triangles (the weapon props' own AABBs report about 1.7 m tall).
- **Verified:**
  - `tests/map_editor_smoke.tscn`: 74 checks (23 new). They cover every tool's placement and keys, entity picking, the pad's arc landing on the second click, the lights and their energy, Shift+L, Shift+P, move, turn and mirror, duplicates relinking, deletes, brush classes, the inspector's fields and flight readout, map settings, the Entities tab, prefabs saved, placed turned and deleted, save and reload, and undo.
  - The self-test passes 64/64.
  - Captures: `work/_map_editor/editor_e4_1_0.png` (entities, the jump pad selected with its flight), `editor_e4_lights_0.png` (lights only) and `editor_e4_prefab_0.png` (the Prefabs tab and the ghost).
  - The live editor reloads every changed script without new console errors.
- **Next (E5):** compile and play-test: q3map (BSP, vis, light) and bspc from the pinned source, F5 to play the map in Gameplay, F6 back.
- **After E4 (review, 2026-10-23):** a second agent reviewed the E4 code. Fixed:
  - Moves and turns rewrote up/down movers' "angle -1/-2".
  - Placed prefabs could merge two links.
  - The map's Message field showed blank.
  - Links to brush entities went to 0 0 0.
  - The jump pad's target wasn't part of the pad.
  - The landing came 10 units short (the player's origin rides 10 above the trigger's center).
  - Arcs could rise through a ceiling, a leak q3map catches (the top now stays 40 under the lowest brush above the path).
  - Spot lights rebuilt on every edit.
  - The selection's edges used one mesh surface per object, past Godot's 256-surface limit.

**E5 done (2026-10-23): compile and play-test.**
- **id's tools, 64-bit:** `tools/q3core/SConstruct.tools` builds `q3map.exe` and `bspc.exe` from the pinned id-q3a source. `build_q3core.py` runs it, copies them into the addon's `bin/tools`, and `export_game.py` ships them in `q3core/tools`. They run as separate programs.
  - `patches/id-q3a/0001-tools-64-bit-and-paks.patch`:
    - `_findfirst` handles kept in `int` (a 64-bit handle, cut: an invalid-parameter fast fail at startup);
    - a heap pointer aligned through `int` in the light stage;
    - the pak library's debug log, which wrote `c:\paklog.txt` on every run;
    - `Q3MAP_PAKPATH`, so textures come from the player's own paks.
  - Linked with a 16 MB stack (id used 4 MB; the 64-bit frames are bigger). bspc force-includes `float.h` ahead of `l_utils.h`'s `_X`/`_Y` macros.
- **The .map** (`Q3MapExport`): every brush in Radiant's brush-primitives form (`brushDef`), each face's texel axes as q3map's texture matrix in the ComputeAxisBase frame, on the plane snapped as q3map snaps it. The compiled map is mapped exactly as the editor shows it: the test compares every compiled vertex, within q3map's own 1/8-unit vertex rounding.
  - Plane points: the best-conditioned triple, ordered for q3map's (p0 − p1) × (p2 − p1).
  - Curves: `patchDef2`.
  - Brush models numbered as q3map's SetModelNumbers does: func_group merges into the world; origin and area-portal brushes don't count; a brush entity with nothing left is left out with a warning, since it would stop the game.
  - Limits checked with warnings: shader names over 63 characters, keys over 31, values over 1023, curves over 32 × 32.
- **The compile** (`Q3MapCompiler`): on a worker thread, in `user://map_editor/compile/quake3/baseq3/maps` (q3map wants a folder with "quake" in its name).
  - Shader scripts are unpacked once from the player's paks, with a `shaderlist.txt` written for them.
  - Stages: bsp, vis, light, aas. A leak still writes the BSP; vis, light and bspc are skipped, and q3map's leak line comes back.
  - The results are copied to `user://q3/baseq3/maps/ed_<name>.bsp` and `.aas`. The `ed_` prefix keeps a map called q3dm17 from hiding the real one.
- **Play-test (F5):**
  - It compiles bsp, plus aas when bots are on (Shift+F5 toggles them), then `Session.play_test` starts Gameplay as a free-for-all without limits.
  - Gameplay draws the map from the document (`Q3PlayMap`):
    - the world and one node per brush model through the editor's mesher, skipping q3map's tool shaders;
    - the spawn markers;
    - the map's lights with q3map's falloff and shadows, or the editor's preview light when it has none (an unlit Quake 3 map is full bright);
    - convex hulls for ragdolls and gibs.
  - q3core runs the compiled BSP, so collision, items, movers and bots are the real game.
  - **F7** shows the editor's entity markers over the game. **F6** (or Leave in the pause menu) goes back to the editor: the same document with its undo history, selection, view and tool.
  - A play-test isn't saved as the menu's last match.
- **Full compile (Ctrl+F5):** bsp, vis, light and aas, to check a map. Errors and warnings open a dialog with q3map's output (Copy log), and a leak draws q3map's leak line in red through everything.
- **Toolbar:** FILE (new, open, save, save as, leave) and PLAY menus; the bar fits a 1920-wide window.
- **Verified:**
  - `tests/map_compile_test.tscn` (9 checks): the export, the kit, all four stages, the BSP read back, brush model numbers, texture coordinates on world-projected, turned, texture-locked and nearly flat faces, q3core spawning the player while a bot moves around on the compiled area file, and a leak.
  - `tests/map_playtest_test.tscn` (6 checks): F5 into Gameplay with the map and 2 bots, F6 back with the same document, undo history, selection and view. Its windowed run (`shots=`) saved `work/_map_editor/e5/playtest.png`, `playtest_markers.png` and `editor_after.png`.
  - The editor smoke test passes 75/75.
- **Reviewed in parallel:** a second agent read the E5 code as it was written. Fixed after its review:
  - Movers with an origin were drawn offset; their mesh now sits back by the origin under the node Gameplay moves.
  - Editor-hidden objects (in the BSP) were invisible walls; play-tests draw them.
  - A play-test plays only the document that was compiled: edits during the compile ask for F5 again.
  - bspc wrote `bspc.log` into the game's folder; it now logs only where `BSPC_LOG` points.
  - Maps named with "quake" in them confused q3map's folder search; q3map now always compiles `editor_map.map`.
  - Copy failures, and stale shader scripts after a pak change, are now caught.
  - Leaving mid-compile is refused.
  - func_group brushes count as world for ragdolls and jump pad ceilings.
  - Isolate and face or vertex picks come back after F6.
- **Not yet:**
  - q3map's lightmaps are compiled but not drawn: play-tests light with real-time lights. The BSP-lightmap input for the world shaders comes with sharing maps.
  - F8 (dev materials) and F3 (a third-person inset) from OZMIUM are left out; F3 is ready-up in Gameplay.

**E6 done (2026-10-23): import and export.**
- **BSP import** (`scripts/map/q3_bsp_import.gd`, `Q3BspImport`): any map in the player's paks (`maps_in_paks`, later paks win) becomes an editable document.
  - Brushes come from the brush, brushside and plane lumps. Brush models keep their entity; their planes move back by the entity's origin.
  - **q3map2's bevels** are its "noshader" sides. A few lie a hair off a real side and shave a sliver off it, so a brush is taken without them whenever it still closes. Bevels on a real side's plane merge into it.
  - **Detail:** q3map keeps only the shader's contents, not the .map's detail flag. A structural brush's inside is a solid leaf (cluster −1), because only structural brushes cut the tree; a detail brush sits in an open leaf. So the leaf at a world brush's middle decides. q3dm17 gives 135 detail brushes. Recompiled, its tree has 784 nodes against id's 735, and 1,112 with every brush structural.
  - **Face mapping:** least squares of each drawn surface's st over its vertices on the side's plane, centered on the surface's middle.
    - Candidate surfaces are matched triangle by triangle, because newer q3maps merge coplanar sides into one surface. They're ranked by the share of their area inside the side, then whether another side already took them (two brushes' coincident sides each draw one, in brush order), then area.
    - Explicit axes become shift/rotate/scale when they have that form (round values first, exact ones next).
  - Curves come from patch surfaces, and every entity keeps its keys.
  - **The audit** (`tests/map_import_test.tscn`): every planar surface q3map drew must be reproduced by some imported face, a triangle at a time. That means the same model, shader and plane, the same facing (by vertex normal), the triangle's middle inside the face, and st equal apart from whole repeats. Fog, sky and zero-area triangles are left out, and T-junction slivers get half a unit of slack.
  - **All 36 maps in the paks pass:** 134,637 of 134,637 surfaces aligned, 69,588 brushes, none dropped, and every map round-trips through .map export and import. q3dm12, the biggest, imports in about 9 s headless. Report: `work/_map_editor/import_audit.json`.
- **.map import** (`Q3MapImport`): Quake 3 form (shift/rotate/scale), brush primitives (`brushDef`), Valve 220, `patchDef2` and `patchDef3`.
  - Planes come from the three points as q3map takes them; points in a line make no plane.
  - Face flags are kept; func_group brushes join the world.
  - A file that breaks off part way imports nothing and names the line.
- **.map export:** brushDef for GtkRadiant, NetRadiant and id's q3map, or Valve 220 for TrenchBroom (`mapversion 220`).
  - q3map maps a brush entity's faces at each point less the entity's origin (its origin brush's middle, else its `origin` key). The export writes them that way, and the import takes it back (`Q3MapExport.entity_origin` / `model_space`).
- **Editor:** FILE menu items:
  - **Import a Quake 3 map** (Ctrl+I, a list of the paks' maps by arena name).
  - **Import .map** (Ctrl+Shift+O).
  - **Export .map (Radiant)** (Ctrl+Shift+E) and **Export .map (TrenchBroom)** (Ctrl+Shift+Alt+E).

  Files go through the system's file picker. An import runs on a thread behind a dialog whose Cancel throws the result away. It opens as a new unsaved map, with the camera at the intermission view (facing its target) or the first spawn.
- **Verified:**
  - `tests/map_import_test.tscn` (37 checks, about 150 s).
  - `tests/map_format_test.tscn` (10 checks): round trips in both forms with every corner's texel coordinates, origin entities, detail, curves, func_group; hand-written samples of each form; a broken file.
  - `tests/map_compile_test.tscn` (12 checks, up from 9):
    - a mover with an origin, checked against q3map's own texture coordinates;
    - q3dm17 imported, exported and compiled again, with the same brushes, curves and entities, every vertex's texture coordinates as imported, and a tree as small as id's.
  - The editor smoke test passes 79/79, and the self-test 64/64.
  - Across the 36 maps, 9,614 of 69,588 brushes come in as detail.
  - Captures: `work/_map_editor/e6/import_q3dm17_0.png` and `import_q3dm6_0.png` (`tests/map_editor_shot.tscn` with `e6=<map>`): the imported map from its intermission point, with a face's imported alignment in the inspector. Tool brushes draw as Radiant draws them, so q3dm6's hint brushes fill the view in light blue; a filter to hide tool brushes belongs in E7.
- **Reviewed in parallel:** a second agent read the E6 code. Fixed after its review:
  - Detail was lost on import, so recompiled maps had every trim brush structural.
  - Brush entities with an origin compiled with their textures shifted.
  - A script error on the import thread left the editor stuck "importing"; the result now comes through `_process`, and a failure is reported.
  - A garbled .map imported as a success.
  - A compile could start during an import.
  - Points in a line made a real plane, which clipped the brush.
  - The intermission camera ignored its target.
  - The round-trip tests compared counts only; they now compare mappings and the tree size.
- **Kept as is:** a Quake 3 form file's shifts and rotation are read as floats, as the editors show them. id's q3map reads them with `atoi`, but q3map2 reads floats. func_group keys aren't kept.

**E7 done (2026-10-23): workflow and polish.** Surveyed against OZMIUM's editor first; where its code and handoff note disagreed, the code won.
- **Big maps** (`tests/map_editor_perf.tscn`, windowed, q3dm12's 5,673 objects):
  - **Nudging everything crashed the editor.** Each of 5,000 puts redrew the whole selection overlay, which is quadratic, and the visuals waiting to be freed ran Godot out of render IDs.
  - `Q3MapDoc.begin_batch()` / `end_batch()` now make one `changed` of many edits. Every command (`execute`), moves, pastes and the tools' drags are batched, and undo's change list is a dictionary.
  - Now: whole map in view 6.3 ms a frame, everything selected 7.4 ms, select / nudge / undo all 0.5-0.7 s, one brush nudged and redrawn 5 ms, save 0.26 s, open 0.55 s.
  - **Import:** each face looked at every drawn surface of its shader. A 256-unit grid per model and shader (`NEAR_CELL`) took q3dm12 from 10 s to about 1 s in the editor, where the audit is off. The full audit went from 150 s to 80 s.
- **View filters** (VIEW menu; Alt+1-6, Alt+0 shows all): caulk, tool brushes (clip, hint, triggers ...), sky, fog, curves, entities, as Radiant's filters. Filtered things aren't drawn, picked, box-selected or selected by Select all; compiles and play-tests keep them. q3dm6's hint brushes no longer fill the view. OZMIUM had no filters; this is new.
- **Status bar:**
  - the map counted by kind ("2081 brushes · 358 curves · 333 entities"), or the selection's kinds and size;
  - an amber notice while isolate or a filter hides something;
  - an imported map goes by its own name until saved.
- **Objects tab** (OZMIUM's Outliner, `q3_editor_outliner.gd`): groups first, folded, with their members, then every object in id order, 100 entries a page.
  - Rows show a name if set, else the kind, the id and the first material (or class and targetname).
  - Show and Lock boxes work per row or per group, each one undo step; Isolate, Unhide all and Unlock all sit above.
  - A filter matches name, class or material.
  - Click selects, double-click frames, right-click names the row. Names save with the map (`name` on any object); the .map export leaves them out.
- **History tab** (`q3_editor_history.gd`): Start and every undo step by label, the current one ▶, redo steps grey. A click jumps there in one batched change (`Q3MapDoc.step`), as OZMIUM's does. 100 steps are kept.
- **Files:**
  - **Recent maps:** up to 10, newest first, in `recent.json`.
  - **Open dialog:** Recent, then all maps, each with a thumbnail and date. The open map is marked, and the autosave is named as such. Delete asks first, then removes the file, its thumbnail and its recent entry.
  - **Thumbnails:** every save renders the view to `<name>.png` beside the map, 512 x 288 as OZMIUM's. A SubViewport shares the world; the grid, selection tint and entity markers move to layer 2 for the shot, so it shows the map as the game sees it.
- **Publish** (Ctrl+Shift+P, FILE menu; OZMIUM's "freeze for players"):
  1. It saves the map (asking for a name first).
  2. It refuses a map without a spawn point or brushes.
  3. It compiles bsp, vis, light and aas as `pub_<name>`, so play-tests (`ed_<name>`, bsp only) can't overwrite it. A leak or a missing bot file stops it with q3map's log.
  4. It lists the map in `user://maps/published.json` (`Q3MapPublish`) with its types (ffa, tourney and team; ctf when both flags are in), the thumbnail as a data: URL, its document and brush model numbers.

  `Session.maps` is the arenas plus published maps, so the map appears in Create Match and passes match validation. Gameplay draws it from its saved document. The web menu takes data: URLs for levelshots (`webui/lib/shot.ts`).
- **Verified:**
  - Smoke 88/88: filters, counts, the Objects tab, History, recent and delete, Open, Publish's refusal.
  - Compile 13/13: Publish end to end in the real editor.
  - Perf 6/6, format 10/10, import 37/37.
  - Captures in `work/_map_editor/e7/` (`tests/map_editor_shot.tscn` with `e7=objects|history|open|filters`).
- **Reviewed in parallel:** a second agent read the E7 code. Fixed after its review:
  - A published map drew from the live document, which could drift from its frozen .bsp (a door added after publishing drew as world, and its brush model numbers went wrong). Publish now writes a copy of the document as compiled, `pub_<name>.q3map.json`, beside the .bsp. The entry's name, types and thumbnail come from that moment too, not from whatever map is open when the compile ends.
  - A script error inside a command would have left the edit batch open and frozen the view. `execute` now survives it, and the editor closes any batch still open at the next frame.
  - A brush whose entry face is a filtered caulk face no longer takes the click.
  - The outliner's right-click rename never fired (`allow_rmb_select`).
  - Huge groups drew every member row: now the first 100, the selected ones, and a "… N more" row.
  - Toggling a box to what it already was made an empty undo step.
  - Two thumbnails could render at once and leave the grid on the hidden layer. The shot now shows the game's view (caulk and tool brushes off, sky and curves on) whatever the filters, and leaving waits for it.
  - A published map whose copy can't be read now refuses to start instead of playing undrawn. `Session.maps` refreshes on every return to the menu.
  - The committed menu dump could have carried this PC's published maps; it now points the registry at an empty file.
  - A published map without a thumbnail shows a dark plate in the menu instead of a broken image.
  - Paint and tint are batched, and two quick History clicks no longer mis-step.
- **Found along the way:**
  - The theme's ItemList focus box was 95% opaque and drawn over the items, so a focused list (the Open dialog) looked nearly black. It's border-only now.
  - The browser is a fixed 460 px with four tile columns, so switching tabs doesn't move the view.

**E8 done (2026-10-23): tests and docs. The map editor is finished**, apart from editing together online, which waits for multiplayer.
- **OZMIUM's lists, checked against ours:** its smoke test has 268 checks on a passing run (not 189) and its input test 85 (not 84). Each was compared with ours.
  - Its shadow-atlas budget, damage numbers, spawn choice, kill floor and Unity material checks don't apply: id's game code does spawning, items and damage here.
  - The rest were covered already or are new now.
- **`tests/map_editor_input.tscn`** (new, 46 checks, headless): the real editor driven only through `Input.parse_input_event`, as a mouse and keyboard deliver events, GUI first.
  - It covers tools by key, a brush made by drag and Enter, select, duplicate, nudge, undo, move by drag, multi-select, hide, rotate, the face tool, text focus, grid keys, the camera, filters, the F1 sheet, Save as, the Open dialog, the Undo button and the Objects tab.
  - Text focus: a click in the field focuses it, a click in the view gives the keys back, and keys typed in the view never reach the field.
  - The camera: W doesn't fly without the right button, right-drag looks, W flies while looking, and the wheel works in the view but not over panels.
  - Events go in window coordinates (`root.get_final_transform()`), since the stretch mode scales the window to the 1920 x 1080 viewport.
  - **It found a bug:** the wheel over a text field fell through to the 3D view and dollied the camera. The view now ignores the wheel whenever a panel control is under the pointer.
- **Smoke test: 96 checks** (8 more, from OZMIUM's list):
  - 30° + 60° lands where 90° does;
  - a mirrored brush keeps every face pointing out;
  - subtract with nothing selected does nothing and makes no undo step;
  - pressing Isolate again gives back the hidden set from before;
  - a pasted group is a new group under the same name (it got "Group N" before; fixed);
  - names, named groups and locks survive save and open;
  - History steps back two and forward one;
  - one material per tint, and tints survive the file.
- **Everything at the end of E8:**
  - smoke 96/96, input 46/46, compile 13/13, format 10/10, import 37/37 (all 36 stock maps), perf 6/6, self-test 64/64;
  - the live editor's console clean after a restart.
- **Captures** in `work/_map_editor/e8/`. Two are in README (`docs/screenshots/editor-*.jpg`).
- **Docs:** README lists the editor under planned features as built and coming in the next release (the open beta doesn't have it). PLAN.md marks it done. CLAUDE.md has the facts from every phase.

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
