# Modern fighters (experimental)

New high-detail models for id's fighters, each on its own skeleton with its own animations, turned on in
**Settings > Experimental > Modern player models** (`r_ozModernFighters 1`, applied when Settings closes). They load
through the same path as the remastered fighters: the `oz` renderer swaps a fighter's `lower/upper/head.md3` for
`models/players/<slot>/oz_rig_modern.iqm` in place of the remastered `oz_rig.iqm`
(`engine/quake3e/code/renderer2/tr_oz_rig.c`), so they show on any server and mod that allows Oz's packs. A fighter
without a modern model keeps its remastered rig.

| Fighter | Replaces | Notes |
|---|---|---|
| `doom` | doom | armored marine |
| `visor` | visor | orange cyborg |
| `anarki` | anarki | rides a hoverboard: the board is rigged, the legs ride it |
| `orb` | orbb | a walker: an eye on two arms it walks on; the gun sits in a mount on top that turns with the aim |

## Build

```
python tools/fighter_swap/build_fighter.py doom visor anarki orb
```

Needs Blender 4.2 or later (`BLENDER`, or `C:\Program Files\Blender Foundation\Blender 5.2\blender.exe`) and your
`baseq3` (found as the other tools find it). It writes `build/fighter_swap/<name>/` and
`build/engine/oz/pak_oz_modern_fighters.pk3`, beside the other packs, so the installer and the Linux release carry it
(`--out` puts it elsewhere). `--from anim` (or `stretch`, `weights`, `pack`) reruns only the later steps.

For each fighter, `fighters/<name>.json` names its source model and texture (`sources/`), the slot it replaces, and
its joints in the model's T-pose (Blender coordinates: x is his left, -y his front, z up, about 1 tall).

1. `prep.py`: welds the mesh and caps its holes. Leftover seams are hidden by a two-sided shader. A config with
   `"repair": true` (orb, whose export came full of holes) also splits the edges three faces share and fans the
   loops plain capping won't take, so the mesh comes out closed: no open or three-sided edges.
2. `stretch_arms.py`: lengthens the arms to a person's proportions. The models come with arms about two thirds as long
   as a person's, too short to hold a gun with both hands.
3. `rig_weights.py`: Blender heat weights, solved on a watertight voxel copy, because the models are dozens of loose
   shells. If the solver gives up on the whole copy, it drops the copy's stray crumbs (thin cables) and tries again,
   then a coarser copy.
4. `anim_fighter.py`: the skeleton and skin, then a pose for every frame the slot's `animation.cfg` names. Legs use
   gait cycles with planted feet and two-bone IK, or for a board rider the feet stay on a hovering board. The hold
   is built from the gun outward: the right palm on the grip (about 4.5 units behind and 3 below `tag_weapon` on every
   id weapon, `q3weapons.py`) and the left under the fore-end, with a rifleman's turn of the torso. It also covers
   recoil, the gauntlet, weapon drop and raise, the taunt and the falls. `tag_weapon` follows the right hand, and the
   tags, spine, ragdoll and IQM come from `tools/make_fighters.py`.

### Walkers

A config with `"body": "walker"` (orb) has no arms to hold a gun. Its arms are its legs (`thigh` = upper arm from the
shoulder, `calf` = forearm, `foot` = hand, `toe` = knuckles), bending backward at the elbow (`"knee_dir": -1`); the
hands roll with the forearms instead of twisting at the wrist. `"mount"` places the gun mount's post, cradle and cup:
the post and cradle are the torso, so they turn and pitch with the aim, and `tag_weapon` sits so a weapon's grip rests
in the cup. `"center"` names the body (`pelvis`) and the eye (`eye`, `eye_front`), which stays with the body. `"lie"`
and `"lie_side"` set how high the body lies in the deaths.

## Checking

- `pose_preview.py <rig> <pak0> <dir> <weapon> <slot>` writes OBJs of a dozen poses with a real weapon model in the
  hand, and `render_hold.py <dir> <texture> full|close` renders them.
- `render_arms.py <dir> <texture>` renders the shoulders, elbows and wrists of those poses up close.
- `clip_check.py` counts how far body parts sink into each other across the animations.
- `freecam.ps1 -bot visor -out <dir>` runs the game unattended. It freezes that fighter as a bot on q3dm17, circles it
  with a free camera (`edpos`, `oz_aimat`), then lets it fight, and saves the screenshots.

## Adding a fighter

1. Put the Tripo export (FBX and its `.fbm` texture folder) in `sources/<name>/`.
2. Copy a config to `fighters/<name>.json` and set `slot` and the joints. A profile of the mesh (arm and leg widths by
   height) shows where the shoulders, elbows, wrists, hips, knees and ankles sit.
3. Run `build_fighter.py <name>`, then check it with `pose_preview.py` and `freecam.ps1`.
