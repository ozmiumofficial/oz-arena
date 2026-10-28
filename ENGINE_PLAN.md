# Oz Arena on ioquake3: implementation plan (2026-10-28)

**Goal:** every feature Oz Arena has today, rebuilt on a modified ioquake3 engine instead of Godot. No web view anywhere: the menus, the HUD and every overlay are native, made to look like the current ones.

**Status:** plan only. Nothing is built yet. Decisions for Ozmium are at the end.

## Why (the r/ArenaFPS launch thread, 2026-10-28)

The technical criticism, once the "AI slop" noise is set aside:

| Criticism | Fair? | How this plan answers it |
|---|---|---|
| Rendering rebuilds meshes from the brushes and throws away the BSP, which is why id Tech 3 runs at 1000+ fps | Yes for rendering. Collision already traces BSP brushes in q3core | The engine draws the BSP itself: PVS, areaportals, lightmaps |
| Three runtimes during play: GDScript, a JS engine in a web view, and the QVM | Yes | One language family: C and C++ in the engine, game code in the QVM. No web view, no GDScript, no JS |
| Lighting is bright, flat and washed out; the fog reads as gas | Yes, five people independently | id's own lightmaps by default, darkness checked against the original renderer per map (P1) |
| Plasma reads as muzzle flash | Probably | Effects keep cgame's timings and the projectile's own model and light (P3) |
| ioquake3 is maintained; why not build on it or contribute? | Yes | This plan. Renderer fixes go upstream to ioquake3 as PRs |
| A modder wants their mod (rearmed) to run | | Standard mods load with `fs_game` as they do in any ioquake3 build |
| Copy reads as AI-written | | Player-facing text (README, release notes, posts, trailer captions) is written by Ozmium; Claude supplies a fact list only |

## Ground rules (unchanged from the Godot build)

- id's GPL game code decides play. The 12 patches in `tools/q3core/patches/` (id 1.32 reverts, Quake Live rules, ready-up, race, damage numbers, autohop, podium taunt, maps path, ...) carry over as they are.
- 125 Hz Pmove (`pmove_fixed 1`, `pmove_msec 8`, `sv_fps 125`).
- The online hub protocol (`online/src/hub.ts`) doesn't change, so friends, parties, ratings and leaderboards carry over, and players keep their identity.
- No "Quake" in player-visible text (CLAUDE.md rules). Internal ids may keep "q3".
- Matte materials: roughness floor 0.45.
- American spelling. Estimates in hours of Claude working time.
- Players must own the game: pak0.pk3 checked against id's retail checksum.

## Architecture

```
OzArena.exe  (ioquake3 fork: client + server + rend2, C)
 ├─ renderer: rend2 (OpenGL 3.x), patched: PBR channel layout, AO, bloom, colorblind filter,
 │            skeletal pose API, UI geometry API, external HDR lightmaps (already supported)
 ├─ sound:    OpenAL Soft (HRTF), Ogg Vorbis music; cinematics: RoQ + Theora (added)
 ├─ oz/ui     native UI: RmlUi 6 (MIT) + FreeType. Menus, pause menu, chat box, scoreboard cards,
 │            results, replay viewer controls, setup gate. Pages are .rml/.rcss in a pk3, logic in C++
 ├─ oz/online hub client (WebSocket + TLS), identity (RSA, imported from the Godot build),
 │            WebRTC transport NA_RTC (libdatachannel, relay-only ICE), direct map links
 ├─ oz/replay server-side all-player recorder, replay playback source, .dm_68 import, clip export
 ├─ oz/platform updater (q3core's q3_updater.c), crash reporter (q3_crash.cpp), ownership gate,
 │            data import from the Godot build
 └─ loads per match:
      game.qvm   id's game code + the 12 q3core patches (+ race, QL rules)
      cgame.qvm  id's cgame + Oz: skeletal players, ragdolls, effects, HUD, spectator and replay
                 cameras, death cam, race HUD and ghost, subtitles
      (q3_ui.qvm only when a mod brings its own UI)

OzEditor     the map editor (phase P13: first the Godot editor as a companion app, then native)
online/      the Cloudflare Worker + Hub Durable Object, unchanged except a build field
```

**The engine-to-cgame bridge.** cgame and the native UI talk over a small, versioned channel, the C equivalent of today's Godot-to-web bridge:
- cgame -> UI: `trap_Oz_Event(const char *json)` (scoreboard, results, kill events, medals, death cam card, race splits, replay markers).
- UI -> cgame: console commands (`oz_*`) and cvars, which cgame already reads.
- New syscalls use numbers from 400 up in both import tables, so mods that never call them are unaffected. They're listed in `engine/BRIDGE.md`.

**Why QVMs for game and cgame (decision 4):** they work with `sv_pure`, stay sandboxed, keep mod compatibility, and ioquake3's x86-64 compiler turns them into native code at load, so they're not an interpreter at play time. If profiling ever shows cgame's new work (ragdolls, cinematic camera) is too slow as a QVM, cgame alone moves to a native DLL.

## Repository layout

```
Q3Remaster/
  engine/
    thirdparty.json        pins: ioq3 fork, RmlUi, FreeType, libdatachannel (+ libjuice, usrsctp),
                           mbedTLS, cJSON, libogg/libtheora
    ioq3/                  the fork (ozmiumofficial GitHub fork of ioquake/ioq3, branch "oz"),
                           fetched at a pinned commit like tools/q3core does today
    src/oz/                new engine code: ui/, online/, replay/, platform/, bridge/
    ui/                    RML pages, RCSS styles, fonts, UI art -> oz/zz-oz-ui.pk3
    tests/                 boot, map, capture, online, replay, race and UI tests (Python drivers)
    build_engine.py        fetch, configure (CMake + Ninja + MSVC 2022), build, stage
    BRIDGE.md              syscall and event reference
  tools/make_pk3s.py       the asset library -> oz/*.pk3 (textures, models, sounds, shaders)
```

The game-code patches move into the fork as commits, one per patch, keeping their `q3core: id 1.32 (audit Rn)` markers. Until the Godot build retires, `tools/q3core` keeps its patch series and both builds use the same game code (decision 3).

**Toolchain on this PC:** Visual Studio 2022 Community (MSVC v143), CMake 4.4 and Ninja 1.13 (pip, installed 2026-10-28). ioquake3's CMake already builds the QVMs (`BUILD_GAME_QVMS`) and its own q3lcc and q3asm.

## Feature map

Every feature in README.md "In the game now" and "What's new", where it lives after the move, and its phase.

| Feature | Today (Godot) | ioquake3 home | Phase |
|---|---|---|---|
| Map rendering, PVS, lightmaps | Godot meshes + LightmapGI | Engine BSP renderer (rend2) | P1 |
| PBR materials (1,346 surfaces, liquids, fog, skies) | Godot materials | rend2 shader stages: `normalMap`, `specularMap`, `r_pbr 1`, cubemaps | P1 |
| Map lighting | LightmapGI relight | id's lightmaps; optional q3map2 relight with deluxe maps as `lm_NNNN.hdr` | P1 |
| 29 rigged fighters, team and alt skins, 60 fps animation | glTF skeletons, layered animation | IQM models + skeletal pose API + cgame layering | P2 |
| Ragdolls that react to the killing hit, burn-away | Godot physics | cgame Verlet ragdoll on `trap_CM_BoxTrace`, dissolve shader | P2 |
| Armor and powerup shells | Godot shaders | rend2 shader stages on `customShader` / extra refEntity | P2 |
| Remade effects + classic toggle | Godot particle scenes | cgame local entities + new shaders + bloom; `cg_fxStyle` | P3 |
| Original sounds, menu sounds, soundtrack shuffle | Godot audio | OpenAL; music playlist in the UI | P3 |
| 10 upscaled cinematics | Ogg Theora in Godot | Theora decode added to `cl_cin.c` | P3 |
| Main menu with the 3D showcase in a smoky void | React web view + Godot scene | RmlUi over a refdef scene drawn by the UI module | P5 |
| Settings with search, descriptions, live preview, share codes (131 entries) | React + `schema.gd` | Settings page generated from a schema file, all cvars | P4/P5 |
| Create Match (modes as data, modifiers), map browser | React | RmlUi page; `game/modes`, `modifiers.json` reused | P5 |
| Single-player campaign, tiers, medals, skill levels | `campaign.gd` + React | RmlUi pages + C++ campaign logic | P12 |
| Pause menu (any setting, crosshair editor) | React | RmlUi overlay over the running match | P5 |
| Modern HUD, 4 presets, speedometer with strafe gain, kill feed, damage arcs, medals | `q3_hud.gd` | cgame HUD | P6 |
| Damage numbers (running total) | Godot | cgame (game patch 0002 already sends them) | P6 |
| Chat box with history, team chat, name completion, color preview | Godot | UI overlay + client chat field | P6 |
| Crosshair editor, per-weapon crosshairs, share codes | React + Godot | Drawn by cgame from cvars; editor in the UI with the live match behind it | P6 |
| Scoreboard with stats and player cards; results screen | Godot + React | cgame sends stats over the bridge; UI draws them | P6 |
| Competitive options (item timers, bright skins, forced enemy model, gun position, FOV, pitched hit sounds, mouse accel, lagometer) | Godot | cgame cvars (most exist in some form in cgame already) | P6 |
| Drop-down console, binds, configs | Godot console reimplementation | The engine's own console (it's the original) | P0 |
| Controller support, southpaw | Godot input | SDL game controller in `sdl_input.c`; UI focus navigation | P4 |
| Friends, parties, presence, party chat, invite links | `Online` autoload + React | `oz/online` + UI | P7 |
| Leaderboards (level and ranked, global and friends) | React | UI page, same hub messages | P7 |
| Host from your PC, listed with optional password | Godot + q3core | Listen server + `NA_RTC` + hub directory | P8 |
| Server browser with search, region and mode filters | React | UI page | P8 |
| Quick Play (Duel, FFA, TDM, CTF), regions with Auto widening | Godot + hub | `oz/online` + UI | P8 |
| Ranked Duel/TDM/CTF, Elo, seasons, tiers, signed scoreboards | Godot + hub | `oz/online` signs; hub unchanged | P8 |
| Every connection relayed, no IPs | WebRTC relay-only | libdatachannel, relay-only ICE | P8 |
| Map download from host over an opt-in direct link | `Q3MapFetch` | `oz/online` file channel + direct link | P8 |
| Vote kick with a 30-minute lobby ban | game + hub | game code (callvote) + hub | P8 |
| Dedicated server from the menu, keeps running after the game closes | Godot headless | `OzArena.exe +set dedicated 2` (or `OzArena-server.exe`) launched by the UI | P9 |
| LAN with no internet | UDP patch 0007 | ioquake3's own UDP and LAN discovery | P9 |
| Linux and macOS builds (planned) | Not built | ioquake3's CMake targets | P9 |
| Every match recorded; any POV, chase, orbit, free cam, cinematic cam | `q3_replay.c` + Godot viewer | `oz/replay` recorder + replay source + cgame cameras | P10 |
| Replay viewer: pause, slow, fast, frame step, next/last frag, timeline markers, name tags, outlines | Godot | cgame + UI controls | P10 |
| Replays page: rename, favorites, watch from results | React | UI page | P10 |
| Clip export to video | `q3_clip_export.gd` | Engine `video` path to Media Foundation H.264 (Windows), AVI elsewhere | P10 |
| .dm_68 demo playback | q3core converter | Native: ioquake3 plays demos | P10 |
| Spectating, chase, auto-director, cinematic broadcast camera | Godot | cgame | P10 |
| Death cam with the killer's card | Godot | cgame camera + UI card | P10 |
| Race: timer, splits, restart, save spot, ghost, records, Defrag courses | patch 0012 + Godot | game patch (kept) + cgame HUD + ghost + client records | P11 |
| XP, levels, 89 achievements, ~380 card parts | `q3_profile.gd`, `q3_cards.gd` | C++ in `oz/ui` + card art pk3 | P12 |
| Map editor (brushes, curves, materials, entities, lights, prefabs, race tools, compile, play-test, BSP/.map import and export, publish) | Godot scene | Companion app first, then native editor mode | P13 |
| Maps folder (drop pk3s beside the exe) | patch 0011 | Same patch | P0 |
| Ownership gate (drag pak0 onto the window) | Godot | SDL drop event + `fs_steampath` / `fs_gogpath` autodetect + checksum | P14 |
| In-game updater, texture pack download | `q3_updater.c` + Godot | Same C updater, now staging pk3s | P14 |
| Crash reports, diagnostics overlay | `q3_crash.cpp`, Godot | Same C++ crash code; cgame/engine overlay | P14 |
| Colorblind filters, announcer subtitles, reduced flashing | Godot shaders + HUD | rend2 post-process + cgame | P14 |
| Splash disclaimer, Credits | Godot + React | UI pages | P5 |

The README's "Planned features" (Quake Live movement, Clan Arena, Freeze Tag, Instagib, the Workshop, editing together) aren't part of this port. They're game-code or hub work that lands on whichever build is current.

## Phases

Each phase ends with automated checks and, where you can see it, captures next to the Godot build's.

### P0. Foundation (2 h)

1. Fork ioquake/ioq3 on GitHub as `ozmiumofficial/ioq3` (branch `oz`), pinned to the commit q3core uses (`87a99d18`).
2. Replay the q3core patch series as commits: 0001-0005 and 0008-0012 as they are. 0006 (Godot transport) and 0007 (UDP for Godot) are replaced by P8's `NA_RTC`.
3. `engine/build_engine.py`: fetch the pins, run CMake with `-G Ninja` under `vcvars64`, `USE_FREETYPE ON`, `BUILD_GAME_QVMS ON`, renderer gl2 as default (`cl_renderer opengl2`). Stage `build/engine/` as `OzArena.exe`, `renderer_opengl2_x86_64.dll`, and `oz/` with `pak_oz_code.pk3` (the QVMs).
4. Standalone identity: `com_basegame baseq3` reads the player's pak0 from their install; `fs_game oz` holds everything Oz. Window title, icon (`godot/icon.ico`), product name "Oz Arena". No "Quake" in the window title or console banner.
5. Checks: boots to the console; `devmap q3dm17` with 3 bots runs; a 60-second headless bot match matches q3core's frag and item log on the same seed (proves the game code is identical).

### P1. World look (3 h)

The thread's loudest complaint, so it comes first and has a hard acceptance test.

1. **Materials.** `tools/make_pk3s.py` writes the world library as rend2 shaders:
   - Base color as BC7 DDS (`tr_image_dds.c` loads it). Normal maps as `normalMap` (BC5).
   - Specular map in rend2's PBR layout: R = gloss, G = metal (`lightall_fp.glsl`, `USE_PBR`), converted from our ORM. `r_glossType` set to roughness (`GLOSS_IS_ROUGHNESS`), and roughness clamped at 0.45.
   - **Engine patch: ambient occlusion** from the specular map's B channel (rend2 has no AO input).
   - The original shader scripts keep their stages, blend modes, deforms, tcMods and sort, so liquids, fog volumes, skies, flares and portals behave as id made them. Only image stages are swapped and PBR keywords added.
2. **Lighting.**
   - Default: id's lightmaps from the BSP, `r_mapOverBrightBits 2`, `r_hdr 1` with a fixed tonemap tuned to the vanilla look (auto exposure off by default).
   - Optional per map: q3map2 relight from id's own light entities with `-deluxe -external` at a higher lightmap scale, written as `maps/<map>/lm_NNNN.hdr` (rend2 already loads these, `tr_bsp.c`). It's the same lights, so the same mood, and the deluxe maps let normal maps respond to baked light.
   - No global volumetric haze. Fog is the maps' own fog volumes only.
   - Cubemaps baked per map into `cubemaps/<map>/env.json` (rend2's format), at item and spawn positions.
3. **Bloom and the remastered sky.** An engine patch adds a bloom pass, built from `tr_postprocess.c`'s existing blur, for glowing rails and lightning (off in the classic look). The space panorama becomes a cubemap skybox for q3dm10/17/19, q3ctf4 and the q3tourney6 maps.
4. **Acceptance (automated, `engine/tests/look_test.py`):**
   - For each of the 35 maps, five fixed camera spots are captured with the vanilla gl1 renderer and with Oz.
   - Mean luminance must land within 10% of vanilla, and the dark-pixel share (luma under 15%) within 5 points. This is the "dark and moody" check.
   - Frame time at 1920x1080 with everything on: at least 3x the Godot build's fps on the same spots.
   - A side-by-side sheet goes to Ozmium for the eye test before P2 starts.

### P2. Fighters (4 h)

1. **Export.** The model pipeline gains an IQM export after `build_rigged` (Blender's IQM exporter from the iqm repo, or a small Python writer from the GLB). Per model: mesh, joints, all clips at 60 fps, material names matching `.skin` files for team and alternate skins. `tag_weapon`, `tag_head` and `tag_flag` become joints.
2. **Engine patch: skeletal pose API.**
   - `trap_R_BuildPose(model, layers[], out)` blends clips per bone mask, with weights and frame lerp.
   - `trap_R_AddRefEntityWithPose(ref, mats, count)` draws an IQM with cgame-supplied bone matrices.
   - Both go in `tr_model_iqm.c`. ioquake3 already skins IQM on the GPU path in rend2.
3. **cgame players.**
   - `CG_Player` on the single skeleton: legs and torso layers by bone mask (split from `rig.json`), torso yaw and pitch twist spread across the spine bones the way `CG_PlayerAngles` turns tags.
   - Weapon on the `tag_weapon` joint. Head look-at.
   - Team skins via `.skin`, bright enemy skins and the forced enemy model as cvars.
4. **Ragdolls.**
   - A Verlet particle-and-constraint ragdoll in cgame (about 15 particles, distance and angle limits per joint), moved at 125 Hz.
   - It collides with the world through `trap_CM_BoxTrace` and gets the killing hit's direction and knockback from `EV_OBITUARY` and the damage event.
   - Bone matrices come from the particles. It's client-side only, cosmetic and deterministic per client.
   - Then the burn-away: a dissolve shader stage driven by `shaderTime`.
5. **Shells:** armor force field and powerup shells as extra refEntities with their shaders.
6. **Checks:** all 29 models and skins render (capture sheet); animation states match the Godot build's clip names; 200 ragdoll deaths on q3dm6 never end inside a wall (trace check), and none explode (bone stretch limit).

### P3. Effects, sound, cinematics (2.5 h)

1. Port `fx.json` and the 30 effect textures to cgame. Rail rings and spiral, lightning beam, missile trails with a burning core, explosions with embers and smoke, impacts, blood. All on cgame's original timings (`cg_weapons.c`, `cg_effects.c`), with `cg_fxStyle 0|1` for classic or remade. Plasma keeps its own projectile sprite and light so it reads as a projectile.
2. Sounds: OpenAL Soft with HRTF as an option. Hit sounds pitched by damage (cgame). Music: Ogg playlists for the menu and matches.
3. Cinematics: add a Theora decoder (libtheora) behind `cl_cin.c`'s API so the 10 upscaled `.ogv` files play at launch, from the menu and after campaign tiers. RoQ stays for mods.
4. Checks: an effects capture sheet per weapon in both styles; `cinematic intro.ogv` plays to the end at the right length.

### P4. Native UI runtime (3 h)

1. **RmlUi 6 + FreeType** linked into the client. A render interface on new renderer entry points:
   - `RE_UI_CompileGeometry` / `RenderGeometry` / `ReleaseGeometry` (VBOs).
   - Scissor, transforms, textures through the engine image loader.
   - Layers and filters on rend2 FBOs, for blur and drop shadows on the glass panels.
2. **Input:** engine key and char events go to RmlUi while `KEYCATCH_UI` is set. Mouse capture follows the menu. The controller drives spatial focus navigation (`nav-up/down/left/right` in RCSS), with A/B/Y as in the Godot menu.
3. **Data and logic:** RmlUi data models bound to C++ structs; page logic in C++ under `src/oz/ui/`. No scripting language.
4. **Settings schema:** `schema.gd`'s 131 entries are exported once to `ui/settings.json`. Per entry: cvar name, type, range, default, category, description and preview kind. The engine creates the cvars from it, and the Settings page is generated from it. Search, descriptions, live preview and share codes (the same code format as today) all come from the schema.
5. **Sounds:** `menu1-4.wav` played exactly as `ui_qmenu.c` does, through `S_StartLocalSound`.
6. **Test hooks:** `ui_page <name>` and `ui_capture <file>` console commands, so every page can be shot headless.
7. Checks: a settings round trip (change, restart, still set); controller navigation reaches every control on every page (automated focus walk).

### P5. Menus (5 h)

Recreate the React pages one by one as RML and RCSS, keeping their layout, type, colors and motion (RCSS transitions and animations stand in for Motion). Source pages are under `webui/components` and `webui/components/pages`:

| Page | Source |
|---|---|
| Splash with the fan-project disclaimer | `scenes/splash` |
| Main menu: banner "Oz Arena", items on the left, update prompt, queue bar | `MenuApp.tsx`, `MenuItem.tsx`, `BannerText.tsx`, `UpdatePrompt.tsx`, `QueueBar.tsx` |
| Showcase: one random fighter or item in a dark smoky void, slow turn, drifting camera | `showcase_stage.gd` -> the UI draws a refdef scene with sprite smoke before the RML layer, as id's UI drew models |
| Play: Quick Play, Ranked, Server Browser, Create Match, Dedicated | `PlayPages.tsx`, `RankedPage.tsx`, `CreateMatchPage.tsx`, `MapBrowser.tsx`, `DedicatedPage.tsx` |
| Single player | `SinglePlayerPage.tsx` |
| Race | `RacePage.tsx`, `RaceBoard.tsx` |
| Replays | `ReplaysPage.tsx` |
| Profile and card editor | `ProfilePage.tsx`, `PlayerCard.tsx`, `RankIcon.tsx` |
| Leaderboards | `LeaderboardPage.tsx` |
| Settings and crosshair editor | `SettingsPage.tsx`, `CrosshairEditor.tsx`, `Crosshair.tsx` |
| Cinematics, Credits, other pages | `CinematicsPage.tsx`, `OtherPages.tsx` |
| Friends panel and party strip | `Social.tsx` |
| Pause menu | `PauseApp.tsx` |

Checks: each page captured headless next to the web view's capture (`webui/lib/shot.ts`), and Ozmium signs off on the sheet.

### P6. HUD and in-match UI (4 h)

1. **cgame HUD** (port `q3_hud.gd`):
   - Four presets.
   - Speedometer showing strafe-jump gain, kill feed, damage direction arcs, award medals.
   - Damage numbers as one running total per target, item respawn timers, lagometer.
   - Text through `trap_R_RegisterFont` (FreeType build) with the same fonts.
2. **Crosshairs:** per-weapon crosshairs drawn from cvars, with share codes decoded in cgame. The editor lives in the pause menu, so the live match shows behind it.
3. **Chat box:** history, team chat, name completion and a live color preview. The UI overlay owns the input field; the engine's `CL_GetServerCommand` feeds it lines. Colored names everywhere.
4. **Scoreboard and results:** cgame sends per-weapon damage and accuracy, distance and medals over the bridge (`q3_match_stats.gd`'s data). The UI draws them with player cards.
5. **Competitive options:** bright skins, forced enemy model, gun position, FOV, pitched hit sounds, raw input with optional mouse acceleration.
6. Checks: a HUD capture per preset; a scripted match fills the scoreboard's stats and they equal the server log's.

**First playable build after P1-P6:** offline matches against bots look and play right, with the full menu.

### P7. Online services (2.5 h)

1. **`src/oz/online/hub.cpp`.** A WebSocket over TLS to `wss://arena-online.fragnet.workers.dev`, using libdatachannel's WebSocket client and mbedTLS. Reconnect with backoff, presence, the same message set as `Q3OnlineClient` and `online.gd`.
2. **Identity:**
   - On first run, `identity.key` is imported from the Godot build's user folder, so friends, level and rating carry over.
   - RSA-2048 signing with SHA-256 and PKCS#1 v1.5 through mbedTLS, the same signature as Godot's `Crypto.sign`.
   - A fresh key is generated only when there's nothing to import.
3. **UI:** Friends panel (friend codes, presence, join), parties of up to 8 with chat, invite links (`ozarena://` URL handler, registered at install), leaderboards.
4. Checks: `npm run smoke` against a local hub plus a new `engine/tests/online_test.py`. Two engine clients add each other, form a party and chat. One Godot client and one engine client do the same (the hub is shared).

### P8. Online play (3.5 h)

1. **Transport `NA_RTC`.** A new address type in `net_ip.c`. Packets go over a libdatachannel data channel (unordered, unreliable), with ICE limited to relay candidates and Cloudflare TURN credentials minted by the hub. Signaling uses the hub messages the Godot build already uses (`net_request`, `net_signal`, ...). The rest of id's netcode (netchan, delta snapshots, prediction in cgame) is untouched, because it's the real client now.
2. **Directory:** hosting from Create Match registers a listing (mode, map, players, region, password, version, ranked flag); the server browser and Quick Play read it. Region rules and Auto widening as in MULTIPLAYER_PLAN.md.
3. **Ranked:**
   - At match end, cgame sends the final scoreboard over the bridge. `oz/online` signs it and reports it.
   - The hub's agreement check and Elo are unchanged.
   - Listings carry `engine: "ioq3"`, so ranked never mixes the Godot and engine builds.
4. **Map download:** a second reliable channel for requests; the pk3 travels only over an opt-in direct link, as today.
5. **Vote kick:** id's `callvote kick` plus the hub's 30-minute lobby ban.
6. Checks: two engine processes through the local hub with `test/lag` latency and loss; a full duel through the live relay between two PCs; a ranked duel recorded on the leaderboard; a joiner fetching a Maps-folder pk3.

### P9. Dedicated servers and other platforms (2 h)

1. The Dedicated page starts `OzArena.exe +set dedicated 2 +exec <config>` detached, so it outlives the game. It shows rcon, the admin view and status from `oz/online`, and "start with Windows" is a scheduled task.
2. LAN: ioquake3's own UDP and LAN broadcast, unchanged.
3. Linux x86-64 dedicated server and client, and macOS (arm64 and x86-64), from ioquake3's CMake targets. libdatachannel and RmlUi build on all three.
4. Checks: a Linux dedicated server under WSL hosts a match a Windows client joins; a macOS build boots to the menu (needs a Mac or CI runner).

### P10. Replays, spectating, death cam (4 h)

1. **Recorder:** port `tools/q3core/src/engine/q3_replay.c` into the server (`sv_oz_replay.c`). Every match writes every player's state, all entities and server commands, with a keyframe every 2 s for seeking.
2. **Playback source:**
   - The client plays a replay as a demo whose point of view can change: it rebuilds a snapshot for the chosen client number each frame and hands it to cgame through the normal snapshot syscalls, so cgame doesn't know it's a replay.
   - Seeking jumps to a keyframe and fast-forwards. Speed comes from `timescale`, and frame stepping from a paused clock.
3. **Cameras in cgame:** first person, chase, orbit, free camera, and the cinematic broadcast camera (port `q3_cine_cam.gd`). The live spectator auto-director uses the same cameras.
4. **Viewer UI:** timeline with frag, death and medal markers, next and last frag, name tags and outlines (an outline shader stage). The Replays page: rename, favorites, watch from the results screen.
5. **Clip export:** frames from the `video` path go to the Windows Media Foundation H.264 encoder (built into Windows, so no extra download), and to MJPEG AVI on other platforms.
6. **Demos:** `.dm_68` plays natively. The existing converter stays for importing them into the Replays page.
7. **Death cam:** the camera follows the corpse, then shows the killer's card.
8. Checks: record a 2-minute bot match, then replay it from each player's view. Positions must match the server log within 1 unit at every keyframe. A seek lands on the right tick, and a clip export gives a playable MP4 of the right length.

### P11. Race (1.5 h)

The game code is already done (patch 0012: Defrag entities, run timer, `g_race`, save spot). This phase adds:
- the cgame HUD: timer, splits against your best, checkpoints;
- R to restart;
- the ghost: the run is recorded client-side and drawn as a translucent player;
- local records per course (`q3_race_records.gd`'s format, imported);
- Defrag courses from the Maps folder.

Check: a scripted run on a test course gives the same time as the Godot build's on the same input file.

### P12. Single player and progression (2.5 h)

1. **Campaign:** port `campaign.gd` (Training, six tiers, the final duel with Xaero, medals, skill levels, tier movies) onto `arenas.txt` and the bots, with the UI pages from P5.
2. **Progression:** port `q3_profile.gd`, `q3_match_stats.gd` and `q3_cards.gd` to C++ in `oz/ui`.
   - XP for every match played to the end, levels, and the 89 achievements.
   - About 380 card parts, with art from `build_cards.py` packed into `zz-oz-cards.pk3`.
   - The profile is imported from the Godot build, and the career syncs to the hub as today.
3. Checks: finishing a scripted bot match grants the same XP and achievements as the Godot build for the same match events (shared test vectors).

### P13. Map editor (6.5 h)

1. **Companion first (0.5 h).** The existing Godot map editor exports as `OzEditor.exe` (editor scene only, no gameplay). The menu's Map Editor item launches it.
   - Play-test: the editor compiles with q3map2 and bspc as today, then starts `OzArena.exe +devmap <map>` with the editor camera as the spawn (patch 0010).
   - Publish writes to the Maps folder, which the engine already loads (patch 0011).

   This keeps the feature from the first engine release.
2. **Native editor (6 h).** An editor mode in the client:
   - The brush kernel is already C++ (`tools/q3core/src/map/brush_kernel.cpp`) and moves over as is.
   - Port the document, undo, and .map and BSP import and export (`q3_map_doc.gd`, `q3_map_import.gd`, `q3_bsp_import.gd`, `q3_map_export.gd`) to C++.
   - The renderer gets a dynamic-mesh path for editable brushes and curves.
   - The panels (outliner, inspector, texture browser, history, filters) are RmlUi.
   - The tools: face and corner handles, 2D Radiant views, grid toggle, entities, lights, prefabs, race tools and the course check.
   - The same compile and play-test flow.
3. Checks: the 1,295-line `map_editor_smoke.gd` scenarios, rewritten as `engine/tests/editor_test.py`, pass. Every stock map imports and re-exports to a .map that compiles.

### P14. Platform (2.5 h)

1. **Ownership gate:**
   - ioquake3 finds Steam and GOG installs (`fs_steampath`, `fs_gogpath`). Otherwise the gate screen takes a dropped pak0, folder or game folder (`SDL_DROPFILE`) or a Browse dialog.
   - The checksum is checked against id's retail pak0 and the path remembered, with the same messages as today. Nothing is copied.
2. **Updater:** `q3_updater.c` carries over. The signed manifest now lists the engine files and pk3s. The texture packs become pk3s (BC7 DDS), downloaded on first launch from the Textures release.
3. **Crash reports:** `q3_crash.cpp` (minidumps) is hooked into `Sys_Error` and the exception filter.
4. **Diagnostics:** the fps and net overlay and log as in `scripts/diagnostics`.
5. **Accessibility:** colorblind filters as a rend2 post-process (`colorblind.gdshader`'s matrices), announcer subtitles in cgame keyed to announcer sounds, and a reduced-flashing mode that damps damage, quad and explosion flashes.
6. **Data import:** settings, binds, profile, replays, race records and identity come from the Godot build's user folder on first run.

### P15. Parity pass and release (2.5 h)

1. Run the whole feature map above as a checklist: automated where a test exists, captures for the rest.
2. Profile on Ozmium's PC and fix the worst frame spikes.
3. Developer docs (DEVELOPMENT.md, CLAUDE.md, this file's "As built"). The player-facing README is written by Ozmium.
4. Release v0.7: the engine build replaces the Godot build on GitHub Releases, and the updater moves existing players over (decision 3).

## Totals and order

| Milestone | Phases | Hours | What you get |
|---|---|---|---|
| A. Looks right | P0-P3 | 11.5 | Maps, fighters and effects in the engine; capture sheet vs the original and the Godot build |
| B. Offline parity | P4-P6, P10-P12 | 21 | Every menu, HUD, campaign, replays, race, progression, offline |
| C. Online parity | P7-P9 | 8 | Friends, parties, hosting, browser, Quick Play, ranked, dedicated, Linux/macOS |
| D. Everything | P13-P15 | 11.5 | Map editor (companion, then native), platform, parity pass, release |
| **Total** | | **about 52** | |

Order: P0 -> P1 -> P2 -> P3, then P4 -> P5 and P6 (either order), P10, P11, P12, then P7 -> P8 -> P9, then P13 -> P14 -> P15. The companion editor (P13 step 1) can ship at any point after P0.

## Risks

- **rend2's rough edges.** It's less used than the gl1 renderer, and some shader features misbehave. Budget fixes in P1, and send them upstream.
- **Matching the web menu's look in RCSS.** Blur and shadow need the filter layer from P4. If a React effect can't be matched, it gets a simpler native look rather than a web view.
- **libdatachannel on MSVC.** It pulls in libjuice, usrsctp and a TLS backend. Building it with mbedTLS keeps one crypto library for TLS, DTLS and identity signing.
- **QVM limits.** cgame grows a lot (skeletons, ragdolls, cameras, HUD). If it hits QVM memory or speed limits, cgame becomes a native DLL; game stays a QVM.
- **The native map editor** is the biggest single port, about 8,000 lines of GDScript. The companion app means it never blocks a release.
- **Legal:** unchanged from PLAN.md "Licensing". The engine is GPL-2.0 like the game code already is.

## Decisions for Ozmium

1. **Where the engine lives:** a GitHub fork `ozmiumofficial/ioq3` (branch `oz`, easy upstream PRs), pinned from `engine/thirdparty.json` in this repo (recommended), or the engine source checked straight into `oz-arena/engine/`.
2. **Map editor:** the companion app first, then native (recommended), or native only, which delays the first release by about 6 h.
3. **The Godot build:** frozen at v0.6.x with hub support kept until the engine build reaches online parity, then replaced through the updater (recommended), or replaced at milestone B with online off until C.
4. **game and cgame as QVMs** (recommended: `sv_pure`, sandboxing, mods) **or native DLLs.**
5. **Lighting default:** id's original lightmaps (recommended, and what the thread asked for), with the q3map2 deluxe relight as a per-map option once each map passes the darkness check, or relit maps by default.
6. **Executable and user folder:** `OzArena.exe` with a new `Oz Arena` user folder that imports the Godot build's data on first run (recommended), or keep the `Q3Remaster` names.
