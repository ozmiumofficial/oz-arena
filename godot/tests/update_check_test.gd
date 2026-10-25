extends Node
## Headless check of the update check (Q3UpdateCheck, UPDATES_PLAN.md U1): version order, which
## release is offered, the skip list, the menu bridge's `update` message, and the settings. With
## `-- feed=http://127.0.0.1:8765/releases.json` it also fetches a real feed over HTTP (serve a
## GitHub "list releases" answer with `python -m http.server`).
##   godot --headless --path . res://tests/update_check_test.tscn --quit-after 3000

var _ok := true


func _ready() -> void:
	var saved := Q3UpdateCheck.skipped()
	_versions()
	_picking()
	_skip_and_signal()
	_bridge()
	_settings()
	var feed := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("feed="):
			feed = a.substr(5)
	if feed != "":
		await _http(feed)
	# put the skip list back as it was
	var c := ConfigFile.new()
	c.load(Q3UpdateCheck.STORE)
	c.set_value("updates", "skipped", saved)
	c.save(Q3UpdateCheck.STORE)
	print("UPDATE_CHECK_TEST ", "PASS" if _ok else "FAIL")
	get_tree().quit(0 if _ok else 1)


func _versions() -> void:
	var cmp := Q3UpdateCheck.compare
	_check("0.4.0-beta is newer than 0.3.0-beta", cmp.call("v0.4.0-beta", "0.3.0-beta") == 1)
	_check("a release is newer than its beta", cmp.call("0.3.0", "0.3.0-beta") == 1 and cmp.call("0.3.0-beta", "0.3.0") == -1)
	_check("0.10.0 is newer than 0.9.0 (numbers, not text)", cmp.call("0.10.0", "0.9.0") == 1)
	_check("beta.2 is older than beta.10", cmp.call("0.3.0-beta.2", "0.3.0-beta.10") == -1)
	_check("rc is newer than beta", cmp.call("0.3.0-rc", "0.3.0-beta") == 1)
	_check("same version compares equal, v prefix or not", cmp.call("v0.3.0-beta", "0.3.0-beta") == 0)
	_check("the textures tag is not a version", Q3UpdateCheck.parse_version("textures").is_empty() and cmp.call("textures", "0.3.0") == 0)
	_check("the build's own version parses", not Q3UpdateCheck.parse_version(Q3UpdateCheck.current_version()).is_empty())


func _release(tag: String, extra := {}) -> Dictionary:
	var r := {"tag_name": tag, "name": tag, "draft": false, "prerelease": false, "body": "notes\r\nline",
		"html_url": "https://github.com/%s/releases/tag/%s" % [Q3UpdateCheck.REPO, tag], "published_at": "2026-10-25T10:00:00Z",
		"assets": [{"name": "Q3Remaster-%s-windows.zip" % tag, "size": 1000}, {"name": "notes.txt", "size": 5}]}
	r.merge(extra, true)
	return r


func _picking() -> void:
	var list := [
		_release("textures", {"prerelease": false, "assets": [{"name": "textures-1.pck", "size": 2000}]}),
		_release("v9.9.9", {"draft": true}),
		_release("v0.3.0-beta"),
		_release("v0.4.0-beta", {"prerelease": true}),
		_release("v0.4.1-beta", {"prerelease": true}),
		"junk",
	]
	_check("pre-releases stay hidden unless asked for", Q3UpdateCheck.pick(list, "0.3.0-beta", false).is_empty())
	var p := Q3UpdateCheck.pick(list, "0.3.0-beta", true)
	_check("the newest newer release wins (not the draft, not textures)", p.get("tag") == "v0.4.1-beta" and p.get("version") == "0.4.1-beta")
	_check("size counts only the zip", p.get("size") == 1000)
	_check("notes lose carriage returns, the date is trimmed", p.get("notes") == "notes\nline" and p.get("published") == "2026-10-25" and p.get("beta") == true)
	_check("nothing newer than the newest", Q3UpdateCheck.pick(list, "0.4.1-beta", true).is_empty())
	var stable := Q3UpdateCheck.pick([_release("v0.4.0")], "0.3.0-beta", false)
	_check("a stable release is offered without betas", stable.get("tag") == "v0.4.0" and stable.get("beta") == false)
	var odd := Q3UpdateCheck.pick([_release("v0.4.0", {"html_url": "https://evil.example/x"})], "0.3.0", false)
	_check("a release link off the repo falls back to the releases page", odd.get("url") == Q3UpdateCheck.RELEASES_URL)
	var long := Q3UpdateCheck.pick([_release("v0.4.0", {"body": "x".repeat(10000)})], "0.3.0", false)
	_check("notes are capped", String(long.get("notes")).length() == Q3UpdateCheck.MAX_NOTES)


func _skip_and_signal() -> void:
	var u := Q3UpdateCheck.new()
	add_child(u)
	var got: Array = []
	u.available.connect(func(i): got.append(i))
	var body := JSON.stringify([_release("v99.0.0-beta", {"prerelease": true})])
	u.handle_body("not json")
	u.handle_body("{}")
	_check("a bad answer is ignored", got.is_empty() and u.info.is_empty())
	Q3UpdateCheck.skip("v98.0.0-beta")
	u.handle_body(body)
	_check("a newer release is announced", got.size() == 1 and u.info.get("tag") == "v99.0.0-beta")
	Q3UpdateCheck.skip("v99.0.0-beta")
	_check("skip is remembered", Q3UpdateCheck.skipped() == "v99.0.0-beta")
	var u2 := Q3UpdateCheck.new()
	add_child(u2)
	var again: Array = []
	u2.available.connect(func(i): again.append(i))
	u2.handle_body(body)
	_check("a skipped version is not offered again", again.is_empty())
	u2.handle_body(JSON.stringify([_release("v99.0.1-beta", {"prerelease": true})]))
	_check("a newer one still is", again.size() == 1)
	Q3UpdateCheck.skip("textures")
	_check("a tag that isn't a version is not stored", Q3UpdateCheck.skipped() == "v99.0.0-beta")
	u.free()
	u2.free()


func _bridge() -> void:
	var b := Q3MenuBridge.new()
	var posts: Array = []
	b.post.connect(func(m): posts.append(m))
	b.handle({"type": "ready"})
	_check("nothing is offered before a release is found", not posts.any(func(m): return m["type"] == "update"))
	var inst := Q3UpdateCheck.instance()
	inst.info = {"tag": "v0.4.0-beta", "version": "0.4.0-beta"}
	b.handle({"type": "ready"})
	var upd := posts.filter(func(m): return m["type"] == "update")
	_check("the page is told once a release is known", upd.size() == 1 and upd[0]["info"]["tag"] == "v0.4.0-beta")
	var pause := Q3MenuBridge.new()
	pause.context = "pause"
	var pp: Array = []
	pause.post.connect(func(m): pp.append(m))
	pause.handle({"type": "ready"})
	_check("the pause menu is never told", not pp.any(func(m): return m["type"] == "update"))
	b.handle({"type": "update_skip", "tag": "v0.4.2-beta"})
	_check("the page's skip reaches the skip list", Q3UpdateCheck.skipped() == "v0.4.2-beta")
	b.handle({"type": "update_skip", "tag": "../../x"})
	_check("a bad tag from the page is dropped", Q3UpdateCheck.skipped() == "v0.4.2-beta")
	inst.info = {}


func _settings() -> void:
	_check("Check for updates is on by default", Settings.get_value("network/check_updates") == true)
	_check("Include betas is off by default", Settings.get_value("network/update_betas") == false)


func _http(feed: String) -> void:
	var u := Q3UpdateCheck.new()
	u.feed_url = feed
	u.force = true
	add_child(u)
	var got: Array = []
	u.available.connect(func(i): got.append(i))
	Q3UpdateCheck.skip("v0.0.1")
	var t := Time.get_ticks_msec()
	u.check()
	while got.is_empty() and Time.get_ticks_msec() - t < 8000:
		await get_tree().process_frame
	_check("a real feed over HTTP is announced (%s)" % [got[0].get("tag") if not got.is_empty() else "nothing"], not got.is_empty())
	u.free()


func _check(what: String, ok: bool) -> void:
	_ok = _ok and ok
	print("  [%s] %s" % ["ok" if ok else "FAIL", what])
