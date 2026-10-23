# Online multiplayer: plan (2026-10-23)

**Order (Ozmium, 2026-10-23):** last, after the map editor (`MAP_EDITOR_PLAN.md`) and replays (`REPLAY_PLAN.md`). Lag compensation (decision 2 below) is still open.

Real online matches, built on the decisions in PLAN.md "Multiplayer and online services" (all from 2026-10-22/23):
- Players host matches on their own PCs, peer-to-peer over WebRTC.
- Every internet connection goes through Cloudflare's TURN relay, so no one sees another player's IP.
- Matches are found in the closest region first, and players can pick a region.
- Anyone can run an always-on dedicated server.
- Ranked is peer-to-peer for now: a result counts only when the players' signed scoreboards agree.

## What exists today (surveyed 2026-10-23)

- **Server side is mostly there.** q3core compiles ioquake3's real server networking unmodified: netchan, `msg.c` delta snapshots, Huffman, `SV_PacketEvent`, the connect handshake (`SV_DirectConnect`), and `SV_SendClientMessages`. Nothing feeds it packets: `net_enabled` is 0, nothing calls `Com_EventLoop`, and the only client is the in-process local one (`q3_local_connect`, `q3_host.c:196`), which skips the network entirely.
- **There is no client side.** The build uses `null_client.c`, and no `cl_*.c` or cgame code is compiled. Gameplay reads the live server: `Q3Server.player_state` (the server's ps, not a snapshot), `configstring`, `cvar`, `trace` and `point_contents` on the server's world. `Q3Server.predict` exists but only a test uses it, and other players aren't interpolated between snapshots.
- **`Q3Client` is reusable.** `apply_snapshot` / `animate` / `present` take plain Dictionaries in `Q3Server.snapshot`'s shape, so a network-fed snapshot can drive it unchanged. Its server ties (configstrings, trace, point_contents) move behind one interface.
- **Online backend:** the hub (`online/src/hub.ts`) does friends, parties, presence and leaderboards. It has no match directory, no signaling, no TURN credentials, and the presence `join` field is never filled.
- **No WebRTC anywhere,** and the webrtc-native extension isn't in `godot/addons/`.
- **No dedicated mode.** `Q3MatchSetup.server_type` already lists `listen` and `dedicated`, but nothing branches on it. There's one export preset (Windows), and the q3core build is Windows-only (`sys_win32.c` hardcoded).

## Design

**One protocol: Quake 3's own.** Remote players speak id's real client protocol (getchallenge, connect, gamestate, delta snapshots, usercmds, reliable commands) over a WebRTC data channel instead of a UDP socket. The server stays id's code untouched, so the netcode behaves the way Quake 3's always has, and a later plain-UDP path for public dedicated servers and LAN uses the same packets.

**Transport hook (server).** A new address type, `NA_GODOT`, as a small patch in `tools/q3core/patches/` like the 1.32 reverts. `NET_SendPacket` for that type queues the packet for Godot; Godot hands incoming packets to `SV_PacketEvent`. The API gains `Q3Server.net_receive(peer, bytes)` and `Q3Server.net_outgoing()`.

**Net client (new C in q3core, `src/net/q3_netclient.c`).** A port of the network half of ioquake3's client, without the renderer, sound or cgame VM:
- connect handshake, gamestate, configstrings, `cs` updates, map changes and `map_restart`;
- snapshot parsing with delta against earlier frames (`CL_ParseSnapshot`, `CL_ParsePacketEntities`);
- usercmd packets with delta and the XOR encoding keyed on the server's commands (`CL_WritePacket`, `CL_Netchan_Encode`), and reliable commands both ways;
- pure-pak checksums, so `sv_pure 1` works and clients must have the real paks;
- its own collision world: the client loads the map's BSP through `CM_LoadMap` (players own pak0), traces against it, and clips against solid snapshot entities as cgame's `CG_Trace` does;
- prediction from the latest snapshot's ps with that trace (`CG_PredictPlayerState`), with the error decay cgame uses.

It's exposed as a `Q3Net` class whose results have the same Dictionary shapes as `Q3Server.snapshot`, so `Q3Client` doesn't care where they came from.

**Gameplay on a match source.** Gameplay, the HUD and `Q3Client` read through one interface with two implementations:
- **`LocalMatch`:** today's path (the host, offline matches, the campaign), unchanged in feel.
- **`RemoteMatch`:** fed by `Q3Net`. Other players and items interpolate between two snapshots (`CG_InterpolateEntityPosition`), the local player is predicted, and serverinfo replaces cvar reads.

A net overlay shows ping, loss, jitter and snapshot rate.

**Connecting (WebRTC through the relay).** Godot's official webrtc-native extension (1.2.2, MIT; Godot 4.3+) provides unreliable, unordered data channels.
- The hub mints short-lived Cloudflare TURN credentials per player, and the TURN key stays a Worker secret.
- Both sides gather relay candidates only (`iceTransportPolicy: relay`), and the hub drops any other candidate it forwards.
- Signaling (offer, answer, candidates) goes over the hub WebSocket each player already has open.
- A `Q3Transport` script moves packets between the data channels and q3core, and can add fake latency, loss and jitter for tests.

**Match directory (hub).** Hosts register a listing (mode, map, players, max, region, password flag, game version, ranked flag) and update it over their open WebSocket. The hub tags each listing with the host's region from `request.cf`, and drops it when the host disconnects. Joining looks up the listing, checks bans and password, then starts signaling. Friends' presence gets a real `join` value, so "Join" works from the Friends panel and parties.

**Regions.** As PLAN.md "Regions": home region from `request.cf`, Auto widens to neighboring regions after 60 s, a named region stays strict.

**Ranked (peer-to-peer).** As PLAN.md "Trust":
- Every player's game signs the final scoreboard it received with its identity key.
- The hub applies Elo only when the reports match (all players in a duel, a majority of non-host players otherwise), voids the match otherwise, and keeps a per-host void count.
- The matchmaker picks as host the player with the best connection to the others.

**Dedicated servers.** The same executable run headless (`--server <config>`), with no local client. It has its own identity key, so its listing, name and bans persist. It gets rcon and an admin page in the menu, sleeps its game loop when empty, and can start with Windows. A Linux x86-64 build follows (q3core with `sys_unix.c`, a Linux export preset); Linux ARM64 for Oracle's free tier comes after. Public servers can also take plain UDP and register with dpmaster.

## Decisions for Ozmium

1. **When:** decided: last, after the map editor and replays.
2. **Lag compensation:** id 1.32 has none, so online players lead hitscan shots by their ping, as in the original. Options:
   - **Classic** (recommended to start): exactly id 1.32.
   - **An optional server setting** for hitscan rewind, added later as a server rule, off by default.
3. **Cloudflare TURN key (Ozmium's step):** create a Realtime TURN key in the Cloudflare dashboard, then store it in the Worker with `npx wrangler secret put TURN_KEY_ID` and `TURN_KEY_TOKEN` in `online/`. The secret never goes in the repo or the chat.

## Phases

Hours of Claude working time, scaled to this project's pace. Each phase ends with headless checks and, where it's visible, a capture.

| # | Phase | What it delivers | Hours |
|---|---|---|---|
| M1 | **Server transport** | `NA_GODOT` patch; `net_receive` / `net_outgoing`; real remote clients reach `SV_DirectConnect` and get snapshots; listen server (host plus remote players) and a server-only mode; userinfo `ip` for remote clients | 0.5 |
| M2 | **Net client** | `Q3Net`: handshake, gamestate, configstrings, delta snapshots, usercmds, reliable commands, pure checksums, map changes, timeouts; client-side collision and prediction; an in-process loopback test (host and client in one process, fake latency and loss) proving the client's snapshots match the server's | 1.5 |
| M3 | **Gameplay over the network** | `LocalMatch` / `RemoteMatch` behind one interface for Gameplay, HUD and `Q3Client`; snapshot interpolation; prediction with error decay; net overlay; connecting and error screens; host-left and kicked flows | 1.5 |
| M4 | **WebRTC and the hub** | webrtc-native addon (and in the export); TURN credential minting; signaling ops; relay-only ICE with candidate filtering; `Q3Transport`; the match directory with regions; two game windows on one PC through the local hub, then two PCs through the live hub | 1.5 |
| M5 | **Menu online** | Live Server Browser (region, mode, ping, full/empty, password, friends in it); Create Match "Online" hosting and listing; join from Friends and party; invites and `connect` links; vote kick with the hub's 30-minute lobby ban | 1 |
| M6 | **Matchmaking and ranked** | Region queues per mode with Auto widening; host election; ranked rules (fixed settings, map pool, duel bans); signed scoreboards and agreement; Elo with placement, divisions and seasons; leaderboard writes; void tracking | 1 |
| M7 | **Dedicated servers** | `--server` headless mode, server identity, config and rotation, rcon and the admin page, idle sleep, start with Windows; Linux x86-64 q3core and export; plain UDP for public servers and LAN discovery | 1.5 |
| M8 | **Tuning, tests, docs** | Bandwidth per player measured (checks PLAN.md's relay budget) and snapshot-rate defaults set; two-process tests under simulated latency and loss; self-test checks; README, PLAN.md, CLAUDE.md | 0.5 |
| | **Total** | | **about 9** (GAMEPLAY_PLAN.md's own guess was 8-10) |

The wildcards are outside the code: debugging real two-PC connections through the relay, and a Linux toolchain for M7.

Order: M1 -> M2 -> M3 -> M4 (first real online match) -> M5 -> M6 / M7 (either order) -> M8.

**Later:** Linux ARM64 build for Oracle (about 0.5 h), collaborative map editing over the same connections (MAP_EDITOR_PLAN.md), dpmaster registration.

## Risks

- **Bandwidth:** the server runs at 125 Hz. Snapshots per client are capped by the client's `snaps` userinfo; the default is set in M8 from measurements, trading smoothness against the relay budget.
- **Host advantage:** the host plays at zero ping, as in any listen server. Host election keeps ranked fair-ish until dedicated servers take it over.
- **webrtc-native on Linux headless:** confirmed in M7 before promising Linux dedicated servers.
- **Build platforms:** a Linux q3core build needs a Linux toolchain (WSL or a CI runner); worked out in M7.
