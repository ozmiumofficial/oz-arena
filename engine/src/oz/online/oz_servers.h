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

#ifdef __cplusplus
}
#endif

#endif
