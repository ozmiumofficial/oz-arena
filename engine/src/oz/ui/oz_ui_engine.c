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

int OzE_FileExists( const char *path ) {
	return FS_FOpenFileRead( path, NULL, qfalse ) > 0;
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

char *OzE_GetClipboard( void ) {
	return Sys_GetClipboardData();
}

void OzE_Free( void *p ) {
	Z_Free( p );
}

int OzE_InIdPak( const char *path ) {
	return FS_FileIsInIdPak( path );
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
