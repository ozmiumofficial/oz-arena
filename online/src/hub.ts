// The social hub: one Durable Object ("global") with its own SQLite holding every player, friendship, friend
// request, party and party invite, plus live presence on the sockets themselves (WebSocket hibernation, so idle
// players cost nothing). The Worker has already checked the player's signature; this object trusts x-pid.
//
// Protocol (JSON, one object per message, "type" says what it is):
//   game -> hub  presence {status, detail, join?}   profile {name, model, color}   sync
//                career {xp, card, stats}   leaderboard {board: level|ranked, scope: global|friends}
//                friend_add {code}   friend_accept {id}   friend_decline {id}   friend_cancel {id}   friend_remove {id}
//                party_invite {id}   party_accept {party}   party_decline {party}   party_leave
//                party_kick {id}   party_promote {id}   party_chat {text}   ping (answered without waking the object)
//   hub -> game  social {me, friends, incoming, outgoing, party, invites}   (the whole picture, sent on every change)
//                notice {text, tone}   party_chat {from, text, at}   pong
//                leaderboard {board, scope, rows, me, total}
import { DurableObject } from 'cloudflare:workers';
import type { Env } from './index';
import { refuse } from './util';

const MAX_PARTY = 8;
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

type Status = 'online' | 'match' | 'editor';
const STATUSES: Status[] = ['online', 'match', 'editor'];

interface Tag {
  pid: string;
  status: Status;
  detail: string;
  join: string;
  since: number;
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
    `);
  }

  // ------------------------------------------------------------------ connection

  async fetch(req: Request): Promise<Response> {
    // the operator's routes; the Worker has already checked the admin token
    const path = new URL(req.url).pathname;
    if (path === '/admin/players') return Response.json(this.adminPlayers());
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
    const tag: Tag = { pid, status: 'online', detail: '', join: '', since: now, tokens: BUCKET, at: now, chat: 0 };
    server.serializeAttachment(tag);
    this.prune(this.partyOf(pid));
    this.send(server, this.snapshot(pid));
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
   * unknown parts back to the default. Ranked columns are never written from here: only official ranked servers will.
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

