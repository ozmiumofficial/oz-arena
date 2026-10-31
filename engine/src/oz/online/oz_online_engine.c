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

static cvar_t	*oz_online, *oz_online_import, *oz_online_debug;
static cvar_t	*oz_network_online, *oz_network_online_server;		// the Settings page's (engine/ui/settings.json)
static int		lastStatus = -1;
static char		lastPresence[256];
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
static void Presence( qboolean force ) {
	char buf[256];
	const char *status = "online", *detail = "In the menu", *join = "";
	cJSON *m;

	if ( cls.state == CA_ACTIVE && cl.gameState.dataCount ) {
		const char *info = cl.gameState.stringData + cl.gameState.stringOffsets[CS_SERVERINFO];
		status = "match";
		static const char *modes[] = { "FFA", "Duel", "FFA", "TDM", "CTF" };
		int gt = atoi( Info_ValueForKey( info, "g_gametype" ) );
		detail = va( "%s on %s", gt >= 0 && gt < ARRAY_LEN( modes ) ? modes[gt] : "Match", Info_ValueForKey( info, "mapname" ) );
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
			}
		}
#endif
	}
	Com_sprintf( buf, sizeof( buf ), "%s|%s|%s", status, detail, join );
	if ( !force && !strcmp( buf, lastPresence ) ) {
		return;
	}
	Q_strncpyz( lastPresence, buf, sizeof( lastPresence ) );
	m = cJSON_CreateObject();
	cJSON_AddStringToObject( m, "type", "presence" );
	cJSON_AddStringToObject( m, "status", status );
	cJSON_AddStringToObject( m, "detail", detail );
	cJSON_AddStringToObject( m, "join", join );
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


static void Handle( const char *text ) {
	cJSON *m = cJSON_Parse( text );
	const cJSON *type;

	if ( !m ) {
		return;
	}
	type = cJSON_GetObjectItemCaseSensitive( m, "type" );
	if ( cJSON_IsString( type ) ) {
		if ( !strcmp( type->valuestring, "social" ) ) {
			const cJSON *me = cJSON_GetObjectItemCaseSensitive( m, "me" );
			const cJSON *code = me ? cJSON_GetObjectItemCaseSensitive( me, "code" ) : NULL;
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
	Cmd_RemoveCommand( "online" );
	Cmd_RemoveCommand( "hub" );
	Cmd_RemoveCommand( "party_accept" );
	Cmd_RemoveCommand( "oz_link" );
}
