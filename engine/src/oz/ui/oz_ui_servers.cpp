/*
===========================================================================
Oz Arena: the community servers in the Server Browser (ENGINE_PLAN.md P8b step 1, PH step 1). GPL-2.0.

The Internet tab's server rows: every server the master servers list that answered (oz_servers.c's
sweep), folded, filtered and sorted here into the menu model's servers.rows for
pages/server_browser.rml. The page's filters (server_browser.*) hold for both kinds of row; the hub's
peer-to-peer matches (server_browser.internet, oz_ui_social.cpp) get their shown flag here.
  Game        All supported (Oz Arena, Vanilla, CPMA and Defrag), one of them, or Other mods: a
              server's fs_game ("game" in its info: none is baseq3, Vanilla; oz is an Oz server).
              Other mods are behind the filter, with no promise they play right (ENGINE_PLAN.md,
              The hub 1)
  Mode        the gametype, named as the server's mod numbers them (CPMA's 5 is Clan Arena and 9 is
              2v2; Defrag's 0 is a run and 4 fast caps)
  Ping        under 50, 100, 150 or 250 ms (servers; Oz Arena's player-hosted matches filter by region)
  Hide full, Hide empty (no human players: g_humanplayers where the server sends it, else any client)
  the search box: name, map, mode, game and address
  sort        the column headers: players (humans, then everyone, then ping), ping, name, game, map,
              mode; the same header again turns it around
The same game listed under several addresses (on 2026-10-31 the A51 relays listed a few servers
under some 380) shows once, at its lowest ping. A row joins with connect, and cl_main.c switches to
the server's mod first (PC step 3). A server with g_needpass asks for its password in the join bar;
the password cvar is set for that server and emptied for any other, so no server hears another's.
A non-pure server, or an Oz server, shows the full Oz look (ENGINE_PLAN.md decision 16): its badge.
===========================================================================
*/
#include "oz_ui_internal.h"
#include "../online/oz_servers.h"
#include "cJSON.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace OzUI {
cJSON *Menu_Node( const char *path );	// oz_ui_menu.cpp
void Menu_Dirty( const char *path );
}
using namespace OzUI;

namespace {

const int MAX_ROWS = 400;				// the most rows the list holds (the best first)
const int REBUILD_MS = 250;				// while the sweep runs, the rows are rebuilt at most this often
const int STALE_MS = 5 * 60 * 1000;		// a list this old is asked for again as the page opens
const int MARGIN = 20;					// rows laid out beyond the part in view, either side (the window)

struct Server {
	std::string address, name, rich, search;	// rich: the name in its colors (RML); search: what the search box looks in, lowercase
	std::string map, mapName, game, kind, gameName, mode;	// kind: oz, vanilla, cpma, defrag, other (a mod) or game (another game)
	std::string engine;							// the engine's tag (Quake3e, ioquake3, CNQ3, oDFe, id 1.32...), "" until known
	int ping = 0, clients = 0, humans = -1, max = 0;	// humans: -1 when the server doesn't say
	int protocol = 0;					// 68 (Quake III's) or 71 (ioquake3's and OpenArena's)
	bool password = false, pure = true, have = true;
	int folded = 0;						// other addresses of the same game
	// favorites and history: a peer-to-peer match's host instead of an address; up, or why not; when it was joined
	std::string host, status, when;
	bool up = true;
	long long at = 0;
};

std::map<std::string, std::string> engines;	// address -> the engine's tag, from the card's getstatus (getinfo has no version)
std::vector<Server> servers;			// parsed and folded, in the sweep's order
std::vector<Server> answers;			// every answer, parsed, before folding
std::vector<Server> lanServers;			// the LAN broadcast's answers
std::map<std::string, Server> previous;	// a refresh's last answers, listed until each server answers again
int parsedGen = -1;
int parsedAt = -100000;					// OzE_Milliseconds of the last Parse
bool matchesChanged;					// the hub's rows came again: their shown flags
int builtAt = -100000;
std::string builtWith;					// the filters and the sweep's state the rows were built with
int numRows;							// servers.rows' length
std::string lastTab;					// the tab the last frame showed (the LAN tab asks as it's opened)
int first, last = 2 * MARGIN;			// the window: the rows laid out
bool viewStale;							// servers.rows changed since the window's rows were copied out
int viewFirst, viewLast;				// the rows in view, as the last frame laid them out
float rowHeight;						// a row's, in pixels, once one is laid out

std::string Lower( std::string s ) {
	for ( char &c : s ) {
		c = (char)tolower( (unsigned char)c );
	}
	return s;
}

/*
A name's characters, each with the color it's drawn in (-1 the default), without the spaces around them.
The codes are the game's (Quake3e's Q_IsColorString and ColorIndex): ^ and any character but ^, its
color the character's low three bits after '0'; ^^ alone is a caret. The double code "^^XY" (X, Y not
letters) is Y's color: on 2026-10-31 a third of the servers named themselves so (A51's "^^05A51|CTF",
"^^22RETRO|FFA", "^^77{CROM} CTF"), X sorting their rows in other browsers. The game shows those as
"^5A51|CTF"; drawn as meant, they read "A51|CTF" in magenta.
*/
struct Glyph { char c; int color; };

std::vector<Glyph> Glyphs( const std::string &s ) {
	auto code = []( char c ) { return c != '^' && c > ' ' && c < 127 && !isalpha( (unsigned char)c ); };
	std::vector<Glyph> out;
	int color = -1;
	for ( size_t i = 0; i < s.size(); i++ ) {
		if ( s[i] == '^' && i + 3 < s.size() && s[i + 1] == '^' && code( s[i + 2] ) && code( s[i + 3] ) ) {
			color = ( s[i + 3] - '0' ) & 7;
			i += 3;
		} else if ( s[i] == '^' && i + 1 < s.size() && s[i + 1] != '^' && s[i + 1] != '\n' && s[i + 1] != '\r' ) {
			color = ( s[i + 1] - '0' ) & 7;
			i++;
		} else if ( (unsigned char)s[i] >= ' ' ) {
			out.push_back( { s[i], color } );	// a caret before a caret is drawn; the second may start a code
		}
	}
	while ( !out.empty() && out.back().c == ' ' ) {
		out.pop_back();
	}
	size_t a = 0;
	while ( a < out.size() && out[a].c == ' ' ) {
		a++;
	}
	return std::vector<Glyph>( out.begin() + a, out.end() );
}

// a name without its color codes (id's browser cleaned names the same way, Q_CleanStr)
std::string Clean( const std::string &s ) {
	std::string out;
	for ( const Glyph &g : Glyphs( s ) ) {
		out += g.c;
	}
	return out;
}

// a name in its colors, as RML: the menus' palette (q3name's), black a dim grey the page's dark rows can show
std::string Rich( const std::string &s ) {
	static const char *const colors[8] = { "#8d847c", "#ff4a1c", "#5ccf4a", "#ffd23c", "#4a7dff", "#3cd2ff", "#ff4aff", "" };
	std::string out, run;
	int color = -1;
	auto flush = [&]() {
		if ( !run.empty() ) {
			const Rml::String text = Rml::StringUtilities::EncodeRml( run );
			out += color < 0 || !colors[color][0] ? text : std::string( "<span style=\"color: " ) + colors[color] + ";\">" + text + "</span>";
		}
		run.clear();
	};
	for ( const Glyph &g : Glyphs( s ) ) {
		if ( g.color != color ) {
			flush();
			color = g.color;
		}
		run += g.c;
	}
	flush();
	return out;
}

// the friends on each listed community server, from presence (oz_ui_social.cpp's server field): address -> their names
std::map<std::string, std::vector<std::string>> Friends() {
	std::map<std::string, std::vector<std::string>> out;
	const cJSON *g, *f;
	cJSON_ArrayForEach( g, Menu_Node( "social.groups" ) ) {
		cJSON_ArrayForEach( f, cJSON_GetObjectItemCaseSensitive( g, "friends" ) ) {
			const cJSON *server = cJSON_GetObjectItemCaseSensitive( f, "server" ), *name = cJSON_GetObjectItemCaseSensitive( f, "name" );
			if ( cJSON_IsString( server ) && server->valuestring[0] ) {
				out[server->valuestring].push_back( Clean( cJSON_IsString( name ) ? name->valuestring : "A friend" ) );
			}
		}
	}
	return out;
}

std::string FriendsText( int n ) {
	return n ? std::to_string( n ) + ( n == 1 ? " friend" : " friends" ) : "";
}

std::map<std::string, std::string> Info( const char *s ) {
	std::map<std::string, std::string> kv;
	while ( *s ) {
		if ( *s == '\\' ) {
			s++;
		}
		const char *k = s;
		while ( *s && *s != '\\' ) {
			s++;
		}
		const std::string key = Lower( std::string( k, s - k ) );
		if ( *s ) {
			s++;
		}
		const char *v = s;
		while ( *s && *s != '\\' ) {
			s++;
		}
		kv[key] = std::string( v, s - v );
	}
	return kv;
}

std::string Value( const std::map<std::string, std::string> &kv, const char *key ) {
	const auto it = kv.find( key );
	return it == kv.end() ? "" : it->second;
}

// the mods players name differently from their folder
const char *ModName( const std::string &dir ) {
	static const char *const names[][2] = { { "osp", "OSP" }, { "q3ut4", "Urban Terror" }, { "excessiveplus", "Excessive Plus" },
		{ "rat", "Rat Arena" }, { "arena", "Rocket Arena" }, { "efreeze", "Freeze Tag" }, { "baseoa", "OpenArena" }, { "missionpack", "Team Arena" } };
	for ( const auto &n : names ) {
		if ( dir == n[0] ) {
			return n[1];
		}
	}
	return nullptr;
}

std::string ModeName( const std::string &kind, int gt ) {
	if ( kind == "defrag" ) {
		return gt == 4 ? "Fast caps" : "Run";
	}
	if ( kind == "cpma" ) {
		switch ( gt ) {
		case -1: return "Hoonymode";
		case 5: return "Clan Arena";
		case 6: return "Freeze Tag";
		case 7: return "Capture Strike";
		case 8: return "NTF";
		case 9: return "2v2";
		}
	}
	switch ( gt ) {
	case 0: return "Free For All";
	case 1: return "Duel";
	case 2: return "Single Player";
	case 3: return "Team Deathmatch";
	case 4: return "Capture the Flag";
	}
	return "Mode " + std::to_string( gt );
}

// an arena's name from the game's scripts (the menu model's maps), else its file name, as the hub's rows have it
std::string MapName( const std::string &map ) {
	const cJSON *m;
	cJSON_ArrayForEach( m, Menu_Node( "maps" ) ) {
		const cJSON *f = cJSON_GetObjectItemCaseSensitive( m, "map" ), *n = cJSON_GetObjectItemCaseSensitive( m, "name" );
		if ( cJSON_IsString( f ) && !StrICmp( f->valuestring, map.c_str() ) ) {
			return cJSON_IsString( n ) ? Clean( n->valuestring ) : map;
		}
	}
	return map;
}

std::string Thousands( int n ) {
	std::string s = std::to_string( n );
	for ( int k = (int)s.size() - 3; k > 0; k -= 3 ) {
		s.insert( k, "," );
	}
	return s;
}

std::string GameName( const std::string &kind, const std::string &game ) {
	const char *named = ModName( game );
	return kind == "vanilla" ? "Vanilla" : kind == "oz" ? "Oz Arena" : kind == "cpma" ? "CPMA" : kind == "defrag" ? "Defrag" : named ? named : game;
}

/*
The engine's tag from its version string (PH step 1; getstatus's version, or getinfo's where an engine
sends one): "Q3 1.32e linux-x86_64 Aug 4 2026" is Quake3e, "ioq3 1.36_GIT..." ioquake3, "CNQ3 1.53"
CNQ3, "oDFe 1.00" oDFe, "Q3 1.32c" (or 1.32b, 1.32) id's own. Anything else keeps its first word.
*/
std::string EngineTag( const std::string &version ) {
	const std::string v = Lower( version );
	if ( v.empty() ) {
		return "";
	}
	if ( v.compare( 0, 5, "ioq3 " ) == 0 || v.compare( 0, 8, "ioquake3" ) == 0 ) {
		return "ioquake3";
	}
	if ( v.compare( 0, 4, "cnq3" ) == 0 ) {
		return "CNQ3";
	}
	if ( v.compare( 0, 4, "odfe" ) == 0 ) {
		return "oDFe";
	}
	if ( v.compare( 0, 8, "q3 1.32e" ) == 0 ) {
		return "Quake3e";
	}
	if ( v.compare( 0, 7, "q3 1.32" ) == 0 || v.compare( 0, 7, "q3 1.31" ) == 0 || v.compare( 0, 7, "q3 1.30" ) == 0 || v.compare( 0, 7, "q3 1.17" ) == 0 ) {
		return "id " + version.substr( 3, version.find( ' ', 3 ) == std::string::npos ? std::string::npos : version.find( ' ', 3 ) - 3 );
	}
	if ( v.compare( 0, 3, "oz " ) == 0 || v.find( "oz arena" ) != std::string::npos ) {
		return "Oz Arena";
	}
	return version.substr( 0, version.find( ' ' ) );
}

/*
Another game on the Quake III masters (PH step 1): an engine's "gamename" (ioquake3's com_gamename;
Quake3e sends none) that isn't Quake III's own, nor the mod's folder, nor a known mod's name. Urban
Terror and OpenArena list there with their own engines; their servers don't take this game, so they
sit behind the Game filter's Other games.
*/
bool OtherGame( const std::string &gamename, const std::string &game ) {
	static const char *const q3[] = { "quake3arena", "quake3", "q3a", "baseq3", "quake3e", "oz", "oz arena", "ozarena" };
	const std::string g = Lower( gamename );
	if ( g.empty() || g == game ) {
		return false;
	}
	for ( const char *n : q3 ) {
		if ( g == n ) {
			return false;
		}
	}
	return ModName( g ) == nullptr && g != "cpma" && g != "defrag";
}

// a server's getinfo answer as a row
Server ParseOne( const ozServerInfo_t &in ) {
	const auto kv = Info( in.info );
	Server s;
	s.address = in.address;
	s.ping = in.ping;
	s.name = Clean( Value( kv, "hostname" ) );
	s.rich = Rich( Value( kv, "hostname" ) );
	if ( s.name.empty() ) {
		s.name = s.rich = s.address;
	}
	s.map = Lower( Value( kv, "mapname" ) );
	s.game = Lower( Value( kv, "game" ) );
	if ( s.game.empty() ) {
		s.game = "baseq3";
	}
	s.kind = s.game == "baseq3" ? "vanilla" : s.game == "oz" || s.game == "cpma" || s.game == "defrag" ? s.game : "other";
	if ( OtherGame( Value( kv, "gamename" ), s.game ) ) {
		s.kind = "game";
	}
	s.gameName = s.kind == "game" ? Value( kv, "gamename" ) : GameName( s.kind, s.game );
	s.engine = EngineTag( Value( kv, "version" ) );
	if ( s.engine.empty() ) {
		const auto known = engines.find( s.address );	// the card asked its status before
		s.engine = known == engines.end() ? "" : known->second;
	}
	s.mode = ModeName( s.kind, atoi( Value( kv, "gametype" ).c_str() ) );
	// A51's Clan Arena and Freeze Tag run on baseq3's team number (3), which only their names tell apart:
	// "*A51* Freeze Tag" and "A51|FRZ", "*A51* Clan Arena" and "A51|CA" (the short words only beside A51:
	// a server in "LA, CA" isn't Clan Arena)
	if ( s.kind == "vanilla" && atoi( Value( kv, "gametype" ).c_str() ) == 3 ) {
		const std::string low = Lower( s.name );
		std::string spaced = " ";
		for ( const char c : low ) {
			spaced += isalnum( (unsigned char)c ) ? c : ' ';
		}
		spaced += ' ';
		const bool a51 = spaced.find( " a51 " ) != std::string::npos;
		if ( spaced.find( " freeze " ) != std::string::npos || ( a51 && spaced.find( " frz " ) != std::string::npos ) ) {
			s.mode = "Freeze Tag";
		} else if ( spaced.find( " clan arena " ) != std::string::npos || ( a51 && spaced.find( " ca " ) != std::string::npos ) ) {
			s.mode = "Clan Arena";
		}
	}
	s.protocol = atoi( Value( kv, "protocol" ).c_str() );
	s.clients = atoi( Value( kv, "clients" ).c_str() );
	s.max = atoi( Value( kv, "sv_maxclients" ).c_str() );
	const std::string humans = Value( kv, "g_humanplayers" );
	s.humans = humans.empty() ? -1 : atoi( humans.c_str() );
	s.password = atoi( Value( kv, "g_needpass" ).c_str() ) != 0;
	s.pure = Value( kv, "pure" ) != "0";
	s.have = OzServers_HaveMod( s.game.c_str() ) != 0;
	s.mapName = MapName( s.map );
	s.search = Lower( s.name + "\n" + s.map + "\n" + s.mapName + "\n" + s.mode + "\n" + s.gameName + "\n" + s.address );
	return s;
}

// one game under several addresses: the same name, map, mod, mode and players
std::string FoldKey( const Server &s ) {
	return Lower( s.name ) + "\n" + s.map + "\n" + s.game + "\n" + s.mode + "\n" + std::to_string( s.clients ) + "/" + std::to_string( s.max );
}

// which of one game's addresses its row joins: Quake III's protocol first, then the lowest ping. Area 51 opens each
// server to OpenArena players too, on another port with protocol 71, where it hands out its OpenArena mod (a51oa) and
// maps, which a Quake III install doesn't have.
bool Better( const Server &s, const Server &than ) {
	if ( ( s.protocol == 68 ) != ( than.protocol == 68 ) ) {
		return s.protocol == 68;
	}
	return s.ping < than.ping;
}

// every server that answered, parsed; while a refresh runs, the last answers of those not asked again yet (so the list
// doesn't empty and refill); then the same game under several addresses folded into the one it joins (Better). And the LAN's.
void Parse() {
	answers.clear();
	std::set<std::string> fresh;
	const int n = OzServers_Count();
	for ( int i = 0; i < n; i++ ) {
		ozServerInfo_t in;
		if ( OzServers_Get( i, &in ) ) {
			answers.push_back( ParseOne( in ) );
			fresh.insert( in.address );
		}
	}
	lanServers.clear();
	for ( int i = 0; i < OzServers_LanCount(); i++ ) {
		ozServerInfo_t in;
		if ( OzServers_LanGet( i, &in ) ) {
			lanServers.push_back( ParseOne( in ) );
		}
	}
	const ozServersState_t state = OzServers_State( nullptr, nullptr, nullptr );
	if ( state == OZS_MASTERS || state == OZS_SERVERS ) {
		for ( const auto &p : previous ) {
			if ( !fresh.count( p.first ) ) {
				answers.push_back( p.second );
			}
		}
	} else {
		previous.clear();	// the sweep is over: a server that didn't answer it is gone
	}

	servers.clear();
	std::map<std::string, size_t> seen;
	for ( Server s : answers ) {
		const std::string key = FoldKey( s );
		const auto it = seen.find( key );
		if ( it == seen.end() ) {
			seen[key] = servers.size();
			servers.push_back( s );
		} else {
			Server &kept = servers[it->second];
			kept.folded++;
			if ( Better( s, kept ) ) {
				s.folded = kept.folded;
				kept = s;
			}
		}
	}
}

/*
================
Favorites and history: <fs_homepath>/profile/servers.json, beside the career (the same under every
mod). An entry is a community server (its address) or a peer-to-peer match (its host's player id),
with what it was when starred or joined, shown as it was until it answers again. The history keeps
the last HISTORY joins, newest first, a place joined again moving to the top.
================
*/
const size_t HISTORY = 50;

struct Saved {
	std::string kind, key;		// kind: server (key, its address) or match (key, its host)
	std::string name, rich, game, gameKind, map, mode;
	long long at = 0;			// the join's or the star's time, in seconds
};

std::vector<Saved> favorites, history;
bool savedLoaded;
int savedGen;					// changes with every change to either list

std::filesystem::path SavedPath() {
	return Prog::Folder() / "servers.json";
}

void LoadSaved() {
	if ( savedLoaded ) {
		return;
	}
	savedLoaded = true;
	cJSON *j = Prog::ReadJson( SavedPath() );
	for ( auto *list : { &favorites, &history } ) {
		const cJSON *e;
		cJSON_ArrayForEach( e, cJSON_GetObjectItemCaseSensitive( j, list == &favorites ? "favorites" : "history" ) ) {
			auto str = [&]( const char *k ) {
				const cJSON *v = cJSON_GetObjectItemCaseSensitive( e, k );
				return std::string( cJSON_IsString( v ) ? v->valuestring : "" );
			};
			Saved s;
			s.kind = str( "kind" ) == "match" ? "match" : "server";
			s.key = str( "key" );
			s.name = str( "name" );
			s.rich = str( "rich" );
			s.game = str( "game" );
			s.gameKind = str( "game_kind" );
			s.map = str( "map" );
			s.mode = str( "mode" );
			const cJSON *at = cJSON_GetObjectItemCaseSensitive( e, "at" );
			s.at = cJSON_IsNumber( at ) ? (long long)at->valuedouble : 0;
			if ( !s.key.empty() ) {
				list->push_back( s );
			}
		}
	}
	cJSON_Delete( j );
}

void SaveSaved() {
	cJSON *j = cJSON_CreateObject();
	for ( const auto *list : { &favorites, &history } ) {
		cJSON *a = cJSON_AddArrayToObject( j, list == &favorites ? "favorites" : "history" );
		for ( const Saved &s : *list ) {
			cJSON *o = cJSON_CreateObject();
			cJSON_AddStringToObject( o, "kind", s.kind.c_str() );
			cJSON_AddStringToObject( o, "key", s.key.c_str() );
			cJSON_AddStringToObject( o, "name", s.name.c_str() );
			cJSON_AddStringToObject( o, "rich", s.rich.c_str() );
			cJSON_AddStringToObject( o, "game", s.game.c_str() );
			cJSON_AddStringToObject( o, "game_kind", s.gameKind.c_str() );
			cJSON_AddStringToObject( o, "map", s.map.c_str() );
			cJSON_AddStringToObject( o, "mode", s.mode.c_str() );
			cJSON_AddNumberToObject( o, "at", (double)s.at );
			cJSON_AddItemToArray( a, o );
		}
	}
	Prog::WriteJson( SavedPath(), j );
	cJSON_Delete( j );
	savedGen++;
}

bool IsFavorite( const std::string &kind, const std::string &key ) {
	LoadSaved();
	for ( const Saved &s : favorites ) {
		if ( s.kind == kind && s.key == key ) {
			return true;
		}
	}
	return false;
}

Saved SavedFrom( const std::string &kind, const std::string &key, const Server *s ) {
	Saved e;
	e.kind = kind;
	e.key = key;
	e.at = (long long)time( nullptr );
	if ( s ) {
		e.name = s->name;
		e.rich = s->rich;
		e.game = s->gameName;
		e.gameKind = s->kind;
		e.map = s->mapName;
		e.mode = s->mode;
	} else {
		e.name = e.rich = key;
	}
	return e;
}

// "Today 14:05", "Yesterday 21:30", "Oct 28"
std::string When( long long at ) {
	if ( at <= 0 ) {
		return "";
	}
	const time_t t = (time_t)at, now = time( nullptr );
	struct tm when, today;
#ifdef _WIN32
	localtime_s( &when, &t );
	localtime_s( &today, &now );
#else
	localtime_r( &t, &when );
	localtime_r( &now, &today );
#endif
	char buf[32];
	const long long beforeToday = (long long)now - ( today.tm_hour * 3600 + today.tm_min * 60 + today.tm_sec ) - at;
	if ( beforeToday < 0 ) {
		strftime( buf, sizeof( buf ), "Today %H:%M", &when );
	} else if ( beforeToday < 86400 ) {
		strftime( buf, sizeof( buf ), "Yesterday %H:%M", &when );
	} else {
		strftime( buf, sizeof( buf ), "%b %d", &when );
	}
	return buf;
}

struct Filters {
	std::string tab, game, mode, region, query, sort;
	int ping = 0;		// 0: any
	bool full = false, empty = false, rev = false;
};

Filters Read() {
	Filters f;
	f.tab = Menu_Get( "server_browser.tab" );
	f.game = Menu_Get( "server_browser.game" );
	f.mode = Menu_Get( "server_browser.mode" );
	f.region = Menu_Get( "server_browser.region" );
	f.query = Lower( Menu_Get( "server_browser.query" ) );
	f.sort = Menu_Get( "server_browser.sort" );
	f.ping = atoi( Menu_Get( "server_browser.ping" ) + strcspn( Menu_Get( "server_browser.ping" ), "0123456789" ) );
	f.full = !strcmp( Menu_Get( "server_browser.hide_full" ), "true" );
	f.empty = !strcmp( Menu_Get( "server_browser.hide_empty" ), "true" );
	f.rev = !strcmp( Menu_Get( "server_browser.sort_rev" ), "true" );
	return f;
}

// what the rows were built with but the sweep: the filters, and whether the hub is up (its rows and the empty list's words)
std::string Key( const Filters &f ) {
	return f.tab + "\n" + f.game + "\n" + f.mode + "\n" + f.region + "\n" + f.query + "\n" + f.sort + "\n" + std::to_string( f.ping ) +
		( f.full ? "F" : "f" ) + ( f.empty ? "E" : "e" ) + ( f.rev ? "R" : "r" ) + Menu_Get( "social.status" );
}

bool GameFits( const Filters &f, const std::string &kind ) {
	if ( f.game == "Other mods" ) {
		return kind == "other";
	}
	if ( f.game == "Other games" ) {
		return kind == "game";
	}
	if ( f.game == "Oz Arena" || f.game == "Vanilla" || f.game == "CPMA" || f.game == "Defrag" ) {
		return Lower( f.game ) == kind || ( f.game == "Oz Arena" && kind == "oz" );
	}
	return kind != "other" && kind != "game";	// All supported
}

// what a row lacking its mod needs (PH step 1's "mod missing"): the mod's name, or "" where there's nothing to import
std::string Needs( const Server &s ) {
	return s.have || s.kind == "game" || s.kind == "oz" || s.kind == "vanilla" || s.gameName.empty() ? "" : s.gameName;
}

bool Fits( const Filters &f, const Server &s ) {
	const int humans = s.humans >= 0 ? s.humans : s.clients;
	return GameFits( f, s.kind ) && ( f.mode == "Any mode" || f.mode == s.mode ) && ( !f.ping || s.ping < f.ping ) &&
		!( f.full && s.max > 0 && s.clients >= s.max ) && !( f.empty && humans == 0 ) &&
		( f.query.empty() || s.search.find( f.query ) != std::string::npos );
}

// players: humans first (everyone where the server doesn't say), then everyone, then the nearest
bool Before( const Filters &f, const Server &a, const Server &b ) {
	const int ha = a.humans >= 0 ? a.humans : a.clients, hb = b.humans >= 0 ? b.humans : b.clients;
	int c = 0;
	if ( f.sort == "ping" ) {
		c = a.ping - b.ping;
	} else if ( f.sort == "name" ) {
		c = StrICmp( a.name.c_str(), b.name.c_str() );
	} else if ( f.sort == "game" ) {
		c = StrICmp( a.gameName.c_str(), b.gameName.c_str() );
	} else if ( f.sort == "map" ) {
		c = StrICmp( a.mapName.c_str(), b.mapName.c_str() );
	} else if ( f.sort == "mode" ) {
		c = StrICmp( a.mode.c_str(), b.mode.c_str() );
	} else {
		c = hb != ha ? hb - ha : b.clients - a.clients;
	}
	if ( !c ) {
		c = a.ping - b.ping;
	}
	return f.rev ? c > 0 : c < 0;
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

// a string value, never read as a number or a bool as Menu_Set would (a server can be called "1337")
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

bool Online() {
	return !strcmp( Menu_Get( "social.status" ), "online" );
}

// the hub's peer-to-peer matches: their shown flags (Oz Arena's, so only under All supported or Oz Arena)
int MatchesShown( const Filters &f ) {
	cJSON *rows = Menu_Node( "server_browser.internet" );
	int shown = 0;
	bool changed = false;
	cJSON *r;
	cJSON_ArrayForEach( r, rows ) {
		auto str = [&]( const char *k ) {
			const cJSON *v = cJSON_GetObjectItemCaseSensitive( r, k );
			return std::string( cJSON_IsString( v ) ? v->valuestring : "" );
		};
		auto flag = [&]( const char *k ) { return cJSON_IsTrue( cJSON_GetObjectItemCaseSensitive( r, k ) ); };
		const std::string hay = Lower( str( "name" ) + "\n" + str( "owner" ) + "\n" + str( "map" ) + "\n" + str( "mode" ) + "\nOz Arena" );
		const bool fits = Online() && GameFits( f, "oz" ) && ( f.mode == "Any mode" || f.mode == str( "mode" ) ) &&
			( f.game != "Oz Arena" || f.region == "All regions" || f.region == str( "where" ) ) &&
			!( f.full && flag( "full" ) ) && !( f.empty && flag( "empty" ) ) && ( f.query.empty() || hay.find( f.query ) != std::string::npos );
		cJSON *old = cJSON_GetObjectItemCaseSensitive( r, "shown" );
		if ( !old || ( cJSON_IsTrue( old ) != 0 ) != fits ) {
			cJSON_DeleteItemFromObjectCaseSensitive( r, "shown" );
			cJSON_AddBoolToObject( r, "shown", fits );
			changed = true;
		}
		const bool fav = IsFavorite( "match", str( "host" ) );	// the star, saved (profile/servers.json)
		if ( ( flag( "fav" ) != 0 ) != fav ) {
			cJSON_DeleteItemFromObjectCaseSensitive( r, "fav" );
			cJSON_AddBoolToObject( r, "fav", fav );
			changed = true;
		}
		shown += fits;
	}
	if ( changed ) {
		Menu_Dirty( "server_browser" );
	}
	return shown;
}

// the server row control with the focus (a row's own button, its Info or its star): its row's place in servers.rows and
// the control's id without the place, or -1
int FocusedRow( std::string &control ) {
	Rml::ElementDocument *doc = !strcmp( TopPageName(), "server_browser" ) ? TopDocument() : nullptr;
	Rml::Element *focus = doc ? GetContext()->GetFocusElement() : nullptr;
	if ( !focus || focus->GetOwnerDocument() != doc || focus->GetId().compare( 0, 6, "sb_sv_" ) ) {
		return -1;
	}
	const Rml::String &id = focus->GetId();
	const size_t place = id.find_first_of( "0123456789", 6 );
	if ( place == Rml::String::npos ) {
		return -1;
	}
	control = id.substr( 0, place );
	return atoi( id.c_str() + place );
}

// the window's rows laid out (servers.view: servers.rows' first to last, each with its place, n, that its controls' ids
// carry), and the pads standing for the rest; true when the rows laid out changed
bool SetWindow( int a, int b ) {
	a = std::max( 0, std::min( a, numRows ) );
	b = std::max( a, std::min( b, numRows ) );
	const bool changed = a != first || b != last || viewStale;
	if ( changed ) {
		first = a;
		last = b;
		viewStale = false;
		Menu_Set( "servers.first", std::to_string( first ).c_str() );
		Menu_Set( "servers.last", std::to_string( last ).c_str() );
		cJSON *view = Menu_Node( "servers.view" ), *out = cJSON_CreateArray();
		int n = first;
		for ( const cJSON *r = cJSON_GetArrayItem( Menu_Node( "servers.rows" ), first ); r && n < last; r = r->next, n++ ) {
			cJSON *o = cJSON_Duplicate( r, true );
			cJSON_AddStringToObject( o, "n", std::to_string( n ).c_str() );
			cJSON_AddItemToArray( out, o );
		}
		if ( view ) {
			Replace( view, out );
		} else {
			cJSON_Delete( out );
		}
		Menu_Dirty( "servers" );
	}
	char px[32];
	snprintf( px, sizeof( px ), "%.0fpx", first * rowHeight );
	SetText( "servers.top", px );
	snprintf( px, sizeof( px ), "%.0fpx", ( numRows - last ) * rowHeight );
	SetText( "servers.bottom", px );
	return changed;
}

// the focus back on a row's control once the rows laid out changed: the row at place n (its own button where it has no
// Info: a match's), or, when that's out of the window now (a scroll left it), the nearest one in view; scrolled to it if asked
void Refocus( int n, const std::string &control, bool scroll = false ) {
	Rml::ElementDocument *doc = !strcmp( TopPageName(), "server_browser" ) ? TopDocument() : nullptr;
	if ( n < 0 || !doc || first >= last ) {
		return;
	}
	if ( n < first || n >= last ) {
		n = std::max( viewFirst, std::min( n, viewLast - 1 ) );
	}
	n = std::max( first, std::min( n, last - 1 ) );
	if ( Rml::Element *list = doc->GetElementById( "sb_list" ) ) {
		list->SetClass( "moving", true );	// the focus changes element under a row that stays: no fade (the next frame clears it)
	}
	GetContext()->Update();	// the rows' elements take their places
	Rml::Element *to = doc->GetElementById( ( control + std::to_string( n ) ).c_str() );
	if ( !to || !to->IsVisible() ) {
		to = doc->GetElementById( ( "sb_sv_" + std::to_string( n ) ).c_str() );
	}
	FocusQuietly( to );
	if ( to && scroll ) {
		to->ScrollIntoView( Rml::ScrollIntoViewOptions( Rml::ScrollAlignment::Nearest ) );
	}
}

/*
The window: the rows in view and MARGIN either side, moved once the view comes within a few rows of
its edge. RmlUi makes the elements and bindings of every row a data-for lists, in view or not (400
rows were 6.3 ms a frame laid out; a filter that took 264 rows away froze the menus for 5 s before
engine/patches/rmlui/0003, and putting them back took 80 ms), so the page lists only the window's
rows (servers.view). A row the d-pad or a scroll reaches is always laid out already; a row's
elements hold whichever server is at their place in the window, so as the window moves the focus
moves to the element its server is in now (Refocus).
*/
void Window() {
	Rml::ElementDocument *doc = strcmp( TopPageName(), "server_browser" ) ? nullptr : TopDocument();
	Rml::Element *list = doc ? doc->GetElementById( "sb_list" ) : nullptr, *pad = doc ? doc->GetElementById( "sb_pad_top" ) : nullptr;
	if ( !list || !pad || !numRows ) {
		return;
	}
	// a row's height (the menu size and the window's change it): the pads follow it
	Rml::Element *hit = doc->GetElementById( ( "sb_sv_" + std::to_string( first ) ).c_str() );
	const float h = hit && hit->GetParentNode() ? hit->GetParentNode()->GetOffsetHeight() : 0.0f;
	if ( h > 0.0f && fabsf( h - rowHeight ) > 0.5f ) {
		rowHeight = h;
		SetWindow( first, last );
		return;
	}
	if ( rowHeight <= 0.0f ) {
		return;
	}
	// where the rows start in the list's content (the top pad stands for the rows above the window), and the part in view
	const float start = pad->GetAbsoluteTop() - list->GetAbsoluteTop() + list->GetScrollTop();
	const float top = list->GetScrollTop() - start, view = list->GetClientHeight();
	const int a = std::max( 0, (int)floorf( top / rowHeight ) ), b = std::min( numRows, (int)ceilf( ( top + view ) / rowHeight ) );
	viewFirst = a;
	viewLast = b;
	if ( ( a - first < 3 && first > 0 ) || ( last - b < 3 && last < numRows ) || a < first || b > last ) {
		std::string control;
		const int held = FocusedRow( control ), was = first;
		if ( SetWindow( a - MARGIN, b + MARGIN ) && ( first != was || held >= last ) ) {
			Refocus( held, control );
		}
	}
}

// a server that answered: the list's row first (the folded one), then any answer of the sweep's or the LAN's
const Server *Find( const std::string &address ) {
	for ( const auto *list : { &servers, &answers, &lanServers } ) {
		for ( const Server &s : *list ) {
			if ( s.address == address ) {
				return &s;
			}
		}
	}
	return nullptr;
}

// a peer-to-peer match the hub lists now (server_browser.internet, oz_ui_social.cpp), as a row
bool ListedMatch( const std::string &host, Server &out ) {
	const cJSON *r;
	cJSON_ArrayForEach( r, Menu_Node( "server_browser.internet" ) ) {
		auto str = [&]( const char *k ) {
			const cJSON *v = cJSON_GetObjectItemCaseSensitive( r, k );
			return std::string( cJSON_IsString( v ) ? v->valuestring : "" );
		};
		if ( str( "host" ) != host ) {
			continue;
		}
		out.host = host;
		out.name = str( "name" );
		out.rich = Rml::StringUtilities::EncodeRml( out.name );
		out.kind = "oz";
		out.gameName = "Oz Arena";
		out.mapName = str( "map" );
		out.mode = str( "mode" );
		sscanf( str( "players" ).c_str(), "%d/%d", &out.clients, &out.max );
		out.humans = out.clients;
		out.password = cJSON_IsTrue( cJSON_GetObjectItemCaseSensitive( r, "password" ) );
		out.search = Lower( out.name + "\n" + out.mapName + "\n" + out.mode + "\nOz Arena" );
		return true;
	}
	return false;
}

// the tab's rows before the filters: the sweep's, the LAN's, or the saved ones as they are now (as they were, when down)
std::vector<Server> Pool( const Filters &f ) {
	if ( f.tab == "lan" ) {
		return lanServers;
	}
	if ( f.tab != "favorites" && f.tab != "history" ) {
		return servers;
	}
	LoadSaved();
	const ozServersState_t state = OzServers_State( nullptr, nullptr, nullptr );
	const bool asking = state == OZS_MASTERS || state == OZS_SERVERS;
	std::vector<Server> pool;
	for ( const Saved &e : f.tab == "favorites" ? favorites : history ) {
		Server s;
		const Server *live = e.kind == "server" ? Find( e.key ) : nullptr;
		if ( live ) {
			s = *live;
		} else if ( e.kind != "match" || !ListedMatch( e.key, s ) ) {
			( e.kind == "match" ? s.host : s.address ) = e.key;
			s.name = e.name;
			s.rich = e.rich.empty() ? Rml::StringUtilities::EncodeRml( e.name ) : e.rich;
			s.gameName = e.game;
			s.kind = e.gameKind.empty() ? "other" : e.gameKind;
			s.mapName = e.map;
			s.mode = e.mode;
			s.up = false;
			s.status = e.kind == "match" ? "not hosting now" : asking ? "asking" : "no answer";
			s.search = Lower( s.name + "\n" + s.mapName + "\n" + s.mode + "\n" + s.gameName + "\n" + e.key );
		}
		s.at = e.at;
		s.when = f.tab == "history" ? When( e.at ) : "";
		pool.push_back( s );
	}
	return pool;
}

// the rows, the status line and what the list says when it shows nothing
void Build( const Filters &f, bool filtered ) {
	const bool saved = f.tab == "favorites" || f.tab == "history";
	const std::vector<Server> pool = Pool( f );
	std::vector<const Server *> fit;
	for ( const Server &s : pool ) {
		// favorites and history: every entry, up or not, of the game, mode and search
		if ( saved ? GameFits( f, s.kind ) && ( f.mode == "Any mode" || f.mode == s.mode ) && ( f.query.empty() || s.search.find( f.query ) != std::string::npos )
			: Fits( f, s ) ) {
			fit.push_back( &s );
		}
	}
	if ( f.tab != "history" ) {	// the history stays newest first
		std::stable_sort( fit.begin(), fit.end(), [&]( const Server *a, const Server *b ) { return Before( f, *a, *b ); } );
	}
	auto friends = Friends();

	cJSON *rows = Menu_Node( "servers.rows" );
	if ( !rows ) {
		return;
	}
	// the server a row with the focus holds: the focus stays with it when the rows move (a row is an element per place)
	Rml::ElementDocument *doc = !strcmp( TopPageName(), "server_browser" ) ? TopDocument() : nullptr;
	std::string control, held;
	const int heldAt = FocusedRow( control );
	if ( heldAt >= 0 ) {
		const cJSON *row = cJSON_GetArrayItem( rows, heldAt );
		const cJSON *a = row ? cJSON_GetObjectItemCaseSensitive( row, "address" ) : nullptr;
		held = cJSON_IsString( a ) ? a->valuestring : "";
	}
	cJSON *out = cJSON_CreateArray();
	for ( size_t i = 0; i < fit.size() && (int)i < MAX_ROWS; i++ ) {
		const Server &s = *fit[i];
		const int bots = s.humans >= 0 ? s.clients - s.humans : 0;
		char players[32];
		snprintf( players, sizeof( players ), "%d/%d", s.clients, s.max );
		cJSON *o = cJSON_CreateObject();
		cJSON_AddStringToObject( o, "address", s.address.c_str() );
		cJSON_AddStringToObject( o, "name", s.name.c_str() );
		cJSON_AddStringToObject( o, "rich", s.rich.c_str() );
		cJSON_AddStringToObject( o, "game", s.gameName.c_str() );
		cJSON_AddStringToObject( o, "kind", s.kind.c_str() );
		cJSON_AddStringToObject( o, "map", s.mapName.c_str() );
		cJSON_AddStringToObject( o, "mapfile", s.map.c_str() );
		cJSON_AddStringToObject( o, "mode", s.mode.c_str() );
		cJSON_AddStringToObject( o, "players", players );
		cJSON_AddStringToObject( o, "bots", bots > 0 ? ( std::to_string( bots ) + ( bots == 1 ? " bot" : " bots" ) ).c_str() : "" );
		cJSON_AddStringToObject( o, "ping", std::to_string( s.ping ).c_str() );
		cJSON_AddBoolToObject( o, "full", s.max > 0 && s.clients >= s.max );
		cJSON_AddBoolToObject( o, "password", s.password );
		cJSON_AddBoolToObject( o, "look", s.kind != "other" && ( !s.pure || s.kind == "oz" ) );	// the full Oz look (decision 16)
		cJSON_AddBoolToObject( o, "have", s.have );
		cJSON_AddStringToObject( o, "needs", Needs( s ).c_str() );
		cJSON_AddStringToObject( o, "engine", s.engine.c_str() );
		cJSON_AddStringToObject( o, "friends", FriendsText( (int)friends[s.address].size() ).c_str() );
		cJSON_AddStringToObject( o, "host", s.host.c_str() );
		cJSON_AddBoolToObject( o, "up", s.up );
		cJSON_AddStringToObject( o, "status", s.status.c_str() );
		cJSON_AddStringToObject( o, "when", s.when.c_str() );
		cJSON_AddBoolToObject( o, "fav", s.host.empty() ? IsFavorite( "server", s.address ) : IsFavorite( "match", s.host ) );
		cJSON_AddItemToArray( out, o );
	}
	Replace( rows, out );
	numRows = std::min( (int)fit.size(), MAX_ROWS );
	viewStale = true;
	int moved = -1;		// the held server's new place
	for ( int i = 0; !held.empty() && i < numRows; i++ ) {
		if ( fit[i]->address == held ) {
			moved = i;
			break;
		}
	}
	if ( filtered ) {	// a new filter or order: back to the top of the list
		if ( Rml::Element *list = doc ? doc->GetElementById( "sb_list" ) : nullptr ) {
			list->SetScrollTop( 0.0f );
		}
		viewFirst = viewLast = 0;
		const int was = first;
		SetWindow( 0, 2 * MARGIN );
		if ( first != was || heldAt >= last ) {	// else each element holds the same place
			Refocus( heldAt, control );
		}
	} else if ( moved >= 0 && moved != heldAt ) {
		SetWindow( moved - MARGIN, moved + MARGIN );	// around the server's new place, scrolled to it (the window follows)
		Refocus( moved, control, true );
	} else {
		const int was = first;
		SetWindow( first, std::max( last, first + 2 * MARGIN ) );
		if ( first != was || heldAt >= last ) {
			Refocus( heldAt, control );
		}
	}

	int listed = 0, answered = 0;
	const ozServersState_t state = OzServers_State( &listed, nullptr, &answered );
	const int matches = MatchesShown( f ), shown = (int)fit.size();
	std::string text, title, why;
	switch ( state ) {
	case OZS_IDLE:
		title = OzServers_MayAsk() ? "Looking for servers" : "Servers aren't asked for";
		why = OzServers_MayAsk() ? "Asking the master servers for every server they list." : "Asking the master servers is off here (oz_servers_ask).";
		break;
	case OZS_MASTERS:
		text = "Asking the master servers";
		title = "Looking for servers";
		why = "Asking the master servers for every server they list.";
		break;
	case OZS_SERVERS:
		text = "Asking " + Thousands( listed ) + " servers: " + Thousands( answered ) + " answered";
		title = "Looking for servers";
		why = Thousands( answered ) + " answered so far.";
		break;
	case OZS_DONE:
		text = shown > MAX_ROWS ? "The first " + Thousands( MAX_ROWS ) + " of " + Thousands( shown ) + " servers that fit" :
			Thousands( shown ) + ( shown == 1 ? " server" : " servers" ) + " of " + Thousands( answered ) + " answered";
		title = "No server fits the filters";
		why = Thousands( answered ) + " servers answered. Game, Mode, Ping and the toggles show more.";
		break;
	case OZS_NO_MASTERS:
		text = "No master server answered";
		title = "No master server answered";
		why = "The game finds servers through the master servers. Check the internet connection, then Refresh.";
		break;
	case OZS_LOCAL_SERVER:
		text = "A match runs on this PC";
		title = "Leave your match to look for servers";
		why = "While a match runs on this PC, its server takes every answer the other servers send.";
		break;
	}
	if ( f.game == "Oz Arena" && !Online() && !shown ) {
		title = !strcmp( Menu_Get( "social.status" ), "connecting" ) ? "Connecting to the online service" : "Offline";
		why = "Player-hosted matches are listed by the online service. It comes back by itself; Settings, Network turns it on or off.";
	} else if ( f.game == "Oz Arena" && !shown && state == OZS_DONE ) {
		title = "No open matches";
		why = "Host one from Custom Match with the server set to Online: it shows here for everyone.";
	}
	// the other tabs say their own
	const int all = (int)pool.size();
	if ( f.tab == "lan" ) {
		const ozServersState_t lanState = OzServers_LanState();
		text = lanState == OZS_SERVERS ? "Looking on your network" : lanState == OZS_LOCAL_SERVER ? "A match runs on this PC"
			: Thousands( all ) + ( all == 1 ? " server" : " servers" ) + " on your network";
		title = lanState == OZS_SERVERS ? "Looking on your network" : lanState == OZS_LOCAL_SERVER ? "Leave your match to look" : all ? "No server fits the filters" : "No LAN servers";
		why = lanState == OZS_LOCAL_SERVER ? "While a match runs on this PC, its server takes every answer the other servers send."
			: all ? "Game, Mode, Ping and the toggles show more."
			: "Nothing answered on your local network. A LAN match is hosted from Create Match (LAN), or a dedicated server with a UDP port.";
	} else if ( f.tab == "favorites" ) {
		text = Thousands( all ) + ( all == 1 ? " favorite" : " favorites" );
		title = all ? "No favorite fits the filters" : "No favorites yet";
		why = all ? "Game, Mode and the search box show more." : "Star a server or a match, in its row or on its card, to keep it here.";
	} else if ( f.tab == "history" ) {
		text = all ? "The last " + Thousands( all ) + ( all == 1 ? " place" : " places" ) + " you joined" : "";
		title = all ? "Nothing joined fits the filters" : "Nothing joined yet";
		why = all ? "Game, Mode and the search box show more." : "The servers and matches you join show here, newest first.";
	}
	SetText( "servers.text", text );
	SetText( "servers.empty_title", title );
	SetText( "servers.empty_text", why );
	Menu_Set( "servers.shown", std::to_string( std::min( shown, MAX_ROWS ) + ( f.tab == "internet" ? matches : 0 ) ).c_str() );
	Menu_Dirty( "servers" );
	builtAt = OzE_Milliseconds();
}

// a star on a row or a card: the favorite added (as the place is now) or taken off
void ToggleFavorite( const std::string &kind, const std::string &key ) {
	LoadSaved();
	for ( size_t i = 0; i < favorites.size(); i++ ) {
		if ( favorites[i].kind == kind && favorites[i].key == key ) {
			favorites.erase( favorites.begin() + i );
			SaveSaved();
			Menu_Toast( "Taken off your favorites.", "info" );
			return;
		}
	}
	Server m;
	favorites.insert( favorites.begin(), SavedFrom( kind, key, kind == "server" ? Find( key ) : ListedMatch( key, m ) ? &m : nullptr ) );
	SaveSaved();
	Menu_Toast( "Added to your favorites.", "info" );
}

// a join: the history's newest entry (a place joined before moves to the top)
void Joined( const std::string &kind, const std::string &key, const Server *s ) {
	LoadSaved();
	for ( size_t i = 0; i < history.size(); i++ ) {
		if ( history[i].kind == kind && history[i].key == key ) {
			history.erase( history.begin() + i );
			break;
		}
	}
	history.insert( history.begin(), SavedFrom( kind, key, s ) );
	if ( history.size() > HISTORY ) {
		history.resize( HISTORY );
	}
	SaveSaved();
}

void Refuse( const char *why ) {
	PlaySound( Sound::Buzz );
	Menu_Toast( why, "warn" );
}

void ClearAsk() {
	SetText( "server_browser.ask.address", "" );
	SetText( "server_browser.ask.name", "" );
}

// a server's name by its letters and digits alone, lowercase
std::string NameKey( const std::string &name ) {
	std::string key;
	for ( const char c : name ) {
		if ( isalnum( (unsigned char)c ) ) {
			key += (char)tolower( (unsigned char)c );
		}
	}
	return key;
}

// an address as connect takes it and nothing else: it goes into a console command, which a ';', a quote or a new line
// in it would end (addresses come from saved files, friends' presence and the party leader too, each checked where
// it arrives; this is the last door)
bool AddressText( const std::string &address ) {
	if ( address.empty() ) {
		return false;
	}
	for ( const char c : address ) {
		if ( !isalnum( (unsigned char)c ) && ( c == '\0' || !strchr( ".-_:[]", c ) ) ) {
			return false;
		}
	}
	return true;
}

// connect, with the server's password or none, the pages closed for the connection screen
void Connect( const std::string &joined, const Server *s, const std::string &password ) {
	std::string address = joined;
	if ( !AddressText( address ) || password.find_first_of( "\"\r\n" ) != std::string::npos ) {
		Printf( "UI: not joining: \"%s\" isn't a server's address, or its password can't be passed on\n",
			AddressText( address ) ? address.c_str() : "?" );
		Refuse( "That isn't a server's address." );
		return;
	}
	if ( s && s->protocol != 68 ) {
		// a favorite, the history or a row folded apart may name the game's other address (Better): join its Quake III
		// one, the same server on the same machine. Not by the player counts, which change between answers, nor the name
		// as written: Area 51's OpenArena door is "*A51* FFA", its Quake III ones "A51|FFA".
		const std::string ip = address.substr( 0, address.rfind( ':' ) );
		for ( const Server &t : answers ) {
			if ( t.protocol == 68 && t.address.substr( 0, t.address.rfind( ':' ) ) == ip && NameKey( t.name ) == NameKey( s->name ) &&
				t.game == s->game && t.mode == s->mode ) {
				Printf( "UI: %s is %s's protocol %d address: joining its Quake III one, %s\n", address.c_str(), s->name.c_str(),
					s->protocol, t.address.c_str() );
				address = t.address;
				s = &t;
				break;
			}
		}
	}
	if ( s && s->kind == "game" ) {
		Menu_Toast( ( s->name + " runs " + s->gameName + ", another game: its server may not take you." ).c_str(), "info" );
	} else if ( s && s->kind == "other" ) {
		Menu_Toast( ( s->name + " runs " + s->gameName + ": other mods may not play right." ).c_str(), "info" );
	} else if ( s && !s->have ) {
		Menu_Toast( ( s->gameName + " isn't in your game folder: the server hands its files over as you join, where it offers them." ).c_str(), "info" );
	}
	Printf( "UI: joining %s (%s, %s)\n", address.c_str(), s ? s->game.c_str() : "?", s ? s->name.c_str() : "?" );
	Joined( "server", address, s );
	OzE_ExecuteNow( ( "set password \"" + password + "\"\n" ).c_str() );
	CloseAllPages();
	// a party leader's party follows them onto the server (PH step 3): the hub's party_follow, after the connect
	OzE_InsertText( ( "connect " + address + "\n" + Social_FollowText( "server", address.c_str(), password.c_str() ) ).c_str() );
}

// the masters asked again, and the saved servers with them (those they don't list too: a private server, a LAN one)
void Refresh() {
	if ( OzServers_MayAsk() ) {
		previous.clear();
		for ( const Server &s : answers ) {
			previous[s.address] = s;
		}
		OzServers_Refresh();
		LoadSaved();
		for ( const auto *list : { &favorites, &history } ) {
			for ( const Saved &e : *list ) {
				if ( e.kind == "server" ) {
					OzServers_Also( e.key.c_str() );
				}
			}
		}
	}
	builtWith.clear();
}

/*
================
The server card (Info on a row): the server's status (getstatus, oz_servers.c over Quake3e's
CL_ServerStatus): the version of its engine and mod, CPMA's and Defrag's physics, and its players
with their scores and pings (a bot's is 0), friends among them (presence, and the names they play
under). Asked again every CARD_MS while it shows; "No answer" when the first ask gets none in CARD_WAIT.
================
*/
const int CARD_MS = 5000, CARD_WAIT = 4000;
struct {
	std::string address;
	bool open = false, waiting = false, answered = false;
	int askedAt = 0;
} card;

std::string CardText( const std::map<std::string, std::string> &kv, const std::string &kind ) {
	// "Q3 1.32e linux-x86_64 Aug 4 2026" -> "Q3 1.32e"; "ioq3 1.36_GIT_6d748965-..." -> "ioq3 1.36"
	std::string engine = Value( kv, "version" );
	size_t cut = engine.find( ' ' );
	cut = cut == std::string::npos ? cut : engine.find( ' ', cut + 1 );
	engine = engine.substr( 0, cut );
	engine = engine.substr( 0, engine.find( '_' ) );
	std::string mod;
	if ( kind == "cpma" && !Value( kv, "gameversion" ).empty() ) {
		mod = "CPMA " + Value( kv, "gameversion" );
	} else if ( kind == "defrag" && Value( kv, "defrag_vers" ).size() == 5 ) {
		const std::string v = Value( kv, "defrag_vers" );	// 19131: 1.91.31
		mod = "Defrag " + v.substr( 0, 1 ) + "." + v.substr( 1, 2 ) + "." + v.substr( 3 );
	}
	return mod.empty() ? engine : engine.empty() ? mod : mod + " on " + engine;
}

std::string CardPhysics( const std::map<std::string, std::string> &kv, const std::string &kind ) {
	if ( kind == "cpma" ) {
		const std::string play = Value( kv, "server_gameplay" ), mode = Value( kv, "mode_current" );
		return play + ( !play.empty() && !mode.empty() ? ", mode " : "" ) + mode;
	}
	if ( kind == "defrag" && !Value( kv, "df_promode" ).empty() ) {
		return Value( kv, "df_promode" ) == "1" ? "CPM" : "VQ3";
	}
	return "";
}

// the status's info and players: "\k\v...\k\v" "\" then "\<score> <ping> "<name>"" each, then "\"
void CardStatus( const std::string &status, const Server *s ) {
	std::vector<std::string> tok;
	for ( size_t a = 0, b; a <= status.size(); a = b + 1 ) {
		b = status.find( '\\', a );
		b = b == std::string::npos ? status.size() : b;
		tok.push_back( status.substr( a, b - a ) );
	}
	std::map<std::string, std::string> kv;
	size_t i = 1;
	while ( i < tok.size() && !tok[i].empty() ) {	// a key is never empty: the empty one ends the info
		kv[Lower( tok[i] )] = i + 1 < tok.size() ? tok[i + 1] : "";
		i += 2;
	}
	const std::string kind = s ? s->kind : "vanilla";
	std::set<std::string> friends;
	const auto where = Friends();	// kept: a range-for over an element of the temporary would read a freed map
	const auto here = where.find( card.address );
	for ( const std::string &n : here != where.end() ? here->second : std::vector<std::string>() ) {
		friends.insert( Lower( n ) );
	}
	struct P { std::string rich, name; int score, ping; };
	std::vector<P> players;
	for ( i++; i < tok.size(); i++ ) {
		int score = 0, ping = 0;
		const size_t q = tok[i].find( '"' ), r = tok[i].rfind( '"' );
		if ( q == std::string::npos || r <= q || sscanf( tok[i].c_str(), "%d %d", &score, &ping ) != 2 ) {
			continue;
		}
		const std::string name = tok[i].substr( q + 1, r - q - 1 );
		players.push_back( { Rich( name ), Clean( name ), score, ping } );
	}
	std::stable_sort( players.begin(), players.end(), []( const P &a, const P &b ) { return a.score > b.score; } );
	cJSON *list = cJSON_CreateArray();
	int bots = 0;
	for ( const P &p : players ) {
		const bool isFriend = friends.count( Lower( p.name ) ) > 0;
		cJSON *o = cJSON_CreateObject();
		cJSON_AddStringToObject( o, "rich", p.rich.empty() ? "?" : p.rich.c_str() );
		cJSON_AddNumberToObject( o, "score", p.score );
		cJSON_AddStringToObject( o, "ping", p.ping ? std::to_string( p.ping ).c_str() : "bot" );
		cJSON_AddBoolToObject( o, "bot", p.ping == 0 );
		cJSON_AddBoolToObject( o, "friend", isFriend );
		cJSON_AddItemToArray( list, o );
		bots += p.ping == 0;
	}
	if ( cJSON *node = Menu_Node( "servers.card.list" ) ) {
		Replace( node, list );
	} else {
		cJSON_Delete( list );
	}
	const int humans = (int)players.size() - bots;
	const std::string engine = EngineTag( Value( kv, "version" ) );
	if ( !engine.empty() && engines[card.address] != engine ) {
		engines[card.address] = engine;	// the row's engine tag, from now on
		for ( auto *list : { &servers, &answers, &lanServers } ) {
			for ( Server &x : *list ) {
				if ( x.address == card.address ) {
					x.engine = engine;
				}
			}
		}
		builtWith.clear();
	}
	SetText( "servers.card.engine", engine );
	SetText( "servers.card.version", CardText( kv, kind ) );
	SetText( "servers.card.physics", CardPhysics( kv, kind ) );
	SetText( "servers.card.who", players.empty() ? "Nobody's playing" : std::to_string( humans ) + ( humans == 1 ? " player" : " players" ) +
		( bots ? ", " + std::to_string( bots ) + ( bots == 1 ? " bot" : " bots" ) : "" ) );
	SetText( "servers.card.status", "ok" );
	Menu_Dirty( "servers" );
}

void CardAsk( bool again ) {
	char buf[8192];
	card.askedAt = OzE_Milliseconds();
	card.waiting = true;
	if ( OzServers_PlayerStatus( card.address.c_str(), buf, sizeof( buf ), again ) ) {
		card.waiting = false;
		card.answered = true;
		CardStatus( buf, Find( card.address ) );
	}
}

void CardOpen( const std::string &address ) {
	const Server *s = Find( address );
	card = {};
	card.address = address;
	card.open = true;
	SetText( "servers.card.address", address );
	SetText( "servers.card.rich", s ? s->rich : Rml::StringUtilities::EncodeRml( address ) );
	SetText( "servers.card.name", s ? s->name : address );
	SetText( "servers.card.game", s ? s->gameName : "" );
	SetText( "servers.card.kind", s ? s->kind : "other" );
	SetText( "servers.card.map", s ? s->mapName + ( s->mapName != s->map ? " (" + s->map + ")" : "" ) : "" );
	SetText( "servers.card.mode", s ? s->mode : "" );
	SetText( "servers.card.ping", s ? std::to_string( s->ping ) + " ms" : "" );
	SetText( "servers.card.engine", s ? s->engine : "" );
	SetText( "servers.card.needs", s ? Needs( *s ) : "" );
	SetText( "servers.card.version", "" );
	SetText( "servers.card.physics", "" );
	SetText( "servers.card.who", "" );
	SetText( "servers.card.status", "asking" );
	std::string friends;
	const auto where = Friends();
	const auto here = where.find( address );
	for ( const std::string &n : here != where.end() ? here->second : std::vector<std::string>() ) {
		friends += ( friends.empty() ? "" : ", " ) + n;
	}
	SetText( "servers.card.friends", friends );
	Menu_Set( "servers.card.password", s && s->password ? "true" : "false" );
	Menu_Set( "servers.card.fav", IsFavorite( "server", address ) ? "true" : "false" );
	Menu_Set( "servers.card.look", s && s->kind != "other" && ( !s->pure || s->kind == "oz" ) ? "true" : "false" );
	if ( cJSON *node = Menu_Node( "servers.card.list" ) ) {
		Replace( node, cJSON_CreateArray() );
	}
	Menu_Set( "servers.card.open", "true" );
	CardAsk( true );
	GetContext()->Update();
	if ( Rml::ElementDocument *doc = TopDocument() ) {
		FocusQuietly( doc->GetElementById( "sb_card_join" ) );
	}
}

void CardClose() {
	if ( !card.open ) {
		return;
	}
	card.open = false;
	Menu_Set( "servers.card.open", "false" );
	GetContext()->Update();
	if ( Rml::ElementDocument *doc = TopDocument() ) {	// back to the server's row, where it's laid out
		for ( int i = first; i < last; i++ ) {
			const cJSON *row = cJSON_GetArrayItem( Menu_Node( "servers.rows" ), i );
			const cJSON *a = row ? cJSON_GetObjectItemCaseSensitive( row, "address" ) : nullptr;
			if ( cJSON_IsString( a ) && card.address == a->valuestring ) {
				FocusQuietly( doc->GetElementById( ( "sb_sv_" + std::to_string( i ) ).c_str() ) );
				break;
			}
		}
	}
}

void CardFrame() {
	if ( !card.open ) {
		return;
	}
	const int now = OzE_Milliseconds();
	if ( card.waiting ) {
		char buf[8192];
		if ( OzServers_PlayerStatus( card.address.c_str(), buf, sizeof( buf ), 0 ) ) {
			card.waiting = false;
			card.answered = true;
			CardStatus( buf, Find( card.address ) );
		} else if ( !card.answered && now - card.askedAt > CARD_WAIT && strcmp( Menu_Get( "servers.card.status" ), "none" ) ) {
			SetText( "servers.card.status", "none" );
		}
	} else if ( now - card.askedAt > CARD_MS ) {
		CardAsk( true );
	}
}

/*
================
A mod the player doesn't have (ENGINE_PLAN.md decision 24): joining a CPMA or Defrag server without
it opens this first. Import the zip or the folder they got from its makers, or the copy in their
install the file system doesn't find (oz_mods.c), or join anyway: a server may hand its files over.
Oz never downloads the mod or points at where to get it.
================
*/
struct { std::string address; bool open = false; } modAsk;
const int PATH_TEXT = 1024;

void ModOpen( const Server &s ) {
	char found[PATH_TEXT];
	OzMods_InInstall( s.game.c_str(), found, sizeof( found ) );
	modAsk.address = s.address;
	modAsk.open = true;
	SetText( "servers.mod.address", s.address );
	SetText( "servers.mod.dir", s.game );
	SetText( "servers.mod.game", s.gameName );
	SetText( "servers.mod.name", s.name );
	SetText( "servers.mod.found", found );
	SetText( "servers.mod.status", "" );
	SetText( "servers.mod.message", "" );
	Menu_Set( "servers.mod.open", "true" );
	Printf( "UI: %s isn't here: asking about importing it (%s)\n", s.game.c_str(), found[0] ? found : "no copy in the install" );
	GetContext()->Update();
	if ( Rml::ElementDocument *doc = TopDocument() ) {
		FocusQuietly( doc->GetElementById( found[0] ? "sb_mod_install" : "sb_mod_zip" ) );
	}
}

void ModClose() {
	modAsk.open = false;
	Menu_Set( "servers.mod.open", "false" );
}

// modimport zip|folder|install [path]: the picker (or the path, for tests), the import, what it took or why not
void ModImport( const std::string &how, const std::string &given ) {
	char path[PATH_TEXT], message[256];
	if ( !given.empty() ) {
		snprintf( path, sizeof( path ), "%s", given.c_str() );
	} else if ( how == "install" ) {
		snprintf( path, sizeof( path ), "%s", Menu_Get( "servers.mod.found" ) );
	} else if ( !OzMods_Pick( how == "folder", path, sizeof( path ) ) ) {
		return;	// cancelled
	}
	const int n = OzMods_Import( path, Menu_Get( "servers.mod.dir" ), message, sizeof( message ) );
	SetText( "servers.mod.status", n > 0 ? "done" : "failed" );
	SetText( "servers.mod.message", n > 0 ? std::string( "Imported " ) + message : message );
	if ( n > 0 ) {
		PlaySound( Sound::In );
		GetContext()->Update();
		if ( Rml::ElementDocument *doc = TopDocument() ) {
			FocusQuietly( doc->GetElementById( "sb_mod_join" ) );
		}
	} else {
		PlaySound( Sound::Buzz );
	}
}

}	// namespace

namespace OzUI {

// the server rows' actions (Menu_Run asks here before the online pages'): true when taken
//   servers               ask the master servers again (the Refresh button, with browse)
//   serverjoin <address> [locked]  join a server (one with a password asks for it first: locked from Friends)
//   serverinfo <address>  the server card: its status, version, physics and players; cardclose closes it
//   favorite server <address> | match <host>, historyclear   the saved lists
//   modimport zip|folder|install [path], modjoin, modclose   the question about a mod the player doesn't have
//   sbsort <column>       sort the rows by a column (players, ping, name, game, map, mode); again turns it around
//   joinask, askcancel    the password prompt, for a server's address (a peer-to-peer match's are oz_ui_social.cpp's;
//                         askcancel empties the field for both)
bool Servers_Action( const Rml::String &verb, const Rml::String &arg, Rml::Element *element ) {
	const bool asking = Menu_Get( "server_browser.ask.address" )[0] != '\0';
	if ( verb == "servers" ) {
		if ( !strcmp( Menu_Get( "server_browser.tab" ), "lan" ) ) {
			OzServers_LanRefresh();
		} else {
			Refresh();
		}
	} else if ( verb == "favorite" ) {
		// favorite server <address> | match <host>: the star on a row or a card
		const std::string kind = arg.substr( 0, arg.find( ' ' ) ), key = arg.find( ' ' ) == Rml::String::npos ? "" : arg.substr( arg.find( ' ' ) + 1 );
		if ( ( kind == "server" || kind == "match" ) && !key.empty() ) {
			ToggleFavorite( kind, key );
			if ( card.open && kind == "server" && key == card.address ) {
				Menu_Set( "servers.card.fav", IsFavorite( "server", key ) ? "true" : "false" );
			}
		}
	} else if ( verb == "historyclear" ) {
		LoadSaved();
		history.clear();
		SaveSaved();
	} else if ( verb == "sbsort" ) {
		if ( arg == Menu_Get( "server_browser.sort" ) ) {
			Menu_Set( "server_browser.sort_rev", strcmp( Menu_Get( "server_browser.sort_rev" ), "true" ) ? "true" : "false" );
		} else {
			Menu_Set( "server_browser.sort", arg.c_str() );
			Menu_Set( "server_browser.sort_rev", "false" );
		}
	} else if ( verb == "serverinfo" ) {
		CardOpen( arg );
	} else if ( verb == "cardclose" ) {
		CardClose();
	} else if ( verb == "modimport" ) {
		ModImport( arg.substr( 0, arg.find( ' ' ) ), arg.find( ' ' ) == Rml::String::npos ? "" : arg.substr( arg.find( ' ' ) + 1 ) );
	} else if ( verb == "modjoin" ) {
		const std::string address = modAsk.address;
		ModClose();
		return Servers_Action( "serverjoin", address + " anyway", element );
	} else if ( verb == "modclose" ) {
		ModClose();
	} else if ( verb == "serverjoin" ) {
		// serverjoin <address> [locked] [anyway]: locked, a friend's server that takes a password (presence says so);
		// anyway, past the question about a mod the player doesn't have
		const std::string address = arg.substr( 0, arg.find( ' ' ) );
		const bool locked = arg.find( " locked" ) != Rml::String::npos;
		const Server *s = Find( address );
		const std::string join = Menu_Get( "server_browser.join.status" );
		card.open = false;
		Menu_Set( "servers.card.open", "false" );
		if ( join == "asking" || join == "connecting" ) {
			Refuse( "Already joining a match. Cancel that first." );
		} else if ( s && !Needs( *s ).empty() && arg.find( " anyway" ) == Rml::String::npos ) {
			ModOpen( *s );	// a mod the player doesn't have: the import flow first (PH step 1)
		} else if ( ( s && s->password ) || locked ) {
			SetText( "server_browser.ask.host", "" );
			SetText( "server_browser.ask.address", address );
			SetText( "server_browser.ask.name", s ? s->name : address );
			if ( strcmp( TopPageName(), "server_browser" ) ) {
				OpenPageByName( "server_browser" );	// from Friends: the prompt is the browser's
			}
			GetContext()->Update();
			if ( Rml::ElementDocument *doc = TopDocument() ) {
				FocusQuietly( doc->GetElementById( "sb_password" ) );
			}
		} else {
			Connect( address, s, "" );
		}
	} else if ( verb == "joinask" && asking ) {
		Rml::ElementDocument *doc = element ? element->GetOwnerDocument() : TopDocument();
		Rml::Element *input = doc ? doc->GetElementById( "sb_password" ) : nullptr;
		const Rml::String password = input ? input->GetAttribute<Rml::String>( "value", "" ) : "";
		if ( password.find( '"' ) != Rml::String::npos ) {
			Refuse( "A password can't have quotation marks." );
			return true;
		}
		const std::string address = Menu_Get( "server_browser.ask.address" );
		ClearAsk();
		if ( input ) {
			input->SetAttribute( "value", "" );
		}
		Connect( address, Find( address ), password );
	} else if ( verb == "askcancel" ) {
		// the field empties, or the next prompt (another server's, or a match's) starts with this password in it
		Rml::ElementDocument *doc = element ? element->GetOwnerDocument() : TopDocument();
		if ( Rml::Element *input = doc ? doc->GetElementById( "sb_password" ) : nullptr ) {
			input->SetAttribute( "value", "" );
		}
		if ( !asking ) {
			return false;	// a peer-to-peer match's prompt: oz_ui_social.cpp closes it
		}
		ClearAsk();
	} else {
		return false;
	}
	return true;
}

// the Server Browser opened: the masters asked again when the list is old (or was never asked for)
void Servers_Opened( const char *page ) {
	if ( strcmp( page, "server_browser" ) ) {
		return;
	}
	const int age = OzServers_Age();
	if ( age < 0 || age > STALE_MS ) {
		Refresh();
	}
	builtWith.clear();
	lastTab.clear();	// opened on the LAN tab: it asks again
}

// the hub's rows came again (oz_ui_social.cpp): their shown flags at the next frame
void Servers_MatchesChanged() {
	matchesChanged = true;
}

// a peer-to-peer match joined (oz_ui_social.cpp, oz_ice.c's joined): the history's newest entry
void Servers_JoinedMatch( const char *host, const char *name ) {
	Server m;
	if ( !ListedMatch( host, m ) ) {
		m.name = m.rich = name;
		m.kind = "oz";
		m.gameName = "Oz Arena";
	}
	Joined( "match", host, &m );
}

// the server a connection is for, as the Server Browser names it ("" when it isn't one of its rows)
std::string Servers_Name( const char *address ) {
	const Server *s = Find( address );
	return s ? s->name : "";
}

// the party leader joined a community server (oz_ui_social.cpp's net_follow, PH step 3): joined as from a row, with
// the leader's password; a mod the player doesn't have asks about importing it first, in the browser
void Servers_FollowServer( const char *address, const char *password ) {
	const Server *s = Find( address );
	if ( s && !Needs( *s ).empty() ) {
		if ( strcmp( TopPageName(), "server_browser" ) ) {
			OpenPageByName( "server_browser" );
		}
		ModOpen( *s );
		return;
	}
	Connect( address, s, password );
}

// each frame while a page is open: the rows follow the sweep (at most every REBUILD_MS) and the filters (at once)
void Servers_Frame() {
	if ( !IsPageOpen( "server_browser" ) ) {
		return;
	}
	Rml::ElementDocument *doc = !strcmp( TopPageName(), "server_browser" ) ? TopDocument() : nullptr;
	if ( Rml::Element *list = doc ? doc->GetElementById( "sb_list" ) : nullptr ) {
		list->SetClass( "moving", false );	// the frame after a Refocus drew without the fade
	}
	// while the sweep runs every answer changes the generation, and parsing them all again each frame cost more with
	// each one (1,200 answers a frame at its end): then at most every REBUILD_MS, as the rows are built
	const int gen = OzServers_Generation();
	if ( gen != parsedGen ) {
		const ozServersState_t sweep = OzServers_State( nullptr, nullptr, nullptr );
		const int now = OzE_Milliseconds();
		if ( ( sweep != OZS_MASTERS && sweep != OZS_SERVERS ) || now - parsedAt >= REBUILD_MS ) {
			Parse();
			parsedGen = gen;
			parsedAt = now;
		}
	}
	const Filters f = Read();
	if ( f.tab == "lan" && lastTab != "lan" ) {
		OzServers_LanRefresh();	// the LAN tab asks its network each time it's opened
	}
	lastTab = f.tab;
	std::string where;	// where the friends are, and the saved lists' changes: their rows follow without moving the list
	for ( const auto &p : Friends() ) {
		where += p.first + " " + std::to_string( p.second.size() ) + ";";
	}
	const std::string with = Key( f ) + "\x01" + where + std::to_string( savedGen ) + "\x01" + std::to_string( parsedGen );	// what was parsed, not what's newest
	const bool filters = Key( f ) != builtWith.substr( 0, builtWith.find( '\x01' ) );
	if ( ( with != builtWith || matchesChanged ) && ( filters || matchesChanged || OzE_Milliseconds() - builtAt >= REBUILD_MS ) ) {
		matchesChanged = false;
		builtWith = with;
		Build( f, filters );
	}
	Window();
	CardFrame();
}

// Escape or B: the mod's question or the server card closes first
bool Servers_Back() {
	if ( modAsk.open ) {
		ModClose();
	} else if ( card.open ) {
		CardClose();
	} else {
		return false;
	}
	PlaySound( Sound::Out );
	return true;
}

}	// namespace OzUI
