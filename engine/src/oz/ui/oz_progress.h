/*
Oz: single player and progression (ENGINE_PLAN.md P12), the C side's view of oz_progress.cpp: the
career's XP and level, the cards the HUD draws, a match's end, the campaign's postgame, and the career
the hub keeps (oz_online_engine.c).
*/
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// a card as the HUD draws it: images by their game path ("" for none), colors as #rrggbb
typedef struct {
	char	banner[64];		// ui/cards/banners/<id> (4:1) or levelshots/<map> (4:3)
	int		bannerWide;		// 1: the banner is 4:1 card art, 0: a 4:3 level shot
	char	emblem[64];
	char	title[64];
	char	accent[8];
	char	frame[8];		// the frame's main color
	int		level;			// 0: not shown
} ozCardLook_t;

// counts by name, as the career's stats key them (catalog.json stats_doc: frags_weapon.railgun, pickups.quad ...)
#define OZ_MATCH_COUNTS	24
typedef struct {
	int		num;
	char	key[OZ_MATCH_COUNTS][16];
	int		n[OZ_MATCH_COUNTS];
} ozCounts_t;

// what the end of a match was, as the HUD sees it at the intermission (gameplay.gd's _show_results)
#define OZ_MATCH_SCORES	64
typedef struct {
	const char	*map;
	int			gametype;			// g_gametype (2 is the campaign's single player)
	int			numScores;			// the standings, best first, without spectators
	int			scores[OZ_MATCH_SCORES];
	int			place;				// the local player's index in them
	int			team;				// 1 red, 2 blue
	int			teamScores[2];
	int			impressive, excellent, gauntlet, defend, assist, captures, killed;	// the playerState's counts
	const char	*model, *skin;
	int			seconds;
	int			topSpeed;			// ups, the player's own movement's (the HUD's TopSpeed)
	// what the Godot build never recorded (the stats recorder, ENGINE_PLAN.md P8b step 5), from the HUD's match state
	ozCounts_t	fragsWeapon;		// your frags by weapon (gauntlet ... bfg)
	ozCounts_t	pickups;			// quad, regen, haste, battlesuit, invis, flight, mega, armor_red, armor_yellow, medkit, teleporter
	ozCounts_t	deathsBy;			// lava, slime, fall, drown, telefrag, crush
	int			railStreak;			// the most rail hits in a row
	int			railShots, railHits;
} ozMatchEnd_t;

#define OZ_XP_LINES	12
typedef struct {
	int		numLines;
	char	lines[OZ_XP_LINES][48];
	int		amounts[OZ_XP_LINES];
	int		before, gain;
	int		numUnlocked;
	char	unlocked[4][64];		// "Banner: The Longest Yard"
	int		campaign;				// 1: a campaign arena
} ozAward_t;

int		OzProgress_LevelFor( int xp, int *into, int *next );
int		OzProgress_TotalXP( void );
void	OzProgress_LocalCard( ozCardLook_t *out );
void	OzProgress_BotCard( const char *name, const char *model, int skill, ozCardLook_t *out );

// Once per match, at the intermission. 0: wait (a campaign match waits up to a second for the game's
// postgame), 1: awarded into *out (and saved, unless `demo`).
int		OzProgress_MatchEnd( const ozMatchEnd_t *m, int demo, ozAward_t *out );
void	OzProgress_MatchStarted( void );		// a new match: the next intermission awards again
// after a campaign match's results: leave for the campaign page, through the tier's movie
void	OzProgress_CampaignContinue( void );
int		OzProgress_CampaignMatch( void );		// 1 while a campaign arena runs
// g_arenas.c's "postgame" console command (its arguments), for the campaign's awards
void	OzProgress_Postgame( const char *args );

// the hub's "career" message (xp, the card others see, its showcase stats) and a count that moves
// whenever it changes
const char *OzProgress_CareerJson( void );
int		OzProgress_CareerVersion( void );

#ifdef __cplusplus
}
#endif
