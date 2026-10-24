class_name Q3RtcPeer
extends RefCounted
## One WebRTC connection to another player (MULTIPLAYER_PLAN.md M4), as the godot-webrtc-native
## extension (libdatachannel) provides it, carrying id's client packets over a single unreliable,
## unordered data channel. Both ends create the same channel (negotiated, id 1), so there is no
## in-band channel setup. The joiner makes the offer; the host answers.
##
## Privacy: the connection is meant to run through Cloudflare's TURN relay only, so neither player
## sees the other's address. The extension has no relay-only switch, so it is enforced where it can
## be: this class never hands a non-relay candidate to the signaling (a host or server-reflexive
## candidate carries an address) and ignores any it is given, and the hub strips candidate lines and
## the connection address from SDPs and drops non-relay candidates (online/src/hub.ts netSignal). The
## peer therefore only ever learns the relay's address. `relay_only` is false only on a hub run with
## NET_DIRECT=1, for tests on one machine.

signal description_ready(kind: String, sdp: String)             ## "offer" or "answer", for the signaling
signal candidate_ready(mid: String, index: int, candidate: String)

const CHANNEL_ID := 1
const CONNECT_TIMEOUT := 20.0
const MAX_PACKET := 2048

var pc: WebRTCPeerConnection
var channel: WebRTCDataChannel
var relay_only := true
var state := "new"          ## new, connecting, open, closed or failed
var error := ""
var sent := 0
var received := 0
var _remote_set := false
var _queued: Array = []     ## candidates that arrived before the remote description
var _age := 0.0
var _was_open := false

## Tests only (tests/relay_bandwidth.gd): (url, offerer) -> the URL to use instead, to put tools/perf/turn_meter.py
## between this peer and the relay. Empty in the game.
static var turn_rewrite := Callable()


## ice: {servers: [{urls, username, credential}], relay_only}. offerer: this side makes the offer.
func start(ice: Dictionary, offerer: bool) -> bool:
	relay_only = bool(ice.get("relay_only", true))
	pc = WebRTCPeerConnection.new()
	var servers: Array = []
	for s in ice.get("servers", []):
		var urls: Array = []
		for u in s.get("urls", []):
			var url := String(u)
			# libjuice relays over UDP; TCP and TLS URLs only slow the gathering down
			if url.begins_with("turns:") or url.contains("transport=tcp"):
				continue
			if turn_rewrite.is_valid():
				url = turn_rewrite.call(url, offerer)
			if not urls.has(url):
				urls.append(url)
		if urls.size():
			servers.append({"urls": urls, "username": s.get("username", ""), "credential": s.get("credential", "")})
	if relay_only and servers.is_empty():
		error = "No relay servers."
		state = "failed"
		return false
	var err := pc.initialize({"iceServers": servers})
	if err != OK:
		error = "WebRTC isn't available (error %d)." % err
		state = "failed"
		return false
	pc.session_description_created.connect(_on_description)
	pc.ice_candidate_created.connect(_on_candidate)
	channel = pc.create_data_channel("q3", {"negotiated": true, "id": CHANNEL_ID, "ordered": false, "maxRetransmits": 0})
	if channel == null:
		error = "Couldn't open the data channel."
		state = "failed"
		return false
	channel.write_mode = WebRTCDataChannel.WRITE_MODE_BINARY
	state = "connecting"
	if offerer:
		pc.create_offer()
	return true


func _on_description(kind: String, sdp: String) -> void:
	pc.set_local_description(kind, sdp)
	description_ready.emit(kind, sdp)


func _on_candidate(mid: String, index: int, candidate: String) -> void:
	if relay_only and not candidate.contains(" typ relay"):
		return   # an address of ours: never leaves this PC
	candidate_ready.emit(mid, index, candidate)


## The other side's offer or answer.
func remote_description(kind: String, sdp: String) -> void:
	if pc == null:
		return
	if pc.set_remote_description(kind, sdp) != OK:
		error = "The other side's connection details were refused."
		state = "failed"
		return
	_remote_set = true
	for c in _queued:
		pc.add_ice_candidate(c[0], c[1], c[2])
	_queued.clear()


func remote_candidate(mid: String, index: int, candidate: String) -> void:
	if pc == null or (relay_only and not candidate.contains(" typ relay")):
		return
	if _remote_set:
		pc.add_ice_candidate(mid, index, candidate)
	else:
		_queued.append([mid, index, candidate])


## Runs the connection; call every frame. Updates `state`.
func poll(delta: float) -> void:
	if pc == null or channel == null or state == "closed" or state == "failed":
		return
	pc.poll()
	var ready := channel.get_ready_state()
	if ready == WebRTCDataChannel.STATE_OPEN:
		state = "open"
		_was_open = true
	elif ready == WebRTCDataChannel.STATE_CLOSING or ready == WebRTCDataChannel.STATE_CLOSED:
		if _was_open:
			state = "closed"
			error = "The connection was lost."
	if pc.get_connection_state() == WebRTCPeerConnection.STATE_FAILED:
		state = "failed" if not _was_open else "closed"
		error = "Couldn't connect to the other player." if not _was_open else "The connection was lost."
	elif pc.get_connection_state() == WebRTCPeerConnection.STATE_CLOSED and state != "failed":
		state = "closed"
	if state == "connecting":
		_age += delta
		if _age > CONNECT_TIMEOUT:
			state = "failed"
			error = "Couldn't connect to the other player (timed out)."


## Packets that arrived since the last call.
func receive() -> Array:
	var out: Array = []
	if channel == null or state != "open":
		return out
	while channel.get_available_packet_count() > 0:
		out.append(channel.get_packet())
	received += out.size()
	return out


func send(data: PackedByteArray) -> void:
	if channel != null and state == "open" and data.size() <= MAX_PACKET and channel.get_ready_state() == WebRTCDataChannel.STATE_OPEN:
		channel.put_packet(data)
		sent += 1


func close() -> void:
	if pc != null:
		# the connection's signals call back into this object and it holds the connection: break the cycle, or
		# libdatachannel waits for it at exit forever
		if pc.session_description_created.is_connected(_on_description):
			pc.session_description_created.disconnect(_on_description)
		if pc.ice_candidate_created.is_connected(_on_candidate):
			pc.ice_candidate_created.disconnect(_on_candidate)
		if channel != null:
			channel.close()
		pc.close()
		channel = null
		pc = null
	state = "closed"
