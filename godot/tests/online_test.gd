extends Node
## Friends and parties end to end: two Q3OnlineClients (fresh identities) against a running hub,
## plus the Online autoload itself and the menu bridge's "social" messages.
##   cd online && npm run dev           (a local hub on 127.0.0.1:8787, the default here)
##   godot --headless --path . res://tests/online_test.tscn [-- server=wss://arena-online.fragnet.workers.dev]
## Against the live hub it leaves two test players behind, so local is the default.
## Prints PASS/FAIL per check and "ONLINE TEST ok|FAILED"; exit code 1 on failure.

const TIMEOUT := 8.0

var a: Q3OnlineClient
var b: Q3OnlineClient
var failed := 0
var chats: Array = []
var notices: Array = []
var server := ""


func _ready() -> void:
	var args := {}
	for arg in OS.get_cmdline_user_args():
		if arg.contains("="):
			args[arg.get_slice("=", 0)] = arg.get_slice("=", 1)
	server = String(args.get("server", "ws://127.0.0.1:8787"))
	for f in ["user://online_test/a.key", "user://online_test/b.key"]:
		if FileAccess.file_exists(f):
			DirAccess.remove_absolute(f)
	a = Q3OnlineClient.new("user://online_test/a.key")
	b = Q3OnlineClient.new("user://online_test/b.key")
	b.party_chat.connect(func(m): chats.append(m))
	a.notice.connect(func(t, _tone): notices.append(t))
	a.open(server, {"name": "^4Tester A", "model": "visor", "color": "#2266ff"})
	b.open(server, {"name": "Tester B", "model": "orbb", "color": "#22ff66"})
	await _run()
	print("ONLINE TEST ", "ok" if failed == 0 else "FAILED (%d)" % failed)
	get_tree().quit(0 if failed == 0 else 1)


func _process(delta: float) -> void:
	if a:
		a.poll(delta)
		b.poll(delta)


func _run() -> void:
	var up := await _until(func(): return not a.state.is_empty() and not b.state.is_empty())
	_check(up, "both clients signed in (%s / %s)" % [a.reason, b.reason])
	if not up:
		return
	var code := String(a.state["me"]["code"])
	_check(code.length() == 9 and code[4] == "-", "friend code " + code)
	_check(a.state["me"]["name"] == "^4Tester A", "name with color codes and a space survives")
	_check(a.public_key() == Q3OnlineClient.new("user://online_test/a.key").public_key(), "identity reloads from its key file")

	b.send("friend_add", {"code": code})
	_check(await _until(func(): return a.state.get("incoming", []).size() == 1), "A sees B's request")
	a.send("friend_accept", {"id": b.state["me"]["id"]})
	_check(await _until(func(): return _friend(a, "Tester B").get("online", false)), "friends, B online")
	_check(notices.any(func(t): return String(t).contains("wants to be your friend")), "A got a notice for the request")

	b.send("presence", {"status": "match", "detail": "Duel on q3tourney2"})
	_check(await _until(func(): return _friend(a, "Tester B").get("detail", "") == "Duel on q3tourney2"), "A sees B's match")

	a.send("party_invite", {"id": b.state["me"]["id"]})
	_check(await _until(func(): return b.state.get("invites", []).size() == 1), "B gets the invite")
	b.send("party_accept", {"party": b.state["invites"][0]["party"]})
	_check(await _until(func(): return _party_size(a) == 2 and _party_size(b) == 2), "party of 2")
	a.send("party_chat", {"text": "gl hf"})
	_check(await _until(func(): return chats.size() == 1), "B hears party chat")

	# the autoload and the menu bridge, against the same hub
	Settings.set_for_run("network/online_server", server)
	_check(await _until(func(): return Online.client.status == "online" and not Online.client.state.is_empty()), "Online autoload connected (%s)" % Online.client.reason)
	var bridge := Q3MenuBridge.new()
	var posts: Array = []
	bridge.post.connect(func(m): posts.append(m))
	bridge.handle({"type": "ready"})
	_check(posts.any(func(m): return m.get("type") == "social" and m.get("status") == "online"), "bridge sends the social state on ready")
	bridge.handle({"type": "social", "op": "friend_add", "code": code})
	_check(await _until(func(): return posts.any(func(m): return m.get("type") == "social" and m["state"].get("outgoing", []).size() == 1)), "bridge op reaches the hub (request to A)")
	bridge.handle({"type": "social", "op": "friend_cancel", "id": a.state["me"]["id"]})
	_check(await _until(func(): return a.state.get("incoming", []).size() == 0), "bridge cancel clears A's request")
	# the autoload sent this PC's career on connect; the level board places it, and ranked has nobody yet
	bridge.handle({"type": "social", "op": "leaderboard", "board": "level", "scope": "global"})
	var board_of := func(kind: String) -> Dictionary:
		for m in posts:
			if m.get("type") == "leaderboard" and m["board"].get("board") == kind:
				return m["board"]
		return {}
	_check(await _until(func(): return board_of.call("level").get("me") is Dictionary), "level leaderboard places this PC")
	var mine: Dictionary = board_of.call("level").get("me", {})
	_check(int(mine.get("xp", -1)) == Q3Profile.total_xp() and not Q3Cards.part("emblems", String(mine.get("card", {}).get("emblem", ""))).is_empty(),
			"with its XP and a known card (#%d, %d XP)" % [int(mine.get("rank", 0)), int(mine.get("xp", 0))])
	bridge.handle({"type": "social", "op": "leaderboard", "board": "ranked", "scope": "friends"})
	_check(await _until(func(): return not board_of.call("ranked").is_empty()) and board_of.call("ranked")["rows"].is_empty(), "ranked leaderboard empty")
	Online.scene_changed(Session.MAP_EDITOR)
	await _until(func(): return false, 0.5)
	# the test mustn't leave its server in the player's settings (they save on a timer; the test quits first)
	Settings.reset("network/online_server")
	Settings.save()

	b.send("party_leave")
	_check(await _until(func(): return a.state.get("party") == null), "party ends when B leaves")
	a.send("friend_remove", {"id": b.state["me"]["id"]})
	_check(await _until(func(): return b.state.get("friends", []).is_empty()), "unfriended")
	a.close()
	b.close()


func _friend(c: Q3OnlineClient, name: String) -> Dictionary:
	for f in c.state.get("friends", []):
		if f["name"] == name:
			return f
	return {}


func _party_size(c: Q3OnlineClient) -> int:
	var p: Variant = c.state.get("party")
	return p["members"].size() if p is Dictionary else 0


func _until(cond: Callable, secs := TIMEOUT) -> bool:
	var t := 0.0
	while t < secs:
		if cond.call():
			return true
		await get_tree().process_frame
		t += get_process_delta_time()
	return cond.call()


func _check(ok: bool, what: String) -> void:
	print(("PASS " if ok else "FAIL ") + what)
	if not ok:
		failed += 1
