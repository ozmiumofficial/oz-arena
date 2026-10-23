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
