/*
Oz: the stats recorder's match record (ENGINE_PLAN.md P8b steps 5 and 6): what the HUD hands oz_stats.c
at a match's end, which keeps it until the hub's Stats object has it.
*/
#ifndef OZ_STATS_H
#define OZ_STATS_H

#include "../ui/oz_progress.h"	// ozCounts_t

#define OZ_RECORD_ROWS	( MAX_CLIENTS + 2 )

typedef struct {
	int		client, score, team, bot;
	char	name[36];
} ozRecordRow_t;

typedef struct {
	const char		*map;
	int				gametype;
	int				start;				// CS_LEVEL_START_TIME: the server's, the same for everyone in the match
	int				seconds;
	int				me;					// your client number
	int				numRows;
	ozRecordRow_t	rows[OZ_RECORD_ROWS];	// the final scoreboard, best first, spectators out; team games end with
										// the team scores as clients -1 (red) and -2 (blue)
	int				place, won, score, frags, deaths, xp, railStreak, distance, speed;
	ozCounts_t		fragsWeapon, deathsWeapon;	// your frags and deaths by weapon, or by what killed you without one
	ozCounts_t		shots, hits, damage;		// by weapon (hits and damage from Oz's game's dmg; the rail's hits from PERS_HITS)
	ozCounts_t		pickups, medals;
} ozMatchRecord_t;

void	OzStats_Init( void );
void	OzStats_Record( const ozMatchRecord_t *r );
void	OzStats_Online( void );		// the hub connection came up: the pending reports go again
void	OzStats_Frame( void );		// while online: the pending reports go to the hub, one at a time
struct cJSON;
void	OzStats_HubMessage( const char *type, const struct cJSON *m );

#endif
