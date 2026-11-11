/*
Oz: the engine's services for the native UI's C++ (ENGINE_PLAN.md P4).

Compiled with the client (its defines and headers), so the C++ in oz_ui*.cpp only sees the plain
C functions of oz_ui_engine.h.
*/
#include "../../../quake3e/code/client/client.h"
#include "oz_ui_engine.h"

void OzE_Print( const char *text ) {
	Com_Printf( "%s", text );
}

void OzE_DPrint( const char *text ) {
	Com_DPrintf( "%s", text );
}

int OzE_Milliseconds( void ) {
	return Sys_Milliseconds();
}

long long OzE_ProfStart( void ) {
	return Com_ProfStart();
}

void OzE_ProfEnd( int part, long long start ) {
	Com_ProfEnd( (comProf_t)part, start );
}

void OzE_ProfDetail( const char *name, long long start ) {
	Com_ProfDetail( name, start );
}

#ifdef USE_SDL
void IN_PumpEvents( void );	// sdl_input.c (Oz)
#endif

void OzE_PumpEvents( void ) {
#ifdef USE_SDL
	IN_PumpEvents();
#endif
}

int OzE_SettingsRestart( void ) {
	return OzSettings_TakeRestart();
}

void OzSettings_NeedRestart( void );	// oz_settings_apply.c

void OzE_SettingsNeedRestart( void ) {
	OzSettings_NeedRestart();
}

/*
The pause menu pauses a match on this PC's own server. The native pause page opens before the call
where the game code's menu set cl_paused, and nothing else set it: bots went on fragging and the clock
ran while a player read Settings, and a campaign arena could be lost from the pause menu. The server
itself pauses only with one human in it (SV_CheckPaused). Asked every frame the UI draws: on while the
pause pages want it, and off again when they don't, when the match isn't running (a map load, a renderer
restart: the cgame needs a snapshot to come back) or isn't local. A paused client sends nothing, so
the pause also lifts for the moment a command waits to go to the server (the pause menu's own "team",
the HUD asking the scores for a duel's line every 2 s): 64 of them unsent would drop the player with
"Client command overflow". cl_ozPause 0 leaves the match running (tests that drive a match from the
pause menu).
*/
void OzE_Pause( int on ) {
	static cvar_t	*cl_ozPause;
	static qboolean	ours;

	if ( !cl_ozPause ) {
		cl_ozPause = Cvar_Get( "cl_ozPause", "1", CVAR_ARCHIVE_ND );
		Cvar_CheckRange( cl_ozPause, "0", "1", CV_INTEGER );
		Cvar_SetDescription( cl_ozPause, "The pause menu pauses a match on your own server while you're its only player (0: the match runs on)." );
	}
	if ( on && cl_ozPause->integer && cls.state == CA_ACTIVE && !clc.demoplaying && com_sv_running && com_sv_running->integer
		&& clc.reliableSequence == clc.reliableAcknowledge ) {
		if ( !cl_paused->integer ) {
			Cvar_Set( "cl_paused", "1" );
		}
		ours = qtrue;
	} else if ( ours ) {
		ours = qfalse;
		if ( cl_paused->integer ) {
			Cvar_Set( "cl_paused", "0" );
		}
	}
}

int OzE_Monitors( void ) {
	return OzSettings_Monitors();
}

#ifdef USE_SDL
void SNDDMA_ListDevices( char *list, int size );	// sdl_snd.c (Oz)
#endif

void OzE_AudioDevices( char *list, int size ) {
#ifdef USE_SDL
	SNDDMA_ListDevices( list, size );
#else
	if ( size > 0 ) {
		list[0] = '\0';
	}
#endif
}

int OzE_ReadFile( const char *path, void **buffer ) {
	const int len = FS_ReadFile( path, buffer );

	return ( len < 0 || !*buffer ) ? -1 : len;
}

void OzE_FreeFile( void *buffer ) {
	FS_FreeFile( buffer );
}

int OzE_ReadFileHead( const char *path, void *buffer, int size ) {
	fileHandle_t f;
	int len, got;

	len = FS_FOpenFileRead( path, &f, qfalse );
	if ( len < 0 || f == FS_INVALID_HANDLE ) {
		return -1;
	}
	got = FS_Read( buffer, len < size ? len : size, f );
	FS_FCloseFile( f );
	return got;
}

int OzE_ReadFileAt( const char *path, int offset, void *buffer, int size ) {
	fileHandle_t f;
	int len, got;

	len = FS_FOpenFileRead( path, &f, qfalse );
	if ( len < 0 || f == FS_INVALID_HANDLE ) {
		return -1;
	}
	// a loose file's seek gives fseek's 0, a pak's the offset: only a negative one failed
	if ( offset < 0 || offset > len || FS_Seek( f, offset, FS_SEEK_SET ) < 0 ) {
		FS_FCloseFile( f );
		return -1;
	}
	got = FS_Read( buffer, len - offset < size ? len - offset : size, f );
	FS_FCloseFile( f );
	return got;
}

int OzE_FileExists( const char *path ) {
	return FS_FOpenFileRead( path, NULL, qfalse ) > 0;
}

unsigned OzE_ImageStamp( const char *image, int *pak ) {
	static const char *const exts[] = { "dds", "tga", "jpg", "png", "jpeg" };
	char		base[MAX_QPATH], name[MAX_QPATH];
	const char	*ext = COM_GetExtension( image ), *custom;
	unsigned	h = 2166136261u;
	int			i, len, sum, any = 0;

	*pak = 0;
#define OZ_MIX( v ) ( h = ( h ^ (unsigned)( v ) ) * 16777619u )
	COM_StripExtension( image, base, sizeof( base ) );
	for ( i = -1; i < (int)ARRAY_LEN( exts ); i++ ) {
		if ( i < 0 ) {
			if ( !*ext || !Q_stricmp( ext, "dds" ) || !Q_stricmp( ext, "tga" ) || !Q_stricmp( ext, "jpg" ) ||
				 !Q_stricmp( ext, "png" ) || !Q_stricmp( ext, "jpeg" ) ) {
				continue;	// among the ones below
			}
			Q_strncpyz( name, image, sizeof( name ) );
		} else {
			Com_sprintf( name, sizeof( name ), "%s.%s", base, exts[i] );
		}
		len = FS_FOpenFileRead( name, NULL, qfalse );
		if ( len < 0 ) {
			continue;
		}
		sum = 0;
		FS_FileIsInPAK( name, &sum, NULL );
		OZ_MIX( i + 2 );
		OZ_MIX( len );
		OZ_MIX( sum );
		if ( !any ) {
			*pak = sum;
		}
		any = 1;
	}
	custom = OzTex_Find( image, "dds png tga jpg jpeg" );
	if ( custom ) {
		for ( i = 0; custom[i]; i++ ) {
			OZ_MIX( tolower( (unsigned char)custom[i] ) );
		}
		OZ_MIX( FS_FOpenFileRead( custom, NULL, qfalse ) );
		any = 1;
	}
#undef OZ_MIX
	return any ? ( h ? h : 1 ) : 0;
}

void *OzE_FOpenOS( const char *path, const char *mode ) {
	return Sys_FOpen( path, mode );
}

unsigned OzE_SearchStamp( void ) {
	return FS_OzSearchStamp();
}

int OzE_IsPlayerHome( void ) {
	return !Q_stricmp( Cvar_VariableString( "fs_homepath" ), Sys_DefaultHomePath() );
}

int OzE_CvarExists( const char *name ) {
	return Cvar_Flags( name ) != CVAR_NONEXISTENT;
}

const char *OzE_CvarString( const char *name ) {
	return Cvar_VariableString( name );
}

void OzE_CvarSet( const char *name, const char *value ) {
	Cvar_Set( name, value );
}

void OzE_CvarCreate( const char *name, const char *value, int latched, const char *description, const char *min, const char *max, int integral ) {
	cvar_t *cv = Cvar_Get( name, value, CVAR_ARCHIVE_ND | ( latched ? CVAR_LATCH : 0 ) );

	if ( description && *description ) {
		Cvar_SetDescription( cv, description );
	}
	if ( min && max ) {
		Cvar_CheckRange( cv, min, max, integral ? CV_INTEGER : CV_FLOAT );
	}
}

void OzE_AddCommand( const char *name, void (*function)( void ) ) {
	Cmd_AddCommand( name, function );
}

int OzE_Argc( void ) {
	return Cmd_Argc();
}

const char *OzE_Argv( int arg ) {
	return Cmd_Argv( arg );
}

const char *OzE_ArgsFrom( int arg ) {
	return Cmd_ArgsFrom( arg );
}

void OzE_ExecuteNow( const char *text ) {
	Cbuf_ExecuteText( EXEC_NOW, text );
}

void OzE_WriteFile( const char *path, const void *data, int size ) {
	FS_WriteFile( path, data, size );
}

void OzE_AppendText( const char *text ) {
	Cbuf_AddText( text );
}

void OzE_InsertText( const char *text ) {
	Cbuf_InsertText( text );
}

#ifdef USE_SDL
qboolean IN_OpenURL( const char *url );	// sdl_input.c
#endif

int OzE_OpenURL( const char *url ) {
#ifdef USE_SDL
	return IN_OpenURL( url );
#else
	return 0;
#endif
}

// as the platform sends it, so it goes through CL_KeyEvent (Escape in a match opens the pause menu)
void OzE_QueueKey( int key ) {
	Sys_QueEvent( 0, SE_KEY, key, qtrue, 0, NULL );
	Sys_QueEvent( 0, SE_KEY, key, qfalse, 0, NULL );
}

// q3_ui registers the menu sounds once; registering by name each time finds the same one (a hash
// lookup) and survives the sound system restarting with a mod switch
void OzE_StartLocalSound( const char *name ) {
	const sfxHandle_t sfx = S_RegisterSound( name, qfalse );

	if ( sfx ) {
		S_StartLocalSound( sfx, CHAN_LOCAL_SOUND );
	}
}

const ozUiRender_t *OzE_Renderer( void ) {
	if ( !cls.rendererStarted || !re.GetUIRender ) {
		return NULL;
	}
	return re.GetUIRender();
}

void OzE_ScreenSize( int *width, int *height ) {
	*width = cls.glconfig.vidWidth;
	*height = cls.glconfig.vidHeight;
}

// KEYCATCH_UI while a page is open; the mod's menu may have it already, and keeps it then
void OzE_CatchKeys( int on ) {
	static qboolean ours;

	if ( on ) {
		ours = !( Key_GetCatcher() & KEYCATCH_UI );
		Key_SetCatcher( Key_GetCatcher() | KEYCATCH_UI );
	} else if ( ours ) {
		Key_SetCatcher( Key_GetCatcher() & ~KEYCATCH_UI );
		ours = qfalse;
	}
}

int OzE_ConsoleOpen( void ) {
	return ( Key_GetCatcher() & KEYCATCH_CONSOLE ) != 0;
}

int OzE_KeyDown( int key ) {
	return Key_IsDown( key );
}

const char *OzE_KeyName( int key ) {
	return Key_KeynumToString( key );
}

int OzE_KeyNum( const char *name ) {
	return Key_StringToKeynum( name );
}

const char *OzE_Binding( int key ) {
	return Key_GetBinding( key );
}

void OzE_SetBinding( int key, const char *command ) {
	Key_SetBinding( key, command );
}

char *Sys_GetClipboardText( void );	// sdl_glimp.c: whole, line ends taken out (the console's read stops at the first)

char *OzE_GetClipboard( void ) {
	return Sys_GetClipboardText();
}

void Sys_SetClipboardText( const char *text );	// sdl_glimp.c

void OzE_SetClipboard( const char *text ) {
	Sys_SetClipboardText( text );
}

void OzE_Free( void *p ) {
	Z_Free( p );
}

int OzE_InIdPak( const char *path ) {
	return FS_FileIsInIdPak( path );
}

void OzE_EngineCvar( const char *name ) {
	Com_OzEngineCvar( name );
}

char **OzE_ListFiles( const char *dir, const char *ext, int *count ) {
	return FS_ListFiles( dir, ext, count );
}

char **OzE_ListTree( const char *dir, const char *ext, int *count ) {
	return FS_OzListTree( dir, ext, count );
}

void OzE_FreeFileList( char **list ) {
	FS_FreeFileList( list );
}

int OzE_ClientState( void ) {
	if ( cls.state == CA_ACTIVE ) {
		return 2;
	}
	return cls.state == CA_DISCONNECTED || cls.state == CA_CINEMATIC ? 0 : 1;
}

int OzE_Connection( char *server, int serverSize, int *tries, char *message, int messageSize, int *downloadMs ) {
	if ( cls.state < CA_CONNECTING || cls.state > CA_CONNECTED || clc.demoplaying ) {
		return 0;
	}
	Q_strncpyz( server, cls.servername, serverSize );
	Q_strncpyz( message, clc.serverMessage, messageSize );
	*tries = clc.connectPacketCount;
	*downloadMs = Cvar_VariableString( "cl_downloadName" )[0] ? cls.realtime - Cvar_VariableIntegerValue( "cl_downloadTime" ) : 0;
	return cls.state == CA_CONNECTING ? 1 : cls.state == CA_CHALLENGING ? 2 : 3;
}

int OzE_PlayingDemo( void ) {
	return clc.demoplaying;
}

// baseq3's config strings and persistant slot (bg_public.h); the client doesn't include the game's headers
#define OZ_CS_WARMUP			5
#define OZ_CS_LEVEL_START_TIME	21
#define OZ_PERS_SCORE			0

int OzE_MatchInfo( char *map, int mapSize, int *gametype, int *seconds, int *warmup, int *score, int *localServer ) {
	const char *info;

	if ( cls.state != CA_ACTIVE ) {
		return 0;
	}
	info = cl.gameState.stringData + cl.gameState.stringOffsets[CS_SERVERINFO];
	Q_strncpyz( map, Info_ValueForKey( info, "mapname" ), mapSize );
	*gametype = atoi( Info_ValueForKey( info, "g_gametype" ) );
	*seconds = ( cl.snap.serverTime - atoi( cl.gameState.stringData + cl.gameState.stringOffsets[OZ_CS_LEVEL_START_TIME] ) ) / 1000;
	*warmup = atoi( cl.gameState.stringData + cl.gameState.stringOffsets[OZ_CS_WARMUP] ) != 0;
	*score = cl.snap.ps.persistant[OZ_PERS_SCORE];
	*localServer = com_sv_running && com_sv_running->integer;
	return 1;
}

const char *OzE_GameDir( void ) {
	return FS_GetCurrentGameDir();
}

int OzE_IsOzGame( void ) {
	return !Q_stricmp( FS_GetCurrentGameDir(), DEFAULT_GAME );
}

const char *OzE_Version( void ) {
	return Cvar_VariableString( "version" );
}

static int rendererGeneration = 1;

void OzE_RendererRestarted( void ) {
	rendererGeneration++;
}

int OzE_RendererGeneration( void ) {
	return rendererGeneration;
}
