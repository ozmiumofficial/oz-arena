/*
===========================================================================
Oz Arena: Skirmish's map browser (pages/offline_bots.rml): the mod a match plays under, the search box
and the sorts. GPL-2.0.

  Mod     Oz (Oz's own game), CPMA or Defrag: only the ones the player has, where a switch to them loads
          them (oz_mods.c OzMods_Ready), or in the install where it doesn't (Defrag's bundle nests its mod
          in defrag/defrag): the page then offers to import it, as the Server Browser does (decision 24:
          Oz never downloads a mod). Fight under CPMA plays the bot match under CPMA's game (startmatch's
          mod: oz_playmod, then CPMA's own mode for the page's, mode_start); under Defrag it's a race,
          Defrag's game on the race map with its own timer and no bots (oz_playmod defrag, a plain map).
          Leaving the match comes back to Oz (oz_playmod's way back, cl_parse.c).
  Maps    Oz and CPMA: the arenas (the engine's maps, in arenas.txt's order), and first, under User
          generated, the maps the player compiled in the map editor (maps' "user" entries, at its end:
          oz_ui_menu.cpp UserMaps, looked for again as the page opens). Defrag: the race maps
          oz_races.c finds (Defrag's start and finish timers), read a slice a frame while the page shows.
  Search  the map's name and file name (a race's author too), any case.
  Sort    the game's order or by name; under Defrag easiest or hardest first (Oz's estimate, below) or
          by name.

The cards bind views, not the lists: skirmish_view (the arenas: {i, maps' index; show}) and
skirmish_rview (the races: {i, skirmish_races' index; show}), as long as their lists, the shown ones
first in the sort's order. A search or a sort changes the views' values, not their length, so RmlUi
keeps its cards (a data-for that gets shorter removes elements, which is slow).

How hard a race is: nothing the player has says (Defrag's map info gives a map's name, author and
style; the community sites' difficulty votes are theirs, and a handful a map), so Oz estimates it from
the map itself (oz_races.c), and the page shows why. 1, then
  +1.5 a weapon lies on the course (rocket, plasma, grenade or BFG: weapon jumps), +0.5 two kinds or more
  +0.5 a teleport or a hurt trigger (most reset a fall), +1 six or more
  +0.5 lava or slime, +0.5 slick floors
  +0.5 a course over 20,000 units (start, checkpoints, finish in straight lines), +1 over 50,000
Defrag's training maps (their map info's style) are 1. At most 5; shown rounded: Easy, Moderate, Hard,
Very hard, Extreme.
===========================================================================
*/
#include "oz_ui_internal.h"
#include "../online/oz_servers.h"
#include "oz_races.h"
#include "cJSON.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace OzUI {
cJSON *Menu_Node( const char *path );	// oz_ui_menu.cpp
void Menu_Dirty( const char *path );
}
using namespace OzUI;

namespace {

const char *const PAGE = "offline_bots";
const int SCAN_MS = 2;			// looking for race maps, a frame while the page shows
const int SCAN_DEFRAG_MS = 6;	// while the Defrag tab is open
const int SCAN_TEXT_MS = 200;	// how often the looking's progress is shown

struct Mod { const char *id, *name; };
const Mod MODS[] = { { "oz", "Oz" }, { "cpma", "CPMA" }, { "defrag", "Defrag" } };

struct Race {
	std::string map, title, author, style, shot, search;
	std::vector<std::string> facts;
	double score = 1;
	int length = 0;
};

std::vector<Race> races;		// skirmish_races' entries
std::string builtWith;			// what the views were last built from
std::string raceShown;			// what skirmish_race shows: the picked race's map and the list it's from
int racesGen = -1, racesCount = -1, scanShownAt;
bool racesDone;
// kept across a UI restart (a match under another mod reloads the pages): the player's last choices
std::string keptMod = "oz", keptRace, keptQuery, keptSort, keptArena;

std::string Lower( std::string s ) {
	std::transform( s.begin(), s.end(), s.begin(), []( unsigned char c ) { return (char)tolower( c ); } );
	return s;
}

// a name without its color codes
std::string Plain( const char *s ) {
	std::string out;
	for ( ; s && *s; s++ ) {
		if ( s[0] == '^' && s[1] >= '0' && s[1] <= '9' ) {
			s++;
		} else {
			out += *s;
		}
	}
	return out;
}

std::string Str( const char *path ) {
	return Menu_Get( path );
}

// A map's name as it may go into a console command: letters, digits and _ - + . / only. A race map's name
// is a file name out of a pak: one with a quote, a semicolon or a line break would run as its own command.
bool CommandSafe( const std::string &name ) {
	if ( name.empty() || name.size() > 63 ) {
		return false;
	}
	for ( const unsigned char c : name ) {
		if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_' || c == '-' || c == '+' || c == '.' || c == '/' ) ) {
			return false;
		}
	}
	return true;
}

// a string value, never read as a number or a bool as Menu_Set would (a map can be called "1337")
void SetText( const char *path, const std::string &value ) {
	cJSON *j = Menu_Node( path );
	if ( !j || cJSON_IsArray( j ) || cJSON_IsObject( j ) || ( cJSON_IsString( j ) && value == j->valuestring ) ) {
		return;
	}
	if ( cJSON_IsString( j ) && j->valuestring ) {
		cJSON_free( j->valuestring );
	}
	j->type = ( j->type & ~0xff ) | cJSON_String;
	j->valuestring = (char *)cJSON_malloc( value.size() + 1 );
	memcpy( j->valuestring, value.c_str(), value.size() + 1 );
	Menu_Dirty( path );
}

void SetNumber( const char *path, double v ) {
	cJSON *j = Menu_Node( path );
	if ( cJSON_IsNumber( j ) && j->valuedouble == v ) {
		return;
	}
	Menu_Set( path, Rml::CreateString( "%g", v ).c_str() );
}

void SetBool( const char *path, bool v ) {
	cJSON *j = Menu_Node( path );
	if ( cJSON_IsBool( j ) && ( cJSON_IsTrue( j ) != 0 ) == v ) {
		return;
	}
	Menu_Set( path, v ? "true" : "false" );
}

// the node's payload replaced by src's (src consumed); the node stays, as the model holds it
void Replace( cJSON *dst, cJSON *src ) {
	cJSON *c;
	while ( ( c = dst->child ) != nullptr ) {
		cJSON_Delete( cJSON_DetachItemViaPointer( dst, c ) );
	}
	while ( ( c = src->child ) != nullptr ) {
		cJSON_AddItemToArray( dst, cJSON_DetachItemViaPointer( src, c ) );
	}
	cJSON_Delete( src );
}

const char *ModName( const std::string &id ) {
	for ( const Mod &m : MODS ) {
		if ( id == m.id ) {
			return m.name;
		}
	}
	return "Oz";
}

/*
================
The mods: offered when the player has them; ready when a switch to them finds their game code
================
*/
void Mods() {
	std::string options;
	for ( const Mod &m : MODS ) {
		char found[1024];
		const bool own = !strcmp( m.id, "oz" );
		const bool ready = own || OzMods_Ready( m.id );
		const bool install = !ready && OzMods_InInstall( m.id, found, sizeof( found ) );
		if ( ready || install ) {
			options += ( options.empty() ? "" : "|" ) + std::string( m.id );
		}
		const std::string p = std::string( "skirmish.have." ) + m.id;
		SetText( ( p + ".state" ).c_str(), ready ? "ready" : install ? "import" : "none" );
		SetText( ( p + ".found" ).c_str(), install ? found : "" );
	}
	SetText( "skirmish.mods", options );
	if ( ( "|" + options + "|" ).find( "|" + Str( "skirmish.mod" ) + "|" ) == std::string::npos ) {
		SetText( "skirmish.mod", "oz" );	// a mod that went away
	}
}

// the picked mod's state (ready, import, none), its name, its sorts
void ModChanged() {
	const std::string mod = Str( "skirmish.mod" );
	const std::string state = Str( ( "skirmish.have." + mod + ".state" ).c_str() );
	SetText( "skirmish.mod_name", ModName( mod ) );
	SetBool( "skirmish.ready", state == "ready" );
	SetText( "skirmish.found", Str( ( "skirmish.have." + mod + ".found" ).c_str() ) );
	const std::string sort = Str( "skirmish.sort" );
	if ( mod == "defrag" && sort != "easy" && sort != "hard" && sort != "name" ) {
		SetText( "skirmish.sort", "easy" );
	} else if ( mod != "defrag" && sort != "order" && sort != "name" ) {
		SetText( "skirmish.sort", "order" );
	}
	keptMod = mod;
}

/*
================
The race maps, and Oz's estimate of how hard each is
================
*/
std::string Thousands( int n ) {
	std::string s = std::to_string( n );
	for ( int i = (int)s.size() - 3; i > 0; i -= 3 ) {
		s.insert( i, "," );
	}
	return s;
}

Race Estimate( const ozRace_t &r ) {
	Race e;
	e.map = r.map;
	e.title = Plain( r.name[0] ? r.name : r.map );	// map info's names and authors carry color codes too
	e.author = Plain( r.author );
	e.style = r.style;
	e.shot = r.shot;
	e.length = r.length;
	e.search = Lower( e.title + "\n" + e.map + "\n" + e.author );

	static const struct { int bit; const char *name; } weapons[] = {
		{ OZ_RACE_ROCKET, "rocket" }, { OZ_RACE_PLASMA, "plasma" }, { OZ_RACE_GRENADE, "grenade" }, { OZ_RACE_BFG, "BFG" } };
	std::vector<std::string> kinds;
	for ( const auto &w : weapons ) {
		if ( r.weapons & w.bit ) {
			kinds.push_back( w.name );
		}
	}
	double score = 1;
	if ( !kinds.empty() ) {
		score += kinds.size() > 1 ? 2 : 1.5;
		std::string list;
		for ( size_t i = 0; i < kinds.size(); i++ ) {
			list += ( i == 0 ? "" : i + 1 == kinds.size() ? " and " : ", " ) + kinds[i];
		}
		list[0] = (char)toupper( (unsigned char)list[0] );
		e.facts.push_back( list + " jumps: the weapons lie on the course" );
	} else {
		e.facts.push_back( "Strafe only: no weapons on the course" );
	}
	const int resets = r.teleports + r.hurts;
	if ( resets ) {
		score += resets >= 6 ? 1 : 0.5;
		e.facts.push_back( Rml::CreateString( "%d %s, which most often reset a fall", resets,
			r.teleports && r.hurts ? "teleports and hurt triggers" : r.teleports ? ( resets == 1 ? "teleport" : "teleports" ) : ( resets == 1 ? "hurt trigger" : "hurt triggers" ) ) );
	}
	if ( r.lava ) {
		score += 0.5;
		e.facts.push_back( "Lava or slime" );
	}
	if ( r.slick ) {
		score += 0.5;
		e.facts.push_back( "Slick floors" );
	}
	score += r.length > 50000 ? 1 : r.length > 20000 ? 0.5 : 0;
	e.facts.push_back( Rml::CreateString( "%s %s, %s units start to finish", r.checkpoints ? std::to_string( r.checkpoints ).c_str() : "No",
		r.checkpoints == 1 ? "checkpoint" : "checkpoints", Thousands( r.length ).c_str() ) );
	if ( !StrICmp( r.style, "training" ) ) {
		score = 1;
		e.facts.insert( e.facts.begin(), "One of Defrag's training maps" );
	}
	e.score = std::min( score, 5.0 );
	return e;
}

int Level( double score ) {
	return std::max( 1, std::min( 5, (int)std::floor( score + 0.5 ) ) );
}

const char *LevelWord( int level ) {
	static const char *const words[] = { "Easy", "Moderate", "Hard", "Very hard", "Extreme" };
	return words[level - 1];
}

// skirmish_races from oz_races.c's list; the picked race stays picked by its map
void BuildRaces() {
	races.clear();
	cJSON *list = cJSON_CreateArray();
	int pick = -1;
	for ( int i = 0; i < OzRaces_Count(); i++ ) {
		const ozRace_t *r = OzRaces_Get( i );
		races.push_back( Estimate( *r ) );
		const Race &e = races.back();
		const int level = Level( e.score );
		cJSON *o = cJSON_CreateObject();
		cJSON_AddStringToObject( o, "map", e.map.c_str() );
		cJSON_AddStringToObject( o, "title", e.title.c_str() );
		cJSON_AddStringToObject( o, "author", e.author.c_str() );
		cJSON_AddStringToObject( o, "shot", e.shot.c_str() );
		cJSON_AddStringToObject( o, "tile", e.shot.empty() ? "" : ( "/@tile" + e.shot ).c_str() );
		cJSON_AddNumberToObject( o, "level", level );
		cJSON_AddStringToObject( o, "word", LevelWord( level ) );
		cJSON_AddStringToObject( o, "score", Rml::CreateString( "%.1f", e.score ).c_str() );
		cJSON *facts = cJSON_CreateArray();
		for ( const std::string &f : e.facts ) {
			cJSON_AddItemToArray( facts, cJSON_CreateString( f.c_str() ) );
		}
		cJSON_AddItemToObject( o, "facts", facts );
		cJSON_AddItemToArray( list, o );
		if ( !StrICmp( e.map.c_str(), keptRace.c_str() ) ) {
			pick = i;
		}
	}
	Replace( Menu_Node( "skirmish_races" ), list );
	Menu_Dirty( "skirmish_races" );
	SetNumber( "create_match_pick.skirmish.racei", pick < 0 ? 0 : pick );
	if ( pick < 0 ) {
		keptRace.clear();	// none picked, or it went away: the Defrag tab's list's first (Skirmish_Frame)
	}
}

// skirmish_race: the picked race, for the side column (a data-for can't index a list by a variable)
void ShowRace() {
	const int i = atoi( Menu_Get( "create_match_pick.skirmish.racei" ) );
	const Race *r = i >= 0 && i < (int)races.size() ? &races[i] : nullptr;
	const std::string key = r ? r->map + "\n" + std::to_string( racesGen ) : "";
	if ( key == raceShown ) {
		return;
	}
	raceShown = key;
	const int level = r ? Level( r->score ) : 0;
	SetText( "skirmish_race.title", r ? r->title : "" );
	SetText( "skirmish_race.map", r ? r->map : "" );
	SetText( "skirmish_race.author", r ? r->author : "" );
	SetText( "skirmish_race.shot", r ? r->shot : "" );
	SetText( "skirmish_race.word", r ? LevelWord( level ) : "" );
	SetText( "skirmish_race.score", r ? Rml::CreateString( "%.1f", r->score ) : "" );
	SetNumber( "skirmish_race.level", level );
	cJSON *facts = cJSON_CreateArray();
	for ( size_t k = 0; r && k < r->facts.size(); k++ ) {
		cJSON_AddItemToArray( facts, cJSON_CreateString( r->facts[k].c_str() ) );
	}
	Replace( Menu_Node( "skirmish_race.facts" ), facts );
	Menu_Dirty( "skirmish_race" );
}

/*
================
The views: the shown cards first, in the sort's order; the others after, hidden
================
*/
// fits: each entry's labels by its index in the list (the arenas' modes: a data-for can't walk maps[v.i].types)
void FillView( const char *path, const std::vector<int> &order, int shown, const std::vector<std::vector<std::string>> *fits = nullptr ) {
	cJSON *view = Menu_Node( path );
	if ( !view ) {
		return;
	}
	if ( cJSON_GetArraySize( view ) != (int)order.size() ) {
		cJSON *fresh = cJSON_CreateArray();
		for ( size_t k = 0; k < order.size(); k++ ) {
			cJSON *v = cJSON_CreateObject();
			cJSON_AddNumberToObject( v, "i", 0 );
			cJSON_AddFalseToObject( v, "show" );
			cJSON_AddItemToObject( v, "fits", cJSON_CreateArray() );
			cJSON_AddItemToArray( fresh, v );
		}
		Replace( view, fresh );
	}
	int k = 0;
	for ( cJSON *v = view->child; v; v = v->next, k++ ) {
		cJSON *i = cJSON_GetObjectItem( v, "i" ), *show = cJSON_GetObjectItem( v, "show" );
		cJSON_SetNumberValue( i, order[k] );
		show->type = ( show->type & ~0xff ) | ( k < shown ? cJSON_True : cJSON_False );
		if ( fits ) {
			cJSON *labels = cJSON_CreateArray();
			for ( const std::string &f : ( *fits )[order[k]] ) {
				cJSON_AddItemToArray( labels, cJSON_CreateString( f.c_str() ) );
			}
			Replace( cJSON_GetObjectItem( v, "fits" ), labels );
		}
	}
	Menu_Dirty( path );
}

void BuildViews() {
	const std::string mod = Str( "skirmish.mod" ), sort = Str( "skirmish.sort" ), q = Lower( Str( "skirmish.q" ) );
	std::vector<int> order, hidden;
	int total = 0, offMod = 0;	// offMod: the player's own maps, not counted on another mod's tab

	if ( mod == "defrag" ) {
		total = (int)races.size();
		for ( int i = 0; i < total; i++ ) {
			( q.empty() || races[i].search.find( q ) != std::string::npos ? order : hidden ).push_back( i );
		}
		std::stable_sort( order.begin(), order.end(), [&sort]( int a, int b ) {
			const Race &x = races[a], &y = races[b];
			if ( sort != "name" && x.score != y.score ) {
				return sort == "hard" ? x.score > y.score : x.score < y.score;
			}
			if ( sort != "name" && x.length != y.length ) {
				return sort == "hard" ? x.length > y.length : x.length < y.length;
			}
			return Lower( x.title ) < Lower( y.title );
		} );
		const int shown = (int)order.size();
		order.insert( order.end(), hidden.begin(), hidden.end() );
		FillView( "skirmish_rview", order, shown );
		SetNumber( "skirmish.shown", shown );
	} else {
		static const char *const modes[][2] = { { "ffa", "Free For All" }, { "tourney", "Duel" }, { "team", "Team Deathmatch" }, { "ctf", "Capture the Flag" } };
		std::vector<std::string> names;
		std::vector<std::vector<std::string>> fits;
		int users = 0;	// user maps the search shows: their section shows with one or more
		cJSON *maps = Menu_Node( "maps" );
		for ( cJSON *m = maps ? maps->child : nullptr; m; m = m->next, total++ ) {
			const std::string name = Plain( cJSON_GetStringValue( cJSON_GetObjectItem( m, "name" ) ) );
			const char *id = cJSON_GetStringValue( cJSON_GetObjectItem( m, "map" ) );
			names.push_back( Lower( name ) + "\n" + Lower( id ? id : "" ) );
			// The maps the player compiled are loose files in Oz's own folder, which another mod's game doesn't
			// load: picked on the CPMA tab, the match never started and the game stayed under CPMA
			const bool user = cJSON_IsTrue( cJSON_GetObjectItem( m, "user" ) );
			const bool shows = ( q.empty() || names.back().find( q ) != std::string::npos ) && ( !user || mod == "oz" );
			( shows ? order : hidden ).push_back( total );
			users += shows && user;
			offMod += user && mod != "oz";
			fits.emplace_back();
			cJSON *types = cJSON_GetObjectItem( m, "types" );
			for ( cJSON *t = types ? types->child : nullptr; t; t = t->next ) {
				for ( const auto &mode : modes ) {
					if ( cJSON_IsString( t ) && !strcmp( t->valuestring, mode[0] ) ) {
						fits.back().push_back( mode[1] );
					}
				}
			}
		}
		if ( sort == "name" ) {
			std::stable_sort( order.begin(), order.end(), [&names]( int a, int b ) { return names[a] < names[b]; } );
		}
		const int shown = (int)order.size();
		order.insert( order.end(), hidden.begin(), hidden.end() );
		FillView( "skirmish_view", order, shown, &fits );
		SetNumber( "skirmish.shown", shown );
		SetNumber( "skirmish.user_shown", users );
	}
	SetNumber( "skirmish.count", total - offMod );
}

// A map of the player's own stays picked only on Oz's tab: on another mod's the pick goes back to the
// mode's own arena, so Fight can't start a map that mod's game doesn't load
void KeepPickLoadable( const std::string &mod ) {
	if ( mod == "oz" || mod == "defrag" ) {
		return;
	}
	const std::string picked = "maps." + Str( "create_match_pick.skirmish.mapi" );
	if ( !cJSON_IsTrue( cJSON_GetObjectItem( Menu_Node( picked.c_str() ), "user" ) ) ) {
		return;
	}
	int arena = atoi( Menu_Get( ( "create_match.modes." + Str( "create_match.skirmish.mi" ) + ".pick" ).c_str() ) );
	if ( cJSON_IsTrue( cJSON_GetObjectItem( Menu_Node( ( "maps." + std::to_string( arena ) ).c_str() ), "user" ) ) ) {
		arena = 0;	// the arenas come first in maps
	}
	if ( cJSON_IsTrue( cJSON_GetObjectItem( Menu_Node( ( "maps." + std::to_string( arena ) ).c_str() ), "user" ) ) ) {
		return;	// no arenas at all
	}
	SetNumber( "create_match_pick.skirmish.mapi", arena );
	Printf( "UI: skirmish: the maps you made play under Oz Arena only: the pick went back to an arena\n" );
	Menu_Toast( "The maps you made play under Oz Arena only.", "info" );
}

// the line in place of the race cards while there are none, and the looking's progress
void ScanText() {
	int done = 0, total = 0;
	const ozRacesState_t state = OzRaces_State( &done, &total );
	std::string text, empty;
	if ( state != OZR_DONE ) {
		text = total ? Rml::CreateString( "Looking for race maps: %d of %d paks read", done, total ) : "Looking for race maps";
	}
	if ( races.empty() ) {
		const char *maps = OzE_CvarString( "fs_mapspath" );
		empty = state != OZR_DONE ? text : std::string( "No race maps here yet. A race map has Defrag's start and finish timers; Oz looks in " ) +
			( *maps ? std::string( "your Maps folder (" ) + maps + "), " : std::string() ) + "the baseq3 and defrag folders of your game and your Oz folder.";
	} else if ( !atoi( Menu_Get( "skirmish.shown" ) ) ) {
		empty = "No race map's name, file name or author has \"" + Str( "skirmish.q" ) + "\".";
	}
	SetText( "skirmish_scan.text", text );
	SetText( "skirmish_scan.empty", empty );
}

// the picked mod: Mod's import of the copy in the install (the Server Browser's, oz_mods.c)
void Import() {
	const std::string mod = Str( "skirmish.mod" ), found = Str( "skirmish.found" );
	char message[256];
	if ( found.empty() ) {
		PlaySound( Sound::Buzz );
		return;
	}
	const int n = OzMods_Import( found.c_str(), mod.c_str(), message, sizeof( message ) );
	SetText( "skirmish.import_message", n > 0 ? std::string( "Imported " ) + message : message );
	if ( n <= 0 ) {
		PlaySound( Sound::Buzz );
		return;
	}
	PlaySound( Sound::In );
	Mods();
	ModChanged();
	if ( mod == "defrag" ) {
		OzRaces_Again();	// Defrag's own paks: its map info names maps
	}
}

// Fight under Defrag: Defrag's game on the race map, its own timer and no bots; a plain map (no cheats), so it times runs
void Race() {
	const int i = atoi( Menu_Get( "create_match_pick.skirmish.racei" ) );
	if ( i < 0 || i >= (int)races.size() ) {
		PlaySound( Sound::Buzz );
		Menu_Toast( "Pick a race map first.", "warn" );
		return;
	}
	if ( strcmp( Menu_Get( "skirmish.have.defrag.state" ), "ready" ) ) {
		PlaySound( Sound::Buzz );
		Menu_Toast( "Defrag has to be imported first.", "warn" );
		return;
	}
	if ( !CommandSafe( races[i].map ) ) {
		PlaySound( Sound::Buzz );
		Printf( "UI: skirmish: that race map's name can't go into a command\n" );
		Menu_Toast( "That map's file name has characters the game can't load.", "warn" );
		return;
	}
	Printf( "UI: skirmish: racing %s under Defrag\n", races[i].map.c_str() );
	OzE_ExecuteNow( "oz_unhost\n" );
	CloseAllPages();
	// in front of the buffer: a test script's queued lines would hold them back
	OzE_InsertText( ( "oz_playmod defrag\nset g_gametype 0\nmap \"" + races[i].map + "\"\n" ).c_str() );
}

}	// namespace

namespace OzUI {

// skmod <id>, skimport, race: the page's own actions
bool Skirmish_Action( const Rml::String &verb, const Rml::String &arg ) {
	if ( verb == "skimport" ) {
		Import();
	} else if ( verb == "race" ) {
		Race();
	} else if ( verb == "skmods" ) {	// the mods looked for again (tests)
		Mods();
		ModChanged();
		builtWith.clear();
	} else {
		return false;
	}
	return true;
}

// ui_skirmish: the page's state for tests: the mod, the sort, the search, the pick and the cards shown, in order
void Skirmish_Commands() {
	OzE_AddCommand( "ui_skirmish", []() {
		const std::string mod = Str( "skirmish.mod" );
		const bool defrag = mod == "defrag";
		cJSON *view = Menu_Node( defrag ? "skirmish_rview" : "skirmish_view" );
		std::string cards, pick;
		int n = 0;
		for ( cJSON *v = view ? view->child : nullptr; v && cJSON_IsTrue( cJSON_GetObjectItem( v, "show" ) ) && n < 12; v = v->next, n++ ) {
			const int i = (int)cJSON_GetNumberValue( cJSON_GetObjectItem( v, "i" ) );
			cards += ( n ? " | " : " " ) + ( defrag ? ( i < (int)races.size() ? races[i].map + Rml::CreateString( " (%.1f)", races[i].score ) : "?" )
				: Plain( Menu_Get( Rml::CreateString( "maps.%d.name", i ).c_str() ) ) + " (" + Str( Rml::CreateString( "maps.%d.map", i ).c_str() ) + ")" );
		}
		const int r = atoi( Menu_Get( "create_match_pick.skirmish.racei" ) );
		pick = defrag ? ( r >= 0 && r < (int)races.size() ? races[r].map : "" ) : Str( ( "maps." + Str( "create_match_pick.skirmish.mapi" ) + ".map" ).c_str() );
		// each value copied: Menu_Get gives numbers in one buffer
		Printf( "ui_skirmish: mod %s (%s) sort %s q \"%s\" shown %s of %s pick %s:%s\n", mod.c_str(), Str( "skirmish.mods" ).c_str(), Str( "skirmish.sort" ).c_str(),
			Str( "skirmish.q" ).c_str(), Str( "skirmish.shown" ).c_str(), Str( "skirmish.count" ).c_str(), pick.c_str(), cards.c_str() );
	} );
}

// the tab the page opens on next (the Mod Library's Play on CPMA or Defrag)
void Skirmish_SetMod( const char *mod ) {
	keptMod = mod;
}

void Skirmish_Opened( const char *page ) {
	if ( strcmp( page, PAGE ) ) {
		return;
	}
	SetText( "skirmish.mod", keptMod );
	SetText( "skirmish.q", keptQuery );
	if ( !keptSort.empty() ) {
		SetText( "skirmish.sort", keptSort );
	}
	if ( !keptArena.empty() ) {
		SetNumber( "create_match_pick.skirmish.mapi", atof( keptArena.c_str() ) );
	}
	SetText( "skirmish.import_message", "" );
	Mods();
	ModChanged();
	if ( racesDone ) {
		OzRaces_Again();	// paks may have come or gone since: only those are read
		racesDone = false;
	}
	racesGen = -1;		// the model may be new (a UI restart): the race list again
	raceShown.clear();
	builtWith.clear();
}

// each frame while the page is open: the race maps looked for, the views follow the mod, search and sort
void Skirmish_Frame() {
	if ( !IsPageOpen( PAGE ) ) {
		return;
	}
	const std::string mod = Str( "skirmish.mod" );
	if ( mod != keptMod ) {
		ModChanged();
	}
	KeepPickLoadable( mod );
	keptQuery = Str( "skirmish.q" );
	keptSort = Str( "skirmish.sort" );
	keptArena = Str( "create_match_pick.skirmish.mapi" );
	// the race picked on the Defrag tab, from the model's own list (after a UI restart it's empty until rebuilt)
	const int pick = atoi( Menu_Get( "create_match_pick.skirmish.racei" ) );
	if ( mod == "defrag" && !keptRace.empty() && cJSON_GetArraySize( Menu_Node( "skirmish_races" ) ) == (int)races.size() && pick >= 0 && pick < (int)races.size() ) {
		keptRace = races[pick].map;
	}
	const bool defrag = strcmp( Menu_Get( "skirmish.have.defrag.state" ), "none" ) != 0;
	if ( defrag && OzRaces_State( nullptr, nullptr ) != OZR_DONE ) {
		OzRaces_Step( mod == "defrag" ? SCAN_DEFRAG_MS : SCAN_MS );
	}
	const bool done = OzRaces_State( nullptr, nullptr ) == OZR_DONE;
	if ( OzRaces_Generation() != racesGen && ( racesGen < 0 || OzRaces_Count() != racesCount || done ) ) {
		racesGen = OzRaces_Generation();
		racesCount = OzRaces_Count();
		BuildRaces();
		builtWith.clear();
	}
	racesDone = done;
	const std::string with = mod + "\n" + Str( "skirmish.sort" ) + "\n" + Str( "skirmish.q" ) + "\n" + std::to_string( racesCount ) + "\n" +
		std::to_string( cJSON_GetArraySize( Menu_Node( "maps" ) ) );
	ShowRace();
	if ( with != builtWith ) {
		builtWith = with;
		BuildViews();
		ScanText();
		cJSON *first = cJSON_GetArrayItem( Menu_Node( "skirmish_rview" ), 0 );
		if ( mod == "defrag" && keptRace.empty() && cJSON_IsTrue( cJSON_GetObjectItem( first, "show" ) ) ) {
			const int i = (int)cJSON_GetNumberValue( cJSON_GetObjectItem( first, "i" ) );	// none picked yet: the list's first
			SetNumber( "create_match_pick.skirmish.racei", i );
			keptRace = races[i].map;
		}
	} else if ( defrag && OzE_Milliseconds() - scanShownAt >= SCAN_TEXT_MS ) {
		scanShownAt = OzE_Milliseconds();
		ScanText();
	}
}

}	// namespace OzUI
