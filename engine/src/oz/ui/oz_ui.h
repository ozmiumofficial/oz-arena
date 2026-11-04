/*
Oz: the native UI (ENGINE_PLAN.md P4), as the engine's C code calls it.

RmlUi 6 with FreeType, pages as RML and RCSS from oz/zz-oz-ui.pk3 (engine/ui), page logic in C++
(oz_ui*.cpp). It draws through the renderer's UI calls (renderercommon/tr_oz_ui.h); a renderer
without them shows no UI.
*/
#ifndef OZ_UI_H
#define OZ_UI_H

#ifdef __cplusplus
extern "C" {
#endif

void	OzUI_Init( void );				// CL_Init: the settings' cvars, the console commands, RmlUi
void	OzUI_Shutdown( void );			// CL_Shutdown, before the renderer goes
void	OzUI_RendererShutdown( void );	// before every re.Shutdown: give back the renderer's handles
void	OzUI_Draw( void );				// SCR_DrawScreenField, between the game's 2D and the console
int		OzUI_IsActive( void );			// a page is open: it takes the keys, characters and mouse
void	OzUI_KeepKeys( void );			// CL_ShutdownUI took KEYCATCH_UI: an open page takes it back
int		OzUI_EditorOpen( void );		// the map editor's page is open (its dialogs over it too): no menu music
void	OzUI_KeyEvent( int key, int down );
void	OzUI_CharEvent( int ch );
void	OzUI_MouseEvent( int x, int y );	// absolute, in the window's pixels
void	OzUI_EditorLook( int dx, int dy );	// relative, while the map editor's fly camera looks (oz_ui_editor.cpp)
int		OzEd_Looking( void );			// the map editor's fly camera wants the mouse captured (oz_editor_engine.c)

// oz_settings_apply.c: the Settings page's oz_ cvars applied to the engine's when they change
void	OzSettings_Frame( void );		// CL_Frame
int		OzSettings_TakeRestart( void );	// a latched renderer setting changed since the last call
int		OzSettings_Monitors( void );	// Video > Monitor's choices

// P5: Oz's own game (fs_game oz) gets the native menus (ui_nativeMenu 1), on every renderer
int		OzUI_MainMenu( void );			// disconnected with no menu up: opens the splash or the main menu
int		OzUI_PauseMenu( void );			// Escape in a match: opens the pause menu
int		OzUI_NativeMenus( void );		// the native menus show (not the game code's)
// P8, decision 21: connecting (and downloading) under any mod: opens the connection screen; 0 leaves
// it to the game code's (ui_nativeMenu 0, no renderer for the UI yet)
int		OzUI_ConnectScreen( void );

// P6: the HUD canvas (oz_ui_hud.cpp) the HUD (oz_hud.c) draws through; drawn under the pages
typedef struct {
	float			xy[2];			// pixels from the top left
	float			st[2];
	unsigned char	color[4];		// straight alpha
} ozHudVertex_t;

#define OZ_HUD_ALIGN_LEFT		0
#define OZ_HUD_ALIGN_CENTER		1
#define OZ_HUD_ALIGN_RIGHT		2
#define OZ_HUD_TEXT_NOCODES		4	// "^" is just a character
#define OZ_HUD_TEXT_MEASURE		8	// only the width
#define OZ_HUD_TEXT_STEPPED		16	// a size that drifts from frame to frame: drawn from the nearest of a few
#define OZ_HUD_TEXT_WARM		32	// nothing drawn: the face and outline made, and drawn ahead off the screen

int		OzUI_HudAvailable( void );		// the renderer draws the UI
int		OzUI_HudImage( const char *name );	// a handle, 0 for none
void	OzUI_HudMesh( const ozHudVertex_t *verts, int numVerts, const int *indexes, int numIndexes, int image );	// triangles by index
// y is the baseline; returns the width. Laid out at size, drawn scale times as large about (x, y): a pop
// is a scale, so a text whose size animates keeps its face
float	OzUI_HudText( const char *text, float x, float y, int font, float size, float scale, int flags,
			const float *rgba, float outline, float maxWidth );
int		OzUI_EmojiFont( void );			// the UI's fonts draw emoji (Windows' Segoe UI Emoji, oz_emoji.c)

// oz_hud.c, for the crosshair editor (oz_ui_crosshair.cpp): the weapon tab the match behind shows (each
// frame; 0: the weapon in hand), Test hit, and the crosshair as the HUD draws it, its triangles handed to sink: centered
// at (cx, cy), k pixels to a 1080p pixel, shape NULL for the weapon's (0: the main one), size 0 for the
// setting's (1080p pixels), hitT seconds since a hit
void	OzHud_CrosshairWeapon( int weapon );
void	OzHud_CrosshairTestHit( void );
void	OzHud_CrosshairPreview( float cx, float cy, float k, const char *shape, int weapon, float size, float hitT,
			void ( *sink )( const ozHudVertex_t *verts, int numVerts, int image, void *ctx ), void *ctx );

#ifdef __cplusplus
}
#endif

#endif
