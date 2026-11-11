/*
===========================================================================
Oz Arena: the community servers (ENGINE_PLAN.md P8b step 1). GPL-2.0.

Every server the master servers list, for both protocols, asked what it runs: the Server Browser's
rows (oz_ui_servers.cpp). The lists are Quake3e's own: each master (sv_master1..5) is asked with
getservers 68 and 71 ("full empty", so it leaves none out), and CL_ServersResponsePacket keeps the
addresses in cls.globalServers, without duplicates, as the globalservers command has them. A
master's name is looked up once a run, one a frame (the lookup blocks).

Asking the servers is this file's: Quake3e's ping queue (CL_UpdateVisiblePings_f) holds 32 requests
and waits cl_maxPing (800 ms) on each server that doesn't answer, so the 1,700 servers the masters
listed on 2026-10-31 (a third never answer) took half a minute, and it keeps 31 characters of a
server's name. This sends getinfo to all of them at 500 a second, asks again after 1 s without an
answer and gives up 1 s after that, and keeps each infoResponse whole (cl_main.c
CL_ServerInfoPacket hands it over past its protocol check: 68 or 71) with the time it took, its
ping. A sweep of every master's list is over in about 6 s.

The player's saved servers (favorites, history: oz_ui_servers.cpp) are asked with them (OzServers_Also),
so one no master lists shows live too; only an address a master listed counts as public
(OzServers_Listed: what presence may name). A server's status (its players) is Quake3e's getstatus
(CL_ServerStatus). The LAN tab is Quake3e's own broadcast (localservers: getinfo to ports
27960-27963), its answers kept here for LAN_WAIT.

The answers reach the client only while no server runs on this PC: net_ip.c hands every packet to a
running server, as it did for id's browser.

  oz_servers			the sweep's state and every server that answered
  oz_servers refresh	asks the masters again, then every server they list
  oz_servers_ask		auto (the default): the Server Browser asks the masters as it opens and with
						Refresh, from the player's own user folder only (tests with their own
						fs_homepath never ask the internet's servers unless they say so); 1: from
						anywhere; 0: never (oz_servers refresh still asks)
===========================================================================
*/
#include "../../../quake3e/code/client/client.h"
#include "oz_servers.h"

#define SWEEP_RATE		0.5f	// getinfo requests a millisecond
#define SWEEP_BURST		64.0f	// at most in one frame
#define ANSWER_WAIT		1000	// ms before asking a server again, and before giving up after that
#define SWEEP_TRIES		2
#define MASTER_WAIT		3000	// ms the masters get to send their lists

#define LAN_WAIT		2000	// ms the LAN's servers get to answer the broadcast

typedef struct {
	netadr_t	adr;
	char		address[NET_ADDRSTRMAXLEN + 8];
	int			sent;		// Sys_Milliseconds of the last getinfo
	int			first;		// of the first one: the ping counts from it unless the answer is the second's
	int			tries;
	int			ping;
	char		*info;		// its infoResponse, NULL until it answers
	qboolean	listed;		// a master listed it (not only the player's favorites or history)
} ozServer_t;

static ozServer_t	servers[MAX_GLOBAL_SERVERS];
static int			numServers;		// the servers asked: the player's saved ones, then the masters' in cls.globalServers' order
static int			numSaved;		// the first numSaved are the player's saved ones (OzServers_Also)
static int			numTaken;		// cls.globalServers' addresses taken so far
static int			nextAsk;		// the next server asked for the first time

static ozServer_t	lan[MAX_OTHER_SERVERS];	// the servers that answered the LAN broadcast
static int			numLan;
static int			lanSent;		// Sys_Milliseconds of the broadcast
static ozServersState_t	lanState;
static int			numAnswered;
static int			generation;

static ozServersState_t	state;
static qboolean		active;
static int			refreshTime;	// Sys_Milliseconds of the last refresh
static int			lastFrame;
static float		credit;			// requests the pace allows now

static struct {
	char		name[MAX_CVAR_VALUE_STRING];
	netadr_t	adr;
	qboolean	ok;
} masters[MAX_MASTER_SERVERS];
static int			mastersDone;	// sv_master1..mastersDone handled this refresh
static int			lastMasterAsk;

static cvar_t		*oz_servers_ask;
static char			modList[16384];	// FS_GetFileList's $modlist: name, description, name...
static int			numMods = -1;


static void Clear( void ) {
	int i;

	for ( i = 0; i < numServers; i++ ) {
		if ( servers[i].info ) {
			Z_Free( servers[i].info );
		}
	}
	Com_Memset( servers, 0, sizeof( servers[0] ) * numServers );
	numServers = numSaved = numTaken = nextAsk = numAnswered = 0;
	generation++;
}

static ozServer_t *Known( const netadr_t *adr, int count ) {
	int i;

	for ( i = 0; i < count; i++ ) {
		if ( NET_CompareAdr( adr, &servers[i].adr ) ) {
			return &servers[i];
		}
	}
	return NULL;
}

static ozServer_t *Add( const netadr_t *adr, qboolean listed ) {
	ozServer_t *s;

	if ( numServers >= MAX_GLOBAL_SERVERS ) {
		return NULL;
	}
	s = &servers[numServers++];
	s->adr = *adr;
	s->listed = listed;
	Q_strncpyz( s->address, NET_AdrToStringwPort( adr ), sizeof( s->address ) );
	return s;
}


/*
==================
OzServers_Also

A server the masters may not list (a favorite, one joined before): asked in this sweep with theirs
==================
*/
void OzServers_Also( const char *address ) {
	netadr_t adr;
	const int r = NET_StringToAdr( address, &adr, NA_IP );

	if ( !r || !active ) {
		return;
	}
	if ( r == 2 ) {
		adr.port = BigShort( PORT_SERVER );
	}
	if ( !Known( &adr, numServers ) && Add( &adr, qfalse ) ) {
		numSaved = numServers;
		generation++;
	}
}


/*
==================
OzServers_Refresh
==================
*/
void OzServers_Refresh( void ) {
	Clear();
	numMods = -1;	// a mod installed since is found
	refreshTime = lastFrame = lastMasterAsk = Sys_Milliseconds();
	credit = 0;
	mastersDone = 0;
	if ( com_sv_running->integer ) {
		state = OZS_LOCAL_SERVER;
		active = qfalse;
		return;
	}
	state = OZS_MASTERS;
	active = qtrue;
	// the masters' answers start the list afresh (CL_ServersResponsePacket), as globalservers has it
	cls.numglobalservers = -1;
	cls.pingUpdateSource = AS_GLOBAL;
}


// the next master with a name: looked up once a run (the first lookup a frame), asked for both protocols
static void AskMasters( void ) {
	while ( mastersDone < MAX_MASTER_SERVERS ) {
		const int i = mastersDone++;
		const char *name = Cvar_VariableString( va( "sv_master%d", i + 1 ) );
		qboolean lookedUp = qfalse;
		int r;

		if ( !*name ) {
			continue;
		}
		// a lookup that failed is tried again at the next refresh: kept as failed for the run, a browser
		// opened before the network was up stayed empty until the game was restarted
		if ( strcmp( masters[i].name, name ) || !masters[i].ok ) {
			Q_strncpyz( masters[i].name, name, sizeof( masters[i].name ) );
			r = NET_StringToAdr( name, &masters[i].adr, NA_IP );
			if ( r == 2 ) {
				masters[i].adr.port = BigShort( PORT_MASTER );
			}
			masters[i].ok = r != 0;
			if ( !r ) {
				Com_Printf( "oz_servers: couldn't look up the master server %s\n", name );
			}
			lookedUp = qtrue;
		}
		if ( masters[i].ok ) {
			NET_OutOfBandPrint( NS_CLIENT, &masters[i].adr, "getservers %d full empty", OLD_PROTOCOL_VERSION );
			NET_OutOfBandPrint( NS_CLIENT, &masters[i].adr, "getservers %d full empty", NEW_PROTOCOL_VERSION );
			lastMasterAsk = Sys_Milliseconds();
		}
		if ( lookedUp ) {
			return;
		}
	}
}


// the addresses the masters' lists brought since the last frame (a saved server among them: listed too)
static void TakeAddresses( void ) {
	const int n = cls.numglobalservers > MAX_GLOBAL_SERVERS ? MAX_GLOBAL_SERVERS : cls.numglobalservers;

	if ( numTaken >= n ) {
		return;		// none new (or the list was reset: a mod switch restarts the client)
	}
	for ( ; numTaken < n; numTaken++ ) {
		// Quake3e's list has no duplicates (CL_ServersResponsePacket's hash): only the saved ones may be there already
		ozServer_t *s = Known( &cls.globalServers[numTaken].adr, numSaved );
		if ( s ) {
			s->listed = qtrue;
		} else {
			Add( &cls.globalServers[numTaken].adr, qtrue );
		}
	}
	if ( state == OZS_MASTERS ) {
		state = OZS_SERVERS;
	}
	generation++;
}


// The second getinfo's challenge tells its answer from a late answer to the first (a server echoes
// it): a server slower than ANSWER_WAIT answered the first one just after the second went out, and its
// ping, counted from the second, read a second too low.
#define SECOND_ASK		"xx2"

static void Ask( ozServer_t *s, int now ) {
	NET_OutOfBandPrint( NS_CLIENT, &s->adr, s->tries ? "getinfo " SECOND_ASK : "getinfo xxx" );
	if ( !s->tries ) {
		s->first = now;
	}
	s->sent = now;
	s->tries++;
	credit -= 1.0f;
}


/*
==================
OzServers_Frame
==================
*/
void OzServers_Frame( void ) {
	static qboolean wasConnecting;
	int now, i;
	qboolean pending, connecting;

	now = Sys_Milliseconds();
	if ( lanState == OZS_SERVERS && now - lanSent > LAN_WAIT ) {
		lanState = OZS_DONE;
		generation++;
		Com_Printf( "oz_servers: %d on the LAN\n", numLan );
	}
	// a connection that begins, or a server coming up on this PC, ends the sweep with what it has: it
	// went on sending getinfo at its pace through the join, and a local server takes every answer
	connecting = cls.state >= CA_CONNECTING && cls.state != CA_CINEMATIC;
	if ( active && ( com_sv_running->integer || ( connecting && !wasConnecting ) ) ) {
		active = qfalse;
		state = OZS_DONE;
		generation++;
		Com_Printf( "oz_servers: the sweep stopped for a %s, %d of %d had answered\n",
			com_sv_running->integer ? "server on this PC" : "connection", numAnswered, numServers );
	}
	wasConnecting = connecting;
	if ( !active ) {
		return;
	}
	credit += ( now - lastFrame ) * SWEEP_RATE;
	if ( credit > SWEEP_BURST ) {
		credit = SWEEP_BURST;
	}
	lastFrame = now;

	AskMasters();
	TakeAddresses();

	// the servers that didn't answer the first time, then the ones not asked yet
	pending = qfalse;
	for ( i = 0; i < nextAsk; i++ ) {
		ozServer_t *s = &servers[i];
		if ( s->info || now - s->sent < ANSWER_WAIT ) {
			pending |= !s->info;
			continue;
		}
		if ( s->tries < SWEEP_TRIES ) {
			pending = qtrue;
			if ( credit >= 1.0f ) {
				Ask( s, now );
			}
		}
	}
	while ( nextAsk < numServers && credit >= 1.0f ) {
		Ask( &servers[nextAsk++], now );
	}

	if ( pending || nextAsk < numServers || mastersDone < MAX_MASTER_SERVERS || now - lastMasterAsk < MASTER_WAIT ) {
		return;
	}
	active = qfalse;
	state = numServers ? OZS_DONE : OZS_NO_MASTERS;
	generation++;
	Com_Printf( "oz_servers: %d listed, %d answered (%.1f s)\n", numServers, numAnswered, ( now - refreshTime ) / 1000.0f );
}


/*
==================
OzServers_InfoResponse

A server's infoResponse (cl_main.c CL_ServerInfoPacket, of a protocol this client plays)
==================
*/
void OzServers_InfoResponse( const netadr_t *from, const char *info ) {
	ozServer_t *s;
	int i;

	for ( i = 0; i < nextAsk; i++ ) {
		s = &servers[i];
		if ( !s->info && NET_CompareAdr( from, &s->adr ) ) {
			const qboolean second = s->tries > 1 && !strcmp( Info_ValueForKey( info, "challenge" ), SECOND_ASK );
			s->ping = Sys_Milliseconds() - ( second ? s->sent : s->first );
			if ( s->ping < 1 ) {
				s->ping = 1;
			}
			s->info = CopyString( info );
			numAnswered++;
			generation++;
			return;
		}
	}
	// not asked by the sweep (or answered it already): an answer to the LAN broadcast, while it's open
	if ( lanState != OZS_SERVERS || numLan >= MAX_OTHER_SERVERS ) {
		return;
	}
	// an internet server the sweep asks may answer both of its getinfos while the LAN tab listens: its
	// second answer isn't a LAN server's
	if ( !Sys_IsLANAddress( from ) && Known( from, numServers ) ) {
		return;
	}
	for ( i = 0; i < numLan; i++ ) {
		if ( NET_CompareAdr( from, &lan[i].adr ) ) {
			return;		// the broadcast goes out twice
		}
	}
	s = &lan[numLan++];
	s->adr = *from;
	Q_strncpyz( s->address, NET_AdrToStringwPort( from ), sizeof( s->address ) );
	s->ping = Sys_Milliseconds() - lanSent;
	s->ping = s->ping < 1 ? 1 : s->ping;
	s->info = CopyString( info );
	s->listed = qfalse;
	generation++;
}


/*
==================
OzServers_LanRefresh

The LAN tab: Quake3e's localservers broadcasts getinfo to the server ports (27960-27963) twice; the
answers that come within LAN_WAIT are the LAN's servers
==================
*/
void OzServers_LanRefresh( void ) {
	int i;

	for ( i = 0; i < numLan; i++ ) {
		Z_Free( lan[i].info );
	}
	Com_Memset( lan, 0, sizeof( lan[0] ) * numLan );
	numLan = 0;
	generation++;
	if ( com_sv_running->integer ) {
		lanState = OZS_LOCAL_SERVER;
		return;
	}
	lanSent = Sys_Milliseconds();
	lanState = OZS_SERVERS;
	Cmd_ExecuteString( "localservers" );
}

ozServersState_t OzServers_LanState( void ) {
	return lanState;
}

int OzServers_LanCount( void ) {
	return numLan;
}

int OzServers_LanGet( int index, ozServerInfo_t *out ) {
	if ( index < 0 || index >= numLan ) {
		return 0;
	}
	out->address = lan[index].address;
	out->info = lan[index].info;
	out->ping = lan[index].ping;
	return 1;
}


ozServersState_t OzServers_State( int *listed, int *asked, int *answered ) {
	if ( listed ) {
		*listed = numServers;
	}
	if ( asked ) {
		*asked = nextAsk;
	}
	if ( answered ) {
		*answered = numAnswered;
	}
	return state;
}

int OzServers_Age( void ) {
	return state == OZS_IDLE ? -1 : Sys_Milliseconds() - refreshTime;
}

int OzServers_Generation( void ) {
	return generation;
}

int OzServers_Count( void ) {
	return numServers;
}

int OzServers_Get( int index, ozServerInfo_t *out ) {
	if ( index < 0 || index >= numServers || !servers[index].info ) {
		return 0;
	}
	out->address = servers[index].address;
	out->info = servers[index].info;
	out->ping = servers[index].ping;
	return 1;
}

int OzServers_Listed( const char *address ) {
	int i;

	for ( i = 0; i < numServers; i++ ) {
		if ( servers[i].listed && !strcmp( servers[i].address, address ) ) {
			return 1;
		}
	}
	return 0;
}

/*
A server's status: its serverinfo and players, through Quake3e's own request (CL_ServerStatus, which
sends getstatus again every cl_serverStatusResendTime until the answer comes). Its string is the
info, "\\", then "\\<score> <ping> \"<name>\"" for each player, then "\\". Asking a server it has
answered before returns that answer again: again forgets every answer first.
*/
int OzServers_PlayerStatus( const char *address, char *buf, int size, int again ) {
	if ( again ) {
		CL_ServerStatus( NULL, NULL, 0 );
	}
	return CL_ServerStatus( address, buf, size );
}

int OzServers_MayAsk( void ) {
	if ( !Q_stricmp( oz_servers_ask->string, "auto" ) ) {
		return !Q_stricmp( Cvar_VariableString( "fs_homepath" ), Sys_DefaultHomePath() );
	}
	return oz_servers_ask->integer != 0;
}

void OzServers_ForgetMods( void ) {
	numMods = -1;
	generation++;	// the rows' "have" again
}

int OzServers_HaveMod( const char *game ) {
	const char *p;
	int i;

	if ( !*game || !Q_stricmp( game, BASEGAME ) || !Q_stricmp( game, DEFAULT_GAME ) ) {
		return 1;
	}
	if ( numMods < 0 ) {
		numMods = FS_GetFileList( "$modlist", "", modList, sizeof( modList ) );
	}
	for ( i = 0, p = modList; i < numMods; i++ ) {
		if ( !Q_stricmp( p, game ) ) {
			return 1;
		}
		p += strlen( p ) + 1;	// the name, then its description
		p += strlen( p ) + 1;
	}
	return 0;
}


static void OzServers_f( void ) {
	static const char *names[] = { "never asked", "asking the master servers", "asking the servers", "done",
		"no master server answered", "a server runs on this PC" };
	int i;

	if ( !Q_stricmp( Cmd_Argv( 1 ), "refresh" ) ) {
		OzServers_Refresh();
		return;
	}
	Com_Printf( "oz_servers: %s, %d listed, %d asked, %d answered\n", names[state], numServers, nextAsk, numAnswered );
	for ( i = 0; i < numServers; i++ ) {
		const ozServer_t *s = &servers[i];
		const char *game;
		if ( !s->info ) {
			continue;
		}
		game = Info_ValueForKey( s->info, "game" );
		Com_Printf( "%-22s %4d ms  %-10s %-14s %2s/%-2s  %s\n", s->address, s->ping, *game ? game : BASEGAME,
			Info_ValueForKey( s->info, "mapname" ), Info_ValueForKey( s->info, "clients" ),
			Info_ValueForKey( s->info, "sv_maxclients" ), Info_ValueForKey( s->info, "hostname" ) );
	}
}


void OzServers_Init( void ) {
	static qboolean done;

	if ( done ) {
		return;
	}
	done = qtrue;
	oz_servers_ask = Cvar_Get( "oz_servers_ask", "auto", 0 );
	Cvar_SetDescription( oz_servers_ask, "Whether the Server Browser asks the master servers: auto (only from the player's own user folder), 1 or 0." );
	Cmd_AddCommand( "oz_servers", OzServers_f );
}
