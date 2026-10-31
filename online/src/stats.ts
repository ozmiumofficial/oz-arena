// Match records (ENGINE_PLAN.md P8b steps 5-7): every Oz player's signed report of a match they played to the end, on
// any server, the matches those reports make, and the careers and leaderboards of the matches that count. Its own
// Durable Object ("global") with its own SQLite, so match traffic never slows the social hub. The Hub calls it (RPC)
// once it has checked a report's signature against the key its sender signed in with, and reads its careers, boards
// and histories for the game.
//
// A report (the engine's recorder, engine/src/oz/online/oz_stats.c, from the HUD's match state):
//   {v: 1, rid, match_id?, server, hostname, mod, gametype, mode, map, pure, serverid, start, seconds, ended, me, left,
//    rows: [{client, score, team, name, bot}], you: {place, won, score, frags, deaths, xp, frags_weapon, deaths_weapon,
//    shots, hits, damage, pickups, medals, rail_streak, distance, speed}}
// `serverid` (sv_serverid: the server's clock at the map load, so a restarted server's next match is another) and
// `start` (the level start time, CS_LEVEL_START_TIME, which a warmup's restart moves) are the server's own, the same for
// everyone in the match; team games add the team scores as rows -1 (red) and -2 (blue), so the reports agree on them
// too. A match is keyed by its match_id when the matchmaker placed it, else by server address, map, serverid and start
// (within KEY_WINDOW_MS). "local" (the reporter's own listen server, not hosted online) is never anyone else's match.
//
// Trust (step 7): a match counts for nothing shared until two or more Oz players' reports agree on its scoreboard
// (Corroborated), decided by ranked.ts decide(), the hub's one agreement check. A lone report (Self-reported) stays in
// its player's own history. A match that counted keeps counting; a later report that disagrees just doesn't count.
// Verified (the Oz Server Agent, step 8) comes later. Shared XP, the level board's, is the XP the agreed scoreboard
// gives: 100 for finishing, 10 a point of score, 150 a win (q3_profile.gd's lines that the scoreboard proves).
import { DurableObject } from 'cloudflare:workers';
import type { Env } from './index';
import { canonical, decide, MODES, type Report, type Row } from './ranked';

/** Reports of the same server, map and start this close together are one match. */
const KEY_WINDOW_MS = 6 * 3600_000;
/** A report older than this (a game that was offline for long) is refused. */
const MAX_AGE_MS = 30 * 86_400_000;
const HISTORY_ROWS = 20;
const XP_FINISHED = 100;
const XP_FRAG = 10;
const XP_WIN = 150;

export type Trust = 'self' | 'corroborated' | 'disputed' | 'verified';

/** A report as stored, cleaned. */
interface Clean {
  v: number;
  rid: string;
  match_id: string;
  server: string;
  hostname: string;
  mod: string;
  gametype: number;
  mode: string;
  map: string;
  pure: boolean;
  serverid: number;
  start: number;
  seconds: number;
  ended: number;
  me: number;
  left: boolean;
  rows: (Row & { name: string; bot: boolean })[];
  you: You;
}

interface You {
  place: number;
  won: boolean;
  score: number;
  frags: number;
  deaths: number;
  xp: number;
  rail_streak: number;
  distance: number;
  speed: number;
  frags_weapon: Record<string, number>;
  deaths_weapon: Record<string, number>;
  shots: Record<string, number>;
  hits: Record<string, number>;
  damage: Record<string, number>;
  pickups: Record<string, number>;
  medals: Record<string, number>;
}

export type Stored =
  | { ok: true; rid: string; match: number; key: string; trust: Trust; reports: number; agreed: number; map: string; mode: string;
      /** reporters whose report agrees with the decided scoreboard (each told when the match's trust changes) */
      reporters: string[]; changed: boolean; counted: string[] }
  | { ok: false; rid: string; reason: string };

export interface HistoryRow {
  match: number; at: number; server: string; hostname: string; mod: string; mode: string; map: string; seconds: number;
  trust: Trust; reports: number; agreed: number; counted: boolean; place: number; players: number; score: number; won: boolean;
  frags: number; deaths: number; xp: number; shared_xp: number; rows: { name: string; score: number; team: number; bot: boolean; you: boolean }[];
}

export interface Career {
  xp: number; matches: number; wins: number; frags: number; deaths: number; seconds: number;
  modes: { mod: string; mode: string; matches: number; wins: number; frags: number; deaths: number; seconds: number }[];
  weapons: { weapon: string; frags: number; deaths: number; shots: number; hits: number }[];
  maps: { map: string; matches: number; wins: number; frags: number; deaths: number }[];
}

export interface StatsBoardRow { pid: string; value: number; xp: number; matches: number; wins: number; frags: number }

export class Stats extends DurableObject<Env> {
  private sql: SqlStorage;

  constructor(ctx: DurableObjectState, env: Env) {
    super(ctx, env);
    this.sql = ctx.storage.sql;
    this.sql.exec(`
      CREATE TABLE IF NOT EXISTS matches(id INTEGER PRIMARY KEY AUTOINCREMENT, key TEXT NOT NULL, server TEXT NOT NULL,
        hostname TEXT NOT NULL, mod TEXT NOT NULL, mode TEXT NOT NULL, gametype INTEGER NOT NULL, map TEXT NOT NULL,
        start INTEGER NOT NULL, first INTEGER NOT NULL, seconds INTEGER NOT NULL, trust TEXT NOT NULL, rows TEXT NOT NULL,
        reports INTEGER NOT NULL DEFAULT 0, agreed INTEGER NOT NULL DEFAULT 0);
      CREATE INDEX IF NOT EXISTS matches_key ON matches(key, first);
      CREATE TABLE IF NOT EXISTS reports(match INTEGER NOT NULL, pid TEXT NOT NULL, rid TEXT NOT NULL, at INTEGER NOT NULL,
        report TEXT NOT NULL, sig TEXT NOT NULL, agreed INTEGER NOT NULL DEFAULT 0, counted INTEGER NOT NULL DEFAULT 0,
        xp INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(match, pid));
      CREATE INDEX IF NOT EXISTS reports_pid ON reports(pid, at DESC);
      CREATE UNIQUE INDEX IF NOT EXISTS reports_rid ON reports(pid, rid);
      CREATE TABLE IF NOT EXISTS careers(pid TEXT PRIMARY KEY, xp INTEGER NOT NULL DEFAULT 0, matches INTEGER NOT NULL DEFAULT 0,
        wins INTEGER NOT NULL DEFAULT 0, frags INTEGER NOT NULL DEFAULT 0, deaths INTEGER NOT NULL DEFAULT 0,
        seconds INTEGER NOT NULL DEFAULT 0, updated INTEGER NOT NULL);
      CREATE INDEX IF NOT EXISTS careers_xp ON careers(xp DESC);
      CREATE TABLE IF NOT EXISTS career_modes(pid TEXT NOT NULL, mod TEXT NOT NULL, mode TEXT NOT NULL, matches INTEGER NOT NULL DEFAULT 0,
        wins INTEGER NOT NULL DEFAULT 0, frags INTEGER NOT NULL DEFAULT 0, deaths INTEGER NOT NULL DEFAULT 0,
        seconds INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(pid, mod, mode));
      CREATE TABLE IF NOT EXISTS career_weapons(pid TEXT NOT NULL, weapon TEXT NOT NULL, frags INTEGER NOT NULL DEFAULT 0,
        deaths INTEGER NOT NULL DEFAULT 0, shots INTEGER NOT NULL DEFAULT 0, hits INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(pid, weapon));
      CREATE TABLE IF NOT EXISTS career_maps(pid TEXT NOT NULL, map TEXT NOT NULL, matches INTEGER NOT NULL DEFAULT 0,
        wins INTEGER NOT NULL DEFAULT 0, frags INTEGER NOT NULL DEFAULT 0, deaths INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(pid, map));
    `);
  }

  // ------------------------------------------------------------------ reports

  /** One player's signed report (the Hub has checked `sig` against their key): stored, and the match decided again. */
  report(pid: string, text: string, sig: string): Stored {
    let raw: unknown;
    try {
      raw = JSON.parse(text);
    } catch {
      return { ok: false, rid: '', reason: 'The report is not JSON.' };
    }
    const r = cleanReport(raw);
    if (typeof r === 'string') return { ok: false, rid: ridOf(raw), reason: r };
    const now = Date.now();
    if (r.ended < now - MAX_AGE_MS || r.ended > now + 5 * 60_000) return { ok: false, rid: r.rid, reason: 'The report is too old.' };
    // the same report again (the game didn't hear the answer): the same answer
    const again = this.one<{ match: number }>('SELECT match FROM reports WHERE pid = ? AND rid = ?', pid, r.rid);
    if (again) return this.answer(again.match, r.rid, false, []);
    const key = r.match_id ? `id:${r.match_id}` : r.server === 'local' ? `local:${pid}:${r.rid}` : `${r.server}|${r.map}|${r.serverid}|${r.start}`;
    let match = this.one<{ id: number }>('SELECT id FROM matches WHERE key = ? AND first > ? ORDER BY first DESC LIMIT 1', key, now - KEY_WINDOW_MS)?.id;
    if (match === undefined) {
      match = this.one<{ id: number }>(
        `INSERT INTO matches(key, server, hostname, mod, mode, gametype, map, start, first, seconds, trust, rows)
         VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 'self', ?) RETURNING id`,
        key, r.server, r.hostname, r.mod, r.mode, r.gametype, r.map, r.start, now, r.seconds, JSON.stringify(r.rows))!.id;
    } else if (this.one('SELECT 1 AS x FROM reports WHERE match = ? AND pid = ?', match, pid)) {
      // a second report of the same match from the same player: the first counts
      return this.answer(match, r.rid, false, []);
    }
    this.sql.exec('INSERT INTO reports(match, pid, rid, at, report, sig) VALUES (?, ?, ?, ?, ?, ?)', match, pid, r.rid, now, JSON.stringify(r), sig);
    return this.resolve(match, r.rid);
  }

  /** Decides the match from all its reports (decide()); counts it for every agreeing reporter once it's Corroborated. */
  private resolve(match: number, rid: string): Stored {
    const m = this.one<{ trust: Trust; mode: string; mod: string; map: string; seconds: number }>(
      'SELECT trust, mode, mod, map, seconds FROM matches WHERE id = ?', match)!;
    const rows = this.rows<{ pid: string; report: string; counted: number }>('SELECT pid, report, counted FROM reports WHERE match = ?', match);
    const cleanBy: Record<string, Clean> = {};
    const reports: Record<string, Report> = {};
    for (const x of rows) {
      const c = JSON.parse(x.report) as Clean;
      cleanBy[x.pid] = c;
      reports[x.pid] = { me: c.me, rows: c.rows, left: c.left, peers: {} };
    }
    const verdict = decide(Object.keys(reports), '', reports, m.mode === 'duel', true);
    let trust: Trust = m.trust;
    let agreed: string[] = [];
    let decided = rows.length ? cleanBy[rows[0].pid].rows : [];
    if (verdict !== 'wait' && verdict.ok) {
      const want = canonical(verdict.rows);
      agreed = Object.keys(reports).filter((p) => !reports[p].left && canonical(reports[p].rows) === want);
      decided = cleanBy[agreed[0]]?.rows ?? decided;
      if (agreed.length >= 2 && trust !== 'verified') trust = 'corroborated';
    } else if (rows.length >= 2 && trust === 'self') {
      trust = 'disputed';
    }
    const changed = trust !== m.trust;
    this.sql.exec('UPDATE matches SET trust = ?, rows = ?, reports = ?, agreed = ? WHERE id = ?',
      trust, JSON.stringify(decided), rows.length, agreed.length, match);
    for (const p of agreed) this.sql.exec('UPDATE reports SET agreed = 1 WHERE match = ? AND pid = ?', match, p);
    const counted: string[] = [];
    if (trust === 'corroborated' || trust === 'verified') {
      for (const x of rows) {
        if (x.counted || !agreed.includes(x.pid)) continue;
        this.count(x.pid, match, cleanBy[x.pid], decided, m);
        counted.push(x.pid);
      }
    }
    return this.answer(match, rid, changed, counted);
  }

  /** Adds one counted match to a player's careers: totals, the mode's, the weapons', the map's. */
  private count(pid: string, match: number, r: Clean, decided: Clean['rows'], m: { mode: string; mod: string; map: string; seconds: number }) {
    const mine = decided.find((x) => x.client === r.me);
    const score = Math.max(mine?.score ?? 0, 0);
    const win = wonOn(decided, r.me, m.mode, r.gametype) ? 1 : 0;
    const xp = XP_FINISHED + XP_FRAG * score + XP_WIN * win;
    const deaths = r.you.deaths;
    const now = Date.now();
    this.sql.exec(
      `INSERT INTO careers(pid, xp, matches, wins, frags, deaths, seconds, updated) VALUES (?, ?, 1, ?, ?, ?, ?, ?)
       ON CONFLICT(pid) DO UPDATE SET xp = xp + excluded.xp, matches = matches + 1, wins = wins + excluded.wins, frags = frags + excluded.frags,
       deaths = deaths + excluded.deaths, seconds = seconds + excluded.seconds, updated = excluded.updated`,
      pid, xp, win, score, deaths, m.seconds, now);
    this.sql.exec(
      `INSERT INTO career_modes(pid, mod, mode, matches, wins, frags, deaths, seconds) VALUES (?, ?, ?, 1, ?, ?, ?, ?)
       ON CONFLICT(pid, mod, mode) DO UPDATE SET matches = matches + 1, wins = wins + excluded.wins, frags = frags + excluded.frags,
       deaths = deaths + excluded.deaths, seconds = seconds + excluded.seconds`,
      pid, m.mod, m.mode, win, score, deaths, m.seconds);
    this.sql.exec(
      `INSERT INTO career_maps(pid, map, matches, wins, frags, deaths) VALUES (?, ?, 1, ?, ?, ?)
       ON CONFLICT(pid, map) DO UPDATE SET matches = matches + 1, wins = wins + excluded.wins, frags = frags + excluded.frags,
       deaths = deaths + excluded.deaths`,
      pid, m.map, win, score, deaths);
    const y = r.you;
    for (const w of new Set([...Object.keys(y.frags_weapon), ...Object.keys(y.deaths_weapon), ...Object.keys(y.shots), ...Object.keys(y.hits)])) {
      this.sql.exec(
        `INSERT INTO career_weapons(pid, weapon, frags, deaths, shots, hits) VALUES (?, ?, ?, ?, ?, ?)
         ON CONFLICT(pid, weapon) DO UPDATE SET frags = frags + excluded.frags, deaths = deaths + excluded.deaths,
         shots = shots + excluded.shots, hits = hits + excluded.hits`,
        pid, w, y.frags_weapon[w] ?? 0, y.deaths_weapon[w] ?? 0, y.shots[w] ?? 0, Math.min(y.hits[w] ?? 0, y.shots[w] ?? Infinity));
    }
    this.sql.exec('UPDATE reports SET counted = 1, xp = ? WHERE match = ? AND pid = ?', xp, match, pid);
  }

  private answer(match: number, rid: string, changed: boolean, counted: string[]): Stored {
    const m = this.one<{ key: string; trust: Trust; reports: number; agreed: number; map: string; mode: string }>(
      'SELECT key, trust, reports, agreed, map, mode FROM matches WHERE id = ?', match)!;
    const reporters = this.rows<{ pid: string }>('SELECT pid FROM reports WHERE match = ? AND agreed = 1', match).map((x) => x.pid);
    return { ok: true, rid, match, key: m.key.startsWith('local:') ? 'local' : m.key, trust: m.trust, reports: m.reports, agreed: m.agreed,
      map: m.map, mode: m.mode, reporters, changed, counted };
  }

  // ------------------------------------------------------------------ reading

  /** A player's own matches, newest first: every report they sent, whatever its trust. */
  history(pid: string, before: number, limit = HISTORY_ROWS): { matches: HistoryRow[]; more: boolean } {
    const n = Math.max(1, Math.min(HISTORY_ROWS, limit));
    const list = this.rows<{ match: number; at: number; report: string; counted: number; xp: number; server: string; hostname: string;
      mod: string; mode: string; map: string; seconds: number; trust: Trust; reports: number; agreed: number; rows: string }>(
      `SELECT r.match, r.at, r.report, r.counted, r.xp, m.server, m.hostname, m.mod, m.mode, m.map, m.seconds, m.trust, m.reports, m.agreed, m.rows
       FROM reports r JOIN matches m ON m.id = r.match WHERE r.pid = ? AND r.at < ? ORDER BY r.at DESC LIMIT ?`,
      pid, before > 0 ? before : Number.MAX_SAFE_INTEGER, n + 1);
    const matches = list.slice(0, n).map((x): HistoryRow => {
      const r = JSON.parse(x.report) as Clean;
      const rows = (JSON.parse(x.rows) as Clean['rows']).filter((row) => row.client >= 0 && row.team !== 3);
      rows.sort((a, b) => b.score - a.score);
      const mine = rows.find((row) => row.client === r.me);
      return {
        match: x.match, at: x.at, server: x.server, hostname: x.hostname, mod: x.mod, mode: x.mode, map: x.map, seconds: x.seconds,
        trust: x.trust, reports: x.reports, agreed: x.agreed, counted: !!x.counted, place: mine ? rows.indexOf(mine) + 1 : r.you.place + 1,
        players: rows.length, score: mine?.score ?? r.you.score, won: r.you.won, frags: r.you.frags, deaths: r.you.deaths, xp: r.you.xp,
        shared_xp: x.xp, rows: rows.slice(0, 8).map((row) => ({ name: row.name, score: row.score, team: row.team, bot: row.bot, you: row.client === r.me })),
      };
    });
    return { matches, more: list.length > n };
  }

  /** A player's shared career: only matches that counted (Corroborated or Verified). */
  career(pid: string): Career {
    const c = this.one<{ xp: number; matches: number; wins: number; frags: number; deaths: number; seconds: number }>(
      'SELECT xp, matches, wins, frags, deaths, seconds FROM careers WHERE pid = ?', pid);
    return {
      xp: c?.xp ?? 0, matches: c?.matches ?? 0, wins: c?.wins ?? 0, frags: c?.frags ?? 0, deaths: c?.deaths ?? 0, seconds: c?.seconds ?? 0,
      modes: this.rows<Career['modes'][number]>('SELECT mod, mode, matches, wins, frags, deaths, seconds FROM career_modes WHERE pid = ? ORDER BY matches DESC', pid),
      weapons: this.rows<Career['weapons'][number]>('SELECT weapon, frags, deaths, shots, hits FROM career_weapons WHERE pid = ? ORDER BY frags DESC', pid),
      maps: this.rows<Career['maps'][number]>('SELECT map, matches, wins, frags, deaths FROM career_maps WHERE pid = ? ORDER BY matches DESC LIMIT 12', pid),
    };
  }

  /**
   * A leaderboard from the counted matches: `level` (shared XP), `wins` or `frags`, all modes or one (`mode`, any mod or
   * `mod`). `among`: only these players (a friends board), else everyone. Players with nothing counted aren't on it.
   */
  board(board: string, mode: string, mod: string, among: string[] | null, me: string, limit: number):
    { rows: StatsBoardRow[]; total: number; me: (StatsBoardRow & { rank: number }) | null } {
    const col = board === 'wins' ? 'wins' : board === 'frags' ? 'frags' : 'xp';
    const args: SqlStorageValue[] = [];
    let from: string;
    if (mode && col !== 'xp') {
      // one mode: its own counts, summed over mods unless one is named
      from = `(SELECT pid, SUM(matches) AS matches, SUM(wins) AS wins, SUM(frags) AS frags, 0 AS xp FROM career_modes
        WHERE mode = ?${mod ? ' AND mod = ?' : ''} GROUP BY pid) c`;
      args.push(mode);
      if (mod) args.push(mod);
    } else {
      from = 'careers c';
    }
    const where = [`c.${col} > 0`];
    if (among) {
      where.push('c.pid IN (SELECT value FROM json_each(?))');
      args.push(JSON.stringify(among));
    }
    const q = `FROM ${from} WHERE ${where.join(' AND ')}`;
    const cols = `c.pid AS pid, c.${col} AS value, c.xp AS xp, c.matches AS matches, c.wins AS wins, c.frags AS frags`;
    const rows = this.rows<StatsBoardRow>(`SELECT ${cols} ${q} ORDER BY value DESC, c.matches DESC, c.pid LIMIT ?`, ...args, limit);
    const total = Number(this.one<{ n: number }>(`SELECT COUNT(*) AS n ${q}`, ...args)?.n ?? 0);
    let mine: (StatsBoardRow & { rank: number }) | null = null;
    const at = rows.findIndex((r) => r.pid === me);
    if (at >= 0) {
      mine = { ...rows[at], rank: at + 1 };
    } else {
      const r = this.one<StatsBoardRow>(`SELECT ${cols} ${q} AND c.pid = ?`, ...args, me);
      if (r) mine = { ...r, rank: Number(this.one<{ n: number }>(`SELECT COUNT(*) AS n ${q} AND c.${col} > ?`, ...args, r.value)?.n ?? 0) + 1 };
    }
    return { rows, total, me: mine };
  }

  /** The admin's purge: the players' reports, careers and boards go; matches keep the others' reports. */
  purge(ids: string[]): void {
    for (const pid of ids) {
      for (const t of ['reports', 'careers', 'career_modes', 'career_weapons', 'career_maps']) this.sql.exec(`DELETE FROM ${t} WHERE pid = ?`, pid);
    }
    this.sql.exec('DELETE FROM matches WHERE id NOT IN (SELECT match FROM reports)');
  }

  // ------------------------------------------------------------------ storage helpers

  private rows<T>(q: string, ...args: SqlStorageValue[]): T[] {
    return this.sql.exec(q, ...args).toArray() as T[];
  }

  private one<T = Record<string, SqlStorageValue>>(q: string, ...args: SqlStorageValue[]): T | null {
    return (this.rows<T>(q, ...args)[0] ?? null) as T | null;
  }
}

/** Did the client `me` win on this scoreboard? Team games: the team rows (-1 red, -2 blue), else the members' scores;
 *  otherwise first place alone, with someone to beat. */
export function wonOn(rows: Row[], me: number, mode: string, gametype: number): boolean {
  const mine = rows.find((r) => r.client === me);
  if (!mine) return false;
  if (MODES[mode]?.team || gametype >= 3) {
    const teamScore = (t: number) => {
      const row = rows.find((r) => r.client === -t);
      return row ? row.score : rows.filter((r) => r.client >= 0 && r.team === t).reduce((a, r) => a + r.score, 0);
    };
    if (mine.team !== 1 && mine.team !== 2) return false;
    return teamScore(mine.team) > teamScore(3 - mine.team);
  }
  const players = rows.filter((r) => r.client >= 0 && r.team !== 3);
  return players.length > 1 && players.every((r) => r === mine || r.score < mine.score);
}

function ridOf(v: unknown): string {
  const rid = (v as { rid?: unknown } | null)?.rid;
  return typeof rid === 'string' && /^[0-9a-f]{8,32}$/.test(rid) ? rid : '';
}

const int = (v: unknown, lo: number, hi: number) => Math.max(lo, Math.min(hi, Math.floor(Number(v) || 0)));
const text = (v: unknown, max: number) => String(v ?? '').replace(/[\u0000-\u001f\u007f]/g, '').trim().slice(0, max);
const token = (v: unknown, max: number) => {
  const s = String(v ?? '').toLowerCase();
  return new RegExp(`^[a-z0-9_.\\-]{1,${max}}$`).test(s) ? s : '';
};

/** A {name: count} of short ids (weapons, items, medals), 32 at most. */
function counts(v: unknown): Record<string, number> {
  const out: Record<string, number> = {};
  if (!v || typeof v !== 'object') return out;
  for (const [k, x] of Object.entries(v as Record<string, unknown>).slice(0, 32)) {
    const n = int(x, 0, 1_000_000);
    if (/^[a-z0-9_]{1,16}$/.test(k) && n > 0) out[k] = n;
  }
  return out;
}

/** The report in a fixed shape with every field bounded, or why it can't be taken. */
function cleanReport(v: unknown): Clean | string {
  if (!v || typeof v !== 'object') return 'The report is empty.';
  const o = v as Record<string, unknown>;
  if (o.v !== 1) return 'This report format is not known.';
  const rid = ridOf(o);
  if (!rid) return 'The report has no id.';
  const server = String(o.server ?? '');
  if (!/^[\w.:\-[\]]{1,64}$/.test(server)) return 'The report names no server.';
  const map = token(o.map, 64);
  if (!map) return 'The report names no map.';
  const rows = (Array.isArray(o.rows) ? o.rows : []).slice(0, 66).map((x) => {
    const r = (x ?? {}) as Record<string, unknown>;
    return { client: int(r.client, -2, 63), score: int(r.score, -100_000, 100_000), team: int(r.team, 0, 3), name: text(r.name, 36), bot: r.bot === true };
  });
  const me = int(o.me, 0, 63);
  if (!rows.some((r) => r.client === me && r.client >= 0)) return 'The reporter is not on the scoreboard.';
  if (new Set(rows.map((r) => r.client)).size !== rows.length) return 'The scoreboard lists a player twice.';
  const y = (o.you && typeof o.you === 'object' ? o.you : {}) as Record<string, unknown>;
  const matchId = String(o.match_id ?? '');
  return {
    v: 1, rid, match_id: /^[\w-]{1,64}$/.test(matchId) ? matchId : '', server, hostname: text(o.hostname, 64), mod: token(o.mod, 32) || 'baseq3',
    gametype: int(o.gametype, -1, 32), mode: token(o.mode, 16) || 'ffa', map, pure: o.pure === true || o.pure === 1,
    serverid: int(o.serverid, -(2 ** 31), 2 ** 31), start: int(o.start, 0, 2 ** 31), seconds: int(o.seconds, 0, 86_400),
    ended: int(o.ended, 0, 2 ** 52), me, left: o.left === true, rows,
    you: {
      place: int(y.place, 0, 64), won: y.won === true, score: int(y.score, -100_000, 100_000), frags: int(y.frags, 0, 100_000),
      deaths: int(y.deaths, 0, 100_000), xp: int(y.xp, 0, 1_000_000), rail_streak: int(y.rail_streak, 0, 10_000),
      distance: int(y.distance, 0, 1e9), speed: int(y.speed, 0, 100_000), frags_weapon: counts(y.frags_weapon),
      deaths_weapon: counts(y.deaths_weapon), shots: counts(y.shots), hits: counts(y.hits), damage: counts(y.damage), pickups: counts(y.pickups),
      medals: counts(y.medals),
    },
  };
}
