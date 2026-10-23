// Fills the game's Friends panel for a look at it: three bot players around the game's own identity.
//   node test/friend_bot.mjs [identity.key] [ws://127.0.0.1:8787] [seconds]
// identity.key defaults to Godot's user://online/identity.key for the Q3Remaster project. The script also
// signs in as that identity (a second "device") to accept two of the bots' requests and the party
// invite, then stays online as the bots for `seconds` (default 120) so they show as online.
import { createHash, createPrivateKey, createPublicKey, generateKeyPairSync, sign } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';

const keyPath = process.argv[2] || join(process.env.APPDATA, 'Godot', 'app_userdata', 'Q3Remaster', 'online', 'identity.key');
const BASE = (process.argv[3] ?? 'ws://127.0.0.1:8787') + '/hub';
const SECONDS = Number(process.argv[4] ?? 120);

const player = { privateKey: createPrivateKey(readFileSync(keyPath)) };
player.key = createPublicKey(player.privateKey).export({ type: 'spki', format: 'der' }).toString('base64');
const playerId = createHash('sha256').update(Buffer.from(player.key, 'base64')).digest('hex').slice(0, 20);

function fresh() {
  const { publicKey, privateKey } = generateKeyPairSync('rsa', { modulusLength: 2048 });
  return { key: publicKey.export({ type: 'spki', format: 'der' }).toString('base64'), privateKey };
}

let lastTs = 0;
function open(id, name, model, color) {
  const ts = Math.max(Date.now(), lastTs + 1);
  lastTs = ts;
  const headers = {
    'x-proto': '1', 'x-key': id.key, 'x-ts': String(ts), 'x-sig': sign('sha256', Buffer.from(`hub|${ts}`), id.privateKey).toString('base64'),
    'x-name': encodeURIComponent(name), 'x-model': model, 'x-color': color,
  };
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(BASE, { headers });
    const c = { ws, state: null, waiters: [], send: (type, d = {}) => ws.send(JSON.stringify({ type, ...d })) };
    c.until = (test, ms = 4000) => new Promise((res, rej) => {
      if (c.state && test(c.state)) return res(c.state);
      c.waiters.push({ test, res });
      setTimeout(() => rej(new Error(`${name}: timed out`)), ms);
    });
    ws.onmessage = (e) => {
      const m = JSON.parse(e.data);
      if (m.type !== 'social') return;
      c.state = m;
      c.waiters = c.waiters.filter((w) => !(w.test(m) && (w.res(m), true)));
    };
    ws.onclose = (e) => reject(new Error(`${name}: closed ${e.code} ${e.reason}`));
    c.until(() => true).then(() => resolve(c), reject);
  });
}

// the game's identity, from a second device (it never changes the game's own name or look: no profile message)
const me = await open(player, 'Player', 'sarge', '#db2410');
console.log(`player ${playerId} code ${me.state.me.code} (${me.state.me.name})`);
me.ws.onclose = null;

const visor = await open(fresh(), '^4Visor', 'visor', '#2266ff');
const klesk = await open(fresh(), 'Klesk', 'klesk', '#5ccf4a');
const slash = await open(fresh(), '^5Slash', 'slash', '#00ffff');
for (const b of [visor, klesk, slash]) b.send('friend_add', { code: me.state.me.code });
await me.until((s) => s.incoming.length >= 3);
me.send('friend_accept', { id: visor.state.me.id });
me.send('friend_accept', { id: klesk.state.me.id });
await me.until((s) => s.friends.filter((f) => f.online).length >= 2);
visor.send('presence', { status: 'match', detail: 'Duel on q3tourney2' });
await klesk.until((s) => s.friends.length === 1);
klesk.send('party_invite', { id: playerId });
await me.until((s) => s.invites.length === 1);
me.send('party_accept', { party: me.state.invites[0].party });
await klesk.until((s) => s.party?.members.length === 2);
klesk.send('party_chat', { text: 'gl hf, duel queue after this?' });
me.ws.close();
console.log(`bots online for ${SECONDS} s: Visor (in a match), Klesk (party leader), Slash (request waiting)`);
setTimeout(() => process.exit(0), SECONDS * 1000);
