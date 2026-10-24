extends Node
## Autoload "Online": friends, presence, parties and leaderboards (PLAN.md "Multiplayer and online services").
## Holds one Q3OnlineClient across scene changes, reconnects with backoff, and keeps presence in
## step with Session (menu, match, map editor) and the Profile settings. The web menu reaches it
## through Q3MenuBridge's "social" messages. Your career (total XP, card, showcase stats) goes to the
## hub on connect and whenever it changes, for the level leaderboard.

signal changed                                  ## status or social state; read to_dict()
signal notice(text: String, tone: String)
signal party_chat(msg: Dictionary)
signal leaderboard(board: Dictionary)           ## a board the menu asked for, cards checked against the catalog
signal net(msg: Dictionary)                     ## a match message from the hub (net_matches, net_request, net_signal ...)
signal matches(msg: Dictionary)                 ## the directory: {matches: [...], region}
signal join_state(state: Dictionary)            ## joining a match: {status, reason, host}
signal queue_changed                            ## the matchmaking queue, a match found, a result or the ratings changed (read queue_state, ratings, last_result)

## The deployed Worker (online/, Ozmium's personal Cloudflare account). Settings
## network/online_server overrides it; a local `npm run dev` in online/ is ws://127.0.0.1:8787.
const DEFAULT_SERVER := "wss://arena-online.fragnet.workers.dev"
const KEY_PATH := "user://online/identity.key"
const CHAT_KEEP := 50
## What the menu may ask the hub to do (online/src/hub.ts lists them).
const OPS := ["friend_add", "friend_accept", "friend_decline", "friend_cancel", "friend_remove",
	"party_invite", "party_accept", "party_decline", "party_leave", "party_kick", "party_promote", "party_chat", "sync",
	"leaderboard"]

var client: Q3OnlineClient
var joiner: Q3NetJoin                           ## the join in progress, if any
var queue_state: Dictionary = {}                ## the hub's last queue_state ({status: queued|idle|locked|refused, ...})
var ratings: Dictionary = {}                    ## the hub's last ratings message: {season, placements, queues: {duel: {rating, rated, wins, losses, peak}}}
var last_result: Dictionary = {}                ## the last match_result (voided or not)
var _found_join := {}                           ## a matchmade match to join: {host, until}
var _found_try := 0.0
var _join_status := ""
var _ice: Dictionary = {}                       ## the last relay credentials the hub minted for a join
var _ice_at := 0.0
var chat: Array = []                            ## the party's recent chat lines
var presence := {"status": "online", "detail": "In the menu", "join": ""}
var _retry_in := -1.0
var _backoff := 2.0
var _profile_in := -1.0
var _career_in := -1.0
var _clip_render := false                       ## a clip render's copy (Q3ClipExport): the player's own game holds the connection


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		_clip_render = _clip_render or String(a).begins_with("clip=") or String(a).begins_with("dedicated=")   # a dedicated server has its own identity
	client = Q3OnlineClient.new(KEY_PATH)
	client.status_changed.connect(_on_status)
	client.social.connect(func(_s): changed.emit())
	client.notice.connect(func(t, tone): notice.emit(t, tone))
	client.party_chat.connect(_on_chat)
	client.leaderboard.connect(_on_leaderboard)
	client.queue.connect(_on_queue_message)
	client.net.connect(func(m):
		net.emit(m)
		if String(m.get("type", "")) == "net_matches":
			matches.emit(m))
	Settings.changed.connect(_on_setting)
	connect_now()


## Hang up while the engine is still up: this also erases a hostname lookup still waiting, which
## would crash Godot's exit (Q3OnlineClient explains).
func _exit_tree() -> void:
	if client:
		client.close(false)


func enabled() -> bool:
	return bool(Settings.get_value("network/online")) and not _clip_render


func server() -> String:
	var s := String(Settings.get_value("network/online_server")).strip_edges()
	return s if s != "" else DEFAULT_SERVER


func connect_now() -> void:
	_retry_in = -1.0
	if enabled():
		client.open(server(), _profile())
	else:
		client.close()
		changed.emit()


## The menu's requests. Returns false when the op is unknown or we're offline.
func request(op: String, data: Dictionary) -> bool:
	match op:
		"retry":
			_backoff = 2.0
			connect_now()
			return true
		"copy_code":
			var code := String(client.state.get("me", {}).get("code", ""))
			if code != "" and DisplayServer.get_name() != "headless":
				DisplayServer.clipboard_set(code)
			return code != ""
	if not op in OPS:
		return false
	var m := {}
	for k in ["id", "code", "party", "text", "board", "scope"]:
		if data.has(k):
			m[k] = String(data[k])
	return client.send(op, m)


## Queues for a match: `modes` (duel, ffa, tdm, ctf), ranked or casual. The hub answers with queue_state, then match_found.
func queue_join(modes: Array, ranked: bool) -> bool:
	if not client.send("queue_join", {"modes": modes, "ranked": ranked, "region": String(Settings.get_value("network/region")), "rtt": client.rtt}):
		return false
	queue_state = {"status": "queued", "since": Time.get_unix_time_from_system() * 1000.0, "modes": modes, "ranked": ranked}
	queue_changed.emit()
	return true


func queue_leave() -> void:
	client.send("queue_leave")
	queue_state = {"status": "idle"}
	queue_changed.emit()


func _on_queue_message(m: Dictionary) -> void:
	match String(m.get("type", "")):
		"queue_state":
			queue_state = m
		"ratings":
			ratings = m
			var duel: Variant = m.get("queues", {}).get("duel")
			if duel is Dictionary:
				Q3Profile.set_ranked(duel)
		"match_result":
			last_result = m
		"match_found":
			queue_state = {"status": "found", "match": m.get("match", "")}
			_on_match_found(m)
	queue_changed.emit()


## The matchmaker made a match for us. The host starts it (a private listing the others find); the rest join it as soon as it
## is listed, trying every couple of seconds for a minute.
func _on_match_found(m: Dictionary) -> void:
	Session.matchmade = m
	UiSound.play("move")
	if bool(m.get("you_host", false)):
		var s := Q3MatchSetup.from_found(m)
		s.player = Session.new_setup(s.mode, s.map).player
		var errs := Session.start_match(s)
		if errs.size():
			push_warning("matchmaking: couldn't host: " + "; ".join(errs))
	else:
		_found_join = {"host": String(m.get("host", "")), "until": Time.get_unix_time_from_system() + 60.0}
		_found_try = 0.0


## After a match: tell the hub we are in it (it waits for everyone), and report how it ended.
func match_joined(id: String) -> void:
	client.send("match_joined", {"match": id})


func match_report(id: String, me: int, rows: Array, left: bool, peers: Dictionary) -> void:
	client.send("match_report", {"match": id, "me": me, "rows": rows, "left": left, "peers": peers})


## The version a host lists and a joiner sends: a match refuses another version (the hub says so).
func game_version() -> String:
	return String(ProjectSettings.get_setting("application/config/version", ""))


## Asks the hub for the open matches; the answer arrives as `matches`. `region` "" is the setting's (auto = yours).
func net_browse(region := "") -> bool:
	var r := region if region != "" else String(Settings.get_value("network/region"))
	return client.send("net_browse", {} if r in ["", "auto"] else {"region": r})


## Joins a listed match: relay credentials, the host's say-so, the WebRTC connection; then Gameplay starts as a
## client (Session.join). `join_state` reports it (status asking / connecting / failed / open).
func net_join(host: String, password := "") -> void:
	if joiner != null:
		return
	joiner = Q3NetJoin.new()
	add_child(joiner)
	joiner.finished.connect(_on_join_finished)
	_join_status = ""
	joiner.ice_minted.connect(_on_ice)
	var fresh := Time.get_unix_time_from_system() - _ice_at < 2700.0   # they last an hour
	joiner.begin(client, host, password, game_version(), _ice if fresh else {})
	join_state.emit({"status": "asking", "reason": "", "host": host})


func _on_ice(ice: Dictionary) -> void:
	_ice = ice
	_ice_at = Time.get_unix_time_from_system()


func net_cancel() -> void:
	if joiner != null:
		joiner._fail("Cancelled.")


func _on_join_finished(ok: bool, transport: Q3RtcTransport, why: String) -> void:
	var host := joiner.host if joiner != null else ""
	if joiner != null:
		joiner.queue_free()
		joiner = null
	if ok:
		_found_join = {}
		join_state.emit({"status": "open", "reason": "", "host": host})
		Session.join_match(transport)
	elif not _found_join.is_empty() and why in ["That match is gone.", "The host didn't answer."]:
		pass   # the host has not listed it yet: the loop tries again
	else:
		_found_join = {}
		join_state.emit({"status": "failed", "reason": why, "host": host})


## The career changed (a match ended, the card was edited): the hub hears about it shortly after.
func career_changed() -> void:
	_career_in = 1.0


## Session calls this on every scene change.
func scene_changed(path: String) -> void:
	match path:
		Session.GAMEPLAY:
			var detail := "In a match"
			if Session.setup:
				detail = Session.setup.summary().split("\n")[0]
			set_presence("match", detail)
		Session.MAP_EDITOR:
			set_presence("editor", "Map editor")
		_:
			set_presence("online", "In the menu")


func set_presence(status: String, detail: String, join := "") -> void:
	presence = {"status": status, "detail": detail, "join": join}
	client.send("presence", presence)


## Everything the menu draws: connection status and the hub's last picture.
func to_dict() -> Dictionary:
	var status := client.status if enabled() else "disabled"
	return {"status": status, "reason": client.reason, "state": client.state, "chat": chat}


func _process(delta: float) -> void:
	client.poll(delta)
	if not _found_join.is_empty():
		_found_try -= delta
		if Time.get_unix_time_from_system() > float(_found_join["until"]):
			_found_join = {}
			Session.matchmade = {}
			join_state.emit({"status": "failed", "reason": "The match never came up.", "host": ""})
		elif _found_try <= 0.0 and joiner == null:
			_found_try = 2.0
			net_join(String(_found_join["host"]))
	if joiner != null and joiner.status != _join_status:
		_join_status = joiner.status
		if _join_status in ["connecting", "relay"]:
			join_state.emit({"status": "connecting", "reason": "", "host": joiner.host})
	if _retry_in > 0.0:
		_retry_in -= delta
		if _retry_in <= 0.0:
			connect_now()
	if _profile_in > 0.0:
		_profile_in -= delta
		if _profile_in <= 0.0:
			client.send("profile", _profile())
	if _career_in > 0.0:
		_career_in -= delta
		if _career_in <= 0.0:
			client.send("career", _career())


func _on_status(status: String, why: String) -> void:
	if status != "offline":
		_retry_in = -1.0
	if status == "online":
		_backoff = 2.0
		client.send("presence", presence)
		client.send("career", _career())
	elif status == "offline" and enabled():
		print("online: offline (", why, ")")
		# an outdated game stays offline; anything else tries again, backing off to a minute
		if client.close_code != Q3OnlineClient.CLOSE_OUTDATED and _retry_in < 0.0:
			_retry_in = _backoff
			_backoff = minf(_backoff * 2.0, 60.0)
	if status == "online":
		print("online: connected to ", server())
	if status != "online":
		chat.clear()
	changed.emit()


func _on_chat(m: Dictionary) -> void:
	chat.append({"from": m.get("from", {}), "text": m.get("text", ""), "at": m.get("at", 0)})
	if chat.size() > CHAT_KEEP:
		chat = chat.slice(-CHAT_KEEP)
	party_chat.emit(m)


## Cards from other games may name parts this build doesn't have; those go back to the default.
func _on_leaderboard(m: Dictionary) -> void:
	var b := m.duplicate(true)
	for r in b.get("rows", []):
		r["card"] = Q3Cards.known_parts(r.get("card", {}))
	if b.get("me") is Dictionary:
		b["me"]["card"] = Q3Cards.known_parts(b["me"].get("card", {}))
	leaderboard.emit(b)


func _career() -> Dictionary:
	var shown := Q3Profile.card()
	var stats := {}
	for id in shown.get("stats", []):
		stats[id] = Q3Cards.stat_value(id, Q3Profile.data())
	return {"xp": Q3Profile.total_xp(), "card": shown, "stats": stats}


func _on_setting(key: String, _value: Variant) -> void:
	if key in ["network/online", "network/online_server"]:
		_backoff = 2.0
		connect_now()
	elif key in ["profile/name", "profile/model", "profile/team_color"]:
		_profile_in = 1.0   # typing a name sends one update, not one per letter


func _profile() -> Dictionary:
	return {"name": String(Settings.get_value("profile/name")), "model": String(Settings.get_value("profile/model")),
		"color": "#" + (Settings.get_value("profile/team_color") as Color).to_html(false)}
