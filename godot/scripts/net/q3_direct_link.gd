class_name Q3DirectLink
extends RefCounted
## A direct WebRTC connection between two PCs for one map download (Q3MapFetch), so the file never
## goes through the relay (Ozmium, 2026-10-25: map downloads mustn't use the relay's budget). Opt-in
## on both sides (Settings > Network > Allow direct map downloads; dedicated servers always allow
## it), because unlike the match's connection (Q3RtcPeer, relay only) the two ends learn each
## other's IP address. The match itself stays on the relay.
##
## Its offer, answer and candidates travel over the relayed connection the two already share (the
## files channel); `outgoing()` hands them to the caller and `apply()` takes the other side's. Public
## STUN servers only tell each side its own address, for NAT traversal: they carry no data. Some
## NATs (both sides behind symmetric ones) can't connect directly: the download then fails, saying so.

const STUN := ["stun:stun.cloudflare.com:3478", "stun:stun.l.google.com:19302"]
const CHANNEL_ID := 1
const OPEN_TIMEOUT := 20.0
const BUFFER := 1 << 20     ## bytes queued before send() waits

var state := "connecting"   ## connecting, open, closed or failed
var error := ""
var pc: WebRTCPeerConnection
var files: WebRTCDataChannel
var _out: Array = []        ## signaling for the other side: {"sdp", "kind"} or {"cand": [mid, index, candidate]}
var _queued: Array = []     ## candidates that came before the remote description
var _remote_set := false
var _age := 0.0
var _was_open := false


func start(offerer: bool) -> bool:
	pc = WebRTCPeerConnection.new()
	if pc.initialize({"iceServers": [{"urls": STUN}]}) != OK:
		return _failed("WebRTC isn't available.")
	pc.session_description_created.connect(_on_description)
	pc.ice_candidate_created.connect(_on_candidate)
	files = pc.create_data_channel("map", {"negotiated": true, "id": CHANNEL_ID, "ordered": true})
	if files == null:
		return _failed("Couldn't open the download channel.")
	files.write_mode = WebRTCDataChannel.WRITE_MODE_BINARY
	if offerer:
		pc.create_offer()
	return true


## Signaling to send to the other side since the last call.
func outgoing() -> Array:
	var o := _out
	_out = []
	return o


## One of the other side's signaling messages.
func apply(m: Dictionary) -> void:
	if pc == null:
		return
	if m.has("sdp"):
		if pc.set_remote_description(String(m.get("kind", "")), String(m["sdp"])) != OK:
			_failed("The other side's connection details were refused.")
			return
		_remote_set = true
		for c in _queued:
			pc.add_ice_candidate(c[0], c[1], c[2])
		_queued.clear()
	elif m.has("cand"):
		var c: Array = m["cand"]
		if c.size() == 3:
			if _remote_set:
				pc.add_ice_candidate(String(c[0]), int(c[1]), String(c[2]))
			else:
				_queued.append([String(c[0]), int(c[1]), String(c[2])])


func poll(delta: float) -> void:
	if pc == null or state == "failed" or state == "closed":
		return
	pc.poll()
	var ready := files.get_ready_state()
	if ready == WebRTCDataChannel.STATE_OPEN:
		state = "open"
		_was_open = true
	elif _was_open and (ready == WebRTCDataChannel.STATE_CLOSING or ready == WebRTCDataChannel.STATE_CLOSED):
		# the other side closed it (the joiner, once it has the file): what arrived can still be read
		state = "closed"
		error = "The direct connection was lost."
	elif pc.get_connection_state() == WebRTCPeerConnection.STATE_FAILED:
		_failed("Couldn't connect the two PCs directly (a strict NAT or firewall on one side).")
	elif state == "connecting":
		_age += delta
		if _age > OPEN_TIMEOUT:
			_failed("Couldn't connect the two PCs directly (timed out).")


func receive() -> Array:
	var got: Array = []
	if files == null or (state != "open" and state != "closed"):
		return got
	while files.get_available_packet_count() > 0:
		got.append(files.get_packet())
	return got


## Queues one message; false when the link isn't open or already holds BUFFER bytes.
func send(data: PackedByteArray) -> bool:
	if files == null or state != "open" or files.get_buffered_amount() > BUFFER:
		return false
	return files.put_packet(data) == OK


func close() -> void:
	if pc != null:
		if pc.session_description_created.is_connected(_on_description):
			pc.session_description_created.disconnect(_on_description)
		if pc.ice_candidate_created.is_connected(_on_candidate):
			pc.ice_candidate_created.disconnect(_on_candidate)
		if files != null:
			files.close()
		pc.close()
		files = null
		pc = null
	if state != "failed":
		state = "closed"


func _on_description(kind: String, sdp: String) -> void:
	pc.set_local_description(kind, sdp)
	_out.append({"sdp": sdp, "kind": kind})


func _on_candidate(mid: String, index: int, candidate: String) -> void:
	_out.append({"cand": [mid, index, candidate]})


func _failed(why: String) -> bool:
	error = why
	state = "failed"
	return false
