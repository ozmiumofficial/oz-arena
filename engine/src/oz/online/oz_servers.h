/*
Oz: the community servers (ENGINE_PLAN.md P8b step 1, oz_servers.c), as the native UI's C++ reads
them. Plain C types only. The engine's own calls (OzServers_Init, OzServers_InfoResponse) are in
client.h.
*/
#ifndef OZ_SERVERS_H
#define OZ_SERVERS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	OZS_IDLE,			// never asked
	OZS_MASTERS,		// asking the master servers, no list yet
	OZS_SERVERS,		// asking the servers they list
	OZS_DONE,
	OZS_NO_MASTERS,		// no master server answered (offline, or a firewall)
	OZS_LOCAL_SERVER	// a server runs on this PC: it takes every packet (net_ip.c), so nothing was asked
} ozServersState_t;

typedef struct {
	const char	*address;	// "203.0.113.7:27960"
	const char	*info;		// what getinfo answered: hostname, mapname, game, gametype, clients, g_humanplayers,
							// sv_maxclients, g_needpass, pure, protocol
	int			ping;		// ms
} ozServerInfo_t;

void	OzServers_Refresh( void );		// asks every master (sv_master1..5) for both protocols, then each server listed
void	OzServers_Frame( void );		// each frame: the masters one by one, the servers at the sweep's pace
ozServersState_t OzServers_State( int *listed, int *asked, int *answered );	// any count may be NULL
int		OzServers_Age( void );			// ms since the last refresh, -1 before the first
int		OzServers_Generation( void );	// changes whenever the list or an answer does
int		OzServers_Count( void );		// the listed servers, answered or not
int		OzServers_Get( int index, ozServerInfo_t *out );	// 1 for a server that answered
void	OzServers_Also( const char *address );	// a saved server, asked with the masters' (just after a refresh)
int		OzServers_Listed( const char *address );	// a master listed it: a public server (presence may name it)
void	OzServers_LanRefresh( void );	// the LAN tab: Quake3e's localservers broadcast, answers for LAN_WAIT
ozServersState_t OzServers_LanState( void );	// OZS_IDLE, OZS_SERVERS (listening), OZS_DONE or OZS_LOCAL_SERVER
int		OzServers_LanCount( void );
int		OzServers_LanGet( int index, ozServerInfo_t *out );
int		OzServers_PlayerStatus( const char *address, char *buf, int size, int again );	// getstatus (Quake3e's
						// CL_ServerStatus): 1 once its answer is in buf; again asks afresh
int		OzServers_MayAsk( void );		// oz_servers_ask: the Server Browser may ask the masters by itself
int		OzServers_HaveMod( const char *game );	// a mod folder with paks in the install or the user folder (baseq3 and oz: 1)
void	OzServers_ForgetMods( void );	// a mod was imported: HaveMod looks again

// oz_mods.c (src/oz/ui): the mods a player brings (ENGINE_PLAN.md decision 24)
int		OzMods_Import( const char *source, const char *mod, char *message, int size );	// a zip, pak or folder into
						// <fs_homepath>/<mod>: how many files, or -1 (message says why; on success, what was taken)
int		OzMods_InInstall( const char *mod, char *path, int size );	// a copy in the install the file system doesn't
						// find (its paks in <mod>/<mod>, Defrag's bundle): 1 and its folder
int		OzMods_Pick( int folder, char *path, int size );	// Windows' open dialog: a zip or pak, or a folder (0: cancelled)
int		OzMods_Ready( const char *mod );	// the mod's game code is where a switch to it finds it (a pak named for it
						// in <mod> of the user folder, the base path or the Steam install)

// the Mod Library (PH step 2, oz_ui_mods.cpp)
typedef struct {
	const char	*dir, *name, *uses;	// its folder, its name, what it is and the engine extensions it uses
	int			supported;			// CPMA and Defrag (decision 14); the rest play with no promise
} ozKnownMod_t;

typedef struct {
	char		dir[64], name[64], version[64], where[256];	// where: the folder it was found in
	int			ready, imported, nested, supported, known, paks;	// ready: OzMods_Ready; imported: in the user folder;
						// nested: only a copy the file system doesn't find (<mod>/<mod>); known: in the table
} ozModInfo_t;

int		OzMods_Known( const ozKnownMod_t **out );	// the table of known mods (no download links, ever)
const ozKnownMod_t *OzMods_KnownMod( const char *dir );
int		OzMods_List( ozModInfo_t *out, int max );	// every mod folder with paks, where it is, its version
int		OzMods_Remove( const char *mod, char *message, int size );	// an imported mod's folder deleted: files, or -1 and why
int		OzMods_Guess( const char *source, char *mod, int size );	// which mod a zip, pak or folder holds, from its paks
void	OzMods_Dropped( const char *path );	// a file or folder dropped on the window (sdl_input.c)

#ifdef __cplusplus
}
#endif

#endif
