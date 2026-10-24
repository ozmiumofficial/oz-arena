// Checks the hub's match directory, joining and WebRTC signaling (MULTIPLAYER_PLAN.md M4) against `npm run dev`
// (wrangler on 127.0.0.1:8787). With NET_DIRECT=1 in .dev.vars the hub allows direct candidates; this test is run
// both ways by the Godot side, so here it only checks what holds either way, and the relay filter when NET_DIRECT
// is off (pass "relay" as the second argument).
//   node test/match.mjs [ws://127.0.0.1:8787] [relay]
import { generateKeyPairSync, sign } from 'node:crypto';

const BASE = (process.argv[2] ?? 'ws://127.0.0.1:8787') + '/hub';
const RELAY = process.argv[3] === 'relay';
let failed = 0;
const check = (ok, what) => {
  console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
  if (!ok) failed++;
};

function identity() {
  const { publicKey, privateKey } = generateKeyPairSync('rsa', { modulusLength: 2048 });
  return { key: publicKey.export({ type: 'spki', format: 'der' }).toString('base64'), privateKey };
}

class Client {
  constructor(name) {
    this.name = name;
    this.id = identity();
    this.msgs = [];
    this.waiters = [];
  }
  open() {
    return new Promise((resolve) => {
      const ts = Date.now();
      const sig = sign('sha256', Buffer.from(`hub|${ts}`), this.id.privateKey).toString('base64');
      this.ws = new WebSocket(BASE, {
        headers: { 'x-proto': '1', 'x-key': this.id.key, 'x-ts': String(ts), 'x-sig': sig, 'x-name': encodeURIComponent(this.name), 'x-model': 'sarge', 'x-color': '#2266ff' },
      });
      this.ws.onmessage = (e) => {
        const m = JSON.parse(e.data);
        this.msgs.push(m);
        if (m.type === 'social') {
          this.state = m;
          this.pid = m.me.id;
        }
        this.waiters = this.waiters.filter((w) => !(w.test(m) && (w.resolve(m), true)));
      };
      this.ws.onclose = () => resolve(this);
      this.next('social', () => true, 4000).then(() => resolve(this), () => resolve(this));
    });
  }
  send(type, data = {}) {
    this.ws.send(JSON.stringify({ type, ...data }));
  }
  next(type, test = () => true, ms = 3000) {
    return new Promise((resolve, reject) => {
      this.waiters.push({ test: (m) => m.type === type && test(m), resolve });
      setTimeout(() => reject(new Error(`${this.name}: no ${type} in ${ms} ms`)), ms);
    });
  }
  /** True when no message of `type` shows up for `ms`. */
  async never(type, test = () => true, ms = 600) {
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

const host = await new Client('Host').open();
const joiner = await new Client('Joiner').open();
const other = await new Client('Other').open();

// a listing shows up in the directory, with the host's region, and is the host's alone
host.send('net_host', { name: 'Test arena', mode: 'ffa', map: 'q3dm17', players: 1, max: 2, password: false, ranked: false, version: '1.0' });
await new Promise((r) => setTimeout(r, 200));
joiner.send('net_browse');
const list = await wait(joiner.next('net_matches'));
const mine = list?.matches?.find((m) => m.host === host.pid);
check(mine && mine.name === 'Test arena' && mine.map === 'q3dm17' && mine.owner === 'Host', 'the listing is browsable');
check(mine && /^[a-z_]+$/.test(mine.region) && list.region === mine.region, `the listing and the asker have a region (${mine?.region})`);
host.send('net_browse');
const own = await wait(host.next('net_matches'));
check(own && !own.matches.some((m) => m.host === host.pid), 'a host does not see their own match in the browser');

// joining: the host is asked, accepts, the joiner hears
const waitP = wait(joiner.next('net_wait'));
const reqP = wait(host.next('net_request'));
joiner.send('net_join', { host: host.pid, password: 'x', version: '1.0' });
const wait1 = await waitP;
const req = await reqP;
check(wait1?.host === host.pid && req?.peer === joiner.pid && req.name === 'Joiner' && req.password === 'x', 'the host gets the join request');
// signaling before the host accepts goes nowhere
joiner.send('net_signal', { to: host.pid, kind: 'offer', sdp: 'v=0' });
check(await host.never('net_signal'), 'no signaling before the host accepts');
host.send('net_accept', { peer: joiner.pid });
const acc = await wait(joiner.next('net_accepted'));
check(acc?.host === host.pid, 'the joiner is told they are accepted');

// signaling: an offer and an answer pass; a stranger cannot inject
const sdp = 'v=0\r\no=- 1 1 IN IP4 127.0.0.1\r\nc=IN IP4 203.0.113.9\r\na=candidate:1 1 UDP 2122 203.0.113.9 5000 typ host\r\na=mid:0\r\n';
host.send('net_signal', { to: joiner.pid, kind: 'offer', sdp });
const offer = await wait(joiner.next('net_signal', (m) => m.kind === 'offer'));
check(offer?.from === host.pid, 'the offer reaches the joiner');
if (RELAY) check(offer && !offer.sdp.includes('a=candidate') && !offer.sdp.includes('203.0.113.9'), 'candidate lines and the connection address are stripped from the SDP');
other.send('net_signal', { to: host.pid, kind: 'answer', sdp: 'v=0' });
check(await host.never('net_signal'), 'a player with no link cannot signal the host');
joiner.send('net_signal', { to: host.pid, kind: 'answer', sdp: 'v=0\r\n' });
check(!!(await wait(host.next('net_signal', (m) => m.kind === 'answer'))), 'the answer reaches the host');
joiner.send('net_signal', { to: host.pid, kind: 'candidate', candidate: 'candidate:2 1 UDP 100 198.51.100.7 6000 typ relay raddr 0.0.0.0 rport 0', mid: '0', index: 0 });
const relay = await wait(host.next('net_signal', (m) => m.kind === 'candidate'));
check(relay?.candidate.includes('typ relay') && relay.mid === '0', 'a relay candidate passes');
joiner.send('net_signal', { to: host.pid, kind: 'candidate', candidate: 'candidate:3 1 UDP 100 192.168.1.20 6000 typ host', mid: '0', index: 0 });
if (RELAY) check(await host.never('net_signal', (m) => m.kind === 'candidate'), 'a host candidate (an address) is dropped');

// full matches, missing matches, version mismatches
host.send('net_host', { name: 'Test arena', mode: 'ffa', map: 'q3dm17', players: 2, max: 2, version: '1.0' });
await new Promise((r) => setTimeout(r, 200));
other.send('net_join', { host: host.pid, version: '1.0' });
const full = await wait(other.next('net_refused'));
check(full?.reason === 'The match is full.', 'a full match refuses');
host.send('net_host', { name: 'Test arena', mode: 'ffa', map: 'q3dm17', players: 1, max: 2, version: '1.0' });
await new Promise((r) => setTimeout(r, 200));
other.send('net_join', { host: host.pid, version: '0.9' });
const ver = await wait(other.next('net_refused'));
check(ver?.reason?.includes('version'), `a version mismatch refuses (${ver?.reason})`);
other.send('net_join', { host: 'ffffffffffffffffffff' });
const none = await wait(other.next('net_refused'));
check(none?.reason === 'That match is gone.', 'an unknown match refuses');

// reject
const req2P = wait(host.next('net_request'));
other.send('net_join', { host: host.pid, version: '1.0' });
const req2 = await req2P;
const rejP = wait(other.next('net_refused', (m) => m.reason === 'Banned.'));
host.send('net_reject', { peer: other.pid, reason: 'Banned.' });
const rej = await rejP;
check(!!rej && !!req2, 'the host can reject a join');

// a kick (a passed vote in the game): the player is told, and can't rejoin that host for a while
const req3P = wait(host.next('net_request'));
other.send('net_join', { host: host.pid, version: '1.0' });
await req3P;
host.send('net_accept', { peer: other.pid });
await wait(other.next('net_accepted'));
const kickedP = wait(other.next('net_gone'));
host.send('net_kick', { peer: other.pid });
const kicked = await kickedP;
check(kicked?.pid === host.pid && kicked.kicked === true, 'a kicked player is told they were kicked');
const banP = wait(other.next('net_refused'));
other.send('net_join', { host: host.pid, version: '1.0' });
const banned = await banP;
check(banned?.reason?.includes('kicked') && banned.reason.includes('minutes'), `a kicked player cannot rejoin that host (${banned?.reason})`);
joiner.send('net_kick', { peer: other.pid });   // only the host can kick
check(await other.never('net_gone'), 'a player who is not the host cannot kick');

// relay credentials: ok with a TURN key or NET_DIRECT, an explained error otherwise; and rate limited
joiner.send('net_ice');
const ice = await wait(joiner.next('net_ice'));
check(ice && (Array.isArray(ice.servers) || typeof ice.error === 'string'), `relay credentials answer (${ice?.error ?? `${ice?.servers?.length} servers, relay_only ${ice?.relay_only}`})`);
joiner.send('net_ice');
const again = await wait(joiner.next('net_ice'));
check(again?.error === 'Too many requests.', 'asking again at once is refused');

// hanging up: the other side is told, and the listing goes
joiner.close();
const gone = await wait(host.next('net_gone'));
check(gone?.pid === joiner.pid, 'the host hears the peer left');
host.send('net_unhost');
await new Promise((r) => setTimeout(r, 200));
other.send('net_browse');
const after = await wait(other.next('net_matches'));
check(after && !after.matches.some((m) => m.host === host.pid), 'an unlisted match leaves the directory');
host.send('net_host', { name: 'Again', mode: 'ffa', map: 'q3dm6', players: 1, max: 8 });
await new Promise((r) => setTimeout(r, 200));
host.close();
await new Promise((r) => setTimeout(r, 300));
other.send('net_browse');
const dead = await wait(other.next('net_matches'));
check(dead && !dead.matches.some((m) => m.host === host.pid), 'a host that disconnects leaves the directory');

other.close();
console.log(failed ? `${failed} FAILED` : 'ALL PASSED');
process.exit(failed ? 1 : 0);
