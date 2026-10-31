/*
===========================================================================
Oz Arena: the menu's music (ENGINE_PLAN.md P3), after the Godot build's Q3MenuMusic. GPL-2.0.

Out of a match, under Oz's own game, id's soundtrack in the player's paks plays shuffled:
music/fla22k_* and sonic*, .wav or .ogg (not the single-player postgame's win and loss, nor the
music or ambience map packs in the install bring), a track's _intro and _loop as one song, each
heard once a round. oz_audio_menu_music 0 (Settings > Audio > Menu music)
stops it. A match's own music (the map's, from the cgame) takes over when one starts; a
cinematic and the map editor silence it, and the next song starts after.
===========================================================================
*/
#include "client.h"
#include "snd_codec.h"

#define MAX_OZ_SONGS	64

static cvar_t	*oz_menuMusic;

static struct {
	char	intro[MAX_QPATH];	// a song's first or only file
	char	loop[MAX_QPATH];	// its _loop, played once after the _intro
	int		msec;				// both together
} songs[MAX_OZ_SONGS];
static int	numSongs;
static int	order[MAX_OZ_SONGS], next;	// this round's shuffle
static int	playingUntil;				// cls.realtime the song ends, 0 while none plays
static char	listedGame[MAX_QPATH];		// the game the list is of


static int SongMsec( const char *name ) {
	snd_stream_t *s;
	int msec = 0;

	if ( !name[0] ) {
		return 0;
	}
	s = S_CodecOpenStream( name );
	if ( s ) {
		if ( s->info.rate > 0 ) {
			msec = (int)( (double)s->info.samples * 1000.0 / s->info.rate );
		}
		S_CodecCloseStream( s );
	}
	return msec;
}


static void ListSongs( void ) {
	static const char *exts[] = { ".wav", ".ogg" };
	char **files, base[MAX_QPATH], *p;
	int i, j, k, n;

	numSongs = 0;
	Q_strncpyz( listedGame, FS_GetCurrentGameDir(), sizeof( listedGame ) );
	for ( k = 0; k < ARRAY_LEN( exts ); k++ ) {
		files = FS_ListFiles( "music", exts[k], &n );
		for ( i = 0; i < n && numSongs < MAX_OZ_SONGS; i++ ) {
			COM_StripExtension( files[i], base, sizeof( base ) );
			Q_strlwr( base );
			if ( Q_stricmpn( base, "fla22k_", 7 ) && Q_stricmpn( base, "sonic", 5 ) ) {
				continue;	// not id's soundtrack: the postgame's win and loss, map packs' music and loops
			}
			p = strstr( base, "_loop" );
			if ( p && p[5] == '\0' ) {
				continue;	// played after its _intro
			}
			for ( j = 0; j < numSongs; j++ ) {
				if ( !Q_stricmpn( songs[j].intro + 6, base, strlen( base ) ) &&
					 songs[j].intro[6 + strlen( base )] == '.' ) {
					break;	// the same song in the other format
				}
			}
			if ( j < numSongs ) {
				continue;
			}
			Com_sprintf( songs[numSongs].intro, sizeof( songs[0].intro ), "music/%s", files[i] );
			songs[numSongs].loop[0] = '\0';
			p = strstr( base, "_intro" );
			if ( p && p[6] == '\0' ) {
				*p = '\0';
				Com_sprintf( songs[numSongs].loop, sizeof( songs[0].loop ), "music/%s_loop%s", base, exts[k] );
				if ( FS_FOpenFileRead( songs[numSongs].loop, NULL, qfalse ) <= 0 ) {
					songs[numSongs].loop[0] = '\0';
				}
			}
			songs[numSongs].msec = SongMsec( songs[numSongs].intro ) + SongMsec( songs[numSongs].loop );
			if ( songs[numSongs].msec > 0 ) {
				numSongs++;
			}
		}
		FS_FreeFileList( files );
	}
	next = numSongs;	// shuffle at the first song
	Com_Printf( "menu music: %d songs\n", numSongs );}


static void Shuffle( void ) {
	static int seed;
	int i, j, t;

	if ( !seed ) {
		seed = Com_RealTime( NULL ) ^ Sys_Milliseconds();	// a new order each launch
	}
	for ( i = 0; i < numSongs; i++ ) {
		order[i] = i;
	}
	for ( i = numSongs - 1; i > 0; i-- ) {
		j = ( Q_rand( &seed ) & 0x7fffffff ) % ( i + 1 );
		t = order[i]; order[i] = order[j]; order[j] = t;
	}
	next = 0;
}


/*
==================
CL_OzMusicStop

The song's track goes (a match or a cinematic starts, the map editor opens, or the setting went off).
==================
*/
static void CL_OzMusicStop( void ) {
	if ( playingUntil ) {
		S_StopBackgroundTrack();
		playingUntil = 0;
	}
}


/*
==================
CL_OzMusicFrame

Every client frame.
==================
*/
void CL_OzMusicFrame( void ) {
	const char *game;
	qboolean editor = qfalse;

	if ( !oz_menuMusic ) {
		oz_menuMusic = Cvar_Get( "oz_audio_menu_music", "1", CVAR_ARCHIVE );
	}
#ifdef USE_OZ_UI
	editor = OzUI_EditorOpen() ? qtrue : qfalse;	// the mapper works in quiet
#endif
	game = FS_GetCurrentGameDir();
	if ( cls.state != CA_DISCONNECTED || !oz_menuMusic->integer || !cls.soundStarted || editor ||
		 ( Q_stricmp( game, "oz" ) && Q_stricmp( game, BASEGAME ) ) ) {
		if ( cls.state == CA_DISCONNECTED || cls.state == CA_CINEMATIC ) {
			CL_OzMusicStop();
		} else {
			playingUntil = 0;	// the match's own music has the track now
		}
		return;
	}
	if ( playingUntil && cls.realtime < playingUntil ) {
		return;
	}
	if ( Q_stricmp( listedGame, game ) ) {
		ListSongs();	// the first time, and after a switch of mods
	}
	if ( !numSongs ) {
		return;
	}
	if ( next >= numSongs ) {
		Shuffle();
	}
	Com_Printf( "menu music: %s%s%s (%d s)\n", songs[order[next]].intro, songs[order[next]].loop[0] ? " + " : "",
		songs[order[next]].loop, songs[order[next]].msec / 1000 );
	S_StartBackgroundTrack( songs[order[next]].intro, songs[order[next]].loop );
	playingUntil = cls.realtime + songs[order[next]].msec;
	next++;
}
