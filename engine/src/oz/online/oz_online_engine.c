/*
===========================================================================
Oz Arena: the client's side of the online services (ENGINE_PLAN.md P7). GPL-2.0.

Starts the hub connection (hub.cpp) with this PC's identity (identity.cpp), keeps the hub told what
the player is doing (presence) and who they are (profile), and hands what the hub says to the menus
(the native UI's Friends panel, party and Leaderboards; oz_ui_social.cpp).

  oz_network_online			Settings > Network > Online services (1, the default)
  oz_network_online_server	Settings > Network > Online server: empty is wss://arena-online.fragnet.workers.dev
							(ws://127.0.0.1:8787 is a local hub)
  oz_online			auto (the default): online only when the game runs from the player's own user folder
					(%APPDATA%\Oz Arena), so a test with its own fs_homepath never takes the player's identity
					online; 1: online wherever (tests, with a local hub); 0: offline
  oz_online_import	1: a new identity is first looked for in the Godot build's user folder (or this file)

  online			prints the connection, the player id and friend code
  hub <type> [<key> <value>]...	sends the hub a message, its values strings: hub friend_add code ABCD-1234
  party_accept		joins the party of the newest invite
  oz_link <link>	ozarena://friend/<code> (adds a friend), ozarena://join/<host> (joins a peer-to-peer
					match, the Server Browser's Link) or ozarena://connect/<address>
===========================================================================
*/
#include "../../../quake3e/code/client/client.h"
#include "oz_online.h"
#include "oz_stats.h"
#include "cJSON.h"

#define OZ_ONLINE_DEFAULT_SERVER	"wss://arena-online.fragnet.workers.dev"
#define OZ_ONLINE_MSG_MAX			( 512 * 1024 )

#ifdef USE_OZ_UI
#include "../ui/oz_progress.h"
void OzUI_OnlineStatus( int status, const char *reason );
void OzUI_HubMessage( const char *type, const cJSON *msg );
static int		careerSent = -1;	// the career's version the hub last heard (P12)
#endif
#ifdef USE_OZ_ICE
// peer-to-peer play (oz_ice.c, ENGINE_PLAN.md P8)
void		OzIce_Init( void );
void		OzIce_Shutdown( qboolean quitting );
void		OzIce_Frame( void );
qboolean	OzIce_HubMessage( const char *type, const cJSON *m );
const char	*OzIce_JoinString( void );
#endif

void OzUpdate_Init( void );					// oz_update.c: the update check
void OzUpdate_Frame( void );
void OzUpdate_ClientShutdown( qboolean quit );

static cvar_t	*oz_online, *oz_online_import, *oz_online_debug;
static cvar_t	*oz_network_online, *oz_network_online_server;		// the Settings page's (engine/ui/settings.json)
static int		lastStatus = -1;
static char		lastPresence[512];
static int		nameModified = -1;
static qboolean	started;
static qboolean	decided;	// whether to start was asked since the cvars last changed
static char		friendCode[16];
static int		friendsCount, friendsOnline, partySize, incomingCount;
static char		newestInvite[64];	// the newest party invite's party (party_accept)
static char		pendingFriend[16];	// a friend link's code, added once online
static char		pendingJoin[24];	// a join link's host, joined once online
static char		msgBuf[OZ_ONLINE_MSG_MAX];


static const char *Server( void ) {
	return oz_network_online_server->string[0] ? oz_network_online_server->string : OZ_ONLINE_DEFAULT_SERVER;
}


static qboolean OnlineWanted( void ) {
	if ( !oz_network_online->integer ) {
		return qfalse;
	}
	if ( !Q_stricmp( oz_online->string, "auto" ) ) {
		// the player's own user folder: a test's scratch fs_homepath never takes the player's identity online
		const char *home = Cvar_VariableString( "fs_homepath" );
		return !Q_stricmp( home, Sys_DefaultHomePath() );
	}
	return oz_online->integer != 0;
}


static void Start( void ) {
	char keyPath[MAX_OSPATH], importPath[MAX_OSPATH], error[256];
	const char *appdata = getenv( "APPDATA" );

	Com_sprintf( keyPath, sizeof( keyPath ), "%s%conline%cidentity.key", Cvar_VariableString( "fs_homepath" ), PATH_SEP, PATH_SEP );
	importPath[0] = '\0';
	if ( oz_online_import->string[0] && strcmp( oz_online_import->string, "0" ) && strcmp( oz_online_import->string, "1" ) ) {
		Q_strncpyz( importPath, oz_online_import->string, sizeof( importPath ) );	// a key file to import (tests)
	} else if ( oz_online_import->integer && appdata && appdata[0] ) {
		Com_sprintf( importPath, sizeof( importPath ), "%s\\Godot\\app_userdata\\Q3Remaster\\online\\identity.key", appdata );
	}
	if ( !OzIdentity_Load( keyPath, importPath, error, sizeof( error ) ) ) {
		Com_Printf( S_COLOR_YELLOW "online: %s\n", error );
		return;
	}
	OzOnline_Start( Server(), Cvar_VariableString( "name" ), Cvar_VariableString( "model" ), "#db2410" );
	started = qtrue;
	lastStatus = -1;
	lastPresence[0] = '\0';
	nameModified = Cvar_Get( "name", "", 0 )->modificationCount;
}


static void Stop( void ) {
	if ( started ) {
		OzOnline_Stop();
		started = qfalse;
	}
}


static void SendJson( cJSON *m ) {
	char *text = cJSON_PrintUnformatted( m );
	if ( text ) {
		OzOnline_Send( text );
		cJSON_free( text );
	}
	cJSON_Delete( m );
}


// what the player is doing, as the Godot build words it: "In the menu", or the match; and how a
// friend joins it (a peer-to-peer match: oz_ice.c's OzIce_JoinString; a listed community server: its address)
// the mod's name as players know it (the Server Browser's: oz_ui_servers.cpp names the rest)
static const char *ModLabel( const char *game ) {
	if ( !*game || !Q_stricmp( game, BASEGAME ) ) {
		return "";
	}
	if ( !Q_stricmp( game, DEFAULT_GAME ) ) {
		return "";	// Oz's own game: nothing to say
	}
	if ( !Q_stricmp( game, "cpma" ) ) {
		return "CPMA ";
	}
	if ( !Q_stricmp( game, "defrag" ) ) {
		return "Defrag ";
	}
	if ( !Q_stricmp( game, "osp" ) ) {
		return "OSP ";
	}
	return va( "%s ", game );
}

// a mode's name under the mod's own numbering (what oz_ui_servers.cpp's ModeName does for the rows)
static const char *ModeLabel( const char *game, int gt ) {
	static const char *modes[] = { "FFA", "Duel", "FFA", "TDM", "CTF" };
	if ( !Q_stricmp( game, "defrag" ) ) {
		return gt == 4 ? "Fast caps" : "Run";
	}
	if ( !Q_stricmp( game, "cpma" ) ) {
		switch ( gt ) {
		case -1: return "Hoonymode";
		case 5: return "CA";
		case 6: return "Freeze Tag";
		case 7: return "CTFS";
		case 8: return "NTF";
		case 9: return "2v2";
		}
	}
	return gt >= 0 && gt < ARRAY_LEN( modes ) ? modes[gt] : "Match";
}

static void Presence( qboolean force ) {
	char buf[512];
	const char *status = "online", *detail = "In the menu", *join = "", *mod = "", *map = "", *server = "";
	cJSON *m;

	if ( cls.state == CA_ACTIVE && cl.gameState.dataCount ) {
		// the match's mod, mode and map (PH step 3: presence carries server, mod and map)
		const char *info = cl.gameState.stringData + cl.gameState.stringOffsets[CS_SERVERINFO];
		const char *game = FS_GetCurrentGameDir();
		int gt = atoi( Info_ValueForKey( info, "g_gametype" ) );
		status = "match";
		map = Info_ValueForKey( info, "mapname" );
		mod = !*game || !Q_stricmp( game, DEFAULT_GAME ) ? BASEGAME : game;
		detail = va( "%s%s on %s", ModLabel( game ), ModeLabel( game, gt ), map );
#ifdef USE_OZ_ICE
		join = OzIce_JoinString();
#endif
#ifdef USE_OZ_UI
		// a community server the master servers list (public: never a home or LAN server's address), so a friend
		// can join it from Friends or see it in the Server Browser (P8b): "server:<address>", " locked" with a password
		if ( !*join && !com_sv_running->integer && clc.serverAddress.type == NA_IP ) {
			const char *address = NET_AdrToStringwPort( &clc.serverAddress );
			if ( OzServers_Listed( address ) ) {
				join = va( "server:%s%s", address, atoi( Info_ValueForKey( info, "g_needpass" ) ) ? " locked" : "" );
				server = address;
			}
		}
#endif
	}
	Com_sprintf( buf, sizeof( buf ), "%s|%s|%s|%s|%s|%s", status, detail, join, mod, map, server );
	if ( !force && !strcmp( buf, lastPresence ) ) {
		return;
	}
	Q_strncpyz( lastPresence, buf, sizeof( lastPresence ) );
	m = cJSON_CreateObject();
	cJSON_AddStringToObject( m, "type", "presence" );
	cJSON_AddStringToObject( m, "status", status );
	cJSON_AddStringToObject( m, "detail", detail );
	cJSON_AddStringToObject( m, "join", join );
	cJSON_AddStringToObject( m, "mod", mod );
	cJSON_AddStringToObject( m, "map", map );
	cJSON_AddStringToObject( m, "server", server );
	SendJson( m );
}


static void Profile( void ) {
	cJSON *m = cJSON_CreateObject();
	cJSON_AddStringToObject( m, "type", "profile" );
	cJSON_AddStringToObject( m, "name", Cvar_VariableString( "name" ) );
	cJSON_AddStringToObject( m, "model", Cvar_VariableString( "model" ) );
	cJSON_AddStringToObject( m, "color", "#db2410" );
	SendJson( m );
}


/*
==================
The match's Oz players (the scoreboard's player cards, oz_hud.c)

While the player is in a match on a server others can be on (not their own local one), the hub hears their
seat: the server as the stats reports name it (OzStats_ServerOf), the server's id for the map load, their
client number and name there. While the scoreboard is up the HUD wants the roster, the Oz players seated
there with their cards (asked every few seconds); a card that opens asks for that player's career. Friend
relations come from the hub's social snapshot.
==================
*/
#define ROSTER_EVERY	5000		// ms between roster asks while the board is up
#define CAREER_EVERY	60000		// ms before a career shown is asked again
#define CAREER_RETRY	10000		// ms before an unanswered ask goes again
#define CAREERS			16
#define RELATIONS		512

static struct {
	char		sent[160];			// the seat the hub has: "<server>|<serverid>|<client>|<name>"
	char		server[64];			// "" while not seated
	int			serverid;
	int			wanted;				// cls.realtime the board last wanted the roster
	int			asked;				// cls.realtime it was last asked for, 0 not yet for this seat
	char		signature[256];		// the last roster printed (clients and ids), to print changes only
	int			numSeats;
	struct { int client; char seatName[40]; ozSeat_t s; } seats[MAX_CLIENTS];
} roster;

static struct {
	char		id[24];
	int			asked, answered;	// cls.realtime, 0 not yet
	ozCareer_t	c;
} careers[CAREERS];

static char		relIds[RELATIONS][24];
static byte		relKinds[RELATIONS];
static int		numRelations;
static char		notice[256];
static int		noticeAt = -1;


static void Seat( qboolean force ) {
	char key[sizeof( roster.sent )], server[64], name[40];
	int serverid = 0, client = -1;
	cJSON *m;

	server[0] = name[0] = '\0';
	if ( cls.state == CA_ACTIVE && cl.snap.valid && !clc.demoplaying ) {
		OzStats_ServerOf( server, sizeof( server ) );
		if ( !strcmp( server, "local" ) ) {
			server[0] = '\0';		// the player's own match: no one else is there
		} else {
			serverid = cl.serverId;
			client = clc.clientNum;
			if ( client >= 0 && client < MAX_CLIENTS && cl.gameState.stringOffsets[CS_PLAYERS + client] ) {
				Q_strncpyz( name, Info_ValueForKey( cl.gameState.stringData + cl.gameState.stringOffsets[CS_PLAYERS + client], "n" ),
					sizeof( name ) );
			}
		}
	}
	Com_sprintf( key, sizeof( key ), "%s|%d|%d|%s", server, serverid, client, name );
	if ( !force && !strcmp( key, roster.sent ) ) {
		return;
	}
	if ( strcmp( server, roster.server ) || serverid != roster.serverid ) {
		roster.numSeats = 0;		// another match: its own roster
		roster.signature[0] = '\0';
	}
	Q_strncpyz( roster.sent, key, sizeof( roster.sent ) );
	Q_strncpyz( roster.server, server, sizeof( roster.server ) );
	roster.serverid = serverid;
	roster.asked = 0;
	m = cJSON_CreateObject();
	cJSON_AddStringToObject( m, "type", "seat" );
	cJSON_AddStringToObject( m, "server", server );
	cJSON_AddNumberToObject( m, "serverid", serverid );
	cJSON_AddNumberToObject( m, "client", client );
	cJSON_AddStringToObject( m, "name", name );
	SendJson( m );
}


static void RosterFrame( void ) {
	cJSON *m;

	if ( !roster.server[0] || cls.realtime - roster.wanted > 500 || ( roster.asked && cls.realtime - roster.asked < ROSTER_EVERY ) ) {
		return;
	}
	roster.asked = cls.realtime ? cls.realtime : 1;
	m = cJSON_CreateObject();
	cJSON_AddStringToObject( m, "type", "roster" );
	cJSON_AddStringToObject( m, "server", roster.server );
	cJSON_AddNumberToObject( m, "serverid", roster.serverid );
	SendJson( m );
}


static const char *JsonString( const cJSON *o, const char *key ) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive( o, key );
	return cJSON_IsString( v ) ? v->valuestring : "";
}


static int JsonInt( const cJSON *o, const char *key ) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive( o, key );
	return cJSON_IsNumber( v ) ? v->valueint : 0;
}


// a part of a message as JSON text (a card, its stats), "{}" when it doesn't fit
static void JsonText( const cJSON *o, const char *key, char *out, int size ) {
	char *text = cJSON_PrintUnformatted( cJSON_GetObjectItemCaseSensitive( o, key ) );
	Q_strncpyz( out, text && (int)strlen( text ) < size ? text : "{}", size );
	cJSON_free( text );
}


static void HandleRoster( const cJSON *m ) {
	const cJSON *p, *players = cJSON_GetObjectItemCaseSensitive( m, "players" );
	char signature[sizeof( roster.signature )];

	if ( strcmp( JsonString( m, "server" ), roster.server ) || JsonInt( m, "serverid" ) != roster.serverid ) {
		return;		// a match the player left
	}
	roster.numSeats = 0;
	signature[0] = '\0';
	cJSON_ArrayForEach( p, players ) {
		const int client = JsonInt( p, "client" );
		if ( client < 0 || client >= MAX_CLIENTS || roster.numSeats >= MAX_CLIENTS ) {
			continue;
		}
		roster.seats[roster.numSeats].client = client;
		Q_strncpyz( roster.seats[roster.numSeats].seatName, JsonString( p, "seat_name" ), sizeof( roster.seats[0].seatName ) );
		{
			ozSeat_t *s = &roster.seats[roster.numSeats].s;
			Q_strncpyz( s->id, JsonString( p, "id" ), sizeof( s->id ) );
			Q_strncpyz( s->name, JsonString( p, "name" ), sizeof( s->name ) );
			s->level = JsonInt( p, "level" );
			JsonText( p, "card", s->card, sizeof( s->card ) );
			JsonText( p, "stats", s->stats, sizeof( s->stats ) );
			Q_strcat( signature, sizeof( signature ), va( " %d:%s", client, s->id ) );
		}
		roster.numSeats++;
	}
	if ( strcmp( signature, roster.signature ) ) {
		Q_strncpyz( roster.signature, signature, sizeof( roster.signature ) );
		Com_Printf( "online: roster of %s|%d:%s\n", roster.server, roster.serverid, signature[0] ? signature : " no one" );
	}
}


static void HandleCareer( const cJSON *m ) {
	const char *id = JsonString( m, "id" );
	const cJSON *c = cJSON_GetObjectItemCaseSensitive( m, "career" ), *w;
	int i;

	for ( i = 0; i < CAREERS && strcmp( careers[i].id, id ); i++ ) {
	}
	if ( i == CAREERS || !id[0] ) {
		return;
	}
	memset( &careers[i].c, 0, sizeof( careers[i].c ) );
	careers[i].answered = cls.realtime ? cls.realtime : 1;
	if ( !cJSON_IsObject( c ) ) {
		Com_Printf( "online: no career for %s\n", id );
		return;
	}
	careers[i].c.known = 1;
	careers[i].c.level = JsonInt( c, "level" );
	careers[i].c.matches = JsonInt( c, "matches" );
	careers[i].c.wins = JsonInt( c, "wins" );
	careers[i].c.frags = JsonInt( c, "frags" );
	careers[i].c.deaths = JsonInt( c, "deaths" );
	careers[i].c.seconds = JsonInt( c, "seconds" );
	w = cJSON_GetArrayItem( cJSON_GetObjectItemCaseSensitive( c, "weapons" ), 0 );	// the most frags first
	if ( w && JsonInt( w, "frags" ) > 0 ) {
		Q_strncpyz( careers[i].c.weapon, JsonString( w, "weapon" ), sizeof( careers[i].c.weapon ) );
		careers[i].c.weaponFrags = JsonInt( w, "frags" );
		careers[i].c.weaponShots = JsonInt( w, "shots" );
		careers[i].c.weaponHits = JsonInt( w, "hits" );
	}
	Com_Printf( "online: career of %s: level %d, %d matches, %d wins, %d frags\n", id, careers[i].c.level, careers[i].c.matches,
		careers[i].c.wins, careers[i].c.frags );
}


static void HandleRelations( const cJSON *m ) {
	static const char *lists[3] = { "friends", "outgoing", "incoming" };
	static const byte kinds[3] = { OZ_REL_FRIEND, OZ_REL_SENT, OZ_REL_ASKED };
	const cJSON *p;
	int i;

	numRelations = 0;
	for ( i = 0; i < 3; i++ ) {
		cJSON_ArrayForEach( p, cJSON_GetObjectItemCaseSensitive( m, lists[i] ) ) {
			if ( numRelations < RELATIONS ) {
				Q_strncpyz( relIds[numRelations], JsonString( p, "id" ), sizeof( relIds[0] ) );
				relKinds[numRelations++] = kinds[i];
			}
		}
	}
}


int OzOnline_Online( void ) {
	return started && OzOnline_Status( NULL, 0 ) == OZ_ONLINE_ONLINE;
}


void OzOnline_WantRoster( void ) {
	roster.wanted = cls.realtime;
}


int OzOnline_Seated( int client, const char *name, ozSeat_t *out ) {
	char want[64], seat[64];
	int i;

	Q_strncpyz( want, name, sizeof( want ) );
	Q_CleanStr( want );
	for ( i = 0; i < roster.numSeats; i++ ) {
		if ( roster.seats[i].client != client ) {
			continue;
		}
		Q_strncpyz( seat, roster.seats[i].seatName, sizeof( seat ) );
		Q_CleanStr( seat );
		if ( !Q_stricmp( seat, want ) && strcmp( roster.seats[i].s.id, OzIdentity_PlayerId() ) ) {
			*out = roster.seats[i].s;
			return 1;
		}
	}
	return 0;
}


int OzOnline_Career( const char *id, ozCareer_t *out ) {
	int i, slot = 0;
	cJSON *m;

	if ( !id[0] ) {
		return 0;
	}
	for ( i = 0; i < CAREERS; i++ ) {
		if ( !strcmp( careers[i].id, id ) ) {
			break;
		}
		if ( careers[i].asked < careers[slot].asked ) {
			slot = i;		// the one asked longest ago makes room
		}
	}
	if ( i == CAREERS ) {
		i = slot;
		memset( &careers[i], 0, sizeof( careers[i] ) );
		Q_strncpyz( careers[i].id, id, sizeof( careers[i].id ) );
	}
	if ( !careers[i].asked || cls.realtime - careers[i].asked > ( careers[i].answered ? CAREER_EVERY : CAREER_RETRY ) ) {
		if ( OzOnline_Online() ) {
			careers[i].asked = cls.realtime ? cls.realtime : 1;
			m = cJSON_CreateObject();
			cJSON_AddStringToObject( m, "type", "player_stats" );
			cJSON_AddStringToObject( m, "id", id );
			SendJson( m );
		}
	}
	if ( careers[i].answered ) {
		*out = careers[i].c;
		return 1;
	}
	return 0;
}


int OzOnline_Relation( const char *id ) {
	int i;

	for ( i = 0; i < numRelations; i++ ) {
		if ( !strcmp( relIds[i], id ) ) {
			return relKinds[i];
		}
	}
	return OZ_REL_NONE;
}


void OzOnline_AddFriend( const char *id ) {
	cJSON *m = cJSON_CreateObject();

	cJSON_AddStringToObject( m, "type", OzOnline_Relation( id ) == OZ_REL_ASKED ? "friend_accept" : "friend_add" );
	cJSON_AddStringToObject( m, "id", id );
	SendJson( m );
}


int OzOnline_Notice( char *out, int size ) {
	Q_strncpyz( out, notice, size );
	return noticeAt < 0 ? -1 : cls.realtime - noticeAt;
}


static void Handle( const char *text ) {
	cJSON *m = cJSON_Parse( text );
	const cJSON *type;

	if ( !m ) {
		return;
	}
	type = cJSON_GetObjectItemCaseSensitive( m, "type" );
	if ( cJSON_IsString( type ) ) {
		if ( !strcmp( type->valuestring, "roster" ) ) {
			HandleRoster( m );
		} else if ( !strcmp( type->valuestring, "player_stats" ) ) {
			HandleCareer( m );
		}
		if ( !strcmp( type->valuestring, "social" ) ) {
			const cJSON *me = cJSON_GetObjectItemCaseSensitive( m, "me" );
			const cJSON *code = me ? cJSON_GetObjectItemCaseSensitive( me, "code" ) : NULL;
			HandleRelations( m );
			if ( cJSON_IsString( code ) ) {
				Q_strncpyz( friendCode, code->valuestring, sizeof( friendCode ) );
			}
			{
				const cJSON *f, *list = cJSON_GetObjectItemCaseSensitive( m, "friends" );
				const cJSON *party = cJSON_GetObjectItemCaseSensitive( m, "party" );
				const cJSON *inv = cJSON_GetArrayItem( cJSON_GetObjectItemCaseSensitive( m, "invites" ), 0 );
				const cJSON *pid = inv ? cJSON_GetObjectItemCaseSensitive( inv, "party" ) : NULL;
				friendsCount = cJSON_GetArraySize( list );
				friendsOnline = 0;
				cJSON_ArrayForEach( f, list ) {
					friendsOnline += cJSON_IsTrue( cJSON_GetObjectItemCaseSensitive( f, "online" ) );
				}
				partySize = cJSON_IsObject( party ) ? cJSON_GetArraySize( cJSON_GetObjectItemCaseSensitive( party, "members" ) ) : 0;
				incomingCount = cJSON_GetArraySize( cJSON_GetObjectItemCaseSensitive( m, "incoming" ) );
				Q_strncpyz( newestInvite, cJSON_IsString( pid ) ? pid->valuestring : "", sizeof( newestInvite ) );
			}
		} else if ( !strcmp( type->valuestring, "notice" ) ) {
			const cJSON *t = cJSON_GetObjectItemCaseSensitive( m, "text" );
			if ( cJSON_IsString( t ) ) {
				Com_Printf( "online: %s\n", t->valuestring );
				Q_strncpyz( notice, t->valuestring, sizeof( notice ) );	// the scoreboard's card shows it a moment
				noticeAt = cls.realtime;
			}
		} else if ( !strcmp( type->valuestring, "party_chat" ) ) {
			const cJSON *from = cJSON_GetObjectItemCaseSensitive( m, "from" ), *t = cJSON_GetObjectItemCaseSensitive( m, "text" );
			const cJSON *name = cJSON_IsObject( from ) ? cJSON_GetObjectItemCaseSensitive( from, "name" ) : from;
			Com_Printf( "party: %s" S_COLOR_WHITE ": %s\n", cJSON_IsString( name ) ? name->valuestring : "?",
				cJSON_IsString( t ) ? t->valuestring : "" );
		}
		if ( oz_online_debug->integer ) {
			Com_Printf( "hub: %s\n", type->valuestring );
		}
		if ( !strncmp( type->valuestring, "stats_", 6 ) ) {
			OzStats_HubMessage( type->valuestring, m );	// the match reports (oz_stats.c)
		}
#ifdef USE_OZ_ICE
		if ( !strncmp( type->valuestring, "net_", 4 ) || !strncmp( type->valuestring, "match_", 6 ) ) {
			OzIce_HubMessage( type->valuestring, m );	// hosting and joining (the menus see them too)
		}
#endif
#ifdef USE_OZ_UI
		OzUI_HubMessage( type->valuestring, m );
#endif
	}
	cJSON_Delete( m );
}


/*
==================
CL_OzOnlineFrame

Every client frame: the connection's state, the hub's messages, presence.
==================
*/
void CL_OzOnlineFrame( void ) {
	char reason[256];
	int status;

#ifdef USE_OZ_ICE
	OzIce_Frame();	// the peer-to-peer links run whether or not the hub is there
#endif
	OzUpdate_Frame();	// GitHub's latest release, asked once a run

	if ( oz_online->modified || oz_network_online->modified || oz_network_online_server->modified ) {
		oz_online->modified = oz_network_online->modified = oz_network_online_server->modified = qfalse;
		Stop();
		decided = qfalse;
	}
	if ( !started ) {
		// once per change of the cvars, not every frame: the home path check asks the Windows shell
		// each call (Sys_DefaultHomePath), and a failed start would retry every frame
		if ( !decided ) {
			decided = qtrue;
			if ( OnlineWanted() ) {
				Start();
			}
		}
		if ( !started ) {
			return;
		}
	}
	status = OzOnline_Status( reason, sizeof( reason ) );
	if ( status != lastStatus ) {
		if ( status == OZ_ONLINE_ONLINE ) {
			Com_Printf( "online: connected to %s as %s\n", Server(), OzIdentity_PlayerId() );
			Presence( qtrue );
			Seat( qtrue );	// a new connection has no seat
			Profile();		// the name may have changed while offline
			OzStats_Online();
#ifdef USE_OZ_UI
			careerSent = -1;
#endif
		} else if ( status == OZ_ONLINE_OFFLINE && lastStatus == OZ_ONLINE_ONLINE && reason[0] ) {
			Com_Printf( "online: %s\n", reason );
		} else if ( status == OZ_ONLINE_OFFLINE && OzOnline_CloseCode() >= 4000 ) {
			Com_Printf( S_COLOR_YELLOW "online: %s\n", reason );
		}
		lastStatus = status;
#ifdef USE_OZ_UI
		OzUI_OnlineStatus( status, reason );
#endif
	}
	if ( status != OZ_ONLINE_ONLINE ) {
		return;
	}
	while ( OzOnline_Receive( msgBuf, sizeof( msgBuf ) ) ) {
		Handle( msgBuf );
	}
	Presence( qfalse );
	Seat( qfalse );
	RosterFrame();
	OzStats_Frame();		// the finished matches' reports (P8b)
#ifdef USE_OZ_UI
	// the career (XP, the card others see, its showcase stats) for the level board, as online.gd's career_changed sends it
	if ( OzProgress_CareerVersion() != careerSent ) {
		careerSent = OzProgress_CareerVersion();
		OzOnline_Send( OzProgress_CareerJson() );
	}
#endif
	if ( pendingFriend[0] ) {
		cJSON *m = cJSON_CreateObject();
		cJSON_AddStringToObject( m, "type", "friend_add" );
		cJSON_AddStringToObject( m, "code", pendingFriend );
		SendJson( m );
		pendingFriend[0] = '\0';
	}
	if ( pendingJoin[0] ) {
		Cbuf_AddText( va( "oz_join %s\n", pendingJoin ) );
		pendingJoin[0] = '\0';
	}
	{
		cvar_t *name = Cvar_Get( "name", "", 0 );
		if ( name->modificationCount != nameModified ) {
			nameModified = name->modificationCount;
			Profile();
		}
	}
}


static void CL_Online_f( void ) {
	char reason[256];
	static const char *names[] = { "offline", "connecting", "online" };
	int status = started ? OzOnline_Status( reason, sizeof( reason ) ) : OZ_ONLINE_OFFLINE;

	if ( !started ) {
		Q_strncpyz( reason, !oz_network_online->integer ? "off in Settings > Network" : OnlineWanted() ? "" :
			"oz_online is off for this user folder", sizeof( reason ) );
	}
	Com_Printf( "online: %s%s%s\n", names[status], reason[0] ? " - " : "", reason );
	Com_Printf( "  server %s\n", Server() );
	if ( started ) {
		Com_Printf( "  player %s, friend code %s, ping %i ms\n", OzIdentity_PlayerId(), friendCode[0] ? friendCode : "?", OzOnline_Rtt() );
		Com_Printf( "  friends %i (%i online), requests %i, party %i%s\n", friendsCount, friendsOnline, incomingCount, partySize,
			newestInvite[0] ? ", an invite waiting" : "" );
	}
}


// hub <type> [<key> <value>]...: a message to the hub with string values (tests, and the menus' buttons)
static void CL_Hub_f( void ) {
	cJSON *m;
	int i;

	if ( Cmd_Argc() < 2 || ( Cmd_Argc() & 1 ) ) {	// the type, then key and value pairs: an even count
		Com_Printf( "usage: hub <type> [<key> <value>]...\n" );
		return;
	}
	if ( !started || OzOnline_Status( NULL, 0 ) != OZ_ONLINE_ONLINE ) {
		Com_Printf( "online: not connected\n" );
		return;
	}
	m = cJSON_CreateObject();
	cJSON_AddStringToObject( m, "type", Cmd_Argv( 1 ) );
	for ( i = 2; i + 1 < Cmd_Argc(); i += 2 ) {
		cJSON_AddStringToObject( m, Cmd_Argv( i ), Cmd_Argv( i + 1 ) );
	}
	SendJson( m );
}


// party_accept: joins the party of the newest invite (a bind, and tests that can't know the party's id)
static void CL_PartyAccept_f( void ) {
	cJSON *m;
	if ( !newestInvite[0] || !started || OzOnline_Status( NULL, 0 ) != OZ_ONLINE_ONLINE ) {
		Com_Printf( "online: no party invite\n" );
		return;
	}
	m = cJSON_CreateObject();
	cJSON_AddStringToObject( m, "type", "party_accept" );
	cJSON_AddStringToObject( m, "party", newestInvite );
	SendJson( m );
}


/*
==================
CL_OzLink_f

oz_link <ozarena://...>: an Oz Arena link, as Windows hands one to the game ("+oz_link %1", the scheme
registered by the installer, P14):
  ozarena://friend/<code>		adds that friend (once online)
  ozarena://join/<host>			joins that player's peer-to-peer match (once online)
  ozarena://connect/<address>	joins that server
==================
*/
static void CL_OzLink_f( void ) {
	const char *link = Cmd_Argv( 1 ), *rest;
	char code[16];
	int i, n = 0;

	if ( Q_stricmpn( link, "ozarena://", 10 ) ) {
		Com_Printf( "usage: oz_link ozarena://friend/<code> | ozarena://join/<host> | ozarena://connect/<address>\n" );
		return;
	}
	rest = link + 10;
	if ( !Q_stricmpn( rest, "join/", 5 ) ) {
		for ( i = 5; rest[i] && n < (int)sizeof( pendingJoin ) - 1; i++ ) {
			if ( isxdigit( (unsigned char)rest[i] ) ) {	// a player id: hex digits
				pendingJoin[n++] = tolower( (unsigned char)rest[i] );
			}
		}
		pendingJoin[n] = '\0';
		Com_Printf( "online: joining %s's match\n", pendingJoin );
	} else if ( !Q_stricmpn( rest, "friend/", 7 ) ) {
		for ( i = 7; rest[i] && n < (int)sizeof( code ) - 1; i++ ) {
			if ( isalnum( (unsigned char)rest[i] ) || rest[i] == '-' ) {
				code[n++] = toupper( (unsigned char)rest[i] );
			}
		}
		code[n] = '\0';
		Q_strncpyz( pendingFriend, code, sizeof( pendingFriend ) );
		Com_Printf( "online: adding friend %s\n", code );
	} else if ( !Q_stricmpn( rest, "connect/", 8 ) && rest[8] ) {
		char addr[MAX_OSPATH];
		Q_strncpyz( addr, rest + 8, sizeof( addr ) );
		if ( addr[0] && addr[strlen( addr ) - 1] == '/' ) {
			addr[strlen( addr ) - 1] = '\0';
		}
		Cbuf_AddText( va( "connect %s\n", addr ) );
	} else {
		Com_Printf( "oz_link: not a link Oz Arena knows: %s\n", link );
	}
}


void CL_OzOnlineInit( void ) {
	oz_online = Cvar_Get( "oz_online", "auto", 0 );
	Cvar_SetDescription( oz_online, "Where online services may run (Settings > Network > Online turns them off):\n auto - only from your own user folder\n 1 - anywhere (tests with a local hub)\n 0 - nowhere" );
	oz_network_online = Cvar_Get( "oz_network_online", "1", CVAR_ARCHIVE );
	oz_network_online_server = Cvar_Get( "oz_network_online_server", "", CVAR_ARCHIVE );
	oz_online_import = Cvar_Get( "oz_online_import", "1", CVAR_ARCHIVE );
	Cvar_SetDescription( oz_online_import, "1: a new online identity is first imported from the Godot build's user folder." );
	oz_online_debug = Cvar_Get( "oz_online_debug", "0", 0 );
	Cvar_SetDescription( oz_online_debug, "1: print the type of every message from the hub (developer 1 cuts qconsole.log short)." );
	decided = qfalse;	// a client restart (a mod switch) asks again
#ifdef USE_OZ_ICE
	OzIce_Init();
#endif
	OzStats_Init();
	OzUpdate_Init();
	Cmd_AddCommand( "online", CL_Online_f );
	Cmd_AddCommand( "hub", CL_Hub_f );
	Cmd_AddCommand( "party_accept", CL_PartyAccept_f );
	Cmd_AddCommand( "oz_link", CL_OzLink_f );
}


void CL_OzOnlineShutdown( qboolean quit ) {
#ifdef USE_OZ_ICE
	OzIce_Shutdown( quit );
#endif
	Stop();
	OzUpdate_ClientShutdown( quit );
	Cmd_RemoveCommand( "online" );
	Cmd_RemoveCommand( "hub" );
	Cmd_RemoveCommand( "party_accept" );
	Cmd_RemoveCommand( "oz_link" );
}
