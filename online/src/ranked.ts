// Matchmaking and ranked rules as pure functions (MULTIPLAYER_PLAN.md M6, PLAN.md "Ranked (Elo)" and "Trust"): who
// plays whom, how a result moves ratings, when players' reports agree. The Hub (hub.ts) owns the sockets and the
// storage and calls these; online/test/ranked_unit.mjs checks them without a server.

export interface ModeInfo {
  size: number;
  team: boolean;
  maps: string[];
  /** in the ranked queues (the plan's headline modes first; Clan Arena and the rest wait for the player count) */
  ranked: boolean;
}

/**
 * The official map pools. Ranked duel is the duel pool (DUEL_MAPS_PLAN.md, godot/game/duel_pool.json): only the entries
 * built so far; add each map's library id here as it ships. The hub picks one at random for now (alternating bans come later).
 */
export const MODES: Record<string, ModeInfo> = {
  duel: { size: 2, team: false, ranked: true, maps: ['pro-q3dm6', 'pro-q3dm13', 'pro-q3tourney4'] },
  ffa: { size: 4, team: false, ranked: false, maps: ['q3dm1', 'q3dm6', 'q3dm7', 'q3dm11', 'q3dm17'] },
  tdm: { size: 4, team: true, ranked: true, maps: ['q3dm1', 'q3dm6', 'q3dm7', 'q3dm11', 'q3dm17'] },
  ctf: { size: 6, team: true, ranked: true, maps: ['q3ctf1', 'q3ctf2', 'q3ctf3', 'q3ctf4'] },
};

/** Ranked opens at this level (q3_profile.gd RANKED_LEVEL); the hub checks it against the level it computes from XP. */
export const RANKED_LEVEL = 30;
export const START_RATING = 1000;
export const RATING_FLOOR = 100;
export const PLACEMENTS = 10;

// ---------------------------------------------------------------- queueing

export interface QEntry {
  pid: string;
  modes: string[];
  ranked: boolean;
  /** the region the player asked for; auto means their own, widening to neighbors after a wait */
  region: string;
  auto: boolean;
  since: number;
  /** round trip to the hub in ms, as the game measured it: the host is the best connected of the group */
  rtt: number;
  /** the rating in each queue (ranked only) */
  ratings: Record<string, number>;
}

/** Regions a wait of REGION_WIDEN_MS opens to (PLAN.md "Regions"). */
export const NEIGHBORS: Record<string, string[]> = {
  na_east: ['na_west', 'sa', 'eu'],
  na_west: ['na_east', 'asia', 'oce'],
  eu: ['na_east', 'meaf', 'asia'],
  sa: ['na_east'],
  asia: ['oce', 'meaf', 'na_west', 'eu'],
  oce: ['asia', 'na_west'],
  meaf: ['eu', 'asia'],
};
export const REGION_WIDEN_MS = 60_000;
/** trac3r's numbers: the window starts at +-150 and widens 25 a second; after 10 s the closest available player is taken. */
export const WINDOW_START = 150;
export const WINDOW_PER_S = 25;
export const CLOSEST_AFTER_MS = 10_000;

function regionOk(a: QEntry, b: QEntry, now: number): boolean {
  if (a.region === b.region) return true;
  if (!a.auto || !b.auto) return false; // a named region stays strict
  const waited = Math.min(now - a.since, now - b.since); // the shorter wait: both have to be willing
  return waited >= REGION_WIDEN_MS && (NEIGHBORS[a.region] ?? []).includes(b.region);
}

/**
 * Picks the players for one match of `mode` around the oldest entry in `pool` (every entry queued for it), or null when
 * there aren't enough yet. Ranked entries are matched by rating within the widening window; after 10 s the closest ones
 * are taken whatever the window. Casual queues just take the longest waiting.
 */
export function formGroup(pool: QEntry[], mode: string, now: number): QEntry[] | null {
  const info = MODES[mode];
  if (!info || pool.length < info.size) return null;
  const sorted = [...pool].sort((a, b) => a.since - b.since);
  for (const anchor of sorted) {
    const waited = now - anchor.since;
    const rating = (e: QEntry) => e.ratings[mode] ?? START_RATING;
    let cands = sorted.filter((e) => e !== anchor && regionOk(anchor, e, now));
    if (anchor.ranked) {
      cands.sort((a, b) => Math.abs(rating(a) - rating(anchor)) - Math.abs(rating(b) - rating(anchor)));
      if (waited < CLOSEST_AFTER_MS) {
        const window = WINDOW_START + (WINDOW_PER_S * waited) / 1000;
        cands = cands.filter((e) => Math.abs(rating(e) - rating(anchor)) <= window);
      }
    }
    if (cands.length < info.size - 1) continue;
    const group = [anchor, ...cands.slice(0, info.size - 1)];
    // every pair must be able to play (regions, and for a team match the spread inside the window)
    if (group.every((a, i) => group.every((b, j) => i === j || regionOk(a, b, now)))) return group;
  }
  return null;
}

/** Splits a team match's players into red and blue by rating, snake order (1, 2 / 3, 4 ...), so the teams come out even. */
export function assignTeams(group: QEntry[], mode: string): number[] {
  const rating = (e: QEntry) => e.ratings[mode] ?? START_RATING;
  const order = group.map((e, i) => i).sort((a, b) => rating(group[b]) - rating(group[a]));
  const teams = new Array(group.length).fill(0);
  order.forEach((idx, n) => {
    const round = Math.floor(n / 2);
    teams[idx] = (n + round) % 2 === 0 ? 1 : 2;
  });
  return teams;
}

/** The host of a ranked match: the best connected player (the lowest round trip to the hub), the plan's "best connection". */
export function electHost(group: QEntry[]): QEntry {
  return [...group].sort((a, b) => a.rtt - b.rtt || a.since - b.since)[0];
}

// ---------------------------------------------------------------- ratings

export function expectedScore(me: number, opp: number): number {
  return 1 / (1 + Math.pow(10, (opp - me) / 400));
}

/** K = 40 for the first 30 ranked games, 24 after, 16 in the top tier (2200+). */
export function kFactor(rated: number, rating: number): number {
  if (rating >= 2200) return 16;
  return rated < 30 ? 40 : 24;
}

export interface Played {
  pid: string;
  rating: number;
  /** ranked matches played in this queue so far */
  rated: number;
  team: number;
  score: number;
  /** left before the end: a loss, whatever the score */
  left: boolean;
}

/**
 * Rating changes for one finished match. Duel: plain Elo. Free for all: each pair of players is a duel on score, the
 * pairs averaged. Team games: each team's average rating against the other's, every member moving the same amount.
 * Leaving is a loss. Zero-sum for equal K; the floor is 100.
 */
export function eloDeltas(mode: string, players: Played[]): Record<string, number> {
  const out: Record<string, number> = {};
  const team = MODES[mode]?.team ?? false;
  if (team) {
    const sides = [1, 2].map((t) => players.filter((p) => p.team === t));
    if (sides.some((s) => !s.length)) return Object.fromEntries(players.map((p) => [p.pid, 0]));
    const avg = sides.map((s) => s.reduce((a, p) => a + p.rating, 0) / s.length);
    const score = sides.map((s) => s.reduce((a, p) => a + p.score, 0));
    const result = score[0] > score[1] ? 1 : score[0] < score[1] ? 0 : 0.5;
    sides.forEach((s, i) => {
      const e = expectedScore(avg[i], avg[1 - i]);
      const got = i === 0 ? result : 1 - result;
      for (const p of s) {
        const adj = p.left ? 0 : got; // a leaver loses whatever the scoreboard says
        out[p.pid] = Math.round(kFactor(p.rated, p.rating) * (adj - e));
      }
    });
  } else {
    for (const p of players) {
      let sum = 0;
      for (const q of players) {
        if (q === p) continue;
        let got = p.score > q.score ? 1 : p.score < q.score ? 0 : 0.5;
        if (p.left && !q.left) got = 0;
        else if (q.left && !p.left) got = 1;
        sum += got - expectedScore(p.rating, q.rating);
      }
      out[p.pid] = Math.round((kFactor(p.rated, p.rating) * sum) / Math.max(players.length - 1, 1));
    }
  }
  for (const p of players) if (p.rating + out[p.pid] < RATING_FLOOR) out[p.pid] = RATING_FLOOR - p.rating;
  return out;
}

/** Did this player win? Duel and free for all: first place on score (a tie for first is not a win); team games: the team. */
export function won(mode: string, me: Played, players: Played[]): boolean {
  if (me.left) return false;
  if (MODES[mode]?.team) {
    const mine = players.filter((p) => p.team === me.team).reduce((a, p) => a + p.score, 0);
    const theirs = players.filter((p) => p.team !== me.team).reduce((a, p) => a + p.score, 0);
    return mine > theirs;
  }
  return players.every((p) => p === me || p.score < me.score || p.left);
}

/** The season of a time: the calendar quarter, "2026-Q4". */
export function seasonId(t: number): string {
  const d = new Date(t);
  return `${d.getUTCFullYear()}-Q${Math.floor(d.getUTCMonth() / 3) + 1}`;
}

/** At a season's end the rating eases a quarter of the way back toward 1000. */
export function easeToStart(rating: number): number {
  return Math.round(START_RATING + (rating - START_RATING) * 0.75);
}

// ---------------------------------------------------------------- results and agreement

export interface Row {
  client: number;
  score: number;
  team: number;
}

export interface Report {
  /** the reporter's own client number in the match */
  me: number;
  rows: Row[];
  left: boolean;
  /** the host only: which client number each remote player holds */
  peers: Record<string, string>;
}

/** A scoreboard in one fixed form, so two reports can be compared byte for byte. */
export function canonical(rows: Row[]): string {
  return JSON.stringify([...rows].sort((a, b) => a.client - b.client).map((r) => [r.client, r.score, r.team]));
}

export type Verdict =
  | { ok: true; rows: Row[]; clients: Record<number, string>; hostOverruled: boolean }
  | { ok: false; reason: string; blame: string | null };

/**
 * Decides a match from the players' reports (PLAN.md "Trust": the result counts only when the players' games agree).
 * `players` is everyone in the match with their pid, `host` the host's pid. Reports from players who left don't need to
 * agree on the scoreboard, but their client number counts. A duel needs every player who stayed to report the same
 * scoreboard; a bigger match needs more than half of the non-host players who stayed to. `final` says no more reports
 * are coming: without it, a decision needs every report in (or an agreeing majority).
 */
export function decide(players: string[], host: string, reports: Record<string, Report>, duel: boolean, final: boolean): Verdict | 'wait' {
  const reported = Object.keys(reports);
  const stayed = players.filter((p) => !reports[p]?.left);
  const stayedReports = stayed.filter((p) => reports[p]);
  const nonHost = stayed.filter((p) => p !== host);
  const groups = new Map<string, string[]>();
  for (const p of stayedReports) {
    const key = canonical(reports[p].rows);
    groups.set(key, [...(groups.get(key) ?? []), p]);
  }
  const best = [...groups.entries()].sort((a, b) => b[1].length - a[1].length)[0];
  const allIn = stayed.every((p) => reports[p]);
  const majority = best && best[1].filter((p) => p !== host).length > nonHost.length / 2;
  if (!stayed.length) return { ok: false, reason: 'No one stayed to the end.', blame: null };
  if (duel) {
    if (!allIn) return final ? { ok: false, reason: 'A player never reported the result.', blame: host } : 'wait';
    if (groups.size > 1) return { ok: false, reason: "The players' scoreboards differ.", blame: host };
  } else if (!majority) {
    if (!allIn && !final) return 'wait';
    return { ok: false, reason: "The players' scoreboards don't agree.", blame: host };
  }
  if (!best) return { ok: false, reason: 'No scoreboard was reported.', blame: host };
  // who held which client number: each reporter claims their own, the host also lists the others; they must not clash
  const clients: Record<number, string> = {};
  const claim = (c: number, pid: string): boolean => (clients[c] && clients[c] !== pid ? false : ((clients[c] = pid), true));
  for (const p of reported) if (!claim(reports[p].me, p)) return { ok: false, reason: 'Two players claim the same place in the match.', blame: host };
  for (const [c, pid] of Object.entries(reports[host]?.peers ?? {})) if (!claim(Number(c), pid)) return { ok: false, reason: "The host's player list disagrees with the players'.", blame: host };
  const rows = reports[best[1][0]].rows;
  for (const p of players) if (!Object.values(clients).includes(p)) return { ok: false, reason: 'A player could not be found on the scoreboard.', blame: host };
  const hostOverruled = !!reports[host] && !reports[host].left && canonical(reports[host].rows) !== best[0];
  return { ok: true, rows, clients, hostOverruled };
}
