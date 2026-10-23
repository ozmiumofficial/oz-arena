#include "q3server.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>

#include "q3_api.h"

using namespace godot;

void Q3Server::_bind_methods() {
	ClassDB::bind_static_method("Q3Server", D_METHOD("start", "command_line"), &Q3Server::start);
	ClassDB::bind_static_method("Q3Server", D_METHOD("is_ready"), &Q3Server::is_ready);
	ClassDB::bind_static_method("Q3Server", D_METHOD("error"), &Q3Server::error);
	ClassDB::bind_static_method("Q3Server", D_METHOD("exec", "text"), &Q3Server::exec);
	ClassDB::bind_static_method("Q3Server", D_METHOD("exec_now", "text"), &Q3Server::exec_now);
	ClassDB::bind_static_method("Q3Server", D_METHOD("frame", "msec"), &Q3Server::frame);
	ClassDB::bind_static_method("Q3Server", D_METHOD("running"), &Q3Server::running);
	ClassDB::bind_static_method("Q3Server", D_METHOD("time"), &Q3Server::time);
	ClassDB::bind_static_method("Q3Server", D_METHOD("cvar", "name"), &Q3Server::cvar);
	ClassDB::bind_static_method("Q3Server", D_METHOD("set_cvar", "name", "value"), &Q3Server::set_cvar);
	ClassDB::bind_static_method("Q3Server", D_METHOD("configstring", "index"), &Q3Server::configstring);
	ClassDB::bind_static_method("Q3Server", D_METHOD("connect_local", "userinfo"), &Q3Server::connect_local);
	ClassDB::bind_static_method("Q3Server", D_METHOD("disconnect_local", "client"), &Q3Server::disconnect_local);
	ClassDB::bind_static_method("Q3Server",
			D_METHOD("usercmd", "client", "server_time", "angles", "buttons", "weapon", "forward", "right", "up"),
			&Q3Server::usercmd);
	ClassDB::bind_static_method("Q3Server", D_METHOD("snapshot", "client"), &Q3Server::snapshot);
	ClassDB::bind_static_method("Q3Server", D_METHOD("predict", "client", "cmds"), &Q3Server::predict);
	ClassDB::bind_static_method("Q3Server", D_METHOD("next_commands", "client"), &Q3Server::next_commands);
	ClassDB::bind_static_method("Q3Server", D_METHOD("client_command", "client", "text"), &Q3Server::client_command);
	ClassDB::bind_static_method("Q3Server", D_METHOD("player_state", "client"), &Q3Server::player_state);
	ClassDB::bind_static_method("Q3Server", D_METHOD("client_state", "client"), &Q3Server::client_state);
	ClassDB::bind_static_method("Q3Server", D_METHOD("trace", "start", "end", "mins", "maxs", "pass_entity", "content_mask"),
			&Q3Server::trace);
	ClassDB::bind_static_method("Q3Server", D_METHOD("point_contents", "point", "pass_entity"), &Q3Server::point_contents);
	ClassDB::bind_static_method("Q3Server", D_METHOD("postgame"), &Q3Server::postgame);
}

static Vector3 v3(const float *f) {
	return Vector3(f[0], f[1], f[2]);
}

static PackedInt32Array ints(const int *a, int n) {
	PackedInt32Array out;
	out.resize(n);
	for (int i = 0; i < n; i++) {
		out.set(i, a[i]);
	}
	return out;
}

static Dictionary trajectory(const q3_trajectory &t) {
	Dictionary d;
	d["trType"] = t.trType;
	d["trTime"] = t.trTime;
	d["trDuration"] = t.trDuration;
	d["trBase"] = v3(t.trBase);
	d["trDelta"] = v3(t.trDelta);
	return d;
}

static Dictionary entity(const q3_entity_state &e) {
	Dictionary d;
	d["number"] = e.number;
	d["eType"] = e.eType;
	d["eFlags"] = e.eFlags;
	d["pos"] = trajectory(e.pos);
	d["apos"] = trajectory(e.apos);
	d["time"] = e.time;
	d["time2"] = e.time2;
	d["origin"] = v3(e.origin);
	d["origin2"] = v3(e.origin2);
	d["angles"] = v3(e.angles);
	d["angles2"] = v3(e.angles2);
	d["otherEntityNum"] = e.otherEntityNum;
	d["otherEntityNum2"] = e.otherEntityNum2;
	d["groundEntityNum"] = e.groundEntityNum;
	d["constantLight"] = e.constantLight;
	d["loopSound"] = e.loopSound;
	d["modelindex"] = e.modelindex;
	d["modelindex2"] = e.modelindex2;
	d["clientNum"] = e.clientNum;
	d["frame"] = e.frame;
	d["solid"] = e.solid;
	d["event"] = e.event;
	d["eventParm"] = e.eventParm;
	d["powerups"] = e.powerups;
	d["weapon"] = e.weapon;
	d["legsAnim"] = e.legsAnim;
	d["torsoAnim"] = e.torsoAnim;
	d["generic1"] = e.generic1;
	return d;
}

static Dictionary player(const q3_player_state &p) {
	Dictionary d;
	d["commandTime"] = p.commandTime;
	d["pm_type"] = p.pm_type;
	d["bobCycle"] = p.bobCycle;
	d["pm_flags"] = p.pm_flags;
	d["pm_time"] = p.pm_time;
	d["origin"] = v3(p.origin);
	d["velocity"] = v3(p.velocity);
	d["weaponTime"] = p.weaponTime;
	d["gravity"] = p.gravity;
	d["speed"] = p.speed;
	d["delta_angles"] = ints(p.delta_angles, 3);
	d["groundEntityNum"] = p.groundEntityNum;
	d["legsTimer"] = p.legsTimer;
	d["legsAnim"] = p.legsAnim;
	d["torsoTimer"] = p.torsoTimer;
	d["torsoAnim"] = p.torsoAnim;
	d["movementDir"] = p.movementDir;
	d["grapplePoint"] = v3(p.grapplePoint);
	d["eFlags"] = p.eFlags;
	d["eventSequence"] = p.eventSequence;
	d["events"] = ints(p.events, Q3_MAX_PS_EVENTS);
	d["eventParms"] = ints(p.eventParms, Q3_MAX_PS_EVENTS);
	d["externalEvent"] = p.externalEvent;
	d["externalEventParm"] = p.externalEventParm;
	d["externalEventTime"] = p.externalEventTime;
	d["clientNum"] = p.clientNum;
	d["weapon"] = p.weapon;
	d["weaponstate"] = p.weaponstate;
	d["viewangles"] = v3(p.viewangles);
	d["viewheight"] = p.viewheight;
	d["damageEvent"] = p.damageEvent;
	d["damageYaw"] = p.damageYaw;
	d["damagePitch"] = p.damagePitch;
	d["damageCount"] = p.damageCount;
	d["stats"] = ints(p.stats, Q3_MAX_STATS);
	d["persistant"] = ints(p.persistant, Q3_MAX_PERSISTANT);
	d["powerups"] = ints(p.powerups, Q3_MAX_POWERUPS);
	d["ammo"] = ints(p.ammo, Q3_MAX_WEAPONS);
	d["generic1"] = p.generic1;
	d["loopSound"] = p.loopSound;
	d["jumppad_ent"] = p.jumppad_ent;
	d["ping"] = p.ping;
	return d;
}

int Q3Server::start(const String &command_line) {
	return q3_host_init(command_line.utf8().get_data());
}

bool Q3Server::is_ready() {
	return q3_host_ready() != 0;
}

String Q3Server::error() {
	return String::utf8(q3_host_error());
}

void Q3Server::exec(const String &text) {
	String line = text;
	line += String("\n");
	q3_host_exec(line.utf8().get_data());
}

int Q3Server::exec_now(const String &text) {
	return q3_host_exec_now(text.utf8().get_data());
}

int Q3Server::frame(int msec) {
	return q3_host_frame(msec);
}

bool Q3Server::running() {
	return q3_host_server_running() != 0;
}

int Q3Server::time() {
	return q3_host_server_time();
}

String Q3Server::cvar(const String &name) {
	return String::utf8(q3_host_cvar(name.utf8().get_data()));
}

void Q3Server::set_cvar(const String &name, const String &value) {
	q3_host_set_cvar(name.utf8().get_data(), value.utf8().get_data());
}

String Q3Server::configstring(int index) {
	return String::utf8(q3_host_configstring(index));
}

int Q3Server::connect_local(const String &userinfo) {
	return q3_local_connect(userinfo.utf8().get_data());
}

void Q3Server::disconnect_local(int client) {
	q3_local_disconnect(client);
}

void Q3Server::usercmd(int client, int server_time, const Vector3 &angles, int buttons, int weapon,
		int forward, int right, int up) {
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
	q3_local_usercmd(client, &cmd);
}

// {serverTime, ps: {...}, entities: [{...}, ...]} or {} before the first snapshot.
Dictionary Q3Server::snapshot(int client) {
	static q3_snapshot snap;   // about 210 KB: too big for the stack
	Dictionary d;
	if (!q3_local_snapshot(client, &snap)) {
		return d;
	}
	d["serverTime"] = snap.serverTime;
	d["ps"] = player(snap.ps);
	Array ents;
	for (int i = 0; i < snap.num_entities; i++) {
		ents.push_back(entity(snap.entities[i]));
	}
	d["entities"] = ents;
	return d;
}

// cgame's prediction over `cmds` (oldest first), each {serverTime, angles: Vector3 (degrees),
// buttons, weapon, forward, right, up}. Returns the predicted playerState, or {} before the
// first snapshot.
Dictionary Q3Server::predict(int client, const Array &cmds) {
	static q3_usercmd buf[128];
	int n = 0;
	for (int i = MAX(0, (int)cmds.size() - 128); i < cmds.size(); i++) {
		Dictionary c = cmds[i];
		Vector3 a = c.get("angles", Vector3());
		q3_usercmd &u = buf[n++];
		u.serverTime = c.get("serverTime", 0);
		u.angles[0] = a.x;
		u.angles[1] = a.y;
		u.angles[2] = a.z;
		u.buttons = c.get("buttons", 0);
		u.weapon = c.get("weapon", 0);
		u.forwardmove = c.get("forward", 0);
		u.rightmove = c.get("right", 0);
		u.upmove = c.get("up", 0);
	}
	q3_player_state ps;
	if (n == 0 || !q3_local_predict(client, buf, n, &ps)) {
		return Dictionary();
	}
	return player(ps);
}

PackedStringArray Q3Server::next_commands(int client) {
	PackedStringArray out;
	const char *c;
	while ((c = q3_local_next_command(client)) != nullptr) {
		out.push_back(String::utf8(c));
	}
	return out;
}

bool Q3Server::client_command(int client, const String &text) {
	return q3_local_command(client, text.utf8().get_data()) != 0;
}

Dictionary Q3Server::player_state(int client) {
	q3_player_state ps;
	if (!q3_client_player_state(client, &ps)) {
		return Dictionary();
	}
	return player(ps);
}

int Q3Server::client_state(int client) {
	return q3_client_state(client);
}

// CG_Trace for effects, id units and axes: {fraction, endpos, normal, surfaceFlags, contents,
// entityNum, startsolid, allsolid}, or {} without a map.
Dictionary Q3Server::trace(const Vector3 &start, const Vector3 &end, const Vector3 &mins, const Vector3 &maxs,
		int pass_entity, int content_mask) {
	float s[3] = { start.x, start.y, start.z }, e[3] = { end.x, end.y, end.z };
	float mn[3] = { mins.x, mins.y, mins.z }, mx[3] = { maxs.x, maxs.y, maxs.z };
	q3_trace_result tr;
	Dictionary d;
	if (!q3_trace(s, e, mn, mx, pass_entity, content_mask, &tr)) {
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

int Q3Server::point_contents(const Vector3 &point, int pass_entity) {
	float p[3] = { point.x, point.y, point.z };
	return q3_point_contents(p, pass_entity);
}

// Single player's results at the intermission (g_arenas.c "postgame": clients, you, accuracy,
// impressive, excellent, gauntlet, score, perfect, then client rank score per player), read once.
String Q3Server::postgame() {
	return String::utf8(q3_host_postgame());
}
