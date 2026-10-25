/* The net client (MULTIPLAYER_PLAN.md M2): the network half of ioquake3's client, without the
 * renderer, sound, cgame VM or console. Godot's side (Q3Net) feeds it the packets a transport
 * brings and takes the packets it makes (q3_net_next with sock 0, patch 0006 routes the netchan's
 * sends there). It speaks id's own protocol:
 *
 *   getchallenge / challengeResponse / connect / connectResponse   (CL_CheckForResend, CL_ConnectionlessPacket)
 *   gamestate: configstrings, baselines, client number, checksum feed   (CL_ParseGamestate)
 *   snapshots delta-compressed against earlier ones                     (CL_ParseSnapshot, CL_ParsePacketEntities)
 *   server commands, one reliable stream each way                       (CL_ParseCommandString, CL_WritePacket)
 *   usercmds, several to a packet, delta-encoded and keyed             (CL_WritePacket)
 *   the clock: serverTimeDelta drifting toward the newest snapshot     (CL_AdjustTimeDelta, CL_SetCGameTime)
 *   prediction: Pmove over the unacknowledged usercmds, with the error  (CG_PredictPlayerState)
 *
 * The collision world is the engine's own (CM_*): a process that runs a match already has the map;
 * a process that only joins loads it from the gamestate (maps/<mapname>.bsp from the player's paks).
 * One client per process.
 *
 * Errors from msg.c (a bad packet) are ERR_DROP: every entry point is guarded, and a packet that
 * trips one disconnects with the message instead of taking Godot down. */
#include "../../thirdparty/ioq3/code/server/server.h"
#include "../q3_api.h"
#include "q3_engine.h"

#define NC_CMD_BACKUP 64
#define NC_CMD_MASK (NC_CMD_BACKUP - 1)
#define NC_MAX_PARSE_ENTITIES (PACKET_BACKUP * MAX_SNAPSHOT_ENTITIES)
#define NC_RETRANSMIT_MS 1000
#define NC_CONNECT_GIVE_UP_MS 20000
#define NC_RESET_TIME 500
#define NC_PORT 27960
#define NC_COMMAND_QUEUE 256
#define NC_PACKETDUP 1   /* cl_packetdup, Quake 3's default (2 made no measurable difference on a lossy link: M8) */

/* GUARD / GUARD_V, but an ERR_DROP from a bad packet disconnects with its message instead of just returning */
#define NC_GUARD_FAIL(fatal) if (!q3_ready) return (fatal); if (setjmp(q3_fatal_frame)) { q3_fatal_armed = 0; nc_fail("The engine stopped"); return (fatal); } q3_fatal_armed = 1; if (setjmp(abortframe)) { q3_fatal_armed = 0; Q_strncpyz(q3_last_error, com_errorMessage, sizeof(q3_last_error)); nc_fail(com_errorMessage); return (fatal); }
#define NC_GUARD_FAIL_V() if (!q3_ready) return; if (setjmp(q3_fatal_frame)) { q3_fatal_armed = 0; nc_fail("The engine stopped"); return; } q3_fatal_armed = 1; if (setjmp(abortframe)) { q3_fatal_armed = 0; Q_strncpyz(q3_last_error, com_errorMessage, sizeof(q3_last_error)); nc_fail(com_errorMessage); return; }

enum { NC_DISCONNECTED, NC_CONNECTING, NC_CHALLENGING, NC_CONNECTED, NC_PRIMED, NC_ACTIVE };

typedef struct {
	int valid;
	int snapFlags;
	int serverTime;
	int messageNum;
	int deltaNum;
	int ping;
	byte areamask[MAX_MAP_AREA_BYTES];
	int cmdNum;
	playerState_t ps;
	int numEntities;
	int parseEntitiesNum;
} nc_snapshot_t;

typedef struct {
	int cmdNumber, serverTime, realtime;
} nc_out_packet_t;

typedef struct {
	int state;
	char error[MAX_STRING_CHARS];
	netadr_t server;
	netchan_t chan;
	int challenge, qport;
	int realtime;           /* ms the client has run (cls.realtime) */
	int connectTime, connectStart, lastPacketTime, lastPacketSentTime;
	int timeout_ms, maxpackets;
	char userinfo[MAX_INFO_STRING];
	int maxpackets_set;

	/* the stream of reliable commands */
	int serverId, serverMessageSequence, serverCommandSequence;
	int reliableSequence, reliableAcknowledge;
	char reliableCommands[MAX_RELIABLE_COMMANDS][MAX_STRING_CHARS];
	char serverCommands[MAX_RELIABLE_COMMANDS][MAX_STRING_CHARS];
	int clientNum, checksumFeed;

	/* the gamestate */
	char *cs[MAX_CONFIGSTRINGS];
	char *big;
	entityState_t baselines[MAX_GENTITIES];
	char mapname[MAX_QPATH];
	int map_loaded;           /* this client loaded the collision model (and frees it) */
	int map_missing;          /* the server's map isn't in any pak here: the game fetches it, then q3_nc_reload_map */

	/* snapshots */
	nc_snapshot_t snapshots[PACKET_BACKUP];
	nc_snapshot_t snap;
	entityState_t parseEntities[NC_MAX_PARSE_ENTITIES];
	int parseEntitiesNum;
	int newSnapshots, extrapolatedSnapshot;
	int serverTime, oldServerTime, oldFrameServerTime, serverTimeDelta;
	int received;             /* snapshots parsed since connecting */

	/* usercmds */
	usercmd_t cmds[NC_CMD_BACKUP];
	int cmdNumber;
	nc_out_packet_t outPackets[PACKET_BACKUP];

	/* server commands for Godot, in order (cs and bcs are applied here as well) */
	char queue[NC_COMMAND_QUEUE][MAX_STRING_CHARS];
	int queue_read, queue_write;

	/* prediction */
	playerState_t predicted;
	int predicted_valid;
	int snap_message_predicted;
	float predicted_error[3];
	int predicted_error_time;
	int teleport_bit;
	int hyperspace;

	/* statistics */
	int packets_in, packets_out, bytes_in, bytes_out, dropped;
} netclient_t;

static netclient_t nc;
static int nc_hold_entry;
static int nc_active_at;     /* realtime the client became active */
#define NC_SETTLE_MS 1500   /* after that, a correction snaps instead of easing: the view has only just appeared */   /* the game is still loading the map: don't enter the world yet (q3_nc_hold_entry) */
static char nc_rcon_out[4096];   /* what the server printed back to a remote console command */
static pmoveRules_t nc_rules;

static void nc_fail(const char *message);

/* ---------------------------------------------------------------- strings */

static void nc_set_cs(int index, const char *s) {
	if (index < 0 || index >= MAX_CONFIGSTRINGS)
		return;
	free(nc.cs[index]);
	nc.cs[index] = NULL;
	if (s[0]) {
		size_t n = strlen(s) + 1;
		nc.cs[index] = malloc(n);
		if (nc.cs[index])
			memcpy(nc.cs[index], s, n);
	}
	if (index == CS_SYSTEMINFO)   /* CL_SystemInfoChanged: when the serverId changes, so do our messages */
		nc.serverId = atoi(Info_ValueForKey(nc.cs[CS_SYSTEMINFO] ? nc.cs[CS_SYSTEMINFO] : "", "sv_serverid"));
}

static void nc_free_strings(void) {
	int i;
	for (i = 0; i < MAX_CONFIGSTRINGS; i++) {
		free(nc.cs[i]);
		nc.cs[i] = NULL;
	}
	free(nc.big);
	nc.big = NULL;
}

/* "cs 5 <quote>text<quote>": the index and the rest without its quotes (Cmd_ArgsFrom(2)). */
static int nc_parse_cs_command(const char *s, int *index, char *text, int size) {
	const char *p = strchr(s, ' ');
	int n;
	if (!p)
		return 0;
	*index = atoi(p + 1);
	p = strchr(p + 1, ' ');
	if (!p)
		return 0;
	while (*p == ' ')
		p++;
	if (*p == 34)
		p++;
	Q_strncpyz(text, p, size);
	n = (int)strlen(text);
	while (n > 0 && (text[n - 1] == 10 || text[n - 1] == 13 || text[n - 1] == 32))
		text[--n] = 0;
	if (n > 0 && text[n - 1] == 34)
		text[--n] = 0;
	return 1;
}

static void nc_push_command(const char *s) {
	if (nc.queue_write - nc.queue_read >= NC_COMMAND_QUEUE)
		nc.queue_read++;
	Q_strncpyz(nc.queue[nc.queue_write % NC_COMMAND_QUEUE], s, MAX_STRING_CHARS);
	nc.queue_write++;
}

/* A server command: cs and bcs change the configstring table (CL_ConfigstringModified); every
 * command, those included, is queued for Godot in order. */
static void nc_server_command(const char *s) {
	static char text[BIG_INFO_STRING];
	int index;
	if (!Q_strncmp(s, "cs ", 3)) {
		if (nc_parse_cs_command(s, &index, text, sizeof(text)))
			nc_set_cs(index, text);
	} else if (!Q_strncmp(s, "bcs0 ", 5) || !Q_strncmp(s, "bcs1 ", 5) || !Q_strncmp(s, "bcs2 ", 5)) {
		char *joined;
		size_t a, b;
		if (nc_parse_cs_command(s, &index, text, sizeof(text))) {
			if (s[3] == '0') {
				free(nc.big);
				nc.big = NULL;
			}
			a = nc.big ? strlen(nc.big) : 0;
			b = strlen(text);
			joined = malloc(a + b + 1);
			if (joined) {
				if (a)
					memcpy(joined, nc.big, a);
				memcpy(joined + a, text, b + 1);
				free(nc.big);
				nc.big = joined;
				if (s[3] == '2') {
					nc_set_cs(index, nc.big);
					free(nc.big);
					nc.big = NULL;
				}
			}
		}
		return;   /* the pieces mean nothing to Godot; the finished string is in the table */
	}
	nc_push_command(s);
}

/* ---------------------------------------------------------------- the connection */

static netadr_t nc_server_adr(void) {
	netadr_t adr;
	Com_Memset(&adr, 0, sizeof(adr));
	adr.type = NA_IP;
	adr.ip[0] = 10;
	adr.ip[1] = 77;
	adr.ip[2] = 0;
	adr.ip[3] = 1;   /* the server's peer: the transport knows who it is talking to */
	adr.port = BigShort(NC_PORT);
	return adr;
}

static void nc_clear_state(void) {
	Com_Memset(nc.snapshots, 0, sizeof(nc.snapshots));
	Com_Memset(&nc.snap, 0, sizeof(nc.snap));
	Com_Memset(nc.baselines, 0, sizeof(nc.baselines));
	Com_Memset(nc.cmds, 0, sizeof(nc.cmds));
	Com_Memset(nc.outPackets, 0, sizeof(nc.outPackets));
	nc.parseEntitiesNum = 0;
	nc.newSnapshots = nc.extrapolatedSnapshot = 0;
	nc.serverTime = nc.oldServerTime = nc.oldFrameServerTime = nc.serverTimeDelta = 0;
	nc.cmdNumber = 0;
	nc.received = 0;
	nc.predicted_valid = 0;
	nc.snap_message_predicted = -1;
	VectorClear(nc.predicted_error);
	nc.predicted_error_time = 0;
	nc.queue_read = nc.queue_write = 0;
	nc_free_strings();
}

static void nc_unload_map(void) {
	if (nc.map_loaded && q3_ready && !com_sv_running->integer)
		CM_ClearMap();
	nc.map_loaded = 0;
	nc.map_missing = 0;
}

static void nc_fail(const char *message) {
	Q_strncpyz(nc.error, message, sizeof(nc.error));
	nc.state = NC_DISCONNECTED;
}

static void nc_send_oob(const char *text) {
	NET_OutOfBandPrint(NS_CLIENT, nc.server, "%s", text);
}

/* ---------------------------------------------------------------- reliable commands */

static void nc_add_reliable(const char *cmd) {
	if (nc.reliableSequence - nc.reliableAcknowledge > MAX_RELIABLE_COMMANDS) {
		nc_fail("Client command overflow");
		return;
	}
	nc.reliableSequence++;
	Q_strncpyz(nc.reliableCommands[nc.reliableSequence & (MAX_RELIABLE_COMMANDS - 1)], cmd, MAX_STRING_CHARS);
}

/* ---------------------------------------------------------------- usercmd packets */

static void nc_write_packet(void) {
	msg_t buf;
	byte data[MAX_MSGLEN];
	int i, j, packetNum, oldPacketNum, count, key;
	usercmd_t *cmd, *oldcmd, nullcmd;

	Com_Memset(&nullcmd, 0, sizeof(nullcmd));
	oldcmd = &nullcmd;
	MSG_Init(&buf, data, sizeof(data));
	MSG_Bitstream(&buf);
	MSG_WriteLong(&buf, nc.serverId);
	MSG_WriteLong(&buf, nc.serverMessageSequence);
	MSG_WriteLong(&buf, nc.serverCommandSequence);
	for (i = nc.reliableAcknowledge + 1; i <= nc.reliableSequence; i++) {
		MSG_WriteByte(&buf, clc_clientCommand);
		MSG_WriteLong(&buf, i);
		MSG_WriteString(&buf, nc.reliableCommands[i & (MAX_RELIABLE_COMMANDS - 1)]);
	}
	/* every usercmd made since NC_PACKETDUP packets before the last, so that many lost in a row cost nothing */
	oldPacketNum = (nc.chan.outgoingSequence - 1 - NC_PACKETDUP) & PACKET_MASK;
	count = nc.cmdNumber - nc.outPackets[oldPacketNum].cmdNumber;
	if (count > MAX_PACKET_USERCMDS)
		count = MAX_PACKET_USERCMDS;
	if (count > NC_CMD_BACKUP - 1)
		count = NC_CMD_BACKUP - 1;
	if (count >= 1) {
		if (!nc.snap.valid || nc.serverMessageSequence != nc.snap.messageNum)
			MSG_WriteByte(&buf, clc_moveNoDelta);
		else
			MSG_WriteByte(&buf, clc_move);
		MSG_WriteByte(&buf, count);
		key = nc.checksumFeed;
		key ^= nc.serverMessageSequence;
		key ^= MSG_HashKey(nc.serverCommands[nc.serverCommandSequence & (MAX_RELIABLE_COMMANDS - 1)], 32);
		for (i = 0; i < count; i++) {
			j = (nc.cmdNumber - count + i + 1) & NC_CMD_MASK;
			cmd = &nc.cmds[j];
			MSG_WriteDeltaUsercmdKey(&buf, key, oldcmd, cmd);
			oldcmd = cmd;
		}
	}
	packetNum = nc.chan.outgoingSequence & PACKET_MASK;
	nc.outPackets[packetNum].realtime = nc.realtime;
	nc.outPackets[packetNum].serverTime = oldcmd->serverTime;
	nc.outPackets[packetNum].cmdNumber = nc.cmdNumber;
	nc.lastPacketSentTime = nc.realtime;
	MSG_WriteByte(&buf, clc_EOF);
	Netchan_Transmit(&nc.chan, buf.cursize, buf.data);
	while (nc.chan.unsentFragments)   /* fragments of a long message, sent without delay */
		Netchan_TransmitNextFragment(&nc.chan);
	nc.packets_out++;
	nc.bytes_out += buf.cursize;
}

static int nc_ready_to_send(void) {
	int oldPacketNum, delta;
	/* without a gamestate yet, only one packet a second */
	if (nc.state != NC_ACTIVE && nc.state != NC_PRIMED && nc.realtime - nc.lastPacketSentTime < 1000)
		return 0;
	oldPacketNum = (nc.chan.outgoingSequence - 1) & PACKET_MASK;
	delta = nc.realtime - nc.outPackets[oldPacketNum].realtime;
	if (delta < 1000 / nc.maxpackets)
		return 0;   /* the accumulated commands go out in the next packet */
	return 1;
}

/* ---------------------------------------------------------------- parsing */

static void nc_delta_entity(msg_t *msg, nc_snapshot_t *frame, int newnum, entityState_t *old, qboolean unchanged) {
	entityState_t *state = &nc.parseEntities[nc.parseEntitiesNum & (NC_MAX_PARSE_ENTITIES - 1)];
	if (unchanged)
		*state = *old;
	else
		MSG_ReadDeltaEntity(msg, old, state, newnum);
	if (state->number == MAX_GENTITIES - 1)
		return;   /* removed */
	nc.parseEntitiesNum++;
	frame->numEntities++;
}

#define NC_NONE 99999

static void nc_next_old(nc_snapshot_t *oldframe, int *oldindex, int *oldnum, entityState_t **oldstate) {
	if (!oldframe || *oldindex >= oldframe->numEntities) {
		*oldnum = NC_NONE;
		return;
	}
	*oldstate = &nc.parseEntities[(oldframe->parseEntitiesNum + *oldindex) & (NC_MAX_PARSE_ENTITIES - 1)];
	*oldnum = (*oldstate)->number;
}

static void nc_parse_packet_entities(msg_t *msg, nc_snapshot_t *oldframe, nc_snapshot_t *newframe) {
	int newnum, oldindex = 0, oldnum;
	entityState_t *oldstate = NULL;

	newframe->parseEntitiesNum = nc.parseEntitiesNum;
	newframe->numEntities = 0;
	nc_next_old(oldframe, &oldindex, &oldnum, &oldstate);
	for (;;) {
		newnum = MSG_ReadBits(msg, GENTITYNUM_BITS);
		if (newnum == MAX_GENTITIES - 1)
			break;
		if (msg->readcount > msg->cursize)
			Com_Error(ERR_DROP, "nc: end of message in the entities");
		while (oldnum < newnum) {   /* unchanged from the old frame */
			nc_delta_entity(msg, newframe, oldnum, oldstate, qtrue);
			oldindex++;
			nc_next_old(oldframe, &oldindex, &oldnum, &oldstate);
		}
		if (oldnum == newnum) {   /* a delta from the old state */
			nc_delta_entity(msg, newframe, newnum, oldstate, qfalse);
			oldindex++;
			nc_next_old(oldframe, &oldindex, &oldnum, &oldstate);
			continue;
		}
		if (oldnum > newnum)   /* a delta from the baseline */
			nc_delta_entity(msg, newframe, newnum, &nc.baselines[newnum], qfalse);
	}
	while (oldnum != NC_NONE) {   /* the rest of the old frame is unchanged */
		nc_delta_entity(msg, newframe, oldnum, oldstate, qtrue);
		oldindex++;
		nc_next_old(oldframe, &oldindex, &oldnum, &oldstate);
	}
}

static void nc_parse_snapshot(msg_t *msg) {
	int len, deltaNum, oldMessageNum, i, packetNum;
	nc_snapshot_t *old = NULL;
	nc_snapshot_t newSnap;

	Com_Memset(&newSnap, 0, sizeof(newSnap));
	newSnap.serverTime = MSG_ReadLong(msg);
	newSnap.messageNum = nc.serverMessageSequence;
	deltaNum = MSG_ReadByte(msg);
	newSnap.deltaNum = deltaNum ? newSnap.messageNum - deltaNum : -1;
	newSnap.snapFlags = MSG_ReadByte(msg);

	if (newSnap.deltaNum <= 0) {
		newSnap.valid = qtrue;   /* uncompressed */
	} else {
		old = &nc.snapshots[newSnap.deltaNum & PACKET_MASK];
		if (!old->valid) {
			Com_DPrintf("nc: delta from an invalid frame\n");
		} else if (old->messageNum != newSnap.deltaNum) {
			Com_DPrintf("nc: delta frame too old\n");
		} else if (nc.parseEntitiesNum - old->parseEntitiesNum > NC_MAX_PARSE_ENTITIES - MAX_SNAPSHOT_ENTITIES) {
			Com_DPrintf("nc: delta parseEntitiesNum too old\n");
		} else {
			newSnap.valid = qtrue;
		}
	}
	len = MSG_ReadByte(msg);
	if (len > (int)sizeof(newSnap.areamask))
		Com_Error(ERR_DROP, "nc: areamask of %d bytes", len);
	MSG_ReadData(msg, &newSnap.areamask, len);
	MSG_ReadDeltaPlayerstate(msg, old ? &old->ps : NULL, &newSnap.ps);
	nc_parse_packet_entities(msg, old, &newSnap);
	if (!newSnap.valid)
		return;   /* read through, then dumped: the next client packet asks for a full one */

	/* frames between the last one and this one were dropped: they can't be delta sources */
	oldMessageNum = nc.snap.messageNum + 1;
	if (newSnap.messageNum - oldMessageNum >= PACKET_BACKUP)
		oldMessageNum = newSnap.messageNum - (PACKET_BACKUP - 1);
	for (; oldMessageNum < newSnap.messageNum; oldMessageNum++) {
		if (nc.received > 0)
			nc.dropped++;
		nc.snapshots[oldMessageNum & PACKET_MASK].valid = qfalse;
	}
	nc.snap = newSnap;
	nc.snap.ping = 999;
	for (i = 0; i < PACKET_BACKUP; i++) {
		packetNum = (nc.chan.outgoingSequence - 1 - i) & PACKET_MASK;
		if (nc.snap.ps.commandTime >= nc.outPackets[packetNum].serverTime) {
			nc.snap.ping = nc.realtime - nc.outPackets[packetNum].realtime;
			break;
		}
	}
	nc.snapshots[nc.snap.messageNum & PACKET_MASK] = nc.snap;
	nc.newSnapshots = qtrue;
	nc.received++;
}

/* The map's collision model, for a process that isn't running the match. */
static void nc_load_map(void) {
	const char *info = nc.cs[CS_SERVERINFO] ? nc.cs[CS_SERVERINFO] : "";
	const char *name = Info_ValueForKey(info, "mapname");
	int checksum;
	if (com_sv_running->integer || !*name) {
		Q_strncpyz(nc.mapname, name, sizeof(nc.mapname));
		return;
	}
	if (nc.map_loaded && !Q_stricmp(nc.mapname, name))
		return;
	Q_strncpyz(nc.mapname, name, sizeof(nc.mapname));
	nc.map_loaded = 0;
	/* a map this player doesn't have (the host's Maps folder): stay primed and say so instead of dropping;
	   the game downloads its pk3 from the host, then q3_nc_reload_map loads it */
	nc.map_missing = FS_ReadFile(va("maps/%s.bsp", name), NULL) <= 0;
	if (nc.map_missing)
		return;
	Hunk_Clear();
	CM_LoadMap(va("maps/%s.bsp", name), qtrue, &checksum);
	nc.map_loaded = 1;
}

static void nc_parse_gamestate(msg_t *msg) {
	int cmd;
	entityState_t nullstate;

	nc_clear_state();
	nc.serverCommandSequence = MSG_ReadLong(msg);
	for (;;) {
		cmd = MSG_ReadByte(msg);
		if (cmd == svc_EOF)
			break;
		if (cmd == svc_configstring) {
			int i = MSG_ReadShort(msg);
			const char *s;
			if (i < 0 || i >= MAX_CONFIGSTRINGS)
				Com_Error(ERR_DROP, "nc: configstring %d", i);
			s = MSG_ReadBigString(msg);
			nc_set_cs(i, s);
		} else if (cmd == svc_baseline) {
			int n = MSG_ReadBits(msg, GENTITYNUM_BITS);
			if (n < 0 || n >= MAX_GENTITIES)
				Com_Error(ERR_DROP, "nc: baseline %d", n);
			Com_Memset(&nullstate, 0, sizeof(nullstate));
			MSG_ReadDeltaEntity(msg, &nullstate, &nc.baselines[n], n);
		} else {
			Com_Error(ERR_DROP, "nc: bad gamestate command %d", cmd);
		}
	}
	nc.clientNum = MSG_ReadLong(msg);
	nc.checksumFeed = MSG_ReadLong(msg);
	nc_load_map();
	/* CL_DownloadsComplete -> CL_SendPureChecksums, then three packets so the server enters us in the world */
	{
		const char *sysinfo = nc.cs[CS_SYSTEMINFO] ? nc.cs[CS_SYSTEMINFO] : "";
		if (atoi(Info_ValueForKey(sysinfo, "sv_pure")))
			nc_add_reliable(va("cp %d %s", nc.serverId, FS_ReferencedPakPureChecksums()));
		else
			nc_add_reliable(va("cp %d", nc.serverId));
	}
	nc.state = NC_PRIMED;
}

static void nc_parse_command_string(msg_t *msg) {
	int seq = MSG_ReadLong(msg);
	const char *s = MSG_ReadString(msg);
	if (nc.serverCommandSequence >= seq)
		return;   /* already seen */
	nc.serverCommandSequence = seq;
	Q_strncpyz(nc.serverCommands[seq & (MAX_RELIABLE_COMMANDS - 1)], s, sizeof(nc.serverCommands[0]));
	nc_server_command(s);
}

static void nc_parse_server_message(msg_t *msg) {
	int cmd;
	MSG_Bitstream(msg);
	nc.reliableAcknowledge = MSG_ReadLong(msg);
	if (nc.reliableAcknowledge < nc.reliableSequence - MAX_RELIABLE_COMMANDS)
		nc.reliableAcknowledge = nc.reliableSequence;
	for (;;) {
		if (msg->readcount > msg->cursize)
			Com_Error(ERR_DROP, "nc: read past the end of a server message");
		cmd = MSG_ReadByte(msg);
		if (cmd == svc_EOF)
			break;
		switch (cmd) {
		case svc_nop:
			break;
		case svc_serverCommand:
			nc_parse_command_string(msg);
			break;
		case svc_gamestate:
			nc_parse_gamestate(msg);
			break;
		case svc_snapshot:
			nc_parse_snapshot(msg);
			break;
		case svc_download:
			Com_Error(ERR_DROP, "nc: the server tried to send a download");
			break;
		default:
			Com_Error(ERR_DROP, "nc: illegible server message %d", cmd);
		}
	}
}

static void nc_connectionless(netadr_t from, msg_t *msg) {
	const char *s, *c;
	int challenge;

	MSG_BeginReadingOOB(msg);
	MSG_ReadLong(msg);   /* the -1 */
	s = MSG_ReadStringLine(msg);
	Cmd_TokenizeString(s);
	c = Cmd_Argv(0);

	if (!Q_stricmp(c, "challengeResponse")) {
		if (nc.state != NC_CONNECTING)
			return;
		c = Cmd_Argv(2);
		challenge = *c ? atoi(c) : 0;
		if (!*c || challenge != nc.challenge)
			return;   /* not answering our challenge */
		nc.challenge = atoi(Cmd_Argv(1));
		nc.state = NC_CHALLENGING;
		nc.connectTime = nc.realtime - NC_RETRANSMIT_MS;   /* send the connect now */
		return;
	}
	if (!Q_stricmp(c, "connectResponse")) {
		if (nc.state != NC_CHALLENGING)
			return;
		c = Cmd_Argv(1);
		if (!*c || atoi(c) != nc.challenge)
			return;
		Netchan_Setup(NS_CLIENT, &nc.chan, from, nc.qport, nc.challenge, qfalse);
		nc.state = NC_CONNECTED;
		nc.lastPacketSentTime = nc.realtime - 5000;   /* first packet at once */
		nc.lastPacketTime = nc.realtime;
		return;
	}
	if (!Q_stricmp(c, "print")) {
		const char *text = MSG_ReadString(msg);
		if (nc.state == NC_CONNECTING || nc.state == NC_CHALLENGING) {
			char line[MAX_STRING_CHARS];
			int n;
			Q_strncpyz(line, text, sizeof(line));
			n = (int)strlen(line);
			while (n > 0 && (line[n - 1] == 10 || line[n - 1] == 13))
				line[--n] = 0;
			nc_fail(line[0] ? line : "The server refused the connection");
		} else {
			Q_strcat(nc_rcon_out, sizeof(nc_rcon_out), text);   /* a remote console's answer */
		}
		return;
	}
}

/* ---------------------------------------------------------------- the clock (cl_cgame.c) */

static void nc_adjust_time_delta(void) {
	int newDelta, deltaDelta;
	nc.newSnapshots = 0;
	newDelta = nc.snap.serverTime - nc.realtime;
	deltaDelta = abs(newDelta - nc.serverTimeDelta);
	if (deltaDelta > NC_RESET_TIME) {
		nc.serverTimeDelta = newDelta;
		nc.oldServerTime = nc.snap.serverTime;
		nc.serverTime = nc.snap.serverTime;
	} else if (deltaDelta > 100) {
		nc.serverTimeDelta = (nc.serverTimeDelta + newDelta) >> 1;   /* fast adjust: half the difference */
	} else if (nc.extrapolatedSnapshot) {
		nc.extrapolatedSnapshot = 0;
		nc.serverTimeDelta -= 2;
	} else {
		nc.serverTimeDelta++;   /* slow drift toward the least latency */
	}
}

static void nc_set_time(void) {
	if (nc.state == NC_PRIMED) {
		if (nc.newSnapshots) {
			nc.newSnapshots = 0;
			if (!(nc.snap.snapFlags & SNAPFLAG_NOT_ACTIVE)) {   /* CL_FirstSnapshot */
				nc.state = NC_ACTIVE;
				nc_active_at = nc.realtime;
				nc.serverTimeDelta = nc.snap.serverTime - nc.realtime;
				nc.oldServerTime = nc.snap.serverTime;
			}
		}
		if (nc.state != NC_ACTIVE)
			return;
	}
	if (nc.state != NC_ACTIVE || !nc.snap.valid)
		return;
	if (nc.snap.serverTime < nc.oldFrameServerTime) {
		/* the server's clock went back (a new map keeps counting, a restart doesn't): take the new one */
		nc.serverTimeDelta = nc.snap.serverTime - nc.realtime;
		nc.oldServerTime = nc.snap.serverTime;
	}
	nc.oldFrameServerTime = nc.snap.serverTime;
	nc.serverTime = nc.realtime + nc.serverTimeDelta;
	if (nc.serverTime < nc.oldServerTime)   /* time never flows backwards */
		nc.serverTime = nc.oldServerTime;
	nc.oldServerTime = nc.serverTime;
	/* CL_SetCGameTime: almost past the newest snapshot by this clock, so adjust back a bit when the next one comes.
	   The clock's own test, not the drawing's: Q3NetFeed draws `interp` ms behind it (cl_timeNudge), and the drawing's
	   test let the clock drift forward until that delay was gone (M8). */
	if (nc.realtime + nc.serverTimeDelta >= nc.snap.serverTime - 5)
		nc.extrapolatedSnapshot = 1;
	if (nc.newSnapshots)
		nc_adjust_time_delta();
}

/* ---------------------------------------------------------------- API */

static void nc_release(void) {
	nc_unload_map();
	nc_free_strings();
}

void q3_nc_reset(void) {
	nc_release();
	Com_Memset(&nc, 0, sizeof(nc));
}

int q3_nc_connect(const char *userinfo, int maxpackets) {
	nc_hold_entry = 0;
	GUARD(1, 1);
	nc_release();
	Com_Memset(&nc, 0, sizeof(nc));
	nc.server = nc_server_adr();
	nc.challenge = ((rand() << 16) ^ rand()) ^ Sys_Milliseconds();
	nc.qport = Cvar_VariableIntegerValue("net_qport");   /* the netchan writes this cvar's value into every packet */
	nc.maxpackets = maxpackets < 15 ? 15 : (maxpackets > 125 ? 125 : maxpackets);
	nc.timeout_ms = 30000;
	Q_strncpyz(nc.userinfo, userinfo, sizeof(nc.userinfo));
	nc_clear_state();
	nc.snap_message_predicted = -1;
	nc.connectTime = -99999;
	nc.connectStart = 0;
	nc.state = NC_CONNECTING;
	UNGUARD();
	return 0;
}

void q3_nc_disconnect(void) {
	if (nc.state >= NC_CONNECTED) {
		/* CL_Disconnect: a few "disconnect" commands in a packet, so one lost packet doesn't leave a ghost */
		int i;
		GUARD_V();
		nc_add_reliable("disconnect");
		for (i = 0; i < 3; i++)
			nc_write_packet();
		UNGUARD();
	}
	nc.state = NC_DISCONNECTED;
	nc_release();
}

int q3_nc_state(void) { return nc.state; }
const char *q3_nc_error(void) { return nc.error; }

void q3_nc_set_maxpackets(int maxpackets) {
	nc.maxpackets = maxpackets < 15 ? 15 : (maxpackets > 125 ? 125 : maxpackets);
}

void q3_nc_receive(const unsigned char *data, int len) {
	msg_t msg;
	static byte buf[MAX_MSGLEN];
	netadr_t from = nc_server_adr();
	if (nc.state == NC_DISCONNECTED || len <= 0 || len > MAX_MSGLEN)
		return;
	NC_GUARD_FAIL_V();
	MSG_Init(&msg, buf, sizeof(buf));
	Com_Memcpy(buf, data, len);
	msg.cursize = len;
	nc.lastPacketTime = nc.realtime;
	nc.packets_in++;
	nc.bytes_in += len;
	if (msg.cursize >= 4 && *(int *)msg.data == -1) {
		nc_connectionless(from, &msg);
	} else if (nc.state >= NC_CONNECTED && msg.cursize >= 4) {
		if (Netchan_Process(&nc.chan, &msg)) {
			nc.serverMessageSequence = LittleLong(*(int *)msg.data);
			nc_parse_server_message(&msg);
		}
	}
	UNGUARD();
}

int q3_nc_frame(int msec, int extrapolated) {
	if (nc.state == NC_DISCONNECTED)
		return nc.state;
	nc.realtime += msec > 0 ? msec : 0;
	(void)extrapolated;   /* the clock judges that itself (nc_set_time) */
	{
		NC_GUARD_FAIL(nc.state);
		if (nc.state == NC_CONNECTING || nc.state == NC_CHALLENGING) {
			/* CL_CheckForResend */
			if (nc.realtime - nc.connectStart > NC_CONNECT_GIVE_UP_MS) {
				nc_fail("The server didn't answer");
			} else if (nc.realtime - nc.connectTime >= NC_RETRANSMIT_MS) {
				nc.connectTime = nc.realtime;
				if (nc.state == NC_CONNECTING) {
					nc_send_oob(va("getchallenge %d %s", nc.challenge, com_gamename->string));
				} else {
					char info[MAX_INFO_STRING], data[MAX_INFO_STRING + 10];
					Q_strncpyz(info, nc.userinfo, sizeof(info));
					Info_SetValueForKey(info, "protocol", va("%i", com_protocol->integer));
					Info_SetValueForKey(info, "qport", va("%i", nc.qport));
					Info_SetValueForKey(info, "challenge", va("%i", nc.challenge));
					Com_sprintf(data, sizeof(data), "connect \"%s\"", info);
					NET_OutOfBandData(NS_CLIENT, nc.server, (byte *)data, (int)strlen(data));
				}
			}
		} else {
			/* CL_CheckTimeout */
			if (nc.realtime - nc.lastPacketTime > nc.timeout_ms) {
				nc_fail("Server connection timed out");
			} else {
				/* CL_CreateNewCommands for a client that has no input yet: the server needs a usercmd to
				   enter it in the world. Every frame until it does, as Quake 3 makes one every frame from
				   CA_PRIMED: the server sends a primed client snapshots too (SNAPFLAG_NOT_ACTIVE), and
				   stopping at the first of them left a client whose commands hadn't arrived yet (a resent
				   gamestate, a lost packet) primed forever (M8, tests/net_lag_test.tscn). */
				if (nc.state == NC_PRIMED && !nc_hold_entry) {
					usercmd_t *cmd;
					nc.cmdNumber++;
					cmd = &nc.cmds[nc.cmdNumber & NC_CMD_MASK];
					Com_Memset(cmd, 0, sizeof(*cmd));
					cmd->serverTime = nc.serverTime;
				}
				nc_set_time();
				if (nc_ready_to_send())
					nc_write_packet();
			}
		}
		UNGUARD();
	}
	return nc.state;
}

/* CL_InitCGame comes before entering the world in Quake 3: while held, a primed client sends no usercmd, so the
   server doesn't spawn the player until the game has loaded the map. Entered during a load, the player stood still
   on the server through it, and the first command after spanned the whole load: a jump of hundreds of units (M8). */
void q3_nc_hold_entry(int hold) { nc_hold_entry = hold; }

/* The server's map when this process couldn't find it (nc_load_map), else "". */
const char *q3_nc_missing_map(void) { return nc.map_missing ? nc.mapname : ""; }

/* After the game saved the missing map's pk3: the file system looks at the paks again (FS_Restart, as
   Quake 3's client does once its downloads are done) and the map loads. 0 on success. */
int q3_nc_reload_map(void) {
	char name[MAX_QPATH];
	NC_GUARD_FAIL(1);
	if (nc.map_missing) {
		Q_strncpyz(name, nc.mapname, sizeof(name));
		FS_Restart(nc.checksumFeed);
		nc.map_missing = 0;
		nc.mapname[0] = 0;
		nc_load_map();
		if (nc.map_missing || Q_stricmp(nc.mapname, name)) {
			q3_fatal_armed = 0;
			return 1;
		}
	}
	q3_fatal_armed = 0;
	return 0;
}

/* A remote console command (the server's rconpassword): an out-of-band packet, answered by print packets. */
void q3_nc_rcon(const char *password, const char *command) {
	if (nc.state == NC_DISCONNECTED)
		return;
	GUARD_V();
	nc_rcon_out[0] = 0;
	NET_OutOfBandPrint(NS_CLIENT, nc.server, "rcon %s %s", password, command);
	UNGUARD();
}

/* What came back since the last call (and clears it). */
const char *q3_nc_rcon_output(void) {
	static char out[sizeof(nc_rcon_out)];
	Q_strncpyz(out, nc_rcon_out, sizeof(out));
	nc_rcon_out[0] = 0;
	return out;
}

int q3_nc_time(void) { return nc.serverTime; }
int q3_nc_client_num(void) { return nc.clientNum; }
int q3_nc_server_id(void) { return nc.serverId; }

static void nc_to_usercmd(const q3_usercmd *in, usercmd_t *cmd) {
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

void q3_nc_usercmd(const q3_usercmd *in) {
	if (nc.state < NC_PRIMED)
		return;
	nc.cmdNumber++;
	nc_to_usercmd(in, &nc.cmds[nc.cmdNumber & NC_CMD_MASK]);
}

void q3_nc_command(const char *text) {
	if (nc.state >= NC_CONNECTED)
		nc_add_reliable(text);
}

const char *q3_nc_next_command(void) {
	if (nc.queue_read >= nc.queue_write)
		return NULL;
	return nc.queue[nc.queue_read++ % NC_COMMAND_QUEUE];
}

const char *q3_nc_configstring(int index) {
	if (index < 0 || index >= MAX_CONFIGSTRINGS || !nc.cs[index])
		return "";
	return nc.cs[index];
}

static void nc_extract(const nc_snapshot_t *s, q3_snapshot *out) {
	int i, n = s->numEntities;
	out->serverTime = s->serverTime;
	Com_Memcpy(&out->ps, &s->ps, sizeof(playerState_t));
	if (n > Q3_MAX_GENTITIES)
		n = Q3_MAX_GENTITIES;
	out->num_entities = n;
	for (i = 0; i < n; i++)
		Com_Memcpy(&out->entities[i], &nc.parseEntities[(s->parseEntitiesNum + i) & (NC_MAX_PARSE_ENTITIES - 1)], sizeof(entityState_t));
}

/* CG_ProcessSnapshots' pair: the newest snapshot at or before `time` and the one after it, found
 * in the ring by message number. Returns 2 with both, 1 with only the first (the caller extrapolates,
 * and the next frame says so), 0 with none yet. */
int q3_nc_snapshot_pair(int time, q3_snapshot *a, q3_snapshot *b, q3_nc_snap_info *info) {
	int m, newest = nc.snap.messageNum, found = -1, next = -1, i;
	if (nc.state < NC_PRIMED || !nc.snap.valid)
		return 0;
	for (i = 0, m = newest; i < PACKET_BACKUP; i++, m--) {
		const nc_snapshot_t *s = &nc.snapshots[m & PACKET_MASK];
		if (!s->valid || s->messageNum != m)
			continue;
		if (s->serverTime <= time) {
			found = m;
			break;
		}
		next = m;
	}
	if (found < 0)   /* we're behind the oldest we kept: show it anyway */
		found = next >= 0 ? next : newest;
	if (a)
		nc_extract(&nc.snapshots[found & PACKET_MASK], a);
	if (info) {
		info->message = found;
		info->snap_flags = nc.snapshots[found & PACKET_MASK].snapFlags;
		info->ping = nc.snap.ping;
		info->newest_message = newest;
		info->newest_time = nc.snap.serverTime;
	}
	if (next >= 0 && next != found) {
		if (b)
			nc_extract(&nc.snapshots[next & PACKET_MASK], b);
		if (info)
			info->next_message = next;
		return 2;
	}
	if (info)
		info->next_message = -1;
	return 1;
}

void q3_nc_get_stats(q3_nc_stats *out) {
	out->state = nc.state;
	out->ping = nc.snap.ping;
	out->packets_in = nc.packets_in;
	out->packets_out = nc.packets_out;
	out->bytes_in = nc.bytes_in;
	out->bytes_out = nc.bytes_out;
	out->dropped = nc.dropped;
	out->snapshots = nc.received;
	out->server_time = nc.serverTime;
	out->time_delta = nc.serverTimeDelta;
}

/* ---------------------------------------------------------------- collision and prediction */

static int nc_entity_clip(const entityState_t *s, int at, clipHandle_t *model, vec3_t origin, vec3_t angles) {
	if (!s->solid || s->eType == ET_ITEM || s->eType == ET_PUSH_TRIGGER || s->eType == ET_TELEPORT_TRIGGER)
		return 0;
	if (s->solid == SOLID_BMODEL) {
		if (s->modelindex <= 0 || s->modelindex >= CM_NumInlineModels())
			return 0;
		*model = CM_InlineModel(s->modelindex);
		BG_EvaluateTrajectory(&s->apos, at, angles);
		BG_EvaluateTrajectory(&s->pos, at, origin);
	} else {
		vec3_t mins, maxs;
		int x = s->solid & 255, zd = (s->solid >> 8) & 255, zu = ((s->solid >> 16) & 255) - 32;
		mins[0] = mins[1] = -x;
		maxs[0] = maxs[1] = x;
		mins[2] = -zd;
		maxs[2] = zu;
		*model = CM_TempBoxModel(mins, maxs, qfalse);
		VectorClear(angles);
		VectorCopy(s->pos.trBase, origin);
	}
	return 1;
}

static const nc_snapshot_t *nc_trace_snapshot;

/* CG_Trace: the world, then every solid entity of the snapshot being predicted from. */
static void nc_pm_trace(trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs,
		const vec3_t end, int passEntityNum, int contentMask) {
	trace_t t;
	vec3_t origin, angles;
	clipHandle_t model;
	int i;
	const nc_snapshot_t *snap = nc_trace_snapshot;
	CM_BoxTrace(results, start, end, mins, maxs, 0, contentMask, qfalse);
	results->entityNum = results->fraction != 1.0f ? ENTITYNUM_WORLD : ENTITYNUM_NONE;
	for (i = 0; snap && i < snap->numEntities; i++) {
		const entityState_t *es = &nc.parseEntities[(snap->parseEntitiesNum + i) & (NC_MAX_PARSE_ENTITIES - 1)];
		if (es->number == passEntityNum || !nc_entity_clip(es, snap->serverTime, &model, origin, angles))
			continue;
		CM_TransformedBoxTrace(&t, start, end, mins, maxs, model, contentMask, origin, angles, qfalse);
		if (t.allsolid || t.fraction < results->fraction) {
			t.entityNum = es->number;
			*results = t;
		} else if (t.startsolid) {
			results->startsolid = qtrue;
		}
	}
}

static int nc_pm_pointcontents(const vec3_t point, int passEntityNum) {
	const nc_snapshot_t *snap = nc_trace_snapshot;
	vec3_t origin, angles;
	clipHandle_t model;
	int i, c = CM_PointContents(point, 0);
	for (i = 0; snap && i < snap->numEntities; i++) {
		const entityState_t *es = &nc.parseEntities[(snap->parseEntitiesNum + i) & (NC_MAX_PARSE_ENTITIES - 1)];
		if (es->number == passEntityNum || es->solid != SOLID_BMODEL)
			continue;
		if (nc_entity_clip(es, snap->serverTime, &model, origin, angles))
			c |= CM_TransformedPointContents(point, model, origin, angles);
	}
	return c;
}

/* The rules and server cvars Pmove reads come from the server's own configstrings (serverinfo and
 * systeminfo), the way the cgame learns them; our own cvars answer only for what the server didn't send. */
static const char *nc_cvar(const char *name) {
	static char value[BIG_INFO_VALUE];
	int cs;
	for (cs = CS_SYSTEMINFO; cs >= CS_SERVERINFO; cs--) {
		const char *info = nc.cs[cs];
		if (info && *Info_ValueForKey(info, name)) {
			Q_strncpyz(value, Info_ValueForKey(info, name), sizeof(value));
			return value;
		}
	}
	return Cvar_VariableString(name);
}

/* CG_AdjustPositionForMover: where a mover carried what stands on it between two times. */
static void nc_adjust_for_mover(const vec3_t in, int moverNum, int fromTime, int toTime, vec3_t out, const nc_snapshot_t *snap) {
	int i;
	VectorCopy(in, out);
	if (moverNum <= 0 || moverNum >= ENTITYNUM_MAX_NORMAL)
		return;
	for (i = 0; snap && i < snap->numEntities; i++) {
		const entityState_t *es = &nc.parseEntities[(snap->parseEntitiesNum + i) & (NC_MAX_PARSE_ENTITIES - 1)];
		vec3_t oldOrigin, origin, deltaOrigin, oldAngles, angles, deltaAngles;
		if (es->number != moverNum) {
			continue;
		}
		if (es->eType != ET_MOVER)
			return;
		BG_EvaluateTrajectory(&es->pos, fromTime, oldOrigin);
		BG_EvaluateTrajectory(&es->apos, fromTime, oldAngles);
		BG_EvaluateTrajectory(&es->pos, toTime, origin);
		BG_EvaluateTrajectory(&es->apos, toTime, angles);
		VectorSubtract(origin, oldOrigin, deltaOrigin);
		VectorSubtract(angles, oldAngles, deltaAngles);
		VectorAdd(in, deltaOrigin, out);
		return;   /* the yaw a turning mover gives is added by the view; the position is what matters */
	}
}

/* CG_TouchTriggerPrediction: jump pads under the predicted player act at once. */
static void nc_touch_triggers(playerState_t *ps, const nc_snapshot_t *snap, const vec3_t mins, const vec3_t maxs) {
	int i;
	if (ps->pm_type == PM_SPECTATOR)
		return;
	for (i = 0; snap && i < snap->numEntities; i++) {
		const entityState_t *es = &nc.parseEntities[(snap->parseEntitiesNum + i) & (NC_MAX_PARSE_ENTITIES - 1)];
		clipHandle_t model;
		trace_t trace;
		if (es->eType != ET_PUSH_TRIGGER && es->eType != ET_TELEPORT_TRIGGER)
			continue;
		if (es->solid == SOLID_BMODEL) {
			if (es->modelindex <= 0 || es->modelindex >= CM_NumInlineModels())
				continue;
			model = CM_InlineModel(es->modelindex);
		} else {
			vec3_t bmins, bmaxs;
			int x = es->solid & 255, zd = (es->solid >> 8) & 255, zu = ((es->solid >> 16) & 255) - 32;
			bmins[0] = bmins[1] = -x;
			bmaxs[0] = bmaxs[1] = x;
			bmins[2] = -zd;
			bmaxs[2] = zu;
			model = CM_TempBoxModel(bmins, bmaxs, qfalse);
		}
		CM_BoxTrace(&trace, ps->origin, ps->origin, mins, maxs, model, -1, qfalse);
		if (!trace.startsolid)
			continue;
		if (es->eType == ET_TELEPORT_TRIGGER)
			nc.hyperspace = 1;
		else
			BG_TouchJumpPad(ps, (entityState_t *)es);
	}
}

/* CG_PredictPlayerState: the newest snapshot's playerState with Pmove run over the usercmds the
 * server hasn't acknowledged. error is the correction a misprediction leaves (the view decays it
 * over 100 ms from error_time, a server time). *teleported says a teleporter or a server-side
 * teleport reset the prediction. Returns 0 until there's a snapshot (or a collision model). */
int q3_nc_predict(q3_player_state *out, float *error, int *error_time, int *teleported) {
	static playerState_t ps, old_ps;
	static pmove_t pm;
	int cmdNum, current, msec, moved = 0, have_old, compared = 0, teleport = 0;
	usercmd_t latest;
	const nc_snapshot_t *snap = &nc.snap;

	if (teleported)
		*teleported = 0;
	VectorClear(error);
	if (nc.state != NC_ACTIVE || !snap->valid || (!nc.map_loaded && !com_sv_running->integer))
		return 0;
	{
		GUARD(0, 0);
		nc_trace_snapshot = snap;
		old_ps = nc.predicted;
		have_old = nc.predicted_valid;
		ps = snap->ps;
		nc.hyperspace = 0;
		/* the server moved us (a respawn, a teleporter): no error to decay */
		if (have_old && ((old_ps.eFlags ^ snap->ps.eFlags) & EF_TELEPORT_BIT))
			teleport = 1;
		/* just in the world: the load's stall still settling (the first command can span half a second) snaps */
		if (nc.realtime - nc_active_at < NC_SETTLE_MS)
			teleport = 1;

		Com_Memset(&pm, 0, sizeof(pm));
		pm.ps = &ps;
		pm.trace = nc_pm_trace;
		pm.pointcontents = nc_pm_pointcontents;
		pm.tracemask = ps.pm_type == PM_DEAD ? MASK_PLAYERSOLID & ~CONTENTS_BODY : MASK_PLAYERSOLID;
		if (ps.persistant[PERS_TEAM] == TEAM_SPECTATOR)
			pm.tracemask &= ~CONTENTS_BODY;
		pm.noFootsteps = (atoi(nc_cvar("dmflags")) & DF_NO_FOOTSTEPS) > 0;
		pm.pmove_fixed = atoi(nc_cvar("pmove_fixed"));
		msec = atoi(nc_cvar("pmove_msec"));
		pm.pmove_msec = msec < 8 ? 8 : (msec > 33 ? 33 : msec);
		BG_QLRules(&nc_rules, nc_cvar);
		bg_autoHop = nc_rules.ql && atoi(nc_cvar("pmove_AutoHop")[0] ? nc_cvar("pmove_AutoHop") : "1");   /* patch 0008 */
		pm.rules = &nc_rules;

		if (ps.pm_flags & PMF_FOLLOW) {   /* watching someone else: the snapshot's state is theirs, not ours to predict */
			nc.predicted = ps;
			nc.predicted_valid = 0;
			Com_Memcpy(out, &ps, sizeof(ps));
			UNGUARD();
			return 1;
		}

		current = nc.cmdNumber;
		latest = nc.cmds[current & NC_CMD_MASK];
		for (cmdNum = current - NC_CMD_BACKUP + 1; cmdNum <= current; cmdNum++) {
			if (cmdNum < 1)
				continue;
			pm.cmd = nc.cmds[cmdNum & NC_CMD_MASK];
			if (pm.pmove_fixed)
				PM_UpdateViewAngles(pm.ps, &pm.cmd);
			if (pm.cmd.serverTime <= ps.commandTime)
				continue;   /* before the snapshot's player time */
			if (pm.cmd.serverTime > latest.serverTime)
				continue;   /* from before a map_restart */
			/* last frame's prediction ended at this command time: where the new one is now, compared */
			if (have_old && !compared && ps.commandTime == old_ps.commandTime) {
				compared = 1;
				if (teleport) {
					VectorClear(nc.predicted_error);
				} else {
					vec3_t adjusted, delta;
					float len, f;
					nc_adjust_for_mover(ps.origin, ps.groundEntityNum, snap->serverTime, nc.oldServerTime, adjusted, snap);
					VectorSubtract(old_ps.origin, adjusted, delta);
					len = VectorLength(delta);
					if (len > 0.1f) {
						f = 1.0f - (float)(nc.serverTime - nc.predicted_error_time) / 100.0f;   /* decay what was left */
						if (f > 0)
							VectorScale(nc.predicted_error, f, nc.predicted_error);
						else
							VectorClear(nc.predicted_error);
						VectorAdd(delta, nc.predicted_error, nc.predicted_error);
						nc.predicted_error_time = nc.oldServerTime;
					}
				}
			}
			pm.gauntletHit = qfalse;
			if (pm.pmove_fixed)
				pm.cmd.serverTime = ((pm.cmd.serverTime + pm.pmove_msec - 1) / pm.pmove_msec) * pm.pmove_msec;
			Pmove(&pm);
			moved = 1;
			nc_touch_triggers(&ps, snap, pm.mins, pm.maxs);
		}
		if (teleport && teleported)
			*teleported = 1;
		if (nc.hyperspace && teleported)
			*teleported = 1;
		(void)moved;
		VectorCopy(nc.predicted_error, error);
		if (error_time)
			*error_time = nc.predicted_error_time;
		nc.predicted = ps;
		nc.predicted_valid = 1;
		UNGUARD();
	}
	Com_Memcpy(out, &ps, sizeof(ps));
	return 1;
}

/* CG_Trace / CG_PointContents for the effects (lightning, pellets): the world and the newest snapshot's solids. */
int q3_nc_trace(const float *start, const float *end, const float *mins, const float *maxs,
		int pass_entity, int content_mask, q3_trace_result *out) {
	trace_t tr;
	vec3_t s, e, mn, mx;
	if (nc.state != NC_ACTIVE || (!nc.map_loaded && !com_sv_running->integer))
		return 0;
	VectorCopy(start, s);
	VectorCopy(end, e);
	VectorCopy(mins, mn);
	VectorCopy(maxs, mx);
	{
		GUARD(0, 0);
		nc_trace_snapshot = &nc.snap;
		nc_pm_trace(&tr, s, mn, mx, e, pass_entity, content_mask);
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

int q3_nc_point_contents(const float *point, int pass_entity) {
	vec3_t p;
	int c;
	if (nc.state != NC_ACTIVE || (!nc.map_loaded && !com_sv_running->integer))
		return 0;
	VectorCopy(point, p);
	{
		GUARD(0, 0);
		nc_trace_snapshot = &nc.snap;
		c = nc_pm_pointcontents(p, pass_entity);
		UNGUARD();
	}
	return c;
}

/* The server's own snapshot for this client (client_t's frames[] ring), for the loopback check that
 * what the client decoded is what the server built. `message` is the netchan sequence it went out
 * as. The states come back as the network carries them: through msg.c's field widths (eType is 8
 * bits, damagePitch 8 ...), so a comparison with what the client decoded is exact. */
int q3_nc_server_frame(int client, int message, q3_snapshot *out) {
	client_t *cl;
	clientSnapshot_t *frame;
	static byte buf[MAX_MSGLEN];
	msg_t msg;
	playerState_t nullps, ps;
	entityState_t nulles, es;
	int i;
	if (!q3_host_server_running() || client < 0 || client >= sv_maxclients->integer)
		return 0;
	cl = &svs.clients[client];
	if (message >= cl->netchan.outgoingSequence || cl->netchan.outgoingSequence - message >= PACKET_BACKUP)
		return 0;
	frame = &cl->frames[message & PACKET_MASK];
	{
		GUARD(0, 0);
		Com_Memset(&nullps, 0, sizeof(nullps));
		Com_Memset(&nulles, 0, sizeof(nulles));
		MSG_Init(&msg, buf, sizeof(buf));
		MSG_WriteDeltaPlayerstate(&msg, &nullps, &frame->ps);
		MSG_BeginReading(&msg);
		MSG_ReadDeltaPlayerstate(&msg, &nullps, &ps);
		Com_Memcpy(&out->ps, &ps, sizeof(ps));
		out->serverTime = 0;
		out->num_entities = frame->num_entities;
		for (i = 0; i < frame->num_entities && i < Q3_MAX_GENTITIES; i++) {
			const entityState_t *src = &svs.snapshotEntities[(frame->first_entity + i) % svs.numSnapshotEntities];
			MSG_Init(&msg, buf, sizeof(buf));
			MSG_WriteDeltaEntity(&msg, &nulles, (entityState_t *)src, qtrue);
			MSG_BeginReading(&msg);
			MSG_ReadBits(&msg, GENTITYNUM_BITS);   /* the entity number leads; the caller of MSG_ReadDeltaEntity reads it */
			MSG_ReadDeltaEntity(&msg, &nulles, &es, src->number);
			Com_Memcpy(&out->entities[i], &es, sizeof(es));
		}
		UNGUARD();
	}
	return 1;
}
