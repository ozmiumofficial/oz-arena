#pragma once
// Q3Server: ioquake3's server, game module and botlib running in this process (engine/q3_host.c).
// One per process (static methods). Godot drives it: start() once, exec("map q3dm17"),
// connect_local() for the human, usercmd() as input arrives, frame(msec) on its own clock,
// then snapshot() / next_commands() for the presentation. Values are raw id units and axes.

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace godot {

class Q3Server : public RefCounted {
	GDCLASS(Q3Server, RefCounted)

protected:
	static void _bind_methods();

public:
	static int start(const String &command_line);
	static bool is_ready();
	static String error();
	static void exec(const String &text);
	static int exec_now(const String &text);
	static int frame(int msec);
	static bool running();
	static int time();
	static String cvar(const String &name);
	static void set_cvar(const String &name, const String &value);
	static String configstring(int index);

	static int connect_local(const String &userinfo);
	static void disconnect_local(int client);
	static void usercmd(int client, int server_time, const Vector3 &angles, int buttons, int weapon,
			int forward, int right, int up);
	static Dictionary snapshot(int client);
	static Dictionary predict(int client, const Array &cmds);
	static PackedStringArray next_commands(int client);
	static bool client_command(int client, const String &text);
	static Dictionary player_state(int client);
	static int client_state(int client);
	static Dictionary trace(const Vector3 &start, const Vector3 &end, const Vector3 &mins, const Vector3 &maxs,
			int pass_entity, int content_mask);
	static int point_contents(const Vector3 &point, int pass_entity);
	static String postgame();
	static Array items();
	static Array entities();
	static bool record_start(int hz);
	static void record_stop();
	static bool recording();
	static int record_frames();
	static PackedByteArray record_pull();
};

} // namespace godot
