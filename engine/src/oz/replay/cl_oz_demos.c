/*
===========================================================================
Oz Arena: your view of a match on another server, recorded, and the replay library (ENGINE_PLAN.md
P10 steps 4 and 6). GPL-2.0.

A server running in this game records its matches whole (sv_oz_replay.c). Someone else's server
(a community server, a friend's peer-to-peer match) runs no such recorder, so the client records what
it receives: from the first snapshot of each map to the next map or the disconnect, as Quake3e's own
"record" writes a demo, to <mod>/demos/<date>_<time>_<map>_<mode>.dm_68. When it stops, the match as
the HUD last saw it (the standings, your place, the server) goes in replays/library.json beside the
names and favorites (oz_replay_lib.h), and the oldest of Oz's own demos past sv_ozReplayKeep go,
favorites never. A demo you record yourself ("record") is yours: never touched.

  sv_ozReplay		1: record every match (the server's recorder too); 0: none
  sv_ozReplayKeep	the newest kept of each kind (50)
  ozreplays			lists the library, for tests
===========================================================================
*/
#include "../../../quake3e/code/client/client.h"
#include "oz_replay.h"
#include "oz_replay_lib.h"
#include "cJSON.h"
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#ifdef _MSC_VER
#pragma comment( lib, "shell32.lib" )
#endif
#endif

int OzE_IsPlayerHome( void );	// oz_ui_engine.c: the player's own user folder (not a test's)

#define MIN_LENGTH_MS	10000		// a visit shorter than this (joined, looked, left) isn't kept
#define SCORE_ASK_MS	30000		// the board asked this often while recording, for the standings

static struct {
	cvar_t		*record, *keep;
	qboolean	active;				// the recording under way is Oz's
	qboolean	tried;				// this map's recording started, or wasn't to be
	char		game[MAX_QPATH];	// the mod folder it's in
	char		map[MAX_QPATH];
	char		mapTitle[128];
	char		hostname[128];
	char		server[MAX_OSPATH];
	char		gameDir[MAX_QPATH];
	int			gametype;
	int			unixStart;
	int			firstTime, lastTime;	// server times seen
	qboolean	intermission;
	ozLibStandings_t standings;		// the last board with rows
	int			standingsAt, askAt;
	char		rid[24];
	int			won, place, match;
} dm;


static const char *CS( int index ) {
	const int ofs = cl.gameState.stringOffsets[index];
	return ofs ? cl.gameState.stringData + ofs : "";
}

// a number field, 0 when it isn't one
static double Num( const cJSON *e, const char *k ) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive( e, k );
	return cJSON_IsNumber( v ) ? v->valuedouble : 0;
}

static const char *ModeKey( int gametype ) {
	static const char *keys[] = { "ffa", "duel", "campaign", "tdm", "ctf" };
	return gametype >= 0 && gametype < (int)ARRAY_LEN( keys ) ? keys[gametype] : va( "mode%i", gametype );
}


/*
=============================================================================
the library file
=============================================================================
*/

const char *OzLib_Home( void ) {
	return Cvar_VariableString( "fs_homepath" );
}

static const char *LibraryPath( void ) {
	return va( "%s%c%s%clibrary.json", OzLib_Home(), PATH_SEP, OZ_REPLAY_DIR, PATH_SEP );
}

static qboolean libRead;		// the file was read this session
static qboolean libBroken;		// and didn't read: the favorites aren't known, so nothing is trimmed

/*
A library that's there and doesn't read (cut short by a crash or a full disk) was taken as an empty one,
and the next match's trim deleted every former favorite. It's kept as library.bad.json, a new one starts,
and no old recording is deleted for the rest of the session (OzLib_Broken).
*/
static void KeepBadLibrary( const char *path ) {
	char	keep[MAX_OSPATH];
	FILE	*f;

	if ( !libBroken ) {
		Com_Printf( S_COLOR_YELLOW "replay: %s/library.json doesn't read: it's kept as library.bad.json, and no old recording is deleted this session\n", OZ_REPLAY_DIR );
	}
	libBroken = qtrue;
	Com_sprintf( keep, sizeof( keep ), "%s%c%s%clibrary.bad.json", OzLib_Home(), PATH_SEP, OZ_REPLAY_DIR, PATH_SEP );
	f = Sys_FOpen( keep, "rb" );
	if ( f ) {
		fclose( f );	// an earlier one is the one with the favorites: it stays
		return;
	}
	if ( rename( path, keep ) ) {
		Com_Printf( S_COLOR_YELLOW "replay: couldn't rename %s\n", path );
	}
}

cJSON *OzLib_Read( void ) {
	cJSON		*lib = NULL;
	char		path[MAX_OSPATH];
	qboolean	bad = qfalse;
	FILE		*f;

	Q_strncpyz( path, LibraryPath(), sizeof( path ) );
	f = Sys_FOpen( path, "rb" );
	libRead = qtrue;
	if ( f ) {
		long n;
		fseek( f, 0, SEEK_END );
		n = ftell( f );
		fseek( f, 0, SEEK_SET );
		if ( n > 0 && n < ( 16 << 20 ) ) {
			char *text = Z_Malloc( n + 1 );
			n = (long)fread( text, 1, n, f );
			text[n] = 0;
			lib = cJSON_Parse( text );
			Z_Free( text );
			bad = !cJSON_IsObject( lib );
		} else if ( n != 0 ) {
			bad = qtrue;
		}
		fclose( f );
	}
	if ( bad ) {
		KeepBadLibrary( path );
	}
	if ( !cJSON_IsObject( lib ) ) {
		cJSON_Delete( lib );
		lib = cJSON_CreateObject();
	}
	return lib;
}

// the library didn't read this session: the trims (this file's and sv_oz_replay.c's) delete nothing
int OzLib_Broken( void ) {
	if ( !libRead ) {
		cJSON_Delete( OzLib_Read() );
	}
	return libBroken;
}

// Written beside the file and renamed over it: written in place with nothing checked, a full disk or a
// crash mid-write left half a library, and every name and favorite went with it
static void WriteLibrary( const cJSON *lib ) {
	char		*text = cJSON_Print( lib );
	char		path[MAX_OSPATH], tmp[MAX_OSPATH];
	qboolean	ok;
	size_t		len;
	FILE		*f;

	if ( !text ) {
		return;
	}
	Sys_Mkdir( OzLib_Home() );
	Sys_Mkdir( va( "%s%c%s", OzLib_Home(), PATH_SEP, OZ_REPLAY_DIR ) );
	Q_strncpyz( path, LibraryPath(), sizeof( path ) );
	Com_sprintf( tmp, sizeof( tmp ), "%s.tmp", path );
	len = strlen( text );
	f = Sys_FOpen( tmp, "wb" );
	ok = f != NULL;
	if ( f ) {
		ok = fwrite( text, 1, len, f ) == len;
		if ( fclose( f ) ) {
			ok = qfalse;
		}
	}
	if ( ok ) {
#ifdef _WIN32
		ok = MoveFileExA( tmp, path, MOVEFILE_REPLACE_EXISTING ) != 0;	// rename() doesn't replace on Windows
#else
		ok = rename( tmp, path ) == 0;
#endif
	}
	if ( !ok ) {
		if ( f ) {
			remove( tmp );
		}
		Com_Printf( S_COLOR_YELLOW "replay: can't write %s\n", path );
	}
	cJSON_free( text );
}

// the key's entry, made if it isn't there: "favorite" first (the server's trim reads it as text)
static cJSON *Entry( cJSON *lib, const char *key ) {
	cJSON *e = cJSON_GetObjectItemCaseSensitive( lib, key );
	if ( !cJSON_IsObject( e ) ) {
		cJSON_DeleteItemFromObjectCaseSensitive( lib, key );
		e = cJSON_CreateObject();
		cJSON_AddFalseToObject( e, "favorite" );
		cJSON_AddStringToObject( e, "name", "" );
		cJSON_AddItemToObject( lib, key, e );
	}
	return e;
}

static void Put( cJSON *e, const char *field, cJSON *value ) {
	if ( cJSON_GetObjectItemCaseSensitive( e, field ) ) {
		cJSON_ReplaceItemInObjectCaseSensitive( e, field, value );
	} else {
		cJSON_AddItemToObject( e, field, value );
	}
}

void OzLib_Set( const char *key, const char *field, cJSON *value ) {
	cJSON *lib = OzLib_Read();
	cJSON *e = Entry( lib, key );
	if ( value ) {
		Put( e, field, value );
	} else {
		cJSON_DeleteItemFromObjectCaseSensitive( e, field );
	}
	WriteLibrary( lib );
	cJSON_Delete( lib );
}

void OzLib_Forget( const char *key ) {
	cJSON *lib = OzLib_Read();
	if ( cJSON_GetObjectItemCaseSensitive( lib, key ) ) {
		cJSON_DeleteItemFromObjectCaseSensitive( lib, key );
		WriteLibrary( lib );
	}
	cJSON_Delete( lib );
}

// several entries gone at once (the Replays page's scan), in one write
void OzLib_ForgetMany( const char *const *keys, int count ) {
	cJSON	*lib;
	int		i, gone = 0;

	if ( count <= 0 ) {
		return;
	}
	lib = OzLib_Read();
	for ( i = 0; i < count; i++ ) {
		if ( cJSON_GetObjectItemCaseSensitive( lib, keys[i] ) ) {
			cJSON_DeleteItemFromObjectCaseSensitive( lib, keys[i] );
			gone++;
		}
	}
	if ( gone ) {
		WriteLibrary( lib );
	}
	cJSON_Delete( lib );
}

int OzLib_IsDemoName( const char *name ) {
	const char *ext = strrchr( name, '.' );
	int protocol;

	if ( !ext || Q_stricmpn( ext, "." DEMOEXT, 1 + (int)strlen( DEMOEXT ) ) ) {
		return 0;
	}
	ext += 1 + strlen( DEMOEXT );
	if ( ext[0] < '0' || ext[0] > '9' || ext[1] < '0' || ext[1] > '9' || ext[2] ) {
		return 0;
	}
	protocol = atoi( ext );
	return protocol >= 66 && protocol <= 91;
}

// a replay's key is its file name in replays/; a demo's is <mod>/demos/<file> under the user folder
int OzLib_Path( const char *key, char *out, int size ) {
	const char *slash = strchr( key, '/' );
	const char *c;

	out[0] = 0;
	if ( !key[0] || strstr( key, ".." ) || strchr( key, '\\' ) || strchr( key, ':' ) ) {
		return 0;
	}
	// a key goes into console commands inside quotes (replay "<key>", demo "<name>"): one with a quote,
	// a semicolon or a control character would end the quotes and run what follows as a command
	for ( c = key; *c; c++ ) {
		if ( (unsigned char)*c < ' ' || *c == '"' || *c == ';' ) {
			return 0;
		}
	}
	if ( !slash ) {
		const char *ext = strrchr( key, '.' );
		if ( !ext || Q_stricmp( ext, "." OZ_REPLAY_EXT ) ) {
			return 0;
		}
		Com_sprintf( out, size, "%s%c%s%c%s", OzLib_Home(), PATH_SEP, OZ_REPLAY_DIR, PATH_SEP, key );
		return 1;
	}
	if ( slash == key || Q_stricmpn( slash, "/demos/", 7 ) || strchr( slash + 7, '/' ) || !OzLib_IsDemoName( slash + 7 ) ) {
		return 0;
	}
	Com_sprintf( out, size, "%s%c%.*s%cdemos%c%s", OzLib_Home(), PATH_SEP, (int)( slash - key ), key, PATH_SEP, PATH_SEP, slash + 7 );
	return 1;
}

const char *OzLib_Recording( void ) {
	static char key[MAX_OSPATH];
	if ( !dm.active || !clc.demorecording ) {
		return "";
	}
	Com_sprintf( key, sizeof( key ), "%s/%s", dm.gameDir, clc.recordName );	// recordName: demos/<name>, no extension yet
	return key;
}

int OzLib_Keep( void ) {
	return dm.keep ? dm.keep->integer : 50;
}

const char *OzLib_CurrentReplay( void ) {
	return com_sv_running && com_sv_running->integer ? SV_OzReplayCurrent() : "";
}

// Windows' Recycle Bin takes what the player deletes from their own folder (a test's scratch files never go there)
int OzLib_Recycles( void ) {
#ifdef _WIN32
	return OzE_IsPlayerHome();
#else
	return 0;
#endif
}

// a file to the Recycle Bin, where the player can get it back, or removed
static qboolean Discard( const char *path ) {
#ifdef _WIN32
	WCHAR			wide[MAX_OSPATH + 2];
	SHFILEOPSTRUCTW	op;
	int				n = MultiByteToWideChar( CP_ACP, 0, path, -1, wide, MAX_OSPATH );	// as fopen takes it
	int				i;

	if ( !OzLib_Recycles() ) {
		return remove( path ) == 0;
	}
	if ( n <= 0 ) {
		return qfalse;
	}
	for ( i = 0; i < n; i++ ) {
		if ( wide[i] == L'/' ) {
			wide[i] = L'\\';
		}
	}
	wide[n] = 0;	// the list ends with two zeros
	memset( &op, 0, sizeof( op ) );
	op.wFunc = FO_DELETE;
	op.pFrom = wide;
	op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
	return SHFileOperationW( &op ) == 0 && !op.fAnyOperationsAborted;
#else
	return remove( path ) == 0;
#endif
}

int OzLib_Delete( const char *key, char *why, int whySize ) {
	char	path[MAX_OSPATH];
	FILE	*f;

	why[0] = 0;
	if ( !OzLib_Path( key, path, sizeof( path ) ) ) {
		Q_strncpyz( why, "That isn't a recording in your user folder.", whySize );
		return 0;
	}
	if ( !Q_stricmp( key, OzLib_CurrentReplay() ) || ( OzLib_Recording()[0] && !Q_stricmpn( key, OzLib_Recording(), (int)strlen( OzLib_Recording() ) ) ) ) {
		Q_strncpyz( why, "It's still being recorded.", whySize );
		return 0;
	}
	if ( clc.demoplaying && strstr( key, clc.demoName ) ) {
		Q_strncpyz( why, "It's playing.", whySize );
		return 0;
	}
	f = Sys_FOpen( path, "rb" );
	if ( f ) {
		fclose( f );
		if ( !Discard( path ) ) {
			Q_strncpyz( why, "Windows didn't let it go (is it open somewhere?).", whySize );
			return 0;
		}
	}
	OzLib_Forget( key );
	Com_Printf( "replay: deleted %s%s\n", key, OzLib_Recycles() ? " (to the Recycle Bin)" : "" );
	return OzLib_Recycles() ? 2 : 1;
}


/*
=============================================================================
reading a demo: its gamestate, its first and last snapshots
=============================================================================
*/

static int ReadLE( FILE *f, int *v ) {
	byte b[4];
	if ( fread( b, 1, 4, f ) != 4 ) {
		return 0;
	}
	*v = b[0] | ( b[1] << 8 ) | ( b[2] << 16 ) | ( b[3] << 24 );
	return 1;
}

// one message's snapshot time (the commands before it skipped), or -1
static int SnapshotTime( byte *data, int len ) {
	msg_t	msg;
	int		cmd;

	MSG_Init( &msg, data, MAX_MSGLEN_BUF );
	msg.cursize = len;
	MSG_Bitstream( &msg );
	MSG_ReadLong( &msg );	// reliable acknowledge
	while ( msg.readcount <= msg.cursize ) {
		cmd = MSG_ReadByte( &msg );
		if ( cmd == svc_serverCommand ) {
			MSG_ReadLong( &msg );
			MSG_ReadString( &msg );
		} else if ( cmd == svc_snapshot ) {
			return MSG_ReadLong( &msg );
		} else if ( cmd != svc_nop ) {
			return -1;
		}
	}
	return -1;
}

int OzDemo_Info( const char *osPath, ozDemoInfo_t *info ) {
	static byte		data[MAX_MSGLEN_BUF];
	char			serverinfo[BIG_INFO_STRING];
	entityState_t	nullstate, es;
	msg_t			msg;
	FILE			*f;
	int				seq, len, cmd;
	long			lastPos = -1;
	int				lastLen = 0;

	memset( info, 0, sizeof( *info ) );
	info->clientNum = -1;
	f = Sys_FOpen( osPath, "rb" );
	if ( !f ) {
		return 0;
	}
	// the first message: the gamestate
	if ( !ReadLE( f, &seq ) || !ReadLE( f, &len ) || len <= 0 || len > MAX_MSGLEN || fread( data, 1, len, f ) != (size_t)len ) {
		fclose( f );
		return 0;
	}
	MSG_Init( &msg, data, sizeof( data ) );
	msg.cursize = len;
	MSG_Bitstream( &msg );
	MSG_ReadLong( &msg );
	serverinfo[0] = 0;
	memset( &nullstate, 0, sizeof( nullstate ) );
	for ( ;; ) {
		cmd = MSG_ReadByte( &msg );
		if ( msg.readcount > msg.cursize || cmd == svc_EOF || cmd < 0 ) {
			break;
		}
		if ( cmd == svc_serverCommand ) {
			MSG_ReadLong( &msg );
			MSG_ReadString( &msg );
		} else if ( cmd == svc_gamestate ) {
			MSG_ReadLong( &msg );
			for ( ;; ) {
				cmd = MSG_ReadByte( &msg );
				if ( msg.readcount > msg.cursize || cmd == svc_EOF ) {
					break;
				}
				if ( cmd == svc_configstring ) {
					const int index = MSG_ReadShort( &msg );
					const char *s = MSG_ReadBigString( &msg );
					if ( index == CS_SERVERINFO ) {
						Q_strncpyz( serverinfo, s, sizeof( serverinfo ) );
					} else if ( index >= CS_PLAYERS && index < CS_PLAYERS + MAX_CLIENTS && info->numPlayers < OZ_LIB_ROWS ) {
						const char *n = Info_ValueForKey( s, "n" );
						if ( n[0] && atoi( Info_ValueForKey( s, "t" ) ) != TEAM_SPECTATOR ) {
							Q_strncpyz( info->players[info->numPlayers], n, sizeof( info->players[0] ) );
							info->playerClient[info->numPlayers++] = index - CS_PLAYERS;
						}
					}
				} else if ( cmd == svc_baseline ) {
					const int num = MSG_ReadBits( &msg, GENTITYNUM_BITS );
					if ( num < 0 || num >= MAX_GENTITIES ) {
						break;
					}
					MSG_ReadDeltaEntity( &msg, &nullstate, &es, num );
				} else {
					break;
				}
			}
			if ( cmd == svc_EOF ) {
				info->clientNum = MSG_ReadLong( &msg );
			}
			break;
		} else if ( cmd != svc_nop ) {
			break;
		}
	}
	if ( !serverinfo[0] ) {
		fclose( f );
		return 0;
	}
	Q_strncpyz( info->map, Info_ValueForKey( serverinfo, "mapname" ), sizeof( info->map ) );
	Q_strncpyz( info->hostname, Info_ValueForKey( serverinfo, "sv_hostname" ), sizeof( info->hostname ) );
	Q_strncpyz( info->game, Info_ValueForKey( serverinfo, "gamename" ), sizeof( info->game ) );
	if ( !Q_stricmp( info->game, BASEGAME ) || !Q_stricmp( info->game, "baseq3" ) ) {
		info->game[0] = 0;
	}
	info->gametype = atoi( Info_ValueForKey( serverinfo, "g_gametype" ) );

	// the rest: message lengths only, the first snapshot's time and the last's
	info->firstTime = -1;
	for ( ;; ) {
		if ( !ReadLE( f, &seq ) || !ReadLE( f, &len ) ) {
			break;		// cut off: the game closed while recording
		}
		if ( len == -1 ) {
			info->complete = 1;
			break;
		}
		if ( len <= 0 || len > MAX_MSGLEN ) {
			break;
		}
		if ( info->firstTime < 0 ) {
			if ( fread( data, 1, len, f ) != (size_t)len ) {
				break;
			}
			info->firstTime = SnapshotTime( data, len );
		} else {
			lastPos = ftell( f );
			lastLen = len;
			if ( fseek( f, len, SEEK_CUR ) ) {
				break;
			}
		}
	}
	info->lastTime = info->firstTime;
	if ( lastPos >= 0 && !fseek( f, lastPos, SEEK_SET ) && fread( data, 1, lastLen, f ) == (size_t)lastLen ) {
		const int t = SnapshotTime( data, lastLen );
		if ( t >= info->firstTime ) {
			info->lastTime = t;
		}
	}
	fclose( f );
	return 1;
}


/*
=============================================================================
recording a match on another server
=============================================================================
*/

static void TakeStandings( void ) {
	ozLibStandings_t s;
	if ( OzHud_Standings( &s ) > 0 ) {
		dm.standings = s;
	} else if ( !dm.standings.numRows ) {
		dm.standings.me = s.me;		// the mode's name, until a board comes
		dm.standings.gametype = s.gametype;
		Q_strncpyz( dm.standings.mode, s.mode, sizeof( dm.standings.mode ) );
	}
	dm.standingsAt = cls.realtime;
}

static void Start( void ) {
	char		name[MAX_QPATH], file[MAX_QPATH];
	qtime_t		t;
	int			i;

	Com_RealTime( &t );
	Q_strncpyz( dm.map, COM_SkipPath( cl.mapname ), sizeof( dm.map ) );
	COM_StripExtension( dm.map, dm.map, sizeof( dm.map ) );
	dm.gametype = atoi( Info_ValueForKey( CS( CS_SERVERINFO ), "g_gametype" ) );
	// the map's name is the server's: a space in it gave "record" more arguments than it takes (nothing
	// was recorded), and the rest aren't a file name's characters, as the server's recorder has it
	Q_strncpyz( file, dm.map, sizeof( file ) );
	for ( i = 0; file[i]; i++ ) {
		if ( strchr( "/\\:*?\"<>|; ", file[i] ) || (unsigned char)file[i] <= ' ' || (unsigned char)file[i] > '~' ) {
			file[i] = '_';
		}
	}
	Com_sprintf( name, sizeof( name ), "%04i-%02i-%02i_%02i%02i%02i_%s_%s", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
				 t.tm_hour, t.tm_min, t.tm_sec, file, ModeKey( dm.gametype ) );
	Cbuf_ExecuteText( EXEC_NOW, va( "record %s", name ) );
	if ( !clc.demorecording ) {
		Com_Printf( S_COLOR_YELLOW "replay: couldn't record this match\n" );
		return;
	}
	dm.active = qtrue;
	Q_strncpyz( dm.gameDir, FS_GetCurrentGameDir(), sizeof( dm.gameDir ) );
	Q_strncpyz( dm.mapTitle, CS( CS_MESSAGE ), sizeof( dm.mapTitle ) );
	Q_strncpyz( dm.hostname, Info_ValueForKey( CS( CS_SERVERINFO ), "sv_hostname" ), sizeof( dm.hostname ) );
	Q_strncpyz( dm.server, cls.servername, sizeof( dm.server ) );
	dm.unixStart = (int)time( NULL );
	dm.firstTime = dm.lastTime = cl.snap.serverTime;
	dm.intermission = qfalse;
	memset( &dm.standings, 0, sizeof( dm.standings ) );
	dm.rid[0] = 0;
	dm.won = dm.place = 0;
	dm.match = 0;
	dm.askAt = cls.realtime + 5000;
	TakeStandings();
	Com_Printf( "replay: recording your view on %s to %s/%s\n", dm.server, dm.gameDir, clc.recordName );
}

void CL_OzDemo_Frame( void ) {
	if ( !dm.record || cls.state != CA_ACTIVE || clc.demoplaying ) {
		return;
	}
	if ( dm.active ) {
		if ( !clc.demorecording ) {
			dm.active = qfalse;		// stopped some way CL_StopRecord_f didn't tell
			return;
		}
		if ( cl.snap.valid ) {
			dm.lastTime = cl.snap.serverTime;
			if ( cl.snap.ps.pm_type == PM_INTERMISSION ) {
				dm.intermission = qtrue;
			} else if ( dm.intermission ) {
				// the next match began on the same map, as a duel server's map_restart starts it (no new
				// gamestate): it gets a recording of its own
				CL_StopRecord_f();
				dm.tried = qfalse;
				return;
			}
		}
		if ( cls.realtime - dm.standingsAt >= 1000 ) {
			TakeStandings();
		}
		if ( !dm.intermission && cls.realtime >= dm.askAt ) {
			CL_AddReliableCommand( "score", qfalse );	// the board as the server has it, as Tab asks for it
			dm.askAt = cls.realtime + SCORE_ASK_MS;
		}
		return;
	}
	if ( dm.tried || clc.demorecording ) {
		return;		// recorded already, or the player records it themselves
	}
	dm.tried = qtrue;
	if ( !dm.record->integer || NET_IsLocalAddress( &clc.serverAddress ) || !cl.snap.valid ) {
		return;		// this game's own server records it whole
	}
	Start();
}

int CL_OzDemo_StopAtGamestate( void ) {
	dm.tried = qfalse;		// another map: it's recorded too
	return dm.active && clc.demorecording && !clc.demoplaying;
}

// whether the demo being recorded is this recorder's, not one the player started with record
int CL_OzDemo_Recording( void ) {
	return dm.active && clc.demorecording && !clc.demoplaying;
}

static int QDECL NewestFirst( const void *a, const void *b ) {
	const double ux = Num( *(const cJSON *const *)a, "unix" ), uy = Num( *(const cJSON *const *)b, "unix" );
	return ux < uy ? 1 : ux > uy ? -1 : 0;
}

// Oz's own demos past sv_ozReplayKeep, oldest first, but the favorites
static void Trim( cJSON *lib ) {
	cJSON	*list[512], *e;
	int		n = 0, i, kept = 0;

	if ( OzLib_Keep() <= 0 || libBroken ) {
		return;		// a library that didn't read doesn't know the favorites: nothing is deleted this session
	}
	cJSON_ArrayForEach( e, lib ) {
		if ( n < (int)ARRAY_LEN( list ) && cJSON_IsTrue( cJSON_GetObjectItemCaseSensitive( e, "auto" ) ) ) {
			list[n++] = e;
		}
	}
	qsort( list, n, sizeof( list[0] ), NewestFirst );
	for ( i = 0; i < n; i++ ) {
		char path[MAX_OSPATH];
		if ( cJSON_IsTrue( cJSON_GetObjectItemCaseSensitive( list[i], "favorite" ) ) || ++kept <= OzLib_Keep() ) {
			continue;
		}
		if ( OzLib_Path( list[i]->string, path, sizeof( path ) ) ) {
			FS_Remove( path );
		}
		Com_Printf( "replay: deleted %s (sv_ozReplayKeep %i)\n", list[i]->string, OzLib_Keep() );
		cJSON_DeleteItemFromObjectCaseSensitive( lib, list[i]->string );
	}
}

static cJSON *StandingsJson( const ozLibStandings_t *s ) {
	cJSON	*rows = cJSON_CreateArray();
	int		i;

	for ( i = 0; i < s->numRows; i++ ) {
		cJSON *r = cJSON_CreateObject();
		cJSON_AddNumberToObject( r, "client", s->rows[i].client );
		cJSON_AddStringToObject( r, "name", s->rows[i].name );
		cJSON_AddNumberToObject( r, "score", s->rows[i].score );
		cJSON_AddNumberToObject( r, "team", s->rows[i].team );
		cJSON_AddBoolToObject( r, "bot", s->rows[i].bot );
		cJSON_AddItemToArray( rows, r );
	}
	return rows;
}

void CL_OzDemo_Stopped( const char *finalName ) {
	char	key[MAX_OSPATH], path[MAX_OSPATH];
	cJSON	*lib, *e;
	int		length;

	if ( !dm.active ) {
		return;
	}
	dm.active = qfalse;
	Com_sprintf( key, sizeof( key ), "%s/%s", dm.gameDir, finalName );
	length = dm.lastTime - dm.firstTime;
	if ( length < MIN_LENGTH_MS && !dm.intermission ) {
		if ( OzLib_Path( key, path, sizeof( path ) ) ) {
			FS_Remove( path );
		}
		Com_Printf( "replay: %s not kept: %.1f s\n", key, length / 1000.0f );
		return;
	}
	lib = OzLib_Read();
	e = Entry( lib, key );
	Put( e, "auto", cJSON_CreateTrue() );
	Put( e, "map", cJSON_CreateString( dm.map ) );
	Put( e, "map_title", cJSON_CreateString( dm.mapTitle ) );
	Put( e, "gametype", cJSON_CreateNumber( dm.gametype ) );
	Put( e, "mode_name", cJSON_CreateString( dm.standings.mode ) );
	Put( e, "server", cJSON_CreateString( dm.server ) );
	Put( e, "hostname", cJSON_CreateString( dm.hostname ) );
	Put( e, "game", cJSON_CreateString( dm.gameDir ) );
	Put( e, "unix", cJSON_CreateNumber( dm.unixStart ) );
	Put( e, "unix_end", cJSON_CreateNumber( (double)time( NULL ) ) );
	Put( e, "length_ms", cJSON_CreateNumber( length ) );
	Put( e, "finished", cJSON_CreateBool( dm.intermission ) );
	Put( e, "me", cJSON_CreateNumber( dm.standings.me ) );
	Put( e, "standings", StandingsJson( &dm.standings ) );
	if ( dm.standings.teams ) {
		Put( e, "red", cJSON_CreateNumber( dm.standings.red ) );
		Put( e, "blue", cJSON_CreateNumber( dm.standings.blue ) );
	}
	if ( dm.rid[0] ) {
		Put( e, "rid", cJSON_CreateString( dm.rid ) );
		Put( e, "won", cJSON_CreateBool( dm.won ) );
		Put( e, "place", cJSON_CreateNumber( dm.place ) );
	}
	if ( dm.match ) {
		Put( e, "match", cJSON_CreateNumber( dm.match ) );
	}
	Trim( lib );
	WriteLibrary( lib );
	cJSON_Delete( lib );
	Com_Printf( "replay: wrote %s: %.1f s, %i on the board%s\n", key, length / 1000.0f, dm.standings.numRows,
				dm.intermission ? ", to the end" : "" );
}

// the match's report was made (the final board is in): what it says goes with the recording
void CL_OzLib_MatchRecorded( const char *rid, int won, int place ) {
	const char *replay = OzLib_CurrentReplay();

	if ( dm.active ) {
		TakeStandings();
		dm.intermission = qtrue;
		Q_strncpyz( dm.rid, rid, sizeof( dm.rid ) );
		dm.won = won;
		dm.place = place;
		return;
	}
	if ( replay[0] && NET_IsLocalAddress( &clc.serverAddress ) ) {
		ozLibStandings_t s;
		cJSON *lib = OzLib_Read(), *e = Entry( lib, replay );
		OzHud_Standings( &s );
		Put( e, "rid", cJSON_CreateString( rid ) );
		Put( e, "me", cJSON_CreateNumber( s.me ) );
		Put( e, "won", cJSON_CreateBool( won ) );
		Put( e, "place", cJSON_CreateNumber( place ) );
		Put( e, "mode_name", cJSON_CreateString( s.mode ) );
		WriteLibrary( lib );
		cJSON_Delete( lib );
	}
}

// the hub stored that report as its match number: the Profile's row finds the recording by it
void CL_OzLib_MatchStored( const char *rid, int match ) {
	cJSON	*lib, *e;

	if ( !rid[0] || match <= 0 ) {
		return;
	}
	if ( dm.active && !strcmp( dm.rid, rid ) ) {
		dm.match = match;
		return;
	}
	lib = OzLib_Read();
	cJSON_ArrayForEach( e, lib ) {
		const cJSON *r = cJSON_GetObjectItemCaseSensitive( e, "rid" );
		if ( cJSON_IsString( r ) && !strcmp( r->valuestring, rid ) ) {
			Put( e, "match", cJSON_CreateNumber( match ) );
			WriteLibrary( lib );
			break;
		}
	}
	cJSON_Delete( lib );
}

// ozreplays [stored <rid> <match>]: the library; "stored" stands in for the hub's answer to a report (tests)
static void OzReplays_f( void ) {
	cJSON		*lib;
	const cJSON	*e;
	int			n = 0;

	if ( !Q_stricmp( Cmd_Argv( 1 ), "stored" ) ) {
		CL_OzLib_MatchStored( Cmd_Argv( 2 ), atoi( Cmd_Argv( 3 ) ) );
		Com_Printf( "ozreplays: report %s is match %s\n", Cmd_Argv( 2 ), Cmd_Argv( 3 ) );
		return;
	}
	lib = OzLib_Read();
	cJSON_ArrayForEach( e, lib ) {
		const cJSON *st = cJSON_GetObjectItemCaseSensitive( e, "standings" );
		const cJSON *rid = cJSON_GetObjectItemCaseSensitive( e, "rid" );
		Com_Printf( "ozreplays: %s: favorite %i, name \"%s\", map %s, length %i, finished %i, me %i, board %i, match %i, rid %s\n",
			e->string, cJSON_IsTrue( cJSON_GetObjectItemCaseSensitive( e, "favorite" ) ),
			cJSON_IsString( cJSON_GetObjectItemCaseSensitive( e, "name" ) ) ? cJSON_GetObjectItemCaseSensitive( e, "name" )->valuestring : "",
			cJSON_IsString( cJSON_GetObjectItemCaseSensitive( e, "map" ) ) ? cJSON_GetObjectItemCaseSensitive( e, "map" )->valuestring : "-",
			(int)Num( e, "length_ms" ),
			cJSON_IsTrue( cJSON_GetObjectItemCaseSensitive( e, "finished" ) ),
			cJSON_IsNumber( cJSON_GetObjectItemCaseSensitive( e, "me" ) ) ? (int)Num( e, "me" ) : -1,
			cJSON_IsArray( st ) ? cJSON_GetArraySize( st ) : 0,
			(int)Num( e, "match" ), cJSON_IsString( rid ) ? rid->valuestring : "-" );
		n++;
	}
	Com_Printf( "ozreplays: %i in the library; recording %s\n", n, OzLib_Recording()[0] ? OzLib_Recording() : "nothing" );
	cJSON_Delete( lib );
}

/*
A crash while this recorder wrote left demos/<date>_<time>_<map>_<mode>.tmp, which nothing lists or
removes: the ones in this mod's demos folder with a name of this recorder's go at start. One written in
the last ten minutes may be another running game's, still recording: it stays.
*/
static void RemoveStaleTemps( void ) {
	static const char pattern[] = "dddd-dd-dd_dddddd_";
	char	dir[MAX_OSPATH], path[MAX_OSPATH], **list;
	int		n = 0, i, k;

	Com_sprintf( dir, sizeof( dir ), "%s%c%s%cdemos", OzLib_Home(), PATH_SEP, FS_GetCurrentGameDir(), PATH_SEP );
	list = Sys_ListFiles( dir, ".tmp", NULL, &n, qfalse );
	for ( i = 0; list && i < n; i++ ) {
		fileOffset_t	size;
		fileTime_t		mtime, ctime;
		for ( k = 0; pattern[k]; k++ ) {
			if ( pattern[k] == 'd' ? ( list[i][k] < '0' || list[i][k] > '9' ) : list[i][k] != pattern[k] ) {
				break;
			}
		}
		if ( pattern[k] ) {
			continue;	// not this recorder's name: the player's own, or a download's
		}
		Com_sprintf( path, sizeof( path ), "%s%c%s", dir, PATH_SEP, list[i] );
		if ( !Sys_GetFileStats( path, &size, &mtime, &ctime ) || time( NULL ) - mtime < 600 ) {
			continue;
		}
		if ( remove( path ) == 0 ) {
			Com_Printf( "replay: removed %s, left by a recording that never finished\n", list[i] );
		}
	}
	Sys_FreeFileList( list );
}

void CL_OzDemo_Init( void ) {
	RemoveStaleTemps();
	// the server's recorder's cvars, made here too so the Settings page and this recorder share them
	dm.record = Cvar_Get( "sv_ozReplay", "1", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( dm.record, "Records every match: a server in this game records it whole, a match on another server is recorded as your view." );
	dm.keep = Cvar_Get( "sv_ozReplayKeep", "50", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( dm.keep, "The newest replays kept; favorites are never deleted. 0 keeps every one." );
	Cmd_AddCommand( "ozreplays", OzReplays_f );
}
