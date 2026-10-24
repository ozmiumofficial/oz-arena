// The matchmaking and ranked functions (src/ranked.ts) without a server. Node strips the types itself.
//   node test/ranked_unit.mjs
import { MODES, formGroup, assignTeams, electHost, eloDeltas, kFactor, won, seasonId, easeToStart, decide, canonical, expectedScore } from '../src/ranked.ts';

let failed = 0;
const check = (ok, what) => {
  console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
  if (!ok) failed++;
};
const entry = (pid, over = {}) => ({ pid, modes: ['duel'], ranked: true, region: 'na_east', auto: true, since: 0, rtt: 50, ratings: { duel: 1000 }, ...over });

// ---- queueing
let g = formGroup([entry('a'), entry('b', { ratings: { duel: 1100 } })], 'duel', 1000);
check(g && g.length === 2, 'two players within the window are matched');
g = formGroup([entry('a'), entry('b', { ratings: { duel: 1500 } })], 'duel', 1000);
check(g === null, 'a 500 point gap is outside the opening window (150)');
g = formGroup([entry('a'), entry('b', { ratings: { duel: 1500 } })], 'duel', 9000);
check(g === null, 'still outside after 9 s (window 350)');
g = formGroup([entry('a'), entry('b', { ratings: { duel: 1500 } })], 'duel', 10_000);
check(g && g.length === 2, 'after 10 s the closest available player is taken');
g = formGroup([entry('a'), entry('b', { region: 'eu' })], 'duel', 30_000);
check(g === null, 'another region is not matched before 60 s');
g = formGroup([entry('a'), entry('b', { region: 'eu' })], 'duel', 61_000);
check(g && g.length === 2, 'a neighboring region is matched after 60 s (Auto)');
g = formGroup([entry('a', { auto: false }), entry('b', { region: 'eu' })], 'duel', 120_000);
check(g === null, 'a named region stays strict');
g = formGroup([entry('a'), entry('b', { region: 'oce' })], 'duel', 300_000);
check(g === null, 'a region that is not a neighbor never matches');
g = formGroup([entry('a', { ranked: false }), entry('b', { ranked: false, ratings: {} })], 'duel', 1000);
check(g && g.length === 2, 'casual queues ignore rating');
const four = [1, 2, 3, 4].map((i) => entry(`p${i}`, { modes: ['tdm'], ratings: { tdm: 1000 + i * 40 }, since: i }));
g = formGroup(four, 'tdm', 2000);
check(g && g.length === 4, 'four players make a team match');
check(formGroup(four.slice(0, 3), 'tdm', 2000) === null, 'three are not enough for 2v2');
const teams = assignTeams(four, 'tdm');
const sum = (t) => four.reduce((a, e, i) => a + (teams[i] === t ? e.ratings.tdm : 0), 0);
check(teams.filter((t) => t === 1).length === 2 && Math.abs(sum(1) - sum(2)) <= 80, `teams come out even (${sum(1)} against ${sum(2)})`);
check(electHost([entry('a', { rtt: 90 }), entry('b', { rtt: 20 }), entry('c', { rtt: 60 })]).pid === 'b', 'the host is the best connected player');

// ---- ratings
check(Math.abs(expectedScore(1000, 1000) - 0.5) < 1e-9 && expectedScore(1400, 1000) > 0.9, 'expected score is the Elo curve');
check(kFactor(0, 1000) === 40 && kFactor(29, 1500) === 40 && kFactor(30, 1500) === 24 && kFactor(99, 2300) === 16, 'K is 40, then 24, then 16 at the top');
const duel = (sa, sb, ra = 1000, rb = 1000, la = false) => [
  { pid: 'a', rating: ra, rated: 0, team: 0, score: sa, left: la },
  { pid: 'b', rating: rb, rated: 0, team: 0, score: sb, left: false },
];
let d = eloDeltas('duel', duel(20, 10));
check(d.a === 20 && d.b === -20, `an even duel win moves 20 points (${d.a}, ${d.b})`);
d = eloDeltas('duel', duel(20, 10, 1400, 1000));
check(d.a > 0 && d.a < 10 && d.a === -d.b, `beating a much lower rating earns little and it is zero-sum (${d.a}, ${d.b})`);
d = eloDeltas('duel', duel(10, 10));
check(d.a === 0 && d.b === 0, 'a drawn even duel moves nothing');
d = eloDeltas('duel', duel(30, 0, 1000, 1000, true));
check(d.a < 0 && d.b > 0, 'leaving is a loss whatever the score');
d = eloDeltas('duel', duel(0, 10, 120, 1000));
check(d.a + 120 >= 100, 'the floor is 100');
const ffa = [60, 30, 10, 0].map((score, i) => ({ pid: `p${i}`, rating: 1000, rated: 0, team: 0, score, left: false }));
d = eloDeltas('ffa', ffa);
check(d.p0 > d.p1 && d.p1 > d.p2 && d.p2 > d.p3 && Object.values(d).reduce((a, b) => a + b, 0) === 0, `free for all pays by place and sums to zero (${JSON.stringify(d)})`);
const tdm = [
  { pid: 'a', rating: 1100, rated: 0, team: 1, score: 15, left: false },
  { pid: 'b', rating: 900, rated: 0, team: 1, score: 10, left: false },
  { pid: 'c', rating: 1000, rated: 0, team: 2, score: 20, left: false },
  { pid: 'd', rating: 1000, rated: 0, team: 2, score: 5, left: false },
];
d = eloDeltas('tdm', tdm);
check(d.a === d.b && d.c === d.d && d.a === -d.c, `a team moves together (${d.a}, ${d.c})`);
const w1 = duel(5, 3);
const w2 = duel(3, 3);
check(won('duel', w1[0], w1) && !won('duel', w1[1], w1) && !won('duel', w2[0], w2), 'a win is first place; a tie is not a win');
check(seasonId(Date.UTC(2026, 9, 24)) === '2026-Q4' && seasonId(Date.UTC(2027, 0, 1)) === '2027-Q1', 'seasons are calendar quarters');
check(easeToStart(1400) === 1300 && easeToStart(800) === 850, 'a season ends with a quarter of the way back to 1000');

// ---- agreement
const rows = (a, b) => [{ client: 0, score: a, team: 0 }, { client: 1, score: b, team: 0 }];
const rep = (me, rs, over = {}) => ({ me, rows: rs, left: false, peers: {}, ...over });
let v = decide(['h', 'p'], 'h', { h: rep(0, rows(10, 5), { peers: { 1: 'p' } }) }, true, false);
check(v === 'wait', 'a duel waits for the second report');
v = decide(['h', 'p'], 'h', { h: rep(0, rows(10, 5), { peers: { 1: 'p' } }), p: rep(1, rows(10, 5)) }, true, false);
check(v.ok && v.clients[0] === 'h' && v.clients[1] === 'p', 'agreeing duel reports decide it, with who is who');
v = decide(['h', 'p'], 'h', { h: rep(0, rows(10, 5), { peers: { 1: 'p' } }), p: rep(1, rows(5, 10)) }, true, false);
check(!v.ok && v.blame === 'h', 'disagreeing duel reports void it and blame the host');
v = decide(['h', 'p'], 'h', { h: rep(0, rows(10, 5), { peers: { 1: 'p' } }) }, true, true);
check(!v.ok, 'a duel whose other player never reported is void when time is up');
v = decide(['h', 'p'], 'h', { h: rep(0, rows(10, 5), { peers: { 1: 'p' } }), p: rep(1, [], { left: true }) }, true, false);
check(v.ok, 'a player who left needs no agreeing scoreboard: the stayer decides');
v = decide(['h', 'p'], 'h', { h: rep(0, rows(10, 5), { peers: { 1: 'q' } }), p: rep(1, rows(10, 5)) }, true, false);
check(!v.ok, "the host's player list must match who the players say they are");
const big = (hr, ...pr) => {
  const r = { h: rep(0, hr, { peers: { 1: 'p1', 2: 'p2', 3: 'p3' } }) };
  pr.forEach((x, i) => (r[`p${i + 1}`] = rep(i + 1, x)));
  return r;
};
const four4 = (a) => [0, 1, 2, 3].map((c) => ({ client: c, score: a[c], team: 0 }));
const same = four4([9, 7, 5, 3]);
const odd = four4([30, 7, 5, 3]);
v = decide(['h', 'p1', 'p2', 'p3'], 'h', big(odd, same, same, same), false, false);
check(v.ok && v.hostOverruled && v.rows[0].score === 9, 'three players outvote a host who reports another scoreboard');
v = decide(['h', 'p1', 'p2', 'p3'], 'h', big(same, same, odd, four4([1, 1, 1, 1])), false, false);
check(!v.ok, 'no agreeing majority voids a bigger match');
v = decide(['h', 'p1', 'p2', 'p3'], 'h', big(same, same), false, false);
check(v === 'wait', 'a bigger match waits while no majority has reported');
v = decide(['h', 'p1', 'p2', 'p3'], 'h', big(same, same, same), false, false);
check(v.ok, 'a majority of the non-host players is enough');
check(canonical(rows(1, 2)) === canonical([...rows(1, 2)].reverse()), 'scoreboards compare in one order');
check(Object.keys(MODES).every((m) => MODES[m].maps.length > 0), 'every mode has a map pool');

console.log(failed ? `${failed} FAILED` : 'ALL PASSED');
process.exit(failed ? 1 : 0);
