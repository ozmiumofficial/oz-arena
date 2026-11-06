// Oz: OpenGL frames shown through a DXGI swap chain on Windows (sdl_dxgi.c)
#ifndef SDL_DXGI_H
#define SDL_DXGI_H

#ifdef _WIN32
qboolean	DXGI_Start( SDL_Window *window, int width, int height, int swapInterval );
void		DXGI_Stop( void );
qboolean	DXGI_Active( void );
void		DXGI_Present( void );
void		*DXGI_GetProcAddress( const char *name, void *proc );
#endif

#endif
