// Matchmaking, ranked results and the leaderboard against `npm run dev` with these in online/.dev.vars:
//   MATCH_FORM_MS=3000  MATCH_REPORT_MS=3000
//   node test/ranked.mjs [ws://127.0.0.1:8787]
import { generateKeyPairSync, sign } from 'node:crypto';

const BASE = (process.argv[2] ?? 'ws://127.0.0.1:8787') + '/hub';
let failed = 0;
const check = (ok, what) => {
  console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
  if (!ok) failed++;
};

class Client {
  constructor(name) {
    this.name = name;
    const { publicKey, privateKey } = generateKeyPairSync('rsa', { modulusLength: 2048 });
    this.key = publicKey.export({ type: 'spki', format: 'der' }).toString('base64');
    this.privateKey = privateKey;
    this.msgs = [];
    this.waiters = [];
  }
  open() {
    return new Promise((resolve) => {
      const ts = Date.now();
      const sig = sign('sha256', Buffer.from(`hub|${ts}`), this.privateKey).toString('base64');
      this.ws = new WebSocket(BASE, { headers: { 'x-proto': '1', 'x-key': this.key, 'x-ts': String(ts), 'x-sig': sig, 'x-name': encodeURIComponent(this.name), 'x-model': 'sarge', 'x-color': '#2266ff' } });
      this.ws.onmessage = (e) => {
        const m = JSON.parse(e.data);
        this.msgs.push(m);
        if (m.type === 'social') this.pid = m.me.id;
        this.waiters = this.waiters.filter((w) => !(w.test(m) && (w.resolve(m), true)));
      };
      this.next('social', () => true, 4000).then(() => resolve(this), () => resolve(this));
    });
  }
  send(type, data = {}) {
    this.ws.send(JSON.stringify({ type, ...data }));
  }
  next(type, test = () => true, ms = 6000) {
    const old = this.msgs.find((m) => m.type === type && test(m) && !m.__seen);
    return new Promise((resolve, reject) => {
      this.waiters.push({ test: (m) => m.type === type && test(m), resolve });
      setTimeout(() => reject(new Error(`${this.name}: no ${type} in ${ms} ms`)), ms);
    });
  }
  async never(type, test = () => true, ms = 800) {
    try {
      await this.next(type, test, ms);
      return false;
    } catch {
      return true;
    }
  }
  close() {
    this.ws.close();
  }
}
const wait = (p) => p.then((x) => x, (e) => (console.log('  ', e.message), null));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const a = await new Client('Alpha').open();
const b = await new Client('Beta').open();
const c = await new Client('Gamma').open();
for (const x of [a, b]) x.send('career', { xp: 5_000_000, card: {}, stats: {} });
await sleep(300);

// ---- ranked needs the level; casual doesn't
c.send('queue_join', { modes: ['duel'], ranked: true, region: 'auto', rtt: 50 });
const low = await wait(c.next('queue_state', (m) => m.status === 'refused'));
check(low && low.reason.includes('level'), `ranked is refused below level 30 (${low?.reason})`);
c.send('queue_join', { modes: ['ffa', 'duel'], ranked: true, region: 'auto', rtt: 50 });
const nomode = await wait(c.next('queue_state', (m) => m.status === 'refused'));
check(nomode && nomode.reason.includes('no ranked queue'), 'a mode with no ranked queue is refused');

// ---- a ranked duel is matched, with a host chosen by connection
const fa = wait(a.next('match_found'));
const fb = wait(b.next('match_found'));
a.send('queue_join', { modes: ['duel'], ranked: true, region: 'auto', rtt: 90 });
const q = await wait(a.next('queue_state', (m) => m.status === 'queued'));
check(q?.modes[0] === 'duel' && q.ranked, 'queued');
b.send('queue_join', { modes: ['duel'], ranked: true, region: 'auto', rtt: 25 });
const [foundA, foundB] = [await fa, await fb];
check(foundA && foundB && foundA.match === foundB.match && foundA.mode === 'duel', 'both players get the same match');
check(foundB?.you_host === true && foundA?.you_host === false && foundA.host === b.pid, 'the best connected player (rtt 25) hosts');
check(/^q3tourney[1-6]$/.test(foundA?.map ?? ''), `the map is from the duel pool (${foundA?.map})`);
const id = foundA.match;

// ---- the match is private: hidden from the browser, joinable only by its players
b.send('net_host', { name: 'Ranked duel', mode: 'duel', map: foundA.map, players: 1, max: 2, ranked: true, match: id });
await sleep(300);
c.send('net_browse');
const list = await wait(c.next('net_matches'));
check(list && !list.matches.some((m) => m.host === b.pid), 'a matchmade match is not in the browser');
c.send('net_join', { host: b.pid });
const inv = await wait(c.next('net_refused'));
check(inv?.reason === 'This match is invite only.', 'a stranger cannot join it');
const reqP = wait(b.next('net_request'));
a.send('net_join', { host: b.pid });
check(!!(await reqP), 'its player can');

// ---- results: the players' reports agree, ratings move
a.send('match_joined', { match: id });
b.send('match_joined', { match: id });
await sleep(200);
const rows = [{ client: 0, score: 20, team: 0 }, { client: 1, score: 8, team: 0 }];
const resA = wait(a.next('match_result'));
const resB = wait(b.next('match_result'));
b.send('match_report', { match: id, me: 0, rows, left: false, peers: { 1: a.pid } });
await sleep(300);
check(await a.never('match_result', () => true, 300), 'a duel waits for the second report');
a.send('match_report', { match: id, me: 1, rows, left: false, peers: {} });
const [ra, rb] = [await resA, await resB];
check(ra && rb && !ra.voided && !rb.voided, 'agreeing reports decide the match');
check(rb?.won === true && rb.delta === 20 && rb.after === 1020 && ra.delta === -20 && ra.after === 980, `the winner gains 20, the loser drops 20 (${rb?.after}, ${ra?.after})`);
check(rb?.rated === 1 && rb.wins === 1 && ra.losses === 1, 'wins, losses and matches played are counted');
const rat = await wait(a.next('ratings'));
check(rat?.queues?.duel?.rating === 980, 'the new rating arrives with a ratings message');
a.send('leaderboard', { board: 'ranked', scope: 'global' });
const board = await wait(a.next('leaderboard'));
check(board && board.total === 0, 'the ranked board stays empty until placement (10 matches)');

// ---- disagreement voids it and moves nothing
const f2a = wait(a.next('match_found'));
const f2b = wait(b.next('match_found'));
a.send('queue_join', { modes: ['duel'], ranked: true, region: 'auto', rtt: 40 });
b.send('queue_join', { modes: ['duel'], ranked: true, region: 'auto', rtt: 60 });
const [m2a, m2b] = [await f2a, await f2b];
const id2 = m2a.match;
const host2 = m2a.you_host ? a : b;
const guest2 = m2a.you_host ? b : a;
a.send('match_joined', { match: id2 });
b.send('match_joined', { match: id2 });
await sleep(200);
const v1 = wait(a.next('match_result'));
const v2 = wait(b.next('match_result'));
const hostClient = 0;
host2.send('match_report', { match: id2, me: hostClient, rows: [{ client: 0, score: 30, team: 0 }, { client: 1, score: 0, team: 0 }], peers: { 1: guest2.pid } });
guest2.send('match_report', { match: id2, me: 1, rows: [{ client: 0, score: 5, team: 0 }, { client: 1, score: 9, team: 0 }] });
const [va, vb] = [await v1, await v2];
check(va?.voided && vb?.voided && va.reason.includes('differ'), `different scoreboards void the match (${va?.reason})`);

// ---- leaving is a loss
const f3a = wait(a.next('match_found'));
const f3b = wait(b.next('match_found'));
a.send('queue_join', { modes: ['duel'], ranked: true, region: 'auto', rtt: 40 });
b.send('queue_join', { modes: ['duel'], ranked: true, region: 'auto', rtt: 60 });
const [m3a] = [await f3a, await f3b];
const id3 = m3a.match;
const host3 = m3a.you_host ? a : b;
const guest3 = m3a.you_host ? b : a;
a.send('match_joined', { match: id3 });
b.send('match_joined', { match: id3 });
await sleep(200);
const l1 = wait(a.next('match_result'));
const l2 = wait(b.next('match_result'));
guest3.send('match_report', { match: id3, me: 1, rows: [], left: true });
host3.send('match_report', { match: id3, me: 0, rows: [{ client: 0, score: 3, team: 0 }, { client: 1, score: 12, team: 0 }], peers: { 1: guest3.pid } });
const [la, lb] = [await l1, await l2];
const hostRes = host3 === a ? la : lb;
const guestRes = host3 === a ? lb : la;
check(hostRes && guestRes && !hostRes.voided && hostRes.won === true && guestRes.won === false, 'a player who left loses even with the higher score');

// ---- a match that never starts is void and the missing player is locked out
const f4a = wait(a.next('match_found'));
const f4b = wait(b.next('match_found'));
a.send('queue_join', { modes: ['duel'], ranked: true, region: 'auto', rtt: 40 });
b.send('queue_join', { modes: ['duel'], ranked: true, region: 'auto', rtt: 60 });
const m4 = await f4a;
await f4b;
const only = m4.you_host ? a : b;
const other = m4.you_host ? b : a;
only.send('match_joined', { match: m4.match });
const nv = wait(only.next('match_result'));
const never = await nv;
check(never?.voided && never.reason.includes('never started'), `a match nobody else joined is void (${never?.reason})`);
other.send('queue_join', { modes: ['duel'], ranked: true, region: 'auto', rtt: 50 });
const locked = await wait(other.next('queue_state', (m) => m.status === 'locked'));
check(!!locked, 'the player who never showed is locked out of the queue');
only.send('queue_join', { modes: ['duel'], ranked: true, region: 'auto', rtt: 50 });
check(!!(await wait(only.next('queue_state', (m) => m.status === 'queued'))), 'the player who showed can queue again');
only.send('queue_leave');
check(!!(await wait(only.next('queue_state', (m) => m.status === 'idle'))), 'leaving the queue works');

// ---- a casual team queue forms from four and splits into even teams
const extra = [await new Client('T1').open(), await new Client('T2').open(), await new Client('T3').open(), await new Client('T4').open()];
const four = extra;
const founds = four.map((x) => wait(x.next('match_found')));
for (const x of four) x.send('queue_join', { modes: ['tdm'], ranked: false, region: 'auto', rtt: 50 });
const fm = await Promise.all(founds);
check(fm.every((f) => f && f.match === fm[0].match && f.players.length === 4), 'four players make a casual team match');
check(fm[0] && fm[0].players.filter((p) => p.team === 1).length === 2 && fm[0].players.filter((p) => p.team === 2).length === 2, 'two on each team');
check(fm[0] && !fm[0].ranked, 'a casual match is not ranked');

for (const x of [a, b, c, ...extra]) x.close();
console.log(failed ? `${failed} FAILED` : 'ALL PASSED');
process.exit(failed ? 1 : 0);
