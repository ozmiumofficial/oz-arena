/*
Oz: the engine's services for the map editor's C++ (oz_editor_engine.c, compiled with the client),
beside the native UI's (oz_ui_engine.h). Plain C types only.
*/
#ifndef OZ_ED_ENGINE_H
#define OZ_ED_ENGINE_H

#include "../../../quake3e/code/renderercommon/tr_oz_editor.h"

// the compile tools' file names (tools/q3map2.exe on Windows, tools/q3map2 on Linux)
#ifdef _WIN32
#define OZ_ED_EXE ".exe"
#else
#define OZ_ED_EXE ""
#endif

#ifdef __cplusplus
extern "C" {
#endif

// The renderer's editor calls; NULL while it's down or has none (only the oz renderer has them).
const ozEdRender_t *OzEd_Renderer( void );
const char	*OzEd_RendererName( void );		// cl_renderer

// One 3D view: a scene of models drawn into x, y, w, h (window pixels, from the top left).
void		OzEd_ClearScene( void );
void		OzEd_AddModel( int model, const float origin[3], const float axis[3][3], int customShader, const unsigned char rgba[4] );
int			OzEd_RegisterModel( const char *name );	// a model of the game's (an item's .md3), 0 when there's none
// A fighter standing (its legs idle, its torso in its stand), as the menus' showcase draws one: model
// "sarge", skin "default" (or "red", "blue"); feet 24 under origin, facing yaw. 0 when it has no model.
int			OzEd_AddFighter( const char *model, const char *skin, const float origin[3], float yaw );
// Another player in an editing session (PE): their fighter with its eye at eye, facing yaw, its head
// tilted by pitch; 0 when it has no model.
int			OzEd_AddFlyer( const char *model, const char *skin, const float eye[3], float yaw, float pitch );
// ortho: a 2D view (RDF_OZ_ORTHO), fovX and fovY its width and height in map units
void		OzEd_RenderScene( int x, int y, int w, int h, const float origin[3], const float axis[3][3], float fovX, float fovY, int timeMs, int ortho );
void		OzEd_Fill( int x, int y, int w, int h, const float rgba[4] );	// 2D, under the scene
// The lit preview: a compiled map ("maps/ed_room.bsp") as the scene's world, "" to drop it (a command's
// work: the client's systems start again). OzEd_World: the one loaded, "" when none.
void		OzEd_LoadWorld( const char *name );
const char	*OzEd_World( void );

// The mouse: relative motion while looking (the fly camera), the cursor put back where it was after.
void		OzEd_SetLooking( int on );
int			OzEd_Looking( void );

// Files outside the game's search path (maps, the q3map2 run), by OS path.
int			OzEd_ReadOSFile( const char *path, char **data );	// the length, -1 when missing; free with OzEd_Free
int			OzEd_WriteOSFile( const char *path, const void *data, int size );	// atomically (a temporary, then a rename)
void		OzEd_Free( void *p );
void		OzEd_MakeDirs( const char *path );		// every folder of a file's path
const char	*OzEd_HomePath( void );					// fs_homepath
const char	*OzEd_BaseQ3( void );					// the player's baseq3 folder, "" when unknown
const char	*OzEd_EngineDir( void );				// the folder OzArena.exe is in
int			OzEd_FileExists( const char *path );	// an OS path
int			OzEd_RemoveOSFile( const char *path );	// 1 when it's gone
// An RGB image (rows bottom-up, as glReadPixels and the screenshots have them) written as a JPEG; 1 when written.
int			OzEd_SaveJPG( const char *path, int width, int height, const unsigned char *rgb, int quality );
char		**OzEd_ListOSFiles( const char *dir, const char *ext, int *count );	// file names; free with OzEd_FreeList
void		OzEd_FreeList( char **list );
// Runs a program and waits (taking the window's messages meanwhile); its output goes to log. The exit code, -1 when it didn't start.
int			OzEd_Run( const char *exe, const char *args, const char *dir, const char *log );
// The same in the background: a handle (0 when it didn't start); Poll is 1 while it runs, else 0 with
// its exit code; Stop ends it. Wait sleeps, taking the window's messages.
int			OzEd_Start( const char *exe, const char *args, const char *dir, const char *log );
int			OzEd_Poll( int handle, int *code );
void		OzEd_Stop( int handle );
void		OzEd_Wait( int ms );

#ifdef __cplusplus
}
#endif

#endif
