class_name Q3OnlineClient
extends RefCounted
## One connection to the online hub (`online/`: a Cloudflare Worker and Durable Object, PLAN.md
## "Multiplayer and online services"). The Online autoload owns one; tests make several, each with
## its own key file.
##
## Identity without accounts: an RSA key pair made on first use and kept at key_path. Every
## connection sends the public key (base64 DER), a millisecond timestamp and an RSA-SHA256
## signature of "hub|<ts>" as handshake headers; the player id is derived from the key.
## Messages are JSON objects with a "type" (the list is at the top of online/src/hub.ts).

signal status_changed(status: String, reason: String)   ## "connecting", "online" or "offline"
signal social(state: Dictionary)                        ## the whole picture: me, friends, requests, party, invites
signal notice(text: String, tone: String)
signal party_chat(msg: Dictionary)
signal leaderboard(board: Dictionary)                   ## {board, scope, rows, me, total}

const PROTOCOL := "1"
const PING_EVERY := 25.0
## Close codes the hub uses: 4001 identity refused, 4002 this game is out of date.
const CLOSE_IDENTITY := 4001
const CLOSE_OUTDATED := 4002

var key_path := ""
var status := "offline"
var reason := ""
var close_code := 0
var state: Dictionary = {}
var _ws: WebSocketPeer
var _key: CryptoKey
var _ping_in := PING_EVERY


func _init(path: String) -> void:
	key_path = path


## Opens the connection. `base` is the server ("wss://..." or "ws://127.0.0.1:8787" for a local
## `npm run dev`); `profile` is {name, model, color}.
func open(base: String, profile: Dictionary) -> void:
	close(false)
	if not _ensure_key():
		_set_status("offline", "Couldn't create this PC's online identity.")
		return
	var ts := int(Time.get_unix_time_from_system() * 1000.0)
	var sig := Crypto.new().sign(HashingContext.HASH_SHA256, ("hub|%d" % ts).sha256_buffer(), _key)
	_ws = WebSocketPeer.new()
	_ws.handshake_headers = PackedStringArray([
		"x-proto: " + PROTOCOL,
		"x-key: " + public_key(),
		"x-ts: %d" % ts,
		"x-sig: " + Marshalls.raw_to_base64(sig),
		"x-name: " + String(profile.get("name", "Player")).uri_encode(),
		"x-model: " + String(profile.get("model", "sarge")),
		"x-color: " + String(profile.get("color", "#db2410")),
	])
	close_code = 0
	var err := _ws.connect_to_url(base.trim_suffix("/") + "/hub")
	if err != OK:
		_ws = null
		_set_status("offline", "Can't reach the online service.")
		return
	_set_status("connecting", "")


## Hangs up. `report` false skips the status signal (open() replacing a connection).
func close(report := true) -> void:
	if _ws:
		_ws.close()
		_ws = null
	state = {}
	if report and status != "offline":
		_set_status("offline", "")


## Runs the socket; call every frame.
func poll(delta: float) -> void:
	if _ws == null:
		return
	_ws.poll()
	match _ws.get_ready_state():
		WebSocketPeer.STATE_OPEN:
			if status != "online":
				_set_status("online", "")
			while _ws.get_available_packet_count() > 0:
				_receive(_ws.get_packet().get_string_from_utf8())
			_ping_in -= delta
			if _ping_in <= 0.0:
				_ping_in = PING_EVERY
				_ws.send_text('{"type":"ping"}')
		WebSocketPeer.STATE_CLOSED:
			close_code = _ws.get_close_code()
			var why := _ws.get_close_reason()
			_ws = null
			state = {}
			if why == "":
				why = "Can't reach the online service." if status == "connecting" else "Lost the connection to the online service."
			_set_status("offline", why)


func send(type: String, data := {}) -> bool:
	if status != "online":
		return false
	var m := data.duplicate()
	m["type"] = type
	return _ws.send_text(JSON.stringify(m)) == OK


## Base64 DER of the public key: the PEM body without its header lines.
func public_key() -> String:
	if not _ensure_key():
		return ""
	var pem := _key.save_to_string(true)
	var body := ""
	for line in pem.split("\n"):
		if not line.begins_with("-----"):
			body += line.strip_edges()
	return body


func _ensure_key() -> bool:
	if _key:
		return true
	_key = CryptoKey.new()
	if FileAccess.file_exists(key_path) and _key.load(key_path) == OK:
		return true
	DirAccess.make_dir_recursive_absolute(key_path.get_base_dir())
	_key = Crypto.new().generate_rsa(2048)
	if _key == null or _key.save(key_path) != OK:
		_key = null
		return false
	return true


func _receive(text: String) -> void:
	var m: Variant = JSON.parse_string(text)
	if not m is Dictionary:
		return
	match String(m.get("type", "")):
		"social":
			state = m
			social.emit(m)
		"notice":
			notice.emit(String(m.get("text", "")), String(m.get("tone", "info")))
		"party_chat":
			party_chat.emit(m)
		"leaderboard":
			leaderboard.emit(m)


func _set_status(s: String, why: String) -> void:
	status = s
	reason = why
	status_changed.emit(s, why)
