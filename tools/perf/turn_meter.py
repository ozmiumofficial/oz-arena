"""Counts what Cloudflare's TURN relay bills (MULTIPLAYER_PLAN.md M8): a UDP forwarder between each local WebRTC
peer and the relay, so every byte the relay sends a client (its billed egress, TURN framing included) is counted.

python tools/perf/turn_meter.py --listen 34780 34781 [--upstream turn.cloudflare.com:3478] [--out work/turn_meter.json]

Each --listen port is one TURN client (point a peer's ICE server at turn:127.0.0.1:<port>?transport=udp; the test
does it with Q3RtcPeer.turn_rewrite). TURN's message integrity doesn't cover the client's address, so the relay
treats the forwarder like any NAT. The counters are rewritten to --out twice a second:
  {"time": s, "<port>": {"up": [packets, bytes], "down": [...], "down_data": [...], "down_indication": [...],
   "down_control": [...]}}
down_data is ChannelData (data the relay forwards on a bound channel, 4 bytes of framing), down_indication is
STUN Data indications (data before a channel is bound, 36+ bytes of framing), down_control is everything else
(allocations, permissions, refreshes, connectivity checks' answers). Bytes are UDP payloads, as Cloudflare
meters them; IP and UDP headers (28 bytes a packet) are not included.
"""
import argparse, json, os, select, socket, time

ap = argparse.ArgumentParser()
ap.add_argument("--listen", type=int, nargs="+", required=True)
ap.add_argument("--upstream", default="turn.cloudflare.com:3478")
ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "..", "work", "turn_meter.json"))
a = ap.parse_args()

host, port = a.upstream.rsplit(":", 1)
relay = (socket.gethostbyname(host), int(port))
legs = {}   # local socket -> leg; upstream socket -> leg
for p in a.listen:
    local = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    local.bind(("127.0.0.1", p))
    up = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    up.bind(("0.0.0.0", 0))
    leg = {"port": p, "local": local, "up_sock": up, "client": None,
           "c": {k: [0, 0] for k in ("up", "down", "down_data", "down_indication", "down_control")}}
    legs[local] = leg
    legs[up] = leg


def kind(b: bytes) -> str:
    if len(b) >= 4 and 0x40 <= b[0] <= 0x7F:
        return "down_data"
    if len(b) >= 20 and int.from_bytes(b[0:2], "big") == 0x0017:
        return "down_indication"
    return "down_control"


def count(c, k, n):
    c[k][0] += 1
    c[k][1] += n


def dump():
    d = {"time": time.time(), "upstream": "%s:%d" % relay}
    for leg in {id(l): l for l in legs.values()}.values():
        d[str(leg["port"])] = leg["c"]
    tmp = a.out + ".tmp"
    with open(tmp, "w") as f:
        json.dump(d, f)
    os.replace(tmp, a.out)


print("turn meter: %s -> %s:%d, writing %s" % (a.listen, relay[0], relay[1], os.path.abspath(a.out)), flush=True)
last = 0.0
socks = list(legs.keys())
while True:
    r, _, _ = select.select(socks, [], [], 0.25)
    for s in r:
        leg = legs.get(s)
        if leg is None:
            continue   # an upstream socket replaced this round
        try:
            data, addr = s.recvfrom(65535)
        except OSError:
            continue   # Windows reports an earlier ICMP unreachable on the next read
        if s is leg["local"]:
            if leg["client"] != addr and leg["client"] is not None:
                # a new client (another run): a new mapping, as a NAT would make, or the relay answers its Allocate
                # with 437 (allocation mismatch) while the last run's allocation lives
                del legs[leg["up_sock"]]
                socks.remove(leg["up_sock"])
                leg["up_sock"].close()
                leg["up_sock"] = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                leg["up_sock"].bind(("0.0.0.0", 0))
                legs[leg["up_sock"]] = leg
                socks.append(leg["up_sock"])
            leg["client"] = addr
            count(leg["c"], "up", len(data))
            leg["up_sock"].sendto(data, relay)
        elif leg["client"] is not None:
            count(leg["c"], "down", len(data))
            count(leg["c"], kind(data), len(data))
            leg["local"].sendto(data, leg["client"])
    if time.time() - last >= 0.5:
        last = time.time()
        dump()
