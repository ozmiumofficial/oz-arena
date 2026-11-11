/*
Oz: the engine's services for the map editor's C++ (ENGINE_PLAN.md P13, engine/EDITOR.md).

Compiled with the client (its defines and headers), so the editor's C++ only sees ed_engine.h.
*/
#include "../../../quake3e/code/client/client.h"
#include "ed_engine.h"

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
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

static edFighter_t edFighters[16];	// spawn points' fighters and the others' in a session (PE)

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

/*
================
OzEd_AddFlyer: another player of an editing session (PE), where their camera is: their fighter standing
in the air with their eye there, facing their way, the head tilted up or down with their look
================
*/
int OzEd_AddFlyer( const char *model, const char *skin, const float eye[3], float yaw, float pitch ) {
	const edFighter_t *f = OzEd_Fighter( model, skin );
	refEntity_t legs, torso, head;
	vec3_t angles, tilt[3], turned[3];

	if ( !f ) {
		return 0;
	}
	Com_Memset( &legs, 0, sizeof( legs ) );
	legs.reType = RT_MODEL;
	VectorSet( angles, 0, yaw, 0 );
	AnglesToAxis( angles, legs.axis );
	VectorCopy( eye, legs.origin );
	legs.origin[2] -= 26;	// a player's eye is 26 over the origin (DEFAULT_VIEWHEIGHT)
	VectorCopy( legs.origin, legs.oldorigin );
	VectorCopy( legs.origin, legs.lightingOrigin );
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
	VectorSet( angles, Com_Clamp( -60, 60, pitch ), 0, 0 );
	AnglesToAxis( angles, tilt );
	MatrixMultiply( tilt, head.axis, turned );
	AxisCopy( turned, head.axis );
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
	// Oz: the folder OzArena.x86_64 is in, as Windows' module path
	const ssize_t n = readlink( "/proc/self/exe", dir, sizeof( dir ) - 1 );
	char *slash;
	if ( n > 0 ) {
		dir[n] = '\0';
		slash = strrchr( dir, '/' );
		if ( slash ) {
			*slash = '\0';
		}
	} else {
		Q_strncpyz( dir, ".", sizeof( dir ) );
	}
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

int OzEd_RemoveOSFile( const char *path ) {
	return remove( path ) == 0 || !OzEd_FileExists( path );
}

int OzEd_SaveJPG( const char *path, int width, int height, const unsigned char *rgb, int quality ) {
	const size_t cap = (size_t)width * height * 3 + 4096;
	byte *buf;
	size_t len;
	int ok;

	if ( width <= 0 || height <= 0 ) {
		return 0;
	}
	buf = (byte *)Z_Malloc( (int)cap );
	len = CL_SaveJPGToBuffer( buf, cap, quality, width, height, (byte *)rgb, 0 );
	ok = len > 0 && OzEd_WriteOSFile( path, buf, (int)len );
	Z_Free( buf );
	return ok;
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

// The tools die with the game: a job that kills what's in it when its last handle closes, and the game's
// is the only one (it isn't inherited). q3map2 and mbspc ran on for minutes after the game quit or crashed
// in the middle of a compile, a core each. NULL when Windows gives none: the tools then run as before.
static HANDLE OzEd_ToolJob( void ) {
	static HANDLE job;
	static qboolean tried;

	if ( !tried ) {
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
		tried = qtrue;
		job = CreateJobObjectA( NULL, NULL );
		if ( job ) {
			Com_Memset( &info, 0, sizeof( info ) );
			info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			if ( !SetInformationJobObject( job, JobObjectExtendedLimitInformation, &info, sizeof( info ) ) ) {
				CloseHandle( job );
				job = NULL;
			}
		}
	}
	return job;
}
#else
static pid_t procs[4];	// Oz: Linux, the tool run directly with its arguments split as Windows splits a command line
#endif

int OzEd_Start( const char *exe, const char *args, const char *dir, const char *log ) {
#ifdef _WIN32
	static char cmd[32768];	// Windows' longest command line; q3map2's grows with every map pack (-fs_forbiddenpath)
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	SECURITY_ATTRIBUTES sa;
	HANDLE out;
	int slot, n;

	for ( slot = 0; slot < (int)ARRAY_LEN( procs ) && procs[slot].process; slot++ ) {
	}
	if ( slot == (int)ARRAY_LEN( procs ) ) {
		return 0;
	}
	// not Com_sprintf: it formats into 32000 bytes on the stack unchecked, and cut the line at 8191
	// characters with only a console warning (about 200 map packs: every compile and import failed)
	n = snprintf( cmd, sizeof( cmd ), "\"%s\" %s", exe, args );
	if ( n < 0 || n >= (int)sizeof( cmd ) ) {
		Com_Printf( S_COLOR_YELLOW "editor: the tool's command line is too long (%d characters): too many paks in baseq3\n", n );
		return 0;
	}
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
	if ( OzEd_ToolJob() ) {
		AssignProcessToJobObject( OzEd_ToolJob(), pi.hProcess );	// refused (a job of its own already): it runs as before
	}
	CloseHandle( pi.hThread );
	procs[slot].process = pi.hProcess;
	procs[slot].out = out;
	return slot + 1;
#else
	static char cmd[32768];	// Windows' longest command line; q3map2's grows with every map pack (-fs_forbiddenpath)
	static char shell[] = "sh";
	pid_t pid, parent;
	int slot, fd, n, argc, quoted;
	char **argv, *p, *out;

	for ( slot = 0; slot < (int)ARRAY_LEN( procs ) && procs[slot]; slot++ ) {
	}
	if ( slot == (int)ARRAY_LEN( procs ) ) {
		return 0;
	}
	// args is a command line, quoted as Windows' is ("..." keeps blanks in one argument). It's split here
	// and the tool run with the pieces: it used to go through sh -c, where $ and ` in a pak's or a folder's
	// name were the shell's to expand and run.
	n = (int)strlen( args );
	if ( n >= (int)sizeof( cmd ) ) {
		Com_Printf( S_COLOR_YELLOW "editor: the tool's command line is too long (%d characters): too many paks in baseq3\n", n );
		return 0;
	}
	memcpy( cmd, args, n + 1 );
	// an argument is a character and a blank at least: n / 2 + 1 of them, and sh, the tool and the end
	argv = (char **)malloc( sizeof( char * ) * ( n / 2 + 5 ) );
	if ( !argv ) {
		return 0;
	}
	argc = 0;
	argv[argc++] = shell;			// only for a tool that is a script without a #! line (below)
	argv[argc++] = (char *)exe;
	p = out = cmd;	// split in place: an argument never grows (its quotes go)
	for ( ;; ) {
		while ( *p == ' ' || *p == '\t' ) {
			p++;
		}
		if ( !*p ) {
			break;
		}
		argv[argc++] = out;
		quoted = 0;
		while ( *p && ( quoted || ( *p != ' ' && *p != '\t' ) ) ) {
			if ( *p == '"' ) {
				quoted = !quoted;
			} else {
				*out++ = *p;
			}
			p++;
		}
		if ( *p ) {
			p++;	// the blank that ended it: its place may be this argument's end
		}
		*out++ = '\0';
	}
	argv[argc] = NULL;
	OzEd_MakeDirs( log );
	fd = open( log, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644 );
	parent = getpid();
	pid = fork();
	if ( pid < 0 ) {
		if ( fd >= 0 ) {
			close( fd );
		}
		free( argv );
		return 0;
	}
	if ( pid == 0 ) {
#ifdef __linux__
		// the tool dies with the game (it ran on after the game quit or crashed mid-compile); the game gone
		// already, between the fork and here: the same
		prctl( PR_SET_PDEATHSIG, SIGTERM );
		if ( getppid() != parent ) {
			_exit( 127 );
		}
#endif
		if ( fd >= 0 ) {
			dup2( fd, STDOUT_FILENO );
			dup2( fd, STDERR_FILENO );
		}
		if ( dir && *dir && chdir( dir ) != 0 ) {
			_exit( 127 );
		}
		if ( nice( 5 ) == -1 ) {	// below the game, as BELOW_NORMAL_PRIORITY_CLASS
		}
		execv( exe, argv + 1 );
		if ( errno == ENOEXEC ) {
			execv( "/bin/sh", argv );	// a script without a #! line: sh <tool> <arguments>, nothing for sh to expand
		}
		_exit( 127 );
	}
	if ( fd >= 0 ) {
		close( fd );
	}
	free( argv );
	procs[slot] = pid;
	return slot + 1;
#endif
}

static void OzEd_Close( int slot ) {
#ifdef _WIN32
	CloseHandle( procs[slot].process );
	if ( procs[slot].out != INVALID_HANDLE_VALUE ) {
		CloseHandle( procs[slot].out );
	}
	Com_Memset( &procs[slot], 0, sizeof( procs[slot] ) );
#else
	procs[slot] = 0;
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
	const int slot = handle - 1;
	int status = 0;
	pid_t r;

	if ( slot < 0 || slot >= (int)ARRAY_LEN( procs ) || !procs[slot] ) {
		*code = -1;
		return 0;
	}
	r = waitpid( procs[slot], &status, WNOHANG );
	if ( r == 0 ) {
		return 1;
	}
	*code = r == procs[slot] && WIFEXITED( status ) ? WEXITSTATUS( status ) : -1;
	OzEd_Close( slot );
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
#else
	const int slot = handle - 1;

	if ( slot >= 0 && slot < (int)ARRAY_LEN( procs ) && procs[slot] ) {
		int i, status;
		kill( procs[slot], SIGTERM );
		for ( i = 0; i < 40 && waitpid( procs[slot], &status, WNOHANG ) == 0; i++ ) {	// up to 2 s, as Windows'
			Sys_Sleep( 50 );
		}
		if ( i == 40 ) {
			kill( procs[slot], SIGKILL );
			waitpid( procs[slot], &status, 0 );
		}
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

/*
==============================================================================

EDITING TOGETHER (ENGINE_PLAN.md PE): ed_net.h over oz_ice.c and oz_edlink.c

==============================================================================
*/
#ifdef USE_OZ_EDLINK
#include "../online/oz_edlink.h"
#include "../online/oz_online.h"
void OzIce_EditPresence( const char *join, const char *detail );
#endif
#include "ed_net.h"
#include <stdlib.h>

int OzEdNet_Available( void ) {
#ifdef USE_OZ_EDLINK
	return 1;
#else
	return 0;
#endif
}

int OzEdNet_Online( void ) {
#ifdef USE_OZ_EDLINK
	return OzOnline_Status( NULL, 0 ) == OZ_ONLINE_ONLINE;
#else
	return 0;
#endif
}

const char *OzEdNet_Me( void ) {
#ifdef USE_OZ_EDLINK
	return OzOnline_Online() ? OzIdentity_PlayerId() : "";
#else
	return "";
#endif
}

const char *OzEdNet_Name( void ) {
	return Cvar_VariableString( "name" );
}

const char *OzEdNet_Model( void ) {
	const char *m = Cvar_VariableString( "model" );
	return m[0] ? m : "sarge/default";
}

const char *OzEdNet_Version( void ) {
#ifdef USE_OZ_EDLINK
	return OzIce_EditVersion();
#else
	return "";
#endif
}

void OzEdNet_Host( const char *name, const char *map, int players, int max, const char *password ) {
#ifdef USE_OZ_EDLINK
	OzIce_EditHost( name, map, players, max, password );
#endif
}

void OzEdNet_Unhost( void ) {
#ifdef USE_OZ_EDLINK
	OzIce_EditUnhost();
	OzEdLink_Clear();
#endif
}

void OzEdNet_Join( const char *host, const char *password ) {
#ifdef USE_OZ_EDLINK
	OzIce_EditJoin( host, password );
#endif
}

void OzEdNet_Leave( const char *pid ) {
#ifdef USE_OZ_EDLINK
	OzIce_EditLeave( pid );
#endif
}

void OzEdNet_Browse( void ) {
#ifdef USE_OZ_EDLINK
	OzIce_EditBrowse();
#endif
}

void OzEdNet_Presence( const char *join, const char *detail ) {
#ifdef USE_OZ_EDLINK
	OzIce_EditPresence( join, detail );
#endif
}

void OzEdNet_Send( const char *pid, const void *data, int len, int fast ) {
#ifdef USE_OZ_EDLINK
	OzEdLink_Send( pid, data, len, fast );
#endif
}

int OzEdNet_Queued( const char *pid ) {
#ifdef USE_OZ_EDLINK
	return OzEdLink_Queued( pid );
#else
	return 0;
#endif
}

int OzEdNet_Relayed( const char *pid ) {
#ifdef USE_OZ_EDLINK
	return OzIce_EditRelayed( pid );
#else
	return 0;
#endif
}

int OzEdNet_Poll( char *pid, int pidSize, void **data, int *len, int *fast ) {
#ifdef USE_OZ_EDLINK
	return OzEdLink_Poll( pid, pidSize, data, len, fast );
#else
	return 0;
#endif
}

void OzEdNet_Free( void *p ) {
	free( p );
}

int OzEdNet_Milliseconds( void ) {
	return Sys_Milliseconds();
}

void OzEdNet_Command( const char *text ) {
	Cbuf_ExecuteText( EXEC_INSERT, text );
}

int OzEdNet_ClientState( void ) {
	return cls.state == CA_DISCONNECTED ? 0 : cls.state == CA_ACTIVE ? 2 : 1;
}

int OzEdNet_ServerRunning( void ) {
	return com_sv_running && com_sv_running->integer;
}

const char *OzEdNet_MapName( void ) {
	// a match joined: its server's map (a joiner's own mapname cvar is the server's, never set here)
	if ( cls.state >= CA_PRIMED && cl.gameState.dataCount ) {
		static char map[MAX_QPATH];
		Q_strncpyz( map, Info_ValueForKey( cl.gameState.stringData + cl.gameState.stringOffsets[CS_SERVERINFO], "mapname" ), sizeof( map ) );
		return map;
	}
	return Cvar_VariableString( "mapname" );
}

void OzEdNet_SetCvar( const char *name, const char *value ) {
	Cvar_Set( name, value );
}

int OzEdNet_PlayerPos( float origin[3], float angles[3] ) {
	if ( cls.state != CA_ACTIVE ) {
		return 0;
	}
	VectorCopy( cl.snap.ps.origin, origin );
	VectorCopy( cl.viewangles, angles );
	return 1;
}

void OzEdNet_ClearError( void ) {
	Cvar_Set( "com_errorMessage", "" );
}

int OzEdNet_GameType( void ) {
	if ( cls.state != CA_ACTIVE || !cl.gameState.dataCount ) {
		return -1;
	}
	return atoi( Info_ValueForKey( cl.gameState.stringData + cl.gameState.stringOffsets[CS_SERVERINFO], "g_gametype" ) );
}

int OzEdNet_Spectating( void ) {
	// baseq3's PERS_TEAM (3) and TEAM_SPECTATOR (3), bg_public.h: Oz's game and every mod's keep them
	return cls.state == CA_ACTIVE && cl.snap.valid && cl.snap.ps.persistant[3] == 3;
}
