// The relay's usage guard (MULTIPLAYER_PLAN.md M8) against a mock of Cloudflare's APIs: this starts the mock (TURN
// credentials, revoke, GraphQL analytics) and its own `wrangler dev` on a port of its own with fresh storage, fake keys and a
// 1.5 s check, then plays it through:
//   1. credentials last 12 h in usage mode, and are recorded;
//   2. the analytics' month total reaches the admin view, and the queries name the key and the account;
//   3. a credential sending faster than the game can is revoked;
//   4. 30 mints an hour per player, then refused;
//   5. at 90% of the budget new credentials stop; at 100% every live one is revoked;
//   6. a failing analytics API is reported, and its mints count at their worst case (fail safe).
//   node test/relay_guard.mjs
import { spawn } from 'node:child_process';
import { generateKeyPairSync, sign } from 'node:crypto';
import { createServer } from 'node:http';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

// ports of their own each run: a stopped `wrangler dev` can leave a workerd answering on its old port (CLAUDE.md)
const MOCK = 20000 + Math.floor(Math.random() * 10000);
const HUB = MOCK + 10000;
const ADMIN = 'relay-guard-test-admin-token-0123456789abcdef';
let failed = 0;
const check = (ok, what) => { console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`); if (!ok) failed++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// ---------------------------------------------------------------- the mock Cloudflare
const mock = { minted: 0, ttls: [], revoked: [], queries: [], monthBytes: 0, perUser: {}, fail: false };
const server = createServer((req, res) => {
  let body = '';
  req.on('data', (c) => (body += c));
  req.on('end', () => {
    const url = req.url ?? '';
    // connection: close, so workerd never reuses a connection the mock has dropped ("Network connection lost")
    const send = (code, obj) => { res.writeHead(code, { 'content-type': 'application/json', connection: 'close' }); res.end(obj === undefined ? '' : JSON.stringify(obj)); };
    if (url.endsWith('/generate-ice-servers')) {
      mock.minted++;
      mock.ttls.push(JSON.parse(body || '{}').ttl);
      return send(201, { iceServers: [{ urls: ['stun:stun.example:3478'] }, { urls: ['turn:turn.example:3478?transport=udp'], username: `user${mock.minted}`, credential: 'secret' }] });
    }
    const rv = url.match(/\/credentials\/([^/]+)\/revoke$/);
    if (rv) { mock.revoked.push(decodeURIComponent(rv[1])); res.writeHead(204, { connection: 'close' }); return res.end(); }
    if (url === '/graphql') {
      const q = JSON.parse(body).query;
      mock.queries.push(q);
      if (mock.fail) return send(500, { errors: [{ message: 'down' }] });
      const groups = q.includes('dimensions { username }')
        ? Object.entries(mock.perUser).map(([username, b]) => ({ dimensions: { username }, sum: { egressBytes: b } }))
        : [{ sum: { egressBytes: mock.monthBytes } }];
      return send(200, { data: { viewer: { accounts: [{ callsTurnUsageAdaptiveGroups: groups }] } }, errors: null });
    }
    send(404, {});
  });
});
server.keepAliveTimeout = 120_000;   // workerd reuses connections; Node's 5 s default closed them under it
await new Promise((r) => server.listen(MOCK, '127.0.0.1', r));

// ---------------------------------------------------------------- the hub
const state = mkdtempSync(join(tmpdir(), 'relay-guard-'));
const vars = {
  TURN_KEY_ID: 'testkey', TURN_KEY_TOKEN: 'testtoken', TURN_ANALYTICS_TOKEN: 'analytics', TURN_ACCOUNT_ID: 'acct123',
  RELAY_CHECK_MS: process.env.CHECK_MS ?? '1500', RELAY_BUDGET_GB: '100', ADMIN_TOKEN: ADMIN,
  CF_GRAPHQL_URL: `http://127.0.0.1:${MOCK}/graphql`, TURN_API_URL: `http://127.0.0.1:${MOCK}`,
};
// a stopped `wrangler dev` leaves workerd running on its port (CLAUDE.md): clear the port first and last
const freePort = () => spawn('powershell', ['-NoProfile', '-Command',
  `Get-NetTCPConnection -LocalPort ${HUB} -State Listen -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.OwningProcess -Force -ErrorAction SilentlyContinue }`], { stdio: 'ignore' });
await new Promise((r) => freePort().on('exit', r));
const args = ['wrangler', 'dev', '--port', String(HUB), '--persist-to', state, ...Object.entries(vars).flatMap(([k, v]) => ['--var', `${k}:${v}`])];
const hub = spawn('npx', args, { cwd: new URL('..', import.meta.url), shell: true, stdio: ['ignore', 'pipe', 'pipe'] });
let log = '';
hub.stdout.on('data', (d) => (log += d));
hub.stderr.on('data', (d) => (log += d));
for (let i = 0; i < 120 && !log.includes('Ready on'); i++) await sleep(500);
if (!log.includes('Ready on')) { console.log(log); throw new Error('the hub did not start'); }
const ready = log.match(/Ready on (\S+)/)?.[1] ?? '';
if (!ready.endsWith(`:${HUB}`)) { console.log(log.slice(-2000)); throw new Error(`the hub started on ${ready}, not port ${HUB}`); }

const BASE = `ws://127.0.0.1:${HUB}`;
const identities = {};
function identity(name) {
  return (identities[name] ??= generateKeyPairSync('rsa', { modulusLength: 2048 }));
}
async function socket(name) {
  const { publicKey, privateKey } = identity(name);
  const ts = Date.now();
  const ws = new WebSocket(BASE + '/hub', { headers: { 'x-proto': '1', 'x-key': publicKey.export({ type: 'spki', format: 'der' }).toString('base64'),
    'x-ts': String(ts), 'x-sig': sign('sha256', Buffer.from(`hub|${ts}`), privateKey).toString('base64'), 'x-name': name } });
  let why = '';
  ws.onerror = (e) => (why = String(e.message ?? e.type));
  await Promise.race([new Promise((r) => (ws.onopen = r)), sleep(10_000).then(() => { throw new Error(`${name}: the socket never opened (${why})\n${log.slice(-1500)}`); })]);
  await sleep(200);
  return ws;
}
async function ice(ws) {
  const got = new Promise((res) => { ws.onmessage = (e) => { const m = JSON.parse(e.data); if (m.type === 'net_ice') res(m); }; });
  ws.send(JSON.stringify({ type: 'net_ice' }));
  return Promise.race([got, sleep(10_000).then(() => ({ error: 'no answer' }))]);
}
const relay = async () => (await fetch(`http://127.0.0.1:${HUB}/admin/relay`, { headers: { authorization: `Bearer ${ADMIN}` } })).json();
// the next check that got figures (one may fail on a reused connection; the guard retries at the next)
const waitCheck = async (good = true) => { const t = (await relay())[good ? 'checked' : 'tried']; for (let i = 0; i < 60; i++) { await sleep(250); const r = await relay(); if (r[good ? 'checked' : 'tried'] > t) return r; } return relay(); };

try {
  // 1. a credential, 12 h
  const a = await socket('Alpha');
  const ia = await ice(a);
  check(!ia.error && ia.ttl === 43200 && mock.ttls[0] === 43200 && ia.servers.some((s) => s.username === 'user1'),
    `usage mode mints 12-hour credentials (ttl ${ia.ttl}, asked ${mock.ttls[0]}${ia.error ? ', ' + ia.error : ''})`);

  // 2. the month's figure
  mock.monthBytes = 1e9;
  let r = await waitCheck();
  check(r.mode === 'usage' && Math.abs(r.used_gb - 1) < 1e-6 && r.error === '' && r.live === 1, `the analytics' month total reaches the admin view (${JSON.stringify({ mode: r.mode, used: r.used_gb, error: r.error, live: r.live })})`);
  const q = mock.queries.find((x) => !x.includes('username')) ?? '';
  check(q.includes('accountTag: "acct123"') && q.includes('keyId: "testkey"') && q.includes('date_geq'), 'the query names the account, the key and the dates');

  // 3. too fast: revoked
  await sleep(3200);   // old enough to judge at this check interval
  mock.perUser = { user1: 50e6 };
  r = await waitCheck();
  await sleep(300);
  r = await relay();
  check(mock.revoked.includes('user1') && r.revoked.some((x) => x.username === 'user1' && x.revoked.includes('KB/s')),
    `a credential sending faster than the game can is revoked (${JSON.stringify(r.revoked)})`);
  mock.perUser = {};

  console.log('  (opening 31 sockets as one player)');
  // 4. the hourly mint limit
  const many = await Promise.all(Array.from({ length: 30 }, () => socket('Bravo')));
  const answers = [];
  // a mint whose answer is lost (local workerd drops a request now and then under a burst of outgoing ones; MULTIPLAYER_PLAN.md
  // M8) is asked again, as the game's join does after its timeout
  for (const s of many) { let x = await ice(s); for (let i = 0; i < 3 && x.error === 'no answer'; i++) x = await ice(s); answers.push(x); }
  const extra = await socket('Bravo');
  const last = await ice(extra);
  check(answers.filter((x) => !x.error).length === 30 && /Too many/.test(last.error ?? ''), `30 credentials an hour a player, then refused (${answers.filter((x) => !x.error).length} minted; then ${last.error}; ${[...new Set(answers.map((x) => x.error).filter(Boolean))].join(', ')})`);
  if (answers.some((x) => x.error === 'no answer'))
    console.log('  unanswered:', answers.map((x, i) => (x.error === 'no answer' ? i : -1)).filter((i) => i >= 0).join(' '), '\n  hub log:\n' + log.split('\n').slice(-25).join('\n'));
  for (const s of [...many, extra]) s.close();

  // 5. the budget: 90% stops new credentials, 100% revokes the live ones
  mock.monthBytes = 91e9;
  await waitCheck();
  const c = await socket('Charlie');
  const ic = await ice(c);
  check(ic.error === 'Relayed play is paused for this month.', `at 90% of the budget new credentials stop (${ic.error})`);
  const before = mock.revoked.length;
  mock.monthBytes = 101e9;
  // every check revokes what is still live, so one cut short by a lost request is finished by the next
  for (let i = 0; i < 6; i++) { r = await waitCheck(); await sleep(300); r = await relay(); if (r.live === 0) break; }
  check(mock.revoked.length - before >= answers.filter((x) => !x.error).length && r.live === 0, `at 100% every live credential is revoked (${mock.revoked.length - before} revoked, ${r.live} live)`);

  // 6. a failing API: reported, and it fails safe
  mock.fail = true;
  for (let i = 0; i < 4; i++) { r = await waitCheck(false); if (r.error) break; }
  check(r.error.includes('500'), `a failing analytics API is reported (${r.error})`);
  check(r.estimate_gb >= r.used_gb, `and the estimate never drops below the last good figure (${r.estimate_gb} >= ${r.used_gb})`);
  for (const s of [a, c]) s.close();
} finally {
  hub.kill();
  spawn('taskkill', ['/pid', String(hub.pid), '/T', '/F'], { shell: true });
  await new Promise((r) => freePort().on('exit', r));
  server.close();
  await sleep(1500);
  try { rmSync(state, { recursive: true, force: true }); } catch {}
}
console.log(failed ? `RELAY GUARD FAILED (${failed})` : 'RELAY GUARD ok');
process.exit(failed ? 1 : 0);
