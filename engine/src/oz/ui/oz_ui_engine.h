/*
Oz: the engine's services for the native UI's C++ (oz_ui_engine.c, compiled with the client).
Plain C types only, so the C++ never includes the engine's headers.
*/
#ifndef OZ_UI_ENGINE_H
#define OZ_UI_ENGINE_H

#include "../../../quake3e/code/renderercommon/tr_oz_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

void		OzE_Print( const char *text );
void		OzE_DPrint( const char *text );		// with developer 1
int			OzE_Milliseconds( void );
long long	OzE_ProfStart( void );		// "frametime"'s parts: 0 when it isn't timing
void		OzE_ProfEnd( int part, long long start );	// part: 0 ui update, 1 ui draw (comProf_t)
void		OzE_ProfDetail( const char *name, long long start );	// a named piece of a slow frame (Com_ProfDetail)
void		OzE_PumpEvents( void );		// takes the window's messages in a long command, so Windows doesn't call it hung
int			OzE_SettingsRestart( void );	// a renderer setting waits for a vid_restart (taken: the next call says 0)
void		OzE_SettingsNeedRestart( void );	// a setting bound straight to a latched renderer cvar changed: it waits for that restart too
void		OzE_Pause( int on );			// every frame: the pause pages want a local match paused (cl_paused), or not
int			OzE_Monitors( void );			// how many monitors
void		OzE_AudioDevices( char *list, int size );	// the sound output devices' names, one a line

int			OzE_ReadFile( const char *path, void **buffer );	// the length, -1 when missing
void		OzE_FreeFile( void *buffer );
int			OzE_ReadFileHead( const char *path, void *buffer, int size );	// a file's first bytes (an image's header): how many, -1 when missing
int			OzE_ReadFileAt( const char *path, int offset, void *buffer, int size );	// up to size bytes from offset on (a BSP's lump): how many, -1 when missing
void		OzE_WriteFile( const char *path, const void *data, int size );	// in the home path's game folder
int			OzE_FileExists( const char *path );		// in the search path or the UI pak, without reading it
int			OzE_IsPlayerHome( void );				// fs_homepath is the player's own user folder (not a test's)
// the map editor's thumbnail cache (oz_ui_thumbs.cpp): a number for an image's files (each one the
// renderer or q3map2 may read for it, a custom texture's too, their sizes and the paks holding them)
// that changes when any of them does; 0 when none is there. *pak: the pak holding the first, so a
// pak's images are read together
unsigned	OzE_ImageStamp( const char *image, int *pak );
void		*OzE_FOpenOS( const char *path, const char *mode );	// an OS path as a FILE *; fclose it
unsigned	OzE_SearchStamp( void );	// changes when the search path's paks or folders do (FS_OzSearchStamp)

int			OzE_CvarExists( const char *name );
const char	*OzE_CvarString( const char *name );
void		OzE_CvarSet( const char *name, const char *value );
void		OzE_CvarCreate( const char *name, const char *value, int latched, const char *description,
				const char *min, const char *max, int integral );	// archived; min and max may be NULL

void		OzE_AddCommand( const char *name, void (*function)( void ) );
int			OzE_Argc( void );
const char	*OzE_Argv( int arg );
const char	*OzE_ArgsFrom( int arg );
void		OzE_ExecuteNow( const char *text );
void		OzE_AppendText( const char *text );		// queued, after what's queued already (waits count frames)
void		OzE_InsertText( const char *text );		// queued, before what's queued already
int			OzE_OpenURL( const char *url );			// in the player's browser, outside the game; 0 if it didn't open
void		OzE_QueueKey( int key );				// a key press and release through the engine's event queue (tests)

void		OzE_StartLocalSound( const char *name );

const ozUiRender_t *OzE_Renderer( void );	// NULL while the renderer is down or has no UI calls
void		OzE_ScreenSize( int *width, int *height );

void		OzE_CatchKeys( int on );			// KEYCATCH_UI while a page is open
int			OzE_ConsoleOpen( void );
int			OzE_KeyDown( int key );
const char	*OzE_KeyName( int key );
int			OzE_KeyNum( const char *name );
const char	*OzE_Binding( int key );
void		OzE_SetBinding( int key, const char *command );
char		*OzE_GetClipboard( void );			// free with OzE_Free
void		OzE_SetClipboard( const char *text );	// onto the system's clipboard (UTF-8)
void		OzE_Free( void *p );

// P5, the menus
char		**OzE_ListFiles( const char *dir, const char *ext, int *count );	// free with OzE_FreeFileList
char		**OzE_ListTree( const char *dir, const char *ext, int *count );	// its subfolders' too (the map editor's pick lists)
int			OzE_InIdPak( const char *path );	// one of id's paks (baseq3/pak0 to pak8) has the file
void		OzE_FreeFileList( char **list );
int			OzE_ClientState( void );			// 0 disconnected (the main menu), 1 connecting or loading, 2 in a match
// the connection under way, for the connection screen (P8): 0 none (or loading, or a demo), 1 asking
// for a challenge, 2 challenging, 3 connected (the gamestate, or a download: its ms so far); the
// server as typed, the packets sent, what the server said
int			OzE_Connection( char *server, int serverSize, int *tries, char *message, int messageSize, int *downloadMs );
int			OzE_PlayingDemo( void );
// the match the client is in, for the pause menu; 0 when not in one
int			OzE_MatchInfo( char *map, int mapSize, int *gametype, int *seconds, int *warmup, int *score, int *localServer );
// the pause menu's team choices (oz_hud.c): your team and the sides, from the configstrings; 0 when not in a match
typedef struct {
	int		gametype;
	int		team;			// baseq3's: 0 free, 1 red, 2 blue, 3 spectator
	int		count[4];		// the players on each of those
	int		redScore, blueScore;
	int		fraglimit, timelimit, capturelimit;
	int		line;			// a duel's waiting spectator: your place in the line to play (1 next), 0 not known
	int		following;		// watching a player (not flying free)
	int		intermission;	// the match is over (the scores screen)
	int		ozRules;		// the server runs Oz's game code (its serverinfo has g_ruleset): a duel's line is id's
} ozTeamInfo_t;
int			OzE_TeamInfo( ozTeamInfo_t *t );
const char	*OzE_GameDir( void );				// the running mod ("oz" is Oz's own game)
int			OzE_IsOzGame( void );
const char	*OzE_Version( void );
void		OzE_RendererRestarted( void );		// the renderer's handles are gone (OzUI_RendererShutdown)
int			OzE_RendererGeneration( void );

// oz_q3import.c: the player's Quake III config, imported (binds and the settings Settings shows)
void		OzQ3Import_Init( void );			// at CL_Init, before the first config write
int			OzQ3Import_Offer( void );		// once: Oz's first run, with something to import (planned)
int			OzQ3Import_Plan( void );			// reads the config: how many binds and settings would come over
const char	*OzQ3Import_From( void );		// the folder it's in, "" without one
const char	*OzQ3Import_Kept( void );		// what stays as Oz has it, a line
int			OzQ3Import_Summary( int line, char *name, int nameSize, char *text, int textSize );	// 0 past the last
int			OzQ3Import_Apply( void );		// the plan imported: how many

// oz_ui_showcase.c: the main menu's 3D showcase, drawn before the page layer
void		OzE_ShowcaseDraw( int width, int height, const float *accent );
const char	*OzE_ShowcaseSubject( const char **kind );
void		OzE_ShowcaseNext( void );

#ifdef __cplusplus
}
#endif

#endif
