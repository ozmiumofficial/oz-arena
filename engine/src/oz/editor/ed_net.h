/*
Oz: editing together's network for the map editor's C++ (ENGINE_PLAN.md PE): the session's links, its
listing on the hub and its channel (oz_edlink.c over oz_ice.c), through oz_editor_engine.c. Plain C types.
Without the online service or the peer-to-peer transport built in, OzEdNet_Available is 0 and the rest
does nothing.
*/
#ifndef OZ_ED_NET_H
#define OZ_ED_NET_H

#ifdef __cplusplus
extern "C" {
#endif

int			OzEdNet_Available( void );		// 1: built with the transport
int			OzEdNet_Online( void );			// 1: connected to the hub
const char	*OzEdNet_Me( void );			// this player's hub id, "" offline
const char	*OzEdNet_Name( void );			// this player's name (the name cvar, with its colors)
const char	*OzEdNet_Model( void );			// their fighter ("sarge/default")
const char	*OzEdNet_Version( void );		// "oz-ed1 <the game's version>"
void		OzEdNet_Host( const char *name, const char *map, int players, int max, const char *password );
void		OzEdNet_Unhost( void );			// the listing down and every editing link closed
void		OzEdNet_Join( const char *host, const char *password );
void		OzEdNet_Leave( const char *pid );
void		OzEdNet_Browse( void );			// the hub's listings: OzEdNet_Sessions has the editing ones after
// presence while in a session: "edit:<host>[ locked]" and "Editing <map>"; both empty: none
void		OzEdNet_Presence( const char *join, const char *detail );
void		OzEdNet_Send( const char *pid, const void *data, int len, int fast );
int			OzEdNet_Queued( const char *pid );
int			OzEdNet_Relayed( const char *pid );
// The next event (1 a message, 2 a link up, 3 down, 4 a join failed (data: why); 0 none). data: free with OzEdNet_Free.
int			OzEdNet_Poll( char *pid, int pidSize, void **data, int *len, int *fast );
void		OzEdNet_Free( void *p );
int			OzEdNet_Milliseconds( void );
// the console's and a test's: a command run in front of the buffer (a play-test's map, connect)
void		OzEdNet_Command( const char *text );
int			OzEdNet_ClientState( void );	// 0 disconnected, 1 connecting, 2 in a match
int			OzEdNet_ServerRunning( void );	// a listen server runs here
const char	*OzEdNet_MapName( void );		// the running map ("" none)
void		OzEdNet_SetCvar( const char *name, const char *value );
// this player in the match: their origin and view angles (a Reload puts them back there); 0 when not in one
int			OzEdNet_PlayerPos( float origin[3], float angles[3] );
void		OzEdNet_ClearError( void );		// the last match's ending isn't shown as an error (a play-test together ended)
int			OzEdNet_GameType( void );		// the match's g_gametype (its serverinfo's), -1 when not in one
int			OzEdNet_Spectating( void );		// 1: this player is a spectator in the match (a team mode's start)

#ifdef __cplusplus
}
#endif

#endif
