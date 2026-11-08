/*
===========================================================================
Oz Arena: editing together's channel (ENGINE_PLAN.md PE step 2). GPL-2.0.

An editing session's players talk over P8's ICE links (oz_ice.c), beside any match on the same link:
their packets carry a prefix of their own, split off in OzIce_GetPacket before id's netcode sees them.
On each link a reliable ordered stream (KCP) carries edits, snapshots and play-test paks as messages,
and fast datagrams carry presence (where each player looks and points) about 20 times a second.
===========================================================================
*/
#ifndef OZ_EDLINK_H
#define OZ_EDLINK_H

#ifdef __cplusplus
extern "C" {
#endif

#define OZ_EDLINK_PREFIX_LEN	4
// a packet of the editing channel: 0xFE 'O' 'z' then 'K' (a KCP segment) or 'F' (a fast message). As a
// netchan header its sequence would be about 1.2 billion packets in, never reached; a connectionless one is 0xFFFFFFFF.
int		OzEdLink_Is( const unsigned char *data, int len );
void	OzEdLink_Input( int slot, int gen, const char *pid, const unsigned char *data, int len );	// from oz_ice.c (libjuice's packets, in the frame)
void	OzEdLink_Up( int slot, int gen, const char *pid, int relayed );	// an editing link connected
void	OzEdLink_Down( int slot, int gen, const char *pid, const char *why );	// it closed
void	OzEdLink_Failed( const char *pid, const char *why );	// a join that never connected
void	OzEdLink_Frame( void );		// KCP's clock, each frame

// The session's side (oz_editor_engine.c's OzEdNet_*):
void	OzEdLink_Send( const char *pid, const void *data, int len, int fast );	// fast: unreliable, at most 1100 bytes
int		OzEdLink_Queued( const char *pid );		// bytes the stream hasn't had acknowledged yet
enum { OZ_EDEV_NONE, OZ_EDEV_MESSAGE, OZ_EDEV_UP, OZ_EDEV_DOWN, OZ_EDEV_FAILED };
// The next event: a message (data: its bytes, *len their count; free with free()), a link up or down,
// a join that failed (data: why, a string). OZ_EDEV_NONE when there's none.
int		OzEdLink_Poll( char *pid, int pidSize, void **data, int *len, int *fast );
void	OzEdLink_Clear( void );		// every link's stream forgotten (the session ended)

// oz_ice.c's side for editing links
qboolean	OzIce_SendSlot( int slot, int gen, const void *data, int len );
void		OzIce_EditHost( const char *name, const char *map, int players, int max, const char *password );	// lists the session (again when it changes)
void		OzIce_EditUnhost( void );
void		OzIce_EditJoin( const char *host, const char *password );
void		OzIce_EditLeave( const char *pid );	// the link with that player closed, the hub told
void		OzIce_EditBrowse( void );			// the hub's listings (net_matches): the sessions are the "edit" ones
const char	*OzIce_EditVersion( void );			// "oz-ed1 <the game's version>": a session takes only its own
int			OzIce_EditRelayed( const char *pid );	// 1: the link to that player goes through the relay

#ifdef __cplusplus
}
#endif

#endif
