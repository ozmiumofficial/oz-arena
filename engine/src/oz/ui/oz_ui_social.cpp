/*
===========================================================================
Oz Arena: the online services in the menus (ENGINE_PLAN.md P7, P8). GPL-2.0.

What the hub says (engine/src/oz/online) put into the menu model the pages bind, in the shapes P5's
stand-in data had (ui/data/menu.json "social", ui/data/leaderboards.json, ui/data/server_browser.json):
  social			the top bar's status and counts, the Friends panel (pages/friends.rml): your friend
					code, party invites, friend requests both ways, friends grouped by what they're doing
					(a friend in a peer-to-peer match has a Join button), and your party with its chat
  leaderboards		the Level and Ranked boards, everyone or friends
  server_browser	the Internet tab's rows, the hub's open peer-to-peer matches (net_matches), each
					with its link badge: Relayed when the host or you hide your IP, else Direct; and a
					join under way (oz_ice.c's OzUI_JoinState), its bar, toasts and password prompt
					(the community servers' rows, and every row's filters, are oz_ui_servers.cpp's)
  quick_join		Quick Play: the hub's pick of the open match in a mode (net_quick), joined at once,
					asked about when it's on another continent, or why there's none
The pages' buttons send the hub messages with the console's "hub" command (oz_online_engine.c), and
join with oz_join (oz_ice.c), through the actions here (Social_Action). A party follows its leader
into the match the leader hosts (party_follow, net_follow).
===========================================================================
*/
#include "oz_ui_internal.h"
#include "oz_emoji.h"
#include "oz_ui.h"
#include "cJSON.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <vector>

namespace OzUI {
cJSON *Menu_Node( const char *path );	// oz_ui_menu.cpp
void Menu_Dirty( const char *path );
}
using namespace OzUI;

namespace {

const size_t CHAT_LINES = 40;

struct ChatLine { std::string name, text; };
std::vector<ChatLine> chat;
std::string myId;
int lastStatus = -1;
std::map<std::string, std::string> matchNames;	// a host's player id -> what its match is called (the join bar, the connection screen)
int browsedAt = -100000;						// when the Server Browser last asked for the open matches
struct { std::string host, name, line; int est = 0; } quick;	// the match Quick Play joins or offers
std::string followHost;							// the party leader's match being joined (its toast is the follow's)

// a player model as the hub has it ("sarge" or "sarge/blue") -> its icon's folder
std::string Model( const cJSON *m ) {
	std::string s = cJSON_IsString( m ) && m->valuestring[0] ? m->valuestring : "sarge";
	return s.substr( 0, s.find( '/' ) );
}

const char *Str( const cJSON *o, const char *key, const char *fallback = "" ) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive( o, key );
	return cJSON_IsString( v ) ? v->valuestring : fallback;
}

double Num( const cJSON *o, const char *key ) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive( o, key );
	return cJSON_IsNumber( v ) ? v->valuedouble : 0.0;
}

bool Bool( const cJSON *o, const char *key ) {
	return cJSON_IsTrue( cJSON_GetObjectItemCaseSensitive( o, key ) );
}

// a name without its color codes
std::string Plain( const char *s ) {
	std::string out;
	for ( ; *s; s++ ) {
		if ( s[0] == '^' && s[1] >= '0' && s[1] <= '9' ) {
			s++;
			continue;
		}
		out += *s;
	}
	return out;
}

// "Last seen 3 hours ago" from a millisecond time
std::string Seen( double ms ) {
	if ( ms <= 0 ) {
		return "Offline";
	}
	const double s = (double)time( nullptr ) - ms / 1000.0;
	char buf[64];
	if ( s < 120 ) return "Last seen just now";
	if ( s < 7200 ) { snprintf( buf, sizeof( buf ), "Last seen %d minutes ago", (int)( s / 60 ) ); return buf; }
	if ( s < 172800 ) { snprintf( buf, sizeof( buf ), "Last seen %d hours ago", (int)( s / 3600 ) ); return buf; }
	snprintf( buf, sizeof( buf ), "Last seen %d days ago", (int)( s / 86400 ) );
	return buf;
}

cJSON *Person( const cJSON *p, const std::string &sub ) {
	cJSON *o = cJSON_CreateObject();
	cJSON_AddStringToObject( o, "id", Str( p, "id" ) );
	cJSON_AddStringToObject( o, "name", Str( p, "name", "Player" ) );
	cJSON_AddStringToObject( o, "model", Model( cJSON_GetObjectItemCaseSensitive( p, "model" ) ).c_str() );
	cJSON_AddStringToObject( o, "color", Str( p, "color", "#db2410" ) );
	cJSON_AddStringToObject( o, "sub", sub.c_str() );
	return o;
}

// the node's payload replaced by src's (src consumed); the node stays, as the model holds it
void Replace( cJSON *dst, cJSON *src ) {
	cJSON *c;
	while ( ( c = dst->child ) != nullptr ) {
		cJSON_Delete( cJSON_DetachItemViaPointer( dst, c ) );
	}
	while ( ( c = src->child ) != nullptr ) {
		cJSON_AddItemToArray( dst, cJSON_DetachItemViaPointer( src, c ) );	// keeps each item's key
	}
	cJSON_Delete( src );
}

// the party's chat, its codes and smileys as emoji as in the match's chat (oz_emoji.c)
void Chat( cJSON *party ) {
	cJSON *lines = cJSON_AddArrayToObject( party, "chat" );
	const bool emoji = OzUI_EmojiFont() && atoi( OzE_CvarString( "oz_hud_chat_emoji" ) );
	for ( const ChatLine &l : chat ) {
		cJSON *o = cJSON_CreateObject();
		cJSON_AddStringToObject( o, "name", l.name.c_str() );
		std::vector<char> shown( l.text.size() * 2 + 1 );
		if ( emoji ) {
			OzEmoji_Expand( l.text.c_str(), shown.data(), (int)shown.size(), -1, nullptr );
		}
		cJSON_AddStringToObject( o, "text", emoji ? shown.data() : l.text.c_str() );
		cJSON_AddItemToArray( lines, o );
	}
}

void Social( const cJSON *m ) {
	cJSON *node = Menu_Node( "social" );
	if ( !node ) {
		return;
	}
	const cJSON *me = cJSON_GetObjectItemCaseSensitive( m, "me" );
	myId = Str( me, "id" );
	cJSON *s = cJSON_CreateObject();
	cJSON_AddStringToObject( s, "status", "online" );
	cJSON_AddStringToObject( s, "status_label", "Online" );
	cJSON_AddStringToObject( s, "code", Str( me, "code", "?" ) );

	// party invites and friend requests, both ways
	cJSON *invites = cJSON_AddArrayToObject( s, "invites" );
	const cJSON *i;
	cJSON_ArrayForEach( i, cJSON_GetObjectItemCaseSensitive( m, "invites" ) ) {
		char sub[64];
		snprintf( sub, sizeof( sub ), "Party invite, %d in the party", (int)Num( i, "size" ) );
		cJSON *o = Person( cJSON_GetObjectItemCaseSensitive( i, "from" ), sub );
		cJSON_AddStringToObject( o, "party", Str( i, "party" ) );
		cJSON_AddItemToArray( invites, o );
	}
	cJSON *incoming = cJSON_AddArrayToObject( s, "incoming" );
	cJSON_ArrayForEach( i, cJSON_GetObjectItemCaseSensitive( m, "incoming" ) ) {
		cJSON_AddItemToArray( incoming, Person( i, "Wants to be your friend" ) );
	}
	cJSON *outgoing = cJSON_AddArrayToObject( s, "outgoing" );
	cJSON_ArrayForEach( i, cJSON_GetObjectItemCaseSensitive( m, "outgoing" ) ) {
		cJSON_AddItemToArray( outgoing, Person( i, "Friend request sent" ) );
	}

	// friends: in a match, online (the menu or the editor), offline
	static const char *labels[3] = { "In a match", "Online", "Offline" };
	cJSON *lists[3];
	int online = 0, count = 0;
	cJSON *groups = cJSON_AddArrayToObject( s, "groups" );
	for ( int g = 0; g < 3; g++ ) {
		lists[g] = cJSON_CreateArray();
	}
	cJSON_ArrayForEach( i, cJSON_GetObjectItemCaseSensitive( m, "friends" ) ) {
		const bool on = Bool( i, "online" );
		const std::string status = Str( i, "status", "offline" );
		const int g = !on ? 2 : status == "match" ? 0 : 1;
		cJSON *o = Person( i, on ? Str( i, "detail", "Online" ) : Seen( Num( i, "seen" ) ) );
		cJSON_AddStringToObject( o, "status", on ? status.c_str() : "offline" );
		cJSON_AddBoolToObject( o, "online", on );
		// their peer-to-peer match (oz_ice.c's OzIce_JoinString): "ice:<host>", or the listed community server they're
		// on (oz_online_engine.c): "server:<address>"; " locked" with a password
		std::string join = on ? Str( i, "join" ) : "";
		const bool locked = join.size() > 7 && join.compare( join.size() - 7, 7, " locked" ) == 0;
		const std::string target = join.substr( join.find( ':' ) + 1, join.find( ' ' ) == std::string::npos ? std::string::npos : join.find( ' ' ) - join.find( ':' ) - 1 );
		std::string host = !join.compare( 0, 4, "ice:" ) ? target : "";
		const std::string server = !join.compare( 0, 7, "server:" ) ? target : "";
		if ( host == myId ) {
			host.clear();	// they're in your own match
		}
		cJSON_AddBoolToObject( o, "join", !host.empty() || !server.empty() );
		cJSON_AddStringToObject( o, "host", host.c_str() );
		cJSON_AddStringToObject( o, "server", server.c_str() );
		cJSON_AddBoolToObject( o, "locked", locked );
		// the match's mod and map (PH step 3), beside the detail line that names them
		cJSON_AddStringToObject( o, "mod", on ? Str( i, "mod" ) : "" );
		cJSON_AddStringToObject( o, "map", on ? Str( i, "map" ) : "" );
		if ( !host.empty() && !matchNames.count( host ) ) {
			matchNames[host] = Plain( Str( i, "name", "A friend" ) ) + "'s match";
		}
		cJSON_AddItemToArray( lists[g], o );
		online += on;
		count++;
	}
	for ( int g = 0; g < 3; g++ ) {
		if ( cJSON_GetArraySize( lists[g] ) == 0 ) {
			cJSON_Delete( lists[g] );
			continue;
		}
		cJSON *grp = cJSON_CreateObject();
		cJSON_AddStringToObject( grp, "label", labels[g] );
		cJSON_AddItemToObject( grp, "friends", lists[g] );
		cJSON_AddItemToArray( groups, grp );
	}
	cJSON_AddNumberToObject( s, "online", online );
	cJSON_AddNumberToObject( s, "waiting", cJSON_GetArraySize( invites ) + cJSON_GetArraySize( incoming ) );
	cJSON_AddNumberToObject( s, "count", count );
	const cJSON *limits = cJSON_GetObjectItemCaseSensitive( m, "limits" );
	cJSON_AddNumberToObject( s, "limit", limits ? Num( limits, "friends" ) : 200 );

	// the party: its members (the leader first as the hub sends them), who may kick, and its chat
	cJSON *party = cJSON_AddObjectToObject( s, "party" );
	const cJSON *p = cJSON_GetObjectItemCaseSensitive( m, "party" );
	const bool inParty = cJSON_IsObject( p );
	cJSON_AddBoolToObject( party, "on", inParty );
	cJSON *members = cJSON_AddArrayToObject( party, "members" );
	bool leaderMe = false;
	if ( inParty ) {
		leaderMe = myId == Str( p, "leader" );
		cJSON_ArrayForEach( i, cJSON_GetObjectItemCaseSensitive( p, "members" ) ) {
			const bool lead = Bool( i, "leader" );
			cJSON *o = Person( i, lead ? "Leader" : Bool( i, "online" ) ? Str( i, "detail", "Online" ) : "Offline" );
			cJSON_AddBoolToObject( o, "leader", lead );
			cJSON_AddBoolToObject( o, "me", myId == Str( i, "id" ) );
			cJSON_AddItemToArray( members, o );
		}
		cJSON_ArrayForEach( i, cJSON_GetObjectItemCaseSensitive( p, "pending" ) ) {
			cJSON *o = Person( i, "Invited" );
			cJSON_AddBoolToObject( o, "leader", false );
			cJSON_AddBoolToObject( o, "me", false );
			cJSON_AddBoolToObject( o, "pending", true );
			cJSON_AddItemToArray( members, o );
		}
		cJSON_AddNumberToObject( party, "max", Num( p, "max" ) );
	} else {
		cJSON_AddNumberToObject( party, "max", limits ? Num( limits, "party" ) : 8 );
		chat.clear();
	}
	cJSON_AddBoolToObject( party, "lead", leaderMe );
	cJSON_AddNumberToObject( party, "size", cJSON_GetArraySize( members ) );
	Chat( party );

	if ( strcmp( Menu_Get( "social.status" ), "online" ) ) {
		browsedAt = -100000;	// just online: an open Server Browser asks for the matches now
	}
	Replace( node, s );
	Menu_Dirty( "social" );
}

void PartyChat( const cJSON *m ) {
	const cJSON *from = cJSON_GetObjectItemCaseSensitive( m, "from" );
	std::string name = cJSON_IsObject( from ) ? Str( from, "name", "?" ) : cJSON_IsString( from ) ? from->valuestring : "?";
	chat.push_back( { name, Str( m, "text" ) } );
	if ( chat.size() > CHAT_LINES ) {
		chat.erase( chat.begin() );
	}
	cJSON *party = Menu_Node( "social.party" );
	if ( party ) {
		cJSON_DeleteItemFromObjectCaseSensitive( party, "chat" );
		Chat( party );
		Menu_Dirty( "social" );
	}
}

// the hub's board into leaderboards.rows, in the page's shape: the podium's colors, the level and XP
void Leaderboard( const cJSON *m ) {
	cJSON *rows = Menu_Node( "leaderboards.rows" );
	if ( !rows ) {
		return;
	}
	const std::string board = Str( m, "board", "level" ), scope = Str( m, "scope", "global" );
	static const char *podium[3] = { "#ffd24a", "#d6dde6", "#d68a4a" };
	cJSON *out = cJSON_CreateArray();
	// the other scope's rows stay until its board comes
	const cJSON *r;
	cJSON_ArrayForEach( r, rows ) {
		if ( scope != Str( r, "scope" ) ) {
			cJSON_AddItemToArray( out, cJSON_Duplicate( r, true ) );
		}
	}
	cJSON_ArrayForEach( r, cJSON_GetObjectItemCaseSensitive( m, "rows" ) ) {
		const int rank = (int)Num( r, "rank" );
		const char *accent = rank >= 1 && rank <= 3 ? podium[rank - 1] : "#c8894a";
		char xp[32];
		snprintf( xp, sizeof( xp ), "%.0f", Num( r, "xp" ) );
		std::string xps = xp;
		for ( int k = (int)xps.size() - 3; k > 0; k -= 3 ) {
			xps.insert( k, "," );
		}
		cJSON *o = cJSON_CreateObject();
		cJSON_AddStringToObject( o, "scope", scope.c_str() );
		cJSON_AddStringToObject( o, "id", Str( r, "id" ) );
		cJSON_AddNumberToObject( o, "rank", rank );
		cJSON_AddStringToObject( o, "name", Str( r, "name", "Player" ) );
		cJSON_AddNumberToObject( o, "level", board == "ranked" ? Num( r, "rating" ) : Num( r, "level" ) );
		cJSON_AddStringToObject( o, "xp", xps.c_str() );
		cJSON_AddBoolToObject( o, "online", Bool( r, "online" ) );
		cJSON_AddBoolToObject( o, "mine", myId == Str( r, "id" ) );
		cJSON_AddBoolToObject( o, "podium", rank >= 1 && rank <= 3 );
		cJSON_AddStringToObject( o, "rank_color", accent );
		cJSON_AddStringToObject( o, "glow", ( std::string( accent ) + "88" ).c_str() );
		// the player's card as their game shows it, with the parts this catalog doesn't know put back to the
		// default (q3_cards.gd known_parts): the hub can't check unlocks without the career
		cJSON *known = Prog::KnownParts( cJSON_GetObjectItemCaseSensitive( r, "card" ) );
		cJSON *card = Prog::CardView( known, (int)Num( r, "level" ), cJSON_GetObjectItemCaseSensitive( r, "stats" ), Str( r, "id" ) );
		cJSON_Delete( known );
		cJSON_AddStringToObject( o, "accent", Prog::Str( card, "accent" ).c_str() );
		cJSON_AddStringToObject( o, "title", Prog::Str( card, "title" ).c_str() );
		cJSON_AddStringToObject( o, "emblem", Prog::Str( card, "emblem" ).c_str() );
		cJSON_AddStringToObject( o, "banner", Prog::Str( card, "banner_bg" ).c_str() );
		cJSON_AddItemToObject( o, "card", card );
		cJSON_AddItemToArray( out, o );
	}
	Replace( rows, out );
	char path[64];
	snprintf( path, sizeof( path ), "leaderboards.count.%s", scope.c_str() );
	{
		char total[64];
		const int n = (int)Num( m, "total" );
		snprintf( total, sizeof( total ), n == 1 ? "%d player" : "%d players", n );
		Menu_Set( path, total );	// cJSON_SetValuestring won't grow a string
	}
	snprintf( path, sizeof( path ), "leaderboards.you.%s", scope.c_str() );
	{
		const cJSON *me = cJSON_GetObjectItemCaseSensitive( m, "me" );
		char you[64];
		if ( cJSON_IsObject( me ) ) {
			snprintf( you, sizeof( you ), "You: #%d", (int)Num( me, "rank" ) );
		} else {
			snprintf( you, sizeof( you ), "You: not on this board yet" );
		}
		Menu_Set( path, you );
	}
	// the page lays out around a picked row: yours when you're on the board, else the top one
	{
		const cJSON *me = cJSON_GetObjectItemCaseSensitive( m, "me" );
		const cJSON *top = cJSON_GetArrayItem( cJSON_GetObjectItemCaseSensitive( m, "rows" ), 0 );
		const char *pick = cJSON_IsObject( me ) ? Str( me, "id" ) : top ? Str( top, "id" ) : "";
		if ( pick[0] ) {
			Menu_Set( "leaderboards.pick", pick );
		}
	}
	Menu_Dirty( "leaderboards" );
}

// a string value, never read as a number or a bool as Menu_Set would (a player id can be all digits)
void SetString( const char *path, const char *value ) {
	cJSON *j = Menu_Node( path );
	if ( !j || cJSON_IsArray( j ) || cJSON_IsObject( j ) || ( cJSON_IsString( j ) && !strcmp( j->valuestring, value ) ) ) {
		return;
	}
	if ( cJSON_IsString( j ) && j->valuestring ) {
		cJSON_free( j->valuestring );
	}
	j->type = ( j->type & ~0xff ) | cJSON_String;
	j->valuestring = (char *)cJSON_malloc( strlen( value ) + 1 );
	memcpy( j->valuestring, value, strlen( value ) + 1 );
	Menu_Dirty( path );
}

bool Online() {
	return !strcmp( Menu_Get( "social.status" ), "online" );
}

bool Joining() {
	const std::string s = Menu_Get( "server_browser.join.status" );
	return s == "asking" || s == "connecting";
}

std::string MatchName( const std::string &host ) {
	const auto it = matchNames.find( host );
	return it != matchNames.end() ? it->second : "that match";
}

void Refuse( const char *why ) {
	PlaySound( Sound::Buzz );
	Menu_Toast( why, "warn" );
}

// the hub's listing words (hub.ts's REGIONS, oz_ice.c's modes) as the page's filters have them
const char *ModeLabel( const std::string &id ) {
	return id == "duel" ? "Duel" : id == "tdm" ? "Team Deathmatch" : id == "ctf" ? "Capture the Flag" : "Free For All";
}

const char *RegionLabel( const std::string &id ) {
	static const char *const regions[][2] = { { "na_east", "North America East" }, { "na_west", "North America West" },
		{ "eu", "Europe" }, { "sa", "South America" }, { "asia", "Asia" }, { "oce", "Oceania" }, { "meaf", "Middle East and Africa" } };
	for ( const auto &r : regions ) {
		if ( id == r[0] ) {
			return r[1];
		}
	}
	return "Unknown region";
}

// an arena's name from the game's scripts (the menu model's maps), else its file name
std::string MapLabel( const std::string &map ) {
	const cJSON *m;
	cJSON_ArrayForEach( m, Menu_Node( "maps" ) ) {
		if ( !StrICmp( Str( m, "map" ), map.c_str() ) ) {
			return Plain( Str( m, "name", map.c_str() ) );
		}
	}
	return map;
}

// the hub's open matches (net_matches {matches, region}) as the Server Browser's Internet rows
void Browser( const cJSON *m ) {
	cJSON *rows = Menu_Node( "server_browser.internet" );
	if ( !rows ) {
		return;
	}
	// a star the player put on a row stays with its host
	std::map<std::string, bool> fav;
	const cJSON *r;
	cJSON_ArrayForEach( r, rows ) {
		if ( Bool( r, "fav" ) ) {
			fav[Str( r, "host" )] = true;
		}
	}
	const bool hide = atoi( OzE_CvarString( "oz_network_hide_ip" ) ) != 0;
	cJSON *out = cJSON_CreateArray();
	cJSON_ArrayForEach( r, cJSON_GetObjectItemCaseSensitive( m, "matches" ) ) {
		const std::string host = Str( r, "host" );
		const int players = (int)Num( r, "players" ), max = (int)Num( r, "max" );
		const std::string name = Plain( Str( r, "name", "Match" ) );
		// the link a join gets: relayed when either side hides its IP, else direct first (the relay
		// only when the two networks can't reach each other, which shows once connected)
		const bool relayed = Bool( r, "relay" ) || hide;
		char count[32];
		snprintf( count, sizeof( count ), "%d/%d", players, max );
		matchNames[host] = name;
		cJSON *o = cJSON_CreateObject();
		cJSON_AddStringToObject( o, "name", name.c_str() );
		cJSON_AddStringToObject( o, "owner", Plain( Str( r, "owner" ) ).c_str() );
		cJSON_AddStringToObject( o, "target", "" );
		cJSON_AddBoolToObject( o, "password", Bool( r, "password" ) );
		cJSON_AddBoolToObject( o, "ranked", Bool( r, "ranked" ) );
		cJSON_AddBoolToObject( o, "dedicated", Bool( r, "dedicated" ) );
		cJSON_AddStringToObject( o, "map", MapLabel( Str( r, "map" ) ).c_str() );
		cJSON_AddStringToObject( o, "mode", ModeLabel( Str( r, "mode" ) ) );
		cJSON_AddStringToObject( o, "players", count );
		cJSON_AddBoolToObject( o, "full", players >= max );
		cJSON_AddBoolToObject( o, "empty", players == 0 );
		cJSON_AddStringToObject( o, "where", RegionLabel( Str( r, "region" ) ) );
		cJSON_AddBoolToObject( o, "up", true );
		cJSON_AddBoolToObject( o, "fav", fav.count( host ) > 0 );
		cJSON_AddBoolToObject( o, "mine", host == myId );
		cJSON_AddStringToObject( o, "host", host.c_str() );
		cJSON_AddBoolToObject( o, "relayed", relayed );
		cJSON_AddStringToObject( o, "badge", relayed ? "Relayed" : "Direct" );
		cJSON_AddBoolToObject( o, "open", Str( r, "address" )[0] != '\0' );	// a UDP port open too (UPnP): any game can join
		cJSON_AddItemToArray( out, o );
	}
	Replace( rows, out );
	Menu_Set( "server_browser.answered", "true" );
	Menu_Dirty( "server_browser" );
	Servers_MatchesChanged();	// the page's filters give each its shown flag (oz_ui_servers.cpp)
}

void Browse() {
	browsedAt = OzE_Milliseconds();
	if ( Online() ) {
		OzE_ExecuteNow( "oz_browse" );
	}
}

void ClearAsk() {
	SetString( "server_browser.ask.host", "" );
	SetString( "server_browser.ask.name", "" );
}

/*
================
Quick Play (pages/quick_join.rml, the web menu's QuickJoinPage): the hub's pick of the open match in a
mode most worth joining (net_quick, asked with oz_ice.c's oz_quick). found is joined at once, with
oz_join and the Server Browser's join state; far is the best there is, on another continent, and the
page asks first; none says why, with how many matches in the mode a join would refuse (full, locked,
another version). quick_join.status: searching, joining, far, none, failed or "" (nothing to show);
asked is the mode an answer is for (the page shows it only on that mode), text what the page says.
================
*/
// the region Quick Play asks for (Settings > Network), as the page shows it
void QuickRegion() {
	const char *region = OzE_CvarString( "oz_network_region" );
	SetString( "quick_join.region", !region[0] || !StrICmp( region, "auto" ) ? "Automatic" : RegionLabel( region ) );
}

std::string ModeName( const std::string &id ) {
	const cJSON *m;
	cJSON_ArrayForEach( m, Menu_Node( "quick_join.modes" ) ) {
		if ( id == Str( m, "id" ) ) {
			return Str( m, "name" );
		}
	}
	return id;
}

bool QuickBusy() {
	const std::string s = Menu_Get( "quick_join.status" );
	return s == "searching" || s == "joining";
}

void QuickShow( const char *status, const std::string &text ) {
	SetString( "quick_join.status", status );
	SetString( "quick_join.text", text.c_str() );
}

// a join under way: "Asking Menu Match (The Longest Yard, 3/12, about 50 ms)"
void QuickJoining( const char *join ) {
	QuickShow( "joining", std::string( !strcmp( join, "connecting" ) ? "Connecting to " : "Asking " ) + quick.name + " (" + quick.line + ")" );
}

void QuickJoin() {
	matchNames[quick.host] = quick.name;
	QuickJoining( "asking" );
	OzE_ExecuteNow( ( "oz_join " + quick.host + "\n" ).c_str() );
}

// net_quick {status: found|far|none, mode, region, match?, est?, near, skipped}
void QuickAnswer( const cJSON *m ) {
	const std::string mode = Str( m, "mode" ), status = Str( m, "status" ), name = ModeName( mode );
	// a late answer, for a search the player cancelled or made again in another mode
	if ( strcmp( Menu_Get( "quick_join.status" ), "searching" ) || mode != Menu_Get( "quick_join.asked" ) ) {
		return;
	}
	const cJSON *match = cJSON_GetObjectItemCaseSensitive( m, "match" );
	if ( cJSON_IsObject( match ) && ( status == "found" || status == "far" ) ) {
		char line[192];
		quick.host = Str( match, "host" );
		quick.name = Plain( Str( match, "name", "Match" ) );
		quick.est = (int)Num( m, "est" );
		snprintf( line, sizeof( line ), "%s, %d/%d, about %d ms", MapLabel( Str( match, "map" ) ).c_str(), (int)Num( match, "players" ),
			(int)Num( match, "max" ), quick.est );
		quick.line = line;
		if ( status == "found" ) {
			QuickJoin();
		} else {
			QuickShow( "far", "The closest open " + name + " match is in " + RegionLabel( Str( match, "region" ) ) + ", about " +
				std::to_string( quick.est ) + " ms away." );
		}
		return;
	}
	const int skipped = (int)Num( m, "skipped" );
	QuickShow( "none", "No open " + name + " match right now" +
		( skipped ? " (" + std::to_string( skipped ) + " full, locked or on another version)" : "" ) + "." );
}

/*
================
A party follows its leader (the Godot build's online.gd): the leader's hosted match is listed, then the
hub hears party_follow, and each member gets net_follow {host, leader, name, password} and joins it from
the menus. A member in a match already, or joining one, is only told: Friends' Join takes them there.
================
*/
void Follow( const cJSON *m ) {
	const std::string host = Str( m, "host" ), server = Str( m, "server" ), who = Plain( Str( m, "name", "Your party leader" ) ), password = Str( m, "password" );
	if ( ( host.empty() && server.empty() ) || host == myId ) {
		return;
	}
	if ( !host.empty() && !matchNames.count( host ) ) {
		matchNames[host] = who + "'s match";
	}
	if ( OzE_ClientState() != 0 || Joining() || password.find( '"' ) != std::string::npos ) {
		Printf( "UI: party: %s is in a match, not followed\n", who.c_str() );
		Menu_Toast( ( who + " is in a match. Join it from Friends." ).c_str(), "info" );
		return;
	}
	if ( host.empty() ) {
		// a community server (PH step 3): joined as from the Server Browser, with the leader's password
		Printf( "UI: party: following %s onto %s\n", who.c_str(), server.c_str() );
		Menu_Toast( ( "Joining " + who + "'s server" ).c_str(), "info" );
		Servers_FollowServer( server.c_str(), password.c_str() );
		return;
	}
	Printf( "UI: party: following %s into their match\n", who.c_str() );
	Menu_Toast( ( "Joining " + who + "'s match" ).c_str(), "info" );
	followHost = host;
	OzE_ExecuteNow( ( "oz_join " + host + " \"" + password + "\"\n" ).c_str() );
}

/*
================
The connection screen (pages/connect.rml, P8 and decision 21): the server, where the connection is,
a download, what the server said, and a peer-to-peer match's link line (oz_net_link: direct or
relayed). Only while a connection is under way: otherwise the model stays as it is (captures set it).
================
*/
void SetIfChanged( const char *path, const std::string &value ) {
	if ( value != Menu_Get( path ) ) {
		SetString( path, value.c_str() );
	}
}

std::string connectLogged;	// what the connection screen last logged; cleared once it closes, so each connection logs

void ConnectFrame() {
	char server[256], message[1024], buf[160];
	int tries = 0, downloadMs = 0;
	const int phase = OzE_Connection( server, sizeof( server ), &tries, message, sizeof( message ), &downloadMs );
	if ( !phase ) {
		return;
	}
	std::string title = server, sub = "Server", link;
	if ( !StrICmp( std::string( server ).substr( 0, 4 ).c_str(), "ice:" ) ) {
		const std::string host = server + 4;
		title = matchNames.count( host ) ? matchNames[host] : "A peer-to-peer match";
		sub = "Peer-to-peer";
		const std::string l = OzE_CvarString( "oz_net_link" );
		if ( l == "direct" ) {
			link = "Direct link to the host";
		} else if ( l == "relayed" ) {
			link = atoi( OzE_CvarString( "oz_network_hide_ip" ) ) ? "Relayed link: Hide my IP is on"
				: "Relayed link: the host hides its IP, or a network blocks direct links";
		}
	} else if ( !Servers_Name( server ).empty() ) {	// a community server joined from the Server Browser: its name
		title = Servers_Name( server );
		sub = std::string( "Server " ) + server;
	}
	std::string download = phase == 3 ? OzE_CvarString( "cl_downloadName" ) : "";
	download = download.substr( download.find_last_of( '/' ) + 1 );	// the pak, without its game folder
	std::string state;
	if ( phase == 1 ) {
		state = tries > 1 ? "Reaching the server, try " + std::to_string( tries ) : "Reaching the server";
	} else if ( phase == 2 ) {
		state = tries > 1 ? "Joining, try " + std::to_string( tries ) : "Joining";
	} else {
		state = download.empty() ? "Getting the match" : "Downloading";
	}
	SetIfChanged( "connect.server", title );
	SetIfChanged( "connect.sub", sub );
	SetIfChanged( "connect.state", state );
	SetIfChanged( "connect.message", phase < 3 ? message : "" );
	SetIfChanged( "connect.link", link );
	SetIfChanged( "connect.download.name", download );
	if ( !download.empty() ) {
		const double size = atof( OzE_CvarString( "cl_downloadSize" ) ), count = atof( OzE_CvarString( "cl_downloadCount" ) );
		const int pct = size > 0 ? (int)( count * 100.0 / size ) : 0;
		if ( size > 0 ) {
			snprintf( buf, sizeof( buf ), "%.0f of %.0f KB, %.0f KB/s", count / 1024.0, size / 1024.0, downloadMs > 0 ? count / 1024.0 / ( downloadMs / 1000.0 ) : 0.0 );
		} else {
			snprintf( buf, sizeof( buf ), "%.0f KB", count / 1024.0 );
		}
		SetIfChanged( "connect.download.text", buf );
		SetIfChanged( "connect.download.pct", std::to_string( pct < 0 ? 0 : pct > 100 ? 100 : pct ) + "%" );
	}
	// once a connection, for the tests' logs: what the screen shows
	const std::string line = title + ( link.empty() ? "" : " (" + link + ")" ) + ( download.empty() ? "" : ", downloading " + download );
	if ( line != connectLogged ) {
		connectLogged = line;
		Printf( "UI: connection screen: %s\n", line.c_str() );
	}
}

}	// namespace

namespace OzUI {

// the online pages' actions (Menu_Run asks here last): true when taken
//   browse                ask the hub for the open matches again
//   join <host> [locked]  join a peer-to-peer match (locked: its password first, in the Server Browser)
//   joinask               join the match the password prompt asks about, with the password typed
//   askcancel, unjoin     close the password prompt, give up joining
//   quick                 Quick Play: the hub's pick in quick_join.mode, joined when it's close
//   quickjoin             join the far match Quick Play offers
//   quickcancel           stop looking, or give up the join
//   quickhost             Custom Match on Quick Play's mode, hosted online
bool Social_Action( const Rml::String &verb, const Rml::String &arg, Rml::Element *element ) {
	if ( verb == "browse" ) {
		Browse();
	} else if ( verb == "quick" ) {
		const std::string mode = Menu_Get( "quick_join.mode" );
		if ( QuickBusy() ) {
			Refuse( "Already looking." );
		} else if ( Joining() ) {
			Refuse( "Already joining a match." );
		} else if ( !Online() ) {
			Refuse( "The online service isn't connected." );
		} else {
			SetString( "quick_join.asked", mode.c_str() );
			SetString( "quick_join.asked_name", ModeName( mode ).c_str() );
			QuickShow( "searching", "Looking for the best " + ModeName( mode ) + " match" );
			OzE_ExecuteNow( ( "oz_quick " + mode + "\n" ).c_str() );
		}
	} else if ( verb == "quickjoin" ) {
		if ( Joining() ) {
			Refuse( "Already joining a match." );
		} else if ( !strcmp( Menu_Get( "quick_join.status" ), "far" ) ) {
			QuickJoin();
		}
	} else if ( verb == "quickcancel" ) {
		if ( !strcmp( Menu_Get( "quick_join.status" ), "joining" ) ) {
			OzE_ExecuteNow( "oz_unjoin\n" );	// its join state clears the page
		}
		QuickShow( "", "" );
	} else if ( verb == "quickhost" ) {
		// the web menu's presetCreateMatch: Custom Match on the mode, its map browser on the mode's arenas (the
		// arena kept when it's made for the mode, as the page's own mode spinner does), the server Online
		const std::string mode = Menu_Get( "quick_join.mode" );
		int i = 0;
		const cJSON *md;
		cJSON_ArrayForEach( md, Menu_Node( "create_match.modes" ) ) {
			if ( mode == Str( md, "id" ) ) {
				const std::string fit = Str( md, "fit" );
				const cJSON *map = cJSON_GetArrayItem( Menu_Node( "maps" ), atoi( Menu_Get( "create_match_pick.custom.mapi" ) ) ), *t;
				bool fits = false;
				cJSON_ArrayForEach( t, ( map ? cJSON_GetObjectItemCaseSensitive( map, "types" ) : nullptr ) ) {
					fits = fits || ( cJSON_IsString( t ) && fit == t->valuestring );
				}
				Menu_Set( "create_match.custom.mi", std::to_string( i ).c_str() );
				SetString( "create_match_pick.custom.fit", fit.c_str() );
				if ( !fits ) {
					Menu_Set( "create_match_pick.custom.mapi", std::to_string( (int)Num( md, "pick" ) ).c_str() );
				}
				break;
			}
			i++;
		}
		Menu_Set( "create_match.custom.server", "Online (listed in the Server Browser)" );
		OpenPageByName( "create_match" );
	} else if ( verb == "join" ) {
		Rml::StringList a;
		Rml::StringUtilities::ExpandString( a, arg, ' ' );
		if ( a.empty() ) {
			return true;
		}
		const std::string host = a[0];
		if ( !Online() ) {
			Refuse( "Joining needs the online service, and it isn't connected." );
		} else if ( Joining() ) {
			Refuse( "Already joining a match. Cancel that first." );
		} else if ( host == myId ) {
			Refuse( "That's your own match." );
		} else if ( a.size() > 1 && a[1] == "locked" ) {
			SetString( "server_browser.ask.host", host.c_str() );
			SetString( "server_browser.ask.name", MatchName( host ).c_str() );
			if ( strcmp( TopPageName(), "server_browser" ) ) {
				OpenPageByName( "server_browser" );
			}
			GetContext()->Update();
			if ( Rml::ElementDocument *doc = TopDocument() ) {
				FocusQuietly( doc->GetElementById( "sb_password" ) );
			}
		} else {
			OzE_ExecuteNow( ( "oz_join " + host + "\n" ).c_str() );
		}
	} else if ( verb == "joinask" ) {
		Rml::ElementDocument *doc = element ? element->GetOwnerDocument() : TopDocument();
		Rml::Element *input = doc ? doc->GetElementById( "sb_password" ) : nullptr;
		const Rml::String password = input ? input->GetAttribute<Rml::String>( "value", "" ) : "";
		const std::string host = Menu_Get( "server_browser.ask.host" );
		if ( host.empty() ) {
			return true;
		}
		if ( password.find( '"' ) != Rml::String::npos ) {
			Refuse( "A password can't have quotation marks." );
			return true;
		}
		ClearAsk();
		if ( input ) {
			input->SetAttribute( "value", "" );
		}
		OzE_ExecuteNow( ( "oz_join " + host + " \"" + password + "\"\n" ).c_str() );
	} else if ( verb == "askcancel" ) {
		ClearAsk();
	} else if ( verb == "unjoin" ) {
		OzE_ExecuteNow( "oz_unjoin\n" );
	} else {
		return false;
	}
	return true;
}

// a page opened: the Server Browser asks for the open matches at once; Quick Play shows the region
// it asks for and drops a finished search (it belongs to the page it was made on); Custom Match in a
// party hosts online, so the party follows its leader in (the web menu's CreateMatchPage)
void Social_Opened( const char *page ) {
	if ( !strcmp( page, "server_browser" ) ) {
		Browse();
	} else if ( !strcmp( page, "quick_join" ) ) {
		QuickRegion();
		if ( !QuickBusy() ) {
			QuickShow( "", "" );
		}
	} else if ( !strcmp( page, "create_match" ) && Online() && cJSON_IsTrue( Menu_Node( "social.party.on" ) ) ) {
		Menu_Set( "create_match.custom.server", "Online (listed in the Server Browser)" );
	}
}

// the hub's party_follow: the match a party leader hosts, run once it's listed (the host action, after
// oz_host), or the community server the leader joins (PH step 3: "server", its address, from the Server
// Browser's Connect); "" when this player doesn't lead a party
Rml::String Social_FollowText( const char *kind, const char *key, const char *password ) {
	if ( !Online() || myId.empty() || !cJSON_IsTrue( Menu_Node( "social.party.on" ) ) || !cJSON_IsTrue( Menu_Node( "social.party.lead" ) ) ) {
		return "";
	}
	if ( !strcmp( kind, "server" ) ) {
		return std::string( "hub party_follow server " ) + key + " password \"" + password + "\"\n";
	}
	return "hub party_follow host " + myId + " password \"" + password + "\"\n";
}

// each frame while a page is open: the Server Browser's list kept fresh (every 15 s, as the hub
// allows), Quick Play's region (Settings may have changed it underneath), the connection screen
void Social_Frame() {
	if ( IsPageOpen( "server_browser" ) && Online() && OzE_Milliseconds() - browsedAt > 15000 ) {
		Browse();
	}
	if ( IsPageOpen( "quick_join" ) ) {
		QuickRegion();
	}
	if ( IsPageOpen( "connect" ) ) {
		ConnectFrame();
	} else {
		connectLogged.clear();
	}
}

}	// namespace OzUI

extern "C" void OzUI_OnlineStatus( int status, const char *reason ) {
	static const char *names[] = { "offline", "connecting", "online" }, *labels[] = { "Offline", "Connecting", "Online" };
	if ( status < 0 || status > 2 ) {
		return;
	}
	if ( lastStatus == 2 && status != 2 && reason && reason[0] ) {
		Menu_Toast( reason, "warn" );
	}
	lastStatus = status;
	if ( status != 2 ) {
		Menu_Set( "social.status", names[status] );
		Menu_Set( "social.status_label", labels[status] );
	}
}

extern "C" void OzUI_HubMessage( const char *type, const cJSON *msg ) {
	if ( !strcmp( type, "social" ) ) {
		Social( msg );
	} else if ( !strcmp( type, "party_chat" ) ) {
		PartyChat( msg );
	} else if ( !strcmp( type, "leaderboard" ) ) {
		Leaderboard( msg );
	} else if ( !strcmp( type, "match_history" ) ) {
		Prog::MatchHistory( msg );
	} else if ( !strcmp( type, "career" ) ) {
		Prog::SharedCareer( msg );
	} else if ( !strcmp( type, "stats_report" ) || !strcmp( type, "stats_match" ) ) {
		if ( !strcmp( TopPageName(), "profile" ) ) {
			OzE_ExecuteNow( "hub match_history" );	// a match came in or counted while the history shows
		}
	} else if ( !strcmp( type, "notice" ) ) {
		const cJSON *tone = cJSON_GetObjectItemCaseSensitive( msg, "tone" );
		Menu_Toast( Str( msg, "text" ), cJSON_IsString( tone ) && !strcmp( tone->valuestring, "info" ) ? "info" : "warn" );
	} else if ( !strcmp( type, "net_matches" ) ) {
		Browser( msg );
	} else if ( !strcmp( type, "net_quick" ) ) {
		QuickAnswer( msg );
	} else if ( !strcmp( type, "net_follow" ) ) {
		Follow( msg );
	}
}

// a join's progress (oz_ice.c): asking, connecting, joined (reason: the link), failed (reason: why), ""
extern "C" void OzUI_JoinState( const char *status, const char *host, const char *reason ) {
	const std::string name = MatchName( host );
	Menu_Set( "server_browser.join.status", status );
	SetString( "server_browser.join.host", host );
	SetString( "server_browser.join.name", name.c_str() );
	SetString( "server_browser.join.link", !strcmp( status, "joined" ) ? reason : "" );
	if ( !strcmp( status, "asking" ) && strcmp( TopPageName(), "server_browser" ) && strcmp( TopPageName(), "quick_join" ) && followHost != host ) {
		Menu_Toast( ( "Asking to join " + name ).c_str(), "info" );	// from Friends: the browser and Quick Play show the join
	} else if ( !strcmp( status, "failed" ) ) {
		Refuse( ( "Couldn't join " + name + ": " + reason ).c_str() );
	} else if ( !strcmp( status, "joined" ) ) {
		Servers_JoinedMatch( host, name.c_str() );	// the Server Browser's history (oz_ui_servers.cpp)
	}
	if ( strcmp( status, "asking" ) && strcmp( status, "connecting" ) ) {
		followHost.clear();
	}
	// Quick Play's join: its line follows the join; a refusal or a failed link stays on the page, which then
	// offers hosting instead; joined (the connection screen takes over) or given up, the page is clear again
	if ( !strcmp( Menu_Get( "quick_join.status" ), "joining" ) && ( quick.host == host || !status[0] ) ) {
		if ( !strcmp( status, "asking" ) || !strcmp( status, "connecting" ) ) {
			QuickJoining( status );
		} else if ( !strcmp( status, "failed" ) ) {
			std::string why = reason[0] ? reason : "the host didn't answer";
			while ( !why.empty() && why.back() == '.' ) {
				why.pop_back();
			}
			QuickShow( "failed", "Couldn't join " + quick.name + ": " + why + "." );
		} else {
			QuickShow( "", "" );
		}
	}
}
