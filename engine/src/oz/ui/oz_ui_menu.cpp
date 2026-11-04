/*
Oz: the menus (ENGINE_PLAN.md P5), the Godot build's web menu (webui/components) as RML pages in
ui/pages, their data and their actions.

- Data: one data model, "menu", over a JSON tree: ui/data/menu.json (the menu tree, and stand-in
  data for the systems not built yet: friends, boards, replays, servers, the profile) and what the
  engine knows (the arenas and bots from the game's scripts, the characters, the player's name, the
  version). Each top-level key is a variable: pages read it with {{ }}, data-for and data-if, and
  change their own state with data-event assignments (data-event-click="state.tab = 'lan'"). C++
  changes a node in place (Set) and dirties its top-level key.
- Actions: on<event> attributes hold commands separated by ';':
    page <name>      open a page                 sub <name>     a submenu of the main menu
    back             go back                     close          close every page
    soon <system>    refused: the buzz, and a toast that it comes with that system
    cmd <command>    a console command           social         the friends panel
    toast <text>     a toast                     sound <in|move|out|buzz>
    set <path> <v>   a value (a string)          next           the showcase's next subject
    url <link>       an https link in the player's browser (Discord, GitHub, Steam's hosts only)
    startmatch <p>   an offline match            host <path>    the same match, hosted online (P8)
    and the online pages' own: browse, join, joinask, askcancel, unjoin, quick, quickjoin,
    quickcancel, quickhost (oz_ui_social.cpp); servers, serverjoin, sbsort (oz_ui_servers.cpp);
    Skirmish's skimport and race (oz_ui_skirmish.cpp)
- Spinners: an element with class "spinner", spin="<path>" and options="a|b|c" steps through the
  options with Left and Right (or its .prev and .next arrows), wrapping, and stores the option.
- Tabs: Q and E (the shoulder buttons) click the next or previous .tab in the page's .tabs bar.
*/
#include "oz_ui_internal.h"

#include "cJSON.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

namespace OzUI {

static cJSON *root;
static Rml::DataModelHandle model;
static std::vector<Rml::String> boundKeys;

/*
================
The JSON data model: a variable definition per JSON kind, so data-for walks arrays, dotted
addresses walk objects and everything else is a scalar
================
*/
static Rml::DataVariable Var( cJSON *j );

class JsonScalar : public Rml::VariableDefinition {
public:
	JsonScalar() : Rml::VariableDefinition( Rml::DataVariableType::Scalar ) {}

	Rml::StringList ReflectMemberNames() override {	// RmlUi's debugger lists every model
		return {};
	}

	bool Get( void *ptr, Rml::Variant &variant ) override {
		const cJSON *j = (const cJSON *)ptr;
		if ( cJSON_IsString( j ) ) {
			variant = Rml::String( j->valuestring );
		} else if ( cJSON_IsBool( j ) ) {
			variant = cJSON_IsTrue( j ) ? true : false;
		} else if ( cJSON_IsNumber( j ) ) {
			variant = j->valuedouble;
		} else {
			variant = Rml::String();
		}
		return true;
	}

	bool Set( void *ptr, const Rml::Variant &variant ) override {
		cJSON *j = (cJSON *)ptr;
		if ( cJSON_IsString( j ) && j->valuestring ) {
			cJSON_free( j->valuestring );
			j->valuestring = nullptr;
		}
		j->type &= ~0xff;
		switch ( variant.GetType() ) {
		case Rml::Variant::BOOL:
			j->type |= variant.Get<bool>() ? cJSON_True : cJSON_False;
			break;
		case Rml::Variant::STRING: {
			const Rml::String &s = variant.GetReference<Rml::String>();
			j->type |= cJSON_String;
			j->valuestring = (char *)cJSON_malloc( s.size() + 1 );
			memcpy( j->valuestring, s.c_str(), s.size() + 1 );
			break;
		}
		default:
			j->type |= cJSON_Number;
			j->valuedouble = variant.Get<double>( 0.0 );
			j->valueint = (int)j->valuedouble;
			break;
		}
		return true;
	}
};

class JsonArray : public Rml::VariableDefinition {
public:
	JsonArray() : Rml::VariableDefinition( Rml::DataVariableType::Array ) {}

	Rml::StringList ReflectMemberNames() override {
		return {};
	}

	int Size( void *ptr ) override {
		return cJSON_GetArraySize( (cJSON *)ptr );
	}

	Rml::DataVariable Child( void *ptr, const Rml::DataAddressEntry &address ) override {
		if ( address.index < 0 ) {
			return address.name == "size" ? Rml::MakeLiteralIntVariable( Size( ptr ) ) : Rml::DataVariable();
		}
		cJSON *c = cJSON_GetArrayItem( (cJSON *)ptr, address.index );
		return c ? Var( c ) : Rml::DataVariable();
	}
};

class JsonObject : public Rml::VariableDefinition {
public:
	JsonObject() : Rml::VariableDefinition( Rml::DataVariableType::Struct ) {}

	Rml::StringList ReflectMemberNames() override {	// the debugger can't tell which object it is asked about
		return {};
	}

	Rml::DataVariable Child( void *ptr, const Rml::DataAddressEntry &address ) override {
		cJSON *c = cJSON_GetObjectItemCaseSensitive( (cJSON *)ptr, address.name.c_str() );
		return c ? Var( c ) : Rml::DataVariable();
	}
};

static JsonScalar jsonScalar;
static JsonArray jsonArray;
static JsonObject jsonObject;

static Rml::DataVariable Var( cJSON *j ) {
	return Rml::DataVariable( cJSON_IsArray( j ) ? (Rml::VariableDefinition *)&jsonArray
		: cJSON_IsObject( j ) ? (Rml::VariableDefinition *)&jsonObject : (Rml::VariableDefinition *)&jsonScalar, j );
}

// "a.b.2.c" -> the node, or NULL
cJSON *Menu_Node( const char *path ) {
	cJSON *j = root;
	char part[128];

	while ( j && *path ) {
		size_t n = strcspn( path, "." );
		if ( n >= sizeof( part ) ) {
			return nullptr;
		}
		memcpy( part, path, n );
		part[n] = '\0';
		path += n + ( path[n] == '.' );
		j = cJSON_IsArray( j ) ? cJSON_GetArrayItem( j, atoi( part ) ) : cJSON_GetObjectItemCaseSensitive( j, part );
	}
	return j;
}

void Menu_Dirty( const char *path ) {
	char top[64];
	const size_t n = strcspn( path, "." );

	if ( !model || n >= sizeof( top ) ) {
		return;
	}
	memcpy( top, path, n );
	top[n] = '\0';
	model.DirtyVariable( top );
}

const char *Menu_Get( const char *path ) {
	static char buf[64];
	cJSON *j = Menu_Node( path );
	if ( cJSON_IsString( j ) ) {
		return j->valuestring;
	}
	if ( cJSON_IsBool( j ) ) {
		return cJSON_IsTrue( j ) ? "true" : "false";
	}
	if ( cJSON_IsNumber( j ) ) {
		snprintf( buf, sizeof( buf ), "%g", j->valuedouble );
		return buf;
	}
	return "";
}

// a scalar, as the page would set it: true and false are bools, numbers numbers, the rest strings
bool Menu_Set( const char *path, const char *value ) {
	cJSON *j = Menu_Node( path );
	if ( !j || cJSON_IsArray( j ) || cJSON_IsObject( j ) ) {
		return false;
	}
	char *end;
	const double d = strtod( value, &end );
	if ( !strcmp( value, "true" ) || !strcmp( value, "false" ) ) {
		jsonScalar.Set( j, Rml::Variant( !strcmp( value, "true" ) ) );
	} else if ( *value && !*end ) {
		jsonScalar.Set( j, Rml::Variant( d ) );
	} else {
		jsonScalar.Set( j, Rml::Variant( Rml::String( value ) ) );
	}
	Menu_Dirty( path );
	return true;
}

// the node's payload replaced by src's (src is consumed); the node itself stays, as the model holds it
static void Replace( cJSON *dst, cJSON *src ) {
	cJSON *c;
	while ( ( c = dst->child ) != nullptr ) {
		cJSON_Delete( cJSON_DetachItemViaPointer( dst, c ) );
	}
	while ( ( c = src->child ) != nullptr ) {
		cJSON_AddItemToArray( dst, cJSON_DetachItemViaPointer( src, c ) );	// keeps the item's key
	}
	cJSON_Delete( src );
}

/*
================
What the engine knows: the arenas and bots from the game's scripts (scripts/arenas.txt and *.arena,
scripts/bots.txt and *.bot), the characters
================
*/
static std::vector<std::map<Rml::String, Rml::String>> InfoBlocks( const char *text ) {
	std::vector<std::map<Rml::String, Rml::String>> out;
	const char *p = text;

	while ( *p ) {
		// to the next {, past comments
		if ( p[0] == '/' && p[1] == '/' ) {
			while ( *p && *p != '\n' ) p++;
			continue;
		}
		if ( *p != '{' ) {
			p++;
			continue;
		}
		p++;
		std::map<Rml::String, Rml::String> block;
		Rml::String key;
		while ( *p && *p != '}' ) {
			while ( *p && ( *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' ) ) p++;
			if ( p[0] == '/' && p[1] == '/' ) {
				while ( *p && *p != '\n' ) p++;
				continue;
			}
			if ( !*p || *p == '}' ) {
				break;
			}
			Rml::String tok;
			if ( *p == '"' ) {
				p++;
				while ( *p && *p != '"' ) tok += *p++;
				if ( *p ) p++;
			} else {
				while ( *p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != '}' ) tok += *p++;
			}
			if ( key.empty() ) {
				key = Rml::StringUtilities::ToLower( tok );
			} else {
				block[key] = tok;
				key.clear();
			}
		}
		if ( *p ) p++;
		out.push_back( block );
	}
	return out;
}

static void ReadInfoFiles( const char *first, const char *ext, std::vector<std::map<Rml::String, Rml::String>> &out ) {
	std::vector<Rml::String> files = { first };
	int n = 0;
	char **list = OzE_ListFiles( "scripts", ext, &n );
	for ( int i = 0; i < n; i++ ) {
		files.push_back( Rml::String( "scripts/" ) + list[i] );
	}
	OzE_FreeFileList( list );
	for ( const Rml::String &f : files ) {
		void *buf = nullptr;
		const int len = OzE_ReadFile( f.c_str(), &buf );
		if ( len <= 0 ) {
			continue;
		}
		const Rml::String text( (const char *)buf, len );
		OzE_FreeFile( buf );
		for ( auto &b : InfoBlocks( text.c_str() ) ) {
			out.push_back( b );
		}
	}
}

static cJSON *Words( const Rml::String &s ) {
	cJSON *a = cJSON_CreateArray();
	Rml::StringList words;
	Rml::StringUtilities::ExpandString( words, s, ' ' );
	for ( const Rml::String &w : words ) {
		if ( !w.empty() ) {
			cJSON_AddItemToArray( a, cJSON_CreateString( w.c_str() ) );
		}
	}
	return a;
}

static void FillFromGame() {
	std::vector<std::map<Rml::String, Rml::String>> blocks;
	cJSON *maps = cJSON_CreateArray(), *bots = cJSON_CreateArray(), *chars = cJSON_CreateArray();
	std::map<Rml::String, bool> seen;

	ReadInfoFiles( "scripts/arenas.txt", ".arena", blocks );
	for ( auto &b : blocks ) {
		Rml::String m = Rml::StringUtilities::ToLower( b["map"] );
		if ( m.empty() || seen[m] ) {
			continue;
		}
		seen[m] = true;
		cJSON *e = cJSON_CreateObject();
		cJSON_AddStringToObject( e, "map", m.c_str() );
		cJSON_AddStringToObject( e, "name", b["longname"].empty() ? m.c_str() : b["longname"].c_str() );
		cJSON_AddItemToObject( e, "types", Words( b["type"].empty() ? "ffa" : b["type"] ) );
		cJSON_AddItemToObject( e, "bots", Words( b["bots"] ) );
		cJSON_AddStringToObject( e, "special", b["special"].c_str() );	// the campaign's training and final (P12)
		cJSON_AddNumberToObject( e, "fraglimit", atoi( b["fraglimit"].c_str() ) );
		cJSON_AddNumberToObject( e, "timelimit", atoi( b["timelimit"].c_str() ) );
		// an arena pack without a level shot gets an empty one, which the pages draw as a dark plate
		const bool shot = OzE_FileExists( ( "levelshots/" + m + ".jpg" ).c_str() ) || OzE_FileExists( ( "levelshots/" + m + ".tga" ).c_str() );
		cJSON_AddStringToObject( e, "shot", shot ? ( "/levelshots/" + m ).c_str() : "" );
		cJSON_AddStringToObject( e, "tile", shot ? ( "/@tile/levelshots/" + m ).c_str() : "" );	// the cards': smaller, after the page shows
		cJSON_AddItemToArray( maps, e );
	}

	blocks.clear();
	ReadInfoFiles( "scripts/bots.txt", ".bot", blocks );
	for ( auto &b : blocks ) {
		if ( b["name"].empty() ) {
			continue;
		}
		Rml::String modelName = b["model"], skin = "default";
		const size_t slash = modelName.find( '/' );
		if ( slash != Rml::String::npos ) {
			skin = modelName.substr( slash + 1 );
			modelName = modelName.substr( 0, slash );
		}
		cJSON *e = cJSON_CreateObject();
		cJSON_AddStringToObject( e, "name", b["name"].c_str() );
		cJSON_AddStringToObject( e, "model", modelName.c_str() );
		cJSON_AddStringToObject( e, "skin", skin.c_str() );
		cJSON_AddStringToObject( e, "icon", ( "/models/players/" + modelName + "/icon_" + skin ).c_str() );
		cJSON_AddItemToArray( bots, e );
	}

	int n = 0;
	char **dirs = OzE_ListFiles( "models/players", "/", &n );
	for ( int i = 0; i < n; i++ ) {
		Rml::String dir = dirs[i];
		if ( !dir.empty() && dir.back() == '/' ) {	// directories come with their slash
			dir.pop_back();
		}
		if ( !dir.empty() && dir[0] != '.' ) {
			void *buf = nullptr;
			if ( OzE_ReadFile( ( "models/players/" + dir + "/icon_default.tga" ).c_str(), &buf ) > 0 ) {
				OzE_FreeFile( buf );
				cJSON_AddItemToArray( chars, cJSON_CreateString( dir.c_str() ) );
			}
		}
	}
	OzE_FreeFileList( dirs );

	Replace( cJSON_GetObjectItem( root, "maps" ), maps );
	Replace( cJSON_GetObjectItem( root, "bots" ), bots );
	Replace( cJSON_GetObjectItem( root, "characters" ), chars );
	Menu_Dirty( "maps" );
	Menu_Dirty( "bots" );
	Menu_Dirty( "characters" );
	Prog::SetArenas( cJSON_GetObjectItem( root, "maps" ) );	// the campaign's tiers, then the pages from the career (P12)
	Prog::Changed();
}

// the player's name, model and the version, each time a menu opens
// the player's name keeps its color codes (pages show it with | q3name or | plain), so the Character tab saves it whole
static void FillPlayer() {
	const char *name = OzE_CvarString( "name" );
	Rml::String modelName = OzE_CvarString( "model" );
	const size_t slash = modelName.find( '/' );
	const Rml::String skin = slash == Rml::String::npos ? "default" : modelName.substr( slash + 1 );
	modelName = modelName.substr( 0, slash );
	Menu_Set( "player.name", name[0] ? name : "Player" );
	Menu_Set( "player.model", modelName.c_str() );
	Menu_Set( "player.icon", ( "/models/players/" + modelName + "/icon_" + skin ).c_str() );
	Menu_Set( "player.version", "dev " __DATE__ );	// the engine's version string names Quake3e; the release numbers come with P14
}

/*
================
Toasts: a line in the upper right for 4.2 s, four at most (Chrome.tsx's Toasts). Four fixed slots,
shown or not: a list that shrank under its views would warn about the rows that went.
================
*/
#define NUM_TOASTS	4

static void MakeToastSlots() {
	cJSON *toasts = Menu_Node( "toasts" );
	while ( toasts && cJSON_GetArraySize( toasts ) < NUM_TOASTS ) {
		cJSON *t = cJSON_CreateObject();
		cJSON_AddStringToObject( t, "text", "" );
		cJSON_AddStringToObject( t, "tone", "info" );
		cJSON_AddNumberToObject( t, "until", 0 );
		cJSON_AddFalseToObject( t, "show" );
		cJSON_AddItemToArray( toasts, t );
	}
}

void Menu_Toast( const char *text, const char *tone ) {
	cJSON *toasts = Menu_Node( "toasts" );
	if ( !toasts ) {
		return;
	}
	// the first free slot, or the one that goes soonest
	int slot = 0;
	double soonest = 1e30;
	for ( int i = 0; i < NUM_TOASTS; i++ ) {
		cJSON *t = cJSON_GetArrayItem( toasts, i );
		const double until = cJSON_GetNumberValue( cJSON_GetObjectItem( t, "until" ) );
		if ( !cJSON_IsTrue( cJSON_GetObjectItem( t, "show" ) ) ) {
			slot = i;
			break;
		}
		if ( until < soonest ) {
			soonest = until;
			slot = i;
		}
	}
	const Rml::String p = Rml::CreateString( "toasts.%d.", slot );
	Menu_Set( ( p + "text" ).c_str(), text );
	Menu_Set( ( p + "tone" ).c_str(), tone );
	Menu_Set( ( p + "until" ).c_str(), Rml::CreateString( "%d", OzE_Milliseconds() + 4200 ).c_str() );
	Menu_Set( ( p + "show" ).c_str(), "true" );
}

static void ExpireToasts() {
	cJSON *toasts = Menu_Node( "toasts" );
	for ( int i = 0; i < cJSON_GetArraySize( toasts ); i++ ) {
		cJSON *t = cJSON_GetArrayItem( toasts, i );
		if ( cJSON_IsTrue( cJSON_GetObjectItem( t, "show" ) ) && cJSON_GetNumberValue( cJSON_GetObjectItem( t, "until" ) ) < OzE_Milliseconds() ) {
			Menu_Set( Rml::CreateString( "toasts.%d.show", i ).c_str(), "false" );
		}
	}
}

/*
================
Actions
================
*/
// the systems that aren't built yet, as a refused action names them
static const char *SoonText( const Rml::String &what ) {
	static const struct { const char *key, *text; } soon[] = {
		{ "online", "Online play comes with the online services (P7, P8)." },
		{ "ranked", "Ranked queues come with matchmaking (P8b)." },
		{ "matchmaking", "Matchmaking comes with P8b." },
		{ "friends", "Friends and parties come with the online services (P7)." },
		{ "replays", "Replays come with the replay system (P10)." },
		{ "editor", "The map editor comes later (P13)." },
		{ "dedicated", "Dedicated servers come with P9." },
		{ "lan", "LAN hosting comes with the LAN browser (P9)." },
		{ "history", "Favorites and history come with the community browser (P8b)." },
		{ "update", "The updater comes with the platform work (P14)." },
		{ "leaderboards", "Leaderboards come with the online services (P7)." },
	};
	for ( const auto &s : soon ) {
		if ( what == s.key ) {
			return s.text;
		}
	}
	return "That isn't built yet.";
}

// the url action: an https link to one of these hosts (the Godot build's URL_HOSTS) opens in the
// player's browser; nothing else leaves the game
static bool OpenLink( const Rml::String &url ) {
	static const char *hosts[] = { "discord.gg", "github.com", "store.steampowered.com", "steamcommunity.com" };

	if ( url.compare( 0, 8, "https://" ) || url.size() >= 512 || url.find_first_of( " \"" ) != Rml::String::npos ) {
		return false;
	}
	const Rml::String host = Rml::StringUtilities::ToLower( url.substr( 8, url.find( '/', 8 ) - 8 ) );
	for ( const char *h : hosts ) {
		if ( host == h ) {
			if ( !OzE_OpenURL( url.c_str() ) ) {
				return false;
			}
			Printf( "UI: opened %s in the browser\n", url.c_str() );
			return true;
		}
	}
	return false;
}

static Rml::Element *FindSpinner( Rml::Element *e ) {
	for ( ; e; e = e->GetParentNode() ) {
		if ( e->IsClassSet( "spinner" ) && e->HasAttribute( "spin" ) ) {
			return e;
		}
	}
	return nullptr;
}

// a spinner's next or previous option, wrapping (SpinControl_Key)
static bool Spin( Rml::Element *spinner, int dir ) {
	const Rml::String path = spinner->GetAttribute<Rml::String>( "spin", "" );
	Rml::StringList options;
	Rml::StringUtilities::ExpandString( options, spinner->GetAttribute<Rml::String>( "options", "" ), '|' );
	if ( options.empty() ) {
		return false;
	}
	const Rml::String cur = Menu_Get( path.c_str() );
	int i = 0;
	for ( size_t k = 0; k < options.size(); k++ ) {
		if ( options[k] == cur ) {
			i = (int)k;
		}
	}
	i = ( i + dir + (int)options.size() ) % (int)options.size();
	Menu_Set( path.c_str(), options[i].c_str() );
	return true;
}

/*
startmatch <path>: an offline match from the object at path, as q3_ui's StartServer starts one:
  gametype (0 FFA, 1 duel, 3 TDM, 4 CTF), map, fraglimit, timelimit, capturelimit, maxplayers,
  skill (1-5), bots (an array of names, or of objects with a name and their own skill and team),
  mod (Skirmish's: "cpma" plays it under CPMA's game, oz_playmod, with CPMA's own mode for the gametype)
The rules and the map now, the bots once the map has loaded. A listing left from hosting goes down.
host <path>: the same match hosted online (P8): its name and password (name, password) as the
server's, and once the map is up, oz_host lists it in the hub's directory (oz_ice.c). A party leader's
party follows them in: the hub's party_follow goes once the listing is up (Social_FollowText).
*/
static void StartMatch( const char *path, bool online = false ) {
	cJSON *m = Menu_Node( path );
	if ( !cJSON_IsObject( m ) || !cJSON_GetStringValue( cJSON_GetObjectItem( m, "map" ) ) ) {
		PlaySound( Sound::Buzz );
		Printf( "^3UI: startmatch %s: no match there\n", path );
		return;
	}
	const char *password = cJSON_GetStringValue( cJSON_GetObjectItem( m, "password" ) );
	if ( online ) {
		const char *name = cJSON_GetStringValue( cJSON_GetObjectItem( m, "name" ) );
		Rml::String plain;	// the player's name without its colors: "Sarge's match"
		for ( const char *s = Menu_Get( "player.name" ); *s; s++ ) {
			if ( s[0] == '^' && s[1] >= '0' && s[1] <= '9' ) {
				s++;
			} else {
				plain += *s;
			}
		}
		OzE_CvarSet( "sv_hostname", name && *name ? name : ( plain + "'s match" ).c_str() );
		OzE_CvarSet( "g_password", password ? password : "" );
	} else {
		OzE_ExecuteNow( "oz_unhost\n" );
		OzE_CvarSet( "g_password", "" );
	}
	auto num = [m]( const char *k, double d ) {
		cJSON *v = cJSON_GetObjectItem( m, k );
		return cJSON_IsNumber( v ) ? v->valuedouble : cJSON_IsString( v ) ? atof( v->valuestring ) : d;
	};
	char cmd[1024];
	// set: before the first map the game hasn't registered some of them (capturelimit)
	snprintf( cmd, sizeof( cmd ), "set g_gametype %d; set fraglimit %d; set timelimit %d; set capturelimit %d; set sv_maxclients %d; set g_spSkill %d; map %s\n",
		(int)num( "gametype", 0 ), (int)num( "fraglimit", 20 ), (int)num( "timelimit", 0 ), (int)num( "capturelimit", 8 ),
		(int)num( "maxplayers", 12 ), (int)num( "skill", 3 ), cJSON_GetStringValue( cJSON_GetObjectItem( m, "map" ) ) );
	// another mod's game: switched to first (the rest runs under it), and back to Oz when the match ends.
	// CPMA starts the mode its mode_start names (its standard ones; its cfg-maps may still change it for a map)
	const char *mod = online ? nullptr : cJSON_GetStringValue( cJSON_GetObjectItem( m, "mod" ) );
	Rml::String text;
	if ( mod && *mod && StrICmp( mod, "oz" ) && StrICmp( mod, OzE_GameDir() ) ) {
		static const char *const cpmaModes[] = { "ffa", "1v1", "ffa", "tdm", "ctf" };
		const int gt = (int)num( "gametype", 0 );
		text = Rml::String( "oz_playmod " ) + mod + "\n";
		if ( !StrICmp( mod, "cpma" ) ) {
			text += Rml::String( "set mode_start " ) + cpmaModes[gt >= 0 && gt < 5 ? gt : 0] + "\n";
		}
	}
	CloseAllPages();
	// in front of the buffer, in order (a test script's queued lines, a vstr loop, would hold them back);
	// oz_host after the map: the server is up when it lists it; the party's follow after the listing (the
	// hub sends it only to a listed match's party)
	text += Rml::String( cmd ) + ( online ? "oz_host\n" + Social_FollowText( "host", "", password ? password : "" ) : "" ) + "wait 30\n";
	cJSON *list = cJSON_GetObjectItem( m, "bots" );
	for ( cJSON *b = list ? list->child : nullptr; b; b = b->next ) {
		const char *name = cJSON_IsString( b ) ? b->valuestring : cJSON_GetStringValue( cJSON_GetObjectItem( b, "name" ) );
		if ( name && *name ) {
			char line[128];
			cJSON *bs = cJSON_IsObject( b ) ? cJSON_GetObjectItem( b, "skill" ) : nullptr;
			const char *team = cJSON_IsObject( b ) ? cJSON_GetStringValue( cJSON_GetObjectItem( b, "team" ) ) : nullptr;
			const int skill = cJSON_IsNumber( bs ) ? bs->valueint : cJSON_IsString( bs ) ? atoi( bs->valuestring ) : (int)num( "skill", 3 );
			snprintf( line, sizeof( line ), "addbot %s %d %s\nwait 5\n", name, skill, team ? team : "" );
			text += line;
		}
	}
	OzE_InsertText( text.c_str() );
}

// {path} in an action is that value of the model's ("cmd map {create_match.map}")
static Rml::String Substitute( const Rml::String &command ) {
	Rml::String out;
	for ( size_t i = 0; i < command.size(); i++ ) {
		const size_t close = command[i] == '{' ? command.find( '}', i ) : Rml::String::npos;
		if ( close == Rml::String::npos ) {
			out += command[i];
			continue;
		}
		out += Menu_Get( command.substr( i + 1, close - i - 1 ).c_str() );
		i = close;
	}
	return out;
}

void Menu_Run( const Rml::String &commands, Rml::Element *element ) {
	Rml::StringList list;
	Rml::StringUtilities::ExpandString( list, commands, ';' );
	for ( const Rml::String &raw : list ) {
		Rml::String command = Substitute( raw );
		const size_t first = command.find_first_not_of( " \t\r\n" );
		command = first == Rml::String::npos ? "" : command.substr( first, command.find_last_not_of( " \t\r\n" ) - first + 1 );
		const size_t sp = command.find( ' ' );
		const Rml::String verb = command.substr( 0, sp );
		const Rml::String arg = sp == Rml::String::npos ? "" : command.substr( sp + 1 );

		if ( verb == "page" ) {
			OpenPageByName( arg.c_str() );
		} else if ( verb == "sub" ) {
			Menu_OpenSub( arg.c_str() );
		} else if ( verb == "back" ) {
			GoBack();
		} else if ( verb == "close" ) {
			CloseAllPages();
		} else if ( verb == "soon" ) {
			PlaySound( Sound::Buzz );
			Menu_Toast( SoonText( arg ), "warn" );
		} else if ( verb == "refuse" ) {	// refuse <text>: the buzz, and why
			PlaySound( Sound::Buzz );
			Menu_Toast( arg.c_str(), "warn" );
		} else if ( verb == "cmd" ) {
			OzE_ExecuteNow( ( arg + "\n" ).c_str() );
		} else if ( verb == "displaykeep" ) {	// display_confirm.rml: the new window settings stay
			Settings_DisplayKeep();
		} else if ( verb == "settingsleave" ) {	// settingsleave apply|discard: apply_confirm.rml, then Settings closes
			Settings_Leave( arg == "apply" );
		} else if ( verb == "pickcolor" ) {		// pickcolor <setting key>: color_picker.rml for it
			Settings_PickColor( arg.c_str() );
		} else if ( verb == "pickerset" ) {		// pickerset [#rrggbb]: the picker's color (else its sliders')
			Settings_PickerSet( arg.c_str() );
		} else if ( verb == "social" ) {
			Menu_Social();
		} else if ( verb == "toast" ) {
			Menu_Toast( arg.c_str(), "info" );
		} else if ( verb == "sound" ) {
			PlaySound( arg == "in" ? Sound::In : arg == "out" ? Sound::Out : arg == "buzz" ? Sound::Buzz : Sound::Move );
		} else if ( verb == "set" ) {
			const size_t s2 = arg.find( ' ' );
			Menu_Set( arg.substr( 0, s2 ).c_str(), s2 == Rml::String::npos ? "" : arg.substr( s2 + 1 ).c_str() );
		} else if ( verb == "spin" && element ) {
			Rml::Element *s = FindSpinner( element );
			if ( s ) {
				Spin( s, arg == "-1" ? -1 : 1 );
			}
		} else if ( verb == "next" ) {
			OzE_ShowcaseNext();
		} else if ( verb == "url" ) {
			if ( !OpenLink( arg ) ) {
				PlaySound( Sound::Buzz );
				Menu_Toast( "That link didn't open.", "warn" );
			}
		} else if ( verb == "copy" ) {	// copy <text>: to the clipboard (the friend code)
			Rml::GetSystemInterface()->SetClipboardText( arg );
		} else if ( verb == "hubinput" ) {
			// hubinput <type> <key> <input id>: the hub message with that input's text, which then empties
			// (Add a friend, the party chat; P7, oz_online_engine.c's "hub" command)
			Rml::StringList a;
			Rml::StringUtilities::ExpandString( a, arg, ' ' );
			Rml::Element *input = a.size() == 3 && element ? element->GetOwnerDocument()->GetElementById( a[2] ) : nullptr;
			const Rml::String text = input ? input->GetAttribute<Rml::String>( "value", "" ) : "";
			if ( text.empty() || text.find( '"' ) != Rml::String::npos ) {
				PlaySound( Sound::Buzz );
				continue;
			}
			OzE_ExecuteNow( ( "hub " + a[0] + " " + a[1] + " \"" + text + "\"\n" ).c_str() );
			input->SetAttribute( "value", "" );
		} else if ( verb == "copycode" ) {	// copycode [prefix]: the settings' share code to the clipboard
			Settings_CopyCode( arg.c_str() );
			Menu_Toast( "Code copied.", "info" );
		} else if ( verb == "pastecode" ) {
			const int n = Settings_PasteCode();
			if ( n < 0 ) {
				PlaySound( Sound::Buzz );
			}
			Menu_Toast( n < 0 ? "That isn't a config code." : Rml::CreateString( "Applied %d settings from the code.", n ).c_str(), n < 0 ? "warn" : "info" );
		} else if ( verb == "focus" ) {	// focus <id> on the top page, once what the actions changed is laid out
			GetContext()->Update();
			if ( Rml::ElementDocument *doc = TopDocument() ) {
				FocusQuietly( doc->GetElementById( arg ) );
			}
		} else if ( verb == "toggle" || verb == "choose" ) {
			// toggle <path> [setting], choose <path> <value> [setting]: the model's value, and the setting with it
			Rml::StringList a;
			Rml::StringUtilities::ExpandString( a, arg, ' ' );
			if ( a.empty() ) {
				continue;
			}
			const Rml::String value = verb == "toggle" ? ( strcmp( Menu_Get( a[0].c_str() ), "true" ) ? "true" : "false" ) : ( a.size() > 1 ? a[1] : "" );
			Menu_Set( a[0].c_str(), value.c_str() );
			const size_t s = verb == "toggle" ? 1 : 2;
			if ( a.size() > s ) {
				Settings_Set( a[s].c_str(), value.c_str() );
			}
		} else if ( verb == "startmatch" ) {
			StartMatch( arg.c_str() );
		} else if ( verb == "host" ) {
			if ( strcmp( Menu_Get( "social.status" ), "online" ) ) {
				PlaySound( Sound::Buzz );
				Menu_Toast( "Hosting online needs the online service, and it isn't connected.", "warn" );
			} else if ( strchr( Menu_Get( ( arg + ".password" ).c_str() ), '"' ) ) {
				PlaySound( Sound::Buzz );	// a joiner's password prompt and the party's follow can't pass one on
				Menu_Toast( "A password can't have quotation marks.", "warn" );
			} else {
				StartMatch( arg.c_str(), true );
			}
		} else if ( verb == "append" ) {
			OzE_AppendText( ( arg + "\n" ).c_str() );
		} else if ( Crosshair_Action( verb, arg ) ) {	// the crosshair editor's (oz_ui_crosshair.cpp)
		} else if ( Profile_Action( verb, arg ) ) {	// the Profile and Campaign pages' (oz_ui_profile.cpp)
		} else if ( Skirmish_Action( verb, arg ) ) {	// Skirmish's mods and races (oz_ui_skirmish.cpp)
		} else if ( Mods_Action( verb, arg ) ) {	// the Mod Library (oz_ui_mods.cpp)
		} else if ( Servers_Action( verb, arg, element ) ) {	// the Server Browser's community servers (oz_ui_servers.cpp)
		} else if ( Social_Action( verb, arg, element ) ) {	// the Server Browser's and Friends' joins (oz_ui_social.cpp)
		} else if ( !verb.empty() ) {
			Printf( "^3UI: no action %s\n", verb.c_str() );
		}
	}
}

class ActionListener : public Rml::EventListener {
public:
	explicit ActionListener( const Rml::String &commands ) : commands( commands ) {}

	void ProcessEvent( Rml::Event &event ) override {
		Menu_Run( commands, event.GetCurrentElement() );
	}

	void OnDetach( Rml::Element * ) override {
		delete this;
	}

private:
	Rml::String commands;
};

class ActionInstancer : public Rml::EventListenerInstancer {
public:
	Rml::EventListener *InstanceEventListener( const Rml::String &value, Rml::Element * ) override {
		return new ActionListener( value );
	}
};

static ActionInstancer actionInstancer;

/*
================
Spinners and tabs, on every menu page (bubbling, so a page's own handlers come first)
================
*/
class MenuListener : public Rml::EventListener {
public:
	void ProcessEvent( Rml::Event &event ) override {
		Rml::Element *target = event.GetTargetElement();
		if ( !target ) {
			return;
		}
		if ( event.GetId() == Rml::EventId::Keydown ) {
			const int key = event.GetParameter<int>( "key_identifier", 0 );
			if ( key != Rml::Input::KI_LEFT && key != Rml::Input::KI_RIGHT ) {
				return;
			}
			if ( Rml::Element *s = FindSpinner( target ) ) {
				if ( Spin( s, key == Rml::Input::KI_LEFT ? -1 : 1 ) ) {
					event.StopPropagation();
				}
				return;
			}
			if ( target->GetOwnerDocument()->GetId() == "menu" && Menu_Side( target, key == Rml::Input::KI_LEFT ? -1 : 1 ) ) {
				event.StopPropagation();
			}
		} else if ( event.GetId() == Rml::EventId::Change ) {
			// a control with setting="<key>" writes that setting as it changes (the crosshair editor's sliders)
			const Rml::String key = target->GetAttribute<Rml::String>( "setting", "" );
			if ( !key.empty() ) {
				Settings_Set( key.c_str(), event.GetParameter<Rml::String>( "value", "" ).c_str() );
			}
		} else if ( event.GetId() == Rml::EventId::Click ) {
			Rml::Element *s = FindSpinner( target );
			if ( s && target != s && ( target->IsClassSet( "prev" ) || target->IsClassSet( "next" ) ) ) {
				Spin( s, target->IsClassSet( "prev" ) ? -1 : 1 );
				PlaySound( Sound::Move );
			} else if ( s ) {	// the box or its value (a click on the value lands on its span)
				Spin( s, 1 );
				if ( target != s ) {
					PlaySound( Sound::Move );	// the page's click sound is the box's own
				}
			}
		}
	}
};

static MenuListener menuListener;

// Q and E, the shoulder buttons: the next tab of the top page's tab bar
bool Menu_Tab( Rml::ElementDocument *doc, int dir ) {
	Rml::ElementList bars;
	doc->GetElementsByClassName( bars, "tabs" );
	for ( Rml::Element *bar : bars ) {
		if ( !bar->IsVisible() ) {
			continue;
		}
		std::vector<Rml::Element *> tabs;
		int active = -1;
		for ( int i = 0; i < bar->GetNumChildren(); i++ ) {
			Rml::Element *t = bar->GetChild( i );
			if ( t->IsClassSet( "tab" ) && t->IsVisible() ) {
				if ( t->IsClassSet( "active" ) ) {
					active = (int)tabs.size();
				}
				tabs.push_back( t );
			}
		}
		if ( tabs.size() < 2 ) {
			continue;
		}
		const int next = ( active + dir + (int)tabs.size() ) % (int)tabs.size();
		tabs[next]->Click();
		FocusQuietly( tabs[next] );
		return true;
	}
	return false;
}

/*
================
The main menu: its submenus (MenuApp.tsx's SubMenu), and the shell's state while pages are over it
================
*/
static Rml::ElementDocument *menuDoc, *pauseDoc;

void Menu_OpenSub( const char *name ) {
	if ( !menuDoc ) {
		return;
	}
	Menu_Set( "state.sub", name );
	GetContext()->Update();
	Rml::Element *first = menuDoc->QuerySelector( Rml::String( "#sub_" ) + name + " button" );
	if ( first ) {
		first->Focus();
	}
	PlaySound( Sound::In );
}

static void CloseSub() {
	const Rml::String sub = Menu_Get( "state.sub" );
	Menu_Set( "state.sub", "" );
	GetContext()->Update();
	if ( Rml::Element *item = menuDoc->GetElementById( "main_" + sub ) ) {
		FocusQuietly( item );
	}
	PlaySound( Sound::Out );
}

// Right on a main item with a submenu opens it; Left in a submenu goes back to the main column
bool Menu_Side( Rml::Element *target, int dir ) {
	const Rml::String id = target->GetId();
	if ( dir > 0 && !id.compare( 0, 5, "main_" ) && !target->GetAttribute<Rml::String>( "submenu", "" ).empty() ) {
		Menu_OpenSub( target->GetAttribute<Rml::String>( "submenu", "" ).c_str() );
		return true;
	}
	if ( dir < 0 && *Menu_Get( "state.sub" ) ) {
		for ( Rml::Element *e = target; e; e = e->GetParentNode() ) {
			if ( !e->GetId().compare( 0, 4, "sub_" ) ) {
				CloseSub();
				return true;
			}
		}
	}
	return false;
}

// Escape, B, the right mouse button: true when the page handled it
bool Menu_Back( const char *page ) {
	if ( !strcmp( page, "server_browser" ) && Servers_Back() ) {	// its server card closes first
		return true;
	}
	if ( !strcmp( page, "mods" ) && Mods_Back() ) {	// the Mod Library's import card or remove question closes first
		return true;
	}
	if ( !strcmp( page, "menu" ) ) {
		if ( *Menu_Get( "state.sub" ) ) {
			CloseSub();
		}
		return true;	// the main menu stays
	}
	if ( !strcmp( page, "splash" ) ) {
		Menu_SplashDone();
		return true;
	}
	if ( !strcmp( page, "error" ) ) {
		OzE_CvarSet( "com_errorMessage", "" );	// read: it doesn't come back (and closes as any page)
		return false;
	}
	if ( !strcmp( page, "display_confirm" ) ) {
		Settings_DisplayRevert();	// unless Keep cleared it: Escape, Revert and the 15 s all go back
		return false;
	}
	if ( !strcmp( page, "settings" ) && Settings_Unapplied() ) {
		return OpenPageByName( "apply_confirm" );	// Video changes waiting for Apply: apply, discard or stay
	}
	if ( !strcmp( page, "connect" ) ) {
		// the connection screen: Escape gives up, as on the game code's (the main menu comes back)
		OzE_ExecuteNow( "disconnect\n" );
		OzE_ExecuteNow( "oz_unjoin\n" );
		return true;
	}
	if ( !strcmp( page, "pause" ) ) {
		if ( *Menu_Get( "state.confirm" ) ) {
			Menu_Set( "state.confirm", "" );
			GetContext()->Update();
			if ( Rml::Element *e = TopDocument()->GetElementById( "pause_leave" ) ) {
				FocusQuietly( e );
			}
			PlaySound( Sound::Out );
			return true;
		}
		CloseAllPages();	// resume
		PlaySound( Sound::Out );
		return true;
	}
	return false;
}

void Menu_SplashDone() {
	CloseAllPages();
	OpenPageByName( "menu" );
}

// the friends panel (Y, F, the top bar's button), over the main menu and its pages
void Menu_Social() {
	const char *top = TopPageName();
	if ( !strcmp( top, "friends" ) ) {
		GoBack();
		return;
	}
	if ( !IsPageOpen( "menu" ) ) {
		PlaySound( Sound::Buzz );
		return;
	}
	OpenPageByName( "friends" );
}

// what a page over the main menu changes in it: the shade's dim, the column's opacity (Shade, MainColumn)
void Menu_Covered( const char *top ) {
	if ( pauseDoc ) {	// a page over the pause menu dims it; the crosshair editor clears it from the screen's middle
		pauseDoc->SetClass( "covered", *top && strcmp( top, "pause" ) );
		pauseDoc->SetClass( "clear", !strcmp( top, "crosshair" ) );
	}
	if ( !menuDoc ) {
		return;
	}
	const bool wide = strcmp( top, "menu" ) && strcmp( top, "quit" ) && strcmp( top, "friends" );
	Menu_Set( "state.wide", wide ? "true" : "false" );
	Menu_Set( "state.quit", !strcmp( top, "quit" ) ? "true" : "false" );
	Menu_Set( "state.social", !strcmp( top, "friends" ) ? "true" : "false" );
}

void Menu_Attach( const char *name, Rml::ElementDocument *doc ) {
	doc->AddEventListener( Rml::EventId::Keydown, &menuListener );
	doc->AddEventListener( Rml::EventId::Click, &menuListener );
	doc->AddEventListener( Rml::EventId::Change, &menuListener );
	if ( !strcmp( name, "menu" ) ) {
		menuDoc = doc;
		doc->SetId( "menu" );
	}
	if ( !strcmp( name, "pause" ) ) {
		pauseDoc = doc;
	}
}

static bool focusFirst;	// the main menu's first item, once its items exist (after the next update)

void Menu_Opened( const char *name ) {
	FillPlayer();
	if ( !strcmp( name, "menu" ) ) {	// the main menu opens on its first item, no submenu
		focusFirst = true;
		Menu_Set( "state.sub", "" );
	}
	if ( !strcmp( name, "pause" ) ) {
		Menu_Set( "state.confirm", "" );
	}
	if ( !strcmp( name, "profile" ) ) {
		Prog::Profile_Fill();
		OzE_ExecuteNow( "hub match_history" );	// the Matches tab, from the online hub (P8b); offline, "hub" says so in the console
	}
	if ( !strcmp( name, "campaign" ) ) {
		Prog::Campaign_Fill();
	}
	Social_Opened( name );
	Servers_Opened( name );
	Skirmish_Opened( name );
	Mods_Opened( name );
	if ( !strcmp( name, "leaderboards" ) ) {	// both scopes from the online hub (P7); offline, "hub" says so in the console
		const Rml::String tab = Menu_Get( "leaderboards.tab" );
		OzE_ExecuteNow( ( "hub leaderboard board " + tab + " scope global" ).c_str() );	// one command a call
		OzE_ExecuteNow( ( "hub leaderboard board " + tab + " scope friends" ).c_str() );
	}
	if ( !strcmp( name, "crosshair" ) ) {
		Crosshair_Opened();
	}
}

/*
================
Each frame while a page is open: the showcase's caption, the toasts
================
*/
// the bottom bar's news line, a new item every 7 s (Chrome.tsx's BottomBar)
static void RotateNews() {
	static int last, index = -1;
	cJSON *news = Menu_Node( "news" );
	const int n = cJSON_GetArraySize( news );
	if ( n <= 0 || ( index >= 0 && OzE_Milliseconds() - last < 7000 ) ) {
		return;
	}
	last = OzE_Milliseconds();
	index = ( index + 1 ) % n;
	cJSON *item = cJSON_GetArrayItem( news, index );
	Menu_Set( "state.news_title", cJSON_GetStringValue( cJSON_GetArrayItem( item, 0 ) ) );
	Menu_Set( "state.news_text", cJSON_GetStringValue( cJSON_GetArrayItem( item, 1 ) ) );
}

// the pause menu's match: its mode and arena, the clock, warmup, the local player's score (the full
// standings come with P6's scoreboard over the bridge)
static void PauseFrame() {
	static int last;
	static const char *const modes[] = { "Free For All", "Duel", "Single Player", "Team Deathmatch", "Capture the Flag" };
	char map[64];
	int gametype, seconds, warmup, score, local;

	if ( !IsPageOpen( "pause" ) || OzE_Milliseconds() - last < 250 ) {
		return;
	}
	last = OzE_Milliseconds();
	if ( !OzE_MatchInfo( map, sizeof( map ), &gametype, &seconds, &warmup, &score, &local ) ) {
		return;
	}
	Rml::String name = map;
	cJSON *maps = Menu_Node( "maps" );
	for ( cJSON *m = maps ? maps->child : nullptr; m; m = m->next ) {
		const char *id = cJSON_GetStringValue( cJSON_GetObjectItem( m, "map" ) );
		if ( id && !StrICmp( id, map ) ) {
			name = cJSON_GetStringValue( cJSON_GetObjectItem( m, "name" ) );
		}
	}
	char clock[32];
	seconds = seconds < 0 ? 0 : seconds;
	snprintf( clock, sizeof( clock ), "%d:%02d", seconds / 60, seconds % 60 );
	Menu_Set( "pause.title", ( Rml::String( gametype >= 0 && gametype < 5 ? modes[gametype] : "Match" ) + "  -  " + name ).c_str() );
	Menu_Set( "pause.clock", clock );
	Menu_Set( "pause.warmup", warmup ? "true" : "false" );
	Menu_Set( "pause.can_restart", local ? "true" : "false" );
	Menu_Set( "pause.editor", local && !strncmp( map, "ed_", 3 ) ? "true" : "false" );	// a map editor's play-test (P13)
	Menu_Set( "pause.standings.0.name", Menu_Get( "player.name" ) );
	Menu_Set( "pause.standings.0.score", Rml::CreateString( "%d", score ).c_str() );
}

void Menu_Frame() {
	RotateNews();
	PauseFrame();
	Social_Frame();
	Servers_Frame();
	Skirmish_Frame();
	Crosshair_Frame();
	if ( focusFirst && menuDoc && !strcmp( TopPageName(), "menu" ) ) {
		Rml::Element *f = GetContext()->GetFocusElement();
		Rml::Element *first = menuDoc->GetElementById( "main_play" );
		if ( first && ( !f || f->GetOwnerDocument() != menuDoc || f == menuDoc ) ) {
			FocusQuietly( first );
		}
		focusFirst = !first;
	}
	const char *kind = "";
	const char *name = OzE_ShowcaseSubject( &kind );
	if ( name && strcmp( name, Menu_Get( "subject.name" ) ) ) {
		Menu_Set( "subject.name", name );
		Menu_Set( "subject.kind", kind );
	}
	ExpireToasts();
}

// the rim light's color: the player's accent (the profile's team color), q3_ui's red by default
void Menu_Accent( float rgb[3] ) {
	unsigned c = 0xdb2410;
	const char *hex = Menu_Get( "player.accent" );
	if ( hex[0] == '#' ) {
		c = (unsigned)strtoul( hex + 1, nullptr, 16 );
	}
	rgb[0] = ( ( c >> 16 ) & 255 ) / 255.0f;
	rgb[1] = ( ( c >> 8 ) & 255 ) / 255.0f;
	rgb[2] = ( c & 255 ) / 255.0f;
}

/*
================
<banner text="...">: id's red banner font (menu/art/font2_prop from the player's pak0), capitals only,
as UI_DrawBannerString draws it with q3_ui's propMapB (ui_atoms.c). The element's font-size is the
glyphs' cell height; each letter is an <img> of its rect.
================
*/
static const int propMapB[26][3] = {
	{ 11, 12, 33 }, { 49, 12, 31 }, { 85, 12, 31 }, { 120, 12, 30 }, { 156, 12, 21 }, { 183, 12, 21 }, { 207, 12, 32 },
	{ 13, 55, 30 }, { 49, 55, 13 }, { 66, 55, 29 }, { 101, 55, 31 }, { 135, 55, 21 }, { 158, 55, 40 }, { 204, 55, 32 },
	{ 12, 97, 31 }, { 48, 97, 31 }, { 82, 97, 30 }, { 118, 97, 30 }, { 153, 97, 30 }, { 185, 97, 25 }, { 213, 97, 30 },
	{ 11, 139, 32 }, { 42, 139, 51 }, { 93, 139, 32 }, { 126, 139, 31 }, { 158, 139, 25 },
};
#define PROPB_GAP_WIDTH		4
#define PROPB_SPACE_WIDTH	12
#define PROPB_HEIGHT		36

class ElementBanner : public Rml::Element {
public:
	explicit ElementBanner( const Rml::String &tag ) : Rml::Element( tag ) {}

protected:
	void OnAttributeChange( const Rml::ElementAttributes &changed ) override {
		Rml::Element::OnAttributeChange( changed );
		if ( changed.count( "text" ) ) {
			Build( GetAttribute<Rml::String>( "text", "" ) );
		}
	}

private:
	void Build( const Rml::String &text ) {
		while ( GetNumChildren() > 0 ) {
			RemoveChild( GetChild( 0 ) );
		}
		const float em = 1.0f / PROPB_HEIGHT;
		char style[160], rect[64];
		float pending = 0.0f;	// a space's width, added to the next letter's margin
		bool first = true;
		for ( const char ch : text ) {
			const int c = toupper( (unsigned char)ch );
			if ( c < 'A' || c > 'Z' ) {
				pending += PROPB_SPACE_WIDTH * em;
				continue;
			}
			const int *g = propMapB[c - 'A'];
			Rml::XMLAttributes attributes;
			snprintf( rect, sizeof( rect ), "%d %d %d %d", g[0], g[1], g[2], PROPB_HEIGHT );
			snprintf( style, sizeof( style ), "width: %.4fem; height: 1em; margin-left: %.4fem;", g[2] * em, first ? pending : pending + PROPB_GAP_WIDTH * em );
			attributes["src"] = Rml::String( "/menu/art/font2_prop" );
			attributes["rect"] = Rml::String( rect );
			attributes["style"] = Rml::String( style );
			Rml::ElementPtr img = Rml::Factory::InstanceElement( this, "img", "img", attributes );
			if ( img ) {
				AppendChild( std::move( img ) );
			}
			pending = 0.0f;
			first = false;
		}
	}
};

static Rml::ElementInstancerGeneric<ElementBanner> bannerInstancer;

/*
================
Setup
================
*/
void Menu_Init() {
	void *buf = nullptr;
	const int len = OzE_ReadFile( "ui/data/menu.json", &buf );
	if ( len > 0 ) {
		root = cJSON_ParseWithLength( (const char *)buf, len );
		OzE_FreeFile( buf );
	}
	if ( !root ) {
		Printf( "^3UI: ui/data/menu.json is missing or isn't JSON\n" );
		root = cJSON_CreateObject();
	}
	// each page's own data, ui/data/<name>.json for each name in "include", merged in by top-level key
	cJSON *include = cJSON_DetachItemFromObject( root, "include" );
	for ( cJSON *name = include ? include->child : nullptr; name; name = name->next ) {
		const Rml::String path = Rml::String( "ui/data/" ) + cJSON_GetStringValue( name ) + ".json";
		void *data = nullptr;
		const int n = OzE_ReadFile( path.c_str(), &data );
		if ( n <= 0 ) {
			continue;
		}
		cJSON *part = cJSON_ParseWithLength( (const char *)data, n );
		OzE_FreeFile( data );
		if ( !part ) {
			Printf( "^3UI: %s isn't JSON\n", path.c_str() );
			continue;
		}
		cJSON *k;
		while ( ( k = part->child ) != nullptr ) {
			cJSON_DetachItemViaPointer( part, k );
			if ( k->string[0] == '_' ) {	// a file's notes
				cJSON_Delete( k );
				continue;
			}
			if ( cJSON_GetObjectItemCaseSensitive( root, k->string ) ) {
				Printf( "^3UI: %s: %s is in another data file too\n", path.c_str(), k->string );
				cJSON_Delete( k );
				continue;
			}
			cJSON_AddItemToObject( root, k->string, k );	// cJSON copies the key before freeing the old one
		}
		cJSON_Delete( part );
	}
	cJSON_Delete( include );
	// the keys C++ fills in, so the pages always find them
	for ( const char *k : { "state", "player", "subject" } ) {
		if ( !cJSON_GetObjectItem( root, k ) ) {
			cJSON_AddItemToObject( root, k, cJSON_CreateObject() );
		}
	}
	for ( const char *k : { "maps", "bots", "characters", "toasts" } ) {
		if ( !cJSON_GetObjectItem( root, k ) ) {
			cJSON_AddItemToObject( root, k, cJSON_CreateArray() );
		}
	}
	for ( const char *k : { "name", "model", "icon", "version", "accent" } ) {
		if ( !cJSON_GetObjectItem( Menu_Node( "player" ), k ) ) {
			cJSON_AddStringToObject( Menu_Node( "player" ), k, "" );
		}
	}
	for ( const char *k : { "name", "kind" } ) {
		if ( !cJSON_GetObjectItem( Menu_Node( "subject" ), k ) ) {
			cJSON_AddStringToObject( Menu_Node( "subject" ), k, "" );
		}
	}
	for ( const char *k : { "sub", "confirm", "news_title", "news_text" } ) {
		if ( !cJSON_GetObjectItem( Menu_Node( "state" ), k ) ) {
			cJSON_AddStringToObject( Menu_Node( "state" ), k, "" );
		}
	}
	for ( const char *k : { "wide", "quit", "social" } ) {
		if ( !cJSON_GetObjectItem( Menu_Node( "state" ), k ) ) {
			cJSON_AddFalseToObject( Menu_Node( "state" ), k );
		}
	}
	MakeToastSlots();
	Rml::Factory::RegisterEventListenerInstancer( &actionInstancer );
	Rml::Factory::RegisterElementInstancer( "banner", &bannerInstancer );
	Crosshair_Init();
}

void Menu_BindModel( Rml::Context *context ) {
	Rml::DataModelConstructor c = context->CreateDataModel( "menu" );
	if ( !c ) {
		return;
	}
	// a player's name with its color codes as RML (data-rml="f.name | q3name"), or without them (| plain).
	// Black letters would vanish on the dark pages: a name with any goes on a soft grey panel (Ozmium, 2026-10-31)
	c.RegisterTransformFunc( "q3name", []( const Rml::VariantList &args ) -> Rml::Variant {
		static const char *const colors[8] = { "#000000", "#ff4a1c", "#5ccf4a", "#ffd23c", "#4a7dff", "#3cd2ff", "#ff4aff", "#fff3e6" };
		const Rml::String in = args.empty() ? "" : args[0].Get<Rml::String>();
		Rml::String out, run;
		const char *color = nullptr;
		bool black = false;
		auto flush = [&]() {
			if ( run.empty() ) {
				return;
			}
			black = black || ( color == colors[0] && run.find_first_not_of( ' ' ) != Rml::String::npos );
			const Rml::String text = Rml::StringUtilities::EncodeRml( run );
			out += color ? Rml::String( "<span style=\"color: " ) + color + ";\">" + text + "</span>" : text;
			run.clear();
		};
		for ( size_t i = 0; i < in.size(); i++ ) {
			if ( in[i] == '^' && i + 1 < in.size() && in[i + 1] >= '0' && in[i + 1] <= '9' ) {
				flush();
				const int n = in[++i] - '0';
				color = n == 7 ? nullptr : colors[n & 7];
				continue;
			}
			run += in[i];
		}
		flush();
		if ( black ) {
			out = "<span style=\"background-color: #9e9e9e80; padding: 0 0.2em;\">" + out + "</span>";
		}
		return Rml::Variant( out );
	} );
	// a | contains(b): b is in a, ignoring case (a search box filtering a list: data-if="c.name | contains(maps.query)")
	c.RegisterTransformFunc( "contains", []( const Rml::VariantList &args ) -> Rml::Variant {
		Rml::String hay = args.size() > 0 ? Rml::StringUtilities::ToLower( args[0].Get<Rml::String>() ) : "";
		Rml::String needle = args.size() > 1 ? Rml::StringUtilities::ToLower( args[1].Get<Rml::String>() ) : "";
		return Rml::Variant( needle.empty() || hay.find( needle ) != Rml::String::npos );
	} );
	c.RegisterTransformFunc( "plain", []( const Rml::VariantList &args ) -> Rml::Variant {
		const Rml::String in = args.empty() ? "" : args[0].Get<Rml::String>();
		Rml::String out;
		for ( size_t i = 0; i < in.size(); i++ ) {
			if ( in[i] == '^' && i + 1 < in.size() && in[i + 1] >= '0' && in[i + 1] <= '9' ) {
				i++;
				continue;
			}
			out += in[i];
		}
		return Rml::Variant( out );
	} );
	boundKeys.clear();
	for ( cJSON *k = root->child; k; k = k->next ) {
		if ( k->string[0] == '_' ) {	// notes
			continue;
		}
		c.BindCustomDataVariable( k->string, Var( k ) );
		boundKeys.push_back( k->string );
	}
	model = c.GetModelHandle();
	FillFromGame();
	FillPlayer();
}

void Menu_Shutdown() {
	model = Rml::DataModelHandle();
	menuDoc = pauseDoc = nullptr;
	if ( root ) {
		cJSON_Delete( root );
		root = nullptr;
	}
}

// ui_menu_get <path> / ui_menu_set <path> <value>, for tests
void Menu_Commands() {
	Prog::Commands();
	Skirmish_Commands();
	Mods_Commands();
	OzE_AddCommand( "ui_menu_get", []() {
		Printf( "ui_menu_get: %s = %s\n", OzE_Argv( 1 ), Menu_Get( OzE_Argv( 1 ) ) );
	} );
	OzE_AddCommand( "ui_menu_set", []() {
		if ( !Menu_Set( OzE_Argv( 1 ), OzE_ArgsFrom( 2 ) ) ) {
			Printf( "ui_menu_set: no value %s\n", OzE_Argv( 1 ) );
		}
	} );
	OzE_AddCommand( "ui_run", []() {	// an action, as a page's on<event> runs it
		Menu_Run( OzE_ArgsFrom( 1 ), nullptr );
	} );
	OzE_AddCommand( "ui_presskey", []() {	// a key through the engine's own input path (ui_key goes to the open page only)
		const int key = OzE_KeyNum( OzE_Argv( 1 ) );
		if ( key >= 0 ) {
			OzE_QueueKey( key );
		}
	} );
	OzE_AddCommand( "ui_fileinfo", []() {	// a file as the UI reads it (the UI pak under any mod, on pure servers)
		void *buf = nullptr;
		const int len = OzE_ReadFile( OzE_Argv( 1 ), &buf );
		if ( len >= 0 ) {
			OzE_FreeFile( buf );
		}
		Printf( "ui_fileinfo: %s %d\n", OzE_Argv( 1 ), len );
	} );
}

} // namespace OzUI
