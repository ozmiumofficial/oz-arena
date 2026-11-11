/*
===========================================================================
Oz Arena: replays (ENGINE_PLAN.md P10). GPL-2.0.

The Godot build's q3core replays (its tools/q3core/src/engine/q3_replay.c), ported to the engine: the
server records every match (sv_oz_replay.c), the client plays one through CNQ3's new demo player
(cl_oz_replay.c, cl_ndp.c), from any player's view.

A .q3replay file is the Godot build's, so its replays play here as they are (P14):
  "Q3RP", u32 version (1), u32 header length, the header (JSON: map, mode, date, ...)
  frame blocks: u32 payload length, u8 kind (0 delta, 1 keyframe), i32 server time, the payload
  the trailer (JSON: frames, length, standings), u32 its length, "Q3RT"
A file the server didn't finish has no trailer and plays to its last whole frame.

A payload is msg.c's bit stream, as a network client gets it (Huffman, deltas):
  short n, then n x (short index, short length, bytes)   configstrings that changed
  short n, then n x (long mask0, long mask1, short length, bytes)   commands, with the clients they reached
  long mask0, long mask1, then a playerState delta per client in the masks (from the frame before, or zero)
  entity deltas as SV_EmitPacketEntities writes them (from the frame before, or zero), ended by
    MAX_GENTITIES - 1
  short n, then n x (short entity, byte kind, byte client)   SVF_SINGLECLIENT (1) / SVF_NOTSINGLECLIENT (2)
  long OZ_REPLAY_FRAME_END
A keyframe deltas from nothing (its configstrings are still only the ones that changed: the first frame
holds them all), so playback can start at any keyframe.
===========================================================================
*/
#ifndef OZ_REPLAY_H
#define OZ_REPLAY_H

#define OZ_REPLAY_EXT			"q3replay"
#define OZ_REPLAY_DIR			"replays"		// in fs_homepath, whichever mod runs (as profile/)
#define OZ_REPLAY_VERSION		1
#define OZ_REPLAY_MAGIC			"Q3RP"
#define OZ_REPLAY_TRAILER_MAGIC	"Q3RT"
#define OZ_REPLAY_BLOCK_HEADER	9
#define OZ_REPLAY_FRAME_END		0x51335245		// "Q3RE"
#define OZ_REPLAY_HZ			40
#define OZ_REPLAY_KEYFRAME_MSEC	2000
#define OZ_REPLAY_FILTER_SINGLE		1
#define OZ_REPLAY_FILTER_NOTSINGLE	2

// one decoded frame: everything a snapshot could carry for anyone
typedef struct {
	int				time;
	byte			present[MAX_GENTITIES];
	entityState_t	ents[MAX_GENTITIES];
	byte			filter[MAX_GENTITIES];
	byte			filterClient[MAX_GENTITIES];
	byte			psPresent[MAX_CLIENTS];
	playerState_t	ps[MAX_CLIENTS];
} ozReplayFrame_t;

// the server (sv_oz_replay.c)
void SV_OzReplayFrame( void );					// after the server's game frames, every SV_Frame
void SV_OzReplayStop( const char *why );		// a map ends: the server shuts down or loads another
void SV_OzReplayRestart( void );				// map_restart, before it: after an intermission the next match gets its own file
const char *SV_OzReplayCurrent( void );			// the file being written, "" for none

// the client (cl_oz_replay.c)
void CL_OzReplay_Init( void );
qboolean CL_OzReplay_Playing( void );
int CL_OzReplay_Frames( void );
int CL_OzReplay_FrameTime( int frame );
qboolean CL_OzReplay_IsKeyframe( int frame );
const ozReplayFrame_t *CL_OzReplay_Decode( int frame );	// frame after frame, or from a keyframe; NULL on bad data
const char *CL_OzReplay_ConfigString( int index, int *changedTime );	// as of the last frame decoded
int CL_OzReplay_Commands( int frame, int client, const char **out, int max );	// the frame's commands to that client
int CL_OzReplay_View( void );					// whose eyes: the client number playback shows
void CL_OzReplay_Close( void );

#endif
