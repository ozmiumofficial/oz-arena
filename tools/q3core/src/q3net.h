#pragma once
// Q3Net: this process as a client of a server it reaches over packets (engine/q3_netclient.c).
// The transport (a WebRTC data channel, or a pipe in tests) carries the packets:
//   incoming: Q3Net.receive(bytes)             outgoing: Q3Net.outgoing() -> [bytes, ...]
// connect_to() starts the handshake, frame(msec) runs the clock and sends what is due, and snapshots,
// the clock, prediction and server commands come back in the shapes Q3Server uses, so Q3Client and
// Gameplay don't care where a match comes from. One per process (static methods).

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace godot {

class Q3Net : public RefCounted {
	GDCLASS(Q3Net, RefCounted)

protected:
	static void _bind_methods();

public:
	static int connect_to(const String &userinfo, int maxpackets);
	static void disconnect_from();
	static int state();
	static String error();
	static void set_maxpackets(int maxpackets);
	static void receive(const PackedByteArray &data);
	static Array outgoing(int max_packets);
	static int frame(int msec, bool extrapolated);
	static void hold_entry(bool hold);
	static String missing_map();
	static int reload_map();
	static void rcon(const String &password, const String &command);
	static String rcon_output();
	static int time();
	static int client_num();
	static int server_id();
	static void usercmd(int server_time, const Vector3 &angles, int buttons, int weapon, int forward, int right, int up);
	static void command(const String &text);
	static PackedStringArray next_commands();
	static String configstring(int index);
	static Dictionary snapshots(int time);
	static Dictionary predict();
	static Dictionary stats();
	static Dictionary trace(const Vector3 &start, const Vector3 &end, const Vector3 &mins, const Vector3 &maxs,
			int pass_entity, int content_mask);
	static int point_contents(const Vector3 &point, int pass_entity);
	// The loopback check: the server's snapshot for `client` that went out as netchan message `message`,
	// as the network carries it.
	static Dictionary server_frame(int client, int message);
};

} // namespace godot
