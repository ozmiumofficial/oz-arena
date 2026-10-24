#include "q3replay.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include "q3_dicts.h"

using namespace godot;

static int last_checksum = 0;

void Q3Replay::_bind_methods() {
	ClassDB::bind_method(D_METHOD("open", "path"), &Q3Replay::open);
	ClassDB::bind_method(D_METHOD("close"), &Q3Replay::close);
	ClassDB::bind_method(D_METHOD("is_open"), &Q3Replay::is_open);
	ClassDB::bind_method(D_METHOD("header"), &Q3Replay::header);
	ClassDB::bind_method(D_METHOD("frame_count"), &Q3Replay::frame_count);
	ClassDB::bind_method(D_METHOD("frame_time", "frame"), &Q3Replay::frame_time);
	ClassDB::bind_method(D_METHOD("frame_at", "ms"), &Q3Replay::frame_at);
	ClassDB::bind_method(D_METHOD("frame_is_key", "frame"), &Q3Replay::frame_is_key);
	ClassDB::bind_method(D_METHOD("seek", "frame"), &Q3Replay::seek);
	ClassDB::bind_method(D_METHOD("current"), &Q3Replay::current);
	ClassDB::bind_method(D_METHOD("has_next"), &Q3Replay::has_next);
	ClassDB::bind_method(D_METHOD("snapshot", "client", "which"), &Q3Replay::snapshot, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("player_state", "client", "which"), &Q3Replay::player_state, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("clients", "which"), &Q3Replay::clients, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("configstring", "index"), &Q3Replay::configstring);
	ClassDB::bind_method(D_METHOD("commands", "after_frame", "to_frame", "client"), &Q3Replay::commands);
	ClassDB::bind_method(D_METHOD("events"), &Q3Replay::events);
	ClassDB::bind_method(D_METHOD("trace", "start", "end", "mins", "maxs", "pass_entity", "content_mask"), &Q3Replay::trace);
	ClassDB::bind_method(D_METHOD("point_contents", "point", "pass_entity"), &Q3Replay::point_contents);
	ClassDB::bind_static_method("Q3Replay", D_METHOD("demo_info", "path"), &Q3Replay::demo_info);
	ClassDB::bind_static_method("Q3Replay", D_METHOD("load_map", "name"), &Q3Replay::load_map);
	ClassDB::bind_static_method("Q3Replay", D_METHOD("map_checksum"), &Q3Replay::map_checksum);
	ClassDB::bind_static_method("Q3Replay", D_METHOD("unload_map"), &Q3Replay::unload_map);
}

Q3Replay::~Q3Replay() {
	close();
}

static Dictionary parse_json(const PackedByteArray &bytes, int64_t from, int64_t len) {
	if (len <= 0) {
		return Dictionary();
	}
	Variant v = JSON::parse_string(bytes.slice(from, from + len).get_string_from_utf8());
	return v.get_type() == Variant::DICTIONARY ? Dictionary(v) : Dictionary();
}

static uint32_t u32_at(const PackedByteArray &b, int64_t at) {
	return (uint32_t)b[at] | ((uint32_t)b[at + 1] << 8) | ((uint32_t)b[at + 2] << 16) | ((uint32_t)b[at + 3] << 24);
}

// Opens a .q3replay: "" on success, else why not. A file whose recording was cut short (no
// trailer) still opens up to its last whole frame.
String Q3Replay::open(const String &path) {
	close();
	info = Dictionary();
	PackedByteArray b = FileAccess::get_file_as_bytes(path);
	if (path.get_extension().to_lower().begins_with("dm_")) {
		return open_demo(b, path, 0);
	}
	int64_t n = b.size();
	if (n < 12 || b[0] != 'Q' || b[1] != '3' || b[2] != 'R' || b[3] != 'P') {
		return "not a replay file";
	}
	uint32_t version = u32_at(b, 4);
	if (version != 1) {
		return vformat("replay format %d is newer than this game reads", (int64_t)version);
	}
	int64_t hlen = u32_at(b, 8);
	if (12 + hlen > n) {
		return "the replay's header is cut short";
	}
	info = parse_json(b, 12, hlen);
	int64_t start = 12 + hlen, end = n;
	bool complete = false;
	if (n - start >= 8 && b[n - 4] == 'Q' && b[n - 3] == '3' && b[n - 2] == 'R' && b[n - 1] == 'T') {
		int64_t tlen = u32_at(b, n - 8);
		if (n - 8 - tlen >= start) {
			Dictionary trailer = parse_json(b, n - 8 - tlen, tlen);
			Array keys = trailer.keys();
			for (int i = 0; i < keys.size(); i++) {
				info[keys[i]] = trailer[keys[i]];
			}
			end = n - 8 - tlen;
			complete = true;
		}
	}
	info["complete"] = complete;
	char err[256];
	rep = q3_replay_open(b.ptr() + start, (int)(end - start), err, sizeof(err));
	if (!rep) {
		return String::utf8(err);
	}
	return "";
}

// Quake 3's "\key\value\..." info string as a Dictionary.
static Dictionary info_string(const String &s) {
	Dictionary d;
	PackedStringArray parts = s.trim_prefix("\\").split("\\");
	for (int i = 0; i + 1 < parts.size(); i += 2) {
		d[parts[i]] = parts[i + 1];
	}
	return d;
}

// A Quake 3 demo (.dm_68): converted into replay frames (engine/q3_replay.c q3_demo_convert),
// with a header made from its configstrings: the map and mode from the serverinfo, the player
// who recorded it. `max_frames` > 0 reads only the start (demo_info).
String Q3Replay::open_demo(const PackedByteArray &bytes, const String &path, int max_frames) {
	unsigned char *frames = nullptr;
	int len = 0;
	char err[256];
	if (bytes.is_empty()) {
		return "can't read " + path;
	}
	if (!q3_demo_convert(bytes.ptr(), (int)bytes.size(), max_frames, &frames, &len, err, sizeof(err))) {
		return String::utf8(err);
	}
	rep = q3_replay_open(frames, len, err, sizeof(err));
	q3_demo_free(frames);
	if (!rep) {
		return String::utf8(err);
	}
	q3_replay_seek(rep, 0);
	Dictionary si = info_string(String::utf8(q3_replay_configstring(rep, 0)));   // CS_SERVERINFO
	PackedInt32Array cls = clients(0);
	int me = cls.size() ? cls[0] : -1;
	String map = String(si.get("mapname", "")).to_lower();
	int gt = String(si.get("g_gametype", "0")).to_int();
	static const char *modes[] = { "ffa", "duel", "ffa", "tdm", "ctf" };
	int last = q3_replay_frames(rep) - 1;
	info = Dictionary();
	info["demo"] = true;
	info["map"] = map;
	info["map_name"] = map;
	info["mode"] = String(gt >= 0 && gt < 5 ? modes[gt] : "ffa");
	info["title"] = "QUAKE 3 DEMO  -  " + map.to_upper();
	info["local_client"] = me;
	info["player"] = me >= 0 ? String(info_string(String::utf8(q3_replay_configstring(rep, 544 + me))).get("n", "")) : String();
	info["frames"] = last + 1;
	info["start_time"] = q3_replay_frame_time(rep, 0);
	info["end_time"] = q3_replay_frame_time(rep, last);
	info["length_ms"] = max_frames > 0 ? 0 : q3_replay_frame_time(rep, last) - q3_replay_frame_time(rep, 0);
	info["complete"] = true;
	return "";
}

// The Replays page's line for a demo: map, mode, player (the start of the file only), or {error}.
Dictionary Q3Replay::demo_info(const String &path) {
	Ref<Q3Replay> r;
	r.instantiate();
	// the gamestate and the first snapshot are in the first few KB
	Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ);
	PackedByteArray start = f.is_valid() ? f->get_buffer(MIN((int64_t)131072, (int64_t)f->get_length())) : PackedByteArray();
	String err = r->open_demo(start, path, 1);
	Dictionary d = err == "" ? r->header() : Dictionary();
	if (err != "") {
		d["error"] = err;
	}
	d["bytes"] = f.is_valid() ? (int64_t)f->get_length() : (int64_t)0;
	return d;
}

void Q3Replay::close() {
	if (rep) {
		q3_replay_close(rep);
		rep = nullptr;
	}
}

bool Q3Replay::is_open() const { return rep != nullptr; }
Dictionary Q3Replay::header() const { return info; }
int Q3Replay::frame_count() const { return q3_replay_frames(rep); }
int Q3Replay::frame_time(int i) const { return q3_replay_frame_time(rep, i); }
bool Q3Replay::frame_is_key(int i) const { return q3_replay_frame_is_key(rep, i) != 0; }
int Q3Replay::current() const { return q3_replay_current(rep); }
bool Q3Replay::has_next() const { return q3_replay_has_next(rep) != 0; }
bool Q3Replay::seek(int frame) { return q3_replay_seek(rep, frame) != 0; }

// The last frame at or before `ms` (server time), 0 before the first.
int Q3Replay::frame_at(int ms) const {
	int lo = 0, hi = q3_replay_frames(rep) - 1;
	if (hi < 0) {
		return 0;
	}
	while (lo < hi) {
		int mid = (lo + hi + 1) / 2;
		if (q3_replay_frame_time(rep, mid) <= ms) {
			lo = mid;
		} else {
			hi = mid - 1;
		}
	}
	return lo;
}

// {serverTime, ps (the client's, if they're in the match), entities} for the frame shown
// (which 0) or the next (1): the shape Q3Server.snapshot() returns.
Dictionary Q3Replay::snapshot(int client, int which) const {
	static q3_entity_state ents[Q3_MAX_GENTITIES];
	Dictionary d;
	if (!rep || q3_replay_current(rep) < 0 || (which && !q3_replay_has_next(rep))) {
		return d;
	}
	int f = q3_replay_current(rep) + (which ? 1 : 0);
	d["serverTime"] = q3_replay_frame_time(rep, f);
	q3_player_state ps;
	if (client >= 0 && q3_replay_player_state(rep, which, client, &ps)) {
		d["ps"] = q3_player_dict(ps);
	}
	int n = q3_replay_entities(rep, which, client, ents, Q3_MAX_GENTITIES);
	Array list;
	for (int i = 0; i < n; i++) {
		list.push_back(q3_entity_dict(ents[i]));
	}
	d["entities"] = list;
	return d;
}

Dictionary Q3Replay::player_state(int client, int which) const {
	q3_player_state ps;
	if (!q3_replay_player_state(rep, which, client, &ps)) {
		return Dictionary();
	}
	return q3_player_dict(ps);
}

// The clients in the match in that frame (an active client has a playerState).
PackedInt32Array Q3Replay::clients(int which) const {
	PackedInt32Array out;
	q3_player_state ps;
	for (int i = 0; i < Q3_MAX_CLIENTS; i++) {
		if (q3_replay_player_state(rep, which, i, &ps)) {
			out.push_back(i);
		}
	}
	return out;
}

String Q3Replay::configstring(int index) const {
	return String::utf8(q3_replay_configstring(rep, index));
}

// The reliable commands that reached `client` in frames after_frame + 1 .. to_frame (client -1:
// every command, once).
PackedStringArray Q3Replay::commands(int after_frame, int to_frame, int client) const {
	PackedStringArray out;
	int n = q3_replay_command_count(rep);
	int lo = 0, hi = n;
	while (lo < hi) {   // the first command after after_frame
		int mid = (lo + hi) / 2, f;
		unsigned m[2];
		q3_replay_command(rep, mid, &f, m);
		if (f <= after_frame) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	for (int i = lo; i < n; i++) {
		int f;
		unsigned m[2];
		const char *t = q3_replay_command(rep, i, &f, m);
		if (f > to_frame) {
			break;
		}
		if (client < 0 || (client < 64 && (m[client >> 5] >> (client & 31)) & 1)) {
			out.push_back(String::utf8(t));
		}
	}
	return out;
}

// The timeline's events found at open: [{frame, time, type (1 obituary, 2 team sound, 3 medal), a, b, c}].
Array Q3Replay::events() const {
	Array out;
	int n = q3_replay_event_count(rep);
	for (int i = 0; i < n; i++) {
		q3_replay_event e;
		q3_replay_get_event(rep, i, &e);
		Dictionary d;
		d["frame"] = e.frame;
		d["time"] = e.time;
		d["type"] = e.type;
		d["a"] = e.a;
		d["b"] = e.b;
		d["c"] = e.c;
		out.push_back(d);
	}
	return out;
}

Dictionary Q3Replay::trace(const Vector3 &start, const Vector3 &end, const Vector3 &mins, const Vector3 &maxs,
		int pass_entity, int content_mask) const {
	float s[3] = { start.x, start.y, start.z }, e[3] = { end.x, end.y, end.z };
	float mn[3] = { mins.x, mins.y, mins.z }, mx[3] = { maxs.x, maxs.y, maxs.z };
	q3_trace_result tr;
	Dictionary d;
	if (!q3_replay_trace(rep, s, e, mn, mx, pass_entity, content_mask, &tr)) {
		return d;
	}
	d["fraction"] = tr.fraction;
	d["endpos"] = Vector3(tr.endpos[0], tr.endpos[1], tr.endpos[2]);
	d["normal"] = Vector3(tr.normal[0], tr.normal[1], tr.normal[2]);
	d["surfaceFlags"] = tr.surfaceFlags;
	d["contents"] = tr.contents;
	d["entityNum"] = tr.entityNum;
	d["startsolid"] = tr.startsolid != 0;
	d["allsolid"] = tr.allsolid != 0;
	return d;
}

int Q3Replay::point_contents(const Vector3 &point, int pass_entity) const {
	float p[3] = { point.x, point.y, point.z };
	return q3_replay_point_contents(rep, p, pass_entity);
}

// Loads maps/<name>.bsp's collision model for playback (no server may be running): "" or why not.
String Q3Replay::load_map(const String &name) {
	int checksum = 0;
	if (q3_replay_load_map(name.utf8().get_data(), &checksum) != 0) {
		return String::utf8(q3_host_error());
	}
	last_checksum = checksum;
	return "";
}

int Q3Replay::map_checksum() {
	return last_checksum;
}

void Q3Replay::unload_map() {
	q3_replay_unload_map();
}
