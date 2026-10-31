/*
===========================================================================
Oz: a mod's own 2D under the Oz HUD (ENGINE_PLAN.md P6; decision 17).

While the HUD draws, the game code's 2D is dropped (cl_cgame.c). What a mod draws there is how its
HUD was compared with the Oz HUD's (CPMA's default HUD, Defrag's boxes), by what it draws them
with, never by reading the mods' code:

  cl_ozHudLog2D 1	prints every picture of the game code's 2D the next frame drops
					("ozhud 2d: <shader> x y w h s t"), then goes back to 0
===========================================================================
*/
#include "../../../quake3e/code/client/client.h"

#define MAX_NOTED_SHADERS	4096

static char		shaderNames[MAX_NOTED_SHADERS][MAX_QPATH];	// by handle, as the game code registered them
static cvar_t	*cl_ozHudLog2D;
static int		logFrame = -1;

void OzHud_ModsInit( void ) {
	cl_ozHudLog2D = Cvar_Get( "cl_ozHudLog2D", "0", CVAR_TEMP );
	Cvar_SetDescription( cl_ozHudLog2D, "1: print every picture of the game code's 2D the Oz HUD drops in the next frame." );
}

void OzHud_NoteShader( int shader, const char *name ) {
	if ( shader > 0 && shader < MAX_NOTED_SHADERS && name ) {
		Q_strncpyz( shaderNames[shader], name, sizeof( shaderNames[shader] ) );
	}
}

// a picture of the game code's 2D the HUD drops
void OzHud_DroppedPic( float x, float y, float w, float h, float s, float t, int shader ) {
	if ( !cl_ozHudLog2D || !cl_ozHudLog2D->integer ) {
		return;
	}
	if ( logFrame < 0 ) {
		logFrame = cls.framecount;
	}
	if ( cls.framecount != logFrame ) {
		Cvar_Set( "cl_ozHudLog2D", "0" );
		logFrame = -1;
		return;
	}
	Com_Printf( "ozhud 2d: %s %.1f %.1f %.1f %.1f %.4f %.4f\n",
		shader > 0 && shader < MAX_NOTED_SHADERS && shaderNames[shader][0] ? shaderNames[shader] : "?", x, y, w, h, s, t );
}
