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
