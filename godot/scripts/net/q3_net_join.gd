class_name Q3NetJoin
extends Node
## Joining a listed match (MULTIPLAYER_PLAN.md M4), from asking to a working data channel:
##   1. net_ice: relay credentials from the hub;
##   2. net_join: the host is asked and decides (password, room, version);
##   3. net_accepted: this side makes the WebRTC offer and the two trade details through the hub;
##   4. the data channel opens: `finished` hands over a Q3RtcTransport for Session.join.
## `status` says where it is (for the menu's "Connecting ..."); `finished(false, null, why)` explains a failure.

signal finished(ok: bool, transport: Q3RtcTransport, reason: String)
signal ice_minted(ice: Dictionary)   ## fresh relay credentials, for the next join to reuse

const ASK_TIMEOUT := 30.0       ## the host has this long to answer
const TOTAL_TIMEOUT := 60.0

var client: Q3OnlineClient
var host := ""
var password := ""
var version := ""
var status := "starting"        ## starting, relay, asking, connecting, open or failed
var reason := ""
var _ice: Dictionary = {}
var _peer: Q3RtcPeer
var _link: Q3RtcLink
var _age := 0.0
var _done := false
var _host_left := 0.0           ## s the host's relay credentials had left when it accepted


## cached_ice: relay credentials already minted (they last an hour), so a retry doesn't ask the hub again.
func begin(online_client: Q3OnlineClient, host_pid: String, pw := "", game_version := "", cached_ice := {}) -> void:
	client = online_client
	host = host_pid
	password = pw
	version = game_version
	client.net.connect(_on_net)
	if not cached_ice.is_empty():
		_ice = cached_ice
		status = "asking"
		if not client.send("net_join", {"host": host, "password": password, "version": version}):
			_fail("Not connected to the online service.")
		return
	status = "relay"
	if not client.send("net_ice"):
		_fail("Not connected to the online service.")


func _on_net(m: Dictionary) -> void:
	if _done:
		return
	match String(m.get("type", "")):
		"net_ice":
			if m.has("error"):
				_fail(String(m["error"]))
				return
			_ice = m.duplicate()
			_ice["at"] = Time.get_unix_time_from_system()   # when they end decides the handover (Q3RtcTransport)
			ice_minted.emit(_ice)
			status = "asking"
			client.send("net_join", {"host": host, "password": password, "version": version})
		"net_refused":
			if String(m.get("host", "")) == host:
				_fail(String(m.get("reason", "The host refused you.")))
		"net_accepted":
			if String(m.get("host", "")) == host and not _ice.is_empty() and _peer == null:
				status = "connecting"
				_host_left = float(m.get("expires_in", 0))
				_peer = Q3RtcPeer.new()
				_link = Q3RtcLink.new(client, host, _peer)
				if not _peer.start(_ice, true):
					_fail(_peer.error)
		"net_gone":
			if String(m.get("pid", "")) == host:
				_fail("The host left.")


func _process(delta: float) -> void:
	if _done:
		return
	_age += delta
	if _peer != null:
		_peer.poll(delta)
		if _peer.state == "open":
			_succeed()
		elif _peer.state == "failed" or _peer.state == "closed":
			_fail(_peer.error if _peer.error != "" else "Couldn't connect to the host.")
	elif _age > ASK_TIMEOUT:
		_fail("The host didn't answer.")
	if _age > TOTAL_TIMEOUT:
		_fail("Couldn't connect to the host (timed out).")


func _succeed() -> void:
	_done = true
	status = "open"
	var t := Q3RtcTransport.new(_peer, _link)
	t.hub = client
	t.host = host
	t.version = version
	t.expires_at = Q3RtcTransport.credentials_end(_ice, _host_left)
	_disconnect()
	finished.emit(true, t, "")


func _fail(why: String) -> void:
	if _done:
		return
	_done = true
	status = "failed"
	reason = why
	if _peer != null:
		_peer.close()
	if _link != null:
		_link.release()
	if client != null:
		client.send("net_leave", {"peer": host})
	_disconnect()
	finished.emit(false, null, why)


func _disconnect() -> void:
	if client != null and client.net.is_connected(_on_net):
		client.net.disconnect(_on_net)
