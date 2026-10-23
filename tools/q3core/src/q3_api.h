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
/* Single player's results ("postgame" from g_arenas.c, its arguments), once; "" if none since. */
const char *q3_host_postgame(void);

#ifdef __cplusplus
}
#endif

#endif
