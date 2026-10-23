// Online services Worker. One route that matters: GET /hub upgrades to the social hub's WebSocket.
//
// Identity without accounts: on first launch the game makes an RSA key pair (Godot Crypto) and keeps it in
// user://. Every connection carries the public key (x-key, base64 SPKI DER), a millisecond timestamp (x-ts) and
// an RSA-SHA256 PKCS#1 v1.5 signature of "hub|<ts>" (x-sig). The player id is the first 20 hex digits of
// SHA-256(key); the hub also rejects a timestamp that isn't newer than the last one it saw, so a captured
// handshake can't be replayed. A rejected connection still upgrades, then closes with a 4xxx code and a
// reason, so the game can show why.
import { Hub } from './hub';
import { b64, hex, refuse } from './util';

export { Hub };

export interface Env {
  HUB: DurableObjectNamespace<Hub>;
}

/** Bumped when the message protocol changes incompatibly; older games are told to update. */
const PROTOCOL = '1';
const CLOCK_SKEW_MS = 5 * 60_000;

export default {
  async fetch(req: Request, env: Env): Promise<Response> {
    const url = new URL(req.url);
    if (url.pathname === '/health') return Response.json({ ok: true, protocol: PROTOCOL });
    if (url.pathname !== '/hub') return new Response('not found', { status: 404 });
    if (req.headers.get('upgrade') !== 'websocket') return new Response('expected websocket', { status: 426 });
    if ((req.headers.get('x-proto') ?? '') !== PROTOCOL) return refuse(4002, 'This version of the game is out of date for online play.');
    const who = await verify(req.headers);
    if ('error' in who) return refuse(4001, who.error);
    const headers = new Headers(req.headers);
    headers.set('x-pid', who.pid);
    return env.HUB.get(env.HUB.idFromName('global')).fetch(new Request(req, { headers }));
  },
} satisfies ExportedHandler<Env>;

async function verify(h: Headers): Promise<{ pid: string } | { error: string }> {
  const key = h.get('x-key') ?? '';
  const ts = Number(h.get('x-ts'));
  const sig = h.get('x-sig') ?? '';
  if (!key || !sig || !Number.isFinite(ts)) return { error: 'Missing identity.' };
  if (Math.abs(Date.now() - ts) > CLOCK_SKEW_MS) return { error: "Your PC's clock is off by more than 5 minutes." };
  let der: Uint8Array;
  let sigBytes: Uint8Array;
  try {
    der = b64(key);
    sigBytes = b64(sig);
  } catch {
    return { error: 'Malformed identity.' };
  }
  if (der.length > 1024 || sigBytes.length > 1024) return { error: 'Malformed identity.' };
  try {
    const pub = await crypto.subtle.importKey('spki', der, { name: 'RSASSA-PKCS1-v1_5', hash: 'SHA-256' }, false, ['verify']);
    const ok = await crypto.subtle.verify('RSASSA-PKCS1-v1_5', pub, sigBytes, new TextEncoder().encode(`hub|${ts}`));
    if (!ok) return { error: 'Identity check failed.' };
  } catch {
    return { error: 'Malformed identity.' };
  }
  return { pid: hex(new Uint8Array(await crypto.subtle.digest('SHA-256', der))).slice(0, 20) };
}
