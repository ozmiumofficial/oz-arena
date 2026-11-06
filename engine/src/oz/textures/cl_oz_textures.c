/*
===========================================================================
Oz Arena: custom textures, a folder players drop images into

<user folder>/custom_textures holds images named after the game's textures (wall01.png for
textures/base_wall/wall01.tga). When a renderer loads an image it asks here first (refimport
OzCustomTexture); a file whose name, without its extension, is the texture's last part answers,
whatever either one's format. Subfolders narrow it: textures/base_wall/wall01.png, or a pack's
own folder above that (MyPack/textures/base_wall/wall01.png), takes only that texture. The file is
read as it is from the folder (files.c FS_ReadFile, fs_ozCustomPath), so nothing in any pak changes
and taking the file out puts the game's own texture back.

The folder is read again each time the renderer starts (every map load and vid_restart), and only
while the file system isn't pure: a pure server allows only its own paks, as Oz's own asset paks
are left out there (decision 16), so a see-through wall can't be had where the server checks.
cl_ozCustomTextures 0 turns it off; oz_textures [list|reload|open].
===========================================================================
*/

#include "../../../quake3e/code/client/client.h"

#define OZ_TEX_FOLDER	"custom_textures"
#define OZ_TEX_VPATH	"$oztex/"		// the name a renderer loads one by: "$oztex/<index>.<ext>"
#define OZ_TEX_MAX		8192
#define OZ_TEX_DEPTH	8

static const char *const ozTexExts[] = { "png", "tga", "jpg", "jpeg", "bmp", "pcx", "dds" };

typedef struct {
	char	rel[MAX_QPATH * 2];		// its path in the folder: lowercase, '/', no extension
	char	ext[8];					// lowercase, no dot
	char	*os;					// the file on disk
	int		used;					// images it gave since the last scan
} ozTex_t;

qboolean IN_OpenURL( const char *url );	// sdl_input.c

static ozTex_t	*ozTexs;
static int		ozNumTexs;
static char		ozTexDir[MAX_OSPATH];
static cvar_t	*cl_ozCustomTextures;

static const char *Leaf( const char *path ) {
	const char *s = strrchr( path, '/' );
	return s ? s + 1 : path;
}

static qboolean KnownExt( const char *ext ) {
	int i;
	for ( i = 0; i < ARRAY_LEN( ozTexExts ); i++ ) {
		if ( !Q_stricmp( ext, ozTexExts[i] ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

static void ScanDir( const char *dir, const char *rel, int depth ) {
	char	**list;
	int		n, i;

	list = Sys_ListFiles( dir, "", NULL, &n, qfalse );
	for ( i = 0; list && i < n && ozNumTexs < OZ_TEX_MAX; i++ ) {
		const char	*f = list[i];
		const char	*dot = strrchr( f, '.' );
		ozTex_t		*t;
		char		stem[MAX_QPATH * 2];
		int			len;

		if ( f[0] == '.' || !dot || !KnownExt( dot + 1 ) ) {
			continue;
		}
		len = dot - f;
		if ( len <= 0 || len >= (int)sizeof( stem ) ) {
			continue;
		}
		Q_strncpyz( stem, f, len + 1 );
		t = &ozTexs[ozNumTexs];
		if ( *rel ) {
			Com_sprintf( t->rel, sizeof( t->rel ), "%s/%s", rel, stem );
		} else {
			Q_strncpyz( t->rel, stem, sizeof( t->rel ) );
		}
		Q_strlwr( t->rel );
		Q_strncpyz( t->ext, dot + 1, sizeof( t->ext ) );
		Q_strlwr( t->ext );
		t->os = CopyString( va( "%s%c%s", dir, PATH_SEP, f ) );
		t->used = 0;
		ozNumTexs++;
	}
	Sys_FreeFileList( list );

	if ( depth <= 0 ) {
		return;
	}
	list = Sys_ListFiles( dir, "/", NULL, &n, qfalse );
	for ( i = 0; list && i < n; i++ ) {
		char sub[MAX_OSPATH], subRel[MAX_QPATH * 2];
		if ( list[i][0] == '.' ) {			// ".", "..", and folders put aside with a leading dot
			continue;
		}
		Com_sprintf( sub, sizeof( sub ), "%s%c%s", dir, PATH_SEP, list[i] );
		if ( *rel ) {
			Com_sprintf( subRel, sizeof( subRel ), "%s/%s", rel, list[i] );
		} else {
			Q_strncpyz( subRel, list[i], sizeof( subRel ) );
		}
		ScanDir( sub, subRel, depth - 1 );
	}
	Sys_FreeFileList( list );
}

/*
================
OzTex_Scan: the folder read again (CL_InitRenderer, before any image loads)
================
*/
void OzTex_Scan( void ) {
	int i;

	for ( i = 0; i < ozNumTexs; i++ ) {
		Z_Free( ozTexs[i].os );
	}
	ozNumTexs = 0;
	if ( !ozTexs ) {
		ozTexs = Z_Malloc( OZ_TEX_MAX * sizeof( ozTexs[0] ) );
	}
	if ( !cl_ozCustomTextures || !cl_ozCustomTextures->integer || !ozTexDir[0] ) {
		return;
	}
	ScanDir( ozTexDir, "", OZ_TEX_DEPTH );
	if ( ozNumTexs ) {
		Com_Printf( "Custom textures: %d in %s%s\n", ozNumTexs, ozTexDir,
			FS_OzPure() ? " (off on this pure server)" : "" );
	}
}

/*
================
OzTex_Find (refimport OzCustomTexture)

name: what a renderer is loading ("textures/base_wall/wall01.tga", or without an extension);
exts: the formats it reads, in its order of preference ("png tga jpg jpeg pcx bmp"). Returns the
name to load the custom file by, or NULL. A file whose folder path and the texture's path end the
same way answers; the one sharing the longest stretch of path wins, then the format the renderer
prefers.
================
*/
const char *OzTex_Find( const char *name, const char *exts ) {
	static char	out[2][MAX_QPATH];
	static int	which;
	char		stem[MAX_QPATH * 2];
	const char	*leaf;
	int			i, best = -1, bestScore = -1, bestRank = 0, stemLen;

	if ( !ozNumTexs || !cl_ozCustomTextures->integer || !name || !*name || FS_OzPure() ||
		 !Q_strncmp( name, OZ_TEX_VPATH, sizeof( OZ_TEX_VPATH ) - 1 ) ) {
		return NULL;
	}
	COM_StripExtension( name, stem, sizeof( stem ) );
	for ( i = 0; stem[i]; i++ ) {
		stem[i] = stem[i] == '\\' ? '/' : tolower( (unsigned char)stem[i] );
	}
	leaf = Leaf( stem );
	stemLen = strlen( stem );

	for ( i = 0; i < ozNumTexs; i++ ) {
		const ozTex_t	*t = &ozTexs[i];
		const char		*hit;
		int				relLen, score, rank;

		if ( strcmp( Leaf( t->rel ), leaf ) ) {
			continue;
		}
		// the shorter path must be the end of the longer one, at a folder's start
		relLen = strlen( t->rel );
		if ( relLen <= stemLen ) {
			if ( relLen < stemLen && stem[stemLen - relLen - 1] != '/' ) {
				continue;
			}
			if ( strcmp( stem + stemLen - relLen, t->rel ) ) {
				continue;
			}
			score = relLen;
		} else {
			if ( t->rel[relLen - stemLen - 1] != '/' || strcmp( t->rel + relLen - stemLen, stem ) ) {
				continue;
			}
			score = stemLen;
		}
		hit = Q_stristr( exts, t->ext );		// a format this renderer reads, and its place in the list
		while ( hit && ( ( hit > exts && hit[-1] != ' ' ) || ( hit[strlen( t->ext )] && hit[strlen( t->ext )] != ' ' ) ) ) {
			hit = Q_stristr( hit + 1, t->ext );
		}
		if ( !hit ) {
			continue;
		}
		rank = hit - exts;
		if ( score > bestScore || ( score == bestScore && rank < bestRank ) ) {
			best = i;
			bestScore = score;
			bestRank = rank;
		}
	}
	if ( best < 0 ) {
		return NULL;
	}
	ozTexs[best].used++;
	if ( com_developer && com_developer->integer ) {
		Com_Printf( "Custom texture: %s from %s.%s\n", name, ozTexs[best].rel, ozTexs[best].ext );
	}
	which ^= 1;
	Com_sprintf( out[which], sizeof( out[which] ), OZ_TEX_VPATH "%d.%s", best, ozTexs[best].ext );
	return out[which];
}

/*
================
OzTex_Path (files.c fs_ozCustomPath): the file on disk behind a "$oztex/<index>.<ext>" name, or NULL
================
*/
static const char *OzTex_Path( const char *qpath ) {
	int i;
	if ( Q_strncmp( qpath, OZ_TEX_VPATH, sizeof( OZ_TEX_VPATH ) - 1 ) ) {
		return NULL;
	}
	i = atoi( qpath + sizeof( OZ_TEX_VPATH ) - 1 );
	return i >= 0 && i < ozNumTexs ? ozTexs[i].os : NULL;
}

static const char *const ozTexReadme =
	"Oz Arena custom textures\r\n"
	"========================\r\n"
	"\r\n"
	"Drop images here to replace the game's textures. Name each one after the texture it replaces:\r\n"
	"wall01.png replaces textures/base_wall/wall01, whatever format the game keeps it in.\r\n"
	"\r\n"
	"- Formats: .png, .tga, .jpg, .bmp, .pcx; .dds too with the Oz renderer.\r\n"
	"- Subfolders narrow it when two textures share a name: textures/base_wall/wall01.png, or a\r\n"
	"  pack's own folder above that (MyPack/textures/base_wall/wall01.png), replaces only that one.\r\n"
	"- The game reads this folder at every map load. To see a change at once, type oz_textures reload\r\n"
	"  in the console. Take a file out to get the game's own texture back.\r\n"
	"- A folder whose name starts with a dot is skipped: put a pack aside that way.\r\n"
	"- Oz's remastered textures have extra maps beside them: name_n (normal) and name_s (specular).\r\n"
	"  Drop those too to replace them, or the game keeps Oz's with your new color.\r\n"
	"- Custom textures are off on servers that only allow their own files (pure servers), and when\r\n"
	"  Settings > Graphics > Custom textures is off.\r\n"
	"- oz_textures in the console lists what's here and what each file replaced.\r\n";

static void OzTex_Cmd( void ) {
	const char	*arg = Cmd_Argv( 1 );
	int			i, used = 0;

	if ( !Q_stricmp( arg, "reload" ) ) {
		Cbuf_ExecuteText( EXEC_APPEND, "vid_restart\n" );	// the renderer starts again: the folder is read, every image loaded again
		return;
	}
	if ( !Q_stricmp( arg, "open" ) ) {
		char url[MAX_OSPATH + 8];
		Com_sprintf( url, sizeof( url ), "file:///%s", ozTexDir );
		for ( i = 0; url[i]; i++ ) {
			if ( url[i] == '\\' ) {
				url[i] = '/';
			}
		}
		IN_OpenURL( url );
		return;
	}
	Com_Printf( "Custom textures (%s): %s\n", ozTexDir,
		!cl_ozCustomTextures->integer ? "off (cl_ozCustomTextures 0)" : FS_OzPure() ? "off on this pure server" : "on" );
	for ( i = 0; i < ozNumTexs; i++ ) {
		Com_Printf( "  %s.%s%s\n", ozTexs[i].rel, ozTexs[i].ext,
			ozTexs[i].used ? va( "  (%d image%s)", ozTexs[i].used, ozTexs[i].used == 1 ? "" : "s" ) : "  (unused)" );
		used += ozTexs[i].used ? 1 : 0;
	}
	Com_Printf( "%d file%s, %d in use. oz_textures reload reads the folder again; oz_textures open shows it.\n",
		ozNumTexs, ozNumTexs == 1 ? "" : "s", used );
}

/*
================
OzTex_Init: at CL_Init. The folder is made, with a README, so players find where to drop.
================
*/
void OzTex_Init( void ) {
	char	readme[MAX_OSPATH];
	FILE	*f;

	cl_ozCustomTextures = Cvar_Get( "cl_ozCustomTextures", "1", CVAR_ARCHIVE );
	Cvar_SetDescription( cl_ozCustomTextures, "Images in <user folder>/custom_textures replace the game's textures of the same name (not on pure servers)." );
	Com_sprintf( ozTexDir, sizeof( ozTexDir ), "%s%c%s", FS_GetHomePath(), PATH_SEP, OZ_TEX_FOLDER );
	Sys_Mkdir( ozTexDir );
	Com_sprintf( readme, sizeof( readme ), "%s%cREADME.txt", ozTexDir, PATH_SEP );
	if ( ( f = Sys_FOpen( readme, "rb" ) ) != NULL ) {
		fclose( f );
	} else if ( ( f = Sys_FOpen( readme, "wb" ) ) != NULL ) {
		fwrite( ozTexReadme, 1, strlen( ozTexReadme ), f );
		fclose( f );
	}
	fs_ozCustomPath = OzTex_Path;
	Cmd_AddCommand( "oz_textures", OzTex_Cmd );
}
