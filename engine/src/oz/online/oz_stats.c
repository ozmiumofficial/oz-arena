/*
===========================================================================
Oz Arena: the stats recorder's reports (ENGINE_PLAN.md P8b steps 5 and 6). GPL-2.0.

The HUD is the recorder (oz_hud.c RecordMatch): its match state, read from what the client receives on
any server, pure or not, under any mod (baseq3's scores, CPMA's xscores and dmscores, the obituaries,
the player's own playerState), handed here once the intermission's final scoreboard is in. Each
match becomes a report (online/src/stats.ts lists its fields), kept in <fs_homepath>/profile/
stats_pending.json until the hub has it: while online, the oldest is signed with the identity key and
sent as stats_report {report: <its JSON text>, sig}, and dropped when the hub answers for its rid
(stored, or refused for good). A report made offline goes the next time the game is online.

A match is the server's address, its map, the server's id for that map load (sv_serverid, the
milliseconds since it started, so a restarted server's next match isn't its last) and the level start
time the server set (CS_LEVEL_START_TIME, which a warmup's restart moves), which every client in it
sees alike: two Oz players' reports of it meet in the hub's Stats object, which decides whether they
agree. A peer-to-peer match is its host's
("ice:<host id>", on the host's own game too); a game on the player's own listen server that isn't
hosted online is "local", never anyone else's; oz_match_id, when the matchmaker placed the match,
names it instead.

  oz_match_id		the matchmaker's id for the match being played (set with its placement)
  ozstats			the reports waiting for the hub
===========================================================================
*/
#include "../../../quake3e/code/client/client.h"
#include "oz_online.h"
#include "oz_stats.h"
#include "../replay/oz_replay_lib.h"
#include "cJSON.h"
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define PENDING_MAX		32			// reports kept while offline; the oldest goes first
#define RESEND_MS		15000		// a report the hub didn't answer goes again
#define SIGN_WAIT_MAX	3600000		// a report that can't be signed is tried again less and less often, up to this

#ifdef USE_OZ_ICE
const char *OzIce_HostKey( void );
#endif

typedef struct {
	char	*text;
	char	rid[24];
} pending_t;

static pending_t	pending[PENDING_MAX];
static int			numPending;
static qboolean		loaded;
static char			sentRid[24];	// the report in flight, "" for none
static int			sentAt;
static int			signWait;		// after a report couldn't be signed: how long until the next try, 0 when the last one signed
static cvar_t		*oz_match_id;


static const char *PendingPath( void ) {
	static char path[MAX_OSPATH];
	Q_strncpyz( path, FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), "profile", "stats_pending.json" ), sizeof( path ) );
	return path;
}


// a file moved over another, replacing it (rename alone doesn't replace on Windows)
static qboolean MoveOver( const char *from, const char *to ) {
#ifdef _WIN32
	return MoveFileExA( from, to, MOVEFILE_REPLACE_EXISTING ) != 0;
#else
	return rename( from, to ) == 0;
#endif
}


static void Save( void ) {
	cJSON		*list = cJSON_CreateArray();
	char		*text;
	char		path[MAX_OSPATH], tmp[MAX_OSPATH];
	FILE		*f;
	qboolean	ok = qfalse;
	int			i;

	for ( i = 0; i < numPending; i++ ) {
		cJSON_AddItemToArray( list, cJSON_CreateString( pending[i].text ) );
	}
	text = cJSON_Print( list );
	cJSON_Delete( list );
	// Written beside the file and moved over it. Written in place, a crash or a full disk during the write
	// left a cut-off file, and every report waiting for the hub was lost with it
	Q_strncpyz( path, PendingPath(), sizeof( path ) );
	Q_strncpyz( tmp, path, sizeof( tmp ) );
	Q_strcat( tmp, sizeof( tmp ), ".tmp" );
	FS_CreatePath( path );
	f = text ? Sys_FOpen( tmp, "wb" ) : NULL;
	if ( f ) {
		const size_t len = strlen( text );
		ok = fwrite( text, 1, len, f ) == len;
		if ( fclose( f ) != 0 ) {
			ok = qfalse;
		}
		if ( ok ) {
			ok = MoveOver( tmp, path );
		}
		if ( !ok ) {
			remove( tmp );
		}
	}
	if ( !ok ) {
		Com_Printf( S_COLOR_YELLOW "ozstats: couldn't write %s\n", path );
	}
	cJSON_free( text );
}


static void Add( const char *text ) {
	cJSON		*r = cJSON_Parse( text );
	const cJSON	*rid = r ? cJSON_GetObjectItemCaseSensitive( r, "rid" ) : NULL;

	if ( cJSON_IsString( rid ) ) {
		if ( numPending == PENDING_MAX ) {
			Com_Printf( S_COLOR_YELLOW "ozstats: %d reports are waiting for the hub: the oldest, %s, is dropped\n", PENDING_MAX, pending[0].rid );
			if ( !strcmp( sentRid, pending[0].rid ) ) {
				sentRid[0] = '\0';
			}
			Z_Free( pending[0].text );
			memmove( pending, pending + 1, sizeof( pending[0] ) * ( PENDING_MAX - 1 ) );
			numPending--;
		}
		pending[numPending].text = CopyString( text );
		Q_strncpyz( pending[numPending].rid, rid->valuestring, sizeof( pending[0].rid ) );
		numPending++;
	}
	cJSON_Delete( r );
}


static void Load( void ) {
	FILE		*f;
	long		len;
	char		*buf;
	cJSON		*list;
	const cJSON	*x;

	loaded = qtrue;
	f = Sys_FOpen( PendingPath(), "rb" );
	if ( !f ) {
		return;
	}
	fseek( f, 0, SEEK_END );
	len = ftell( f );
	fseek( f, 0, SEEK_SET );
	if ( len < 0 ) {
		len = 0;
	}
	buf = Z_Malloc( len + 1 );
	len = (long)fread( buf, 1, len, f );
	buf[len] = '\0';
	fclose( f );
	list = cJSON_Parse( buf );
	Z_Free( buf );
	if ( !cJSON_IsArray( list ) ) {
		// a file that is there but can't be read is kept aside: the next save would have replaced it
		char path[MAX_OSPATH], bad[MAX_OSPATH];
		Q_strncpyz( path, PendingPath(), sizeof( path ) );
		Q_strncpyz( bad, FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), "profile", "stats_pending.bad.json" ), sizeof( bad ) );
		if ( MoveOver( path, bad ) ) {
			Com_Printf( S_COLOR_YELLOW "ozstats: stats_pending.json couldn't be read; kept as stats_pending.bad.json\n" );
		}
	}
	cJSON_ArrayForEach( x, list ) {
		if ( cJSON_IsString( x ) ) {
			Add( x->valuestring );
		}
	}
	cJSON_Delete( list );
}


static void Drop( const char *rid ) {
	int i;
	for ( i = 0; i < numPending; i++ ) {
		if ( !strcmp( pending[i].rid, rid ) ) {
			Z_Free( pending[i].text );
			memmove( pending + i, pending + i + 1, sizeof( pending[0] ) * ( numPending - i - 1 ) );
			numPending--;
			Save();
			return;
		}
	}
}


// the server, as everyone in the match sees it (see the top)
static void ServerOf( char *out, int size ) {
	if ( clc.serverAddress.type == NA_LOOPBACK || com_sv_running->integer ) {
#ifdef USE_OZ_ICE
		const char *host = OzIce_HostKey();
		if ( host[0] ) {
			Q_strncpyz( out, host, size );
			return;
		}
#endif
		Q_strncpyz( out, "local", size );
		return;
	}
	Q_strncpyz( out, NET_AdrToStringwPort( &clc.serverAddress ), size );
}


void OzStats_ServerOf( char *out, int size ) {
	ServerOf( out, size );
}


// g_gametype as the hub's modes name it (baseq3's numbering; a mod's own modes keep their number until
// the per-mod parsers, ENGINE_PLAN.md PH step 4)
static const char *ModeOf( int gametype ) {
	static const char *const modes[] = { "ffa", "duel", "single", "tdm", "ctf" };
	return gametype >= 0 && gametype < ARRAY_LEN( modes ) ? modes[gametype] : va( "gt%d", gametype );
}


static void AddCounts( cJSON *to, const char *name, const ozCounts_t *c ) {
	cJSON	*o = cJSON_AddObjectToObject( to, name );
	int		i;
	for ( i = 0; i < c->num; i++ ) {
		cJSON_AddNumberToObject( o, c->key[i], c->n[i] );
	}
}


/*
==================
OzStats_Record

A finished match from the HUD: its report, kept until the hub has it.
==================
*/
void OzStats_Record( const ozMatchRecord_t *r ) {
	const char	*info = cl.gameState.stringData + cl.gameState.stringOffsets[CS_SERVERINFO];
	const char	*game = FS_GetCurrentGameDir();
	char		server[MAX_STRING_CHARS], rid[17];
	byte		bytes[8];
	cJSON		*j = cJSON_CreateObject(), *rows, *you;
	char		*text;
	int			i;

	if ( !Sys_RandomBytes( bytes, sizeof( bytes ) ) ) {
		for ( i = 0; i < (int)sizeof( bytes ); i++ ) {
			bytes[i] = (byte)( rand() ^ Sys_Milliseconds() >> ( i * 3 ) );
		}
	}
	for ( i = 0; i < (int)sizeof( bytes ); i++ ) {
		Com_sprintf( rid + i * 2, 3, "%02x", bytes[i] );
	}
	ServerOf( server, sizeof( server ) );
	cJSON_AddNumberToObject( j, "v", 1 );
	cJSON_AddStringToObject( j, "rid", rid );
	if ( oz_match_id->string[0] ) {
		cJSON_AddStringToObject( j, "match_id", oz_match_id->string );
	}
	cJSON_AddStringToObject( j, "server", server );
	cJSON_AddStringToObject( j, "hostname", Info_ValueForKey( info, "sv_hostname" ) );
	cJSON_AddStringToObject( j, "mod", !Q_stricmp( game, "oz" ) || !game[0] ? BASEGAME : game );
	cJSON_AddNumberToObject( j, "gametype", r->gametype );
	cJSON_AddStringToObject( j, "mode", ModeOf( r->gametype ) );
	cJSON_AddStringToObject( j, "map", r->map );
	cJSON_AddBoolToObject( j, "pure", cl_connectedToPureServer != 0 );
	cJSON_AddNumberToObject( j, "serverid", cl.serverId );
	cJSON_AddNumberToObject( j, "start", r->start );
	cJSON_AddNumberToObject( j, "seconds", r->seconds );
	cJSON_AddNumberToObject( j, "ended", (double)time( NULL ) * 1000.0 );
	cJSON_AddNumberToObject( j, "me", r->me );
	cJSON_AddBoolToObject( j, "left", qfalse );
	rows = cJSON_AddArrayToObject( j, "rows" );
	for ( i = 0; i < r->numRows; i++ ) {
		cJSON *row = cJSON_CreateObject();
		cJSON_AddNumberToObject( row, "client", r->rows[i].client );
		cJSON_AddNumberToObject( row, "score", r->rows[i].score );
		cJSON_AddNumberToObject( row, "team", r->rows[i].team );
		cJSON_AddStringToObject( row, "name", r->rows[i].name );
		cJSON_AddBoolToObject( row, "bot", r->rows[i].bot );
		cJSON_AddItemToArray( rows, row );
	}
	you = cJSON_AddObjectToObject( j, "you" );
	cJSON_AddNumberToObject( you, "place", r->place );
	cJSON_AddBoolToObject( you, "won", r->won );
	cJSON_AddNumberToObject( you, "score", r->score );
	cJSON_AddNumberToObject( you, "frags", r->frags );
	cJSON_AddNumberToObject( you, "deaths", r->deaths );
	cJSON_AddNumberToObject( you, "xp", r->xp );
	cJSON_AddNumberToObject( you, "rail_streak", r->railStreak );
	cJSON_AddNumberToObject( you, "distance", r->distance );
	cJSON_AddNumberToObject( you, "speed", r->speed );
	AddCounts( you, "frags_weapon", &r->fragsWeapon );
	AddCounts( you, "deaths_weapon", &r->deathsWeapon );
	AddCounts( you, "shots", &r->shots );
	AddCounts( you, "hits", &r->hits );
	AddCounts( you, "damage", &r->damage );
	AddCounts( you, "pickups", &r->pickups );
	AddCounts( you, "medals", &r->medals );
	text = cJSON_PrintUnformatted( j );
	cJSON_Delete( j );
	if ( !text ) {
		return;
	}
	if ( !loaded ) {
		Load();
	}
	Add( text );
	Save();
	CL_OzLib_MatchRecorded( rid, r->won, r->place );	// the replay or demo of this match carries it (P10)
	Com_Printf( "ozstats: recorded %s on %s (%s), place %d of %d, %d frags, %d deaths\n", ModeOf( r->gametype ), r->map, server,
		r->place + 1, r->numRows - ( r->numRows && r->rows[r->numRows - 1].client < 0 ? 2 : 0 ), r->frags, r->deaths );
	// the whole report, for engine/tests/stats_test.py (a console line holds a few players' report)
	if ( strlen( text ) < MAXPRINTMSG - 32 ) {
		Com_Printf( "ozstats: report %s\n", text );
	}
	cJSON_free( text );
}


void OzStats_Online( void ) {
	sentRid[0] = '\0';
}


/*
==================
OzStats_Frame

While online: the oldest pending report, signed, to the hub; again after RESEND_MS without an answer.
==================
*/
void OzStats_Frame( void ) {
	char	sig[1024];
	cJSON	*m;
	char	*text;

	if ( !loaded ) {
		Load();
	}
	if ( !numPending || ( sentRid[0] && Sys_Milliseconds() - sentAt < ( signWait ? signWait : RESEND_MS ) ) ) {
		return;
	}
	if ( !OzIdentity_Sign( pending[0].text, sig, sizeof( sig ) ) ) {
		// tried again later and later (it was every 15 seconds for as long as the game ran), and said once
		if ( !signWait ) {
			Com_Printf( S_COLOR_YELLOW "ozstats: couldn't sign the report\n" );
			Com_Printf( S_COLOR_YELLOW "ozstats: the reports wait until one can be signed\n" );
			signWait = RESEND_MS;
		} else {
			signWait = signWait >= SIGN_WAIT_MAX / 2 ? SIGN_WAIT_MAX : signWait * 2;
		}
		sentAt = Sys_Milliseconds();
		Q_strncpyz( sentRid, pending[0].rid, sizeof( sentRid ) );
		return;
	}
	signWait = 0;
	m = cJSON_CreateObject();
	cJSON_AddStringToObject( m, "type", "stats_report" );
	cJSON_AddStringToObject( m, "report", pending[0].text );
	cJSON_AddStringToObject( m, "sig", sig );
	text = cJSON_PrintUnformatted( m );
	cJSON_Delete( m );
	if ( text && OzOnline_Send( text ) ) {
		Q_strncpyz( sentRid, pending[0].rid, sizeof( sentRid ) );
		sentAt = Sys_Milliseconds();
		Com_Printf( "ozstats: sent report %s\n", sentRid );
	}
	cJSON_free( text );
}


static const char *Str( const cJSON *m, const char *k ) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive( m, k );
	return cJSON_IsString( v ) ? v->valuestring : "";
}

static int Num( const cJSON *m, const char *k ) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive( m, k );
	return cJSON_IsNumber( v ) ? v->valueint : 0;
}


/*
==================
OzStats_HubMessage

stats_report: the hub has a report (or refused it for good): it leaves the queue. stats_match: a match
of yours changed its trust (another Oz player's report agreed with yours).
==================
*/
void OzStats_HubMessage( const char *type, const struct cJSON *m ) {
	if ( !strcmp( type, "stats_report" ) ) {
		const char *rid = Str( m, "rid" );
		if ( cJSON_IsTrue( cJSON_GetObjectItemCaseSensitive( m, "ok" ) ) ) {
			Com_Printf( "ozstats: report %s stored: match %d, %s (%d of %d reports agree)\n", rid, Num( m, "match" ), Str( m, "trust" ),
				Num( m, "agreed" ), Num( m, "reports" ) );
			CL_OzLib_MatchStored( rid, Num( m, "match" ) );	// the Profile's row finds its recording by the match (P10)
		} else {
			Com_Printf( S_COLOR_YELLOW "ozstats: report %s refused: %s\n", rid, Str( m, "reason" ) );
		}
		if ( rid[0] ) {
			Drop( rid );
		} else if ( numPending && sentRid[0] ) {
			Drop( sentRid );	// a refusal that couldn't name it: the one in flight
		}
		if ( !rid[0] || !strcmp( rid, sentRid ) ) {
			sentRid[0] = '\0';
		}
	} else if ( !strcmp( type, "stats_match" ) ) {
		Com_Printf( "ozstats: match %d on %s is now %s (%d reports agree)\n", Num( m, "match" ), Str( m, "map" ), Str( m, "trust" ),
			Num( m, "agreed" ) );
	}
}


static void OzStats_f( void ) {
	int i;
	if ( !loaded ) {
		Load();
	}
	Com_Printf( "ozstats: %d report%s waiting for the hub%s\n", numPending, numPending == 1 ? "" : "s", sentRid[0] ? va( ", %s sent", sentRid ) : "" );
	for ( i = 0; i < numPending; i++ ) {
		Com_Printf( "  %s, %d bytes\n", pending[i].rid, (int)strlen( pending[i].text ) );
	}
}


void OzStats_Init( void ) {
	oz_match_id = Cvar_Get( "oz_match_id", "", CVAR_TEMP );
	Cvar_SetDescription( oz_match_id, "The matchmaker's id for the match being played: its players' reports name it." );
	Cmd_AddCommand( "ozstats", OzStats_f );
}
