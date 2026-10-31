/*
Oz: the engine's services for the map editor's C++ (ENGINE_PLAN.md P13, engine/EDITOR.md).

Compiled with the client (its defines and headers), so the editor's C++ only sees ed_engine.h.
*/
#include "../../../quake3e/code/client/client.h"
#include "ed_engine.h"

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#endif

const ozEdRender_t *OzEd_Renderer( void ) {
	const ozEdRender_t *r;

	if ( !cls.rendererStarted || !re.GetEditorRender ) {
		return NULL;
	}
	r = re.GetEditorRender();
	return r && r->version == OZ_ED_RENDER_VERSION ? r : NULL;
}

const char *OzEd_RendererName( void ) {
	return Cvar_VariableString( "cl_renderer" );
}

void OzEd_ClearScene( void ) {
	re.ClearScene();
}

void OzEd_AddModel( int model, const float origin[3], const float axis[3][3], int customShader, const unsigned char rgba[4] ) {
	refEntity_t ent;

	Com_Memset( &ent, 0, sizeof( ent ) );
	ent.reType = RT_MODEL;
	ent.hModel = model;
	VectorCopy( origin, ent.origin );
	VectorCopy( origin, ent.oldorigin );
	VectorCopy( axis[0], ent.axis[0] );
	VectorCopy( axis[1], ent.axis[1] );
	VectorCopy( axis[2], ent.axis[2] );
	ent.customShader = customShader;
	ent.shader.rgba[0] = rgba ? rgba[0] : 255;
	ent.shader.rgba[1] = rgba ? rgba[1] : 255;
	ent.shader.rgba[2] = rgba ? rgba[2] : 255;
	ent.shader.rgba[3] = rgba ? rgba[3] : 255;
	ent.renderfx = RF_NOSHADOW;
	ent.nonNormalizedAxes = fabs( VectorLength( axis[0] ) - 1.0f ) > 0.001f;	// a scaled model (the weapons' 1.5)
	re.AddRefEntityToScene( &ent, qfalse );
}

/*
================
OzEd_AddFighter: the parts registered once a renderer start (the oz renderer swaps them for the rig),
their standing frames from animation.cfg (LEGS_IDLE and TORSO_STAND, as the showcase reads them)
================
*/
int OzE_RendererGeneration( void );	// oz_ui_engine.c

typedef struct {
	char		name[64];
	int			gen;
	qhandle_t	part[3], skin[3];
	int			legs, torso;
} edFighter_t;

static edFighter_t edFighters[8];

static const edFighter_t *OzEd_Fighter( const char *model, const char *skin ) {
	static const char *parts[3] = { "lower", "upper", "head" };
	char key[64];
	edFighter_t *f, *spare = NULL;
	char *text;
	int i;

	Com_sprintf( key, sizeof( key ), "%s/%s", model, skin );
	for ( i = 0; i < ARRAY_LEN( edFighters ); i++ ) {
		f = &edFighters[i];
		if ( !Q_stricmp( f->name, key ) && f->gen == OzE_RendererGeneration() ) {
			return f->part[0] ? f : NULL;
		}
		if ( !spare && ( !f->name[0] || f->gen != OzE_RendererGeneration() ) ) {
			spare = f;
		}
	}
	f = spare ? spare : &edFighters[0];
	Com_Memset( f, 0, sizeof( *f ) );
	Q_strncpyz( f->name, key, sizeof( f->name ) );
	f->gen = OzE_RendererGeneration();
	if ( !cls.rendererStarted ) {
		return NULL;
	}
	for ( i = 0; i < 3; i++ ) {
		f->part[i] = re.RegisterModel( va( "models/players/%s/%s.md3", model, parts[i] ) );
		f->skin[i] = re.RegisterSkin( va( "models/players/%s/%s_%s.skin", model, parts[i], skin ) );
		if ( !f->part[i] ) {
			f->part[0] = 0;
			return NULL;
		}
	}
	if ( FS_ReadFile( va( "models/players/%s/animation.cfg", model ), (void **)&text ) > 0 ) {
		const char *p = text, *tok;
		int nums[25], n = 0;
		while ( n < 25 ) {
			tok = COM_Parse( &p );
			if ( !tok[0] ) {
				break;
			}
			if ( !( tok[0] >= '0' && tok[0] <= '9' ) ) {
				if ( !Q_stricmp( tok, "headoffset" ) ) {
					COM_Parse( &p ); COM_Parse( &p ); COM_Parse( &p );
				} else if ( !Q_stricmp( tok, "sex" ) || !Q_stricmp( tok, "footsteps" ) ) {
					COM_Parse( &p );
				}
				continue;
			}
			nums[n++] = atoi( tok );
			COM_Parse( &p ); COM_Parse( &p ); COM_Parse( &p );
		}
		FS_FreeFile( text );
		if ( n >= 25 ) {
			f->torso = nums[11];						// TORSO_STAND
			f->legs = nums[22] - ( nums[13] - nums[6] );	// LEGS_IDLE, less the torso-only frames
		}
	}
	return f;
}

static void OzEd_Attach( refEntity_t *e, const refEntity_t *parent, const char *tag ) {
	orientation_t o;
	int i;

	re.LerpTag( &o, parent->hModel, parent->frame, parent->frame, 0, tag );
	VectorCopy( parent->origin, e->origin );
	for ( i = 0; i < 3; i++ ) {
		VectorMA( e->origin, o.origin[i], parent->axis[i], e->origin );
	}
	MatrixMultiply( o.axis, ( (refEntity_t *)parent )->axis, e->axis );
	VectorCopy( e->origin, e->oldorigin );
}

int OzEd_AddFighter( const char *model, const char *skin, const float origin[3], float yaw ) {
	const edFighter_t *f = OzEd_Fighter( model, skin );
	refEntity_t legs, torso, head;
	vec3_t angles;

	if ( !f ) {
		return 0;
	}
	Com_Memset( &legs, 0, sizeof( legs ) );
	legs.reType = RT_MODEL;
	VectorSet( angles, 0, yaw, 0 );
	AnglesToAxis( angles, legs.axis );
	VectorCopy( origin, legs.origin );
	VectorCopy( origin, legs.oldorigin );
	VectorCopy( origin, legs.lightingOrigin );
	legs.renderfx = RF_LIGHTING_ORIGIN | RF_NOSHADOW;
	legs.shader.rgba[0] = legs.shader.rgba[1] = legs.shader.rgba[2] = legs.shader.rgba[3] = 255;
	torso = head = legs;
	legs.hModel = f->part[0];
	legs.customSkin = f->skin[0];
	legs.frame = legs.oldframe = f->legs;
	re.AddRefEntityToScene( &legs, qfalse );
	torso.hModel = f->part[1];
	torso.customSkin = f->skin[1];
	torso.frame = torso.oldframe = f->torso;
	OzEd_Attach( &torso, &legs, "tag_torso" );
	re.AddRefEntityToScene( &torso, qfalse );
	head.hModel = f->part[2];
	head.customSkin = f->skin[2];
	OzEd_Attach( &head, &torso, "tag_head" );
	re.AddRefEntityToScene( &head, qfalse );
	return 1;
}

int OzEd_RegisterModel( const char *name ) {
	return cls.rendererStarted ? re.RegisterModel( name ) : 0;
}

void OzEd_RenderScene( int x, int y, int w, int h, const float origin[3], const float axis[3][3], float fovX, float fovY, int timeMs, int ortho ) {
	refdef_t rd;

	Com_Memset( &rd, 0, sizeof( rd ) );
	rd.x = x;
	rd.y = y;
	rd.width = w;
	rd.height = h;
	rd.fov_x = fovX;
	rd.fov_y = fovY;
	VectorCopy( origin, rd.vieworg );
	VectorCopy( axis[0], rd.viewaxis[0] );
	VectorCopy( axis[1], rd.viewaxis[1] );
	VectorCopy( axis[2], rd.viewaxis[2] );
	rd.time = timeMs;
	rd.rdflags = OzEd_World()[0] ? RDF_OZ_EDITOR : RDF_NOWORLDMODEL | RDF_OZ_EDITOR;	// the lit preview: the compiled map as the world
	if ( ortho ) {
		rd.rdflags = RDF_NOWORLDMODEL | RDF_OZ_EDITOR | RDF_OZ_ORTHO;	// a 2D view: the editor's lines alone
	}
	re.RenderScene( &rd );
}

/*
================
The lit preview's world: a compiled map loaded as the renderer's world while the editor runs. As a
map load (and a disconnect) does, the client's systems stop, letting go of their hunk, and start
again first: the renderer takes one world a start, and a world left on the hunk would pile up.
Only from the menus (no map running); run as a command, outside any frame's drawing.
================
*/
int OzE_RendererGeneration( void );	// oz_ui_engine.c

static int edWorldGen = -1;
static char edWorld[MAX_QPATH];

void OzEd_LoadWorld( const char *name ) {
	if ( cls.state != CA_DISCONNECTED ) {
		return;
	}
	CL_ShutdownAll();	// its CL_ShutdownVMs clears the client's hunk, the world's with it
	CL_StartHunkUsers();
	edWorld[0] = '\0';
	if ( name && name[0] && FS_FileExists( name ) && cls.rendererStarted ) {
		re.LoadWorld( name );
		Q_strncpyz( edWorld, name, sizeof( edWorld ) );
		edWorldGen = OzE_RendererGeneration();
	}
}

const char *OzEd_World( void ) {
	return edWorld[0] && edWorldGen == OzE_RendererGeneration() ? edWorld : "";
}

void OzEd_Fill( int x, int y, int w, int h, const float rgba[4] ) {
	re.SetColor( rgba );
	re.DrawStretchPic( x, y, w, h, 0, 0, 1, 1, cls.whiteShader );
	re.SetColor( NULL );
}

/*
The fly camera's mouse: while looking, sdl_input.c's IN_Frame keeps the mouse captured (relative
motion, the cursor hidden) although a page is open, and CL_MouseEvent hands the motion to the editor
(OzUI_EditorLook). When looking ends, the cursor goes back where it was.
*/
static qboolean looking;

void OzEd_SetLooking( int on ) {
	looking = on ? qtrue : qfalse;
}

int OzEd_Looking( void ) {
	return looking;
}

int OzEd_ReadOSFile( const char *path, char **data ) {
	FILE *f = Sys_FOpen( path, "rb" );
	long len;

	*data = NULL;
	if ( !f ) {
		return -1;
	}
	fseek( f, 0, SEEK_END );
	len = ftell( f );
	fseek( f, 0, SEEK_SET );
	*data = Z_Malloc( len + 1 );
	if ( len > 0 && fread( *data, 1, len, f ) != (size_t)len ) {
		Z_Free( *data );
		*data = NULL;
		fclose( f );
		return -1;
	}
	(*data)[len] = '\0';
	fclose( f );
	return (int)len;
}

int OzEd_WriteOSFile( const char *path, const void *data, int size ) {
	char tmp[MAX_OSPATH];
	FILE *f;
	qboolean ok;

	OzEd_MakeDirs( path );
	Com_sprintf( tmp, sizeof( tmp ), "%s.tmp", path );
	f = Sys_FOpen( tmp, "wb" );
	if ( !f ) {
		return 0;
	}
	ok = fwrite( data, 1, size, f ) == (size_t)size;
	ok = fclose( f ) == 0 && ok;
	if ( !ok ) {
		remove( tmp );
		return 0;
	}
#ifdef _WIN32
	return MoveFileExA( tmp, path, MOVEFILE_REPLACE_EXISTING ) ? 1 : 0;
#else
	return rename( tmp, path ) == 0;
#endif
}

void OzEd_Free( void *p ) {
	if ( p ) {
		Z_Free( p );
	}
}

void OzEd_MakeDirs( const char *path ) {
	char buf[MAX_OSPATH];
	char *s;

	Q_strncpyz( buf, path, sizeof( buf ) );
	for ( s = buf + 1; *s; s++ ) {
		if ( *s == '/' || *s == '\\' ) {
			const char c = *s;
			*s = '\0';
			Sys_Mkdir( buf );
			*s = c;
		}
	}
}

const char *OzEd_HomePath( void ) {
	return Cvar_VariableString( "fs_homepath" );
}

const char *OzEd_BaseQ3( void ) {
	static char path[MAX_OSPATH];
	const char *roots[2] = { Cvar_VariableString( "fs_basepath" ), Cvar_VariableString( "fs_steampath" ) };
	int i;

	for ( i = 0; i < 2; i++ ) {
		if ( roots[i][0] ) {
			Com_sprintf( path, sizeof( path ), "%s/baseq3/pak0.pk3", roots[i] );
			if ( OzEd_FileExists( path ) ) {
				Com_sprintf( path, sizeof( path ), "%s/baseq3", roots[i] );
				return path;
			}
		}
	}
	return "";
}

const char *OzEd_EngineDir( void ) {
	static char dir[MAX_OSPATH];
#ifdef _WIN32
	char *slash;
	GetModuleFileNameA( NULL, dir, sizeof( dir ) );
	slash = strrchr( dir, '\\' );
	if ( slash ) {
		*slash = '\0';
	}
#else
	Q_strncpyz( dir, ".", sizeof( dir ) );
#endif
	return dir;
}

int OzEd_FileExists( const char *path ) {
	FILE *f = Sys_FOpen( path, "rb" );
	if ( !f ) {
		return 0;
	}
	fclose( f );
	return 1;
}

char **OzEd_ListOSFiles( const char *dir, const char *ext, int *count ) {
	return Sys_ListFiles( dir, ext, NULL, count, qfalse );
}

void OzEd_FreeList( char **list ) {
	Sys_FreeFileList( list );
}

#ifdef USE_SDL
void IN_PumpEvents( void );	// sdl_input.c (Oz)
#endif

/*
================
Programs the editor runs (q3map2, mbspc), in the background: a few at a time, each a slot
================
*/
#ifdef _WIN32
static struct {
	HANDLE	process;
	HANDLE	out;
} procs[4];
#endif

int OzEd_Start( const char *exe, const char *args, const char *dir, const char *log ) {
#ifdef _WIN32
	char cmd[8192];
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	SECURITY_ATTRIBUTES sa;
	HANDLE out;
	int slot;

	for ( slot = 0; slot < (int)ARRAY_LEN( procs ) && procs[slot].process; slot++ ) {
	}
	if ( slot == (int)ARRAY_LEN( procs ) ) {
		return 0;
	}
	Com_sprintf( cmd, sizeof( cmd ), "\"%s\" %s", exe, args );
	Com_Memset( &sa, 0, sizeof( sa ) );
	sa.nLength = sizeof( sa );
	sa.bInheritHandle = TRUE;
	OzEd_MakeDirs( log );
	out = CreateFileA( log, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
	Com_Memset( &si, 0, sizeof( si ) );
	si.cb = sizeof( si );
	si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
	si.wShowWindow = SW_HIDE;
	si.hStdOutput = out;
	si.hStdError = out;
	si.hStdInput = GetStdHandle( STD_INPUT_HANDLE );
	if ( !CreateProcessA( NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, NULL, dir, &si, &pi ) ) {
		if ( out != INVALID_HANDLE_VALUE ) {
			CloseHandle( out );
		}
		return 0;
	}
	CloseHandle( pi.hThread );
	procs[slot].process = pi.hProcess;
	procs[slot].out = out;
	return slot + 1;
#else
	return 0;
#endif
}

static void OzEd_Close( int slot ) {
#ifdef _WIN32
	CloseHandle( procs[slot].process );
	if ( procs[slot].out != INVALID_HANDLE_VALUE ) {
		CloseHandle( procs[slot].out );
	}
	Com_Memset( &procs[slot], 0, sizeof( procs[slot] ) );
#endif
}

int OzEd_Poll( int handle, int *code ) {
#ifdef _WIN32
	DWORD c = (DWORD)-1;
	const int slot = handle - 1;

	if ( slot < 0 || slot >= (int)ARRAY_LEN( procs ) || !procs[slot].process ) {
		*code = -1;
		return 0;
	}
	if ( WaitForSingleObject( procs[slot].process, 0 ) == WAIT_TIMEOUT ) {
		return 1;
	}
	GetExitCodeProcess( procs[slot].process, &c );
	*code = (int)c;
	OzEd_Close( slot );
	return 0;
#else
	*code = -1;
	return 0;
#endif
}

void OzEd_Stop( int handle ) {
#ifdef _WIN32
	const int slot = handle - 1;

	if ( slot >= 0 && slot < (int)ARRAY_LEN( procs ) && procs[slot].process ) {
		TerminateProcess( procs[slot].process, 1 );
		WaitForSingleObject( procs[slot].process, 2000 );
		OzEd_Close( slot );
	}
#endif
}

void OzEd_Wait( int ms ) {
#ifdef USE_SDL
	IN_PumpEvents();	// the window's messages, so Windows doesn't call it hung
#endif
	Sys_Sleep( ms );
}

int OzEd_Run( const char *exe, const char *args, const char *dir, const char *log ) {
	int code = -1;
	const int h = OzEd_Start( exe, args, dir, log );

	if ( !h ) {
		return -1;
	}
	// a compile can take a while: take the window's messages meanwhile
	while ( OzEd_Poll( h, &code ) ) {
		OzEd_Wait( 50 );
	}
	return code;
}
