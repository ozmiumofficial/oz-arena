#include "q3net.h"

#include <cstring>

#include <godot_cpp/core/class_db.hpp>

#include "q3_api.h"
#include "q3_dicts.h"

using namespace godot;

void Q3Net::_bind_methods() {
	ClassDB::bind_static_method("Q3Net", D_METHOD("connect_to", "userinfo", "maxpackets"), &Q3Net::connect_to);
	ClassDB::bind_static_method("Q3Net", D_METHOD("disconnect_from"), &Q3Net::disconnect_from);
	ClassDB::bind_static_method("Q3Net", D_METHOD("state"), &Q3Net::state);
	ClassDB::bind_static_method("Q3Net", D_METHOD("error"), &Q3Net::error);
	ClassDB::bind_static_method("Q3Net", D_METHOD("set_maxpackets", "maxpackets"), &Q3Net::set_maxpackets);
	ClassDB::bind_static_method("Q3Net", D_METHOD("receive", "data"), &Q3Net::receive);
	ClassDB::bind_static_method("Q3Net", D_METHOD("outgoing", "max_packets"), &Q3Net::outgoing);
	ClassDB::bind_static_method("Q3Net", D_METHOD("frame", "msec", "extrapolated"), &Q3Net::frame);
	ClassDB::bind_static_method("Q3Net", D_METHOD("hold_entry", "hold"), &Q3Net::hold_entry);
	ClassDB::bind_static_method("Q3Net", D_METHOD("rcon", "password", "command"), &Q3Net::rcon);
	ClassDB::bind_static_method("Q3Net", D_METHOD("rcon_output"), &Q3Net::rcon_output);
	ClassDB::bind_static_method("Q3Net", D_METHOD("time"), &Q3Net::time);
	ClassDB::bind_static_method("Q3Net", D_METHOD("client_num"), &Q3Net::client_num);
	ClassDB::bind_static_method("Q3Net", D_METHOD("server_id"), &Q3Net::server_id);
	ClassDB::bind_static_method("Q3Net",
			D_METHOD("usercmd", "server_time", "angles", "buttons", "weapon", "forward", "right", "up"),
			&Q3Net::usercmd);
	ClassDB::bind_static_method("Q3Net", D_METHOD("command", "text"), &Q3Net::command);
	ClassDB::bind_static_method("Q3Net", D_METHOD("next_commands"), &Q3Net::next_commands);
	ClassDB::bind_static_method("Q3Net", D_METHOD("configstring", "index"), &Q3Net::configstring);
	ClassDB::bind_static_method("Q3Net", D_METHOD("snapshots", "time"), &Q3Net::snapshots);
	ClassDB::bind_static_method("Q3Net", D_METHOD("predict"), &Q3Net::predict);
	ClassDB::bind_static_method("Q3Net", D_METHOD("stats"), &Q3Net::stats);
	ClassDB::bind_static_method("Q3Net", D_METHOD("trace", "start", "end", "mins", "maxs", "pass_entity", "content_mask"),
			&Q3Net::trace);
	ClassDB::bind_static_method("Q3Net", D_METHOD("point_contents", "point", "pass_entity"), &Q3Net::point_contents);
	ClassDB::bind_static_method("Q3Net", D_METHOD("server_frame", "client", "message"), &Q3Net::server_frame);
}

int Q3Net::connect_to(const String &userinfo, int maxpackets) {
	return q3_nc_connect(userinfo.utf8().get_data(), maxpackets);
}

void Q3Net::disconnect_from() {
	q3_nc_disconnect();
}

int Q3Net::state() {
	return q3_nc_state();
}

String Q3Net::error() {
	return String::utf8(q3_nc_error());
}

void Q3Net::set_maxpackets(int maxpackets) {
	q3_nc_set_maxpackets(maxpackets);
}

void Q3Net::receive(const PackedByteArray &data) {
	if (data.size() > 0) {
		q3_nc_receive(data.ptr(), data.size());
	}
}

// The packets the client queued for the server since the last call: [PackedByteArray, ...].
Array Q3Net::outgoing(int max_packets) {
	Array out;
	unsigned char buf[2048];
	int peer;
	while (out.size() < max_packets) {
		int len = q3_net_next(0, &peer, buf, sizeof(buf));
		if (len < 0) {
			break;
		}
		PackedByteArray bytes;
		bytes.resize(len);
		memcpy(bytes.ptrw(), buf, len);
		out.push_back(bytes);
	}
	return out;
}

int Q3Net::frame(int msec, bool extrapolated) {
	return q3_nc_frame(msec, extrapolated ? 1 : 0);
}

// While the game loads the map, a primed client stays out of the world (see q3_nc_hold_entry).
void Q3Net::hold_entry(bool hold) {
	q3_nc_hold_entry(hold ? 1 : 0);
}

// A remote console command (the server's rconpassword); the answer is read back with rcon_output() after the packets travel.
void Q3Net::rcon(const String &password, const String &command) {
	q3_nc_rcon(password.utf8().get_data(), command.utf8().get_data());
}

String Q3Net::rcon_output() {
	return String::utf8(q3_nc_rcon_output());
}

int Q3Net::time() {
	return q3_nc_time();
}

int Q3Net::client_num() {
	return q3_nc_client_num();
}

int Q3Net::server_id() {
	return q3_nc_server_id();
}

void Q3Net::usercmd(int server_time, const Vector3 &angles, int buttons, int weapon, int forward, int right, int up) {
	q3_usercmd cmd;
	cmd.serverTime = server_time;
	cmd.angles[0] = angles.x;
	cmd.angles[1] = angles.y;
	cmd.angles[2] = angles.z;
	cmd.buttons = buttons;
	cmd.weapon = weapon;
	cmd.forwardmove = forward;
	cmd.rightmove = right;
	cmd.upmove = up;
	q3_nc_usercmd(&cmd);
}

void Q3Net::command(const String &text) {
	q3_nc_command(text.utf8().get_data());
}

PackedStringArray Q3Net::next_commands() {
	PackedStringArray out;
	const char *c;
	while ((c = q3_nc_next_command()) != nullptr) {
		out.push_back(String::utf8(c));
	}
	return out;
}

String Q3Net::configstring(int index) {
	return String::utf8(q3_nc_configstring(index));
}

static Dictionary snapshot_dict(const q3_snapshot &snap) {
	Dictionary d;
	d["serverTime"] = snap.serverTime;
	d["ps"] = q3_player_dict(snap.ps);
	Array ents;
	for (int i = 0; i < snap.num_entities; i++) {
		ents.push_back(q3_entity_dict(snap.entities[i]));
	}
	d["entities"] = ents;
	return d;
}

// CG_ProcessSnapshots' pair for `time`: {snap, next (absent when there is none), message,
// next_message, snap_flags, ping, newest_message, newest_time}, or {} before the first snapshot.
Dictionary Q3Net::snapshots(int time) {
	static q3_snapshot a, b; // about 210 KB each: too big for the stack
	q3_nc_snap_info info;
	Dictionary d;
	int n = q3_nc_snapshot_pair(time, &a, &b, &info);
	if (n == 0) {
		return d;
	}
	d["snap"] = snapshot_dict(a);
	if (n == 2) {
		d["next"] = snapshot_dict(b);
	}
	d["message"] = info.message;
	d["next_message"] = info.next_message;
	d["snap_flags"] = info.snap_flags;
	d["ping"] = info.ping;
	d["newest_message"] = info.newest_message;
	d["newest_time"] = info.newest_time;
	return d;
}

// The predicted playerState plus {error: Vector3, error_time, teleported}, or {} until there is a
// snapshot and a collision model.
Dictionary Q3Net::predict() {
	q3_player_state ps;
	float error[3] = { 0, 0, 0 };
	int error_time = 0, teleported = 0;
	if (!q3_nc_predict(&ps, error, &error_time, &teleported)) {
		return Dictionary();
	}
	Dictionary d = q3_player_dict(ps);
	d["error"] = Vector3(error[0], error[1], error[2]);
	d["error_time"] = error_time;
	d["teleported"] = teleported != 0;
	return d;
}

Dictionary Q3Net::stats() {
	q3_nc_stats s;
	q3_nc_get_stats(&s);
	Dictionary d;
	d["state"] = s.state;
	d["ping"] = s.ping;
	d["packets_in"] = s.packets_in;
	d["packets_out"] = s.packets_out;
	d["bytes_in"] = s.bytes_in;
	d["bytes_out"] = s.bytes_out;
	d["dropped"] = s.dropped;
	d["snapshots"] = s.snapshots;
	d["server_time"] = s.server_time;
	d["time_delta"] = s.time_delta;
	return d;
}

Dictionary Q3Net::trace(const Vector3 &start, const Vector3 &end, const Vector3 &mins, const Vector3 &maxs,
		int pass_entity, int content_mask) {
	float s[3] = { start.x, start.y, start.z }, e[3] = { end.x, end.y, end.z };
	float mn[3] = { mins.x, mins.y, mins.z }, mx[3] = { maxs.x, maxs.y, maxs.z };
	q3_trace_result tr;
	Dictionary d;
	if (!q3_nc_trace(s, e, mn, mx, pass_entity, content_mask, &tr)) {
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

int Q3Net::point_contents(const Vector3 &point, int pass_entity) {
	float p[3] = { point.x, point.y, point.z };
	return q3_nc_point_contents(p, pass_entity);
}

Dictionary Q3Net::server_frame(int client, int message) {
	static q3_snapshot snap;
	if (!q3_nc_server_frame(client, message, &snap)) {
		return Dictionary();
	}
	return snapshot_dict(snap);
}
