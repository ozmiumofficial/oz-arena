# Q3Remaster tools

Tools for remastering Quake III Arena's assets to modern standards, for a Godot (recommended) or Unity remake. Everything here reads your own legally owned copy of the game; see [PLAN.md](PLAN.md) for the roadmap, design decisions and the licensing section.

Started 2026-10-22 with Keel as the test case (`case_study_keel/`), then extended to every player model, item, entity and visual effect, with a Godot 4 project that loads the whole library.

## What's built

| Library | Count | What the tools do |
|---|---|---|
| Player models | 29 | Weld and smooth the MD3 meshes (about 800 -> 13,000 triangles); recover a 21-30 bone skeleton + skin weights from the baked vertex animation (2-13 mm average error); 25 named clips at 60 fps, legs/torso layerable; matte PBR maps; .blend / .fbx / .glb |
| Props | 87 | Every weapon (assembled with barrel + flash), item, powerup, ammo box, flag, gib, projectile, weapon-hit model and map object; tag points as attachment nodes; vertex animation as morph targets; matte PBR with alpha cut-outs, additive energy layers, reflection-only chrome and glass |
| Visual effects | 30 textures + 17 effects | Explosion flipbooks, beams, smoke, blood, bubbles, marks/decals, powerup shells, remastered from the effect shaders; parameters taken from id's GPL cgame code (fx.json) |
| World materials | 1,346 surfaces + 23 liquids + 46 fog + 49 skies | Every world shader and every `textures/` image. Tiling-aware matte PBR from the painted textures (1,234 texture jobs). One Godot material per Quake 3 shader with its glow layers, tcMod motion, rgbGen waves, animMap flipbooks, surface lights and cut-outs. Liquids with refraction, flow and vertex waves; six-sided skies as panoramas, cloud skies as layers; stochastic tiling on 310 isotropic textures. `world_materials.json` maps each shader name to its resource (for the map importer) |
| Entity catalog | 35 items + 48 map entity classes | bg_misc.c item list (names, amounts, models, icons, sounds), g_items.c respawn times, g_spawn.c entity classes |
| Godot project | `godot/` | All of the above plus character, pickup and effect scripts, an import step that applies id Tech 3 draw modes, a showcase scene and a 39-check self-test |
| Main menu | `godot/scenes`, `webui/` | Splash with the fan-project disclaimer, then a live React menu over a Skyrim-style 3D showcase. Quake 3's menu sounds, fonts and banner logo, settings, match setup, the single-player campaign, cinematics. Gameplay and Map editor are stubs |
| Game code (q3core) | GDExtension + game module | id's GPL game code (ioquake3) running inside Godot as `addons/q3core`: the server, the game module (movement, weapons, items, rules) and botlib with the original `.aas` bot navigation and bot personalities, reading the player's own paks. A local player joins as an in-process client with cgame's prediction. Godot does all presentation (G3 onward, [GAMEPLAY_PLAN.md](GAMEPLAY_PLAN.md)) |
| Maps | 35 | Every map in the player's paks (q3dm0-19, q3tourney1-6 + 6_ctf, q3ctf1-4, the four pro maps): drawn surfaces with the remastered world materials, patches re-tessellated smooth, relit with q3map's own light model (point lights, surface lights, the sky's sun) and baked with Godot's LightmapGI; collision for ragdolls and gibs, fog volumes, entity markers. The game itself still plays the .bsp through q3core |
| Friends and parties | `online/`, `scripts/online/` | Online services on Cloudflare's free tier: no accounts (a key pair per PC, friend codes like `ABCD-1234`), friend requests, presence (menu, match, map editor), parties of up to 8 with leader, invites, kick, promote and party chat. In the menu: the top bar's status pill, party strip and friends button, and a Friends panel (F, or Y on a controller). Live on Cloudflare at `arena-online.fragnet.workers.dev` ([online/README.md](online/README.md)) |
| Cinematics | 10 | Quake 3's RoQ movies (id logo, intro, tiers 1-7, ending) as 4:3 Ogg Theora at 1280x960 |
| Player cards | 378 parts + 89 achievements | The card catalog, built the way trac3r does it but with Quake 3 content (PLAN.md "Player cards, progression and ranked"). Art: 35 arena banners, the 29 fighters rendered in the menu's smoky void, 7 skies, 27 world materials as lit walls, 126 emblems (every skin's head icon, weapons, items, flags, medals) and frame swatches. `Q3Cards` handles unlocks, achievements, forged-card fallback and the level curve. Card renderers and the editor come later |

## Quick start

```
pip install -r tools/requirements.txt
python tools/setup_check.py
python tools/remaster_model.py --all     # player models (about 30 min)
python tools/build_library.py            # props, effects, catalog, menu assets, cinematics, Godot project, web menu, import + self-test
```

The first run extracts models, shaders, effect textures, menu art, sounds, level shots and movies from your `pak*.pk3` into `extracted/`. The web menu needs Node.js 20+ (`npm` on PATH); without it the last build in `godot/webui` is kept. The cinematics need ffmpeg, from PATH or the imageio-ffmpeg Python package. The game code (q3core) needs Visual Studio 2022's C++ tools and git; the first build fetches its sources and takes about 10 minutes, and `--skip-q3core` keeps the last build. Pass `--blender`, `--q3` or `--godot` if the programs are somewhere unusual, or set `GODOT_PATH`.

After changing a later step, rerun just that part:

```
python tools/remaster_model.py --all --from pbr     # character textures/exports only
python tools/build_library.py --skip-props          # Godot scripts/effects/catalog only
python tools/build_library.py --skip-props --skip-world   # reuse props and world textures too
python tools/build_cards.py extracted/baseq3 work/_cards  # player card catalog + art (renders in Godot, windowed, about 1 min)
python tools/build_cards.py extracted/baseq3 work/_cards --no-render   # catalog and Python-made art only
python tools/make_music.py                                  # the soundtrack (music/*.wav from the paks) -> godot/library/music as Ogg
python tools/export_game.py                                 # a Windows build in build/windows (about 2 min, 3.9 GB)
```

`build_cards.py` writes to `work/_cards`. `make_godot_project.py` copies it into `godot/library/cards`, and `build_library.py` runs both after the first import (`--skip-card-renders` keeps the last renders).

Materials are deliberately matte (roughness never below 0.45); the values are at the top of `tools/make_pbr.py`.

## The Godot project

Open `godot/project.godot` in Godot 4.7 (the first open imports about 1,000 assets). F5 runs the game: the splash, Quake 3's id logo and intro on the first launch, then the main menu.

### Main menu

| Part | Where | What |
|---|---|---|
| Splash | `scenes/splash/` | The fan-project disclaimer: models derived from id Software's and belonging to id Software, and Quake III Arena on Steam required. Loads the menu meanwhile |
| Showcase | `scenes/main_menu/showcase_stage.gd` | One library model in a smoky void: volumetric fog, embers, a soft key light, a rim in your accent color, depth of field. The camera drifts between four framings. The subject changes every 15-25 s through the smoke (`weapons/change.wav`). Characters idle and gesture, hold a weapon at `tag_weapon`, and sometimes wear a powerup shell |
| Logo | `scenes/main_menu/main_menu.gd` | banner5, drawn the way q3_ui's main menu draws it (60 degree strip, 5 degree sway). Flat "REMASTERED" under it in the banner font. One constant to swap once the project has its own name |
| Menu | `webui/` (Next.js 16, React 19, Tailwind 4, Motion) | Runs live in a transparent godot_wry web view (WebView2) over the 3D. The tree is in `webui/lib/menu.json`: Single Player, Play (Continue, Quick Play, Ranked, Server Browser, Create Match, Offline vs Bots, Training), Map Editor, Profile, Workshop, Cinematics, Settings, Credits, Quit |
| Bridge | `scenes/main_menu/menu_bridge.gd` | The JSON messages between the page and Godot. Godot keeps settings, match setups, campaign progress, sounds, bind capture and the controller |
| Settings | `scripts/settings/` (autoload `Settings`) | 131 settings in 10 tabs from one schema: live application, `user://settings.cfg`, search, reset (R), config/crosshair codes, a 15 s display check. Settings for systems that don't exist yet are stored and marked "not yet wired" |
| Match setup | `scripts/match/`, `game/modes/*.tres`, `game/modifiers.json` | `Q3MatchSetup` (mode, map, rules, modifiers, bots, server type), 7 game modes as data, validation, `Session.start_match()` -> Gameplay |
| Campaign | `scripts/match/campaign.gd` | Quake 3's single player: Training, 6 tiers of 4, the Final. Tiers unlock in order; 5 skills; medals; progress in `user://campaign.json`. `record_result()` (for Gameplay) returns the tier movie the win unlocks |
| Cinematics | `scripts/ui/cinematic_player.gd`, `scenes/cinematic/` | 4:3 full-screen playback; any key skips. The intro plays on the first launch (Settings > Gameplay > Intro movie); tier movies unlock with the campaign |
| Sounds | `scripts/ui/ui_sound.gd` (autoload `UiSound`) | `menu1-4.wav` and `change.wav`, played as `ui_qmenu.c` does: move on cursor change, in after a menu's first frame, out on back, buzz at limits and on refused actions |

Work on the web menu in a normal browser: `cd webui && npm run dev`. A mock bridge stands in for Godot, using the state Godot dumped to `public/q3/dev_state.json`. `python tools/build_webui.py` rebuilds it into `godot/webui`.

The old library showcase is `showcase/showcase.tscn` (F6 on that scene):

| Key | Action |
|---|---|
| Left / Right | Featured character (all 29) |
| Up / Down | Legs clip (LEGS_* and whole-body BOTH_*) |
| PgUp / PgDn | Torso clip |
| R | Ragdoll / stand up |
| Q | Quad damage shell |
| 1-9 | Fire one effect (rocket, rail, plasma, lightning, grenade, BFG, bullets, gibs, teleport) |
| Tab | Effect loop on/off |
| Right mouse + WASD, Space/C | Fly the camera |

`showcase/materials.tscn` is the world material browser: every material on a 2x2 tiling panel with a sphere, under a moving light.

| Key | Action |
|---|---|
| PgUp / PgDn | Page |
| 1–7 | Filter: all, metal, stone, light, organic, liquid, effects |
| K | Next sky |

World materials live in `godot/library/world/`:
- **`materials/<path>.tres`:** one per shader. The path is the shader name minus `textures/`.
- **`skies/<path>.tres`:** Sky resources.
- **`world_materials.json`:** the lookup table for a map importer: resource, kind, base size, light energy and color.

Shaders are in `godot/shaders/world/`: `world_opaque`/`_2s`, `world_blend`/`_2s`, `world_add`, `liquid`, `sky_panorama`, `sky_clouds`.

Scripts to build on (all in `godot/scripts`):

| Script | Use |
|---|---|
| `q3.gd` (autoload `Q3`) | Catalogs (`Q3.characters`, `Q3.props`, `Q3.entities`, `Q3.map_entities`, `Q3.fx`), `Q3.UNIT` (1/32 m), `Q3.q3_to_godot()`, loaders |
| `q3_character.gd` (`Q3Character`) | `load_model()`, `play_legs()`, `play_torso()`, `clips()`, `attach()` (a prop at `tag_weapon`/`tag_head`), `ragdoll()`, `stand_up()`; AnimationTree with a torso bone filter |
| `q3_pickup.gd` (`Q3Pickup`) | An item by classname, spinning and bobbing like cg_ents.c |
| `session.gd` (autoload `Session`) | Scene flow (splash, menu, Gameplay, Map editor, cinematics), the current match setup, modes, maps, bots, campaign, cinematic unlocks |
| `settings/settings.gd` (autoload `Settings`) | Every setting: values, live application, InputMap binds, `user://settings.cfg` |
| `ui/q3_fonts.gd`, `ui/q3_theme.gd` | Quake 3's fonts (signed-distance prop font, glow, banner) and the native UI theme (splash, stubs) |
| `cards/q3_cards.gd` (`Q3Cards`) | The player card catalog (`library/cards/cards.json`): `parts()`, `part()`, `is_unlocked()`, `unlock_text()`, `achieved()`, `progress()`, `missing()`, `enforce_unlocks()` (locked parts fall back to the default before anyone sees a card), `level_for()`, `stat_value()` |
| `fx/q3_fx.gd` (`Q3FX`) | `explosion()`, `rail()`, `lightning()`, `rocket()`, `plasma()`, `muzzle_flash()`, `blood()`, `gibs()`, `teleport()`, `shell_material()` |
| `game/q3_host.gd` (`Q3Host`) | Starts id's game code (the `Q3Server` class from q3core) with this project's paths and the 125 Hz setup; userinfo and info-string helpers. `Q3Server`: `exec("map q3dm17")`, `exec("addbot sarge 3")`, `connect_local()`, `usercmd()`, `frame(msec)`, `snapshot()`, `predict()`, `next_commands()`, `configstring()`, `cvar()` |
| `import/q3_post_import.gd` | Runs on every .glb: `__add` / `__mask` / `__blend` / `__2s` material names (and characters' `rig.json` material modes) become Godot material settings; `flash` nodes start hidden |

Tests:

```
godot --headless --path godot res://tests/selftest.tscn
godot --headless --path godot res://tests/q3core_match.tscn -- map=q3dm17 seconds=60 bots=3
godot --headless --path godot res://tests/gameplay_smoke.tscn
godot --path godot res://tests/weapon_fx.tscn -- out=C:/temp/wfx [move=1 pitch=8 preset=classic]   # every weapon fired, frames saved
godot --path godot res://tests/pause_menu.tscn   # pause menu, crosshair editor live, resume (6 checks)
godot --headless --path godot res://tests/campaign_flow.tscn   # a campaign arena start to finish (7 checks)
godot --headless --path godot res://tests/check_scripts.tscn   # every script parses (what the editor's Errors panel would list)
godot --path godot res://tests/ragdoll_test.tscn -- out=C:/temp/rag [seq=sarge] [burn=1]
godot --path godot res://tests/scoreboard_test.tscn -- out=C:/temp/board
godot --path godot res://tests/capture.tscn -- out=C:/temp/shot frames=220 count=1 view=fx fire=rail
godot --path godot res://tests/card_renders.tscn -- job=C:/.../work/_cards/_render_job.json   # build_cards.py writes the job
godot --path godot res://tests/capture.tscn -- scene=res://scenes/main_menu/main_menu.tscn out=C:/temp/menu frames=300 subject=char:bones framing=0
powershell -File tools/capture_window.ps1 -Title Q3Remaster -Out C:/temp/menu_web.png -Delay 9
```

The menu's web layer is invisible to Godot's own screenshots, so `capture.tscn` shows the 3D and logo, and `capture_window.ps1` (PrintWindow on the running game window) shows the web layer. Main-menu capture options: `subject=char:<model>|item:<classname>|prop:<path>`, `framing=0-3`, `swap=<seconds>`, `page=<menu page id>`.

Use the scene form, not `--script`: a `--script` main loop compiles before autoloads exist, so the `Q3` autoload isn't found and Godot idles.

**Claude control:** `tools/godot-mcp` is Ozmium's MCP server for Godot: 19 tools to run projects, edit scenes, and drive the open editor through the bridge add-on, which the project already includes. Register it once:

```
claude mcp add -s user -e GODOT_PATH="<path to Godot exe>" godot -- node <path>/Q3Remaster/tools/godot-mcp/dist/index.js
```

Verify with `npm test` in `tools/godot-mcp` (23 checks).

## Folder layout

```
Q3Remaster/
  README.md  TOOLS.md  PLAN.md  CLAUDE.md
  tools/                 every script, the Godot template, godot-mcp
  extracted/baseq3/      models, shaders and effect textures from your paks (recreated on demand)
  work/<model>/          per player model: export/ (blend, fbx, glb, textures, unity/), viewer/, reports
  work/_props/           props: export/<id>.glb, tex/, props.json, materials.json, textures.json, tag_models.json
  work/_fx/              effect textures + fx.json
  work/_entities/        entities.json
  work/_world/           world materials.json, textures.json, src/; tex/ (all maps + previews) is left out
                         of the zip because godot/library/world/textures holds the maps in use;
                         python tools/build_library.py rebuilds it (about 40 min, 4 workers)
  work/index.html        gallery of every player model's web viewer (serve work/ with python -m http.server)
  work/_ui/              menu sounds, fonts, maps.json, bots, crosshairs, icons, campaign art (make_ui.py)
  work/_cinematics/      Quake 3's movies as .ogv + thumbnails (make_cinematics.py)
  work/_cards/           player card catalog (cards.json), emblems/, banners/, frames/, _render/ (the Godot renders),
                         _sheet.jpg (a check sheet of every part's art) (build_cards.py)
  webui/                 the main menu's web UI (Next.js); node_modules, .next and out are build output
  online/                friends and parties: the Cloudflare Worker + Durable Object (online/README.md); node_modules is build output
  godot/                 the Godot 4 project
  case_study_keel/       the original Keel session
  tools/q3core/          the q3core GDExtension: src/ (C++ classes, C bridge), SConstruct; thirdparty/ and build/
                         are left out of the zip (python tools/build_q3core.py refetches and rebuilds them)
```

## The tools

| Script | Runs in | What it does |
|---|---|---|
| `remaster_model.py` | Python | Player models: every step for one, several or all; `--from mesh/fit/pbr/rig/viewer` |
| `build_library.py` | Python | Props, effects, catalog, Godot project, Godot import and self-test |
| `make_music.py` | Python | Quake 3's soundtrack as Ogg Vorbis for `Q3Music` |
| `export_game.py` | Python | The Windows build: Godot's export plus q3core's game module beside the exe. `Q3Remaster.exe map=q3dm17 bots=3 skill=4` goes straight into a match |
| `extract_pk3.py` | Python | Unpacks pk3s in engine order (later paks win), lower-case paths |
| `build_mesh_data.py` | Python | Player MD3s -> welded cage + Loop subdivision operator, texture lookup like the engine |
| `fit_rig.py` | Python | Skinning decomposition: bones + weights from vertex frames; automatic bone count |
| `make_pbr.py` | Python | PBR maps for any texture list: painted, additive, reflection-only, alpha and glow-layer handling |
| `build_rigged.py` | Blender | Player mesh + armature + clips -> .blend/.fbx/.glb |
| `build_props.py` | Python | Every non-player MD3 -> mesh data, material flags from shaders, tags, frames |
| `build_props_blender.py` | Blender | Props -> GLB (weapons assembled, tags as nodes, morph animation) |
| `make_fx.py` | Python | Effect textures (flipbooks, decals, shells) + fx.json from cgame values |
| `make_entities.py` | Python | Entity catalog |
| `q3shader.py` | module | Full shader-script parser: stages, blend modes, tcMods, rgbGen waves, animMap, surfaceparms, sky/fog/light directives |
| `build_world.py` | Python | Every world shader + `textures/` image -> material list and PBR jobs (tile, glow, sky), groups (metal, stone, light, organic, liquid) |
| `make_godot_world.py` | Python | Godot materials, skies (six-sided -> panorama via tr_sky.c's face table), fog, `world_materials.json` |
| `make_godot_project.py` | Python | Template + library -> `godot/` (copies only what changed) |
| `make_ui.py` | Python | Menu sounds, Quake 3's fonts (signed-distance atlas, glow, banner, and a traced TTF for the web), maps and bots from the arena/bot scripts, level shots, crosshairs, icons, campaign medals |
| `card_catalog.py` | module | The player card catalog as data: every part with its unlock rule, the achievements, showcase stats, ranked tiers, level track (structure from trac3r's `cosmetics.ts`/`achievements.ts`) |
| `build_cards.py` | Python + Godot | Player card art (emblems, levelshot banners, frame swatches; character, sky and material banners rendered by `tests/card_renders.tscn`) and `cards.json` |
| `make_cinematics.py` | Python + ffmpeg | `video/*.roq` -> 1280x960 Ogg Theora, thumbnails, `cinematics.json` |
| `build_webui.py` | Python + npm | Web menu assets, Godot's state dump for browser dev, `npm run build`, copy into `godot/webui` |
| `build_maps.py` + `q3bsp.py` | Python | BSP reader (lumps, patch tessellation, brush hulls, entities) -> `work/_maps/<map>/<map>.glb` + `.json` (entities, lights in q3map's model, sky, fog, collision, report); `--godot` copies them into `godot/library/maps/` |
| `build_q3core.py` | Python + SCons + MSVC | Fetches the pinned ioquake3, godot-cpp and id sources (`q3core/thirdparty.json`), builds the q3core GDExtension (`q3core/`), copies the addon into the template and `godot/` |
| `make_prop_shaders.py` | Python | Quake 3's multi-stage shaders for pickups and effect materials (chrome, energy, scrolling electricity) -> `prop_shaders.json` + stage textures; applied by the import script with `shaders/q3_stages*.gdshader` |
| `rig_extras.py` | Python | rig.json extras: `tag_weapon`/`tag_head` rest frames and material draw modes (also run by `build_rigged.py`) |
| `capture_window.ps1` | PowerShell | Screenshot of a window including its web view (PrintWindow) |
| `unity_repack.py` | Python | URP texture repack (MetallicSmoothness, Occlusion) + UNITY_IMPORT.txt |
| `pack_viewer.py`, `make_gallery.py` | Python | Web viewers and the gallery page |
| `q3common.py`, `meshops.py` | modules | MD3/skin/cfg/shader readers; weld, creases, subdivision |
| `setup_check.py` | Python | Checks the toolchain |
| `models.json` | config | Per-model overrides (bone counts, face band, skin) |
| `godot_template/` | Godot | Project source copied into `godot/` |

**Scale and axes:** 32 id units = 1 meter. Characters have their feet at the origin. Blender faces −Y, glTF/Godot +Z, Unity +Z.

## Known limits (details and fixes in PLAN.md)

- **Classification:** materials are classified by color alone, so red cloth (Sarge's camo) can read as glowing heat. Per-model ID maps fix this.
- **Shape and joints:** subdivision rounds the 1999 shapes without adding detail, and the 1999 topology bends stiffly at the joints.
- **Not yet in the tools:** .map (Radiant source) import and export, and the map editor. Planned in PLAN.md. Map entities that move or trigger are drawn by Gameplay from the game's entity states (G3).
- **Menu:** Gameplay and Map editor are placeholder scenes, so campaign wins, medals and tier movies after a match aren't recorded yet. Steam features (matchmaking, server list, Workshop, achievements) wait on GodotSteam.
- **Web view:** godot_wry's transparency doesn't work on Linux yet, and packaging `webui/` into an exported build is untested.
- **Unshipped shaders:** 369 world shaders (336 surfaces, 33 skies) reference images id cut from the retail paks. They're listed in the catalog as `unshipped`.
- **Water preview:** water is transparent and refractive, so in the material browser, with nothing behind it, it reads dark.
- **Missing model:** the medkit's sphere model isn't in the retail paks.
