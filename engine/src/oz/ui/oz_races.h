/*
Oz: Defrag's race maps for Skirmish (oz_races.c), as the native UI's C++ reads them. Plain C types only.
*/
#ifndef OZ_RACES_H
#define OZ_RACES_H

#ifdef __cplusplus
extern "C" {
#endif

// the weapons lying on a course: the ones a run jumps with
#define OZ_RACE_ROCKET		1
#define OZ_RACE_PLASMA		2
#define OZ_RACE_GRENADE		4
#define OZ_RACE_BFG			8

typedef struct {
	char	map[64];		// the BSP's name, as map takes it
	char	name[96];		// Defrag's map info's long name (scripts/<map>.defi), else ""
	char	author[64];		// the map info's, else the worldspawn's author, else ""
	char	style[32];		// the map info's style (run, training, accuracy...), else ""
	char	shot[80];		// "/levelshots/<map>" where Oz's game finds the map's level shot, else ""
	char	where[256];		// the pak (or loose BSP) it's in
	int		checkpoints;
	int		length;			// units: the start, the checkpoints nearest first, the finish, in straight lines
	int		weapons;		// OZ_RACE_*
	int		teleports;
	int		hurts;			// trigger_hurt
	int		lava;			// lava or slime somewhere in the map
	int		slick;			// slick floors somewhere in the map
} ozRace_t;

typedef enum {
	OZR_IDLE,		// never looked
	OZR_LOOKING,	// reading the paks that are new or changed since the last look
	OZR_DONE
} ozRacesState_t;

int			OzRaces_Step( int msec );		// looks for up to msec (a slice a frame): 1 while not done
void		OzRaces_Again( void );			// look again (a mod was imported): only new or changed paks are read
ozRacesState_t OzRaces_State( int *done, int *total );	// the paks read of those to read
int			OzRaces_Generation( void );		// changes whenever the list does
int			OzRaces_Count( void );
const ozRace_t *OzRaces_Get( int index );

#ifdef __cplusplus
}
#endif

#endif
