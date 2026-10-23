# Online services

Friends, presence and parties for the remaster, on Cloudflare's free tier (PLAN.md "Multiplayer and online services").
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

## Run it locally

```
npm install
npm run dev                      # wrangler on ws://127.0.0.1:8787
npm run smoke                    # 20 end-to-end checks with three signed clients (pass wss://... to test the live hub)
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

Running the smoke test against the live hub leaves its test players in the database; they never sign in again.

Rename the Worker once the project has its name; nothing may say "Quake". Renaming changes the address, so update
`DEFAULT_SERVER` at the same time.
