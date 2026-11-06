"""Run a script (or a snippet) in the live Blender: python bl.py <script.py | -c "code"> [--port 9877]

The code runs on Blender's main thread (BlenderMCP's execute_code) with this folder on sys.path; what it prints
comes back here. Errors come back with their traceback."""
import sys, os, json, socket

port = 9877
args = sys.argv[1:]
if "--port" in args:
    i = args.index("--port"); port = int(args[i + 1]); del args[i:i + 2]
HERE = os.path.dirname(os.path.abspath(__file__)).replace("\\", "/")
if args[0] == "--shot":        # what Tony sees: the 3D viewport, saved to a file
    cmd = {"type": "get_viewport_screenshot", "params": {"filepath": os.path.abspath(args[1]), "max_size": 1400}}
else:
    code = args[1] if args[0] == "-c" else open(args[0], encoding="utf-8").read()
    code = "import sys\nif %r not in sys.path: sys.path.insert(0, %r)\n" % (HERE, HERE) + code
    cmd = {"type": "execute_code", "params": {"code": code}}
s = socket.create_connection(("localhost", port), timeout=900)
s.sendall(json.dumps(cmd).encode("utf-8"))
buf = b""
r = None
while True:
    d = s.recv(1 << 20)
    if not d:
        break
    buf += d
    try:
        r = json.loads(buf.decode("utf-8"))
        break
    except Exception:
        continue
s.close()
if r is None:
    sys.exit("ERROR: the connection closed before a whole reply came back (%d bytes)" % len(buf))
if r.get("status") == "success":
    res = r["result"]
    out = (res.get("result", json.dumps(res)) if isinstance(res, dict) else res)
    sys.stdout.write(out if isinstance(out, str) else json.dumps(out))
else:
    msg = r.get("message", r)
    try:
        m = json.loads(msg)
        sys.stdout.write("ERROR %s: %s\n%s" % (m.get("exception_type"), m.get("message"), m.get("traceback", "")))
    except Exception:
        sys.stdout.write("ERROR %s" % msg)
    sys.exit(1)
