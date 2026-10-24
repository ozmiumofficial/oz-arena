class_name Q3RtcLink
extends RefCounted
## Joins a Q3RtcPeer to the hub's signaling (net_signal), for one other player: what the peer produces
## goes out as net_signal messages, and what the hub forwards from that player goes into the peer.
## `gen` tells a renewed connection's signals from the old one's while both are up (the credential handover,
## Q3RtcTransport.renew): 0 for the first connection, +1 for each renewal.

var peer: Q3RtcPeer
var client: Q3OnlineClient
var other := ""          ## the other player's id
var gen := 0


func _init(online_client: Q3OnlineClient, other_pid: String, rtc: Q3RtcPeer, generation := 0) -> void:
	client = online_client
	other = other_pid
	peer = rtc
	gen = generation
	peer.description_ready.connect(func(kind, sdp): client.send("net_signal", {"to": other, "kind": kind, "sdp": sdp, "gen": gen}))
	peer.candidate_ready.connect(func(mid, index, candidate):
		client.send("net_signal", {"to": other, "kind": "candidate", "candidate": candidate, "mid": mid, "index": index, "gen": gen}))
	client.net.connect(_on_net)


## Stops listening; the peer stays as it is.
func release() -> void:
	if client != null and client.net.is_connected(_on_net):
		client.net.disconnect(_on_net)


func _on_net(m: Dictionary) -> void:
	match String(m.get("type", "")):
		"net_signal":
			if String(m.get("from", "")) != other or int(m.get("gen", 0)) != gen:
				return
			match String(m.get("kind", "")):
				"offer", "answer":
					peer.remote_description(String(m["kind"]), String(m.get("sdp", "")))
				"candidate":
					peer.remote_candidate(String(m.get("mid", "0")), int(m.get("index", 0)), String(m.get("candidate", "")))
		"net_gone":
			if String(m.get("pid", "")) == other:
				peer.close()
