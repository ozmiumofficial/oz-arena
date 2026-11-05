/*
===========================================================================
Oz Arena: updates. GPL-2.0.

At launch the game asks GitHub for the latest release (update.cpp, on a thread) and, when its tag is
newer than this build's OZ_VERSION, offers it over the main menu (menu.rml's Update available:
Update now, Later, Skip this version). Update now:

  Windows	an install Setup made (oz-setup.txt beside OzArena.exe): the release's OzArenaSetup.exe is
			downloaded into %TEMP% and started as `/update "/dir=<this folder>" /go`; the game quits, and
			Setup waits for it to close, updates the folder and starts the game again.
  Linux		the launcher beside the game (oz-arena-launcher --update), which does the same.
  Otherwise	(a dev build, a zip unpacked by hand) the release's page opens in the browser.

  oz_update_check	auto (the default): only from the player's own user folder, so tests never ask
					GitHub; 1: always; 0: never (the Linux launcher sets 0: it asked before the game)
  oz_update_url		the release to ask about (a test points it at a local stand-in)
  oz_update_skip	the version Skip this version passed over (archived)

  oz_update [check|now|skip|status]
===========================================================================
*/
#include "../../../quake3e/code/client/client.h"
#include "oz_online.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#define OZ_UPDATE_DEFAULT_URL	"https://api.github.com/repos/ozmiumofficial/oz-arena/releases/latest"
#define OZ_UPDATE_PAGE			"https://github.com/ozmiumofficial/oz-arena/releases/latest"
#define OZ_UPDATE_DOWNLOADS		"https://github.com/ozmiumofficial/oz-arena/releases/download/"

#ifdef USE_OZ_UI
void OzUI_UpdateOffer( const char *version, const char *name, const char *published, const char *channel,
	const char notes[][160], int numNotes );
void OzUI_UpdateStatus( const char *text, int failed );
#endif
qboolean IN_OpenURL( const char *url );	// sdl_input.c

static cvar_t		*oz_update_check, *oz_update_url, *oz_update_skip;
static qboolean		asked;			// once a run: a mod switch restarts the client, not the check
static qboolean		reported;		// the answer handled
static qboolean		downloading;
static ozRelease_t	latest;
static int			newer;			// 1: latest is newer than this build
static char			setupPath[MAX_OSPATH];
static int			lastPercent = -1;


static qboolean Wanted( void ) {
	if ( !Q_stricmp( oz_update_check->string, "auto" ) ) {
		// the player's own user folder: a test's scratch fs_homepath never asks GitHub
		return !Q_stricmp( Cvar_VariableString( "fs_homepath" ), Sys_DefaultHomePath() );
	}
	return oz_update_check->integer != 0;
}


static void Status( const char *text, qboolean failed ) {
	Com_Printf( "update: %s\n", text );
#ifdef USE_OZ_UI
	OzUI_UpdateStatus( text, failed );
#endif
}


static void Offer( void ) {
#ifdef USE_OZ_UI
	const char *channel = Q_stristr( latest.tag, "alpha" ) ? "Alpha" : Q_stristr( latest.tag, "beta" ) ? "Beta"
		: Q_stristr( latest.tag, "rc" ) ? "Release candidate" : "Release";
	OzUI_UpdateOffer( latest.tag, latest.name, latest.published, channel, (const char (*)[160])latest.notes, latest.numNotes );
#endif
}


static void OpenPage( void ) {
	const char *page = !Q_stricmpn( latest.page, "https://github.com/", 19 ) ? latest.page : OZ_UPDATE_PAGE;
	if ( IN_OpenURL( page ) ) {
		Status( va( "opened %s: download %s there", page, latest.tag[0] ? latest.tag : "the latest release" ), qfalse );
	} else {
		Status( va( "the browser didn't open: the update is at %s", page ), qtrue );
	}
}


#ifdef _WIN32
// this game's folder, wide (Sys_Pwd's is the ANSI code page's)
static void GameFolder( wchar_t *dir, int size ) {
	wchar_t *slash;
	GetModuleFileNameW( NULL, dir, size );
	dir[size - 1] = 0;
	slash = wcsrchr( dir, L'\\' );
	if ( slash ) {
		*slash = 0;
	}
}

static qboolean SetupInstalled( void ) {
	wchar_t dir[MAX_PATH], path[MAX_PATH + 32];
	GameFolder( dir, MAX_PATH );
	_snwprintf( path, MAX_PATH + 32, L"%s\\oz-setup.txt", dir );
	path[MAX_PATH + 31] = 0;
	return GetFileAttributesW( path ) != INVALID_FILE_ATTRIBUTES;
}

// Setup in its update mode, as the player (Setup asks for administrator rights itself when the folder needs them)
static qboolean RunSetup( const wchar_t *exe ) {
	wchar_t dir[MAX_PATH], cmd[3 * MAX_PATH];
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;

	GameFolder( dir, MAX_PATH );
	_snwprintf( cmd, ARRAY_LEN( cmd ), L"\"%s\" /update \"/dir=%s\" /go", exe, dir );
	cmd[ARRAY_LEN( cmd ) - 1] = 0;
	memset( &si, 0, sizeof( si ) );
	si.cb = sizeof( si );
	if ( !CreateProcessW( exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi ) ) {
		Com_Printf( "update: Setup didn't start (error %lu)\n", GetLastError() );
		return qfalse;
	}
	CloseHandle( pi.hThread );
	CloseHandle( pi.hProcess );
	return qtrue;
}

static void Downloaded( void ) {
	wchar_t exe[MAX_OSPATH];
	MultiByteToWideChar( CP_UTF8, 0, setupPath, -1, exe, MAX_OSPATH );
	if ( !RunSetup( exe ) ) {
		Status( "Setup didn't start.", qtrue );
		OpenPage();
		return;
	}
	Status( va( "Setup is updating Oz Arena to %s: the game closes now", latest.tag ), qfalse );
	Cbuf_ExecuteText( EXEC_INSERT, "quit\n" );	// in front of anything queued
}

static void Now( void ) {
	wchar_t temp[MAX_PATH], folder[MAX_PATH + 64], dir[MAX_PATH], unins[MAX_PATH + 32];
	qboolean ours;

	if ( !SetupInstalled() ) {
		OpenPage();	// not an install Setup made: never Setup over a dev build or a hand-made folder
		return;
	}
	ours = !Q_stricmp( oz_update_url->string, OZ_UPDATE_DEFAULT_URL );
	if ( latest.setup[0] && ( !ours || !Q_stricmpn( latest.setup, OZ_UPDATE_DOWNLOADS, strlen( OZ_UPDATE_DOWNLOADS ) ) ) ) {
		GetTempPathW( MAX_PATH, temp );
		_snwprintf( folder, ARRAY_LEN( folder ), L"%sOzArenaUpdate-%u", temp, (unsigned)GetTickCount() );
		folder[ARRAY_LEN( folder ) - 1] = 0;
		CreateDirectoryW( folder, NULL );	// a folder of its own: Setup installs from files beside it when there are any
		WideCharToMultiByte( CP_UTF8, 0, folder, -1, setupPath, sizeof( setupPath ) - 24, NULL, NULL );
		Q_strcat( setupPath, sizeof( setupPath ), "\\OzArenaSetup.exe" );
		OzUpdate_Download( latest.setup, setupPath );
		downloading = qtrue;
		lastPercent = -1;
		Status( "Downloading the installer...", qfalse );
		return;
	}
	// no installer in the release: the one this install came with, which fetches the release itself
	GameFolder( dir, MAX_PATH );
	_snwprintf( unins, ARRAY_LEN( unins ), L"%s\\Uninstall Oz Arena.exe", dir );
	unins[ARRAY_LEN( unins ) - 1] = 0;
	if ( GetFileAttributesW( unins ) != INVALID_FILE_ATTRIBUTES && RunSetup( unins ) ) {
		Status( va( "Setup is updating Oz Arena to %s: the game closes now", latest.tag ), qfalse );
		Cbuf_ExecuteText( EXEC_INSERT, "quit\n" );	// in front of anything queued
		return;
	}
	OpenPage();
}

#else	// Linux: the launcher beside the game

static void Downloaded( void ) {
}

static void Now( void ) {
	char launcher[MAX_OSPATH];
	pid_t pid;

	Com_sprintf( launcher, sizeof( launcher ), "%s/oz-arena-launcher", Sys_Pwd() );
	if ( access( launcher, X_OK ) ) {
		OpenPage();
		return;
	}
	pid = fork();
	if ( pid == 0 ) {
		int fd;
		setsid();
		for ( fd = 3; fd < 256; fd++ ) {
			close( fd );	// not the game's sockets and files
		}
		execl( launcher, launcher, "--update", (char *)NULL );
		_exit( 127 );
	}
	if ( pid < 0 ) {
		Status( "The launcher didn't start.", qtrue );
		OpenPage();
		return;
	}
	Status( va( "the launcher is updating Oz Arena to %s: the game closes now", latest.tag ), qfalse );
	Cbuf_ExecuteText( EXEC_INSERT, "quit\n" );	// in front of anything queued
}
#endif


static void Ask( void ) {
	asked = qtrue;
	reported = qfalse;
	OzUpdate_Check( oz_update_url->string );
}


static void CL_OzUpdate_f( void ) {
	const char *what = Cmd_Argv( 1 );

	if ( !Q_stricmp( what, "check" ) ) {
		Cvar_Set( "oz_update_skip", "" );	// asked for by hand: offer even a skipped version
		Ask();
		Com_Printf( "update: asking %s\n", oz_update_url->string );
	} else if ( !Q_stricmp( what, "now" ) ) {
		if ( !newer ) {
			Com_Printf( "update: nothing newer to update to (%s)\n", latest.tag[0] ? latest.tag : "not asked yet" );
			return;
		}
		if ( downloading ) {
			return;
		}
		Now();
	} else if ( !Q_stricmp( what, "skip" ) ) {
		if ( latest.tag[0] ) {
			Cvar_Set( "oz_update_skip", latest.tag );
			Com_Printf( "update: skipping %s\n", latest.tag );
		}
	} else {
		Com_Printf( "update: this is %s; latest %s%s; check %s\n", OZ_VERSION,
			latest.tag[0] ? latest.tag : "not known", newer ? " (newer)" : "", oz_update_check->string );
		Com_Printf( "usage: oz_update [check|now|skip|status]\n" );
	}
}


/*
==================
OzUpdate_Frame

Every client frame: asks once a run, then offers the answer; a running download's progress.
==================
*/
void OzUpdate_Frame( void ) {
	int state;

	if ( !asked ) {
		if ( !Wanted() ) {
			return;
		}
		Ask();
		return;
	}
	if ( !reported ) {
		state = OzUpdate_Checked( &latest );
		if ( state == OZ_UPDATE_BUSY ) {
			return;
		}
		reported = qtrue;
		if ( state == OZ_UPDATE_FAILED ) {
			Com_Printf( "update: the release check failed (%s)\n", latest.error );
			return;
		}
		newer = OzUpdate_Compare( latest.tag, OZ_VERSION ) > 0;
		if ( !newer ) {
			Com_Printf( "update: %s is the latest (the latest release is %s)\n", OZ_VERSION, latest.tag );
			return;
		}
		if ( !Q_stricmp( latest.tag, oz_update_skip->string ) ) {
			Com_Printf( "update: %s is out (this is %s), skipped\n", latest.tag, OZ_VERSION );
			return;
		}
		Com_Printf( "update: %s is out (this is %s)\n", latest.tag, OZ_VERSION );
		Offer();
		return;
	}
	if ( downloading ) {
		long long done, total;
		char error[128];
		state = OzUpdate_Downloaded( &done, &total, error, sizeof( error ) );
		if ( state == OZ_UPDATE_BUSY ) {
			const int pct = total > 0 ? (int)( done * 100 / total ) : -1;
			if ( pct != lastPercent && pct >= 0 ) {
				lastPercent = pct;
#ifdef USE_OZ_UI
				OzUI_UpdateStatus( va( "Downloading the installer... %d%%", pct ), qfalse );
#endif
			}
			return;
		}
		downloading = qfalse;
		if ( state == OZ_UPDATE_FAILED ) {
			Status( va( "The installer didn't download (%s).", error ), qtrue );
			OpenPage();
			return;
		}
		Downloaded();
	}
}


void OzUpdate_Init( void ) {
	oz_update_check = Cvar_Get( "oz_update_check", "auto", 0 );
	Cvar_SetDescription( oz_update_check, "Ask GitHub for a newer release at launch:\n auto - only from your own user folder\n 1 - always\n 0 - never" );
	oz_update_url = Cvar_Get( "oz_update_url", OZ_UPDATE_DEFAULT_URL, 0 );
	Cvar_SetDescription( oz_update_url, "The latest release's JSON (GitHub's releases/latest)." );
	oz_update_skip = Cvar_Get( "oz_update_skip", "", CVAR_ARCHIVE );
	Cvar_SetDescription( oz_update_skip, "The release Skip this version passed over: not offered again at launch." );
	Cmd_AddCommand( "oz_update", CL_OzUpdate_f );
	if ( reported && newer && Q_stricmp( latest.tag, oz_update_skip->string ) ) {
		Offer();	// a client restart (a mod switch, vid_restart) rebuilt the menus: the offer stands
	}
}


void OzUpdate_ClientShutdown( qboolean quit ) {
	Cmd_RemoveCommand( "oz_update" );
	if ( quit ) {
		OzUpdate_Shutdown();
	}
}
