# Q3Remaster: context for Claude

Ozmium is planning a complete modern remaster of Quake III Arena in Unity or Godot (engine not chosen). **Current stage: Gameplay (approved 2026-10-22).** The asset pipeline and main menu are done; the Gameplay scene is being built in phases G0-G8 per `GAMEPLAY_PLAN.md` (G0 toolchain done). Read `PLAN.md` for phases and open decisions, and `README.md` for how the tools run.

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
- Full multiplayer with **no Steamworks and no paid hosting** (decided 2026-10-22): player-hosted matches over peer-to-peer WebRTC, optional dedicated servers, one match browser, and accounts, matchmaking and player data on Cloudflare's free tier. See PLAN.md "Multiplayer and online services". Friends and parties are built (`online/`, `scripts/online/`).
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
- **Material modes:** names carry `__add` / `__mask` / `__blend` / `__2s`. Additive materials swap the emission texture into albedo and render unshaded with additive blending.
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
- **Exporting (`tools/export_game.py`):**
  - It's a debug export, because q3core has only a template_debug library. The `.pck` is separate (3.7 GB).
  - The game module goes to `<exe dir>/q3core/baseq3`. `Q3Host.bin_path()` looks there whenever the `editor` feature is absent.
  - `export/convert_text_resources_to_binary=false`. The binary conversion dropped `Q3GameMode.map_types`, so every map failed validation in the build.
  - Export templates don't accept a scene path on the command line. To check a build, launch it with `map=<map> bots=<n>` (`Session.after_splash`, source "launch", which takes any arena) and read `--log-file`.
- **Headless runs don't compile shaders.** Check for shader errors with a windowed capture (`tests/capture.tscn -- scene=res://showcase/materials.tscn`).
- **Sky panoramas** use `u = atan(dir.x, dir.z)/TAU + 0.5, v = acos(dir.y)/PI` (sky_panorama.gdshader). The faces follow tr_sky.c's st_to_vec table: rt=+X, lf=-X, bk=+Y, ft=-Y, up/dn in id axes, mapped to Godot with x_q=Z, y_q=X, z_q=Y.
- **make_pbr world kinds:**
  - `tile`: wrap-padded, then cropped. The padding variable is `pad`, because `P` is the params dict.
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
- **View weapon:** every gun is made once at match start (`_register_guns`). The hand pose eases into each torso animation over one frame time (CG_RunLerpFrame's initialLerp); `tests/weapon_switch_test.tscn` checks for snaps.
- **Ragdolls:**
  - Capsules are sized from the skinned mesh around each bone (`_mesh_radii`, cached per model), so limbs rest on the floor instead of sinking.
  - Launch speed is capped at 8 m/s, and physics runs at 120 Hz.
  - A fragged player who respawns out of sight leaves the snapshot. `_remove` keeps that body as a corpse; before, it vanished before it could burn.
- **Local player events:**
  - Without `cg_predictItems` in the userinfo, the game sends item pickups, pain and death through `ps.externalEvent`. Q3Client plays it when it changes, as CG_CheckPlayerstateEvents does.
  - It also replays predictable-event slots the server changed (CG_CheckChangedPredictableEvents).
  - `tests/pickup_events.tscn` checks pickups against the log's `Item:` lines.
- **Pickup effect (remaster only):** when an item leaves the snapshot within 0.5 s and 2.5 m of a pickup event, `Q3Pickup.collect()` swells and shrinks it, and `Q3FX.pickup` bursts in the item's color. See `tests/pickup_fx_test.tscn`.
- **Music:** `Q3Music` plays CS_MUSIC (intro once, then the loop) on the Music bus, 10 dB under effects. The campaign postgame plays win or loss. See `tests/music_test.tscn`.
- **Levelshots:**
  - `tests/levelshots.tscn` renders 1280x960 shots from each remastered map's intermission point, with items placed, into `work/_levelshots`.
  - `make_ui.py` and `build_cards.py` prefer those over id's 128 px originals. q3tourney5 is shot without its fog.
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
  - Tests: `npm run smoke [wss://...]` (20 checks) and `res://tests/online_test.tscn` (17 checks, local by default, `-- server=` for another).
  - Stopping a background `wrangler dev` task leaves node and workerd running. Kill the node `wrangler ... dev` process, then the workerd under `online
ode_modules`.
  - `node test/friend_bot.mjs` fills the Friends panel for a look.
  - The menu capture option `page=friends` opens the panel.
- **Identity:**
  - `user://online/identity.key` is the player. Deleting it makes a new player.
  - Node can sign as it (`friend_bot.mjs` does), which is handy for tests.
  - Every connect overwrites the stored name, model and color from the handshake headers.
- **Gotchas:**
  - Workers refuse non-handler exports from the entry module ("Incorrect type for map entry"), so constants stay unexported.
  - Settings save on a timer, so a test that changes one must call `Settings.save()` before quitting. Otherwise the change sticks and the reset is lost (the online test once left `network/online_server` pointing at localhost).
  - Names travel URI-encoded in `x-name`.
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
- **Holding fire from the spawn never shoots** (bg_pmove.c `PMF_RESPAWNED`); test scripts press it after spawning. Headless runs need `gameplay.test_input = true`, because firing needs a captured mouse.
- **Physics is Jolt** (`physics/3d/physics_engine`); only ragdolls, gibs and brass use it.
  - Ragdoll bodies come only from the rig's main parts by name (`RAGDOLL_PARTS` in `q3_character.gd`), with per-part masses and limits; knees are hinges.
  - Corpses stay 10 s, then `disintegrate()` burns them away (`shaders/character_dissolve.gdshader`).
  - Check them with `tests/ragdoll_test.tscn` (`seq=sarge` for a fall sequence, `burn=1` for the burn).
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
- **Captures:** `tests/capture.tscn -- scene=res://library/maps/<map>/<map>.tscn cam=intermission|spawn:<n>|x,y,z,lx,ly,lz`.
- **Mirrors and portals** (`Q3Portal`, built at runtime by `Q3Map._ready` from the JSON's "portals"):
  - `build_maps.py` groups surfaces whose shader has the `portal` keyword by plane and pairs each with its `misc_portal_surface` (within 64 units). It follows R_GetPortalOrientations, CG_Portal and locateCamera: the byte-packed direction, the roll, the 4-degree swing.
  - Each view is a SubViewport camera at R_MirrorPoint's eye, with an off-axis frustum whose near plane is the surface rectangle. `q3_portal.gdshader` draws the image by position, then the shader's stages.
  - `--json-only` re-exports the JSON without touching the baked GLB. Portal surfaces in the GLB (portal_sfx, bugmirror) are hidden with a discard material.
  - Layers: 18 is the local body (mirrors only; the main camera drops it), 19 the view gun (no mirrors), 20 the portal surfaces.
  - The views render with linear tone mapping and HDR, so the main view tone-maps once.
- **First spawn:** the local client's userinfo has `ip localhost`, as SV_DirectConnect gives it. Its first spawn therefore takes the spawn point marked initial (q3dm0: the mirror room).
- **setviewpos** is a teleport with a 400 ups shove forward. Tests that place the player need room, or must face away and turn.

## Pipeline facts (hard-won)

- **MD3 format:**
  - Header counts start at byte 76 (ident, version, name[64], flags, then counts); surface headers are 108 bytes.
  - Triangle winding is opposite the stored normals, so it gets flipped.
  - Normals are lat/lng bytes.
  - Vertices are int16 / 64.
- **Coordinates:** id Tech 3 is +X forward, +Y left, +Z up. The player origin sits 24 units above the feet. 32 units = 1 m (the GAME project's scale).
- **Leg frame numbering:** in lower.md3, the file frame is the cfg frame minus `skip`, where skip = LEGS_WALKCR.first − TORSO_GESTURE.first.
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

## Results so far

- **Keel:** 21 bones, legs 5.4 mm and torso 6.5 mm RMS. The original session is in `case_study_keel/`. Online viewer (Ozmium's account): https://claude.ai/artifact/8idHYcHtmEor4qy1ZbvWbT
- **All models:** all 29 player models convert with no manual fixes: 21–30 bones each, about 2–13 mm average error. `work/summary.json` has the numbers, and `work/index.html` is a gallery of every viewer.
- **Props, effects and catalog:** 87 props, 30 effect textures, 17 effects, 35 items and 48 map entity classes, all loading in `godot/`. The self-test passes 15/15, and the showcase was verified by screenshots.
- **World materials (done):**
  - 1,415 resources plus 49 skies in `godot/library/world`.
  - `world_materials.json` maps each Quake 3 shader name to its resource, size, light energy and color.
  - The self-test passes 17/17, and windowed captures of the material browser show zero shader errors.
- **Main menu (2026-10-22):**
  - Splash, live React menu over the 3D showcase, settings (131 entries, 10 tabs), match setup with 7 modes, the single-player campaign (8 tiers, 26 arenas), 10 cinematics, and Quake 3's menu sounds.
  - Gameplay and Map editor are stubs.
  - The self-test passes 39/39, including q3core's three checks, the two card checks and q3dm0's mirror and portal (see README).
  - It exits with a segfault after passing (2026-10-22). Bisected to the main-menu web view checks in a headless run; not yet fixed.
- **Player card catalog and art (2026-10-22):**
  - 378 parts and 89 achievements in `godot/library/cards`.
  - The data is `tools/card_catalog.py`, baked by `build_cards.py`; `Q3Cards` reads it.
  - `work/_cards/_sheet.jpg` shows every part's art.
  - Renderers, the editor, `Q3Progress` (career stats, XP) and ranked are still to do (PLAN.md).
- **Next tool phases** (PLAN.md): the BSP/.map importer, then the map editor.
- **Highest-error models** (look at these first): klesk (12.8 / 10.8 mm), uriel (10.2 / 12.9), lucy (10.4 / 9.9), doom (6.3 / 10.7), orbb legs (11.2). Many are non-human shapes or have long tails and wings that bend more than rigid bones can follow.
