# Replays, replay viewer and spectator mode: plan (approved by Ozmium, 2026-10-23, with every recommended choice below; built after the map editor)

Ozmium asked for replays, a replay viewer and spectator mode (2026-10-23). PLAN.md already lists them under "Multiplayer" (free and follow cams, server-side and client demos, killcam); this plan makes them a phase of their own, because most of it doesn't need netcode.

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

**Killcam:**
- A setting, off by default, so the original feel holds.
- While you're dead, it plays the last 4 s from your killer's view, read from the recorder's ring buffer.
- Clicking respawns exactly when Quake 3 would, so it never delays play.

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
| R6 | **Killcam** | Ring buffer playback of the killer's view while dead (off by default) | 1 |
| R7 | **Quake 3 demos** | `.dm_68` playback; optional `.dm_68` export | 2.5 |
| R8 | **Clip export** | Movie Maker render of a marked range | 1 |
| R9 | **Tests and docs** | `tests/replay_test.tscn`: record a headless match, play it back and compare every frame's entities and playerStates with the live run, check seek accuracy and size. Also `spectator_test`, a `.dm_68` parse test, captures, README, CLAUDE.md | 1.5 |
| | **Total** | | **about 18.5** |

Order: R1 -> R2 -> R3 -> R4, then R5 to R8 in any order, R9 last. Replays are watchable after R3.

## Decisions (decided 2026-10-23: every recommended choice)

1. **When:** after the map editor (E8). R2's interpolation is reused by online play.
2. **Recording rate:** 40 Hz with interpolation (about 1 MB/min), not the full 125 Hz.
3. **Auto-record:** every match, keeping the last 50 plus favorites.
4. **Killcam:** a setting, off by default.
5. **Original `.dm_68` demos:** playback only. Export to `.dm_68` stays optional (+1 h of R7) and isn't scheduled.
