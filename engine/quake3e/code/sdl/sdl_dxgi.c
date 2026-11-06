/*
===========================================================================
Oz: OpenGL frames shown through a DXGI swap chain (Windows)

Screen recorders and streaming apps (OBS, Discord, the Xbox Game Bar, ShadowPlay) are built
around DirectX games. An OpenGL window covering the screen in front is scanned out straight by
the driver (NVIDIA's "native" present, its default), past the desktop compositor: window
captures froze on a stale picture, and exclusive full screen also cut off desktop duplication
(OBS's display capture, Discord's screen share) while the mode was set.

So the GL renderers draw into a framebuffer of ours, and each frame goes into a DXGI flip-model
swap chain on the game's window and is presented, as a DirectX game does. Everything that
captures DirectX games captures Oz then. Flip model keeps the latency low: a full screen window
goes to independent flip, and with vsync off frames tear as before (DXGI_PRESENT_ALLOW_TEARING).

The frame crosses from GL to Direct3D 11 through shared textures GL imports
(GL_EXT_memory_object_win32), synchronized on the GPU by two shared D3D11 fences GL imports as
semaphores (GL_EXT_semaphore_win32): GL waits for D3D's copy out of a texture before drawing
into it again, D3D waits for GL's blit before copying. The cost (2026-11-05, RTX 3070 Ti): the
signal makes the game wait for NVIDIA's threaded-optimization driver thread to catch up, about
0.65 ms a frame, so a 1000 fps cap runs about 715; caps of 500 and below hold.
WGL_NV_DX_interop2's lock cost as much and more.

The renderers know nothing of it: GL_GetProcAddress hands them wrappers that take framebuffer 0
for ours (glBindFramebuffer), GL_BACK and GL_FRONT for its color attachment (glDrawBuffer,
glReadBuffer) and report ours as 0 (glGetIntegerv). Anything that fails on the way leaves the
window to OpenGL's own SwapBuffers, as before. r_dxgi 0 turns it off.
===========================================================================
*/
#ifdef _WIN32

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>	// the IIDs below, without dxguid.lib
#include <d3d11_4.h>
#include <dxgi1_5.h>

#ifdef USE_LOCAL_HEADERS
#	include "SDL.h"
#	include "SDL_syswm.h"
#else
#	include <SDL.h>
#	include <SDL_syswm.h>
#endif

#include "../client/client.h"
#include "sdl_dxgi.h"

#ifndef APIENTRY
#define APIENTRY __stdcall
#endif

typedef unsigned int	GLenum_;
typedef unsigned int	GLuint_;
typedef int				GLint_;
typedef int				GLsizei_;
typedef unsigned char	GLboolean_;
typedef unsigned long long	GLuint64_;

#define GL_TEXTURE_2D_				0x0DE1
#define GL_FRAMEBUFFER_				0x8D40
#define GL_READ_FRAMEBUFFER_		0x8CA8
#define GL_DRAW_FRAMEBUFFER_		0x8CA9
#define GL_DRAW_FRAMEBUFFER_BINDING_	0x8CA6
#define GL_READ_FRAMEBUFFER_BINDING_	0x8CAA
#define GL_RENDERBUFFER_			0x8D41
#define GL_COLOR_ATTACHMENT0_		0x8CE0
#define GL_DEPTH_STENCIL_ATTACHMENT_	0x821A
#define GL_DEPTH24_STENCIL8_		0x88F0
#define GL_RGBA8_					0x8058
#define GL_FRAMEBUFFER_COMPLETE_	0x8CD5
#define GL_COLOR_BUFFER_BIT_		0x00004000
#define GL_NEAREST_					0x2600
#define GL_SCISSOR_TEST_			0x0C11
#define GL_DRAW_BUFFER_				0x0C01
#define GL_READ_BUFFER_				0x0C02
#define GL_DRAW_BUFFER0_			0x8825
#define GL_FRONT_LEFT_				0x0400
#define GL_FRONT_RIGHT_				0x0401
#define GL_BACK_LEFT_				0x0402
#define GL_BACK_RIGHT_				0x0403
#define GL_FRONT_					0x0404
#define GL_BACK_					0x0405
#define GL_LEFT_					0x0406
#define GL_FRONT_AND_BACK_			0x0408
#define GL_DEVICE_LUID_EXT_			0x9599
#define GL_DEDICATED_MEMORY_OBJECT_EXT_	0x9581
#define GL_HANDLE_TYPE_D3D11_IMAGE_EXT_	0x958B
#define GL_HANDLE_TYPE_D3D12_FENCE_EXT_	0x9594
#define GL_D3D12_FENCE_VALUE_EXT_	0x9595
#define GL_LAYOUT_COLOR_ATTACHMENT_EXT_	0x958E
#define GL_LAYOUT_TRANSFER_SRC_EXT_	0x9592

typedef void		(APIENTRY *pGenFramebuffers)( GLsizei_ n, GLuint_ *ids );
typedef void		(APIENTRY *pDeleteFramebuffers)( GLsizei_ n, const GLuint_ *ids );
typedef void		(APIENTRY *pBindFramebuffer)( GLenum_ target, GLuint_ fb );
typedef void		(APIENTRY *pFramebufferRenderbuffer)( GLenum_ target, GLenum_ attachment, GLenum_ rbtarget, GLuint_ rb );
typedef void		(APIENTRY *pFramebufferTexture2D)( GLenum_ target, GLenum_ attachment, GLenum_ textarget, GLuint_ tex, GLint_ level );
typedef GLenum_		(APIENTRY *pCheckFramebufferStatus)( GLenum_ target );
typedef void		(APIENTRY *pGenRenderbuffers)( GLsizei_ n, GLuint_ *ids );
typedef void		(APIENTRY *pDeleteRenderbuffers)( GLsizei_ n, const GLuint_ *ids );
typedef void		(APIENTRY *pBindRenderbuffer)( GLenum_ target, GLuint_ rb );
typedef void		(APIENTRY *pRenderbufferStorage)( GLenum_ target, GLenum_ format, GLsizei_ w, GLsizei_ h );
typedef void		(APIENTRY *pBlitFramebuffer)( GLint_ sx0, GLint_ sy0, GLint_ sx1, GLint_ sy1, GLint_ dx0, GLint_ dy0, GLint_ dx1, GLint_ dy1, unsigned int mask, GLenum_ filter );
typedef void		(APIENTRY *pGetIntegerv)( GLenum_ pname, GLint_ *data );
typedef void		(APIENTRY *pDrawBuffer)( GLenum_ mode );
typedef void		(APIENTRY *pDrawBuffers)( GLsizei_ n, const GLenum_ *bufs );
typedef void		(APIENTRY *pReadBuffer)( GLenum_ mode );
typedef GLboolean_	(APIENTRY *pIsEnabled)( GLenum_ cap );
typedef void		(APIENTRY *pEnableDisable)( GLenum_ cap );
typedef GLenum_		(APIENTRY *pGetError)( void );
typedef void		(APIENTRY *pFlush)( void );
typedef void		(APIENTRY *pGenTextures)( GLsizei_ n, GLuint_ *ids );
typedef void		(APIENTRY *pDeleteTextures)( GLsizei_ n, const GLuint_ *ids );
typedef void		(APIENTRY *pBindTexture)( GLenum_ target, GLuint_ tex );
typedef void		(APIENTRY *pGetUnsignedBytevEXT)( GLenum_ pname, unsigned char *data );
typedef void		(APIENTRY *pCreateMemoryObjectsEXT)( GLsizei_ n, GLuint_ *ids );
typedef void		(APIENTRY *pDeleteMemoryObjectsEXT)( GLsizei_ n, const GLuint_ *ids );
typedef void		(APIENTRY *pMemoryObjectParameterivEXT)( GLuint_ mem, GLenum_ pname, const GLint_ *params );
typedef void		(APIENTRY *pImportMemoryWin32HandleEXT)( GLuint_ mem, GLuint64_ size, GLenum_ type, void *handle );
typedef void		(APIENTRY *pTexStorageMem2DEXT)( GLenum_ target, GLsizei_ levels, GLenum_ format, GLsizei_ w, GLsizei_ h, GLuint_ mem, GLuint64_ offset );
typedef void		(APIENTRY *pGenSemaphoresEXT)( GLsizei_ n, GLuint_ *ids );
typedef void		(APIENTRY *pDeleteSemaphoresEXT)( GLsizei_ n, const GLuint_ *ids );
typedef void		(APIENTRY *pImportSemaphoreWin32HandleEXT)( GLuint_ sem, GLenum_ type, void *handle );
typedef void		(APIENTRY *pSemaphoreParameterui64vEXT)( GLuint_ sem, GLenum_ pname, const GLuint64_ *params );
typedef void		(APIENTRY *pWaitSemaphoreEXT)( GLuint_ sem, GLuint_ nbuf, const GLuint_ *bufs, GLuint_ ntex, const GLuint_ *texs, const GLenum_ *layouts );
typedef void		(APIENTRY *pSignalSemaphoreEXT)( GLuint_ sem, GLuint_ nbuf, const GLuint_ *bufs, GLuint_ ntex, const GLuint_ *texs, const GLenum_ *layouts );

typedef HRESULT		(WINAPI *pD3D11CreateDevice)( IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext ** );
typedef HRESULT		(WINAPI *pCreateDXGIFactory1)( REFIID, void ** );

// Shared textures in turn, so GL draws the next frame into one while D3D may still copy another
#define DX_SHARED	3

static struct {
	qboolean				active;
	int						width, height;
	int						interval;
	UINT					presentFlags;

	HMODULE					d3d11Dll, dxgiDll;
	ID3D11Device5			*device;
	ID3D11DeviceContext4	*context;
	IDXGISwapChain1			*swapChain;
	ID3D11Texture2D			*backBuffer;
	ID3D11Texture2D			*shared[DX_SHARED];	// what GL writes, copied into the back buffer
	ID3D11Fence				*fence[2];			// GL's blits (GL signals, D3D waits), D3D's copies (the other way)
	UINT64					drawn;				// the last blit's value on fence 0
	UINT64					copied[DX_SHARED];	// fence 1's value once D3D's copy out of each shared texture is done
	UINT64					copies;				// the last copy's value on fence 1

	GLuint_					memory[DX_SHARED], texture[DX_SHARED], semaphore[2];
	GLuint_					shareFbo[DX_SHARED];	// the shared textures, rows the right way up
	GLuint_					gameFbo, gameColor, gameDepth;	// the renderers' "framebuffer 0"
	GLuint_					boundDraw, boundRead;	// what the renderers have bound, as GL names
	unsigned				frame;
	qboolean				scissor;			// GL_SCISSOR_TEST as the renderers left it, known without asking GL
	int						failures;
} dx;

static struct {
	pGenFramebuffers		GenFramebuffers;
	pDeleteFramebuffers		DeleteFramebuffers, DeleteFramebuffersEXT;
	pBindFramebuffer		BindFramebuffer, BindFramebufferEXT;
	pFramebufferRenderbuffer	FramebufferRenderbuffer;
	pFramebufferTexture2D	FramebufferTexture2D;
	pCheckFramebufferStatus	CheckFramebufferStatus;
	pGenRenderbuffers		GenRenderbuffers;
	pDeleteRenderbuffers	DeleteRenderbuffers;
	pBindRenderbuffer		BindRenderbuffer;
	pRenderbufferStorage	RenderbufferStorage;
	pBlitFramebuffer		BlitFramebuffer;
	pGetIntegerv			GetIntegerv;
	pDrawBuffer				DrawBuffer;
	pDrawBuffers			DrawBuffers;
	pReadBuffer				ReadBuffer;
	pIsEnabled				IsEnabled;
	pEnableDisable			Enable, Disable;
	pGetError				GetError;
	pFlush					Flush;
	pGenTextures			GenTextures;
	pDeleteTextures			DeleteTextures;
	pBindTexture			BindTexture;
	pCreateMemoryObjectsEXT	CreateMemoryObjectsEXT;
	pDeleteMemoryObjectsEXT	DeleteMemoryObjectsEXT;
	pMemoryObjectParameterivEXT	MemoryObjectParameterivEXT;
	pImportMemoryWin32HandleEXT	ImportMemoryWin32HandleEXT;
	pTexStorageMem2DEXT		TexStorageMem2DEXT;
	pGenSemaphoresEXT		GenSemaphoresEXT;
	pDeleteSemaphoresEXT	DeleteSemaphoresEXT;
	pImportSemaphoreWin32HandleEXT	ImportSemaphoreWin32HandleEXT;
	pSemaphoreParameterui64vEXT	SemaphoreParameterui64vEXT;
	pWaitSemaphoreEXT		WaitSemaphoreEXT;
	pSignalSemaphoreEXT		SignalSemaphoreEXT;
} gl;

static cvar_t *r_dxgi;


/*
=================
The renderers' side: framebuffer 0 is ours
=================
*/
static GLuint_ Mapped( GLuint_ fb ) {
	return fb ? fb : dx.gameFbo;
}

static void Bound( GLenum_ target, GLuint_ fb ) {
	if ( target == GL_FRAMEBUFFER_ || target == GL_DRAW_FRAMEBUFFER_ ) {
		dx.boundDraw = fb;
	}
	if ( target == GL_FRAMEBUFFER_ || target == GL_READ_FRAMEBUFFER_ ) {
		dx.boundRead = fb;
	}
}

static void APIENTRY W_BindFramebuffer( GLenum_ target, GLuint_ fb ) {
	fb = Mapped( fb );
	gl.BindFramebuffer( target, fb );
	Bound( target, fb );
}

static void APIENTRY W_BindFramebufferEXT( GLenum_ target, GLuint_ fb ) {
	fb = Mapped( fb );
	gl.BindFramebufferEXT( target, fb );
	Bound( target, fb );
}

static void Unbinding( GLsizei_ n, const GLuint_ *ids ) {
	GLsizei_ i;
	for ( i = 0; i < n; i++ ) {
		// deleting a bound framebuffer binds 0, which is ours
		if ( ids[i] && ids[i] == dx.boundDraw ) {
			W_BindFramebuffer( GL_DRAW_FRAMEBUFFER_, 0 );
		}
		if ( ids[i] && ids[i] == dx.boundRead ) {
			W_BindFramebuffer( GL_READ_FRAMEBUFFER_, 0 );
		}
	}
}

static void APIENTRY W_DeleteFramebuffers( GLsizei_ n, const GLuint_ *ids ) {
	gl.DeleteFramebuffers( n, ids );
	Unbinding( n, ids );
}

static void APIENTRY W_DeleteFramebuffersEXT( GLsizei_ n, const GLuint_ *ids ) {
	gl.DeleteFramebuffersEXT( n, ids );
	Unbinding( n, ids );
}

// a window's buffers named while ours is bound: its color attachment
static GLenum_ Buffer( GLenum_ mode, GLuint_ bound ) {
	if ( bound != dx.gameFbo ) {
		return mode;
	}
	switch ( mode ) {
	case GL_FRONT_LEFT_: case GL_FRONT_RIGHT_: case GL_BACK_LEFT_: case GL_BACK_RIGHT_:
	case GL_FRONT_: case GL_BACK_: case GL_LEFT_: case GL_FRONT_AND_BACK_:
		return GL_COLOR_ATTACHMENT0_;
	}
	return mode;
}

static void APIENTRY W_DrawBuffer( GLenum_ mode ) {
	gl.DrawBuffer( Buffer( mode, dx.boundDraw ) );
}

static void APIENTRY W_DrawBuffers( GLsizei_ n, const GLenum_ *bufs ) {
	GLenum_ mapped[8];
	GLsizei_ i;
	if ( dx.boundDraw != dx.gameFbo || n > 8 ) {
		gl.DrawBuffers( n, bufs );
		return;
	}
	for ( i = 0; i < n; i++ ) {
		mapped[i] = Buffer( bufs[i], dx.boundDraw );
	}
	gl.DrawBuffers( n, mapped );
}

// the scissor test followed as the renderers set it: glIsEnabled, any glGet, waits under NVIDIA's
// threaded optimization for the driver's thread to catch up (0.7 ms a frame here)
static void APIENTRY W_Enable( GLenum_ cap ) {
	if ( cap == GL_SCISSOR_TEST_ ) {
		dx.scissor = qtrue;
	}
	gl.Enable( cap );
}

static void APIENTRY W_Disable( GLenum_ cap ) {
	if ( cap == GL_SCISSOR_TEST_ ) {
		dx.scissor = qfalse;
	}
	gl.Disable( cap );
}

static void APIENTRY W_ReadBuffer( GLenum_ mode ) {
	gl.ReadBuffer( Buffer( mode, dx.boundRead ) );
}

static void APIENTRY W_GetIntegerv( GLenum_ pname, GLint_ *data ) {
	gl.GetIntegerv( pname, data );
	switch ( pname ) {
	case GL_DRAW_FRAMEBUFFER_BINDING_:
	case GL_READ_FRAMEBUFFER_BINDING_:
		if ( (GLuint_)*data == dx.gameFbo ) {
			*data = 0;
		}
		break;
	case GL_DRAW_BUFFER_:
	case GL_DRAW_BUFFER0_:
		if ( dx.boundDraw == dx.gameFbo && *data == GL_COLOR_ATTACHMENT0_ ) {
			*data = GL_BACK_;
		}
		break;
	case GL_READ_BUFFER_:
		if ( dx.boundRead == dx.gameFbo && *data == GL_COLOR_ATTACHMENT0_ ) {
			*data = GL_BACK_;
		}
		break;
	}
}


/*
=================
DXGI_GetProcAddress

The renderers' GL functions: the wrappers above while frames go through DXGI
=================
*/
void *DXGI_GetProcAddress( const char *name, void *proc ) {
	if ( !dx.active || !proc ) {
		return proc;
	}
	if ( !strcmp( name, "glBindFramebuffer" ) ) { return (void *)W_BindFramebuffer; }
	if ( !strcmp( name, "glBindFramebufferEXT" ) ) { gl.BindFramebufferEXT = (pBindFramebuffer)proc; return (void *)W_BindFramebufferEXT; }
	if ( !strcmp( name, "glDeleteFramebuffers" ) ) { return (void *)W_DeleteFramebuffers; }
	if ( !strcmp( name, "glDeleteFramebuffersEXT" ) ) { gl.DeleteFramebuffersEXT = (pDeleteFramebuffers)proc; return (void *)W_DeleteFramebuffersEXT; }
	if ( !strcmp( name, "glDrawBuffer" ) ) { return (void *)W_DrawBuffer; }
	if ( !strcmp( name, "glDrawBuffers" ) || !strcmp( name, "glDrawBuffersARB" ) ) { gl.DrawBuffers = (pDrawBuffers)proc; return (void *)W_DrawBuffers; }
	if ( !strcmp( name, "glReadBuffer" ) ) { return (void *)W_ReadBuffer; }
	if ( !strcmp( name, "glGetIntegerv" ) ) { return (void *)W_GetIntegerv; }
	if ( !strcmp( name, "glEnable" ) ) { return (void *)W_Enable; }
	if ( !strcmp( name, "glDisable" ) ) { return (void *)W_Disable; }
	return proc;
}


qboolean DXGI_Active( void ) {
	return dx.active;
}


/*
=================
DXGI_Stop

Before the GL context goes: a window takes one flip-model swap chain at a time
=================
*/
void DXGI_Stop( void ) {
	int i;

	if ( dx.gameFbo || dx.shareFbo[0] ) {
		GLuint_ fbs[1 + DX_SHARED];
		fbs[0] = dx.gameFbo;
		for ( i = 0; i < DX_SHARED; i++ ) {
			fbs[1 + i] = dx.shareFbo[i];
		}
		gl.BindFramebuffer( GL_FRAMEBUFFER_, 0 );
		gl.DeleteFramebuffers( ARRAY_LEN( fbs ), fbs );
	}
	if ( dx.gameColor || dx.gameDepth ) {
		GLuint_ rbs[2] = { dx.gameColor, dx.gameDepth };
		gl.DeleteRenderbuffers( 2, rbs );
	}
	if ( dx.texture[0] ) {
		gl.DeleteTextures( DX_SHARED, dx.texture );
	}
	if ( dx.memory[0] ) {
		gl.DeleteMemoryObjectsEXT( DX_SHARED, dx.memory );
	}
	if ( dx.semaphore[0] || dx.semaphore[1] ) {
		gl.DeleteSemaphoresEXT( 2, dx.semaphore );
	}
	if ( dx.context ) {
		ID3D11DeviceContext4_ClearState( dx.context );
		ID3D11DeviceContext4_Flush( dx.context );
	}
	for ( i = 0; i < DX_SHARED; i++ ) {
		if ( dx.shared[i] ) ID3D11Texture2D_Release( dx.shared[i] );
	}
	if ( dx.fence[0] ) ID3D11Fence_Release( dx.fence[0] );
	if ( dx.fence[1] ) ID3D11Fence_Release( dx.fence[1] );
	if ( dx.backBuffer ) ID3D11Texture2D_Release( dx.backBuffer );
	if ( dx.swapChain ) IDXGISwapChain1_Release( dx.swapChain );
	if ( dx.context ) ID3D11DeviceContext4_Release( dx.context );
	if ( dx.device ) ID3D11Device5_Release( dx.device );
	{
		HMODULE d3d11 = dx.d3d11Dll, dxgi = dx.dxgiDll;
		Com_Memset( &dx, 0, sizeof( dx ) );
		dx.d3d11Dll = d3d11;	// kept loaded: the driver's threads may outlive the device a moment
		dx.dxgiDll = dxgi;
	}
}


static qboolean Fail( const char *why, HRESULT hr ) {
	if ( hr ) {
		Com_Printf( "DXGI presentation off: %s (0x%08lx); OpenGL presents\n", why, (unsigned long)hr );
	} else {
		Com_Printf( "DXGI presentation off: %s; OpenGL presents\n", why );
	}
	DXGI_Stop();
	return qfalse;
}


#define LOAD( x, name ) ( ( *(void **)&gl.x = SDL_GL_GetProcAddress( name ) ) != NULL )

static qboolean LoadGL( void ) {
	return LOAD( GenFramebuffers, "glGenFramebuffers" ) && LOAD( DeleteFramebuffers, "glDeleteFramebuffers" )
		&& LOAD( BindFramebuffer, "glBindFramebuffer" ) && LOAD( FramebufferRenderbuffer, "glFramebufferRenderbuffer" )
		&& LOAD( FramebufferTexture2D, "glFramebufferTexture2D" ) && LOAD( CheckFramebufferStatus, "glCheckFramebufferStatus" )
		&& LOAD( GenRenderbuffers, "glGenRenderbuffers" ) && LOAD( DeleteRenderbuffers, "glDeleteRenderbuffers" )
		&& LOAD( BindRenderbuffer, "glBindRenderbuffer" ) && LOAD( RenderbufferStorage, "glRenderbufferStorage" )
		&& LOAD( BlitFramebuffer, "glBlitFramebuffer" ) && LOAD( GetIntegerv, "glGetIntegerv" )
		&& LOAD( DrawBuffer, "glDrawBuffer" ) && LOAD( ReadBuffer, "glReadBuffer" ) && LOAD( IsEnabled, "glIsEnabled" )
		&& LOAD( Enable, "glEnable" ) && LOAD( Disable, "glDisable" ) && LOAD( GetError, "glGetError" ) && LOAD( Flush, "glFlush" )
		&& LOAD( GenTextures, "glGenTextures" ) && LOAD( DeleteTextures, "glDeleteTextures" ) && LOAD( BindTexture, "glBindTexture" )
		&& LOAD( CreateMemoryObjectsEXT, "glCreateMemoryObjectsEXT" ) && LOAD( DeleteMemoryObjectsEXT, "glDeleteMemoryObjectsEXT" )
		&& LOAD( MemoryObjectParameterivEXT, "glMemoryObjectParameterivEXT" )
		&& LOAD( ImportMemoryWin32HandleEXT, "glImportMemoryWin32HandleEXT" ) && LOAD( TexStorageMem2DEXT, "glTexStorageMem2DEXT" )
		&& LOAD( GenSemaphoresEXT, "glGenSemaphoresEXT" ) && LOAD( DeleteSemaphoresEXT, "glDeleteSemaphoresEXT" )
		&& LOAD( ImportSemaphoreWin32HandleEXT, "glImportSemaphoreWin32HandleEXT" )
		&& LOAD( SemaphoreParameterui64vEXT, "glSemaphoreParameterui64vEXT" )
		&& LOAD( WaitSemaphoreEXT, "glWaitSemaphoreEXT" ) && LOAD( SignalSemaphoreEXT, "glSignalSemaphoreEXT" );
}


/*
=================
The DXGI adapter the GL context runs on (GL_EXT_memory_object's LUID): sharing works on one GPU
=================
*/
static IDXGIAdapter1 *GLAdapter( IDXGIFactory1 *factory, char *name, int nameSize ) {
	pGetUnsignedBytevEXT getLuid = (pGetUnsignedBytevEXT)SDL_GL_GetProcAddress( "glGetUnsignedBytevEXT" );
	unsigned char luid[8];
	IDXGIAdapter1 *adapter;
	UINT i;

	Q_strncpyz( name, "the default adapter", nameSize );
	if ( !getLuid ) {
		return NULL;
	}
	Com_Memset( luid, 0, sizeof( luid ) );
	getLuid( GL_DEVICE_LUID_EXT_, luid );
	while ( gl.GetError() ) {
		;
	}
	for ( i = 0; IDXGIFactory1_EnumAdapters1( factory, i, &adapter ) != DXGI_ERROR_NOT_FOUND; i++ ) {
		DXGI_ADAPTER_DESC1 desc;
		if ( SUCCEEDED( IDXGIAdapter1_GetDesc1( adapter, &desc ) ) && !memcmp( &desc.AdapterLuid, luid, sizeof( luid ) ) ) {
			WideCharToMultiByte( CP_UTF8, 0, desc.Description, -1, name, nameSize, NULL, NULL );
			return adapter;
		}
		IDXGIAdapter1_Release( adapter );
	}
	return NULL;
}


/*
=================
A shared texture's NT handle, imported into GL as memory and a texture on it
=================
*/
static qboolean ShareTexture( int i, int width, int height ) {
	D3D11_TEXTURE2D_DESC tex;
	IDXGIResource1 *resource = NULL;
	HANDLE handle = NULL;
	const GLint_ dedicated = 1;
	HRESULT hr;

	Com_Memset( &tex, 0, sizeof( tex ) );
	tex.Width = width;
	tex.Height = height;
	tex.MipLevels = 1;
	tex.ArraySize = 1;
	tex.Format = DXGI_FORMAT_R8G8B8A8_UNORM;	// GL_RGBA8's own layout
	tex.SampleDesc.Count = 1;
	tex.Usage = D3D11_USAGE_DEFAULT;
	tex.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	tex.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
	hr = ID3D11Device5_CreateTexture2D( dx.device, &tex, NULL, &dx.shared[i] );
	if ( FAILED( hr ) ) {
		return Fail( "CreateTexture2D", hr );
	}
	hr = ID3D11Texture2D_QueryInterface( dx.shared[i], &IID_IDXGIResource1, (void **)&resource );
	if ( SUCCEEDED( hr ) ) {
		hr = IDXGIResource1_CreateSharedHandle( resource, NULL, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, NULL, &handle );
		IDXGIResource1_Release( resource );
	}
	if ( FAILED( hr ) || !handle ) {
		return Fail( "CreateSharedHandle (texture)", hr );
	}

	while ( gl.GetError() ) {
		;
	}
	{
		// the size a D3D11 image's memory takes: 0 here (NVIDIA: the texture's own, padded size is
		// refused by the texture storage as too small), else the padded size
		const GLuint64_ pitch = ( (GLuint64_)width * 4 + 255 ) & ~(GLuint64_)255;
		const GLuint64_ sizes[2] = { 0, ( pitch * height + 65535 ) & ~(GLuint64_)65535 };
		int k;
		for ( k = 0; k < ARRAY_LEN( sizes ); k++ ) {
			gl.CreateMemoryObjectsEXT( 1, &dx.memory[i] );
			gl.MemoryObjectParameterivEXT( dx.memory[i], GL_DEDICATED_MEMORY_OBJECT_EXT_, &dedicated );
			gl.ImportMemoryWin32HandleEXT( dx.memory[i], sizes[k], GL_HANDLE_TYPE_D3D11_IMAGE_EXT_, handle );
			gl.GenTextures( 1, &dx.texture[i] );
			gl.BindTexture( GL_TEXTURE_2D_, dx.texture[i] );
			gl.TexStorageMem2DEXT( GL_TEXTURE_2D_, 1, GL_RGBA8_, width, height, dx.memory[i], 0 );
			gl.BindTexture( GL_TEXTURE_2D_, 0 );
			if ( !gl.GetError() ) {
				break;
			}
			while ( gl.GetError() ) {
				;
			}
			gl.DeleteTextures( 1, &dx.texture[i] );
			gl.DeleteMemoryObjectsEXT( 1, &dx.memory[i] );
			dx.memory[i] = dx.texture[i] = 0;
		}
	}
	CloseHandle( handle );	// the import keeps its own reference
	if ( !dx.texture[i] ) {
		return Fail( "the GL import of a shared texture", 0 );
	}

	gl.GenFramebuffers( 1, &dx.shareFbo[i] );
	gl.BindFramebuffer( GL_FRAMEBUFFER_, dx.shareFbo[i] );
	gl.FramebufferTexture2D( GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_TEXTURE_2D_, dx.texture[i], 0 );
	if ( gl.CheckFramebufferStatus( GL_FRAMEBUFFER_ ) != GL_FRAMEBUFFER_COMPLETE_ ) {
		return Fail( "the shared texture's framebuffer is incomplete", 0 );
	}
	return qtrue;
}


/*
=================
DXGI_Start

With the GL context current on the new window. width and height are its drawable's.
=================
*/
qboolean DXGI_Start( SDL_Window *window, int width, int height, int swapInterval ) {
	static const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
	pD3D11CreateDevice createDevice;
	pCreateDXGIFactory1 createFactory;
	IDXGIFactory1 *factory1 = NULL;
	IDXGIFactory2 *factory2 = NULL;
	IDXGIFactory5 *factory5 = NULL;
	IDXGIAdapter1 *adapter;
	IDXGIDevice1 *dxgiDevice = NULL;
	ID3D11Device *device = NULL;
	ID3D11DeviceContext *context = NULL;
	DXGI_SWAP_CHAIN_DESC1 desc;
	SDL_SysWMinfo info;
	BOOL tearing = FALSE;
	char adapterName[128];
	HWND hwnd;
	HRESULT hr;
	int i;

	r_dxgi = Cvar_Get( "r_dxgi", "1", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_CheckRange( r_dxgi, "0", "1", CV_INTEGER );
	Cvar_SetDescription( r_dxgi, "Windows: show the OpenGL renderers' frames through a DXGI swap chain, as DirectX games do, so recorders and streaming apps (OBS, Discord, the Game Bar) capture the game. 0: OpenGL presents them itself." );

	DXGI_Stop();
	if ( !r_dxgi->integer ) {
		return qfalse;
	}

	SDL_VERSION( &info.version );
	if ( !SDL_GetWindowWMInfo( window, &info ) || info.subsystem != SDL_SYSWM_WINDOWS ) {
		return Fail( "no window handle", 0 );
	}
	hwnd = info.info.win.window;

	if ( !SDL_GL_ExtensionSupported( "GL_EXT_memory_object_win32" ) || !SDL_GL_ExtensionSupported( "GL_EXT_semaphore_win32" ) ) {
		return Fail( "the GL driver has no GL_EXT_memory_object_win32 and GL_EXT_semaphore_win32", 0 );
	}
	if ( !LoadGL() ) {
		return Fail( "GL functions missing", 0 );
	}
	gl.DrawBuffers = (pDrawBuffers)SDL_GL_GetProcAddress( "glDrawBuffers" );

	if ( !dx.d3d11Dll ) dx.d3d11Dll = LoadLibraryA( "d3d11.dll" );
	if ( !dx.dxgiDll ) dx.dxgiDll = LoadLibraryA( "dxgi.dll" );
	createDevice = dx.d3d11Dll ? (pD3D11CreateDevice)GetProcAddress( dx.d3d11Dll, "D3D11CreateDevice" ) : NULL;
	createFactory = dx.dxgiDll ? (pCreateDXGIFactory1)GetProcAddress( dx.dxgiDll, "CreateDXGIFactory1" ) : NULL;
	if ( !createDevice || !createFactory ) {
		return Fail( "no Direct3D 11", 0 );
	}
	hr = createFactory( &IID_IDXGIFactory1, (void **)&factory1 );
	if ( FAILED( hr ) ) {
		return Fail( "CreateDXGIFactory1", hr );
	}
	adapter = GLAdapter( factory1, adapterName, sizeof( adapterName ) );
	IDXGIFactory1_Release( factory1 );
	if ( !adapter ) {
		return Fail( "no DXGI adapter matches the GL context's", 0 );
	}
	hr = createDevice( (IDXGIAdapter *)adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		levels, ARRAY_LEN( levels ), D3D11_SDK_VERSION, &device, NULL, &context );
	IDXGIAdapter1_Release( adapter );
	if ( FAILED( hr ) ) {
		return Fail( "D3D11CreateDevice", hr );
	}
	// Direct3D 11.4 (Windows 10 1703): shared fences
	hr = ID3D11Device_QueryInterface( device, &IID_ID3D11Device5, (void **)&dx.device );
	if ( SUCCEEDED( hr ) ) {
		hr = ID3D11DeviceContext_QueryInterface( context, &IID_ID3D11DeviceContext4, (void **)&dx.context );
	}
	ID3D11DeviceContext_Release( context );
	ID3D11Device_Release( device );
	if ( FAILED( hr ) ) {
		return Fail( "no Direct3D 11.4 (Windows 10 1703 or newer)", hr );
	}

	if ( SUCCEEDED( ID3D11Device5_QueryInterface( dx.device, &IID_IDXGIDevice1, (void **)&dxgiDevice ) ) ) {
		IDXGIAdapter *devAdapter = NULL;
		IDXGIDevice1_SetMaximumFrameLatency( dxgiDevice, 1 );
		if ( SUCCEEDED( IDXGIDevice1_GetAdapter( dxgiDevice, &devAdapter ) ) ) {
			IDXGIAdapter_GetParent( devAdapter, &IID_IDXGIFactory2, (void **)&factory2 );
			IDXGIAdapter_Release( devAdapter );
		}
		IDXGIDevice1_Release( dxgiDevice );
	}
	if ( !factory2 ) {
		return Fail( "no IDXGIFactory2", 0 );
	}
	if ( SUCCEEDED( IDXGIFactory2_QueryInterface( factory2, &IID_IDXGIFactory5, (void **)&factory5 ) ) ) {
		if ( FAILED( IDXGIFactory5_CheckFeatureSupport( factory5, DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof( tearing ) ) ) ) {
			tearing = FALSE;
		}
		IDXGIFactory5_Release( factory5 );
	}

	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.Width = width;
	desc.Height = height;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;	// as the shared textures, for CopyResource
	desc.SampleDesc.Count = 1;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.BufferCount = 2;
	desc.Scaling = DXGI_SCALING_STRETCH;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	desc.Flags = tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
	hr = IDXGIFactory2_CreateSwapChainForHwnd( factory2, (IUnknown *)dx.device, hwnd, &desc, NULL, NULL, &dx.swapChain );
	if ( FAILED( hr ) ) {
		IDXGIFactory2_Release( factory2 );
		return Fail( "CreateSwapChainForHwnd", hr );
	}
	// the window's full screen stays SDL's: no Alt+Enter of DXGI's own
	IDXGIFactory2_MakeWindowAssociation( factory2, hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES );
	IDXGIFactory2_Release( factory2 );

	hr = IDXGISwapChain1_GetBuffer( dx.swapChain, 0, &IID_ID3D11Texture2D, (void **)&dx.backBuffer );
	if ( FAILED( hr ) ) {
		return Fail( "GetBuffer", hr );
	}

	// the fences, GL's as semaphores
	for ( i = 0; i < 2; i++ ) {
		HANDLE fenceHandle = NULL;
		hr = ID3D11Device5_CreateFence( dx.device, 0, D3D11_FENCE_FLAG_SHARED, &IID_ID3D11Fence, (void **)&dx.fence[i] );
		if ( SUCCEEDED( hr ) ) {
			hr = ID3D11Fence_CreateSharedHandle( dx.fence[i], NULL, GENERIC_ALL, NULL, &fenceHandle );
		}
		if ( FAILED( hr ) || !fenceHandle ) {
			return Fail( "a shared fence", hr );
		}
		gl.GenSemaphoresEXT( 1, &dx.semaphore[i] );
		gl.ImportSemaphoreWin32HandleEXT( dx.semaphore[i], GL_HANDLE_TYPE_D3D12_FENCE_EXT_, fenceHandle );
		CloseHandle( fenceHandle );
		if ( gl.GetError() ) {
			return Fail( "the GL import of a fence", 0 );
		}
	}

	for ( i = 0; i < DX_SHARED; i++ ) {
		if ( !ShareTexture( i, width, height ) ) {
			return qfalse;
		}
	}

	gl.GenRenderbuffers( 1, &dx.gameColor );
	gl.BindRenderbuffer( GL_RENDERBUFFER_, dx.gameColor );
	gl.RenderbufferStorage( GL_RENDERBUFFER_, GL_RGBA8_, width, height );
	gl.GenRenderbuffers( 1, &dx.gameDepth );
	gl.BindRenderbuffer( GL_RENDERBUFFER_, dx.gameDepth );
	gl.RenderbufferStorage( GL_RENDERBUFFER_, GL_DEPTH24_STENCIL8_, width, height );
	gl.BindRenderbuffer( GL_RENDERBUFFER_, 0 );
	gl.GenFramebuffers( 1, &dx.gameFbo );
	gl.BindFramebuffer( GL_FRAMEBUFFER_, dx.gameFbo );
	gl.FramebufferRenderbuffer( GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_RENDERBUFFER_, dx.gameColor );
	gl.FramebufferRenderbuffer( GL_FRAMEBUFFER_, GL_DEPTH_STENCIL_ATTACHMENT_, GL_RENDERBUFFER_, dx.gameDepth );
	if ( gl.CheckFramebufferStatus( GL_FRAMEBUFFER_ ) != GL_FRAMEBUFFER_COMPLETE_ ) {
		return Fail( "the game's framebuffer is incomplete", 0 );
	}
	gl.DrawBuffer( GL_COLOR_ATTACHMENT0_ );
	gl.ReadBuffer( GL_COLOR_ATTACHMENT0_ );
	dx.boundDraw = dx.boundRead = dx.gameFbo;
	gl.Disable( GL_SCISSOR_TEST_ );	// as a new context has it; the renderers' own calls are followed from here
	dx.scissor = qfalse;
	while ( gl.GetError() ) {
		;
	}

	dx.width = width;
	dx.height = height;
	dx.interval = swapInterval > 0 ? swapInterval : 0;
	dx.presentFlags = ( !dx.interval && tearing ) ? DXGI_PRESENT_ALLOW_TEARING : 0;
	dx.active = qtrue;
	Com_Printf( "DXGI presentation: %dx%d on %s, flip discard, vsync %s%s\n", width, height, adapterName,
		dx.interval ? "on" : "off", dx.presentFlags ? ", tearing" : "" );
	return qtrue;
}


/*
=================
DXGI_Present

The frame the renderers drew into our framebuffer: turned the right way up into a shared texture
(D3D's rows run down, GL's up), copied into the swap chain's back buffer, presented. The GPU
waits, never the CPU: GL for D3D's last copy out of that texture, D3D for GL's blit.
=================
*/
void DXGI_Present( void ) {
	const int n = dx.frame % DX_SHARED;
	const GLenum_ drawLayout = GL_LAYOUT_COLOR_ATTACHMENT_EXT_, srcLayout = GL_LAYOUT_TRANSFER_SRC_EXT_;
	GLuint64_ value;
	GLboolean_ scissor;
	HRESULT hr;

	if ( !dx.active ) {
		return;
	}

	// a GL wait is one more catch-up with NVIDIA's driver thread: the fence is asked first, and D3D's
	// copy out of this texture, three frames back, is nearly always done
	if ( dx.copied[n] && ID3D11Fence_GetCompletedValue( dx.fence[1] ) < dx.copied[n] ) {
		value = dx.copied[n];
		gl.SemaphoreParameterui64vEXT( dx.semaphore[1], GL_D3D12_FENCE_VALUE_EXT_, &value );
		gl.WaitSemaphoreEXT( dx.semaphore[1], 0, NULL, 1, &dx.texture[n], &drawLayout );
	}
	scissor = dx.scissor;
	if ( scissor ) {
		gl.Disable( GL_SCISSOR_TEST_ );	// a blit is scissored
	}
	gl.BindFramebuffer( GL_READ_FRAMEBUFFER_, dx.gameFbo );
	gl.BindFramebuffer( GL_DRAW_FRAMEBUFFER_, dx.shareFbo[n] );
	gl.BlitFramebuffer( 0, 0, dx.width, dx.height, 0, dx.height, dx.width, 0, GL_COLOR_BUFFER_BIT_, GL_NEAREST_ );
	gl.BindFramebuffer( GL_READ_FRAMEBUFFER_, dx.boundRead );
	gl.BindFramebuffer( GL_DRAW_FRAMEBUFFER_, dx.boundDraw );
	if ( scissor ) {
		gl.Enable( GL_SCISSOR_TEST_ );
	}
	value = ++dx.drawn;
	gl.SemaphoreParameterui64vEXT( dx.semaphore[0], GL_D3D12_FENCE_VALUE_EXT_, &value );
	gl.SignalSemaphoreEXT( dx.semaphore[0], 0, NULL, 1, &dx.texture[n], &srcLayout );
	gl.Flush();	// the signal reaches the GPU now, not at GL's next flush

	ID3D11DeviceContext4_Wait( dx.context, dx.fence[0], value );
	ID3D11DeviceContext4_CopyResource( dx.context, (ID3D11Resource *)dx.backBuffer, (ID3D11Resource *)dx.shared[n] );
	dx.copied[n] = ++dx.copies;
	ID3D11DeviceContext4_Signal( dx.context, dx.fence[1], dx.copied[n] );
	hr = IDXGISwapChain1_Present( dx.swapChain, dx.interval, dx.presentFlags );
	dx.frame++;

	if ( hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ) {
		// the driver restarted (a GPU reset, a driver update): a vid_restart builds everything again
		if ( ++dx.failures == 1 ) {
			Com_Printf( S_COLOR_YELLOW "DXGI presentation lost (0x%08lx): restarting the renderer\n", (unsigned long)hr );
			Cbuf_AddText( "vid_restart\n" );
		}
	}
}

#endif // _WIN32
