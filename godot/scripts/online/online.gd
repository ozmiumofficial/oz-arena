extends Node
## Autoload "Online": friends, presence and parties (PLAN.md "Multiplayer and online services").
## Holds one Q3OnlineClient across scene changes, reconnects with backoff, and keeps presence in
## step with Session (menu, match, map editor) and the Profile settings. The web menu reaches it
## through Q3MenuBridge's "social" messages.

signal changed                                  ## status or social state; read to_dict()
signal notice(text: String, tone: String)
signal party_chat(msg: Dictionary)

## The deployed Worker (online/, Ozmium's personal Cloudflare account). Settings
## network/online_server overrides it; a local `npm run dev` in online/ is ws://127.0.0.1:8787.
const DEFAULT_SERVER := "wss://arena-online.fragnet.workers.dev"
const KEY_PATH := "user://online/identity.key"
const CHAT_KEEP := 50
## What the menu may ask the hub to do (online/src/hub.ts lists them).
const OPS := ["friend_add", "friend_accept", "friend_decline", "friend_cancel", "friend_remove",
	"party_invite", "party_accept", "party_decline", "party_leave", "party_kick", "party_promote", "party_chat", "sync"]

var client: Q3OnlineClient
var chat: Array = []                            ## the party's recent chat lines
var presence := {"status": "online", "detail": "In the menu", "join": ""}
var _retry_in := -1.0
var _backoff := 2.0
var _profile_in := -1.0


func _ready() -> void:
	client = Q3OnlineClient.new(KEY_PATH)
	client.status_changed.connect(_on_status)
	client.social.connect(func(_s): changed.emit())
	client.notice.connect(func(t, tone): notice.emit(t, tone))
	client.party_chat.connect(_on_chat)
	Settings.changed.connect(_on_setting)
	connect_now()


func enabled() -> bool:
	return bool(Settings.get_value("network/online"))


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
	for k in ["id", "code", "party", "text"]:
		if data.has(k):
			m[k] = String(data[k])
	return client.send(op, m)


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
	if _retry_in > 0.0:
		_retry_in -= delta
		if _retry_in <= 0.0:
			connect_now()
	if _profile_in > 0.0:
		_profile_in -= delta
		if _profile_in <= 0.0:
			client.send("profile", _profile())


func _on_status(status: String, why: String) -> void:
	if status != "offline":
		_retry_in = -1.0
	if status == "online":
		_backoff = 2.0
		client.send("presence", presence)
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


func _on_setting(key: String, _value: Variant) -> void:
	if key in ["network/online", "network/online_server"]:
		_backoff = 2.0
		connect_now()
	elif key in ["profile/name", "profile/model", "profile/team_color"]:
		_profile_in = 1.0   # typing a name sends one update, not one per letter


func _profile() -> Dictionary:
	return {"name": String(Settings.get_value("profile/name")), "model": String(Settings.get_value("profile/model")),
		"color": "#" + (Settings.get_value("profile/team_color") as Color).to_html(false)}
