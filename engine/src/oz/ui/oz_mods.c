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

  oz_mod_import <mod> <zip or folder>	imports one, as the Server Browser's Import does
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
#endif

#define MAX_IMPORT_FILES	4096

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

static qboolean WriteOut( const char *mod, const char *rel, const void *data, int len ) {
	const char *ospath = FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), mod, rel );
	FILE *f;

	FS_CreatePath( ospath );
	f = Sys_FOpen( ospath, "wb" );
	if ( !f ) {
		return qfalse;
	}
	fwrite( data, 1, len, f );
	fclose( f );
	return qtrue;
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
		void *data;
		long len;
		if ( Q_stricmpn( files[i].path, base, strlen( base ) ) || Refused( rel ) ) {
			continue;
		}
		Wide( files[i].path, relw, ARRAY_LEN( relw ) );
		swprintf( from, ARRAY_LEN( from ), L"%ls\\%ls", root, relw );
		f = _wfopen( from, L"rb" );
		if ( !f ) {
			continue;
		}
		fseek( f, 0, SEEK_END );
		len = ftell( f );
		fseek( f, 0, SEEK_SET );
		data = Z_Malloc( len > 0 ? len : 1 );
		if ( fread( data, 1, len, f ) == (size_t)len && WriteOut( mod, rel, data, (int)len ) ) {
			n++;
		}
		Z_Free( data );
		fclose( f );
	}
	return n;
}

static int ImportZip( const char *source, const char *mod, char *message, int size ) {
	wchar_t from[MAX_OSPATH * 2], to[MAX_OSPATH * 2];
	const char *copy = FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), mod, "import.zip.tmp" );
	const char *base;
	unzFile z;
	int i, n = 0;

	// a copy where unzOpen can open it (the user folder's path fits the system's code page; a download folder may not)
	FS_CreatePath( copy );
	Wide( source, from, ARRAY_LEN( from ) );
	MultiByteToWideChar( CP_ACP, 0, copy, -1, to, ARRAY_LEN( to ) );
	if ( !CopyFileW( from, to, FALSE ) || !( z = unzOpen( copy ) ) ) {
		Com_sprintf( message, size, "That file couldn't be read as a zip." );
		DeleteFileW( to );
		return -1;
	}
	numFiles = 0;
	for ( i = unzGoToFirstFile( z ); i == UNZ_OK && numFiles < MAX_IMPORT_FILES; i = unzGoToNextFile( z ) ) {
		unz_file_info info;
		char name[MAX_OSPATH], *c;
		unzGetCurrentFileInfo( z, &info, name, sizeof( name ), NULL, 0, NULL, 0 );
		for ( c = name; *c; c++ ) {
			*c = *c == '\\' ? '/' : *c;
		}
		if ( name[0] && name[strlen( name ) - 1] != '/' ) {	// a folder's own entry is no file
			Q_strncpyz( files[numFiles++].path, name, sizeof( files[0].path ) );
		}
	}
	if ( !( base = ModBase( mod ) ) ) {
		Com_sprintf( message, size, "There are no paks in that zip." );
		unzClose( z );
		DeleteFileW( to );
		return -1;
	}
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
	unzClose( z );
	DeleteFileW( to );
	return n;
}
#endif


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
	if ( !*mod || strchr( mod, '/' ) || strchr( mod, '\\' ) || strstr( mod, ".." ) || FS_InvalidGameDir( mod ) ) {
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
	Com_sprintf( message, size, "Importing a mod is for the Windows game." );
#endif
	if ( n == 0 ) {
		Com_sprintf( message, size, "Nothing in it could be taken." );
		n = -1;
	}
	if ( n > 0 ) {
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
	path[0] = '\0';
	return 0;
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

void OzMods_Init( void ) {
	static qboolean done;

	if ( !done ) {
		done = qtrue;
		Cmd_AddCommand( "oz_mod_import", ModImport_f );
	}
}
