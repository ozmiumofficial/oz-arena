# Remodeled weapons from Tripo exports (experimental)

High-detail weapons in place of id's, drawn with **Settings > Experimental > Remodeled weapons**
(`r_ozRemodeledWeapons 1`, applied at the next vid_restart): `RE_RegisterModel` (renderer2/tr_model.c) loads
`<model>_remodel.iqm` in place of a `models/weapons2` md3 when a pak has one, so it shows in the hand, in other
fighters' hands and on the floor. Off, everyone sees id's weapons. Each weapon built here ships in its own
`build/engine/oz/pak_oz_remodeled_<name>.pk3` (the rocket launcher's is `tools/remodeled_weapons`'
`pak_oz_remodeled_weapons.pk3`).

| Weapon | Replaces | Notes |
|---|---|---|
| `railgun` | `models/weapons2/railgun/railgun.md3` | red parts in the player's rail color; trim pulses, cells breathe |

The railgun that ships is now the rebuilt one (`tools/remodeled_weapons/rebuild_railgun`: modeled from scratch over
this Tripo model, 32k triangles, flat colors, packed with this folder's `export_iqm.py` and `pack.py` and that folder's
`railgun.json`). `build_weapon.py railgun` still makes the sealed Tripo version, into the same pak.

## Build

```
python tools/weapon_swap/build_weapon.py railgun
```

Blender 4.2+ (`BLENDER`, or `C:\Program Files\Blender Foundation\Blender 5.2\blender.exe`) and Python with numpy,
opencv-python, scipy, scikit-image, libigl and xatlas (`pip install --target <dir> ...`, then `PYTHONPATH=<dir>`).
`--from <step>` reruns the later steps only; `--work` and `--out` move the work folder (default
`build/weapon_swap/<name>`) and the pak. The DDS encoder (`tools/gpu_bc.py`) needs the engine's SDL2.dll: a worktree
without an engine build uses the main checkout's.

`weapons/<name>.json` holds the source (`sources/<name>/`, Tripo's FBX and texture), the model's axes (`forward`,
`up`), `scale`, `grip` (where the palm sits, in the model's units: it lands where id's weapons have it, 4.5 units
behind and 3 below `tag_weapon`), `tags` (`tag_flash`: the muzzle) and the surface/shader names.

## Steps

1. **Geometry.** `prep_orig.py` welds Tripo's mesh. `solid.py` builds a watertight surface without capping holes:
   inside is where the generalized winding number (libigl) is over 1/2, joined with the surface thickened by
   `thick` so Tripo's half-open parts stay; exact distances near the surface, marching cubes. Thin fins (anything
   thinner than about 0.1 game units sticking out) are cut and smoothed. Open-sheet slivers are dropped, and
   `drop_faces` leaves out faces found by eye (the railgun's flipped flap in its side vent). Capping holes first
   (the earlier way) stretched spiky membranes across tangled holes (Tony circled them in the cell cut-outs and the
   vent). `geo.py` decimates to `tris` (200k).
2. **UVs and bakes.** xatlas charts (`atlas.py`), then Tripo's color, the normal and AO from the 2.5M-triangle
   surface, and its curvature (pointiness: `bake_curv.py`).
3. **Texture** (`texture.py`), made to follow the shape: lettering, logos and painted highlights on flat parts are
   painted over; wear goes on the real convex edges and grime in the real grooves; Tripo's dark painted seams stay
   and are cut into the normal map; the red paint keeps crisp edges and is split into trim (thin streaks and
   outlines) and cells (the large marbled parts). The red parts are stored gray at their brightness, for the shader
   to color.
4. **Export** (`export_iqm.py`): id's axes and units, one still frame, joints `tag_weapon` and `tag_flash`, pieces
   under 999 vertices (SHADER_MAX_VERTEXES), triangles turned to id's clockwise front faces, t = 1 - v with the
   bitangent sign and the normal map's green flipped.
5. **Pack** (`pack.py`): BC7/BC5 DDS (`.dds`, `_n`, `_s` in rend2's specular layout, `_tint`, `_trim`, `_cell`) and
   `scripts/oz_remodeled_weapons_<name>.ozmtr`:
   - the painted stage, lit by rend2 with the normal and specular maps;
   - the red parts in the rail color: `rgbGen entity` (color1, as id's railgun's glow strips; the cgame dims it
     after a shot and brings it back over 1.5 s), blended `GL_DST_COLOR GL_ONE_MINUS_SRC_ALPHA`, which is
     dst * (1 - mask + mask * color);
   - the cells' glow and the trim's pulse: additive stages in the rail color whose strength is an `alphaGen wave
     sin`, above white for bloom (`ozGlow`, which also keeps them on rend2's generic program).

The idle sway in the hand and the pickup's spin and bob are the cgame's own (`CG_CalculateWeaponPosition`,
`CG_Item`), as for id's weapons.

## Checking

- `look.py <model> <prefix> [res] [tex|clay]` renders 8 views with inside faces magenta; `closeup.py` renders a
  region of a view up close; `preview.py` renders the PBR maps; `tint_preview.py` makes maps as the game draws
  them for one rail color.
- `shots.ps1 -out <dir> [-color 4]` runs the built game (not the installed one) with its own home folder: first
  person idle and after a shot, third person on a modern fighter, the pickup on q3dm17. `-gunpos "x,y,z;..."` adds
  first-person shots at view offsets. color1 is id's bits: 1 blue, 2 green, 4 red (id's default).
