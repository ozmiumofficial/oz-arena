extends Node
func _ready() -> void:
	var t := Time.get_ticks_msec()
	var pc := WebRTCPeerConnection.new()
	var err := pc.initialize({"iceServers": [{"urls": ["turn:turn.cloudflare.com:3478?transport=udp", "turn:turn.cloudflare.com:443?transport=udp"], "username": "x", "credential": "y"}]})
	print("WEBRTC initialize with TURN: ", err, " in ms ", Time.get_ticks_msec() - t)
	var ch := pc.create_data_channel("q3", {"negotiated": true, "id": 1, "ordered": false, "maxRetransmits": 0})
	print("WEBRTC channel ", ch != null, " in ms ", Time.get_ticks_msec() - t)
	pc.session_description_created.connect(func(k, s): print("WEBRTC offer created in ms ", Time.get_ticks_msec() - t))
	pc.ice_candidate_created.connect(func(m, i, c): print("WEBRTC candidate ", c.substr(0, 60)))
	pc.create_offer()
	for i in 300:
		pc.poll()
		await get_tree().create_timer(0.02).timeout
	print("WEBRTC done ms ", Time.get_ticks_msec() - t)
	pc.close()
	get_tree().quit()
