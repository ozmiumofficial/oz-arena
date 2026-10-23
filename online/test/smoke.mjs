// End-to-end check of the social hub against `npm run dev` (wrangler on 127.0.0.1:8787), with three players
// signing the way the game does (RSA-SHA256 PKCS#1 v1.5 over "hub|<ts>").
//   node test/smoke.mjs [ws://127.0.0.1:8787]
import { generateKeyPairSync, sign } from 'node:crypto';

const BASE = (process.argv[2] ?? 'ws://127.0.0.1:8787') + '/hub';
let failed = 0;
const check = (ok, what) => {
  console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
  if (!ok) failed++;
};

function identity() {
  const { publicKey, privateKey } = generateKeyPairSync('rsa', { modulusLength: 2048 });
  return { key: publicKey.export({ type: 'spki', format: 'der' }).toString('base64'), privateKey };
}

function headers(id, extra = {}, ts = Date.now()) {
  const sig = sign('sha256', Buffer.from(`hub|${ts}`), id.privateKey).toString('base64');
  return { 'x-proto': '1', 'x-key': id.key, 'x-ts': String(ts), 'x-sig': sig, ...extra };
}

class Client {
  constructor(name, id = identity()) {
    this.name = name;
    this.id = id;
    this.msgs = [];
    this.waiters = [];
  }
  open(hdrs) {
    return new Promise((resolve) => {
      // Node's WebSocket (undici) takes custom handshake headers this way
      this.hdrs = hdrs ?? headers(this.id, { 'x-name': encodeURIComponent(this.name), 'x-model': 'sarge', 'x-color': '#2266ff' });
      this.ws = new WebSocket(BASE, { headers: this.hdrs });
      this.ws.onmessage = (e) => {
        const m = JSON.parse(e.data);
        this.msgs.push(m);
        if (m.type === 'social') this.state = m;
        this.waiters = this.waiters.filter((w) => !(w.test(m) && (w.resolve(m), true)));
      };
      this.ws.onclose = (e) => {
        this.closed = { code: e.code, reason: e.reason };
        resolve(this);
      };
      this.next('social', () => true, 4000).then(() => resolve(this), () => resolve(this));
    });
  }
  send(type, data = {}) {
    this.ws.send(JSON.stringify({ type, ...data }));
  }
  /** The next message of `type` passing `test`, or a rejection after `ms`. */
  next(type, test = () => true, ms = 3000) {
    return new Promise((resolve, reject) => {
      const w = { test: (m) => m.type === type && test(m), resolve };
      this.waiters.push(w);
      setTimeout(() => reject(new Error(`${this.name}: no ${type} in ${ms} ms`)), ms);
    });
  }
  close() {
    this.ws.close();
  }
}

const wait = (p) => p.then((x) => x, (e) => (console.log('  ', e.message), null));

const a = await new Client('^1Ranger').open();
const b = await new Client('Visor').open();
const c = await new Client('Orbb').open();
check(a.state?.me?.code?.match(/^[0-9A-Z]{4}-[0-9A-Z]{4}$/), `friend code issued (${a.state?.me?.code})`);
check(a.state?.me?.name === '^1Ranger', 'name with color codes kept');

// bad signature, replayed handshake, old protocol
const bad = await new Client('Bad').open({ ...headers(identity()), 'x-sig': headers(identity())['x-sig'] });
check(bad.closed?.code === 4001, `forged signature refused (${bad.closed?.reason})`);
// a captured handshake sent again, byte for byte
const replay = await new Client('Replay', a.id).open(a.hdrs);
check(replay.closed?.code === 4001, `replayed sign-in refused (${replay.closed?.reason})`);
const old = await new Client('Old').open({ ...headers(identity()), 'x-proto': '0' });
check(old.closed?.code === 4002, 'old protocol told to update');

// friend request by code (lowercase, no dash, O for 0 all work)
a.send('friend_add', { code: b.state.me.code.toLowerCase().replace('-', '').replace(/0/g, 'o') });
const bReq = await wait(b.next('social', (m) => m.incoming.length === 1));
check(bReq?.incoming[0]?.id === a.state.me.id, 'B sees A\'s request');
check(a.state.outgoing.length === 1 || (await wait(a.next('social', (m) => m.outgoing.length === 1))), 'A sees the outgoing request');
b.send('friend_accept', { id: a.state.me.id });
const aFriends = await wait(a.next('social', (m) => m.friends.length === 1));
check(aFriends?.friends[0]?.online && aFriends.friends[0].name === 'Visor', 'A and B are friends, B shows online');

// presence
b.send('presence', { status: 'match', detail: 'Duel on q3tourney2', join: '' });
const pres = await wait(a.next('social', (m) => m.friends[0]?.status === 'match'));
check(pres?.friends[0]?.detail === 'Duel on q3tourney2', 'A sees B in a match');

// C adds A, A adds C back by code = accept
c.send('friend_add', { code: a.state.me.code });
await wait(a.next('social', (m) => m.incoming.length === 1));
a.send('friend_add', { code: c.state.me.code });
const ac = await wait(a.next('social', (m) => m.friends.length === 2));
check(!!ac, 'adding someone who asked you accepts their request');

// party: A invites B and C, both join, chat, promote, kick, leave
a.send('party_invite', { id: b.state.me.id });
const inv = await wait(b.next('social', (m) => m.invites.length === 1));
check(inv?.invites[0]?.from?.id === a.state.me.id, 'B gets A\'s party invite');
b.send('party_accept', { party: inv?.invites[0]?.party });
const joined = await wait(a.next('social', (m) => m.party?.members.length === 2));
check(joined?.party?.leader === a.state.me.id, 'party of 2 with A leading');
a.send('party_invite', { id: c.state.me.id });
const cinv = await wait(c.next('social', (m) => m.invites.length === 1));
c.send('party_accept', { party: cinv?.invites[0]?.party });
await wait(a.next('social', (m) => m.party?.members.length === 3));
check(a.state.party?.members.length === 3, 'party of 3');
c.send('party_chat', { text: 'gl hf' });
const chat = await wait(b.next('party_chat'));
check(chat?.text === 'gl hf' && chat.from.name === 'Orbb', 'party chat reaches members');
b.send('party_kick', { id: c.state.me.id });
const refusedKick = await wait(b.next('notice', (m) => m.tone === 'error'));
check(!!refusedKick, 'only the leader can kick');
a.send('party_promote', { id: b.state.me.id });
const promoted = await wait(b.next('social', (m) => m.party?.leader === b.state.me.id));
check(!!promoted, 'leader handed to B');
b.send('party_kick', { id: c.state.me.id });
const kicked = await wait(c.next('social', (m) => m.party === null));
check(!!kicked, 'C kicked by the new leader');
// the leader drops: the party passes to A
b.close();
const handed = await wait(a.next('social', (m) => m.party?.leader === a.state.me.id && m.friends.find((f) => f.name === 'Visor')?.online === false));
check(!!handed, 'leader went offline: A leads, B shows offline');
a.send('party_leave');
const left = await wait(a.next('social', (m) => m.party === null));
check(!!left, 'party of one ends when A leaves');

// unfriend
a.send('friend_remove', { id: c.state.me.id });
const unf = await wait(c.next('social', (m) => m.friends.length === 0));
check(!!unf, 'unfriending clears both lists');

a.close();
c.close();
console.log(failed ? `${failed} check(s) failed` : 'all checks passed');
process.exit(failed ? 1 : 0);
