# Gameplay scene: plan (approved by Ozmium, 2026-10-22)

**Approved 2026-10-22 with the recommended answers:** id's GPL code in a C++ GDExtension, hybrid UI (native HUD, web pause menu), original bots via botlib and `.aas`, baked LightmapGI. Work starts with G0.

**Goal (Ozmium):** "the gameplay scene and flow from the main menu. this will include modernised in game user interface. also we need a crosshair editor. we need the full quake 3 arena experience."

**Done means:** from the main menu (Create Match, Offline vs Bots, Training, Single Player) a match loads a real Quake 3 map and plays with Quake 3's own movement, weapons, items, rules and bots. It has a modern HUD, a crosshair editor, a pause menu, a scoreboard, end-of-match results (campaign wins recorded, tier movies played) and a clean return to the menu. Self-test and captures verify it; README, CLAUDE.md, PLAN.md and the zip are updated.

Nothing below is built yet. Four decisions need Ozmium's answer first (section "Decisions"); the phases assume the recommended answers.

## What we have to work with

| Have | Gap |
|---|---|
| 29 rigged characters with layered legs/torso, `attach()` at `tag_weapon`, ragdolls | No game simulation at all |
| 87 props incl. every weapon, item, projectile, gib | No maps: the BSP importer doesn't exist |
| `Q3FX` effects from cgame values, 1,415 world materials, 49 skies | No HUD, crosshair renderer, scoreboard, pause menu |
| `Q3MatchSetup`, 7 modes as data, modifiers, campaign `record_result()` | Gameplay scene is a stub |
| Settings for mouse, FOV, binds, crosshair, HUD, gameplay (stored, "not yet wired") | Nothing reads them in a match |
| The player's paks: 35 maps (q3dm0-19, q3tourney1-6, q3ctf1-4, q3tourney6_ctf, 4 pro maps), **30 `.aas` bot-navigation files**, 145 `botfiles/` (bot personalities, chat, weapon/item weights), every game sound | |
| MSVC 2022 (14.44) installed, so a C++ GDExtension can be built here | No SCons/godot-cpp yet (pip install) |
| OZMIUM (`C:\gAME\GAME`, about 69k lines of C#), designs to borrow, not code: `BspMapImporter.cs` (IBSP 46/47 lumps, patches, entities, alignment fit), `BrushTraceWorld.cs` (CM_TraceThroughBrush port), `PlayerMovement.cs` (bg_pmove port), `MatchState.cs` (warmup, countdown, end board, votes), the playtest HUD (kill feed, chat, scoreboard, pause), `Crosshair.tsx` + `CrosshairConfig.cs`, `DamageNumbers.cs`, `DamageVignette.cs`, botlib-style bots on a baked AAS-like graph | OZMIUM's weapons use Quake Live values, not Quake 3's, and it has no CTF |
| | ioquake3's source isn't on this machine; G0 clones and pins it |

## The central decision: whose game code runs the match

(2026-10-23: the standard is now Quake Live's competitive rules on top of this code, with id 1.32 as the Classic ruleset; see [QUAKE_LIVE_PLAN.md](QUAKE_LIVE_PLAN.md).) "The original game feel is sacred." The most faithful way to get Quake 3's movement, weapons, items, damage, game rules **and bots** is not to re-write them but to **run id's own GPL code**:

**Recommended: a C++ GDExtension, `q3core`, built from ioquake3's source (GPLv2).**
- **What goes in:** the collision model (`cm_*.c`: loads the `.bsp` brushes and patches, `CM_BoxTrace`), the server-side world (`sv_world.c` entity linking, `sv_game.c` syscalls, `sv_bot.c`), the game module (`g_*.c`, `bg_*.c`: Pmove, weapons, items, damage, FFA/Duel/TDM/CTF rules, intermission, awards), and botlib (`be_*.c`, `ai_*.c`) reading the original `.aas` files and `botfiles/`.
- **What stays out:** id's renderer, sound, client, VM and network layers. Godot does all presentation.
- **How Godot talks to it:** one `Q3Server` class: `start(map, cvars)`, `add_bot(name, skill, team)`, `client_command(usercmd)`, `frame(msec)`, then read `playerState_t`, `entityState_t`s, events and configstrings each tick. A `Q3Pmove` class exposes `bg_pmove` for client-side prediction.
- **Why:** movement, weapon timings, splash, armor, item respawns, spawn selection, rules, awards and the original bots (Sarge, Xaero, Crash ... with their chat lines and skill levels) come out identical to Quake 3 by construction. The campaign's bots behave as they did. Later, ioquake3's own snapshot netcode is the obvious base for multiplayer.
- **Bonus:** the game data (`.bsp`, `.aas`, `botfiles/`, `scripts/`) is read from the player's own `pak0.pk3` at runtime, which is the ownership check (licensing mitigation 1) for free.
- **Modern modes:** Instagib is a few lines in the game module; Clan Arena and Freeze Tag are new C code in the same module (about 1.5 h each), driven by the existing `Q3GameMode` data.
- **Cost:** a C++ build step (godot-cpp + SCons, Windows first; Linux/macOS builds later).
- **One licensing flag (moot since 2026-10-22: no Steamworks, see PLAN.md "Multiplayer and online services"; only matters if Steam ever comes back):** GPLv2 code in the same process as the closed Steamworks SDK is a known conflict (id's code has no linking exception). It applies equally to a GDScript translation of the GPL code. The usual way out is keeping Steam in a separate helper process. Worth a line in PLAN.md "Licensing" when Steam work starts.

**Alternative: re-write it in GDScript** (Pmove, a BSP trace, weapons, items, rules) with new navmesh bots. Easier to read and mod in Godot, but every rule is re-derived by hand with fidelity risk, the original bots are lost, and it costs more: roughly 4 h trace + Pmove, 8 h rules, 10+ h bots.

**Tick:** the simulation runs at **125 Hz with fixed 8 ms Pmove steps** (`pmove_fixed 1`, `pmove_msec 8`). That is the 125 fps feel competitive players tuned for (jump height quirk included), but at any render frame rate. PLAN.md's 128 Hz doesn't fit Pmove's integer milliseconds. Rendering runs at any rate with interpolation, and the local player is predicted so input latency is one frame.

## Phases

Efforts in hours of Claude working time (rescaled 2026-10-22: Ozmium found the first, human-developer estimates far too high; the asset pipeline and main menu took about a day). Each phase ends with self-test checks and captures.

| # | Phase | What it delivers | Hours |
|---|---|---|---|
| G0 | Toolchain | godot-cpp + SCons, `tools/build_q3core.py`, ioquake3 source pinned, a GDExtension that loads in 4.7.2, self-test hook | 1 |
| G1 | **Map import** (tools) | `tools/build_maps.py`: BSP reader; drawn surfaces grouped by shader into meshes with our PBR materials (`world_materials.json`); patches re-tessellated smooth; autosprites, flares, fog volumes, liquids, sky; Godot collision (convex per solid brush) for ragdolls and gibs; entities as nodes (spawns, items as `Q3Pickup`, jump pads, teleporters, movers, `misc_model`, speakers, intermission camera); light entities and surface lights as Godot lights; per-map `.tscn` in `library/maps/`; batch for all 35 maps, an audit (every drawn surface placed, every shader resolved) and a capture per map | 6 |
| G2 | **q3core** (simulation) | The GDExtension above: pak reading, collision, server world, game module, botlib with AAS; `Q3Server`, `Q3Pmove`; headless test: load q3dm17, add 3 bots, run 60 s, frags happen, no errors | 6 |
| G3 | **Presentation** (cgame in GDScript) | First-person view: weapon view model with Quake 3's bob, sway, kick and gun position; predicted local player; other players as `Q3Character` driven by legs/torso animation numbers, aim, team skins, bright skins; events to `Q3FX` and 3D sounds (weapons, impacts, pain, footsteps by surface, item pickups, announcer); movers; jump pads and teleports; damage feedback; death camera, ragdolls from the killing hit, gibs; powerup shells; dynamic lights for rockets/plasma | 8 |
| G4 | **Match flow and modes** | Menu -> loading screen (levelshot, map name, progress, as Q3's connect screen modernized) -> warmup and countdown -> match -> intermission -> results -> menu, next arena or rematch; every entry point (Create Match, Offline vs Bots, Training, Single Player, Continue); modes and modifiers mapped onto game cvars; Instagib, Clan Arena, Freeze Tag in the game module; campaign `record_result()` and tier movies | 5 |
| G5 | **In-game UI** (native Godot) | QL-style HUD driven by `hud/*` (scale, opacity, presets, ammo bar, item timers, kill feed, damage numbers, FPS), center prints and award medals with the announcer; the crosshair renderer (Q3 a-j and vector shapes, per weapon, hit color, health color); Tab scoreboard per mode; chat and a console; the pause menu; the end-of-match results screen (Q3's postgame awards, campaign medals) | 7 |
| G6 | **Crosshair editor** | In the pause menu and in Settings: edit with the real in-game crosshair live in the paused match behind it, presets, per-weapon tabs, import/export codes (the codes already exist) | 1.5 |
| G7 | **Settings wiring** | Mouse (sensitivity in Q3 units, acceleration, invert, raw input, zoom), FOV Hor+, every bind, gameplay settings (autoswitch, gun position/FOV, bob, force model, bright skins), audio buses, subtitles for the announcer; drop the "not yet wired" flags | 1.5 |
| G8 | **Feel check, tests, docs** | Side by side with Quake 3 on Steam: strafe-jump speed, jump height, rocket jump, weapon refire, item respawns; self-test checks for each system; captures of the HUD and each mode; README, CLAUDE.md ("tools only" is out of date), PLAN.md; zip rebuilt | 2 |
| | **Total** | | **about 38** |

Order: G0 -> G1 and G2 (independent, can overlap) -> G3 -> G4 -> G5/G6/G7 -> G8. The first playable point is after G3 (q3dm17 with bots, no HUD beyond a crosshair), at about 21 h.

**Later, not in this effort unless Ozmium wants it now:** multiplayer (listen and dedicated servers over ioquake3's snapshot netcode, with Godot transport), about 8-10 h; Steamworks (GodotSteam, lobbies, server browser, the helper-process question above), about 5 h; HUD layout editor (drag elements), about 1.5 h; demos and spectator cams; the map editor. The design above is server-authoritative from the start (even offline, a local `Q3Server` runs the match), so multiplayer adds transport and prediction, not a rewrite.

## Progress

**G0 done (2026-10-22).**
- **Sources:** pinned in `tools/q3core/thirdparty.json`: ioquake3 87a99d1, godot-cpp 8070599 (`api_version=4.7`), and id's 1.32 release dbe4ddb as the audit reference.
- **Build:** `tools/build_q3core.py` builds `q3core.windows.template_debug.x86_64.dll` with SCons and MSVC 14.44. The build is clean, with no warnings.
- **Extension so far:** it compiles `q_shared.c`, `q_math.c` and `bg_misc.c` behind the plain C bridge (`src/q3_api.h`). `Com_Error` returns control to Godot.
- **Verified:** loaded live in the open 4.7.2 editor (`Q3Core.source_version()` reports ioquake3 87a99d18206e, 35 items). The self-test passes 32/32, including the two new q3core checks: the game's `bg_itemlist` matches the catalog, and an error returns to Godot.
- **For G2:** ioquake3's game module differs from id's in 33 files. Most changes are renames, constants and bot or command fixes; `bg_pmove.c` has about 45 changed lines (for example, water contents tested as bitmasks). Every gameplay-affecting change gets checked against id's original before G2 is called done.

**G2 done (2026-10-22): id's game code runs a match inside Godot.**
- **Architecture:** ioquake3's dedicated-server engine is compiled unmodified into q3core: collision, server, snapshots, cvars and commands, the file system over the player's paks, and botlib. `src/engine/q3_sys.c` replaces `sys_main.c`, so there's no main() and no exit().
  - The game module is its own DLL, `bin/baseq3/qagamex86_64.dll`, loaded the way ioquake3 loads it.
  - A local player joins as an in-process loopback client. Usercmds go straight to `SV_ClientThink`, and the snapshots and reliable commands the server builds for it are read directly.
- **Godot API:** `Q3Server` (start, exec, frame, connect_local, usercmd, snapshot, predict, next_commands, configstring, cvar, player_state) and `Q3Host` (paths and the 125 Hz setup).
- **Prediction:** `Q3Server.predict()` is cgame's `CG_PredictPlayerState` loop running the same `bg_pmove.c`.
- **Verified** with `tests/q3core_match.tscn` on q3dm17 with Sarge, Xaero and Crash at skill 3 plus a test player, 60 s of game time at about 25x real time:
  - The bots load their personalities from `botfiles/`, navigate with `q3dm17.aas`, pick up items, frag each other and chat their original lines.
  - The game reads all nine paks from the Steam install and runs the 125 Hz game frame. A snapshot arrives every 8 ms.
  - Prediction lands exactly where the server puts the player on 7,491 of 7,499 moves; the rest are respawns and jump pads, which cgame can't predict either.
  - The self-test runs it for 20 s.
- **Moved to G3:** the prediction-error smoothing and the mover adjustment of `CG_PredictPlayerState`, since they're presentation.
- **id 1.32 behavior restored:** a full audit of ioquake3's game module against id's 1.32 release is in [tools/q3core/AUDIT_ioq3_vs_id.md](tools/q3core/AUDIT_ioq3_vs_id.md). It found 92 changes to keep (security, crash, spectator and print fixes) and 13 that changed play.
  - The 13 are reverted to id's code in `tools/q3core/patches/0001-id-1.32-gameplay.patch`, which `build_q3core.py` reapplies after every fetch.
  - They cover the water-jump test, splash line of sight, the hit beep on shootable doors, nodrop deaths, the drowning sounds, two CTF bonuses, the frag limit with one player, toggleable trigger_hurt, and four bot behaviors: strafe speed, button weapons, the q3tourney6 crusher and CTF orders.
  - Weapon damage, spread, fire rates, item amounts, respawn times and Pmove's rounding were already identical.
  - Six judgment calls (A1-A6 in the audit), decided by Ozmium on 2026-10-22: A2 (CTF carrier-protect bonus) and A5 (suicidal bots never retreat again) use id's 1.32 code. A1, A3, A4 and A6 keep ioquake3's fixes: duel intermission restart, TDM warmup per team, force-balance exemption, and bots not shooting through client 0.
  - Retested after the reverts: q3dm17, q3tourney6, q3ctf1 and q3dm0 all play.
- **Note for G4:** ioquake3 takes a player's starting team from userinfo `teampref`, not `team`. The menu's setup has to send `teampref`.
- **Note for G4 (online presence, from the friends/parties session):** call `Online.set_presence("match", detail, join)` when the map, mode or score changes, for example "Duel on q3tourney2, 7-4". Leave `join` as "" until there's something to join, and don't call it every frame.

**Weapon effects and the view weapon (2026-10-22), from id 1.32's cgame:**
- **View weapon:** hangs on every frame of the hand md3's `tag_weapon` (recoil and switch come from those frames), with Q3's bob, idle drift, landing dip and fov drop.
- **Effects:** muzzle-flash models on `tag_flash`, the held lightning beam, shotgun pellets re-traced with the server's seed, tracers, brass, model impacts and growing sprite blasts. Also missile spin and smoke trails, weapon loops, player rail colors, and marks with Q3's lifetimes and a cap.
- **Engine:** `Q3Server.trace` / `point_contents` added for these effects.
- **Check:** `tests/weapon_fx.tscn`.

**G5 in-game HUD, first pass (2026-10-22):** `scenes/gameplay/hud/` (native, in the web menu's palette, Barlow numbers).
- **Elements:** vitals with segmented bars, the ammo panel, the weapon bar, top-center clock and scores, the kill feed with weapon icons, and directional damage arcs. Also the crosshair renderer (every crosshair setting), center prints, award medals with the announcer, pickups, powerup timers, the low-ammo warning, the warmup countdown and "FIGHT!", and an FPS counter.
- **Speedometer:** an arc gauge in UPS with a strafe-jump readout (takeoff speed and the gain on the last jump).
- **Presets:** modern, competitive, minimal and classic, with HUD scale and opacity.
- **Q3 local feedback:** hit sounds, lead changes, frags-left and time warnings, the damage view kick, view bob, the dead view, cg_autoswitch and the out-of-ammo switch.
- **Chat (2026-10-23):** `hud_chat.gd` (`Q3HudChat`), bottom left. T says, Y says to your team; the line goes to id's game as `say` / `say_team`, so G_Say formats it and bots' lines arrive the same way. While typing, the usercmd carries only BUTTON_TALK (you stand still, others see `sprites/balloon3`). Up/Down recall, Tab completes names, a preview shows color codes, and Settings > HUD > Chat has the time, lines and team-only (cg_teamChatsOnly). Color codes (`Q3Colors`) are drawn in chat, prints, center prints, the kill feed, the scores, player cards and the pause menu.
- **Still to do in G5:** a console, item timers, net graph.

**Pause menu and crosshair editor (G5/G6, 2026-10-22):**
- **Pause menu:** the web menu in the "pause" context (`PauseApp`), from a hidden web view made at match start (`Q3WebLayer`), so Escape opens it at once.
  - Items: Resume, Ready up (warmup), Crosshair, Settings (every tab, live), Restart match, Leave match and Quit, the last two confirmed.
  - It shows the match title, clock and leaders. Offline matches stop while it's up.
- **Crosshair editor** (`CrosshairEditor`; also from Settings > Crosshair):
  - In a match it leaves the screen's center clear, so the HUD's real crosshair is the preview. Weapon tabs switch the HUD to that weapon's crosshair, and "Test hit" flashes it.
  - Shapes, look, colors, five presets, per-weapon crosshairs, and code copy/paste.
- **Check:** `tests/pause_menu.tscn` (six checks).

**Campaign, ragdolls, scoreboard (2026-10-22, evening):**
- **Campaign:** plays through: `spmap`, id's own single-player rules, results from the game's `postgame`, and the tier order checked against q3_ui. Campaign XP: 250 x skill/3 per new arena, 100 per harder clear, 1000 per tier, 5000 for the campaign.
- **Ragdolls:** rebuilt on Jolt with real masses and joint limits (knees hinge). Corpses last 10 s, then burn away.
- **Scoreboard:** restyled. Right click with Tab held for player cards and stats.
- **Ranked:** opens at level 30.

## Added by Ozmium (2026-10-22)

- **Lighting:** keep the baked LightmapGI for play. The map editor will preview with real-time lights and bake on save.
- **Ready-up:** players ready up during warmup before the countdown (G4).
- **Post-match screen:** results with an animated XP gain bar, driven by `Q3Cards.level_for()` / `xp_to_next()` and the profile (G4/G5).
- **Tab scoreboard:** shows each player's card (banner, emblem, frame, title, from `Q3Cards`) in an intuitive layout (G5).
- **Priority:** working gameplay now; the map editor starts tomorrow (2026-10-23).

## Decisions for Ozmium

1. **Game code:** run id's GPL code in a C++ GDExtension (recommended), or re-write it in GDScript?
2. **In-game UI tech:**
   - **Recommended, hybrid:** HUD, crosshair, scoreboard, chat and kill feed in native Godot (no web-view latency, visible in captures, works on Linux); the pause menu, in-match settings and the crosshair editor reuse the existing web menu, shown only while paused (the native crosshair stays visible through the transparent web view, so the editor previews the real thing).
   - All native: the pause menu and settings rebuilt in Godot controls (about +3 h, two settings UIs to maintain).
   - All web: the HUD in the web view too (simplest styling, but adds a frame of latency, invisible to captures, no Linux transparency).
3. **Bots:** id's original bots via botlib and the `.aas` files (recommended, comes with decision 1), or new navmesh bots later (the campaign then waits for them)?
4. **Map lighting** (the baked lightmaps are dropped, per PLAN.md):
   - **Recommended:** import every light entity and surface light, then bake Godot's LightmapGI per map in the tool pipeline, so the library ships baked maps; dynamic lights (rockets, plasma, muzzle flashes) on top. Adds about 1.5 h and a bake step of minutes per map.
   - Real-time SDFGI: no bake, but leaks light in tight indoor maps and costs frame rate.
   - Keep the original lightmaps as a "classic" option alongside either, about 0.5 h more.

Defaults assumed unless Ozmium says otherwise: offline play vs bots first, multiplayer after (the zero-cost stack in PLAN.md); 125 Hz fixed Pmove; q3dm17 as the vertical slice, then all 35 maps in the batch.
