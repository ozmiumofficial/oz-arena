// The social hub: one Durable Object ("global") with its own SQLite holding every player, friendship, friend
// request, party and party invite, plus live presence on the sockets themselves (WebSocket hibernation, so idle
// players cost nothing). The Worker has already checked the player's signature; this object trusts x-pid.
//
// Protocol (JSON, one object per message, "type" says what it is):
//   game -> hub  presence {status, detail, join?}   profile {name, model, color}   sync
//                career {xp, card, stats}   leaderboard {board: level|ranked, scope: global|friends}
//                net_host {name, mode, map, players, max, password, ranked, version, rtt}   net_unhost   net_browse {region?}
//                net_quick {mode, region?, version, rtt} (Quick Play: the best open match in a mode)
//                net_join {host, password?, version?}   net_accept {peer}   net_reject {peer, reason?}   net_ice
//                net_signal {to, kind: offer|answer|candidate, sdp | candidate, mid, index}   net_leave {peer}   net_kick {peer}
//                queue_join {modes, ranked, region, rtt}   queue_leave   match_joined {match}
//                match_report {match, me, rows: [{client, score, team}], left, peers}
//                friend_add {code}   friend_accept {id}   friend_decline {id}   friend_cancel {id}   friend_remove {id}
//                party_invite {id}   party_accept {party}   party_decline {party}   party_leave
//                party_kick {id}   party_promote {id}   party_chat {text}   party_follow {host, password?}
//                ping (answered without waking the object)
//   hub -> game  social {me, friends, incoming, outgoing, party, invites}   (the whole picture, sent on every change)
//                notice {text, tone}   party_chat {from, text, at}   pong
//                leaderboard {board, scope, rows, me, total}
//                net_matches {matches, region}   net_quick {status: found|far|none, mode, region, match?, est?, near, skipped}
//                net_wait {host}   net_request {peer, name, password, version}
//                net_accepted {host}   net_refused {host, reason}   net_ice {servers, relay_only, ttl | error}
//                net_signal {from, kind, ...}   net_gone {pid}   net_follow {host, leader, name, password} (the party leader's match)
//                queue_state {status: queued|idle|locked|refused, ...}   match_found {match, host, you_host, mode, map, ranked, team, players}
//                match_result {match, voided, reason?, queue, before, after, delta, rated, wins, losses, won}   ratings {season, queues}
import { DurableObject } from 'cloudflare:workers';
import type { Env } from './index';
import { refuse } from './util';
import {
  MODES, PLACEMENTS, RANKED_LEVEL, START_RATING, assignTeams, decide, easeToStart, electHost, eloDeltas, formGroup, seasonId, won,
  type Played, type QEntry, type Report, type Row as ScoreRow,
} from './ranked';

/**
 * The relay's spending guard. Cloudflare Realtime TURN has no hard cap (it bills past 1,000 GB a month), so the hub
 * keeps its own: every credential it mints is charged its worst-case traffic, and at the monthly budget it stops
 * minting. A player is limited by their `rate` (about 90 KB/s) for the credential's hour, both relay legs counted:
 * 0.7 GB. A host relays to up to 8 players: 5 GB. Real use is far lower, so the budget (800 GB by default, the
 * RELAY_BUDGET_GB variable) stays under the free allowance whatever happens.
 */
const RELAY_GB_PLAYER = 0.7;
const RELAY_GB_HOST = 5;
const RELAY_BUDGET_GB = 800;

/**
 * With TURN_ANALYTICS_TOKEN and TURN_ACCOUNT_ID the guard runs on what the relay really sent (MULTIPLAYER_PLAN.md M8:
 * worst cases were about 10x real use, and didn't bound abuse: a credential taken out of the game could push 50-100 Mbps
 * for its hour). Every RELAY_CHECK_MS while credentials live, the hub reads Cloudflare's analytics (callsTurnUsageAdaptiveGroups,
 * about 30 s behind): the month's egress for the key, and each credential's over the last minutes.
 *  - A credential sending faster than the game can (a player's 100 KB/s, a host's 500 KB/s; the game uses 5-15 and
 *    about 7 a player) is revoked.
 *  - New credentials stop at 90% of the budget; at 100% every live one is revoked: the hard stop.
 *  - What the analytics can't see yet (credentials minted since the last good check, or all of them while it fails)
 *    is counted at its worst case, so a broken token or API fails safe: relayed play pauses, it never runs unmetered.
 * Mints are limited too: 30 an hour a player, 300 every 10 minutes in all.
 */
const RELAY_STOP_FRACTION = 0.9;
const RELAY_CHECK_MS = 120_000;
const RELAY_RATE_PLAYER = 100_000;
const RELAY_RATE_HOST = 500_000;
const RELAY_RATE_WINDOW_MS = 6 * 60_000;
const RELAY_MINTS_PER_HOUR = 30;
const RELAY_MINTS_PER_10_MIN = 300;
const RELAY_TTL_USAGE_S = 12 * 3600;
const RELAY_TTL_WORST_S = 3600;

const MAX_PARTY = 8;
/** A player the host kicked (a passed vote) can't rejoin that host's matches for this long. */
const KICK_BAN_MS = 30 * 60_000;
const MAX_FRIENDS = 200;
const MAX_PENDING = 50;
const INVITE_TTL_MS = 5 * 60_000;
/** A party member offline this long drops out of the party. */
const PARTY_GRACE_MS = 10 * 60_000;
const CHAT_MAX = 200;
const CHAT_GAP_MS = 400;
/** Rate limit per socket: a bucket of this many messages, refilled this many per second. */
const BUCKET = 20;
const REFILL_PER_S = 5;
/** Leaderboards: rows sent per board, and how big a card or its showcase stats may be. */
const BOARD_ROWS = 50;
const CARD_MAX = 2048;
/** The level curve, as library/cards/cards.json "levels" has it: L -> L+1 takes XP_BASE + XP_STEP * (L - 1). */
const XP_BASE = 1000;
const XP_STEP = 25;
const MAX_LEVEL = 100;
/** Ranked rows show once placement is done (cards.json "ranked"). */
const RANKED_PLACEMENTS = 10;
/** The global boards are kept this long between reads (SQLite row reads are what the free tier counts). */
const BOARD_CACHE_MS = 60_000;

/** Matchmaking regions (PLAN.md "Regions"), as the Worker names them. */
const REGIONS = ['na_east', 'na_west', 'eu', 'sa', 'asia', 'oce', 'meaf'];

/** Regions close enough to play across (one relay hop of tens of ms, not a continent). Quick Play joins a match in
 *  the player's own region or a neighbor without asking; anything farther is offered, not joined. */
const NEIGHBORS: Record<string, string[]> = {
  na_east: ['na_west', 'sa', 'eu'], na_west: ['na_east'], eu: ['meaf', 'na_east'], sa: ['na_east'],
  asia: ['oce'], oce: ['asia'], meaf: ['eu'],
};

/** Quick Play's ping guess for a listing, in ms (nothing is measured through the relay before joining). The hub socket
 *  round trips of joiner and host stand in for each one's leg to its nearest relay, plus a fixed cost for the gap
 *  between regions: 0 in the same region, 60 for a neighbor, 180 for anything farther. */
function pingGuess(fromRegion: string, fromRtt: number, toRegion: string, toRtt: number): number {
  const gap = fromRegion === toRegion ? 0 : (NEIGHBORS[fromRegion] ?? []).includes(toRegion) ? 60 : 180;
  // the hub is in one place, so half of each round trip is the closest thing to a player's own last mile
  return Math.round(fromRtt / 2 + toRtt / 2 + gap);
}

function regionHeader(v: string | null): string {
  return REGIONS.includes(v ?? '') ? (v as string) : 'na_east';
}

type Status = 'online' | 'match' | 'editor';
const STATUSES: Status[] = ['online', 'match', 'editor'];

interface Tag {
  pid: string;
  status: Status;
  detail: string;
  join: string;
  since: number;
  /** the matchmaking region the Worker saw the player in */
  region: string;
  /** the match this player hosts, as JSON (net_host), or "" */
  listing: string;
  /** when the player last asked for relay credentials */
  iceAt: number;
  /** the matchmaking queue this player is in (a QEntry as JSON), or "" */
  queue: string;
  tokens: number;
  at: number;
  chat: number;
}

interface Player {
  id: string;
  code: string;
  name: string;
  model: string;
  color: string;
  seen: number;
}

/** A person as the game shows them: who, how they look, and whether they're on. */
interface Card {
  id: string;
  name: string;
  model: string;
  color: string;
  online: boolean;
  status: Status | 'offline';
  detail: string;
  join: string;
  seen: number;
}

type Msg = Record<string, unknown> & { type: string };

export class Hub extends DurableObject<Env> {
  private sql: SqlStorage;
  /** board -> the global top rows and total (lost when the object sleeps, which is fine) */
  private boards = new Map<string, { at: number; rows: BoardRow[]; total: number }>();

  constructor(ctx: DurableObjectState, env: Env) {
    super(ctx, env);
    this.sql = ctx.storage.sql;
    this.ctx.setWebSocketAutoResponse(new WebSocketRequestResponsePair('{"type":"ping"}', '{"type":"pong"}'));
    this.sql.exec(`
      CREATE TABLE IF NOT EXISTS players(id TEXT PRIMARY KEY, code TEXT UNIQUE NOT NULL, name TEXT NOT NULL,
        model TEXT NOT NULL, color TEXT NOT NULL, created INTEGER NOT NULL, seen INTEGER NOT NULL, last_ts INTEGER NOT NULL);
      CREATE TABLE IF NOT EXISTS friends(a TEXT NOT NULL, b TEXT NOT NULL, since INTEGER NOT NULL, PRIMARY KEY(a, b));
      CREATE TABLE IF NOT EXISTS requests(src TEXT NOT NULL, dst TEXT NOT NULL, at INTEGER NOT NULL, PRIMARY KEY(src, dst));
      CREATE INDEX IF NOT EXISTS requests_dst ON requests(dst);
      CREATE TABLE IF NOT EXISTS parties(id TEXT PRIMARY KEY, leader TEXT NOT NULL, created INTEGER NOT NULL);
      CREATE TABLE IF NOT EXISTS members(pid TEXT PRIMARY KEY, party TEXT NOT NULL, joined INTEGER NOT NULL);
      CREATE INDEX IF NOT EXISTS members_party ON members(party);
      CREATE TABLE IF NOT EXISTS invites(party TEXT NOT NULL, pid TEXT NOT NULL, src TEXT NOT NULL, at INTEGER NOT NULL,
        PRIMARY KEY(party, pid));
      CREATE INDEX IF NOT EXISTS invites_pid ON invites(pid);
      CREATE TABLE IF NOT EXISTS careers(pid TEXT PRIMARY KEY, xp INTEGER NOT NULL, level INTEGER NOT NULL, card TEXT NOT NULL,
        stats TEXT NOT NULL, rating INTEGER NOT NULL DEFAULT 0, rated INTEGER NOT NULL DEFAULT 0, peak INTEGER NOT NULL DEFAULT 0,
        updated INTEGER NOT NULL);
      CREATE INDEX IF NOT EXISTS careers_xp ON careers(xp DESC);
      CREATE INDEX IF NOT EXISTS careers_rating ON careers(rating DESC);
      CREATE TABLE IF NOT EXISTS relay_budget(month TEXT PRIMARY KEY, gb REAL NOT NULL DEFAULT 0, mints INTEGER NOT NULL DEFAULT 0);
      CREATE TABLE IF NOT EXISTS relay_creds(username TEXT PRIMARY KEY, pid TEXT NOT NULL, host INTEGER NOT NULL, minted INTEGER NOT NULL,
        expires INTEGER NOT NULL, worst REAL NOT NULL, revoked TEXT NOT NULL DEFAULT '');
      CREATE TABLE IF NOT EXISTS relay_usage(month TEXT PRIMARY KEY, bytes REAL NOT NULL DEFAULT 0, checked INTEGER NOT NULL DEFAULT 0,
        tried INTEGER NOT NULL DEFAULT 0, error TEXT NOT NULL DEFAULT '');
      CREATE TABLE IF NOT EXISTS links(host TEXT NOT NULL, peer TEXT NOT NULL, at INTEGER NOT NULL, accepted INTEGER NOT NULL DEFAULT 0,
        PRIMARY KEY(host, peer));
      CREATE INDEX IF NOT EXISTS links_peer ON links(peer);
      CREATE TABLE IF NOT EXISTS ratings(pid TEXT NOT NULL, queue TEXT NOT NULL, rating INTEGER NOT NULL, rated INTEGER NOT NULL DEFAULT 0,
        wins INTEGER NOT NULL DEFAULT 0, losses INTEGER NOT NULL DEFAULT 0, peak INTEGER NOT NULL, season TEXT NOT NULL, PRIMARY KEY(pid, queue));
      CREATE TABLE IF NOT EXISTS seasons(pid TEXT NOT NULL, queue TEXT NOT NULL, season TEXT NOT NULL, final INTEGER NOT NULL, peak INTEGER NOT NULL,
        wins INTEGER NOT NULL, losses INTEGER NOT NULL, PRIMARY KEY(pid, queue, season));
      CREATE TABLE IF NOT EXISTS rmatches(id TEXT PRIMARY KEY, queue TEXT NOT NULL, ranked INTEGER NOT NULL, host TEXT NOT NULL, map TEXT NOT NULL,
        created INTEGER NOT NULL, state TEXT NOT NULL, players TEXT NOT NULL, reports TEXT NOT NULL DEFAULT '{}', first_report INTEGER NOT NULL DEFAULT 0);
      CREATE TABLE IF NOT EXISTS lockouts(pid TEXT PRIMARY KEY, until INTEGER NOT NULL);
      CREATE TABLE IF NOT EXISTS host_stats(pid TEXT PRIMARY KEY, matches INTEGER NOT NULL DEFAULT 0, voids INTEGER NOT NULL DEFAULT 0);
      CREATE TABLE IF NOT EXISTS bans(host TEXT NOT NULL, pid TEXT NOT NULL, until INTEGER NOT NULL, PRIMARY KEY(host, pid));
    `);
  }

  // ------------------------------------------------------------------ connection

  async fetch(req: Request): Promise<Response> {
    // the operator's routes; the Worker has already checked the admin token
    const path = new URL(req.url).pathname;
    if (path === '/admin/players') return Response.json(this.adminPlayers());
    if (path === '/admin/relay') {
      const month = this.relayMonth();
      const row = this.one<{ gb: number; mints: number }>('SELECT gb, mints FROM relay_budget WHERE month = ?', month);
      const u = this.one<{ bytes: number; checked: number; tried: number; error: string }>('SELECT bytes, checked, tried, error FROM relay_usage WHERE month = ?', month);
      const now = Date.now();
      return Response.json({ month, worst_case_gb: row?.gb ?? 0, credentials: row?.mints ?? 0, budget_gb: this.relayBudget(),
        mode: this.relayUsageMode() ? 'usage' : 'worst-case', used_gb: (u?.bytes ?? 0) / 1e9, estimate_gb: this.relayEstimateGb(),
        checked: u?.checked ?? 0, tried: u?.tried ?? 0, error: u?.error ?? '',
        live: this.count("SELECT COUNT(*) AS n FROM relay_creds WHERE expires > ? AND revoked = ''", now),
        revoked: this.rows<{ username: string; pid: string; revoked: string }>("SELECT username, pid, revoked FROM relay_creds WHERE revoked != '' AND minted > ?", now - 86_400_000) });
    }
    if (path === '/admin/purge' && req.method === 'POST') {
      const body = (await req.json().catch(() => ({}))) as { ids?: unknown };
      const ids = Array.isArray(body.ids) ? body.ids.filter((x): x is string => typeof x === 'string' && /^[0-9a-f]{20}$/.test(x)) : [];
      return Response.json({ removed: this.purge(ids) });
    }
    const h = req.headers;
    const pid = h.get('x-pid') ?? '';
    const ts = Number(h.get('x-ts'));
    if (!/^[0-9a-f]{20}$/.test(pid)) return refuse(4001, 'Missing identity.');
    const now = Date.now();
    const row = this.player(pid);
    if (row && ts <= this.one<{ last_ts: number }>('SELECT last_ts FROM players WHERE id = ?', pid)!.last_ts)
      return refuse(4001, 'That sign-in was already used.');
    const name = cleanName(decodeName(h.get('x-name')));
    const model = cleanModel(h.get('x-model'));
    const color = cleanColor(h.get('x-color'));
    if (row) {
      this.sql.exec('UPDATE players SET name = ?, model = ?, color = ?, seen = ?, last_ts = ? WHERE id = ?', name, model, color, now, ts, pid);
    } else {
      const code = await this.newCode(pid);
      this.sql.exec('INSERT INTO players(id, code, name, model, color, created, seen, last_ts) VALUES (?, ?, ?, ?, ?, ?, ?, ?)',
        pid, code, name, model, color, now, now, ts);
    }
    const [client, server] = Object.values(new WebSocketPair());
    this.ctx.acceptWebSocket(server, [pid]);
    const tag: Tag = { pid, status: 'online', detail: '', join: '', since: now, region: regionHeader(h.get('x-region')), listing: '', iceAt: 0, queue: '',
      tokens: BUCKET, at: now, chat: 0 };
    server.serializeAttachment(tag);
    this.prune(this.partyOf(pid));
    this.send(server, this.snapshot(pid));
    this.send(server, this.ratingsMessage(pid));
    // friends and the party see them come online (or their new name and look)
    this.push(this.related(pid).filter((p) => p !== pid));
    return new Response(null, { status: 101, webSocket: client });
  }

  async webSocketMessage(ws: WebSocket, data: string | ArrayBuffer) {
    const tag = ws.deserializeAttachment() as Tag | null;
    if (!tag) return;
    // token bucket: a burst of BUCKET, then REFILL_PER_S a second
    const now = Date.now();
    tag.tokens = Math.min(BUCKET, tag.tokens + ((now - tag.at) / 1000) * REFILL_PER_S);
    tag.at = now;
    if (tag.tokens < 1) {
      ws.serializeAttachment(tag);
      return this.notice(ws, 'Slow down.', 'warn');
    }
    tag.tokens -= 1;
    ws.serializeAttachment(tag);
    let m: Msg;
    try {
      m = JSON.parse(typeof data === 'string' ? data : new TextDecoder().decode(data));
    } catch {
      return;
    }
    if (!m || typeof m.type !== 'string') return;
    const me = tag.pid;
    const id = typeof m.id === 'string' ? m.id : '';
    const party = typeof m.party === 'string' ? m.party : '';
    switch (m.type) {
      case 'sync':
        return this.send(ws, this.snapshot(me));
      case 'presence':
        return this.presence(ws, tag, m);
      case 'profile':
        return this.profile(me, m);
      case 'career':
        return this.career(me, m);
      case 'leaderboard':
        return this.leaderboard(ws, me, m);
      case 'friend_add':
        return this.friendAdd(ws, me, String(m.code ?? ''));
      case 'friend_accept':
        return this.friendAccept(ws, me, id);
      case 'friend_decline':
        this.sql.exec('DELETE FROM requests WHERE src = ? AND dst = ?', id, me);
        return this.push([me, id]);
      case 'friend_cancel':
        this.sql.exec('DELETE FROM requests WHERE src = ? AND dst = ?', me, id);
        return this.push([me, id]);
      case 'friend_remove':
        this.sql.exec('DELETE FROM friends WHERE (a = ? AND b = ?) OR (a = ? AND b = ?)', me, id, id, me);
        return this.push([me, id]);
      case 'party_invite':
        return this.partyInvite(ws, me, id);
      case 'party_accept':
        return this.partyAccept(ws, me, party);
      case 'party_decline':
        return this.partyDecline(me, party);
      case 'party_leave':
        return this.partyLeave(me);
      case 'party_kick':
        return this.partyKick(ws, me, id);
      case 'party_promote':
        return this.partyPromote(ws, me, id);
      case 'party_chat':
        return this.partyChat(ws, tag, String(m.text ?? ''));
      case 'party_follow':
        return this.partyFollow(me, m);
      case 'net_host':
        return this.netHost(ws, tag, m);
      case 'net_unhost':
        tag.listing = '';
        return ws.serializeAttachment(tag);
      case 'net_browse':
        return this.netBrowse(ws, tag, m);
      case 'net_join':
        return this.netJoin(ws, tag, m);
      case 'net_quick':
        return this.netQuick(ws, tag, m);
      case 'net_accept':
        return this.netAccept(ws, me, id || String(m.peer ?? ''), m);
      case 'net_reject':
        return this.netReject(me, String(m.peer ?? ''), clean(m.reason, 60) || 'The host refused you.');
      case 'net_ice':
        return this.netIce(ws, tag);
      case 'net_signal':
        return this.netSignal(ws, me, m);
      case 'net_leave':
        return this.netLeave(me, String(m.peer ?? ''));
      case 'net_kick':
        return this.netKick(me, String(m.peer ?? ''));
      case 'queue_join':
        return this.queueJoin(ws, tag, m);
      case 'queue_leave':
        return this.queueLeave(ws, tag);
      case 'match_joined':
        return this.matchJoined(me, String(m.match ?? ''));
      case 'match_report':
        return this.matchReport(me, m);
    }
  }

  async webSocketClose(ws: WebSocket, code: number, reason: string) {
    try {
      ws.close(code, reason);
    } catch {
      /* already closed */
    }
    this.gone(ws);
  }

  async webSocketError(ws: WebSocket) {
    this.gone(ws);
  }

  /** A socket went away. When it was the player's last one, they go offline for everyone. */
  private gone(ws: WebSocket) {
    const tag = ws.deserializeAttachment() as Tag | null;
    if (!tag || this.sockets(tag.pid, ws).length) return;
    const pid = tag.pid;
    this.netDropLinks(pid);
    this.queueDrop(pid);
    this.sql.exec('UPDATE players SET seen = ? WHERE id = ?', Date.now(), pid);
    // party invites only stand while the invitee is on
    this.sql.exec('DELETE FROM invites WHERE pid = ?', pid);
    const party = this.partyOf(pid);
    if (party) {
      const online = this.memberIds(party).filter((p) => p !== pid && this.sockets(p, ws).length);
      if (!online.length) {
        this.dissolve(party);
      } else if (this.leaderOf(party) === pid) {
        // the party needs a leader who's here: the longest-standing member online
        this.sql.exec('UPDATE parties SET leader = ? WHERE id = ?', online[0], party);
      }
    }
    this.push(this.related(pid).filter((p) => p !== pid), ws);
  }

  // ------------------------------------------------------------------ presence and profile

  private presence(ws: WebSocket, tag: Tag, m: Msg) {
    const status = STATUSES.includes(m.status as Status) ? (m.status as Status) : 'online';
    const detail = clean(m.detail, 80);
    const join = clean(m.join, 120);
    if (status !== tag.status) tag.since = Date.now();
    Object.assign(tag, { status, detail, join });
    ws.serializeAttachment(tag);
    // the player too: their own row in the party shows it
    this.push(this.related(tag.pid));
  }

  private profile(me: string, m: Msg) {
    this.sql.exec('UPDATE players SET name = ?, model = ?, color = ?, seen = ? WHERE id = ?',
      cleanName(m.name as string), cleanModel(m.model as string), cleanColor(m.color as string), Date.now(), me);
    this.push(this.related(me));
  }

  // ------------------------------------------------------------------ careers and leaderboards

  /**
   * The player's level board entry: total XP (the level comes from it here), the card as their own game shows it, and
   * its showcase stat values. All self-reported, like every offline career (PLAN.md "Trust"); viewers' games put
   * unknown parts back to the default. Ranked columns are never written from here: only the hub sets them, from
   * peer-to-peer ranked matches whose players' signed scoreboards agree (MULTIPLAYER_PLAN.md M6).
   */
  private career(me: string, m: Msg) {
    const xp = Math.max(0, Math.min(1e9, Math.floor(Number(m.xp) || 0)));
    const card = JSON.stringify(cleanCard(m.card));
    const stats = JSON.stringify(cleanStats(m.stats));
    if (card.length > CARD_MAX || stats.length > CARD_MAX) return;
    this.sql.exec(
      `INSERT INTO careers(pid, xp, level, card, stats, updated) VALUES (?, ?, ?, ?, ?, ?)
       ON CONFLICT(pid) DO UPDATE SET xp = excluded.xp, level = excluded.level, card = excluded.card, stats = excluded.stats,
       updated = excluded.updated`,
      me, xp, levelFor(xp), card, stats, Date.now());
  }

  /** The top of a board, everyone or just you and your friends, plus your own place on it. */
  private leaderboard(ws: WebSocket, me: string, m: Msg) {
    const board = m.board === 'ranked' ? 'ranked' : 'level';
    const scope = m.scope === 'friends' ? 'friends' : 'global';
    const order = board === 'ranked' ? 'c.rating DESC, c.updated' : 'c.xp DESC, c.updated';
    const where = [board === 'ranked' ? `c.rated >= ${RANKED_PLACEMENTS}` : '1 = 1'];
    const args: SqlStorageValue[] = [];
    if (scope === 'friends') {
      where.push('(c.pid = ? OR c.pid IN (SELECT b FROM friends WHERE a = ?))');
      args.push(me, me);
    }
    const from = `FROM careers c JOIN players p ON p.id = c.pid WHERE ${where.join(' AND ')}`;
    const cols = 'c.pid, p.name, p.model, p.color, c.xp, c.level, c.card, c.stats, c.rating, c.rated, c.peak';
    const out = (r: Row, rank: number): BoardRow => ({
      rank, id: r.pid, name: r.name, model: r.model, color: r.color, xp: r.xp, level: r.level, card: JSON.parse(r.card),
      stats: JSON.parse(r.stats), rating: r.rating, rated: r.rated, peak: r.peak, online: this.sockets(r.pid).length > 0,
    });
    const cached = scope === 'global' ? this.boards.get(board) : undefined;
    let rows: BoardRow[];
    let total: number;
    if (cached && Date.now() - cached.at < BOARD_CACHE_MS) {
      // online dots are live even when the rows aren't
      rows = cached.rows.map((r) => ({ ...r, online: this.sockets(r.id).length > 0 }));
      total = cached.total;
    } else {
      rows = this.rows<Row>(`SELECT ${cols} ${from} ORDER BY ${order} LIMIT ${BOARD_ROWS}`, ...args).map((r, i) => out(r, i + 1));
      total = this.count(`SELECT COUNT(*) AS n ${from}`, ...args);
      if (scope === 'global') this.boards.set(board, { at: Date.now(), rows, total });
    }
    let mine = rows.find((r) => r.id === me) ?? null;
    if (!mine) {
      const r = this.one<Row>(`SELECT ${cols} ${from} AND c.pid = ?`, ...args, me);
      if (r) {
        const ahead = board === 'ranked'
          ? this.count(`SELECT COUNT(*) AS n ${from} AND c.rating > ?`, ...args, r.rating)
          : this.count(`SELECT COUNT(*) AS n ${from} AND c.xp > ?`, ...args, r.xp);
        mine = out(r, ahead + 1);
      }
    }
    this.send(ws, { type: 'leaderboard', board, scope, rows, me: mine, total });
  }

  // ------------------------------------------------------------------ admin

  /** Every player with what tells real from test: when they joined and were last seen, XP, friends. */
  private adminPlayers() {
    return this.rows<Player & { created: number; xp: number | null; friends: number }>(
      `SELECT p.id, p.code, p.name, p.model, p.color, p.created, p.seen, c.xp AS xp,
       (SELECT COUNT(*) FROM friends f WHERE f.a = p.id) AS friends
       FROM players p LEFT JOIN careers c ON c.pid = p.id ORDER BY p.created`)
      .map((p) => ({ ...p, online: this.sockets(p.id).length > 0 }));
  }

  /** Removes players and everything that points at them; their friends and parties see them go. */
  private purge(ids: string[]): number {
    let n = 0;
    for (const pid of ids) {
      if (!this.player(pid)) continue;
      const touched = this.related(pid).filter((p) => p !== pid);
      for (const s of this.sockets(pid)) s.close(4003, 'This player was removed.');
      const party = this.partyOf(pid);
      if (party) this.removeMember(party, pid);
      this.sql.exec('DELETE FROM friends WHERE a = ? OR b = ?', pid, pid);
      this.sql.exec('DELETE FROM requests WHERE src = ? OR dst = ?', pid, pid);
      this.sql.exec('DELETE FROM invites WHERE pid = ? OR src = ?', pid, pid);
      this.sql.exec('DELETE FROM careers WHERE pid = ?', pid);
      this.sql.exec('DELETE FROM players WHERE id = ?', pid);
      this.push(touched);
      n++;
    }
    this.boards.clear();
    return n;
  }

  // ------------------------------------------------------------------ friends

  private friendAdd(ws: WebSocket, me: string, raw: string) {
    const code = normalizeCode(raw);
    if (!code) return this.notice(ws, 'Friend codes look like ABCD-1234.', 'error');
    const them = this.one<Player>('SELECT * FROM players WHERE code = ?', code);
    if (!them) return this.notice(ws, `No player has the code ${code}.`, 'error');
    if (them.id === me) return this.notice(ws, "That's your own code.", 'error');
    if (this.areFriends(me, them.id)) return this.notice(ws, `${them.name} is already your friend.`, 'warn');
    // they asked first: adding them back accepts
    if (this.one('SELECT 1 AS x FROM requests WHERE src = ? AND dst = ?', them.id, me)) return this.friendAccept(ws, me, them.id);
    if (this.one('SELECT 1 AS x FROM requests WHERE src = ? AND dst = ?', me, them.id))
      return this.notice(ws, `You already asked ${them.name}.`, 'warn');
    if (this.count('SELECT COUNT(*) AS n FROM requests WHERE src = ?', me) >= MAX_PENDING)
      return this.notice(ws, 'Too many requests waiting. Cancel some first.', 'error');
    if (this.count('SELECT COUNT(*) AS n FROM friends WHERE a = ?', me) >= MAX_FRIENDS)
      return this.notice(ws, `Your friends list is full (${MAX_FRIENDS}).`, 'error');
    this.sql.exec('INSERT INTO requests(src, dst, at) VALUES (?, ?, ?)', me, them.id, Date.now());
    this.notice(ws, `Friend request sent to ${them.name}.`);
    this.noticeTo(them.id, `${this.player(me)!.name} wants to be your friend.`);
    this.push([me, them.id]);
  }

  private friendAccept(ws: WebSocket, me: string, them: string) {
    if (!this.one('SELECT 1 AS x FROM requests WHERE src = ? AND dst = ?', them, me)) return this.send(ws, this.snapshot(me));
    for (const p of [me, them])
      if (this.count('SELECT COUNT(*) AS n FROM friends WHERE a = ?', p) >= MAX_FRIENDS)
        return this.notice(ws, p === me ? `Your friends list is full (${MAX_FRIENDS}).` : 'Their friends list is full.', 'error');
    const now = Date.now();
    this.sql.exec('DELETE FROM requests WHERE (src = ? AND dst = ?) OR (src = ? AND dst = ?)', me, them, them, me);
    this.sql.exec('INSERT OR IGNORE INTO friends(a, b, since) VALUES (?, ?, ?), (?, ?, ?)', me, them, now, them, me, now);
    this.noticeTo(them, `${this.player(me)!.name} accepted your friend request.`);
    this.push([me, them]);
  }

  private areFriends(a: string, b: string) {
    return !!this.one('SELECT 1 AS x FROM friends WHERE a = ? AND b = ?', a, b);
  }

  // ------------------------------------------------------------------ parties

  private partyInvite(ws: WebSocket, me: string, them: string) {
    if (!this.areFriends(me, them)) return this.notice(ws, 'You can only invite friends.', 'error');
    if (!this.sockets(them).length) return this.notice(ws, "They're offline.", 'warn');
    let party = this.partyOf(me);
    if (party && this.partyOf(them) === party) return this.notice(ws, "They're already in your party.", 'warn');
    if (party && this.memberIds(party).length >= MAX_PARTY) return this.notice(ws, `Parties hold ${MAX_PARTY} players.`, 'error');
    const now = Date.now();
    if (!party) {
      // inviting someone starts a party with you as its leader
      party = crypto.randomUUID().slice(0, 12);
      this.sql.exec('INSERT INTO parties(id, leader, created) VALUES (?, ?, ?)', party, me, now);
      this.sql.exec('INSERT INTO members(pid, party, joined) VALUES (?, ?, ?)', me, party, now);
    }
    this.sql.exec('INSERT OR REPLACE INTO invites(party, pid, src, at) VALUES (?, ?, ?, ?)', party, them, me, now);
    this.notice(ws, `Party invite sent to ${this.player(them)!.name}.`);
    this.noticeTo(them, `${this.player(me)!.name} invited you to a party.`);
    this.push([me, them]);
  }

  private partyAccept(ws: WebSocket, me: string, party: string) {
    const inv = this.one<{ src: string; at: number }>('SELECT src, at FROM invites WHERE party = ? AND pid = ?', party, me);
    if (!inv || Date.now() - inv.at > INVITE_TTL_MS || !this.one('SELECT 1 AS x FROM parties WHERE id = ?', party)) {
      this.sql.exec('DELETE FROM invites WHERE party = ? AND pid = ?', party, me);
      this.notice(ws, 'That invite has expired.', 'warn');
      return this.push([me]);
    }
    if (this.memberIds(party).length >= MAX_PARTY) return this.notice(ws, 'That party is full.', 'error');
    const old = this.partyOf(me);
    if (old && old !== party) this.removeMember(old, me);
    this.sql.exec('DELETE FROM invites WHERE party = ? AND pid = ?', party, me);
    this.sql.exec('INSERT OR REPLACE INTO members(pid, party, joined) VALUES (?, ?, ?)', me, party, Date.now());
    const name = this.player(me)!.name;
    for (const p of this.memberIds(party)) if (p !== me) this.noticeTo(p, `${name} joined the party.`);
    this.push([me, ...this.memberIds(party), ...this.related(me)]);
  }

  private partyDecline(me: string, party: string) {
    const inv = this.one<{ src: string }>('SELECT src FROM invites WHERE party = ? AND pid = ?', party, me);
    this.sql.exec('DELETE FROM invites WHERE party = ? AND pid = ?', party, me);
    if (inv) this.noticeTo(inv.src, `${this.player(me)!.name} declined the party invite.`);
    const members = this.memberIds(party);
    this.prune(party);
    this.push([me, ...members]);
  }

  private partyLeave(me: string) {
    const party = this.partyOf(me);
    if (!party) return this.push([me]);
    const others = this.memberIds(party).filter((p) => p !== me);
    this.removeMember(party, me);
    for (const p of others) this.noticeTo(p, `${this.player(me)!.name} left the party.`);
    this.push([me, ...others, ...this.related(me)]);
  }

  private partyKick(ws: WebSocket, me: string, them: string) {
    const party = this.partyOf(me);
    if (!party || this.leaderOf(party) !== me) return this.notice(ws, 'Only the party leader can do that.', 'error');
    if (them === me || this.partyOf(them) !== party) return;
    const others = this.memberIds(party);
    this.removeMember(party, them);
    this.noticeTo(them, 'You were removed from the party.', 'warn');
    this.push([...others, ...this.related(them)]);
  }

  private partyPromote(ws: WebSocket, me: string, them: string) {
    const party = this.partyOf(me);
    if (!party || this.leaderOf(party) !== me) return this.notice(ws, 'Only the party leader can do that.', 'error');
    if (this.partyOf(them) !== party) return;
    this.sql.exec('UPDATE parties SET leader = ? WHERE id = ?', them, party);
    this.noticeTo(them, 'You lead the party now.');
    this.push(this.memberIds(party));
  }

  private partyChat(ws: WebSocket, tag: Tag, raw: string) {
    const party = this.partyOf(tag.pid);
    if (!party) return;
    const text = raw.replace(/[\u0000-\u001f\u007f]/g, ' ').replace(/\s+/g, ' ').trim().slice(0, CHAT_MAX);
    if (!text) return;
    const now = Date.now();
    if (now - tag.chat < CHAT_GAP_MS) return this.notice(ws, 'Slow down.', 'warn');
    tag.chat = now;
    ws.serializeAttachment(tag);
    const me = this.player(tag.pid)!;
    const out = JSON.stringify({ type: 'party_chat', from: { id: me.id, name: me.name }, text, at: now });
    for (const p of this.memberIds(party)) for (const s of this.sockets(p)) trySend(s, out);
  }

  /** Takes a member out; a party left with one player ends, and a leaderless one passes to the next member. */
  private removeMember(party: string, pid: string) {
    this.sql.exec('DELETE FROM members WHERE pid = ?', pid);
    const rest = this.memberIds(party);
    if (rest.length <= 1) return this.dissolve(party);
    if (this.leaderOf(party) === pid) {
      const next = rest.find((p) => this.sockets(p).length) ?? rest[0];
      this.sql.exec('UPDATE parties SET leader = ? WHERE id = ?', next, party);
    }
  }

  private dissolve(party: string) {
    this.sql.exec('DELETE FROM members WHERE party = ?', party);
    this.sql.exec('DELETE FROM invites WHERE party = ?', party);
    this.sql.exec('DELETE FROM parties WHERE id = ?', party);
  }

  /** Drops members who've been offline past the grace period and stale invites; ends a party left with one player and no invites out. */
  private prune(party: string | null) {
    if (!party) return;
    const cutoff = Date.now() - PARTY_GRACE_MS;
    for (const p of this.memberIds(party)) {
      if (this.sockets(p).length) continue;
      if ((this.player(p)?.seen ?? 0) < cutoff) this.removeMember(party, p);
    }
    this.sql.exec('DELETE FROM invites WHERE party = ? AND at < ?', party, Date.now() - INVITE_TTL_MS);
    // a party nobody joined (the invites ran out or were declined) ends
    if (this.memberIds(party).length <= 1 && !this.count('SELECT COUNT(*) AS n FROM invites WHERE party = ?', party)) this.dissolve(party);
  }

  /** The leader hosted or joined a match: the rest of the party follow them into it (net_follow). The password
   *  passes through to the members only; it is never stored. */
  private partyFollow(me: string, m: Msg) {
    const party = this.partyOf(me);
    if (!party || this.leaderOf(party) !== me) return;
    const host = String(m.host ?? '');
    if (!host || !this.listings().some((l) => l.host === host)) return;
    const msg = { type: 'net_follow', host, leader: me, name: this.player(me)?.name ?? 'Player', password: clean(m.password, 40) };
    for (const p of this.memberIds(party)) if (p !== me && p !== host) this.sendTo(p, msg);
  }

  private partyOf(pid: string): string | null {
    return this.one<{ party: string }>('SELECT party FROM members WHERE pid = ?', pid)?.party ?? null;
  }

  private leaderOf(party: string): string | null {
    return this.one<{ leader: string }>('SELECT leader FROM parties WHERE id = ?', party)?.leader ?? null;
  }

  /** Members in the order they joined. */
  private memberIds(party: string): string[] {
    return this.rows<{ pid: string }>('SELECT pid FROM members WHERE party = ? ORDER BY joined', party).map((r) => r.pid);
  }

  // ------------------------------------------------------------------ what each player sees

  /** Everyone whose view of `pid` would change: their friends, their party, and themself. */
  private related(pid: string): string[] {
    const out = new Set([pid]);
    for (const r of this.rows<{ b: string }>('SELECT b FROM friends WHERE a = ?', pid)) out.add(r.b);
    const party = this.partyOf(pid);
    if (party) for (const p of this.memberIds(party)) out.add(p);
    return [...out];
  }

  private card(p: Player, skip?: WebSocket): Card {
    const socks = this.sockets(p.id, skip);
    // with more than one game open, a match beats the editor beats the menu
    const tags = socks.map((s) => s.deserializeAttachment() as Tag);
    const t = tags.find((x) => x.status === 'match') ?? tags.find((x) => x.status === 'editor') ?? tags[0];
    return {
      id: p.id, name: p.name, model: p.model, color: p.color, online: !!t,
      status: t ? t.status : 'offline', detail: t?.detail ?? '', join: t?.join ?? '', seen: t ? t.since : p.seen,
    };
  }

  private snapshot(pid: string, skip?: WebSocket) {
    const me = this.player(pid)!;
    const now = Date.now();
    const friends = this.rows<Player & { since: number }>(
      'SELECT p.*, f.since AS since FROM friends f JOIN players p ON p.id = f.b WHERE f.a = ?', pid)
      .map((p) => ({ ...this.card(p, skip), since: p.since }));
    const incoming = this.rows<Player & { at: number }>(
      'SELECT p.*, r.at AS at FROM requests r JOIN players p ON p.id = r.src WHERE r.dst = ? ORDER BY r.at DESC', pid)
      .map((p) => ({ id: p.id, name: p.name, model: p.model, color: p.color, at: p.at }));
    const outgoing = this.rows<Player & { at: number }>(
      'SELECT p.*, r.at AS at FROM requests r JOIN players p ON p.id = r.dst WHERE r.src = ? ORDER BY r.at DESC', pid)
      .map((p) => ({ id: p.id, name: p.name, model: p.model, color: p.color, at: p.at }));
    const pid_party = this.partyOf(pid);
    let party = null;
    if (pid_party) {
      const leader = this.leaderOf(pid_party);
      const members = this.rows<Player>('SELECT p.* FROM members m JOIN players p ON p.id = m.pid WHERE m.party = ? ORDER BY m.joined', pid_party)
        .map((p) => ({ ...this.card(p, skip), leader: p.id === leader }));
      const pending = this.rows<Player>('SELECT p.* FROM invites i JOIN players p ON p.id = i.pid WHERE i.party = ? AND i.at > ?', pid_party, now - INVITE_TTL_MS)
        .map((p) => ({ id: p.id, name: p.name, model: p.model, color: p.color }));
      party = { id: pid_party, leader, max: MAX_PARTY, members, pending };
    }
    const invites = this.rows<{ party: string; at: number } & Player>(
      'SELECT i.party AS party, i.at AS at, p.* FROM invites i JOIN players p ON p.id = i.src WHERE i.pid = ? AND i.at > ? ORDER BY i.at DESC',
      pid, now - INVITE_TTL_MS)
      .map((r) => ({ party: r.party, at: r.at, expires: r.at + INVITE_TTL_MS, size: this.memberIds(r.party).length,
        from: { id: r.id, name: r.name, model: r.model, color: r.color } }));
    return {
      type: 'social',
      me: { id: me.id, code: me.code, name: me.name, model: me.model, color: me.color },
      friends, incoming, outgoing, party, invites, limits: { friends: MAX_FRIENDS, party: MAX_PARTY },
    };
  }

  /** Sends each online player in `pids` their whole picture again. */
  private push(pids: Iterable<string>, skip?: WebSocket) {
    for (const pid of new Set(pids)) {
      const socks = this.sockets(pid, skip);
      if (!socks.length || !this.player(pid)) continue;
      const data = JSON.stringify(this.snapshot(pid, skip));
      for (const s of socks) trySend(s, data);
    }
  }

  private notice(ws: WebSocket, text: string, tone: 'info' | 'warn' | 'error' = 'info') {
    this.send(ws, { type: 'notice', text, tone });
  }

  private noticeTo(pid: string, text: string, tone: 'info' | 'warn' | 'error' = 'info') {
    const data = JSON.stringify({ type: 'notice', text, tone });
    for (const s of this.sockets(pid)) trySend(s, data);
  }

  private send(ws: WebSocket, msg: object) {
    trySend(ws, JSON.stringify(msg));
  }

  // ------------------------------------------------------------------ matches: directory, joining, signaling
  // MULTIPLAYER_PLAN.md M4. A host lists its match (net_host) on its own socket, so the listing vanishes with the
  // host. A joiner asks (net_join); the host answers (net_accept / net_reject); then both sides trade WebRTC
  // signaling (net_signal) through the hub, which only forwards between a host and a peer it linked and drops
  // anything that isn't a relay candidate, so no player's address ever reaches another. Credentials for the
  // Cloudflare TURN relay are minted per request (net_ice) from the TURN key, a Worker secret.

  private netHost(ws: WebSocket, tag: Tag, m: Msg) {
    const num = (v: unknown, lo: number, hi: number) => Math.max(lo, Math.min(hi, Math.floor(Number(v) || 0)));
    const id = (v: unknown) => (/^[a-z0-9_]{1,32}$/.test(String(v ?? '')) ? String(v) : '');
    const listing = {
      name: clean(m.name, 40) || 'Match', mode: id(m.mode), map: id(m.map), players: num(m.players, 0, 64), max: num(m.max, 1, 64),
      password: m.password === true, ranked: m.ranked === true, version: clean(m.version, 24), dedicated: m.dedicated === true,
      // the host's hub round trip (Quick Play's ping guess); 0 from games that don't send it
      rtt: num(m.rtt, 0, 2000),
      // a matchmade match: invite only (the matchmaker's players), never in the browser
      match: this.ownMatch(tag.pid, String(m.match ?? '')),
    };
    tag.listing = JSON.stringify(listing);
    ws.serializeAttachment(tag);
  }

  /** Every open listing: the host's id and name, the match, and the host's region. */
  private listings(): { host: string; owner: string; region: string; [k: string]: unknown }[] {
    const out: { host: string; owner: string; region: string; [k: string]: unknown }[] = [];
    for (const s of this.ctx.getWebSockets()) {
      if (s.readyState !== 1) continue;
      const t = s.deserializeAttachment() as Tag | null;
      if (!t || !t.listing) continue;
      const l = JSON.parse(t.listing);
      out.push({ host: t.pid, owner: this.player(t.pid)?.name ?? 'Player', region: t.region, ...l });
    }
    return out;
  }

  private netBrowse(ws: WebSocket, tag: Tag, m: Msg) {
    const region = REGIONS.includes(String(m.region)) ? String(m.region) : tag.region;
    // your own match lists too (the menu marks it), so a host can see it went up
    const all = this.listings().filter((l) => !l.match);
    // the asker's region first, fuller matches before emptier ones
    all.sort((a, b) => Number(b.region === region) - Number(a.region === region) || Number(b.players) - Number(a.players));
    this.send(ws, { type: 'net_matches', matches: all.slice(0, 100), region });
  }

  /**
   * Quick Play: the open match in `mode` most worth joining. Only matches a join would succeed in count: listed in the
   * browser (not matchmade), not your own, not full, no password, your version, and no ban from that host. Then, best
   * first: the lowest ping guess in 40 ms steps (pingGuess), the fullest, then the fewest free slots.
   * `found` is a match close enough to join at once; `far` is the best there is when that's another continent (the menu
   * asks); `none` says how many matches in the mode were skipped (full, locked, another version), so the menu can say why.
   */
  private netQuick(ws: WebSocket, tag: Tag, m: Msg) {
    const mode = /^[a-z0-9_]{1,32}$/.test(String(m.mode ?? '')) ? String(m.mode) : '';
    const region = REGIONS.includes(String(m.region)) ? String(m.region) : tag.region;
    const rtt = Math.max(0, Math.min(2000, Math.floor(Number(m.rtt) || 100)));
    const version = clean(m.version, 24);
    const now = Date.now();
    this.sql.exec('DELETE FROM bans WHERE until < ?', now);
    const banned = new Set(this.rows<{ host: string }>('SELECT host FROM bans WHERE pid = ?', tag.pid).map((r) => r.host));
    const inMode = this.listings().filter((l) => !l.match && l.host !== tag.pid && (!mode || l.mode === mode));
    const open = inMode.filter((l) => Number(l.players) < Number(l.max) && !l.password && !banned.has(l.host)
      && (!version || !l.version || l.version === version));
    const scored = open.map((l) => ({ l, est: pingGuess(region, rtt, l.region, Number(l.rtt) || 100) }));
    scored.sort((a, b) => Math.floor(a.est / 40) - Math.floor(b.est / 40) || Number(b.l.players) - Number(a.l.players)
      || Number(a.l.max) - Number(a.l.players) - (Number(b.l.max) - Number(b.l.players)) || a.est - b.est);
    const best = scored[0];
    const near = best && (best.l.region === region || (NEIGHBORS[region] ?? []).includes(best.l.region));
    this.send(ws, {
      type: 'net_quick', status: best ? (near ? 'found' : 'far') : 'none', mode, region,
      ...(best ? { match: best.l, est: best.est } : {}),
      near: scored.filter((x) => x.l.region === region).length, skipped: inMode.length - open.length,
    });
  }

  private netJoin(ws: WebSocket, tag: Tag, m: Msg) {
    const host = String(m.host ?? '');
    const refuse = (reason: string) => this.send(ws, { type: 'net_refused', host, reason });
    if (host === tag.pid) return refuse('That is your own match.');
    if (m.renew === true) {
      // the joiner's connection is about to outlive a relay credential: a second connection beside it, same match, same slot
      const link = this.one<{ accepted: number }>('SELECT accepted FROM links WHERE host = ? AND peer = ?', host, tag.pid);
      if (!link?.accepted) return refuse('That connection is gone.');
      this.sendTo(host, { type: 'net_request', peer: tag.pid, name: this.player(tag.pid)?.name ?? 'Player', renew: true, gen: gen(m.gen) });
      return this.send(ws, { type: 'net_wait', host });
    }
    const listing = this.listings().find((l) => l.host === host);
    if (!listing) return refuse('That match is gone.');
    if (listing.match && !this.matchPlayers(String(listing.match)).some((p) => p.id === tag.pid)) return refuse('This match is invite only.');
    if (Number(listing.players) >= Number(listing.max)) return refuse('The match is full.');
    if (listing.version && clean(m.version, 24) && listing.version !== clean(m.version, 24))
      return refuse(`The match is on version ${listing.version}; you have ${clean(m.version, 24)}.`);
    const now = Date.now();
    this.sql.exec('DELETE FROM bans WHERE until < ?', now);
    const ban = this.one<{ until: number }>('SELECT until FROM bans WHERE host = ? AND pid = ?', host, tag.pid);
    if (ban) return refuse(`You were kicked from this host's matches. Try again in ${Math.ceil((ban.until - now) / 60_000)} minutes.`);
    this.sql.exec('DELETE FROM links WHERE accepted = 0 AND at < ?', now - 60_000);
    if (this.count('SELECT COUNT(*) AS n FROM links WHERE host = ?', host) >= 64) return refuse('The match is full.');
    this.sql.exec('INSERT OR REPLACE INTO links(host, peer, at, accepted) VALUES (?, ?, ?, 0)', host, tag.pid, now);
    const me = this.player(tag.pid);
    this.sendTo(host, { type: 'net_request', peer: tag.pid, name: me?.name ?? 'Player', password: clean(m.password, 40), version: clean(m.version, 24) });
    this.send(ws, { type: 'net_wait', host });
  }

  private netAccept(ws: WebSocket, me: string, peer: string, m: Msg = { type: 'net_accept' }) {
    const link = this.one<{ at: number }>('SELECT at FROM links WHERE host = ? AND peer = ?', me, peer);
    if (!link) return this.notice(ws, 'That player is no longer joining.', 'warn');
    this.sql.exec('UPDATE links SET accepted = 1, at = ? WHERE host = ? AND peer = ?', Date.now(), me, peer);
    // expires_in: how long the host's relay credentials last, so the joiner renews the connection before either side's run out
    this.sendTo(peer, { type: 'net_accepted', host: me, renew: m.renew === true, gen: gen(m.gen),
      expires_in: Math.max(0, Math.min(172_800, Math.floor(Number(m.expires_in) || 0))) });
  }

  private netReject(me: string, peer: string, reason: string) {
    this.sql.exec('DELETE FROM links WHERE host = ? AND peer = ?', me, peer);
    this.sendTo(peer, { type: 'net_refused', host: me, reason });
  }

  private netLeave(me: string, other: string) {
    this.sql.exec('DELETE FROM links WHERE (host = ? AND peer = ?) OR (host = ? AND peer = ?)', me, other, other, me);
    this.sendTo(other, { type: 'net_gone', pid: me });
  }

  /** The host's game kicked a player (a passed vote): their link ends and they can't rejoin this host for 30 minutes. */
  private netKick(me: string, peer: string) {
    const link = this.one<{ accepted: number }>('SELECT accepted FROM links WHERE host = ? AND peer = ?', me, peer);
    if (!link) return;
    this.sql.exec('DELETE FROM links WHERE host = ? AND peer = ?', me, peer);
    this.sql.exec('INSERT OR REPLACE INTO bans(host, pid, until) VALUES (?, ?, ?)', me, peer, Date.now() + KICK_BAN_MS);
    this.sendTo(peer, { type: 'net_gone', pid: me, kicked: true });
  }

  /** A player is gone (or hung up): their links end and the other side hears of it. */
  private netDropLinks(pid: string) {
    const others = this.rows<{ host: string; peer: string }>('SELECT host, peer FROM links WHERE host = ? OR peer = ?', pid, pid);
    if (!others.length) return;
    this.sql.exec('DELETE FROM links WHERE host = ? OR peer = ?', pid, pid);
    for (const l of others) this.sendTo(l.host === pid ? l.peer : l.host, { type: 'net_gone', pid });
  }

  /** Short-lived relay credentials (Cloudflare Realtime TURN). Locally, NET_DIRECT=1 allows a direct connection instead. */
  private async netIce(ws: WebSocket, tag: Tag) {
    const now = Date.now();
    if (now - tag.iceAt < 2000) return this.send(ws, { type: 'net_ice', error: 'Too many requests.' });
    tag.iceAt = now;
    ws.serializeAttachment(tag);
    const direct = this.env.NET_DIRECT === '1';
    const id = this.env.TURN_KEY_ID;
    const token = this.env.TURN_KEY_TOKEN;
    if (!id || !token) {
      if (direct) return this.send(ws, { type: 'net_ice', servers: [], relay_only: false, ttl: 3600 });
      return this.send(ws, { type: 'net_ice', error: 'Relayed play is not set up on this hub.' });
    }
    const isHost = tag.listing !== '';
    const refusal = this.relayRefusal(tag.pid, isHost);
    if (refusal) return this.send(ws, { type: 'net_ice', error: refusal });
    const ttl = this.relayTtl();
    try {
      const res = await fetch(`${this.turnApi()}/v1/turn/keys/${encodeURIComponent(id)}/credentials/generate-ice-servers`, {
        method: 'POST',
        headers: { authorization: `Bearer ${token}`, 'content-type': 'application/json' },
        body: JSON.stringify({ ttl }),
      });
      if (!res.ok) return this.send(ws, { type: 'net_ice', error: `The relay refused us (${res.status}).` });
      const body = (await res.json()) as { iceServers?: { urls?: string[] | string; username?: string; credential?: string }[] };
      const servers = (body.iceServers ?? [])
        .filter((s) => s.username && s.credential)
        .map((s) => ({ urls: ([] as string[]).concat(s.urls ?? []), username: s.username, credential: s.credential }));
      if (!servers.length) return this.send(ws, { type: 'net_ice', error: 'The relay gave no servers.' });
      const now = Date.now();
      this.sql.exec('INSERT OR REPLACE INTO relay_creds(username, pid, host, minted, expires, worst) VALUES (?, ?, ?, ?, ?, ?)',
        String(servers[0].username), tag.pid, isHost ? 1 : 0, now, now + ttl * 1000, isHost ? RELAY_GB_HOST : RELAY_GB_PLAYER);
      this.sql.exec('DELETE FROM relay_creds WHERE expires < ?', now - 40 * 86_400_000);
      this.send(ws, { type: 'net_ice', servers, relay_only: !direct, ttl });
      // only when no check is pending: until one comes, the credential counts at its worst case, so there is no hurry,
      // and moving an alarm that is due was one way local workerd lost requests (M8)
      if (this.relayUsageMode() && !this.alarmRunning && !(await this.ctx.storage.getAlarm())) await this.ctx.storage.setAlarm(now + this.relayCheckMs());
    } catch {
      this.send(ws, { type: 'net_ice', error: 'Could not reach the relay.' });
    }
  }

  private relayMonth(): string {
    return new Date().toISOString().slice(0, 7);
  }

  private relayBudget(): number {
    const v = Number(this.env.RELAY_BUDGET_GB);
    return Number.isFinite(v) && v > 0 ? v : RELAY_BUDGET_GB;
  }

  private relayUsageMode(): boolean {
    return !!(this.env.TURN_ANALYTICS_TOKEN && this.env.TURN_ACCOUNT_ID && this.env.TURN_KEY_ID);
  }

  private relayTtl(): number {
    const v = Math.floor(Number(this.env.TURN_TTL_S));
    if (Number.isFinite(v) && v >= 60) return Math.min(v, 172_800);
    return this.relayUsageMode() ? RELAY_TTL_USAGE_S : RELAY_TTL_WORST_S;
  }

  private relayCheckMs(): number {
    const v = Number(this.env.RELAY_CHECK_MS);
    return Number.isFinite(v) && v >= 500 ? v : RELAY_CHECK_MS;
  }

  private turnApi(): string {
    return this.env.TURN_API_URL || 'https://rtc.live.cloudflare.com';
  }

  /** Why a credential can't be minted now, or "" (the mint limits, then the budget). */
  private relayRefusal(pid: string, host: boolean): string {
    const now = Date.now();
    if (this.count('SELECT COUNT(*) AS n FROM relay_creds WHERE pid = ? AND minted > ?', pid, now - 3_600_000) >= RELAY_MINTS_PER_HOUR)
      return 'Too many relay requests from you; try again later.';
    if (this.count('SELECT COUNT(*) AS n FROM relay_creds WHERE minted > ?', now - 600_000) >= RELAY_MINTS_PER_10_MIN)
      return 'The relay is busy; try again in a few minutes.';
    if (this.relayUsageMode()) {
      this.relayCount(host);   // the worst-case total stays as a record in the admin view
      return this.relayEstimateGb() >= this.relayBudget() * RELAY_STOP_FRACTION ? 'Relayed play is paused for this month.' : '';
    }
    return this.relayCharge(host) ? '' : 'Relayed play is paused for this month.';
  }

  /** This month's relay traffic as far as the hub can know it: the analytics' last good figure, plus the worst case of
   *  every credential minted since (they aren't in it yet). */
  private relayEstimateGb(): number {
    const u = this.one<{ bytes: number; checked: number }>('SELECT bytes, checked FROM relay_usage WHERE month = ?', this.relayMonth());
    const since = Math.max(u?.checked ?? 0, Date.parse(this.relayMonth() + '-01T00:00:00Z'));
    const recent = this.one<{ gb: number }>('SELECT COALESCE(SUM(worst), 0) AS gb FROM relay_creds WHERE minted > ?', since);
    return (u?.bytes ?? 0) / 1e9 + (recent?.gb ?? 0);
  }

  private relayCount(host: boolean) {
    const cost = host ? RELAY_GB_HOST : RELAY_GB_PLAYER;
    this.sql.exec(
      'INSERT INTO relay_budget(month, gb, mints) VALUES (?, ?, 1) ON CONFLICT(month) DO UPDATE SET gb = gb + ?, mints = mints + 1',
      this.relayMonth(), cost, cost);
  }

  /** The usage guard's check (from the alarm): Cloudflare's figures, then revocations. Returns when it wants to run
   *  next, or 0 when no credential is alive any more and the last figures are in. */
  private async relayTick(): Promise<number> {
    if (!this.relayUsageMode()) return 0;
    const now = Date.now();
    const every = this.relayCheckMs();
    const month = this.relayMonth();
    const u = this.one<{ checked: number; tried: number }>('SELECT checked, tried FROM relay_usage WHERE month = ?', month);
    const lastExpiry = this.one<{ t: number }>('SELECT COALESCE(MAX(expires), 0) AS t FROM relay_creds')?.t ?? 0;
    // keep checking a while after the last credential ends, so its final traffic is counted
    if (lastExpiry + 5 * 60_000 < (u?.checked ?? 0)) return 0;
    if (u && now - u.tried < every - 250) return u.tried + every;
    this.sql.exec('INSERT INTO relay_usage(month, tried) VALUES (?, ?) ON CONFLICT(month) DO UPDATE SET tried = ?', month, now, now);
    let used: number;
    try {
      used = await this.turnEgress(Date.parse(month + '-01T00:00:00Z'), now, false) as number;
      this.sql.exec('UPDATE relay_usage SET bytes = ?, checked = ?, error = ? WHERE month = ?', used, now, '', month);
    } catch (e) {
      this.sql.exec('UPDATE relay_usage SET error = ? WHERE month = ?', String((e as Error).message ?? e).slice(0, 300), month);
      return now + every;
    }
    const live = this.rows<{ username: string; pid: string; host: number; minted: number }>(
      "SELECT username, pid, host, minted FROM relay_creds WHERE expires > ? AND revoked = ''", now);
    if (used >= this.relayBudget() * 1e9) {
      for (const c of live) await this.relayRevoke(c.username, 'the month\'s budget is used up');
      return now + every;
    }
    try {
      const recent = await this.turnEgress(now - RELAY_RATE_WINDOW_MS, now, true) as Map<string, number>;
      for (const c of live) {
        const span = Math.min(RELAY_RATE_WINDOW_MS, now - c.minted) / 1000;
        if (span < Math.min(60, (2 * every) / 1000)) continue;   // too young to judge: a minute (two checks) in the game
        const rate = (recent.get(c.username) ?? 0) / span;
        if (rate > (c.host ? RELAY_RATE_HOST : RELAY_RATE_PLAYER))
          await this.relayRevoke(c.username, `${Math.round(rate / 1000)} KB/s, more than the game sends`);
      }
    } catch (e) {
      this.sql.exec('UPDATE relay_usage SET error = ? WHERE month = ?', 'per credential: ' + String((e as Error).message ?? e).slice(0, 280), month);
    }
    return now + every;
  }

  /** The relay's egress for this key since `from`: in all, or per credential (username). */
  private async turnEgress(from: number, to: number, perCredential: boolean): Promise<number | Map<string, number>> {
    const day = (t: number) => new Date(t).toISOString().slice(0, 10);
    const key = JSON.stringify(this.env.TURN_KEY_ID ?? '');
    const filter = perCredential
      ? `{ keyId: ${key}, date_geq: ${JSON.stringify(day(from))}, date_leq: ${JSON.stringify(day(to))}, datetimeMinute_geq: ${JSON.stringify(new Date(from).toISOString().slice(0, 17) + '00Z')} }`
      : `{ keyId: ${key}, date_geq: ${JSON.stringify(day(from))}, date_leq: ${JSON.stringify(day(to))} }`;
    const query = `{ viewer { accounts(filter: { accountTag: ${JSON.stringify(this.env.TURN_ACCOUNT_ID ?? '')} }) {
      callsTurnUsageAdaptiveGroups(filter: ${filter}, limit: ${perCredential ? 5000 : 1}) {
        ${perCredential ? 'dimensions { username }' : ''} sum { egressBytes } } } } }`;
    const res = await fetch(this.env.CF_GRAPHQL_URL || 'https://api.cloudflare.com/client/v4/graphql', {
      method: 'POST',
      headers: { authorization: `Bearer ${this.env.TURN_ANALYTICS_TOKEN}`, 'content-type': 'application/json' },
      body: JSON.stringify({ query }),
    });
    if (!res.ok) throw new Error(`analytics answered ${res.status}`);
    const body = (await res.json()) as { data?: { viewer?: { accounts?: { callsTurnUsageAdaptiveGroups?: { dimensions?: { username?: string }; sum?: { egressBytes?: number } }[] }[] } }; errors?: { message?: string }[] | null };
    if (body.errors?.length) throw new Error('analytics: ' + body.errors.map((e) => e.message).join('; '));
    const groups = body.data?.viewer?.accounts?.[0]?.callsTurnUsageAdaptiveGroups;
    if (!groups) throw new Error('analytics: no data for the account');
    if (!perCredential) return Number(groups[0]?.sum?.egressBytes ?? 0);
    const out = new Map<string, number>();
    for (const g of groups) if (g.dimensions?.username) out.set(g.dimensions.username, Number(g.sum?.egressBytes ?? 0));
    return out;
  }

  /** Ends a credential at the relay (its connections drop shortly after) and notes why. */
  private async relayRevoke(username: string, why: string) {
    const id = this.env.TURN_KEY_ID ?? '';
    try {
      const res = await fetch(`${this.turnApi()}/v1/turn/keys/${encodeURIComponent(id)}/credentials/${encodeURIComponent(username)}/revoke`, {
        method: 'POST', headers: { authorization: `Bearer ${this.env.TURN_KEY_TOKEN}` },
      });
      if (res.ok || res.status === 404) this.sql.exec('UPDATE relay_creds SET revoked = ? WHERE username = ?', why, username);
    } catch {
      // tried again at the next check
    }
  }

  /** Charges one credential against this month's budget; false (and no charge) when it would pass it. */
  private relayCharge(host: boolean): boolean {
    const month = this.relayMonth();
    const cost = host ? RELAY_GB_HOST : RELAY_GB_PLAYER;
    const row = this.one<{ gb: number }>('SELECT gb FROM relay_budget WHERE month = ?', month);
    if ((row?.gb ?? 0) + cost > this.relayBudget()) return false;
    this.sql.exec(
      'INSERT INTO relay_budget(month, gb, mints) VALUES (?, ?, 1) ON CONFLICT(month) DO UPDATE SET gb = gb + ?, mints = mints + 1',
      month, cost, cost);
    return true;
  }

  /** WebRTC signaling between a linked host and peer. Only relay candidates pass (unless NET_DIRECT): an address
   *  in a candidate would show one player's IP to the other. */
  private netSignal(ws: WebSocket, me: string, m: Msg) {
    const to = String(m.to ?? '');
    const link = this.one<{ accepted: number }>('SELECT accepted FROM links WHERE (host = ? AND peer = ?) OR (host = ? AND peer = ?)', me, to, to, me);
    if (!link || !link.accepted) return;
    const direct = this.env.NET_DIRECT === '1';
    const kind = String(m.kind ?? '');
    if (kind === 'offer' || kind === 'answer') {
      let sdp = String(m.sdp ?? '');
      if (sdp.length > 12_000) return;
      if (!direct)
        sdp = sdp.split(/\r?\n/).filter((l) => !l.startsWith('a=candidate')).map((l) => (l.startsWith('c=IN IP') ? 'c=IN IP4 0.0.0.0' : l)).join('\r\n') + '\r\n';
      return this.sendTo(to, { type: 'net_signal', from: me, kind, sdp, gen: gen(m.gen) });
    }
    if (kind === 'candidate') {
      const candidate = String(m.candidate ?? '');
      if (candidate.length > 400 || (!direct && !/ typ relay\b/.test(candidate))) return;
      return this.sendTo(to, { type: 'net_signal', from: me, kind, candidate, mid: clean(m.mid, 32), index: Math.max(0, Math.min(8, Math.floor(Number(m.index) || 0))), gen: gen(m.gen) });
    }
  }

  private sendTo(pid: string, msg: object) {
    const data = JSON.stringify(msg);
    for (const s of this.sockets(pid)) trySend(s, data);
  }

  // ------------------------------------------------------------------ matchmaking and ranked
  // MULTIPLAYER_PLAN.md M6. A player in the queue has a QEntry on their socket. While anyone is queued or a match is
  // forming, an alarm ticks once a second (ranked.ts formGroup): the oldest entry's group is picked by rating window
  // and region, the best connected of them hosts (electHost), and everyone gets match_found. The host starts a private
  // listing (net_host with the match id) and the others join it. Results come back as each player's own report of the
  // final scoreboard over their signed-in socket; ranked.ts decide() says when they agree, and ratings move only then.

  private ownMatch(host: string, id: string): string {
    if (!/^[0-9a-f]{16}$/.test(id)) return '';
    return this.one('SELECT id FROM rmatches WHERE id = ? AND host = ?', id, host) ? id : '';
  }

  private matchPlayers(id: string): { id: string; name: string; team: number; joined: boolean }[] {
    const r = this.one<{ players: string }>('SELECT players FROM rmatches WHERE id = ?', id);
    return r ? JSON.parse(r.players) : [];
  }

  private ratingRow(pid: string, queue: string) {
    const season = seasonId(Date.now());
    const row = this.one<{ rating: number; rated: number; wins: number; losses: number; peak: number; season: string }>(
      'SELECT rating, rated, wins, losses, peak, season FROM ratings WHERE pid = ? AND queue = ?', pid, queue);
    if (!row) return { rating: START_RATING, rated: 0, wins: 0, losses: 0, peak: START_RATING, season };
    if (row.season !== season) {
      // a new season, noticed on first contact: archive the last one and ease the rating back a quarter of the way
      this.sql.exec('INSERT OR REPLACE INTO seasons(pid, queue, season, final, peak, wins, losses) VALUES (?, ?, ?, ?, ?, ?, ?)',
        pid, queue, row.season, row.rating, row.peak, row.wins, row.losses);
      const eased = easeToStart(row.rating);
      this.sql.exec('UPDATE ratings SET rating = ?, peak = ?, wins = 0, losses = 0, season = ? WHERE pid = ? AND queue = ?', eased, eased, season, pid, queue);
      return { rating: eased, rated: row.rated, wins: 0, losses: 0, peak: eased, season };
    }
    return row;
  }

  private ratingsMessage(pid: string) {
    const queues: Record<string, unknown> = {};
    for (const q of Object.keys(MODES)) {
      if (!MODES[q].ranked) continue;
      const r = this.ratingRow(pid, q);
      queues[q] = { rating: r.rating, rated: r.rated, wins: r.wins, losses: r.losses, peak: r.peak };
    }
    return { type: 'ratings', season: seasonId(Date.now()), placements: PLACEMENTS, queues };
  }

  private entries(): { entry: QEntry; ws: WebSocket; tag: Tag }[] {
    const out: { entry: QEntry; ws: WebSocket; tag: Tag }[] = [];
    for (const s of this.ctx.getWebSockets()) {
      if (s.readyState !== 1) continue;
      const t = s.deserializeAttachment() as Tag | null;
      if (t && t.queue) out.push({ entry: JSON.parse(t.queue), ws: s, tag: t });
    }
    return out;
  }

  private async queueJoin(ws: WebSocket, tag: Tag, m: Msg) {
    const refused = (why: string) => this.send(ws, { type: 'queue_state', status: 'refused', reason: why });
    const modes = (Array.isArray(m.modes) ? m.modes : []).map(String).filter((x) => x in MODES);
    const ranked = m.ranked === true;
    if (!modes.length) return refused('Pick a playlist.');
    if (ranked && !modes.every((x) => MODES[x].ranked)) return refused('That mode has no ranked queue yet.');
    const now = Date.now();
    const lock = this.one<{ until: number }>('SELECT until FROM lockouts WHERE pid = ?', tag.pid);
    if (lock && lock.until > now) return this.send(ws, { type: 'queue_state', status: 'locked', until: lock.until, reason: 'You left a match you were sent to. Try again in a few minutes.' });
    if (ranked) {
      const level = this.one<{ level: number }>('SELECT level FROM careers WHERE pid = ?', tag.pid)?.level ?? 1;
      if (level < RANKED_LEVEL) return refused(`Ranked opens at level ${RANKED_LEVEL}. You are level ${level}.`);
    }
    const ratings: Record<string, number> = {};
    if (ranked) for (const q of modes) ratings[q] = this.ratingRow(tag.pid, q).rating;
    const named = REGIONS.includes(String(m.region)); // "auto" (or nothing) means the player's own region, widening after a wait
    const region = named ? String(m.region) : tag.region;
    const entry: QEntry = {
      pid: tag.pid, modes, ranked, region, auto: !named,
      since: now, rtt: Math.max(0, Math.min(2000, Math.floor(Number(m.rtt) || 100))), ratings,
    };
    // one queue per player, on their newest socket
    for (const s of this.sockets(tag.pid)) {
      const t = s.deserializeAttachment() as Tag;
      t.queue = s === ws ? JSON.stringify(entry) : '';
      s.serializeAttachment(t);
    }
    this.send(ws, { type: 'queue_state', status: 'queued', since: now, modes, ranked, region, auto: entry.auto });
    await this.wakeAt(now + 1000);
  }

  /** The Durable Object has one alarm; it serves the matchmaking tick and the relay guard, whichever is due first.
   *  Never set while the alarm handler runs (getAlarm() reads empty then): setting it there restarted the object
   *  under the handler and dropped what else was in flight (a mint's answer: "Network connection lost", M8). The
   *  handler schedules what was asked for when it ends. */
  private alarmRunning = false;
  private wakeWanted = Infinity;
  private async wakeAt(t: number) {
    if (this.alarmRunning) {
      this.wakeWanted = Math.min(this.wakeWanted, t);
      return;
    }
    const a = await this.ctx.storage.getAlarm();
    if (!a || a > t) await this.ctx.storage.setAlarm(t);
  }

  private queueLeave(ws: WebSocket, tag: Tag) {
    for (const s of this.sockets(tag.pid)) {
      const t = s.deserializeAttachment() as Tag;
      t.queue = '';
      s.serializeAttachment(t);
    }
    this.send(ws, { type: 'queue_state', status: 'idle' });
  }

  /** A player's last socket closed: they leave the queue (their entry went with the socket) and the match they were sent to counts them missing. */
  private queueDrop(_pid: string) {}

  async alarm() {
    this.alarmRunning = true;
    this.wakeWanted = Infinity;
    let next = Infinity;
    try {
      this.matchTick();
      let relay = 0;
      try {
        relay = await this.relayTick();
      } catch {
        relay = Date.now() + this.relayCheckMs();
      }
      const busy = this.entries().length > 0 || this.one("SELECT id FROM rmatches WHERE state IN ('forming', 'live') LIMIT 1");
      next = Math.min(busy ? Date.now() + 1000 : Infinity, relay || Infinity);
    } finally {
      // the handler sets its own next alarm, and takes any wake asked for meanwhile (even during these writes)
      // before others may set it again
      let set = Infinity;
      for (;;) {
        const want = Math.min(next, this.wakeWanted);
        if (want >= set) break;
        set = want;
        await this.ctx.storage.setAlarm(want);
      }
      this.alarmRunning = false;
    }
  }

  private matchTick() {
    const now = Date.now();
    // 1. form matches from the queue
    let pool = this.entries();
    for (const mode of Object.keys(MODES)) {
      for (const ranked of [true, false]) {
        for (;;) {
          const here = pool.filter((e) => e.entry.ranked === ranked && e.entry.modes.includes(mode));
          const group = formGroup(here.map((e) => e.entry), mode, now);
          if (!group) break;
          this.formMatch(group, mode, ranked, pool);
          const gone = new Set(group.map((g) => g.pid));
          pool = pool.filter((e) => !gone.has(e.entry.pid));
        }
      }
    }
    // 2. matches that never started, and results still waiting for reports
    for (const row of this.rows<{ id: string; created: number; first_report: number; state: string; ranked: number }>(
      "SELECT id, created, first_report, state, ranked FROM rmatches WHERE state IN ('forming', 'live')")) {
      if (row.state === 'forming' && now - row.created > (Number(this.env.MATCH_FORM_MS) || 90_000)) this.voidMatch(row.id, 'The match never started.', null, true);
      else if (row.ranked && row.first_report && now - row.first_report > (Number(this.env.MATCH_REPORT_MS) || 60_000)) this.resolveMatch(row.id, true);
      else if (!row.ranked && row.state === 'live' && now - row.created > 3 * 3600_000) this.sql.exec("UPDATE rmatches SET state = 'done' WHERE id = ?", row.id);
    }
    this.sql.exec("DELETE FROM rmatches WHERE state IN ('done', 'void') AND created < ?", now - 24 * 3600_000);
  }

  private formMatch(group: QEntry[], mode: string, ranked: boolean, pool: { entry: QEntry; ws: WebSocket; tag: Tag }[]) {
    const info = MODES[mode];
    // a host whose matches keep getting voided loses hosting (when someone else can)
    const bad = new Set(this.rows<{ pid: string }>('SELECT pid FROM host_stats WHERE voids >= 3 AND voids * 3 > matches').map((r) => r.pid));
    const eligible = group.filter((g) => !bad.has(g.pid));
    const host = electHost(eligible.length ? eligible : group);
    const teams = info.team ? assignTeams(group, mode) : group.map(() => 0);
    const id = [...crypto.getRandomValues(new Uint8Array(8))].map((b) => b.toString(16).padStart(2, '0')).join('');
    const map = info.maps[Math.floor(Math.random() * info.maps.length)];
    const players = group.map((g, i) => ({ id: g.pid, name: this.player(g.pid)?.name ?? 'Player', team: teams[i], joined: false }));
    this.sql.exec('INSERT INTO rmatches(id, queue, ranked, host, map, created, state, players) VALUES (?, ?, ?, ?, ?, ?, ?, ?)',
      id, mode, ranked ? 1 : 0, host.pid, map, Date.now(), 'forming', JSON.stringify(players));
    for (const g of group) {
      const e = pool.find((x) => x.entry.pid === g.pid);
      if (e) {
        e.tag.queue = '';
        e.ws.serializeAttachment(e.tag);
      }
      this.sendTo(g.pid, {
        type: 'match_found', match: id, host: host.pid, you_host: g.pid === host.pid, mode, map, ranked,
        team: players.find((p) => p.id === g.pid)!.team, players: players.map((p) => ({ id: p.id, name: p.name, team: p.team })),
      });
    }
  }

  private matchJoined(me: string, id: string) {
    const row = this.one<{ players: string; state: string }>('SELECT players, state FROM rmatches WHERE id = ?', id);
    if (!row) return;
    const players = JSON.parse(row.players) as { id: string; joined: boolean }[];
    const p = players.find((x) => x.id === me);
    if (!p) return;
    p.joined = true;
    const live = row.state === 'forming' && players.every((x) => x.joined);
    this.sql.exec('UPDATE rmatches SET players = ?, state = ? WHERE id = ?', JSON.stringify(players), live ? 'live' : row.state, id);
  }

  private cleanReport(m: Msg): Report | null {
    const num = (v: unknown, lo: number, hi: number) => Math.max(lo, Math.min(hi, Math.floor(Number(v) || 0)));
    const rows: ScoreRow[] = (Array.isArray(m.rows) ? m.rows : []).slice(0, 64).map((r) => {
      const o = (r ?? {}) as Record<string, unknown>;
      return { client: num(o.client, 0, 63), score: num(o.score, -100000, 100000), team: num(o.team, 0, 3) };
    });
    const peers: Record<string, string> = {};
    if (m.peers && typeof m.peers === 'object')
      for (const [c, pid] of Object.entries(m.peers as Record<string, unknown>).slice(0, 64))
        if (/^\d{1,2}$/.test(c) && /^[0-9a-f]{20}$/.test(String(pid))) peers[c] = String(pid);
    return { me: num(m.me, 0, 63), rows, left: m.left === true, peers };
  }

  private matchReport(me: string, m: Msg) {
    const id = String(m.match ?? '');
    const row = this.one<{ players: string; reports: string; first_report: number; state: string; ranked: number }>(
      'SELECT players, reports, first_report, state, ranked FROM rmatches WHERE id = ?', id);
    if (!row || !row.ranked || (row.state !== 'live' && row.state !== 'forming')) return;
    if (!(JSON.parse(row.players) as { id: string }[]).some((p) => p.id === me)) return;
    const reports = JSON.parse(row.reports) as Record<string, Report>;
    if (reports[me]) return; // the first report counts
    const r = this.cleanReport(m);
    if (!r) return;
    reports[me] = r;
    this.sql.exec('UPDATE rmatches SET reports = ?, first_report = ? WHERE id = ?', JSON.stringify(reports), row.first_report || Date.now(), id);
    this.resolveMatch(id, false);
    // the alarm makes sure a match whose other reports never come is decided too
    void this.wakeAt(Date.now() + 1000);
  }

  private resolveMatch(id: string, final: boolean) {
    const row = this.one<{ queue: string; host: string; players: string; reports: string; state: string }>(
      'SELECT queue, host, players, reports, state FROM rmatches WHERE id = ?', id);
    if (!row || (row.state !== 'live' && row.state !== 'forming')) return;
    const players = JSON.parse(row.players) as { id: string; name: string; team: number }[];
    const reports = JSON.parse(row.reports) as Record<string, Report>;
    const verdict = decide(players.map((p) => p.id), row.host, reports, row.queue === 'duel', final);
    if (verdict === 'wait') return;
    if (!verdict.ok) return this.voidMatch(id, verdict.reason, verdict.blame);
    const byClient = verdict.clients;
    const left = new Set(players.filter((p) => reports[p.id]?.left).map((p) => p.id));
    const played: Played[] = players.map((p) => {
      const client = Number(Object.keys(byClient).find((c) => byClient[Number(c)] === p.id));
      const r = verdict.rows.find((x) => x.client === client);
      const rating = this.ratingRow(p.id, row.queue);
      return { pid: p.id, rating: rating.rating, rated: rating.rated, team: r?.team || p.team, score: r?.score ?? 0, left: left.has(p.id) || !r };
    });
    const deltas = eloDeltas(row.queue, played);
    const season = seasonId(Date.now());
    for (const p of played) {
      const before = this.ratingRow(p.pid, row.queue);
      const after = before.rating + deltas[p.pid];
      const win = won(row.queue, p, played);
      this.sql.exec(
        `INSERT INTO ratings(pid, queue, rating, rated, wins, losses, peak, season) VALUES (?, ?, ?, 1, ?, ?, ?, ?)
         ON CONFLICT(pid, queue) DO UPDATE SET rating = excluded.rating, rated = rated + 1, wins = wins + ?, losses = losses + ?, peak = MAX(peak, excluded.rating)`,
        p.pid, row.queue, after, win ? 1 : 0, win ? 0 : 1, Math.max(after, before.peak), season, win ? 1 : 0, win ? 0 : 1);
      if (row.queue === 'duel') {
        // the leaderboard's ranked columns: the headline queue (the duel). Only the hub writes them.
        const r = this.ratingRow(p.pid, 'duel');
        this.sql.exec('INSERT OR IGNORE INTO careers(pid, xp, level, card, stats, updated) VALUES (?, 0, 1, ?, ?, ?)', p.pid, '{}', '{}', Date.now());
        this.sql.exec('UPDATE careers SET rating = ?, rated = ?, peak = ? WHERE pid = ?', r.rating, r.rated, r.peak, p.pid);
      }
      const now = this.ratingRow(p.pid, row.queue);
      this.sendTo(p.pid, {
        type: 'match_result', match: id, voided: false, queue: row.queue, before: before.rating, after: now.rating, delta: deltas[p.pid],
        rated: now.rated, wins: now.wins, losses: now.losses, won: win,
      });
      this.sendTo(p.pid, this.ratingsMessage(p.pid));
    }
    this.sql.exec("UPDATE rmatches SET state = 'done' WHERE id = ?", id);
    this.sql.exec('INSERT INTO host_stats(pid, matches, voids) VALUES (?, 1, ?) ON CONFLICT(pid) DO UPDATE SET matches = matches + 1, voids = voids + ?',
      row.host, verdict.hostOverruled ? 1 : 0, verdict.hostOverruled ? 1 : 0);
  }

  /** The match is void: no rating moves. `lockMissing`: players who never joined it get a lockout (queue dodging). */
  private voidMatch(id: string, reason: string, blame: string | null, lockMissing = false) {
    const row = this.one<{ players: string; host: string; ranked: number }>('SELECT players, host, ranked FROM rmatches WHERE id = ?', id);
    if (!row) return;
    const players = JSON.parse(row.players) as { id: string; joined: boolean }[];
    this.sql.exec("UPDATE rmatches SET state = 'void' WHERE id = ?", id);
    for (const p of players) {
      if (lockMissing && !p.joined) this.sql.exec('INSERT OR REPLACE INTO lockouts(pid, until) VALUES (?, ?)', p.id, Date.now() + 5 * 60_000);
      this.sendTo(p.id, { type: 'match_result', match: id, voided: true, reason });
    }
    if (blame === row.host)
      this.sql.exec('INSERT INTO host_stats(pid, matches, voids) VALUES (?, 1, 1) ON CONFLICT(pid) DO UPDATE SET matches = matches + 1, voids = voids + 1', row.host);
  }

  // ------------------------------------------------------------------ storage helpers

  /** The player's open sockets (a closing one can still be listed while its close handler runs). */
  private sockets(pid: string, skip?: WebSocket): WebSocket[] {
    return this.ctx.getWebSockets(pid).filter((s) => s !== skip && s.readyState === 1);
  }

  private player(pid: string): Player | null {
    return this.one<Player>('SELECT id, code, name, model, color, seen FROM players WHERE id = ?', pid);
  }

  private rows<T>(q: string, ...args: SqlStorageValue[]): T[] {
    return this.sql.exec(q, ...args).toArray() as T[];
  }

  private one<T = Record<string, SqlStorageValue>>(q: string, ...args: SqlStorageValue[]): T | null {
    return (this.rows<T>(q, ...args)[0] ?? null) as T | null;
  }

  private count(q: string, ...args: SqlStorageValue[]): number {
    return Number(this.one<{ n: number }>(q, ...args)?.n ?? 0);
  }

  /** An 8-character friend code from the player id, in an alphabet without look-alikes (Crockford base32). */
  private async newCode(pid: string): Promise<string> {
    for (let salt = 0; ; salt++) {
      const h = new Uint8Array(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(`${pid}:${salt}`)));
      let bits = 0n;
      for (const b of h.slice(0, 5)) bits = (bits << 8n) | BigInt(b);
      let s = '';
      for (let i = 0; i < 8; i++) s = CODE_ALPHABET[Number((bits >> BigInt(i * 5)) & 31n)] + s;
      const code = `${s.slice(0, 4)}-${s.slice(4)}`;
      if (!this.one('SELECT 1 AS x FROM players WHERE code = ?', code)) return code;
    }
  }
}

type Row = { pid: string; name: string; model: string; color: string; xp: number; level: number; card: string; stats: string;
  rating: number; rated: number; peak: number };

/** One leaderboard row as the game gets it. */
interface BoardRow {
  rank: number; id: string; name: string; model: string; color: string; xp: number; level: number;
  card: Record<string, unknown>; stats: Record<string, string>; rating: number; rated: number; peak: number; online: boolean;
}

const CODE_ALPHABET = '0123456789ABCDEFGHJKMNPQRSTVWXYZ';

/** "abcd 1234", "ABCD1234" and "abcd-l234" all mean ABCD-1234 (O reads as 0, I and L as 1). */
export function normalizeCode(raw: string): string {
  const s = raw.toUpperCase().replace(/[^0-9A-Z]/g, '').replace(/O/g, '0').replace(/[IL]/g, '1');
  if (s.length !== 8 || [...s].some((c) => !CODE_ALPHABET.includes(c))) return '';
  return `${s.slice(0, 4)}-${s.slice(4)}`;
}

function trySend(ws: WebSocket, data: string) {
  try {
    ws.send(data);
  } catch {
    /* closed */
  }
}

/** A connection's generation (0 the first, +1 each renewal), as signals and renewals carry it. */
function gen(v: unknown): number {
  return Math.max(0, Math.min(1_000_000, Math.floor(Number(v) || 0)));
}

function clean(v: unknown, max: number): string {
  return String(v ?? '').replace(/[\u0000-\u001f\u007f]/g, '').trim().slice(0, max);
}

/** The game URI-encodes the name so any characters survive an HTTP header. */
function decodeName(v: string | null): string {
  try {
    return decodeURIComponent(v ?? '');
  } catch {
    return v ?? '';
  }
}

/** Quake 3 names keep their ^ color codes; 32 characters like the game's own limit. */
function cleanName(v: unknown): string {
  return clean(v, 32) || 'Player';
}

function cleanModel(v: unknown): string {
  const s = String(v ?? '').toLowerCase();
  return /^[a-z0-9_]{1,24}$/.test(s) ? s : 'sarge';
}

/** Level from total XP, the game's curve (Q3Cards.level_for). */
export function levelFor(xp: number): number {
  let level = 1;
  let left = xp;
  while (level < MAX_LEVEL && left >= XP_BASE + XP_STEP * (level - 1)) {
    left -= XP_BASE + XP_STEP * (level - 1);
    level++;
  }
  return level;
}

const CARD_SLOTS = ['banner', 'emblem', 'frame', 'title', 'accent', 'rail', 'taunt'];

/** A card is part ids by slot plus up to three showcase stat ids and badge ids; anything else is dropped. */
function cleanCard(v: unknown): Record<string, unknown> {
  const c = (v && typeof v === 'object' ? v : {}) as Record<string, unknown>;
  const id = (x: unknown) => (typeof x === 'string' && /^[a-z0-9_]{1,48}$/.test(x) ? x : '');
  const out: Record<string, unknown> = {};
  for (const k of CARD_SLOTS) out[k] = id(c[k]);
  for (const k of ['stats', 'badges']) out[k] = (Array.isArray(c[k]) ? c[k] : []).map(id).filter(Boolean).slice(0, 3);
  return out;
}

/** Showcase stat values as the card prints them: id -> short text. */
function cleanStats(v: unknown): Record<string, string> {
  const out: Record<string, string> = {};
  if (!v || typeof v !== 'object') return out;
  for (const [k, x] of Object.entries(v as Record<string, unknown>).slice(0, 24))
    if (/^[a-z0-9_]{1,32}$/.test(k)) out[k] = clean(x, 16);
  return out;
}

function cleanColor(v: unknown): string {
  const s = String(v ?? '').replace(/^#/, '').toLowerCase();
  return /^[0-9a-f]{6}$/.test(s) ? `#${s}` : '#db2410';
}

