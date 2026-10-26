/* The plain C interface between id's code and the Godot classes. Only this header (and
 * q3_types.h) is shared by both sides, so q_shared.h never meets godot-cpp. */
#ifndef Q3_API_H
#define Q3_API_H

#include "q3_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*q3_print_fn)(const char *text, int is_error);

/* Where Com_Printf / Sys_Print / Com_Error text goes (Godot's print / push_error). */
void q3_set_print(q3_print_fn fn);

/* Which ioquake3 commit this was built from. */
const char *q3_source_version(void);

/* bg_misc.c's bg_itemlist: the game's item table. */
int q3_item_count(void);
const char *q3_item_classname(int index);
int q3_item_quantity(int index);

/* Runs a call that hits Com_Error(ERR_DROP); returns 1 if control came back. */
int q3_test_error(void);

/* ---- the host: ioquake3's server, game module and botlib in this process (q3_host.c) ---- */

/* Com_Init with a Quake 3 command line ("+set fs_basepath ... +set sv_fps 125").
 * Once per process; 0 on success. */
int q3_host_init(const char *command_line);
int q3_host_ready(void);
/* The last ERR_DROP / ERR_FATAL message ("" if none). */
const char *q3_host_error(void);
/* Queues console text ("map q3dm17", "addbot sarge 3"); runs on the next frame. */
void q3_host_exec(const char *text);
/* Runs queued commands now (EXEC_NOW), guarded. 0 on success. */
int q3_host_exec_now(const char *text);
/* Advances the server msec milliseconds. 0 ok, 1 dropped (map ended in error), 2 fatal. */
int q3_host_frame(int msec);
int q3_host_server_running(void);
int q3_host_server_time(void);
const char *q3_host_cvar(const char *name);
/* Every console command / cvar the engine registered, one name a line (valid until the next call). */
const char *q3_host_command_names(void);
const char *q3_host_cvar_names(void);
/* Cvar_Flags (CVAR_ARCHIVE 1 ...); CVAR_NONEXISTENT if there's no such cvar. */
int q3_host_cvar_flags(const char *name);
/* A file through the engine's search path; NULL (len -1) if none. Valid until the next call. */
const unsigned char *q3_host_read_file(const char *path, int *len);
/* The pak files the engine has open (0) or the ones the game referenced (1), space separated. */
const char *q3_host_pak_names(int referenced);
void q3_host_set_cvar(const char *name, const char *value);
const char *q3_host_configstring(int index);

/* A human player in this process (a loopback client that never reads its packets).
 * Returns the client number, or -1 (q3_host_error() says why). */
int q3_local_connect(const char *userinfo);
void q3_local_disconnect(int client);
/* Feeds one usercmd, as a client packet would (SV_ClientThink). */
void q3_local_usercmd(int client, const q3_usercmd *cmd);
/* The newest snapshot the server built for this client; 0 if none yet. */
int q3_local_snapshot(int client, q3_snapshot *out);
/* cgame's prediction: the newest snapshot's playerState with Pmove run over the given
 * usercmds (oldest first; ones the server already ran are skipped). 0 if no snapshot yet. */
int q3_local_predict(int client, const q3_usercmd *cmds, int count, q3_player_state *out);
/* A console command from the local player (score, ready, say, team ...). 1 if sent. */
int q3_local_command(int client, const char *text);
/* Next reliable server command (print, cp, cs, scores, chat ...) or NULL. */
const char *q3_local_next_command(int client);
/* The client's live playerState (server side), or 0. */
int q3_client_player_state(int client, q3_player_state *out);
/* 0 free, 1 zombie, 2 connected, 3 primed, 4 active. */
int q3_client_state(int client);
/* cgame's CG_Trace / CG_PointContents for effects (lightning beam, shotgun pellets, tracers):
 * the live server world and entities. 0 without a running map. */
int q3_trace(const float *start, const float *end, const float *mins, const float *maxs,
		int pass_entity, int content_mask, q3_trace_result *out);
int q3_point_contents(const float *point, int pass_entity);
/* Every item on the map with its respawn state (item timers). Returns how many were written. */
int q3_host_items(q3_item_state *out, int max);
/* Single player's results ("postgame" from g_arenas.c, its arguments), once; "" if none since. */
const char *q3_host_postgame(void);
/* Every entity a snapshot could carry this frame (linked, not SVF_NOCLIENT), as the recorder sees them. */
int q3_host_entities(q3_entity_state *out, int max);

/* ---- UDP for public servers and the LAN (engine/q3_host.c, patch 0007) ---- */

/* Opens ioquake3's IPv4 UDP socket on `port` (the server answers plain Quake 3 clients and LAN getinfo broadcasts);
 * 1 if it opened. Polled every host frame. */
int q3_net_udp_open(int port);
void q3_net_udp_close(void);
int q3_net_udp_is_open(void);

/* ---- network transport (engine/q3_host.c, patch 0006): remote players over packets Godot carries ---- */

/* A peer is an integer 0..65535 the transport chooses. Feeds one packet from it to the server
 * (SV_PacketEvent); 1 if it was handled. */
int q3_net_receive(int peer, const unsigned char *data, int len);
/* Pops the next packet queued for sending. sock 1: the server's (to a remote player), 0: the net
 * client's in this process (to the server). Returns its length, -1 if none; *peer is the receiver. */
int q3_net_next(int sock, int *peer, unsigned char *buf, int cap);
int q3_net_pending(int sock);
/* The client slot a peer holds, or -1. */
int q3_net_peer_client(int peer);
/* The peer's transport closed: drop its player. */
void q3_net_peer_gone(int peer);
void q3_net_reset(void);

/* ---- net client (engine/q3_netclient.c): this process joining a server over packets ---- */

/* states: 0 disconnected, 1 connecting, 2 challenging, 3 connected, 4 primed (gamestate in), 5 active */
int q3_nc_connect(const char *userinfo, int maxpackets);
void q3_nc_disconnect(void);
void q3_nc_reset(void);
int q3_nc_state(void);
/* Why the last connection ended ("" if none): a refusal's text, a timeout, a bad packet. */
const char *q3_nc_error(void);
void q3_nc_set_maxpackets(int maxpackets);
/* A packet from the server. */
void q3_nc_receive(const unsigned char *data, int len);
/* Advances the client's clock msec; sends what is due (via q3_net_next sock 0). extrapolated: the
 * presentation had to extrapolate past the newest snapshot, so the clock nudges back. Returns the state. */
int q3_nc_frame(int msec, int extrapolated);
/* The server time to draw at, and to stamp usercmds with. */
/* Holds a primed client out of the world while the game loads the map (cleared by every connect). */
void q3_nc_hold_entry(int hold);
const char *q3_nc_missing_map(void);
int q3_nc_reload_map(void);
/* A remote console command with the server's rconpassword; its answer is read back with q3_nc_rcon_output. */
void q3_nc_rcon(const char *password, const char *command);
const char *q3_nc_rcon_output(void);
int q3_nc_time(void);
int q3_nc_client_num(void);
int q3_nc_server_id(void);
void q3_nc_usercmd(const q3_usercmd *cmd);
void q3_nc_command(const char *text);
const char *q3_nc_next_command(void);
const char *q3_nc_configstring(int index);
/* The newest snapshot at or before time and the one after it. 2 with both, 1 with the first only, 0 none. */
int q3_nc_snapshot_pair(int time, q3_snapshot *a, q3_snapshot *b, q3_nc_snap_info *info);
void q3_nc_get_stats(q3_nc_stats *out);
int q3_nc_predict(q3_player_state *out, float *error, int *error_time, int *teleported);
int q3_nc_trace(const float *start, const float *end, const float *mins, const float *maxs,
		int pass_entity, int content_mask, q3_trace_result *out);
int q3_nc_point_contents(const float *point, int pass_entity);
/* The loopback check: the server's snapshot for a client as the network carries it. */
int q3_nc_server_frame(int client, int message, q3_snapshot *out);

/* ---- replays (engine/q3_replay.c): the whole match recorded on the server ---- */

/* Starts recording at `hz` frames a second (40) after every server frame; stops by itself on
 * another map. 1 on success. */
int q3_record_start(int hz);
void q3_record_stop(void);
int q3_record_active(void);
int q3_record_frames(void);
/* The frame blocks recorded since the last q3_record_consume(). */
const unsigned char *q3_record_data(int *len);
void q3_record_consume(void);

/* A recording's frame blocks (the file between its JSON header and trailer), decoded for playback. */
typedef struct q3_replay q3_replay;
q3_replay *q3_replay_open(const unsigned char *data, int len, char *err, int errlen);
void q3_replay_close(q3_replay *r);
int q3_replay_frames(q3_replay *r);
int q3_replay_frame_time(q3_replay *r, int i);
int q3_replay_frame_is_key(q3_replay *r, int i);
int q3_replay_current(q3_replay *r);
int q3_replay_seek(q3_replay *r, int frame);
int q3_replay_has_next(q3_replay *r);
/* which 0: the frame shown, 1: the one after it. client -1: a free camera. */
int q3_replay_entities(q3_replay *r, int which, int client, q3_entity_state *out, int max);
int q3_replay_player_state(q3_replay *r, int which, int client, q3_player_state *out);
const char *q3_replay_configstring(q3_replay *r, int index);
int q3_replay_command_count(q3_replay *r);
const char *q3_replay_command(q3_replay *r, int i, int *frame, unsigned *mask);
int q3_replay_event_count(q3_replay *r);
int q3_replay_get_event(q3_replay *r, int i, q3_replay_event *out);
/* The map's collision model without a server (0 ok), for traces in playback. */
int q3_replay_load_map(const char *name, int *checksum);
void q3_replay_unload_map(void);
int q3_replay_trace(q3_replay *r, const float *start, const float *end, const float *mins, const float *maxs,
		int pass_entity, int content_mask, q3_trace_result *out);
int q3_replay_point_contents(q3_replay *r, const float *point, int pass_entity);
/* A Quake 3 demo (.dm_68, protocol 68) as replay frame blocks for q3_replay_open: 1 on success
 * (free *out with q3_demo_free). max_frames > 0 stops after that many frames. */
int q3_demo_convert(const unsigned char *data, int len, int max_frames, unsigned char **out, int *out_len, char *err, int errlen);
void q3_demo_free(unsigned char *data);

#ifdef __cplusplus
}
#endif

#endif
