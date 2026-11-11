/*
===========================================================================
Oz Arena: the server's replay recorder (ENGINE_PLAN.md P10 step 1). GPL-2.0.

The Godot build's recorder (q3core's q3_replay.c), which ran after every server frame of its in-process
server, now in the engine's own server, dedicated or listen. Once a client is in the game, a map is
recorded to <fs_homepath>/replays/<date>_<time>_<map>_<mode>.q3replay (oz_replay.h has the format)
until the server loads another map or shuts down: the file is written as the match plays, so a crash
leaves one that plays to its last whole frame.

Every server frame it gathers each client's new reliable commands (the 64-deep ring is read before it
can wrap), and at 40 Hz (each frame at Quake3e's sv_fps 20) writes one frame: every linked entity that
isn't SVF_NOCLIENT (players, missiles, items, movers, temp events), every active client's playerState
(bots too), the configstrings that changed and the commands, as deltas from the frame before through
msg.c, what a network client receives. Every 2 s a keyframe deltas from nothing, for seeking.

  sv_ozReplay		1: record every match with a client in it (the default); 0: none
  sv_ozReplayKeep	the newest replays kept (50); a favorite (replays/library.json) is never deleted
  sv_ozReplayLog	1: print where each client is at every keyframe (tests check playback against it)
===========================================================================
*/
#include "../../../quake3e/code/server/server.h"
#include "oz_replay.h"
#include <time.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#if !defined( DEDICATED ) && defined( USE_OZ_UI )
#include "oz_replay_lib.h"		// the client's library: a trimmed replay's entry goes with its file
#define OZ_REPLAY_LIBRARY
#endif

#define KEEP_PLAYTESTS	10		// the newest replays kept of the map editor's play-tests (maps named ed_*)

#define MSG_BYTES	( 1 << 20 )
#define MAX_PENDING	512

typedef struct {
	unsigned	mask[2];
	char		text[MAX_STRING_CHARS];
} pendingCmd_t;

static struct {
	cvar_t		*enabled, *keep, *log;
	qboolean	active;
	qboolean	halted;						// a write failed: nothing more is recorded until the map ends
	qboolean	intermission;				// the match ended: a map_restart now starts the next one's replay
	FILE		*file;
	char		name[MAX_QPATH];
	char		path[MAX_OSPATH];
	char		map[MAX_QPATH];
	int			nextTime;
	int			lastKey;
	int			frames;
	int			startTime;
	int			bytes;
	char		*cs[MAX_CONFIGSTRINGS];		// the configstrings as last written
	int			cmdSeq[MAX_CLIENTS];
	pendingCmd_t pending[MAX_PENDING];
	int			numPending;
	int			lastPending[MAX_CLIENTS];	// each client's newest pending entry: later ones merge only after it
} rec;

static ozReplayFrame_t recLast, recCur;
static byte msgBuf[MSG_BYTES];
static const playerState_t nullPS;


static void PutU32( byte *p, unsigned v ) {
	p[0] = v & 255; p[1] = ( v >> 8 ) & 255; p[2] = ( v >> 16 ) & 255; p[3] = ( v >> 24 ) & 255;
}


/*
Text as its bytes: msg.c's string writers turn '%' and every byte over 127 into '.', and chat has to come
back as it was said.
*/
static void WriteText( msg_t *msg, const char *s ) {
	int n = (int)strlen( s );
	if ( n > BIG_INFO_STRING - 1 ) {
		n = BIG_INFO_STRING - 1;
	}
	MSG_WriteShort( msg, n );
	MSG_WriteData( msg, s, n );
}


// commands the recording leaves out: configstrings travel on their own, "scores" answers one client's Tab,
// and "disconnect" is the network's
static qboolean SkipCommand( const char *t ) {
	return !Q_strncmp( t, "cs ", 3 ) || !Q_strncmp( t, "bcs", 3 ) || !Q_strncmp( t, "scores ", 7 ) ||
		!Q_strncmp( t, "disconnect", 10 );
}


/*
A command that reached `client`. One broadcast to many clients is kept once, with all of them in its mask;
merging only into entries after the client's last one keeps each client's order.
*/
static void AddPending( int client, const char *text ) {
	pendingCmd_t *p;
	int i;

	for ( i = rec.lastPending[client] + 1; i < rec.numPending; i++ ) {
		p = &rec.pending[i];
		if ( !( p->mask[client >> 5] & ( 1u << ( client & 31 ) ) ) && !strcmp( p->text, text ) ) {
			p->mask[client >> 5] |= 1u << ( client & 31 );
			rec.lastPending[client] = i;
			return;
		}
	}
	if ( rec.numPending >= MAX_PENDING ) {
		return;
	}
	p = &rec.pending[rec.numPending];
	p->mask[0] = p->mask[1] = 0;
	p->mask[client >> 5] = 1u << ( client & 31 );
	Q_strncpyz( p->text, text, sizeof( p->text ) );
	rec.lastPending[client] = rec.numPending++;
}


static void ClearPending( void ) {
	int i;

	rec.numPending = 0;
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		rec.lastPending[i] = -1;
	}
}


// every client's reliable commands since the last server frame, read from client_t's ring as the network
// would send them
static void HarvestCommands( void ) {
	int i;

	for ( i = 0; i < sv.maxclients && i < MAX_CLIENTS; i++ ) {
		const client_t *cl = &svs.clients[i];
		if ( cl->state < CS_CONNECTED ) {
			rec.cmdSeq[i] = 0;		// a new connection counts from 0
			continue;
		}
		if ( cl->reliableSequence < rec.cmdSeq[i] ) {
			rec.cmdSeq[i] = 0;		// the slot was freed and taken again within one frame
		}
		if ( cl->state < CS_PRIMED ) {
			continue;				// nothing is sent before the gamestate
		}
		if ( cl->reliableSequence - rec.cmdSeq[i] > MAX_RELIABLE_COMMANDS ) {
			rec.cmdSeq[i] = cl->reliableSequence - MAX_RELIABLE_COMMANDS;
		}
		while ( rec.cmdSeq[i] < cl->reliableSequence ) {
			const char *t;
			rec.cmdSeq[i]++;
			t = cl->reliableCommands[rec.cmdSeq[i] & ( MAX_RELIABLE_COMMANDS - 1 )];
			if ( !SkipCommand( t ) ) {
				AddPending( i, t );
			}
		}
	}
}


// what a snapshot could carry for anyone: linked, not SVF_NOCLIENT (SV_AddEntitiesVisibleFromPoint without
// the PVS). SVF_CLIENTMASK (no game Oz plays sets it) goes to everyone.
static void GatherEntities( ozReplayFrame_t *f ) {
	int e;

	Com_Memset( f->present, 0, sizeof( f->present ) );
	Com_Memset( f->filter, 0, sizeof( f->filter ) );
	if ( !sv.gentities ) {
		return;
	}
	for ( e = 0; e < sv.num_entities && e < MAX_GENTITIES - 1; e++ ) {
		const sharedEntity_t *ent = SV_GentityNum( e );
		if ( !ent->r.linked || ( ent->r.svFlags & SVF_NOCLIENT ) ) {
			continue;
		}
		f->present[e] = 1;
		f->ents[e] = ent->s;
		f->ents[e].number = e;
		if ( ent->r.svFlags & SVF_SINGLECLIENT ) {
			f->filter[e] = OZ_REPLAY_FILTER_SINGLE;
			f->filterClient[e] = (byte)ent->r.singleClient;
		} else if ( ent->r.svFlags & SVF_NOTSINGLECLIENT ) {
			f->filter[e] = OZ_REPLAY_FILTER_NOTSINGLE;
			f->filterClient[e] = (byte)ent->r.singleClient;
		}
	}
}


// everyone who played in the recording, as last seen: the trailer's standings keep a player who left before the end
static struct {
	qboolean	seen;
	char		name[36];
	int			score, team;
} played[MAX_CLIENTS];

static int GatherPlayers( ozReplayFrame_t *f ) {
	int i, n = 0;

	Com_Memset( f->psPresent, 0, sizeof( f->psPresent ) );
	for ( i = 0; i < sv.maxclients && i < MAX_CLIENTS; i++ ) {
		if ( svs.clients[i].state != CS_ACTIVE ) {
			continue;
		}
		f->psPresent[i] = 1;
		f->ps[i] = *SV_GameClientNum( i );
		played[i].seen = qtrue;
		Q_strncpyz( played[i].name, Info_ValueForKey( sv.configstrings[CS_PLAYERS + i] ? sv.configstrings[CS_PLAYERS + i] : "", "n" ), sizeof( played[i].name ) );
		played[i].score = f->ps[i].persistant[PERS_SCORE];
		played[i].team = f->ps[i].persistant[PERS_TEAM];
		if ( f->ps[i].pm_type == PM_INTERMISSION ) {
			rec.intermission = qtrue;
		}
		n++;
	}
	return n;
}


static void WriteMasks( msg_t *msg, const byte *present ) {
	unsigned m[2] = { 0, 0 };
	int i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( present[i] ) {
			m[i >> 5] |= 1u << ( i & 31 );
		}
	}
	MSG_WriteLong( msg, (int)m[0] );
	MSG_WriteLong( msg, (int)m[1] );
}


/*
One frame as a block: the configstrings that differ from what was written (brought up to date here), the
commands, then the playerStates and entities as deltas from the frame before (from nothing for a
keyframe). qfalse if it didn't fit or the file can't be written.
*/
static qboolean WriteFrame( qboolean key ) {
	static short changed[MAX_CONFIGSTRINGS];
	byte header[OZ_REPLAY_BLOCK_HEADER];
	entityState_t nullES;
	msg_t msg;
	int i, e, n;
	long start;

	MSG_Init( &msg, msgBuf, sizeof( msgBuf ) );
	n = 0;
	for ( i = 0; i < MAX_CONFIGSTRINGS; i++ ) {
		const char *now = sv.configstrings[i] ? sv.configstrings[i] : "";
		const char *was = rec.cs[i] ? rec.cs[i] : "";
		if ( strcmp( now, was ) ) {
			changed[n++] = (short)i;
		}
	}
	MSG_WriteShort( &msg, n );
	for ( i = 0; i < n; i++ ) {
		const int idx = changed[i];
		const char *now = sv.configstrings[idx] ? sv.configstrings[idx] : "";
		MSG_WriteShort( &msg, idx );
		WriteText( &msg, now );
		if ( rec.cs[idx] ) {
			Z_Free( rec.cs[idx] );
		}
		rec.cs[idx] = now[0] ? CopyString( now ) : NULL;
	}

	MSG_WriteShort( &msg, rec.numPending );
	for ( i = 0; i < rec.numPending; i++ ) {
		MSG_WriteLong( &msg, (int)rec.pending[i].mask[0] );
		MSG_WriteLong( &msg, (int)rec.pending[i].mask[1] );
		WriteText( &msg, rec.pending[i].text );
	}

	WriteMasks( &msg, recCur.psPresent );
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( recCur.psPresent[i] ) {
			MSG_WriteDeltaPlayerstate( &msg, ( !key && recLast.psPresent[i] ) ? &recLast.ps[i] : &nullPS, &recCur.ps[i] );
		}
	}

	for ( e = 0; e < MAX_GENTITIES - 1; e++ ) {
		const qboolean had = !key && recLast.present[e];
		if ( recCur.present[e] ) {
			if ( had ) {
				MSG_WriteDeltaEntity( &msg, &recLast.ents[e], &recCur.ents[e], qfalse );
			} else {
				Com_Memset( &nullES, 0, sizeof( nullES ) );
				nullES.number = e;
				MSG_WriteDeltaEntity( &msg, &nullES, &recCur.ents[e], qtrue );
			}
		} else if ( had ) {
			MSG_WriteDeltaEntity( &msg, &recLast.ents[e], NULL, qfalse );
		}
	}
	MSG_WriteBits( &msg, MAX_GENTITIES - 1, GENTITYNUM_BITS );

	n = 0;
	for ( e = 0; e < MAX_GENTITIES; e++ ) {
		n += recCur.present[e] && recCur.filter[e];
	}
	MSG_WriteShort( &msg, n );
	for ( e = 0; e < MAX_GENTITIES; e++ ) {
		if ( recCur.present[e] && recCur.filter[e] ) {
			MSG_WriteShort( &msg, e );
			MSG_WriteByte( &msg, recCur.filter[e] );
			MSG_WriteByte( &msg, recCur.filterClient[e] );
		}
	}
	MSG_WriteLong( &msg, OZ_REPLAY_FRAME_END );
	if ( msg.overflowed ) {
		return qfalse;
	}

	PutU32( header, (unsigned)msg.cursize );
	header[4] = (byte)key;
	PutU32( header + 5, (unsigned)recCur.time );
	start = ftell( rec.file );
	if ( fwrite( header, 1, sizeof( header ), rec.file ) != sizeof( header ) ||
		 fwrite( msg.data, 1, msg.cursize, rec.file ) != (size_t)msg.cursize ) {
		// A full disk left part of a block, and the trailer went over it: the file is cut back to its
		// last whole block, where the trailer belongs
		if ( start >= 0 ) {
			int cut;
			fflush( rec.file );
			clearerr( rec.file );
#ifdef _WIN32
			cut = _chsize( _fileno( rec.file ), start );
#else
			cut = ftruncate( fileno( rec.file ), (off_t)start );
#endif
			if ( cut ) {
				Com_Printf( S_COLOR_YELLOW "replay: couldn't cut %s back to its last whole frame\n", rec.name );
			}
			fseek( rec.file, start, SEEK_SET );
		}
		return qfalse;
	}
	rec.bytes += sizeof( header ) + msg.cursize;
	if ( key ) {
		fflush( rec.file );		// a crash keeps the replay up to here
	}
	return qtrue;
}


/*
=============================================================================
the header and the trailer: JSON, as the Godot build's Q3ReplayRecorder wrote them
=============================================================================
*/

typedef struct {
	char	*s;
	int		len, size;
} jsonBuf_t;

static void J_Raw( jsonBuf_t *j, const char *s ) {
	const int n = (int)strlen( s );
	if ( j->len + n < j->size ) {
		memcpy( j->s + j->len, s, n + 1 );
		j->len += n;
	}
}

static void J_String( jsonBuf_t *j, const char *s ) {
	char c[8];

	J_Raw( j, "\"" );
	for ( ; *s; s++ ) {
		const unsigned char ch = (unsigned char)*s;
		if ( ch == '"' || ch == '\\' ) {
			c[0] = '\\'; c[1] = ch; c[2] = 0;
		} else if ( ch < 32 ) {
			Com_sprintf( c, sizeof( c ), "\\u%04x", ch );
		} else {
			c[0] = ch; c[1] = 0;
		}
		J_Raw( j, c );
	}
	J_Raw( j, "\"" );
}

static void J_Key( jsonBuf_t *j, const char *key ) {
	J_Raw( j, j->len > 1 && j->s[j->len - 1] != '{' && j->s[j->len - 1] != '[' ? "," : "" );
	J_String( j, key );
	J_Raw( j, ":" );
}

static void J_KeyString( jsonBuf_t *j, const char *key, const char *value ) {
	J_Key( j, key );
	J_String( j, value );
}

static void J_KeyInt( jsonBuf_t *j, const char *key, int value ) {
	J_Key( j, key );
	J_Raw( j, va( "%i", value ) );
}

// a JSON block with its magic, as the file has it: the header after "Q3RP" and the version, the trailer
// before its length and "Q3RT"
static void WriteJSON( const jsonBuf_t *j, qboolean trailer ) {
	byte n[4];

	PutU32( n, (unsigned)j->len );
	if ( trailer ) {
		fwrite( j->s, 1, j->len, rec.file );
		fwrite( n, 1, 4, rec.file );
		fwrite( OZ_REPLAY_TRAILER_MAGIC, 1, 4, rec.file );
	} else {
		PutU32( n, OZ_REPLAY_VERSION );
		fwrite( OZ_REPLAY_MAGIC, 1, 4, rec.file );
		fwrite( n, 1, 4, rec.file );
		PutU32( n, (unsigned)j->len );
		fwrite( n, 1, 4, rec.file );
		fwrite( j->s, 1, j->len, rec.file );
	}
}

static const char *ModeKey( int gametype, qboolean name ) {
	static const char *keys[] = { "ffa", "duel", "campaign", "tdm", "ctf" };
	static const char *names[] = { "Free For All", "Duel", "Single Player", "Team Deathmatch", "Capture the Flag" };
	if ( gametype < 0 || gametype >= (int)ARRAY_LEN( keys ) ) {
		return name ? "Custom" : va( "mode%i", gametype );
	}
	return name ? names[gametype] : keys[gametype];
}

static const char *ConfigString( int index ) {
	return sv.configstrings[index] ? sv.configstrings[index] : "";
}

static void WriteHeader( const qtime_t *t ) {
	static char text[4096];
	jsonBuf_t j = { text, 0, sizeof( text ) };
	const char *game = Cvar_VariableString( "fs_game" );
	const int gametype = Cvar_VariableIntegerValue( "g_gametype" );

	text[0] = 0;
	J_Raw( &j, "{" );
	J_KeyInt( &j, "format", OZ_REPLAY_VERSION );
	J_KeyInt( &j, "rate", OZ_REPLAY_HZ );
	J_KeyString( &j, "date", va( "%04i-%02i-%02i %02i:%02i:%02i", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
								  t->tm_hour, t->tm_min, t->tm_sec ) );
	J_KeyInt( &j, "unix", (int)time( NULL ) );
	J_KeyString( &j, "map", rec.map );
	J_KeyString( &j, "map_name", ConfigString( CS_MESSAGE ) );
	J_KeyString( &j, "mode", ModeKey( gametype, qfalse ) );
	J_KeyString( &j, "mode_name", ModeKey( gametype, qtrue ) );
	J_KeyString( &j, "title", Cvar_VariableString( "sv_hostname" ) );
	J_KeyString( &j, "source", com_dedicated->integer ? "server" : "local" );
	J_KeyString( &j, "map_checksum", Cvar_VariableString( "sv_mapChecksum" ) );
	J_KeyString( &j, "game", game[0] ? game : BASEGAME );
	J_KeyString( &j, "game_version", ConfigString( CS_GAME_VERSION ) );
	J_KeyString( &j, "engine", Cvar_VariableString( "version" ) );
	J_Raw( &j, "}" );
	WriteJSON( &j, qfalse );
}

static void WriteTrailer( const char *why ) {
	static char text[16384];
	jsonBuf_t j = { text, 0, sizeof( text ) };
	qboolean first = qtrue;
	int i;

	text[0] = 0;
	J_Raw( &j, "{" );
	J_KeyInt( &j, "frames", rec.frames );
	J_KeyInt( &j, "start_time", rec.startTime );
	J_KeyInt( &j, "end_time", recLast.time );
	J_KeyInt( &j, "length_ms", recLast.time - rec.startTime );
	J_KeyString( &j, "ended", why );
	J_Key( &j, "standings" );
	J_Raw( &j, "[" );
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( !played[i].seen ) {
			continue;		// everyone who played, those who left before the end too, as last seen
		}
		J_Raw( &j, first ? "{" : ",{" );
		first = qfalse;
		J_KeyInt( &j, "client", i );
		J_KeyString( &j, "name", played[i].name );
		J_KeyInt( &j, "score", played[i].score );
		J_KeyInt( &j, "team", played[i].team );
		J_Raw( &j, "}" );
	}
	J_Raw( &j, "]}" );
	WriteJSON( &j, qtrue );
}


/*
=============================================================================
the files
=============================================================================
*/

static const char *Folder( void ) {
	return va( "%s%c%s", Cvar_VariableString( "fs_homepath" ), PATH_SEP, OZ_REPLAY_DIR );
}

// replays/library.json (the Replays page's names and favorites, the Godot build's file) marks this one a favorite
static qboolean Favorite( const char *library, const char *file ) {
	const char *p = library ? strstr( library, va( "\"%s\"", file ) ) : NULL;
	const char *end, *fav;

	if ( !p ) {
		return qfalse;
	}
	end = strchr( p, '}' );
	fav = strstr( p, "\"favorite\"" );
	if ( !fav || ( end && fav > end ) ) {
		return qfalse;
	}
	fav = strchr( fav, ':' );
	while ( fav && ( *++fav == ' ' || *fav == '\t' ) ) {
	}
	return fav && !Q_strncmp( fav, "true", 4 );
}

typedef struct {
	const char	*name;
	double		when;
	qboolean	playtest;
} trimFile_t;

// the moment in a name the recorder made ("2026-11-08_100801_q3dm17_ffa.q3replay"), -1 for any other name
static double NameTime( const char *name ) {
	static const char pattern[] = "dddd-dd-dd_dddddd_";
	struct tm tm;
	time_t t;
	int i;

	for ( i = 0; pattern[i]; i++ ) {
		if ( pattern[i] == 'd' ? ( name[i] < '0' || name[i] > '9' ) : name[i] != pattern[i] ) {
			return -1;
		}
	}
	memset( &tm, 0, sizeof( tm ) );
	tm.tm_year = atoi( name ) - 1900;
	tm.tm_mon = atoi( name + 5 ) - 1;
	tm.tm_mday = atoi( name + 8 );
	tm.tm_hour = ( name[11] - '0' ) * 10 + ( name[12] - '0' );
	tm.tm_min = ( name[13] - '0' ) * 10 + ( name[14] - '0' );
	tm.tm_sec = ( name[15] - '0' ) * 10 + ( name[16] - '0' );
	tm.tm_isdst = -1;
	t = mktime( &tm );
	return t == (time_t)-1 ? -1 : (double)t;
}

static int QDECL NewestFirst( const void *a, const void *b ) {
	const trimFile_t *x = (const trimFile_t *)a, *y = (const trimFile_t *)b;
	if ( x->when != y->when ) {
		return x->when < y->when ? 1 : -1;
	}
	return Q_stricmp( y->name, x->name );
}

/*
Keeps the newest sv_ozReplayKeep replays that aren't favorites. A replay is as old as the date its name
begins with, or as its file when the name is someone else's: sorted by name alone, a replay brought in
under another name counted as the newest forever. The map editor's play-tests (maps named ed_*) are
counted apart, the newest KEEP_PLAYTESTS kept: an evening of them pushed the real matches out.
*/
static void Trim( void ) {
	static qboolean said;
	char dir[MAX_OSPATH], path[MAX_OSPATH], **list, *library = NULL;
	trimFile_t *files;
	qboolean unread = qfalse;
	int n, i, kept = 0, keptTests = 0;
	FILE *f;

	if ( rec.keep->integer <= 0 ) {
		return;
	}
	Q_strncpyz( dir, Folder(), sizeof( dir ) );
	Com_sprintf( path, sizeof( path ), "%s%clibrary.json", dir, PATH_SEP );
	f = Sys_FOpen( path, "rb" );
	if ( f ) {
		fseek( f, 0, SEEK_END );
		n = (int)ftell( f );
		fseek( f, 0, SEEK_SET );
		if ( n > 0 && n < ( 16 << 20 ) ) {
			library = Z_Malloc( n + 1 );
			if ( (int)fread( library, 1, n, f ) != n ) {
				unread = qtrue;
			}
			library[n] = 0;
			// an object from its first brace to its last, or it was cut short
			for ( i = 0; library[i] == ' ' || library[i] == '\t' || library[i] == '\r' || library[i] == '\n'; i++ ) {
			}
			if ( library[i] != '{' ) {
				unread = qtrue;
			}
			for ( i = n - 1; i > 0 && ( library[i] == ' ' || library[i] == '\t' || library[i] == '\r' || library[i] == '\n' ); i-- ) {
			}
			if ( library[i] != '}' ) {
				unread = qtrue;
			}
		} else if ( n != 0 ) {
			unread = qtrue;
		}
		fclose( f );
	}
#ifdef OZ_REPLAY_LIBRARY
	if ( OzLib_Broken() ) {
		unread = qtrue;
	}
#endif
	// A library that doesn't read was taken as an empty one, and the trim deleted every former favorite:
	// nothing is deleted while the favorites aren't known
	if ( unread ) {
		if ( !said ) {
			said = qtrue;
			Com_Printf( S_COLOR_YELLOW "replay: %s/library.json doesn't read, so the favorites aren't known: no old replay is deleted\n", OZ_REPLAY_DIR );
		}
		if ( library ) {
			Z_Free( library );
		}
		return;
	}
	list = Sys_ListFiles( dir, "." OZ_REPLAY_EXT, NULL, &n, qfalse );
	if ( !list || n <= 0 ) {
		Sys_FreeFileList( list );
		if ( library ) {
			Z_Free( library );
		}
		return;
	}
	files = Z_Malloc( n * sizeof( files[0] ) );
	for ( i = 0; i < n; i++ ) {
		files[i].name = list[i];
		files[i].when = NameTime( list[i] );
		files[i].playtest = files[i].when >= 0 && !Q_stricmpn( list[i] + 18, "ed_", 3 );
		if ( files[i].when < 0 ) {
			fileOffset_t size;
			fileTime_t mtime, ctime;
			Com_sprintf( path, sizeof( path ), "%s%c%s", dir, PATH_SEP, list[i] );
			files[i].when = Sys_GetFileStats( path, &size, &mtime, &ctime ) ? (double)mtime : 0;
		}
	}
	qsort( files, n, sizeof( files[0] ), NewestFirst );
	for ( i = 0; i < n; i++ ) {
		const char *name = files[i].name;
		if ( Favorite( library, name ) ) {
			continue;
		}
		// the one just written counts as one of the kept (it didn't, so one more than the setting stayed)
		if ( files[i].playtest ? ++keptTests <= KEEP_PLAYTESTS : ++kept <= rec.keep->integer ) {
			continue;
		}
		if ( !Q_stricmp( name, rec.name ) ) {
			continue;
		}
#ifdef OZ_REPLAY_LIBRARY
		{
			// as the Replays page deletes one: to the Recycle Bin from the player's own folder, and its
			// library entry with it
			char why[128];
			if ( !OzLib_Delete( name, why, sizeof( why ) ) ) {
				Com_Printf( "replay: %s stays: %s\n", name, why );
				continue;
			}
		}
#else
		Com_sprintf( path, sizeof( path ), "%s%c%s", dir, PATH_SEP, name );
		FS_Remove( path );
#endif
		if ( files[i].playtest ) {
			Com_Printf( "replay: deleted %s (the newest %i play-tests are kept)\n", name, KEEP_PLAYTESTS );
		} else {
			Com_Printf( "replay: deleted %s (sv_ozReplayKeep %i)\n", name, rec.keep->integer );
		}
	}
	Z_Free( files );
	Sys_FreeFileList( list );
	if ( library ) {
		Z_Free( library );
	}
}

static qboolean Start( void ) {
	char dir[MAX_OSPATH], base[MAX_QPATH], file[MAX_QPATH];
	qtime_t t;
	int i;

	Q_strncpyz( dir, Folder(), sizeof( dir ) );

	Com_RealTime( &t );
	Q_strncpyz( rec.map, sv_mapname->string, sizeof( rec.map ) );
	Sys_Mkdir( va( "%s", Cvar_VariableString( "fs_homepath" ) ) );
	Sys_Mkdir( dir );
	// a map in a folder of maps/ ("devmap pack/arena") is one file's name here, not a folder's: its
	// slash made every frame's Start fail, a yellow line a frame
	Q_strncpyz( file, rec.map, sizeof( file ) );
	for ( i = 0; file[i]; i++ ) {
		if ( strchr( "/\\:*?\"<>|", file[i] ) || (unsigned char)file[i] < ' ' ) {
			file[i] = '_';
		}
	}
	Com_sprintf( base, sizeof( base ), "%04i-%02i-%02i_%02i%02i%02i_%s_%s", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
				 t.tm_hour, t.tm_min, t.tm_sec, file, ModeKey( Cvar_VariableIntegerValue( "g_gametype" ), qfalse ) );
	for ( i = 1; i < 100; i++ ) {		// two matches in one second: <name>_2, ...
		FILE *f;
		if ( i == 1 ) {
			Com_sprintf( rec.name, sizeof( rec.name ), "%s.%s", base, OZ_REPLAY_EXT );
		} else {
			Com_sprintf( rec.name, sizeof( rec.name ), "%s_%i.%s", base, i, OZ_REPLAY_EXT );
		}
		Com_sprintf( rec.path, sizeof( rec.path ), "%s%c%s", dir, PATH_SEP, rec.name );
		f = Sys_FOpen( rec.path, "rb" );
		if ( !f ) {
			break;
		}
		fclose( f );
	}
	rec.file = Sys_FOpen( rec.path, "wb" );
	if ( !rec.file ) {
		Com_Printf( S_COLOR_YELLOW "replay: can't write %s; this map isn't recorded\n", rec.path );
		rec.halted = qtrue;		// said once, not every frame until the map ends
		return qfalse;
	}
	for ( i = 0; i < MAX_CONFIGSTRINGS; i++ ) {
		if ( rec.cs[i] ) {
			Z_Free( rec.cs[i] );
			rec.cs[i] = NULL;
		}
	}
	Com_Memset( played, 0, sizeof( played ) );
	rec.active = qtrue;
	rec.intermission = qfalse;
	rec.nextTime = 0;
	rec.frames = 0;
	rec.bytes = 0;
	rec.startTime = sv.time;
	ClearPending();
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		rec.cmdSeq[i] = i < sv.maxclients ? svs.clients[i].reliableSequence : 0;
	}
	Com_Memset( recLast.present, 0, sizeof( recLast.present ) );
	Com_Memset( recLast.psPresent, 0, sizeof( recLast.psPresent ) );
	recLast.time = sv.time;
	WriteHeader( &t );
	Com_Printf( "replay: recording %s/%s\n", OZ_REPLAY_DIR, rec.name );
	return qtrue;
}

// where each client is at a keyframe (sv_ozReplayLog 1): the positions playback has to land on
static void LogKeyframe( void ) {
	int i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( recCur.psPresent[i] ) {
			const playerState_t *ps = &recCur.ps[i];
			Com_Printf( "replay: key %i client %i at %.3f %.3f %.3f\n", recCur.time, i, ps->origin[0], ps->origin[1], ps->origin[2] );
		}
	}
}


/*
=============================================================================
the server's calls
=============================================================================
*/

void SV_OzReplayStop( const char *why ) {
	int i;

	if ( strcmp( why, "error" ) ) {
		rec.halted = qfalse;	// a map ends: the next one records again
	}
	if ( !rec.active ) {
		return;
	}
	rec.active = qfalse;	// first: an error in the last write mustn't leave it recording
	if ( sv.time > recLast.time && rec.frames ) {
		HarvestCommands();
		if ( rec.numPending ) {		// commands that came after the last frame go out in one more
			recCur.time = sv.time;
			GatherEntities( &recCur );
			GatherPlayers( &recCur );
			if ( WriteFrame( qfalse ) ) {
				Com_Memcpy( &recLast, &recCur, sizeof( recLast ) );
				rec.frames++;
			}
		}
	}
	WriteTrailer( why );
	fclose( rec.file );
	rec.file = NULL;
	Com_Printf( "replay: wrote %s/%s: %i frames, %.1f s, %i KB (%s)\n", OZ_REPLAY_DIR, rec.name, rec.frames,
				( recLast.time - rec.startTime ) / 1000.0f, ( rec.bytes + 1023 ) / 1024, why );
	for ( i = 0; i < MAX_CONFIGSTRINGS; i++ ) {
		if ( rec.cs[i] ) {
			Z_Free( rec.cs[i] );
			rec.cs[i] = NULL;
		}
	}
	// Under 10 s it isn't kept, as the client's recorder has it (cl_oz_demos.c MIN_LENGTH_MS): every
	// map load with a player wrote a replay, so an evening of the map editor's play-tests or quick
	// devmaps filled sv_ozReplayKeep's 50 and the trim below deleted the real matches for them
	if ( recLast.time - rec.startTime < 10000 ) {
		remove( rec.path );
		Com_Printf( "replay: %s was under 10 s, not kept\n", rec.name );
		return;
	}
	Trim();
}

/*
map_restart, before the game restarts (sv_ccmds.c). After an intermission it starts the next match on the
same map, as a duel server does after every match: that match gets a replay of its own, as the client's
recorder gives it one (cl_oz_demos.c). Back-to-back matches were one file and one row of the Replays page.
A restart in a warmup or mid-match doesn't split.
*/
void SV_OzReplayRestart( void ) {
	if ( rec.active && rec.intermission ) {
		SV_OzReplayStop( "match end" );
	}
}

// the replay this server is writing, as its file's name ("" when none): the replay library links the
// match's report to it (cl_oz_demos.c)
const char *SV_OzReplayCurrent( void ) {
	return rec.active ? rec.name : "";
}

void SV_OzReplayFrame( void ) {
	qboolean key;
	int i;

	if ( !rec.enabled ) {
		rec.enabled = Cvar_Get( "sv_ozReplay", "1", CVAR_ARCHIVE_ND );
		Cvar_SetDescription( rec.enabled, "Records every match with a player in it to the replays folder." );
		rec.keep = Cvar_Get( "sv_ozReplayKeep", "50", CVAR_ARCHIVE_ND );
		Cvar_SetDescription( rec.keep, "The newest replays kept; favorites are never deleted. 0 keeps every one." );
		rec.log = Cvar_Get( "sv_ozReplayLog", "0", CVAR_TEMP );
		Cvar_SetDescription( rec.log, "Prints where each client is at every replay keyframe." );
	}
	if ( sv.state != SS_GAME || !svs.clients ) {
		return;
	}
	if ( rec.active && ( strcmp( rec.map, sv_mapname->string ) || sv.time < recLast.time ) ) {
		SV_OzReplayStop( "map change" );	// another map, or the same one loaded again: this match is over
	}
	if ( !rec.active ) {
		if ( !rec.enabled->integer || rec.halted ) {
			return;
		}
		for ( i = 0; i < sv.maxclients && svs.clients[i].state != CS_ACTIVE; i++ ) {
		}
		if ( i == sv.maxclients || !Start() ) {
			return;
		}
	}

	HarvestCommands();
	if ( sv.time < rec.nextTime ) {
		return;
	}
	recCur.time = sv.time;
	GatherEntities( &recCur );
	GatherPlayers( &recCur );
	key = rec.frames == 0 || sv.time - rec.lastKey >= OZ_REPLAY_KEYFRAME_MSEC;
	if ( !WriteFrame( key ) ) {
		Com_Printf( S_COLOR_YELLOW "replay: the frame at %i ms didn't fit or couldn't be written; this map isn't recorded further\n", sv.time );
		SV_OzReplayStop( "error" );
		rec.halted = qtrue;
		return;
	}
	if ( key ) {
		rec.lastKey = sv.time;
		if ( rec.log->integer ) {
			LogKeyframe();
		}
	}
	ClearPending();
	Com_Memcpy( &recLast, &recCur, sizeof( recLast ) );
	rec.frames++;
	rec.nextTime = ( sv.time / ( 1000 / OZ_REPLAY_HZ ) + 1 ) * ( 1000 / OZ_REPLAY_HZ );
}
