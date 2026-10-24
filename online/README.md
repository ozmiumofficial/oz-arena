# Online services

Friends, presence, parties and leaderboards for the remaster, on Cloudflare's free tier (PLAN.md "Multiplayer and online services").
A Worker (`src/index.ts`) checks each player's signature and hands the WebSocket to one Durable Object, `Hub`
(`src/hub.ts`). The Hub keeps players, friendships, friend requests, parties and party invites in its own SQLite.
Presence lives on the sockets themselves, which use hibernation, so idle players cost nothing.

## Identity

There are no accounts. On first launch the game makes an RSA key pair and keeps it at
`user://online/identity.key` (`%APPDATA%\Godot\app_userdata\Q3Remaster\online\` on Windows).

Every connection carries these headers:

- `x-key`: the public key, as base64 DER.
- `x-ts`: a millisecond timestamp.
- `x-sig`: an RSA-SHA256 signature of `hub|<ts>`.

The player id is the first 20 hex digits of SHA-256(key). The friend code (`ABCD-1234`, Crockford base32) is derived
from the id. The Hub refuses a timestamp that isn't newer than the last one it saw, so a captured handshake can't be
replayed.

A refused connection still upgrades, then closes with a code:

- **4001:** the identity check failed.
- **4002:** the game is out of date (`PROTOCOL` in `src/index.ts`).

Losing the key file means a new identity, and friends have to be added again. A "link Discord" recovery option is
planned (PLAN.md).

## Rules

| Rule | Value |
|---|---|
| Friends per player | 200 |
| Pending requests per player | 50 |
| Party size | 8 |
| Party invite lifetime | 5 min; invites only go to online friends |
| Inviting with no party | Starts a party with you as leader |
| Party with one member and no invites left | Ends |
| Leader leaves or goes offline | The longest-standing online member leads |
| Member offline for more than 10 min | Drops out of the party |
| Party chat | Max 200 characters, one message every 0.4 s |
| Rate limit | 20 messages per socket, refilled at 5 a second |

## Leaderboards

- The game sends `career {xp, card, stats}` on connect and a second after its career changes (a match ends, the card is
  edited). The hub works out the level from total XP with the game's curve (`XP_BASE`, `XP_STEP`, `MAX_LEVEL`, copied
  from `library/cards/cards.json`) and keeps one row per player in `careers`.
- `leaderboard {board: level|ranked, scope: global|friends}` answers with the top 50, the asker's own row and rank, and
  the total. The global boards are cached for 60 s, because SQLite row reads are what the free tier counts.
- Level board rows are self-reported, like every offline career (PLAN.md "Trust"). Cards are cleaned to part ids, and
  the viewer's game puts parts it doesn't know back to the default (`Q3Cards.known_parts`).
- The ranked columns (`rating`, `rated`, `peak`) are never written from a game. The hub will set them itself from
  peer-to-peer ranked matches, once every player's signed scoreboard agrees (PLAN.md "Trust", MULTIPLAYER_PLAN.md M6);
  verified dedicated servers take over later. The ranked board stays empty until then. A player shows on it after 10
  ranked matches.

## Matches (MULTIPLAYER_PLAN.md M4)

The hub is also the match directory and the WebRTC signaling channel; no game data passes through it.

- **Directory:** a host sends `net_host {name, mode, map, players, max, password (a flag), ranked, version}` and the listing lives on its socket, so it goes when the host does. `net_browse {region?}` answers `net_matches` (the asker's region first). Regions come from Cloudflare's request data: na_east, na_west, eu, sa, asia, oce, meaf.
- **Joining:** `net_join {host, password, version}` -> the host gets `net_request` and answers `net_accept` or `net_reject` (it decides on room, password, version and later bans) -> the joiner gets `net_accepted` or `net_refused`. Links last until either side leaves (`net_leave`, `net_gone`) or disconnects.
- **Signaling:** `net_signal {to, kind: offer|answer|candidate, ...}` is forwarded only between a host and a peer it accepted. Candidates that aren't relay candidates are dropped, and SDPs lose their `a=candidate` lines and `c=` address, so no player's address reaches another. `NET_DIRECT=1` turns that off for local tests.
- **Kicks:** `net_kick {peer}` (the host only) ends the link and bans that player from the host's matches for 30 minutes (`bans`); the player hears `net_gone {kicked: true}`, and a rejoin is refused with the minutes left. The game calls it when a passed vote kicks someone.
- **Relay credentials:** `net_ice` mints 1-hour credentials from a Cloudflare Realtime TURN key (dashboard > Realtime > TURN), one request every 2 s per player. The key stays a Worker secret:

```
npx wrangler secret put TURN_KEY_ID
npx wrangler secret put TURN_KEY_TOKEN
TURN_KEY_ID=... TURN_KEY_TOKEN=... node test/turn_check.mjs     # checks the key before it goes in
```

- **Spending guard:** Cloudflare Realtime TURN bills past 1,000 GB a month and has no hard cap, so the hub keeps its own. Each credential it mints is charged its worst-case traffic (a player 0.7 GB, a host 5 GB; real use measured in M8 is 56-80 MB a player-hour, MULTIPLAYER_PLAN.md). Hosts ask for credentials only when someone joins and theirs are over 45 minutes old and minting stops at 800 GB a month (`RELAY_BUDGET_GB` overrides), answering "Relayed play is paused for this month." The counter resets on the 1st. `GET /admin/relay` shows the month's total. `node test/relay_budget.mjs <admin token>` checks it (see the file for the `.dev.vars` it needs).

`npm run match` runs the directory and signaling checks against `npm run dev`; put `NET_DIRECT=1` in `.dev.vars` for the Godot tests (`rtc_loopback_test`, `net_host_test`), and leave it out to run `node test/match.mjs ws://127.0.0.1:8787 relay`.

## Matchmaking and ranked (MULTIPLAYER_PLAN.md M6)

- **Queue:** `queue_join {modes, ranked, region, rtt}` / `queue_leave`; the hub answers `queue_state` and, when a group forms, `match_found {match, host, you_host, mode, map, ranked, team, players}`. The matcher (`src/ranked.ts` `formGroup`) runs from a Durable Object alarm once a second while anyone is queued: ranked windows start at +-150 and widen 25 a second (the closest after 10 s), regions widen to neighbors after 60 s (Auto only), the best connected player hosts. Modes: duel 2, ffa 4, tdm 4, ctf 6; ranked queues are duel, tdm and ctf, from level 30.
- **Private matches:** the host lists with `net_host {match}`; the listing is hidden from the browser and only the match's players can join it.
- **Results:** `match_joined {match}` from everyone (90 s, or the match is void and the missing players are locked out for 5 minutes), then `match_report {match, me, rows, left, peers}`; `decide()` checks they agree, Elo moves the ratings (tables `ratings`, `seasons`, `rmatches`, `lockouts`, `host_stats`) and each player gets `match_result` and `ratings`. The duel rating also fills the leaderboard's ranked columns.
- **Tests:** `node test/ranked_unit.mjs` (no server); `node test/ranked.mjs` against `npm run dev` with `MATCH_FORM_MS=3000` and `MATCH_REPORT_MS=3000` in `.dev.vars` (start from a clean `.wrangler/state`).

## Run it locally

```
npm install
npm run dev                      # wrangler on ws://127.0.0.1:8787
npm run smoke                    # 24 end-to-end checks with three signed clients (pass wss://... to test the live hub)
node test/friend_bot.mjs         # fills the game's Friends panel: three bots befriend this PC's identity
```

The Godot side has its own test. It needs the local server running, and it uses the local server unless you pass `server=`:

```
godot --headless --path ../godot res://tests/online_test.tscn
```

## Deployed

The hub went live on 2026-10-22 at `wss://arena-online.fragnet.workers.dev`. It runs on Ozmium's personal
Cloudflare account (`account_id` in `wrangler.jsonc`), which keeps it off the business account and keeps its free-tier
allowance separate from TRAC3R's. The game's `Online.DEFAULT_SERVER` points there. Players can point the Online server
setting (Settings, Network) at any other hub.

To ship a change: `npm run deploy`. If the protocol changed incompatibly, bump `PROTOCOL` in `src/index.ts` and
`Q3OnlineClient.PROTOCOL` together.

Against a remote hub the smoke test only reads the leaderboards, so its test players never appear on the public
boards. With `HUB_ADMIN_TOKEN` set it removes its three players when it's done; without it they stay in the database.
The Godot online test's two players (`^4Tester A`, `Tester B`) stay too.

## Admin

`/admin/*` is the operator's, refused (403) unless the request carries `authorization: Bearer <ADMIN_TOKEN>`. The token
is a Worker secret (`npx wrangler secret put ADMIN_TOKEN`); locally it goes in `online/.dev.vars` (git-ignored). Ozmium's
copy is in `work/online_admin_token.txt`, outside the repo.

- `GET /admin/players`: every player with id, code, name, look, created, last seen, XP and friend count.
- `POST /admin/purge {"ids": [...]}`: removes those players and their friendships, requests, invites, party places and
  careers. Friends and party members see them go.

Rename the Worker once the project has its name; nothing may say "Quake". Renaming changes the address, so update
`DEFAULT_SERVER` at the same time.
