/*
===========================================================================
Oz Arena: peer-to-peer play (ENGINE_PLAN.md P8). GPL-2.0.

The NA_ICE transport: one libjuice ICE agent per remote player, its packets handed to id's netcode
as an address of their own (net_ip.c, NA_ICE: the link's slot in ipv._4, "ice:<player id>" as a
string), so netchan, delta snapshots and prediction run as over any UDP socket. libjuice is plain
UDP ICE (no DTLS or SCTP): the packets on the wire are the game's own. Candidates are host (LAN),
server-reflexive (STUN, hole punching) and relay (TURN); the connection takes the best pair that
answers.

Signaling goes through the hub (online/src/hub.ts, M4's messages):
  host    oz_host lists the running listen server (net_host), and keeps the listing current;
          net_request -> net_accept (or net_reject: password, room, version);
          the joiner's offer -> an agent, the answer and its candidates back.
  joiner  oz_join <host> -> net_join -> net_accepted -> relay credentials (net_ice) -> an agent,
          the offer and its candidates; once ICE connects, "connect ice:<host>".
Hide my IP (oz_network_hide_ip) makes this side's agent relay-only (libjuice patched,
engine/patches/libjuice): only relay candidates are offered, nothing is sent from the player's own
address, and the hub (direct: false) strips addresses from what it forwards. Without it, direct: true
lets the hub forward host and server-reflexive candidates.

A host whose game kicks a player (a passed vote, clientkick) tells the hub (net_kick), which ends the
link and keeps that player out of the host's matches for 30 minutes.

The menus (oz_ui_social.cpp) hear a join's progress (OzUI_JoinState: asking, connecting, joined or
failed, with why), and presence carries the match a player hosts or joined (OzIce_JoinString), so a
friend's Join button can follow.

  oz_host			lists the listen server in the hub's directory (Create Match does it)
  oz_unhost			takes the listing down
  oz_browse			asks the hub for the open matches (printed, and the menus' net_matches)
  oz_quick <mode>		asks the hub for the open match in a mode most worth joining (Quick Play:
					printed, and the menus' net_quick, which join it)
  oz_join <host> [password]	joins a listed match: its host's player id
  oz_unjoin			gives up joining (links whose game connection isn't up yet)
  oz_ice			the links: state, and direct or relayed
  oz_iceserver <port> <user> <password>	a TURN server of this process's own (libjuice's), for tests
					that need a relay without the internet; 0 stops it

  oz_net_stun		STUN server, host:port (stun.cloudflare.com:3478); empty for none
  oz_net_turn		extra relays, user:password@host:port[,...], added to the hub's (tests use a local one)
  oz_ice_lag, oz_ice_loss	added one-way latency (ms) and packet loss (%) on what this side receives
  oz_ice_debug		1: libjuice's log in the console
===========================================================================
*/
#include "../../../quake3e/code/client/client.h"
#include "oz_online.h"
#include "cJSON.h"
#include <juice/juice.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef CRITICAL_SECTION iceLock_t;
#define LockInit( l )	InitializeCriticalSection( l )
#define Lock( l )		EnterCriticalSection( l )
#define Unlock( l )		LeaveCriticalSection( l )
#else
#include <pthread.h>
typedef pthread_mutex_t iceLock_t;
#define LockInit( l )	pthread_mutex_init( l, NULL )
#define Lock( l )		pthread_mutex_lock( l )
#define Unlock( l )		pthread_mutex_unlock( l )
#endif

#define OZ_P2P_VERSION		"oz-p8"		// the listing's and the join's version: both builds must match
#define ICE_LINKS			32
#define ICE_PACKETS			2048		// received packets waiting for the frame
#define ICE_PACKET_MAX		1600
#define ICE_EVENTS			512
#define ICE_PENDING			24			// remote candidates kept until the agent exists
#define ICE_RELAYS			4
#define ICE_CONNECT_MS		30000		// a link that doesn't connect in this time fails
#define ICE_IDLE_MS			40000		// a connected link that hears nothing this long is closed

typedef struct {
	int			used;
	int			gen;					// counts the slot's reuse, so a dead link's events are dropped
	char		pid[24];				// the other player's hub id
	char		name[64];
	qboolean	hosting;				// we host and they joined
	juice_agent_t	*agent;
	juice_state_t	state;
	qboolean	descSet;				// the remote description is in
	char		*offer;					// host: the joiner's offer, until the relay credentials are in
	char		pending[ICE_PENDING][JUICE_MAX_CANDIDATE_SDP_STRING_LEN];
	int			numPending;
	qboolean	relayed;
	char		pair[160];				// the selected candidates' types
	qboolean	accepted;				// joiner: the host took us (net_accepted)
	qboolean	connectSent;			// joiner: the game's connect went out
	qboolean	wasActive;				// joiner: the game connected over it
	int			created, connectedAt;
	volatile int	lastRecv;			// Sys_Milliseconds of the last packet (libjuice's thread)
	char		password[64];
} iceLink_t;

typedef struct {
	int		slot, gen;
	int		due;
	int		length;
	byte	data[ICE_PACKET_MAX];
} icePacket_t;

typedef enum { EV_STATE, EV_CANDIDATE, EV_GATHERED, EV_LOG } iceEventKind_t;

typedef struct {
	iceEventKind_t	kind;
	int		slot, gen;
	int		state;
	char	text[JUICE_MAX_CANDIDATE_SDP_STRING_LEN];
} iceEvent_t;

static iceLink_t	links[ICE_LINKS];
static iceLock_t	lock;
static qboolean		lockReady;

static icePacket_t	*packets;					// a ring, filled on libjuice's thread
static int			packetHead, packetTail;
static iceEvent_t	events[ICE_EVENTS];
static int			eventHead, eventTail;
static volatile int	lagMs, lossPct;				// copies of the cvars for libjuice's thread

// relay credentials (the hub's net_ice, plus oz_net_turn)
static struct {
	qboolean	have, asked;
	int			askedAt, expires;
	int			count;
	char		host[ICE_RELAYS][128], user[ICE_RELAYS][160], pass[ICE_RELAYS][256];
	uint16_t	port[ICE_RELAYS];
	juice_turn_server_t	servers[ICE_RELAYS];
	char		error[160];
} relay;

static struct {
	qboolean	on;
	char		last[1024];
	int			checkedAt;
} host;

static juice_server_t	*testServer;
static char		stunHost[128];
static char		joinPassword[64];

static cvar_t	*oz_network_hide_ip, *oz_net_stun, *oz_net_turn, *oz_ice_lag, *oz_ice_loss, *oz_ice_debug;
static cvar_t	*oz_net_link;	// "direct" or "relayed": the joined match's link (the connection screen)

#ifdef USE_OZ_UI
void OzUI_HubMessage( const char *type, const cJSON *msg );
void OzUI_JoinState( const char *status, const char *host, const char *reason );
#endif


static qboolean HideIp( void ) {
	return oz_network_hide_ip && oz_network_hide_ip->integer;
}


// the region the player picked (Settings > Network, oz_network_region), or "" for Automatic: the hub
// then takes the one it saw the player in
static const char *Region( void ) {
	const char *r = Cvar_VariableString( "oz_network_region" );
	return Q_stricmp( r, "auto" ) ? r : "";
}


// a join's progress for the menus (the Server Browser's join bar, the toasts): asking, connecting,
// joined (the reason is the link: direct or relayed), failed (the reason a sentence), or "" (given up)
static void JoinState( const char *status, const char *host, const char *reason ) {
#ifdef USE_OZ_UI
	OzUI_JoinState( status, host, reason );
#endif
}


static void SendJson( cJSON *m ) {
	char *text = cJSON_PrintUnformatted( m );
	if ( text ) {
		if ( !OzOnline_Send( text ) ) {
			Com_Printf( S_COLOR_YELLOW "ice: not online, %s not sent\n", cJSON_GetObjectItemCaseSensitive( m, "type" )->valuestring );
		}
		cJSON_free( text );
	}
	cJSON_Delete( m );
}


static cJSON *Msg( const char *type ) {
	cJSON *m = cJSON_CreateObject();
	cJSON_AddStringToObject( m, "type", type );
	return m;
}


/*
==============================================================================

LINKS

==============================================================================
*/

static iceLink_t *FindLink( const char *pid ) {
	int i;
	for ( i = 0; i < ICE_LINKS; i++ ) {
		if ( links[i].used && !strcmp( links[i].pid, pid ) ) {
			return &links[i];
		}
	}
	return NULL;
}


static iceLink_t *NewLink( const char *pid, qboolean hosting ) {
	int i;
	for ( i = 0; i < ICE_LINKS; i++ ) {
		iceLink_t *l = &links[i];
		if ( !l->used ) {
			int gen = l->gen + 1;
			Com_Memset( l, 0, sizeof( *l ) );
			l->used = 1;
			l->gen = gen;
			Q_strncpyz( l->pid, pid, sizeof( l->pid ) );
			l->hosting = hosting;
			l->created = Sys_Milliseconds();
			l->lastRecv = l->created;
			return l;
		}
	}
	Com_Printf( S_COLOR_YELLOW "ice: no free link for %s\n", pid );
	return NULL;
}


static void FreeLink( iceLink_t *l, const char *why ) {
	if ( !l->used ) {
		return;
	}
	if ( why ) {
		Com_Printf( "ice: link with %s closed (%s)\n", l->pid, why );
	}
	if ( l->agent ) {
		juice_destroy( l->agent );		// no callback runs after it returns
	}
	if ( l->offer ) {
		Z_Free( l->offer );
	}
	l->agent = NULL;
	l->offer = NULL;
	l->used = 0;
}


static int SlotOf( const iceLink_t *l ) {
	return (int)( l - links );
}


// a joiner's link that ends before its game connection went out: the menus say why
static void JoinFailed( const iceLink_t *l, const char *reason ) {
	if ( l->used && !l->hosting && !l->connectSent ) {
		JoinState( "failed", l->pid, reason );
	}
}


/*
==============================================================================

LIBJUICE'S THREAD

Only the queues are touched here, under the lock; the frame does the rest.

==============================================================================
*/

static void PushEvent( iceEventKind_t kind, void *user, int state, const char *text ) {
	intptr_t u = (intptr_t)user;
	int next;

	Lock( &lock );
	next = ( eventHead + 1 ) % ICE_EVENTS;
	if ( next != eventTail ) {
		iceEvent_t *e = &events[eventHead];
		e->kind = kind;
		e->slot = (int)( u & 0xff );
		e->gen = (int)( u >> 8 );
		e->state = state;
		Q_strncpyz( e->text, text ? text : "", sizeof( e->text ) );
		eventHead = next;
	}
	Unlock( &lock );
}


static void CbState( juice_agent_t *agent, juice_state_t state, void *user ) {
	PushEvent( EV_STATE, user, state, NULL );
}


static void CbCandidate( juice_agent_t *agent, const char *sdp, void *user ) {
	PushEvent( EV_CANDIDATE, user, 0, sdp );
}


static void CbGathered( juice_agent_t *agent, void *user ) {
	PushEvent( EV_GATHERED, user, 0, NULL );
}


static void CbRecv( juice_agent_t *agent, const char *data, size_t size, void *user ) {
	intptr_t u = (intptr_t)user;
	int slot = (int)( u & 0xff ), next, now;

	if ( size > ICE_PACKET_MAX || slot < 0 || slot >= ICE_LINKS ) {
		return;
	}
	now = Sys_Milliseconds();
	links[slot].lastRecv = now;
	if ( lossPct > 0 && ( rand() % 100 ) < lossPct ) {
		return;		// oz_ice_loss
	}
	Lock( &lock );
	next = ( packetHead + 1 ) % ICE_PACKETS;
	if ( packets && next != packetTail ) {
		icePacket_t *p = &packets[packetHead];
		p->slot = slot;
		p->gen = (int)( u >> 8 );
		p->due = now + lagMs;
		p->length = (int)size;
		memcpy( p->data, data, size );
		packetHead = next;
	}
	Unlock( &lock );
}


static void CbLog( juice_log_level_t level, const char *message ) {
	PushEvent( EV_LOG, (void *)(intptr_t)0, level, message );
}


/*
==============================================================================

THE TRANSPORT (net_ip.c, net_chan.c)

==============================================================================
*/

qboolean OzIce_Active( void ) {
	int i;
	for ( i = 0; i < ICE_LINKS; i++ ) {
		if ( links[i].used && links[i].agent ) {
			return qtrue;
		}
	}
	return qfalse;
}


qboolean OzIce_StringToAdr( const char *s, netadr_t *a ) {
	const iceLink_t *l;
	int slot;

	if ( !Q_stricmpn( s, "ice:", 4 ) ) {
		s += 4;
	}
	l = FindLink( s );
	if ( !l ) {
		return qfalse;
	}
	slot = SlotOf( l );
	Com_Memset( a, 0, sizeof( *a ) );
	a->type = NA_ICE;
	a->ipv._4[0] = 0xff;	// never a real IPv4 address the rate limiter could mix it with
	a->ipv._4[2] = (byte)( l->gen & 0xff );
	a->ipv._4[3] = (byte)slot;
	return qtrue;
}


static iceLink_t *LinkOfAdr( const netadr_t *a ) {
	int slot = a->ipv._4[3];
	if ( a->type != NA_ICE || slot >= ICE_LINKS || !links[slot].used || ( links[slot].gen & 0xff ) != a->ipv._4[2] ) {
		return NULL;
	}
	return &links[slot];
}


// the stats recorder's server (oz_stats.c): a peer-to-peer match is its host's, on the host's own game too
const char *OzIce_HostKey( void ) {
	return host.on && com_sv_running->integer ? va( "ice:%s", OzIdentity_PlayerId() ) : "";
}


const char *OzIce_AdrToString( const netadr_t *a ) {
	static char s[32];
	const iceLink_t *l = LinkOfAdr( a );
	Com_sprintf( s, sizeof( s ), "ice:%s", l ? l->pid : "gone" );
	return s;
}


void OzIce_SendPacket( int length, const void *data, const netadr_t *to ) {
	iceLink_t *l = LinkOfAdr( to );
	if ( l && l->agent && ( l->state == JUICE_STATE_CONNECTED || l->state == JUICE_STATE_COMPLETED ) ) {
		juice_send( l->agent, (const char *)data, length );
	}
}


qboolean OzIce_GetPacket( netadr_t *from, void *data, int *length, int maxsize ) {
	qboolean got = qfalse;
	int now = Sys_Milliseconds();

	if ( !lockReady ) {
		return qfalse;
	}
	Lock( &lock );
	while ( packetTail != packetHead ) {
		icePacket_t *p = &packets[packetTail];
		iceLink_t *l = &links[p->slot];
		if ( p->due - now > 0 ) {
			break;		// oz_ice_lag: not yet
		}
		packetTail = ( packetTail + 1 ) % ICE_PACKETS;
		if ( !l->used || l->gen != p->gen || p->length > maxsize ) {
			continue;
		}
		Com_Memset( from, 0, sizeof( *from ) );
		from->type = NA_ICE;
		from->ipv._4[0] = 0xff;
		from->ipv._4[2] = (byte)( l->gen & 0xff );
		from->ipv._4[3] = (byte)p->slot;
		memcpy( data, p->data, p->length );
		*length = p->length;
		got = qtrue;
		break;
	}
	Unlock( &lock );
	return got;
}


/*
==============================================================================

RELAY CREDENTIALS AND AGENTS

==============================================================================
*/

// oz_net_turn: user:password@host:port[,...]
static void AddTurnCvar( void ) {
	char buf[MAX_CVAR_VALUE_STRING], *item, *next;

	Q_strncpyz( buf, oz_net_turn->string, sizeof( buf ) );
	for ( item = buf; item && *item && relay.count < ICE_RELAYS; item = next ) {
		char *at, *colon, *port;
		next = strchr( item, ',' );
		if ( next ) {
			*next++ = '\0';
		}
		at = strrchr( item, '@' );
		colon = strchr( item, ':' );
		if ( !at || !colon || colon > at || !( port = strrchr( at, ':' ) ) ) {
			continue;
		}
		*at = *colon = *port = '\0';
		Q_strncpyz( relay.user[relay.count], item, sizeof( relay.user[0] ) );
		Q_strncpyz( relay.pass[relay.count], colon + 1, sizeof( relay.pass[0] ) );
		Q_strncpyz( relay.host[relay.count], at + 1, sizeof( relay.host[0] ) );
		relay.port[relay.count] = (uint16_t)atoi( port + 1 );
		relay.count++;
	}
}


static void AskRelay( void ) {
	int now = Sys_Milliseconds();
	if ( relay.have && relay.expires - now > 15 * 60 * 1000 ) {
		return;		// still good for a while (the hub's guard: hosts renew only past 45 minutes)
	}
	if ( relay.asked && now - relay.askedAt < 3000 ) {
		return;		// one request every 2 s per player at the hub
	}
	relay.asked = qtrue;
	relay.askedAt = now;
	SendJson( Msg( "net_ice" ) );
}


// net_ice {servers: [{urls, username, credential}], relay_only, ttl | error}
static void TakeRelay( const cJSON *m ) {
	const cJSON *servers = cJSON_GetObjectItemCaseSensitive( m, "servers" ), *s;
	const cJSON *err = cJSON_GetObjectItemCaseSensitive( m, "error" );
	const cJSON *ttl = cJSON_GetObjectItemCaseSensitive( m, "ttl" );

	relay.asked = qfalse;
	if ( cJSON_IsString( err ) ) {
		Com_Printf( "ice: relay: %s\n", err->valuestring );
	}
	if ( !cJSON_IsArray( servers ) && relay.have ) {
		return;		// refused (too soon, the month's budget): what we have stays
	}
	Q_strncpyz( relay.error, cJSON_IsString( err ) ? err->valuestring : "", sizeof( relay.error ) );
	relay.count = 0;
	cJSON_ArrayForEach( s, servers ) {
		const cJSON *urls = cJSON_GetObjectItemCaseSensitive( s, "urls" ), *u;
		const cJSON *user = cJSON_GetObjectItemCaseSensitive( s, "username" );
		const cJSON *cred = cJSON_GetObjectItemCaseSensitive( s, "credential" );
		if ( !cJSON_IsString( user ) || !cJSON_IsString( cred ) ) {
			continue;
		}
		cJSON_ArrayForEach( u, urls ) {
			// turn:host:port[?transport=udp]: libjuice relays over UDP only
			char url[256], *q, *port;
			if ( !cJSON_IsString( u ) || Q_stricmpn( u->valuestring, "turn:", 5 ) || relay.count >= ICE_RELAYS ) {
				continue;
			}
			Q_strncpyz( url, u->valuestring + 5, sizeof( url ) );
			q = strchr( url, '?' );
			if ( q ) {
				if ( !strstr( q, "transport=udp" ) ) {
					continue;
				}
				*q = '\0';
			}
			port = strrchr( url, ':' );
			relay.port[relay.count] = port ? (uint16_t)atoi( port + 1 ) : 3478;
			if ( port ) {
				*port = '\0';
			}
			Q_strncpyz( relay.host[relay.count], url, sizeof( relay.host[0] ) );
			Q_strncpyz( relay.user[relay.count], user->valuestring, sizeof( relay.user[0] ) );
			Q_strncpyz( relay.pass[relay.count], cred->valuestring, sizeof( relay.pass[0] ) );
			relay.count++;
		}
	}
	AddTurnCvar();
	relay.have = qtrue;
	relay.expires = Sys_Milliseconds() + ( cJSON_IsNumber( ttl ) ? ttl->valueint : 3600 ) * 1000;
}


static void Signal( iceLink_t *l, const char *kind, const char *key, const char *value ) {
	cJSON *m = Msg( "net_signal" );
	cJSON_AddStringToObject( m, "to", l->pid );
	cJSON_AddStringToObject( m, "kind", kind );
	cJSON_AddStringToObject( m, key, value );
	cJSON_AddBoolToObject( m, "direct", !HideIp() );	// the hub forwards our own address only with this
	SendJson( m );
}


static qboolean StartAgent( iceLink_t *l ) {
	juice_config_t cfg;
	char desc[JUICE_MAX_SDP_STRING_LEN];
	int i;

	if ( HideIp() && !relay.count ) {
		Com_Printf( S_COLOR_YELLOW "ice: Hide my IP needs the relay, and %s\n", relay.error[0] ? relay.error : "this hub gave none" );
		JoinFailed( l, "Hide my IP needs the relay, and the online service has none right now." );
		return qfalse;
	}
	Com_Memset( &cfg, 0, sizeof( cfg ) );
	cfg.concurrency_mode = JUICE_CONCURRENCY_MODE_POLL;
	if ( !HideIp() && oz_net_stun->string[0] ) {
		char *colon;
		Q_strncpyz( stunHost, oz_net_stun->string, sizeof( stunHost ) );
		colon = strrchr( stunHost, ':' );
		cfg.stun_server_port = colon ? (uint16_t)atoi( colon + 1 ) : 3478;
		if ( colon ) {
			*colon = '\0';
		}
		cfg.stun_server_host = stunHost;
	}
	for ( i = 0; i < relay.count; i++ ) {
		relay.servers[i].host = relay.host[i];
		relay.servers[i].port = relay.port[i];
		relay.servers[i].username = relay.user[i];
		relay.servers[i].password = relay.pass[i];
	}
	cfg.turn_servers = relay.count ? relay.servers : NULL;
	cfg.turn_servers_count = relay.count;
	cfg.cb_state_changed = CbState;
	cfg.cb_candidate = CbCandidate;
	cfg.cb_gathering_done = CbGathered;
	cfg.cb_recv = CbRecv;
	cfg.user_ptr = (void *)(intptr_t)( SlotOf( l ) | ( l->gen << 8 ) );
	l->agent = juice_create( &cfg );
	if ( !l->agent ) {
		Com_Printf( S_COLOR_YELLOW "ice: couldn't make an agent\n" );
		JoinFailed( l, "The connection couldn't start." );
		return qfalse;
	}
	if ( HideIp() ) {
		juice_set_relay_only( l->agent, true );
	}
	l->state = JUICE_STATE_DISCONNECTED;
	if ( l->offer ) {
		// host: the joiner's offer first, so this side answers
		juice_set_remote_description( l->agent, l->offer );
		l->descSet = qtrue;
		Z_Free( l->offer );
		l->offer = NULL;
		for ( i = 0; i < l->numPending; i++ ) {
			juice_add_remote_candidate( l->agent, l->pending[i] );
		}
		l->numPending = 0;
	}
	juice_get_local_description( l->agent, desc, sizeof( desc ) );
	Signal( l, l->hosting ? "answer" : "offer", "sdp", desc );
	juice_gather_candidates( l->agent );
	Com_Printf( "ice: %s %s, %i relay%s%s\n", l->hosting ? "answering" : "calling", l->pid, relay.count, relay.count == 1 ? "" : "s",
		HideIp() ? ", relay only (Hide my IP)" : "" );
	return qtrue;
}


/*
==============================================================================

HOSTING

==============================================================================
*/

static const char *ModeName( int gt ) {
	static const char *modes[] = { "ffa", "duel", "ffa", "tdm", "ctf" };
	return gt >= 0 && gt < (int)ARRAY_LEN( modes ) ? modes[gt] : "ffa";
}


void SV_GetConfigstring( int index, char *buffer, int bufferSize );
#define OZ_CS_PLAYERS	544		// baseq3's CS_PLAYERS (game/bg_public.h), shared by CPMA and Defrag

// the players in the match, bots too (they take slots)
static int PlayerCount( void ) {
	int i, n = 0;
	char buf[MAX_INFO_STRING];
	for ( i = 0; i < Cvar_VariableIntegerValue( "sv_maxclients" ) && i < MAX_CLIENTS; i++ ) {
		SV_GetConfigstring( OZ_CS_PLAYERS + i, buf, sizeof( buf ) );
		if ( buf[0] ) {
			n++;
		}
	}
	return n;
}


#ifdef _WIN32
// oz_upnp.c: the router's forward for an open host
const char	*OzUpnp_Address( void );
void		OzUpnp_Close( void );
void		OzUpnp_Print( void );
#endif

// net_host {name, mode, map, players, max, password, ranked, version, rtt, address, relay}
static void Listing( qboolean force ) {
	cJSON *m;
	char *text;
	const char *address = "";

#ifdef _WIN32
	address = OzUpnp_Address();
#endif
	m = Msg( "net_host" );
	cJSON_AddStringToObject( m, "name", Cvar_VariableString( "sv_hostname" ) );
	cJSON_AddStringToObject( m, "mode", ModeName( Cvar_VariableIntegerValue( "g_gametype" ) ) );
	cJSON_AddStringToObject( m, "map", Cvar_VariableString( "mapname" ) );
	cJSON_AddNumberToObject( m, "players", PlayerCount() );
	cJSON_AddNumberToObject( m, "max", Cvar_VariableIntegerValue( "sv_maxclients" ) );
	cJSON_AddBoolToObject( m, "password", Cvar_VariableString( "g_password" )[0] != '\0' );
	cJSON_AddBoolToObject( m, "ranked", qfalse );
	cJSON_AddStringToObject( m, "version", OZ_P2P_VERSION );
	cJSON_AddNumberToObject( m, "rtt", OzOnline_Rtt() );
	cJSON_AddStringToObject( m, "address", address );
	cJSON_AddBoolToObject( m, "relay", HideIp() );
	text = cJSON_PrintUnformatted( m );
	if ( text && ( force || strcmp( text, host.last ) ) ) {
		Q_strncpyz( host.last, text, sizeof( host.last ) );
		if ( OzOnline_Send( text ) ) {
			Com_Printf( "ice: listed %s on %s, %s\n", Cvar_VariableString( "sv_hostname" ), Cvar_VariableString( "mapname" ),
				address[0] ? va( "open at %s", address ) : "peer-to-peer only" );
		}
	}
	if ( text ) {
		cJSON_free( text );
	}
	cJSON_Delete( m );
}


static void Unhost( void ) {
	int i;
	if ( !host.on ) {
		return;
	}
	host.on = qfalse;
	host.last[0] = '\0';
	SendJson( Msg( "net_unhost" ) );
#ifdef _WIN32
	OzUpnp_Close();
#endif
	for ( i = 0; i < ICE_LINKS; i++ ) {
		if ( links[i].used && links[i].hosting ) {
			FreeLink( &links[i], "no longer hosting" );
		}
	}
	Com_Printf( "ice: listing taken down\n" );
}


// net_request {peer, name, password, version}
static void Request( const cJSON *m ) {
	const cJSON *peer = cJSON_GetObjectItemCaseSensitive( m, "peer" );
	const cJSON *name = cJSON_GetObjectItemCaseSensitive( m, "name" );
	const cJSON *password = cJSON_GetObjectItemCaseSensitive( m, "password" );
	const cJSON *version = cJSON_GetObjectItemCaseSensitive( m, "version" );
	const char *want = Cvar_VariableString( "g_password" ), *refuse = NULL;
	iceLink_t *l;
	cJSON *r;

	if ( !cJSON_IsString( peer ) ) {
		return;
	}
	if ( !host.on || !com_sv_running->integer ) {
		refuse = "That match has ended.";
	} else if ( want[0] && ( !cJSON_IsString( password ) || strcmp( password->valuestring, want ) ) ) {
		refuse = "Wrong password.";
	} else if ( PlayerCount() >= Cvar_VariableIntegerValue( "sv_maxclients" ) ) {
		refuse = "The match is full.";
	} else if ( cJSON_IsString( version ) && version->valuestring[0] && strcmp( version->valuestring, OZ_P2P_VERSION ) ) {
		refuse = "Another version of the game.";
	}
	if ( refuse ) {
		r = Msg( "net_reject" );
		cJSON_AddStringToObject( r, "peer", peer->valuestring );
		cJSON_AddStringToObject( r, "reason", refuse );
		SendJson( r );
		Com_Printf( "ice: refused %s: %s\n", peer->valuestring, refuse );
		return;
	}
	l = FindLink( peer->valuestring );
	if ( l ) {
		FreeLink( l, "joining again" );
	}
	l = NewLink( peer->valuestring, qtrue );
	if ( !l ) {
		return;
	}
	Q_strncpyz( l->name, cJSON_IsString( name ) ? name->valuestring : "Player", sizeof( l->name ) );
	AskRelay();
	r = Msg( "net_accept" );
	cJSON_AddStringToObject( r, "peer", l->pid );
	cJSON_AddNumberToObject( r, "expires_in", relay.have ? MAX( 0, ( relay.expires - Sys_Milliseconds() ) / 1000 ) : 3600 );
	SendJson( r );
	Com_Printf( "ice: %s (%s) is joining\n", l->name, l->pid );
}


/*
==============================================================================

THE HUB'S MESSAGES

==============================================================================
*/

// a candidate's type (host, srflx, prflx, relay), as one word
static const char *TypeOf( const char *candidate ) {
	static char t[2][16];
	static int n;
	const char *p = strstr( candidate, " typ " );
	int i;
	char *out = t[n++ & 1];
	if ( !p ) {
		return "?";
	}
	for ( p += 5, i = 0; p[i] && p[i] != ' ' && i < 15; i++ ) {
		out[i] = p[i];
	}
	out[i] = '\0';
	return out;
}


static void SignalIn( const cJSON *m ) {
	const cJSON *from = cJSON_GetObjectItemCaseSensitive( m, "from" );
	const cJSON *kind = cJSON_GetObjectItemCaseSensitive( m, "kind" );
	iceLink_t *l;

	if ( !cJSON_IsString( from ) || !cJSON_IsString( kind ) ) {
		return;
	}
	l = FindLink( from->valuestring );
	if ( !l ) {
		return;
	}
	if ( !strcmp( kind->valuestring, "offer" ) || !strcmp( kind->valuestring, "answer" ) ) {
		const cJSON *sdp = cJSON_GetObjectItemCaseSensitive( m, "sdp" );
		if ( !cJSON_IsString( sdp ) || l->descSet ) {
			return;
		}
		if ( l->agent ) {
			int i;
			juice_set_remote_description( l->agent, sdp->valuestring );
			l->descSet = qtrue;
			for ( i = 0; i < l->numPending; i++ ) {
				juice_add_remote_candidate( l->agent, l->pending[i] );
			}
			l->numPending = 0;
		} else if ( l->hosting && !l->offer ) {
			l->offer = CopyString( sdp->valuestring );	// StartAgent takes it once the relay's credentials are in
			if ( relay.have && !StartAgent( l ) ) {
				FreeLink( l, "no agent" );
			}
		}
	} else if ( !strcmp( kind->valuestring, "candidate" ) ) {
		const cJSON *c = cJSON_GetObjectItemCaseSensitive( m, "candidate" );
		if ( !cJSON_IsString( c ) ) {
			return;
		}
		if ( oz_ice_debug->integer ) {
			// the type only: what the hub let through (Hide my IP's check)
			Com_Printf( "ice: candidate from %s: typ %s\n", l->pid, TypeOf( c->valuestring ) );
		}
		if ( l->agent && l->descSet ) {
			juice_add_remote_candidate( l->agent, c->valuestring );
		} else if ( l->numPending < ICE_PENDING ) {
			Q_strncpyz( l->pending[l->numPending++], c->valuestring, sizeof( l->pending[0] ) );
		}
	}
}


static void PrintMatches( const cJSON *m ) {
	const cJSON *list = cJSON_GetObjectItemCaseSensitive( m, "matches" ), *e;
	int n = 0;
	cJSON_ArrayForEach( e, list ) {
		const cJSON *h = cJSON_GetObjectItemCaseSensitive( e, "host" ), *name = cJSON_GetObjectItemCaseSensitive( e, "name" );
		const cJSON *map = cJSON_GetObjectItemCaseSensitive( e, "map" ), *mode = cJSON_GetObjectItemCaseSensitive( e, "mode" );
		const cJSON *pl = cJSON_GetObjectItemCaseSensitive( e, "players" ), *mx = cJSON_GetObjectItemCaseSensitive( e, "max" );
		const cJSON *pw = cJSON_GetObjectItemCaseSensitive( e, "password" ), *rl = cJSON_GetObjectItemCaseSensitive( e, "relay" );
		const cJSON *ad = cJSON_GetObjectItemCaseSensitive( e, "address" );
		Com_Printf( "  %s  %s" S_COLOR_WHITE "  %s %s  %i/%i%s%s%s%s\n", cJSON_IsString( h ) ? h->valuestring : "?",
			cJSON_IsString( name ) ? name->valuestring : "?", cJSON_IsString( mode ) ? mode->valuestring : "?",
			cJSON_IsString( map ) ? map->valuestring : "?", cJSON_IsNumber( pl ) ? pl->valueint : 0, cJSON_IsNumber( mx ) ? mx->valueint : 0,
			cJSON_IsTrue( pw ) ? ", password" : "", cJSON_IsTrue( rl ) ? ", relayed" : "",
			cJSON_IsString( ad ) && ad->valuestring[0] ? ", open at " : "", cJSON_IsString( ad ) ? ad->valuestring : "" );
		n++;
	}
	Com_Printf( "ice: %i open match%s\n", n, n == 1 ? "" : "es" );
}


// net_quick {status: found|far|none, mode, region, match?, est?, near, skipped}
static void PrintQuick( const cJSON *m ) {
	const cJSON *status = cJSON_GetObjectItemCaseSensitive( m, "status" ), *mode = cJSON_GetObjectItemCaseSensitive( m, "mode" );
	const cJSON *match = cJSON_GetObjectItemCaseSensitive( m, "match" ), *est = cJSON_GetObjectItemCaseSensitive( m, "est" );
	const cJSON *skipped = cJSON_GetObjectItemCaseSensitive( m, "skipped" );
	const char *s = cJSON_IsString( status ) ? status->valuestring : "?";

	if ( cJSON_IsObject( match ) ) {
		const cJSON *h = cJSON_GetObjectItemCaseSensitive( match, "host" ), *name = cJSON_GetObjectItemCaseSensitive( match, "name" );
		const cJSON *region = cJSON_GetObjectItemCaseSensitive( match, "region" );
		Com_Printf( "ice: quick %s: %s %s %s" S_COLOR_WHITE " in %s, about %i ms\n", cJSON_IsString( mode ) ? mode->valuestring : "", s,
			cJSON_IsString( h ) ? h->valuestring : "?", cJSON_IsString( name ) ? name->valuestring : "?",
			cJSON_IsString( region ) ? region->valuestring : "?", cJSON_IsNumber( est ) ? est->valueint : 0 );
	} else {
		Com_Printf( "ice: quick %s: %s, %i skipped\n", cJSON_IsString( mode ) ? mode->valuestring : "", s,
			cJSON_IsNumber( skipped ) ? skipped->valueint : 0 );
	}
}


/*
==================
OzIce_HubMessage

The hub's match messages (oz_online_engine.c hands every net_* here). qtrue when taken.
==================
*/
qboolean OzIce_HubMessage( const char *type, const cJSON *m ) {
	if ( !strcmp( type, "net_request" ) ) {
		Request( m );
	} else if ( !strcmp( type, "net_accepted" ) ) {
		const cJSON *h = cJSON_GetObjectItemCaseSensitive( m, "host" );
		iceLink_t *l = cJSON_IsString( h ) ? FindLink( h->valuestring ) : NULL;
		if ( l && !l->hosting && !l->agent ) {
			Com_Printf( "ice: %s accepted, connecting\n", l->pid );
			l->accepted = qtrue;
			JoinState( "connecting", l->pid, "" );
			AskRelay();
			if ( relay.have && relay.expires - Sys_Milliseconds() > 60000 && !StartAgent( l ) ) {
				FreeLink( l, "no agent" );
			}
		}
	} else if ( !strcmp( type, "net_refused" ) ) {
		const cJSON *h = cJSON_GetObjectItemCaseSensitive( m, "host" ), *why = cJSON_GetObjectItemCaseSensitive( m, "reason" );
		iceLink_t *l = cJSON_IsString( h ) ? FindLink( h->valuestring ) : NULL;
		Com_Printf( "ice: refused: %s\n", cJSON_IsString( why ) ? why->valuestring : "no reason" );
		if ( l && !l->hosting ) {
			JoinFailed( l, cJSON_IsString( why ) && why->valuestring[0] ? why->valuestring : "The host refused." );
			FreeLink( l, NULL );
		}
	} else if ( !strcmp( type, "net_wait" ) ) {
		Com_Printf( "ice: waiting for the host\n" );
	} else if ( !strcmp( type, "net_ice" ) ) {
		int i;
		TakeRelay( m );
		// links waiting for credentials start now: a joiner's call, a host's answer to an offer that came first
		for ( i = 0; i < ICE_LINKS; i++ ) {
			iceLink_t *l = &links[i];
			if ( l->used && !l->agent && ( ( !l->hosting && l->accepted ) || ( l->hosting && l->offer ) ) && !StartAgent( l ) ) {
				FreeLink( l, "no agent" );
			}
		}
	} else if ( !strcmp( type, "net_signal" ) ) {
		SignalIn( m );
	} else if ( !strcmp( type, "net_gone" ) ) {
		const cJSON *pid = cJSON_GetObjectItemCaseSensitive( m, "pid" );
		iceLink_t *l = cJSON_IsString( pid ) ? FindLink( pid->valuestring ) : NULL;
		if ( cJSON_IsTrue( cJSON_GetObjectItemCaseSensitive( m, "kicked" ) ) ) {
			Com_Printf( "ice: kicked by the host %s: 30 minutes out of its matches\n", cJSON_IsString( pid ) ? pid->valuestring : "?" );
		}
		// a connected link outlives the hub's: the game connection is ours, and it times out by itself
		if ( l && l->state != JUICE_STATE_CONNECTED && l->state != JUICE_STATE_COMPLETED ) {
			JoinFailed( l, "The host left." );
			FreeLink( l, "gone from the hub" );
		}
	} else if ( !strcmp( type, "net_matches" ) ) {
		PrintMatches( m );
	} else if ( !strcmp( type, "net_quick" ) ) {
		PrintQuick( m );
	} else if ( !strcmp( type, "net_follow" ) ) {
		// the party leader hosted or joined a match: the menus follow them in (oz_ui_social.cpp)
		const cJSON *h = cJSON_GetObjectItemCaseSensitive( m, "host" ), *name = cJSON_GetObjectItemCaseSensitive( m, "name" );
		Com_Printf( "ice: the party leader %s" S_COLOR_WHITE " is in %s's match\n", cJSON_IsString( name ) ? name->valuestring : "?",
			cJSON_IsString( h ) ? h->valuestring : "?" );
	} else {
		return qfalse;
	}
	return qtrue;
}


/*
==============================================================================

THE FRAME

==============================================================================
*/

static void LinkConnected( iceLink_t *l ) {
	char local[JUICE_MAX_CANDIDATE_SDP_STRING_LEN], remote[JUICE_MAX_CANDIDATE_SDP_STRING_LEN];

	l->connectedAt = Sys_Milliseconds();
	local[0] = remote[0] = '\0';
	juice_get_selected_candidates( l->agent, local, sizeof( local ), remote, sizeof( remote ) );
	l->relayed = !strcmp( TypeOf( local ), "relay" ) || !strcmp( TypeOf( remote ), "relay" );
	// the types only: the addresses stay out of the console
	Com_sprintf( l->pair, sizeof( l->pair ), "local %s, remote %s", TypeOf( local ), TypeOf( remote ) );
	Com_Printf( "ice: connected to %s, %s (%s) in %i ms\n", l->pid, l->relayed ? "relayed" : "direct", l->pair, l->connectedAt - l->created );
	if ( !l->hosting && !l->connectSent ) {
		l->connectSent = qtrue;
		Cvar_Set( "oz_net_link", l->relayed ? "relayed" : "direct" );
		JoinState( "joined", l->pid, l->relayed ? "relayed" : "direct" );
		// first in the buffer: queued behind a script's waits (or a vstr loop) it would never run
		Cbuf_ExecuteText( EXEC_INSERT, va( "connect ice:%s\n", l->pid ) );
	}
}


static void DrainEvents( void ) {
	for ( ;; ) {
		iceEvent_t e;
		iceLink_t *l;

		Lock( &lock );
		if ( eventTail == eventHead ) {
			Unlock( &lock );
			break;
		}
		e = events[eventTail];
		eventTail = ( eventTail + 1 ) % ICE_EVENTS;
		Unlock( &lock );

		if ( e.kind == EV_LOG ) {
			Com_Printf( "juice: %s\n", e.text );
			continue;
		}
		l = &links[e.slot];
		if ( !l->used || l->gen != e.gen || !l->agent ) {
			continue;
		}
		switch ( e.kind ) {
		case EV_STATE:
			l->state = (juice_state_t)e.state;
			if ( oz_ice_debug->integer ) {
				Com_Printf( "ice: %s %s\n", l->pid, juice_state_to_string( l->state ) );
			}
			if ( ( l->state == JUICE_STATE_CONNECTED || l->state == JUICE_STATE_COMPLETED ) && !l->connectedAt ) {
				LinkConnected( l );
			} else if ( l->state == JUICE_STATE_FAILED ) {
				JoinFailed( l, "No connection could be made between the two PCs." );
				FreeLink( l, l->connectedAt ? "the connection was lost" : "no path between the two PCs" );
			}
			break;
		case EV_CANDIDATE:
			// Hide my IP: only the relay's address leaves this PC (the agent is relay-only too)
			if ( HideIp() && !strstr( e.text, " typ relay" ) ) {
				break;
			}
			Signal( l, "candidate", "candidate", e.text );
			break;
		case EV_GATHERED:
			if ( oz_ice_debug->integer ) {
				Com_Printf( "ice: %s candidates gathered\n", l->pid );
			}
			break;
		default:
			break;
		}
	}
}


/*
==================
OzIce_Frame

Every client frame (CL_OzOnlineFrame), online or not.
==================
*/
void OzIce_Frame( void ) {
	int i, now = Sys_Milliseconds();

	lagMs = oz_ice_lag->integer > 0 ? oz_ice_lag->integer : 0;
	lossPct = oz_ice_loss->integer > 0 ? oz_ice_loss->integer : 0;
	if ( oz_ice_debug->modified ) {
		oz_ice_debug->modified = qfalse;
		juice_set_log_level( oz_ice_debug->integer ? JUICE_LOG_LEVEL_INFO : JUICE_LOG_LEVEL_NONE );
	}
	DrainEvents();

	for ( i = 0; i < ICE_LINKS; i++ ) {
		iceLink_t *l = &links[i];
		if ( !l->used ) {
			continue;
		}
		if ( !l->connectedAt && now - l->created > ICE_CONNECT_MS ) {
			JoinFailed( l, l->accepted ? "The connection didn't come up in time." : "The host didn't answer in time." );
			FreeLink( l, "it didn't connect in time" );
		} else if ( l->connectedAt && now - l->lastRecv > ICE_IDLE_MS ) {
			FreeLink( l, "nothing heard" );
		} else if ( !l->hosting && l->connectSent ) {
			// the joiner's game connection is the link's reason to be: when it ends, so does the link
			if ( cls.state == CA_ACTIVE ) {
				l->wasActive = qtrue;
			} else if ( cls.state == CA_DISCONNECTED && l->wasActive ) {
				cJSON *m = Msg( "net_leave" );
				cJSON_AddStringToObject( m, "peer", l->pid );
				SendJson( m );
				Cvar_Set( "oz_net_link", "" );
				FreeLink( l, "left the match" );
			}
		}
	}

	if ( host.on ) {
		if ( !com_sv_running->integer ) {
			Unhost();
		} else if ( now - host.checkedAt > 1000 ) {
			host.checkedAt = now;
			Listing( qfalse );	// the map, the players, the name: sent when they change
		}
	}
}


/*
==================
OzIce_Kicked

The server kicked a client (SV_DropClient, "was kicked"): on a peer-to-peer link, the hub bans them
from this host's matches for 30 minutes.
==================
*/
void OzIce_Kicked( const netadr_t *adr ) {
	iceLink_t *l = LinkOfAdr( adr );
	cJSON *m;
	if ( !l || !l->hosting ) {
		return;
	}
	m = Msg( "net_kick" );
	cJSON_AddStringToObject( m, "peer", l->pid );
	SendJson( m );
	Com_Printf( "ice: kicked %s: banned from your matches for 30 minutes\n", l->pid );
}


/*
==================
OzIce_Report

A ranked match's result (P8 step 5): {match, me, rows: [{client, score, team}], left, peers}, sent as
match_report {report: <that JSON as text>, sig: base64 RSA-SHA256 PKCS#1 v1.5 of the text} with the
player's identity key. The hub checks the signature against the key the player signed in with, then
its agreement check and Elo take the report as the Godot build's. `tamper` changes the text after
signing (the test's refused case).
==================
*/
qboolean OzIce_Report( const char *match, int me, cJSON *rows, qboolean left, cJSON *peers, qboolean tamper ) {
	char sig[1024];
	char *text;
	cJSON *r = cJSON_CreateObject(), *m;

	cJSON_AddStringToObject( r, "match", match );
	cJSON_AddNumberToObject( r, "me", me );
	cJSON_AddItemToObject( r, "rows", rows ? rows : cJSON_CreateArray() );
	cJSON_AddBoolToObject( r, "left", left );
	cJSON_AddItemToObject( r, "peers", peers ? peers : cJSON_CreateObject() );
	text = cJSON_PrintUnformatted( r );
	cJSON_Delete( r );
	if ( !text ) {
		return qfalse;
	}
	if ( !OzIdentity_Sign( text, sig, sizeof( sig ) ) ) {
		cJSON_free( text );
		Com_Printf( S_COLOR_YELLOW "ice: couldn't sign the report\n" );
		return qfalse;
	}
	if ( tamper ) {
		char *me_ = strstr( text, "\"me\":" );
		if ( me_ ) {
			me_[5] = me_[5] == '1' ? '2' : '1';	// a client number changed after signing
		}
	}
	m = Msg( "match_report" );
	cJSON_AddStringToObject( m, "report", text );
	cJSON_AddStringToObject( m, "sig", sig );
	cJSON_free( text );
	SendJson( m );
	return qtrue;
}


/*
==============================================================================

COMMANDS

==============================================================================
*/

static qboolean Online( void ) {
	if ( OzOnline_Status( NULL, 0 ) != OZ_ONLINE_ONLINE ) {
		Com_Printf( "ice: not online\n" );
		return qfalse;
	}
	return qtrue;
}


static void OzHost_f( void ) {
	if ( !com_sv_running->integer || com_dedicated->integer ) {
		Com_Printf( "oz_host: start a match first (map, Create Match)\n" );
		return;
	}
	if ( !Online() ) {
		return;
	}
	host.on = qtrue;
	host.checkedAt = Sys_Milliseconds();
	// map download (P8 step 6): the joiners get a map they lack over the game connection, at Quake3e's top
	// rate rather than its 100 KB/s default, or from the HTTP sv_dlURL when the host sets one
	Cvar_Set( "sv_allowDownload", "1" );
	if ( Cvar_VariableIntegerValue( "sv_dlRate" ) < 500 ) {
		Cvar_Set( "sv_dlRate", "500" );
	}
	// Quake3e's sv_noReferencedPaks 1 (its default) tells clients nothing about the paks they lack, so they
	// download nothing: off, and the running map's list made now (SV_SpawnServer makes it at the next map)
	if ( Cvar_VariableIntegerValue( "sv_noReferencedPaks" ) ) {
		Cvar_Set( "sv_noReferencedPaks", "0" );
		Cvar_Set( "sv_referencedPakNames", FS_ReferencedPakNames() );
		Cvar_Set( "sv_referencedPaks", FS_ReferencedPakChecksums() );
	}
	Listing( qtrue );
}


static void OzUnhost_f( void ) {
	Unhost();
}


static void OzBrowse_f( void ) {
	if ( Online() ) {
		cJSON *m = Msg( "net_browse" );
		if ( Region()[0] ) {
			cJSON_AddStringToObject( m, "region", Region() );	// its matches first
		}
		SendJson( m );
	}
}


// oz_quick <mode>: net_quick {mode, region?, version, rtt}; the hub picks among the open matches a join
// would get into (not full, no password, this version), by its ping guess from both sides' regions and
// hub round trips
static void OzQuick_f( void ) {
	cJSON *m;

	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "usage: oz_quick <mode: ffa, duel, tdm, ctf>\n" );
		return;
	}
	if ( !Online() ) {
		return;
	}
	m = Msg( "net_quick" );
	cJSON_AddStringToObject( m, "mode", Cmd_Argv( 1 ) );
	if ( Region()[0] ) {
		cJSON_AddStringToObject( m, "region", Region() );
	}
	cJSON_AddStringToObject( m, "version", OZ_P2P_VERSION );
	cJSON_AddNumberToObject( m, "rtt", OzOnline_Rtt() );
	SendJson( m );
	Com_Printf( "ice: asking for the best %s match%s\n", Cmd_Argv( 1 ), Region()[0] ? va( " in %s", Region() ) : "" );
}


static void OzJoin_f( void ) {
	const char *pid = Cmd_Argv( 1 );
	iceLink_t *l;
	cJSON *m;

	if ( Cmd_Argc() < 2 || !pid[0] ) {
		Com_Printf( "usage: oz_join <host's player id> [password]\n" );
		return;
	}
	if ( !Online() ) {
		JoinState( "failed", pid, "The online service isn't connected." );
		return;
	}
	l = FindLink( pid );
	if ( l ) {
		FreeLink( l, "joining again" );
	}
	l = NewLink( pid, qfalse );
	if ( !l ) {
		return;
	}
	Q_strncpyz( joinPassword, Cmd_Argv( 2 ), sizeof( joinPassword ) );
	m = Msg( "net_join" );
	cJSON_AddStringToObject( m, "host", pid );
	cJSON_AddStringToObject( m, "password", joinPassword );
	cJSON_AddStringToObject( m, "version", OZ_P2P_VERSION );
	SendJson( m );
	Cvar_Set( "oz_net_link", "" );
	JoinState( "asking", pid, "" );
	Com_Printf( "ice: asking %s to join%s\n", pid, HideIp() ? " (Hide my IP: relayed)" : "" );
}


// oz_unjoin: gives up joining: the links whose game connection isn't up (the browser's Cancel, Escape
// on the connection screen); the host hears it (net_leave)
static void OzUnjoin_f( void ) {
	int i;
	for ( i = 0; i < ICE_LINKS; i++ ) {
		iceLink_t *l = &links[i];
		if ( l->used && !l->hosting && !( l->wasActive && cls.state == CA_ACTIVE ) ) {
			cJSON *m = Msg( "net_leave" );
			cJSON_AddStringToObject( m, "peer", l->pid );
			SendJson( m );
			FreeLink( l, "given up" );
		}
	}
	Cvar_Set( "oz_net_link", "" );
	JoinState( "", "", "" );
}


/*
==================
OzIce_JoinString

The match this player is in, as a friend's Join button reaches it (presence's join): ice:<host> for a
peer-to-peer match, ours while we host it, " locked" after it when it takes a password. Empty
otherwise: a server's address isn't passed on (a home server's would be its owner's IP).
==================
*/
const char *OzIce_JoinString( void ) {
	static char s[64];
	int i;

	if ( host.on && com_sv_running->integer && !com_dedicated->integer ) {
		Com_sprintf( s, sizeof( s ), "ice:%s%s", OzIdentity_PlayerId(), Cvar_VariableString( "g_password" )[0] ? " locked" : "" );
		return s;
	}
	for ( i = 0; i < ICE_LINKS; i++ ) {
		const iceLink_t *l = &links[i];
		if ( l->used && !l->hosting && l->wasActive && cls.state == CA_ACTIVE ) {
			Com_sprintf( s, sizeof( s ), "ice:%s%s", l->pid, joinPassword[0] ? " locked" : "" );
			return s;
		}
	}
	return "";
}


// oz_ice_report <match> | tamper: a signed report with no rows (the hub's signature check; tests)
static void OzIceReport_f( void ) {
	const char *match = Cmd_Argv( 1 );
	qboolean tamper = !Q_stricmp( match, "tamper" );
	if ( !match[0] ) {
		Com_Printf( "usage: oz_ice_report <match id> | tamper\n" );
		return;
	}
	if ( Online() && OzIce_Report( match, 0, NULL, qfalse, NULL, tamper ) ) {
		Com_Printf( "ice: report %s sent\n", match );
	}
}


#ifdef _WIN32
static void OzUpnp_f( void ) {
	OzUpnp_Print();
}
#endif


static void OzIce_f( void ) {
	int i, n = 0, now = Sys_Milliseconds();
	for ( i = 0; i < ICE_LINKS; i++ ) {
		const iceLink_t *l = &links[i];
		if ( !l->used ) {
			continue;
		}
		Com_Printf( "  %s %s %s: %s%s%s, %i s\n", l->hosting ? "joiner" : "host", l->pid, l->name,
			l->agent ? juice_state_to_string( l->state ) : "waiting", l->connectedAt ? ( l->relayed ? ", relayed" : ", direct" ) : "",
			l->connectedAt ? va( " (%s)", l->pair ) : "", ( now - l->created ) / 1000 );
		n++;
	}
	Com_Printf( "ice: %i link%s%s; relay %s%s\n", n, n == 1 ? "" : "s", host.on ? ", hosting" : "",
		relay.have ? va( "%i server%s", relay.count, relay.count == 1 ? "" : "s" ) : "not asked", HideIp() ? ", Hide my IP on" : "" );
}


// oz_iceserver <port> <user> <password>: libjuice's TURN server in this process (tests; 0 stops it)
static void OzIceServer_f( void ) {
	static char user[64], pass[64];
	juice_server_credentials_t cred;
	juice_server_config_t cfg;
	int port = atoi( Cmd_Argv( 1 ) );

	if ( testServer ) {
		juice_server_destroy( testServer );
		testServer = NULL;
		Com_Printf( "ice: test relay stopped\n" );
	}
	if ( port <= 0 ) {
		return;
	}
	Q_strncpyz( user, Cmd_Argv( 2 ), sizeof( user ) );
	Q_strncpyz( pass, Cmd_Argv( 3 ), sizeof( pass ) );
	Com_Memset( &cred, 0, sizeof( cred ) );
	cred.username = user;
	cred.password = pass;
	cred.allocations_quota = 16;
	Com_Memset( &cfg, 0, sizeof( cfg ) );
	cfg.credentials = &cred;
	cfg.credentials_count = 1;
	cfg.max_allocations = 64;
	cfg.max_peers = 16;
	cfg.bind_address = "127.0.0.1";
	cfg.external_address = "127.0.0.1";
	cfg.port = (uint16_t)port;
	cfg.relay_port_range_begin = (uint16_t)( port + 1 );
	cfg.relay_port_range_end = (uint16_t)( port + 200 );
	cfg.realm = "oz";
	testServer = juice_server_create( &cfg );
	Com_Printf( testServer ? "ice: test relay on 127.0.0.1:%i\n" : S_COLOR_YELLOW "ice: test relay couldn't start on %i\n",
		testServer ? juice_server_get_port( testServer ) : port );
}


void OzIce_Init( void ) {
	if ( !lockReady ) {
		LockInit( &lock );
		packets = malloc( sizeof( icePacket_t ) * ICE_PACKETS );
		lockReady = qtrue;
		juice_set_log_handler( CbLog );
		juice_set_log_level( JUICE_LOG_LEVEL_NONE );
	}
	oz_network_hide_ip = Cvar_Get( "oz_network_hide_ip", "0", CVAR_ARCHIVE );
	Cvar_SetDescription( oz_network_hide_ip, "Hide my IP (Settings > Network): peer-to-peer matches connect through the relay only." );
	oz_net_stun = Cvar_Get( "oz_net_stun", "stun.cloudflare.com:3478", CVAR_ARCHIVE );
	Cvar_SetDescription( oz_net_stun, "The STUN server peer-to-peer links learn their public address from (host:port); empty for none." );
	oz_net_turn = Cvar_Get( "oz_net_turn", "", 0 );
	Cvar_SetDescription( oz_net_turn, "Extra TURN relays for peer-to-peer links, user:password@host:port[,...]." );
	oz_ice_lag = Cvar_Get( "oz_ice_lag", "0", 0 );
	Cvar_SetDescription( oz_ice_lag, "Added one-way latency (ms) on what peer-to-peer links receive (tests)." );
	oz_ice_loss = Cvar_Get( "oz_ice_loss", "0", 0 );
	Cvar_SetDescription( oz_ice_loss, "Percent of what peer-to-peer links receive that is dropped (tests)." );
	oz_ice_debug = Cvar_Get( "oz_ice_debug", "0", 0 );
	oz_ice_debug->modified = qtrue;
	oz_net_link = Cvar_Get( "oz_net_link", "", 0 );
	Cvar_SetDescription( oz_net_link, "The joined peer-to-peer match's link: direct or relayed (empty when none)." );
	Cmd_AddCommand( "oz_host", OzHost_f );
	Cmd_AddCommand( "oz_unhost", OzUnhost_f );
	Cmd_AddCommand( "oz_browse", OzBrowse_f );
	Cmd_AddCommand( "oz_quick", OzQuick_f );
	Cmd_AddCommand( "oz_join", OzJoin_f );
	Cmd_AddCommand( "oz_unjoin", OzUnjoin_f );
	Cmd_AddCommand( "oz_ice", OzIce_f );
	Cmd_AddCommand( "oz_iceserver", OzIceServer_f );
	Cmd_AddCommand( "oz_ice_report", OzIceReport_f );
#ifdef _WIN32
	Cmd_AddCommand( "oz_upnp", OzUpnp_f );
#endif
}


void OzIce_Shutdown( qboolean quitting ) {
	int i;
	Cmd_RemoveCommand( "oz_host" );
	Cmd_RemoveCommand( "oz_unhost" );
	Cmd_RemoveCommand( "oz_browse" );
	Cmd_RemoveCommand( "oz_quick" );
	Cmd_RemoveCommand( "oz_join" );
	Cmd_RemoveCommand( "oz_unjoin" );
	Cmd_RemoveCommand( "oz_ice" );
	Cmd_RemoveCommand( "oz_iceserver" );
	Cmd_RemoveCommand( "oz_upnp" );
	Cmd_RemoveCommand( "oz_ice_report" );
	if ( !quitting ) {
		return;		// a client restart (a mod switch) keeps the links: the connection carries on
	}
	for ( i = 0; i < ICE_LINKS; i++ ) {
		FreeLink( &links[i], NULL );
	}
	if ( testServer ) {
		juice_server_destroy( testServer );
		testServer = NULL;
	}
}
