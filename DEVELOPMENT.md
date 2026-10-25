# Developer guide

How the remaster is built. The player-facing overview, with screenshots, is [README.md](README.md).

Tools for remastering Quake III Arena's assets to modern standards, for a Godot (recommended) or Unity remake. The tools read your own copy of the game; see [PLAN.md](PLAN.md) for the roadmap, design decisions and the licensing section.

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
| Main menu | `godot/scenes`, `webui/` | Splash with the fan-project disclaimer, then a live React menu over a Skyrim-style 3D showcase. Quake 3's menu sounds, fonts and banner logo, settings, match setup, the single-player campaign, cinematics |
| Map editor | `godot/scenes/map_editor`, `godot/scripts/map`, q3core `Q3MapGeo` | Opened from the menu: plane-based brushes in double precision (C++ kernel), box create, select/move/rotate/mirror/duplicate/group, CSG subtract, hide/lock/isolate, the face tool (push/pull, texture shift/scale/rotate, eyedropper, texture lock), the vertex tool (corner drags, chamfers and wedges, vertex snap), curved patches (bevel, curve, cylinder, arch), undo, OZMIUM's keymap with an F1 sheet and rebinding, save/open. Then materials and the inspector, entities, lights and prefabs, compile and play-test (id's q3map and bspc), BSP and Radiant .map import, .map export (brushDef, Valve 220), view filters, the Objects and History tabs, recent maps with thumbnails, and Publish to Create Match. Done in phases E0-E8 of [MAP_EDITOR_PLAN.md](MAP_EDITOR_PLAN.md) |
| Replays | `godot/scripts/replay`, q3core `Q3Replay` (`engine/q3_replay.c`) | Every match recorded on the server (every entity and every player's playerState at 40 Hz, msg.c delta + Huffman, about 0.9 MB a minute for 8 players) and played back in the Gameplay scene through `Q3Feed`: a Godot-drawn viewer (speeds, frame step, frag skip, a marked timeline, first person / chase / orbit / free cameras, name tags, outlines), the menu's Replays page, clip export with Movie Maker, and Quake 3's own .dm_68 demos. Also id's spectator mode with a chase cam and an auto-director, the death cam, and the scoreboard's per-weapon stats. Done in phases R1-R9 of [REPLAY_PLAN.md](REPLAY_PLAN.md) |
| Diagnostics | `godot/scripts/diagnostics`, q3core `install_crash_dumps` | Crash and freeze reports written at the next launch (Godot's backtrace, q3core's minidump and note, every error with its script backtrace, breadcrumbs, machine and state), a menu notice after a crash, and the F10 overlay |
| Game code (q3core) | GDExtension + game module | id's GPL game code (ioquake3) running inside Godot as `addons/q3core`: the server, the game module (movement, weapons, items, rules) and botlib with the original `.aas` bot navigation and bot personalities, reading the player's own paks. A local player joins as an in-process client with cgame's prediction. Godot does all presentation (G3 onward, [GAMEPLAY_PLAN.md](GAMEPLAY_PLAN.md)) |
| Maps | 35 | Every map in the player's paks (q3dm0-19, q3tourney1-6 + 6_ctf, q3ctf1-4, the four pro maps): drawn surfaces with the remastered world materials, patches re-tessellated smooth, relit with q3map's own light model (point lights, surface lights, the sky's sun) and baked with Godot's LightmapGI; collision for ragdolls and gibs, fog volumes, entity markers. The game itself still plays the .bsp through q3core |
| Friends and parties | `online/`, `scripts/online/` | Online services on Cloudflare's free tier: no accounts (a key pair per PC, friend codes like `ABCD-1234`), friend requests, presence (menu, match, map editor), parties of up to 8 with leader, invites, kick, promote and party chat. In the menu: the top bar's status pill, party strip and friends button, and a Friends panel (F, or Y on a controller). Live on Cloudflare at `arena-online.fragnet.workers.dev` ([online/README.md](online/README.md)) |
| Cinematics | 10 | Quake 3's RoQ movies (id logo, intro, tiers 1-7, ending) as 4:3 Ogg Theora at 1280x960 |
| Player cards | 378 parts + 89 achievements | The card catalog, built the way trac3r does it but with Quake 3 content (PLAN.md "Player cards, progression and ranked"). Art: 35 arena banners, the 29 fighters rendered in the menu's smoky void, 7 skies, 27 world materials as lit walls, 126 emblems (every skin's head icon, weapons, items, flags, medals) and frame swatches. `Q3Cards` handles unlocks, achievements, forged-card fallback and the level curve. Card renderers and the editor come later |

## Quick start

A clone already includes the remastered library (`godot/library`, `godot/webui`) through Git LFS: run `git lfs pull`, then open `godot/` in Godot. The steps below are only needed to regenerate the library from your own paks.

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
| Menu | `webui/` (Next.js 16, React 19, Tailwind 4, Motion) | Runs live in a transparent godot_wry web view (WebView2) over the 3D. The tree is in `webui/lib/menu.json`: Single Player (Skirmish, Campaign, Training), Play (Continue, Matchmaking, Ranked), Multiplayer (the server browser; Host a match and Dedicated server are buttons on it), Leaderboards, Replays, Map Editor, Profile, Cinematics, Settings, Credits, Quit |
| Bridge | `scenes/main_menu/menu_bridge.gd` | The JSON messages between the page and Godot. Godot keeps settings, match setups, campaign progress, sounds, bind capture and the controller |
| Settings | `scripts/settings/` (autoload `Settings`) | 132 settings in 10 tabs from one schema: live application, `user://settings.cfg`, search, reset (R), config/crosshair codes, a 15 s display check. Settings for systems that don't exist yet are stored and marked "not yet wired" |
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
| `diagnostics/diagnostics.gd` (autoload `Diagnostics`, first) | Crash and freeze reports in `user://diagnostics/reports` (with q3core's minidumps), every error caught with its backtrace, breadcrumbs (`note()`), `set_context()`, `expect_stall()`, the F10 overlay |
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
godot --headless --path godot res://tests/ownership_test.tscn   # the pak0 check: checksums, every kind of drop (23 checks)
godot --path godot res://tests/setup_gate_test.tscn -- window=1600x900   # the launch gate: blocks, refuses a fake, accepts the real pak0 (5 checks)
godot --headless --path godot res://tests/gameplay_smoke.tscn
godot --headless --path godot res://tests/warmup_restart_test.tscn   # duel warmup ends (map_restart, FIGHT!), the clock runs on, Restart resets the scores
godot --path godot res://tests/weapon_fx.tscn -- out=C:/temp/wfx [move=1 pitch=8 preset=classic]   # every weapon fired, frames saved
godot --path godot res://tests/pause_menu.tscn   # pause menu, crosshair editor live, resume (6 checks)
godot --headless --path godot res://tests/campaign_flow.tscn   # a campaign arena start to finish (7 checks)
godot --headless --path godot res://tests/chat_test.tscn   # chat and color codes: T/Y, EF_TALK, G_Say round trip, Escape, held Enter (19 checks)
godot --headless --path godot res://tests/settings_wiring_test.tscn   # every setting drives something: item timers, announcer, gun fov, toggle crouch, accel, skins, effects (29 checks)
godot --path godot res://tests/chat_capture.tscn -- out=C:/temp/chat window=1600x900   # the chat open with a preview, and the talk balloon
godot --headless --path godot res://tests/check_scripts.tscn   # every script parses (what the editor's Errors panel would list)
godot --headless --path godot res://tests/map_geo_test.tscn   # map editor geometry: kernel, document, files, mesher (25 checks)
godot --headless --path godot res://tests/map_editor_smoke.tscn   # the Map editor scene driven like the mouse and keys, imports, exports, filters, Objects and History tabs, recent maps, OZMIUM's remaining checks (96 checks)
godot --headless --path godot res://tests/map_compile_test.tscn   # .map export, q3map bsp/vis/light, bspc, the .bsp read back, q3core plays it with a bot, q3dm17 imported and recompiled, Publish (13 checks)
godot --headless --path godot res://tests/map_import_test.tscn    # every map in the paks imported from its .bsp: every drawn surface aligned, .map round trip (37 checks, ~150 s; -- maps=q3dm17 report=<path> debug=1)
godot --headless --path godot res://tests/map_format_test.tscn    # .map import/export: brushDef and Valve 220 round trips, each Radiant form, a broken file (10 checks)
godot --headless --path godot res://tests/map_editor_input.tscn  # the editor driven only by input events: keys, mouse, text focus, camera, dialogs (46 checks)
godot --path godot res://tests/map_editor_perf.tscn               # windowed: q3dm12 in the editor, import/save/open/nudge/undo times and frame times (6 checks; -- map= report=)
godot --path godot res://tests/skins_capture.tscn -- mode=ctf map=q3ctf1 bots=8 out=<dir>   # windowed: every body's model and skin (red/blue by team; campaign=q3dm2 for Phobos's own), one shot each
godot --path godot res://tests/match_perf.tscn -- map=q3dm6 seconds=40 bots=5   # windowed bot match: fps, p99, GPU vs render CPU, every stall with what it created (set=key=value,... for one run)
godot --headless --path godot res://tests/audio_decode_test.tscn   # every sound decoded start to end through its playback; fails loops read from a file (614 sounds)
godot --headless --path godot res://tests/map_playtest_test.tscn   # F5 into Gameplay with the editor's map and bots, F6 back with the undo history
godot --path godot res://tests/capture.tscn -- scene=res://tests/map_editor_shot.tscn out=C:/temp/ed count=1 window=1920x1080 [sheet=1 dialog=1 diag=1 e2=vertex|face e3=1 e4=1|lights|prefab]
godot --headless --path godot res://tests/replay_test.tscn   # a recorded match against the live server, frame by frame; seeks, the library, playback (-- seconds=600 compare=0: the size run)
godot --headless --path godot res://tests/spectator_test.tscn   # id's spectator mode: join, follow, cycle, chase cam, scoreboard, auto-director, Join/Spectate
godot --headless --path godot res://tests/death_cam_test.tscn   # the death cam: the body, the killer's card, the ruleset's respawn timing (-- ruleset=ql|classic; windowed with -- out= for shots)
godot --headless --path godot res://tests/demo_test.tscn   # id's demos/four.dm_68 (from your pak8) parsed and played
godot --path godot res://tests/clip_test.tscn   # clip export: a 3 s range rendered by Movie Maker, the AVI checked
godot --path godot res://tests/replay_capture.tscn -- out=C:/temp/replay   # the replay viewer: first person, chase, the player list, orbit
godot --headless --path godot res://tests/diagnostics_test.tscn   # real crashes and a freeze in child processes, and their reports (8 checks, ~40 s)
godot --path godot res://tests/ragdoll_test.tscn -- out=C:/temp/rag [seq=sarge] [burn=1]
godot --path godot res://tests/aim_test.tscn -- out=C:/temp/aim               # body angles: pitch, yaw swing, lean, LEGS_TURN (8 checks, two shots)
godot --path godot res://tests/body_motion_capture.tscn -- out=C:/temp/motion [map=q3dm17] [bots=5]   # spectated bot match: side shots of strafing bots
godot --path godot res://tests/scoreboard_test.tscn -- out=C:/temp/board
godot --path godot res://tests/capture.tscn -- out=C:/temp/shot frames=220 count=1 view=fx fire=rail
godot --path godot res://tests/screenshots.tscn -- window=1920x1080 out=C:/temp/shot map=q3dm6 [count=6 hud=0]   # README screenshots: a bot match, aiming and firing
godot --path godot res://tests/card_renders.tscn -- job=C:/.../work/_cards/_render_job.json   # build_cards.py writes the job
godot --path godot res://tests/capture.tscn -- scene=res://scenes/main_menu/main_menu.tscn out=C:/temp/menu frames=300 subject=char:bones framing=0
powershell -File tools/capture_window.ps1 -Title Q3Remaster -Out C:/temp/menu_web.png -Delay 9
```

The menu's web layer is invisible to Godot's own screenshots, so `capture.tscn` shows the 3D and logo, and `capture_window.ps1` (PrintWindow on the running game window) shows the web layer. Main-menu capture options: `subject=char:<model>|item:<classname>|prop:<path>`, `framing=0-3`, `swap=<seconds>`, `page=<menu page id>` (put `swap=` before `subject=`, or the subject keeps the default swap timer). Neither shows both layers together; the README's menu shots stack `capture.tscn`'s 3D under the page rendered by headless Chrome from `npm run dev` with a transparent background, at the same size. Any run takes `-- window=1920x1080` to use a window of that size whatever the saved display settings say.

Use the scene form, not `--script`: a `--script` main loop compiles before autoloads exist, so the `Q3` autoload isn't found and Godot idles.

**Claude control:** `tools/godot-mcp` is Ozmium's MCP server for Godot: 19 tools to run projects, edit scenes, and drive the open editor through the bridge add-on, which the project already includes. Register it once:

```
claude mcp add -s user -e GODOT_PATH="<path to Godot exe>" godot -- node <path>/Q3Remaster/tools/godot-mcp/dist/index.js
```

Verify with `npm test` in `tools/godot-mcp` (23 checks). The server needs its packages first: `npm install --omit=dev --no-package-lock` in `tools/godot-mcp` (the lock file's dev typings are out of date, so `npm ci` refuses).

**godot-ai** (`addons/godot_ai`, [hi-godot/godot-ai](https://github.com/hi-godot/godot-ai) v4.3.1, MIT) runs next to it: 47 tools for screenshots of the editor and the running game, input injected into the game, editor and game logs, performance monitors, filesystem scan and reimport, and batch edits with rollback. It has no tool for running GDScript in the editor, which is why the project's own server stays. Its plugin starts a Python server through uv on ports 8000 and 9500, and adds a `_mcp_game_helper` autoload that its export plugin strips from builds. Telemetry is off (`godot_ai/telemetry_enabled = false` in the editor settings, `--disable-telemetry` in the launch). Register it with the dock's **Configure** button for Claude Code, or by hand with the command `ClientConfigurator.resolve_attach_launch(ClientConfigurator.capture_launch_context())` prints (its `console_command` and `console_args`).

Driving a match with it: set `editor/run/main_run_args` to `-- map=q3dm6 bots=3` (in memory, through the `godot` server's `editor_run_gdscript`, and clear it after), then `project_run`. Its 8 s readiness wait times out while the game boots, but the game starts. The splash's disclaimer waits for a key: `game_manage` `input_action` `ui_accept`. Keep `editor_screenshot`'s `max_resolution` at 800 or less: a bigger PNG overflows the 1 MB event limit between its server and the client. `node tools/mcp_match.mjs [map] [bots] [seconds]` does all of it over stdio and saves the frames in `work/mcp_match`.

## Folder layout

```
Q3Remaster/
  README.md  DEVELOPMENT.md  TOOLS.md  PLAN.md  CLAUDE.md
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
| `build_world.py` | Python | Every world shader + `textures/` image -> material list and PBR jobs (tile, glow, additive base layers, sky), groups (metal, stone, light, organic, liquid) |
| `make_godot_world.py` | Python | Godot materials, skies (six-sided -> panorama via tr_sky.c's face table), fog, `world_materials.json` |
| `make_godot_project.py` | Python | Template + library -> `godot/` (copies only what changed) |
| `make_ui.py` | Python | Menu sounds, Quake 3's fonts (signed-distance atlas, glow, banner, and a traced TTF for the web), maps and bots from the arena/bot scripts, level shots, crosshairs, icons, campaign medals |
| `card_catalog.py` | module | The player card catalog as data: every part with its unlock rule, the achievements, showcase stats, ranked tiers, level track (structure from trac3r's `cosmetics.ts`/`achievements.ts`) |
| `build_cards.py` | Python + Godot | Player card art (emblems, levelshot banners, frame swatches; character, sky and material banners rendered by `tests/card_renders.tscn`) and `cards.json` |
| `make_cinematics.py` | Python + ffmpeg | `video/*.roq` -> 1280x960 Ogg Theora, thumbnails, `cinematics.json` |
| `build_webui.py` | Python + npm | Web menu assets, Godot's state dump for browser dev, `npm run build`, copy into `godot/webui` |
| `build_maps.py` + `q3bsp.py` | Python | BSP reader (lumps, patch tessellation, brush hulls, entities) -> `work/_maps/<map>/<map>.glb` + `.json` (entities, lights in q3map's model, sky, fog, collision, report); `--godot` copies them into `godot/library/maps/` |
| `build_q3core.py` | Python + SCons + MSVC | Fetches the pinned ioquake3, godot-cpp and id sources (`q3core/thirdparty.json`), builds the q3core GDExtension (`q3core/`) and id's q3map and bspc for the map editor (`q3core/SConstruct.tools`, patched by `q3core/patches/id-q3a/`), copies the addon into the template and `godot/` |
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
- **Maps in the tools:** the map editor imports any stock map from its .bsp and Radiant .map files, and exports .map (MAP_EDITOR_PLAN.md E6). Map entities that move or trigger are drawn by Gameplay from the game's entity states (G3).
- **Menu:** Quick Play, the Server Browser's list and the Ranked queues wait on online matches (MULTIPLAYER_PLAN.md). There is no Workshop yet (planned as M9 in MULTIPLAYER_PLAN.md, on Cloudflare R2) and no Steam.
- **Web view:** godot_wry's transparency doesn't work on Linux yet, and packaging `webui/` into an exported build is untested.
- **Unshipped shaders:** 369 world shaders (336 surfaces, 33 skies) reference images id cut from the retail paks. They're listed in the catalog as `unshipped`.
- **Water preview:** water is transparent and refractive, so in the material browser, with nothing behind it, it reads dark.
- **Missing model:** the medkit's sphere model isn't in the retail paks.

## License

GPL-2.0 for the code; id Software's assets aren't included. See [README.md](README.md#license).
