"""End to end: an installed build updates itself from a release (UPDATES_PLAN.md U5).

python tools/test_update_e2e.py <installed build folder> [--godot <exe>] [--port 8765]

The folder is an export with the updater in it (python tools/export_game.py --out <folder>); it plays
the player's install and is changed by the test. The test:
  1. exports patch.pck against the install's Q3Remaster.pck with game/version.txt bumped to <old>-e2e
     (put back afterwards), the way export_game.py --release builds a patch release,
  2. lays out a release server like GitHub's: releases.json (the feed) and download/<tag>/<asset>,
     with manifest.json signed by the real release key (the build only trusts that key) and the base
     pack listed on its own release,
  3. serves it (with Range, as GitHub does) and starts the build with -- update_server=...: the menu
     finds the release, Update automatically downloads only what changed, and the window is closed,
     so the update installs at quit (Session.quit_game -> Q3Updater.exe),
  4. starts the build again: it must run the new version (UpdateBoot mounts the patch and reads
     version.txt) and, on reaching the menu, clear the update's backup.
Prints "UPDATE E2E ok" or what failed. Needs the setup gate passed on this PC (a remembered pak0).
"""
import argparse, http.server, json, os, shutil, socketserver, subprocess, sys, threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import release_manifest  # noqa: E402

REPO = "ozmiumofficial/quake-3-arena-remastered-fan-project"
fails = []


def check(name, cond, detail=""):
    print("  [%s] %s%s" % ("ok" if cond else "FAIL", name, "" if cond else ": " + str(detail)[:300]))
    if not cond:
        fails.append(name)
    return cond


class RangeHandler(http.server.SimpleHTTPRequestHandler):
    """Serves files with byte ranges (206), as GitHub's release downloads do."""
    def log_message(self, *a):
        pass

    def do_GET(self):
        path = self.translate_path(self.path)
        rng = self.headers.get("Range")
        if not rng or not os.path.isfile(path):
            return super().do_GET()
        size = os.path.getsize(path)
        start, _, end = rng.replace("bytes=", "").partition("-")
        start, end = int(start), min(int(end) if end else size - 1, size - 1)
        self.send_response(206)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Range", "bytes %d-%d/%d" % (start, end, size))
        self.send_header("Content-Length", str(end - start + 1))
        self.end_headers()
        with open(path, "rb") as f:
            f.seek(start)
            left = end - start + 1
            while left > 0:
                chunk = f.read(min(left, 1 << 20))
                self.wfile.write(chunk)
                left -= len(chunk)
        served.append((self.path, end - start + 1))


served = []


def link(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    try:
        os.link(src, dst)
    except OSError:
        shutil.copy2(src, dst)


def wait_for(cond, seconds):
    end = time.time() + seconds
    while time.time() < end:
        if cond():
            return True
        time.sleep(1)
    return False


def log_text(path):
    return open(path, encoding="utf-8", errors="replace").read() if os.path.isfile(path) else ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("install")
    ap.add_argument("--godot", default=r"F:\SteamLibrary\steamapps\common\Godot Engine\godot.windows.opt.tools.64.exe")
    ap.add_argument("--port", type=int, default=8765)
    a = ap.parse_args()
    install = os.path.abspath(a.install)
    project = os.path.join(ROOT, "godot")
    work = os.path.join(os.path.dirname(install), "_e2e")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    for f in ("Q3Remaster.exe", "Q3Remaster.pck", "Q3Updater.exe"):
        if not os.path.isfile(os.path.join(install, f)):
            sys.exit("not an export with the updater: %s is missing" % f)
    shutil.rmtree(os.path.join(install, ".update"), ignore_errors=True)
    if os.path.exists(os.path.join(install, "patch.pck")):
        os.remove(os.path.join(install, "patch.pck"))

    # 1. the patch release
    version_txt = os.path.join(project, "game", "version.txt")
    old = open(version_txt).read().strip()
    new = old.split("-")[0] + "-e2e" if "-" in old else old + "-e2e"
    base_tag, tag = "v" + old, "v" + new
    print("patch %s -> %s" % (old, new))
    try:
        with open(version_txt, "w", newline="\n") as f:
            f.write(new + "\n")
        patch = os.path.join(work, "patch.pck")
        subprocess.run([a.godot, "--headless", "--path", project, "--export-patch", "Windows Desktop", patch,
                        "--patches", os.path.join(install, "Q3Remaster.pck"), "--log-file", os.path.join(work, "patch.log")],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    finally:
        with open(version_txt, "w", newline="\n") as f:
            f.write(old + "\n")
    if not check("patch exported", os.path.isfile(patch)):
        return
    print("  patch.pck: %.2f MB (the base pack is %.0f MB)" % (os.path.getsize(patch) / 1e6, os.path.getsize(os.path.join(install, "Q3Remaster.pck")) / 1e6))

    # 2. the release server: the new build's files (the install's, plus the patch), signed
    newbuild = os.path.join(work, "newbuild")
    for d, dirs, fs in os.walk(install):
        dirs[:] = [x for x in dirs if x not in release_manifest.SKIP_DIRS]
        for f in fs:
            src = os.path.join(d, f)
            link(src, os.path.join(newbuild, os.path.relpath(src, install)))
    link(patch, os.path.join(newbuild, "patch.pck"))
    data = release_manifest.build(newbuild, tag, tags={"Q3Remaster.pck": base_tag})
    sig = release_manifest.sign(data)
    if not check("signed with the release key", sig is not None, release_manifest.private_key_path()):
        return
    server = os.path.join(work, "server")
    rel = os.path.join(server, "download", tag)
    os.makedirs(rel)
    open(os.path.join(rel, "manifest.json"), "wb").write(data)
    open(os.path.join(rel, "manifest.sig"), "wb").write(sig)
    for e in json.loads(data)["files"]:
        if e["source"] == "release":
            link(os.path.join(newbuild, e["path"]), os.path.join(server, "download", e.get("tag", tag), e["asset"]))
        elif e["source"] == "textures":
            link(os.path.join(newbuild, e["path"]), os.path.join(server, "download", "textures", e["path"]))
    with open(os.path.join(server, "releases.json"), "w") as f:
        json.dump([{"tag_name": tag, "name": tag + ": end-to-end test", "draft": False, "prerelease": True,
                    "html_url": "https://github.com/%s/releases/tag/%s" % (REPO, tag), "body": "## Test\n- the updater",
                    "published_at": "2026-10-27T00:00:00Z", "assets": []}], f)

    # 3. serve it; the old build finds it, downloads, and installs it when its window closes
    handler = lambda *x, **k: RangeHandler(*x, directory=server, **k)
    httpd = socketserver.ThreadingTCPServer(("127.0.0.1", a.port), handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    url = "http://127.0.0.1:%d/" % a.port
    exe = os.path.join(install, "Q3Remaster.exe")
    try:
        log1 = os.path.join(work, "run1.log")
        game = subprocess.Popen([exe, "--log-file", log1, "--", "update_server=" + url], cwd=install)
        staged = wait_for(lambda: os.path.isfile(os.path.join(install, ".update", "apply.txt")), 240)
        check("the old build downloaded the update", staged, log_text(log1)[-1500:])
        if staged:
            listed = open(os.path.join(install, ".update", "apply.txt")).read().split()
            check("only the patch was needed", listed == [tag, "patch.pck"], listed)
            got = sum(n for p, n in served)
            check("downloaded about the patch's size", got <= os.path.getsize(patch) + 4096, "%d bytes: %s" % (got, served))
        # the window's close button: Session.quit_game hands over to Q3Updater.exe
        subprocess.run(["powershell", "-NoProfile", "-Command", "(Get-Process -Id %d).CloseMainWindow() | Out-Null" % game.pid])
        try:
            game.wait(60)
        except subprocess.TimeoutExpired:
            game.kill()
            check("the game quit on its close button", False)
        check("the update was installed at quit", wait_for(lambda: log_text(os.path.join(install, ".update", "applied")).strip() == "quit", 60),
              log_text(os.path.join(install, ".update", "updater.log")))
        check("patch.pck is in the install", os.path.isfile(os.path.join(install, "patch.pck"))
              and release_manifest.sha256_file(os.path.join(install, "patch.pck")) == release_manifest.sha256_file(patch))

        # 4. the new build: runs the new version and clears the backup once its menu is up
        log2 = os.path.join(work, "run2.log")
        game = subprocess.Popen([exe, "--log-file", log2], cwd=install)
        cleared = wait_for(lambda: not os.path.exists(os.path.join(install, ".update", "applied")), 180)
        check("the new build reached its menu and cleared the backup", cleared and not os.path.exists(os.path.join(install, ".update", "backup")))
        # Diagnostics' heartbeat names the running version (application/config/version, which
        # UpdateBoot sets from the patched game/version.txt)
        session = os.path.join(os.environ["APPDATA"], "Godot", "app_userdata", "Q3Remaster", "diagnostics", "sessions", "%d.json" % game.pid)
        ran = wait_for(lambda: ("Q3Remaster %s " % new) in log_text(session), 30)
        check("it runs %s" % new, ran, log_text(session)[:300])
        subprocess.run(["powershell", "-NoProfile", "-Command", "(Get-Process -Id %d).CloseMainWindow() | Out-Null" % game.pid])
        try:
            game.wait(60)
        except subprocess.TimeoutExpired:
            game.kill()

        # 5. a first install from the zip alone: the splash fetches the missing texture pack itself
        pack = os.path.join(install, "textures-3.pck")
        want = release_manifest.sha256_file(pack)
        os.remove(pack)   # (the server keeps its own link to the same file)
        served.clear()
        log3 = os.path.join(work, "run3.log")
        game = subprocess.Popen([exe, "--log-file", log3, "--", "update_server=" + url], cwd=install)
        back = wait_for(lambda: os.path.isfile(pack) and os.path.getsize(pack) > 0, 300)
        check("the splash downloaded textures-3.pck", back and release_manifest.sha256_file(pack) == want, served[-3:])
        session = os.path.join(os.environ["APPDATA"], "Godot", "app_userdata", "Q3Remaster", "diagnostics", "sessions", "%d.json" % game.pid)
        check("and went on to the menu", wait_for(lambda: "main_menu" in log_text(session), 90), log_text(session)[:400])
        subprocess.run(["powershell", "-NoProfile", "-Command", "(Get-Process -Id %d).CloseMainWindow() | Out-Null" % game.pid])
        try:
            game.wait(60)
        except subprocess.TimeoutExpired:
            game.kill()
        if not os.path.isfile(pack):
            link(os.path.join(server, "download", "textures", "textures-3.pck"), pack)
    finally:
        httpd.shutdown()
    print("updater log:\n  " + log_text(os.path.join(install, ".update", "updater.log")).strip().replace("\n", "\n  "))
    print("UPDATE E2E %s" % ("ok" if not fails else "FAIL: " + ", ".join(fails)))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
