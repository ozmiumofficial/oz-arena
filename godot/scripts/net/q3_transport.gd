class_name Q3Transport
extends RefCounted
## What carries id's client packets between this game and its server (MULTIPLAYER_PLAN.md). The
## protocol is Quake 3's own, so a transport only moves datagrams: Q3NetPipe in one process (tests,
## fake latency and loss), a WebRTC data channel through the relay (M4), plain UDP later. Packets are
## unreliable and may be reordered or lost; the netchan above deals with it.


## Packets that arrived from the server since the last call.
func poll() -> Array:
	return []


## One packet to the server.
func send(_data: PackedByteArray) -> void:
	pass


## False once the link is gone (the data channel closed): the client treats it as a lost connection.
func is_open() -> bool:
	return true


func close() -> void:
	pass
