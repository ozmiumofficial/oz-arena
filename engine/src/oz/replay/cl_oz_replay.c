/*
===========================================================================
Oz Arena: replay playback (ENGINE_PLAN.md P10 step 2). GPL-2.0.

The Godot build's decoder (q3core's q3_replay.c, its playback half) in the client. A replay plays as a
demo through CNQ3's new demo player (cl_ndp.c): this file reads the file, keeps its configstrings and
commands in tables, and decodes frames, one after another or from the keyframe before one; cl_ndp.c
builds the snapshot of the client being watched from each frame and hands it to the cgame through the
normal snapshot syscalls, so the cgame plays it as a demo. Its keyframes are the player's seek points.

The engine runs the clock for Oz's own cgame (cl_ndp.c's engine clock), so the commands below seek,
pause and step any demo Oz's game plays, .dm_68 included; CPMA runs its own (demo_seek).

  replay [<name> [<client>]]	plays <fs_homepath>/replays/<name>.q3replay from that client's view
								(the first player in it by default); with no name, lists them
  replay_view <client>			watches another player
  replay_seek <s>|+<s>|-<s>		jumps to s seconds in, or that far ahead or back
  replay_pause [0|1]			stops and starts the clock (toggles without an argument)
  replay_step					one snapshot ahead, paused
  replay_info					where playback is, and who's in the replay
===========================================================================
*/
#include "../../../quake3e/code/client/client.h"
#include "oz_replay.h"

typedef struct {
	int		frame, index;
	char	*text;
} csChange_t;

typedef struct {
	int			frame;
	unsigned	mask[2];
	char		*text;
} replayCmd_t;

static struct {
	qboolean	open;
	char		name[MAX_QPATH];
	char		map[MAX_QPATH];
	char		game[MAX_QPATH];
	byte		*data;
	int			len;
	int			numFrames;
	int			*offset, *size, *time;
	byte		*kind;
	csChange_t	*changes;
	int			numChanges;
	replayCmd_t	*cmds;
	int			numCmds;
	int			*firstCmd;					// each frame's first command; numFrames + 1 entries
	ozReplayFrame_t *frame, *scratch;		// the frame last decoded, and the one being decoded
	int			decoded;					// the frame in `frame`, -1 for none
	const char	*cs[MAX_CONFIGSTRINGS];		// the configstrings as of csFrame
	int			csTime[MAX_CONFIGSTRINGS];	// the time each last changed (INT_MIN: never set)
	int			csApplied, csFrame;
	int			view;
} rp;

static const playerState_t nullPS;


static unsigned GetU32( const byte *p ) {
	return p[0] | ( p[1] << 8 ) | ( p[2] << 16 ) | ( (unsigned)p[3] << 24 );
}


// text written as its bytes (sv_oz_replay.c WriteText); NULL on bad data
static const char *ReadText( msg_t *msg ) {
	static char buf[BIG_INFO_STRING];
	const int n = MSG_ReadShort( msg );
	int i;

	if ( n < 0 || n >= (int)sizeof( buf ) ) {
		return NULL;
	}
	for ( i = 0; i < n; i++ ) {
		buf[i] = (char)MSG_ReadByte( msg );
	}
	buf[n] = 0;
	return msg->readcount > msg->cursize ? NULL : buf;
}


static char *CopyText( const char *s ) {
	const size_t n = strlen( s ) + 1;
	char *c = malloc( n );
	if ( c ) {
		memcpy( c, s, n );
	}
	return c;
}


// room for `need` items in *p; qfalse (and *p untouched) if memory ran out
static qboolean Grow( void **p, int *cap, int need, int item ) {
	void *n;
	int c;

	if ( need <= *cap ) {
		return qtrue;
	}
	c = *cap ? *cap * 2 : 256;
	while ( c < need ) {
		c *= 2;
	}
	n = realloc( *p, (size_t)c * item );
	if ( !n ) {
		return qfalse;
	}
	*p = n;
	*cap = c;
	return qtrue;
}


static void StartReading( msg_t *msg, int frame ) {
	MSG_Init( msg, rp.data + rp.offset[frame], rp.size[frame] );
	msg->cursize = rp.size[frame];
	MSG_BeginReading( msg );
}


// the configstring and command sections of frame i, into the tables (the pass at open); qfalse on bad data
static qboolean ScanFrame( int i, int *capChanges, int *capCmds ) {
	msg_t msg;
	int n, k;

	StartReading( &msg, i );
	n = MSG_ReadShort( &msg );
	for ( k = 0; k < n; k++ ) {
		const int idx = MSG_ReadShort( &msg );
		const char *s = ReadText( &msg );
		char *text;
		if ( idx < 0 || idx >= MAX_CONFIGSTRINGS || !s ) {
			return qfalse;
		}
		text = CopyText( s );
		if ( !text || !Grow( (void **)&rp.changes, capChanges, rp.numChanges + 1, sizeof( csChange_t ) ) ) {
			free( text );
			return qfalse;
		}
		rp.changes[rp.numChanges].frame = i;
		rp.changes[rp.numChanges].index = idx;
		rp.changes[rp.numChanges].text = text;
		rp.numChanges++;
	}
	rp.firstCmd[i] = rp.numCmds;
	n = MSG_ReadShort( &msg );
	for ( k = 0; k < n; k++ ) {
		unsigned m[2];
		const char *s;
		char *text;
		m[0] = (unsigned)MSG_ReadLong( &msg );
		m[1] = (unsigned)MSG_ReadLong( &msg );
		s = ReadText( &msg );
		if ( !s ) {
			return qfalse;
		}
		text = CopyText( s );
		if ( !text || !Grow( (void **)&rp.cmds, capCmds, rp.numCmds + 1, sizeof( replayCmd_t ) ) ) {
			free( text );
			return qfalse;
		}
		rp.cmds[rp.numCmds].frame = i;
		rp.cmds[rp.numCmds].mask[0] = m[0];
		rp.cmds[rp.numCmds].mask[1] = m[1];
		rp.cmds[rp.numCmds].text = text;
		rp.numCmds++;
	}
	return msg.readcount <= msg.cursize;
}


/*
Decodes frame i over `base` (ignored for a keyframe) into `out`. The configstrings and commands are the
tables' already, so their sections are only read past. qfalse on bad data.
*/
static qboolean DecodeFrame( int i, const ozReplayFrame_t *base, ozReplayFrame_t *out ) {
	const qboolean key = rp.kind[i];
	msg_t msg;
	int n, k, e;
	unsigned masks[2];

	if ( base && !key ) {
		Com_Memcpy( out, base, sizeof( *out ) );
	} else {
		Com_Memset( out->present, 0, sizeof( out->present ) );
		Com_Memset( out->psPresent, 0, sizeof( out->psPresent ) );
	}
	Com_Memset( out->filter, 0, sizeof( out->filter ) );
	out->time = rp.time[i];
	StartReading( &msg, i );

	n = MSG_ReadShort( &msg );
	for ( k = 0; k < n; k++ ) {
		MSG_ReadShort( &msg );
		if ( !ReadText( &msg ) ) {
			return qfalse;
		}
	}
	n = MSG_ReadShort( &msg );
	for ( k = 0; k < n; k++ ) {
		MSG_ReadLong( &msg );
		MSG_ReadLong( &msg );
		if ( !ReadText( &msg ) ) {
			return qfalse;
		}
	}

	masks[0] = (unsigned)MSG_ReadLong( &msg );
	masks[1] = (unsigned)MSG_ReadLong( &msg );
	for ( k = 0; k < MAX_CLIENTS; k++ ) {
		playerState_t from;
		if ( !( ( masks[k >> 5] >> ( k & 31 ) ) & 1 ) ) {
			out->psPresent[k] = 0;
			continue;
		}
		from = ( !key && base && base->psPresent[k] ) ? base->ps[k] : nullPS;
		MSG_ReadDeltaPlayerstate( &msg, &from, &out->ps[k] );
		out->psPresent[k] = 1;
	}

	for ( ;; ) {
		entityState_t from;
		e = MSG_ReadBits( &msg, GENTITYNUM_BITS );
		if ( e == MAX_GENTITIES - 1 ) {
			break;
		}
		if ( msg.readcount > msg.cursize || e < 0 || e >= MAX_GENTITIES - 1 ) {
			return qfalse;
		}
		if ( !key && out->present[e] ) {
			from = out->ents[e];
		} else {
			Com_Memset( &from, 0, sizeof( from ) );
			from.number = e;
		}
		MSG_ReadDeltaEntity( &msg, &from, &out->ents[e], e );
		out->present[e] = out->ents[e].number != MAX_GENTITIES - 1;
	}

	n = MSG_ReadShort( &msg );
	for ( k = 0; k < n; k++ ) {
		int kind, client;
		e = MSG_ReadShort( &msg );
		kind = MSG_ReadByte( &msg );
		client = MSG_ReadByte( &msg );
		if ( e >= 0 && e < MAX_GENTITIES ) {
			out->filter[e] = (byte)kind;
			out->filterClient[e] = (byte)client;
		}
	}
	return MSG_ReadLong( &msg ) == OZ_REPLAY_FRAME_END && msg.readcount <= msg.cursize;
}


// the configstrings as of `frame`
static void ApplyChanges( int frame ) {
	int i;

	if ( frame < rp.csFrame ) {
		for ( i = 0; i < MAX_CONFIGSTRINGS; i++ ) {
			rp.cs[i] = NULL;
			rp.csTime[i] = INT_MIN;
		}
		rp.csApplied = 0;
	}
	while ( rp.csApplied < rp.numChanges && rp.changes[rp.csApplied].frame <= frame ) {
		const csChange_t *c = &rp.changes[rp.csApplied++];
		rp.cs[c->index] = c->text;
		rp.csTime[c->index] = rp.time[c->frame];
	}
	rp.csFrame = frame;
}


/*
=============================================================================
what cl_ndp.c reads
=============================================================================
*/

qboolean CL_OzReplay_Playing( void ) {
	return rp.open;
}

int CL_OzReplay_Frames( void ) {
	return rp.numFrames;
}

int CL_OzReplay_FrameTime( int frame ) {
	return frame >= 0 && frame < rp.numFrames ? rp.time[frame] : 0;
}

qboolean CL_OzReplay_IsKeyframe( int frame ) {
	return frame >= 0 && frame < rp.numFrames && rp.kind[frame];
}

int CL_OzReplay_View( void ) {
	return rp.view;
}

/*
Frame `frame`, decoded: the next one after the last decoded goes over it, anything else starts again at
the keyframe at or before it (or carries on from the last one decoded, when that's on the way). NULL on
bad data.
*/
const ozReplayFrame_t *CL_OzReplay_Decode( int frame ) {
	ozReplayFrame_t *swap;
	int k, i;

	if ( !rp.open || frame < 0 || frame >= rp.numFrames ) {
		return NULL;
	}
	if ( frame != rp.decoded ) {
		k = frame;
		while ( k > 0 && !rp.kind[k] ) {
			k--;
		}
		i = rp.decoded >= k && rp.decoded < frame ? rp.decoded + 1 : k;
		for ( ; i <= frame; i++ ) {
			if ( !DecodeFrame( i, i == k ? NULL : rp.frame, rp.scratch ) ) {
				rp.decoded = -1;
				return NULL;
			}
			swap = rp.frame; rp.frame = rp.scratch; rp.scratch = swap;
		}
		rp.decoded = frame;
	}
	ApplyChanges( frame );
	return rp.frame;
}

const char *CL_OzReplay_ConfigString( int index, int *changedTime ) {
	if ( index < 0 || index >= MAX_CONFIGSTRINGS || !rp.cs[index] ) {
		*changedTime = INT_MIN;
		return "";
	}
	*changedTime = rp.csTime[index];
	return rp.cs[index];
}

int CL_OzReplay_Commands( int frame, int client, const char **out, int max ) {
	int i, n = 0;

	if ( frame < 0 || frame >= rp.numFrames || client < 0 || client >= MAX_CLIENTS ) {
		return 0;
	}
	for ( i = rp.firstCmd[frame]; i < rp.firstCmd[frame + 1] && n < max; i++ ) {
		if ( rp.cmds[i].mask[client >> 5] & ( 1u << ( client & 31 ) ) ) {
			out[n++] = rp.cmds[i].text;
		}
	}
	return n;
}

void CL_OzReplay_Close( void ) {
	int i;

	for ( i = 0; i < rp.numChanges; i++ ) {
		free( rp.changes[i].text );
	}
	for ( i = 0; i < rp.numCmds; i++ ) {
		free( rp.cmds[i].text );
	}
	free( rp.changes );
	free( rp.cmds );
	free( rp.firstCmd );
	free( rp.offset );
	free( rp.size );
	free( rp.time );
	free( rp.kind );
	free( rp.frame );
	free( rp.scratch );
	free( rp.data );
	Com_Memset( &rp, 0, sizeof( rp ) );
	rp.decoded = -1;
}


/*
=============================================================================
opening a file
=============================================================================
*/

// a string field of the header's JSON (the keys the recorder writes have no escapes to worry about)
static void HeaderField( const char *json, int len, const char *key, char *out, int size ) {
	const char *p, *end = json + len;
	char k[64];
	int n = 0, kl;

	out[0] = 0;
	Com_sprintf( k, sizeof( k ), "\"%s\"", key );
	kl = (int)strlen( k );
	for ( p = json; p + kl <= end && memcmp( p, k, kl ); p++ ) {
	}
	if ( p + kl > end ) {
		return;
	}
	p += kl;
	while ( p < end && ( *p == ' ' || *p == ':' ) ) {
		p++;
	}
	if ( p >= end || *p != '"' ) {
		return;
	}
	for ( p++; p < end && *p != '"' && n < size - 1; p++ ) {
		if ( *p == '\\' && p + 1 < end ) {
			p++;
		}
		out[n++] = *p;
	}
	out[n] = 0;
}

// the file at `path`, read and indexed; qfalse with the reason printed
static qboolean Open( const char *path, const char *name ) {
	int capOffsets = 0, capChanges = 0, capCmds = 0, pos, hlen, i;
	FILE *f;
	long n;

	CL_OzReplay_Close();
	f = Sys_FOpen( path, "rb" );
	if ( !f ) {
		Com_Printf( "replay: can't open %s\n", path );
		return qfalse;
	}
	fseek( f, 0, SEEK_END );
	n = ftell( f );
	fseek( f, 0, SEEK_SET );
	rp.data = n > 12 ? malloc( n ) : NULL;
	rp.len = rp.data ? (int)fread( rp.data, 1, n, f ) : 0;
	fclose( f );
	if ( rp.len < 12 || memcmp( rp.data, OZ_REPLAY_MAGIC, 4 ) || GetU32( rp.data + 4 ) != OZ_REPLAY_VERSION ) {
		Com_Printf( "replay: %s isn't a replay\n", name );
		CL_OzReplay_Close();
		return qfalse;
	}
	hlen = (int)GetU32( rp.data + 8 );
	if ( hlen < 0 || hlen > rp.len - 12 ) {
		Com_Printf( "replay: %s is damaged\n", name );
		CL_OzReplay_Close();
		return qfalse;
	}
	HeaderField( (const char *)rp.data + 12, hlen, "map", rp.map, sizeof( rp.map ) );
	HeaderField( (const char *)rp.data + 12, hlen, "game", rp.game, sizeof( rp.game ) );

	// the blocks: each is length-prefixed, so the file is its own index; a recording cut short ends at its
	// last whole frame, and the trailer isn't a block (its first bytes don't make a kind 0 or 1)
	for ( pos = 12 + hlen; pos + OZ_REPLAY_BLOCK_HEADER <= rp.len; ) {
		const int size = (int)GetU32( rp.data + pos );
		if ( size <= 0 || size > rp.len - pos - OZ_REPLAY_BLOCK_HEADER || rp.data[pos + 4] > 1 ) {
			break;
		}
		if ( !Grow( (void **)&rp.offset, &capOffsets, rp.numFrames + 1, sizeof( int ) ) ) {
			break;
		}
		rp.offset[rp.numFrames++] = pos + OZ_REPLAY_BLOCK_HEADER;
		pos += OZ_REPLAY_BLOCK_HEADER + size;
	}
	rp.size = malloc( sizeof( int ) * ( rp.numFrames + 1 ) );
	rp.time = malloc( sizeof( int ) * ( rp.numFrames + 1 ) );
	rp.kind = malloc( rp.numFrames + 1 );
	rp.firstCmd = malloc( sizeof( int ) * ( rp.numFrames + 1 ) );
	rp.frame = malloc( sizeof( ozReplayFrame_t ) );
	rp.scratch = malloc( sizeof( ozReplayFrame_t ) );
	if ( !rp.numFrames || !rp.size || !rp.time || !rp.kind || !rp.firstCmd || !rp.frame || !rp.scratch ) {
		Com_Printf( "replay: %s has no frames\n", name );
		CL_OzReplay_Close();
		return qfalse;
	}
	for ( i = 0; i < rp.numFrames; i++ ) {
		const byte *h = rp.data + rp.offset[i] - OZ_REPLAY_BLOCK_HEADER;
		rp.size[i] = (int)GetU32( h );
		rp.kind[i] = h[4];
		rp.time[i] = (int)GetU32( h + 5 );
		if ( ( i && rp.time[i] < rp.time[i - 1] ) || !ScanFrame( i, &capChanges, &capCmds ) ) {
			rp.numFrames = i;		// keep what reads
			break;
		}
	}
	if ( !rp.numFrames || !rp.kind[0] ) {
		Com_Printf( "replay: %s is damaged at its start\n", name );
		CL_OzReplay_Close();
		return qfalse;
	}
	rp.firstCmd[rp.numFrames] = rp.numCmds;
	rp.decoded = -1;
	rp.csFrame = INT_MAX;		// ApplyChanges starts from nothing
	ApplyChanges( -1 );
	Q_strncpyz( rp.name, name, sizeof( rp.name ) );
	rp.open = qtrue;
	return qtrue;
}


/*
=============================================================================
the commands
=============================================================================
*/

static const char *Folder( void ) {
	return va( "%s%c%s", Cvar_VariableString( "fs_homepath" ), PATH_SEP, OZ_REPLAY_DIR );
}

// a player's name in the replay as of now, "" if the slot is empty
static const char *PlayerName( int client ) {
	int t;
	const char *info = CL_OzReplay_ConfigString( CS_PLAYERS + client, &t );
	return Info_ValueForKey( info, "n" );
}

static void Replay_f( void ) {
	char name[MAX_QPATH], path[MAX_OSPATH];
	const ozReplayFrame_t *f;
	int view, i;

	if ( Cmd_Argc() < 2 ) {
		char **list;
		int n;
		list = Sys_ListFiles( Folder(), "." OZ_REPLAY_EXT, NULL, &n, qfalse );
		Com_Printf( "replay <name> [<client>]: %i in %s\n", n, Folder() );
		for ( i = 0; i < n; i++ ) {
			Com_Printf( "  %s\n", list[i] );
		}
		Sys_FreeFileList( list );
		return;
	}
	Q_strncpyz( name, Cmd_Argv( 1 ), sizeof( name ) );
	if ( strstr( name, ".." ) || strchr( name, '/' ) || strchr( name, '\\' ) || strchr( name, ':' ) ) {
		Com_Printf( "replay: a name in %s, without a folder\n", Folder() );
		return;
	}
	if ( !strstr( name, "." OZ_REPLAY_EXT ) ) {
		Q_strcat( name, sizeof( name ), "." OZ_REPLAY_EXT );
	}
	Com_sprintf( path, sizeof( path ), "%s%c%s", Folder(), PATH_SEP, name );
	view = Cmd_Argc() > 2 ? atoi( Cmd_Argv( 2 ) ) : -1;

	// a file that isn't there, or isn't a replay, leaves the game as it was
	{
		FILE *f0 = Sys_FOpen( path, "rb" );
		byte magic[4] = { 0 };
		if ( f0 ) {
			if ( fread( magic, 1, 4, f0 ) != 4 ) {
				magic[0] = 0;
			}
			fclose( f0 );
		}
		if ( memcmp( magic, OZ_REPLAY_MAGIC, 4 ) ) {
			Com_Printf( "replay: no replay %s in %s\n", name, Folder() );
			return;
		}
	}
	CL_Disconnect( qtrue );		// closes the replay that was playing
	Cvar_Set( "sv_killserver", "2" );		// a local server stops; 2: the client isn't disconnected again
	SV_Frame( 0 );
	if ( !Open( path, name ) ) {
		return;
	}
	if ( rp.game[0] && Q_stricmp( rp.game, BASEGAME ) && Q_stricmp( rp.game, Cvar_VariableString( "fs_game" ) ) ) {
		Com_Printf( S_COLOR_YELLOW "replay: %s was recorded in %s; plays in %s\n", name, rp.game,
					Cvar_VariableString( "fs_game" ) );
	}
	f = CL_OzReplay_Decode( 0 );
	if ( view < 0 || view >= MAX_CLIENTS || !f || !f->psPresent[view] ) {
		for ( i = 0; f && i < MAX_CLIENTS && !f->psPresent[i]; i++ ) {
		}
		view = i < MAX_CLIENTS ? i : 0;
	}
	rp.view = view;

	Com_Printf( "replay: %s, %s, %i frames, %.1f s, watching %i (%s)\n", name, rp.map, rp.numFrames,
				( rp.time[rp.numFrames - 1] - rp.time[0] ) / 1000.0f, view, PlayerName( view ) );
	Q_strncpyz( clc.demoName, name, sizeof( clc.demoName ) );
	Q_strncpyz( cls.servername, name, sizeof( cls.servername ) );
	Con_Close();
	cls.state = CA_CONNECTED;
	clc.demoplaying = qtrue;
	clc.compat = qfalse;
	if ( !CL_NDP_StartReplay() ) {
		CL_Disconnect( qtrue );
	}
}

static qboolean EngineClock( void ) {
	if ( !CL_NDP_EngineClock() ) {
		Com_Printf( "replay: nothing is playing that Oz's clock runs (a replay, or a demo of Oz's game)\n" );
		return qfalse;
	}
	return qtrue;
}

static void ReplayView_f( void ) {
	const ozReplayFrame_t *f;
	int view;

	if ( !rp.open || !CL_NDP_EngineClock() ) {
		Com_Printf( "replay: no replay is playing\n" );
		return;
	}
	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "replay_view <client>: watching %i (%s)\n", rp.view, PlayerName( rp.view ) );
		return;
	}
	view = atoi( Cmd_Argv( 1 ) );
	f = rp.decoded >= 0 ? rp.frame : NULL;
	if ( view < 0 || view >= MAX_CLIENTS || ( f && !f->psPresent[view] ) ) {
		Com_Printf( "replay: no player %i in this replay now\n", view );
		return;
	}
	rp.view = view;
	Com_Printf( "replay: watching %i (%s)\n", view, PlayerName( view ) );
	CL_NDP_Jump( cl.serverTime );		// the snapshots built again from their eyes, from where playback is
}

static void ReplaySeek_f( void ) {
	const char *arg = Cmd_Argv( 1 );
	int first, last, t;

	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "replay_seek <seconds in>|+<seconds>|-<seconds>\n" );
		return;
	}
	if ( !EngineClock() ) {
		return;
	}
	CL_NDP_TimeRange( &first, &last );
	if ( arg[0] == '+' || arg[0] == '-' ) {
		t = cl.serverTime + (int)( atof( arg ) * 1000.0f );
	} else {
		t = first + (int)( atof( arg ) * 1000.0f );
	}
	t = CL_NDP_Jump( t );
	Com_Printf( "replay: at %i (%.3f s in)\n", t, ( t - first ) / 1000.0f );
}

static void ReplayPause_f( void ) {
	if ( EngineClock() ) {
		CL_NDP_Pause( Cmd_Argc() > 1 ? atoi( Cmd_Argv( 1 ) ) : -1 );
	}
}

static void ReplayStep_f( void ) {
	if ( EngineClock() ) {
		CL_NDP_Step();
	}
}

static void ReplayInfo_f( void ) {
	const ozReplayFrame_t *f = rp.open && rp.decoded >= 0 ? rp.frame : NULL;
	int first, last, i;

	if ( !EngineClock() ) {
		return;
	}
	CL_NDP_TimeRange( &first, &last );
	Com_Printf( "replay: %s at %.3f of %.3f s%s\n", clc.demoName, ( cl.serverTime - first ) / 1000.0f,
				( last - first ) / 1000.0f, CL_NDP_Pause( -2 ) ? ", paused" : "" );
	if ( !f ) {
		return;
	}
	Com_Printf( "  %s, %i frames, watching %i (%s)\n", rp.map, rp.numFrames, rp.view, PlayerName( rp.view ) );
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( f->psPresent[i] ) {
			Com_Printf( "  %2i %s\n", i, PlayerName( i ) );
		}
	}
}

void CL_OzReplay_Init( void ) {
	rp.decoded = -1;
	Cvar_Get( "cl_ozDemoSeek", "0", CVAR_TEMP );		// Oz's cgame says it resets on a seek (CL_InitCGame)
	Cmd_AddCommand( "replay", Replay_f );
	Cmd_AddCommand( "replay_view", ReplayView_f );
	Cmd_AddCommand( "replay_seek", ReplaySeek_f );
	Cmd_AddCommand( "replay_pause", ReplayPause_f );
	Cmd_AddCommand( "replay_step", ReplayStep_f );
	Cmd_AddCommand( "replay_info", ReplayInfo_f );
}
