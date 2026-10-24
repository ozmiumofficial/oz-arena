/* q3core: stands in for id's q3map/gldraw.c, an OpenGL (glaux) debug window the map editor's
   compiles never open. The functions nodraw.c doesn't already stub, drawing nothing. */
#include "qbsp.h"

void InitWindow (void) {}
void Draw_SetRed (void) {}
void Draw_SetGrey (void) {}
void Draw_SetBlack (void) {}
void DrawWinding (winding_t *w) {}
void DrawAuxWinding (winding_t *w) {}
