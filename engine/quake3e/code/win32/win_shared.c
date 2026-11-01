/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "win_local.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <direct.h>
#include <io.h>
#include <conio.h>
#include <intrin.h>

/*
================
Sys_Milliseconds
================
*/
int Sys_Milliseconds( void )
{
	static qboolean	initialized = qfalse;
	static DWORD sys_timeBase;
	int	sys_curtime;

	if ( !initialized ) {
		sys_timeBase = timeGetTime();
		initialized = qtrue;
	}

	sys_curtime = timeGetTime() - sys_timeBase;

	return sys_curtime;
}


/*
================
Sys_RandomBytes
================
*/
qboolean Sys_RandomBytes( byte *string, int len )
{
	HCRYPTPROV  prov;

	if( !CryptAcquireContext( &prov, NULL, NULL,
		PROV_RSA_FULL, CRYPT_VERIFYCONTEXT ) )  {

		return qfalse;
	}

	if( !CryptGenRandom( prov, len, (BYTE *)string ) )  {
		CryptReleaseContext( prov, 0 );
		return qfalse;
	}
	CryptReleaseContext( prov, 0 );
	return qtrue;
}


#ifdef UNICODE
LPWSTR AtoW( const char *s ) 
{
	static WCHAR buffer[MAXPRINTMSG*2];
	MultiByteToWideChar( CP_ACP, 0, s, strlen( s ) + 1, (LPWSTR) buffer, ARRAYSIZE( buffer ) );
	return buffer;
}

const char *WtoA( const LPWSTR s ) 
{
	static char buffer[MAXPRINTMSG*2];
	WideCharToMultiByte( CP_ACP, 0, s, -1, buffer, ARRAYSIZE( buffer ), NULL, NULL );
	return buffer;
}
#endif


/*
================
Sys_DefaultHomePath
================
*/
const char *Sys_DefaultHomePath( void ) 
{
#ifdef USE_PROFILES
	TCHAR szPath[MAX_PATH];
	static char path[MAX_OSPATH];
	FARPROC qSHGetFolderPath;
	HMODULE shfolder = LoadLibrary("shfolder.dll");
	
	if(shfolder == NULL) {
		Com_Printf("Unable to load SHFolder.dll\n");
		return NULL;
	}

	qSHGetFolderPath = GetProcAddress(shfolder, "SHGetFolderPathA");
	if(qSHGetFolderPath == NULL)
	{
		Com_Printf("Unable to find SHGetFolderPath in SHFolder.dll\n");
		FreeLibrary(shfolder);
		return NULL;
	}

	if( !SUCCEEDED( qSHGetFolderPath( NULL, CSIDL_APPDATA,
		NULL, 0, szPath ) ) )
	{
		Com_Printf("Unable to detect CSIDL_APPDATA\n");
		FreeLibrary(shfolder);
		return NULL;
	}
	Q_strncpyz( path, szPath, sizeof(path) );
	Q_strcat( path, sizeof(path), "\\" HOMEPATH_NAME_WIN );	// Oz
	FreeLibrary(shfolder);
	if( !CreateDirectory( path, NULL ) )
	{
		if( GetLastError() != ERROR_ALREADY_EXISTS )
		{
			Com_Printf("Unable to create directory \"%s\"\n", path);
			return NULL;
		}
	}
	return path;
#else
    return NULL;
#endif
}


#ifdef STEAMPATH_NAME
/*
================
Sys_SteamGameIn

Oz: true when <library>\steamapps\common\<STEAMPATH_NAME>\baseq3 exists; writes that game folder to out.
================
*/
static qboolean Sys_SteamGameIn( const char *library, char *out, int outSize )
{
	char gameDir[ MAX_OSPATH ];
	DWORD attr;

	Com_sprintf( gameDir, sizeof( gameDir ), "%s\\steamapps\\common\\" STEAMPATH_NAME, library );
	attr = GetFileAttributesA( va( "%s\\" BASEGAME, gameDir ) );
	if ( attr == INVALID_FILE_ATTRIBUTES || !( attr & FILE_ATTRIBUTE_DIRECTORY ) )
		return qfalse;

	Q_strncpyz( out, gameDir, outSize );
	return qtrue;
}


/*
================
Sys_SteamLibraryGame

Oz: Steam keeps games in any of several library folders, listed in
<steam>\steamapps\libraryfolders.vdf as "path" "<folder>" entries. Quake3e looked only in Steam's
own folder; this finds the game in whichever library has it.
================
*/
static qboolean Sys_SteamLibraryGame( const char *steamRoot, char *out, int outSize )
{
	char vdfPath[ MAX_OSPATH ], library[ MAX_OSPATH ];
	char *text, *p, *q;
	FILE *f;
	long len;
	qboolean found = qfalse;

	if ( Sys_SteamGameIn( steamRoot, out, outSize ) )
		return qtrue;

	Com_sprintf( vdfPath, sizeof( vdfPath ), "%s\\steamapps\\libraryfolders.vdf", steamRoot );
	f = fopen( vdfPath, "rb" );
	if ( !f )
		return qfalse;
	fseek( f, 0, SEEK_END );
	len = ftell( f );
	fseek( f, 0, SEEK_SET );
	if ( len <= 0 || len > 1024 * 1024 ) {
		fclose( f );
		return qfalse;
	}
	text = malloc( len + 1 );
	if ( !text ) {
		fclose( f );
		return qfalse;
	}
	len = (long)fread( text, 1, len, f );
	text[ len ] = '\0';
	fclose( f );

	for ( p = text; !found && ( p = strstr( p, "\"path\"" ) ) != NULL; ) {
		int n = 0;
		p += 6;
		while ( *p && *p != '"' && *p != '\n' )	// the value's opening quote
			p++;
		if ( *p != '"' )
			continue;
		for ( q = p + 1; *q && *q != '"' && n < (int)sizeof( library ) - 1; q++ ) {
			if ( q[0] == '\\' && q[1] == '\\' )	// VDF escapes backslashes
				q++;
			library[ n++ ] = *q;
		}
		library[ n ] = '\0';
		p = q;
		if ( n && Sys_SteamGameIn( library, out, outSize ) )
			found = qtrue;
	}

	free( text );
	return found;
}
#endif


#ifdef GOGPATH_ID
/*
================
Sys_GogGame

Oz: GOG's copy of the game, as ioquake3's Sys_GogPath finds it: the folder GOG's installer records as
"path" under HKLM\SOFTWARE\GOG.com\Games\<GOGPATH_ID> (the 32-bit view), when it has baseq3.
================
*/
static qboolean Sys_GogGame( char *out, int outSize )
{
	char path[ MAX_OSPATH ];
	DWORD len = sizeof( path ) - 1, type, attr;
	HKEY key;
	int n;

	if ( RegOpenKeyExA( HKEY_LOCAL_MACHINE, "SOFTWARE\\GOG.com\\Games\\" GOGPATH_ID, 0, KEY_QUERY_VALUE | KEY_WOW64_32KEY, &key ) != ERROR_SUCCESS )
		return qfalse;
	if ( RegQueryValueExA( key, "path", NULL, &type, (LPBYTE)path, &len ) != ERROR_SUCCESS || ( type != REG_SZ && type != REG_EXPAND_SZ ) )
		len = 0;
	RegCloseKey( key );
	path[ len ] = '\0';

	n = (int)strlen( path );
	while ( n > 0 && ( path[ n - 1 ] == '\\' || path[ n - 1 ] == '/' ) )	// some installers end it with one
		path[ --n ] = '\0';
	if ( n == 0 )
		return qfalse;
	attr = GetFileAttributesA( va( "%s\\" BASEGAME, path ) );
	if ( attr == INVALID_FILE_ATTRIBUTES || !( attr & FILE_ATTRIBUTE_DIRECTORY ) )
		return qfalse;

	Q_strncpyz( out, path, outSize );
	return qtrue;
}
#endif


/*
================
Sys_SteamPath
================
*/
const char *Sys_SteamPath( void )
{
	static TCHAR steamPath[ MAX_OSPATH ]; // will be converted from TCHAR to ANSI

#if defined(STEAMPATH_NAME) || defined(STEAMPATH_APPID)
	HKEY steamRegKey;
	DWORD pathLen = MAX_OSPATH;
	qboolean finishPath = qfalse;
#endif

#ifdef STEAMPATH_APPID
	// Assuming Steam is a 32-bit app
	if ( !steamPath[0] && RegOpenKeyEx(HKEY_LOCAL_MACHINE, AtoW("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App " STEAMPATH_APPID), 0, KEY_QUERY_VALUE | KEY_WOW64_32KEY, &steamRegKey ) == ERROR_SUCCESS ) 
	{
		pathLen = sizeof( steamPath );
		if ( RegQueryValueEx( steamRegKey, AtoW("InstallLocation"), NULL, NULL, (LPBYTE)steamPath, &pathLen ) != ERROR_SUCCESS )
			steamPath[ 0 ] = '\0';

		RegCloseKey( steamRegKey );
	}

#ifdef STEAMPATH_NAME
	if ( !steamPath[0] && RegOpenKeyEx(HKEY_CURRENT_USER, AtoW("Software\\Valve\\Steam"), 0, KEY_QUERY_VALUE, &steamRegKey ) == ERROR_SUCCESS )
	{
		pathLen = sizeof( steamPath );
		if ( RegQueryValueEx( steamRegKey, AtoW("SteamPath"), NULL, NULL, (LPBYTE)steamPath, &pathLen ) != ERROR_SUCCESS ) {
			pathLen = sizeof( steamPath );
			if ( RegQueryValueEx( steamRegKey, AtoW("InstallPath"), NULL, NULL, (LPBYTE)steamPath, &pathLen ) != ERROR_SUCCESS )
				steamPath[ 0 ] = '\0';
		}

		if ( steamPath[ 0 ] )
			finishPath = qtrue;

		RegCloseKey( steamRegKey );
	}
#endif

	if ( steamPath[ 0 ] )
	{
		if ( pathLen == sizeof( steamPath ) )
			pathLen--;

		*( ((char*)steamPath) + pathLen )  = '\0';
#ifdef UNICODE
		strcpy( (char*)steamPath, WtoA( steamPath ) );
#endif
		if ( finishPath ) {
			// Oz: steamPath is Steam's own folder here; the game may be in any of its libraries
			char steamRoot[ MAX_OSPATH ];
			Q_strncpyz( steamRoot, (char*)steamPath, sizeof( steamRoot ) );
			if ( !Sys_SteamLibraryGame( steamRoot, (char*)steamPath, MAX_OSPATH ) )
				Q_strcat( (char*)steamPath, MAX_OSPATH, "\\SteamApps\\common\\" STEAMPATH_NAME );
		}
	}
#endif

#ifdef GOGPATH_ID
	// Oz: without Steam's copy, GOG's: fs_steampath is the player's own install for the engine and Oz's tools
	{
		DWORD attr = ( (char*)steamPath )[0] ? GetFileAttributesA( va( "%s\\" BASEGAME, (char*)steamPath ) ) : INVALID_FILE_ATTRIBUTES;
		if ( attr == INVALID_FILE_ATTRIBUTES || !( attr & FILE_ATTRIBUTE_DIRECTORY ) )
			Sys_GogGame( (char*)steamPath, MAX_OSPATH );
	}
#endif

	return (const char*)steamPath;
}


/*
================
Sys_SetAffinityMask
================
*/
#ifdef USE_AFFINITY_MASK
static HANDLE hCurrentProcess = 0;

uint64_t Sys_GetAffinityMask( void )
{
	DWORD_PTR dwProcessAffinityMask;
	DWORD_PTR dwSystemAffinityMask;

	if ( hCurrentProcess == 0 )	{
		hCurrentProcess = GetCurrentProcess();
	}

	if ( GetProcessAffinityMask( hCurrentProcess, &dwProcessAffinityMask, &dwSystemAffinityMask ) )	{
		return (uint64_t)dwProcessAffinityMask;
	}

	return 0;
}


qboolean Sys_SetAffinityMask( const uint64_t mask )
{
	DWORD_PTR dwProcessAffinityMask = (DWORD_PTR)mask;

	if ( hCurrentProcess == 0 ) {
		hCurrentProcess = GetCurrentProcess();
	}

	if ( SetProcessAffinityMask( hCurrentProcess, dwProcessAffinityMask ) )	{
		//Sleep( 0 );
		return qtrue;
	}

	return qfalse;
}
#endif // USE_AFFINITY_MASK
