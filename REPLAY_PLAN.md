# Replays, replay viewer and spectator mode: plan (approved by Ozmium, 2026-10-23, with every recommended choice below; built after the map editor)

Ozmium asked for replays, a replay viewer and spectator mode (2026-10-23). PLAN.md already lists them under "Multiplayer" (free and follow cams, server-side and client demos; the killcam it also listed was dropped, 2026-10-23); this plan makes them a phase of their own, because most of it doesn't need netcode.

## What we have to work with

- **The host owns the whole server.** q3core runs ioquake3's server in-process, so a recorder can read every entity and every client's playerState each frame, not just what one player's PVS let through. Original Quake 3 demos are one player's view; ours can be the whole match.
- **One seam in Gameplay.** `Q3Client.apply_snapshot(snap, ps)` draws whatever snapshot dictionary it's given. Gameplay gets everything else from `Q3Server` (time, `player_state(cl)` for every client, configstrings, cvars, `next_commands`): 48 call sites in `gameplay.gd`, plus a few in `q3_hud.gd` and `q3_client.gd`.
- **ioquake3's own tools are already compiled in:** `msg.c` (delta entity and playerState coding, Huffman) for a compact file, and `cm_*.c` for collision, so effects that trace (LG beam, pellets, impacts) still work in playback with only the map loaded.
- **id's game code does spectating itself:** `team spectator`, free fly (PM_SPECTATOR), `follow`, `follownext`/`followprev`, the tourney waiting line, and the follow fixes the audit kept (`AUDIT_ioq3_vs_id.md`).
- **Missing:** `Q3Client` has no snapshot interpolation. The live player reads the server every 8 ms frame, so it never needed it. Replays at a lower rate need it, and so will online clients later; it gets built once, here.

## Design

**Recording (server side, every match):**
- `q3_host.c` records after each server frame at a fixed rate:
  - Every linked entity that isn't `SVF_NOCLIENT`: players, missiles, items, movers, and temp events, which live 300 ms, so none are missed.
  - Every active client's playerState, bots included.
  - Configstring changes.
  - Each client's new reliable commands (prints, center prints, chat, scores), tagged by client.
- **Encoding:** deltas against the previous frame with `MSG_WriteDeltaEntity` / `MSG_WriteDeltaPlayerstate`, Huffman coded. A full keyframe every 5 s, with an index at the end of the file for seeking.
- **File:** `user://replays/<date>_<map>_<mode>.q3replay`. A JSON header carries map, map checksum, mode, players with their card and result, length, date, format version and q3core commit. Then the gamestate (configstrings, baselines), then the frames.
- **Why not record inputs and re-simulate:** the bots and the game's random numbers aren't guaranteed deterministic across builds. Snapshots are what Quake 3, Quake Live and every arena shooter record; they replay exactly.
- **Size:** about 1 MB a minute at 40 Hz for an 8-player FFA (estimate, measured in R1), so 10 to 20 MB a match.

**Playback (the same Gameplay scene):**
- **A feed, not a fork:** Gameplay reads a `Q3Feed` instead of `Q3Server`. `Q3LiveFeed` passes straight through; `Q3ReplayFeed` wraps a C++ `Q3Replay` class that decodes frames into the same dictionaries `Q3Server.snapshot()` returns. The HUD, scoreboard, chat, music, announcer and effects all work unchanged.
- **Map only:** playback loads the map's collision (CM) but runs no game, so traces hit the world.
- **Interpolation** between frames, as cgame does (`CG_InterpolateEntityPosition`, `CG_InterpolatePlayerState`). Missiles keep evaluating their trajectories at any time.
- **Seeking:** jump to the nearest keyframe, decode forward, and rebuild `Q3Client` (entities, beams, loops) with sounds and one-shot effects muted until the target time. Corpses from before the seek point aren't recreated as ragdolls.

**The viewer (Godot-drawn, like the HUD):**
- **Transport bar:** play/pause, speed 0.25x to 4x, frame step, back/forward 5 s, next/previous frag.
- **Timeline markers:** frags and deaths of the watched player, flag events, medals.
- **Cameras:**
  - **First person** as any player, with their HUD and view weapon.
  - **Chase:** third person behind them.
  - **Free fly:** goes anywhere, because the recording has every entity.
  - **Orbit** around a player.
- **Player list** to pick who to watch (also the scoreboard rows), and toggles for the HUD, name tags and outlines.
- **Why Godot-drawn:** Gameplay's web view is the pause menu, and handing keyboard focus back and forth has bitten us before (CLAUDE.md "Pause menu keys"). The bar stays in the match's own UI.

**Replay browser (web menu, a new "Replays" page):**
- **List:** levelshot, map, mode, date, length, players, and your result.
- **Actions:** watch, rename, favorite (kept forever), delete, show folder.
- **Auto-clean:** keep the last 50 that aren't favorites.
- **Results screen:** "Watch replay" and "Save replay" buttons.

**Spectator mode (local now, online later):**
- **Joining:** "Spectate" in match setup, and Join/Spectate in the pause menu. It uses id's `team spectator`, so the tourney queue and team rules are id's own.
- **Cameras:** id's free fly and follow (attack follows the next player, as in Quake 3), plus a remaster chase cam that moves only the camera.
- **Spectator HUD:** the watched player's card, health, armor, weapon and ammo, both team scores, and a "following" banner.
- **Auto-director:** optional; follows the action (the newest frag, the closest fight, the flag carrier).
- **Online, once netcode lands:** spectators are ordinary clients on team spectator, so they come with it. Competitive matches add team-only spectating and a spectator delay (PLAN.md).

**Death cam (Ozmium, 2026-10-23, in place of the killcam):**
- **While you're dead, until you can respawn:** the camera leaves your eyes and follows your own ragdoll from behind and above, with the chase cam's collision (from R5), so you watch yourself fall.
- **Then the killer's card:** "KILLED BY <name>" with the killer's player card (their banner, title and level; bots use their bot cards).
  - It shows over the respawn countdown and comes in after the body has settled (about a second).
  - Deaths with no killer (falling, lava, suicide) show id's own obituary line and no card.
- **Respawning is id's:** the fire button respawns exactly when Quake 3 allows it, so the death cam never delays play.
- **Online (M phases):** the card comes from the killer's profile.
- A setting, on by default.

**Original Quake 3 demos:**
- Play `.dm_68` files: protocol 68 parsed in q3core (CL_ParseGamestate / CL_ParseSnapshot over `msg.c`).
- They hold one player's view, so free fly sees only what that player saw.
- Optional: export any player's view from a replay as a `.dm_68` that plays in Quake 3 and ioquake3.

**Clip export:** mark a range and render it at a fixed frame rate with Godot's Movie Maker (`--write-movie`, AVI or PNG frames). It has to be confirmed in an exported build first.

## Phases

Hours of Claude working time, at this project's pace.

| # | Phase | What it delivers | Hours |
|---|---|---|---|
| R1 | **Recorder** | Server-side recording in `q3_host.c`; delta + Huffman frames, keyframes and index; header; auto-record setting; size measured on a 10-minute 8-bot match | 2 |
| R2 | **Feed and playback core** | `Q3Feed` / `Q3LiveFeed` / `Q3ReplayFeed`; `Q3Replay` decoder in C++; collision-only map load; `Q3Client` interpolation and seek reset | 3 |
| R3 | **Viewer** | Transport bar, timeline markers, first person / chase / free / orbit cameras, player picker, HUD and tag toggles, keybinds in Settings | 3 |
| R4 | **Replay browser** | Menu "Replays" page over the bridge; results-screen buttons; favorites and auto-clean | 2 |
| R5 | **Spectator mode** | Spectate from setup and pause; spectator HUD; scoreboard click to follow; chase cam; auto-director | 2.5 |
| R6 | **Death cam** | While dead: a chase camera on your ragdoll, then "KILLED BY" with the killer's player card over the respawn countdown; id's respawn timing | 1.5 |
| R7 | **Quake 3 demos** | `.dm_68` playback; optional `.dm_68` export | 2.5 |
| R8 | **Clip export** | Movie Maker render of a marked range | 1 |
| R9 | **Tests and docs** | `tests/replay_test.tscn`: record a headless match, play it back and compare every frame's entities and playerStates with the live run, check seek accuracy and size. Also `spectator_test`, a death cam test (the camera on the body, the killer's card, respawn on id's timing), a `.dm_68` parse test, captures, README, CLAUDE.md | 1.5 |
| | **Total** | | **about 19** |

Order: R1 -> R2 -> R3 -> R4 -> R5 -> R6 (the death cam uses R5's chase cam), then R7 and R8 in any order, R9 last. Replays are watchable after R3.

## Decisions (decided 2026-10-23: every recommended choice)

1. **When:** after the map editor (E8). R2's interpolation is reused by online play.
2. **Recording rate:** 40 Hz with interpolation (about 1 MB/min), not the full 125 Hz.
3. **Auto-record:** every match, keeping the last 50 plus favorites.
4. **Killcam:** dropped (Ozmium, 2026-10-23: "dont need a kill cam"). It was planned as an off-by-default setting showing the killer's last 4 s. In its place, the death cam (R6, Ozmium's request the same day): your own ragdoll followed, then the killer's card.
5. **Original `.dm_68` demos:** playback only. Export to `.dm_68` stays optional (+1 h of R7) and isn't scheduled.

## Progress

**R1 recorder done (2026-10-23).**
- **Recording:** `tools/q3core/src/engine/q3_replay.c`, run by `q3_host_frame` after every server frame. It reads each client's new reliable commands every server frame (so none are lost from the 64-deep ring) and writes a frame at 40 Hz: every linked entity that isn't `SVF_NOCLIENT`, every active client's playerState (bots too), the configstrings that changed, and the commands with a mask of the clients they reached (a broadcast is kept once). `SVF_SINGLECLIENT` / `SVF_NOTSINGLECLIENT` are kept per entity, so a player's view leaves out the copies of their own predicted events, as a network client's does.
- **Encoding:** msg.c's `MSG_WriteDeltaEntity` / `MSG_WriteDeltaPlayerstate` over its Huffman coder, deltas against the frame before; a keyframe every 5 s (and the first frame, which holds every configstring: the gamestate). Text (chat, prints, configstrings) is stored as its bytes, because msg.c's string writers turn `%` and every byte over 127 into `.`.
- **File:** `user://replays/<date>_<time>_<map>_<mode>.q3replay`, written by `Q3ReplayRecorder` as the match plays: "Q3RP", version, a JSON header (map, map checksum, mode, title, date, the recording player, q3core version), the frame blocks, then a JSON trailer at the end (length, final standings with player cards, result) and "Q3RT". Each block is length-prefixed, so the file is its own index: opening it hops from block to block, and a recording cut short by a crash still plays to its last whole frame. (The plan's index at the end isn't needed for that.)
- **Auto-record:** Settings > Gameplay > Record replays (on), Replays kept (50); favorites are never deleted. Map editor play-tests aren't recorded (their map exists only in the editor).
- **Size, measured:** a 10-minute match on q3dm17 with 8 players: 24,001 frames, 9.0 MB, **0.88 MB a minute** (the plan's estimate was about 1 MB).
- **Reviewed** by a second agent; its nine findings are fixed: a trace could raise ERR_DROP and shut a running match down (bad inline-model index, playback calls while a server runs), a damaged file could fail the whole open, `record_stop` could leave the recorder on, chat lost `%` and UTF-8, realloc failures, commands reordered within a frame or dropped after a reconnect, a same-map reload, and an integer overflow in the block scan.

**R2 feed and playback core done (2026-10-23).**
- **The feed:** `Q3Feed` (`scripts/replay/`), with `Q3LiveFeed` passing straight through to `Q3Server` and `Q3ReplayFeed` playing a file. Gameplay, Q3Client and the HUD read only the feed (the `Q3Server` calls that read state moved to it); the live match is unchanged.
- **Decoder:** `Q3Replay` (C++, `src/q3replay.cpp`) opens the file, scans every frame once (the configstring and command tables, and the timeline's events: obituaries, flag sounds, medal counts), and keeps two decoded frames, the one shown and the next. Seeking decodes from the keyframe at or before the target (about 8 ms).
- **Collision only:** `Q3Replay.load_map` loads the map's collision model with no server running; traces follow cgame's `CG_ClipMoveToEntities` (the world plus the frame's solid entities), so the lightning beam, pellets and impacts hit as they did.
- **Interpolation:** the watched player's view as `CG_InterpolatePlayerState` (origin, velocity, view angles, bob), the other players as `CG_InterpolateEntityPosition` (`Q3Client.set_next`), not across a teleport. Missiles and movers follow their trajectories at any time.
- **Seeking:** `Q3Client.reset()` then a muted rebuild from the target frame: no sounds or one-shot effects, and bodies already down aren't ragdolled again. A scrub a little ahead plays only its last frames' events.
- **Verified:** `tests/replay_test.tscn` records 30 s of q3dm17 with 8 players and compares every one of 1,201 decoded frames with the live server: every entity and every playerState, field for field at msg.c's widths (what a network client receives). 60 random seeks land on exact frames; the timeline has one obituary per death; the local player's commands come back exactly, `%` and accented letters included.

**R3 viewer done (2026-10-23).** `Q3ReplayViewer` (`scripts/replay/q3_replay_viewer.gd`), Godot-drawn in the HUD's style:
- Transport bar: play/pause, 0.25x to 4x, frame step, back/forward 5 s, previous/next frag (landing 1.5 s before it), and a timeline to click or drag with markers: the watched player's frags (amber) and deaths (red), everyone else's frags, flag events, medals.
- Cameras: first person (their eyes, HUD and gun), chase and orbit (`Q3ChaseCam`: backs off along the view and pulls in where a wall is, `MASK_SOLID` box trace), free fly (right mouse to look, the movement keys to fly).
- The player list (P) picks who to watch; toggles for the HUD, name tags and outlines (`shaders/outline.gdshader`). The HUD's bottom row moves up over the bar.
- Keys in Settings > Controls > Replays (K play, J/L 5 s, -/= speed, comma/period frames, [/] frags, F camera, Q/E player, P list, H HUD, N names, O outlines, B hide the bar).
- Captures: `tests/replay_capture.tscn` (first person, chase with names and outlines, the player list, orbit).
- Reviewed; fixed: times stamped later surviving a jump back (the landing dip lifting the camera, a muzzle flash held lit, smoke frozen in the air), the right mouse leaving the mouse captured, Tab + right click capturing it, warnings silenced for the whole replay, corpse timers after a reset, the mirror body after a switch, a damaged file rebuilding every frame, and outlines hiding the armor flash.

**R4 replay browser done (2026-10-23).**
- Play > Replays (`webui/components/pages/ReplaysPage.tsx`): every replay with its levelshot, map, mode, date, length, players and your result; All / Favorites (Q/E); Watch, Favorite, Rename, Delete (confirmed), Show folder. The browser preview has mock replays.
- Bridge messages `replays`, `replay_watch`, `replay_rename`, `replay_favorite`, `replay_delete`, `replay_folder`; Godot checks every file name is a bare `.q3replay` of its own. Leaving a replay goes back to the Replays page (`Session.menu_page`).
- The results screen has Watch replay (R) and Save replay (S, a favorite).

**R5 spectator mode done (2026-10-23).**
- **Joining:** Create Match > You > Spectate (needs bots to watch), and Spectate / Join match in the pause menu. Both are id's: `teampref` "spectator" (the userinfo never carried `teampref` before; fixed in `Q3Host.userinfo`), and `team spectator` / `team free`, so the duel's waiting line, team balance and the 5-second switch limit are the game's.
- **Cameras:** id's free fly and follow (fire follows the next player); the weapon wheel cycles, Jump goes back to the free camera, Use item toggles the remaster's chase camera (the followed player's body is built from the playerState, since the server never sends their entity), a scoreboard row (Tab, right click) follows that player.
- **HUD:** FOLLOWING over the followed player's card, their vitals, ammo and weapons (the playerState is theirs), both team scores, the keys along the bottom; SPECTATING for the free camera. Spectators are left out of the standings; the intermission shows MATCH OVER with no XP.
- **Auto-director:** Settings > Gameplay > Auto-director (off by default; Gesture toggles it): twice a second it scores the players (a flag carrier, whoever just fragged, a powerup holder, anyone firing, enemies close by) and follows the best, holding each pick 4 s.
- **Verified:** `tests/spectator_test.tscn` (19 checks).

**R6 death cam done (2026-10-23).**
- **The body:** the server never sends you your own entity, so `Q3Client` builds your body from the playerState the moment it goes dead (your model and bright skin, your speed) and ragdolls it; it stays as a corpse when you respawn. A death not seen happening (after a replay jump, or following someone already down) and a gib (health at -40 or under, where `BG_PlayerStateToEntityState` hides the body) build none. A spectator following someone gets the death cam on them; switching players hands the bodies over instead of doubling them.
- **The camera:** behind and above the ragdoll's middle (`Q3Character.body_center`), through `Q3ChaseCam`'s collision.
- **The card:** a second after the death, KILLED BY with the killer's player card (bots' cards for bots) and the weapon (WITH THE RAILGUN, TELEFRAGGED), or id's own obituary line in `CG_Obituary`'s order: killing yourself first ("killed himself", "herself" or "itself" by the model's sex, "blew himself up" ...), then the world's lines ("cratered", "was in the wrong place" ...).
- **Respawning is id's:** fire works 1.7 s after the death (`g_combat.c` respawnTime), and the line under the card counts down to it, then says FIRE TO RESPAWN with g_forcerespawn's countdown. Spectators read FIRE - NEXT PLAYER; replays show no line.
- **The scoreboard** no longer opens by itself when you die (Ozmium, 2026-10-23): Tab only.
- Settings > Gameplay > Death cam (on). Off, the dead view stays at the eyes, as in Quake 3.
- **Verified:** `tests/death_cam_test.tscn` (14 checks: a second local client machineguns the test player; the body, the camera, the card and weapon, fire refused before 1.7 s and respawning after, the corpse, /kill's gib with no body and id's line, the setting off); windowed shots with `-- out=`.

**Scoreboard: your match (Ozmium, 2026-10-23, alongside R6).** The Tab board's left column shows your match (`Q3MatchStats`): score, deaths, damage, accuracy; each weapon's damage, hits against shots and accuracy (shots from your fire events, as `FireWeapon` counts them; hits once a weapon a server frame, as id's accuracy counts a shot); the distance you covered; the medals. The players are on the right; a clicked player's panel takes the left column. The game's hit message now carries the means of death and the time (`dmg <target> <amount> <armor> <means> <time>`, q3core's damage-numbers patch).

**R7 Quake 3 demos done (2026-10-23).**
- **Parsing:** `q3_demo_convert` (q3_replay.c) reads protocol 68 as `cl_parse.c` does: the gamestate (configstrings, baselines), snapshots with deltas from the ring of the last 32, packet entities from their baselines, server commands (the `cs` and `bcs0/1/2` configstring changes joined as cgame joins them), downloads skipped as `CL_ParseDownload` reads them. Each valid snapshot is written back out as a replay frame through the recorder's own encoder, so a demo plays through the same decoder, seeking, timeline and viewer.
- **One player's view:** the playerState goes under the client whose eyes it is (a demo recorded while following shows the followed player, and switches when they do), and their body, which the server never sends them, is built with `BG_PlayerStateToEntityState`. The free camera sees only what that player saw. A second map in one demo ends it at the first.
- **The Replays page** lists demos dropped into the replays folder and the ones in the Quake 3 install's `baseq3/demos` (never deleted from there), with a Q3 DEMO badge; demos on maps the remaster doesn't have are refused with a message. Their lines come from the first 128 KB of the file, kept for the session.
- **Decisions:** playback only; `.dm_68` export stays unscheduled (decision 5).
- **Verified:** `tests/demo_test.tscn` on id's own `demos/four.dm_68` (pak8): q3dm6, 63 s, 1,261 snapshots, opened in under 100 ms, every frame decoding with the player's view, seeks exact, 35 frags on the timeline, the browser's line, played in Gameplay with the other players and the demo's own player drawn.
- Reviewed; fixed: follow demos lost their view, the POV player had no body, back-to-back damaged demos could escalate to a fatal error (patch 0004), a map change mixed two maps, and the list reread every demo.

**R8 clip export done (2026-10-23).**
- In the viewer: IN and OUT (Home, End) mark a range, shaded on the timeline; EXPORT (F9) renders it from the camera on show, with or without the HUD, to `user://clips` (`Q3ClipExport`).
- The render is a second copy of the game under Movie Maker (`--write-movie`, `--fixed-fps 60`): it opens the replay at IN (from the editor, the Gameplay scene directly; in a build, `clip=` takes the splash straight through), plays to OUT with no controls and quits, so every frame is rendered however slow. An AVI (Motion JPEG with the game's sound), at the project's base size, 1600x900: Movie Maker takes that whatever the window or `--resolution` says.
- **Verified:** `tests/clip_test.tscn` (windowed): a 3 s range came out as 182 frames at 60 fps, 1600x900, about 21-26 MB, the copy quit by itself in about 17 s, and frame 120 decoded to the match with its HUD.
- **Reviewed; fixed:**
  - **A render that never ended:** a replay that failed to open, a damaged one, a build without a verified Quake 3 and Escape all used to leave the copy recording forever. It now quits on each, and `--quit-after` plus a time limit back that up.
  - **The render took keys** meant for the player's own game.
  - **Every built clip opened on the disclaimer:** in a build the splash showed and faded first; it's skipped now.
  - **Free and orbit cameras started from the map's origin;** they now start where the viewer was, and name tags and outlines carry over.
  - **A late joiner fell back to someone else:** the player watched was picked before the jump to IN.
  - **The render signed in online as the player.**
  - **Any non-empty file counted as saved;** now the AVI's frame count is checked. EXPORT turns into CANCEL while one renders.
  - **R7 demos:** the demo player's body ran one event behind (`entityEventSequence` isn't sent; the converter now keeps it as the server does).
  - The bridge checks the file's own map, not the page's.
- **Rechecked:** clip_test (182 frames for a 180-frame range, whole, CANCEL stops it), demo_test (175 of 178 body events on time, counting frames clear of an external event, which holds the server's body too).

**R9 tests and docs done (2026-10-23).**
- **New tests:** `replay_test` (every frame of a recorded 8-player match against the live server, 60 random seeks, the timeline, traces, the library and auto-clean, the menu bridge, then playback in the Gameplay scene: frames, jumps, cameras, players, keys, frags, outlines, the end), `spectator_test` (19 checks), `death_cam_test` (14), `demo_test` (id's four.dm_68), `clip_test` (windowed, the AVI and a decoded frame), `replay_capture` (viewer shots), `scoreboard_test` (now with the stats panel).
- **The existing suites still pass:** self-test 64/64, gameplay smoke, chat, settings wiring, campaign flow, the pause menu, map play-test 6/6, map geometry 25/25, map editor 96/96, ownership 23/23, the q3core match.
- **The exported build** (`tools/export_game.py`) renders a clip: `Q3Remaster.exe --write-movie ... -- clip=...` wrote 214 frames of a bot's first-person view at 1600x900 and quit by itself.
- **Matches run by tests** (a scene under `res://tests/`) record into `user://test_replays`, never the player's Replays. A match left by quitting keeps the last standings seen in its trailer (the server is down by then).
- **Docs:** CLAUDE.md (stage line, "Replay facts"), DEVELOPMENT.md (the library table and the tests), README (planned features: replays, spectating, the death cam, the scoreboard's match panel, as built for the next release).
