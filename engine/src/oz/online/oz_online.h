/*
===========================================================================
Oz Arena: online services (ENGINE_PLAN.md P7). GPL-2.0.

The hub is the Godot build's (its online/ folder: a Cloudflare Worker and Durable Object), unchanged:
one WebSocket per player, JSON messages with a "type" (online/src/hub.ts lists them). There are no
accounts: the player is an RSA-2048 key (identity.cpp), and every connection carries the public key,
a millisecond timestamp and an RSA-SHA256 PKCS#1 v1.5 signature of "hub|<ts>".

hub.cpp keeps the connection on a thread of its own (libcurl's WebSocket API, Schannel TLS):
reconnecting with backoff, a ping every 10 s, messages queued both ways. The client
(oz_online_engine.c) runs it from its frame.
===========================================================================
*/
#ifndef OZ_ONLINE_H
#define OZ_ONLINE_H

#ifdef __cplusplus
extern "C" {
#endif

enum { OZ_ONLINE_OFFLINE, OZ_ONLINE_CONNECTING, OZ_ONLINE_ONLINE };

// The player's key: `path` (made with its folder if missing), first imported from `importFrom` when
// that file exists (the Godot build's user://online/identity.key), else generated. 0 on failure, with
// the reason in `error`.
int			OzIdentity_Load( const char *path, const char *importFrom, char *error, int errorSize );
const char	*OzIdentity_PublicKey( void );		// base64 DER (SubjectPublicKeyInfo), as the hub's x-key
const char	*OzIdentity_PlayerId( void );		// the first 20 hex digits of SHA-256 of that DER, as the hub's
int			OzIdentity_Sign( const char *text, char *b64, int size );	// base64 RSA-SHA256 PKCS#1 v1.5

// Connects to `base` ("wss://..." or "ws://127.0.0.1:8787"; "/hub" is added), and reconnects with
// backoff until OzOnline_Stop, except when the hub refuses the identity (4001) or the version (4002).
// `name`, `model` and `color` go in the handshake, as the Godot build sends them.
void		OzOnline_Start( const char *base, const char *name, const char *model, const char *color );
void		OzOnline_Stop( void );
int			OzOnline_Status( char *reason, int size );	// OZ_ONLINE_*, and why it's offline
int			OzOnline_CloseCode( void );					// the hub's close code, 0 if none
int			OzOnline_Rtt( void );						// the last ping's round trip, ms
int			OzOnline_Send( const char *json );			// a message (a JSON object with "type"); 0 when not online
int			OzOnline_Receive( char *buf, int size );	// the next message from the hub, 0 when none

#ifdef __cplusplus
}
#endif

#endif
