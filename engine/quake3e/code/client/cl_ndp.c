/*
===========================================================================
Copyright (C) 2022-2024 Gian 'myT' Schellenbaum

This file is part of Challenge Quake 3 (CNQ3).

Challenge Quake 3 is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Challenge Quake 3 is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Challenge Quake 3. If not, see <https://www.gnu.org/licenses/>.
===========================================================================
*/
// New Demo Player with fast seeking support
//
// Oz: CNQ3's code/client/cl_demo.cpp (bitbucket.org/CPMADevs/cnq3, ef32da8c), translated to C for
// Quake3e. CPMA turns it on through trap_CNQ3_NDP_Enable when cl_demoPlayer is 1. The design notes
// below are CNQ3's.


/*

GENERAL

When the client loads a demo with the new demo player enabled,
CL_NDP_PlayDemo is called with the demo file opened.
It will then proceed to generate a compressed and seekable encoding
of said demo in memory.
The input demo is read from disk exactly once in the process.

STEP BY STEP BREAKDOWN

Demo loading steps:
- Parse until a gamestate is found.
- Make gamestate config strings available to CGame.
- Create CGame VM and call CGAME_INIT.
	- CGame loads the map, the assets, etc.
	- CGame also starts demo analysis with the gamestate config strings.
- Parse commands and snapshots until there's an error, a new gamestate or there's nothing left to parse.
	Command:
	- Feed it to CGame for analysis.
	Snapshot:
	- Feed it to CGame for analysis.
	- Decide whether this snapshot shall be delta-encoded or not based on server time.
		- If full, ask CGame to generate a list of synchronization server commands.
		- If full, save indexing data to be able to reference this snapshot during playback.
	- Write the snapshot to the memory buffer.
- Close the input file and tell CGame to finish demo analysis.
- Jump to the desired time and read the first 2 snapshots from memory so we're ready for playback.

Demo playback steps:
- Always keep a decoded snapshot for the current server time.
- If not at the end, keep a "next" decoded snapshot so CGame can interpolate.
- Once CGame queries a snapshot that exists,
  generate and add all related commands to the CGame command queue.

Seeking steps:
- Find the first full snapshot before the requested server time.
- Decode 2 snapshots so that "current" and "next" are both set, ready for playback.
- Queue commands and decode more snapshots until the playback time reaches the requested time.

ERROR HANDLING

Standard (forward-only) demo playback can afford to use DROP errors
since it doesn't prevent you from seeing everything that's valid before the bad data.
However, this new demo player needs to parse the full file to generate
a new representation that can be used during playback.
A drop error would therefore completely stop the processing and cancel playback.
The new custom handler allows errors to simply stop the parsing phase earlier.
If a gamestate and at least 1 snapshot were read, then loading can be finalized
and playback can happen as if the demo file was simply truncated.

STORAGE

The encoding lives in memory, not in a disk cache: conversion is fast, and a cache would go
stale whenever the client or CPMA changes or a demo file is replaced under the same name.

MULTIPLE GAMESTATES

Only the first gamestate is loaded; demos with more can be split with UDT.

OZ: REPLAYS AND THE ENGINE CLOCK (ENGINE_PLAN.md P10)

A replay (engine/src/oz/replay) plays through this player too. It needs no parse into the memory
buffer: the replay's frames are read in place of it (ReadNextReplaySnapshot), each built into the
snapshot of the client being watched, and the replay's keyframes are the seek points. Watching
another player is a seek to where playback is.

CPMA's cgame runs the playback clock and asks for seeks itself. Oz's own cgame can't (its game code
calls only ioquake3's traps, so Quake3e and ioquake3 players run it too), so the engine runs the
clock for it, as for a classic demo (timescale, and the replay_* commands' pause, step and seek),
and hands it snapshots the classic way: the newest is the one after the playback time. Before a
seek's first snapshot, a vmMain call (cl_ozDemoSeek, the command number Oz's cgame set at its init)
has the cgame drop everything tied to the old time.

*/


#include "client.h"
#include "../../../src/oz/replay/oz_replay.h"

#include <setjmp.h>
#include <limits.h>
#if defined(_MSC_VER)
#pragma warning(disable: 4611) // interaction between '_setjmp' and C++ object destruction
#endif


#define FULL_SNAPSHOT_INTERVAL_MS (8 * 1000)

#define MAX_COMMANDS 256

#define NDP_Drop(Message, ...)    Com_Error(ERR_DROP, "New Demo Player: " Message "\n", ##__VA_ARGS__)
#define NDP_Error(Message, ...)   do { Com_Printf("ERROR: " Message "\n", ##__VA_ARGS__); longjmp(ndp.abortLoad, -1); } while (0)
#define NDP_Warning(Message, ...) Com_Printf("WARNING: " Message "\n", ##__VA_ARGS__)
// Oz: a call into the cgame (or anything that runs it) while loading; see CL_NDP_HandleError
#define NDP_CGAME(Call)           do { ndp.cgameDepth++; Call; ndp.cgameDepth--; } while (0)
#define NDP_Assert(Expr)          do { if (!(Expr)) NDP_Drop("assertion failed: %s", #Expr); } while (0)


typedef struct {
	int index;
	int serverTime;
} configString_t;

typedef struct {
	char data[128 * 1024];
	int numBytes;
} commandBuffer_t;

typedef struct {
	// the first char of configStrings is '\0'
	// so that empty entries can all use offset 0
	commandBuffer_t serverCommands;
	commandBuffer_t synchCommands;
	char configStrings[MAX_GAMESTATE_CHARS]; // matches the size in gameState_t
	int configStringOffsets[MAX_CONFIGSTRINGS];
	int configStringTimes[MAX_CONFIGSTRINGS]; // last time it was changed
	entityState_t entities[MAX_GENTITIES];
	byte areaMask[MAX_MAP_AREA_BYTES];
	playerState_t ps;
	int numAreaMaskBytes;
	int numConfigStringBytes;
	int messageNum;
	int serverTime;
	int numEntities;
	int numServerCommands;
	int serverCommandSequence;
	int snapFlags;
	int ping;
	qboolean isFullSnap;
	qboolean isServerPaused;
} ndpSnapshot_t;

typedef struct {
	ndpSnapshot_t ndpSnapshots[2]; // current one and previous one for delta encoding
	clSnapshot_t snapshots[PACKET_BACKUP];
	entityState_t entityBaselines[MAX_GENTITIES];
	entityState_t entities[MAX_PARSE_ENTITIES];
	char bigConfigString[BIG_INFO_STRING];
	msg_t inMsg;
	msg_t outMsg;
	ndpSnapshot_t* currSnap;
	ndpSnapshot_t* prevSnap;
	int entityWriteIndex; // indexes entities directly
	int messageNum;       // number of the current msg_t data packet
	int bigConfigStringIndex;
	int prevServerTime;
	int lastMessageNum;
	int nextFullSnapshotTime;  // when server time is bigger, write a full snapshot
	int serverCommandSequence; // the command number of the latest command we decoded
	int progress;
	int numGamestates;
} parser_t;

typedef struct {
	int byteOffset;
	int serverTime;
	int snapshotIndex;
} demoIndex_t;

typedef struct {
	byte* data;
	int capacity; // total number of bytes allocated
	int position; // cursor or number of bytes read/written
	int numBytes; // total number of bytes written for read mode
	qboolean isReadMode;
} memoryBuffer_t;

typedef struct {
	char command[MAX_STRING_CHARS]; // the size used by MSG_ReadString
} command_t;

typedef struct {
	ndpSnapshot_t snapshots[2]; // current one and next one for CGame requests
	demoIndex_t indices[4096];
	command_t commands[MAX_COMMANDS];
	memoryBuffer_t buffer;
	ndpSnapshot_t* currSnap;
	ndpSnapshot_t* nextSnap;
	int numSnapshots;
	int numIndices; // the number of full snapshots (i.e. with no delta-encoding)
	int firstServerTime;
	int lastServerTime;
	int snapshotIndex; // the index of currSnap for CGame requests
	int numCommands;   // number of commands written, must be modulo'd to index commands
	qboolean isLastSnapshot;
} demo_t;

typedef struct {
	jmp_buf abortLoad;
	qboolean loading;     // Oz: the parse phase is running (errors stop it instead of dropping)
	int cgameDepth;       // Oz: calls into the cgame in progress; an error inside one must not longjmp out of the VM
	qboolean unsupported; // Oz: the demo's cgame didn't turn the player on; play it the classic way
	char unsupportedGames[1024]; // Oz: game names known not to support it this session, "\name\name\"
	qboolean mapMissing;  // Oz: the demo's map isn't installed: the classic player downloads it (cl_mapAutoDownload)
	qboolean trackServerPause;
	qboolean isServerPaused;
	int serverPauseDelay; // total duration in server pause since the full snapshot
	char demoPath[MAX_OSPATH]; // Oz: reopened on vid_restart
	int lastDumpTime;     // Oz: the last snapshot cl_demoDump wrote, reset by a seek
	qboolean sought;      // Oz: a seek since the last snapshot went to cl.snap (SetClientSnapshot)
	qboolean classicOnly; // Oz: this demo plays the classic way (a timedemo), without the mod being remembered
	qboolean replay;      // Oz: a replay plays (cl_oz_replay.c), not a demo file
	int replayCursor;     // Oz: the replay's next frame to read
	qboolean engineClock; // Oz: the engine runs the playback clock (Oz's cgame), not the cgame
	qboolean paused;      // Oz: the engine's clock stands (replay_pause, replay_step)
	qboolean atEnd;       // Oz: the engine's clock reached the end
	int queuedIndex;      // Oz: the newest snapshot whose commands went to the cgame (engine clock)
	int resumeTime;       // Oz: where a vid_restart picks up (engine clock), or INT_MIN
	qboolean jumped;      // Oz: a jump this frame: the clock holds, so its first frame shows the time asked for
} newDemoPlayer_t;


static parser_t parser;
static demo_t demo;
static newDemoPlayer_t ndp;
static const entityState_t nullEntityState;
static const playerState_t nullPlayerState;


// smallest time value comes first to keep chronological order
static int CompareConfigStringTimes( const void* aPtr, const void* bPtr )
{
	const configString_t* const a = (const configString_t*)aPtr;
	const configString_t* const b = (const configString_t*)bPtr;
	return a->serverTime - b->serverTime;
}


static void MB_GrowTo( memoryBuffer_t* mb, int numBytes )
{
	if (mb->data && mb->capacity < numBytes) {
		byte* const oldData = mb->data;
		numBytes = MAX(numBytes, mb->capacity * 2);
		mb->data = (byte*)calloc(numBytes, 1);
		if (!mb->data) {
			NDP_Drop("Ran out of memory");
		}
		mb->capacity = numBytes;
		Com_Memcpy(mb->data, oldData, mb->position);
		free(oldData);
	} else if (!mb->data) {
		mb->data = (byte*)calloc(numBytes, 1);
		if (!mb->data) {
			NDP_Drop("Ran out of memory");
		}
		mb->capacity = numBytes;
	}
}


static void MB_InitWrite( memoryBuffer_t* mb )
{
	MB_GrowTo(mb, 4 << 20);
	mb->numBytes = 0;
	mb->position = 0;
	mb->isReadMode = qfalse;
}


static void MB_Write( memoryBuffer_t* mb, const void* data, int numBytes )
{
	NDP_Assert(!mb->isReadMode);
	MB_GrowTo(mb, mb->position + numBytes);
	Com_Memcpy(mb->data + mb->position, data, numBytes);
	mb->position += numBytes;
}


static void MB_InitRead( memoryBuffer_t* mb )
{
	NDP_Assert(mb->position > 0);
	mb->numBytes = mb->position;
	mb->position = 0;
	mb->isReadMode = qtrue;
}


static void MB_InitMessage( memoryBuffer_t* mb, msg_t* inMsg )
{
	const int numBytes = *(int*)(mb->data + mb->position);
	NDP_Assert(numBytes > 0 && numBytes <= MAX_MSGLEN);
	mb->position += 4;
	MSG_Init(inMsg, mb->data + mb->position, numBytes);
	mb->position += numBytes;
	MSG_BeginReading(inMsg);
	inMsg->cursize = numBytes;
}


static void MB_Seek( memoryBuffer_t* mb, int position )
{
	NDP_Assert(mb->isReadMode);
	if (position < 0 || position >= mb->numBytes) {
		NDP_Drop("Seeking out of range");
	}
	mb->position = position;
}


static void WriteNDPSnapshot( msg_t* outMsg, const ndpSnapshot_t* prevSnap, ndpSnapshot_t* currSnap, qboolean isServerPaused )
{
	const qboolean isFullSnap = prevSnap == NULL;
	const playerState_t* oldPS;
	int i;

	// header
	MSG_WriteBits(outMsg, isFullSnap, 1);
	MSG_WriteLong(outMsg, currSnap->serverTime);
	MSG_WriteBits(outMsg, !!isServerPaused, 1);

	// player state
	oldPS = !prevSnap ? &nullPlayerState : &prevSnap->ps;
	MSG_WriteDeltaPlayerstate(outMsg, (playerState_t*)oldPS, &currSnap->ps);

	// area mask
	MSG_WriteByte(outMsg, currSnap->numAreaMaskBytes);
	MSG_WriteData(outMsg, currSnap->areaMask, currSnap->numAreaMaskBytes);

	// server commands
	MSG_WriteLong(outMsg, currSnap->serverCommands.numBytes);
	MSG_WriteData(outMsg, currSnap->serverCommands.data, currSnap->serverCommands.numBytes);

	// synchronization commands
	if (isFullSnap) {
		MSG_WriteLong(outMsg, currSnap->synchCommands.numBytes);
		MSG_WriteData(outMsg, currSnap->synchCommands.data, currSnap->synchCommands.numBytes);
	}

	// config strings
	for (i = 0; i < MAX_CONFIGSTRINGS; ++i) {
		const char* const oldString = !prevSnap ? "" : &prevSnap->configStrings[prevSnap->configStringOffsets[i]];
		const char* const newString = currSnap->configStrings + currSnap->configStringOffsets[i];
		if (strcmp(oldString, newString)) {
			MSG_WriteShort(outMsg, i);
			MSG_WriteLong(outMsg, currSnap->configStringTimes[i]);
			MSG_WriteBigString(outMsg, newString);
		}
	}
	MSG_WriteShort(outMsg, MAX_CONFIGSTRINGS); // end marker

	// entities
	for (i = 0; i < MAX_GENTITIES - 1; ++i) {
		const entityState_t* oldEntity = NULL;
		const entityState_t* newEntity;
		if (prevSnap && prevSnap->entities[i].number < MAX_GENTITIES - 1) {
			oldEntity = &prevSnap->entities[i];
		}

		newEntity = &currSnap->entities[i];
		if (newEntity->number != i || newEntity->number >= MAX_GENTITIES - 1) {
			newEntity = NULL;
		}

		if (newEntity && !oldEntity) {
			oldEntity = &nullEntityState;
		}
		MSG_WriteDeltaEntity(outMsg, oldEntity, newEntity, qfalse);
	}
	MSG_WriteBits(outMsg, MAX_GENTITIES - 1, GENTITYNUM_BITS); // end marker
}


static void SaveConfigString( ndpSnapshot_t* snap, int index, const char* string, int serverTime )
{
	static char cs[sizeof(((ndpSnapshot_t*)0)->configStrings)];
	int numStringBytes, numTotalBytes, i;

	snap->configStringTimes[index] = serverTime;

	if (string[0] == '\0') {
		snap->configStringOffsets[index] = 0;
		return;
	}

	numStringBytes = (int)strlen(string) + 1;
	if (snap->numConfigStringBytes + numStringBytes <= (int)sizeof(snap->configStrings)) {
		// we have enough space
		Com_Memcpy(snap->configStrings + snap->numConfigStringBytes, string, numStringBytes);
		snap->configStringOffsets[index] = snap->numConfigStringBytes;
		snap->numConfigStringBytes += numStringBytes;
		return;
	}

	// we ran out of memory, it's time to compact the buffer
	// we first create a new one locally then copy it into the snapshot
	cs[0] = '\0';
	numTotalBytes = 1;
	for (i = 0; i < (int)ARRAY_LEN(snap->configStringOffsets); ++i) {
		const char* const s = (i == index) ? string : snap->configStrings + snap->configStringOffsets[i];
		if (s[0] == '\0') {
			snap->configStringOffsets[i] = 0;
			continue;
		}

		numStringBytes = (int)strlen(s) + 1;
		if (numTotalBytes + numStringBytes > (int)sizeof(snap->configStrings)) {
			NDP_Warning("Ran out of config string memory");
			break;
		}

		Com_Memcpy(cs + numTotalBytes, s, numStringBytes);
		snap->configStringOffsets[i] = numTotalBytes;
		numTotalBytes += numStringBytes;
	}
	Com_Memcpy(snap->configStrings, cs, numTotalBytes);
	snap->numConfigStringBytes = numTotalBytes;
}


static void SaveCommandString( commandBuffer_t* cmdBuf, const char* string )
{
	const int numBytes = (int)strlen(string) + 1;
	if (cmdBuf->numBytes + numBytes > (int)sizeof(cmdBuf->data)) {
		NDP_Drop("Not enough memory for command data");
	}

	Com_Memcpy(cmdBuf->data + cmdBuf->numBytes, string, numBytes);
	cmdBuf->numBytes += numBytes;
}


static void ParseServerCommand( void )
{
	msg_t* const inMsg = &parser.inMsg;
	const int commandNumber = MSG_ReadLong(inMsg);
	const char* const s = MSG_ReadString(inMsg);

	if (commandNumber <= parser.serverCommandSequence ||
	    parser.numGamestates <= 0) {
		return;
	}
	parser.serverCommandSequence = commandNumber;

	Cmd_TokenizeString(s);

	if (!Q_stricmp(Cmd_Argv(0), "bcs0")) {
		parser.bigConfigStringIndex = atoi(Cmd_Argv(1));
		Q_strncpyz(parser.bigConfigString, Cmd_Argv(2), sizeof(parser.bigConfigString));
	} else if (!Q_stricmp(Cmd_Argv(0), "bcs1")) {
		Q_strcat(parser.bigConfigString, sizeof(parser.bigConfigString), Cmd_Argv(2));
	} else if (!Q_stricmp(Cmd_Argv(0), "bcs2")) {
		Q_strcat(parser.bigConfigString, sizeof(parser.bigConfigString), Cmd_Argv(2));
		// must tokenize again for the mod's benefit
		Cmd_TokenizeString(parser.bigConfigString);
		NDP_CGAME(CL_CGNDP_AnalyzeCommand(parser.prevServerTime));
		SaveConfigString(parser.currSnap, parser.bigConfigStringIndex, parser.bigConfigString, parser.prevServerTime);
	} else if (!Q_stricmp(Cmd_Argv(0), "cs")) {
		// already tokenized, so we're good to go
		NDP_CGAME(CL_CGNDP_AnalyzeCommand(parser.prevServerTime));
		SaveConfigString(parser.currSnap, atoi(Cmd_Argv(1)), Cmd_ArgsFrom(2), parser.prevServerTime);
	} else {
		// already tokenized, so we're good to go
		NDP_CGAME(CL_CGNDP_AnalyzeCommand(parser.prevServerTime));
		SaveCommandString(&parser.currSnap->serverCommands, s);
	}
}


static void StartGame( const ndpSnapshot_t* currSnap );


static void ParseGamestate( void )
{
	msg_t* const inMsg = &parser.inMsg;
	ndpSnapshot_t* const currSnap = parser.currSnap;

	parser.serverCommandSequence = MSG_ReadLong(inMsg);

	for (;;) {
		const int cmd = MSG_ReadByte(inMsg);
		if (cmd == svc_EOF) {
			break;
		} else if (cmd == svc_configstring) {
			const int index = MSG_ReadShort(inMsg);
			const char* const string = MSG_ReadBigString(inMsg);
			if (index < 0 || index >= MAX_CONFIGSTRINGS) {
				NDP_Error("Invalid config string index: %d", index);
			}
			SaveConfigString(currSnap, index, string, -10000 + index + 1);
		} else if (cmd == svc_baseline) {
			const int index = MSG_ReadBits(inMsg, GENTITYNUM_BITS);
			if (index < 0 || index >= MAX_GENTITIES) {
				NDP_Error("Invalid baseline index: %d", index);
			}
			MSG_ReadDeltaEntity(inMsg, &nullEntityState, &parser.entityBaselines[index], index);
		} else {
			NDP_Error("Invalid gamestate command byte: %d", cmd);
		}
	}

	clc.clientNum = MSG_ReadLong(inMsg);
	clc.checksumFeed = MSG_ReadLong(inMsg);

	StartGame(currSnap); // Oz: shared with replays
}


/*
Oz: the gamestate's configstrings go to the client, and the cgame starts (ParseGamestate's end, also
a replay's start). It needs the new demo player's interface (CPMA's) or Oz's seek reset (cl_ozDemoSeek),
for which the engine runs the clock.
*/
static void StartGame( const ndpSnapshot_t* currSnap )
{
	// start up CGame now because we need it running for the command parser
	clc.demoplaying = qtrue;
	clc.newDemoPlayer = qtrue;
	clc.serverMessageSequence = 0;
	clc.lastExecutedServerCommand = 0;
	Con_Close();
	CL_ClearState();
	cls.state = CA_LOADING;
	Com_EventLoop();
	Cvar_Set("r_uiFullScreen", "0");
	CL_FlushMemory();
	Com_Memcpy(cl.gameState.stringOffsets, currSnap->configStringOffsets, sizeof(cl.gameState.stringOffsets));
	Com_Memcpy(cl.gameState.stringData, currSnap->configStrings, currSnap->numConfigStringBytes);
	cl.gameState.dataCount = currSnap->numConfigStringBytes;
	{
		// Oz: starting the cgame loads the map; without it, hand the demo to the classic player,
		// whose gamestate check downloads missing maps
		const char *info = cl.gameState.stringData + cl.gameState.stringOffsets[CS_SERVERINFO];
		if (FS_FOpenFileRead(va("maps/%s.bsp", Info_ValueForKey(info, "mapname")), NULL, qfalse) == -1) {
			ndp.mapMissing = qtrue;
			ndp.unsupported = qtrue;
			longjmp(ndp.abortLoad, -1);
		}
	}
	NDP_CGAME(CL_InitCGame());
	if (!cls.cgameNewDemoPlayer && !cls.cgameOzDemoSeek) {
		// Oz: CPMA's cgame and Oz's support it; CL_NDP_StartDemo falls back to the classic player
		ndp.unsupported = qtrue;
		longjmp(ndp.abortLoad, -1);
	}
	if (!cls.cgameNewDemoPlayer && com_timedemo->integer && !ndp.replay) {
		// Oz: a timedemo's fixed steps are the classic player's
		ndp.unsupported = qtrue;
		ndp.classicOnly = qtrue;
		longjmp(ndp.abortLoad, -1);
	}
	ndp.engineClock = !cls.cgameNewDemoPlayer;
	cls.cgameStarted = qtrue;
	cls.state = CA_ACTIVE;
}


static void ParseSnapshot( void )
{
	static int lastRefreshTime;
	msg_t* const inMsg = &parser.inMsg;
	const int byteOffset = demo.buffer.position;
	const int newSnapIndex = parser.messageNum & PACKET_MASK;
	clSnapshot_t* oldSnap = NULL;
	clSnapshot_t* newSnap = &parser.snapshots[newSnapIndex];
	const int currServerTime = MSG_ReadLong(inMsg);
	int numAreaMaskBytes;
	entityState_t* oldstate;
	entityState_t* newstate;
	int oldnum, newnum;
	int oldindex = 0;
	qboolean isFullSnap = qfalse, isServerPaused;
	ndpSnapshot_t* currNDPSnap;
	ndpSnapshot_t* prevNDPSnap;
	ndpSnapshot_t* nextNDPSnap;
	byte outMsgData[MAX_MSGLEN];
	msg_t writeMsg;
	int i;

	Com_Memset(newSnap, 0, sizeof(*newSnap));
	newSnap->deltaNum = MSG_ReadByte(inMsg);
	newSnap->messageNum = parser.messageNum;
	newSnap->snapFlags = MSG_ReadByte(inMsg);

	numAreaMaskBytes = MSG_ReadByte(inMsg);
	if (numAreaMaskBytes > (int)sizeof(newSnap->areamask)) {
		NDP_Error("Invalid area mask size: %d", numAreaMaskBytes);
	}

	if (newSnap->deltaNum == 0) {
		// uncompressed data
		newSnap->deltaNum = -1;
		newSnap->valid = qtrue;
	} else {
		newSnap->deltaNum = newSnap->messageNum - newSnap->deltaNum;
		oldSnap = &parser.snapshots[newSnap->deltaNum & PACKET_MASK];
		if (!oldSnap->valid) {
			NDP_Error("The snapshot deltas against nothing");
		} else if (oldSnap->messageNum != newSnap->deltaNum) {
			NDP_Warning("Delta frame too old");
		} else if (parser.entityWriteIndex - oldSnap->parseEntitiesNum > MAX_PARSE_ENTITIES - 128) {
			NDP_Warning("Delta parseEntitiesNum too old");
		} else {
			newSnap->valid = qtrue;
		}
	}

	MSG_ReadData(inMsg, newSnap->areamask, numAreaMaskBytes);

	MSG_ReadDeltaPlayerstate(inMsg, oldSnap ? &oldSnap->ps : NULL, &newSnap->ps);

	newSnap->parseEntitiesNum = parser.entityWriteIndex;
	newSnap->numEntities = 0;

	newnum = MSG_ReadBits(inMsg, GENTITYNUM_BITS);
	for (;;) {
		if (oldSnap && oldindex < oldSnap->numEntities) {
			// we still haven't dealt with all entities from oldSnap
			const int index = (oldSnap->parseEntitiesNum + oldindex) & (MAX_PARSE_ENTITIES - 1);
			oldstate = &parser.entities[index];
			oldnum = oldstate->number;
		} else {
			// we're done with all entities in oldSnap
			oldstate = NULL;
			oldnum = INT_MAX;
		}

		newstate = &parser.entities[parser.entityWriteIndex];
		if (!oldstate && newnum == MAX_GENTITIES - 1) {
			// we're done with all entities in oldSnap AND got the exit signal
			break;
		} else if (oldnum < newnum) {
			// the old entity isn't present in the new list
			*newstate = *oldstate;
			oldindex++;
		} else if (oldnum == newnum) {
			// the entity changed
			MSG_ReadDeltaEntity(inMsg, oldstate, newstate, newnum);
			newnum = MSG_ReadBits(inMsg, GENTITYNUM_BITS);
			oldindex++;
		} else if (oldnum > newnum) {
			// this is a new entity that was delta'd from the baseline
			MSG_ReadDeltaEntity(inMsg, &parser.entityBaselines[newnum], newstate, newnum);
			newnum = MSG_ReadBits(inMsg, GENTITYNUM_BITS);
		}

		if (newstate->number == MAX_GENTITIES - 1) {
			continue;
		}

		parser.entityWriteIndex = (parser.entityWriteIndex + 1) & (MAX_PARSE_ENTITIES - 1);
		newSnap->numEntities++;
	}

	if (!newSnap->valid || parser.numGamestates <= 0) {
		return;
	}

	// mark missing snapshots as invalid
	if (newSnap->messageNum - parser.lastMessageNum >= PACKET_BACKUP) {
		parser.lastMessageNum = newSnap->messageNum - (PACKET_BACKUP - 1);
	}
	while (parser.lastMessageNum < newSnap->messageNum) {
		parser.snapshots[parser.lastMessageNum++ & PACKET_MASK].valid = qfalse;
	}
	parser.lastMessageNum = newSnap->messageNum + 1;

	// some servers reset the server time
	// some snapshots are to be ignored entirely at the start of a demo
	if (currServerTime < parser.prevServerTime ||
	    (newSnap->snapFlags & SNAPFLAG_NOT_ACTIVE) != 0) {
		// ignore all previous snapshots and the current one too
		parser.prevServerTime = currServerTime;
		parser.lastMessageNum = newSnap->messageNum + 1;
		demo.firstServerTime = INT_MAX;
		demo.lastServerTime = INT_MIN;
		demo.buffer.numBytes = 0;
		demo.numCommands = 0;
		demo.numSnapshots = 0;
		demo.numIndices = 0;
		return;
	}

	if (demo.numSnapshots == 0 || currServerTime > parser.nextFullSnapshotTime) {
		isFullSnap = qtrue;
		parser.nextFullSnapshotTime = currServerTime + FULL_SNAPSHOT_INTERVAL_MS;
	}

	// update our custom snapshot data structure
	currNDPSnap = parser.currSnap;
	for (i = 0; i < MAX_GENTITIES; ++i) {
		currNDPSnap->entities[i].number = MAX_GENTITIES - 1;
	}
	for (i = 0; i < newSnap->numEntities; ++i) {
		const int index = (newSnap->parseEntitiesNum + i) & (MAX_PARSE_ENTITIES - 1);
		const entityState_t* const ent = &parser.entities[index];
		currNDPSnap->entities[ent->number] = *ent;
	}
	currNDPSnap->isFullSnap = isFullSnap;
	currNDPSnap->ps = newSnap->ps;
	currNDPSnap->serverTime = currServerTime;
	currNDPSnap->numAreaMaskBytes = numAreaMaskBytes;
	Com_Memcpy(currNDPSnap->areaMask, newSnap->areamask, sizeof(currNDPSnap->areaMask));

	// add all synchronization commands
	if (isFullSnap) {
		const char* synchCommands;
		int numSynchCommandBytes;
		int synchCommandStart = 0;
		NDP_CGAME(CL_CGNDP_GenerateCommands(&synchCommands, &numSynchCommandBytes));
		while (synchCommandStart < numSynchCommandBytes) {
			const char* const cmd = synchCommands + synchCommandStart;
			SaveCommandString(&currNDPSnap->synchCommands, cmd);
			synchCommandStart += (int)strlen(cmd) + 1;
		}
	}

	// let CGame analyze the snapshot so it can do cool stuff
	// e.g. store events of interest for the timeline overlay
	demo.currSnap = currNDPSnap;
	NDP_CGAME(isServerPaused = CL_CGNDP_AnalyzeSnapshot(parser.progress));

	// draw the current progress once in a while...
	if (Sys_Milliseconds() > lastRefreshTime + 100) {
		NDP_CGAME(SCR_UpdateScreen());
		lastRefreshTime = Sys_Milliseconds();
	}

	// write it out (delta-)compressed
	MSG_Init(&writeMsg, outMsgData, sizeof(outMsgData));
	MSG_Clear(&writeMsg);
	MSG_Bitstream(&writeMsg);
	prevNDPSnap = parser.prevSnap;
	WriteNDPSnapshot(&writeMsg, isFullSnap ? NULL : prevNDPSnap, currNDPSnap, isServerPaused);
	MB_Write(&demo.buffer, &writeMsg.cursize, 4);
	MB_Write(&demo.buffer, writeMsg.data, writeMsg.cursize);

	// prepare the next snapshot
	nextNDPSnap = prevNDPSnap;
	nextNDPSnap->isFullSnap = qfalse;
	nextNDPSnap->serverTime = 0;
	nextNDPSnap->serverCommands.numBytes = 0;
	nextNDPSnap->synchCommands.numBytes = 0;
	Com_Memcpy(nextNDPSnap->configStrings, currNDPSnap->configStrings, currNDPSnap->numConfigStringBytes);
	Com_Memcpy(nextNDPSnap->configStringOffsets, currNDPSnap->configStringOffsets, sizeof(nextNDPSnap->configStringOffsets));
	Com_Memcpy(nextNDPSnap->configStringTimes, currNDPSnap->configStringTimes, sizeof(nextNDPSnap->configStringTimes));
	nextNDPSnap->numConfigStringBytes = currNDPSnap->numConfigStringBytes;
	for (i = 0; i < MAX_GENTITIES; ++i) {
		nextNDPSnap->entities[i].number = MAX_GENTITIES - 1;
	}

	// parser bookkeeping
	parser.prevSnap = currNDPSnap;
	parser.currSnap = nextNDPSnap;
	parser.prevServerTime = currServerTime;

	// build the playback index table
	if (isFullSnap) {
		demoIndex_t* index;
		if (demo.numIndices >= (int)ARRAY_LEN(demo.indices)) {
			NDP_Error("Ran out of indices");
		}
		index = &demo.indices[demo.numIndices++];
		index->byteOffset = byteOffset;
		index->snapshotIndex = demo.numSnapshots;
		index->serverTime = currServerTime;
	}
	demo.numSnapshots++;
	demo.firstServerTime = MIN(demo.firstServerTime, currServerTime);
	demo.lastServerTime = MAX(demo.lastServerTime, currServerTime);
}


static void ParseDemo( void )
{
	static byte oldData[MAX_MSGLEN];
	msg_t* const msg = &parser.inMsg;
	const fileHandle_t fh = clc.demofile;
	int fileLength, i;
	memoryBuffer_t buffer = demo.buffer; // keep the allocation between demos

	Com_Memset(&parser, 0, sizeof(parser));
	parser.currSnap = &parser.ndpSnapshots[0];
	parser.prevSnap = &parser.ndpSnapshots[1];
	parser.currSnap->numConfigStringBytes = 1; // first char is always '\0'

	Com_Memset(&demo, 0, sizeof(demo));
	demo.buffer = buffer;
	demo.firstServerTime = INT_MAX;
	demo.lastServerTime = INT_MIN;
	demo.currSnap = &demo.snapshots[0];
	demo.nextSnap = &demo.snapshots[1];

	for (i = 0; i < MAX_CONFIGSTRINGS; ++i) {
		parser.ndpSnapshots[0].configStringTimes[i] = INT_MIN;
		parser.ndpSnapshots[1].configStringTimes[i] = INT_MIN;
		demo.snapshots[0].configStringTimes[i] = INT_MIN;
		demo.snapshots[1].configStringTimes[i] = INT_MIN;
	}

	MB_InitWrite(&demo.buffer);

	// the progress bar needs the file's length (FS_FTell stays 0 inside a pak, and so does the bar)
	fileLength = FS_FOpenFileRead(ndp.demoPath, NULL, qtrue);

	for (;;) {
		parser.progress = fileLength > 0 ? ((100 * FS_FTell(fh)) / fileLength) : 0;
		parser.progress = MIN(MAX(parser.progress, 0), 99);

		MSG_Init(msg, oldData, sizeof(oldData));

		if (FS_Read(&parser.messageNum, 4, fh) != 4) {
			NDP_Error("File read: message number");
		}

		if (FS_Read(&msg->cursize, 4, fh) != 4) {
			NDP_Error("File read: message size");
		}
		if (msg->cursize < 0) {
			// valid EOF marker
			break;
		} else if (msg->cursize > msg->maxsize) {
			NDP_Error("Message too large: %d", msg->cursize);
		}

		if (FS_Read(msg->data, msg->cursize, fh) != msg->cursize) {
			NDP_Error("File read: message data");
		}

		MSG_BeginReading(msg);
		MSG_ReadLong(msg); // ignore sequence number

		for (;;) {
			byte cmd;
			if (msg->readcount > msg->cursize) {
				NDP_Error("Read past end of message");
			}

			cmd = MSG_ReadByte(msg);
			if (cmd == svc_EOF) {
				break;
			}

			switch (cmd) {
				case svc_nop:
					break;

				case svc_gamestate:
					++parser.numGamestates;
					if (parser.numGamestates >= 2) {
						NDP_Warning("More than 1 gamestate found, only the first one was loaded");
						return;
					}
					ParseGamestate();
					break;

				case svc_snapshot:
					ParseSnapshot();
					break;

				case svc_serverCommand:
					ParseServerCommand();
					break;

				case svc_download:
				default:
					NDP_Error("Invalid command byte");
					break;
			}
		}
	}
}


/*
Oz: a replay's frame as the snapshot of the client watched (CL_OzReplay_View): its playerState, the
entities a snapshot for it would carry (every one: the replay has no PVS; not its own body, nor what
went to someone else only), the areas seen from its eyes, every configstring, and the frame's commands
that reached it. In a frame without that client, the first one in it.
*/
static void FillReplaySnapshot( ndpSnapshot_t* snap, const ozReplayFrame_t* f, int frame )
{
	const char* cmds[MAX_COMMANDS];
	byte areas[MAX_MAP_AREA_BYTES];
	int view = CL_OzReplay_View(), self, n, i, t;

	if (view < 0 || view >= MAX_CLIENTS || !f->psPresent[view]) {
		for (view = 0; view < MAX_CLIENTS && !f->psPresent[view]; view++) {
		}
	}
	snap->isFullSnap = CL_OzReplay_IsKeyframe(frame);
	snap->isServerPaused = qfalse;
	snap->serverTime = f->time;
	snap->messageNum = frame;
	if (view < MAX_CLIENTS) {
		snap->ps = f->ps[view];
	}
	self = snap->ps.clientNum;	// a spectator following someone has their playerState

	for (i = 0; i < MAX_GENTITIES; ++i) {
		snap->entities[i].number = MAX_GENTITIES - 1;
	}
	for (i = 0; i < MAX_GENTITIES - 1; ++i) {
		if (!f->present[i] || i == self ||
		    (f->filter[i] == OZ_REPLAY_FILTER_SINGLE && f->filterClient[i] != self) ||
		    (f->filter[i] == OZ_REPLAY_FILTER_NOTSINGLE && f->filterClient[i] == self)) {
			continue;
		}
		snap->entities[i] = f->ents[i];
	}

	// as SV_BuildClientSnapshot: the areas connected to the eyes' one, inverted into the renderer's mask
	Com_Memset(snap->areaMask, 0, sizeof(snap->areaMask));
	snap->numAreaMaskBytes = 0;
	if (CM_NumClusters() > 0) {
		vec3_t eye;
		VectorCopy(snap->ps.origin, eye);
		eye[2] += snap->ps.viewheight;
		Com_Memset(areas, 0, sizeof(areas));
		n = CM_WriteAreaBits(areas, CM_LeafArea(CM_PointLeafnum(eye)));
		n = MIN(n, (int)sizeof(areas));
		for (i = 0; i < n; ++i) {
			snap->areaMask[i] = areas[i] ^ 255;
		}
		snap->numAreaMaskBytes = n;
	}

	snap->configStrings[0] = '\0';
	snap->numConfigStringBytes = 1;
	for (i = 0; i < MAX_CONFIGSTRINGS; ++i) {
		const char* const s = CL_OzReplay_ConfigString(i, &t);
		SaveConfigString(snap, i, s, t);
	}

	snap->serverCommands.numBytes = 0;
	snap->synchCommands.numBytes = 0;
	n = view < MAX_CLIENTS ? CL_OzReplay_Commands(frame, view, cmds, MAX_COMMANDS) : 0;
	for (i = 0; i < n; ++i) {
		SaveCommandString(&snap->serverCommands, cmds[i]);
	}
}


// Oz: ReadNextSnapshot for a replay: its next frame, decoded
static void ReadNextReplaySnapshot( void )
{
	const ozReplayFrame_t* f;
	ndpSnapshot_t* dest;

	if (ndp.replayCursor >= CL_OzReplay_Frames()) {
		demo.isLastSnapshot = qtrue;
		return;
	}
	f = CL_OzReplay_Decode(ndp.replayCursor);
	if (!f) {
		NDP_Warning("The replay is damaged at frame %d; it ends there", ndp.replayCursor);
		demo.isLastSnapshot = qtrue;
		return;
	}
	demo.isLastSnapshot = qfalse;
	dest = demo.currSnap;
	demo.currSnap = demo.nextSnap;
	demo.nextSnap = dest;
	FillReplaySnapshot(dest, f, ndp.replayCursor);
	ndp.replayCursor++;
	demo.snapshotIndex++;
}


static void ReadNextSnapshot( void )
{
	static qboolean csSet[MAX_CONFIGSTRINGS];
	static qboolean entSet[MAX_GENTITIES];
	msg_t inMsg;
	ndpSnapshot_t* currSnap;
	ndpSnapshot_t* prevSnap;
	qboolean isFullSnap;
	int prevIndex = -1;
	int i;

	if (ndp.replay) {
		ReadNextReplaySnapshot(); // Oz
		return;
	}

	if (demo.buffer.position >= demo.buffer.numBytes) {
		demo.isLastSnapshot = qtrue;
		return;
	}
	demo.isLastSnapshot = qfalse;

	MB_InitMessage(&demo.buffer, &inMsg);

	currSnap = demo.currSnap; // destination
	prevSnap = demo.nextSnap; // source
	demo.currSnap = prevSnap;
	demo.nextSnap = currSnap;

	// header
	isFullSnap = MSG_ReadBits(&inMsg, 1);
	if (isFullSnap) {
		prevSnap = NULL;
	}
	currSnap->isFullSnap = isFullSnap;
	currSnap->serverTime = MSG_ReadLong(&inMsg);
	currSnap->isServerPaused = MSG_ReadBits(&inMsg, 1);

	// player state
	MSG_ReadDeltaPlayerstate(&inMsg, prevSnap ? &prevSnap->ps : (playerState_t*)&nullPlayerState, &currSnap->ps);

	// area mask
	currSnap->numAreaMaskBytes = MSG_ReadByte(&inMsg);
	MSG_ReadData(&inMsg, currSnap->areaMask, currSnap->numAreaMaskBytes);

	// server commands
	currSnap->serverCommands.numBytes = MSG_ReadLong(&inMsg);
	MSG_ReadData(&inMsg, currSnap->serverCommands.data, currSnap->serverCommands.numBytes);

	// synchronization commands
	if (isFullSnap) {
		currSnap->synchCommands.numBytes = MSG_ReadLong(&inMsg);
		MSG_ReadData(&inMsg, currSnap->synchCommands.data, currSnap->synchCommands.numBytes);
	}

	// config strings
	currSnap->configStrings[0] = '\0';
	currSnap->numConfigStringBytes = 1;
	if (isFullSnap) {
		// if we have a full snapshot, the config strings from the message are all we need
		// we can therefore safely leave everything else as empty (i.e. offset 0)
		Com_Memset(currSnap->configStringOffsets, 0, sizeof(currSnap->configStringOffsets));
		Com_Memset(currSnap->configStringTimes, 0, sizeof(currSnap->configStringTimes));
	}
	Com_Memset(csSet, 0, sizeof(csSet));
	for (;;) {
		const int index = MSG_ReadShort(&inMsg);
		int serverTime;
		const char* cs;
		if (index == MAX_CONFIGSTRINGS) {
			break;
		}
		if (index < 0 || index > MAX_CONFIGSTRINGS) {
			NDP_Drop("Bad config string index %d", index);
		}

		serverTime = MSG_ReadLong(&inMsg);
		cs = MSG_ReadBigString(&inMsg);
		SaveConfigString(currSnap, index, cs, serverTime);
		csSet[index] = qtrue;
	}
	if (!isFullSnap) {
		// we need to keep the unchanged config strings
		for (i = 0; i < MAX_CONFIGSTRINGS; ++i) {
			if (!csSet[i]) {
				const char* const cs = prevSnap->configStrings + prevSnap->configStringOffsets[i];
				const int serverTime = prevSnap->configStringTimes[i];
				SaveConfigString(currSnap, i, cs, serverTime);
			}
		}
	}

	// entities
	if (isFullSnap) {
		// mark them all as invalid now
		for (i = 0; i < MAX_GENTITIES; ++i) {
			currSnap->entities[i].number = MAX_GENTITIES - 1;
		}
	}
	Com_Memset(entSet, 0, sizeof(entSet));
	for (;;) {
		const int index = MSG_ReadBits(&inMsg, GENTITYNUM_BITS);
		const entityState_t* prevEnt = &nullEntityState;
		entityState_t* currEnt;
		NDP_Assert(index > prevIndex);
		prevIndex = index;
		if (index == MAX_GENTITIES - 1) {
			break;
		}
		currEnt = &currSnap->entities[index];
		if (prevSnap && prevSnap->entities[index].number == index) {
			prevEnt = &prevSnap->entities[index];
		}
		MSG_ReadDeltaEntity(&inMsg, prevEnt, currEnt, index);
		entSet[index] = qtrue;
	}
	if (!isFullSnap) {
		// copy over old valid entities and mark missing ones as invalid
		for (i = 0; i < MAX_GENTITIES - 1; ++i) {
			if (entSet[i]) {
				continue;
			}
			if (prevSnap->entities[i].number == i) {
				currSnap->entities[i] = prevSnap->entities[i];
			} else {
				currSnap->entities[i].number = MAX_GENTITIES - 1;
			}
		}
	}

	if (ndp.trackServerPause) {
		if (currSnap->isServerPaused && ndp.isServerPaused && prevSnap != NULL) {
			const int delta = currSnap->serverTime - prevSnap->serverTime;
			ndp.serverPauseDelay += delta;
		}
		ndp.isServerPaused = currSnap->isServerPaused;
	}

	demo.snapshotIndex++;
}


static int PeekNextSnapshotTime( void )
{
	return demo.nextSnap != NULL ? demo.nextSnap->serverTime : INT_MIN;
}


static void AddCommand( const char* string )
{
	const int index = ++demo.numCommands % MAX_COMMANDS;
	Q_strncpyz(demo.commands[index].command, string, sizeof(demo.commands[index].command));
}


static void AddAllCommands( const commandBuffer_t* cmdBuf )
{
	int cmdStart = 0;
	while (cmdStart < cmdBuf->numBytes) {
		const char* const cmd = cmdBuf->data + cmdStart;
		AddCommand(cmd);
		cmdStart += (int)strlen(cmd) + 1;
	}
}


static void SeekToIndex( int seekIndex )
{
	static configString_t cs[MAX_CONFIGSTRINGS];
	const demoIndex_t* demoIndex;
	ndpSnapshot_t* syncSnap;
	int numCS = 0, i;

	if (seekIndex < 0) {
		NDP_Warning("Attempted to seek out of range");
		seekIndex = 0;
	} else if (seekIndex >= demo.numIndices) {
		seekIndex = demo.numIndices - 1;
	}

	demoIndex = &demo.indices[seekIndex];
	if (ndp.replay) {
		ndp.replayCursor = demoIndex->byteOffset; // Oz: a replay's keyframe, by its frame number
	} else {
		MB_Seek(&demo.buffer, demoIndex->byteOffset);
	}
	ReadNextSnapshot(); // 1st in next, ??? in current
	ReadNextSnapshot(); // 2nd in next, 1st in current
	demo.snapshotIndex = demoIndex->snapshotIndex;

	syncSnap = demo.currSnap;
	if (!demo.currSnap->isFullSnap) {
		// this is needed when trying to jump past the end
		syncSnap = demo.nextSnap;
	}
	NDP_Assert(syncSnap->isFullSnap);

	// queue special config string changes as commands
	// this will correct the red/blue scores, the game timer, etc.
	for (i = 0; i < MAX_CONFIGSTRINGS; ++i) {
		if (syncSnap->configStringTimes[i] != INT_MIN &&
		    CL_CGNDP_IsConfigStringNeeded(i)) {
			cs[numCS].index = i;
			cs[numCS].serverTime = syncSnap->configStringTimes[i];
			numCS++;
		}
	}

	// sort them so they're executed in the right order
	// (it matters a great deal due to old CPMA mechanisms for network bandwidth optimization)
	qsort(cs, numCS, sizeof(configString_t), &CompareConfigStringTimes);
	for (i = 0; i < numCS; ++i) {
		const int csIndex = cs[i].index;
		const char* const csValue = syncSnap->configStrings + syncSnap->configStringOffsets[csIndex];
		AddCommand(va("cs %d \"%s\"", csIndex, csValue));
	}

	// add special state synchronization commands
	// this will correct item timers, make sure the scoreboard can be shown, etc.
	AddAllCommands(&syncSnap->synchCommands);
}


void CL_NDP_PlayDemo( qboolean videoRestart )
{
	const int startTime = Sys_Milliseconds();
	int fileSize, duration;

	ndp.loading = qtrue;
	ndp.cgameDepth = 0;
	ndp.unsupported = qfalse;
	ndp.mapMissing = qfalse;
	if (setjmp(ndp.abortLoad) == 0) {
		// read from file and write to memory
		ParseDemo();
	}
	ndp.loading = qfalse;
	if (ndp.unsupported) {
		return; // Oz: CL_NDP_StartDemo plays it the classic way
	}

	fileSize = MAX(FS_FTell(clc.demofile), 1); // FS_FTell doesn't work when the demo is in a pak file
	FS_FCloseFile(clc.demofile);
	clc.demofile = FS_INVALID_HANDLE;
	if (!clc.demoplaying || !clc.newDemoPlayer) {
		NDP_Drop("Failed to parse the gamestate message");
	}
	if (demo.numIndices <= 0 || demo.numSnapshots <= 0) {
		NDP_Warning("Failed to get anything out of the demo file");
		return;
	}

	// prepare to read from memory
	demo.currSnap = &demo.snapshots[0];
	demo.nextSnap = &demo.snapshots[1];
	MB_InitRead(&demo.buffer);

	// make sure we don't execute commands from the end of the demo when starting up
	demo.commands[0].command[0] = '\0';
	demo.commands[1].command[0] = '\0';
	demo.numCommands = 1;

	// finalize CGame load and set any extra info needed now
	// CGame will also restore previous state when videoRestart is qtrue
	CL_CGNDP_EndAnalysis(clc.demoName, demo.firstServerTime, demo.lastServerTime, videoRestart);

	// seek to make sure we're synchronized as if the user had manually asked for said time
	if (ndp.engineClock) {
		// Oz: the engine's clock starts there, and the cgame starts from that snapshot
		CL_NDP_Jump(videoRestart && ndp.resumeTime != INT_MIN ? ndp.resumeTime : demo.firstServerTime);
	} else {
		CL_NDP_Seek(videoRestart ? demo.currSnap->serverTime : demo.firstServerTime);
	}

	duration = Sys_Milliseconds() - startTime;
	Com_Printf("New Demo Player: loaded demo in %d.%03d seconds\n", duration / 1000, duration % 1000);
	Com_DPrintf("New Demo Player: I-frame delay %d ms, %d -> %d bytes (%.2fx)\n",
	            FULL_SNAPSHOT_INTERVAL_MS, fileSize, demo.buffer.numBytes, (float)demo.buffer.numBytes / (float)fileSize);
}


/*
====================
CL_NDP_StartDemo

Oz: CL_PlayDemo_f hands the opened demo here when cl_demoPlayer is on; the path is kept so that
vid_restart can load it again (CL_NDP_VideoRestart). Oz plays every mod's demos, and only
CPMA's cgame supports this player: when the demo's cgame doesn't turn it on, the cgame is shut
down, the game name remembered for the session, and qfalse returned with the demo reopened, so
the caller plays it the classic way.
====================
*/
qboolean CL_NDP_StartDemo( const char *path )
{
	const char *game = Cvar_VariableString("fs_game");
	const char *key = va("\\%s\\", game[0] ? game : BASEGAME);

	ndp.mapMissing = qfalse;
	ndp.classicOnly = qfalse;	// Oz
	ndp.replay = qfalse;
	ndp.paused = qfalse;
	ndp.atEnd = qfalse;
	ndp.resumeTime = INT_MIN;
	if (strstr(ndp.unsupportedGames, key)) {
		return qfalse;
	}

	Q_strncpyz(ndp.demoPath, path, sizeof(ndp.demoPath));
	CL_NDP_PlayDemo(qfalse);
	if (!ndp.unsupported) {
		return qtrue;
	}

	// classic playback from the start of the file
	if (ndp.mapMissing) {
		// Oz: not the mod's fault; the next demo with its map installed tries this player again
		Com_Printf("New Demo Player: the map isn't installed, playing the demo the classic way\n");
	} else if (ndp.classicOnly) {
		// Oz: a timedemo; the mod's next demo tries this player again
	} else if (strlen(ndp.unsupportedGames) + strlen(key) < sizeof(ndp.unsupportedGames)) {
		if (!ndp.unsupportedGames[0]) {
			Q_strncpyz(ndp.unsupportedGames, key, sizeof(ndp.unsupportedGames));
		} else {
			Q_strcat(ndp.unsupportedGames, sizeof(ndp.unsupportedGames), key + 1);
		}
	}
	CL_ShutdownCGame();
	clc.newDemoPlayer = qfalse;
	cls.cgameStarted = qfalse;
	FS_FCloseFile(clc.demofile);
	clc.demofile = FS_INVALID_HANDLE;
	if (FS_FOpenFileRead(path, &clc.demofile, qtrue) == -1 || clc.demofile == FS_INVALID_HANDLE) {
		NDP_Drop("couldn't reopen %s", path);
	}
	clc.serverMessageSequence = 0;
	clc.lastExecutedServerCommand = 0;
	cls.state = CA_CONNECTED;
	clc.demoplaying = qtrue;
	return qfalse;
}


/*
====================
CL_NDP_MapMissing

Oz: the demo's path when CL_NDP_StartDemo last stepped aside because its map isn't installed (the
classic player downloads it, and CL_DemoMapDownloaded then plays the demo here again), else NULL
====================
*/
const char *CL_NDP_MapMissing( void )
{
	return ndp.mapMissing ? ndp.demoPath : NULL;
}


/*
====================
CL_NDP_VideoRestart

Oz: as CNQ3 does, vid_restart loads the demo again (the cgame restores its own state)
====================
*/
static qboolean LoadReplay( qboolean videoRestart );

void CL_NDP_VideoRestart( void )
{
	ndp.resumeTime = ndp.engineClock ? cl.serverTime : INT_MIN; // Oz: where the engine's clock was
	if (ndp.replay) {
		// Oz: the replay is still in memory (cl_oz_replay.c)
		clc.demoplaying = qtrue;
		cls.state = CA_CONNECTED;
		if (!LoadReplay(qtrue)) {
			NDP_Drop("couldn't start the replay again");
		}
		return;
	}
	if (FS_FOpenFileRead(ndp.demoPath, &clc.demofile, qtrue) == -1 || clc.demofile == FS_INVALID_HANDLE) {
		NDP_Drop("couldn't reopen %s", ndp.demoPath);
	}
	clc.demoplaying = qtrue;
	cls.state = CA_CONNECTED;
	CL_NDP_PlayDemo(qtrue);
}


/*
=============================================================================

Oz: replays, and the engine's clock (ENGINE_PLAN.md P10)

=============================================================================
*/

// the gamestate from the replay's first frame, and its keyframes as the seek points
static void ParseReplay( void )
{
	memoryBuffer_t buffer = demo.buffer; // keep the allocation between demos
	ndpSnapshot_t* snap;
	int i, t, keys = 0, step, n = 0;

	Com_Memset(&parser, 0, sizeof(parser));
	parser.currSnap = &parser.ndpSnapshots[0];
	parser.prevSnap = &parser.ndpSnapshots[1];
	Com_Memset(&demo, 0, sizeof(demo));
	demo.buffer = buffer;
	demo.currSnap = &demo.snapshots[0];
	demo.nextSnap = &demo.snapshots[1];
	for (i = 0; i < MAX_CONFIGSTRINGS; ++i) {
		demo.snapshots[0].configStringTimes[i] = INT_MIN;
		demo.snapshots[1].configStringTimes[i] = INT_MIN;
	}

	if (!CL_OzReplay_Decode(0)) {
		NDP_Error("The replay's first frame is damaged");
	}
	snap = parser.currSnap;
	snap->configStrings[0] = '\0';
	snap->numConfigStringBytes = 1;
	for (i = 0; i < MAX_CONFIGSTRINGS; ++i) {
		const char* const s = CL_OzReplay_ConfigString(i, &t);
		SaveConfigString(snap, i, s, t);
	}
	clc.clientNum = CL_OzReplay_View();
	clc.checksumFeed = 0;
	StartGame(snap);

	// every keyframe, or every second one and so on when there are more than the index holds
	for (i = 0; i < CL_OzReplay_Frames(); ++i) {
		keys += CL_OzReplay_IsKeyframe(i);
	}
	step = keys / (int)ARRAY_LEN(demo.indices) + 1;
	for (i = 0; i < CL_OzReplay_Frames(); ++i) {
		if (CL_OzReplay_IsKeyframe(i) && n++ % step == 0) {
			demoIndex_t* const index = &demo.indices[demo.numIndices++];
			index->byteOffset = i;
			index->serverTime = CL_OzReplay_FrameTime(i);
			index->snapshotIndex = i;
		}
	}
	demo.numSnapshots = CL_OzReplay_Frames();
	demo.firstServerTime = CL_OzReplay_FrameTime(0);
	demo.lastServerTime = CL_OzReplay_FrameTime(demo.numSnapshots - 1);
}


static qboolean LoadReplay( qboolean videoRestart )
{
	const int startTime = Sys_Milliseconds();

	ndp.loading = qtrue;
	ndp.cgameDepth = 0;
	ndp.unsupported = qfalse;
	ndp.mapMissing = qfalse;
	ndp.classicOnly = qfalse;
	ndp.replay = qtrue;
	ndp.paused = qfalse;
	ndp.atEnd = qfalse;
	ndp.demoPath[0] = '\0';
	if (setjmp(ndp.abortLoad) == 0) {
		ParseReplay();
	}
	ndp.loading = qfalse;
	if (ndp.unsupported || !clc.newDemoPlayer || demo.numIndices <= 0) {
		Com_Printf("replay: %s\n", ndp.mapMissing ? "its map isn't installed" :
		           ndp.unsupported ? "the game code running can't play replays" : "nothing in it plays");
		return qfalse;
	}

	demo.currSnap = &demo.snapshots[0];
	demo.nextSnap = &demo.snapshots[1];
	demo.commands[0].command[0] = '\0';
	demo.commands[1].command[0] = '\0';
	demo.numCommands = 1;
	CL_CGNDP_EndAnalysis(clc.demoName, demo.firstServerTime, demo.lastServerTime, videoRestart);
	if (ndp.engineClock) {
		CL_NDP_Jump(videoRestart && ndp.resumeTime != INT_MIN ? ndp.resumeTime : demo.firstServerTime);
	} else {
		CL_NDP_Seek(videoRestart && ndp.resumeTime != INT_MIN ? ndp.resumeTime : demo.firstServerTime);
	}
	Com_Printf("New Demo Player: loaded replay in %d ms, %d frames, %d seek points\n",
	           Sys_Milliseconds() - startTime, demo.numSnapshots, demo.numIndices);
	return qtrue;
}


/*
====================
CL_NDP_StartReplay

Oz: plays the replay cl_oz_replay.c has open (its replay command set up the client as for a demo)
====================
*/
qboolean CL_NDP_StartReplay( void )
{
	ndp.resumeTime = INT_MIN;
	return LoadReplay(qfalse);
}


// Oz: the engine runs the playback clock (Oz's cgame), and a demo or replay is playing
qboolean CL_NDP_EngineClock( void )
{
	return clc.demoplaying && clc.newDemoPlayer && ndp.engineClock && cls.state == CA_ACTIVE;
}


void CL_NDP_TimeRange( int* first, int* last )
{
	*first = demo.firstServerTime;
	*last = demo.lastServerTime;
}


// Oz: the commands of the current snapshot when the cgame never asked for it (playback passed it in one frame)
static void QueueSkippedCommands( void )
{
	if (demo.snapshotIndex > ndp.queuedIndex) {
		AddAllCommands(&demo.currSnap->serverCommands);
		ndp.queuedIndex = demo.snapshotIndex;
	}
}


/*
====================
CL_NDP_Jump

Oz: the engine clock's seek: the snapshot at or before serverTime and the one after it, and the cgame
told to start over from them (it drops what it had of the old time). Returns the time playback is at.
====================
*/
int CL_NDP_Jump( int serverTime )
{
	if (!clc.newDemoPlayer || !ndp.engineClock || demo.numIndices <= 0) {
		return cl.serverTime;
	}
	// short of the last snapshot: when it's a keyframe, a seek there would have no snapshot after it
	serverTime = MAX(MIN(serverTime, demo.lastServerTime - 1), demo.firstServerTime);
	CL_NDP_Seek(serverTime);
	ndp.queuedIndex = demo.snapshotIndex - 1;
	ndp.atEnd = qfalse;
	ndp.jumped = qtrue;
	cl.serverTime = MAX(serverTime, demo.currSnap->serverTime);
	S_ClearSoundBuffer();
	if (cgvm && cls.cgameOzDemoSeek) {
		VM_Call(cgvm, 2, cls.cgameOzDemoSeek, cl.serverTime, demo.snapshotIndex);
	}
	return cl.serverTime;
}


// Oz: -2 asks, -1 toggles, 0 and 1 set; returns whether the engine's clock stands
qboolean CL_NDP_Pause( int on )
{
	if (on >= -1) {
		ndp.paused = on == -1 ? !ndp.paused : !!on;
		Com_Printf("replay: %s at %d\n", ndp.paused ? "paused" : "playing", cl.serverTime);
	}
	return ndp.paused;
}


// Oz: paused, to the next snapshot's time
void CL_NDP_Step( void )
{
	ndp.paused = qtrue;
	if (!demo.isLastSnapshot) {
		cl.serverTime = PeekNextSnapshotTime();
	}
	Com_Printf("replay: stepped to %d\n", cl.serverTime);
}


void CL_NDP_SetCGameTime( void )
{
	// make sure we don't get timed out by CL_CheckTimeout
	clc.lastPacketTime = cls.realtime;

	// Oz: the engine's clock, as a classic demo's: the frame's time (timescale scales it, 0 stops it)
	if (!ndp.engineClock || cls.state != CA_ACTIVE) {
		return;
	}
	if (ndp.jumped) {
		ndp.jumped = qfalse;
	} else if (!ndp.paused && com_timescale->value != 0.0f) {
		cl.serverTime += cls.frametime;
	}
	if (cl.serverTime >= demo.lastServerTime) {
		cl.serverTime = demo.lastServerTime;
		if (!ndp.atEnd) {
			ndp.atEnd = qtrue;
			Com_Printf("replay: the end, at %d\n", cl.serverTime);
		}
	}
	while (!demo.isLastSnapshot && PeekNextSnapshotTime() <= cl.serverTime) {
		QueueSkippedCommands();
		ReadNextSnapshot();
	}
}


void CL_NDP_GetCurrentSnapshotNumber( int* snapshotNumber, int* serverTime )
{
	if (ndp.engineClock) {
		// Oz: as the classic player, the newest snapshot is the one after the playback time. At the end it's
		// the last one, still in nextSnap (it never becomes currSnap), and its number never goes down, which
		// a classic cgame can't take.
		*snapshotNumber = demo.snapshotIndex + 1;
		*serverTime = demo.nextSnap->serverTime;
		return;
	}
	*snapshotNumber = demo.snapshotIndex;
	*serverTime = demo.currSnap->serverTime;
}


/*
====================
SetClientSnapshot

Oz: the engine's HUD (oz/ui/oz_hud.c) reads the latest snapshot from cl.snap and cl.parseEntities,
as in a classic demo, which this player leaves empty (CL_ClearState). So the snapshots it hands the
cgame go there too: the newest one (the cgame asks for each once, as it becomes the next), or the
first one after a seek, which may be older.
====================
*/
static void SetClientSnapshot( int snapshotNumber, const snapshot_t* snapshot, int areaBytes )
{
	int i;

	if (cl.snap.valid && snapshotNumber <= cl.snap.messageNum && !ndp.sought) {
		return;
	}
	ndp.sought = qfalse;

	cl.snap.valid = qtrue;
	cl.snap.snapFlags = snapshot->snapFlags;
	cl.snap.serverTime = snapshot->serverTime;
	cl.snap.messageNum = snapshotNumber;
	cl.snap.deltaNum = -1;
	cl.snap.ping = 0;
	cl.snap.areabytes = areaBytes;
	Com_Memcpy(cl.snap.areamask, snapshot->areamask, sizeof(cl.snap.areamask));
	cl.snap.ps = snapshot->ps;
	cl.snap.serverCommandNum = snapshot->serverCommandSequence;
	cl.snap.numEntities = snapshot->numEntities;
	cl.snap.parseEntitiesNum = cl.parseEntitiesNum;
	for (i = 0; i < snapshot->numEntities; ++i) {
		cl.parseEntities[cl.parseEntitiesNum++ & (MAX_PARSE_ENTITIES - 1)] = snapshot->entities[i];
	}
}


qboolean CL_NDP_GetSnapshot( int snapshotNumber, snapshot_t* snapshot )
{
	static configString_t cs[MAX_CONFIGSTRINGS];
	const int playbackIndex = demo.snapshotIndex;
	ndpSnapshot_t* ndpSnap;
	int prevNumCommands, numCS = 0, i;

	// we don't give anything until CGame init is done
	if (!cls.cgameStarted || cls.state != CA_ACTIVE) {
		return qfalse;
	}

	// we only allow CGame to get the current snapshot and the next one
	if (snapshotNumber < playbackIndex ||
	    snapshotNumber > playbackIndex + 1) {
		return qfalse;
	}

	// there is no next snapshot when we're already at the end...
	// (Oz: under the engine's clock, nextSnap still holds the last one, and it's handed out)
	if (demo.isLastSnapshot && snapshotNumber == playbackIndex + 1 && !ndp.engineClock) {
		return qfalse;
	}

	ndpSnap = snapshotNumber == playbackIndex ? demo.currSnap : demo.nextSnap;

	// Oz: cl_demoDump lists each snapshot played once (the cgame asks for the current and the next
	// one), not the load's analysis pass
	if (!ndp.loading && ndpSnap->serverTime > ndp.lastDumpTime) {
		ndp.lastDumpTime = ndpSnap->serverTime;
		CL_DemoDump(ndpSnap->serverTime, &ndpSnap->ps);
	}

	Com_Memcpy(snapshot->areamask, ndpSnap->areaMask, sizeof(snapshot->areamask));
	Com_Memcpy(&snapshot->ps, &ndpSnap->ps, sizeof(snapshot->ps));
	snapshot->snapFlags = 0;
	snapshot->ping = 0;
	snapshot->numServerCommands = 0;
	snapshot->serverCommandSequence = demo.numCommands;
	snapshot->serverTime = ndpSnap->serverTime;
	snapshot->numEntities = 0;

	// copy over all valid entities
	for (i = 0; i < MAX_GENTITIES - 1 && snapshot->numEntities < MAX_ENTITIES_IN_SNAPSHOT; ++i) {
		if (ndpSnap->entities[i].number == i) {
			snapshot->entities[snapshot->numEntities++] = ndpSnap->entities[i];
		}
	}

	prevNumCommands = demo.numCommands;

	// Oz: under the engine's clock, a snapshot's commands go once (the cgame asks for each snapshot once,
	// but a seek's first may have been passed already), and with it, as a network client gets them
	if (ndp.engineClock) {
		if (snapshotNumber <= ndp.queuedIndex) {
			goto queued;
		}
		ndp.queuedIndex = snapshotNumber;
	}

	// add config string changes as commands
	for (i = 0; i < MAX_CONFIGSTRINGS; ++i) {
		const char* const csOld = cl.gameState.stringData + cl.gameState.stringOffsets[i];
		const char* const csNew = ndpSnap->configStrings + ndpSnap->configStringOffsets[i];
		if (strcmp(csOld, csNew)) {
			cs[numCS].index = i;
			cs[numCS].serverTime = ndpSnap->configStringTimes[i];
			numCS++;
		}
	}
	qsort(cs, numCS, sizeof(configString_t), &CompareConfigStringTimes);
	for (i = 0; i < numCS; ++i) {
		const int index = cs[i].index;
		const char* const csNew = ndpSnap->configStrings + ndpSnap->configStringOffsets[index];
		AddCommand(va("cs %d \"%s\"", index, csNew));
	}

	// add commands
	AddAllCommands(&ndpSnap->serverCommands);

	// make sure nothing is missing
	if (demo.numCommands - prevNumCommands > MAX_COMMANDS) {
		NDP_Warning("Too many commands for a single snapshot");
	}

queued:
	if (ndp.engineClock) {
		snapshot->serverCommandSequence = demo.numCommands;
	}

	if (!ndp.loading) {
		SetClientSnapshot(snapshotNumber, snapshot, ndpSnap->numAreaMaskBytes);
	}

	return qtrue;
}


qboolean CL_NDP_GetServerCommand( int serverCommandNumber )
{
	const char* cmd;

	if (serverCommandNumber < demo.numCommands - MAX_COMMANDS ||
	    serverCommandNumber > demo.numCommands) {
		return qfalse;
	}

	cmd = demo.commands[serverCommandNumber % MAX_COMMANDS].command;
	Cmd_TokenizeString(cmd);

	if (!strcmp(Cmd_Argv(0), "cs")) {
		CL_ConfigstringModified();
		// reparse the string, because CL_ConfigstringModified may have done another Cmd_TokenizeString()
		Cmd_TokenizeString(cmd);
	}

	return qtrue;
}


int CL_NDP_Seek( int serverTime )
{
	const int seekStartTime = Sys_Milliseconds();
	int index = 0, numSnapshotsRead = 2, i;

	// figure out which full snapshot we want to jump to
	if (serverTime >= demo.lastServerTime) {
		index = demo.numIndices - 1;
	} else if (serverTime <= 0) {
		index = 0;
	} else {
		index = demo.numIndices - 1;
		for (i = 0; i < demo.numIndices; ++i) {
			if (demo.indices[i].serverTime > serverTime) {
				index = MAX(i - 1, 0);
				break;
			}
		}
	}

	// clear all pending commands before adding new ones
	for (i = 0; i < (int)ARRAY_LEN(demo.commands); ++i) {
		demo.commands[i].command[0] = '\0';
	}

	ndp.trackServerPause = qtrue;
	ndp.isServerPaused = qfalse;
	ndp.serverPauseDelay = 0;
	ndp.lastDumpTime = INT_MIN; // Oz
	ndp.sought = qtrue; // Oz: SetClientSnapshot takes the next snapshot, even an older one

	SeekToIndex(index);

	// read more snapshots until we're close to the target time for more precise jumps
	// we want the the closest snapshot whose time is less or equal to the target time
	while (!demo.isLastSnapshot) {
		const int nextServerTime = PeekNextSnapshotTime();
		if (nextServerTime == INT_MIN || nextServerTime > serverTime) {
			break;
		}
		// add all commands from the snapshot we're about to evict
		AddAllCommands(&demo.currSnap->serverCommands);
		ReadNextSnapshot();
		numSnapshotsRead++;
	}

	if (ndp.serverPauseDelay > 0) {
		AddCommand(va("server_pause_delay %d", ndp.serverPauseDelay));
	}
	ndp.trackServerPause = qfalse;

	Com_DPrintf("New Demo Player: sought and read %d snaps in %d ms\n",
	            numSnapshotsRead, Sys_Milliseconds() - seekStartTime);

	cl.serverTime = demo.currSnap->serverTime; // Oz: until the cgame reads on from there (CL_NDP_ReadUntil)
	return demo.currSnap->serverTime;
}


void CL_NDP_ReadUntil( int serverTime )
{
	cl.serverTime = serverTime; // Oz: the cgame's playback clock, which it reads up to every frame (for the HUD)
	while (!demo.isLastSnapshot && demo.currSnap->serverTime < serverTime) {
		ReadNextSnapshot();
	}
}


void CL_NDP_HandleError( void )
{
	// we use our handler for read errors during load
	// read/write errors during playback aren't handled
	if (!ndp.loading) {
		return;
	}
	// Oz: an error inside a cgame call (CNQ3 only stops loading on demo read errors, ERR_DROP_NDP):
	// it drops normally, unwinding past the parse, so the jump target is gone after this
	if (ndp.cgameDepth > 0) {
		ndp.loading = qfalse;
		ndp.cgameDepth = 0;
		return;
	}
	if (clc.newDemoPlayer && clc.demofile != FS_INVALID_HANDLE) {
		longjmp(ndp.abortLoad, -1);
	}
}
