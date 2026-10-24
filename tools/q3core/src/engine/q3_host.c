/* The host: ioquake3's server, the game module (qagamex86_64.dll) and botlib running inside
 * Godot. It replaces Com_Frame's loop: Godot calls q3_host_frame(msec) on its own schedule,
 * and the human player is a loopback client fed usercmds directly (no packets are read; the
 * snapshots and reliable commands the server makes for it are taken straight from client_t).
 *
 * Every entry point that can reach id's code is guarded: Com_Error(ERR_DROP) longjmps to
 * abortframe (common.c), ERR_FATAL / Sys_Error to q3_fatal_frame (q3_sys.c via q3_engine_fatal).
 * After a fatal error the engine is stopped for good; ioquake3 can't Com_Init twice. */
#include <setjmp.h>

#include "../../thirdparty/ioq3/code/server/server.h"
#include "../q3_api.h"
#include "q3_engine.h"

extern jmp_buf abortframe;
extern char com_errorMessage[];   /* common.c */

static q3_print_fn print_fn;
static int dead;
/* the guard's state (q3_engine.h GUARD), shared with q3_replay.c */
int q3_ready;
char q3_last_error[1024];
jmp_buf q3_fatal_frame;
int q3_fatal_armed;

/* Per-client state for local (human) clients. */
#define CMD_QUEUE 1024
typedef struct {
	int active;
	int snap_frame;           /* frames[] slot of the newest snapshot, -1 if none */
	int snap_time;            /* sv.time it was built at */
	int cmd_read, cmd_write;  /* reliable server commands copied out of client_t */
	char cmds[CMD_QUEUE][MAX_STRING_CHARS];
} local_client_t;
static local_client_t locals[MAX_CLIENTS];

/* The C++ side's structs must match id's layouts byte for byte. */
typedef char check_ps[sizeof(q3_player_state) == sizeof(playerState_t) ? 1 : -1];
typedef char check_es[sizeof(q3_entity_state) == sizeof(entityState_t) ? 1 : -1];
typedef char check_tr[sizeof(q3_trajectory) == sizeof(trajectory_t) ? 1 : -1];

void q3_set_print(q3_print_fn fn) { print_fn = fn; }

void q3_engine_print(const char *text, int is_error) {
	if (print_fn)
		print_fn(text, is_error);
	else
		fputs(text, is_error ? stderr : stdout);
}

void q3_engine_fatal(const char *message) {
	Q_strncpyz(q3_last_error, message, sizeof(q3_last_error));
	q3_engine_print(va("fatal: %s\n", message), 1);
	q3_ready = 0;
	dead = 1;
	if (q3_fatal_armed) {
		q3_fatal_armed = 0;
		longjmp(q3_fatal_frame, 1);
	}
	abort();
}

/* g_arenas.c UpdateTournamentInfo sends single player's results as the console command
 * "postgame ..." for q3_ui's postgame menu; there is no UI module here, so the host keeps it. */
static char postgame[MAX_STRING_CHARS];

static void Cmd_Postgame_f(void) {
	Q_strncpyz(postgame, Cmd_Args(), sizeof(postgame));
}

const char *q3_host_postgame(void) {
	static char out[MAX_STRING_CHARS];
	Q_strncpyz(out, postgame, sizeof(out));
	postgame[0] = 0;
	return out;
}

int q3_host_ready(void) { return q3_ready; }
const char *q3_host_error(void) { return q3_last_error; }

int q3_host_init(const char *command_line) {
	if (q3_ready || dead) {
		Q_strncpyz(q3_last_error, dead ? "the engine stopped after a fatal error; restart the game" : "already initialized", sizeof(q3_last_error));
		return 1;
	}
	q3_last_error[0] = 0;
	if (setjmp(q3_fatal_frame)) {
		q3_fatal_armed = 0;
		return 2;
	}
	q3_fatal_armed = 1;
	Sys_PlatformInit();
	Com_Init((char *)command_line);   /* its own setjmp(abortframe): an ERR_DROP here is fatal */
	q3_fatal_armed = 0;
	Cmd_AddCommand("postgame", Cmd_Postgame_f);
	q3_ready = 1;
	return 0;
}

void q3_host_exec(const char *text) {
	if (q3_ready)
		Cbuf_AddText(text);
}

int q3_host_exec_now(const char *text) {
	GUARD(1, 2);
	Cbuf_ExecuteText(EXEC_NOW, text);
	UNGUARD();
	return 0;
}

static void copy_commands(int i, client_t *cl) {
	local_client_t *lc = &locals[i];
	while (cl->reliableAcknowledge < cl->reliableSequence) {
		cl->reliableAcknowledge++;
		if (lc->cmd_write - lc->cmd_read >= CMD_QUEUE)
			lc->cmd_read++;   /* nobody is reading: drop the oldest */
		Q_strncpyz(lc->cmds[lc->cmd_write % CMD_QUEUE],
			cl->reliableCommands[cl->reliableAcknowledge & (MAX_RELIABLE_COMMANDS - 1)], MAX_STRING_CHARS);
		lc->cmd_write++;
	}
}

/* What a real client would do on receiving the gamestate after a map change: enter the world. */
static void keep_local_clients(void) {
	int i;
	for (i = 0; i < sv_maxclients->integer; i++) {
		client_t *cl = &svs.clients[i];
		if (!locals[i].active)
			continue;
		if (cl->state == CS_FREE || cl->state == CS_ZOMBIE) {
			locals[i].active = 0;   /* dropped by the server (kicked, map failed) */
			continue;
		}
		cl->lastPacketTime = svs.time;
		if (cl->state == CS_CONNECTED || cl->state == CS_PRIMED) {
			cl->state = CS_PRIMED;
			locals[i].snap_frame = -1;
			SV_ClientEnterWorld(cl, NULL);
		}
	}
}

int q3_host_frame(int msec) {
	int i;
	GUARD(1, 2);
	Cbuf_Execute();
	if (com_sv_running->integer) {   /* SV_Frame blocks forever (Sys_Sleep) with no map */
		keep_local_clients();
		SV_Frame(msec);
		for (i = 0; i < sv_maxclients->integer; i++) {
			client_t *cl = &svs.clients[i];
			if (!locals[i].active || cl->state < CS_CONNECTED)
				continue;
			copy_commands(i, cl);
			if (cl->state == CS_ACTIVE && cl->lastSnapshotTime == svs.time) {
				locals[i].snap_frame = (cl->netchan.outgoingSequence - 1) & PACKET_MASK;
				locals[i].snap_time = sv.time;
			}
		}
		q3_record_after_frame();
	}
	UNGUARD();
	return 0;
}

int q3_host_server_running(void) { return q3_ready && com_sv_running && com_sv_running->integer; }
int q3_host_server_time(void) { return q3_host_server_running() ? sv.time : 0; }

const char *q3_host_cvar(const char *name) { return q3_ready ? Cvar_VariableString(name) : ""; }

void q3_host_set_cvar(const char *name, const char *value) {
	if (q3_ready)
		Cvar_Set(name, value);
}

const char *q3_host_configstring(int index) {
	if (!q3_host_server_running() || index < 0 || index >= MAX_CONFIGSTRINGS || !sv.configstrings[index])
		return "";
	return sv.configstrings[index];
}

/* ---------------------------------------------------------------- local clients */

int q3_local_connect(const char *userinfo) {
	int i;
	intptr_t denied;
	client_t *cl = NULL;
	netadr_t adr;
	char info[MAX_INFO_STRING];

	if (!q3_host_server_running()) {
		Q_strncpyz(q3_last_error, "no map running", sizeof(q3_last_error));
		return -1;
	}
	for (i = 0; i < sv_maxclients->integer; i++) {
		if (svs.clients[i].state == CS_FREE) {
			cl = &svs.clients[i];
			break;
		}
	}
	if (!cl) {
		Q_strncpyz(q3_last_error, "server is full", sizeof(q3_last_error));
		return -1;
	}
	GUARD(-1, -1);
	Q_strncpyz(info, userinfo, sizeof(info));
	Info_SetValueForKey(info, "snaps", va("%d", sv_fps->integer));   /* a snapshot every server frame */
	/* what SV_DirectConnect gives the player at the keyboard (id 1.32's NET_AdrToString of a
	   loopback address): ClientConnect marks it pers.localClient, and its first spawn takes a
	   spawn point marked 'initial' (q3dm0's mirror room) */
	Info_SetValueForKey(info, "ip", "localhost");
	Com_Memset(cl, 0, sizeof(*cl));
	cl->gentity = SV_GentityNum(i);
	Com_Memset(&adr, 0, sizeof(adr));
	adr.type = NA_LOOPBACK;
	Netchan_Setup(NS_SERVER, &cl->netchan, adr, 0, 0, qfalse);
	cl->netchan_end_queue = &cl->netchan_start_queue;
	Q_strncpyz(cl->userinfo, info, sizeof(cl->userinfo));

	denied = VM_Call(gvm, GAME_CLIENT_CONNECT, i, qtrue, qfalse);
	if (denied) {
		Q_strncpyz(q3_last_error, (char *)VM_ExplicitArgPtr(gvm, denied), sizeof(q3_last_error));
		Com_Memset(cl, 0, sizeof(*cl));
		UNGUARD();
		return -1;
	}
	SV_UserinfoChanged(cl);
	cl->state = CS_CONNECTED;
	cl->lastSnapshotTime = 0;
	cl->lastPacketTime = svs.time;
	cl->lastConnectTime = svs.time;
	cl->gamestateMessageNum = -1;

	Com_Memset(&locals[i], 0, sizeof(locals[i]));
	locals[i].active = 1;
	locals[i].snap_frame = -1;
	keep_local_clients();   /* gamestate "received": enter the world now */
	UNGUARD();
	return i;
}

void q3_local_disconnect(int client) {
	client_t *cl;
	if (client < 0 || client >= MAX_CLIENTS || !locals[client].active || !q3_host_server_running())
		return;
	cl = &svs.clients[client];
	{
		GUARD_V();
		SV_DropClient(cl, "disconnected");
		UNGUARD();
	}
	locals[client].active = 0;
}

/* q3_usercmd (degrees) -> usercmd_t, packed the way CL_CreateCmd does. */
static void to_usercmd(const q3_usercmd *in, usercmd_t *cmd) {
	Com_Memset(cmd, 0, sizeof(*cmd));
	cmd->serverTime = in->serverTime;
	cmd->angles[0] = ANGLE2SHORT(in->angles[0]);
	cmd->angles[1] = ANGLE2SHORT(in->angles[1]);
	cmd->angles[2] = ANGLE2SHORT(in->angles[2]);
	cmd->buttons = in->buttons;
	cmd->weapon = (byte)in->weapon;
	cmd->forwardmove = (signed char)Com_Clamp(-127, 127, in->forwardmove);
	cmd->rightmove = (signed char)Com_Clamp(-127, 127, in->rightmove);
	cmd->upmove = (signed char)Com_Clamp(-127, 127, in->upmove);
}

void q3_local_usercmd(int client, const q3_usercmd *in) {
	usercmd_t cmd;
	client_t *cl;
	if (client < 0 || client >= MAX_CLIENTS || !locals[client].active || !q3_host_server_running())
		return;
	cl = &svs.clients[client];
	if (cl->state != CS_ACTIVE || in->serverTime <= cl->lastUsercmd.serverTime)
		return;   /* SV_UserMove skips commands it already ran */
	to_usercmd(in, &cmd);
	{
		GUARD_V();
		cl->lastPacketTime = svs.time;
		SV_ClientThink(cl, &cmd);
		UNGUARD();
	}
}

int q3_local_snapshot(int client, q3_snapshot *out) {
	client_t *cl;
	clientSnapshot_t *frame;
	int i;
	if (client < 0 || client >= MAX_CLIENTS || !locals[client].active || locals[client].snap_frame < 0 || !q3_host_server_running())
		return 0;
	cl = &svs.clients[client];
	frame = &cl->frames[locals[client].snap_frame];
	out->serverTime = locals[client].snap_time;
	Com_Memcpy(&out->ps, &frame->ps, sizeof(playerState_t));
	out->num_entities = frame->num_entities;
	for (i = 0; i < frame->num_entities; i++) {
		Com_Memcpy(&out->entities[i], &svs.snapshotEntities[(frame->first_entity + i) % svs.numSnapshotEntities],
			sizeof(entityState_t));
	}
	return 1;
}

/* A console command from the local player ("score", "ready", "say hi", "team red"), as if its
 * client had sent it reliably (SV_ExecuteClientCommand). */
int q3_local_command(int client, const char *text) {
	client_t *cl;
	if (client < 0 || client >= MAX_CLIENTS || !locals[client].active || !q3_host_server_running())
		return 0;
	cl = &svs.clients[client];
	if (cl->state != CS_ACTIVE)
		return 0;
	{
		GUARD(0, 0);
		SV_ExecuteClientCommand(cl, text, qtrue);
		UNGUARD();
	}
	return 1;
}

const char *q3_local_next_command(int client) {
	local_client_t *lc;
	if (client < 0 || client >= MAX_CLIENTS)
		return NULL;
	lc = &locals[client];
	if (lc->cmd_read >= lc->cmd_write)
		return NULL;
	return lc->cmds[lc->cmd_read++ % CMD_QUEUE];
}

int q3_client_player_state(int client, q3_player_state *out) {
	if (!q3_host_server_running() || client < 0 || client >= sv_maxclients->integer || svs.clients[client].state < CS_CONNECTED)
		return 0;
	Com_Memcpy(out, SV_GameClientNum(client), sizeof(playerState_t));
	return 1;
}

int q3_client_state(int client) {
	if (!q3_host_server_running() || client < 0 || client >= sv_maxclients->integer)
		return 0;
	return svs.clients[client].state;
}

int q3_trace(const float *start, const float *end, const float *mins, const float *maxs,
		int pass_entity, int content_mask, q3_trace_result *out) {
	trace_t tr;
	vec3_t s, e, mn, mx;
	if (!q3_host_server_running())
		return 0;
	VectorCopy(start, s);
	VectorCopy(end, e);
	VectorCopy(mins, mn);
	VectorCopy(maxs, mx);
	{
		GUARD(0, 0);
		SV_Trace(&tr, s, mn, mx, e, pass_entity, content_mask, qfalse);
		UNGUARD();
	}
	out->fraction = tr.fraction;
	VectorCopy(tr.endpos, out->endpos);
	VectorCopy(tr.plane.normal, out->normal);
	out->surfaceFlags = tr.surfaceFlags;
	out->contents = tr.contents;
	out->entityNum = tr.entityNum;
	out->startsolid = tr.startsolid;
	out->allsolid = tr.allsolid;
	return 1;
}

/* q3_game_layout.c */
int q3_gentity_inuse_offset(void);
int q3_gentity_nextthink_offset(void);

/* g_items.c: a taken item is SVF_NOCLIENT until RespawnItem runs at nextthink; a powerup
 * waits EF_NODRAW for its first spawn (FinishSpawningItem). Only reads the game's memory. */
int q3_host_items(q3_item_state *out, int max) {
	int i, n = 0;
	int inuse = q3_gentity_inuse_offset(), next = q3_gentity_nextthink_offset();
	if (!q3_host_server_running() || !sv.gentities)
		return 0;
	for (i = MAX_CLIENTS; i < sv.num_entities && n < max; i++) {
		sharedEntity_t *ent = SV_GentityNum(i);
		int think;
		if (ent->s.eType != ET_ITEM || !*(qboolean *)((byte *)ent + inuse))
			continue;
		think = *(int *)((byte *)ent + next);
		out[n].number = i;
		out[n].item = ent->s.modelindex;
		VectorCopy(ent->s.pos.trBase, out[n].origin);
		out[n].taken = (ent->r.svFlags & SVF_NOCLIENT) || (ent->s.eFlags & EF_NODRAW);
		out[n].respawn_time = out[n].taken && think > sv.time ? think : 0;
		n++;
	}
	return n;
}

int q3_point_contents(const float *point, int pass_entity) {
	vec3_t p;
	int c;
	if (!q3_host_server_running())
		return 0;
	VectorCopy(point, p);
	{
		GUARD(0, 0);
		c = SV_PointContents(p, pass_entity);
		UNGUARD();
	}
	return c;
}

/* ---------------------------------------------------------------- client-side prediction */

/* cgame traces against the snapshot; a local client can trace the live server world, which is
 * exactly what its own ClientThink will hit. */
static void pm_trace(trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs,
		const vec3_t end, int passEntityNum, int contentMask) {
	/* SV_Trace takes non-const vectors but doesn't write them */
	SV_Trace(results, (float *)start, (float *)mins, (float *)maxs, (float *)end, passEntityNum, contentMask, qfalse);
}

static int pm_pointcontents(const vec3_t point, int passEntityNum) {
	return SV_PointContents(point, passEntityNum);
}

int q3_local_predict(int client, const q3_usercmd *cmds, int count, q3_player_state *out) {
	static playerState_t ps;
	pmove_t pm;
	int i, msec;
	usercmd_t latest;
	client_t *cl;
	if (client < 0 || client >= MAX_CLIENTS || !locals[client].active || locals[client].snap_frame < 0 || !q3_host_server_running())
		return 0;
	cl = &svs.clients[client];
	{
		GUARD(0, 0);
		ps = cl->frames[locals[client].snap_frame].ps;
		Com_Memset(&pm, 0, sizeof(pm));
		pm.ps = &ps;
		pm.trace = pm_trace;
		pm.pointcontents = pm_pointcontents;
		pm.tracemask = ps.pm_type == PM_DEAD ? MASK_PLAYERSOLID & ~CONTENTS_BODY : MASK_PLAYERSOLID;
		if (ps.persistant[PERS_TEAM] == TEAM_SPECTATOR)
			pm.tracemask &= ~CONTENTS_BODY;
		pm.noFootsteps = (Cvar_VariableIntegerValue("dmflags") & DF_NO_FOOTSTEPS) > 0;
		pm.pmove_fixed = Cvar_VariableIntegerValue("pmove_fixed");
		msec = Cvar_VariableIntegerValue("pmove_msec");
		pm.pmove_msec = msec < 8 ? 8 : (msec > 33 ? 33 : msec);
		to_usercmd(&cmds[count > 0 ? count - 1 : 0], &latest);
		for (i = 0; i < count; i++) {   /* CG_PredictPlayerState's loop */
			to_usercmd(&cmds[i], &pm.cmd);
			if (pm.pmove_fixed)
				PM_UpdateViewAngles(pm.ps, &pm.cmd);
			if (pm.cmd.serverTime <= ps.commandTime || pm.cmd.serverTime > latest.serverTime)
				continue;
			pm.gauntletHit = qfalse;
			if (pm.pmove_fixed)
				pm.cmd.serverTime = ((pm.cmd.serverTime + pm.pmove_msec - 1) / pm.pmove_msec) * pm.pmove_msec;
			Pmove(&pm);
		}
		UNGUARD();
	}
	Com_Memcpy(out, &ps, sizeof(ps));
	return 1;
}
