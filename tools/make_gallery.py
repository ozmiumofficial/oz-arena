"""Write work/index.html: one card per model linking to its viewer, with its numbers.

python make_gallery.py [<work dir>]
Serve the work folder (python -m http.server) and open the page.
"""
import os, sys, json, glob, html

HERE = os.path.dirname(os.path.abspath(__file__))
WORK = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(HERE), "work")
rows = []
for rep in sorted(glob.glob(os.path.join(WORK, "*", "report.json"))):
    r = json.load(open(rep))
    m = r["model"]
    thumb = next(iter(sorted(glob.glob(os.path.join(WORK, m, "tex", "prev_*_albedo.jpg")))), None)
    rows.append((r, os.path.relpath(thumb, WORK).replace("\\", "/") if thumb else ""))

cards = []
for r, thumb in rows:
    m = html.escape(r["model"])
    if r["ok"]:
        stats = "%d bones · %s tris · legs %.1f / torso %.1f mm" % (r["bones"], f"{r['tris']:,}", r["lower_rms_mm"], r["upper_rms_mm"])
        cards.append('<a class="card" href="%s/viewer/index.html"><img src="%s" alt="" loading="lazy"><b>%s</b><span>%s</span></a>'
                     % (m, html.escape(thumb), m.capitalize(), stats))
    else:
        cards.append('<div class="card bad"><b>%s</b><span>failed: %s</span></div>' % (m.capitalize(), html.escape(r.get("error", ""))))

page = """<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Q3Remaster Models</title><style>
:root{color-scheme:dark;--bg:#120f0e;--panel:#1b1715;--line:#352c27;--fg:#eee5dc;--muted:#a8998c;--accent:#ff6a2b}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.5 "Segoe UI",system-ui,sans-serif;padding:24px max(16px,4vw)}
h1{font:600 2rem "Arial Narrow",sans-serif;text-transform:uppercase;margin:0 0 4px}p{color:var(--muted);margin:0 0 20px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:12px}
.card{display:grid;gap:4px;background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:10px;color:inherit;text-decoration:none}
.card:hover{border-color:var(--accent)}.card img{width:100%%;aspect-ratio:1;object-fit:cover;border-radius:4px}
.card b{text-transform:uppercase;letter-spacing:.04em}.card span{color:var(--muted);font-size:.82rem}.bad b{color:var(--accent)}
</style></head><body><h1>Q3Remaster models</h1><p>%d of %d converted. Each card opens that model's side-by-side viewer.</p>
<div class="grid">%s</div></body></html>""" % (sum(r["ok"] for r, _ in rows), len(rows), "\n".join(cards))
open(os.path.join(WORK, "index.html"), "w", encoding="utf-8").write(page)
print("gallery:", os.path.join(WORK, "index.html"))
