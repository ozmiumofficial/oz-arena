# Q3Remaster: context for Claude

Ozmium is planning a complete modern remaster of Quake III Arena in Unity or Godot (engine not chosen). **Current stage: replays are done (R1-R9, 2026-10-23), after the map editor; online multiplayer is next, once Ozmium says go.** The asset pipeline, main menu and Gameplay scene (`GAMEPLAY_PLAN.md`) are built. The Map editor scene was built in phases E0-E8 per `MAP_EDITOR_PLAN.md` (all done: kernel, editing, faces/vertices/curves, materials/inspector, entities/lights/prefabs, compile/play-test, BSP/.map import and export, workflow/polish: filters, outliner, history, thumbnails, Publish; E8 tests and docs). **Then:** replays, the replay viewer, spectator mode and the death cam, phases R1-R9 per `REPLAY_PLAN.md` (all done 2026-10-23; no killcam, Ozmium 2026-10-23). **Last:** online multiplayer, phases M1-M9 per `MULTIPLAYER_PLAN.md` (M9: a map Workshop on Cloudflare R2, back in the plan 2026-10-23) (Ozmium put it last, 2026-10-23). A hype trailer is planned (`TRAILER_PLAN.md`): its flyover and still footage is recorded in retail Quake 3 and the remaster (2026-10-23); the rest waits for Ozmium's go, after replays. Read `PLAN.md` for phases and open decisions, and `DEVELOPMENT.md` for how the tools run. `README.md` is the player-facing feature page (the GitHub front page, with screenshots): keep it non-technical and only claim what's built.

**Direction (2026-10-22):**
- Fully open source.
- Players must own Quake 3 to run it.
- Ships a full remastered asset library with rigged skeletons, made with these tools; nothing is converted on the player's machine. This carries legal risk (derivative works); the mitigations are in PLAN.md "Licensing", so raise them when relevant, don't lecture.
- **Scenes:** three top-level scenes (PLAN.md "Scene structure"):
  - **Main menu:** launch UI and match setup.
  - **Gameplay:** match, game-mode rules and modifiers, loaded from the menu's choice.
  - **Map editor:** all tools, Quake 3 map import, creating maps.

  Game modes are data loaded into Gameplay, not separate scenes.
- **Main menu design** (PLAN.md "Main menu design"): a modernized, visually stunning take on the Quake 3 menu.
  - Menu on the left.
  - On the right, a showcase styled like Skyrim's loading screens: one random library model floating in a dark smoky void, slowly rotating while the camera drifts.
  - Quake 3's own menu sounds (`sound/misc/menu1-4.wav`), used exactly as in the original.
  - A fully fleshed-out settings menu and quality-of-life features.
  - Map editor as a menu item that loads the Map editor scene.
- Full multiplayer with **no Steamworks and no paid hosting** (decided 2026-10-22): player-hosted matches over peer-to-peer WebRTC, always relayed through Cloudflare's TURN so no one sees another player's IP, closest region first with a region choice (2026-10-23), optional dedicated servers, one match browser, and accounts, matchmaking and player data on Cloudflare's free tier. See PLAN.md "Multiplayer and online services". Friends and parties are built (`online/`, `scripts/online/`).
- Customizable player cards, XP and levels that unlock card parts, achievement-only unlocks, and ranked Elo matchmaking.
  - Built the way Ozmium's own game trac3r does it (`C:\gAME\TRAC3R\web`: `cosmetics.ts`, `achievements.ts`, `PlayerCard.tsx`, `bannerArt.ts`); its code is free to reuse.
  - The catalog is large and mostly Quake 3 themed.
  - All progression is cosmetic.
  - See PLAN.md "Player cards, progression and ranked".
- A built-in map editor that works exactly like OZMIUM's (spec in PLAN.md; OZMIUM's Unity code in C:\gAME\GAME is the reference).
- Imports Quake 3's original maps (.bsp and .map).
- Every texture in the game becomes fully PBR (1,919 images).
- Every modern enhancement: ragdolls, modern UI, rendering, netcode, multiplayer features and more (PLAN.md, "Modern enhancements").
- Godot recommended.
- The original game feel is sacred.

## Rules

- id Software's assets (models, textures, maps, sounds) are copyrighted. Ozmium's chosen distribution model and its risks are in PLAN.md "Licensing". The finished asset library is public in the repo (Ozmium's decision, 2026-10-23); id's original files and the tools' working files stay private.
- **The source code is public** (Ozmium's decision, 2026-10-23): https://github.com/ozmiumofficial/quake-3-arena-remastered-fan-project (`origin`, branch `main`). Anything committed is published.
  - The repo carries the remastered asset library, `godot/library/` and `godot/webui/`, with its binaries in Git LFS (`.gitattributes`). GitHub's free LFS quota is 10 GiB of storage and 10 GiB of downloads a month, about four full clones; every regenerated texture adds storage.
  - The root `.gitignore` keeps out id's original files (`extracted/`), the tools' working files (`work/`) and the case study's models. It also keeps out builds and refetched code.
  - Check `git add --dry-run` for binaries or media outside the library before committing, and give a new binary type in the library an LFS rule in `.gitattributes`; a pattern with a slash in the middle is anchored to the repo root, so use `**/` for nested paths.
  - Licensed GPL-2.0 (`LICENSE`, the same text as id's and ioquake3's releases), because q3core runs ioquake3's game code. README "License" says it covers code only; the assets in the repo stay id's.
- This project is separate from the GAME/OZMIUM Unity project (`C:\gAME\GAME` on the work PC), which must never contain Quake naming or id assets.
- American spelling ("color", "meter").
- Give effort estimates in hours, not days.
- Working files (.blend sources, scratch exports) stay out of any engine project's Assets folder; only final deliverables go in.
- Verify before claiming results: run the tool, open the viewer, read the reports.
- Materials stay matte: Ozmium found the first PBR pass far too glossy. The roughness floor is 0.45 and painted armor sits around 0.7 with partial metal. Don't add shine unless he asks.

## Running things

```
python tools/setup_check.py
python tools/remaster_model.py --list
python tools/remaster_model.py <model> [<model> ...] | --all
python tools/remaster_model.py --all --from pbr     # redo textures/exports/viewers only
python tools/build_library.py [--skip-props]       # props, fx, catalog, Godot project, import, cards, self-test
python tools/build_cards.py extracted/baseq3 work/_cards [--no-render]   # player card catalog + art
python tools/make_music.py                          # Quake 3's soundtrack -> godot/library/music (Ogg)
python tools/export_game.py                         # Windows build -> build/windows (Q3Remaster.exe map=q3dm17 bots=3 skips the menu)
python tools/make_icon.py [--preview sheet.png]     # the game icon (Quake 3's emblem + R) -> godot/icon.png (window), icon.ico (exe)
python tools/trailer/q3cam/build.py                 # trailer: the q3cam camera mod for retail Quake 3 -> work/trailer/q3cam/pak0.pk3
python tools/trailer/cam_path.py check --all        # trailer: camera paths against the maps' walls
python tools/trailer/capture_q3.py <path> | --all  # trailer: record paths in retail Quake 3 -> work/trailer/retail
python tools/trailer/capture_remaster.py <path> | --all  # trailer: the same paths in the remaster -> work/trailer/remaster
python tools/trailer/compare.py --all               # trailer: each pair split and blended -> work/trailer/compare
```

Results go in `work/<model>/`: `export/` (blend/fbx/glb, textures, unity/), `viewer/` (web viewer: serve with `python -m http.server`), and the reports. Per-model overrides go in `tools/models.json`.

## Godot facts (hard-won)

- **Work in the live editor (Ozmium's rule):** keep the Godot editor open on `godot/` whenever Godot work is being done, so Ozmium can watch the changes happen.
  - If `editor_status` can't connect, launch the editor: `godot.windows.opt.tools.64.exe --editor --path godot`.
  - The `godot_mcp_bridge` addon is already enabled in project.godot and listens on 127.0.0.1:9080.
  - Make scene changes through `editor_open_scene` and `editor_run_gdscript`, then `EditorInterface.save_scene()`.
  - The headless tools (`add_node`, `set_node_property`, `build_library.py`) write files behind the editor's back. A later editor save can overwrite those changes. After running a batch tool, run `EditorInterface.get_resource_filesystem().scan()` and reopen the scene.

- **Godot version:** 4.7.2 (Steam build `godot.windows.opt.tools.64.exe`, a GUI exe; capture output with `--log-file`).
- **godot-mcp:** in `tools/godot-mcp`, 19 tools; register it per TOOLS.md.
- **Headless tests:** run them as scenes (`res://tests/selftest.tscn`), never with `--script`. A `--script` main loop compiles before autoloads exist, so `Q3` is "Identifier not found" and Godot idles forever. Add `--quit-after N` as a backstop.
- **Import script:** `[importer_defaults] scene={"import_script/path": ...}` in project.godot applies `scripts/import/q3_post_import.gd` to every GLB. After changing that script, delete the `.glb.import` files (or `.godot/`) to force a re-import.
  - The scene importer's name suffixes (`nodes/use_name_suffixes`) strip `_alpha` / `_vcol` from material names and turn on transparency or vertex color. `q3_post_import._shader_name()` puts the suffix back from those flags; before it did, `textures/base_wall/atech1_alpha` missed and the tech columns in q3tourney1/2/6 drew the gray `missing` fallback (2026-10-23).
- **3D texture flags:** Godot imports every new texture as 2D (no mipmaps, uncompressed) and switches it only when a 3D material first uses it in the editor. That switch is a reimport inside another import, which prints "Task 'reimport' already exists" or a page of `progress_dialog.cpp` errors ("while flushing the message queue"). Textures never opened in the editor also shipped without mipmaps (806 of them, 2026-10-23). `tools/flag_3d_textures.py` (in `build_library.py` after the first import) flags them up front as Godot would; run it after adding 3D textures any other way.
- **Material modes:** names carry `__add` / `__mask` / `__blend` / `__2s`. Additive materials swap the emission texture into albedo and render unshaded with additive blending.
- **Additive materials need `fog_disabled`** (shaders) or `disable_fog` (StandardMaterial3D): Godot blends volumetric fog into transparent surfaces, and on an additive quad that adds the haze color over the whole square. q3dm6's wall-lamp flares showed as fog-colored boxes until 2026-10-23.
- **Character clips:** every glTF clip keys every bone, so `Q3Character` filters torso bone paths in a Blend2. Restarting clips uses TimeSeek nodes.
- **Effects:** all built at runtime by `Q3FX` from `fx.json`; nothing is pre-baked as scenes.
- **World shaders:** they include `world_surface.gdshaderinc`. Variants exist because render modes can't be uniforms (opaque, opaque two-sided, blend, blend two-sided, add, add_lit). Godot front faces are clockwise as the camera sees them, so hand-built meshes need indices 0,2,1 / 0,3,2 for a camera-facing quad.
  - `env_map`: the base stage is `tcGen environment` (glass01, chrome), with uv from the reflected view (RB_CalcEnvironmentTexCoords).
  - `has_under`: an opaque first stage under a blended paint layer shows where the paint is clear (console chrome, energy under floor grates).
  - `glow_under`: glow stages drawn before the paint (teslacoil arcs).
  - A reflection added under an alphaFunc paint layer is a `next_pass` glass (glass_frame).
  - `world_add_lit`: add stages followed by a `$lightmap` filter stage, lit so the lightmap dims them.
  - Two-sided shaders light back faces with the front normal. The lightmaps are directional, so a flipped normal read them black (q3dm0's tank frame).
  - These flags come from `q3shader.describe` through `build_world.py` and `make_godot_world.py`. On 2026-10-22 the existing `.tres` files were patched directly rather than regenerated under the open editor.
  - **Fixed 2026-10-23** (seen in trailer footage), each through the pipeline, with the live `.tres` files patched to match:
    - An additive shader's base layer is texture kind `additive` (`build_world.py`): upscaled like a glow layer and written as `_albedo`. The ordinary tile path had lifted its black to about 5/255 (edges up to 246), and every glow, beam and teleporter ring drew as a faint square.
    - An opaque first stage makes the surface opaque even when a later additive stage is named like the shader (`chrome_metal` had turned see-through, leaving a hole in q3tourney4's console pillar).
    - `glow_lit`: added stages before the `$lightmap` filter are lit paint, added to the albedo, not glow.
    - The additive shaders add every stage at the same weight (glow layers had been scaled by `emission_energy`, 0.3 for console03), and `world_add_lit` draws its glow layers (red_telep's second ring was missing).
    - `deformVertexes` wave, move and bulge (`shaders/q3_deform.gdshaderinc`, uniforms from `q3shader.deform_uniforms`) in the world shaders and the props' `q3_stages` shaders: banners, the CTF flags and the armor's energy shell move. `normal` (lighting shimmer) is left out.
  - **Regenerating world materials:** 88 live `.tres` differ from what `make_godot_world.py` writes today (older hand patches). So regenerate into a scratch `--godot` folder and copy or patch only the materials meant to change. `make_pbr.py --only` overwrites `pbr_report.json` with just those stems; merge it back.
- **Exporting (`tools/export_game.py`):**
  - It's a debug export, because q3core has only a template_debug library. The `.pck` is separate (3.7 GB).
  - The game module goes to `<exe dir>/q3core/baseq3`. `Q3Host.bin_path()` looks there whenever the `editor` feature is absent.
  - `export/convert_text_resources_to_binary=false`. The binary conversion dropped `Q3GameMode.map_types`, so every map failed validation in the build.
  - The export ships only Godot's imported versions of PNG, JPG, WAV and font files, but the web view needs the raw files. `export_game.py` therefore packs every imported file under `res://webui` into `webui_assets.pck` (`tests/pack_web_assets.tscn`), and `Q3WebLayer._mount_assets()` loads it outside the editor. Without it every menu image is broken.
  - A live godot_wry web view keeps the process alive after Godot's exit, leaving a frozen window. `Session.quit_game()` frees the web views first. Quit and the window's close button both go through it (`auto_accept_quit` is false).
  - Export templates don't accept a scene path on the command line. To check a build, launch it with `map=<map> bots=<n>` (`Session.after_splash`, source "launch", which takes any arena) and read `--log-file`.
- **Headless runs don't compile shaders.** Check for shader errors with a windowed capture (`tests/capture.tscn -- scene=res://showcase/materials.tscn`).
- **Sky panoramas** use `u = atan(dir.x, dir.z)/TAU + 0.5, v = acos(dir.y)/PI` (sky_panorama.gdshader). The faces follow tr_sky.c's st_to_vec table: rt=+X, lf=-X, bk=+Y, ft=-Y, up/dn in id axes, mapped to Godot with x_q=Z, y_q=X, z_q=Y.
- **make_pbr world kinds:**
  - `tile`: wrap-padded, then cropped. The padding variable is `pad`, because `P` is the params dict.
    - Tiles get gentler settings than models (2026-10-23): a light denoise before the upscale and a mild sharpen, bumps from smoothed brightness at half strength, edge wear at 0.3. The model settings posterized grain into leopard-print blobs and gave floors about 30° bumps that crawled like worms under the lightmaps.
  - `glow`
  - `sky`

  Use `--jobs 4`: 4 workers is safe on 16 GB, where RAM exhaustion freezes the work PC.

## Main menu facts (hard-won)

- **Launch flow:** splash (fan-project disclaimer) -> id logo + intro movie on the first launch -> main menu. The main scene is `scenes/splash/splash.tscn`.
- **Layers:** the main menu is Godot's 3D showcase plus the banner5 logo (SubViewport), with the web menu (`webui/`, Next.js 16 static export) in a transparent godot_wry `WebView` on top.
- **Messaging:** `Q3MenuBridge` is the protocol: pure logic, and the self-test drives it headless.
  - Page -> Godot: `ipc.postMessage(json)` arrives as the `ipc_message` signal.
  - Godot -> page: `post_message` arrives as a document `message` CustomEvent, with the JSON in `.detail` (not `.data`).
  - The page can send before `page_load_finished` fires, so Godot queues its messages until the page has spoken.
- **Paths:** godot_wry serves `res://webui/x` as `http://res.webui/x`, so Next's absolute `/_next/...` paths work only while the build sits at `res://webui/`. `build_webui.py` copies it there.
- **Next.js 16:** read `webui/node_modules/next/dist/docs` before using Next APIs (`webui/AGENTS.md` says so).
- **Screenshots:** the web view is invisible to Godot's viewport and to `DisplayServer.screen_get_image`.
  - Use `tools/capture_window.ps1` (PrintWindow) for the web layer; it doesn't see the 3D.
  - Use `tests/capture.tscn` for the 3D; it pins 60 fps, because the default VSync setting is off and frame counts would otherwise run fast.
  - Don't toggle `always_on_top` while the web view runs, or it stops repainting.
- **Don't rebuild under a running editor:** a big template copy plus a re-import while the editor had the project open hung the editor (2026-10-22). Run the batch tools, then rescan per the live-editor rule above.
- **Editor bridge and imports (two editor crashes, 2026-10-22):**
  - Bridge calls can run inside the editor's import progress loop. A `reimport_files()` started while the editor is scanning or importing is a nested reimport, and it crashes the editor. Check `is_scanning()` and `is_importing()` first (`tools/bake_maps.gd` does).
  - Don't rebuild a scene file that's open in the editor and then reload and bake it in one call. Close it, rebuild, open, bake and save as separate steps (`bake_maps.gd` `rebuild`/`bake`/`save`, or `bake_all`).
  - Calls longer than 30 s time the bridge out, but the work goes on. Wait for files instead (a background loop on the outputs).
  - Godot ignores hand-written `.import` files it didn't create (the type stays empty). Let it import first, then edit its `.import` and reimport.
  - Edits to a `class_name` script on disk aren't seen by the running editor until it reloads; `bake_maps.gd rebuild()` reloads `Q3MapBuilder` itself.
- **Weapon attachment:** the rig's `weapon` and `head` bones keep only the tag's position and forward axis; their roll is whatever Blender chose. `rig.json` "tags" (`tools/rig_extras.py`) carries the full `tag_weapon` frame, and `Q3Character.attach(prop)` uses it.
- **Aim pitch:** `Q3Character.aim_pitch` (id degrees, positive looks down) drives an `Aim` SkeletonModifier3D, following CG_PlayerAngles. The torso's first bone takes 0.75 of the pitch, swinging after it; the head bone takes the rest. `tests/aim_test.tscn` shows three bodies side on.
- **Gun placement (Ozmium, 2026-10-23: "too distracting" near the center):** Quake 3 framed the gun for 4:3, so on a wide screen it sat almost in the middle. Gameplay > Widescreen gun placement (on by default) scales its camera-space x by aspect / (4/3); Gun side offset is cg_gunY. Check with `tests/weapon_fx.tscn -- out=... window=3440x1440` (the out folder must exist).
- **View weapon:** every gun is made once at match start (`_register_guns`). The hand pose eases into each torso animation over one frame time (CG_RunLerpFrame's initialLerp); `tests/weapon_switch_test.tscn` checks for snaps.
- **Ragdolls:**
  - Built from the skin, not bone names (`_ragdoll_plan`, cached per model): the rig fit misnames many limbs (Sarge's left arm is `belly_2..4`, Mynx's legs `hips_2..10`). Every bone with 40+ vertices gets a capsule fitted to its own vertices (principal axis, 80th-percentile radius, at least 5 cm); the trunk is pelvis to head, leg joints get hip, knee (hinge) or ankle limits by their height, everything else is an arm. Joints sit at the bone origins, where the animation turns each part.
  - **Godot makes a PhysicalBone3D's joint where the simulator last saw the bones** (its cached pose, not the live skeleton), so limits were measured from the dying animation. `_build_ragdoll` moves every body to the standing pose (hips turned to 30 degrees forward) and remakes each joint there. A joint the dying pose already breaks is remade in that pose (`_ease_joints`), or the solver yanks it apart.
  - Stability (Bones and Tank Jr flew apart): the pelvis is at least 8 kg and the trunk 4, no part outweighs its parent 2.5 times, and every part gets a rod's inertia reaching its farthest joint. Thin capsules otherwise spin to Jolt's 47 rad/s cap.
  - Parts collide with each other (layer 8) except pairs touching standing or at death. CCD is on.
  - Launch speed is capped at 8 m/s, and physics runs at 120 Hz.
  - A fragged player who respawns out of sight leaves the snapshot. `_remove` keeps that body as a corpse; before, it vanished before it could burn.
- **Local player events:**
  - Without `cg_predictItems` in the userinfo, the game sends item pickups, pain and death through `ps.externalEvent`. Q3Client plays it when it changes, as CG_CheckPlayerstateEvents does.
  - It also replays predictable-event slots the server changed (CG_CheckChangedPredictableEvents).
  - `tests/pickup_events.tscn` checks pickups against the log's `Item:` lines.
- **Pickup effect (remaster only):** when an item leaves the snapshot within 0.5 s and 2.5 m of a pickup event, `Q3Pickup.collect()` swells and shrinks it, and `Q3FX.pickup` bursts in the item's color. See `tests/pickup_fx_test.tscn`.
- **Music:** `Q3Music` plays CS_MUSIC (intro once, then the loop) on the Music bus, 10 dB under effects. The campaign postgame plays win or loss. See `tests/music_test.tscn`.
- **Menu music (remaster only, 2026-10-23):** `Q3MenuMusic` (`scripts/ui/q3_menu_music.gd`, made by `main_menu.gd`) shuffles the 12 songs (an intro and its loop are one song; win and loss stay out), each heard once per round; the order is static, so a match doesn't restart it. Setting `audio/menu_music` (on) reaches it through the `menu_music` group; it pauses under cinematics. See `tests/menu_music_test.tscn` (headless, 9 checks).
- **Levelshots:**
  - `tests/levelshots.tscn` renders 1280x960 shots from each remastered map's intermission point, with items placed, into `work/_levelshots`.
  - `make_ui.py` and `build_cards.py` prefer those over id's 128 px originals. q3tourney5 is shot without its fog.
- **Mouse 2:** it is bound to both `ui_cancel` (menu back) and `q3_zoom`. In a match, a mouse button never counts as `ui_cancel`, so right-click zooms instead of pausing. Tab + right-click is handled in `_input`, ahead of the scoreboard's clicks and of zoom (`tests/scoreboard_mouse_test.tscn`).
- **Field of view:** `_fov_x()` follows CG_CalcFov: the `video/fov` setting live, and +zoom to 22.5 over 150 ms. Sensitivity scales by fov_y/75 while zoomed (`tests/fov_test.tscn`).
- **New class_name files:** rescan in the live editor right after creating one. Otherwise the editor rewrites `global_script_class_cache.cfg` without it, and tests and exports fail with "Could not find type" (Q3Music, 2026-10-23).
- **Pause menu keys:** its web view is built with `forward_keys` false. A forwarded Escape arrived after the page had already resumed, and paused the game again.
- **Character draw modes:** player skins can be additive (Bones' xray, Orbb's tail, Razor's goggles, Slash's skates) or cut-out (Hunter's feathers, Uriel's wings). `textures.json` and `rig.json` "material_modes" record them, and the import script applies them.
- **Menu sounds:** played as ui_qmenu.c does.
  - **Move:** when the cursor changes item (hover or keys), when an item with an action activates, and when a value changes.
  - **In:** after a new menu's first frame.
  - **Out:** on Escape, Backspace or the right mouse button.
  - **Buzz:** at a slider's or list's end, and on refused actions.
- **Pickup shaders:** Quake 3's multi-stage model shaders survive on pickups and effect materials. These are environment-mapped chrome (`tcGen environment`), additive energy layers, and `tcMod` scale/rotate/scroll/turb with `rgbGen` waves.
  - `make_prop_shaders.py` writes `prop_shaders.json`, and the import script swaps those materials for `shaders/q3_stages*.gdshader`.
  - Painted weapon and map-object bodies keep their PBR maps.
  - Full-bright stages are doubled to match Quake 3's `r_overBrightBits 1` look, which made Regeneration red, not dark brown.
- **Showcase framing:**
  - The depth of field covers the subject's whole bounding radius. Using its height blurred long weapons seen end-on.
  - Items are framed on their center.
  - The logo slot is sent to Godot as fractions of the page, so display scaling can't shift it.
- **Cinematics:** RoQ 512x256 at 30 fps, stretched to 4:3 by the engine. `make_cinematics.py` uses imageio-ffmpeg's ffmpeg, which decodes RoQ and encodes Theora. Godot plays only Theora (`.ogv`).

## Online facts (hard-won)

- **Pieces:**
  - `online/`: the Worker (`src/index.ts` checks the signature) and one Durable Object `Hub` (`src/hub.ts`, SQLite plus hibernating sockets). See `online/README.md`.
  - `scripts/online/q3_online_client.gd` (`Q3OnlineClient`, one connection).
  - The `Online` autoload (`online.gd`: reconnect with backoff, presence from `Session._go`, profile updates).
  - The menu: `webui/components/Social.tsx` and `lib/social.ts`.
- **Messages:**
  - Bridge: page -> Godot `social {op, ...}`; Godot -> page `social {status, reason, state, chat}`, `social_notice`, `party_chat`.
  - The hub sends the whole picture (`social`) on every change; there are no deltas.
- **Running:**
  - **Live** since 2026-10-22: `wss://arena-online.fragnet.workers.dev` (Ozmium's personal Cloudflare account; `npm run deploy` in `online/`). That is `Online.DEFAULT_SERVER`.
  - Local hub: `cd online && npm run dev` on `ws://127.0.0.1:8787`.
  - Admin: `/admin/players` and `/admin/purge` with the `ADMIN_TOKEN` secret (token in `work/online_admin_token.txt`; online/README.md "Admin"). On 2026-10-23 every test player was purged from the live hub; only this PC's identity remained.
  - Tests: `npm run smoke [wss://...]` (24 checks; `HUB_ADMIN_TOKEN=... ` makes it remove its players) and `res://tests/online_test.tscn` (20 checks, local by default, `-- server=` for another).
  - Stopping a background `wrangler dev` task leaves node and workerd running. Kill the node `wrangler ... dev` process, then the workerd under `online
ode_modules`.
  - `node test/friend_bot.mjs` fills the Friends panel for a look.
  - The menu capture option `page=friends` opens the panel.
- **Leaderboards (2026-10-23):** `career` and `leaderboard` messages (online/README.md "Leaderboards"); `Online` sends the career on connect and after `Q3Profile.save()`; the menu page is `webui/components/pages/LeaderboardPage.tsx` (Play, Leaderboards; also from Ranked). The level board is self-reported; the ranked board only fills from peer-to-peer ranked matches whose players' signed scoreboards agree (Ozmium, 2026-10-23; verified dedicated servers later).
- **Identity:**
  - `user://online/identity.key` is the player. Deleting it makes a new player.
  - Node can sign as it (`friend_bot.mjs` does), which is handy for tests.
  - Every connect overwrites the stored name, model and color from the handshake headers.
- **Gotchas:**
  - Workers refuse non-handler exports from the entry module ("Incorrect type for map entry"), so constants stay unexported.
  - Settings save on a timer, so a test that changes one must call `Settings.save()` before quitting. Otherwise the change sticks and the reset is lost (the online test once left `network/online_server` pointing at localhost).
  - Names travel URI-encoded in `x-name`.
  - **Hostname lookups crash Godot's exit (2026-10-23):** a `WebSocketPeer` closed or freed while its host is still resolving never erases its IP queue item (WSLPeer::close skips `resolver.stop()`). Two items still waiting at exit crash the resolver thread inside `IP::~IP` (0xC0000005, no stderr, no dump). One unresolvable lookup only delays the exit about 3 s. `Q3OnlineClient` therefore resolves the host itself, erases its lookup on close and on free, and calls `connect_to_url` only once the host is in IP's cache. Any new `WebSocketPeer` user has to do the same.
  - The menu lifts Quake 3's `^0` and `^4` colors, which vanish on the dark glass.
  - A hidden Browser pane pauses Motion animations, so the dev preview looks blank until the pane is shown.

## q3core facts (hard-won)

- **What it is:** a C++ GDExtension running id's GPL game code from ioquake3 (64-bit clean). Godot does presentation. Plan and decisions: `GAMEPLAY_PLAN.md`.
- **Pinned sources:** `tools/q3core/thirdparty.json`, fetched by `tools/build_q3core.py`:
  - `ioq3`: what compiles.
  - `godot-cpp`: built with `api_version=4.7`.
  - `id-q3a`: id's original 1.32 release, used to audit ioquake3's game-code changes (`tools/q3core/AUDIT_ioq3_vs_id.md`).
- **id 1.32 gameplay:** `tools/q3core/patches/*.patch` put id's behavior back where ioquake3 changed play (13 reverts, each marked `q3core: id 1.32 (audit Rn)` in the code). `build_q3core.py` applies them after fetching and skips ones already applied.
  - To change a patch, edit `thirdparty/ioq3`, then run `git -C tools/q3core/thirdparty/ioq3 diff > tools/q3core/patches/0001-id-1.32-gameplay.patch`.
  - Never leave unpatched edits in `thirdparty/`: a refetch (`checkout -f`) drops them.
- **Build:** `python tools/build_q3core.py`. It runs SCons with MSVC 2022 and `-j4`; the first build compiles godot-cpp, about 10 min. The addon goes to `tools/godot_template/addons/q3core` and `godot/addons/q3core`.
- **Two libraries:**
  - `q3core.windows.*.dll` is the GDExtension. It holds ioquake3's dedicated-server engine (qcommon, server, botlib, null client) with `src/engine/q3_sys.c` in place of `sys_main.c`. There is no main(), and Sys_Error/Sys_Quit longjmp instead of exiting.
  - `bin/baseq3/qagamex86_64.dll` is the game module. The engine loads it the way ioquake3 does (FS_FindVM in `fs_basepath/baseq3`), so game and engine symbols never collide.
  - The ioquake3 sources compile unmodified. The MASM files are replaced by SSE intrinsics (`src/engine/q3_asm.c`, same instructions), and the QVM JIT is stubbed, so QVMs run interpreted.
- **Runtime:** `Q3Host.start()` (`scripts/game/q3_host.gd`) runs Com_Init.
  - It sets `fs_basepath` to the addon's `bin/`, `fs_homepath` to `user://q3`, `net_enabled 0`, `sv_fps 125`, `pmove_fixed 1` and `pmove_msec 8`.
  - It hands the engine the baseq3 folder the setup screen verified (`Q3Ownership.remembered()`) as `fs_steampath`. Without one, ioquake3 finds the Steam install itself. Either way the engine checks pak0 too.
  - It runs once per process.
- **Ownership gate (2026-10-23, Ozmium's design):** the splash shows `scenes/splash/setup_gate.gd` until a retail pak0.pk3 is verified, and the game can't go on without one.
  - The player drags pak0.pk3, another pak, baseq3 or the game folder onto the window (`Window.files_dropped`), or uses Browse.
  - `Q3Ownership` computes files.c's pak checksum (MD4 over the central directory's CRCs, digest words xored; Godot has no MD4, so it's in GDScript) against `pak_checksums`. The demo pak0 is refused.
  - It remembers the folder in `user://q3/ownership.cfg` (nothing is copied) and re-checks it every launch.
  - Tests: `ownership_test.tscn` (headless) and `setup_gate_test.tscn` (windowed; fires `files_dropped` itself). Both save and restore the remembered folder.
  - Don't make a gate test wait past the splash's fade: `after_splash` changes scene and frees the test before it prints. After an ERR_FATAL the engine is stopped until Godot restarts.
- **Server loop:** never call `SV_Frame` without a map. In DEDICATED builds it does `Sys_Sleep(-1)` and blocks forever; `q3_host_frame` checks `com_sv_running`.
- **Local player:** a loopback client set up like `SV_DirectConnect` that never reads its packets.
  - Usercmds go straight to `SV_ClientThink`.
  - Snapshots are read from `client->frames[]` after `SV_SendClientMessages`.
  - Reliable commands are copied out and acknowledged every frame. Unread, they would overflow and drop the client.
  - After a map change the host enters the world for it (what receiving the gamestate does).
- **Prediction:** `Q3Server.predict()` runs cgame's `CG_PredictPlayerState` loop using `SV_Trace`. It matches the server exactly except on server-side events (respawns, jump pads).
- **Reloading:** the extension is `reloadable`, so the open editor picks up a rebuild (`GDExtensionManager.reload_extension` forces it). The editor never starts the engine; only game runs and tests do. A crash in id's code would take the editor down.
- **Boundary:** id's C files never include godot-cpp headers. Everything crosses `src/q3_api.h` (plain C) and `src/q3_types.h`, because q_shared.h's macros collide with C++. `q3_types.h` copies the network structs, and `q3_host.c` checks their sizes at compile time.
- **Errors:** `Com_Error` longjmps back to the guarded API call (`GUARD` in `q3_host.c`), so an ERR_DROP never takes Godot down. Every new API entry point that can reach id's code needs that guard.
- **GDScript calls:** the self-test calls through `ClassDB.class_call_static("Q3Core", ...)`, so it still parses when the extension isn't built. Gameplay scripts use `Q3Server` directly.
- **Tests:** `tests/q3core_match.tscn` (`-- map=q3dm17 seconds=60 bots=3`) plays a headless match at about 25x real time. The self-test runs it for 20 s.
- **Not in the zip:** `tools/q3core/thirdparty/` (about 350 MB) and `build/` are refetched and rebuilt by the build script.

- **Weapon effects follow id 1.32's cgame** (`tools/q3core/thirdparty/id-q3a/code/cgame`, cvar defaults: cg_oldRocket/oldPlasma 1, tracerChance 0.4, brassTime 2500), but the rail keeps the spiral.
  - `Q3WeaponFx`: flash model on tag_flash for 20 ms (one frame minimum), held for the gauntlet and LG; light; barrel spin.
  - `Q3Client`: pellets re-traced with the server's seed; the LG beam and impacts use `Q3Server.trace` / `point_contents`; smoke puffs live in `Q3Puffs`.
  - The view weapon hangs on every frame of the hand md3's tag_weapon (`library/props/view_weapons.json`, `make_view_weapons.py`); that is the only recoil.
  - Check them with `tests/weapon_fx.tscn` (windowed, devmap + give all, frames per weapon).
- **The HUD** (`scenes/gameplay/hud/`, `Q3Hud`) draws everything itself (`_draw`, no textures but Quake 3's icons in `library/ui/hud`, exported by `make_ui.py`). Numbers use Barlow from `fonts/` with tabular figures.
  - New `class_name` scripts are unknown to test runs until the editor scans the project; run `EditorInterface.get_resource_filesystem().scan()` first.
  - `tests/weapon_fx.tscn -- move=1 preset=classic` captures the HUD while running, jumping and firing.
- **The pause menu is the web menu again:**
  - `Q3WebLayer` (`scripts/ui/q3_web_layer.gd`) owns a web view for both the main menu and Gameplay.
  - Gameplay's is made hidden and shown on Escape; `set_shown(false)` hands the keyboard back with `focus_parent()`.
  - Its bridge has `context = "pause"`, and `AppRoot` draws `PauseApp` for that context.
  - The init payload's `match` stays empty until the first pause, so pages must not assume it.
- **The campaign starts with `spmap`, not `map`:** `SV_Map_f` turns `g_gametype 2` back into FFA on a plain `map`, so no bots spawn.
  - With `spmap`, id's game does single player itself: the arena's bots after a delay (Training adds 10 s), and the arena's frag and time limits.
  - At the intermission the game sends `postgame ...` as a console command for q3_ui. The host keeps it, and `Q3Server.postgame()` reads it.
  - Gameplay judges the result as `ui_sppostgame.c` does: first place, ties included. Awards are accuracy at 50% or better, impressive, excellent, gauntlet, every 100 career frags, and perfect.
  - `Q3Campaign`: Training first, then tiers in order (`UI_GetCurrentGame`). It also grants campaign XP (`xp_lines()`).
  - Check it with `tests/campaign_flow.tscn` (headless; cheats; saves and restores your campaign and profile).
- **Chat and color codes:**
  - `Q3HudChat` sends `say "<text>"` / `say_team` through `Q3Server.client_command`; id's G_Say formats the line (`Name^7: ^2text`, EC 0x19 markers removed) and it comes back as `chat` / `tchat`. Quotes become apostrophes (the tokenizer ends the argument at one).
  - While typing, the usercmd is BUTTON_TALK only; usercmd actions held when the chat closes are ignored until let go (`Gameplay._held`), so the Enter that sent a line isn't Use item.
  - `Q3Colors` follows id 1.32's Q_IsColorString (`^` + any non-`^`), not ioquake3's alnum-only one; ^0 and ^4 are lifted as in the web menu. Standings rows carry `name` (with codes) and `plain` (for logic: bot cards, the announcer's `<name>_wins`).
  - Check with `tests/chat_test.tscn` (headless, 19 checks) and `tests/chat_capture.tscn` (windowed shots).
- **Anisotropic filtering** must be set on the viewport (`Viewport.anisotropic_filtering_level`): Godot reads the project setting only at startup, so the menu's 16x silently stayed at the 4x default until 2026-10-23.
- **Settings are all wired (2026-10-23):** no schema entry has `wired: false`; `tests/settings_wiring_test.tscn` (headless, 31 checks) fails if one comes back.
  - Game sounds play on the `Effects` bus and the announcer on `Announcer` (`default_bus_layout.tres`). Before, everything asked for an `SFX` bus that never existed and fell back to Master, so those volume sliders did nothing in a match.
  - Item timers read `Q3Server.items()`: each map item's taken flag and gentity `nextthink`, found through `offsetof` in `src/engine/q3_game_layout.c` (compiled from the same patched `g_local.h` as qagame).
  - Gun field of view scales the view gun's camera-space x and y by tan(view/2) / tan(gun/2), which is exactly that fov's projection.
- **Holding fire from the spawn never shoots** (bg_pmove.c `PMF_RESPAWNED`); test scripts press it after spawning. Headless runs need `gameplay.test_input = true`, because firing needs a captured mouse.
- **Physics is Jolt** (`physics/3d/physics_engine`); only ragdolls, gibs and brass use it.
  - Corpses stay 10 s, then `disintegrate()` burns them away (`shaders/character_dissolve.gdshader`).
  - Check them with `tests/ragdoll_test.tscn` (`models=a,b,c,d`, `shapes=1` draws the capsules, `seq=sarge` for a fall sequence with `push=x,y,z` and `wall=1`, `burn=1` for the burn). It prints FLEW APART when a part ends over 1.6 m from the pelvis; run all 29 models after changing the ragdoll.
  - Billboarded particle materials need `billboard_keep_scale`, or every quad is 1 m.
- **Script errors:** `tests/check_scripts.tscn` loads every script with autoloads up and lists parse errors.
  - The editor's Output and Errors panels keep every error since it started, stale ones included; `EditorInterface.restart_editor(true)` clears them.
  - Don't validate class_name scripts in the editor through `GDScript.new()` with a fake path: it leaves ghost script tabs that error on every start.
- **Seeing the web layer and the 3D together:** neither Godot's viewport nor a desktop screen grab (CopyFromScreen) shows both; the grab shows flat gray behind the web view, in the main menu too. Check the 3D with Godot's viewport image (`tests/pause_menu.tscn` saves one) and the page with a grab or the `npm run dev` preview (`?pause` mocks the pause menu).
  - **For a picture of both (README shots, 2026-10-23):** render the 3D with `capture.tscn ... window=1920x1080 page=<id>`, and the page in headless Chrome over CDP at 1920x1080 with `Emulation.setDefaultBackgroundColorOverride` alpha 0; then alpha-composite. The web view really is a transparent layer over the 3D, so this is what players see.
  - In the browser the mock draws placeholders (`DevBackdrop`, the "3D logo (Godot)" box); hide both before the shot.
  - The menu's left shade (88% black) sits over the Godot logo, so the logo is dim in the real menu too.
  - `-- window=WxH` (Settings `_forced_window`) overrides the saved display mode for one run. Ozmium's saved mode is fullscreen 3440x1440.

## Map facts (hard-won)

- **Steps:** `build_maps.py --godot ../godot` exports the GLB and JSON. The editor scan imports them. Then, in the live editor, `tools/bake_maps.gd`:
  - `configure(maps)` switches the GLB imports to static lightmaps (UV2) and reimports.
  - `tools/bake_runner.gd` (a node, not a coroutine: the editor reloads changed tool scripts, which kills a paused coroutine) rebuilds each scene (`Q3MapBuilder`), presses the editor's Bake Lightmaps button, saves, and writes `library/maps/bake_report.json`.
  - Baking needs the GPU editor; headless can't bake. All 35 maps take minutes.
- **BSP:**
  - Drawn triangles are stored clockwise. `q3bsp.surface_mesh` orients every triangle to its vertex normals, so the GLB is counter-clockwise as glTF wants.
  - The rotation to Godot axes (x = Y, y = Z, z = X) is proper, so it keeps winding.
  - UVs are the BSP's `st` as they are.
- **misc_model:** q3map bakes these into the BSP as triangle soup, so the entities are ignored. Their shaders come from `models/`, so `build_world.py` adds every non-`textures/` shader the stock maps use to the world library (99 of them).
- **Lighting:** q3map's model.
  - A point light sends `L * 7500` photons and a surface light `S * area * 0.25` per 128-unit patch, with brightness photons / d² on the 0-255 scale.
  - In Godot that is `E = photons / 255 / 32²` (`LIGHT_SCALE` 1.0), plus `q3map_sun` as a directional light and the sky's surfacelight as the bake's sky color (`SKY_SCALE` 4).
  - Calibrated on q3dm17: the bake's mean light is 0.23 against 0.18 for the original lightmaps (`report.q3_lightmap`, `bake_maps.gd measure()`).
  - All lights are bake-only (`BAKE_STATIC`), so the baked data must be saved in the scene, or the map renders black.
  - Since 2026-10-23, `build_maps.py` also follows q3map's `CreateSurfaceLights` for `cull none` shaders: emitters on both faces (console03 lights the recess behind its glass). Surfaces q3map wouldn't subdivide (within the shader's `q3map_lightsubdivide`, 999 by default in id's 1.32 `light.c`, not 120) also get the backsplash point light: the shader's `q3map_backsplash` (default 5% of the photons, 24 units out; `light5_5k` turns it off). The emitters stay on 128-unit cells, which the bake is calibrated on. Only q3tourney4 has been re-exported and rebaked with them; the other maps need `build_maps.py --json-only` and a rebake (each rebake adds LFS storage).
- **Captures:** `tests/capture.tscn -- scene=res://library/maps/<map>/<map>.tscn cam=intermission|spawn:<n>|x,y,z,lx,ly,lz`.
- **Mirrors and portals** (`Q3Portal`, built at runtime by `Q3Map._ready` from the JSON's "portals"):
  - `build_maps.py` groups surfaces whose shader has the `portal` keyword by plane and pairs each with its `misc_portal_surface` (within 64 units). It follows R_GetPortalOrientations, CG_Portal and locateCamera: the byte-packed direction, the roll, the 4-degree swing.
  - Each view is a SubViewport camera at R_MirrorPoint's eye, with an off-axis frustum whose near plane is the surface rectangle. `q3_portal.gdshader` draws the image by position, then the shader's stages.
  - `--json-only` re-exports the JSON without touching the baked GLB. Portal surfaces in the GLB (portal_sfx, bugmirror) are hidden with a discard material.
  - Layers: 18 is the local body (mirrors only; the main camera drops it), 19 the view gun (no mirrors), 20 the portal surfaces.
  - The views render with linear tone mapping and HDR, so the main view tone-maps once.
- **Reflection probes (2026-10-23):** `tools/map_probes.py` cuts each map's open space (64-unit grid, BSP leaf clusters, a wall check between cells) into room boxes; `build_maps.py` writes them as "probes" (`--json-only` is enough), and `Q3Map._add_probes()` adds box-projected `UPDATE_ONCE` probes at runtime. They capture only `Q3Map.WORLD_LAYER` (layer 11, set on the map's Geometry) and use `AMBIENT_DISABLED`, so the bake's lighting is unchanged. Atlas: 128 px, 80 slots (project.godot); at most 64 probes a map.
- **Light shafts:** `Q3Map._add_shaft_light()`: on sun maps (not blacksky) a real-time copy of the sun at 0.01 energy lights only the volumetric fog (`light_volumetric_fog_energy` compensates), shadowed by WORLD_LAYER, with a 0.0015 haze. Follows Volumetric fog and needs Shadows on; Quake 3's fog volumes are always drawn.
- **Graphics settings reach matches only through `Q3Map._apply_settings()`** (before 2026-10-23 no map called `Settings.apply_environment`, so Brightness, Bloom, AO, SSR and Volumetric fog did nothing in a match). `Q3PlayMap` (editor play-tests) still doesn't.
- **Remaster effects:** soft particles (`soft_fade` in `fx_sprite.gdshaderinc`, `fx_puff`; billboards only, distance scales with sprite size), heat shimmer (`fx_heat.gdshader`, `Q3FX.heat`; it draws the opaque screen copy, so its `render_priority` must stay below every other transparent), `Q3Sparks` (bounce off the map through `trace`), `Q3Burns` (lightning gun lines: decals stretched along the beam's hits, glow then fade). Comparisons: `tests/capture.tscn` `probes=0 shafts=0 heat=0 tonemap=`, `tests/weapon_fx.tscn sweep=25`.
- **Tone mapping:** maps stay Filmic (Ozmium, 2026-10-23: AgX drains the fire color, torches went pale white, and was about 25% darker); the menu showcase and material browser use AgX.
- **First spawn:** the local client's userinfo has `ip localhost`, as SV_DirectConnect gives it. Its first spawn therefore takes the spawn point marked initial (q3dm0: the mirror room).
- **setviewpos** is a teleport with a 400 ups shove forward. Tests that place the player need room, or must face away and turn.

## Replay facts (hard-won)

- **Plan and progress:** `REPLAY_PLAN.md`. Code: `tools/q3core/src/engine/q3_replay.c` (recorder, decoder, .dm_68 converter), `src/q3replay.cpp` (`Q3Replay`), `godot/scripts/replay/` (feeds, recorder, file library, viewer, chase cam, clip export), the replay sections at the end of `gameplay.gd`.
- **The file:** "Q3RP", version, JSON header, length-prefixed frame blocks (u32 size, u8 keyframe, i32 server time, msg.c bit stream), JSON trailer, u32 length, "Q3RT". The blocks are the index; a file without a trailer (a crash) plays to its last whole frame. A keyframe every 5 s; the first one holds every configstring.
- **What's recorded:** every linked, non-`SVF_NOCLIENT` entity, every active client's playerState (bots too), changed configstrings, and every client's reliable commands with a client mask, harvested every server frame from `client_t`'s 64-deep ring. SINGLECLIENT / NOTSINGLECLIENT flags are kept per entity, so a player's view drops the copies of their own predicted events (`SendPendingPredictableEvents`).
- **msg.c truncates fields** (eType 8 bits, eFlags 19, damagePitch 8 ...): a replay holds what a network client sees, so compare live values cut to the widths (`replay_test.gd` ES_BITS / PS_BITS). Not sent at all: ps.ping, externalEventTime.
- **msg.c's string writers turn `%` and bytes over 127 into `.`:** replay text goes through `write_raw` / `read_raw` (length + bytes).
- **The guard:** `GUARD` / `GUARD_V` and `q3_ready`, `q3_fatal_frame`, `q3_fatal_armed`, `q3_last_error` live in `engine/q3_engine.h` / q3_host.c, shared by q3_replay.c. Functions that must clean up a heap object on an error write the guard by hand (`q3_replay_open`, `q3_demo_convert`). Patch 0004 stops common.c turning a 4th ERR_DROP within 100 ms into ERR_FATAL while a guard is armed (the replay list reads demos back to back).
- **Playback without a server:** `Q3Replay.load_map` = `Hunk_Clear` + `CM_LoadMap`; traces follow `CG_ClipMoveToEntities`. Decoding, traces and conversion refuse while a match runs (an ERR_DROP would shut it down).
- **The feed:** Gameplay, Q3Client and the HUD (`match_feed`; the HUD's own `feed` is the kill feed) read a `Q3Feed`. `Q3LiveFeed` wraps Q3Server; `Q3ReplayFeed` steps frame by frame (events play once) or jumps (`Q3Client.reset()` + a muted rebuild) when the playhead goes back or more than 80 frames ahead. The watched view interpolates as `CG_InterpolatePlayerState`, other players via `Q3Client.set_next`.
- **Q3Client.reset() keeps every child made in `_ready`** (`_helpers`: puffs, sparks, burns, which have `clear()`); everything else is freed. After a jump, anything stamped with a later time must be forgotten (`Gameplay._forget_times`: the landing dip, the view guns' `Q3WeaponFx.reset`, the death cam).
- **The server never sends a client its own entity, or a follower the followed player's** (sv_snapshot.c): the death cam builds the body from the playerState (`Q3Client.own_body`), the spectator chase cam draws `_self` with `self_visible`, and demos get the POV body from `BG_PlayerStateToEntityState`.
- **Spectating is id's:** `team spectator`, `follow`, fire cycles (SpectatorThink). While following, the playerState is the followed player's copy with PMF_FOLLOW. Team switches are limited to one per 5 s (SetTeam). `g_readyUp` is turned off when the only human spectates (`G_EveryoneReady` needs one).
- **`teampref` reaches the game only through `Q3Host.userinfo()`** (it used to drop it): without one, team games start you on team spectator (g_session.c). Gameplay sends "auto" (PickTeam) or "spectator".
- **The `dmg` command** (patch 0002) is `dmg <target> <amount> <armor> <means> <time>`: damage numbers, and `Q3MatchStats`' per-weapon damage and accuracy (hits once per weapon per server time).
- **Movie Maker** (`Q3ClipExport`: a second copy with `--write-movie --fixed-fps 60`, the Gameplay scene from the editor, `clip=` through the splash in a build) writes frames at the project's base size (1600x900, `display/window/size/viewport_*`) whatever the window or `--resolution` says. Its AVI chunks are "00db" (MJPEG) and "01wb"; avih's frame count is written as the file closes, so a killed copy leaves 0.
  - Every frame the copy draws is a movie frame: the splash skips its disclaimer, loads and fades for `clip=`. The copy takes no input (Escape stops it), stays offline (`Online` sees `clip=`; it would share the player's identity), and quits on any error. `--quit-after` (range + 600 frames) and a wall-clock limit back that up.
  - Cancel is `OS.kill`, which `Diagnostics` would report as a crash next launch, so `cancel()` deletes the copy's session files.
- **Tests:** `replay_test` (every frame against the live server, seeks, size, the library, the bridge, playback in Gameplay; `-- seconds=600 compare=0` for the 10-minute size run), `spectator_test`, `death_cam_test` (windowed `-- out=` saves shots), `demo_test` (id's `demos/four.dm_68` from the player's pak8), `clip_test` (windowed), `replay_capture` (viewer shots), `scoreboard_test` (windowed shots).

## Map editor facts (hard-won)

- **Plan and reference:** `MAP_EDITOR_PLAN.md`; OZMIUM's editor in `C:\gAME\GAME` (start at `Docs/MapEditor-Handoff-2026-09-04.md`). Play-test compiles to BSP with id's q3map, and Bake bots is bspc (both in the pinned `id-q3a` source); nothing ports OZMIUM's trace world or nav.
- **Geometry:** `Q3MapGeo` (C++ in q3core, `src/map/brush_kernel.*`) in id space and doubles; planes are the truth. GDScript's Vector3 is 32-bit, so keep plane and point math in PackedFloat64Array or plain floats (a float32 volume check failed by 0.08 on a 4-million-unit brush).
- **Godot's JSON parser can read a double back one bit off,** so `Q3MapDoc` writes non-integer number lists as `"f64:"` + base64. PackedFloat64Array equality also tells -0.0 from 0.0; the kernel's snap turns -0 into 0.
- **Document objects are immutable once stored:** edit with `doc.edit(id)` (a deep copy), then `doc.put()`. Undo snapshots share objects by identity (`is_same`), and so do the mesher's winding caches.
- **Face mapping:** world-projected faces use q3map's QuakeTextureVecs (`tex`); explicit faces store texel axes (`uv`, 8 numbers). Material names are full shader names (`textures/...`) as in `world_materials.json`. `concrete4` is id's lava shader.
- **The world shaders ignore vertex color,** so per-face tint needs its own shader variant.
- **LightmapGI can't bake in an exported game** (Godot 4.7 class reference), so player maps get q3map's lightmaps.
- **Checks:** `tests/map_geo_test.tscn` (headless, 25 checks, also in the self-test); `tests/map_geo_room.tscn` through `tests/capture.tscn` (`view=patch` for the curve).
- **The editor scene (E1):** `Q3MapEditor.execute(cmd)` is the one entry for every command (keymap ids in `q3_editor_keymap.gd`). Tests drive tools with screen points from `ed.screen_of(id_point)` and set `Q3MapEditor.maps_dir` / `recent` to a scratch folder first (`tests/map_editor_smoke.tscn`, 27 checks; `tests/map_editor_shot.tscn` for captures, `sheet=1` / `dialog=1`).
  - Constants can't hold a `class_name` script (`const K := Q3EditorKeymap` is "not a constant expression"); use `preload(...)`.
  - The UI root under a CanvasLayer needs `set_anchors_and_offsets_preset(FULL_RECT)`; anchors alone kept its first size, so the bars stopped short of the window.
  - Checking scripts in the live editor: `s.source_code = FileAccess.get_file_as_string(p); s.reload(true)`. A plain `reload()` recompiles the editor's cached copy, so errors already fixed on disk come back.
- **E3 materials:**
  - The editor screen is a VBox (bars) around an HBox (browser | open view | inspector). The open middle and the containers are MOUSE_FILTER_IGNORE, so the 3D view under them gets the mouse.
  - `Q3MapMaterials.material(name, tint)` caches tinted copies by the tint's HTML code (8 bits a channel). Thumbnails come from the albedo path parsed out of the `.tres`, so its normal and ORM maps aren't loaded.
  - Favorites and recents are `Q3MapMaterials.prefs_path`, which tests redirect.
  - `paint_targets()`: the face tool's faces, else every face of the selected brushes plus the selected patches.
  - Don't pass a multi-line lambda followed by another argument (`return g,` then the next one): define it as a local first.
- **E5 compile and play-test:**
  - id's q3map and bspc are built 64-bit by `tools/q3core/SConstruct.tools` (part of `build_q3core.py`); fixes live in `patches/id-q3a/`. When you rebuild them, copy `bin/tools/*.exe` into the addons again (build_q3core does): the game once ran a stale copy that still had the startup crash.
  - q3map needs a folder named like "quake3" above `baseq3` (SetQdirFromPath), loose shader scripts plus `shaderlist.txt` (unpacked from the player's paks by `Q3MapCompiler.ensure_kit`), and textures from the paks through `Q3MAP_PAKPATH`. q3map's `-v` breaks its pak lookups (every texture "not found"); don't pass it.
  - Exit code 0xC0000409 from a tool is the C runtime's invalid-parameter fast fail as well as a stack cookie; 0xC00000FD is a stack overflow.
  - q3map flattens planes within 1e-5 of an axis (SnapPlane) and moves each surface's st by whole repeats toward 0. Compare texture coordinates on the snapped plane, apart from whole repeats.
  - Any entity outside the hull is a leak, a jump pad target too. `Q3MapEntities.apex_for` keeps the top under `ceiling_over()`.
  - Play-tests: `Session.playtest` carries the Q3MapDoc into Gameplay (`Q3PlayMap`) and back; tests that change scenes run on a second node added to the root (`tests/map_playtest_test.gd`), or the scene change frees them.
  - Shell heredocs here eat a backslash before a newline and turn `\n` into a real newline: use the Edit tool for code with line continuations or escapes.
- **E7 workflow:**
  - **Batch edits to many objects** (`doc.begin_batch()` / `end_batch()`, nesting): every `put` emits `changed`, and the editor redraws the selection overlay per signal. 5,000 unbatched puts were quadratic and ran Godot out of render IDs ("Element limit reached"). `execute()` batches every command; code outside it that loops over puts (tools, `transform_selection`, `place_copies`) batches itself.
  - **View filters** live in `Q3MapEditor.filters`. The mesher asks `face_filter` (material) and `hide_patches`; `pick`, Select all and box select skip `filtered(id)`.
  - **Publish:** compiles as `pub_<name>` (play-tests are `ed_<name>`) and is listed in `Q3MapPublish.path`. `Session.maps` is the arenas plus those, with `special: "custom"`, `doc` and `models`, and Gameplay draws them with `Q3PlayMap`. Their levelshot is a data: URL (`webui/lib/shot.ts`).
  - **Thumbnails** (`take_thumbnail`) render through a SubViewport on the same world_3d, with the editor's own drawing moved to layer 2. They're skipped headless.
  - **Shot scene:** `tests/map_editor_shot.tscn` clears its recent file first, or it reopens whatever the last capture saved.
  - **ItemList draws its focus stylebox over the items:** keep it border-only (`draw_center = false`).
- **E6 import and export:**
  - `Q3BspImport` (BSP) and `Q3MapImport` (.map) both make a Q3MapDoc; `Q3MapExport.to_map(doc, "brushdef" | "valve220")` writes .map. `tests/map_import_test.tscn` audits every map in the paks (`maps=a,b`, `report=`, `debug=1` prints the misses and the brushes there). A small Python reader of the raw lumps (brushes whose planes hold a missed surface's points) found the cause of every past miss.
  - BSP lumps: shaders 72 bytes (contents at +68), drawverts 44 (normal at +28), surfaces 104 (patch size at +96/+100), models 10 ints (surfaces at 6-7, brushes at 8-9), nodes 9 ints, leaves 12 ints (cluster first). Retail BSPs drop the .map's detail flag; it's read back from the leaf at the brush's middle (solid leaf = structural).
  - q3map2 BSPs (pro-* maps) add "noshader" bevels, some a hair off a real side, list a shader name once per flag set (match surfaces by shader name, not index), and merge coplanar sides into one surface (match by triangle).
  - q3map maps a brush entity's faces at points less its origin (AdjustBrushesForOrigin). The editor keeps world mappings: `Q3MapExport.entity_origin` / `model_space` on export, the inverse on import.
  - `uv_to_tex(..., tol)`: round values first, then exact. BSP fits use 0.05 texels; .map files 0.0001 (`Q3MapImport.EXACT`).
  - The editor's import runs on a Thread; `_process` joins it and emits `_import_done`, so a script error in the worker can't strand the coroutine. Tests `await ed.import_map(kind, source)`.
  - GDScript has no `%e`: use `String.num_scientific`.
  - PowerShell variable names ignore case: `$l = $L[$i]` overwrote the array and a line-rewriting script truncated `q3_bsp_import.gd` to 27 bytes (restored from `tools/godot_template`). Edit code with the Edit tool; scripted rewrites need distinct names.
  - `_fit_face` finds candidate surfaces through a 256-unit grid per model and shader (`NEAR_CELL`); scanning every surface of the shader made q3dm12's import 9 s (now about 1 s in the editor, where the audit is off).
- **E4 entities:**
  - `Q3MapEntities` is the class catalog and id's numbers (AimAtTarget's `push_velocity`, q3map's `light_params`); entities stay id's key/values. Brush entities own brushes through the brush's `entity` field.
  - `pick(screen, true)` includes point entities. Only the select and entity tools pass it, so the create, face and vertex tools never land on an entity's box.
  - `place_copies` relinks copies (entity ids and clashing targetnames) and returns the new ids; `group_all` puts a placed prefab in one group.
  - `Q3EditorThumbs` queues one render loop at a time. Two deferred loops started in one frame cleared each other's stage, and every tile showed the same gun. Frame on `mesh.get_faces()`, not `get_aabb()`.
  - Smoke-test clicks must miss the box at (64..192, 32..96, 0..96): a ray that hits it puts the entity on the box.
  - Prefabs live in `Q3MapPrefabs.dir`, which tests redirect.
- **E2 tools:** face tool (`faces`: `Vector2i(brush, face)`), vertex tool (`verts`: `Vector2i(object, index)` into `object_points()`; indices change on every hull rebuild, so moved corners are found again by position). Patches pick by ray against `doc.patch_tess(id)`.
  - Quake 3 patches are one-sided, so a curve seen from behind vanished in the editor. `Q3MapMesher.show_backs` draws back faces with a CULL_FRONT see-through material (editor only).
  - The smoke test's undo stack is full (100 steps) by the E2 section; tests that undo back to a point mark it with a `checkpoint()` label instead of counting steps.

## Diagnostics facts (hard-won)

- **What it is:** the `Diagnostics` autoload (`scripts/diagnostics/`, first in the list; Ozmium asked for it, 2026-10-23).
  - A `Logger` (`OS.add_logger`) catches every error and warning with its script backtrace and flushes them to `user://diagnostics/sessions/<pid>.errors.log`.
  - `<pid>.json` holds a heartbeat every 2 s with the scene, `context`, stats and breadcrumbs.
  - q3core writes `crash_<date>_<pid>.dmp/.txt` into `user://diagnostics/reports`.
  - At the next launch, a session whose process is gone becomes `crash_*.md` (with Godot's backtrace pulled from that session's own log by its `[diagnostics] session` marker), and the menu shows a `social_notice`.
  - A clean exit with errors keeps `errors_*.log`. F10 shows the overlay.
- **Native dumps:** Godot's Windows crash handler prints its backtrace and returns `EXCEPTION_CONTINUE_SEARCH`, so q3core's `SetUnhandledExceptionFilter` still runs after it. It writes the dump on a new thread, so a stack overflow gets one too, and it catches SIGABRT as well. q3core builds with `debug_symbols=yes`, and the PDB stays in `tools/q3core/bin`.
- **Godot's `OS.is_process_running(pid)` only knows processes Godot started** (Windows). Liveness uses `Q3Core.process_start_time(pid)` (OpenProcess + GetProcessTimes) against the session's start time, so a reused pid can't hide a crash and a busy game isn't reported as dead.
- **Never call RenderingServer (or other servers) from the watchdog thread:** with the main thread frozen it deadlocks, and the game then hangs at exit in `wait_to_finish`. `machine()` is read once at startup.
- **Adding an autoload under the open editor:** `ProjectSettings.set_setting("autoload/...")` reaches the game but not the editor's script globals, so every script using the name shows "Identifier not found". Use an EditorPlugin's `add_autoload_singleton()` instead (a bare `EditorPlugin.new()` works through the bridge).
  - That rebuilds every autoload. One that uses the new name before it exists fails ("Failed to create an autoload"), and loses its global in turn (Session did); remove and add it again the same way, and restore its order.
  - `restart_editor(true)` did nothing while a scene had unsaved changes.
- Wrap known long blocking work in `Diagnostics.expect_stall(true/false)` (tests that `OS.execute` children do). The freeze threshold is 15 s.
- **Check:** `tests/diagnostics_test.tscn` (headless, 8 checks, about 40 s). Child processes crash through a q3core null write, `abort()`, a stack overflow and `OS.crash`, plus a 17 s freeze. The reports stay, marked as test runs, with no menu notice.

## Pipeline facts (hard-won)

- **MD3 format:**
  - Header counts start at byte 76 (ident, version, name[64], flags, then counts); surface headers are 108 bytes.
  - Triangle winding is opposite the stored normals, so it gets flipped.
  - Normals are lat/lng bytes.
  - Vertices are int16 / 64.
- **Coordinates:** id Tech 3 is +X forward, +Y left, +Z up. The player origin sits 24 units above the feet. 32 units = 1 m (the GAME project's scale).
- **Leg frame numbering:** in lower.md3, the file frame is the cfg frame minus `skip`, where skip = LEGS_WALKCR.first − TORSO_GESTURE.first.
- **Subdivided UVs (2026-10-23):** Loop subdivision moves the vertices, so linearly interpolated uv let the paint slide across the rounded surface (zigzag waves on the railgun, up to ~100 texels). `meshops.subdivide_uv` gives each smoothed vertex the uv the md3 triangle shows straight under it, averaged per (vertex, uv island); `build_props.py` and `build_mesh_data.py` both use it. Check with a before/after `tests/prop_shot.tscn -- model=... window=1600x1000`.
- **Texture lookup** follows the engine: exact path, then the .tga/.jpg swap, then the first `map` of a shader with that name.
- **Rig fit:**
  - Skinning decomposition, run on lower and upper frames separately.
  - k-means seeding varies, so it keeps the best of 12 restarts. The seed is fixed, so results are reproducible.
  - Bones that cross the center line inherit their limb side (two-handed grips).
  - No links between bones on opposite sides.
- **Clip export:** Blender's glTF exporter (ACTIONS mode) writes keys for every bone in every clip. Filter tracks per half before layering legs and torso.
- **Texture maps:**
  - ORM layout is R = AO, G = roughness, B = metal. glTF and Godot use it as-is; Unity URP needs `unity_repack.py`.
  - Normal maps are OpenGL style. three.js with flipY = false textures needs normalScale.y = −1.
- **OpenCV:** the pip build can't read TGA; load images with Pillow.
- **Claude artifacts:** they serve text and media types only, so `pack_viewer.py` wraps binaries as base64 JSON.
- **Card art:**
  - **Render pass:** banners are rendered by `tests/card_renders.tscn` in a windowed Godot run (no GPU when headless). It reuses `Q3ShowcaseStage` (close framing, subject yaw −0.6 so the held gun points toward the card's middle, not over the face).
  - **Levelshots:** they're mostly 128 px (a few are 143, 512 or 640), so the bake normalizes them to 384 px.
  - **Skies:** Quake 3's 49 sky shaders share 7 cloud-layer looks.
  - **Godot log path:** pass Godot an absolute `--log-file` path; a relative one doesn't land in the shell's directory, so you end up reading a stale log.

## Trailer facts (hard-won)

- **q3cam** (`tools/trailer/q3cam/`): id's 1.32 cgame and game code plus `src/cg_q3cam.c`, built into QVMs by id's own `lcc` and `q3asm` from `thirdparty/id-q3a`. `build.py` copies the code out of thirdparty and puts the hooks in there. The QVMs come out within a few KB of retail pak8's.
- **Building id's 1999 toolchain with MSVC 2022 (x86):**
  - q3cpp's `outp` and `memmove` clash with MSVC intrinsics; they're renamed with `/D`.
  - q3cpp's `creat(name, 0666)` trips the CRT's mode check (0xC0000409), so it writes to stdout instead.
  - q3asm splits list entries at `:`, so the list names files relative to its folder.
  - cmdlib needs `user32.lib`.
  - lcc's driver adds `-D__LCC__`; without it q_shared.h's inline functions don't parse. Under lcc `VectorCopy` is a struct cast, so `VectorCopy(v + 3, ...)` fails; write `&v[3]`.
- **Retail capture:**
  - Fullscreen only: 1.32 turns overbright off in a window (R_SetColorMappings), and the map loses its look.
  - `cl_avidemo` saves each frame at the start of the next, so the first TGA is from before the path (`LEAD` in `capture_q3.py`).
  - `cl_avidemo 60` steps 16 ms (1000 / 60 truncated), so path time counts frames, not `cg.time`.
  - `con_notifytime 0` still draws a line printed that frame; use -1.
  - The mod's `team spectator` start, `cam_broadcast` (every entity `SVF_BROADCAST`) and the cleared area mask let a path go anywhere. Quake 3 writes `q3cam/q3config.cfg` and `q3cam/screenshots`, never `baseq3`.
- **Remaster capture:** `tests/trailer_shot.tscn` renders into its own 1920x1080 SubViewport (copying the root viewport's quality properties) and pipes raw RGBA into ffmpeg (`OS.execute_with_pipe`, blocking). Godot's Movie Maker records at the project's window size (1600x900) whatever `--resolution` says, and its PNGs were the slow part.
- **Game time vs wall time:** anything Quake 3 times by cg.time reads `Q3.game_ms` (each frame's delta added up). `Time.get_ticks_msec()` is wall time: in recordings that render slower than they play (Movie Maker, `--fixed-fps`), pickups spun and portal cameras swung far too fast. Fixed in pickups, portals, chat, the damage flash and the menu logo.
- **Paths:** the `.cam` spline is `cam_path.evaluate` and `CG_CamEval` (Hermite, Catmull-Rom tangents, flat ends); `trailer_shot.gd` `_eval` matches them. `setviewpos` can't fly a path: it's yaw-only and `TeleportPlayer` flips `EF_TELEPORT_BIT`, which turns interpolation off.

## Results so far

- **Keel:** 21 bones, legs 5.4 mm and torso 6.5 mm RMS. The original session is in `case_study_keel/`. Online viewer (Ozmium's account): https://claude.ai/artifact/8idHYcHtmEor4qy1ZbvWbT
- **All models:** all 29 player models convert with no manual fixes: 21–30 bones each, about 2–13 mm average error. `work/summary.json` has the numbers, and `work/index.html` is a gallery of every viewer.
- **Props, effects and catalog:** 87 props, 30 effect textures, 17 effects, 35 items and 48 map entity classes, all loading in `godot/`. The self-test passes 15/15, and the showcase was verified by screenshots.
- **World materials (done):**
  - 1,415 resources plus 49 skies in `godot/library/world`.
  - `world_materials.json` maps each Quake 3 shader name to its resource, size, light energy and color.
  - The self-test passes 17/17, and windowed captures of the material browser show zero shader errors.
- **Main menu (2026-10-22):**
  - Splash, live React menu over the 3D showcase, settings (132 entries, 10 tabs), match setup with 7 modes, the single-player campaign (8 tiers, 26 arenas), 10 cinematics, and Quake 3's menu sounds.
  - Gameplay and Map editor are stubs.
  - The self-test passes 39/39, including q3core's three checks, the two card checks and q3dm0's mirror and portal (see DEVELOPMENT.md).
  - Its exit crash (0xC0000005 after passing, 2026-10-22/23) is fixed: it was the settings round trip pointing `network/online_server` at "Selftest", which left hostname lookups waiting at exit (Online facts, "Gotchas"). 5 of 5 runs exit 0.
- **Player card catalog and art (2026-10-22):**
  - 378 parts and 89 achievements in `godot/library/cards`.
  - The data is `tools/card_catalog.py`, baked by `build_cards.py`; `Q3Cards` reads it.
  - `work/_cards/_sheet.jpg` shows every part's art.
  - The menu's Profile page (2026-10-23, `webui/components/pages/ProfilePage.tsx`, card renderer `PlayerCard.tsx`): Card editor (every slot, locked parts say how to earn them, NEW tags), Character, Achievements (progress, missing, what each pays) and Career tabs.
    - Godot owns the rules: `Q3Profile.menu_payload()` says what's earned, `set_card()` / `set_showcase()` refuse the rest. Bridge: `card_set`, `card_showcase`, `card_seen`, `taunt`; Godot answers `profile`. The catalog goes in init as `cards`.
    - Medals are stored as `medal.<award>` (what achievements read); `Q3Profile._migrate()` moves old flat `excellents`-style keys. Matches also record `matches_model`, `matches_skin`, `wins_mode`, `duel_wins` and `playtime`.
    - The browser preview's `?demo` swaps in a level-34 profile (`dump_menu_state.gd` DEMO); the dump never reads this PC's `user://profile.json`.
  - Ranked tiers are Bronze to Godlike (Ozmium, 2026-10-23). `Q3Cards.rank(rating)` gives tier and division; `Q3Profile.ranked()` reads the `ranked_rating`, `ranked_matches`, `ranked_wins` and `ranked_peak` stats (only the ranked backend may set them; for now from peer-to-peer matches whose players agree on the result, PLAN.md "Trust"). The Ranked page explains the system.
  - `build_cards.py --no-render` currently picks 3 different representative skies from the shipped catalog, so the ranked rename was spliced into `library/cards/cards.json` instead of taken from a rebuild. Look at the sky choice before the next full rebuild.
  - Still to do: `Q3Progress` (per-weapon frags, pickups, movement records from match events), the duel versus screen, the share image, and ranked (PLAN.md).
- **Next tool phases** (PLAN.md): the BSP/.map importer, then the map editor.
- **Highest-error models** (look at these first): klesk (12.8 / 10.8 mm), uriel (10.2 / 12.9), lucy (10.4 / 9.9), doom (6.3 / 10.7), orbb legs (11.2). Many are non-human shapes or have long tails and wings that bend more than rigid bones can follow.
