/*
Oz: the multiplayer map editor's session screen (ENGINE_PLAN.md PE step 4, ui/pages/together.rml).

The main menu's Map Editor, Multiplayer opens it (its submenu's other item, Single Player, is the map editor alone), apart from
the map editor alone (Ozmium, 2026-11-07):
  Host   a session on a new map, one of your maps or a game map, its name, a password, up to 8 players;
         listed on the online service (a listing of mode "edit"), so friends and the session list find it
  Join   the sessions open now (the hub's listings of mode "edit"), your friends' (Friends' Join), or a host's
         player id typed in (the session's code); ozarena://edit/<host> links come here too
Hosting or joining opens the map editor's page on the multiplayer editor's own session (oz::ed::together_session),
its map, camera and autosave apart from the map editor alone's; the session runs in oz::ed::Together.

Commands:
  editor_together					the session screen (or the session's editor, while one runs)
  edt_host new|open <file.map>|import <map> [name] [password] [max]	hosts, and opens the editor
  edt_join <host's player id> [password]	asks to join, and opens the editor
  edt_leave							leaves the session (a host ends it), back to the session screen
  edt_back							the session's editor again (from a play-test)
  edt_kick <player>, edt_role <player> editor|viewer, edt_follow <player>|off
  edt_browse						asks the hub for the sessions
  edt_stats							the session, its players and what's on its way (tests)
*/
#include "oz_ui_internal.h"
#include "../editor/ed_engine.h"
#include "../editor/ed_net.h"
#include "../editor/ed_mapfile.h"
#include "../editor/ed_together.h"
#include "cJSON.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace OzUI {
cJSON *Menu_Node( const char *path );	// oz_ui_menu.cpp
void Menu_Dirty( const char *path );
}
using namespace OzUI;

namespace {

using oz::ed::Together;
using oz::ed::together;
using oz::ed::together_session;

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

const char *Str( const cJSON *o, const char *k, const char *fallback = "" ) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive( o, k );
	return cJSON_IsString( v ) ? v->valuestring : fallback;
}

double Num( const cJSON *o, const char *k ) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive( o, k );
	return cJSON_IsNumber( v ) ? v->valuedouble : 0;
}

bool Bool( const cJSON *o, const char *k ) {
	return cJSON_IsTrue( cJSON_GetObjectItemCaseSensitive( o, k ) );
}

void Replace( const char *path, cJSON *src ) {
	cJSON *dst = Menu_Node( path );
	if ( !dst ) {
		cJSON_Delete( src );
		return;
	}
	cJSON *c;
	while ( ( c = dst->child ) != nullptr ) {
		cJSON_Delete( cJSON_DetachItemViaPointer( dst, c ) );
	}
	while ( ( c = src->child ) != nullptr ) {
		cJSON_AddItemToArray( dst, cJSON_DetachItemViaPointer( src, c ) );
	}
	cJSON_Delete( src );
	Menu_Dirty( path );
}

void SetString( const char *path, const std::string &v ) {
	cJSON *j = Menu_Node( path );
	if ( j && cJSON_IsString( j ) && v != j->valuestring ) {
		cJSON_SetValuestring( j, v.c_str() );
		Menu_Dirty( path );
	} else if ( j && !cJSON_IsString( j ) ) {
		Menu_Set( path, v.c_str() );
	}
}

std::string MapsDir() {
	return std::string( OzEd_HomePath() ) + "/oz/editor/maps";
}

// the editor's maps folder, by name
void FillMine() {
	int n = 0;
	char **list = OzEd_ListOSFiles( MapsDir().c_str(), ".map", &n );
	std::vector<std::string> names;
	for ( int i = 0; i < n; i++ ) {
		names.push_back( list[i] );
	}
	OzEd_FreeList( list );
	std::sort( names.begin(), names.end() );
	cJSON *out = cJSON_CreateArray();
	for ( const std::string &f : names ) {
		cJSON *o = cJSON_CreateObject();
		cJSON_AddStringToObject( o, "file", f.c_str() );
		cJSON_AddStringToObject( o, "name", f.size() > 4 ? f.substr( 0, f.size() - 4 ).c_str() : f.c_str() );
		cJSON_AddItemToArray( out, o );
	}
	Replace( "together.mine", out );
	Menu_Set( "together.mine_count", std::to_string( names.size() ).c_str() );
}

// opens the editor's page on the session's map
void OpenEditor() {
	oz::ed::use_session( true );
	CloseAllPages();
	OpenPageByName( "editor" );
}

void OpenScreen() {
	CloseAllPages();
	OpenPageByName( "menu" );
	OpenPageByName( "together" );
}

bool Host( const std::string &how, const std::string &what, const std::string &name, const std::string &password, int max ) {
	oz::ed::Session &s = together_session();
	std::string err;
	if ( together().active() ) {
		together().leave();
	}
	if ( !OzEdNet_Online() ) {
		Menu_Toast( "Hosting needs the online service, and it isn't connected.", "warn" );
		return false;
	}
	if ( how == "new" ) {
		s.new_map();
	} else if ( how == "open" ) {
		std::string file = what;
		if ( file.find( '/' ) == std::string::npos && file.find( '\\' ) == std::string::npos ) {
			file = MapsDir() + "/" + file;
		}
		if ( !s.open( file, err ) ) {
			Menu_Toast( err.c_str(), "error" );
			return false;
		}
	} else if ( how == "import" ) {
		if ( !s.import_bsp( what, err, true ) ) {
			Menu_Toast( err.c_str(), "error" );
			return false;
		}
	} else if ( how != "current" ) {
		Printf( "^3edt_host: new, open <file.map>, import <map> or current\n" );
		return false;
	}
	if ( !together().host( name, password, max, err ) ) {
		Menu_Toast( err.c_str(), "error" );
		return false;
	}
	OpenEditor();
	return true;
}

// ---- commands

void EditorTogether_f() {
	if ( together().active() ) {
		OpenEditor();
	} else {
		OpenScreen();
	}
}

void Host_f() {
	if ( OzE_Argc() < 2 ) {
		Printf( "usage: edt_host new|open <file.map>|import <map>|current [name] [password] [max players]\n" );
		return;
	}
	const std::string how = OzE_Argv( 1 );
	int at = 2;
	std::string what;
	if ( how == "open" || how == "import" ) {
		what = OzE_Argv( at++ );
	}
	const std::string name = OzE_Argv( at );
	const std::string password = OzE_Argv( at + 1 );
	const int max = OzE_Argc() > at + 2 ? atoi( OzE_Argv( at + 2 ) ) : 8;
	Host( how, what, name, password, max );
}

void Join_f() {
	if ( OzE_Argc() < 2 ) {
		Printf( "usage: edt_join <host's player id> [password]\n" );
		return;
	}
	together().join( OzE_Argv( 1 ), OzE_Argv( 2 ) );
	OpenEditor();
}

void Leave_f() {
	together().leave();
	if ( IsPageOpen( "editor" ) && oz::ed::using_together() ) {
		OpenScreen();
	}
}

void Back_f() {
	OzE_InsertText( "disconnect\neditor_together\n" );	// from a play-test: the session's editor, the session as it went on
}

void Browse_f() {
	Menu_Set( "together.answered", "false" );
	OzEdNet_Browse();
}

uint32_t PlayerArg( const char *a ) {
	return uint32_t( atoi( a ) );
}

void Kick_f() {
	together().kick( PlayerArg( OzE_Argv( 1 ) ) );
}

void Role_f() {
	const std::string r = OzE_Argv( 2 );
	together().set_role( PlayerArg( OzE_Argv( 1 ) ), r == "viewer" ? Together::Role::Viewer : Together::Role::Editor );
}

void Follow_f() {
	const std::string a = OzE_Argv( 1 );
	together().follow = a == "off" || a.empty() ? 0 : PlayerArg( a.c_str() );
	if ( together().follow ) {
		together_session().notify( "Following " + together().name_of( together().follow ) + ": move to stop", false );
	}
}

void Stats_f() {
	Printf( "%s\n", together().stats().c_str() );
	Printf( "together: doc revision %llu, %s\n", (unsigned long long)together_session().doc.revision, together().status().c_str() );
}

// edt_test <what> ...: edits made the way the tools make them, for the tests (no pointer needed)
//   brush <x> <y> <z> [size]   a box brush in the world        move <id> <dx> <dy> <dz>   moved, texture lock on
//   key <id> <key> <value>      an entity key                   light <x> <y> <z>           a light placed
//   delete <id>                 deleted                         undo, redo
//   random <count> <seed>       random edits of those kinds     hash                        the map's file's hash
void Test_f() {
	oz::ed::Session &s = together_session();
	oz::ed::Doc &d = s.doc;
	const std::string what = OzE_Argv( 1 );
	auto num = []( int i ) { return atof( OzE_Argv( i ) ); };
	auto brush = [&]( double x, double y, double z, double size ) {
		d.begin( "Create brush" );
		const oz::ed::Id id = d.add( oz::ed::box_brush( d, oz::ed::Vec3( x, y, z ), oz::ed::Vec3( x + size, y + size, z + size ),
			oz::ed::shader_id( "base_wall/concrete" ), d.worldspawn() ) );
		d.end();
		return id;
	};
	if ( what == "brush" ) {
		const oz::ed::Id id = brush( num( 2 ), num( 3 ), num( 4 ), OzE_Argc() > 5 ? num( 5 ) : 64 );
		Printf( "edt_test: brush %d\n", id );
	} else if ( what == "move" ) {
		oz::ed::transform( d, { oz::ed::Id( atoi( OzE_Argv( 2 ) ) ) }, oz::ed::translation( oz::ed::Vec3( num( 3 ), num( 4 ), num( 5 ) ) ), true, "Move" );
	} else if ( what == "key" ) {
		oz::ed::set_key( d, { oz::ed::Id( atoi( OzE_Argv( 2 ) ) ) }, OzE_Argv( 3 ), OzE_Argv( 4 ), "Set key" );
	} else if ( what == "light" ) {
		d.begin( "Place light" );
		auto l = std::make_shared<oz::ed::Object>();
		l->kind = oz::ed::Kind::Entity;
		l->entity.set( "classname", "light" );
		l->entity.set( "origin", std::string( OzE_Argv( 2 ) ) + " " + OzE_Argv( 3 ) + " " + OzE_Argv( 4 ) );
		const oz::ed::Id id = d.add( l );
		d.end();
		Printf( "edt_test: light %d\n", id );
	} else if ( what == "delete" ) {
		oz::ed::remove( d, { oz::ed::Id( atoi( OzE_Argv( 2 ) ) ) }, "Delete" );
	} else if ( what == "undo" || what == "redo" ) {
		s.run( what, {} );
	} else if ( what == "random" ) {
		unsigned seed = unsigned( atoi( OzE_Argv( 3 ) ) ) * 2654435761u + 1;
		auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return ( seed >> 8 ) & 0xffffff; };
		const int n = atoi( OzE_Argv( 2 ) );
		for ( int i = 0; i < n; i++ ) {
			std::vector<oz::ed::Id> brushes = d.ids_of( oz::ed::Kind::Brush ), ents = d.ids_of( oz::ed::Kind::Entity );
			ents.erase( std::remove( ents.begin(), ents.end(), d.worldspawn() ), ents.end() );
			switch ( rnd() % 6 ) {
			case 0: case 1:
				if ( !brushes.empty() ) {
					oz::ed::transform( d, { brushes[rnd() % brushes.size()] }, oz::ed::translation( oz::ed::Vec3( double( int( rnd() % 9 ) - 4 ) * 8, double( int( rnd() % 9 ) - 4 ) * 8, 0 ) ), true, "Move" );
				}
				break;
			case 2:
				brush( double( int( rnd() % 32 ) - 16 ) * 64, double( int( rnd() % 32 ) - 16 ) * 64, 0, 64 );
				break;
			case 3:
				if ( !ents.empty() ) {
					static const char *keys[] = { "light", "_color", "angle", "target" };
					oz::ed::set_key( d, { ents[rnd() % ents.size()] }, keys[rnd() % 4], std::to_string( rnd() % 1000 ), "Set key" );
				}
				break;
			case 4:
				if ( brushes.size() > 4 ) {
					oz::ed::remove( d, { brushes[rnd() % brushes.size()] }, "Delete" );
				}
				break;
			case 5:
				s.run( rnd() % 2 ? "undo" : "redo", {} );
				break;
			}
		}
		Printf( "edt_test: %d random edits\n", n );
	} else if ( what == "hash" ) {
		const std::string text = oz::ed::write_map( d );
		unsigned h = 2166136261u;
		for ( char c : text ) {
			h = ( h ^ (unsigned char)c ) * 16777619u;
		}
		Printf( "edt_test: hash %08x, %zu objects, %zu bytes, waiting %zu\n", h, d.size(), text.size(), together().waiting() );
	} else {
		Printf( "usage: edt_test brush|move|key|light|delete|undo|redo|random|hash ...\n" );
	}
}

// edt_play: Play together (the Build menu's, Ctrl+Shift+F5)
void PlayTogether_f() {
	std::string why;
	if ( !together().play_together( why ) ) {
		Printf( "^3together: %s\n", why.c_str() );
	}
}

// edt_reload: the play-test together reloaded with the session's map as it is now (the pause menu's Reload)
void Reload_f() {
	together().reload();
}

std::string lastPlayers;
int lastRev = -1;

}  // namespace

namespace OzUI {

void Together_Commands() {
	OzE_AddCommand( "editor_together", EditorTogether_f );
	OzE_AddCommand( "edt_host", Host_f );
	OzE_AddCommand( "edt_join", Join_f );
	OzE_AddCommand( "edt_leave", Leave_f );
	OzE_AddCommand( "edt_back", Back_f );
	OzE_AddCommand( "edt_browse", Browse_f );
	OzE_AddCommand( "edt_kick", Kick_f );
	OzE_AddCommand( "edt_role", Role_f );
	OzE_AddCommand( "edt_follow", Follow_f );
	OzE_AddCommand( "edt_stats", Stats_f );
	OzE_AddCommand( "edt_test", Test_f );
	OzE_AddCommand( "edt_play", PlayTogether_f );
	OzE_AddCommand( "edt_reload", Reload_f );
}

void Together_Opened( const char *page ) {
	if ( strcmp( page, "together" ) ) {
		return;
	}
	FillMine();
	Menu_Set( "together.online", OzEdNet_Online() ? "true" : "false" );
	if ( !strlen( Menu_Get( "together.name" ) ) ) {
		SetString( "together.name", Plain( OzEdNet_Name() ) + "'s map" );
	}
	SetString( "together.why", together().state == Together::State::Ended ? together().why : "" );
	Browse_f();
}

// the session screen's controls
bool Together_Action( const Rml::String &verb, const Rml::String &arg ) {
	if ( verb == "edthost" ) {
		const std::string source = Menu_Get( "together.source" );
		const std::string name = Menu_Get( "together.name" ), password = Menu_Get( "together.password" );
		const int max = atoi( Menu_Get( "together.max" ) );
		if ( password.find( '"' ) != std::string::npos || name.find( '"' ) != std::string::npos ) {
			Menu_Toast( "A session's name and password can't have quotation marks.", "warn" );
			return true;
		}
		if ( source == "mine" ) {
			const int i = atoi( Menu_Get( "together.mi" ) );
			const char *file = Menu_Get( ( "together.mine." + std::to_string( i ) + ".file" ).c_str() );
			if ( !file[0] ) {
				Menu_Toast( "Pick one of your maps first (there are none in your editor's maps folder yet).", "warn" );
				return true;
			}
			Host( "open", file, name, password, max );
		} else if ( source == "game" ) {
			const std::string map = Menu_Get( "together.game" );
			if ( map.empty() ) {
				Menu_Toast( "Type the game map's name (q3dm17, or a map pack's).", "warn" );
				return true;
			}
			Host( "import", map, name, password, max );
		} else {
			Host( "new", "", name, password, max );
		}
		return true;
	}
	if ( verb == "edtjoin" ) {
		// edtjoin <host> [locked]: a locked one takes the password typed on the page
		Rml::StringList a;
		Rml::StringUtilities::ExpandString( a, arg, ' ' );
		if ( a.empty() ) {
			return true;
		}
		const bool locked = a.size() > 1 && a[1] == "locked";
		const std::string pw = locked ? Menu_Get( "together.join_password" ) : "";
		if ( locked && pw.empty() ) {
			Menu_Set( "together.ask", a[0].c_str() );
			Menu_Toast( "That session takes a password: type it under the list, then Join.", "info" );
			return true;
		}
		together().join( a[0], pw );
		OpenEditor();
		return true;
	}
	if ( verb == "edtcode" ) {
		std::string code = Menu_Get( "together.code" ), id;
		for ( char c : code ) {
			if ( isxdigit( (unsigned char)c ) ) {
				id += char( tolower( (unsigned char)c ) );
			}
		}
		if ( id.size() < 8 ) {
			Menu_Toast( "A session's code is its host's player id: 20 letters and digits (the host's session bar shows it).", "warn" );
			return true;
		}
		together().join( id, Menu_Get( "together.join_password" ) );
		OpenEditor();
		return true;
	}
	if ( verb == "edtbrowse" ) {
		Browse_f();
		return true;
	}
	if ( verb == "edtleave" ) {
		Leave_f();
		return true;
	}
	if ( verb == "edtkeep" ) {
		// the ended session's card closed: the map stays open in the multiplayer editor, alone, to save
		if ( together().state == Together::State::Ended ) {
			together().state = Together::State::Off;
			together().changed_rev++;
		}
		return true;
	}
	if ( verb == "edtfollow" ) {
		together().follow = together().follow == PlayerArg( arg.c_str() ) ? 0 : PlayerArg( arg.c_str() );
		return true;
	}
	if ( verb == "edtkick" ) {
		together().kick( PlayerArg( arg.c_str() ) );
		return true;
	}
	if ( verb == "edtrole" ) {
		const uint32_t n = PlayerArg( arg.c_str() );
		const Together::Player *p = together().player( n );
		if ( p ) {
			together().set_role( n, p->role == Together::Role::Viewer ? Together::Role::Editor : Together::Role::Viewer );
		}
		return true;
	}
	if ( verb == "edtcopycode" ) {
		Rml::GetSystemInterface()->SetClipboardText( together().host_pid );
		Menu_Toast( "The session's code is copied: a friend types it under Join.", "info" );
		return true;
	}
	if ( verb == "edtcopylink" ) {
		Rml::GetSystemInterface()->SetClipboardText( "ozarena://edit/" + together().host_pid );
		Menu_Toast( "The session's link is copied.", "info" );
		return true;
	}
	return false;
}

// the hub's listings (net_matches): the editing sessions among them
void Together_HubMessage( const char *type, const cJSON *m ) {
	if ( strcmp( type, "net_matches" ) ) {
		return;
	}
	const std::string me = OzEdNet_Me();
	cJSON *out = cJSON_CreateArray();
	const cJSON *r;
	int n = 0;
	cJSON_ArrayForEach( r, cJSON_GetObjectItemCaseSensitive( m, "matches" ) ) {
		if ( strcmp( Str( r, "mode" ), "edit" ) ) {
			continue;
		}
		const int players = (int)Num( r, "players" ), max = (int)Num( r, "max" );
		const std::string version = Str( r, "version" );
		cJSON *o = cJSON_CreateObject();
		cJSON_AddStringToObject( o, "host", Str( r, "host" ) );
		cJSON_AddStringToObject( o, "name", Plain( Str( r, "name", "Session" ) ).c_str() );
		cJSON_AddStringToObject( o, "owner", Plain( Str( r, "owner" ) ).c_str() );
		cJSON_AddStringToObject( o, "map", Str( r, "map" ) );
		cJSON_AddStringToObject( o, "players", ( std::to_string( players ) + "/" + std::to_string( max ) ).c_str() );
		cJSON_AddBoolToObject( o, "full", players >= max );
		cJSON_AddBoolToObject( o, "password", Bool( r, "password" ) );
		cJSON_AddBoolToObject( o, "mine", me == Str( r, "host" ) );
		cJSON_AddBoolToObject( o, "other", version != OzEdNet_Version() );	// another version: it would refuse the join
		cJSON_AddStringToObject( o, "version", version.size() > 7 ? version.substr( 7 ).c_str() : version.c_str() );
		cJSON_AddItemToArray( out, o );
		n++;
	}
	Replace( "together.sessions", out );
	Printf( "together: %d session%s listed\n", n, n == 1 ? "" : "s" );
	Menu_Set( "together.answered", "true" );
	Menu_Set( "together.count", std::to_string( n ).c_str() );
}

void Together_Frame() {
	Together &t = together();
	// a party leader in a session (hosting it, or joined): the party follows them in, once (the hub takes the
	// follow only while the session's listing is up, so it goes after the host's listing or the joiner's welcome)
	static std::string followed;
	const std::string key = t.live() ? t.host_pid + "|" + t.session_name : "";
	if ( key != followed ) {
		followed = key;
		if ( !key.empty() ) {
			const Rml::String text = Social_FollowText( "edit", t.host_pid.c_str(), t.password().c_str() );
			if ( !text.empty() ) {
				OzE_ExecuteNow( text.c_str() );
				Printf( "together: the party follows into the session\n" );
			}
		}
	}
	if ( t.changed_rev == lastRev ) {
		return;
	}
	lastRev = t.changed_rev;
	static const char *states[] = { "off", "joining", "syncing", "live", "ended" };
	Menu_Set( "together.state", states[(int)t.state] );
	SetString( "together.status", t.status() );
	if ( t.state == Together::State::Ended ) {
		SetString( "together.why", t.why );
	}
	Menu_Set( "together.online", OzEdNet_Online() ? "true" : "false" );
}

}  // namespace OzUI
