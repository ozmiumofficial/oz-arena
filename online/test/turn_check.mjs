// Checks a Cloudflare Realtime TURN key before it goes into the Worker: asks for credentials the way the hub does.
//   TURN_KEY_ID=... TURN_KEY_TOKEN=... node test/turn_check.mjs
const id = process.env.TURN_KEY_ID;
const token = process.env.TURN_KEY_TOKEN;
if (!id || !token) {
  console.log('Set TURN_KEY_ID and TURN_KEY_TOKEN (Cloudflare dashboard > Realtime > TURN).');
  process.exit(2);
}
const res = await fetch(`https://rtc.live.cloudflare.com/v1/turn/keys/${encodeURIComponent(id)}/credentials/generate-ice-servers`, {
  method: 'POST',
  headers: { authorization: `Bearer ${token}`, 'content-type': 'application/json' },
  body: JSON.stringify({ ttl: 600 }),
});
if (!res.ok) {
  console.log(`FAIL: the relay answered ${res.status}`);
  process.exit(1);
}
const body = await res.json();
const turn = (body.iceServers ?? []).filter((s) => s.username && s.credential);
console.log(`PASS: ${turn.length} TURN server entries`);
for (const s of turn) for (const u of [].concat(s.urls)) console.log('  ', u);
