/*
Oz: the HUD (ENGINE_PLAN.md P6, oz_hud.c), as the client calls it. The same HUD on every server and
mod: it reads the snapshot, configstrings, events and server commands, and while it draws the game
code's own 2D is dropped. Included by client.h after the renderer's types.
*/
#ifndef OZ_HUD_H
#define OZ_HUD_H

void	OzHud_Init( void );							// CL_Init: the cvars
void	OzHud_Reset( void );						// CL_InitCGame: a new map or server
int		OzHud_ServerCommand( const char *text );	// every reliable command as it arrives (it tokenizes); 1: only the HUD reads it
void	OzHud_RenderScene( const refdef_t *fd );	// every scene cgame renders (the world view is kept)
void	OzHud_ScoresKey( int down );				// +scores / -scores
int		OzHud_BoardMouse( int dx, int dy );			// the mouse moved: 1 while the board's cursor has it
int		OzHud_BoardKey( int key );					// a key about to run its binding: 1 when it's the board's (right click under Tab, its clicks)
void	OzHud_NoteSound( int sfx, const char *name );	// the game code registered a sound
int		OzHud_LocalSound( int sfx );				// the game code plays a local sound (0: don't)
void	OzHud_NoteModel( int model, const char *name );	// the game code registered a model
int		OzHud_RefEntity( refEntity_t *ent );		// the game code adds an entity: adjust it (0: drop it)
float	OzHud_ZoomSensitivity( float cgameScale );	// the mouse's extra scale while zoomed
int		OzHud_ZoomCommand( void );					// +zoom / -zoom (tokenized): 0 swallows it
void	OzHud_CompleteName( char *line, int size, int *cursor );	// Tab in the chat line
int		OzHud_ChatChar( char *line, int size, int *cursor, int ch );	// a character for the chat line: emoji as codes (0: the line's)
void	OzHud_ChatPaste( char *line, int size, int *cursor, const char *text );	// a paste into the chat line (UTF-8)
void	OzHud_Draw( void );							// SCR_DrawScreenField, after the game's 2D, before the UI
int		OzHud_Active( void );						// in a match, cl_ozHud 1, on a renderer with the UI
int		OzHud_HideCgame2D( void );					// the game code's 2D is dropped now

// oz_hud_mods.c: what a mod's own 2D draws, for cl_ozHudLog2D
void	OzHud_ModsInit( void );
void	OzHud_NoteShader( int shader, const char *name );	// the game code registered a shader
void	OzHud_DroppedPic( float x, float y, float w, float h, float s, float t, int shader );	// one the HUD drops

// oz_hud_view.c: the options that change the 3D view (the gun, the forced enemy model), and bodies' speeds
void	OzView_NoteModel( int model, const char *name );	// from OzHud_NoteModel
int		OzView_Gun( const refEntity_t *ent );		// a first-person part: kept until the scene renders (0)
int		OzView_Entity( refEntity_t *ent );			// any other the game code adds (0: kept for the scene)
int		OzView_Polys( qhandle_t shader, int numVerts, const polyVert_t *verts, int numPolys );	// 0: kept for the scene
int		OzHud_EnemyBody( const refEntity_t *ent, int *team );	// oz_hud.c: the enemy whose body part it is, or -1
void	OzView_ClearScene( void );					// the cgame clears its scene
void	OzView_RenderScene( const refdef_t *fd );	// before a scene renders: the gun, placed against its view
qboolean OzHud_BodyVelocity( const vec3_t origin, vec3_t velocity );	// refimport: the ragdolls' push
void	OzView_ProbeSnapshot( void );				// CL_ParseSnapshot: cl_ozProbe's line for a new snapshot
extern cvar_t	*cl_ozProbe;
void	OzView_AimDrawn_f( void );					// oz_aimdrawn <client>|off: aim at a player as drawn (tests)
void	OzView_Patrol_f( void );					// oz_patrol <x1> <y1> <x2> <y2>|off: run back and forth (tests)
void	OzView_TestCommand( usercmd_t *cmd );		// CL_CreateCmd: their aim and run

#endif
