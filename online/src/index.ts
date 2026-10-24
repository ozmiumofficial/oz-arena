// Online services Worker. GET /hub upgrades to the social hub's WebSocket; /admin/* is the operator's (moderation,
// removing test players), refused unless it carries the ADMIN_TOKEN secret (`wrangler secret put ADMIN_TOKEN`).
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
  /** unset means the admin routes are off */
  ADMIN_TOKEN?: string;
  /** A Cloudflare Realtime TURN key (dashboard > Realtime > TURN): minted into short-lived credentials for matches. */
  TURN_KEY_ID?: string;
  TURN_KEY_TOKEN?: string;
  /** "1" lets matches connect directly with no relay (local testing only; the deployed hub leaves it unset). */
  NET_DIRECT?: string;
  /** The monthly cap on the relay's traffic, GB (default 800; Cloudflare's free allowance is 1,000). */
  RELAY_BUDGET_GB?: string;
  /** An API token with Account Analytics: Read on the account that holds the TURN key, and that account's id. With both,
   *  the spending guard runs on the relay's real usage (Cloudflare's analytics) and revokes credentials; without them it
   *  charges every credential its worst case. */
  TURN_ANALYTICS_TOKEN?: string;
  TURN_ACCOUNT_ID?: string;
  /** How long relay credentials last, s (default 12 h with the usage guard, 1 h without; Cloudflare allows 48 h). */
  TURN_TTL_S?: string;
  /** Tests only: how often the usage guard checks (default 120000 ms), and where it finds Cloudflare's APIs. */
  RELAY_CHECK_MS?: string;
  CF_GRAPHQL_URL?: string;
  TURN_API_URL?: string;
  /** Tests only: how long a match may take to start (default 90 s) and how long after the first report the rest may take (default 60 s), ms. */
  MATCH_FORM_MS?: string;
  MATCH_REPORT_MS?: string;
}

/** Bumped when the message protocol changes incompatibly; older games are told to update. */
const PROTOCOL = '1';
const CLOCK_SKEW_MS = 5 * 60_000;

export default {
  async fetch(req: Request, env: Env): Promise<Response> {
    const url = new URL(req.url);
    if (url.pathname === '/health') return Response.json({ ok: true, protocol: PROTOCOL });
    if (url.pathname.startsWith('/admin/')) {
      if (!(await admin(req, env))) return new Response('forbidden', { status: 403 });
      return env.HUB.get(env.HUB.idFromName('global')).fetch(req);
    }
    if (url.pathname !== '/hub') return new Response('not found', { status: 404 });
    if (req.headers.get('upgrade') !== 'websocket') return new Response('expected websocket', { status: 426 });
    if ((req.headers.get('x-proto') ?? '') !== PROTOCOL) return refuse(4002, 'This version of the game is out of date for online play.');
    const who = await verify(req.headers);
    if ('error' in who) return refuse(4001, who.error);
    const headers = new Headers(req.headers);
    headers.set('x-pid', who.pid);
    headers.set('x-region', regionOf(req));
    return env.HUB.get(env.HUB.idFromName('global')).fetch(new Request(req, { headers }));
  },
} satisfies ExportedHandler<Env>;

/**
 * The matchmaking region of the caller, from Cloudflare's request data (PLAN.md "Regions"): na_east, na_west, eu, sa,
 * asia, oce or meaf. Without request data (a local `wrangler dev`) it is "na_east".
 */
function regionOf(req: Request): string {
  const cf = (req as Request & { cf?: { continent?: string; country?: string; longitude?: string | number } }).cf;
  const continent = cf?.continent ?? '';
  const country = cf?.country ?? '';
  const lon = Number(cf?.longitude ?? 0);
  if (continent === 'EU') return 'eu';
  if (continent === 'SA') return 'sa';
  if (continent === 'OC') return 'oce';
  if (continent === 'AF') return 'meaf';
  if (continent === 'AS') return MIDDLE_EAST.has(country) ? 'meaf' : 'asia';
  if (continent === 'NA') return lon < -100 ? 'na_west' : 'na_east';
  return 'na_east';
}

const MIDDLE_EAST = new Set(['AE', 'BH', 'IL', 'IQ', 'IR', 'JO', 'KW', 'LB', 'OM', 'PS', 'QA', 'SA', 'SY', 'TR', 'YE', 'EG']);

/** `authorization: Bearer <ADMIN_TOKEN>`, compared in constant time. */
async function admin(req: Request, env: Env): Promise<boolean> {
  const want = env.ADMIN_TOKEN ?? '';
  const got = (req.headers.get('authorization') ?? '').replace(/^Bearer /, '');
  if (want.length < 32 || !got) return false;
  const enc = new TextEncoder();
  const [a, b] = await Promise.all([crypto.subtle.digest('SHA-256', enc.encode(want)), crypto.subtle.digest('SHA-256', enc.encode(got))]);
  return crypto.subtle.timingSafeEqual(a, b);
}

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
