/*
===========================================================================
Oz Arena: the Mod Library page (ENGINE_PLAN.md PH step 2, decision 24). GPL-2.0.

The mods the player brings (oz_mods.c): every mod folder with paks in their user folder and their
Quake III install, each with its version, where it is, whether it's ready to play, and whether Oz
supports it (CPMA and Defrag, decision 14; the rest play with no promise). Import takes a zip, a pak
or a folder (Windows' open dialog, or anything dropped on the window: OzUI_ModDropped) into the mod's
own folder in the user folder; the page guesses the mod from the paks and asks the player to confirm
its folder name. Remove deletes an imported mod's folder (never one in the install). Play opens
Skirmish on the mod's tab (CPMA, Defrag), or starts a match under any other mod (oz_playmod).
The known mods table names each mod and the engine extensions it uses; it never holds a download
link: Oz never says where to get a mod.

  mods.rows		[{dir, name, version, where, state, state_text, ready, imported, nested, supported, known, uses}]
  mods.known	[{dir, name, uses, supported, found}]
  mods.ask		the import card: open, path, dir (editable), file, status, message
  mods.remove	the remove question: open, dir, name
  oz_modlib		prints the page's state, for tests
===========================================================================
*/
#include "oz_ui_internal.h"
#include "../online/oz_servers.h"
#include "cJSON.h"

#include <cstring>
#include <string>
#include <vector>

namespace OzUI {
cJSON *Menu_Node( const char *path );	// oz_ui_menu.cpp
void Menu_Dirty( const char *path );
}
using namespace OzUI;

extern "C" int OzMods_Imported( const char *mod );	// oz_mods.c: Oz's own import brought it (Remove deletes only those)

namespace {

const char *const PAGE = "mods";

std::string Str( const char *path ) {
	return Menu_Get( path );
}

// a string value, never read as a number or a bool as Menu_Set would (a folder can be called "1337")
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

std::vector<ozModInfo_t> listed;

// the rows and the known table from what oz_mods.c finds now
void Fill() {
	ozModInfo_t mods[64];
	const int n = OzMods_List( mods, 64 );
	listed.assign( mods, mods + n );
	cJSON *rows = cJSON_CreateArray();
	for ( const ozModInfo_t &m : listed ) {
		const ozKnownMod_t *k = OzMods_KnownMod( m.dir );
		cJSON *o = cJSON_CreateObject();
		cJSON_AddStringToObject( o, "dir", m.dir );
		cJSON_AddStringToObject( o, "name", m.name );
		cJSON_AddStringToObject( o, "version", m.version[0] ? m.version : "" );
		cJSON_AddStringToObject( o, "where", m.where );
		cJSON_AddStringToObject( o, "place", m.imported ? "Imported" : m.nested ? "In your Quake III folder, nested" : "In your Quake III folder" );
		const char *state = m.ready ? "ready" : m.nested ? "nested" : "paks";
		cJSON_AddStringToObject( o, "state", state );
		cJSON_AddStringToObject( o, "state_text", m.ready ? "Ready" : m.nested ? "Needs import" : "Paks only" );
		cJSON_AddBoolToObject( o, "ready", m.ready != 0 );
		cJSON_AddBoolToObject( o, "imported", m.imported != 0 );
		cJSON_AddBoolToObject( o, "removable", m.imported && OzMods_Imported( m.dir ) );	// what Remove takes
		cJSON_AddBoolToObject( o, "nested", m.nested != 0 );
		cJSON_AddBoolToObject( o, "supported", m.supported != 0 );
		cJSON_AddBoolToObject( o, "known", m.known != 0 );
		cJSON_AddStringToObject( o, "uses", k ? k->uses : "A mod Oz doesn't know. It plays with its own game code and the Oz HUD, with no promise it plays right." );
		cJSON_AddStringToObject( o, "paks", std::to_string( m.paks ).c_str() );
		cJSON_AddItemToArray( rows, o );
	}
	if ( cJSON *node = Menu_Node( "mods.rows" ) ) {
		Replace( node, rows );
	} else {
		cJSON_Delete( rows );
	}
	const ozKnownMod_t *known;
	const int nk = OzMods_Known( &known );
	cJSON *table = cJSON_CreateArray();
	for ( int i = 0; i < nk; i++ ) {
		bool found = false;
		for ( const ozModInfo_t &m : listed ) {
			found |= !StrICmp( m.dir, known[i].dir );
		}
		cJSON *o = cJSON_CreateObject();
		cJSON_AddStringToObject( o, "dir", known[i].dir );
		cJSON_AddStringToObject( o, "name", known[i].name );
		cJSON_AddStringToObject( o, "uses", known[i].uses );
		cJSON_AddBoolToObject( o, "supported", known[i].supported != 0 );
		cJSON_AddBoolToObject( o, "found", found );
		cJSON_AddItemToArray( table, o );
	}
	if ( cJSON *node = Menu_Node( "mods.known" ) ) {
		Replace( node, table );
	} else {
		cJSON_Delete( table );
	}
	SetText( "mods.count", std::to_string( n ) + ( n == 1 ? " mod" : " mods" ) );
	Menu_Dirty( "mods" );
}

const ozModInfo_t *Find( const std::string &dir ) {
	for ( const ozModInfo_t &m : listed ) {
		if ( !StrICmp( m.dir, dir.c_str() ) ) {
			return &m;
		}
	}
	return nullptr;
}

void Refuse( const char *why ) {
	PlaySound( Sound::Buzz );
	Printf( "UI: mods: %s\n", why );	// the console's record too (the toast fades)
	Menu_Toast( why, "warn" );
}

// the import card: the file or folder, the folder name guessed for it (the player may change it)
void AskImport( const std::string &path, const std::string &guess ) {
	const size_t slash = path.find_last_of( "\\/" );
	SetText( "mods.ask.path", path );
	SetText( "mods.ask.file", slash == std::string::npos ? path : path.substr( slash + 1 ) );
	SetText( "mods.ask.dir", guess );
	SetText( "mods.ask.status", "" );
	SetText( "mods.ask.message", guess.empty() ? "Couldn't tell which mod this is: type its folder name (cpma, defrag, osp ...)." : "" );
	SetBool( "mods.ask.open", true );
	Printf( "UI: mods: import of %s, taken for %s\n", path.c_str(), guess.empty() ? "no mod" : guess.c_str() );
	GetContext()->Update();
	if ( Rml::ElementDocument *doc = TopDocument() ) {
		FocusQuietly( doc->GetElementById( guess.empty() ? "ml_ask_dir" : "ml_ask_go" ) );
	}
}

void CloseAsk() {
	SetBool( "mods.ask.open", false );
	SetText( "mods.ask.path", "" );
}

// the import itself: what it took or why not; the rows follow
void DoImport() {
	Rml::ElementDocument *doc = TopDocument();
	Rml::Element *input = doc ? doc->GetElementById( "ml_ask_dir" ) : nullptr;
	std::string dir = input ? input->GetAttribute<Rml::String>( "value", "" ) : Str( "mods.ask.dir" );
	const std::string path = Str( "mods.ask.path" );
	char message[256];
	for ( char &c : dir ) {
		c = (char)tolower( (unsigned char)c );
	}
	while ( !dir.empty() && dir.back() == ' ' ) {
		dir.pop_back();
	}
	SetText( "mods.ask.dir", dir );
	if ( dir.empty() ) {
		SetText( "mods.ask.status", "failed" );
		SetText( "mods.ask.message", "Name the mod's folder first." );
		PlaySound( Sound::Buzz );
		return;
	}
	const int n = OzMods_Import( path.c_str(), dir.c_str(), message, sizeof( message ) );
	SetText( "mods.ask.status", n > 0 ? "done" : "failed" );
	SetText( "mods.ask.message", n > 0 ? std::string( "Imported " ) + message : message );
	if ( n > 0 ) {
		PlaySound( Sound::In );
		Fill();
		Menu_Toast( ( std::string( "Imported " ) + message ).c_str(), "info" );
		GetContext()->Update();
		if ( doc ) {
			FocusQuietly( doc->GetElementById( "ml_ask_close" ) );
		}
	} else {
		PlaySound( Sound::Buzz );
	}
}

// Import a zip or a folder: the picker, then the guess and the card
void PickImport( bool folder ) {
	char path[1024], guess[64];
	if ( !OzMods_Pick( folder ? 1 : 0, path, sizeof( path ) ) ) {
		return;	// cancelled
	}
	OzMods_Guess( path, guess, sizeof( guess ) );
	AskImport( path, guess );
}

// Play: Skirmish on the mod's tab for the supported ones, else a match under it on the game's first arena
void Play( const std::string &dir ) {
	const ozModInfo_t *m = Find( dir );
	if ( !m ) {
		Refuse( "That mod isn't listed." );
		return;
	}
	if ( !m->ready ) {
		Refuse( m->nested ? "Import it first: the game doesn't find a nested copy on its own." : "Its game code isn't in its folder: Skirmish needs the mod's own." );
		return;
	}
	if ( OzE_ClientState() != 0 ) {
		Refuse( "Leave your match first." );
		return;
	}
	if ( m->supported ) {
		Skirmish_SetMod( m->dir );
		OpenPageByName( "offline_bots" );
		return;
	}
	// the folder's name goes into a command: letters, digits and _ - + . only (a ; or a quote would run as its own)
	for ( const char *c = m->dir; *c; c++ ) {
		if ( !( ( *c >= 'a' && *c <= 'z' ) || ( *c >= 'A' && *c <= 'Z' ) || ( *c >= '0' && *c <= '9' ) || *c == '_' || *c == '-' || *c == '+' || *c == '.' ) ) {
			Refuse( "That folder's name has characters a mod's can't have." );
			return;
		}
	}
	Printf( "UI: mods: playing under %s\n", m->dir );
	Menu_Toast( ( std::string( m->name ) + " runs with its own rules; Oz makes no promise it plays right." ).c_str(), "info" );
	CloseAllPages();
	OzE_InsertText( ( std::string( "oz_playmod " ) + m->dir + "\nset g_gametype 0\nmap q3dm17\n" ).c_str() );
}

void AskRemove( const std::string &dir ) {
	const ozModInfo_t *m = Find( dir );
	if ( !m || !m->imported ) {
		Refuse( "Only an imported mod, in your user folder, can be removed." );
		return;
	}
	// A folder in the user folder that Import didn't bring may hold only what a server sent, with the
	// player's settings and demos: asked about as an import, Remove deleted those
	if ( !OzMods_Imported( m->dir ) ) {
		Refuse( "Remove deletes only what Import brought. This folder wasn't imported here: delete it yourself if you mean to." );
		return;
	}
	SetText( "mods.remove.dir", m->dir );
	SetText( "mods.remove.name", m->name );
	SetText( "mods.remove.where", m->where );
	SetBool( "mods.remove.open", true );
	GetContext()->Update();
	if ( Rml::ElementDocument *doc = TopDocument() ) {
		FocusQuietly( doc->GetElementById( "ml_remove_no" ) );
	}
}

void DoRemove() {
	char message[256];
	const std::string dir = Str( "mods.remove.dir" );
	SetBool( "mods.remove.open", false );
	if ( OzMods_Remove( dir.c_str(), message, sizeof( message ) ) < 0 ) {
		Refuse( message );
		return;
	}
	PlaySound( Sound::Out );
	Menu_Toast( message, "info" );
	Fill();
}

}	// namespace

namespace OzUI {

// the page's actions (Menu_Run asks here before the online pages')
//   modlib_import zip|folder [path]   the picker (or the path, for tests), then the import card
//   modlib_go, modlib_close           the card's Import and Close
//   modlib_play <dir>                 Skirmish on the mod, or a match under it
//   modlib_remove <dir>, modlib_remove_go, modlib_remove_no   an imported mod's folder deleted, after the question
bool Mods_Action( const Rml::String &verb, const Rml::String &arg ) {
	if ( verb == "modlib_import" ) {
		const std::string how = arg.substr( 0, arg.find( ' ' ) );
		const std::string given = arg.find( ' ' ) == Rml::String::npos ? "" : arg.substr( arg.find( ' ' ) + 1 );
		if ( !given.empty() ) {
			char guess[64];
			OzMods_Guess( given.c_str(), guess, sizeof( guess ) );
			AskImport( given, guess );
		} else {
			PickImport( how == "folder" );
		}
	} else if ( verb == "modlib_go" ) {
		DoImport();
	} else if ( verb == "modlib_close" ) {
		CloseAsk();
	} else if ( verb == "modlib_play" ) {
		Play( arg );
	} else if ( verb == "modlib_remove" ) {
		AskRemove( arg );
	} else if ( verb == "modlib_remove_go" ) {
		DoRemove();
	} else if ( verb == "modlib_remove_no" ) {
		SetBool( "mods.remove.open", false );
	} else if ( verb == "modlib_refresh" ) {
		OzServers_ForgetMods();
		Fill();
	} else {
		return false;
	}
	return true;
}

void Mods_Opened( const char *page ) {
	if ( strcmp( page, PAGE ) ) {
		return;
	}
	SetBool( "mods.ask.open", false );
	SetBool( "mods.remove.open", false );
	Fill();
}

// Escape or B: the import card or the remove question closes first
bool Mods_Back() {
	if ( !strcmp( Menu_Get( "mods.ask.open" ), "true" ) ) {
		CloseAsk();
	} else if ( !strcmp( Menu_Get( "mods.remove.open" ), "true" ) ) {
		SetBool( "mods.remove.open", false );
	} else {
		return false;
	}
	PlaySound( Sound::Out );
	return true;
}

void Mods_Commands() {
	OzE_AddCommand( "oz_modlib", []() {
		// each value copied: Menu_Get gives numbers in one buffer
		const std::string ask = Str( "mods.ask.open" ), rem = Str( "mods.remove.open" );
		Printf( "oz_modlib: %s; ask %s (%s as %s: %s %s); remove %s (%s)\n", Str( "mods.count" ).c_str(), ask.c_str(), Str( "mods.ask.file" ).c_str(),
			Str( "mods.ask.dir" ).c_str(), Str( "mods.ask.status" ).c_str(), Str( "mods.ask.message" ).c_str(), rem.c_str(), Str( "mods.remove.dir" ).c_str() );
		const cJSON *r;
		cJSON_ArrayForEach( r, Menu_Node( "mods.rows" ) ) {
			auto str = [&]( const char *k ) {
				const cJSON *v = cJSON_GetObjectItemCaseSensitive( r, k );
				return std::string( cJSON_IsString( v ) ? v->valuestring : "" );
			};
			Printf( "oz_modlib: row %s (%s) %s %s, %s, %s\n", str( "dir" ).c_str(), str( "name" ).c_str(), str( "version" ).c_str(), str( "state" ).c_str(),
				str( "place" ).c_str(), str( "where" ).c_str() );
		}
	} );
}

}	// namespace OzUI

// a mod's zip, pak or folder dropped on the window (oz_mods.c): the Mod Library opens with the import card
extern "C" void OzUI_ModDropped( const char *path, const char *mod ) {
	if ( OzE_ClientState() != 0 && strcmp( TopPageName(), "pause" ) ) {
		Menu_Toast( "A dropped mod is imported from the menus: leave your match first.", "info" );
		return;
	}
	if ( strcmp( TopPageName(), PAGE ) ) {
		if ( !TopPageName()[0] ) {
			OpenPageByName( "menu" );
		}
		OpenPageByName( PAGE );
	}
	AskImport( path, mod );
}
