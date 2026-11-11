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
// the reason in `error`. A key file that can't be read is kept as <path>.bad and a new identity made:
// that returns 1 with a sentence for the player in `error` ("" otherwise).
int			OzIdentity_Load( const char *path, const char *importFrom, char *error, int errorSize );
const char	*OzIdentity_PublicKey( void );		// base64 DER (SubjectPublicKeyInfo), as the hub's x-key
const char	*OzIdentity_PlayerId( void );		// the first 20 hex digits of SHA-256 of that DER, as the hub's
int			OzIdentity_Sign( const char *text, char *b64, int size );	// base64 RSA-SHA256 PKCS#1 v1.5

// Connects to `base` ("wss://..." or "ws://127.0.0.1:8787"; "/hub" is added), and reconnects with
// backoff until OzOnline_Stop, except when the hub refuses the version (4002). A refused identity (4001:
// the PC's clock was off, a sign-in the hub had seen) is tried again every 30 s.
// `name`, `model` and `color` go in the handshake, as the Godot build sends them.
void		OzOnline_Start( const char *base, const char *name, const char *model, const char *color );
void		OzOnline_Stop( void );
int			OzOnline_Status( char *reason, int size );	// OZ_ONLINE_*, and why it's offline
int			OzOnline_CloseCode( void );					// the hub's close code, 0 if none
int			OzOnline_Generation( void );				// counts the connections that came up: a new one has none of the last one's listing, seat or presence
int			OzOnline_Rtt( void );						// the last ping's round trip, ms
int			OzOnline_Send( const char *json );			// a message (a JSON object with "type"); 0 when not online
int			OzOnline_Receive( char *buf, int size );	// the next message from the hub, 0 when none

// The Oz players in the match being played, as the hub knows them (oz_online_engine.c: the game tells the hub
// its seat, the scoreboard asks for the roster): the scoreboard's player cards (oz_hud.c), their careers, Add friend.
typedef struct {
	char	id[24];			// the hub's player id
	char	name[40];		// the hub's name for them
	int		level;			// their card's level, as their game sent it
	char	card[1024];		// the card's parts, as the hub keeps them (JSON)
	char	stats[512];		// its showcase stats' values (JSON)
} ozSeat_t;

typedef struct {
	int		known;			// 0: the hub has no career for them
	int		level, matches, wins, frags, deaths, seconds;	// the shared career: the matches that counted
	char	weapon[16];		// the weapon with the most frags, "" with none
	int		weaponFrags, weaponShots, weaponHits;
} ozCareer_t;

enum { OZ_REL_NONE, OZ_REL_FRIEND, OZ_REL_SENT, OZ_REL_ASKED };	// SENT: your request, ASKED: theirs

int			OzOnline_Online( void );					// 1: connected to the hub
void		OzOnline_WantRoster( void );				// the scoreboard is up: who's who, asked now and then
int			OzOnline_Seated( int client, const char *name, ozSeat_t *out );	// 1: an Oz player sits there (their name there)
int			OzOnline_Career( const char *id, ozCareer_t *out );	// 1: the hub's answer is in; asked for when it isn't
int			OzOnline_Relation( const char *id );		// OZ_REL_*
void		OzOnline_AddFriend( const char *id );		// a friend request, or yes to theirs
int			OzOnline_Notice( char *out, int size );		// the hub's last notice, and how long ago in ms (-1: none)

// The update check (update.cpp): GitHub's latest release, asked on a thread at launch; the client
// (oz_online_engine.c) compares its tag with OZ_VERSION and offers it in the main menu.
enum { OZ_UPDATE_IDLE, OZ_UPDATE_BUSY, OZ_UPDATE_DONE, OZ_UPDATE_FAILED };
#define OZ_UPDATE_NOTES		6

typedef struct {
	char		tag[64];			// v0.7.6-beta
	char		name[128];			// the release's title
	char		published[16];		// 2026-11-05
	char		page[256];			// its page on GitHub
	char		setup[512];			// OzArenaSetup.exe's download, "" without one
	long long	setupSize, size;	// the installer's bytes; every asset's
	char		setupSha256[72];	// the installer's SHA-256 in hex, "" when the release's JSON gives none
	char		notes[OZ_UPDATE_NOTES][160];	// the body's top-level bullets, plain
	int			numNotes;
	char		error[128];			// why the check failed
} ozRelease_t;

void		OzUpdate_Check( const char *url );				// releases/latest's JSON, on a thread
int			OzUpdate_Checked( ozRelease_t *out );			// OZ_UPDATE_*; out filled once DONE or FAILED
// a file, on a thread: `path` in UTF-8; it must come `size` bytes long (0: not known) with that SHA-256
// ("": not known), or it is removed and the download fails
void		OzUpdate_Download( const char *url, const char *path, long long size, const char *sha256 );
int			OzUpdate_Downloaded( long long *done, long long *total, char *error, int size );	// OZ_UPDATE_*
int			OzUpdate_Compare( const char *a, const char *b );	// versions ("v0.7.6-beta", "0.7.5-beta"): -1, 0, 1
void		OzUpdate_Shutdown( void );						// stops a download, waits for the threads

#ifdef __cplusplus
}
#endif

#endif
