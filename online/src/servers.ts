// The community server registry and the matchmaker's destination rules (ENGINE_PLAN.md P8b steps 2, 3 and 4) as pure
// functions. Workers can't send UDP, so the clients do every probe: each Oz game's Server Browser sweep sends the hub a
// digest of what it saw (servers_seen), and the Hub keeps the registry in memory with a 5-minute time to live, placing
// each server in a region from the players' own pings to it. When a queue forms a group, the Hub shortlists up to five
// servers from here (Verified first, then community servers passing every rule), the members ping them (mm_ping /
// mm_pings: a getinfo for the ping, a getstatus for the server's own rules), and `pick` names the winner: the lowest
// worst-member ping under the limit. Nothing here touches sockets or storage; online/test/matchmaking_unit.mjs checks it.
import type { QEntry } from './ranked';

/** A server as the registry keeps it, from the clients' digests. */
export interface Seen {
  addr: string;
  mod: string;
  gametype: number;
  map: string;
  clients: number;
  humans: number;
  /** public slots (getinfo's sv_maxclients already leaves the private ones out on ioquake3 and Quake3e) */
  max: number;
  pure: boolean;
  needpass: boolean;
  version: string;
  hostname: string;
  /** when the newest digest named it */
  seen: number;
  /** the lowest ping a player in each region measured, and when (REGISTRY_TTL_MS old entries are dropped) */
  pings: Record<string, { ping: number; at: number }>;
  /** what a getstatus (an mm_pings answer, a card) said about the matchmaker: false once a server said oz_mm 0 */
  mm?: boolean;
}

/** A Verified server (an agent alive, or one of Oz's own): what its agent last said about it. */
export interface Verified {
  addr: string;
  /** Oz's own dedicated servers (decision 23): duels go there first */
  own: boolean;
  /** the agent's own getstatus of its server, as it sends it with each heartbeat (AgentInfo), or nothing yet */
  info?: AgentInfo;
  seen: number;
}

/** A server's own rules, as a getstatus answers them (the engine's mm_pings `info`, the agent's heartbeat `info`). */
export interface AgentInfo {
  game?: string;
  gametype?: number;
  map?: string;
  hostname?: string;
  needpass?: boolean;
  /** serverinfo oz_mm: false opts the server out */
  mm?: boolean;
  /** sv_maxclients and sv_privateClients */
  max?: number;
  priv?: number;
  /** players on it (the status lines), bots included */
  players?: number;
  /** Oz's game: g_ruleset ("classic" or "ql"); id's game sends none */
  ruleset?: string;
}

export const REGISTRY_TTL_MS = 5 * 60_000;
/** An agent silent for this long is no longer Verified for the matchmaker. */
export const AGENT_ALIVE_MS = 3 * 60_000;
/** The etiquette: one matched group per community server per 10 minutes. */
export const PLACEMENT_GAP_MS = 10 * 60_000;
export const SHORTLIST = 5;
/** The winner's worst member ping, ms; a group that waited past the region widening (Auto) takes up to PING_LIMIT_WIDE. */
export const PING_LIMIT_MS = 80;
export const PING_LIMIT_WIDE_MS = 150;
/** Members answer a shortlist within this long. */
export const PING_ANSWER_MS = 2000;

/** The queues' gametypes on id's game (baseq3's numbering, which an Oz server shares). */
export const MODE_GAMETYPE: Record<string, number> = { ffa: 0, duel: 1, tdm: 3, ctf: 4 };

const clampInt = (v: unknown, lo: number, hi: number) => Math.max(lo, Math.min(hi, Math.floor(Number(v) || 0)));
const ADDR = /^(\d{1,3}(\.\d{1,3}){3}|\[[0-9a-f:.]+\]):\d{1,5}$/i;

export function cleanAddr(v: unknown): string {
  const s = String(v ?? '').trim().toLowerCase();
  return ADDR.test(s) && s.length <= 64 ? s : '';
}

/**
 * One digest entry as the engine sends it (oz_mm.c Digest): [addr, mod, gametype, map, clients, humans, max, pure,
 * needpass, version, ping, hostname]. Returns the Seen it describes for the sender's region, or null.
 */
export function cleanSeen(v: unknown, region: string, now: number): Seen | null {
  if (!Array.isArray(v) || v.length < 11) return null;
  const addr = cleanAddr(v[0]);
  if (!addr) return null;
  const token = (x: unknown, max: number) => {
    const s = String(x ?? '').toLowerCase();
    return /^[a-z0-9_.\-]{1,64}$/.test(s) ? s.slice(0, max) : '';
  };
  const ping = clampInt(v[10], 0, 2000);
  return {
    addr, mod: token(v[1], 32) || 'baseq3', gametype: clampInt(v[2], -1, 32), map: token(v[3], 64), clients: clampInt(v[4], 0, 64),
    humans: clampInt(v[5], -1, 64), max: clampInt(v[6], 0, 64), pure: v[7] === 1 || v[7] === true, needpass: v[8] === 1 || v[8] === true,
    version: String(v[9] ?? '').replace(/[\u0000-\u001f\u007f]/g, '').slice(0, 24), hostname: String(v[11] ?? '').replace(/[\u0000-\u001f\u007f]/g, '').slice(0, 40),
    seen: now, pings: ping > 0 ? { [region]: { ping, at: now } } : {},
  };
}

/** Folds a digest entry into the registry: the newest info wins, the pings per region keep their lowest. */
export function merge(registry: Map<string, Seen>, s: Seen): void {
  const old = registry.get(s.addr);
  if (!old) {
    registry.set(s.addr, s);
    return;
  }
  const pings = { ...old.pings };
  for (const [region, p] of Object.entries(s.pings)) {
    const was = pings[region];
    pings[region] = !was || was.at < s.seen - REGISTRY_TTL_MS || p.ping <= was.ping ? p : was;
  }
  registry.set(s.addr, { ...old, ...s, pings, mm: s.mm ?? old.mm });
}

/** Drops what no digest named within the time to live, and the pings older than it. */
export function expire(registry: Map<string, Seen>, now: number): void {
  for (const [addr, s] of registry) {
    if (s.seen < now - REGISTRY_TTL_MS) {
      registry.delete(addr);
      continue;
    }
    for (const r of Object.keys(s.pings)) if (s.pings[r].at < now - REGISTRY_TTL_MS) delete s.pings[r];
  }
}

/** The region a server is placed in: the one whose players reach it fastest; "" with no ping at all. */
export function regionOf(s: Seen): string {
  let best = '';
  let ping = Infinity;
  for (const [r, p] of Object.entries(s.pings)) if (p.ping < ping) [best, ping] = [r, p.ping];
  return best;
}

export interface Rules {
  blocked: Set<string>;
  /** addr -> when the matchmaker last placed a group there */
  placed: Map<string, number>;
}

/** Whether a server's game fits a queue: id's baseq3 (the Classic ruleset pool) or an Oz server; mods have their own queues later. */
export function modFits(mod: string): boolean {
  return mod === 'baseq3' || mod === 'oz' || mod === '';
}

/**
 * The matchmaker's shortlist for a group (ENGINE_PLAN.md P8b step 3): Verified servers first (for a duel Oz's own first),
 * then community servers that pass every rule (the queue's mode on id's rules, no password, room for the whole group in
 * public slots, not opted out, not blocklisted, no group placed there in the last 10 minutes; ranked only empty ones),
 * duels preferring empty servers and the team and free-for-all queues populated ones, the nearer first. Up to SHORTLIST
 * addresses, the members' pings decide among them (pick).
 */
export function shortlist(registry: Map<string, Seen>, verified: Verified[], rules: Rules, group: QEntry[], mode: string,
  ranked: boolean, now: number): string[] {
  const gametype = MODE_GAMETYPE[mode];
  if (gametype === undefined) return [];
  const size = group.length;
  const regions = [...new Set(group.map((g) => g.region))];
  const allowed = (addr: string) => !rules.blocked.has(addr) && (rules.placed.get(addr) ?? 0) < now - PLACEMENT_GAP_MS;
  // a server's worst ping guess over the group's regions: the registry's lowest from each, 120 where none was measured
  const guess = (s: Seen | undefined) => Math.max(...regions.map((r) => s?.pings[r]?.ping ?? 120));
  type Cand = { addr: string; verified: boolean; own: boolean; humans: number; guess: number; empty: boolean };
  const out: Cand[] = [];
  for (const v of verified) {
    if (v.seen < now - AGENT_ALIVE_MS || !allowed(v.addr)) continue;
    const s = registry.get(v.addr);
    const i = v.info ?? {};
    const game = i.game ?? s?.mod ?? 'baseq3';
    const gt = i.gametype ?? s?.gametype;
    const players = i.players ?? s?.clients ?? 0;
    const room = (i.max ?? s?.max ?? 0) - (i.priv ?? 0) - players;
    if (!modFits(game) || gt !== gametype || i.needpass || s?.needpass || i.mm === false || s?.mm === false || room < size) continue;
    if (ranked && players > 0) continue;
    if (i.ruleset && i.ruleset !== 'classic') continue;
    out.push({ addr: v.addr, verified: true, own: v.own, humans: s?.humans ?? players, guess: guess(s), empty: players === 0 });
  }
  for (const s of registry.values()) {
    if (out.some((c) => c.addr === s.addr) || !allowed(s.addr)) continue;
    if (!modFits(s.mod) || s.gametype !== gametype || s.needpass || s.mm === false || s.max - s.clients < size) continue;
    if (ranked && s.clients > 0) continue;
    out.push({ addr: s.addr, verified: false, own: false, humans: Math.max(s.humans, 0), guess: guess(s), empty: s.clients === 0 });
  }
  const duel = mode === 'duel';
  out.sort((a, b) => Number(b.verified) - Number(a.verified) || (duel ? Number(b.own) - Number(a.own) : 0)
    || (duel ? Number(b.empty) - Number(a.empty) : b.humans - a.humans) || a.guess - b.guess || a.addr.localeCompare(b.addr));
  return out.slice(0, SHORTLIST).map((c) => c.addr);
}

/** One member's answer for one shortlisted server (mm_pings): the ping it measured, -1 for no answer, and the getstatus's rules. */
export interface PingAnswer {
  ping: number;
  info?: AgentInfo;
}

/** Cleans an mm_pings entry from a game: {addr, ping, info?}. */
export function cleanPing(v: unknown): { addr: string; answer: PingAnswer } | null {
  const o = (v && typeof v === 'object' ? v : {}) as Record<string, unknown>;
  const addr = cleanAddr(o.addr);
  if (!addr) return null;
  const answer: PingAnswer = { ping: clampInt(o.ping, -1, 2000) };
  if (o.info && typeof o.info === 'object') answer.info = cleanInfo(o.info);
  return { addr, answer };
}

/** A server's rules as a game or agent read them from its getstatus. */
export function cleanInfo(v: unknown): AgentInfo {
  const o = (v && typeof v === 'object' ? v : {}) as Record<string, unknown>;
  const out: AgentInfo = {};
  const token = (x: unknown, max: number) => {
    const s = String(x ?? '').toLowerCase();
    return /^[a-z0-9_.\-]{1,64}$/.test(s) ? s.slice(0, max) : '';
  };
  if (o.game !== undefined) out.game = token(o.game, 32) || 'baseq3';
  if (o.gametype !== undefined) out.gametype = clampInt(o.gametype, -1, 32);
  if (o.map !== undefined) out.map = token(o.map, 64);
  if (o.hostname !== undefined) out.hostname = String(o.hostname).replace(/[\u0000-\u001f\u007f]/g, '').slice(0, 40);
  if (o.needpass !== undefined) out.needpass = o.needpass === true || o.needpass === 1;
  if (o.mm !== undefined) out.mm = !(o.mm === false || o.mm === 0);
  if (o.max !== undefined) out.max = clampInt(o.max, 0, 64);
  if (o.priv !== undefined) out.priv = clampInt(o.priv, 0, 64);
  if (o.players !== undefined) out.players = clampInt(o.players, 0, 64);
  if (o.ruleset !== undefined) out.ruleset = token(o.ruleset, 16);
  return out;
}

/** Whether a server's own rules (its getstatus) let a group of `size` in: the matchmaker's rules checked against the live server. */
export function infoAllows(i: AgentInfo, size: number, ranked: boolean, gametype: number): string {
  if (i.needpass) return 'password';
  if (i.mm === false) return 'opted out';
  if (i.game !== undefined && !modFits(i.game)) return 'another game';
  if (i.gametype !== undefined && i.gametype !== gametype) return 'another mode';
  if (i.ruleset && i.ruleset !== 'classic') return 'another ruleset';
  if (i.max !== undefined && i.max - (i.priv ?? 0) - (i.players ?? 0) < size) return 'full';
  if (ranked && (i.players ?? 0) > 0) return 'not empty';
  return '';
}

/**
 * The winner among the shortlist from every member's pings (mm_pings): a server every member reached, passing its own
 * rules as the freshest getstatus answer has them, with the lowest worst-member ping under `limit`. Null when none.
 */
export function pick(shortlisted: string[], answers: Record<string, Record<string, PingAnswer>>, members: string[], size: number,
  ranked: boolean, gametype: number, limit: number): { addr: string; worst: number } | null {
  let best: { addr: string; worst: number } | null = null;
  for (const addr of shortlisted) {
    let worst = 0;
    let info: AgentInfo | undefined;
    let ok = true;
    for (const pid of members) {
      const a = answers[pid]?.[addr];
      if (!a || a.ping < 0) {
        ok = false;
        break;
      }
      worst = Math.max(worst, a.ping);
      if (a.info) info = a.info;
    }
    if (!ok || worst > limit || (info && infoAllows(info, size, ranked, gametype))) continue;
    if (!best || worst < best.worst) best = { addr, worst };
  }
  return best;
}

/** The ping limit a group gets: PING_LIMIT_MS, or the wide one once every member is on Auto and has waited past the region widening. */
export function pingLimit(group: QEntry[], now: number, widenMs: number): number {
  return group.every((g) => g.auto && now - g.since >= widenMs) ? PING_LIMIT_WIDE_MS : PING_LIMIT_MS;
}
