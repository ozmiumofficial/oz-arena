class_name Q3NetPipe
extends Q3Transport
## A link to the server running in this process, with optional fake latency and loss: how tests
## play a networked match without a second process. The server side is driven by `service()`,
## which whoever steps Q3Server calls after each server frame: it hands the client's packets to
## Q3Server.net_receive and takes what Q3Server.net_outgoing queued for the pipe's peer.

static var _pipes: Dictionary = {}   ## peer -> Q3NetPipe
static var _clock := 0               ## ms, advanced by service()
static var _rng := RandomNumberGenerator.new()

var peer := 7
var latency := 0                     ## one way, ms
var loss := 0.0                      ## fraction of packets dropped each way
var jitter := 0                      ## ms added at random to each packet
var _to_server: Array = []           ## [deliver_at, bytes]
var _to_client: Array = []
var _open := true
var sent := 0
var received := 0
var dropped := 0


func _init(peer_id := 7, one_way_ms := 0, loss_fraction := 0.0) -> void:
	peer = peer_id
	latency = one_way_ms
	loss = loss_fraction
	_pipes[peer] = self


func poll() -> Array:
	var out: Array = []
	while _to_client.size() > 0 and _to_client[0][0] <= _clock:
		out.append(_to_client.pop_front()[1])
	received += out.size()
	return out


func send(data: PackedByteArray) -> void:
	sent += 1
	if loss > 0.0 and _rng.randf() < loss:
		dropped += 1
		return
	_to_server.append([_clock + latency + (_rng.randi_range(0, jitter) if jitter > 0 else 0), data])


func is_open() -> bool:
	return _open


func close() -> void:
	_open = false   # packets already sent still arrive; service() forgets the pipe once they have


## Advances the pipes' clock and moves packets both ways. Call it once per server frame.
static func service(msec: int) -> void:
	_clock += msec
	for p in _pipes.values():
		var pipe: Q3NetPipe = p
		while pipe._to_server.size() > 0 and pipe._to_server[0][0] <= _clock:
			Q3Server.net_receive(pipe.peer, pipe._to_server.pop_front()[1])
		if not pipe._open and pipe._to_server.is_empty():
			_pipes.erase(pipe.peer)
	for pk in Q3Server.net_outgoing(512):
		var pipe: Q3NetPipe = _pipes.get(int(pk["peer"]))
		if pipe == null:
			continue
		if pipe.loss > 0.0 and _rng.randf() < pipe.loss:
			pipe.dropped += 1
			continue
		pipe._to_client.append([_clock + pipe.latency + (_rng.randi_range(0, pipe.jitter) if pipe.jitter > 0 else 0), pk["data"]])
