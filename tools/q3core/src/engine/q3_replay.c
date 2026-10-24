/* Replays (REPLAY_PLAN.md): the whole match recorded on the server, and played back without one.
 *
 * Recording: after every server frame the host calls q3_record_after_frame(). It gathers each
 * client's new reliable commands, and at the recording rate (40 Hz) writes one frame: every
 * linked entity that isn't SVF_NOCLIENT (players, missiles, items, movers, temp events), every
 * active client's playerState (bots too), the configstrings that changed and the commands. Frames
 * are deltas against the frame before, written with msg.c's MSG_WriteDeltaEntity /
 * MSG_WriteDeltaPlayerstate over its Huffman coder, exactly what a network client receives; every
 * 5 s a keyframe deltas from nothing, so playback can seek. The first frame is a keyframe holding
 * every configstring (the gamestate).
 *
 * The bytes go to Godot (q3_record_data / q3_record_consume), which writes the file around them
 * (godot/scripts/replay/q3_replay_recorder.gd: a JSON header first, a JSON trailer last). Each
 * frame is a block: u32 payload length, u8 kind (0 delta, 1 keyframe), i32 server time, then
 * the payload. The length prefix makes the file its own index: opening it hops from block to block.
 *
 * Payload (msg.c bit stream):
 *   short n, then n x (short index, bigstring text)          configstrings that changed
 *   short n, then n x (long mask0, long mask1, bigstring)    reliable commands, with the clients they went to
 *   long mask0, long mask1, then a playerState delta per client in the masks (from the last frame's, or zero)
 *   entity deltas as SV_EmitPacketEntities writes them (from the last frame's, or zero), ended by MAX_GENTITIES-1
 *   short n, then n x (short entity, byte kind, byte client)  SVF_SINGLECLIENT (1) / SVF_NOTSINGLECLIENT (2)
 *   long FRAME_END
 *
 * Playback (q3_replay_*) decodes the same frames into the structs Q3Server hands Godot, keeps the
 * frame shown and the one after it (for interpolation), and traces against the map's collision
 * model plus the frame's solid entities (cgame's CG_Trace) without a server running. */
#include "../../thirdparty/ioq3/code/server/server.h"
#include "../q3_api.h"
#include "q3_engine.h"

#define KEYFRAME_MSEC 5000
#define BLOCK_HEADER 9
#define FRAME_END 0x51335245          /* "Q3RE" */
#define MSG_BYTES (1 << 20)
#define MAX_PENDING 512
#define FILTER_SINGLE 1
#define FILTER_NOTSINGLE 2

typedef struct {
	int time;
	byte present[MAX_GENTITIES];
	entityState_t ents[MAX_GENTITIES];
	byte filter[MAX_GENTITIES];
	byte filter_client[MAX_GENTITIES];
	byte ps_present[MAX_CLIENTS];
	playerState_t ps[MAX_CLIENTS];
} frame_state_t;

static byte msg_buf[MSG_BYTES];
static const playerState_t null_ps;

static void put_u32(byte *p, unsigned v) {
	p[0] = v & 255; p[1] = (v >> 8) & 255; p[2] = (v >> 16) & 255; p[3] = (v >> 24) & 255;
}

static unsigned get_u32(const byte *p) {
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

static char *copy_string(const char *s) {
	size_t n = strlen(s) + 1;
	char *c = malloc(n);
	if (c)
		memcpy(c, s, n);
	return c;
}

/* Text as its bytes: msg.c's string writers turn '%' and every byte over 127 into '.', and chat
 * has to come back as it was said. */
static void write_raw(msg_t *msg, const char *s) {
	int n = (int)strlen(s);
	if (n > BIG_INFO_STRING - 1)
		n = BIG_INFO_STRING - 1;
	MSG_WriteShort(msg, n);
	MSG_WriteData(msg, s, n);
}

/* NULL on bad data. */
static const char *read_raw(msg_t *msg) {
	static char buf[BIG_INFO_STRING];
	int i, n = MSG_ReadShort(msg);
	if (n < 0 || n >= (int)sizeof(buf))
		return NULL;
	for (i = 0; i < n; i++)
		buf[i] = (char)MSG_ReadByte(msg);
	buf[n] = 0;
	return msg->readcount > msg->cursize ? NULL : buf;
}

/* ---------------------------------------------------------------- the recorder */

typedef struct {
	unsigned mask[2];
	char text[MAX_STRING_CHARS];
} pending_cmd_t;

/* A growing byte buffer of frame blocks. */
typedef struct {
	byte *data;
	int len, cap;
} out_t;

static struct {
	int active;
	int interval;
	int next_time;
	int last_key;
	int frames;
	char map[MAX_QPATH];
	char *cs[MAX_CONFIGSTRINGS];
	int cmd_seq[MAX_CLIENTS];
	pending_cmd_t pending[MAX_PENDING];
	int npending;
	int last_pending[MAX_CLIENTS];   /* each client's newest pending entry: later ones merge only after it */
	out_t out;
} rec;
static frame_state_t rec_last, rec_cur;

/* Commands the recording leaves out: configstrings travel on their own, "scores" answers one
 * client's Tab, and "disconnect" is the network's. */
static int skip_command(const char *t) {
	return !Q_strncmp(t, "cs ", 3) || !Q_strncmp(t, "bcs", 3) || !Q_strncmp(t, "scores ", 7) || !Q_strncmp(t, "disconnect", 10);
}

/* A command that reached `client`. One broadcast to many clients is kept once with all of them in
 * its mask; merging only into entries after the client's last one keeps each client's order. */
static void add_pending(int client, const char *text) {
	int i;
	for (i = rec.last_pending[client] + 1; i < rec.npending; i++) {
		pending_cmd_t *p = &rec.pending[i];
		if (!(p->mask[client >> 5] & (1u << (client & 31))) && !strcmp(p->text, text)) {
			p->mask[client >> 5] |= 1u << (client & 31);
			rec.last_pending[client] = i;
			return;
		}
	}
	if (rec.npending >= MAX_PENDING)
		return;
	rec.pending[rec.npending].mask[0] = rec.pending[rec.npending].mask[1] = 0;
	rec.pending[rec.npending].mask[client >> 5] = 1u << (client & 31);
	Q_strncpyz(rec.pending[rec.npending].text, text, MAX_STRING_CHARS);
	rec.last_pending[client] = rec.npending;
	rec.npending++;
}

static void clear_pending(void) {
	int i;
	rec.npending = 0;
	for (i = 0; i < MAX_CLIENTS; i++)
		rec.last_pending[i] = -1;
}

/* Every client's reliable commands since the last server frame, read from client_t's ring the way
 * the network would send them (64 deep; this runs every server frame, so none are overwritten). */
static void harvest_commands(void) {
	int i;
	for (i = 0; i < sv_maxclients->integer && i < MAX_CLIENTS; i++) {
		client_t *cl = &svs.clients[i];
		if (cl->state < CS_CONNECTED) {
			rec.cmd_seq[i] = 0;   /* a new connection counts from 0 */
			continue;
		}
		if (cl->reliableSequence < rec.cmd_seq[i])
			rec.cmd_seq[i] = 0;   /* the slot was freed and taken again within one frame */
		if (cl->state < CS_PRIMED)
			continue;            /* nothing is sent before the gamestate */
		if (cl->reliableSequence - rec.cmd_seq[i] > MAX_RELIABLE_COMMANDS)
			rec.cmd_seq[i] = cl->reliableSequence - MAX_RELIABLE_COMMANDS;
		while (rec.cmd_seq[i] < cl->reliableSequence) {
			const char *t;
			rec.cmd_seq[i]++;
			t = cl->reliableCommands[rec.cmd_seq[i] & (MAX_RELIABLE_COMMANDS - 1)];
			if (!skip_command(t))
				add_pending(i, t);
		}
	}
}

/* What a snapshot could carry for anyone: linked, not SVF_NOCLIENT (SV_AddEntitiesVisibleFromPoint
 * without the PVS). */
static void gather_entities(frame_state_t *f) {
	int e;
	Com_Memset(f->present, 0, sizeof(f->present));
	Com_Memset(f->filter, 0, sizeof(f->filter));
	if (!sv.gentities)
		return;
	for (e = 0; e < sv.num_entities && e < MAX_GENTITIES - 1; e++) {
		sharedEntity_t *ent = SV_GentityNum(e);
		if (!ent->r.linked || (ent->r.svFlags & SVF_NOCLIENT))
			continue;
		f->present[e] = 1;
		f->ents[e] = ent->s;
		f->ents[e].number = e;
		if (ent->r.svFlags & SVF_SINGLECLIENT) {
			f->filter[e] = FILTER_SINGLE;
			f->filter_client[e] = (byte)ent->r.singleClient;
		} else if (ent->r.svFlags & SVF_NOTSINGLECLIENT) {
			f->filter[e] = FILTER_NOTSINGLE;
			f->filter_client[e] = (byte)ent->r.singleClient;
		}
	}
}

static void gather_players(frame_state_t *f) {
	int i;
	Com_Memset(f->ps_present, 0, sizeof(f->ps_present));
	for (i = 0; i < sv_maxclients->integer && i < MAX_CLIENTS; i++) {
		if (svs.clients[i].state != CS_ACTIVE)
			continue;
		f->ps_present[i] = 1;
		f->ps[i] = *SV_GameClientNum(i);
	}
}

static void write_masks(msg_t *msg, const byte *present) {
	unsigned m[2] = { 0, 0 };
	int i;
	for (i = 0; i < MAX_CLIENTS; i++) {
		if (present[i])
			m[i >> 5] |= 1u << (i & 31);
	}
	MSG_WriteLong(msg, (int)m[0]);
	MSG_WriteLong(msg, (int)m[1]);
}

/* Room for `len` more bytes; 0 if memory ran out. */
static int out_reserve(out_t *o, int len) {
	if (o->len + len > o->cap) {
		int cap = o->cap ? o->cap : (1 << 16);
		byte *n;
		while (cap < o->len + len)
			cap *= 2;
		n = realloc(o->data, cap);
		if (!n)
			return 0;
		o->data = n;
		o->cap = cap;
	}
	return 1;
}

static void out_append(out_t *o, const byte *data, int len) {
	memcpy(o->data + o->len, data, len);
	o->len += len;
}

/* One frame as a block (see the top of the file): the configstrings in `cs_now` that differ from
 * `cs_was` (brought up to date here), the commands, then `cur`'s playerStates and entities as
 * deltas from `last` (from nothing for a keyframe). The recorder and the .dm_68 converter both
 * write through here. 0 if the frame didn't fit or memory ran out. */
static int encode_frame(out_t *o, const frame_state_t *cur, const frame_state_t *last, int key,
		char **cs_now, char **cs_was, const pending_cmd_t *cmds, int ncmds) {
	msg_t msg;
	int i, e, n;
	static short changed[MAX_CONFIGSTRINGS];
	byte header[BLOCK_HEADER];
	entityState_t null_es;

	MSG_Init(&msg, msg_buf, sizeof(msg_buf));
	n = 0;
	for (i = 0; i < MAX_CONFIGSTRINGS; i++) {
		const char *now = cs_now[i] ? cs_now[i] : "";
		const char *was = cs_was[i] ? cs_was[i] : "";
		if (strcmp(now, was))
			changed[n++] = (short)i;
	}
	MSG_WriteShort(&msg, n);
	for (i = 0; i < n; i++) {
		int idx = changed[i];
		const char *now = cs_now[idx] ? cs_now[idx] : "";
		MSG_WriteShort(&msg, idx);
		write_raw(&msg, now);
		free(cs_was[idx]);
		cs_was[idx] = now[0] ? copy_string(now) : NULL;
	}

	MSG_WriteShort(&msg, ncmds);
	for (i = 0; i < ncmds; i++) {
		MSG_WriteLong(&msg, (int)cmds[i].mask[0]);
		MSG_WriteLong(&msg, (int)cmds[i].mask[1]);
		write_raw(&msg, cmds[i].text);
	}

	write_masks(&msg, cur->ps_present);
	for (i = 0; i < MAX_CLIENTS; i++) {
		if (!cur->ps_present[i])
			continue;
		MSG_WriteDeltaPlayerstate(&msg, (!key && last->ps_present[i]) ? (playerState_t *)&last->ps[i] : (playerState_t *)&null_ps,
			(playerState_t *)&cur->ps[i]);
	}

	for (e = 0; e < MAX_GENTITIES - 1; e++) {
		int had = !key && last->present[e];
		if (cur->present[e]) {
			if (had) {
				MSG_WriteDeltaEntity(&msg, (entityState_t *)&last->ents[e], (entityState_t *)&cur->ents[e], qfalse);
			} else {
				Com_Memset(&null_es, 0, sizeof(null_es));
				null_es.number = e;
				MSG_WriteDeltaEntity(&msg, &null_es, (entityState_t *)&cur->ents[e], qtrue);
			}
		} else if (had) {
			MSG_WriteDeltaEntity(&msg, (entityState_t *)&last->ents[e], NULL, qfalse);
		}
	}
	MSG_WriteBits(&msg, MAX_GENTITIES - 1, GENTITYNUM_BITS);

	n = 0;
	for (e = 0; e < MAX_GENTITIES; e++) {
		if (cur->present[e] && cur->filter[e])
			n++;
	}
	MSG_WriteShort(&msg, n);
	for (e = 0; e < MAX_GENTITIES; e++) {
		if (cur->present[e] && cur->filter[e]) {
			MSG_WriteShort(&msg, e);
			MSG_WriteByte(&msg, cur->filter[e]);
			MSG_WriteByte(&msg, cur->filter_client[e]);
		}
	}
	MSG_WriteLong(&msg, FRAME_END);

	if (msg.overflowed || !out_reserve(o, BLOCK_HEADER + msg.cursize))
		return 0;
	put_u32(header, (unsigned)msg.cursize);
	header[4] = (byte)key;
	put_u32(header + 5, (unsigned)cur->time);
	out_append(o, header, BLOCK_HEADER);
	out_append(o, msg.data, msg.cursize);
	return 1;
}

/* One recorded frame of the running match. */
static void write_frame(int key) {
	rec_cur.time = sv.time;
	gather_entities(&rec_cur);
	gather_players(&rec_cur);
	if (!encode_frame(&rec.out, &rec_cur, &rec_last, key, sv.configstrings, rec.cs, rec.pending, rec.npending)) {
		Com_Printf(S_COLOR_YELLOW "replay: frame at %d ms too big to record, or out of memory; recording stopped\n", sv.time);
		rec.active = 0;
		return;
	}
	clear_pending();
	Com_Memcpy(&rec_last, &rec_cur, sizeof(rec_last));
	rec.frames++;
	if (key)
		rec.last_key = sv.time;
}

int q3_record_start(int hz) {
	int i;
	if (!q3_host_server_running()) {
		Q_strncpyz(q3_last_error, "no map running", sizeof(q3_last_error));
		return 0;
	}
	for (i = 0; i < MAX_CONFIGSTRINGS; i++) {
		free(rec.cs[i]);
		rec.cs[i] = NULL;
	}
	rec.active = 1;
	rec.interval = 1000 / (hz < 1 ? 40 : (hz > 125 ? 125 : hz));
	rec.next_time = 0;
	rec.frames = 0;
	clear_pending();
	rec.out.len = 0;
	rec_last.time = 0;
	Q_strncpyz(rec.map, sv_mapname->string, sizeof(rec.map));
	for (i = 0; i < MAX_CLIENTS; i++)
		rec.cmd_seq[i] = i < sv_maxclients->integer ? svs.clients[i].reliableSequence : 0;
	Com_Memset(rec_last.present, 0, sizeof(rec_last.present));
	Com_Memset(rec_last.ps_present, 0, sizeof(rec_last.ps_present));
	return 1;
}

/* Commands that came after the last frame go out in one more frame. */
void q3_record_stop(void) {
	int was = rec.active;
	rec.active = 0;   /* first: an error in the last write mustn't leave it recording */
	if (was && q3_host_server_running() && sv.time > rec_last.time) {
		GUARD_V();
		harvest_commands();
		if (rec.npending)
			write_frame(0);
		UNGUARD();
	}
}

int q3_record_active(void) { return rec.active; }
int q3_record_frames(void) { return rec.frames; }

const unsigned char *q3_record_data(int *len) {
	*len = rec.out.len;
	return rec.out.data;
}

void q3_record_consume(void) {
	rec.out.len = 0;
}

/* Called by q3_host_frame after every SV_Frame, inside its guard. */
void q3_record_after_frame(void) {
	if (!rec.active)
		return;
	if (strcmp(rec.map, sv_mapname->string) || (rec.frames && sv.time < rec_last.time)) {
		rec.active = 0;   /* another map, or the same one loaded again: this match is over */
		return;
	}
	harvest_commands();
	if (sv.time < rec.next_time)
		return;
	write_frame(rec.frames == 0 || sv.time - rec.last_key >= KEYFRAME_MSEC);
	rec.next_time = (sv.time / rec.interval + 1) * rec.interval;
}

/* Everything the recorder would write for this frame's entities (tests compare it with playback). */
int q3_host_entities(q3_entity_state *out, int max) {
	int e, n = 0;
	if (!q3_host_server_running())
		return 0;
	gather_entities(&rec_cur);
	for (e = 0; e < MAX_GENTITIES && n < max; e++) {
		if (rec_cur.present[e])
			Com_Memcpy(&out[n++], &rec_cur.ents[e], sizeof(entityState_t));
	}
	return n;
}

/* ---------------------------------------------------------------- playback */

typedef struct { int frame, index; char *text; } cs_change_t;
typedef struct { int frame; unsigned mask[2]; char *text; } replay_cmd_t;

struct q3_replay {
	byte *data;
	int len;
	int nframes;
	int *offset, *size, *time;
	byte *kind;
	frame_state_t *a, *b;           /* the frame shown (cur) and the one after it */
	int cur;
	int has_next;                   /* b holds frame cur + 1 */
	int broken;
	const char *cs[MAX_CONFIGSTRINGS];
	cs_change_t *changes;
	int nchanges, cap_changes;
	int cs_applied;                 /* changes[0 .. cs_applied) are in cs[] */
	int cs_frame;                   /* the frame cs[] is for */
	replay_cmd_t *cmds;
	int ncmds, cap_cmds;
	q3_replay_event *events;
	int nevents, cap_events;
	int scan_at;                    /* the frame the open pass is decoding (kept across a longjmp) */
};

static int map_loaded;

/* Makes room for `need` items in *p; 0 (and *p untouched) if memory ran out. */
static int grow(void **p, int *cap, int need, int item) {
	int c;
	void *n;
	if (need <= *cap)
		return 1;
	c = *cap ? *cap * 2 : 256;
	while (c < need)
		c *= 2;
	n = realloc(*p, (size_t)c * item);
	if (!n)
		return 0;
	*p = n;
	*cap = c;
	return 1;
}

static void read_masks(msg_t *msg, unsigned *m) {
	m[0] = (unsigned)MSG_ReadLong(msg);
	m[1] = (unsigned)MSG_ReadLong(msg);
}

/* Decodes frame i over `base` (ignored for a keyframe) into `out`. With `scan` set, the frame's
 * configstrings and commands are kept in the replay's tables (the pass at open). 0 on bad data. */
static int decode_frame(q3_replay *r, int i, const frame_state_t *base, frame_state_t *out, int scan) {
	msg_t msg;
	int n, k, e;
	unsigned masks[2];
	int key = r->kind[i];
	if (base && !key && base != out)
		Com_Memcpy(out, base, sizeof(*out));
	if (key || !base) {
		Com_Memset(out->present, 0, sizeof(out->present));
		Com_Memset(out->ps_present, 0, sizeof(out->ps_present));
	}
	Com_Memset(out->filter, 0, sizeof(out->filter));
	out->time = r->time[i];
	MSG_Init(&msg, r->data + r->offset[i], r->size[i]);
	msg.cursize = r->size[i];
	MSG_BeginReading(&msg);

	n = MSG_ReadShort(&msg);
	for (k = 0; k < n; k++) {
		int idx = MSG_ReadShort(&msg);
		const char *s = read_raw(&msg);
		if (idx < 0 || idx >= MAX_CONFIGSTRINGS || !s)
			return 0;
		if (scan) {
			cs_change_t *c;
			char *text = copy_string(s);
			if (!text || !grow((void **)&r->changes, &r->cap_changes, r->nchanges + 1, sizeof(cs_change_t))) {
				free(text);
				return 0;
			}
			c = &r->changes[r->nchanges++];
			c->frame = i;
			c->index = idx;
			c->text = text;
		}
	}
	n = MSG_ReadShort(&msg);
	for (k = 0; k < n; k++) {
		unsigned m[2];
		const char *s;
		read_masks(&msg, m);
		s = read_raw(&msg);
		if (!s)
			return 0;
		if (scan) {
			replay_cmd_t *c;
			char *text = copy_string(s);
			if (!text || !grow((void **)&r->cmds, &r->cap_cmds, r->ncmds + 1, sizeof(replay_cmd_t))) {
				free(text);
				return 0;
			}
			c = &r->cmds[r->ncmds++];
			c->frame = i;
			c->mask[0] = m[0];
			c->mask[1] = m[1];
			c->text = text;
		}
	}

	read_masks(&msg, masks);
	for (k = 0; k < MAX_CLIENTS; k++) {
		int on = (masks[k >> 5] >> (k & 31)) & 1;
		playerState_t from;
		if (!on) {
			out->ps_present[k] = 0;
			continue;
		}
		from = (!key && base && base->ps_present[k]) ? base->ps[k] : null_ps;
		MSG_ReadDeltaPlayerstate(&msg, &from, &out->ps[k]);
		out->ps_present[k] = 1;
	}

	for (;;) {
		entityState_t from;
		int had;
		e = MSG_ReadBits(&msg, GENTITYNUM_BITS);
		if (e == MAX_GENTITIES - 1)
			break;
		if (msg.readcount > msg.cursize || e < 0 || e >= MAX_GENTITIES - 1)
			return 0;
		had = !key && out->present[e];
		if (had) {
			from = out->ents[e];
		} else {
			Com_Memset(&from, 0, sizeof(from));
			from.number = e;
		}
		MSG_ReadDeltaEntity(&msg, &from, &out->ents[e], e);
		out->present[e] = out->ents[e].number != MAX_GENTITIES - 1;
	}

	n = MSG_ReadShort(&msg);
	for (k = 0; k < n; k++) {
		e = MSG_ReadShort(&msg);
		{
			int kind = MSG_ReadByte(&msg), client = MSG_ReadByte(&msg);
			if (e >= 0 && e < MAX_GENTITIES) {
				out->filter[e] = (byte)kind;
				out->filter_client[e] = (byte)client;
			}
		}
	}
	return MSG_ReadLong(&msg) == FRAME_END && msg.readcount <= msg.cursize;
}

static void add_event(q3_replay *r, int frame, int type, int a, int b, int c) {
	q3_replay_event *ev;
	if (!grow((void **)&r->events, &r->cap_events, r->nevents + 1, sizeof(q3_replay_event)))
		return;
	ev = &r->events[r->nevents++];
	ev->frame = frame;
	ev->time = r->time[frame];
	ev->type = type;
	ev->a = a;
	ev->b = b;
	ev->c = c;
}

/* What the timeline marks, found while scanning: new obituaries and team sounds (flag events),
 * and medal counts going up. */
static void scan_events(q3_replay *r, int frame, const frame_state_t *prev, const frame_state_t *f) {
	static const int medals[] = { PERS_IMPRESSIVE_COUNT, PERS_EXCELLENT_COUNT, PERS_GAUNTLET_FRAG_COUNT,
		PERS_DEFEND_COUNT, PERS_ASSIST_COUNT, PERS_CAPTURES };
	int e, i, k;
	for (e = 0; e < MAX_GENTITIES - 1; e++) {
		const entityState_t *s = &f->ents[e];
		if (!f->present[e] || s->eType < ET_EVENTS)
			continue;
		if (prev && prev->present[e] && prev->ents[e].eType == s->eType)
			continue;   /* temp events live 300 ms: seen before */
		if (s->eType - ET_EVENTS == EV_OBITUARY)
			add_event(r, frame, Q3_REPLAY_OBITUARY, s->otherEntityNum2, s->otherEntityNum, s->eventParm);
		else if (s->eType - ET_EVENTS == EV_GLOBAL_TEAM_SOUND)
			add_event(r, frame, Q3_REPLAY_TEAM_SOUND, s->eventParm, 0, 0);
	}
	if (!prev)
		return;
	for (i = 0; i < MAX_CLIENTS; i++) {
		if (!f->ps_present[i] || !prev->ps_present[i])
			continue;
		for (k = 0; k < (int)ARRAY_LEN(medals); k++) {
			int now = f->ps[i].persistant[medals[k]], was = prev->ps[i].persistant[medals[k]];
			if (now > was)
				add_event(r, frame, Q3_REPLAY_MEDAL, i, medals[k], now);
		}
	}
}

void q3_replay_close(q3_replay *r) {
	int i;
	if (!r)
		return;
	for (i = 0; i < r->nchanges; i++)
		free(r->changes[i].text);
	for (i = 0; i < r->ncmds; i++)
		free(r->cmds[i].text);
	free(r->changes);
	free(r->cmds);
	free(r->events);
	free(r->offset);
	free(r->size);
	free(r->time);
	free(r->kind);
	free(r->a);
	free(r->b);
	free(r->data);
	free(r);
}

static void apply_changes_to(q3_replay *r, int frame) {
	while (r->cs_applied < r->nchanges && r->changes[r->cs_applied].frame <= frame) {
		cs_change_t *c = &r->changes[r->cs_applied++];
		r->cs[c->index] = c->text;
	}
}

static int open_scan(q3_replay *r) {
	int pos = 0, i, cap = 0;
	frame_state_t *swap;
	while (pos + BLOCK_HEADER <= r->len) {
		int size = (int)get_u32(r->data + pos);
		if (size <= 0 || size > r->len - pos - BLOCK_HEADER)
			break;   /* a recording cut short (the game closed mid-write) ends at its last whole frame */
		if (!grow((void **)&r->offset, &cap, r->nframes + 1, sizeof(int)))
			return 0;
		r->offset[r->nframes] = pos + BLOCK_HEADER;
		r->nframes++;
		pos += BLOCK_HEADER + size;
	}
	if (r->nframes == 0)
		return 0;
	r->size = malloc(sizeof(int) * r->nframes);
	r->time = malloc(sizeof(int) * r->nframes);
	r->kind = malloc(r->nframes);
	if (!r->size || !r->time || !r->kind)
		return 0;
	for (i = 0; i < r->nframes; i++) {
		const byte *h = r->data + r->offset[i] - BLOCK_HEADER;
		r->size[i] = (int)get_u32(h);
		r->kind[i] = h[4];
		r->time[i] = (int)get_u32(h + 5);
	}
	if (!r->kind[0])
		return 0;
	/* one pass over every frame: the configstring and command tables, and the timeline's events */
	r->cur = -1;
	r->cs_frame = -1;
	for (i = 0; i < r->nframes; i++) {
		r->scan_at = i;
		if (!decode_frame(r, i, i ? r->a : NULL, r->b, 1)) {
			if (i == 0)
				return 0;
			r->nframes = i;   /* keep what decodes */
			break;
		}
		scan_events(r, i, i ? r->a : NULL, r->b);
		swap = r->a; r->a = r->b; r->b = swap;
	}
	r->cur = -1;
	r->cs_frame = -1;
	return 1;
}

q3_replay *q3_replay_open(const unsigned char *data, int len, char *err, int errlen) {
	q3_replay *r;
	int ok;
	err[0] = 0;
	if (!q3_ready) {
		Q_strncpyz(err, "the engine isn't running", errlen);
		return NULL;
	}
	if (com_sv_running->integer) {   /* an error while decoding would shut a match down */
		Q_strncpyz(err, "a match is running", errlen);
		return NULL;
	}
	r = calloc(1, sizeof(*r));
	if (!r) {
		Q_strncpyz(err, "out of memory", errlen);
		return NULL;
	}
	r->data = malloc(len > 0 ? len : 1);
	r->a = malloc(sizeof(frame_state_t));
	r->b = malloc(sizeof(frame_state_t));
	if (!r->data || !r->a || !r->b) {
		q3_replay_close(r);
		Q_strncpyz(err, "out of memory", errlen);
		return NULL;
	}
	memcpy(r->data, data, len);
	r->len = len;
	ok = 0;
	{
		/* a guard of its own: GUARD returns from the function, and r must be freed */
		if (setjmp(q3_fatal_frame)) {
			q3_fatal_armed = 0;
			ok = -1;
		} else {
			q3_fatal_armed = 1;
			if (setjmp(abortframe)) {
				q3_fatal_armed = 0;
				Q_strncpyz(q3_last_error, com_errorMessage, sizeof(q3_last_error));
				ok = -1;
				if (r->kind && r->scan_at > 0) {   /* damaged past the start: keep what decoded */
					r->nframes = r->scan_at;
					ok = 1;
				}
			} else {
				ok = open_scan(r);
				q3_fatal_armed = 0;
			}
		}
	}
	if (ok != 1) {
		Q_strncpyz(err, ok < 0 ? q3_last_error : "not a replay, or damaged", errlen);
		q3_replay_close(r);
		return NULL;
	}
	return r;
}

int q3_replay_frames(q3_replay *r) { return r ? r->nframes : 0; }

int q3_replay_frame_time(q3_replay *r, int i) {
	if (!r || i < 0 || i >= r->nframes)
		return 0;
	return r->time[i];
}

int q3_replay_frame_is_key(q3_replay *r, int i) {
	return r && i >= 0 && i < r->nframes && r->kind[i];
}

int q3_replay_current(q3_replay *r) { return r ? r->cur : -1; }


/* Shows frame `frame` and decodes the one after it (for interpolation). Playing steps forward one
 * frame at a time; anywhere else starts again from the keyframe at or before it (or carries on
 * from the frame shown, when that's on the way). 1 on success. */
int q3_replay_seek(q3_replay *r, int frame) {
	int k, i, start;
	frame_state_t *swap;
	if (!r || r->broken || frame < 0 || frame >= r->nframes)
		return 0;
	if (frame == r->cur)
		return 1;
	if (!q3_ready || com_sv_running->integer)
		return 0;   /* an error while decoding would shut a match down */
	/* GUARD, but a decode error also marks the replay broken (a and b may be half written) */
	if (setjmp(q3_fatal_frame)) {
		q3_fatal_armed = 0;
		r->broken = 1;
		r->cur = -1;
		return 0;
	}
	q3_fatal_armed = 1;
	if (setjmp(abortframe)) {
		q3_fatal_armed = 0;
		Q_strncpyz(q3_last_error, com_errorMessage, sizeof(q3_last_error));
		r->broken = 1;
		r->cur = -1;
		return 0;
	}
	if (r->cur >= 0 && frame == r->cur + 1 && r->has_next) {
		swap = r->a; r->a = r->b; r->b = swap;
	} else {
		k = frame;
		while (k > 0 && !r->kind[k])
			k--;
		if (r->cur >= k && r->cur < frame) {
			start = r->cur + 1;
		} else {
			if (!decode_frame(r, k, NULL, r->a, 0))
				goto bad;
			start = k + 1;
		}
		for (i = start; i <= frame; i++) {
			if (!decode_frame(r, i, r->a, r->b, 0))
				goto bad;
			swap = r->a; r->a = r->b; r->b = swap;
		}
	}
	r->cur = frame;
	r->has_next = 0;
	if (frame + 1 < r->nframes) {
		if (!decode_frame(r, frame + 1, r->a, r->b, 0))
			goto bad;
		r->has_next = 1;
	}
	if (frame < r->cs_frame) {
		Com_Memset((void *)r->cs, 0, sizeof(r->cs));
		r->cs_applied = 0;
	}
	apply_changes_to(r, frame);
	r->cs_frame = frame;
	UNGUARD();
	return 1;
bad:
	r->broken = 1;
	r->cur = -1;
	UNGUARD();
	return 0;
}

static const frame_state_t *state_of(q3_replay *r, int which) {
	if (!r || r->cur < 0)
		return NULL;
	if (which)
		return r->has_next ? r->b : NULL;
	return r->a;
}

int q3_replay_has_next(q3_replay *r) { return r && r->cur >= 0 && r->has_next; }

/* The entities a snapshot for `client` would carry (-1: a spectator's camera, for whom
 * single-client events are someone else's; -2: every entity recorded). which 0: the frame
 * shown, 1: the next one. */
int q3_replay_entities(q3_replay *r, int which, int client, q3_entity_state *out, int max) {
	const frame_state_t *f = state_of(r, which);
	int e, n = 0;
	if (!f)
		return 0;
	for (e = 0; e < MAX_GENTITIES && n < max; e++) {
		if (!f->present[e])
			continue;
		if (client > -2 && f->filter[e] == FILTER_SINGLE && f->filter_client[e] != client)
			continue;
		if (client > -2 && f->filter[e] == FILTER_NOTSINGLE && f->filter_client[e] == client)
			continue;
		Com_Memcpy(&out[n++], &f->ents[e], sizeof(entityState_t));
	}
	return n;
}

int q3_replay_player_state(q3_replay *r, int which, int client, q3_player_state *out) {
	const frame_state_t *f = state_of(r, which);
	if (!f || client < 0 || client >= MAX_CLIENTS || !f->ps_present[client])
		return 0;
	Com_Memcpy(out, &f->ps[client], sizeof(playerState_t));
	return 1;
}

const char *q3_replay_configstring(q3_replay *r, int index) {
	if (!r || index < 0 || index >= MAX_CONFIGSTRINGS || !r->cs[index])
		return "";
	return r->cs[index];
}

int q3_replay_command_count(q3_replay *r) { return r ? r->ncmds : 0; }

/* Command i: its text, the frame it arrived in, and the clients it went to (two 32-bit masks). */
const char *q3_replay_command(q3_replay *r, int i, int *frame, unsigned *mask) {
	if (!r || i < 0 || i >= r->ncmds)
		return "";
	*frame = r->cmds[i].frame;
	mask[0] = r->cmds[i].mask[0];
	mask[1] = r->cmds[i].mask[1];
	return r->cmds[i].text;
}

int q3_replay_event_count(q3_replay *r) { return r ? r->nevents : 0; }

int q3_replay_get_event(q3_replay *r, int i, q3_replay_event *out) {
	if (!r || i < 0 || i >= r->nevents)
		return 0;
	*out = r->events[i];
	return 1;
}

/* The map's collision model with no server running (the server loads it the same way). */
int q3_replay_load_map(const char *name, int *checksum) {
	GUARD(1, 2);
	if (com_sv_running->integer) {
		Q_strncpyz(q3_last_error, "a match is running", sizeof(q3_last_error));
		UNGUARD();
		return 1;
	}
	map_loaded = 0;   /* a failed load leaves no collision model behind */
	Hunk_Clear();
	CM_LoadMap(va("maps/%s.bsp", name), qfalse, checksum);
	map_loaded = 1;
	UNGUARD();
	return 0;
}

void q3_replay_unload_map(void) {
	if (!map_loaded)
		return;
	map_loaded = 0;
	if (q3_ready && !com_sv_running->integer)
		CM_ClearMap();
}

/* cgame's CG_ClipMoveToEntities over the frame's solid entities (not triggers or items). */
static int entity_clip(const entityState_t *s, int at, clipHandle_t *model, vec3_t origin, vec3_t angles) {
	if (!s->solid || s->eType == ET_ITEM || s->eType == ET_PUSH_TRIGGER || s->eType == ET_TELEPORT_TRIGGER)
		return 0;
	if (s->solid == SOLID_BMODEL) {
		if (s->modelindex <= 0 || s->modelindex >= CM_NumInlineModels())
			return 0;   /* not this map's (a replay recorded on another version of it) */
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

int q3_replay_trace(q3_replay *r, const float *start, const float *end, const float *mins, const float *maxs,
		int pass_entity, int content_mask, q3_trace_result *out) {
	trace_t tr, t;
	vec3_t s, e, mn, mx, origin, angles;
	const frame_state_t *f = state_of(r, 0);
	int i;
	clipHandle_t model;
	if (!map_loaded || com_sv_running->integer)
		return 0;   /* a match started since: the collision model is its map's */
	VectorCopy(start, s);
	VectorCopy(end, e);
	VectorCopy(mins, mn);
	VectorCopy(maxs, mx);
	{
		GUARD(0, 0);
		CM_BoxTrace(&tr, s, e, mn, mx, 0, content_mask, qfalse);
		tr.entityNum = tr.fraction != 1.0f ? ENTITYNUM_WORLD : ENTITYNUM_NONE;
		for (i = 0; f && i < MAX_GENTITIES - 1; i++) {
			if (!f->present[i] || i == pass_entity || !entity_clip(&f->ents[i], f->time, &model, origin, angles))
				continue;
			CM_TransformedBoxTrace(&t, s, e, mn, mx, model, content_mask, origin, angles, qfalse);
			if (t.allsolid || t.fraction < tr.fraction) {
				t.entityNum = i;
				tr = t;
			} else if (t.startsolid) {
				tr.startsolid = qtrue;
			}
		}
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

/* CG_PointContents: the world, and brush entities where they are now. */
int q3_replay_point_contents(q3_replay *r, const float *point, int pass_entity) {
	vec3_t p, origin, angles;
	const frame_state_t *f = state_of(r, 0);
	int i, c;
	clipHandle_t model;
	if (!map_loaded || com_sv_running->integer)
		return 0;
	VectorCopy(point, p);
	{
		GUARD(0, 0);
		c = CM_PointContents(p, 0);
		for (i = 0; f && i < MAX_GENTITIES - 1; i++) {
			if (!f->present[i] || i == pass_entity || f->ents[i].solid != SOLID_BMODEL)
				continue;
			if (entity_clip(&f->ents[i], f->time, &model, origin, angles))
				c |= CM_TransformedPointContents(p, model, origin, angles);
		}
		UNGUARD();
	}
	return c;
}

/* ---------------------------------------------------------------- Quake 3 demos (.dm_68)
 * A demo is one client's view: the messages its server sent it, each stored as
 * [i32 serverMessageSequence][i32 length][message], ending with length -1 (cl_main.c
 * CL_ReadDemoMessage). It's parsed as cl_parse.c does (CL_ParseGamestate, CL_ParseSnapshot,
 * CL_ParsePacketEntities, CL_ParseCommandString) over msg.c, then written back out as replay
 * frames through encode_frame, so a demo plays through the same decoder, seeking and timeline as
 * a replay. Its one playerState is the recording client's; the entities are what it saw. */

#define DEMO_BACKUP 32                /* PACKET_BACKUP */
#define DEMO_NONE 99999

typedef struct {
	int valid, message;
	playerState_t ps;   /* as the message delta'd it (the frame keeps it under ps.clientNum) */
	frame_state_t *f;
} demo_snap_t;

typedef struct {
	demo_snap_t ring[DEMO_BACKUP];
	frame_state_t *scratch, *written;
	entityState_t baselines[MAX_GENTITIES];
	char *cs[MAX_CONFIGSTRINGS];      /* the demo's configstrings now */
	char *cs_was[MAX_CONFIGSTRINGS];  /* as last written */
	char *big;                        /* a bcs0/bcs1/bcs2 configstring being put together */
	pending_cmd_t *cmds;
	int ncmds;
	int client, command_seq, gamestate, last_message, frames, last_key, last_time;
	int ended;                        /* a second gamestate (another map): stop there */
	int download_block;               /* CL_ParseDownload's clc.downloadBlock */
	int pov, pov_seq;                 /* whose eyes the last snapshot was, and their body's entityEventSequence */
	out_t out;
} demo_t;

static void demo_free(demo_t *d) {
	int i;
	if (!d)
		return;
	for (i = 0; i < DEMO_BACKUP; i++)
		free(d->ring[i].f);
	for (i = 0; i < MAX_CONFIGSTRINGS; i++) {
		free(d->cs[i]);
		free(d->cs_was[i]);
	}
	free(d->scratch);
	free(d->written);
	free(d->big);
	free(d->cmds);
	free(d->out.data);
	free(d);
}

static void demo_set_cs(demo_t *d, int index, const char *s) {
	if (index < 0 || index >= MAX_CONFIGSTRINGS)
		return;
	free(d->cs[index]);
	d->cs[index] = s[0] ? copy_string(s) : NULL;
}

/* "cs 5 <quote>text<quote>": the index and the rest without its quotes (Cmd_ArgsFrom(2)). */
static int parse_cs_command(const char *s, int *index, char *text, int size) {
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

/* A server command: configstrings change the table (as CL_ConfigstringModified and the big
 * configstring pieces do); the rest are the recording client's commands. */
static void demo_command(demo_t *d, const char *s) {
	static char text[BIG_INFO_STRING];
	int index;
	if (!Q_strncmp(s, "cs ", 3)) {
		if (parse_cs_command(s, &index, text, sizeof(text)))
			demo_set_cs(d, index, text);
		return;
	}
	if (!Q_strncmp(s, "bcs0 ", 5) || !Q_strncmp(s, "bcs1 ", 5) || !Q_strncmp(s, "bcs2 ", 5)) {
		char *joined;
		size_t a, b;
		if (!parse_cs_command(s, &index, text, sizeof(text)))
			return;
		if (s[3] == '0') {
			free(d->big);
			d->big = NULL;
		}
		a = d->big ? strlen(d->big) : 0;
		b = strlen(text);
		joined = malloc(a + b + 1);
		if (!joined)
			return;
		if (a)
			memcpy(joined, d->big, a);
		memcpy(joined + a, text, b + 1);
		free(d->big);
		d->big = joined;
		if (s[3] == '2') {
			demo_set_cs(d, index, d->big);
			free(d->big);
			d->big = NULL;
		}
		return;
	}
	if (skip_command(s) || d->ncmds >= MAX_PENDING)
		return;
	/* a demo has one viewer, whoever's eyes it shows when (they follow, follownext): every client */
	d->cmds[d->ncmds].mask[0] = d->cmds[d->ncmds].mask[1] = 0xffffffffu;
	Q_strncpyz(d->cmds[d->ncmds].text, s, MAX_STRING_CHARS);
	d->ncmds++;
}

static void demo_gamestate(demo_t *d, msg_t *msg) {
	entityState_t null_es;
	d->command_seq = MSG_ReadLong(msg);
	for (;;) {
		int c = MSG_ReadByte(msg);
		if (c == svc_EOF)
			break;
		if (msg->readcount > msg->cursize)
			Com_Error(ERR_DROP, "demo: gamestate cut short");
		if (c == svc_configstring) {
			int i = MSG_ReadShort(msg);
			const char *s = MSG_ReadBigString(msg);
			if (i < 0 || i >= MAX_CONFIGSTRINGS)
				Com_Error(ERR_DROP, "demo: configstring %d", i);
			demo_set_cs(d, i, s);
		} else if (c == svc_baseline) {
			int n = MSG_ReadBits(msg, GENTITYNUM_BITS);
			if (n < 0 || n >= MAX_GENTITIES)
				Com_Error(ERR_DROP, "demo: baseline %d", n);
			Com_Memset(&null_es, 0, sizeof(null_es));
			MSG_ReadDeltaEntity(msg, &null_es, &d->baselines[n], n);
		} else {
			Com_Error(ERR_DROP, "demo: bad gamestate command %d", c);
		}
	}
	d->client = MSG_ReadLong(msg);
	MSG_ReadLong(msg);   /* checksumFeed */
	d->gamestate = 1;
}

/* The next entity present in `old` from index *oi on, or DEMO_NONE. */
static int demo_next_old(const frame_state_t *old, int *oi) {
	if (!old)
		return DEMO_NONE;
	while (*oi < MAX_GENTITIES && !old->present[*oi])
		(*oi)++;
	if (*oi >= MAX_GENTITIES)
		return DEMO_NONE;
	return (*oi)++;
}

/* CL_ParsePacketEntities over dense frames: entities not in the delta carry over from `old`; new
 * ones delta from their baselines. */
static void demo_entities(demo_t *d, msg_t *msg, const frame_state_t *old, frame_state_t *f) {
	int oi = 0, oldnum;
	entityState_t tmp;
	Com_Memset(f->present, 0, sizeof(f->present));
	Com_Memset(f->filter, 0, sizeof(f->filter));
	oldnum = demo_next_old(old, &oi);
	for (;;) {
		int n = MSG_ReadBits(msg, GENTITYNUM_BITS);
		if (n == MAX_GENTITIES - 1)
			break;
		if (msg->readcount > msg->cursize || n < 0 || n >= MAX_GENTITIES - 1)
			Com_Error(ERR_DROP, "demo: entities cut short");
		while (oldnum < n) {
			f->ents[oldnum] = old->ents[oldnum];
			f->present[oldnum] = 1;
			oldnum = demo_next_old(old, &oi);
		}
		if (oldnum == n) {
			MSG_ReadDeltaEntity(msg, (entityState_t *)&old->ents[n], &tmp, n);
			oldnum = demo_next_old(old, &oi);
		} else {
			MSG_ReadDeltaEntity(msg, &d->baselines[n], &tmp, n);
		}
		if (tmp.number != MAX_GENTITIES - 1) {
			f->ents[n] = tmp;
			f->present[n] = 1;
		}
	}
	while (oldnum != DEMO_NONE) {
		f->ents[oldnum] = old->ents[oldnum];
		f->present[oldnum] = 1;
		oldnum = demo_next_old(old, &oi);
	}
}

/* CL_ParseSnapshot; a valid one becomes a frame. */
static void demo_snapshot(demo_t *d, msg_t *msg, int message) {
	frame_state_t *f = d->scratch, *old = NULL;
	const playerState_t *old_ps = NULL;
	playerState_t ps;
	byte area[32];
	int time, delta, len, valid = 0, i, key, who;
	time = MSG_ReadLong(msg);
	delta = MSG_ReadByte(msg);
	delta = delta ? message - delta : -1;
	MSG_ReadByte(msg);   /* snapFlags */
	if (delta <= 0) {
		valid = 1;
	} else {
		demo_snap_t *s = &d->ring[delta & (DEMO_BACKUP - 1)];
		old = s->f;   /* read through even when stale, to stay in step with the bits */
		old_ps = &s->ps;
		valid = s->valid && s->message == delta;
	}
	len = MSG_ReadByte(msg);
	if (len > (int)sizeof(area))
		Com_Error(ERR_DROP, "demo: areamask %d", len);
	MSG_ReadData(msg, area, len);
	MSG_ReadDeltaPlayerstate(msg, (playerState_t *)old_ps, &ps);
	demo_entities(d, msg, old, f);
	f->time = time;
	if (!valid)
		return;
	/* the messages between the last snapshot and this one are gone (CL_ParseSnapshot) */
	for (i = message - DEMO_BACKUP + 1 > d->last_message + 1 ? message - DEMO_BACKUP + 1 : d->last_message + 1; i < message; i++)
		d->ring[i & (DEMO_BACKUP - 1)].valid = 0;
	d->last_message = message;
	Com_Memset(f->ps_present, 0, sizeof(f->ps_present));
	Com_Memcpy(d->ring[message & (DEMO_BACKUP - 1)].f, f, sizeof(*f));
	d->ring[message & (DEMO_BACKUP - 1)].ps = ps;
	d->ring[message & (DEMO_BACKUP - 1)].valid = 1;
	d->ring[message & (DEMO_BACKUP - 1)].message = message;
	/* the frame: the playerState under the client whose eyes it is (following, someone else's), and
	 * their body, which the server never sends them (sv_snapshot.c), built from it as the server
	 * would (spectators and the intermission come out ET_INVISIBLE) */
	who = ps.clientNum;
	if (who >= 0 && who < MAX_CLIENTS) {
		f->ps[who] = ps;
		f->ps_present[who] = 1;
		if (!f->present[who]) {
			/* entityEventSequence never travels (msg.c), so it's kept here as the server keeps it in
			 * the gclient: one event onto the body per frame, from where the body last was; a new
			 * pair of eyes starts at their newest, so old events don't play again */
			playerState_t body = ps;
			if (who != d->pov)
				d->pov_seq = ps.eventSequence;
			body.entityEventSequence = d->pov_seq;
			BG_PlayerStateToEntityState(&body, &f->ents[who], qtrue);
			d->pov_seq = body.entityEventSequence;
			f->present[who] = 1;
		}
	}
	d->pov = who;
	if (!d->gamestate || time <= d->last_time)
		return;
	key = d->frames == 0 || time - d->last_key >= KEYFRAME_MSEC;
	if (!encode_frame(&d->out, f, d->written, key, d->cs, d->cs_was, d->cmds, d->ncmds))
		Com_Error(ERR_DROP, "demo: a frame too big, or out of memory");
	d->ncmds = 0;
	Com_Memcpy(d->written, f, sizeof(*f));
	d->frames++;
	d->last_time = time;
	if (key)
		d->last_key = time;
}

static void demo_message(demo_t *d, msg_t *msg, int message) {
	MSG_ReadLong(msg);   /* reliableAcknowledge */
	for (;;) {
		int c;
		if (msg->readcount > msg->cursize)
			Com_Error(ERR_DROP, "demo: message cut short");
		c = MSG_ReadByte(msg);
		if (c == svc_EOF)
			break;
		switch (c) {
		case svc_nop:
			break;
		case svc_serverCommand: {
			int seq = MSG_ReadLong(msg);
			const char *s = MSG_ReadString(msg);
			if (seq > d->command_seq) {
				d->command_seq = seq;
				demo_command(d, s);
			}
			break;
		}
		case svc_gamestate:
			if (d->gamestate) {
				d->ended = 1;   /* another map: the replay is the first one's */
				return;
			}
			demo_gamestate(d, msg);
			break;
		case svc_snapshot:
			demo_snapshot(d, msg, message);
			break;
		case svc_download: {
			/* CL_ParseDownload's reads: the total size with the first block (an error string if
			 * it's negative), then the chunk */
			int block = MSG_ReadShort(msg), size, k;
			if (block == 0 && !d->download_block) {
				if (MSG_ReadLong(msg) < 0) {
					MSG_ReadString(msg);
					break;
				}
			}
			size = MSG_ReadShort(msg);
			if (size < 0 || size > MAX_MSGLEN)
				Com_Error(ERR_DROP, "demo: download block %d", size);
			for (k = 0; k < size; k++)
				MSG_ReadByte(msg);
			d->download_block = size ? block + 1 : 0;
			break;
		}
		default:
			Com_Error(ERR_DROP, "demo: unknown server message %d (not a protocol 68 demo?)", c);
		}
	}
}

/* Converts a .dm_68 into replay frame blocks (q3_replay_open reads them). `max_frames` > 0 stops
 * early (the replay browser only needs the first frame's configstrings). Returns 1 with *out set
 * (free it with q3_demo_free), 0 with err. A demo damaged past its start keeps what came before. */
int q3_demo_convert(const unsigned char *data, int len, int max_frames, unsigned char **out, int *out_len, char *err, int errlen) {
	static byte buf[MAX_MSGLEN];
	demo_t *d;
	int pos = 0, ok = 0, i;
	*out = NULL;
	*out_len = 0;
	err[0] = 0;
	if (!q3_ready || com_sv_running->integer) {
		Q_strncpyz(err, q3_ready ? "a match is running" : "the engine isn't running", errlen);
		return 0;
	}
	d = calloc(1, sizeof(*d));
	if (!d) {
		Q_strncpyz(err, "out of memory", errlen);
		return 0;
	}
	d->scratch = calloc(1, sizeof(frame_state_t));
	d->written = calloc(1, sizeof(frame_state_t));
	d->cmds = calloc(MAX_PENDING, sizeof(pending_cmd_t));
	ok = d->scratch && d->written && d->cmds;
	for (i = 0; i < DEMO_BACKUP; i++) {
		d->ring[i].f = calloc(1, sizeof(frame_state_t));
		ok = ok && d->ring[i].f;
	}
	if (!ok) {
		demo_free(d);
		Q_strncpyz(err, "out of memory", errlen);
		return 0;
	}
	d->client = -1;
	d->pov = -1;
	d->last_message = -1;
	d->last_time = -0x7fffffff;
	ok = 0;
	/* the guard, by hand: d lives on the heap, so a longjmp loses nothing */
	if (setjmp(q3_fatal_frame)) {
		q3_fatal_armed = 0;
		ok = -1;
	} else {
		q3_fatal_armed = 1;
		if (setjmp(abortframe)) {
			q3_fatal_armed = 0;
			Q_strncpyz(q3_last_error, com_errorMessage, sizeof(q3_last_error));
			ok = -1;
		} else {
			while (pos + 8 <= len && !d->ended && (max_frames <= 0 || d->frames < max_frames)) {
				msg_t msg;
				int message = (int)get_u32(data + pos), size = (int)get_u32(data + pos + 4);
				pos += 8;
				if (size == -1)
					break;
				if (size < 0 || size > (int)sizeof(buf) || size > len - pos)
					break;   /* cut short: what came before plays */
				memcpy(buf, data + pos, size);
				pos += size;
				MSG_Init(&msg, buf, sizeof(buf));
				msg.cursize = size;
				MSG_BeginReading(&msg);
				demo_message(d, &msg, message);
			}
			q3_fatal_armed = 0;
			ok = 1;
		}
	}
	if (d->frames == 0) {
		Q_strncpyz(err, ok < 0 ? q3_last_error : (d->gamestate ? "the demo has no whole snapshot" : "not a Quake 3 demo"), errlen);
		demo_free(d);
		return 0;
	}
	*out = d->out.data;
	*out_len = d->out.len;
	d->out.data = NULL;
	demo_free(d);
	return 1;
}

void q3_demo_free(unsigned char *data) {
	free(data);
}
