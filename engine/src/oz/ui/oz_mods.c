/*
===========================================================================
Oz Arena: the mods a player brings (ENGINE_PLAN.md decision 24, PH step 2). GPL-2.0.

Oz never provides CPMA or Defrag. A player imports one they got themselves, a zip or a folder, into
the mod's own folder in the user folder (<fs_homepath>/<mod>), where the file system finds it as it
finds a mod in the install. What's taken: the folder in the zip, or under the folder chosen, that
holds the mod's paks (the one named as the mod where there's one: CPMA's zip holds cpma/, Defrag's
bundle defrag/defrag/), with everything under it but programs (.exe, .dll and the like: Oz runs a
mod's QVMs, never its programs) and paths that would climb out of the folder. A copy in the install
that the file system doesn't find (the Defrag bundle nests its mod in defrag/defrag) is imported the
same way. The picker is Windows' own open dialog; a zip is read with Quake3e's unzip, from a copy
in the user folder (unzOpen takes a path in the system's code page, a download folder may not fit it).

The Mod Library page (oz_ui_mods.cpp) lists what OzMods_List finds: every mod folder with paks in the
user folder, the base path and the Steam install (as the file system's $modlist does, plus the
nested copies), each with its version (the version in description.txt's first line, else the digits in
the pak named for it), where it is, whether it's ready (a mod folder with paks plays: what it lacks of
its own game code comes from baseq3; CPMA and Defrag need theirs, which Skirmish runs), and whether it's
one Oz supports (decision 14: CPMA and Defrag; the rest play
with no promise). A zip or folder dropped on the window (SDL_DROPFILE, sdl_input.c) comes to
OzMods_Dropped, which guesses the mod from the paks' folder or names and hands it to the page.
Remove deletes an imported mod's folder from the user folder, never one in the install. The known
mods table names each mod and the engine extensions it uses, and never holds a download link.

  oz_mod_import <mod> <zip or folder>	imports one, as the Server Browser's Import does
  oz_mod_remove <mod>					removes an imported one
  oz_mods								prints what OzMods_List finds
===========================================================================
*/
#include "../../../quake3e/code/client/client.h"
#include "../../../quake3e/code/qcommon/unzip.h"

#ifdef _WIN32
// the shell's file dialog (IFileOpenDialog) is Vista's: the engine's build asks for older Windows headers
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#undef NTDDI_VERSION
#define NTDDI_VERSION 0x06010000
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <shobjidl.h>
#pragma comment( lib, "ole32.lib" )
#pragma comment( lib, "uuid.lib" )
#pragma comment( lib, "shell32.lib" )
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#define MAX_IMPORT_FILES	4096
#define IMPORT_LIST			"imported_mods.txt"	// in <fs_homepath>/oz: the mods OzMods_Import brought, a folder name a line

static void ForgetReady( void );	// what OzMods_Ready found in the folders' paks: looked into again

typedef struct {
	char	path[MAX_OSPATH];	// in the zip, or under the folder chosen ('/' between folders)
} importFile_t;

static importFile_t	files[MAX_IMPORT_FILES];
static int			numFiles;

static qboolean Ext( const char *path, const char *ext ) {
	const size_t n = strlen( path ), e = strlen( ext );
	return n > e && !Q_stricmp( path + n - e, ext );
}

// a program, or a path that would climb out of the mod's folder: never taken
static qboolean Refused( const char *rel ) {
	static const char *programs[] = { ".exe", ".dll", ".bat", ".cmd", ".com", ".scr", ".ps1", ".vbs", ".msi", ".so", ".dylib", ".sh" };
	int i;

	if ( !*rel || rel[0] == '/' || rel[0] == '\\' || strchr( rel, ':' ) || strstr( rel, ".." ) ) {
		return qtrue;
	}
	for ( i = 0; i < ARRAY_LEN( programs ); i++ ) {
		if ( Ext( rel, programs[i] ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

// the game's own folders and the user folder's: never a mod's to import into, to list or to remove
static const char *const ownFolders[] = { "baseq3", "oz", "profile", "online", "replays", "demos", "screenshots", "maps", "cache", "pb",
	"editor", "custom_textures" };

static qboolean OwnFolder( const char *name ) {
	int i;
	for ( i = 0; i < ARRAY_LEN( ownFolders ); i++ ) {
		if ( !Q_stricmp( name, ownFolders[i] ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
The folder (in the zip, or under the one chosen) the mod is in: of the folders holding paks, the one
named as the mod, the deepest of those; else the one with the most paks. "" is the top; NULL, none.
*/
static const char *ModBase( const char *mod ) {
	static char best[MAX_OSPATH];
	int i, bestPaks = 0, bestNamed = 0;

	best[0] = '\0';
	for ( i = 0; i < numFiles; i++ ) {
		char dir[MAX_OSPATH], *slash;
		int j, paks = 0, named, depth;
		const char *last;

		if ( !Ext( files[i].path, ".pk3" ) ) {
			continue;
		}
		Q_strncpyz( dir, files[i].path, sizeof( dir ) );
		slash = strrchr( dir, '/' );
		*( slash ? slash + 1 : dir ) = '\0';	// "cpma/" or ""
		for ( j = 0; j < numFiles; j++ ) {
			const char *rest = files[j].path + strlen( dir );
			paks += !Q_stricmpn( files[j].path, dir, strlen( dir ) ) && !strchr( rest, '/' ) && Ext( rest, ".pk3" );
		}
		last = dir;
		for ( depth = 0, j = 0; dir[j]; j++ ) {
			if ( dir[j] == '/' && dir[j + 1] ) {
				last = dir + j + 1;
			}
			depth += dir[j] == '/';
		}
		named = *dir && !Q_stricmpn( last, mod, strlen( mod ) ) && last[strlen( mod )] == '/' ? 1 + depth : 0;
		if ( !bestPaks || named > bestNamed || ( named == bestNamed && paks > bestPaks ) ) {
			Q_strncpyz( best, dir, sizeof( best ) );
			bestPaks = paks;
			bestNamed = named;
		}
	}
	return bestPaks ? best : NULL;
}

static int writeFailed;	// files of this import that couldn't be written

// A file into the mod's folder. One that can't be written whole (a full disk) is deleted and counted:
// the import used to call a cut-off pak imported.
static qboolean WriteOut( const char *mod, const char *rel, const void *data, int len ) {
	char ospath[MAX_OSPATH];
	qboolean ok;
	FILE *f;

	Q_strncpyz( ospath, FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), mod, rel ), sizeof( ospath ) );
	FS_CreatePath( ospath );
	f = Sys_FOpen( ospath, "wb" );
	if ( !f ) {
		writeFailed++;
		return qfalse;
	}
	ok = len <= 0 || fwrite( data, 1, len, f ) == (size_t)len;
	if ( fclose( f ) != 0 ) {
		ok = qfalse;
	}
	if ( !ok ) {
		remove( ospath );
		writeFailed++;
	}
	return ok;
}

// every file of an open zip under base (the mod's folder in it) into the mod's folder: how many
static int UnzipInto( unzFile z, const char *base, const char *mod ) {
	int i, n = 0;

	for ( i = unzGoToFirstFile( z ); i == UNZ_OK; i = unzGoToNextFile( z ) ) {
		unz_file_info info;
		char name[MAX_OSPATH], *c;
		const char *rel;
		void *data;
		unzGetCurrentFileInfo( z, &info, name, sizeof( name ), NULL, 0, NULL, 0 );
		for ( c = name; *c; c++ ) {
			*c = *c == '\\' ? '/' : *c;
		}
		rel = name + strlen( base );
		if ( !name[0] || name[strlen( name ) - 1] == '/' || Q_stricmpn( name, base, strlen( base ) ) || Refused( rel ) ) {
			continue;
		}
		data = Z_Malloc( info.uncompressed_size > 0 ? info.uncompressed_size : 1 );
		if ( unzOpenCurrentFile( z ) == UNZ_OK ) {
			const int len = unzReadCurrentFile( z, data, info.uncompressed_size );
			unzCloseCurrentFile( z );
			if ( len == (int)info.uncompressed_size && WriteOut( mod, rel, data, len ) ) {
				n++;
			}
		}
		Z_Free( data );
	}
	return n;
}

// the zip's files listed (folders' own entries left out)
static void ListZip( unzFile z ) {
	int i;

	numFiles = 0;
	for ( i = unzGoToFirstFile( z ); i == UNZ_OK && numFiles < MAX_IMPORT_FILES; i = unzGoToNextFile( z ) ) {
		unz_file_info info;
		char name[MAX_OSPATH], *c;
		unzGetCurrentFileInfo( z, &info, name, sizeof( name ), NULL, 0, NULL, 0 );
		for ( c = name; *c; c++ ) {
			*c = *c == '\\' ? '/' : *c;
		}
		if ( name[0] && name[strlen( name ) - 1] != '/' ) {
			Q_strncpyz( files[numFiles++].path, name, sizeof( files[0].path ) );
		}
	}
}

// a file read whole into the mod's folder as rel
static qboolean CopyInto( FILE *f, const char *mod, const char *rel ) {
	qboolean ok;
	void *data;
	long len;

	fseek( f, 0, SEEK_END );
	len = ftell( f );
	fseek( f, 0, SEEK_SET );
	data = Z_Malloc( len > 0 ? len : 1 );
	ok = fread( data, 1, len, f ) == (size_t)len && WriteOut( mod, rel, data, (int)len );
	Z_Free( data );
	return ok;
}

#ifdef _WIN32
static void Wide( const char *utf8, wchar_t *out, int size ) {
	if ( !MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, out, size ) ) {
		MultiByteToWideChar( CP_ACP, 0, utf8, -1, out, size );
	}
}

// every file under a folder, its path below it with '/'
static void ListFolder( const wchar_t *root, const char *below ) {
	wchar_t pattern[MAX_OSPATH * 2];
	WIN32_FIND_DATAW fd;
	HANDLE h;

	swprintf( pattern, ARRAY_LEN( pattern ), L"%ls\\*", root );
	h = FindFirstFileW( pattern, &fd );
	if ( h == INVALID_HANDLE_VALUE ) {
		return;
	}
	do {
		char name[MAX_OSPATH], rel[MAX_OSPATH];
		if ( !wcscmp( fd.cFileName, L"." ) || !wcscmp( fd.cFileName, L".." ) || numFiles >= MAX_IMPORT_FILES ) {
			continue;
		}
		WideCharToMultiByte( CP_UTF8, 0, fd.cFileName, -1, name, sizeof( name ), NULL, NULL );
		Com_sprintf( rel, sizeof( rel ), "%s%s", below, name );
		if ( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) {
			wchar_t sub[MAX_OSPATH * 2];
			swprintf( sub, ARRAY_LEN( sub ), L"%ls\\%ls", root, fd.cFileName );
			Q_strcat( rel, sizeof( rel ), "/" );
			ListFolder( sub, rel );
		} else {
			Q_strncpyz( files[numFiles++].path, rel, sizeof( files[0].path ) );
		}
	} while ( FindNextFileW( h, &fd ) );
	FindClose( h );
}

static int ImportFolder( const char *source, const char *mod, char *message, int size ) {
	wchar_t root[MAX_OSPATH * 2];
	const char *base;
	int i, n = 0;

	Wide( source, root, ARRAY_LEN( root ) );
	numFiles = 0;
	ListFolder( root, "" );
	if ( !( base = ModBase( mod ) ) ) {
		Com_sprintf( message, size, "There are no paks in that folder." );
		return -1;
	}
	for ( i = 0; i < numFiles; i++ ) {
		const char *rel = files[i].path + strlen( base );
		wchar_t from[MAX_OSPATH * 2], relw[MAX_OSPATH];
		FILE *f;
		if ( Q_stricmpn( files[i].path, base, strlen( base ) ) || Refused( rel ) ) {
			continue;
		}
		Wide( files[i].path, relw, ARRAY_LEN( relw ) );
		swprintf( from, ARRAY_LEN( from ), L"%ls\\%ls", root, relw );
		f = _wfopen( from, L"rb" );
		if ( !f ) {
			continue;
		}
		n += CopyInto( f, mod, rel );
		fclose( f );
	}
	return n;
}

// a zip opened from a copy where unzOpen can open it (the user folder's path fits the system's code page; a
// download folder may not), its files listed; NULL when it isn't a zip. The copy goes with unzClose.
static unzFile OpenZip( const char *source, const char *mod, wchar_t *to, int toSize ) {
	wchar_t from[MAX_OSPATH * 2];
	char direct[MAX_OSPATH];
	BOOL lossy = FALSE;
	const char *copy;
	unzFile z;

	to[0] = 0;		// no copy made yet (the callers delete it when there is one)
	Wide( source, from, ARRAY_LEN( from ) );
	// A path that fits the system's code page is opened where it is. Every zip used to be copied whole into
	// the user folder, once for the guess and again for the import.
	if ( WideCharToMultiByte( CP_ACP, WC_NO_BEST_FIT_CHARS, from, -1, direct, sizeof( direct ), NULL, &lossy ) > 0 && !lossy ) {
		if ( !( z = unzOpen( direct ) ) ) {
			return NULL;
		}
		ListZip( z );
		return z;
	}
	copy = FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), mod, "import.zip.tmp" );
	FS_CreatePath( copy );
	MultiByteToWideChar( CP_ACP, 0, copy, -1, to, toSize );
	if ( !CopyFileW( from, to, FALSE ) || !( z = unzOpen( copy ) ) ) {
		DeleteFileW( to );
		to[0] = 0;
		return NULL;
	}
	ListZip( z );
	return z;
}

static int ImportZip( const char *source, const char *mod, char *message, int size ) {
	wchar_t to[MAX_OSPATH * 2];
	const char *base;
	unzFile z;
	int n;

	if ( !( z = OpenZip( source, mod, to, ARRAY_LEN( to ) ) ) ) {
		Com_sprintf( message, size, "That file couldn't be read as a zip." );
		return -1;
	}
	if ( !( base = ModBase( mod ) ) ) {
		Com_sprintf( message, size, "There are no paks in that zip." );
		unzClose( z );
		if ( to[0] ) {
			DeleteFileW( to );
		}
		return -1;
	}
	n = UnzipInto( z, base, mod );
	unzClose( z );
	if ( to[0] ) {
		DeleteFileW( to );
	}
	return n;
}
#else
// Oz: Linux's: paths are UTF-8 bytes, so the zip opens where it is
static void ListFolder( const char *root, const char *below ) {
	char dir[MAX_OSPATH * 2];
	struct dirent *e;
	DIR *d;

	Com_sprintf( dir, sizeof( dir ), "%s/%s", root, below );
	if ( !( d = opendir( dir ) ) ) {
		return;
	}
	while ( ( e = readdir( d ) ) != NULL && numFiles < MAX_IMPORT_FILES ) {
		char rel[MAX_OSPATH], full[MAX_OSPATH * 2];
		struct stat st;
		if ( !strcmp( e->d_name, "." ) || !strcmp( e->d_name, ".." ) ) {
			continue;
		}
		Com_sprintf( rel, sizeof( rel ), "%s%s", below, e->d_name );
		Com_sprintf( full, sizeof( full ), "%s/%s", root, rel );
		if ( stat( full, &st ) != 0 ) {
			continue;
		}
		if ( S_ISDIR( st.st_mode ) ) {
			Q_strcat( rel, sizeof( rel ), "/" );
			ListFolder( root, rel );
		} else if ( S_ISREG( st.st_mode ) ) {
			Q_strncpyz( files[numFiles++].path, rel, sizeof( files[0].path ) );
		}
	}
	closedir( d );
}

static int ImportFolder( const char *source, const char *mod, char *message, int size ) {
	const char *base;
	int i, n = 0;

	numFiles = 0;
	ListFolder( source, "" );
	if ( !( base = ModBase( mod ) ) ) {
		Com_sprintf( message, size, "There are no paks in that folder." );
		return -1;
	}
	for ( i = 0; i < numFiles; i++ ) {
		const char *rel = files[i].path + strlen( base );
		FILE *f;
		if ( Q_stricmpn( files[i].path, base, strlen( base ) ) || Refused( rel ) ) {
			continue;
		}
		if ( ( f = fopen( va( "%s/%s", source, files[i].path ), "rb" ) ) != NULL ) {
			n += CopyInto( f, mod, rel );
			fclose( f );
		}
	}
	return n;
}

static int ImportZip( const char *source, const char *mod, char *message, int size ) {
	const char *base;
	unzFile z;
	int n;

	if ( !( z = unzOpen( source ) ) ) {
		Com_sprintf( message, size, "That file couldn't be read as a zip." );
		return -1;
	}
	ListZip( z );
	if ( !( base = ModBase( mod ) ) ) {
		Com_sprintf( message, size, "There are no paks in that zip." );
		unzClose( z );
		return -1;
	}
	n = UnzipInto( z, base, mod );
	unzClose( z );
	return n;
}
#endif


/*
==================
The mods Oz's import brought: their folder names, a line each, in <fs_homepath>/oz/imported_mods.txt.
Remove deletes only those. A mod's folder in the user folder with paks in it isn't always an import:
it may hold only what a server sent, beside the player's settings and demos under a mod in the install.
==================
*/
static void ReadImported( char *buf, int size ) {
	FILE *f = Sys_FOpen( FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), "oz", IMPORT_LIST ), "rb" );
	int n = 0;

	if ( f ) {
		n = (int)fread( buf, 1, size - 1, f );
		fclose( f );
	}
	buf[n > 0 ? n : 0] = '\0';
}

// the next line of the list into line; NULL at its end
static const char *ImportedLine( const char *p, char *line, int size ) {
	const char *e;

	while ( *p == '\r' || *p == '\n' ) {
		p++;
	}
	if ( !*p ) {
		return NULL;
	}
	for ( e = p; *e && *e != '\r' && *e != '\n'; e++ ) {
	}
	Q_strncpyz( line, p, MIN( size, (int)( e - p ) + 1 ) );
	return e;
}

int OzMods_Imported( const char *mod );	// the Mod Library's rows ask too (oz_ui_mods.cpp)

int OzMods_Imported( const char *mod ) {
	char list[4096], line[MAX_QPATH];
	const char *p = list;

	if ( !mod || !*mod ) {
		return 0;
	}
	ReadImported( list, sizeof( list ) );
	while ( ( p = ImportedLine( p, line, sizeof( line ) ) ) != NULL ) {
		if ( !Q_stricmp( line, mod ) ) {
			return 1;
		}
	}
	return 0;
}

// the mod put on the list, or taken off it
static void MarkImported( const char *mod, qboolean on ) {
	char list[4096], out[4096 + MAX_QPATH + 8], line[MAX_QPATH], path[MAX_OSPATH], tmp[MAX_OSPATH];
	const char *p = list;
	qboolean ok;
	size_t len;
	FILE *f;

	ReadImported( list, sizeof( list ) );
	out[0] = '\0';
	while ( ( p = ImportedLine( p, line, sizeof( line ) ) ) != NULL ) {
		if ( Q_stricmp( line, mod ) ) {
			Q_strcat( out, sizeof( out ), line );
			Q_strcat( out, sizeof( out ), "\n" );
		}
	}
	if ( on ) {
		Q_strncpyz( line, mod, sizeof( line ) );
		Q_strcat( out, sizeof( out ), line );
		Q_strcat( out, sizeof( out ), "\n" );
	}
	// written beside the list and moved over it, so a write cut short never loses the list
	Q_strncpyz( path, FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), "oz", IMPORT_LIST ), sizeof( path ) );
	Q_strncpyz( tmp, path, sizeof( tmp ) );
	Q_strcat( tmp, sizeof( tmp ), ".tmp" );
	FS_CreatePath( tmp );
	f = Sys_FOpen( tmp, "wb" );
	if ( !f ) {
		Com_Printf( S_COLOR_YELLOW "oz_mods: couldn't write %s\n", path );
		return;
	}
	len = strlen( out );
	ok = fwrite( out, 1, len, f ) == len;
	if ( fclose( f ) != 0 ) {
		ok = qfalse;
	}
#ifdef _WIN32
	ok = ok && MoveFileExA( tmp, path, MOVEFILE_REPLACE_EXISTING ) != 0;
#else
	ok = ok && rename( tmp, path ) == 0;
#endif
	if ( !ok ) {
		remove( tmp );
		Com_Printf( S_COLOR_YELLOW "oz_mods: couldn't write %s\n", path );
	}
}


/*
==================
OzMods_Import

A mod the player got (a zip, a pak, or a folder) into <fs_homepath>/<mod>: how many files, or -1 with
message saying why
==================
*/
int OzMods_Import( const char *source, const char *mod, char *message, int size ) {
	int n = -1;

	message[0] = '\0';
	writeFailed = 0;
	// never into the game's own folders or the user folder's (profile, replays ...): a mod is its own folder
	if ( !*mod || strchr( mod, '/' ) || strchr( mod, '\\' ) || strstr( mod, ".." ) || FS_InvalidGameDir( mod ) || OwnFolder( mod ) ) {
		Com_sprintf( message, size, "That isn't a mod's folder name." );
		return -1;
	}
#ifdef _WIN32
	{
		wchar_t w[MAX_OSPATH * 2];
		DWORD attr;
		Wide( source, w, ARRAY_LEN( w ) );
		attr = GetFileAttributesW( w );
		if ( attr == INVALID_FILE_ATTRIBUTES ) {
			Com_sprintf( message, size, "That file or folder isn't there." );
		} else if ( attr & FILE_ATTRIBUTE_DIRECTORY ) {
			n = ImportFolder( source, mod, message, size );
		} else if ( Ext( source, ".pk3" ) ) {
			// a single pak: into the mod's folder as it is
			const char *slash = strrchr( source, '\\' ) ? strrchr( source, '\\' ) : strrchr( source, '/' );
			FILE *f = _wfopen( w, L"rb" );
			if ( f ) {
				void *data;
				long len;
				fseek( f, 0, SEEK_END );
				len = ftell( f );
				fseek( f, 0, SEEK_SET );
				data = Z_Malloc( len > 0 ? len : 1 );
				n = fread( data, 1, len, f ) == (size_t)len && WriteOut( mod, slash ? slash + 1 : source, data, (int)len ) ? 1 : -1;
				Z_Free( data );
				fclose( f );
			}
			if ( n < 0 ) {
				Com_sprintf( message, size, "That pak couldn't be copied." );
			}
		} else {
			n = ImportZip( source, mod, message, size );
		}
	}
#else
	{
		struct stat st;
		if ( stat( source, &st ) != 0 ) {
			Com_sprintf( message, size, "That file or folder isn't there." );
		} else if ( S_ISDIR( st.st_mode ) ) {
			n = ImportFolder( source, mod, message, size );
		} else if ( Ext( source, ".pk3" ) ) {
			const char *slash = strrchr( source, '/' );
			FILE *f = fopen( source, "rb" );
			if ( f ) {
				n = CopyInto( f, mod, slash ? slash + 1 : source ) ? 1 : -1;
				fclose( f );
			}
			if ( n < 0 ) {
				Com_sprintf( message, size, "That pak couldn't be copied." );
			}
		} else {
			n = ImportZip( source, mod, message, size );
		}
	}
#endif
	if ( writeFailed > 0 && ( n >= 0 || !message[0] ) ) {
		Com_sprintf( message, size, "%d of its files couldn't be written to your user folder: the import isn't whole. Is the disk full?", writeFailed );
		n = -1;
	}
	if ( n == 0 ) {
		Com_sprintf( message, size, "Nothing in it could be taken." );
		n = -1;
	}
	if ( n > 0 ) {
		MarkImported( mod, qtrue );	// Remove asks for it
		ForgetReady();
		Com_sprintf( message, size, "%d %s into %s.", n, n == 1 ? "file" : "files", mod );
		OzServers_ForgetMods();	// the Server Browser finds it now
	}
	Com_Printf( "oz_mod_import: %s %s\n", mod, n > 0 ? message : va( "failed: %s", message ) );
	return n;
}


/*
==================
OzMods_InInstall

A copy of the mod in the install (the Steam copy or the base path) that the file system doesn't
find: its paks one folder deeper, in <mod>/<mod> (the Defrag bundle's layout)
==================
*/
int OzMods_InInstall( const char *mod, char *path, int size ) {
	const char *roots[] = { Cvar_VariableString( "fs_steampath" ), Cvar_VariableString( "fs_basepath" ) };
	int i;

	for ( i = 0; i < ARRAY_LEN( roots ); i++ ) {
		char dir[MAX_OSPATH];
		char **list;
		int n;
		if ( !*roots[i] ) {
			continue;
		}
		Com_sprintf( dir, sizeof( dir ), "%s%c%s%c%s", roots[i], PATH_SEP, mod, PATH_SEP, mod );
		list = Sys_ListFiles( dir, ".pk3", NULL, &n, qfalse );
		Sys_FreeFileList( list );
		if ( n > 0 ) {
			Q_strncpyz( path, dir, size );
			return 1;
		}
	}
	path[0] = '\0';
	return 0;
}


/*
==================
OzMods_Ready

The mod's game code where a switch to it finds it: a pak with vm/cgame.qvm or vm/qagame.qvm in it
(z-cpma-pak153.pk3, zz-defrag_vm_191.pk3) in its folder of the user folder, the base path or the Steam
install. A pak's name alone said so before: a map pack named defrag_pack1.pk3 in the user folder's
defrag made Defrag ready with no Defrag there. The paks named for the mod are looked into first, in
every folder, then the others; what a folder's paks held is kept until its listing changes.
==================
*/
static qboolean PakHasCode( const char *ospath ) {
	unzFile z = unzOpen( ospath );
	qboolean found;

	if ( !z ) {
		return qfalse;
	}
	found = unzLocateFile( z, "vm/cgame.qvm", 2 ) == UNZ_OK || unzLocateFile( z, "vm/qagame.qvm", 2 ) == UNZ_OK;
	unzClose( z );
	return found;
}

typedef struct {
	char		dir[MAX_OSPATH];
	unsigned	sum;		// of the paks' names
	int			count;
	int			looked;		// 1: the paks named for the mod were looked into, 2: the others too
	qboolean	found;
} readyDir_t;

static readyDir_t	readyDirs[16];
static int			nextReadyDir;

static void ForgetReady( void ) {
	memset( readyDirs, 0, sizeof( readyDirs ) );
	nextReadyDir = 0;
}

static qboolean DirHasCode( const char *dir, const char *mod, qboolean all ) {
	readyDir_t *c = NULL;
	unsigned sum = 0;
	char **list;
	int n, i, pass;
	qboolean found;

	list = Sys_ListFiles( dir, ".pk3", NULL, &n, qfalse );
	if ( !list || n <= 0 ) {
		Sys_FreeFileList( list );
		return qfalse;
	}
	for ( i = 0; i < n; i++ ) {
		const char *p;
		for ( p = list[i]; *p; p++ ) {
			sum = sum * 31 + (byte)*p;
		}
		sum = sum * 31 + '/';
	}
	for ( i = 0; i < ARRAY_LEN( readyDirs ); i++ ) {
		if ( readyDirs[i].dir[0] && !Q_stricmp( readyDirs[i].dir, dir ) ) {
			c = &readyDirs[i];
		}
	}
	if ( !c ) {
		c = &readyDirs[nextReadyDir];
		nextReadyDir = ( nextReadyDir + 1 ) % ARRAY_LEN( readyDirs );
		Q_strncpyz( c->dir, dir, sizeof( c->dir ) );
		c->count = -1;
	}
	if ( c->count != n || c->sum != sum ) {
		c->count = n;
		c->sum = sum;
		c->looked = 0;
		c->found = qfalse;
	}
	for ( pass = c->looked; pass < ( all ? 2 : 1 ) && !c->found; pass++ ) {
		// from the last: a mod's code pak is named to sort after its other paks
		for ( i = n - 1; i >= 0 && !c->found; i-- ) {
			if ( ( Q_stristr( list[i], mod ) != NULL ) == ( pass == 0 ) ) {
				char path[MAX_OSPATH];
				Com_sprintf( path, sizeof( path ), "%s%c%s", dir, PATH_SEP, list[i] );
				c->found = PakHasCode( path );
			}
		}
		c->looked = pass + 1;
	}
	found = c->found;
	Sys_FreeFileList( list );
	return found;
}

int OzMods_Ready( const char *mod ) {
	const char *roots[] = { Cvar_VariableString( "fs_homepath" ), Cvar_VariableString( "fs_basepath" ), Cvar_VariableString( "fs_steampath" ) };
	int i, pass;

	for ( pass = 0; pass < 2; pass++ ) {
		for ( i = 0; i < ARRAY_LEN( roots ); i++ ) {
			char dir[MAX_OSPATH];
			if ( !*roots[i] ) {
				continue;
			}
			Com_sprintf( dir, sizeof( dir ), "%s%c%s", roots[i], PATH_SEP, mod );
			if ( DirHasCode( dir, mod, pass == 1 ) ) {
				return 1;
			}
		}
	}
	return 0;
}


/*
==================
OzMods_Pick

Windows' open dialog over the game: a zip (or a pak), or a folder. 0 when the player cancels it.
==================
*/
int OzMods_Pick( int folder, char *path, int size ) {
#ifdef _WIN32
	IFileOpenDialog *dlg = NULL;
	IShellItem *item = NULL;
	PWSTR chosen = NULL;
	DWORD options;
	int ok = 0;
	const HRESULT init = CoInitializeEx( NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE );

	path[0] = '\0';
	if ( SUCCEEDED( CoCreateInstance( &CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog, (void **)&dlg ) ) ) {
		static const COMDLG_FILTERSPEC types[] = { { L"A mod's zip or pak", L"*.zip;*.pk3" } };
		IFileOpenDialog_GetOptions( dlg, &options );
		IFileOpenDialog_SetOptions( dlg, options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | ( folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST ) );
		if ( !folder ) {
			IFileOpenDialog_SetFileTypes( dlg, ARRAY_LEN( types ), types );
		}
		IFileOpenDialog_SetTitle( dlg, folder ? L"The mod's folder" : L"The mod's zip" );
		if ( SUCCEEDED( IFileOpenDialog_Show( dlg, GetActiveWindow() ) ) && SUCCEEDED( IFileOpenDialog_GetResult( dlg, &item ) ) &&
			SUCCEEDED( IShellItem_GetDisplayName( item, SIGDN_FILESYSPATH, &chosen ) ) ) {
			ok = WideCharToMultiByte( CP_UTF8, 0, chosen, -1, path, size, NULL, NULL ) > 0;
			CoTaskMemFree( chosen );
		}
		if ( item ) {
			IShellItem_Release( item );
		}
		IFileOpenDialog_Release( dlg );
	}
	if ( SUCCEEDED( init ) ) {
		CoUninitialize();
	}
	return ok;
#else
	// Oz: Linux's: the desktop's own file dialog, through zenity (GNOME, most desktops) or kdialog (KDE)
	static const char *pickers[2][2] = {
		{ "zenity --file-selection --title=\"The mod's zip\" --file-filter=\"A mod's zip or pak | *.zip *.pk3\" 2>/dev/null",
		  "kdialog --title \"The mod's zip\" --getopenfilename \"$HOME\" \"*.zip *.pk3\" 2>/dev/null" },
		{ "zenity --file-selection --directory --title=\"The mod's folder\" 2>/dev/null",
		  "kdialog --title \"The mod's folder\" --getexistingdirectory \"$HOME\" 2>/dev/null" } };
	int i;

	path[0] = '\0';
	for ( i = 0; i < 2 && !path[0]; i++ ) {
		const char *tool = i == 0 ? "zenity" : "kdialog";
		FILE *p;
		if ( system( va( "command -v %s >/dev/null 2>&1", tool ) ) != 0 ) {
			continue;
		}
		if ( ( p = popen( pickers[folder ? 1 : 0][i], "r" ) ) == NULL ) {
			continue;
		}
		if ( fgets( path, size, p ) ) {
			path[strcspn( path, "\r\n" )] = '\0';
		}
		pclose( p );
		break;	// the player answered this one, cancel included
	}
	return path[0] != '\0';
#endif
}


/*
==================
The known mods (ENGINE_PLAN.md PH step 2): each one's folder, name and the engine extensions it uses.
Only CPMA and Defrag are supported (decision 14); the rest play with no promise. Oz never names where
to get any of them (decision 24): a player brings their own.
==================
*/
static const ozKnownMod_t knownMods[] = {
	{ "cpma", "CPMA", "Challenge ProMode Arena. Uses CNQ3's extensions: the new demo player and its rewind, the HUD's filters.", 1 },
	{ "defrag", "Defrag", "The run and trick mod. Uses oDFe's extensions: brush views, the run timer, cvar watch.", 1 },
	{ "osp", "OSP", "Orange Smoothie Productions' tournament mod. No engine extensions.", 0 },
	{ "excessiveplus", "Excessive Plus", "Fast-firing weapons and physics. No engine extensions.", 0 },
	{ "missionpack", "Team Arena", "id's expansion: its own maps, modes and models. No engine extensions.", 0 },
	{ "arena", "Rocket Arena 3", "Round-based arenas. No engine extensions.", 0 },
	{ "threewave", "Threewave CTF", "Capture the flag with the grapple. No engine extensions.", 0 },
	{ "freeze", "Freeze Tag", "Frozen players thawed by teammates. No engine extensions.", 0 },
	{ "rat", "Rat Arena", "Rocket Arena with its own rules. No engine extensions.", 0 },
	{ "generations", "Generations Arena", "Every era's fighters and weapons. No engine extensions.", 0 },
	{ "q3ut4", "Urban Terror", "A standalone game on its own engine (ioUrbanTerror): its servers don't take this game.", 0 },
	{ "baseoa", "OpenArena", "A standalone game with its own assets: its servers don't take this game.", 0 },
};

int OzMods_Known( const ozKnownMod_t **out ) {
	*out = knownMods;
	return ARRAY_LEN( knownMods );
}

const ozKnownMod_t *OzMods_KnownMod( const char *dir ) {
	int i;
	for ( i = 0; i < ARRAY_LEN( knownMods ); i++ ) {
		if ( !Q_stricmp( knownMods[i].dir, dir ) ) {
			return &knownMods[i];
		}
	}
	return NULL;
}


// the mod's version: description.txt's first line, else the digits in the pak named for it (z-cpma-pak153.pk3 is 1.53,
// zz-defrag_vm_191.pk3 1.91): "" when neither says
static void Version( const char *dir, const char *mod, char *out, int size ) {
	char **list;
	int n, i, best = 0;
	char path[MAX_OSPATH];
	FILE *f;

	out[0] = '\0';
	Com_sprintf( path, sizeof( path ), "%s%cdescription.txt", dir, PATH_SEP );
	f = Sys_FOpen( path, "rb" );
	if ( f ) {
		char line[128];
		if ( fgets( line, sizeof( line ), f ) ) {
			char *c;
			for ( c = line; *c; c++ ) {
				if ( *c == '\r' || *c == '\n' ) {
					*c = '\0';
					break;
				}
			}
			Q_CleanStr( line );
			// the version in it ("Defrag 1.91.31" is 1.91.31): the first word that starts with a digit
			// (or v and a digit), up to the next space
			for ( c = line; *c; c++ ) {
				const int at = c == line || c[-1] == ' ' || c[-1] == '\t';
				char *v = c + ( ( *c == 'v' || *c == 'V' ) && c[1] >= '0' && c[1] <= '9' );
				if ( at && *v >= '0' && *v <= '9' ) {
					int len = 0;
					while ( v[len] && v[len] != ' ' && v[len] != '\t' && v[len] != ',' && v[len] != ')' ) {
						len++;
					}
					Com_sprintf( out, size, "%.*s", len, v );
					break;
				}
			}
		}
		fclose( f );
		if ( out[0] ) {
			return;
		}
	}
	list = Sys_ListFiles( dir, ".pk3", NULL, &n, qfalse );
	for ( i = 0; i < n; i++ ) {
		const char *p = Q_stristr( list[i], mod ), *ext = strrchr( list[i], '.' );
		int digits = 0, run;
		if ( !p ) {
			continue;
		}
		// the longest digit run after the mod's name, before the extension (zz-mymod.pk3 has none)
		for ( ; *p && p != ext; p++ ) {
			if ( *p >= '0' && *p <= '9' ) {
				run = 0;
				while ( p[run] >= '0' && p[run] <= '9' ) {
					run++;
				}
				if ( run > digits && run <= 6 ) {
					digits = run;
					if ( run == 1 ) {
						Com_sprintf( out, size, "%c", p[0] );
					} else if ( run <= 3 ) {
						Com_sprintf( out, size, "%c.%.*s", p[0], run - 1, p + 1 );
					} else if ( run <= 5 ) {
						Com_sprintf( out, size, "%c.%.2s.%.*s", p[0], p + 1, run - 3, p + 3 );
					} else {
						Com_sprintf( out, size, "%.*s", run, p );
					}
				}
				p += run - 1;
			}
		}
		if ( digits ) {
			best = 1;
		}
	}
	Sys_FreeFileList( list );
	if ( !best ) {
		out[0] = '\0';
	}
}


/*
==================
OzMods_List

Every mod folder with paks in the user folder, the base path and the Steam install, the user folder's
first (an imported copy), each once; then the nested copies the file system doesn't find
(<mod>/<mod>). Never baseq3 or oz (the game's own), nor the user folder's own folders.
==================
*/
int OzMods_List( ozModInfo_t *out, int max ) {
	const char *roots[] = { Cvar_VariableString( "fs_homepath" ), Cvar_VariableString( "fs_basepath" ), Cvar_VariableString( "fs_steampath" ) };
	int r, i, j, n = 0;

	for ( r = 0; r < ARRAY_LEN( roots ) && n < max; r++ ) {
		char **dirs;
		int numDirs;
		if ( !*roots[r] ) {
			continue;
		}
		dirs = Sys_ListFiles( roots[r], "/", NULL, &numDirs, qtrue );
		for ( i = 0; i < numDirs && n < max; i++ ) {
			const char *name = dirs[i];
			char dir[MAX_OSPATH];
			char **paks;
			int numPaks, known = 0;
			if ( !strcmp( name, "." ) || !strcmp( name, ".." ) || FS_InvalidGameDir( name ) ) {
				continue;
			}
			known = OwnFolder( name );
			for ( j = 0; j < n; j++ ) {
				known |= !Q_stricmp( out[j].dir, name );	// listed from a root before this one
			}
			if ( known ) {
				continue;
			}
			Com_sprintf( dir, sizeof( dir ), "%s%c%s", roots[r], PATH_SEP, name );
			paks = Sys_ListFiles( dir, ".pk3", NULL, &numPaks, qfalse );
			Sys_FreeFileList( paks );
			if ( numPaks <= 0 ) {
				continue;
			}
			{
				ozModInfo_t *m = &out[n++];
				const ozKnownMod_t *k = OzMods_KnownMod( name );
				memset( m, 0, sizeof( *m ) );
				Q_strncpyz( m->dir, name, sizeof( m->dir ) );
				Q_strncpyz( m->name, k ? k->name : name, sizeof( m->name ) );
				Q_strncpyz( m->where, dir, sizeof( m->where ) );
				m->imported = r == 0;
				// any mod with paks plays: under it, the game code it doesn't bring comes from baseq3's paks.
				// CPMA and Defrag must bring theirs: Skirmish runs their rules (OzMods_Ready, as its mod picker)
				m->ready = k && k->supported ? OzMods_Ready( name ) : 1;
				m->supported = k ? k->supported : 0;
				m->known = k != NULL;
				m->paks = numPaks;
				Version( dir, name, m->version, sizeof( m->version ) );
			}
		}
		Sys_FreeFileList( dirs );
	}
	// the nested copies (the Defrag bundle's defrag/defrag), for the mods not found whole
	for ( i = 0; i < ARRAY_LEN( knownMods ) && n < max; i++ ) {
		char found[MAX_OSPATH];
		int listed = 0;
		for ( j = 0; j < n; j++ ) {
			listed |= !Q_stricmp( out[j].dir, knownMods[i].dir ) && out[j].ready;
		}
		if ( !listed && OzMods_InInstall( knownMods[i].dir, found, sizeof( found ) ) ) {
			ozModInfo_t *m = NULL;
			for ( j = 0; j < n; j++ ) {
				if ( !Q_stricmp( out[j].dir, knownMods[i].dir ) ) {
					m = &out[j];	// its outer folder has paks of its own (the bundle's readme pak): the nested one is the mod
				}
			}
			if ( !m ) {
				m = &out[n++];
				memset( m, 0, sizeof( *m ) );
				Q_strncpyz( m->dir, knownMods[i].dir, sizeof( m->dir ) );
				Q_strncpyz( m->name, knownMods[i].name, sizeof( m->name ) );
				m->supported = knownMods[i].supported;
				m->known = 1;
			}
			Q_strncpyz( m->where, found, sizeof( m->where ) );
			m->nested = 1;
			m->imported = 0;
			{
				int numPaks;
				char **paks = Sys_ListFiles( found, ".pk3", NULL, &numPaks, qfalse );
				Sys_FreeFileList( paks );
				m->paks = numPaks > 0 ? numPaks : 0;	// the nested copy's own, not the outer folder's
			}
			Version( found, m->dir, m->version, sizeof( m->version ) );
		}
	}
	return n;
}


#ifdef _WIN32
static int RemoveTree( const wchar_t *root ) {
	wchar_t pattern[MAX_OSPATH * 2];
	WIN32_FIND_DATAW fd;
	HANDLE h;
	int n = 0;

	swprintf( pattern, ARRAY_LEN( pattern ), L"%ls\\*", root );
	h = FindFirstFileW( pattern, &fd );
	if ( h != INVALID_HANDLE_VALUE ) {
		do {
			wchar_t sub[MAX_OSPATH * 2];
			if ( !wcscmp( fd.cFileName, L"." ) || !wcscmp( fd.cFileName, L".." ) ) {
				continue;
			}
			swprintf( sub, ARRAY_LEN( sub ), L"%ls\\%ls", root, fd.cFileName );
			if ( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) {
				n += RemoveTree( sub );
			} else {
				SetFileAttributesW( sub, FILE_ATTRIBUTE_NORMAL );
				n += DeleteFileW( sub ) ? 1 : 0;
			}
		} while ( FindNextFileW( h, &fd ) );
		FindClose( h );
	}
	RemoveDirectoryW( root );
	return n;
}
#endif

/*
==================
OzMods_Remove

An imported mod's folder (<fs_homepath>/<mod>) deleted, files and all: how many files went, or -1 with
why. Never the game's own folders, and never a mod in the install (the base path or Steam's).
==================
*/
int OzMods_Remove( const char *mod, char *message, int size ) {
	message[0] = '\0';
	// never the game's own folders nor the user folder's: "oz_mod_remove profile" deleted the career
	if ( !*mod || FS_InvalidGameDir( mod ) || !Q_stricmp( mod, BASEGAME ) || !Q_stricmp( mod, DEFAULT_GAME ) || OwnFolder( mod ) ||
			strchr( mod, '/' ) || strchr( mod, '\\' ) || strstr( mod, ".." ) ) {
		Com_sprintf( message, size, "That isn't a mod's folder." );
		return -1;
	}
	if ( !Q_stricmp( mod, Cvar_VariableString( "fs_game" ) ) ) {
		Com_sprintf( message, size, "That mod is running: leave it first." );
		return -1;
	}
#ifdef _WIN32
	{
		wchar_t w[MAX_OSPATH * 2];
		const char *dir = FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), mod, NULL );
		DWORD attr;
		int n;
		Wide( dir, w, ARRAY_LEN( w ) );
		attr = GetFileAttributesW( w );
		if ( attr == INVALID_FILE_ATTRIBUTES || !( attr & FILE_ATTRIBUTE_DIRECTORY ) ) {
			Com_sprintf( message, size, "%s isn't an imported mod: only those in your user folder can be removed.", mod );
			return -1;
		}
		// only a folder Oz's own import made or added to: another may hold what a server sent, with the
		// player's settings and demos under a mod that is in the install
		if ( !OzMods_Imported( mod ) ) {
			Com_sprintf( message, size, "%s isn't an imported mod: Remove deletes only what Import brought.", mod );
			return -1;
		}
		n = RemoveTree( w );
		MarkImported( mod, qfalse );
		ForgetReady();
		OzServers_ForgetMods();
		Com_sprintf( message, size, "%s removed from your user folder (%d %s).", mod, n, n == 1 ? "file" : "files" );
		Com_Printf( "oz_mod_remove: %s\n", message );
		return n;
	}
#else
	Com_sprintf( message, size, "Removing a mod is for the Windows game." );
	return -1;
#endif
}


/*
==================
OzMods_Guess

Which mod a zip or a folder holds, from its paks: the folder they're in when it's named for a mod,
else a known mod's name in a pak's own (z-cpma-pak153.pk3), else the deepest folder holding paks.
"" when there are no paks.
==================
*/
int OzMods_Guess( const char *source, char *mod, int size ) {
	mod[0] = '\0';
#ifdef _WIN32
	{
		wchar_t w[MAX_OSPATH * 2], to[MAX_OSPATH * 2];
		DWORD attr;
		int i, j, best = 0;
		char folder[MAX_QPATH];
		Wide( source, w, ARRAY_LEN( w ) );
		attr = GetFileAttributesW( w );
		numFiles = 0;
		if ( attr == INVALID_FILE_ATTRIBUTES ) {
			return 0;
		} else if ( attr & FILE_ATTRIBUTE_DIRECTORY ) {
			ListFolder( w, "" );
		} else if ( Ext( source, ".pk3" ) ) {
			const char *slash = strrchr( source, '\\' ) ? strrchr( source, '\\' ) : strrchr( source, '/' );
			Q_strncpyz( files[numFiles++].path, slash ? slash + 1 : source, sizeof( files[0].path ) );
		} else {
			unzFile z = OpenZip( source, "oz", to, ARRAY_LEN( to ) );
			if ( !z ) {
				return 0;
			}
			unzClose( z );
			if ( to[0] ) {
				DeleteFileW( to );
			}
		}
		folder[0] = '\0';
		for ( i = 0; i < numFiles; i++ ) {
			const char *p = files[i].path, *last, *slash = strrchr( files[i].path, '/' ), *c;
			char dir[MAX_QPATH];
			int depth = 0, score;
			if ( !Ext( p, ".pk3" ) ) {
				continue;
			}
			// the pak's own folder name, and how deep it is
			dir[0] = '\0';
			last = slash ? slash + 1 : p;
			if ( slash ) {
				const char *from = slash;
				int len;
				while ( from > p && from[-1] != '/' ) {
					from--;
				}
				len = (int)( slash - from );
				Q_strncpyz( dir, from, len + 1 < (int)sizeof( dir ) ? len + 1 : (int)sizeof( dir ) );
			}
			for ( c = p; *c; c++ ) {
				depth += *c == '/';
			}
			// a folder named for a known mod wins; then a known mod named in the pak; then the deepest pak folder
			score = 0;
			for ( j = 0; j < ARRAY_LEN( knownMods ); j++ ) {
				if ( dir[0] && !Q_stricmp( dir, knownMods[j].dir ) ) {
					score = 300 + depth;
					Q_strncpyz( folder, knownMods[j].dir, sizeof( folder ) );
				} else if ( score < 200 && Q_stristr( last, knownMods[j].dir ) ) {
					score = 200;
					Q_strncpyz( folder, knownMods[j].dir, sizeof( folder ) );
				}
			}
			if ( !score && dir[0] && !FS_InvalidGameDir( dir ) && 100 + depth > best ) {
				score = 100 + depth;
				Q_strncpyz( folder, dir, sizeof( folder ) );
			}
			if ( score > best ) {
				best = score;
				Q_strncpyz( mod, folder, size );
			}
		}
		return mod[0] != '\0';
	}
#else
	return 0;
#endif
}


/*
==================
OzMods_Dropped

A file or folder dropped on the window (sdl_input.c, SDL_DROPFILE): a zip, a pak or a folder goes to
the Mod Library page, which shows what it took it for and imports it.
==================
*/
void OzMods_Dropped( const char *path ) {
	char mod[MAX_QPATH];

	Com_Printf( "oz_mods: dropped %s\n", path );
	if ( !Ext( path, ".zip" ) && !Ext( path, ".pk3" ) ) {
#ifdef _WIN32
		wchar_t w[MAX_OSPATH * 2];
		DWORD attr;
		Wide( path, w, ARRAY_LEN( w ) );
		attr = GetFileAttributesW( w );
		if ( attr == INVALID_FILE_ATTRIBUTES || !( attr & FILE_ATTRIBUTE_DIRECTORY ) ) {
			Com_Printf( "oz_mods: a mod is a zip, a pak or a folder\n" );
			return;
		}
#else
		return;
#endif
	}
	OzMods_Guess( path, mod, sizeof( mod ) );
#ifdef USE_OZ_UI
	OzUI_ModDropped( path, mod );
#endif
}


static void ModImport_f( void ) {
	char message[256];

	if ( Cmd_Argc() < 3 ) {
		Com_Printf( "usage: oz_mod_import <mod> <zip, pak or folder>\n" );
		return;
	}
	OzMods_Import( Cmd_ArgsFrom( 2 ), Cmd_Argv( 1 ), message, sizeof( message ) );
}

static void ModRemove_f( void ) {
	char message[256];

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "usage: oz_mod_remove <mod>\n" );
		return;
	}
	if ( OzMods_Remove( Cmd_Argv( 1 ), message, sizeof( message ) ) < 0 ) {
		Com_Printf( "oz_mod_remove: %s\n", message );
	}
}

static void Mods_f( void ) {
	ozModInfo_t mods[64];
	const int n = OzMods_List( mods, ARRAY_LEN( mods ) );
	int i;

	if ( Cmd_Argc() > 2 && !strcmp( Cmd_Argv( 1 ), "drop" ) ) {
		OzMods_Dropped( Cmd_ArgsFrom( 2 ) );	// as a drop on the window, for tests
		return;
	}
	if ( Cmd_Argc() > 1 ) {
		// oz_mods guess <path>, for tests
		char mod[MAX_QPATH];
		OzMods_Guess( Cmd_ArgsFrom( 2 ), mod, sizeof( mod ) );
		Com_Printf( "oz_mods: %s looks like %s\n", Cmd_ArgsFrom( 2 ), mod[0] ? mod : "no mod" );
		return;
	}
	Com_Printf( "oz_mods: %d mod%s\n", n, n == 1 ? "" : "s" );
	for ( i = 0; i < n; i++ ) {
		Com_Printf( "  %s (%s) %s: %s%s%s%s, %d paks in %s\n", mods[i].dir, mods[i].name, mods[i].version[0] ? mods[i].version : "?",
			mods[i].ready ? "ready" : mods[i].nested ? "nested" : "paks only", mods[i].imported ? ", imported" : "",
			mods[i].supported ? ", supported" : "", mods[i].known ? "" : ", unknown", mods[i].paks, mods[i].where );
	}
}

// a zip's copy left in a mod's folder by an import the game didn't live through
static void RemoveStaleCopies( void ) {
	char home[MAX_OSPATH];
	char **dirs;
	int n, i;

	Q_strncpyz( home, Cvar_VariableString( "fs_homepath" ), sizeof( home ) );
	if ( !home[0] ) {
		return;
	}
	dirs = Sys_ListFiles( home, "/", NULL, &n, qtrue );
	for ( i = 0; dirs && i < n; i++ ) {
		if ( strcmp( dirs[i], "." ) && strcmp( dirs[i], ".." ) && !strchr( dirs[i], '/' ) && !strchr( dirs[i], '\\' ) ) {
			remove( FS_BuildOSPath( home, dirs[i], "import.zip.tmp" ) );
		}
	}
	Sys_FreeFileList( dirs );
}

void OzMods_Init( void ) {
	static qboolean done;

	if ( !done ) {
		done = qtrue;
		RemoveStaleCopies();
		Cmd_AddCommand( "oz_mod_import", ModImport_f );
		Cmd_AddCommand( "oz_mod_remove", ModRemove_f );
		Cmd_AddCommand( "oz_mods", Mods_f );
	}
}
