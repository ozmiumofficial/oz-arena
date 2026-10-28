# Quake III Arena modern remaster: plan

**Goal:** a complete modern remaster of Quake III Arena in Unity or Godot, with the same feel and current-generation visuals.

**Current stage:** tools only. Build and prove the asset pipeline first; no game implementation yet.

## Decisions so far

| Decision | Status |
|---|---|
| Must be modern (PBR, skeletal animation, modern lighting) | Decided 2026-10-22 |
| Engine: Unity or Godot | Recommended Godot (fully open source); Ozmium's call. Comparison below |
| ioquake3 / Quake3e as the base | Rejected 2026-10-22 (renderer ceiling); reopened 2026-10-28 after the r/ArenaFPS launch thread: Ozmium wants everything moved onto a modified ioquake3, no web view. Plan in [ENGINE_PLAN.md](ENGINE_PLAN.md) |
| Start with tools, not game code | Decided 2026-10-22 |
| Distribution model | Decided: free/open source, Quake 3 ownership required, ships the remastered asset library. Risk and mitigations in "Licensing" |
| Online services | Decided 2026-10-22: no Steamworks, no paid hosting. Player-hosted matches over peer-to-peer WebRTC, always relayed through Cloudflare's TURN so IPs stay hidden (2026-10-23), closest region first with a region choice, optional dedicated servers, one match browser, and accounts, matchmaking and player data on Cloudflare's free tier. See "Multiplayer and online services" |
| Gameplay architecture | Decided 2026-10-22: id's GPL game code (ioquake3: collision, server world, game module, botlib) runs in a C++ GDExtension, `q3core`; Godot does all presentation. Fixed 125 Hz Pmove (8 ms). Native HUD, web pause menu; original bots from the `.aas` files; LightmapGI baked per map. Phases G0-G8 in [GAMEPLAY_PLAN.md](GAMEPLAY_PLAN.md) |
| Rules and physics | Decided 2026-10-23: Quake Live's competitive rules and physics are the standard (no crouch slide), id 1.32 kept as a Classic ruleset; Quake Live's movement physics on hold (2026-10-26), so movement is 1.32's in both rulesets for now; Quake 3's maps stay id's; the community ports Quake Live's maps through the map editor's import, except the 15-map duel pool (2026-10-25), which we remaster and which is the only pool for ranked duels. See [QUAKE_LIVE_PLAN.md](QUAKE_LIVE_PLAN.md), [DUEL_MAPS_PLAN.md](DUEL_MAPS_PLAN.md) |

## Where the tools stand

| Asset type | Status |
|---|---|
| Player models (29 incl. dev-team models) | Automated: mesh, skeleton, clips, PBR, Unity + Godot exports, viewer |
| Player team skins (red/blue) and alternate skins | Supported via `models.json` `skin`; not batch-run yet |
| Player LODs (`_1`, `_2` md3) | Not handled |
| Weapons (`models/weapons2/`) | Not started. Same MD3 reader; no skeleton needed |
| Items, powerups, projectiles | Not started. Same as weapons |
| Weapons, items, powerups, ammo, flags, gibs, projectiles, map objects | Automated (2026-10-22): 87 props as GLB with tags, morph animation, matte PBR; entity catalog |
| Visual effects | Automated (2026-10-22): 30 remastered effect textures/flipbooks + effect parameters from cgame; Godot effect scenes |
| Shader effects (glow layers, scrolling, additive) | Glow layers -> emissive, additive/cut-out/two-sided flags done for models; world shaders pending |
| Godot project | Done (2026-10-22): the full library loads in Godot 4.7: layered character animation, ragdolls, pickups, every effect; showcase scene; 15-check self-test passes |
| World textures | Done (2026-10-22). Every world shader and image is a Godot material: 1,346 surfaces, 23 liquids, 46 fog, 49 skies. Tiling-aware matte PBR, glow/motion/wave/flipbook/surface-light/cut-out semantics, stochastic tiling on 310 isotropic textures, self-test + GPU captures clean. 369 shaders reference images id never shipped (kept as `unshipped`). Still to do: HUD/menu/icon upscale for the modern UI |
| Maps (BSP geometry, textures, entities) | Done for play (2026-10-22, gameplay G1): all 35 maps from the paks as Godot scenes with the remastered materials (99 map-object shaders added to the world library), q3map-model lights baked with LightmapGI, collision, fog, entities (`build_maps.py`, `Q3MapBuilder`, `tools/bake_maps.gd`). Editable brushes and the .map round trip came with the map editor (E6, 2026-10-23) |
| Built-in map editor | Done (2026-10-23, phases E0-E8 in `MAP_EDITOR_PLAN.md`): brushes, faces, vertices, curves, materials, entities, lights, prefabs; compile and play-test with id's q3map and bspc; every stock map imported from its .bsp with every surface aligned; Radiant .map import and export; filters, outliner, history, thumbnails, Publish to Create Match. Still later: editing together online (after multiplayer) |
| Sounds, music | Menu set wired (2026-10-22): `menu1-4.wav`, `weapons/change.wav`, an optional ambient loop, played as `ui_qmenu.c` does. The rest still needs a cleanup/upsample pass, not conversion |
| Main menu | Done (2026-10-22). Splash with the fan-project disclaimer; a live React menu (Next.js in a godot_wry web view) over the Skyrim-style showcase; settings (131 entries); match setup with 7 modes as data; the single-player campaign; cinematics. Gameplay and Map editor are stubs. See "Main menu design" |
| Cinematics | Done (2026-10-22): the 10 RoQ movies as 4:3 Ogg Theora (1280x960), played at launch, from the Cinematics menu, and (once Gameplay exists) after campaign tiers |

## Engine comparison (Unity vs Godot)

| | Unity 6 (URP or HDRP) | Godot 4 |
|---|---|---|
| Import of these assets | FBX with Generic rig; textures need the URP repack (`unity_repack.py` does it) | glTF/GLB with skeleton and every clip; ORM maps work as-is |
| Rendering ceiling | Higher: HDRP or URP with Forward+, APV probes, VFX Graph | Good and improving: Forward+, SDFGI/VoxelGI, volumetric fog |
| Netcode for 125 Hz-style play | Mature options (FishNet, which the GAME project already tuned for 128 Hz) | Built-in high-level multiplayer; less proven for fast competitive FPS |
| Existing work you can reuse | A lot: id-Tech-3-style movement, 128 Hz netcode, BSP + texture-pack import, map editor in the GAME project | None yet |
| Licensing cost | Free tier with revenue limits; runtime terms have changed before | MIT licensed, free forever |
| Mod support (Q3's lifeblood) | Possible but heavy (asset bundles, scripting sandbox) | Easier: open source, PCK files, GDScript |

**Recommendation (2026-10-22): Godot.** The release is meant to be fully open source, and Unity's engine is closed, so the whole stack can only be open in Godot (MIT). Godot also fits the other requirements:
- GPL code reused from Quake 3 (`bg_pmove.c` movement, weapon timings, bots) sits cleanly on an MIT engine.
- It loads `.glb` at runtime with no plugins, which suits bring-your-own-pak0 conversion.
- It's far easier to mod.

The cost is porting the GAME project's movement, netcode and BSP import ideas instead of reusing the Unity code, and building Quake 3-style snapshot netcode. Unity only wins if the open-source goal is dropped. The tools export for both.

## Phases

### Phase 1: player models, quality pass (tools)
- Batch all 29 player models (done once; see `work/summary.json`), review each in its viewer, and record fixes in `models.json`.
- Per-model ID maps (painted once, about an hour each) to replace color classification (fixes Sarge's glowing camo).
- Use shader glow layers (for example `cigar.glow.tga`) as emissive maps.
- Team and alternate skins; LOD meshes.
- A gallery viewer with a model picker.

### Phase 2: weapons, items, effects (tools)
- `models/weapons2/*`: MD3 -> mesh + PBR. Weapons attach at `tag_weapon`, which is already a bone in every rig. Flash and barrel tags become sockets.
- Items, powerups, armor, health, projectiles: same path.
- Shader parser upgrade: blend modes, `tcMod` scroll/rotate, `rgbGen wave` into engine material parameters.

### Phase 3: maps (tools)
- BSP -> meshes per brush/patch with UVs and lightmap UVs; entities -> spawn points, items, jump pads, teleporters.
- PBR versions of the map textures (same make_pbr.py approach, plus tiling and no-visible-repeat rules).
- Throw away the baked lightmaps and relight in-engine.

### Phase 4: engine choice and vertical slice
- Pick Unity or Godot.
- One map (q3dm17 is the classic test), one character, one weapon, movement with exact id Tech 3 physics, local play.
- Measure feel against the original game side by side before anything else.

### Phase 5: full game
- Every map, weapon and mode, netcode, bots, UI, audio.

### Phase 6: hand-finished art (any time after Phase 1)
- High-poly sculpts over the remastered silhouettes, retopology with joint loops, baked maps, Substance texturing.
- Humanoid rig mapping so modern animation libraries can drive the characters; the recovered 1999 clips stay as the reference for feel.

## What the automated tools can't do (fix for each)

| Limit | Fix |
|---|---|
| No new texture detail (upscaling only sharpens) | AI upscaling for reference, then repaint or Substance |
| Shapes only rounder (subdivision) | Sculpt high-poly, bake to a new game mesh |
| Stiff joints (1999 topology) | Retopology with joint loops, re-skin starting from the recovered weights |
| Color-only material classes | Per-model ID map |
| 15–25 fps source animation | Clean up at 60 fps, add IK foot locking |

## Licensing: the real constraint

id Software's art, maps and sounds are copyrighted. The game code is GPL, but the data is not.

**Open-sourcing the remaster doesn't change this.** An open-source license covers the code you write. It can't grant rights to id's assets, or to remastered versions of them, because those are derived works. Giving them away for free still counts as distributing them. "Quake" is also a trademark. Open source works well with option 1: publish all the code, and players convert their own paks. ioquake3 works exactly this way.

**Decided 2026-10-22 (Ozmium):**
- Free and fully open source.
- Players must own Quake III Arena to run it.
- It ships the remastered asset library made with these tools: modified and modernized id assets with rigged skeletons. Nothing is converted on the player's machine.
- **The public repository carries that library too (decided 2026-10-23):** `godot/library` and `godot/webui`, through Git LFS. id's original files (`extracted/`, the paks) and the tools' working files (`work/`) stay out, and the game still reads the player's `pak0.pk3`. A DMCA notice to GitHub would take down the whole repository, code included, so keep a local mirror.

**Risk, stated plainly:**
- **Ownership doesn't grant rights.** Remastered assets made from id's files are derivative works, and distributing them needs the rights holder's permission even when every player owns the original.
- **It's tolerated, not permitted.** Requiring ownership puts this in the same category as the many fan HD texture and model packs for Quake 3, which have mostly been left alone but exist by tolerance, not by right.
- **The stakes are higher here.** A complete remaster is a bigger target than a texture pack, and Microsoft (id's owner) has released official Quake remasters of its own.
- **Contrast with CPMA and Defrag.** They are mods that ship only their own content, which is why they've been safe for decades.

**Mitigations:**
1. **Ownership check:** the game won't start without a genuine `pak0.pk3` (verify its checksum on the player's machine). The launch splash already states the requirement; the check itself isn't built yet.
2. **Strictly non-commercial:** free, no ads, no paid tiers, no donations tied to the assets.
3. **New project name:** no "Quake" anywhere; that's trademark, separate from copyright.
4. **Ask Bethesda/id for permission:** see their mod and fan-content policies. A written OK removes the risk entirely and is worth asking for before release.
5. **Have an IP lawyer review before launch.**
6. **Keep a fallback:** the asset library spec below works for original art too. If permission is refused or a takedown comes, assets can be replaced one by one with original versions on the same skeleton and clip standard.

Reusable whatever happens (structure and facts, not art): the bone layout, the clip slots and timings, the legs/torso layering, gameplay values, and the GPL game code.

## Modern enhancements (game scope, 2026-10-22)

Ozmium wants every modern enhancement: a full remastered asset library, rigged skeletons, ragdoll physics, better UI, and the rest of a modern feature set. Core rule (changed 2026-10-23): **Quake Live's competitive rules and physics are the standard**, with id 1.32 one setting away (the Classic ruleset). Weapon timing and damage come from the GPL code with Quake Live's numbers on top (movement stays 1.32's: Quake Live's physics is on hold, 2026-10-26) ([QUAKE_LIVE_PLAN.md](QUAKE_LIVE_PLAN.md)); everything below is layered on top, never changes them, and gets measured against them. Until 2026-10-23 the rule was "the original game feel is sacred" (id 1.32 exactly).

Several of these already exist in the GAME project on the work PC (noted as "GAME has"); the designs carry over even though the Unity code doesn't move to Godot.

| Area | Enhancements | Notes |
|---|---|---|
| Characters | Full original asset library, shared rigged skeleton, 60 fps animation, layered legs/torso, IK foot placement, aim offsets, procedural recoil | Skeleton standard from the recovered rig |
| Ragdoll physics | Physics ragdolls on death from the same skeleton, blended out of the death animation, impulse from the killing hit, gibs for splash kills | GAME has bot ragdolls with local gravity and death-barrier handling |
| UI | Modern menus, scalable HUD (in the Quake Live style), server browser, party and lobby, settings with live preview, keybinds incl. mouse wheel, crosshair editor, killfeed, scoreboard, chat | GAME has an OZMIUM menu, QL-style HUD, chat, keybind capture and pause menu |
| Rendering | PBR (matte), dynamic lights for rockets and plasma, real-time GI, volumetric fog, HDR + bloom, SSR, high-quality shadows, modern particle VFX, decals, upscaling (FSR) | Maps relit in-engine; baked lightmaps dropped |
| Feel and fairness | High-refresh rendering with physics independent of frame rate (Q3's 125 fps jump quirk made a deliberate setting or fixed), raw mouse input, widescreen FOV that's correct at any aspect, hit sounds, hitmarkers, damage numbers | GAME has hit registration, damage numbers and a hitmarker |
| Netcode | 128 Hz server tick, client prediction with smoothing, lag-compensated hitscan and projectiles, delta snapshots | GAME has 128 Hz FishNet tuning and Q3-style audits to borrow ideas from |
| Multiplayer | Full multiplayer: matchmaking, ranked (ELO), duel queue, spectator with free/follow cams, demos and replays, player-hosted and dedicated servers. Zero-cost online stack; see "Multiplayer and online services" | GAME has spectator + duel queue, ELO at match end, and Steam lobbies |
| Audio | Spatial/HRTF audio, occlusion, remastered sound set (new recordings), per-device output | GAME has Steam Audio integration |
| Bots | Modern AI on a navmesh, difficulty that scales by skill rather than cheating | GAME has bot AI with nav baked into maps |
| Modding | Open source; mod and map loading from packs, map editor, workshop-style browser | GAME has an in-game collaborative map editor |
| Platform | Gamepad support, accessibility (colorblind modes, subtitles for announcer and callouts, HUD scaling), Linux/Windows/macOS builds | Godot exports all three |
| Anti-cheat | No anti-cheat (Ozmium, 2026-10-22). Lobbies vote-kick (`callvote kick`), and the kicked player is banned from that lobby for 30 minutes | Server authority stays: the game code runs on the host |

## Scene structure (2026-10-22)

Ozmium's layout: three top-level scenes with a clear handoff.

| Scene | Contains | Hands off |
|---|---|---|
| **Main menu** | Everything a player sees on launch: play (mode select, server browser, matchmaking, offline vs bots), map editor entry, settings, profile and stats, quit | Builds a **match setup** (game mode, map, rules, modifiers, bots, server type) and loads Gameplay with it; or loads Map editor |
| **Gameplay** | The match itself. Game-mode rules and every gameplay modifier live here, loaded from the setup the menu made: FFA, Duel, TDM, CTF, Clan Arena, Freeze Tag, Instagib; timelimit, fraglimit, weapon and item sets, physics variants, spawn rules | Back to the main menu (or to the editor when the match was an editor play-test) |
| **Map editor** | The built-in editor (spec under "Built-in map editor"): all tools, Quake 3 map import (.bsp and .map), creating maps from scratch, collaborative sessions, play-test | Play-test runs the map with Gameplay rules in place; leave to main menu |

Rules:
- **Game modes are data.** A mode is a rules resource the Gameplay scene loads, never a separate scene. Modifiers stack on top of the mode, and a new mode means a new rules file, not new scene code.
- **Shared setup.** The menu, a dedicated server's config and the editor's play-test all build the same match-setup object, so every entry point runs identical rules.
- **Shared library.** All three scenes use the same asset library (characters, items, effects, world materials) and the same input, settings and online session autoloads.
- **One scene loads at a time.** Online sessions, settings and the library autoload live across scene changes.

## Main menu design (2026-10-22)

Ozmium's vision: a modernized, visually stunning version of the Quake III Arena main menu. Modern quality-of-life features, a fully fleshed-out settings menu, menu buttons and items on the left, and a stylish, Skyrim-like character showcase on the right: a randomized player model with cool, slowly drifting camera angles.

### Layout
- **Left column:** the menu, in Quake 3's gothic-metal style made modern. Large, clean labels with a glowing red accent, a hover sweep and a subtle sound; sub-menus slide out to the right of the column.
- **Right two-thirds:** the character showcase, a real 3D scene behind the UI.
- **Top bar:** player name, rank/ELO, friends online, notifications.
- **Bottom bar:** news, patch notes and version.

### Built (2026-10-22)
- **UI technology (Ozmium's call):**
  - The menu is a Next.js + React + Tailwind + Motion app (`webui/`), running live in Godot through godot_wry (MIT; WebView2 on Windows, WebKit on macOS, WebKitGTK on Linux) as a transparent layer over the 3D showcase.
  - Godot keeps everything stateful: settings, match setups, campaign progress, sounds and the controller. A small JSON bridge connects the two.
  - **Trade-offs:** the web view always draws on top, its transparency doesn't work on Linux yet, and it's invisible to Godot's own screenshots.
- **Title:** Quake 3's own banner5 logo, with "REMASTERED" under it in flat banner-font letters. It reads "QUAKE III ARENA", which conflicts with licensing mitigation 3 below. It's one constant (`TITLE_PROP` in `main_menu.gd`) to swap once the project has a name.
- **Splash at launch:**
  - It says this is a non-commercial fan project, not made or endorsed by id Software, Bethesda or Microsoft.
  - All models, textures, sounds and maps are derived from id Software's Quake III Arena and belong to id Software.
  - Quake III Arena on Steam is required to play.
  - An actual ownership check (mitigation 1) is still to do.
- **Single player:** Quake 3's campaign from `arenas.txt`:
  - Training, six tiers of four arenas, and the Final against Xaero, each with its own bots and frag limit.
  - Five skills, tier unlocks, medals, and progress in `user://campaign.json`.
  - Wins get recorded once the Gameplay scene exists.
- **Cinematics:**
  - The id logo and intro play on the first launch.
  - Tier movies unlock as tiers are won, and the ending after the final.
  - A Cinematics menu holds them all.

### Showcase (Skyrim loading-screen style)
Like Skyrim's loading screens: one 3D model floating alone in a dark, smoky void, turning slowly while the camera drifts, lit like a museum piece.
- **Subject:** a random asset from the remastered library, changed every 15–25 s with a slow fade through the smoke. Mostly the 29 characters (playing an idle, with an occasional gesture or taunt), sometimes a weapon, a powerup (quad, mega health, armor) or a map object, so the menu shows off the whole library.
- **Space:** no floor or arena, just a deep, near-black background with drifting volumetric smoke and fog, faint embers rising, and a soft vignette.
- **Motion:** the model rotates slowly and bobs gently in place. The camera drifts in slow dolly and orbit moves between a few framings (three-quarter, low hero angle, close on the face or weapon), never cutting hard, with depth of field on the subject.
- **Light:** a soft key from above, a colored rim light (red by default, team color when picked) and a faint glow on any emissive parts. A powerup shell (quad, battle suit, regen) shows now and then on characters.
- **Hover reaction:** hovering a menu item nudges the drift. "Play" eases the camera closer, "Map editor" swaps the subject for a map object.

### Sounds (same as Quake 3)
The menu uses Quake 3's own menu sounds, wired the way `ui_qmenu.c` does:
| Sound | When |
|---|---|
| `sound/misc/menu1.wav` | Opening a menu / entering a sub-menu (menu_in_sound) |
| `sound/misc/menu2.wav` | Moving between items, hover (menu_move_sound) |
| `sound/misc/menu3.wav` | Back / closing a menu (menu_out_sound) |
| `sound/misc/menu4.wav` | Refused action, e.g. a greyed-out item (menu_buzz_sound) |
| `sound/weapons/change.wav` | Cycling the showcase subject (the original uses it for weapon change) |

Played from the player's own paks like the rest of the id content (see "Licensing"). The original menu has no music; an optional ambient track can be added, off by default.

### Menu tree
| Item | Contents |
|---|---|
| Single Player | Quake 3's campaign: tiers of arenas, skill, medals, tier movies |
| Play | Quick play (matchmaking), Ranked, Server browser (filters, favorites, history, ping), Create match (mode, map, rules, modifiers, bots), Offline vs bots, Training/practice |
| Map editor | Loads the Map editor scene (new map, open, import Quake 3 map, recent maps) |
| Profile | Player card editor, level and XP, character and skin select (team colors), stats, match history, achievements, ranked ladder (see "Player cards, progression and ranked") |
| Cinematics | Quake 3's movies: id logo, intro, tiers 1-7, ending (unlocked by the campaign) |
| Settings | See below |
| Credits / Quit | |

### Settings (fully fleshed out)
| Tab | Settings |
|---|---|
| Video | Resolution, display mode, monitor, refresh rate, VSync, frame cap, field of view (correct at any aspect), render scale, upscaler (FSR/off), brightness/gamma |
| Graphics | Quality presets plus every individual setting: shadows, GI, reflections, AO, volumetric fog, post effects, bloom, particles, texture quality, anisotropy, decals, gibs on/off, effect intensity |
| Audio | Master, music, effects, voice, announcer, hit sounds (style, pitch by damage), spatial audio, output device, voice chat device and push-to-talk (the Voice chat mode setting comes back with voice chat) |
| Controls | Every keybind with conflict warnings (mouse buttons and the wheel included), per-weapon binds, controller layout (Godot's SDL gamepad mappings) |
| Mouse | Sensitivity (shown as cm/360), raw input, acceleration off/on, invert, separate zoom sensitivity |
| Crosshair | Full editor with live preview: shape, size, color, outline, dot, gap, per-weapon crosshairs, hit-indicator color |
| HUD | Layout editor (drag elements), scale, opacity, item timers (where the mode allows), damage numbers, kill feed, ammo bar, Quake Live-style presets |
| Gameplay | Weapon switch on pickup, auto-switch rules, view bob, gun position/FOV, enemy model forcing and bright skins, team colors, spawn effects |
| Network | Region, max ping, rate, net graph (interpolation comes back with RemoteMatch, MULTIPLAYER_PLAN.md M3) |
| Accessibility | Colorblind modes, subtitles for announcer and callouts, HUD scaling, reduced flashing, high-contrast menus |

### Quality of life
- **Navigation:**
  - Settings search box.
  - Every setting applies live, with a preview and a Reset to default.
  - Back/Escape works everywhere.
  - Full controller navigation.
  - Tooltips explain each setting.
- **Getting into games:**
  - Continue / rejoin the last match.
  - Party invites from the friends list.
  - Quick map vote in matchmaking.
- **Persistence and data:**
  - Profiles synced to the online backend.
  - Import and export of config and crosshair codes.
- **Speed:** the first frame of the menu comes up instantly (the showcase streams in behind it).

### Prior art
OZMIUM's cyber-theme main menu, settings persistence and keybind capture in the GAME project. The design ideas carry over; the visual identity here is Quake 3 gothic, not OZMIUM's.

## Multiplayer and online services (zero-cost, 2026-10-22)

Ozmium wants full multiplayer capability. **Built last (2026-10-23), after the map editor and replays, per `MULTIPLAYER_PLAN.md`.** **Decided 2026-10-22: no Steamworks and no paid hosting.** Players host matches from their own PCs and connect peer-to-peer, dedicated servers are optional, and accounts, matchmaking, the match browser and player data run on Cloudflare's free tier.

**Why not Steamworks:**
- **App ID:** a Steam release needs an App ID, and Valve's review needs the asset rights from "Licensing" settled first.
- **Borrowed App IDs:** OZMIUM's App ID would put OZMIUM at risk. Spacewar (480) is for testing only and shares its lobbies with every other developer. Quake III Arena's own App ID (2200) belongs to id.
- **GPL:** it also removes the GPLv2-plus-closed-Steamworks conflict flagged in GAMEPLAY_PLAN.md.
- **Later:** if Bethesda/id give permission, Steamworks can come back as a second backend (see the last rule below).

**Multiplayer scope:**
- **Modes:** every original mode (Free For All, Tournament/Duel, Team Deathmatch, CTF), plus modern ones (Clan Arena, Freeze Tag, Instagib) as optional rulesets.
- **Servers:** matches hosted from players' own PCs (no setup, peer-to-peer), optional dedicated servers (headless Godot export, Linux and Windows), and LAN discovery.
- **Netcode:** server-authoritative at 125 Hz, on ioquake3's own networking already compiled into q3core (netchan, delta snapshots; `net_enabled` is 0 today).
  - Client prediction and reconciliation, using the GPL Pmove so movement matches exactly.
  - Interpolation and extrapolation.
  - Lag-compensated hitscan and projectiles.
  - A latency, loss and jitter overlay.
- **Match flow:** warmup, ready-up, countdown, overtime and sudden death, map voting, intermission scoreboard, rematch.
- **Spectating:** free and follow cams, team-only for competitive play, server-side demo recording and client demos. Replays, the replay viewer and local spectating don't need netcode (no killcam: Ozmium, 2026-10-23) and are planned in `REPLAY_PLAN.md` (approved 2026-10-23, built after the map editor).
- **Server admin:** rcon, kick/ban, map rotation and server config files.

**Hosting matches (costs nothing):**

Ozmium's rule: players never have to run a dedicated server. Any player can host a match from their own PC, others connect to them with no port forwarding, and everyone can see and join everyone else's matches.

| Need | How |
|---|---|
| Player-hosted matches | The host's game runs a listen server (q3core's server, same code as a dedicated one) and plays in it |
| Connecting with no setup | Peer-to-peer WebRTC (Godot's official webrtc-native extension, MIT). q3core's netchan packets ride an unreliable, unordered data channel instead of a raw UDP socket: a new virtual address type hands packets to Godot instead of `NET_SendPacket`/`Sys_GetPacket`. Connection setup (signaling) goes through the backend's WebSocket |
| Hidden IPs (Ozmium, 2026-10-23) | Every internet connection goes through Cloudflare's TURN relay (Realtime: 1,000 GB/month free, shared with the SFU, then $0.05/GB), so no player ever sees another's IP, host included. Both sides gather relay candidates only (`iceTransportPolicy: relay`), and the hub drops any non-relay candidate from the signaling it forwards. The relay is UDP, so the data channel keeps Quake 3's unreliable packets. Cost: a few to about 20 ms over a direct path. LAN play stays direct |
| Seeing and joining matches | One match browser lists player-hosted matches and dedicated servers together: mode, map, players, region, ping, friends in it, password lock. A host registers its match in a Durable Object directory and updates it over its open WebSocket. Join in progress or spectate; private matches are invite-only |
| Dedicated servers | Any player can run an always-on dedicated server (Ozmium, 2026-10-23): clans, leagues, community servers, ranked. The dedicated build ships with the game, accepts WebRTC and plain UDP, and lists in the same browser. See "Player-run dedicated servers" below |
| Vote kick, no anti-cheat (Ozmium, 2026-10-22) | Quake 3's own `callvote kick` in the match (game module). The host tells the hub, and the hub keeps the kicked player out of that lobby for 30 minutes: it's hidden from them in the browser and their join is refused. The host's server also refuses their id, so a direct `connect` fails too. The ban is tied to the player's identity key, not their IP. Party kicks don't ban: the leader can re-invite straight away (Ozmium). To be built with the match browser |
| LAN | Quake 3's built-in LAN broadcast search over plain UDP, no internet needed |
| Joining friends | Join from the friends list or party, `connect` links, Discord join buttons |
| Classic Quake 3 browser | Dedicated servers can also register with the public dpmaster masters under our own game name (ask the operator first) |

**Trade-offs:**
- **The host's connection is the server.** If the host quits, the match ends (host migration isn't planned; Quake 3's server state doesn't move between machines). The host also plays at zero ping; for ranked, the matchmaker picks the host with the best connection to the others to keep that small.
- **Relay budget (measured 2026-10-24, MULTIPLAYER_PLAN.md M8):** Cloudflare bills only what its relay sends each client, framing included, so each packet is billed once. A relayed player costs about 56 MB an hour in a 4-player match, 72 in an 8-player one and about 80 in a 12-player one at the defaults (snaps 42, 60 packets a second); the game's own bytes are only about 40% of that (per-packet framing and SCTP acknowledgements are the rest). The free 1,000 GB covers about 14,000-18,000 player-hours a month, about 20-25 players online around the clock. Past that it's about $3.50 per 1,000 player-hours. The fallback if that ever matters: relay by default, with an opt-in "direct connection" setting.
- **Dedicated servers** are relayed like everything else, so a server on someone's home PC doesn't show its IP. An operator can mark a server public (a VPS, a datacenter box); players then connect to it directly and the relay isn't used.

**Player-run dedicated servers (Ozmium, 2026-10-23):** anyone can keep a server running around the clock, on the PC they play on, a spare PC, or a free or cheap VPS.
- **Starting one:** "Host dedicated server" in the menu runs the same executable headless in the background (no window, no rendering) with the server config built in match setup. It keeps running after the game closes, and can start with Windows. On Linux and VPSes, the same build runs from the command line or as a systemd service.
- **One build, two platforms:** Windows and Linux x86-64, plus Linux ARM64 so Oracle's free tier works (a guide in DEVELOPMENT.md or the wiki).
- **Ownership:** the server needs the operator's own retail pak0, checked the same way as the game's setup gate.
- **Always listed:** the server keeps a hibernating WebSocket open to the hub, which lists it in the browser while it's up, shows it as offline when it drops, and reconnects with backoff. A server gets a persistent identity key like a player, so its name, history and bans survive restarts.
- **Running it:** map rotation, modes and rules, password, max players, slots reserved for friends or a clan, MOTD, and Quake 3's server cvars. rcon works in the game's console, and the menu has an admin page for servers the player owns (players, kick/ban, change map, edit rotation, logs).
- **Empty servers:** an idle server sleeps its game loop until someone joins, so it costs almost no CPU. Bots can fill empty slots if the operator wants.
- **Matchmaking:** an operator can opt a server into matchmaking, and the queue sends matches to an empty one in the right region before falling back to a player host.

**Regions (Ozmium, 2026-10-23):** players match in their closest region first and can pick another.
- **Regions:** North America East, North America West, South America, Europe, Asia, Oceania, and Middle East/Africa.
- **Detection:** the Worker reads the connection's `request.cf` location (continent, country, coordinates) at handshake, so the home region costs nothing and needs no ping probing.
- **Setting:** Region is "Auto (closest)" or one named region, in Settings and on the Play page. A host's match is tagged with the host's region.
- **Matchmaking:** Auto searches the home region first and widens to neighboring regions after a wait (start at 60 s, shown in the queue). A named region stays strict.
- **Match browser:** filters by region (Auto shows yours first) and shows each match's ping.

**Online backend (Cloudflare free tier, built from trac3r's Worker + D1):**
| Piece | How |
|---|---|
| Identity | No sign-up. A key pair made on first launch (Godot `Crypto`) is the player's identity; the Worker issues a session after a signed challenge. "Link Discord" (free OAuth) or a backup code moves a profile to another PC |
| Player data | D1 holds profiles: rating, career stats, XP, unlocks, card, keybinds, settings, crosshair. Everything is also saved in `user://`, so the game works offline and the backend only syncs |
| Friends, parties, presence | **Built 2026-10-22** (`online/`, `scripts/online/`, the menu's Friends panel). Everything lives in one Durable Object's own SQLite (not D1): friend codes, requests, presence (menu / match / map editor), parties of up to 8 with leader, invites, kick, promote and chat, over hibernating WebSockets. Live at `wss://arena-online.fragnet.workers.dev` (Ozmium's personal Cloudflare account) |
| Matchmaking | A Durable Object queue per mode and region, by rating (window rules in "Ranked"; region rules in "Regions" above). Players stay connected with hibernating WebSockets instead of polling. The match goes to an empty matchmaking-enabled dedicated server in the region (those servers check in every 5 minutes); otherwise the matched player with the best connection to the others hosts |
| Ranked results | Peer-to-peer for now (Ozmium, 2026-10-23: there are no dedicated servers yet). The matchmaker picks a player host, and the result counts only when the players agree on it (see "Trust" under "Player cards, progression and ranked"). Once verified dedicated servers exist, which sign results with a key we issue to trusted operators, ranked moves to them |
| Workshop | **Planned as M9** in `MULTIPLAYER_PLAN.md` (Ozmium, 2026-10-23): maps on Cloudflare R2's free tier, listed, rated and moderated by the hub Worker. It was dropped earlier that day for lack of hosting, and back once R2 made hosting free |

- **Free-tier budget:** 100,000 Worker requests and 100,000 Durable Object requests a day (WebSocket messages count 20:1), D1's 5 million row reads and 100,000 row writes a day, free STUN, and 1,000 GB of TURN relay a month. That covers a few thousand daily players. Past that, the $5/month Workers plan, with no redesign.
- **One interface:** all online features sit behind one `OnlineBackend` autoload. The Cloudflare backend is the first one; Steamworks (GodotSteam, in a separate helper process for the GPL) or EOS can be added later without touching gameplay code.

Prior art: the GAME project's Steam lobbies, the steam-join snapshot fix and the 128 Hz tuning. The netcode lessons carry over; the Steam parts don't.

## Player cards, progression and ranked (2026-10-22)

Ozmium wants:
- Player cards that players customize, done the way trac3r does them, with a lot of choice and most of it themed on Quake III Arena.
- An XP system whose levels unlock card parts.
- Some parts that only achievements unlock.
- Ranked matchmaking with an Elo rating.

Core rule: **progression is cosmetic only.** Nothing unlocked changes movement, weapons, damage, or what a player can see in a match. Every player has the whole game from the first launch, so the game feel stays sacred. Anything Quake 3 gave everyone for free stays free: every model and skin, colored names, and the original rail colors.

### What we take from trac3r (`C:\gAME\TRAC3R\web`)
trac3r is Ozmium's own game, so its code and design are ours to reuse.
- **`src/game/cosmetics.ts`:** one pure-data catalog shared by the game and the server.
  - Each item is `{ id, name, unlock, value?, desc?, fx? }`, and `unlock` is `{ level: N }` or `{ achievement: id }`.
  - `isUnlocked()` and `unlockText()` ("LEVEL 40", "ACHIEVEMENT: UNTOUCHABLE") drive both the locker and validation.
- **`src/game/achievements.ts`:**
  - **Computed, not stored:** achievements are worked out from synced stats, never kept as flags, so they can't drift between devices.
  - Each one has a `target`, a `progress(profile)` function, a bronze/silver/gold medal, an icon and, for collections, `missing()`, which names what's still needed.
  - **Rarity:** each achievement shows the share of players who hold it, and anything under 10% is tagged RARE.
- **`enforceUnlocks()`:** before a card is shown to anyone else, every part is checked against the owner's level and achievements. A part they haven't earned falls back to the default, so a forged card can't show locked parts.
- **The card (`Card` in `profileSync.ts`):** title, frame, banner, accent, three showcase stats and three showcase badges. The rank badge, level and XP bar are automatic.
- **Banners in three kinds (`ui/bannerArt.ts`):**
  - Pattern banners.
  - Drawn and animated banners. Animated ones loop on the live card, freeze in exports, and stop for Reduce Flashes or reduced motion.
  - Stat-driven banners: Signature Shape, Mastery Map, Trophy Wall, Rank Crest, Season.
- **Prestige rewards:** the hardest achievements pay animated showpieces (trac3r's Blood Bath and Acid Bath).
- **The ladder never goes quiet:** trac3r 1.12.17 added an unlock every 5 levels from 55 to 100 after players found levels 61–99 empty. Do the same from the start.
- **Profile tabs:** CARD (editor), ACHIEVEMENTS, LOCKER.
- **Share card image:** a 1080×1350 PNG drawn from the same banner code (`ui/cardImage.ts`).
- **Where cards show:** any leaderboard row opens that player's card, and the duel lobby shows the opponent's card.
- **Ranked (design doc §8.3):** Ozmium's refined Elo, below.
- **Ranked unlocks at level 30** (Ozmium, 2026-10-22): `Q3Profile.RANKED_LEVEL`. The menu shows it locked with the player's progress, and the hub's ranked queue must refuse anyone under it too.

### The card, Quake 3 style
Styled like Quake 3's own UI: a dark gothic-metal plate, the name in Quake 3's banner font, a red accent glow and medal art.

| Slot | Contents |
|---|---|
| Banner | Wide art across the top (catalog below) |
| Emblem | Square portrait overlapping the banner: a character head, weapon, item or medal |
| Frame | Border around the whole card |
| Name | Quake 3 `^` color codes, free as always |
| Title | One line under the name |
| Accent | Card trim, glow and XP bar color |
| Level, XP bar, rank | Automatic; the rank shows the tier icon, tier and division, and MMR (see "Ranked") |
| Showcase stats (3) | Picked from the career stats list |
| Showcase badges (3) | Achievement medals, drawn as Quake 3 medals in bronze, silver or gold |
| Taunt | The voice line played when the card appears on the duel versus screen: any character's `taunt.wav` |
| ID strip | Player ID code; OFFICIAL when the player's progress includes official-server play, otherwise LOCAL |

**Where it shows:**
- The menu's top bar and Profile.
- Lobby and party lists, and leaderboard rows (click to open the full card).
- The duel versus screen, where both cards slide in and both taunts play.
- Scoreboard rows (compact: emblem, name, level).
- The "fragged by" card on death, the end-of-match results, and the spectator HUD.

### Catalog: 378 parts, mostly Quake 3 (built 2026-10-22)
- **Source:** `tools/card_catalog.py` (the data), baked by `tools/build_cards.py` into `library/cards/`: `cards.json`, 126 emblems, 98 banner images and 14 frame swatches.
- **Godot side:** `Q3Cards` (`scripts/cards/q3_cards.gd`) reads the catalog. It's the port of trac3r's `isUnlocked`, `enforceUnlocks`, achievement progress and `missing`, plus the level curve and showcase stats.
- **Counts:**

  | Banners | Emblems | Frames | Titles | Accents | Rail colors | Taunts | Achievements |
  |---|---|---|---|---|---|---|---|
  | 110 | 126 | 29 | 54 | 15 | 15 | 29 | 89 |
- **Self-test:** two checks. One covers the catalog, art and rules (unique ids, art on disk, achievement references, no 5-level gap in the level track). The other covers the unlock logic (stats unlock parts, achievements are computed, forged cards fall back).
- **Skies:** Quake 3 has only 7 distinct sky looks; its 49 sky shaders reuse the same cloud layers. So there are 7 sky banners, and 27 material banners carry the rest of the level track.

**Banners (110):**

| Group | Count | Unlock |
|---|---|---|
| Arena levelshots: the shot sharp on the right, fading into a blurred wash of itself (re-rendered from the imported maps after G1) | 35 | Win a match on that arena |
| Skies, looking just above the horizon (a slow pan on the live card) | 7 | Level track |
| Character renders: each fighter posed in the smoky showcase style | 29 | Beat that bot in the campaign, or play 25 matches as that character |
| Materials as a lit wall at Quake 3's scale, some animated: the Q3A and proto banners, gothic and bloody blocks, skull arch, head wall, pentagrams, flesh, grates, jump pad, lava, slime, hellfire, CTF flags, arena tower | 27 | Level track |
| Stat-driven: Trophy Wall (your three best medal counts), Weapon Mastery (best weapon and its accuracy), Favorite Arena, Rank Crest, Season, Campaign Map (tiers cleared, skill per arena) | 6 | Achievements |
| Prestige, animated: Quad Damage (the card pulses with the blue shell), Lava Bath (your favorite arena sinks into rising lava), The Longest Yard (rail trails cross the void), Gibbed (gauntlet kill fountain), Nightmare! (Xaero over the final arena) | 5 | Hardest achievements |
| The Void (the menu's smoke) | 1 | Free, the default |

**Emblems (126):**

| Group | Count | Unlock |
|---|---|---|
| Default-skin heads | 29 | Free |
| Alternate-skin heads (Krusade, Roderic, Cadavre, Angel ...) and team heads | 66 | Play 10 matches in that skin |
| Weapon icons (gauntlet to BFG) | 9 | 100 frags with that weapon |
| Item icons: Quad, Regeneration, Haste, Battle Suit, Invisibility, Flight, Mega Health, armors, Medkit, Teleporter; the two flags | 13 | 50 pickups of that item; the flags from captures and returns |
| Medals: Excellent, Impressive, Gauntlet, Capture, Assist, Defend, Accuracy, Frags, Victory | 9 | 100 of that medal (Accuracy, Frags, Victory: Deadeye, 1,000 frags, 100 wins) |

**Frames (29):** Plain (free) and:
- **Level track every 10 levels:** Rusted Iron, Riveted Steel, Gothic Stone, Base Tech, Bloodstained, Bronze, Silver, Gold (Quake 3's gold brick), Arena Master (the skull trim).
- **Powerup frames, animated:** Quad (blue energy), Battle Suit (gold), Regeneration (red pulse), Haste (smoke trail), Invisibility (shimmer). Each comes from its powerup achievement.
- **Campaign:** one frame per tier cleared, and a Nightmare! frame for clearing everything on Nightmare.
- **Ranked:** a frame for each tier from Silver up (peak rating).

**Titles (54):**
- **Level ranks:**
  - 1 Fresh Meat, 5 Initiate, 10 Fragger, 15 Contender, 20 Gladiator, 30 Arena Veteran, 40 Warlord, 50 Champion.
  - 60 Immortal, 70 Vadrigar's Chosen, 80 Deathbringer, 90 Arena Lord, 100 Xaero's Equal.
  - Placeholders for Ozmium to rename. The Vadrigar are the Arena's masters in Quake 3's story.
- **Medal titles:** Excellent!, Impressive!, Humiliator (Gauntlet), Flag Runner, Defender, Team Player.
- **Weapon titles:** Rail God, Rocket Scientist, Plasma Junkie, Shaft Master, Buckshot, Grenadier, Pea Shooter, BFG Bandit.
- **Movement titles:** Strafe Jumper, Speed Demon, Rocket Jumper, Plasma Climber.
- **Campaign titles:** Recruit, Xaero Slayer, Arena Eternal (all on Hardcore), Nightmare Walker (all on Nightmare!).
- **Ranked titles:** one per tier.
- **Feat titles:** Perfect, Untouchable, Telefragger, Tourist, Full Roster, Show-Off, Duelist, Last One Standing, Quad God, Deadeye.
- **Obituary jokes from the right accidents:** Cratered, Lava Swimmer, Telefragged, Sank Like a Rock.

**Accents (15):** Quake 3's seven `^1`–`^7` colors are free. Unlocks add more: rust, bronze, chrome, plasma blue, slime green, quad blue and lava orange on the level track; gold for 1,000 wins.

**Taunts:** every character's taunt is free; it's the original game's sound.

**In-match cosmetics (the locker):**
- **Rail trail:**
  - Quake 3's `color1`/`color2` core and spiral colors stay free.
  - Unlocks add more colors and patterns: a rainbow spiral, a team-colored rail, a pulsing core.
  - The spiral style stays the original.
- **Not in the catalog:** weapon skins, model changes, death and spawn effects, and anything else that changes what an opponent sees beyond the original options.
- **Overrides:** competitive settings (forced enemy model, bright skins, forced rail color) override every cosmetic.

### XP and levels
- **Earned per match** from what q3core's game code already reports:
  - Finishing the match 100, a win 150, each frag 10, each capture 50.
  - Each Quake 3 award 25: Excellent, Impressive, Gauntlet, Defend, Assist, Capture, and the postgame Perfect.
  - The first win of the day doubles that match.
  - Tuned so a level takes 3–5 matches early and 8–10 later.
- **Curve:** levels 1–100.
  - trac3r's curve: XP to the next level is round(80 × level^1.25), rescaled to match XP.
  - An unlock at every level to 10, then at least every 5 levels to 100.
  - Optional prestige after 100: the level resets and a prestige frame is earned.
- **Offline:** matches vs bots and the campaign give half XP, so offline players progress. Training gives none.
- **Anti-farming:**
  - XP only for matches played to the end.
  - None for idle players (no input for 60 s).
  - Servers with non-standard rules (fraglimit 1, modified game code) are flagged unofficial and give no XP.
- **Results screen:** XP breakdown, the bar filling, the level-up, and new unlocks shown as card parts.

### Unlocks
Each part has exactly one rule in `cards.json` (`Q3Cards.is_unlocked`):

| Rule | Meaning |
|---|---|
| `{"free": true}` | Available from the start. Covers everything Quake 3 itself gave away (default heads, the seven colors, rail colors 1-7, taunts), plus a starter set so new cards don't look bare |
| `{"level": N}` | The level track |
| `{"achievement": id}` | Only through that achievement, never through levels |
| `{"stat": "a.b", "min": N}` | A career stat: `wins_map.q3dm17` (levelshot banners), `matches_skin.sarge/krusade`, `pickups.quad`, `ranked_peak` (ranked frames and titles) |
| `{"campaign": [arenas], "skill": S}` | Every listed campaign arena won at skill S or higher (tier frames) |
| `{"any": [rules]}` | Any one of them (character banners: beat that bot in the campaign, or play 25 matches as them) |

### Achievements (89, bronze / silver / gold)
- **Data:** a catalog like trac3r's `achievements.ts`: id, name, description, icon, medal, target, progress from career stats, `missing()` for collections, reward parts.
- **Kinds, all Quake 3:**
  - **Medals:** 10, 100 and 1,000 Excellents, Impressives and Gauntlets.
  - **Weapons:** 100 and 1,000 frags with each weapon. Rail accuracy over 50% in a match.
  - **Feats:** win a duel with no deaths (the postgame Perfect), five rail frags in a row, a frag while airborne from a rocket jump, a telefrag.
  - **Modes:** captures, flag returns, a Clan Arena round won alone.
  - **Campaign:** each tier, each tier on Nightmare, beat Xaero.
  - **Collections:** win on every original arena (`missing()` lists the rest), play every character.
  - **Movement:** reach 800 ups.
  - **Accidents:** die in lava 50 times (pays the Lava Bath banner).
  - **Powerups:** 50 Quad pickups, a frag under every powerup.
- **Tracking:** a `Q3Progress` autoload reads q3core's events and player state into career stats. Achievements are computed from those stats, as in trac3r, and the same stats feed XP and the showcase.
- **Storage:** synced to the online backend; the in-game list works offline.
- **Notification:** the announcer-style medal pop for a new achievement after the match, and a small in-match toast (can be turned off).

### Ranked (Elo)
Taken from trac3r's tuned ladder (Ozmium's "LP should work like the chess rating system"):
- **Rating:**
  - Plain Elo: everyone starts at 1000, and a result moves both players by `K × (score − expected)`, where `expected = 1 / (1 + 10^((opp − me) / 400))`.
  - It's zero-sum, so the ladder can't inflate.
  - K = 40 for a player's first 30 ranked games, 24 after, and 16 in the top tier. The floor is 100.
- **Queues:** Duel (the headline), then TDM, CTF and Clan Arena when the player count allows. Each queue has its own rating. In team modes the opponent rating is the other team's average, and everyone on a team moves by the same amount.
- **Tiers (Ozmium, 2026-10-23; they replaced Quake 3's skill names):**
  - Bronze (below 1000), Silver (1000), Gold (1300), Platinum (1600), Diamond (1900), Godlike (2200+).
  - Divisions III → II → I, every 100 points; Godlike is open-ended.
  - Each tier has its own icon (`webui/components/RankIcon.tsx`): a shield gaining chevrons, blades, wings and a horned crown.
  - The rank and MMR show after 10 placement matches, on the card, the top bar and the Ranked page (which also explains the system).
- **Seasons:**
  - Three calendar months, aligned to quarters.
  - At rollover each player's season is archived (final rating, peak, W/L), and the rating moves a quarter of the way back toward 1000.
  - Done lazily on the player's first contact, so no scheduled job is needed.
  - Season rewards are the tier's frame and title.
- **Rules:**
  - Fixed competitive settings per mode with no modifiers.
  - An official map pool: q3tourney1–6 and the pro maps for Duel.
  - Duel maps are picked by alternating bans until one map is left.
  - Leaving a match counts as a loss, and dodging the queue gets a short lockout.
- **Matchmaking:**
  - The backend's Durable Object queue, by rating and region (see "Multiplayer and online services").
  - The window starts at ±150 and widens 25 per second; after 10 s it takes the closest available player (trac3r's numbers).
  - A party queues at its highest member's rating.

### Trust: the hard part of an open-source game
An open-source client can change anything it stores or reports. OZMIUM's HMAC key would be public here, and any stats the client reports itself can be edited. trac3r's answer is a server that owns the profile: a Cloudflare Worker with D1, `enforceUnlocks()` on every card it serves, and a merge that never loses progress.

| What | Who decides it | Cheating risk |
|---|---|---|
| XP, levels, unlocks, achievements | The match's server. Offline or on a listen server, that's the player's own machine. Stored in `user://`, synced to the backend | Someone can unlock cosmetics they didn't earn. The harm stops at an undeserved-looking card, and the ID strip says LOCAL rather than OFFICIAL |
| Ranked rating | **For now (Ozmium, 2026-10-23): peer-to-peer, by agreement.** Each player's game signs the final scoreboard it received with its identity key and sends it to the ranked backend. The backend applies the result only when the reports match: every player in a duel, a majority of non-host players in larger matches. Mismatches or missing reports void the match and are logged per host. **Later:** verified dedicated servers that sign results with a key that isn't in the repo. The ranked backend stores ratings, matches, seasons and the ladder either way | A modified host can still play unfairly (the others see it and can report it), but it can't change the result on its own. It would need the other players' games to sign a false scoreboard too. Hosts whose matches keep getting voided or reported lose ranked hosting |
| Cards others see | Every viewer runs `enforceUnlocks()` against the owner's stats. The backend does it for leaderboard cards | A card can only show parts its stats justify |

- **Ranked backend:** the same shape as trac3r's (Worker + D1, sessions, cards, ladders), with players identified by their device key (or linked Discord) instead of a Google sign-in.
- **Cost:** the backend runs on Cloudflare's free tier; ranked runs on player hosts until verified dedicated servers, run by trusted community operators, take it over. The backend is also the most public piece of the project, which matters while outputs stay private.

### Effort and order
Hours of Claude working time:

| Piece | Needs | Hours |
|---|---|---|
| **Done:** catalog (`cards.json`, achievements) ported from trac3r's structure, filled with the Quake 3 content above; `Q3Cards` | Nothing | 2 |
| **Done:** `build_cards.py`: levelshots, skies, character renders, icons, medals, material banners, frame swatches | The library; levelshot re-renders wait for G1 maps | 3 |
| `Q3Progress`: career stats, XP, levels, unlocks, the XP results screen | G4 (match flow and events) | 2 |
| Achievements: progress, rarity, toasts, Profile tab | `Q3Progress` | 2 |
| Card renderers: React in the menu and native `Q3PlayerCard` in matches, animated banners, share PNG | G5 (native in-game UI) | 4 |
| Card editor and locker (Profile: CARD, ACHIEVEMENTS, LOCKER), rail colors | Card renderers | 2 |
| Card in the scoreboard, versus screen with taunts, fragged-by card, lobby | G5, multiplayer | 1.5 |
| Ranked rules: Elo, tiers, seasons | Multiplayer | 1.5 |
| Matchmaking queue on the Cloudflare backend, duel map bans | Online backend | 3 |
| Ranked backend (from trac3r's Worker) and signed results from dedicated servers | Dedicated servers | 3.5 |
| **Total** | | **about 25** |

Order:
1. **After G5:** cards, the catalog, XP and achievements. They work offline against bots with no network code.
2. **After G1:** the levelshot banners, re-rendered from the imported maps (until then, the original levelshots upscaled).
3. **After multiplayer and the online backend:** ranked.

## Built-in map editor (same as OZMIUM's, 2026-10-22)

Ozmium wants a fully built-in map editor that works exactly like the OZMIUM one: in the game itself, not a separate tool, usable by every player. OZMIUM's editor (GAME project, Unity) is the spec; the Godot version rebuilds the same features.

| Area | Feature set (as in OZMIUM) |
|---|---|
| Geometry | TrenchBroom-style convex brushes (planes are the truth, windings clipped in double precision); box create, select/move/rotate/mirror/duplicate/group, face tool, vertex tool (delete corners -> wedge/chamfer) |
| CSG | Subtract tool (X) and CSG subtract selection (Ctrl+Shift+X); perimeter groove and empty-space fill modes; cut faces inherit the material and UV alignment |
| Curves | id Tech 3 biquadratic Bezier patches with control-net gizmo |
| Texturing | Material browser with supersampled thumbnails, skies tab, per-face UV axes, world-lock / texture lock to brush (Ctrl+T), per-face tint, all PBR materials from the remastered texture library |
| Entities | Entities tab with rendered pickup tiles for every weapon, armor, health, ammo and powerup; spawn points (P), jump pads (J, two clicks, editable landing), teleporters (K), sound emitters, lights, thumbnail camera; free surface drag with Shift grid snap |
| Prefabs | Save selections as prefabs (with thumbnails) and place them |
| Workflow | Snapshot undo/redo, one keymap table with an F1 cheat sheet, autosave, map thumbnails, leave-to-menu with unsaved check |
| Play-test | In-place play-test (F6) with the real 128 Hz predicted movement; F7 markers, F8 dev materials, F3 third person; test bot from the pause menu |
| Collaboration | Multiplayer editing over the same peer-to-peer connections as matches: host-authoritative, object-diff sync, per-user undo, soft locks from selections, presence avatars with names, invite friends |
| Bots | "Bake bots" in the editor; nav data stored inside the map file |
| Sharing | Maps as one file + thumbnail, shared by hand; Publish lists a map in Create Match. A Workshop on Cloudflare R2 is planned as M9 in `MULTIPLAYER_PLAN.md` |
| Testing | Geometry self-test, API smoke test and input test suites (OZMIUM had 52 + 191 + 84 checks); the Godot port gets the same coverage headless |

Godot notes: runtime brush meshing and its own collision/trace world (not Godot's CSG nodes); UI in Godot Control nodes (OZMIUM used OneJS/React); editor networking rides the same WebRTC connections and match browser as matches.

**Superseded in part by `MAP_EDITOR_PLAN.md` (2026-10-23):** OZMIUM has since dropped the groove/fill subtract modes (Ctrl+Shift+X CSG subtract remains), and play-test is F5 (F6 returns). There is no separate trace world: play-test compiles the map with id's q3map and plays it in q3core, and bots come from bspc.

## Original Quake 3 maps in the remaster (2026-10-22)

Ozmium wants compatibility with Quake 3's original maps, imported into the remaster.

- **BSP import (compiled maps, the 35 in the paks plus community pk3s):** OZMIUM's importer is the proven reference (Aerowalk, Blood Run and Pillamyd were ported with audits).
  - Solid brushes come in as editable brushes, and patches from lump 13.
  - Each drawn surface's texture alignment is fitted back onto its brush face by least squares.
  - Non-solid drawn brushes, liquids, fog and the six-sided sky come across.
  - Every entity class in the catalog maps across: spawns, pads and their targets, teleporters, items, speakers, movers, triggers, `misc_model` props, the intermission camera.
  - Baked lightmaps are dropped and the maps are relit in-engine.
- **.map source import:** Radiant-format brush, patch and entity files, the lossless route into the editor.
- **.map export:** back to GtkRadiant, NetRadiant and TrenchBroom for round trips.
- **Batch and headless import,** plus a BSP-versus-imported audit that checks every drawn surface has an aligned face.
- **Materials:** every map surface resolves to the PBR material library below, by shader name.
- **Map pool:** q3dm0–19, q3tourney1–6, q3ctf1–4 and the four CPM "pro" variants from the player's paks. Community maps carry their own licenses (Blood Run's readme forbids redistribution), so the importer reads each map's readme and flags its license.

## Every texture fully PBR (2026-10-22)

Ozmium wants all of Quake III Arena's textures to be fully PBR. That's 1,919 images in the paks:
- 1,098 world textures (base_*, gothic_*, ctf, liquids, sfx, skies ...)
- 552 model textures
- effects, sprites, icons, menus and level shots

| Group | Treatment |
|---|---|
| World textures (tiling) | make_pbr tiling mode: wrap-aware upscale (no island padding), seamless edges, world-texture classes (stone, metal, wood, gothic carving, tech panels, organic), height -> normal, matte roughness and metal per class, painted lighting moved to AO; stochastic anti-repetition on large faces (Ozmium's no-visible-repeat rule) |
| Shader effects | Additive stages -> emissive; alphaFunc -> cut-outs; surface lights (q3map_surfacelight) -> emissive intensity + real lights; scrolling, rotating and pulsing stages -> Godot shader parameters |
| Liquids | Water, slime and lava as animated PBR shader materials with flow, refraction and damage tags |
| Skies | Six-sided skies upscaled into Godot sky materials |
| Models | Done for all player models, items, weapons and effects (tools/make_pbr.py) |
| HUD, menus, icons | Upscaled and cleaned (no PBR needed), for the modern UI |

Output: one Godot material per shader name, so the BSP importer and the editor's material browser use the same library.

## Asset library spec (the shipping library)

| Item | Standard |
|---|---|
| Characters | The id roster, remastered: tool output as the base, then hand-finished (sculpt, retopo, texture) |
| Skeleton | One shared standard for all humanoids, based on the recovered rig: root, pelvis, thigh/calf/foot L/R, waist, spine, belly, chest, neck, head, upperarm/forearm/hand L/R, weapon socket; extra bones (fingers, tails, wings) per character |
| Animation | The 25 animation.cfg slots (plus the 6 Team Arena slots) at the original timings, newly animated, 60 fps, legs and torso layerable |
| Mesh | 20–40k triangles, joint edge loops, LOD0–2 |
| Textures | Matte PBR: albedo, normal (OpenGL), ORM (R AO / G rough / B metal), emissive; 2048² body, 1024² head |
| Formats | glTF/GLB for Godot; FBX for Unity if needed; .blend sources |
| Per asset | The source id file it came from, the tool output, the hand-finished asset, and a note of what changed (useful for any permission request) | Note that the GAME project's OZMIUM must stay separate: it can't contain Quake naming or id assets.

## Open decisions

1. Unity or Godot (can wait until Phase 4).
2. ~~Distribution model~~ Decided: free and open source, Quake 3 ownership required, ships the remastered asset library (see "Licensing" for the risk and mitigations). Still open: the project's new name, and whether to ask Bethesda/id for permission (recommended).
3. Which comes first after the player-model quality pass: weapons (quick) or maps (biggest visual payoff)?
4. Progression trust (see "Player cards, progression and ranked"). Recommended: XP, unlocks and achievements decided by the match's own server, including offline play, accepting that cosmetics can be cheated. Ranked rating: decided 2026-10-23, peer-to-peer for now, counted only when the players' signed scoreboards agree; verified dedicated servers that sign their results take over later (see "Trust").
5. ~~Ranked storage~~ Decided 2026-10-22: our own backend built from trac3r's Worker + D1, on Cloudflare's free tier.
6. Title names for the level ranks (placeholders in "Catalog").
