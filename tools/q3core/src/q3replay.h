#pragma once
// Q3Replay: a recorded match (engine/q3_replay.c) opened for playback. It reads the file
// (godot/scripts/replay/q3_replay_recorder.gd writes it: "Q3RP", version, JSON header, frame
// blocks, JSON trailer, "Q3RT"), decodes frames on demand into the same dictionaries Q3Server
// hands Gameplay, and traces against the map's collision model with no server running.

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "q3_api.h"

namespace godot {

class Q3Replay : public RefCounted {
	GDCLASS(Q3Replay, RefCounted)

	q3_replay *rep = nullptr;
	Dictionary info;

	String open_demo(const PackedByteArray &bytes, const String &path, int max_frames);

protected:
	static void _bind_methods();

public:
	~Q3Replay();

	String open(const String &path);
	void close();
	bool is_open() const;
	Dictionary header() const;
	int frame_count() const;
	int frame_time(int i) const;
	int frame_at(int ms) const;
	bool frame_is_key(int i) const;
	bool seek(int frame);
	int current() const;
	bool has_next() const;
	Dictionary snapshot(int client, int which) const;
	Dictionary player_state(int client, int which) const;
	PackedInt32Array clients(int which) const;
	String configstring(int index) const;
	PackedStringArray commands(int after_frame, int to_frame, int client) const;
	Array events() const;
	Dictionary trace(const Vector3 &start, const Vector3 &end, const Vector3 &mins, const Vector3 &maxs,
			int pass_entity, int content_mask) const;
	int point_contents(const Vector3 &point, int pass_entity) const;

	static Dictionary demo_info(const String &path);
	static String load_map(const String &name);
	static int map_checksum();
	static void unload_map();
};

} // namespace godot
