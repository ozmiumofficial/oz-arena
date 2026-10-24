class_name Q3RtcTransport
extends Q3Transport
## A Q3Transport over a WebRTC data channel to the host (Q3RtcPeer). Gameplay's Q3NetFeed polls it
## each frame; the peer is polled here, so nothing else has to.
##
## The credential handover: the relay drops a connection shortly after either side's credentials expire, so
## RENEW_LEAD before the earlier of the two (`expires_at`) this side asks the host for a second connection on fresh
## credentials (net_join with renew; the hub's link and the host's slot for us stay), and when it opens, packets
## move to it and the old one closes. To id's netchan nothing changes: the host feeds both into the same engine peer.

const RENEW_LEAD := 600.0          ## s before the credentials end
const RENEW_TIMEOUT := 30.0
const RENEW_RETRY := 60.0

var peer: Q3RtcPeer
var link: Q3RtcLink          ## kept alive with the transport: its signaling goes with it
var hub: Q3OnlineClient
var host := ""
var version := ""
var expires_at := 0.0        ## unix s when the first credential (ours or the host's) ends; 0 is unknown (no renewal)
var gen := 0
var renewals := 0            ## completed handovers
var _last := 0
var _next: Q3RtcPeer
var _next_link: Q3RtcLink
var _next_ice: Dictionary = {}
var _renewing := false
var _renew_t := 0.0
var _retry_at := 0.0


func _init(rtc: Q3RtcPeer, rtc_link: Q3RtcLink = null) -> void:
	peer = rtc
	link = rtc_link


## When the connection's credentials end: ours (`ice`: its "at" and "ttl") or the host's (s left when it accepted).
static func credentials_end(ice: Dictionary, host_left: float) -> float:
	var now := Time.get_unix_time_from_system()
	var mine := float(ice.get("at", now - 2700.0)) + float(ice.get("ttl", 3600))
	return minf(mine, now + host_left) if host_left > 0.0 else mine


func poll() -> Array:
	var now := Time.get_ticks_msec()
	var dt := float(now - _last) / 1000.0 if _last > 0 else 0.0
	_last = now
	peer.poll(dt)
	var got := peer.receive()
	_tick_renewal(dt)
	if _next != null and _next.state == "open":
		got.append_array(_swap())
	return got


func send(data: PackedByteArray) -> void:
	peer.send(data)


func is_open() -> bool:
	return peer.state != "closed" and peer.state != "failed"


func close() -> void:
	_drop_next()
	if link != null:
		link.release()
		if hub != null and host != "":
			hub.send("net_leave", {"peer": host})
	peer.close()


## Starts a handover now (tests; the game waits for the credentials' end).
func renew_now() -> void:
	if not _renewing and hub != null:
		_start_renewal()


func _tick_renewal(dt: float) -> void:
	var now := Time.get_unix_time_from_system()
	if _renewing:
		_renew_t += dt
		if _next != null:
			_next.poll(dt)
			if _next.state == "failed" or _next.state == "closed":
				_renew_failed()
		if _renew_t > RENEW_TIMEOUT:
			_renew_failed()
	elif expires_at > 0.0 and hub != null and now > expires_at - RENEW_LEAD and now > _retry_at:
		_start_renewal()


func _start_renewal() -> void:
	_renewing = true
	_renew_t = 0.0
	_next_ice = {}
	if not hub.net.is_connected(_on_net):
		hub.net.connect(_on_net)
	if not hub.send("net_ice"):
		_renew_failed()


func _on_net(m: Dictionary) -> void:
	if not _renewing:
		return
	match String(m.get("type", "")):
		"net_ice":
			if m.has("error"):
				_renew_failed()
				return
			_next_ice = m.duplicate()
			_next_ice["at"] = Time.get_unix_time_from_system()
			hub.send("net_join", {"host": host, "version": version, "renew": true, "gen": gen + 1})
		"net_refused":
			if String(m.get("host", "")) == host:
				_renew_failed()
		"net_accepted":
			if String(m.get("host", "")) == host and bool(m.get("renew", false)) and int(m.get("gen", -1)) == gen + 1 \
					and _next == null and not _next_ice.is_empty():
				_next = Q3RtcPeer.new()
				_next_link = Q3RtcLink.new(hub, host, _next, gen + 1)
				_next_ice["host_left"] = float(m.get("expires_in", 0))
				if not _next.start(_next_ice, true):
					_renew_failed()


## The new connection is open: it carries the packets from now on. What the old one still had is handed on.
func _swap() -> Array:
	var rest := peer.receive()
	if link != null:
		link.release()
	peer.close()
	peer = _next
	link = _next_link
	gen += 1
	renewals += 1
	expires_at = credentials_end(_next_ice, float(_next_ice.get("host_left", 0)))
	_next = null
	_next_link = null
	_end_renewal()
	return rest


func _renew_failed() -> void:
	push_warning("online match: renewing the relay connection failed; trying again in %d s" % int(RENEW_RETRY))
	_drop_next()
	_retry_at = Time.get_unix_time_from_system() + RENEW_RETRY
	_end_renewal()


func _drop_next() -> void:
	if _next_link != null:
		_next_link.release()
	if _next != null:
		_next.close()
	_next = null
	_next_link = null


func _end_renewal() -> void:
	_renewing = false
	if hub != null and hub.net.is_connected(_on_net):
		hub.net.disconnect(_on_net)
