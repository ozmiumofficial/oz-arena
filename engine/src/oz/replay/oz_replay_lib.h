/*
===========================================================================
Oz Arena: the replay library (ENGINE_PLAN.md P10 steps 4 and 6). GPL-2.0.

Two kinds of recordings, one list (the Replays page, the Profile's Recent matches):
  - a match on a server running in this game (your bot matches, a match you host) is recorded whole by
    that server (sv_oz_replay.c): <fs_homepath>/replays/*.q3replay, every player, any view;
  - a match on someone else's server (a community server, a friend's peer-to-peer match) can't be:
    only that server sees every player. The client records what it receives instead, your own view
    as a .dm_68 demo (cl_oz_demos.c): <fs_homepath>/<mod>/demos/<date>_<time>_<map>_<mode>.dm_68.

replays/library.json holds what isn't in the files: your names and favorites (the Godot build's file,
which the server's trim reads too: "favorite" stays each entry's first key), and for the demos Oz
recorded, the match as it ended (map, mode, server, the standings, your place) and the hub's match
number once its report was stored, which links the Profile's rows to their recordings.
  key	a replay's file name ("2026-11-08_100801_q3dm17_ffa.q3replay"), or a demo's path under the
		user folder ("oz/demos/2026-11-08_101500_q3dm17_duel.dm_68")
===========================================================================
*/
#ifndef OZ_REPLAY_LIB_H
#define OZ_REPLAY_LIB_H

#ifdef __cplusplus
extern "C" {
#endif

struct cJSON;

#define OZ_LIB_ROWS		64

// a scoreboard row as the HUD has it (oz_hud.c OzHud_Standings)
typedef struct {
	int		client, score, team, bot;
	char	name[36];
} ozLibRow_t;

typedef struct {
	int			numRows;			// best first, spectators out; 0 before the server sent a scoreboard
	ozLibRow_t	rows[OZ_LIB_ROWS];
	int			me;					// your client number
	int			gametype;
	char		mode[32];			// as the board's title names it ("DUEL", "FREEZE TAG")
	int			teams;				// a team game with both teams' scores
	int			red, blue;
} ozLibStandings_t;
int		OzHud_Standings( ozLibStandings_t *s );	// oz_hud.c: how many rows

// what a demo's own data says (any demo, Oz's or not): its gamestate and its first and last snapshots
typedef struct {
	char	map[64];
	char	hostname[128];
	char	game[64];				// the mod, "" for baseq3
	int		gametype;
	int		clientNum;				// whose view
	int		firstTime, lastTime;	// server times, ms
	int		complete;				// ends as a recording should (not cut off by a crash)
	int		numPlayers;
	char	players[OZ_LIB_ROWS][36];
	int		playerClient[OZ_LIB_ROWS];
} ozDemoInfo_t;

// the client's side (cl_oz_demos.c)
void		CL_OzDemo_Init( void );
void		CL_OzDemo_Frame( void );				// CL_Frame: starts recording a match on another server
int			CL_OzDemo_StopAtGamestate( void );		// a new gamestate (the next map): stop the recording Oz started
void		CL_OzDemo_Stopped( const char *finalName );	// CL_StopRecord_f: the demo's file, under the mod's folder
void		CL_OzLib_MatchRecorded( const char *rid, int won, int place );	// oz_stats.c: the match's report made
void		CL_OzLib_MatchStored( const char *rid, int match );		// the hub stored it as that match

// the library, for the pages (oz_ui_replays.cpp, oz_ui_profile.cpp)
const char	*OzLib_Home( void );					// fs_homepath
struct cJSON *OzLib_Read( void );					// the whole file (an object, never NULL): cJSON_Delete it
void		OzLib_Set( const char *key, const char *field, struct cJSON *value );	// value taken; NULL removes the field
void		OzLib_Forget( const char *key );
void		OzLib_ForgetMany( const char *const *keys, int count );	// in one write
int			OzLib_Broken( void );					// the file was there and didn't read this session (kept as library.bad.json): nothing is trimmed
int			OzLib_Path( const char *key, char *out, int size );	// the file's OS path, 0 for a key that isn't one
int			OzLib_Delete( const char *key, char *why, int whySize );	// 2: to the Recycle Bin, 1: removed, 0: why says
int			OzLib_Recycles( void );					// a delete goes to the Recycle Bin (Windows, the player's own folder)
int			OzDemo_Info( const char *osPath, ozDemoInfo_t *info );	// 0 if it isn't a demo
int			OzLib_IsDemoName( const char *name );	// .dm_66 to .dm_91
const char	*OzLib_Recording( void );				// the demo key being recorded, "" for none
int			OzLib_Keep( void );						// the newest kept of each kind (sv_ozReplayKeep)
const char	*OzLib_CurrentReplay( void );			// the local server's replay being written, "" for none

#ifdef __cplusplus
}
#endif

#endif
