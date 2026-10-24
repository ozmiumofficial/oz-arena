class_name Q3LagTransport
extends Q3Transport
## Another transport with a worse link in front of it (MULTIPLAYER_PLAN.md M8): every packet each way waits
## `latency` ms plus up to `jitter`, and `loss` of them never arrive. For tests across processes, where the real
## link (WebRTC on one PC) is near perfect. Packets keep their order, as they nearly always do on a real path:
## jitter that reorders makes the netchan throw the late packet away, so it acts as heavy loss, and it breaks the
## reassembly of a fragmented message (a first, full snapshot) every time.

var inner: Q3Transport
var latency := 0      ## one way, ms
var jitter := 0       ## ms added at random to each packet
var loss := 0.0       ## fraction dropped each way
var sent := 0
var dropped := 0
var _out: Array = []  ## [due ms, bytes], to the server
var _in: Array = []   ## from the server
var _rng := RandomNumberGenerator.new()


func _init(wrapped: Q3Transport, one_way_ms := 0, jitter_ms := 0, loss_fraction := 0.0, seed := 0) -> void:
	inner = wrapped
	latency = one_way_ms
	jitter = jitter_ms
	loss = loss_fraction
	if seed != 0:
		_rng.seed = seed


func _due() -> int:
	return Time.get_ticks_msec() + latency + (_rng.randi_range(0, jitter) if jitter > 0 else 0)


func _queue(q: Array, data: PackedByteArray) -> void:
	if _rng.randf() < loss:
		dropped += 1
		return
	var due := _due()
	if q.size():
		due = maxi(due, q.back()[0])   # never ahead of the packet before it
	q.append([due, data])


func poll() -> Array:
	var now := Time.get_ticks_msec()
	while _out.size() and _out[0][0] <= now:
		inner.send(_out.pop_front()[1])
	for p in inner.poll():
		_queue(_in, p)
	var got: Array = []
	while _in.size() and _in[0][0] <= now:
		got.append(_in.pop_front()[1])
	return got


func send(data: PackedByteArray) -> void:
	sent += 1
	_queue(_out, data)


func is_open() -> bool:
	return inner.is_open()


func close() -> void:
	for p in _out:
		inner.send(p[1])
	_out.clear()
	inner.close()
