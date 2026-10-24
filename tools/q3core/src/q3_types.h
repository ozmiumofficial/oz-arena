/* Layout copies of id's network structs (q_shared.h), for the C++ side, which can't include
 * q_shared.h. q3_host.c checks at compile time that every size matches. */
#ifndef Q3_TYPES_H
#define Q3_TYPES_H

#define Q3_MAX_PS_EVENTS 2
#define Q3_MAX_STATS 16
#define Q3_MAX_PERSISTANT 16
#define Q3_MAX_POWERUPS 16
#define Q3_MAX_WEAPONS 16
#define Q3_MAX_CLIENTS 64
#define Q3_MAX_GENTITIES 1024
#define Q3_ENTITYNUM_NONE (Q3_MAX_GENTITIES - 1)
#define Q3_ENTITYNUM_WORLD (Q3_MAX_GENTITIES - 2)
#define Q3_MAX_CONFIGSTRINGS 1024

typedef struct {
	int trType;
	int trTime;
	int trDuration;
	float trBase[3];
	float trDelta[3];
} q3_trajectory;

typedef struct {
	int number;
	int eType;
	int eFlags;
	q3_trajectory pos;
	q3_trajectory apos;
	int time;
	int time2;
	float origin[3];
	float origin2[3];
	float angles[3];
	float angles2[3];
	int otherEntityNum;
	int otherEntityNum2;
	int groundEntityNum;
	int constantLight;
	int loopSound;
	int modelindex;
	int modelindex2;
	int clientNum;
	int frame;
	int solid;
	int event;
	int eventParm;
	int powerups;
	int weapon;
	int legsAnim;
	int torsoAnim;
	int generic1;
} q3_entity_state;

typedef struct {
	int commandTime;
	int pm_type;
	int bobCycle;
	int pm_flags;
	int pm_time;
	float origin[3];
	float velocity[3];
	int weaponTime;
	int gravity;
	int speed;
	int delta_angles[3];
	int groundEntityNum;
	int legsTimer;
	int legsAnim;
	int torsoTimer;
	int torsoAnim;
	int movementDir;
	float grapplePoint[3];
	int eFlags;
	int eventSequence;
	int events[Q3_MAX_PS_EVENTS];
	int eventParms[Q3_MAX_PS_EVENTS];
	int externalEvent;
	int externalEventParm;
	int externalEventTime;
	int clientNum;
	int weapon;
	int weaponstate;
	float viewangles[3];
	int viewheight;
	int damageEvent;
	int damageYaw;
	int damagePitch;
	int damageCount;
	int stats[Q3_MAX_STATS];
	int persistant[Q3_MAX_PERSISTANT];
	int powerups[Q3_MAX_POWERUPS];
	int ammo[Q3_MAX_WEAPONS];
	int generic1;
	int loopSound;
	int jumppad_ent;
	int ping;
	int pmove_framecount;
	int jumppad_frame;
	int entityEventSequence;
} q3_player_state;

/* What the game reads from a client each frame (usercmd_t). Angles are in degrees here;
 * q3_host.c packs them with ANGLE2SHORT like the client does. */
typedef struct {
	int serverTime;
	float angles[3];
	int buttons;
	int weapon;
	int forwardmove, rightmove, upmove;   /* -127..127 */
} q3_usercmd;

/* A world trace (trace_t's useful part). */
typedef struct {
	float fraction;
	float endpos[3];
	float normal[3];
	int surfaceFlags;
	int contents;
	int entityNum;
	int startsolid;
	int allsolid;
} q3_trace_result;

/* An item on the map as the game holds it, for item timers. */
typedef struct {
	int number;
	int item;           /* es.modelindex: the bg_itemlist index */
	float origin[3];
	int taken;          /* picked up (or not in yet) and waiting to come back */
	int respawn_time;   /* the level time it comes back (gentity_t nextthink), 0 if not waiting */
} q3_item_state;

/* Something a replay's timeline marks, found when it's opened. */
#define Q3_REPLAY_OBITUARY 1     /* a: attacker, b: target, c: means of death */
#define Q3_REPLAY_TEAM_SOUND 2   /* a: the GTS_ sound (flag taken, captured, returned) */
#define Q3_REPLAY_MEDAL 3        /* a: client, b: the PERS_ count, c: its new value */
typedef struct {
	int frame;
	int time;
	int type;
	int a, b, c;
} q3_replay_event;

/* One client snapshot as the server built it (clientSnapshot_t), entities copied out. */
typedef struct {
	int serverTime;
	q3_player_state ps;
	int num_entities;
	q3_entity_state entities[Q3_MAX_GENTITIES];
} q3_snapshot;


/* The net client's picture of its snapshots (engine/q3_netclient.c). */
typedef struct {
	int message, next_message;   /* the netchan sequences of the snapshot returned and the one after it (-1 if none) */
	int snap_flags, ping;
	int newest_message, newest_time;
} q3_nc_snap_info;

typedef struct {
	int state, ping, packets_in, packets_out, bytes_in, bytes_out, dropped, snapshots, server_time, time_delta;
} q3_nc_stats;

#endif
