extends Node
## The server's UDP door (MULTIPLAYER_PLAN.md M7, patch 0007): a plain Quake 3 query reaches a running server over
## real UDP and is answered (getinfo, getstatus), and a remote console command with the rconpassword runs and its
## output comes back; a wrong password is refused.
##   godot --headless --path . res://tests/udp_test.tscn
## Prints "UDP ok|FAILED".

const PORT := 27961
var peer := PacketPeerUDP.new()
var checks: Array = []
var step := 0
var t := 0.0
var got: Array = []


func _ready() -> void:
	var err := Q3Host.start()
	if err != "":
		_finish("start: " + err)
		return
	Q3Server.set_cvar("rconpassword", "sekrit")
	Q3Server.set_cvar("sv_hostname", "UdpTest")
	if Q3Server.exec_now("map q3dm17") != 0:
		_finish("map: " + Q3Server.error())
		return
	Q3Server.frame(Q3Host.FRAME_MSEC)
	_check(Q3Server.udp_open(PORT), "the UDP socket opens")
	peer.connect_to_host("127.0.0.1", PORT)


func _check(ok: bool, what: String) -> void:
	checks.append([ok, what])


func _oob(text: String) -> void:
	var b := PackedByteArray([255, 255, 255, 255])
	b.append_array(text.to_utf8_buffer())
	peer.put_packet(b)


func _process(delta: float) -> void:
	t += delta
	Q3Server.frame(Q3Host.FRAME_MSEC)
	while peer.get_available_packet_count() > 0:
		got.append(peer.get_packet().slice(4).get_string_from_utf8())
	if t > 30.0:
		_finish("timed out at step %d (%s)" % [step, str(got)])
		return
	match step:
		0:
			_oob("getinfo abc")
			step = 1
			t = 0.0
		1:
			if got.size():
				_check(String(got[0]).begins_with("infoResponse") and String(got[0]).contains("UdpTest"), "getinfo is answered: " + String(got[0]).left(60))
				got.clear()
				_oob("rcon sekrit status")
				step = 2
				t = 0.0
			elif fmod(t, 1.0) < delta:
				_oob("getinfo abc")
		2:
			if got.size() and t > 0.5:   # every piece of the answer is in by then
				_check(String(got[0]).begins_with("print") and "".join(got).contains("map:"), "rcon runs and answers")
				got.clear()
				_oob("rcon wrong status")
				step = 3
				t = 0.0
		3:
			# a late piece of the status answer may still arrive first
			for g in got:
				if String(g).contains("Bad rcon"):
					_check(true, "a wrong rcon password is refused")
					_finish("")
					return
			if t > 3.0:
				_check(false, "a wrong rcon password is refused (got %s)" % str(got))
				_finish("")


func _finish(problem: String) -> void:
	set_process(false)
	Q3Server.udp_close()
	var ok := problem == ""
	for c in checks:
		print("  [%s] %s" % ["ok" if c[0] else "FAIL", c[1]])
		ok = ok and c[0]
	print("UDP ", "ok" if ok else "FAILED " + problem)
	get_tree().quit(0 if ok else 1)
