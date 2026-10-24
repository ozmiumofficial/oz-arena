class_name Q3HostNet
extends Node
## The host side of an online match (MULTIPLAYER_PLAN.md M4): lists the match with the hub, decides who
## may join, makes a WebRTC connection per player, and carries their packets to and from id's server
## (Q3Server.net_receive / net_outgoing). The host plays through the server's local client as before;
## everyone else is a peer here. Gameplay calls pump_in() before each server frame and pump_out()
## after it.
##
## A peer is the small integer id the server knows as its address (10.77.x.x, patch 0006); the hub
## knows the player by id. Players leave by closing their data channel or by the server timing them out.

signal renewed(pid: String)   ## a player's connection moved to fresh relay credentials (the handover)
signal peer_joined(pid: String, name: String, id: int)
signal peer_left(pid: String, id: int)

const E := preload("res://scripts/game/q3_enums.gd")
const LIST_EVERY := 5.0
## Relay credentials are fetched when someone asks to join and the last ones are older than this (they last an
## hour). Not on a timer: the hub charges every credential its worst case against the month's relay budget, so an
## idle or sleeping server holding fresh ones all day used that budget up (M8).
const ICE_EVERY := 2700.0
const OPEN_TIMEOUT := 25.0

var client: Q3OnlineClient
## The listing: name, mode, map, max (players and bots), password ("" for none), ranked, version.
var listing: Dictionary = {}
var peers: Dictionary = {}        ## pid -> {peer, link, id, name, age, opened}
var _ids: Dictionary = {}         ## id -> pid
var _next_id := 1
var _ice: Dictionary = {}
var _ice_at := -1.0e9            ## when _ice came, s (Time.get_ticks_msec / 1000)
var _ice_ask := 0.0
var _list_t := 0.0
var _waiting: Array = []          ## join requests that came before the relay credentials did
var _last_players := -1
var open := false


func start(online_client: Q3OnlineClient, info: Dictionary) -> void:
	client = online_client
	listing = info
	client.net.connect(_on_net)
	open = true
	_publish()


func stop() -> void:
	if not open:
		return
	open = false
	if client != null:
		client.send("net_unhost")
		if client.net.is_connected(_on_net):
			client.net.disconnect(_on_net)
	for pid in peers.keys():
		_drop(pid, true)


func _exit_tree() -> void:
	stop()


## Players in the match now (humans and bots): clients the server has connected.
func player_count() -> int:
	var n := 0
	for c in int(Q3Server.cvar("sv_maxclients")):
		if Q3Server.client_state(c) >= 2:
			n += 1
	return n


func _publish() -> void:
	_last_players = player_count()
	client.send("net_host", {"name": listing.get("name", "Match"), "mode": listing.get("mode", "ffa"), "map": listing.get("map", ""),
		"players": _last_players, "max": int(listing.get("max", 16)), "password": String(listing.get("password", "")) != "",
		"ranked": bool(listing.get("ranked", false)), "version": String(listing.get("version", "")), "match": String(listing.get("match", "")), "dedicated": bool(listing.get("dedicated", false))})


func _on_net(m: Dictionary) -> void:
	match String(m.get("type", "")):
		"net_ice":
			if m.has("error"):
				push_warning("online match: " + String(m["error"]))
				for r in _waiting:
					client.send("net_reject", {"peer": r["peer"], "reason": String(m["error"])})
				_waiting.clear()
				return
			_ice = m
			_ice_at = Time.get_ticks_msec() / 1000.0
			_ice_ask = 0.0
			for r in _waiting:
				_decide(r)
			_waiting.clear()
		"net_request":
			if _ice_stale():
				_waiting.append(m)
				if _ice_ask <= 0.0:
					_ice_ask = 5.0
					client.send("net_ice")
			else:
				_decide(m)
		"net_signal":
			_on_signal(m)
		"net_gone":
			_drop(String(m.get("pid", "")), false)


func _ice_stale() -> bool:
	return _ice.is_empty() or Time.get_ticks_msec() / 1000.0 - _ice_at > ICE_EVERY


## Room, password and version; the answer goes to the hub, which tells the joiner.
func _decide(req: Dictionary) -> void:
	var pid := String(req.get("peer", ""))
	var why := ""
	if bool(req.get("renew", false)):
		# the credential handover: a second connection for a player who stays, in the same engine slot
		if not peers.has(pid) or not peers[pid]["opened"]:
			client.send("net_reject", {"peer": pid, "reason": "That connection is gone."})
			return
		_drop_next(peers[pid])
		peers[pid]["next_gen"] = int(req.get("gen", 1))
		client.send("net_accept", {"peer": pid, "renew": true, "gen": int(req.get("gen", 1)), "expires_in": _ice_left()})
		return
	if peers.has(pid):
		_drop(pid, false)
	if player_count() + peers.size() - _connected_peers() >= int(listing.get("max", 16)):
		why = "The match is full."
	elif String(listing.get("password", "")) != "" and String(req.get("password", "")) != String(listing.get("password", "")):
		why = "Wrong password."
	elif String(listing.get("version", "")) != "" and String(req.get("version", "")) != "" \
			and String(req.get("version", "")) != String(listing.get("version", "")):
		why = "You are on another version of the game."
	if why != "":
		client.send("net_reject", {"peer": pid, "reason": why})
		return
	peers[pid] = {"peer": null, "link": null, "id": -1, "name": String(req.get("name", "Player")), "age": 0.0, "opened": false, "gen": 0}
	client.send("net_accept", {"peer": pid, "expires_in": _ice_left()})


## s left on the relay credentials this side's new connections use (the joiner renews before either side's end).
func _ice_left() -> int:
	return maxi(0, int(_ice.get("ttl", 3600)) - int(Time.get_ticks_msec() / 1000.0 - _ice_at))


func _connected_peers() -> int:
	var n := 0
	for p in peers.values():
		if p["opened"]:
			n += 1
	return n


## The joiner's offer: this is where the connection to that player starts.
func _on_signal(m: Dictionary) -> void:
	var pid := String(m.get("from", ""))
	if not peers.has(pid) or String(m.get("kind", "")) != "offer":
		return
	var p: Dictionary = peers[pid]
	var g := int(m.get("gen", 0))
	if p.has("next_gen") and g == int(p["next_gen"]):
		if p.get("next") != null:
			return
		var nxt := Q3RtcPeer.new()
		p["next"] = nxt
		p["next_link"] = Q3RtcLink.new(client, pid, nxt, g)
		p["next_age"] = 0.0
		if not nxt.start(_ice, false):
			_drop_next(p)
			return
		nxt.remote_description("offer", String(m.get("sdp", "")))
		return
	if p["peer"] != null:
		return
	var rtc := Q3RtcPeer.new()
	p["peer"] = rtc
	p["link"] = Q3RtcLink.new(client, pid, rtc)
	if not rtc.start(_ice, false):
		push_warning("online match: " + rtc.error)
		_drop(pid, true)
		return
	rtc.remote_description("offer", String(m.get("sdp", "")))


func _drop_next(p: Dictionary) -> void:
	if p.get("next_link") != null:
		(p["next_link"] as Q3RtcLink).release()
	if p.get("next") != null:
		(p["next"] as Q3RtcPeer).close()
	p.erase("next")
	p.erase("next_link")
	p.erase("next_gen")


## A renewed connection opened: it carries this player's packets from now on, in the same engine slot.
func _swap(pid: String, p: Dictionary) -> void:
	var old: Q3RtcPeer = p["peer"]
	for pk in old.receive():
		Q3Server.net_receive(int(p["id"]), pk)
	if p["link"] != null:
		(p["link"] as Q3RtcLink).release()
	old.close()
	p["peer"] = p["next"]
	p["link"] = p["next_link"]
	p["gen"] = int(p["next_gen"])
	p.erase("next")
	p.erase("next_link")
	p.erase("next_gen")
	renewed.emit(pid)


func _drop(pid: String, tell_hub: bool) -> void:
	if not peers.has(pid):
		return
	var p: Dictionary = peers[pid]
	_drop_next(p)
	peers.erase(pid)
	if p["link"] != null:
		(p["link"] as Q3RtcLink).release()
	if p["peer"] != null:
		(p["peer"] as Q3RtcPeer).close()
	if int(p["id"]) >= 0:
		_ids.erase(int(p["id"]))
		if Q3Server.is_ready():
			Q3Server.net_peer_gone(int(p["id"]))
		peer_left.emit(pid, int(p["id"]))
	if tell_hub and client != null:
		client.send("net_leave", {"peer": pid})


## Before a server frame: run the connections, hand the players' packets to the server.
func pump_in(delta: float) -> void:
	if not open:
		return
	for pid in peers.keys():
		var p: Dictionary = peers[pid]
		var rtc: Q3RtcPeer = p["peer"]
		p["age"] += delta
		if rtc == null:
			if p["age"] > OPEN_TIMEOUT:
				_drop(pid, true)
			continue
		rtc.poll(delta)
		if p.get("next") != null:
			var nxt: Q3RtcPeer = p["next"]
			nxt.poll(delta)
			p["next_age"] = float(p.get("next_age", 0.0)) + delta
			if nxt.state == "open":
				_swap(pid, p)
				rtc = p["peer"]
			elif nxt.state == "failed" or nxt.state == "closed" or float(p["next_age"]) > OPEN_TIMEOUT:
				_drop_next(p)
		if rtc.state == "open" and not p["opened"]:
			p["opened"] = true
			p["id"] = _new_id()
			_ids[int(p["id"])] = pid
			peer_joined.emit(pid, String(p["name"]), int(p["id"]))
		elif rtc.state == "failed" or rtc.state == "closed":
			_drop(pid, true)
			continue
		elif rtc.state == "connecting" and p["age"] > OPEN_TIMEOUT:
			_drop(pid, true)
			continue
		if p["opened"]:
			var slot := Q3Server.net_peer_client(int(p["id"]))
			if slot >= 0:   # their name in the match, to know who a kick means
				p["plain"] = Q3Colors.strip(String(Q3Host.parse_info(Q3Server.configstring(E.CS_PLAYERS + slot)).get("n", "")))
			for pk in rtc.receive():
				Q3Server.net_receive(int(p["id"]), pk)
	_list_t -= delta
	if _waiting.size():
		# joiners wait for fresh credentials; ask again if the answer was lost (the hub reconnected)
		_ice_ask -= delta
		if _ice_ask <= 0.0:
			_ice_ask = 5.0
			client.send("net_ice")
	if _list_t <= 0.0 or player_count() != _last_players:
		_list_t = LIST_EVERY
		_publish()


## After a server frame: the server's packets to the players they are for.
func pump_out() -> void:
	if not open:
		return
	for pk in Q3Server.net_outgoing(512):
		var pid: String = _ids.get(int(pk["peer"]), "")
		if pid != "" and peers.has(pid):
			(peers[pid]["peer"] as Q3RtcPeer).send(pk["data"])


## Which client number each connected remote player holds, by their hub id: the host's half of a ranked result (the
## players report their own number; the hub checks the two agree).
func peer_pids() -> Dictionary:
	var out := {}
	for pid in peers.keys():
		var p: Dictionary = peers[pid]
		if p["opened"]:
			var slot := Q3Server.net_peer_client(int(p["id"]))
			if slot >= 0:
				out[str(slot)] = pid
	return out


## The game kicked a player (a passed vote): they can't rejoin this host's matches for 30 minutes (the hub keeps the ban).
func note_kick(plain_name: String) -> void:
	for pid in peers.keys():
		var p: Dictionary = peers[pid]
		if p["opened"] and String(p.get("plain", "")) == plain_name and plain_name != "":
			client.send("net_kick", {"peer": pid})
			_drop(pid, false)
			return


func _new_id() -> int:
	for i in 65536:
		var id := _next_id
		_next_id = _next_id % 65535 + 1
		if not _ids.has(id):
			return id
	return 1
