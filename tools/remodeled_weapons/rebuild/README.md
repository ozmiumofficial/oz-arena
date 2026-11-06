# The rocket launcher, rebuilt

Tripo's rocket launcher modeled again from scratch in Blender: clean hard-surface parts measured off the Tripo model
(kept as the reference), retextured with procedural PBR materials, baked to 4K. Tripo's mesh was triangle soup with
inside-out parts and holes; this one is 49 closed parts, every face pointing out: 37,582 triangles, one 4K
basecolor / normal / ORM set.

Work files (not in git): `build/rocket-launcher-pbr/v3-rebuild/` in the main checkout: `rocket_launcher_rebuild.blend`
(collections Reference, Rebuild, High, Final), `bake7/` (the final bakes), `textures/` (the game maps), `study/` (measurements),
`cmp*/` (comparison renders). The committed `rocket_launcher_rebuild.blend` beside this file holds the Rebuild and Final
collections only.

## Steps

1. **Measure** (`sections.py`, `gridcrop.py`, `slab_outline.py`, `color_outline.py`): orthographic study renders of the
   reference with a mm grid, cross-sections at many stations, side silhouettes of y-slabs, paint-region outlines.
   The barrel axis is y 0, z 0.2206; barrel radius 0.0592, bore 0.046.
2. **Model** (`rl_lib.py`, `rl_parts.py`, run inside Blender): profile extrusions (side, front or top outlines, with
   holes), lathes, chamfered slabs, booleans of a side profile with a front profile, hex bolts, tubes along smoothed
   paths, tapers. Each part gets an angle-limited bevel and area-weighted normals; outlines that should read round
   (collars, plates, the red body, stock and guard flanks) get a wider weighted bevel. Side parts are built on the left
   and mirrored.
3. **High-detail copy** (`rl_high.py`): the same parts, finished, with ring grooves, face rings, seams and slots cut
   in, and shader bump for knurling (barrel), ribs (grip panels, stock pad) and stipple (rubber).
4. **Finalize** (`rl_finalize.py`): modifiers applied, zero-area faces dissolved, seams marked per part (caps and sides
   of each part's extrusion axis, plus one cut per band or holed piece so every island is a disc), angle-based unwrap,
   joined into `rocket_launcher`, islands at one texel density (about 2,000 texels per unit at 4K) and packed.
5. **Bake** (`rl_bake.py`, headless Cycles): `helpers` = normal (high copy to game mesh, tangent space), AO, edge
   mask, material ids; `rl_post.py` folds the normal map's grooves into AO (grime) and edges (wear); `pbr` = base
   color, roughness, metalness from `rl_materials.py`'s procedural materials (red paint, gunmetal, steel, dark metal,
   rubber, teal glass, measured on the v2-sharp textures; bare-steel wear on convex edges, grime in crevices);
   `rl_post.py pack` writes `rocket_launcher_{basecolor,normal,orm}.png`. `bake_all.sh <folder>` runs all of
   that from the work folder; `bake_pbr_only.sh <folder>` redoes just the PBR channels after a material change.
6. **Game files**: `../dump_final.py` + `../rocketl_to_game.py` into `build/engine/oz/pak_oz_remodeled_weapons.pk3`.
   The model keeps Tripo's space and scale, so the tool's grip constants hold; the muzzle flash now sits on the bore.
7. **Check**: `compare.py` (reference vs rebuild from the same cameras in flat color) and `render_pbr.py` (lit, both
   with their textures), `sheet.py` for side-by-side sheets, the closed-mesh audit, `shots_rocketl.ps1` (the built
   game: first person with Tony's gun settings and the defaults, third person from six sides, the pickup).

Watching it live: Blender with the BlenderMCP add-on's server (port 9876), driven by small scripts sent as
`execute_code`; each part appears in the viewport next to the reference ghost as it's built.

## Lessons (for the next weapon)

Build each part as what it is (grip: a loft of rounded sections with palm swell and thumb rest; trigger: a swept
blade; guard: a rounded loop; magazine: seated in a magwell with a floor plate), give lower sections angled facets,
round the painted shells' silhouettes, plan the detail levels before modeling, and check close-ups from below and
behind against the reference before showing anyone. The high-detail bake picks up other parts wherever one part runs
inside another, so `rl_post.py` keeps only the normal map's fine detail.
