// The relay's spending guard against `npm run dev` with TURN_KEY_ID/TURN_KEY_TOKEN set to anything, RELAY_BUDGET_GB=1 and
// an ADMIN_TOKEN (>= 32 chars) in .dev.vars: the first player credential (0.7 GB worst case) goes through to the relay (which refuses the
// fake key), the second would pass the budget and is refused by the hub itself.
//   node test/relay_budget.mjs <admin token> [ws://127.0.0.1:8787]
import { generateKeyPairSync, sign } from 'node:crypto';
const admin = process.argv[2];
const BASE = process.argv[3] ?? 'ws://127.0.0.1:8787';
let failed = 0;
const check = (ok, what) => { console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`); if (!ok) failed++; };
async function ice(name) {
  const { publicKey, privateKey } = generateKeyPairSync('rsa', { modulusLength: 2048 });
  const ts = Date.now();
  const ws = new WebSocket(BASE + '/hub', { headers: { 'x-proto': '1', 'x-key': publicKey.export({ type: 'spki', format: 'der' }).toString('base64'),
    'x-ts': String(ts), 'x-sig': sign('sha256', Buffer.from(`hub|${ts}`), privateKey).toString('base64'), 'x-name': name } });
  const got = new Promise((res) => { ws.onmessage = (e) => { const m = JSON.parse(e.data); if (m.type === 'net_ice') res(m); }; });
  await new Promise((r) => (ws.onopen = r));
  await new Promise((r) => setTimeout(r, 300));
  ws.send(JSON.stringify({ type: 'net_ice' }));
  const m = await got;
  ws.close();
  return m;
}
const a = await ice('A');
check(a.error && !a.error.includes('paused'), `the first credential reaches the relay (${a.error})`);
const b = await ice('B');
check(b.error === 'Relayed play is paused for this month.', `the second is refused by the budget (${b.error})`);
const s = await (await fetch(BASE.replace('ws', 'http') + '/admin/relay', { headers: { authorization: `Bearer ${admin}` } })).json();
check(s.worst_case_gb === 0.7 && s.credentials === 1 && s.budget_gb === 1, `the admin view shows the charge (${JSON.stringify(s)})`);
process.exit(failed ? 1 : 0);
