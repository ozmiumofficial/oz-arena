# The railgun, rebuilt

Tripo's railgun modeled again from scratch in Blender, the way the rocket launcher was (`../rebuild`): clean
hard-surface parts measured off the Tripo model (kept as the reference), baked to 4K, shipped as
`build/engine/oz/pak_oz_remodeled_railgun.pk3` in place of the sealed and retextured Tripo mesh that
`tools/weapon_swap` (`build_weapon.py railgun`) made. 45 closed parts, every face pointing out; the game mesh is
33.3k triangles. The red parts still take the player's rail color: the light strips pulse, the energy cells and
the vents breathe, and the cells keep Tripo's own veins.

`PARTS.md` is the part inventory written before modeling: each part's function, how a real one is built, its
details and joints, the three detail levels, the triangle budget.

Work files (not in git): `build/railgun-pbr/rebuild/` in the main checkout: `railgun_rebuild.blend` (collections
Reference, Rebuild, High, Final), `study/` (measurements), `look/`, `sweep*/` (comparison renders), `bake/` (bakes),
`game/` (the IQM and the maps the pak is made from). The committed `railgun_rebuild.blend` here holds the Rebuild and
Final collections only (`save_clean.py`).

## Steps

1. **Reference and measuring.** `study.py` (Blender, headless) imports Tripo's FBX and turns it into the work frame
   (x along the gun, muzzle at -x, y the gun's right, z up: the rocket launcher's frame, so the toolkit's planes,
   lathes and mirror hold), writes `ref_mesh.npz` and orthographic study renders with `views.json`.
   `profile.py` prints the reference's exact cross-sections as numbers (chained, simplified outlines);
   `gridcrop.py` cuts a study render to a region with a measured grid.
2. **Model** (`rg_lib.py`, `rg_parts.py`, run inside the live Blender): profile prisms, lathes, chamfered slabs,
   booleans of a side profile with a front profile, smooth spline outlines, lofts (the grip: egg-shaped sections,
   finger grooves, palm swell, thumb rest), sweeps (the trigger). Channels and inlays follow an extruded part's own
   surface (`channel()`, `inlay()`: the part's skin crossed with a stroke drawn in the side view), so the light
   strips sit sunk in grooves on curved flanks; raised plates are the same skin grown outward. Every part gets an
   angle bevel and area-weighted normals; silhouettes that should read round get a wider rounded bevel (`rim=`);
   `part()` checks its own result and falls back to sharp mitres where Blender's bevel throws vertices off.
   Booleans apply one cutter at a time (stacked Boolean modifiers left open seams in Blender 5.2).
3. **High-detail copy** (`rg_high.py`): the same parts with seams cut in and shader bump for the carbon weave,
   grip stipple and back-strap ribs, recoil-pad ribs, collar knurling and the head's scale skin.
4. **Finalize** (`rg_finalize.py`): modifiers applied, seams per part, angle-based unwrap, joined into `railgun`,
   triangulated (the bake and the game share one triangulation), zero-area slivers dissolved, islands at one texel
   density (about 1,900 texels per unit at 4K) with the energy cells at 1.7 times that, packed.
5. **Bake** (`rg_bake.py`, headless Cycles): `helpers` = normal (high copy to game mesh), AO, edge mask, material
   ids; `refcolor` = Tripo's color cast onto the rebuild (the cells' veins); `rg_post.py` cleans the normal map and
   folds its grooves into AO and edges; `pbr` = base color, roughness, metalness from `rg_materials.py` (a clean
   finish measured on Tripo's texture); `rg_post.py pack` writes what `tools/weapon_swap/pack.py` reads: the red
   parts gray at their brightness, `tint.png`, `glow_trim.png`, `glow_cell.png`.
6. **Game files**: `rg_dump.py` (corners with the bake's MikkTSpace tangents), then `tools/weapon_swap/export_iqm.py`
   with `railgun.json` (this folder: the work frame's axes, grip and the rebuilt nozzle as `tag_flash`) and
   `tools/weapon_swap/pack.py` into the pak.
7. **Check**: `rg_look.py` + `grid.py` / `pairs.py` (the rebuild beside the reference from the same cameras, every
   region from eight sides), `rg_lib.audit()` (open and non-manifold edges, signed volume),
   `shots_railgun.ps1` (the built game: first person, close third person from six sides, the pickup).

## Live viewing

`live_start.py` opens Blender's window with the BlenderMCP add-on's server on port 9877 (9876 belongs to the rocket
launcher session) and the reference as a ghost drawn 2 mm inside its surface (so it shows where the rebuild falls
short rather than fighting its faces). `bl.py` sends a script or snippet to it (`--shot` saves the viewport).
