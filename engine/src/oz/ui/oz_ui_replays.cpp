/*
===========================================================================
Oz Arena: the Replays page (ENGINE_PLAN.md P10 steps 4 and 6). GPL-2.0.

Every recording in the user folder, newest first (oz_replay_lib.h has the two kinds):
  - replays/*.q3replay, matches a server in this game recorded whole (sv_oz_replay.c): their header
    and trailer say the map, mode, length and standings; Watch plays them from any player's eyes;
  - <mod>/demos/*.dm_68 (and the other protocols), your view of a match on another server
    (cl_oz_demos.c), or any demo you put there: replays/library.json holds what Oz saw of the match
    it recorded; any other demo is read for its map, server, players and length (OzDemo_Info).
The picked one's detail is replays.cur; its name is saved as it's typed. Favorite, Delete (to the
Recycle Bin, after a question) and Show folder (the recording's folder).

  replays.list	[{id, kind, kind_text, tile, map_name, name, favorite, sub, when, length, badge, tone}]
  replays.cur	{id, kind, map_name, mode_name, when, shot, name, favorite, length, result, size, where, views,
				 server_kind, complete, players: [{n, client, name, score, bot, local, eyes}]}
  replay_pick <id>, replay_watch [<id> [<client>]], replay_play <key> [<client>] (the Profile's rows),
  replay_fav, replay_delete, replay_delete_go, replay_delete_no, replay_folder, replay_tab all|favorites
  ui_replays	prints the page's state, for tests
===========================================================================
*/
#include "oz_ui_internal.h"
#include "cJSON.h"
#include "../replay/oz_replay_lib.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace OzUI {
cJSON *Menu_Node( const char *path );	// oz_ui_menu.cpp
void Menu_Dirty( const char *path );
}
using namespace OzUI;
namespace fs = std::filesystem;

namespace {

const char *const PAGE = "replays";

struct Player {
	int client = -1, score = 0, team = 0;
	bool bot = false, hasScore = false;
	std::string name;
};

struct Rec {
	std::string key, kind, path;		// kind: "replay" or "demo"
	std::string map, mapTitle, modeName, hostname, server, userName;
	std::string game;					// a replay: the mod that recorded it, as its header says
	bool favorite = false, complete = true, finished = true, local = false, hasWon = false, won = false, teams = false;
	long long size = 0, unixStart = 0;
	int lengthMs = -1, gametype = -1, me = -1, place = -1, red = 0, blue = 0;
	std::vector<Player> players;
};

std::vector<Rec> recs;
std::string pickKey;
std::string savedName;		// the picked one's name as the library has it: a change is saved as it's typed
int nameChangedAt = 0;
std::string lastTyped;

// ---------------------------------------------------------------- the model

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

void SetBool( const char *path, bool v ) {
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

std::string Str( const cJSON *o, const char *k, const char *def = "" ) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive( o, k );
	return cJSON_IsString( v ) && v->valuestring ? v->valuestring : def;
}

double Num( const cJSON *o, const char *k, double def = 0 ) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive( o, k );
	return cJSON_IsNumber( v ) ? v->valuedouble : def;
}

bool Has( const cJSON *o, const char *k ) {
	return cJSON_GetObjectItemCaseSensitive( o, k ) != nullptr;
}

// a name without its ^ color codes
std::string Plain( const std::string &s ) {
	std::string out;
	for ( size_t i = 0; i < s.size(); i++ ) {
		if ( s[i] == '^' && i + 1 < s.size() && s[i + 1] != '^' ) {
			i++;
			continue;
		}
		out += s[i];
	}
	return out;
}

std::string Lower( std::string s ) {
	for ( char &c : s ) {
		c = (char)tolower( (unsigned char)c );
	}
	return s;
}

// ---------------------------------------------------------------- reading the recordings

FILE *Open( const std::string &path ) {
	return (FILE *)OzE_FOpenOS( path.c_str(), "rb" );
}

unsigned GetU32( const unsigned char *p ) {
	return p[0] | ( p[1] << 8 ) | ( p[2] << 16 ) | ( (unsigned)p[3] << 24 );
}

// a .q3replay's header and trailer; without a trailer (the game closed while it recorded) its length
// from its last frame block
bool ReadReplay( Rec &r ) {
	FILE *f = Open( r.path );
	if ( !f ) {
		return false;
	}
	unsigned char head[12];
	bool ok = fread( head, 1, 12, f ) == 12 && !memcmp( head, "Q3RP", 4 );
	cJSON *h = nullptr, *t = nullptr;
	const unsigned hlen = ok ? GetU32( head + 8 ) : 0;
	if ( ok && hlen > 0 && hlen < ( 1u << 20 ) ) {
		std::string text( hlen, '\0' );
		if ( fread( &text[0], 1, hlen, f ) == hlen ) {
			h = cJSON_Parse( text.c_str() );
		}
	}
	ok = ok && h;
	if ( ok && r.size > (long long)( 12 + hlen + 8 ) ) {
		unsigned char tail[8];
		if ( !fseek( f, -8, SEEK_END ) && fread( tail, 1, 8, f ) == 8 && !memcmp( tail + 4, "Q3RT", 4 ) ) {
			const unsigned tlen = GetU32( tail );
			if ( tlen > 0 && tlen < ( 1u << 20 ) && (long long)tlen + 8 < r.size && !fseek( f, -8 - (long)tlen, SEEK_END ) ) {
				std::string text( tlen, '\0' );
				if ( fread( &text[0], 1, tlen, f ) == tlen ) {
					t = cJSON_Parse( text.c_str() );
				}
			}
		}
		if ( !t ) {		// cut off: the last whole block's time
			int first = -1, last = -1;
			long pos = 12 + (long)hlen;
			unsigned char b[9];
			while ( !fseek( f, pos, SEEK_SET ) && fread( b, 1, 9, f ) == 9 ) {
				const unsigned size = GetU32( b );
				if ( !size || b[4] > 1 || (long long)pos + 9 + size > r.size ) {
					break;
				}
				const int time = (int)GetU32( b + 5 );
				if ( first < 0 ) {
					first = time;
				}
				last = time;
				pos += 9 + (long)size;
			}
			r.lengthMs = first >= 0 ? last - first : -1;
		}
	}
	fclose( f );
	if ( !ok ) {
		cJSON_Delete( h );
		return false;
	}
	r.map = Str( h, "map" );
	r.mapTitle = Str( h, "map_name" );
	r.modeName = Str( h, "mode_name" );
	r.hostname = Str( h, "title" );
	r.game = Str( h, "game" );
	r.local = Str( h, "source" ) == "local";
	r.unixStart = (long long)Num( h, "unix" );
	static const char *const modes[] = { "ffa", "duel", "campaign", "tdm", "ctf" };
	const std::string mode = Str( h, "mode" );
	for ( int i = 0; i < 5; i++ ) {
		if ( mode == modes[i] ) {
			r.gametype = i;
		}
	}
	r.complete = t != nullptr;
	r.finished = t && Str( t, "ended" ) != "error";
	if ( t ) {
		r.lengthMs = (int)Num( t, "length_ms", -1 );
		const cJSON *p;
		cJSON_ArrayForEach( p, cJSON_GetObjectItemCaseSensitive( t, "standings" ) ) {
			Player pl;
			pl.client = (int)Num( p, "client", -1 );
			pl.name = Str( p, "name" );
			pl.score = (int)Num( p, "score" );
			pl.team = (int)Num( p, "team" );
			pl.hasScore = true;
			if ( pl.team != 3 ) {	// TEAM_SPECTATOR
				r.players.push_back( pl );
			}
		}
	}
	cJSON_Delete( h );
	cJSON_Delete( t );
	return true;
}

struct DemoCache { long long size; long long mtime; ozDemoInfo_t info; bool ok; };
std::map<std::string, DemoCache> demoCache;

// what Oz recorded of the match (library.json), else what the demo itself says
void ReadDemo( Rec &r, const cJSON *e, long long mtime ) {
	if ( e && cJSON_IsTrue( cJSON_GetObjectItemCaseSensitive( e, "auto" ) ) ) {
		r.map = Str( e, "map" );
		r.mapTitle = Str( e, "map_title" );
		r.modeName = Str( e, "mode_name" );
		r.gametype = (int)Num( e, "gametype", -1 );
		r.hostname = Str( e, "hostname" );
		r.server = Str( e, "server" );
		r.unixStart = (long long)Num( e, "unix" );
		r.lengthMs = (int)Num( e, "length_ms", -1 );
		r.finished = cJSON_IsTrue( cJSON_GetObjectItemCaseSensitive( e, "finished" ) );
		r.me = (int)Num( e, "me", -1 );
		r.teams = Has( e, "red" ) && Has( e, "blue" );
		r.red = (int)Num( e, "red" );
		r.blue = (int)Num( e, "blue" );
		const cJSON *p;
		cJSON_ArrayForEach( p, cJSON_GetObjectItemCaseSensitive( e, "standings" ) ) {
			Player pl;
			pl.client = (int)Num( p, "client", -1 );
			pl.name = Str( p, "name" );
			pl.score = (int)Num( p, "score" );
			pl.team = (int)Num( p, "team" );
			pl.bot = cJSON_IsTrue( cJSON_GetObjectItemCaseSensitive( p, "bot" ) );
			pl.hasScore = true;
			r.players.push_back( pl );
		}
		return;
	}
	DemoCache &c = demoCache[r.key];
	if ( c.size != r.size || c.mtime != mtime ) {
		c.size = r.size;
		c.mtime = mtime;
		c.ok = OzDemo_Info( r.path.c_str(), &c.info ) != 0;
	}
	r.unixStart = mtime;
	if ( !c.ok ) {
		return;
	}
	r.map = c.info.map;
	r.hostname = c.info.hostname;
	r.gametype = c.info.gametype;
	r.me = c.info.clientNum;
	r.complete = c.info.complete != 0;
	r.lengthMs = c.info.firstTime >= 0 ? c.info.lastTime - c.info.firstTime : -1;
	r.finished = r.complete;
	for ( int i = 0; i < c.info.numPlayers; i++ ) {
		Player pl;
		pl.client = c.info.playerClient[i];
		pl.name = c.info.players[i];
		r.players.push_back( pl );
	}
	if ( r.lengthMs >= 0 ) {
		r.unixStart = mtime - r.lengthMs / 1000;	// the file's time is its end
	}
}

long long FileTime( const fs::path &p ) {
	std::error_code ec;
	const auto t = fs::last_write_time( p, ec );
	if ( ec ) {
		return 0;
	}
	// the file clock's epoch isn't time_t's before C++20: measure from now on both
	const auto now = fs::file_time_type::clock::now();
	return (long long)time( nullptr ) - (long long)std::chrono::duration_cast<std::chrono::seconds>( now - t ).count();
}

void Scan() {
	recs.clear();
	cJSON *lib = OzLib_Read();
	const fs::path home( OzLib_Home() );
	std::error_code ec;

	// A name with letters outside Windows' code page (a demo named in Cyrillic on a western PC) can't be
	// a narrow string: string() throws, and with nothing to catch it the game closed as the page opened.
	// The engine opens files by narrow paths, so such a file can't be played either: it's left out.

	// this game's own servers' replays
	for ( fs::directory_iterator it( home / "replays", ec ), end; !ec && it != end; it.increment( ec ) ) {
		try {
			const fs::path p = it->path();
			if ( !it->is_regular_file( ec ) || Lower( p.extension().string() ) != ".q3replay" ) {
				continue;
			}
			Rec r;
			r.key = p.filename().string();
			r.kind = "replay";
			r.path = p.string();
			r.size = (long long)it->file_size( ec );
			if ( r.key == OzLib_CurrentReplay() || !ReadReplay( r ) ) {
				continue;	// being written, or not a replay
			}
			if ( !r.unixStart ) {
				r.unixStart = FileTime( p );
			}
			recs.push_back( r );
		} catch ( const std::exception & ) {
			continue;
		}
	}
	// every mod folder's demos
	for ( fs::directory_iterator dir( home, ec ), end; !ec && dir != end; dir.increment( ec ) ) {
		std::error_code ec2;
		std::string mod;
		try {
			mod = dir->path().filename().string();
		} catch ( const std::exception & ) {
			continue;
		}
		if ( !dir->is_directory( ec2 ) || mod.empty() || mod[0] == '.' ) {
			continue;
		}
		for ( fs::directory_iterator it( dir->path() / "demos", ec2 ), end2; !ec2 && it != end2; it.increment( ec2 ) ) {
			try {
				std::error_code ec3;
				const fs::path p = it->path();
				const std::string name = p.filename().string();
				if ( !it->is_regular_file( ec3 ) || !OzLib_IsDemoName( name.c_str() ) ) {
					continue;
				}
				Rec r;
				r.key = mod + "/demos/" + name;
				r.kind = "demo";
				r.path = p.string();
				r.size = (long long)it->file_size( ec3 );
				ReadDemo( r, cJSON_GetObjectItemCaseSensitive( lib, r.key.c_str() ), FileTime( p ) );
				recs.push_back( r );
			} catch ( const std::exception & ) {
				continue;
			}
		}
	}
	// names, favorites and the matches' reports; entries whose file went (deleted outside the game) are dropped
	std::vector<std::string> gone;
	std::map<std::string, size_t> byKey;	// with hundreds of recordings, looking each entry up in the list was the scan's cost
	for ( size_t i = 0; i < recs.size(); i++ ) {
		byKey[recs[i].key] = i;
	}
	const cJSON *e;
	cJSON_ArrayForEach( e, lib ) {
		char path[1024];
		const auto found = byKey.find( e->string );
		Rec *r = found != byKey.end() ? &recs[found->second] : nullptr;
		if ( r ) {
			r->favorite = cJSON_IsTrue( cJSON_GetObjectItemCaseSensitive( e, "favorite" ) );
			r->userName = Str( e, "name" );
			if ( Has( e, "me" ) ) {
				r->me = (int)Num( e, "me", -1 );
			}
			if ( Has( e, "won" ) ) {
				r->hasWon = true;
				r->won = cJSON_IsTrue( cJSON_GetObjectItemCaseSensitive( e, "won" ) );
				r->place = (int)Num( e, "place", -1 );
			}
			if ( Str( e, "mode_name" ) != "" ) {
				r->modeName = Str( e, "mode_name" );
			}
		} else if ( OzLib_Path( e->string, path, sizeof( path ) ) && !fs::exists( fs::path( path ), ec ) && strcmp( e->string, OzLib_CurrentReplay() ) &&
					( !OzLib_Recording()[0] || strncmp( e->string, OzLib_Recording(), strlen( OzLib_Recording() ) ) ) ) {
			gone.push_back( e->string );
		}
	}
	if ( !gone.empty() ) {		// in one write of the library, not one each
		std::vector<const char *> keys;
		for ( const std::string &k : gone ) {
			keys.push_back( k.c_str() );
		}
		OzLib_ForgetMany( keys.data(), (int)keys.size() );
	}
	cJSON_Delete( lib );
	std::stable_sort( recs.begin(), recs.end(), []( const Rec &a, const Rec &b ) { return a.unixStart > b.unixStart; } );
}

// ---------------------------------------------------------------- the rows

std::map<std::string, std::string> MapNames() {
	std::map<std::string, std::string> out;
	const cJSON *m;
	cJSON_ArrayForEach( m, Menu_Node( "maps" ) ) {
		out[Lower( Str( m, "map" ) )] = Str( m, "name" );
	}
	return out;
}

std::string ModeName( const Rec &r ) {
	static const char *const board[][2] = { { "FREE FOR ALL", "Free For All" }, { "DUEL", "Duel" }, { "SINGLE PLAYER", "Single Player" },
		{ "TEAM DEATHMATCH", "Team Deathmatch" }, { "CAPTURE THE FLAG", "Capture the Flag" }, { "FREEZE TAG", "Freeze Tag" },
		{ "CLAN ARENA", "Clan Arena" } };
	for ( const auto &b : board ) {
		if ( r.modeName == b[0] ) {
			return b[1];
		}
	}
	if ( !r.modeName.empty() && r.modeName != "Custom" ) {
		return r.modeName;
	}
	static const char *const names[] = { "Free For All", "Duel", "Single Player", "Team Deathmatch", "Capture the Flag" };
	return r.gametype >= 0 && r.gametype < 5 ? names[r.gametype] : "Match";
}

std::string Length( int ms ) {
	if ( ms < 0 ) {
		return "--:--";
	}
	const int s = ms / 1000;
	char buf[32];
	if ( s >= 3600 ) {
		snprintf( buf, sizeof( buf ), "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60 );
	} else {
		snprintf( buf, sizeof( buf ), "%d:%02d", s / 60, s % 60 );
	}
	return buf;
}

std::string When( long long when ) {	// not "unix": a macro on Linux
	char buf[64] = "";
	const time_t t = (time_t)when;
	const struct tm *tm = localtime( &t );
	if ( tm ) {
		strftime( buf, sizeof( buf ), "%b %d  %I:%M %p", tm );
	}
	return buf;
}

std::string Size( long long bytes ) {
	char buf[32];
	if ( bytes >= 1024 * 1024 ) {
		snprintf( buf, sizeof( buf ), "%.1f MB", bytes / ( 1024.0 * 1024.0 ) );
	} else {
		snprintf( buf, sizeof( buf ), "%lld KB", ( bytes + 1023 ) / 1024 );
	}
	return buf;
}

std::string Ordinal( int n ) {
	const char *suffix = n % 100 >= 11 && n % 100 <= 13 ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
	return std::to_string( n ) + suffix;
}

// the players best first (a team game by team, then score), and which one is you
std::vector<Player> Standings( const Rec &r, int *mine ) {
	std::vector<Player> p = r.players;
	const bool scored = std::any_of( p.begin(), p.end(), []( const Player &x ) { return x.hasScore; } );
	if ( scored ) {
		std::stable_sort( p.begin(), p.end(), [&]( const Player &a, const Player &b ) {
			if ( r.teams && a.team != b.team ) {
				return a.team < b.team;
			}
			return a.score > b.score;
		} );
	}
	*mine = -1;
	const std::string own = Lower( Plain( OzE_CvarString( "name" ) ) );
	for ( size_t i = 0; i < p.size(); i++ ) {
		if ( r.me >= 0 ? p[i].client == r.me : ( !own.empty() && Lower( Plain( p[i].name ) ) == own ) ) {
			*mine = (int)i;
		}
	}
	return p;
}

struct Outcome { std::string badge, tone, result; };

Outcome Result( const Rec &r, const std::vector<Player> &p, int mine ) {
	if ( !r.complete && r.kind == "replay" ) {
		return { "UNFINISHED", "dim", "Unfinished" };
	}
	bool won = false;
	int place = -1;
	if ( r.hasWon ) {
		won = r.won;
		place = r.place;
	} else if ( mine >= 0 && p[mine].hasScore ) {
		const bool tied = r.teams ? r.red == r.blue : p.size() >= 2 && p[0].score == p[1].score && p[mine].score == p[0].score;
		if ( tied ) {
			return { "DRAW", "dim", r.finished ? "Draw" : "Left early" };
		}
		if ( r.teams ) {
			const int ours = p[mine].team == 2 ? r.blue : r.red, theirs = p[mine].team == 2 ? r.red : r.blue;
			won = ours > theirs;
		} else {
			won = mine == 0 && ( p.size() < 2 || p[0].score > p[1].score );
		}
		place = mine;
	} else if ( r.kind == "demo" && r.me >= 0 && !p.empty() && mine < 0 && std::all_of( p.begin(), p.end(), []( const Player &x ) { return x.hasScore; } ) ) {
		return { "WATCHED", "dim", "Spectated" };
	} else {
		return { "", "", r.finished ? "-" : "Left early" };
	}
	if ( !r.finished && !r.hasWon ) {
		return { "LEFT EARLY", "dim", "Left early (" + Ordinal( place + 1 ) + ")" };
	}
	const std::string at = place >= 0 && !r.teams ? " (#" + std::to_string( place + 1 ) + ")" : "";
	return won ? Outcome{ "VICTORY", "amber", "VICTORY" + at } : Outcome{ "DEFEAT", "red", "DEFEAT" + at };
}

std::string MapShown( const Rec &r, const std::map<std::string, std::string> &names ) {
	const auto it = names.find( Lower( r.map ) );
	if ( it != names.end() && !it->second.empty() ) {
		return Plain( it->second );
	}
	if ( !Plain( r.mapTitle ).empty() ) {
		return Plain( r.mapTitle );
	}
	return r.map.empty() ? "Unknown map" : r.map;
}

std::string Shot( const std::string &map, bool tile ) {
	if ( map.empty() || !( OzE_FileExists( ( "levelshots/" + map + ".jpg" ).c_str() ) || OzE_FileExists( ( "levelshots/" + map + ".tga" ).c_str() ) ) ) {
		return "";
	}
	return ( tile ? "/@tile/levelshots/" : "/levelshots/" ) + map;
}

std::string Where( const Rec &r ) {
	if ( r.kind == "replay" ) {
		return r.local ? "Your own game" : r.hostname.empty() ? "A server on this PC" : r.hostname;
	}
	if ( !r.hostname.empty() ) {
		return r.hostname;
	}
	return r.server.empty() ? "Another server" : r.server;
}

int Find( const std::string &id ) {
	if ( id.size() < 2 || id[0] != 'r' ) {
		return -1;
	}
	const int i = atoi( id.c_str() + 1 );
	return i >= 0 && i < (int)recs.size() ? i : -1;
}

int FindKey( const std::string &key ) {
	for ( size_t i = 0; i < recs.size(); i++ ) {
		if ( recs[i].key == key ) {
			return (int)i;
		}
	}
	return -1;
}

void ShowCur() {
	const int i = FindKey( pickKey );
	cJSON *cur = cJSON_CreateObject();
	SetBool( "replays.has_cur", i >= 0 );
	if ( i < 0 ) {
		cJSON_AddStringToObject( cur, "id", "" );
		cJSON_AddArrayToObject( cur, "players" );
	} else {
		const Rec &r = recs[i];
		int mine;
		const std::vector<Player> p = Standings( r, &mine );
		const Outcome o = Result( r, p, mine );
		const std::string mapName = MapShown( r, MapNames() );
		cJSON_AddStringToObject( cur, "id", ( "r" + std::to_string( i ) ).c_str() );
		cJSON_AddStringToObject( cur, "kind", r.kind.c_str() );
		cJSON_AddStringToObject( cur, "map_name", mapName.c_str() );
		cJSON_AddStringToObject( cur, "mode_name", ModeName( r ).c_str() );
		cJSON_AddStringToObject( cur, "when", When( r.unixStart ).c_str() );
		cJSON_AddStringToObject( cur, "shot", Shot( r.map, false ).c_str() );
		cJSON_AddStringToObject( cur, "name", r.userName.c_str() );
		cJSON_AddBoolToObject( cur, "favorite", r.favorite );
		cJSON_AddStringToObject( cur, "length", Length( r.lengthMs ).c_str() );
		cJSON_AddStringToObject( cur, "result", o.result.c_str() );
		cJSON_AddStringToObject( cur, "size", Size( r.size ).c_str() );
		cJSON_AddStringToObject( cur, "where", Where( r ).c_str() );
		cJSON_AddStringToObject( cur, "views", r.kind == "replay" ? "Every player's" : "Yours" );
		cJSON_AddStringToObject( cur, "server_kind", r.server.rfind( "ice:", 0 ) == 0 ? "a friend's match" : "someone else's server" );
		cJSON_AddBoolToObject( cur, "complete", r.complete );
		cJSON *players = cJSON_AddArrayToObject( cur, "players" );
		for ( size_t k = 0; k < p.size(); k++ ) {
			cJSON *x = cJSON_CreateObject();
			cJSON_AddNumberToObject( x, "n", (double)( k + 1 ) );
			cJSON_AddNumberToObject( x, "client", p[k].client );
			cJSON_AddStringToObject( x, "name", p[k].name.c_str() );
			cJSON_AddStringToObject( x, "score", p[k].hasScore ? std::to_string( p[k].score ).c_str() : "" );
			cJSON_AddBoolToObject( x, "bot", p[k].bot );
			cJSON_AddBoolToObject( x, "local", (int)k == mine );
			cJSON_AddBoolToObject( x, "eyes", r.kind == "replay" && p[k].client >= 0 );
			cJSON_AddItemToArray( players, x );
		}
	}
	if ( cJSON *node = Menu_Node( "replays.cur" ) ) {
		Replace( node, cur );
	} else {
		cJSON_Delete( cur );
	}
	savedName = i >= 0 ? recs[i].userName : "";
	lastTyped = savedName;
	SetText( "replays.pick", i >= 0 ? "r" + std::to_string( i ) : "" );
	Menu_Dirty( "replays" );
}

int Shown() {
	const bool favs = !strcmp( Menu_Get( "replays.tab" ), "favorites" );
	int n = 0;
	for ( const Rec &r : recs ) {
		n += !favs || r.favorite;
	}
	return n;
}

void Fill() {
	Scan();
	const std::map<std::string, std::string> names = MapNames();
	cJSON *rows = cJSON_CreateArray();
	int replays = 0, demos = 0;
	for ( size_t i = 0; i < recs.size(); i++ ) {
		const Rec &r = recs[i];
		int mine;
		const std::vector<Player> p = Standings( r, &mine );
		const Outcome o = Result( r, p, mine );
		std::string sub = ModeName( r );
		if ( !p.empty() ) {
			sub += "  -  " + std::to_string( p.size() ) + ( p.size() == 1 ? " player" : " players" );
		}
		sub += "  -  " + Plain( Where( r ) );
		cJSON *x = cJSON_CreateObject();
		cJSON_AddStringToObject( x, "id", ( "r" + std::to_string( i ) ).c_str() );
		cJSON_AddStringToObject( x, "kind", r.kind.c_str() );
		cJSON_AddStringToObject( x, "kind_text", r.kind == "replay" ? "ALL VIEWS" : "YOUR VIEW" );
		cJSON_AddStringToObject( x, "tile", Shot( r.map, true ).c_str() );
		cJSON_AddStringToObject( x, "map_name", MapShown( r, names ).c_str() );
		cJSON_AddStringToObject( x, "name", r.userName.c_str() );
		cJSON_AddBoolToObject( x, "favorite", r.favorite );
		cJSON_AddStringToObject( x, "sub", sub.c_str() );
		cJSON_AddStringToObject( x, "when", When( r.unixStart ).c_str() );
		cJSON_AddStringToObject( x, "length", Length( r.lengthMs ).c_str() );
		cJSON_AddStringToObject( x, "badge", o.badge.c_str() );
		cJSON_AddStringToObject( x, "tone", o.tone.c_str() );
		cJSON_AddItemToArray( rows, x );
		( r.kind == "replay" ? replays : demos )++;
	}
	if ( cJSON *node = Menu_Node( "replays.list" ) ) {
		Replace( node, rows );
	} else {
		cJSON_Delete( rows );
	}
	const bool favs = !strcmp( Menu_Get( "replays.tab" ), "favorites" );
	if ( FindKey( pickKey ) < 0 || ( favs && !recs[FindKey( pickKey )].favorite ) ) {
		pickKey.clear();
		for ( const Rec &r : recs ) {
			if ( !favs || r.favorite ) {
				pickKey = r.key;
				break;
			}
		}
	}
	Menu_Set( "replays.kept", std::to_string( OzLib_Keep() ).c_str() );
	Menu_Set( "replays.shown", std::to_string( Shown() ).c_str() );
	SetText( "replays.count", recs.empty() ? "" : std::to_string( recs.size() ) + ( recs.size() == 1 ? " recording" : " recordings" ) );
	const bool off = !atoi( OzE_CvarString( "sv_ozReplay" ) );
	SetText( "replays.note", favs && !recs.empty() ? "No favorites yet: pick a replay and press Favorite, and it stays here forever."
		: off ? "Recording is off: Settings > Gameplay > Record replays turns it on."
		: "Nothing recorded yet. Play a match, against bots or on any server, and it shows here when it ends." );
	ShowCur();
	Printf( "UI: replays: %d listed (%d whole, %d your view)\n", (int)recs.size(), replays, demos );
}

void Refuse( const char *why ) {
	PlaySound( Sound::Buzz );
	Printf( "UI: replays: %s\n", why );
	Menu_Toast( why, "warn" );
}

// the name typed so far goes in the library (as it's typed, a moment after the last key; and before
// anything else happens to the picked one)
void SaveName() {
	const int i = FindKey( pickKey );
	if ( i < 0 ) {
		return;
	}
	std::string name = Menu_Get( "replays.cur.name" );
	while ( !name.empty() && name.back() == ' ' ) {
		name.pop_back();
	}
	if ( name == savedName ) {
		return;
	}
	OzLib_Set( recs[i].key.c_str(), "name", cJSON_CreateString( name.c_str() ) );
	recs[i].userName = savedName = name;
	SetText( ( "replays.list." + std::to_string( i ) + ".name" ).c_str(), name );
	Printf( "UI: replays: %s named \"%s\"\n", recs[i].key.c_str(), name.c_str() );
}

void Pick( int i ) {
	SaveName();
	if ( i < 0 ) {
		return;
	}
	pickKey = recs[i].key;
	ShowCur();
}

// a recording played: a replay from a player's eyes (yours unless another is asked), a demo as it was recorded
void Play( const std::string &key, int client ) {
	char path[1024];
	if ( !OzLib_Path( key.c_str(), path, sizeof( path ) ) ) {
		Refuse( "That recording isn't in your user folder." );
		return;
	}
	if ( !fs::exists( fs::path( path ) ) ) {
		Refuse( "That recording is gone from its folder." );
		return;
	}
	if ( OzE_ClientState() != 0 && !OzE_PlayingDemo() ) {
		Refuse( "Leave your match first." );
		return;
	}
	SaveName();
	std::string cmd;
	// a mod's folder as the engine names the game running: baseq3 is Oz's own
	const auto modOf = []( const std::string &m ) { const std::string l = Lower( m ); return l.empty() || l == "baseq3" ? std::string( "oz" ) : l; };
	if ( key.find( '/' ) == std::string::npos ) {
		const int i = FindKey( key );
		// What the replay command would refuse is said here, while the page is still up: the page closed
		// and nothing played, with the reason only in the console
		if ( i >= 0 && !recs[i].game.empty() && modOf( recs[i].game ) != modOf( OzE_GameDir() ) ) {
			Refuse( ( "Recorded in " + recs[i].game + ": switch to it from the Mod Library to watch." ).c_str() );
			return;
		}
		if ( i >= 0 && OzE_ClientState() == 0 && !recs[i].map.empty() && !OzE_FileExists( ( "maps/" + recs[i].map + ".bsp" ).c_str() ) ) {
			Refuse( ( "Its map, " + recs[i].map + ", isn't installed." ).c_str() );
			return;
		}
		int view = client;
		if ( view < 0 ) {
			view = i >= 0 ? recs[i].me : -1;
		}
		cmd = "replay \"" + key + "\"" + ( view >= 0 ? " " + std::to_string( view ) : "" );
	} else {
		// The demo command takes a name, not a mod: it plays the running mod's demo of that name, then
		// baseq3's, then the first other mod's it finds. When two mods hold one of this name and the
		// command would pick the other, the wrong match played: say so instead.
		const std::string name = Lower( key.substr( key.rfind( '/' ) + 1 ) );
		const std::string mod = Lower( key.substr( 0, key.find( '/' ) ) ), running = Lower( OzE_GameDir() );
		int same = 0;
		bool inRunning = false;
		for ( const Rec &r : recs ) {
			if ( r.kind == "demo" && Lower( r.key.substr( r.key.rfind( '/' ) + 1 ) ) == name ) {
				same++;
				inRunning = inRunning || Lower( r.key.substr( 0, r.key.find( '/' ) ) ) == running;
			}
		}
		if ( same > 1 && mod != running && !( mod == "baseq3" && !inRunning ) ) {
			Refuse( "Another mod's folder has a demo of this name, and that one would play: rename this file to watch it." );
			return;
		}
		cmd = "demo \"" + key.substr( key.rfind( '/' ) + 1 ) + "\"";	// the demo command looks in every mod's demos
	}
	Printf( "UI: replays: watching %s\n", key.c_str() );
	CloseAllPages();
	OzE_InsertText( ( cmd + "\n" ).c_str() );
}

void Favorite() {
	const int i = FindKey( pickKey );
	if ( i < 0 ) {
		return;
	}
	SaveName();
	recs[i].favorite = !recs[i].favorite;
	OzLib_Set( recs[i].key.c_str(), "favorite", cJSON_CreateBool( recs[i].favorite ) );
	PlaySound( Sound::In );
	Printf( "UI: replays: %s %s\n", recs[i].key.c_str(), recs[i].favorite ? "is a favorite" : "isn't a favorite" );
	Fill();
}

void AskDelete() {
	const int i = FindKey( pickKey );
	if ( i < 0 ) {
		return;
	}
	SaveName();
	const Rec &r = recs[i];
	SetText( "replays.confirm.name", r.userName.empty() ? MapShown( r, MapNames() ) : r.userName );
	SetText( "replays.confirm.text", OzLib_Recycles() ? "It goes to the Recycle Bin, where you can still get it back." : "Its file is deleted for good." );
	SetBool( "replays.confirm.open", true );
	GetContext()->Update();
	if ( Rml::ElementDocument *doc = TopDocument() ) {
		FocusQuietly( doc->GetElementById( "rp_del_no" ) );
	}
}

void DoDelete() {
	SetBool( "replays.confirm.open", false );
	const int i = FindKey( pickKey );
	if ( i < 0 ) {
		return;
	}
	char why[256];
	const std::string key = recs[i].key;
	const int how = OzLib_Delete( key.c_str(), why, sizeof( why ) );
	if ( !how ) {
		Refuse( why );
		return;
	}
	PlaySound( Sound::Out );
	Menu_Toast( how == 2 ? "Moved to the Recycle Bin." : "Deleted.", "info" );
	// the next one down takes its place
	pickKey = i + 1 < (int)recs.size() ? recs[i + 1].key : i > 0 ? recs[i - 1].key : "";
	Fill();
	GetContext()->Update();
	if ( Rml::ElementDocument *doc = TopDocument() ) {
		Rml::Element *e = doc->GetElementById( "rp_" + std::string( Menu_Get( "replays.pick" ) ) );
		FocusQuietly( e ? e : doc->GetElementById( "rp_folder" ) );
	}
}

void ShowFolder() {
	const int i = FindKey( pickKey );
	fs::path dir = i >= 0 ? fs::path( recs[i].path ).parent_path() : fs::path( OzLib_Home() ) / "replays";
	std::error_code ec;
	fs::create_directories( dir, ec );
	std::string url;
	try {
		url = "file:///" + dir.string();
	} catch ( const std::exception & ) {		// a folder name outside Windows' code page: string() throws
		Refuse( "The folder didn't open." );
		return;
	}
	for ( char &c : url ) {
		if ( c == '\\' ) {
			c = '/';
		}
	}
	if ( !OzE_OpenURL( url.c_str() ) ) {
		Refuse( "The folder didn't open." );
		return;
	}
	Printf( "UI: replays: opened %s\n", dir.string().c_str() );
}

}	// namespace

namespace OzUI {

bool Replays_Action( const Rml::String &verb, const Rml::String &arg ) {
	if ( verb == "replay_pick" ) {
		Pick( Find( arg ) );
	} else if ( verb == "replay_watch" ) {
		Rml::StringList a;
		Rml::StringUtilities::ExpandString( a, arg, ' ' );
		const int i = a.empty() ? FindKey( pickKey ) : Find( a[0] );
		if ( i < 0 ) {
			Refuse( "Pick a replay first." );
			return true;
		}
		Play( recs[i].key, a.size() > 1 ? atoi( a[1].c_str() ) : -1 );
	} else if ( verb == "replay_play" ) {	// the Profile's Recent matches: a recording by its library key
		const size_t sp = arg.rfind( ' ' );
		const bool client = sp != Rml::String::npos && arg.find_first_not_of( "0123456789", sp + 1 ) == Rml::String::npos;
		if ( recs.empty() ) {
			Scan();
		}
		Play( client ? arg.substr( 0, sp ) : arg, client ? atoi( arg.c_str() + sp + 1 ) : -1 );
	} else if ( verb == "replay_fav" ) {
		Favorite();
	} else if ( verb == "replay_delete" ) {
		AskDelete();
	} else if ( verb == "replay_delete_go" ) {
		DoDelete();
	} else if ( verb == "replay_delete_no" ) {
		SetBool( "replays.confirm.open", false );
		GetContext()->Update();
		if ( Rml::ElementDocument *doc = TopDocument() ) {
			FocusQuietly( doc->GetElementById( "rp_delete" ) );
		}
	} else if ( verb == "replay_folder" ) {
		ShowFolder();
	} else if ( verb == "replay_tab" ) {
		SaveName();
		SetText( "replays.tab", arg == "favorites" ? "favorites" : "all" );
		Fill();
	} else {
		return false;
	}
	return true;
}

void Replays_Opened( const char *page ) {
	if ( strcmp( page, PAGE ) ) {
		return;
	}
	SetBool( "replays.confirm.open", false );
	Fill();
}

bool Replays_Back() {
	if ( !strcmp( Menu_Get( "replays.confirm.open" ), "true" ) ) {
		SetBool( "replays.confirm.open", false );
		PlaySound( Sound::Out );
		return true;
	}
	SaveName();	// the page closes: the name typed goes with it
	return false;
}

// the name is saved a moment after the last key
void Replays_Frame() {
	if ( !IsPageOpen( PAGE ) || pickKey.empty() ) {
		return;
	}
	const std::string typed = Menu_Get( "replays.cur.name" );
	if ( typed != lastTyped ) {
		lastTyped = typed;
		nameChangedAt = OzE_Milliseconds();
	} else if ( typed != savedName && OzE_Milliseconds() - nameChangedAt > 600 ) {
		SaveName();
	}
}

// the Profile's Recent matches: each hub match number's recording, by its library key
std::map<int, std::string> Replays_ByMatch() {
	std::map<int, std::string> out;
	cJSON *lib = OzLib_Read();
	const cJSON *e;
	cJSON_ArrayForEach( e, lib ) {
		char path[1024];
		const int match = (int)Num( e, "match" );
		std::error_code ec;
		if ( match > 0 && OzLib_Path( e->string, path, sizeof( path ) ) && fs::exists( fs::path( path ), ec ) ) {
			out[match] = e->string;
		}
	}
	cJSON_Delete( lib );
	return out;
}

void Replays_Commands() {
	// ui_replays [history <match> <map>]: the page's state; "history" hands the Profile a match_history row
	// as the hub would (tests), and prints its Recent matches rows' recordings
	OzE_AddCommand( "ui_replays", []() {
		if ( !strcmp( OzE_Argv( 1 ), "history" ) ) {
			cJSON *m = cJSON_CreateObject(), *list = cJSON_AddArrayToObject( m, "matches" ), *x = cJSON_CreateObject();
			cJSON_AddNumberToObject( x, "match", atoi( OzE_Argv( 2 ) ) );
			cJSON_AddNumberToObject( x, "at", (double)time( nullptr ) * 1000.0 );
			cJSON_AddStringToObject( x, "map", OzE_Argv( 3 ) );
			cJSON_AddStringToObject( x, "server", "127.0.0.1:27960" );
			cJSON_AddStringToObject( x, "mode", "duel" );
			cJSON_AddNumberToObject( x, "place", 1 );
			cJSON_AddNumberToObject( x, "players", 2 );
			cJSON_AddItemToArray( list, x );
			Prog::MatchHistory( m );
			cJSON_Delete( m );
			const cJSON *r;
			cJSON_ArrayForEach( r, Menu_Node( "profile.matches" ) ) {
				Printf( "ui_replays: recent match %s on %s: replay \"%s\"\n", Str( r, "when" ).c_str(), Str( r, "map" ).c_str(), Str( r, "replay" ).c_str() );
			}
			return;
		}
		const std::string tab = Menu_Get( "replays.tab" ), pick = Menu_Get( "replays.pick" );
		Printf( "ui_replays: %d recordings, tab %s, pick %s (%s), confirm %s\n", (int)recs.size(), tab.c_str(), pick.c_str(), pickKey.c_str(),
			Menu_Get( "replays.confirm.open" ) );
		const cJSON *r;
		int n = 0;
		cJSON_ArrayForEach( r, Menu_Node( "replays.list" ) ) {
			Printf( "ui_replays: row %s %s %s: \"%s\" \"%s\" %s %s %s %s\n", Str( r, "id" ).c_str(), Str( r, "kind" ).c_str(),
				n < (int)recs.size() ? recs[n].key.c_str() : "?", Str( r, "map_name" ).c_str(), Str( r, "name" ).c_str(), Str( r, "sub" ).c_str(),
				Str( r, "length" ).c_str(), Str( r, "badge" ).c_str(), cJSON_IsTrue( cJSON_GetObjectItemCaseSensitive( r, "favorite" ) ) ? "fav" : "-" );
			n++;
		}
		const cJSON *cur = Menu_Node( "replays.cur" );
		Printf( "ui_replays: cur %s %s \"%s\" result %s, where %s, views %s, length %s, size %s\n", Str( cur, "id" ).c_str(), Str( cur, "kind" ).c_str(),
			Str( cur, "name" ).c_str(), Str( cur, "result" ).c_str(), Str( cur, "where" ).c_str(), Str( cur, "views" ).c_str(), Str( cur, "length" ).c_str(),
			Str( cur, "size" ).c_str() );
		cJSON_ArrayForEach( r, cJSON_GetObjectItemCaseSensitive( cur, "players" ) ) {
			Printf( "ui_replays: player %d %s (client %d) %s%s%s\n", (int)Num( r, "n" ), Str( r, "name" ).c_str(), (int)Num( r, "client" ),
				Str( r, "score" ).c_str(), cJSON_IsTrue( cJSON_GetObjectItemCaseSensitive( r, "local" ) ) ? " you" : "",
				cJSON_IsTrue( cJSON_GetObjectItemCaseSensitive( r, "eyes" ) ) ? " eyes" : "" );
		}
	} );
}

}	// namespace OzUI
