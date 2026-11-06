/*
Oz: the map editor's page (ENGINE_PLAN.md P13, engine/EDITOR.md).

ui/pages/editor.rml is the editor's screen: the menu bar, the action bar, the tool strip, the panel and
the status line around an empty viewport element, where the 3D view draws under the page (Editor_Draw,
from OzUI_Draw, before the page layer). The page is the only one open while the editor runs, so the
main menu doesn't come back over it; leaving (Main Menu, File > Exit to main menu) opens the menu again.

The menus (File, Edit, Select, View, Brush, Curve, Texture, Entity, Build, Help) list every action a
mapper does with nothing typed, with its keys, a mark on what's on, greyed where it needs a selection
(MENUS: rows of oz::ed::actions()). A click on a title opens one, the pointer over another title moves
to it, the arrows and Enter work it, Escape or a click elsewhere closes it. Escape in the editor with
nothing to cancel opens File. A button held under the pointer a moment shows its tip: its name, its
keys and what it does. The first time the editor opens, the getting started card shows (F1 again).

Keys and buttons go to the editor first while its page is on top and no text field has the focus:
the view's camera (the right button held looks, W A S D Q E fly), the left button and the wheel over
the view, and the editor's shortcuts (oz::ed::actions(), the one table the keys, the buttons and the
hints read). Everything else is the page's, as on any page. A button's data-action names its action:
the status line says what it does and its keys while the pointer is on it (data-hint: its own text).

The panel's four tabs, made here from the session as it changes:
  Inspect    what's selected: its material, its texture mapping (the faces tool's faces, else every
             face of the selected brushes), the transform, and the keys of its entities (nothing
             selected: the map's own, worldspawn's)
  Materials  every material in the game's files (oz::ed::ShaderLib::materials), by folder, the map's
             own and the recent ones, with thumbnails (a small mip of each, "/@thumb/<image>",
             loaded a few a frame); a click paints the selection
  Objects    the outliner: the world and every entity in file order, their brushes under them
  History    every undo step; a click goes back or forward to it

Over the view: the search (F3), every action that needs nothing typed, found by name, label, what it
does or its keys, the highlighted one done with Enter; and the Keys dialog, every such action's keys,
SET then the keys pressed (oz::ed::Session::bind, saved in keymap.cfg), and the presets (Oz's, Radiant's).
The KEYS help in Inspect shows the player's keys (each kbd's data-keys names its action).

Over the view too: the lists Open map and Open a game map show (the pick list's, the maps in the editor's
maps folder or the game's), and the Save as question.

Commands: editor [new | <file.map> | import <map>], ed <action> [args], ed_open, ed_import, ed_save,
ed_new, ed_camera, ed_stats, ed_leave.
*/
#include "oz_ui_internal.h"
#include "../editor/ed_engine.h"
#include "../editor/ed_shaders.h"
#include "../editor/ed_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <unordered_set>

using namespace OzUI;
using oz::ed::session;

namespace {

const int MAT_PAGE = 30;		// tiles a page
const int OBJ_PAGE = 100;		// outliner rows a page
const int THUMBS_A_FRAME = 3;	// thumbnails loaded each frame

bool leaving;          // ed_leave: closed at the next frame, outside any event
std::string note;      // what the editor last said, shown in the status line a while
bool noteError;
int noteUntil;
Rml::String lastStatus, lastName, lastHint, lastSizeTag, lastTools;
uint64_t lastSel = ~0ull;

// the panel
Rml::String panel = "inspect", shownPanel;
Rml::String lastInspect, lastKeysRml;
std::vector<std::string> shownKeys;	// the inspector's key rows, by index
// the materials browser
Rml::String matView = "all";		// map, favorites, recent, dev, all, folders, folder:<name>, packs
int matPage;
Rml::String lastMats;
std::vector<std::string> recent;
std::string lastShader;
std::unordered_set<std::string> thumbsLoaded;
// the outliner
std::set<int> expanded;
int objPage;
Rml::String lastObjects;
// the history
Rml::String lastHistory;
Rml::String lastEntities;
Rml::String lastChain;	// the panel as last chained for the d-pad
// the search, the compile's warnings and the Keys dialog
bool searchOpen, keymapOpen, warnsOpen;
std::string searchQuery;			// what the rows were found for
std::vector<std::string> found;		// the search's rows, by action
int foundAt;						// the highlighted row: Enter does it
Rml::String lastSearch, lastKeymap, lastKeymapNote, lastKeyHelp;
Rml::String lastPanes;	// the panes' names as last shown
std::string colorGridKey;	// the color key whose full grid of colors shows
bool advancedOpen;			// a light's raw keys shown (Advanced: all keys)
// the materials the player keeps at hand (<fs_homepath>/oz/editor/favorites.txt, a name a line)
std::vector<std::string> favorites;
bool favoritesLoaded;
void LoadFavorites();
// the UV editor: the first target face over its texture, in texture space (a repeat is uvScale dp)
bool uvOpen;
double uvScale = 120, uvDragX, uvDragY;	// the drag under way, in dp (shown, applied when let go)
bool uvFit = true;						// zoomed to fit the face at the next build
Rml::String lastUv;
const double UV_VIEW = 440;				// the view's side, dp
// the pick list a typed key opens (a sound, a model, a name to target)
bool pickOpen;
std::string pickKey, pickType, pickQuery;	// pickType: sound, music, model, target
std::vector<std::string> pickFound;
int pickAt;
Rml::String lastPick;
unsigned lastFilters = ~0u;	// the filter chips as last shown
// the menus: the one open ("" none), its rows by action ("-" a line, "#..." a heading), the highlighted row
std::string menuOpen;
std::vector<std::string> menuRows;
int menuAt = -1;
Rml::String lastMenu;
// the Save as question and the getting started card (shown by itself the first time the editor opens)
bool saveAsOpen, guideOpen, guideChecked;
Rml::String lastSaveAsNote;
// a control's tip: the control the pointer is on and since when; shown, or done with (a click)
Rml::Element *tipFor;
int tipSince;
bool tipShown, tipDone;

Rml::ElementDocument *EditorDoc() {
	if ( !IsPageOpen( "editor" ) ) {
		return nullptr;
	}
	Rml::ElementDocument *doc = TopDocument();
	return doc && !strcmp( TopPageName(), "editor" ) ? doc : nullptr;
}

bool OverView( Rml::ElementDocument *doc ) {
	Rml::Element *hover = GetContext()->GetHoverElement();
	Rml::Element *view = doc->GetElementById( "viewport" );
	return hover && view && ( hover == view || hover->GetParentNode() == view );
}

Rml::Element *TypingIn() {
	Rml::Element *focus = GetContext()->GetFocusElement();
	return focus && ( focus->GetTagName() == "input" || focus->GetTagName() == "textarea" ) ? focus : nullptr;
}

// A text field inside e has the focus.
bool TypingInside( Rml::Element *e ) {
	for ( Rml::Element *f = TypingIn(); f; f = f->GetParentNode() ) {
		if ( f == e ) {
			return true;
		}
	}
	return false;
}

void SetText( Rml::ElementDocument *doc, const char *id, const Rml::String &text, Rml::String &last ) {
	if ( text == last ) {
		return;
	}
	last = text;
	if ( Rml::Element *e = doc->GetElementById( id ) ) {
		e->SetInnerRML( Rml::StringUtilities::EncodeRml( text ) );
	}
}

void SetRml( Rml::ElementDocument *doc, const char *id, const Rml::String &rml ) {
	if ( Rml::Element *e = doc->GetElementById( id ) ) {
		e->SetInnerRML( rml );
	}
}

void Show( Rml::ElementDocument *doc, const char *id, bool on ) {
	if ( Rml::Element *e = doc->GetElementById( id ) ) {
		e->SetClass( "hidden", !on );
	}
}

Rml::String InputValue( Rml::ElementDocument *doc, const char *id ) {
	Rml::Element *e = doc->GetElementById( id );
	auto *f = dynamic_cast<Rml::ElementFormControl *>( e );
	return f ? f->GetValue() : "";
}

void SetInput( Rml::ElementDocument *doc, const char *id, const Rml::String &value ) {
	if ( auto *f = dynamic_cast<Rml::ElementFormControl *>( doc->GetElementById( id ) ) ) {
		f->SetValue( value );
	}
}

Rml::String Num( double v ) {
	char buf[32];
	snprintf( buf, sizeof buf, "%g", v );
	return buf;
}

Rml::String Enc( const std::string &s ) {
	return Rml::StringUtilities::EncodeRml( s );
}

std::string Lower( std::string s ) {
	std::transform( s.begin(), s.end(), s.begin(), []( unsigned char c ) { return (char)tolower( c ); } );
	return s;
}

void Open() {
	CloseAllPages();
	OpenPageByName( "editor" );
}

void Report( const std::string &error ) {
	Printf( "^3map editor: %s\n", error.c_str() );
	Menu_Toast( error.c_str(), "error" );
}

// The editor's notes ("Undo Move", "Saved ...", why nothing happened) show in the status line for a
// while, where the eye already is, not as toasts over the panel.
void Notify( const std::string &text, bool error ) {
	note = text;
	noteError = error;
	// long enough to read: a second for every 15 characters past the first 45 (a warning of two lines ~12 s)
	const int reading = int( std::max<size_t>( text.size(), 45 ) - 45 ) * 1000 / 15;
	noteUntil = OzE_Milliseconds() + ( error ? 5000 : 3000 ) + std::min( reading, 9000 );
}

void Editor_f() {
	const char *a = OzE_Argv( 1 );
	std::string err;
	session().notify = Notify;
	if ( !Rml::StringUtilities::ToLower( a ).compare( "new" ) ) {
		session().new_map();
	} else if ( !Rml::StringUtilities::ToLower( a ).compare( "import" ) && OzE_Argc() > 2 ) {
		if ( !session().import_bsp( OzE_Argv( 2 ), err, true ) ) {
			Report( err );
		}
	} else if ( a[0] ) {
		if ( !session().open( a, err ) ) {
			Report( err );
		}
	}
	Open();
}

// ---------------------------------------------------------------- materials

// A tool material's color, as the view draws it.
const char *ToolColor( oz::ed::Look look ) {
	switch ( look ) {
	case oz::ed::Look::Caulk: return "#b05c96";
	case oz::ed::Look::Clip: return "#9696ff";
	case oz::ed::Look::Trigger: return "#ffa528";
	case oz::ed::Look::Hint: return "#dcc8ff";
	case oz::ed::Look::Nodraw: return "#a0a0a0";
	case oz::ed::Look::Origin: return "#ff7828";
	case oz::ed::Look::Portal: return "#ff3c3c";
	case oz::ed::Look::Sky: return "#6ea0ff";
	case oz::ed::Look::Fog: return "#96aabe";
	default: return nullptr;
	}
}

// What a swatch of the material holds, box rem across: its picture fit in (loaded now when known,
// else marked to load), or a tool's color and name. style: the swatch's own background.
Rml::String SwatchRml( const std::string &name, double box, Rml::String &style, bool lazy ) {
	oz::ed::ShaderLib &lib = oz::ed::shaders_lib();
	const char *tool = ToolColor( lib.look( name ) );
	const bool common = name.compare( 0, 7, "common/" ) == 0;
	style.clear();
	if ( tool && common ) {
		style = Rml::String( "background-color: " ) + tool + ";";
		return "<span>" + Enc( Rml::StringUtilities::ToUpper( name.substr( 7 ) ) ) + "</span>";
	}
	if ( !lib.found( name ) ) {
		style = "background-color: #1a1614;";
		return "<span>NO IMAGE</span>";
	}
	int w = 64, h = 64;
	lib.size( name, w, h );
	const double k = box / std::max( 1, std::max( w, h ) );
	const std::string src = "/@thumb/" + lib.image( name );
	char size[96];
	snprintf( size, sizeof size, "width: %.3frem; height: %.3frem;", w * k, h * k );
	if ( tool ) {
		style = Rml::String( "border-color: " ) + tool + ";";	// a sky or a fog keeps its own picture, in its color's frame
	}
	if ( lazy && !thumbsLoaded.count( src ) ) {
		return "<img data-src=\"" + Enc( src ) + "\" style=\"" + size + "\"/>";
	}
	return "<img src=\"" + Enc( src ) + "\" style=\"" + size + "\"/>";
}

void SetSwatch( Rml::ElementDocument *doc, const char *id, const std::string &name, double box ) {
	Rml::Element *e = doc->GetElementById( id );
	if ( !e ) {
		return;
	}
	Rml::String style;
	const Rml::String rml = name.empty() ? "" : SwatchRml( name, box, style, false );
	e->SetInnerRML( rml );
	e->RemoveProperty( "background-color" );
	e->RemoveProperty( "border-color" );
	e->SetClass( "tool", style.find( "background" ) != Rml::String::npos );
	e->SetClass( "none", style.find( "#1a1614" ) != Rml::String::npos );
	if ( !style.empty() ) {
		e->SetAttribute( "style", style );
	} else {
		e->RemoveAttribute( "style" );
	}
}

// The shaders the map uses, by name.
std::vector<std::string> MapMaterials() {
	const oz::ed::Doc &d = session().doc;
	std::set<std::string> names;
	for ( oz::ed::Id id : d.ids() ) {
		const oz::ed::Object *o = d.get( id );
		if ( o->kind == oz::ed::Kind::Brush ) {
			for ( const oz::ed::Face &f : o->brush.faces ) {
				names.insert( Lower( oz::ed::shader_name( f.shader ) ) );
			}
		} else if ( o->kind == oz::ed::Kind::Patch ) {
			names.insert( Lower( oz::ed::shader_name( o->patch.shader ) ) );
		}
	}
	return std::vector<std::string>( names.begin(), names.end() );
}

// What a tool material does, for its tile's hint (the generic "paints the selection" says nothing of it)
std::string ToolHelp( const std::string &name ) {
	static const std::map<std::string, const char *> help = {
		{ "caulk", "solid but never drawn: for faces nobody sees (the compile and the game skip them)" },
		{ "clip", "an invisible wall players can't pass (shots go through)" },
		{ "weapclip", "an invisible wall that stops shots (and players)" },
		{ "botclip", "an invisible wall for bots only" },
		{ "fullclip", "an invisible wall for players and shots alike" },
		{ "nodraw", "solid and never drawn" },
		{ "nodrawnonsolid", "neither drawn nor solid: a helper volume" },
		{ "trigger", "a trigger's volume: invisible, players pass through" },
		{ "hint", "guides how the compile splits the map for visibility (invisible, not solid)" },
		{ "skip", "a hint brush's faces that shouldn't split anything" },
		{ "origin", "a door's, a mover's or a rotating entity's center" },
		{ "areaportal", "lets a door's two sides be hidden from each other while it's shut" },
		{ "clusterportal", "divides the bots' areas" },
		{ "donotenter", "bots keep out" },
		{ "nodrop", "what falls in vanishes: items and bodies (a pit's bottom)" },
		{ "slick", "players slide on it (ice)" },
		{ "lightgrid", "the light grid's bounds" },
	};
	if ( name.compare( 0, 7, "common/" ) != 0 ) {
		return "";
	}
	auto it = help.find( name.substr( 7 ) );
	return it != help.end() ? it->second : "";
}

void BuildMaterials( Rml::ElementDocument *doc ) {
	const std::string search = Lower( InputValue( doc, "ed-msearch" ) );
	const std::string current = Lower( session().shader );
	LoadFavorites();
	char key[200];
	snprintf( key, sizeof key, "%s|%d|%s|%s|%llu|%d", matView.c_str(), matPage, search.c_str(), current.c_str(),
		matView == "map" ? (unsigned long long)session().doc.revision : 0ull, (int)favorites.size() );
	if ( key == lastMats ) {
		return;
	}
	lastMats = key;
	const std::vector<oz::ed::Material> &all = oz::ed::shaders_lib().materials();

	// the chips: the views, then the folder chosen
	const bool inFolder = matView.compare( 0, 7, "folder:" ) == 0;
	Rml::String chips;
	auto chip = [&]( const char *view, const Rml::String &label, bool on ) {
		chips += Rml::String( "<button class=\"chip" ) + ( on ? " on" : "" ) + "\" onclick=\"append ed panel mats " + view + "\">" + label + "</button>";
	};
	chip( "map", "IN THIS MAP", matView == "map" );
	chip( "favorites", "FAVORITES", matView == "favorites" );
	chip( "recent", "RECENT", matView == "recent" );
	chip( "dev", "DEV TEXTURES", matView == "dev" );
	chip( "all", "ALL", matView == "all" );
	chip( "folders", inFolder ? Enc( Rml::StringUtilities::ToUpper( matView.substr( 7 ) ) ) + " +" : Rml::String( "FOLDERS +" ),
		matView == "folders" || inFolder );
	chip( "packs", "MAP PACKS", matView == "packs" );
	SetRml( doc, "ed-chips", chips );
	// the current material's favorite button
	bool fav = false;
	for ( const std::string &f : favorites ) {
		fav = fav || Lower( f ) == current;
	}
	SetRml( doc, "ed-fav", fav ? "FAVORITE: REMOVE" : "ADD TO FAVORITES" );

	// the folders to choose from: id's, then the map packs'
	if ( matView == "folders" && search.empty() ) {
		std::map<std::string, int> folders[2];
		for ( const oz::ed::Material &m : all ) {
			if ( m.folder != "oz_dev" ) {  // Dev textures has its own chip
				folders[m.stock ? 0 : 1][m.folder.empty() ? "(top)" : m.folder]++;
			}
		}
		Rml::String rml;
		for ( int k = 0; k < 2; k++ ) {
			if ( k == 1 && !folders[1].empty() ) {
				rml += "<p>From the map packs in your game's folder:</p>";
			}
			for ( const auto &kv : folders[k] ) {
				rml += "<button class=\"folder\" onclick=\"append ed panel mats folder:" + Enc( kv.first ) + "\">" + Enc( kv.first ) +
					"<span class=\"n\">" + std::to_string( kv.second ) + "</span></button>";
			}
		}
		SetRml( doc, "ed-mats", rml );
		SetRml( doc, "ed-mpager", "" );
		return;
	}

	// the names in view: Dev textures is Oz's prototyping textures (textures/oz_dev), All the rest of
	// id's own (and the tools), Map packs the rest; a search looks at all of them
	std::vector<std::string> names;
	if ( matView == "map" ) {
		names = MapMaterials();
	} else if ( matView == "recent" ) {
		names = recent;
	} else if ( matView == "favorites" ) {
		names = favorites;
	} else {
		const std::string folder = inFolder ? matView.substr( 7 ) : "";
		for ( const oz::ed::Material &m : all ) {
			const bool in = search.size() ? true
				: inFolder ? m.folder == folder || ( folder == "(top)" && m.folder.empty() )
				: matView == "dev" ? m.folder == "oz_dev"
				: matView == "packs" ? !m.stock : m.folder != "oz_dev";
			if ( in ) {
				names.push_back( m.name );
			}
		}
	}
	if ( !search.empty() ) {
		names.erase( std::remove_if( names.begin(), names.end(), [&]( const std::string &n ) { return Lower( n ).find( search ) == std::string::npos; } ),
			names.end() );
	}
	const int pages = std::max( 1, int( ( names.size() + MAT_PAGE - 1 ) / MAT_PAGE ) );
	matPage = std::min( matPage, pages - 1 );
	Rml::String rml;
	for ( size_t i = size_t( matPage ) * MAT_PAGE; i < names.size() && i < size_t( matPage + 1 ) * MAT_PAGE; i++ ) {
		const std::string &n = names[i];
		const size_t slash = n.rfind( '/' );
		Rml::String style;
		const Rml::String sw = SwatchRml( n, 5.4, style, true );
		const bool tool = style.find( "background" ) != Rml::String::npos;
		const std::string toolHelp = ToolHelp( n );
		const std::string hint = n + ( !toolHelp.empty() ? ": " + toolHelp + "." : "" ) + " Click paints the selection with it, and new brushes use it";
		rml += "<div class=\"mat" + Rml::String( Lower( n ) == current ? " on" : "" ) + "\" data-hint=\"" + Enc( hint ) +
			"\" onclick=\"append ed paint " + Enc( n ) + "\"><div class=\"sw" +
			( tool ? " tool" : "" ) + "\"" + ( style.empty() ? "" : " style=\"" + style + "\"" ) + ">" + sw + "</div>" +
			"<span class=\"mn\">" +
			Enc( slash == std::string::npos ? n : n.substr( slash + 1 ) ) + "</span></div>";
	}
	if ( names.empty() ) {
		rml = matView == "recent" && search.empty() ? "<p>The materials you paint or pick show here.</p>"
			: matView == "favorites" && search.empty() ? "<p>None yet: click a material, then Add to favorites.</p>" : "<p>No material matches.</p>";
	}
	SetRml( doc, "ed-mats", rml );
	Rml::String pager;
	if ( pages > 1 ) {
		pager = "<button class=\"tb\" onclick=\"append ed panel mpage -1\">PREVIOUS</button><span>" + std::to_string( matPage + 1 ) + " / " +
			std::to_string( pages ) + "</span><button class=\"tb\" onclick=\"append ed panel mpage 1\">NEXT</button>";
	}
	SetRml( doc, "ed-mpager", pager );
}

std::string FavoritesFile() {
	return std::string( OzEd_HomePath() ) + "/oz/editor/favorites.txt";
}

void LoadFavorites() {
	if ( favoritesLoaded ) {
		return;
	}
	favoritesLoaded = true;
	char *text = nullptr;
	const int len = OzEd_ReadOSFile( FavoritesFile().c_str(), &text );
	const std::string t = len > 0 ? std::string( text, size_t( len ) ) : std::string();
	OzEd_Free( text );
	size_t at = 0;
	while ( at < t.size() ) {
		size_t eol = t.find( '\n', at );
		std::string line = t.substr( at, eol == std::string::npos ? std::string::npos : eol - at );
		at = eol == std::string::npos ? t.size() : eol + 1;
		line.erase( std::remove( line.begin(), line.end(), '\r' ), line.end() );
		if ( !line.empty() && std::find( favorites.begin(), favorites.end(), line ) == favorites.end() ) {
			favorites.push_back( line );
		}
	}
}

void ToggleFavorite( const std::string &name ) {
	LoadFavorites();
	auto it = std::find( favorites.begin(), favorites.end(), name );
	const bool had = it != favorites.end();
	if ( had ) {
		favorites.erase( it );
	} else {
		favorites.push_back( name );
	}
	std::string out;
	for ( const std::string &f : favorites ) {
		out += f + "\n";
	}
	const std::string file = FavoritesFile();
	OzEd_MakeDirs( file.c_str() );
	OzEd_WriteOSFile( file.c_str(), out.data(), int( out.size() ) );
	Notify( had ? name + ": no longer a favorite" : name + ": a favorite (the Favorites view keeps it at hand)", false );
}

// A few of the tiles' thumbnails a frame, so a page of them never stalls a frame long: the Materials
// tab's, or Sky and sun's
void LoadThumbs( Rml::ElementDocument *doc, const char *id = "ed-mats" ) {
	Rml::Element *mats = doc->GetElementById( id );
	if ( !mats ) {
		return;
	}
	Rml::ElementList imgs;
	mats->GetElementsByTagName( imgs, "img" );
	int n = 0;
	for ( Rml::Element *img : imgs ) {
		if ( n >= THUMBS_A_FRAME ) {
			break;
		}
		const Rml::String src = img->GetAttribute<Rml::String>( "data-src", "" );
		if ( src.empty() || img->HasAttribute( "src" ) ) {
			continue;
		}
		img->SetAttribute( "src", src );
		thumbsLoaded.insert( src );
		n++;
	}
}

// ---------------------------------------------------------------- typed keys

// The colors a light or the map's ambient takes in a click: white, warm, cool, then the hues
const struct {
	const char *rgb, *name;
} PALETTE[] = { { "1 1 1", "White" }, { "1 0.85 0.7", "Warm white" }, { "0.75 0.85 1", "Cool white" }, { "1 0.2 0.15", "Red" },
	{ "1 0.55 0.15", "Orange" }, { "1 0.9 0.3", "Yellow" }, { "0.35 1 0.4", "Green" }, { "0.3 0.9 1", "Cyan" }, { "0.3 0.45 1", "Blue" },
	{ "0.75 0.35 1", "Purple" } };

// "r g b" (0 to 1; 0 to 255 read as such) as #rrggbb, its brightest at full
Rml::String HexColor( const std::string &v ) {
	double c[3] = { 1, 1, 1 };
	if ( sscanf( v.c_str(), "%lf %lf %lf", &c[0], &c[1], &c[2] ) != 3 ) {
		return "#808080";
	}
	const double m = std::max( { c[0], c[1], c[2], 1e-6 } );
	char buf[16];
	snprintf( buf, sizeof buf, "#%02x%02x%02x", int( 255 * std::max( 0.0, c[0] ) / m ), int( 255 * std::max( 0.0, c[1] ) / m ), int( 255 * std::max( 0.0, c[2] ) / m ) );
	return buf;
}

// What a typed key gets under its row: a color its swatch and a palette, an angle a compass, an on/off
// key a switch, a sound a list to pick from and Listen, a model or a target a list to pick from.
Rml::String TypedControls( const std::string &k, const oz::ed::KeyDef &kd, const std::string &v ) {
	const Rml::String key = Enc( k );
	Rml::String out;
	auto button = [&]( const Rml::String &label, const Rml::String &verb, const Rml::String &hint, bool on ) {
		out += "<button class=\"tb" + Rml::String( on ? " on" : "" ) + "\" data-hint=\"" + hint + "\" onclick=\"append ed panel " + verb + "\">" + label + "</button>";
	};
	switch ( kd.type ) {
	case oz::ed::KeyType::Color:
		out += "<span class=\"csw\" style=\"background-color: " + HexColor( v.empty() ? "1 1 1" : v ) + ";\"></span>";
		for ( size_t i = 0; i < sizeof( PALETTE ) / sizeof( PALETTE[0] ); i++ ) {
			out += "<button class=\"cchip" + Rml::String( v == PALETTE[i].rgb ? " on" : "" ) + "\" style=\"background-color: " + HexColor( PALETTE[i].rgb ) +
				";\" data-hint=\"" + Enc( k + ": " + PALETTE[i].name + " (" + PALETTE[i].rgb + ")" + ( k == "_color" ? ". The hue only: how bright it is, is the light's own number" : "" ) ) +
				"\" onclick=\"append ed panel swatch " + key + " " +
				std::to_string( i ) + "\"></button>";
		}
		out += "<button class=\"tb\" data-hint=\"" + Enc( k + ": every hue, " + ( colorGridKey == k ? "hidden again" : "in three strengths" ) ) +
			"\" onclick=\"append ed panel colors " + key + "\">" + ( colorGridKey == k ? "-" : "+" ) + "</button>";
		if ( colorGridKey == k ) {
			// twelve hues by three strengths (the compiler takes a light's color as its hue and strength;
			// how bright it is, is the light's own number)
			for ( double sat : { 1.0, 0.6, 0.3 } ) {
				out += "</div><div class=\"row kt\"><span class=\"nox\"></span>";
				for ( int h = 0; h < 360; h += 30 ) {
					const double c = sat, x = c * ( 1 - std::fabs( std::fmod( h / 60.0, 2 ) - 1 ) ), m = 1 - c;
					double r = 0, g = 0, b = 0;
					switch ( h / 60 ) {
					case 0: r = c, g = x; break;
					case 1: r = x, g = c; break;
					case 2: g = c, b = x; break;
					case 3: g = x, b = c; break;
					case 4: r = x, b = c; break;
					default: r = c, b = x; break;
					}
					char rgb[48];
					snprintf( rgb, sizeof rgb, "%g %g %g", std::round( ( r + m ) * 100 ) / 100, std::round( ( g + m ) * 100 ) / 100, std::round( ( b + m ) * 100 ) / 100 );
					out += "<button class=\"cchip" + Rml::String( v == rgb ? " on" : "" ) + "\" style=\"background-color: " + HexColor( rgb ) + ";\" data-hint=\"" +
						Enc( k + ": " + rgb ) + "\" onclick=\"append ed panel rgb " + key + " " + rgb + "\"></button>";
				}
			}
		}
		break;
	case oz::ed::KeyType::Angle: {
		static const char *names[8] = { "E", "NE", "N", "NW", "W", "SW", "S", "SE" };
		const double a = atof( v.c_str() );
		for ( int i = 0; i < 8; i++ ) {
			button( names[i], "angle " + key + " " + std::to_string( i * 45 ), Enc( k + ": " + std::to_string( i * 45 ) + " degrees (east is 0, north 90)" ),
				!v.empty() && a == i * 45 );
		}
		button( "UP", "angle " + key + " -1", Enc( k + ": -1, up" ), v == "-1" );
		button( "DOWN", "angle " + key + " -2", Enc( k + ": -2, down" ), v == "-2" );
		button( "VIEW", "angle " + key + " view", Enc( k + ": the way the camera faces, to 15 degrees" ), false );
		break;
	}
	case oz::ed::KeyType::Bool:
		button( v == "1" ? "ON" : "OFF", "bool " + key, Enc( k + ": click turns it " + ( v == "1" ? "off" : "on" ) ), v == "1" );
		break;
	case oz::ed::KeyType::Sound:
		button( "PICK", "pick " + key + ( k == "music" ? " music" : " sound" ), Enc( "Pick " + k + " from the game's " + ( k == "music" ? "music" : "sounds" ) ), false );
		button( "LISTEN", "listen " + key, Enc( "Plays " + ( v.empty() ? k : v ) ), false );
		break;
	case oz::ed::KeyType::Model:
		button( "PICK", "pick " + key + " model", Enc( "Pick " + k + " from the game's models" ), false );
		break;
	case oz::ed::KeyType::Target:
		button( "PICK", "pick " + key + " target", Enc( "Pick what " + k + " names from the map's names" ), false );
		break;
	case oz::ed::KeyType::Number: {
		const double step = k == "light" ? 50 : k == "radius" ? 16 : k == "speed" ? 50 : k == "wait" || k == "random" ? 0.5 : k == "ambient" ? 5
			: k == "dmg" ? 5 : k == "lip" ? 4 : 8;
		button( "-", "step " + key + " " + Num( -step ), Enc( k + " " + Num( step ) + " less" ), false );
		button( "+", "step " + key + " " + Num( step ), Enc( k + " " + Num( step ) + " more" ), false );
		break;
	}
	default:
		return "";
	}
	return "<div class=\"row kt\"><span class=\"nox\"></span>" + out + "</div>";
}

// ---------------------------------------------------------------- a light's sliders

// A selected light's intensity and range (a spotlight's cone too), as sliders: each shows its number
// as it moves and sets the key once let go (one undo step), q3map2's keys (oz::ed::light_numbers)
struct LightSlide {
	std::string what;	// "" none waiting
	double value = 0;
	int at = 0;
} lightSlide;

// What an entity is, in words, for its panel's title: a light with a target is a spotlight, an
// info_null a light aims at is its aim point, a pickup goes by its name; else by its class
Rml::String EntityTitle( const oz::ed::Doc &d, oz::ed::Id id ) {
	static const std::map<std::string, std::string> names = { { "info_player_start", "PLAYER SPAWN" }, { "info_player_deathmatch", "PLAYER SPAWN" },
		{ "info_player_intermission", "INTERMISSION CAMERA" }, { "info_null", "AIM POINT" }, { "info_notnull", "TARGET POINT" },
		{ "item_botroam", "BOT ROAM SPOT" }, { "info_camp", "BOT CAMP SPOT" }, { "path_corner", "TRAIN STOP" }, { "target_position", "TARGET POINT" },
		{ "misc_teleporter_dest", "TELEPORTER EXIT" }, { "trigger_teleport", "TELEPORTER TRIGGER" }, { "target_teleporter", "TELEPORTER ACTION" },
		{ "trigger_push", "JUMP PAD TRIGGER" }, { "target_push", "PUSHER" }, { "func_door", "DOOR" }, { "func_button", "BUTTON" }, { "func_plat", "LIFT" },
		{ "func_rotating", "SPINNING BRUSH" }, { "func_bobbing", "BOBBING BRUSH" }, { "func_pendulum", "PENDULUM" }, { "func_train", "TRAIN" },
		{ "func_static", "STILL BRUSH MODEL" }, { "func_timer", "TIMER" }, { "func_group", "GROUP" }, { "trigger_always", "AT MAP START" },
		{ "trigger_multiple", "TRIGGER" }, { "trigger_hurt", "HURT ZONE" }, { "target_give", "GIVE ITEMS" }, { "target_remove_powerups", "TAKE POWERUPS" },
		{ "target_delay", "DELAY" }, { "target_relay", "RELAY" }, { "target_kill", "KILL" }, { "target_speaker", "SPEAKER" }, { "target_print", "MESSAGE" },
		{ "target_laser", "LASER" }, { "target_score", "SCORE" }, { "target_location", "LOCATION NAME" }, { "light", "LIGHT" }, { "misc_model", "MODEL" },
		{ "misc_portal_surface", "MIRROR OR PORTAL" }, { "misc_portal_camera", "PORTAL CAMERA" }, { "shooter_rocket", "ROCKET SHOOTER" },
		{ "shooter_grenade", "GRENADE SHOOTER" }, { "shooter_plasma", "PLASMA SHOOTER" } };
	const oz::ed::Object *o = d.get( id );
	if ( !o ) {
		return "KEYS";
	}
	const std::string cn = o->entity.classname();
	if ( cn == "light" && !o->entity.get( "target" ).empty() ) {
		return "SPOTLIGHT";
	}
	if ( cn == "info_null" ) {
		const std::string name = o->entity.get( "targetname" );
		for ( oz::ed::Id e : d.ids_of( oz::ed::Kind::Entity ) ) {
			const oz::ed::Entity &en = d.get( e )->entity;
			if ( !name.empty() && en.classname() == "light" && en.get( "target" ) == name ) {
				return "SPOTLIGHT AIM POINT";
			}
		}
	}
	const auto it = names.find( cn );
	if ( it != names.end() ) {
		return it->second;
	}
	const oz::ed::EntityDef *def = oz::ed::entity_def( cn );
	if ( def && def->item ) {
		return Rml::StringUtilities::ToUpper( def->help );	// a pickup's help is its name
	}
	if ( def && def->section == "Spawns" ) {
		return "PLAYER SPAWN (" + Rml::StringUtilities::ToUpper( cn ) + ")";
	}
	return Rml::StringUtilities::ToUpper( cn );
}

// The match filters every spawned entity takes (ed_entdefs.cpp common_keys)
bool MatchFilter( const std::string &k ) {
	return k == "gametype" || k == "not_gametype" || k == "notfree" || k == "notteam" || k == "notsingle";
}

// An add-key chip's words: what a key does for the common ones, else its name
Rml::String ChipLabel( const std::string &k ) {
	static const std::map<std::string, std::string> labels = { { "target", "LINK TO" }, { "targetname", "NAME" },
		{ "gametype", "ONLY IN THESE GAME TYPES" }, { "not_gametype", "NOT IN THESE GAME TYPES (CPMA)" }, { "notfree", "NOT IN FREE FOR ALL" },
		{ "notteam", "NOT IN TEAM GAMES" }, { "notsingle", "NOT IN SINGLE PLAYER" } };
	const auto it = labels.find( k );
	return it != labels.end() ? Rml::String( it->second ) : Rml::StringUtilities::ToUpper( k );
}

Rml::String LightSlideText( const std::string &what, double v, const oz::ed::LightNumbers &n ) {
	if ( what == "range" && v <= 0 ) {
		const double i = std::fabs( n.intensity * n.scale );
		return "Auto, about " + Num( std::round( std::sqrt( i * 7500 / 16 ) ) );
	}
	return Num( std::round( v ) ) + ( what == "intensity" ? "" : " units" );
}

Rml::String LightSliders( const oz::ed::Object *o ) {
	if ( !o || o->kind != oz::ed::Kind::Entity || o->entity.classname() != "light" ) {
		return "";
	}
	const oz::ed::LightNumbers n = oz::ed::light_numbers( o->entity );
	const bool spot = !o->entity.get( "target" ).empty();
	Rml::String out = "<div class=\"sub\">LIGHT</div>";
	// its color: the palette that's under the key's row, here with a name
	if ( const oz::ed::KeyDef *kd = oz::ed::key_def( "light", "_color" ) ) {
		Rml::String c = TypedControls( "_color", *kd, o->entity.get( "_color", "1 1 1" ) );
		const Rml::String nox = "<span class=\"nox\"></span>";
		const size_t at = c.find( nox );
		if ( at != Rml::String::npos ) {
			c.replace( at, nox.size(), "<span class=\"lbl\">Color</span>" );
		}
		out += c;
	}
	auto slider = [&]( const char *what, const char *label, double lo, double hi, double step, double v, const std::string &hint ) {
		const Rml::String w = what;
		out += "<div class=\"row lsl\"><span class=\"lbl\">" + Rml::String( label ) + "</span><input type=\"range\" class=\"slider\" id=\"ed-ls-" + w +
			"\" min=\"" + Num( lo ) + "\" max=\"" + Num( hi ) + "\" step=\"" + Num( step ) + "\" value=\"" + Num( std::clamp( v, lo, hi ) ) + "\" data-hint=\"" +
			Enc( hint ) + "\" data-slide=\"" + w + "\"/><span class=\"n\" id=\"ed-ls-" + w + "-n\">" + LightSlideText( what, v, n ) +
			"</span></div>";
	};
	slider( "intensity", "Intensity", 25, 2500, 25, n.intensity, "Intensity: how bright the light is (the \"light\" key, 300 by default)" +
		std::string( n.linear ? "; it keeps its range" : "; it also reaches further" ) );
	slider( "range", "Range", 0, 2048, 16, n.range, "Range: how far it reaches, fading evenly to nothing there (linear falloff and its \"fade\" key). "
		"All the way left is Auto: it fades as real light does, further the brighter it is" );
	if ( spot ) {
		slider( "cone", "Cone", 8, 512, 8, atof( o->entity.get( "radius", "64" ).c_str() ),
			"Cone: how wide the spotlight is where it meets its aim point (the \"radius\" key, 64 by default)" );
	}
	// a point light shines every way, a spotlight one way: at its aim point, a marker to drag
	out += "<div class=\"row\">" + Rml::String( spot
		? "<button class=\"tb\" data-action=\"point_light\" onclick=\"append ed point_light\">BACK TO A POINT LIGHT</button>"
		  "<span class=\"lnote\">A spotlight: drag its aim point (" + Enc( o->entity.get( "target" ) ) + ") to aim it</span>"
		: "<button class=\"tb\" data-action=\"spotlight\" onclick=\"append ed spotlight\">MAKE SPOTLIGHT</button>"
		  "<span class=\"lnote\">Shines every way. A spotlight shines one way, at a point you drag</span>" ) + "</div>";
	return out;
}

// A slider let go (the mouse up, a moment after its last move): the lights' key set, unless it's
// what they have already
void LightSlideTick() {
	if ( lightSlide.what.empty() || OzE_KeyDown( K_MOUSE1 ) || OzE_Milliseconds() - lightSlide.at < 150 ) {
		return;
	}
	const std::string what = lightSlide.what;
	lightSlide.what.clear();
	oz::ed::Session &s = session();
	const std::vector<oz::ed::Id> ents = s.key_entities();
	const oz::ed::Object *o = ents.empty() ? nullptr : s.doc.get( ents[0] );
	if ( !o ) {
		return;
	}
	const oz::ed::LightNumbers n = oz::ed::light_numbers( o->entity );
	const double now = what == "intensity" ? n.intensity : what == "range" ? n.range : atof( o->entity.get( "radius", "64" ).c_str() );
	if ( std::fabs( now - lightSlide.value ) < 0.5 ) {
		return;
	}
	s.notify = Notify;
	s.run( "light_" + what, { Num( lightSlide.value ) } );
}

// A light's slider moved (a change on the page, dragged, clicked or stepped with the keys): straight
// to ed panel light_slide, not through the console's buffer, which a script's waits would hold up
bool Typed( const std::string &what, const std::vector<std::string> &args, Rml::Element *field );

class SlideListener : public Rml::EventListener {
public:
	void ProcessEvent( Rml::Event &e ) override {
		Rml::Element *t = e.GetTargetElement();
		const Rml::String what = t ? t->GetAttribute<Rml::String>( "data-slide", "" ) : "";
		if ( !what.empty() ) {
			Typed( "panel", { "light_slide", what, Num( e.GetParameter<float>( "value", 0 ) ) }, nullptr );
		}
	}
};
SlideListener slideListener;

// ---------------------------------------------------------------- the inspector

void BuildInspector( Rml::ElementDocument *doc ) {
	oz::ed::Session &s = session();
	const std::vector<oz::ed::FaceRef> faces = s.target_faces();
	const std::vector<oz::ed::Id> patches = s.target_patches();
	char key[200];
	snprintf( key, sizeof key, "%llu|%llu|%d|%s", (unsigned long long)s.sel_rev, (unsigned long long)s.doc.revision, (int)s.tool, s.shader.c_str() );
	if ( key == lastInspect ) {
		return;
	}
	const oz::ed::Doc &d = s.doc;

	// what's selected
	Rml::String what;
	if ( s.tool == oz::ed::Tool::Faces ) {
		what = faces.empty() ? "Faces tool: click faces to pick them" : std::to_string( faces.size() ) + ( faces.size() == 1 ? " face picked" : " faces picked" );
	} else if ( s.sel.empty() ) {
		what = "Nothing is selected: the basics, then the map's own keys";
	} else {
		int b = 0, p = 0, e = 0;
		for ( oz::ed::Id id : s.sel ) {
			const oz::ed::Object *o = d.get( id );
			if ( o ) {
				( o->kind == oz::ed::Kind::Brush ? b : o->kind == oz::ed::Kind::Patch ? p : e )++;
			}
		}
		auto add = [&]( int n, const char *one, const char *many ) {
			if ( n ) {
				what += ( what.empty() ? "" : ", " ) + std::to_string( n ) + " " + ( n == 1 ? one : many );
			}
		};
		add( b, "brush", "brushes" );
		add( p, "curve", "curves" );
		add( e, "entity", "entities" );
		what = "Selected: " + what;
	}
	// the selected brushes all detail: the button lit; their collision ticked unless every one is off
	{
		int brushes = 0, detail = 0, off = 0;
		for ( oz::ed::Id id : s.sel ) {
			const oz::ed::Object *o = d.get( id );
			if ( o && o->kind == oz::ed::Kind::Brush ) {
				brushes++;
				detail += oz::ed::is_detail( *o );
				off += oz::ed::is_nocollide( *o );
			}
		}
		if ( Rml::Element *e = doc->GetElementById( "ed-detail" ) ) {
			e->SetClass( "on", brushes && detail == brushes );
		}
		if ( Rml::Element *e = doc->GetElementById( "ed-collide" ) ) {
			e->SetClass( "ticked", brushes && off < brushes );
			e->SetClass( "mixed", off && off < brushes );
		}
		SetRml( doc, "ed-collide-note", !brushes ? Rml::String()
			: off == 0 ? Rml::String( "Players stand on it and bump into it" )
			: off == brushes ? Rml::String( "Off: players and shots pass through (washed blue in the view)" )
			: Rml::String( std::to_string( off ) + " of " + std::to_string( brushes ) + " off: a click turns them all on" ) );
		if ( brushes && detail ) {
			what += detail == brushes ? " (detail)" : " (" + std::to_string( detail ) + " detail)";
		}
	}
	SetRml( doc, "ed-what", Enc( what ) );

	// the material on what's selected
	std::set<std::string> on;
	for ( const oz::ed::FaceRef &r : faces ) {
		on.insert( oz::ed::shader_name( d.get( r.first )->brush.faces[size_t( r.second )].shader ) );
	}
	for ( oz::ed::Id id : patches ) {
		on.insert( oz::ed::shader_name( d.get( id )->patch.shader ) );
	}
	Show( doc, "sec-material", !on.empty() );
	// Color: the faces' own when they share one; a dot for each of the palette's, the hues, none
	{
		std::set<int> tints;
		for ( const oz::ed::FaceRef &r : faces ) {
			unsigned char rgb[3];
			const oz::ed::Face &f = d.get( r.first )->brush.faces[size_t( r.second )];
			tints.insert( oz::ed::face_tint( f, rgb ) ? ( rgb[0] << 16 ) | ( rgb[1] << 8 ) | rgb[2] : -1 );
		}
		const int now = tints.size() == 1 ? *tints.begin() : -2;  // -1 none, -2 mixed
		auto dot = [&]( double r, double g, double b, const Rml::String &name ) {
			const int v = ( int( std::round( r * 255 ) ) << 16 ) | ( int( std::round( g * 255 ) ) << 8 ) | int( std::round( b * 255 ) );
			char rgb[48], hex[16];
			snprintf( rgb, sizeof rgb, "%g %g %g", std::round( r * 100 ) / 100, std::round( g * 100 ) / 100, std::round( b * 100 ) / 100 );
			snprintf( hex, sizeof hex, "#%06x", v );
			return "<button class=\"cchip" + Rml::String( now == v ? " on" : "" ) + "\" style=\"background-color: " + hex + ";\" data-hint=\"Color: " +
				name + " (" + rgb + "): the texture is multiplied by it\" onclick=\"append ed tint " + rgb + "\"></button>";
		};
		Rml::String rml = "<button class=\"cchip none" + Rml::String( now == -1 ? " on" : "" ) +
			"\" data-hint=\"No color: the texture as it is\" onclick=\"append ed tint none\"></button>";
		for ( double sat : { 0.75, 0.45, 0.2 } ) {
			for ( int h = 0; h < 360; h += 30 ) {
				const double c = sat, x = c * ( 1 - std::fabs( std::fmod( h / 60.0, 2 ) - 1 ) ), m = 1 - c;
				double r = 0, g = 0, b = 0;
				switch ( h / 60 ) {
				case 0: r = c, g = x; break;
				case 1: r = x, g = c; break;
				case 2: g = c, b = x; break;
				case 3: g = x, b = c; break;
				case 4: r = x, b = c; break;
				default: r = c, b = x; break;
				}
				rml += dot( r + m, g + m, b + m, std::to_string( h ) + " degrees" );
			}
		}
		for ( double v : { 0.75, 0.5, 0.3 } ) {
			rml += dot( v, v, v, "gray" );
		}
		SetRml( doc, "ed-tints", faces.empty() ? Rml::String( "<p>Pick faces or select brushes to color them</p>" ) : rml );
	}
	if ( !on.empty() ) {
		SetRml( doc, "ed-face-mat", Enc( on.size() == 1 ? *on.begin() : std::to_string( on.size() ) + " materials" ) );
		SetSwatch( doc, "ed-face-sw", on.size() == 1 ? *on.begin() : "", 3.8 );
		SetRml( doc, "ed-current", Enc( Lower( *on.begin() ) == Lower( s.shader ) && on.size() == 1 ? "The current material" : "Current: " + s.shader ) );
	}

	// the texture mapping: each number where every face has the same, else blank (a curve's: the buttons)
	Show( doc, "sec-mapping", !faces.empty() || !patches.empty() );
	if ( faces.empty() && !patches.empty() && !TypingInside( doc->GetElementById( "sec-mapping" ) ) ) {
		static const char *fields[5] = { "ed-tex-ss", "ed-tex-st", "ed-tex-rot", "ed-tex-sc", "ed-tex-tc" };
		for ( const char *f : fields ) {
			SetInput( doc, f, "" );
		}
		const oz::ed::Patch &pt = d.get( patches[0] )->patch;
		SetRml( doc, "ed-tex-note", Enc( std::to_string( patches.size() ) + ( patches.size() == 1 ? " curve" : " curves" ) + " (" +
			std::to_string( pt.width ) + " x " + std::to_string( pt.height ) + " control points): the buttons map the texture along it; the numbers are a brush face's" ) );
	}
	if ( !faces.empty() && !TypingInside( doc->GetElementById( "sec-mapping" ) ) ) {
		double v[5] = {};
		bool same[5] = { true, true, true, true, true }, first = true;
		int skewed = 0;
		std::set<int> forms;
		for ( const oz::ed::FaceRef &r : faces ) {
			const oz::ed::Object *o = d.get( r.first );
			const oz::ed::Face &f = o->brush.faces[size_t( r.second )];
			const std::vector<oz::ed::Winding> &ws = d.windings( r.first );
			forms.insert( (int)f.form );
			oz::ed::TexFields tf;
			if ( !oz::ed::tex_fields( d, f, size_t( r.second ) < ws.size() ? ws[size_t( r.second )] : oz::ed::Winding{}, oz::ed::origin_for( d, *o ), tf ) ) {
				skewed++;
				for ( bool &b : same ) {
					b = false;
				}
				continue;
			}
			for ( int i = 0; i < 5; i++ ) {
				if ( first ) {
					v[i] = tf.v[i];
				} else if ( std::fabs( v[i] - tf.v[i] ) > 1e-6 ) {
					same[i] = false;
				}
			}
			first = false;
		}
		static const char *ids[5] = { "ed-tex-ss", "ed-tex-st", "ed-tex-rot", "ed-tex-sc", "ed-tex-tc" };
		for ( int i = 0; i < 5; i++ ) {
			const bool show = same[i] && !first;
			SetInput( doc, ids[i], show ? Num( std::fabs( v[i] ) < 1e-9 ? 0 : v[i] ) : "" );
			if ( Rml::Element *e = doc->GetElementById( ids[i] ) ) {
				e->SetAttribute( "placeholder", show ? "" : "mixed" );
			}
		}
		static const char *formNames[3] = { "Quake", "brushDef", "Valve 220" };
		Rml::String note = std::to_string( faces.size() ) + ( faces.size() == 1 ? " face, " : " faces, " );
		bool firstForm = true;
		for ( int f : forms ) {
			note += Rml::String( firstForm ? "" : " and " ) + formNames[f];
			firstForm = false;
		}
		note += " numbers";
		if ( skewed ) {
			note += ". " + std::to_string( skewed ) + " skewed: their numbers can't show, the buttons still work";
		}
		SetRml( doc, "ed-tex-note", Enc( note ) );
	}

	Show( doc, "sec-transform", !s.sel.empty() && s.tool != oz::ed::Tool::Faces );
	Show( doc, "sec-help", s.sel.empty() && s.faces.empty() );
	Show( doc, "sec-keyhelp", s.sel.empty() && s.faces.empty() );
	// carving and curves for the selected brushes, reshaping for the selected curves
	bool brushes = false, curves = false;
	for ( oz::ed::Id id : s.sel ) {
		if ( const oz::ed::Object *o = d.get( id ) ) {
			brushes |= o->kind == oz::ed::Kind::Brush;
			curves |= o->kind == oz::ed::Kind::Patch;
		}
	}
	Show( doc, "sec-shape", brushes && s.tool != oz::ed::Tool::Faces );
	Show( doc, "sec-curve", curves && s.tool != oz::ed::Tool::Faces );

	// the keys of the selection's entities: each key's value where they all have the same
	Rml::Element *keysBox = doc->GetElementById( "ed-keys" );
	if ( keysBox && !TypingInside( keysBox ) ) {
		const std::vector<oz::ed::Id> ents = s.key_entities();
		std::vector<std::string> order;
		std::map<std::string, std::string> value;
		std::map<std::string, int> count;
		std::set<std::string> differ;
		for ( oz::ed::Id e : ents ) {
			const oz::ed::Object *o = d.get( e );
			if ( !o ) {
				continue;
			}
			std::set<std::string> seen;
			for ( const auto &kv : o->entity.keys ) {
				if ( !seen.insert( kv.first ).second ) {
					continue;	// a repeat shows once (the first, which a set changes)
				}
				if ( !count.count( kv.first ) ) {
					order.push_back( kv.first );
					value[kv.first] = kv.second;
				} else if ( value[kv.first] != kv.second ) {
					differ.insert( kv.first );
				}
				count[kv.first]++;
			}
		}
		const oz::ed::Object *one = ents.size() == 1 ? d.get( ents[0] ) : nullptr;

		// the class, when they share one: what it is, its spawnflags, the keys it takes that aren't set
		std::string cn = one ? one->entity.classname() : "";
		for ( oz::ed::Id e : ents ) {
			if ( const oz::ed::Object *o = d.get( e ) ) {
				cn = cn.empty() || cn == o->entity.classname() ? o->entity.classname() : "*";
			}
		}
		const oz::ed::EntityDef *def = cn == "*" ? nullptr : oz::ed::entity_def( cn );
		// the title in words (the class name leads the line under it, for those who know them)
		Rml::String title = ents.size() > 1 ? ( cn == "*" ? "KEYS OF " + std::to_string( ents.size() ) + " ENTITIES"
			: std::to_string( ents.size() ) + " x " + EntityTitle( d, ents[0] ) )
			: ents.size() == 1 && ents[0] == d.worldspawn() ? Rml::String( "MAP KEYS" ) : one ? EntityTitle( d, ents[0] ) : "KEYS";
		SetRml( doc, "ed-ent-title", Enc( title ) );
		SetRml( doc, "ed-ent-help", def ? Enc( ( cn == "worldspawn" || Rml::StringUtilities::ToUpper( cn ) == title ? "" : cn + ": " ) + def->help + ( def->mod.empty() ? "" : " (" + def->mod + ")" ) )
			: Rml::String() );
		// a jump pad's throw, in numbers, or why it does nothing: the pad's own (its pieces, or its target, selected)
		oz::ed::Id pad = 0;
		for ( oz::ed::Id e : ents ) {
			const std::string c = d.get( e ) ? d.get( e )->entity.classname() : "";
			if ( c == "trigger_push" || c == "target_push" ) {
				pad = e;
				break;
			}
		}
		for ( size_t i = 0; !pad && i < ents.size(); i++ ) {
			const std::string *n = d.get( ents[i] ) ? d.get( ents[i] )->entity.find( "targetname" ) : nullptr;
			const std::vector<oz::ed::Id> ps = n ? s.pushers_at( *n ) : std::vector<oz::ed::Id>{};
			pad = ps.empty() ? 0 : ps[0];
		}
		Show( doc, "ed-push", pad != 0 );
		if ( pad ) {
			double speed = 0, up = 0, across = 0, secs = 0;
			const std::string problem = s.push_problem( pad );
			if ( !problem.empty() ) {
				SetRml( doc, "ed-push-text", "<span class=\"bad\">" + Enc( problem ) + "</span>" );
			} else if ( s.push_flight( pad, speed, up, across, secs ) ) {
				SetRml( doc, "ed-push-text", Enc( "The throw: " + Num( std::round( speed ) ) + " a second, " + Num( std::round( up ) ) + " up, " +
					Num( std::round( across ) ) + " across, " + Num( std::round( secs * 10 ) / 10 ) + " s in the air" ) );
			} else {
				SetRml( doc, "ed-push-text", "" );
			}
		}
		// a mover: how a door opens, a lift's travel, a train's path, played
		{
			std::string mover;
			for ( oz::ed::Id e : ents ) {
				const std::string c = d.get( e ) ? d.get( e )->entity.classname() : "";
				if ( c == "func_door" || c == "func_plat" || c == "func_button" || c == "func_train" || c == "func_rotating" || c == "func_bobbing" ||
						c == "func_pendulum" ) {
					mover = mover.empty() || mover == c ? c : "*";
				}
			}
			Rml::String m;
			auto check = [&]( const char *id, const Rml::String &label, bool on, bool mixed, const char *verb, const Rml::String &hint ) {
				m += "<button class=\"check" + Rml::String( on ? " ticked" : "" ) + ( mixed ? " mixed" : "" ) + "\" id=\"" + id + "\" data-hint=\"" + hint +
					"\" onclick=\"append ed " + verb + "\"><span class=\"tick\"></span><span class=\"lbl2\">" + label + "</span></button>";
			};
			if ( mover == "func_door" ) {
				const oz::ed::Session::DoorState ds = s.door_state();
				m += "<div class=\"sub\">OPENS WHEN</div><div class=\"row\">";
				check( "ed-door-near", "A PLAYER COMES NEAR", ds.near, ds.near_mixed, "door_near",
					"A trigger around the doorway, 120 units out each side: walking up to it opens it" );
				check( "ed-door-shot", "IT'S SHOT", ds.shot, ds.shot_mixed, "door_shot", "Any shot that hits it opens it" );
				m += "</div><div class=\"row\"><button class=\"tb\" data-action=\"door_button\" onclick=\"append ed door_button\">ADD BUTTON</button>";
				m += "<span class=\"note\">" + Enc( ds.buttons ? std::to_string( ds.buttons ) + ( ds.buttons == 1 ? " button or trigger fires it" : " buttons or triggers fire it" )
					: !ds.team.empty() ? "Its halves open together (team " + ds.team + ")" : "The arrows above: which way it slides" ) + "</span></div>";
			} else if ( mover == "func_plat" ) {
				m += "<div class=\"sub\">TRAVEL</div><div class=\"row\"><button class=\"tb\" onclick=\"append ed lift_travel -16\">-</button>"
					"<button class=\"tb\" onclick=\"append ed lift_travel +16\">+</button><span class=\"note\">Built at its top: the outline shows its bottom</span></div>";
			} else if ( mover == "func_train" ) {
				m += "<div class=\"row\"><button class=\"tb\" data-action=\"train_path\" onclick=\"append ed train_path\">LAY PATH</button>"
					"<span class=\"note\">Click its corners in order; the first again closes the loop</span></div>";
			}
			if ( !mover.empty() ) {
				m += "<div class=\"row\"><button class=\"tb" + Rml::String( s.mover_play ? " on" : "" ) + "\" data-action=\"mover_play\" onclick=\"append ed mover_play\">" +
					( s.mover_play ? "STOP" : "PLAY MOTION" ) + "</button><span class=\"note\">The outline shows where it moves to</span></div>";
			}
			SetRml( doc, "ed-mover", m );
			Show( doc, "ed-mover", !m.empty() );
		}
		// a jump pad's pieces selected together: named as one thing
		bool padGroup = ents.size() > 1;
		for ( oz::ed::Id e : ents ) {
			padGroup &= d.get( e ) && !d.get( e )->entity.get( oz::ed::JUMPPAD_KEY ).empty();
		}
		if ( padGroup ) {
			SetRml( doc, "ed-ent-title", "JUMP PAD" );
			SetRml( doc, "ed-ent-help", Enc( "Its pad, trigger and target, moved and copied together. A double-click takes one piece alone (its keys, or the target to drag)." ) );
		}
		Rml::String flags;
		if ( def && ents[0] != d.worldspawn() ) {
			for ( const oz::ed::FlagDef &f : def->flags ) {
				if ( cn == "light" && f.bit == 1 ) {
					continue;	// linear falloff: the Range slider's own
				}
				bool on = true;
				for ( oz::ed::Id e : ents ) {
					on &= ( atoi( d.get( e )->entity.get( "spawnflags", "0" ).c_str() ) & f.bit ) != 0;
				}
				const std::string what = cn == "light" && f.bit == 2 ? "Walls the light meets at a slant are lit as brightly as walls facing it. " : "";
				flags += "<button class=\"chip flag" + Rml::String( on ? " on" : "" ) + "\" data-hint=\"" + Enc( what ) + "Spawnflag " + std::to_string( f.bit ) +
					": click turns it " + ( on ? "off" : "on" ) + "\" onclick=\"append ed flag " + std::to_string( f.bit ) + "\">" +
					Enc( Rml::StringUtilities::ToUpper( f.label ) ) + "</button>";
			}
		}
		SetRml( doc, "ed-flags", flags );
		Rml::String adds;
		if ( def || ( !ents.empty() && ents[0] == d.worldspawn() ) ) {
			std::vector<oz::ed::KeyDef> known = def ? def->keys : std::vector<oz::ed::KeyDef>{};
			if ( ents[0] != d.worldspawn() ) {
				known.insert( known.end(), oz::ed::common_keys().begin(), oz::ed::common_keys().end() );
			}
			// a light: its sliders and spotlight button stand for its own keys, and the match filters never
			// reach it (the compile bakes it into the map; the game never spawns it)
			const bool light = cn == "light";
			const bool baked = light || cn == "info_null" || cn == "misc_model";	// the compile takes these out of the map
			std::set<std::string> listed;
			Rml::String filters;
			for ( const oz::ed::KeyDef &k : known ) {
				if ( count.count( k.name ) || !listed.insert( k.name ).second ) {
					continue;
				}
				const bool filter = MatchFilter( k.name );
				if ( ents[0] == d.worldspawn() && ( k.name == "ambient" || k.name == "_color" ) ) {
					continue;	// Sky and sun's ambient light and its color
				}
				if ( ( baked && filter ) || ( cn == "info_null" && k.name == "target" ) ) {
					continue;
				}
				if ( light && ( filter || k.name == "light" || k.name == "_color" || k.name == "radius" || k.name == "target" || k.name == "targetname" ) ) {
					continue;
				}
				( filter ? filters : adds ) += "<button class=\"chip\" data-hint=\"" + Enc( k.name + ": " + k.help +
					( k.def.empty() ? "" : " (the game's default: " + k.def + ")" ) ) + "\" onclick=\"append ed key_new " + Enc( k.name ) + "\">+ " +
					Enc( ChipLabel( k.name ) ) + "</button>";
			}
			if ( !filters.empty() ) {
				adds += "<div class=\"chipsub\">ONLY IN SOME MATCHES</div><p class=\"chipnote\">Leave it out of some game types, or keep it to some</p>" + filters;
			}
		}
		if ( ents.size() == 1 && ents[0] == d.worldspawn() ) {
			// the map's own keys: its sky and light have a dialog of their own
			adds = "<button class=\"chip on\" data-action=\"sky_sun\" onclick=\"append ed sky_sun\">SKY AND SUN</button>" + adds;
		}
		SetRml( doc, "ed-addkeys", adds );
		bool owned = false;
		for ( oz::ed::Id id : s.sel ) {
			const oz::ed::Object *o = d.get( id );
			owned |= o && o->kind != oz::ed::Kind::Entity && o->owner != d.worldspawn();
		}
		Show( doc, "ed-toworld", owned );

		// a light: its sliders over its keys
		// a light: its controls over its keys, which fold away under Advanced (all of them, with Add key)
		const bool fold = cn == "light" && !ents.empty();
		Rml::String rml = fold ? LightSliders( d.get( ents[0] ) ) : Rml::String();
		if ( fold ) {
			rml += "<div class=\"row\"><button class=\"tb adv\" id=\"ed-advanced\" data-hint=\"" +
				Rml::String( advancedOpen ? "Hides the light's keys again" : "Every key the light has, as the .map file writes them, to change by hand" ) +
				"\" onclick=\"append ed panel advanced\">" + ( advancedOpen ? "- " : "+ " ) + "ADVANCED: ALL KEYS (" + std::to_string( order.size() ) + ")</button></div>";
		}
		const bool rawKeys = !fold || advancedOpen;
		Show( doc, "ed-addrow", rawKeys );
		Show( doc, "ed-addrow2", rawKeys );
		shownKeys = order;
		for ( size_t i = 0; rawKeys && i < order.size(); i++ ) {
			const std::string &k = order[i];
			const bool mixed = differ.count( k ) || count[k] < int( ents.size() );
			const Rml::String idx = std::to_string( i );
			const oz::ed::KeyDef *kd = cn == "*" ? nullptr : oz::ed::key_def( cn, k );
			const Rml::String hint = kd ? " data-hint=\"" + Enc( k + ": " + kd->help ) + "\"" : Rml::String();
			// the remove button first: a text field takes Left and Right, so the d-pad reaches a row's
			// buttons from the rows above and below
			rml += "<div class=\"row kv\">" + ( k == "classname" ? Rml::String( "<span class=\"nox\"></span>" )
				: "<button class=\"tb x\" data-action=\"key_remove\" onclick=\"append ed panel key_remove_at " + idx + "\">X</button>" ) +
				"<input type=\"text\" class=\"text key\" id=\"ed-k-" + idx + "\"" + hint + " value=\"" + Enc( k ) +
				"\" data-apply=\"key_name_typed\" data-key-index=\"" + idx + "\" data-action=\"key_rename\"/><input type=\"text\" class=\"text val\" id=\"ed-v-" + idx +
				"\"" + hint + " value=\"" + ( mixed ? "" : Enc( value[k] ) ) + "\"" + ( mixed ? " placeholder=\"(differs)\"" : "" ) + " data-apply=\"key_typed\" data-key-index=\"" + idx +
				"\" data-action=\"key_set\"/></div>";
			if ( kd && !( fold && k == "_color" ) ) {	// a light's color is with its controls
				rml += TypedControls( k, *kd, mixed ? "" : value[k] );
			}
		}
		if ( rml != lastKeysRml ) {
			lastKeysRml = rml;
			keysBox->SetInnerRML( rml );
		}
		lastInspect = key;
	}
}

// ---------------------------------------------------------------- the entities

// Every class the editor knows, by the catalog's sections, as tiles: a pickup's icon, else its color
// and name. A click arms the place tool with it (a brush class with brushes selected makes them one).
void BuildEntities( Rml::ElementDocument *doc ) {
	const oz::ed::Session &s = session();
	const std::string search = Lower( InputValue( doc, "ed-esearch" ) );
	const Rml::String key = search + "|" + s.place_class + "|" + std::to_string( (int)s.tool ) + "|" + std::to_string( oz::ed::entity_defs().size() ) + "|" +
		std::to_string( s.prefab_rev );
	if ( key == lastEntities ) {
		return;
	}
	lastEntities = key;
	// the saved prefabs: a click puts one where the pointer is (or by the selection)
	Rml::String prefabs;
	for ( const std::string &n : s.prefab_names() ) {
		prefabs += "<button class=\"chip\" data-hint=\"Puts the prefab " + Enc( n ) + " in the map, where the pointer is\" onclick=\"append ed prefab_insert " +
			Enc( n ) + "\">" + Enc( n ) + "</button>";
	}
	SetRml( doc, "ed-prefabs", prefabs.empty() ? Rml::String( "<p>None yet: select brushes and entities, name it, Save selection.</p>" ) : prefabs );
	// by section, in the order the catalog first names them (a mod's class joins its section)
	std::vector<std::string> sections;
	std::map<std::string, std::vector<const oz::ed::EntityDef *>> in;
	for ( const oz::ed::EntityDef &d : oz::ed::entity_defs() ) {
		if ( d.section.empty() ) {
			continue;	// worldspawn, func_group: not placed
		}
		if ( !search.empty() && Lower( d.name + " " + d.help + " " + d.mod + " " + d.section ).find( search ) == std::string::npos ) {
			continue;
		}
		if ( !in.count( d.section ) ) {
			sections.push_back( d.section );
		}
		in[d.section].push_back( &d );
	}
	std::vector<const oz::ed::EntityDef *> order;
	for ( const std::string &sec : sections ) {
		order.insert( order.end(), in[sec].begin(), in[sec].end() );
	}
	Rml::String rml, section;
	int shown = 0;
	for ( const oz::ed::EntityDef *dp : order ) {
		const oz::ed::EntityDef &d = *dp;
		if ( d.section != section ) {
			if ( !section.empty() ) {
				rml += "</div>";
			}
			section = d.section;
			rml += "<div class=\"esec\">" + Enc( Rml::StringUtilities::ToUpper( section ) ) + "</div><div class=\"mats\">";
		}
		Rml::String sw, style;
		static std::map<std::string, bool> iconThere;	// CPMA's green armor's is in CPMA's pak alone
		if ( !d.icon.empty() && !iconThere.count( d.icon ) ) {
			void *buf = nullptr;
			iconThere[d.icon] = OzE_ReadFile( ( d.icon + ".tga" ).c_str(), &buf ) > 0;
			if ( buf ) {
				OzE_FreeFile( buf );
			}
		}
		if ( !d.icon.empty() && iconThere[d.icon] ) {
			// the pickup's icon whole, as the HUD draws it: its shape is in its alpha
			sw = "<img src=\"/" + Enc( d.icon ) + "\" style=\"width: 3.4rem; height: 3.4rem;\"/>";
		} else {
			char c[64];
			snprintf( c, sizeof c, "background-color: #%02x%02x%02x;", int( d.color.x * 200 ), int( d.color.y * 200 ), int( d.color.z * 200 ) );
			style = c;
			sw = "<span>" + Enc( Rml::StringUtilities::ToUpper( d.brush ? "brush" : "point" ) ) + "</span>";
		}
		// the name past its prefix (item_armor_body: armor_body), but whole where the prefix is what tells two
		// apart (trigger_push and target_push, func_door, misc_model), with a space for the underscore
		const size_t us = d.name.find( '_' );
		const std::string prefix = us == std::string::npos ? "" : d.name.substr( 0, us );
		std::string shortName = prefix == "trigger" || prefix == "target" || prefix == "func" || prefix == "misc" || prefix == "shooter"
			? d.name : d.name.substr( us == std::string::npos ? 0 : us + 1 );
		if ( prefix == "trigger" || prefix == "target" || prefix == "func" || prefix == "misc" || prefix == "shooter" ) {
			shortName[us] = ' ';
		}
		const bool on = s.tool == oz::ed::Tool::Place && s.place_class == d.name;
		rml += "<div class=\"mat" + Rml::String( on ? " on" : "" ) + "\" data-hint=\"" + Enc( d.name + ": " + d.help + ( d.mod.empty() ? "" : " (" + d.mod + ")" ) +
			( d.brush ? ". Click, then click in the view for a 64-unit brush of it; with brushes selected, makes them one" : ". Click, then where it goes" ) ) +
			"\" onclick=\"append ed place " + Enc( d.name ) + "\"><div class=\"sw" + ( style.empty() ? "" : " tool" ) + "\"" +
			( style.empty() ? "" : " style=\"" + style + "\"" ) + ">" + sw + "</div>" + ( d.mod.empty() ? "" : "<span class=\"badge\">" + Enc( Rml::StringUtilities::ToUpper( d.mod ) ) + "</span>" ) +
			"<span class=\"mn\">" + Enc( shortName ) + "</span></div>";
		shown++;
	}
	if ( !section.empty() ) {
		rml += "</div>";
	}
	if ( !shown ) {
		rml = "<p>No class matches.</p>";
	}
	SetRml( doc, "ed-ents", rml );
}

// ---------------------------------------------------------------- the outliner

struct Row {
	oz::ed::Id id;
	int depth;
	bool toggle, open;
	std::string cls, sub, count;
};

// A brush's main material: the one most of its faces have, tool shaders last.
std::string BrushMaterial( const oz::ed::Object &o ) {
	std::map<std::string, int> n;
	for ( const oz::ed::Face &f : o.brush.faces ) {
		n[oz::ed::shader_name( f.shader )]++;
	}
	std::string best;
	int most = -1;
	for ( const auto &kv : n ) {
		const int score = kv.second - ( kv.first.compare( 0, 7, "common/" ) == 0 ? 1000 : 0 );
		if ( score > most ) {
			best = kv.first, most = score;
		}
	}
	return best;
}

Row ObjectRow( const oz::ed::Doc &d, oz::ed::Id id, int depth ) {
	const oz::ed::Object *o = d.get( id );
	Row r{ id, depth, false, false, "", "", "" };
	if ( o->kind == oz::ed::Kind::Brush ) {
		r.cls = "brush";
		r.sub = BrushMaterial( *o );
	} else if ( o->kind == oz::ed::Kind::Patch ) {
		r.cls = "curve";
		r.sub = oz::ed::shader_name( o->patch.shader );
	} else {
		const oz::ed::Entity &e = o->entity;
		r.cls = e.classname();
		for ( const char *k : { "targetname", "target", "model", "message", "music" } ) {
			const std::string *v = e.find( k );
			if ( v && !v->empty() && !( !strcmp( k, "model" ) && (*v)[0] == '*' ) ) {
				r.sub = *v;
				break;
			}
		}
	}
	r.count = "#" + std::to_string( id );
	return r;
}

void BuildObjects( Rml::ElementDocument *doc ) {
	const oz::ed::Session &s = session();
	const oz::ed::Doc &d = s.doc;
	const std::string filter = Lower( InputValue( doc, "ed-ofilter" ) );
	// the selection changed: its first object's owner opened, its page shown, its row scrolled to
	static uint64_t followed = ~0ull;
	oz::ed::Id follow = 0;
	if ( s.sel_rev != followed ) {
		followed = s.sel_rev;
		if ( !s.sel.empty() ) {
			follow = *s.sel.begin();
			const oz::ed::Object *o = d.get( follow );
			if ( o && o->kind != oz::ed::Kind::Entity ) {
				expanded.insert( o->owner );
			}
		}
	}
	Rml::String key = std::to_string( d.revision ) + "|" + std::to_string( s.sel_rev ) + "|" + std::to_string( s.show_rev ) + "|" + filter + "|" +
		std::to_string( objPage ) + "|";
	for ( int e : expanded ) {
		key += std::to_string( e ) + ",";
	}
	if ( key == lastObjects ) {
		return;
	}
	lastObjects = key;

	std::vector<Row> rows;
	const oz::ed::Id world = d.worldspawn();
	if ( filter.empty() ) {
		// the world, then every entity in file order; the open ones' brushes and curves under them
		const std::vector<oz::ed::Id> worldOwn = d.owned( world );
		int brushes = 0, curves = 0;
		for ( oz::ed::Id id : worldOwn ) {
			( d.get( id )->kind == oz::ed::Kind::Brush ? brushes : curves )++;
		}
		Row w{ world, 0, true, expanded.count( world ) != 0, "World", "", std::to_string( brushes ) + " brushes, " + std::to_string( curves ) + " curves" };
		rows.push_back( w );
		if ( w.open ) {
			for ( oz::ed::Id id : worldOwn ) {
				rows.push_back( ObjectRow( d, id, 1 ) );
			}
		}
		for ( oz::ed::Id e : d.ids_of( oz::ed::Kind::Entity ) ) {
			if ( e == world ) {
				continue;
			}
			Row r = ObjectRow( d, e, 0 );
			const std::vector<oz::ed::Id> own = d.owned( e );
			if ( !own.empty() ) {
				r.toggle = true;
				r.open = expanded.count( e ) != 0;
				r.count = std::to_string( own.size() ) + ( own.size() == 1 ? " brush" : " brushes" );
			}
			rows.push_back( r );
			if ( r.open ) {
				for ( oz::ed::Id id : own ) {
					rows.push_back( ObjectRow( d, id, 1 ) );
				}
			}
		}
	} else {
		// everything that matches, in file order
		for ( oz::ed::Id id : d.ids() ) {
			if ( id == world ) {
				continue;
			}
			const oz::ed::Object *o = d.get( id );
			bool match = false;
			if ( o->kind == oz::ed::Kind::Entity ) {
				for ( const auto &kv : o->entity.keys ) {
					match |= Lower( kv.second ).find( filter ) != std::string::npos;
				}
			} else if ( o->kind == oz::ed::Kind::Brush ) {
				for ( const oz::ed::Face &f : o->brush.faces ) {
					match |= Lower( oz::ed::shader_name( f.shader ) ).find( filter ) != std::string::npos;
				}
			} else {
				match = Lower( oz::ed::shader_name( o->patch.shader ) ).find( filter ) != std::string::npos;
			}
			if ( match ) {
				rows.push_back( ObjectRow( d, id, 0 ) );
			}
		}
	}

	// which rows are selected: an entity's own, a brush's own (the faces tool: its faces' brushes)
	std::set<oz::ed::Id> picked( s.sel.begin(), s.sel.end() );
	for ( const oz::ed::FaceRef &f : s.faces ) {
		picked.insert( f.first );
	}
	const int pages = std::max( 1, int( ( rows.size() + OBJ_PAGE - 1 ) / OBJ_PAGE ) );
	for ( size_t i = 0; follow && i < rows.size(); i++ ) {
		if ( rows[i].id == follow ) {
			objPage = int( i / OBJ_PAGE );
			break;
		}
	}
	objPage = std::min( objPage, pages - 1 );
	Rml::String rml;
	for ( size_t i = size_t( objPage ) * OBJ_PAGE; i < rows.size() && i < size_t( objPage + 1 ) * OBJ_PAGE; i++ ) {
		const Row &r = rows[i];
		const Rml::String id = std::to_string( r.id );
		const bool isWorld = r.id == world;
		// hidden or locked: dimmed, marked
		const bool shown = isWorld || s.visible( r.id ), free = isWorld || s.pickable( r.id );
		const Rml::String badge = !shown ? "HIDDEN" : !free ? "LOCKED" : "";
		rml += "<div class=\"orow" + Rml::String( picked.count( r.id ) ? " on" : "" ) + ( shown ? "" : " hid" ) + "\" id=\"ed-orow-" + id + "\" data-hint=\"" +
			( isWorld ? Rml::String( "The world's brushes and curves: click to show or hide them" ) : Rml::String( "Click selects (Shift adds, Ctrl toggles); a double-click frames it" ) ) +
			"\" onclick=\"append ed " + ( isWorld ? "panel expand " + id : "row " + id ) + "\"" + ( isWorld ? "" : " ondblclick=\"append ed frame_ids " + id + "\"" ) + ">";
		for ( int k = 0; k < r.depth; k++ ) {
			rml += "<span class=\"in\"></span>";
		}
		rml += "<span class=\"tog\"" + ( r.toggle && !isWorld ? " onclick=\"append ed panel expand " + id + "\"" : Rml::String() ) + ">" +
			( r.toggle ? ( r.open ? "-" : "+" ) : "" ) + "</span><span>" + Enc( r.cls ) + "</span>" +
			( r.sub.empty() ? "" : "<span class=\"sub\">" + Enc( r.sub ) + "</span>" ) + ( badge.empty() ? "" : "<span class=\"ostate\">" + badge + "</span>" ) +
			"<span class=\"cnt\">" + Enc( r.count ) + "</span></div>";
	}
	if ( rows.empty() ) {
		rml = "<p>Nothing matches.</p>";
	}
	SetRml( doc, "ed-objects", rml );
	if ( follow ) {
		if ( Rml::Element *row = doc->GetElementById( "ed-orow-" + std::to_string( follow ) ) ) {
			row->ScrollIntoView( Rml::ScrollIntoViewOptions( Rml::ScrollAlignment::Nearest ) );
		}
	}
	Rml::String pager;
	if ( pages > 1 ) {
		pager = "<button class=\"tb\" onclick=\"append ed panel opage -1\">PREVIOUS</button><span>" + std::to_string( objPage + 1 ) + " / " +
			std::to_string( pages ) + "</span><button class=\"tb\" onclick=\"append ed panel opage 1\">NEXT</button>";
	}
	SetRml( doc, "ed-opager", pager );
}

// ---------------------------------------------------------------- the history

void BuildHistory( Rml::ElementDocument *doc ) {
	const oz::ed::Doc &d = session().doc;
	const Rml::String key = std::to_string( d.revision ) + "|" + std::to_string( d.undo_size() ) + "|" + std::to_string( d.redo_size() );
	if ( key == lastHistory ) {
		return;
	}
	lastHistory = key;
	const std::vector<std::string> done = d.undo_labels(), redo = d.redo_labels();
	auto row = [&]( size_t n, const std::string &label, const char *cls ) {
		return "<div class=\"orow " + Rml::String( cls ) + "\" data-hint=\"" + ( n == done.size() ? Rml::String( "Where the map is now" ) : Rml::String( "Click: the map as it was after this" ) ) +
			"\" onclick=\"append ed history " + std::to_string( n ) + "\"><span class=\"mark\">" + ( n == done.size() ? ">" : "" ) + "</span><span>" + Enc( label ) + "</span></div>";
	};
	Rml::String rml = row( 0, "Opened", done.empty() ? "cur" : "" );
	for ( size_t i = 0; i < done.size(); i++ ) {
		rml += row( i + 1, done[i], i + 1 == done.size() ? "cur" : "" );
	}
	for ( size_t i = 0; i < redo.size(); i++ ) {
		rml += row( done.size() + i + 1, redo[i], "redo" );
	}
	SetRml( doc, "ed-history", rml );
}

// ---------------------------------------------------------------- the d-pad

void CollectFocusable( Rml::Element *e, std::vector<Rml::Element *> &out ) {
	if ( !e->IsVisible() ) {
		return;
	}
	if ( e->GetComputedValues().tab_index() == Rml::Style::TabIndex::Auto && e->GetComputedValues().focus() != Rml::Style::Focus::None ) {
		out.push_back( e );
	}
	for ( int i = 0; i < e->GetNumChildren(); i++ ) {
		CollectFocusable( e->GetChild( i ), out );
	}
}

// The open panel's controls chained for the d-pad, as it's made again: spatial navigation doesn't
// go from the tabs into the scrolling panel, and a text field takes Left and Right. Down and Up go
// a row of buttons or a text field at a time, Left and Right along a row; the tabs lead in.
void ChainPanel( Rml::ElementDocument *doc ) {
	Rml::Element *body = doc->GetElementById( "panel-" + panel );
	if ( !body ) {
		return;
	}
	doc->UpdateDocument();	// what shows now
	std::vector<Rml::Element *> all;
	CollectFocusable( body, all );
	// the stops Down and Up go through: a row of buttons is one (Left and Right go along it), a text
	// field one of its own
	std::vector<std::vector<Rml::Element *>> rows;
	for ( Rml::Element *e : all ) {
		const bool field = e->GetTagName() == "input";
		if ( rows.empty() || field || rows.back().front()->GetTagName() == "input" || rows.back().front()->GetParentNode() != e->GetParentNode() ) {
			rows.emplace_back();
		}
		rows.back().push_back( e );
	}
	static int named;
	auto id = [&]( Rml::Element *e ) {
		if ( e->GetId().empty() ) {
			e->SetId( "ed-nav-" + std::to_string( ++named ) );
		}
		return "#" + e->GetId();
	};
	for ( size_t r = 0; r < rows.size(); r++ ) {
		const Rml::String up = r ? id( rows[r - 1].front() ) : Rml::String( "#tab-" ) + panel;
		const Rml::String down = r + 1 < rows.size() ? id( rows[r + 1].front() ) : Rml::String( "none" );
		for ( size_t i = 0; i < rows[r].size(); i++ ) {
			Rml::Element *e = rows[r][i];
			e->SetProperty( "nav-up", up );
			e->SetProperty( "nav-down", down );
			if ( rows[r].size() > 1 ) {
				// along the row in its order: a row wrapped onto two lines (the keys to add) reads on
				e->SetProperty( "nav-left", i ? id( rows[r][i - 1] ) : Rml::String( "none" ) );
				e->SetProperty( "nav-right", i + 1 < rows[r].size() ? id( rows[r][i + 1] ) : Rml::String( "none" ) );
			}
		}
	}
	static const char *tabs[5] = { "tab-inspect", "tab-materials", "tab-entities", "tab-objects", "tab-history" };
	for ( const char *t : tabs ) {
		if ( Rml::Element *e = doc->GetElementById( t ) ) {
			e->SetProperty( "nav-down", rows.empty() ? Rml::String( "none" ) : id( rows[0].front() ) );
		}
	}
}

// ---------------------------------------------------------------- the search and the keys

std::string Spaced( std::string name ) {
	std::replace( name.begin(), name.end(), '_', ' ' );
	return name;
}

// The actions a query names: each of its words somewhere in an action's label, name, help or keys.
// Those whose label starts with the query come first, then those with its first word in the label or
// name, then the rest; the shorter label first ("Turn left" before "Turn texture left").
std::vector<std::string> Matches( const std::string &query ) {
	std::vector<std::string> words;
	for ( size_t at = 0; at < query.size(); ) {
		size_t sp = query.find( ' ', at );
		sp = sp == std::string::npos ? query.size() : sp;
		if ( sp > at ) {
			words.push_back( query.substr( at, sp - at ) );
		}
		at = sp + 1;
	}
	std::vector<std::pair<int, std::string>> scored;
	for ( const oz::ed::Action &a : oz::ed::actions() ) {
		if ( !oz::ed::runs_alone( a ) ) {
			continue;
		}
		const std::string label = Lower( a.label ), name = Spaced( a.name );
		const std::string text = label + " " + name + " " + Lower( a.help ) + " " + Lower( session().keys_of( a ) );
		bool all = true;
		for ( const std::string &w : words ) {
			all = all && text.find( w ) != std::string::npos;
		}
		if ( !all ) {
			continue;
		}
		int score = 3;
		if ( words.empty() || label.compare( 0, query.size(), query ) == 0 ) {
			score = 0;
		} else if ( label.find( words[0] ) != std::string::npos || name.find( words[0] ) != std::string::npos ) {
			score = 1;
		}
		scored.emplace_back( score * 1000 + int( label.size() ), a.name );
	}
	std::stable_sort( scored.begin(), scored.end(), []( const auto &x, const auto &y ) { return x.first < y.first; } );
	std::vector<std::string> out;
	for ( const auto &s : scored ) {
		out.push_back( s.second );
	}
	return out;
}

// An element's d-pad neighbors up and down ("none" at either end).
void NavUpDown( Rml::ElementDocument *doc, const std::string &id, const std::string &up, const std::string &down ) {
	if ( Rml::Element *e = doc->GetElementById( id ) ) {
		e->SetProperty( "nav-up", up.empty() ? Rml::String( "none" ) : "#" + up );
		e->SetProperty( "nav-down", down.empty() ? Rml::String( "none" ) : "#" + down );
	}
}

void BuildSearch( Rml::ElementDocument *doc ) {
	std::string query = Lower( InputValue( doc, "ed-search-text" ) );
	query.erase( 0, query.find_first_not_of( ' ' ) == std::string::npos ? query.size() : query.find_first_not_of( ' ' ) );
	query.erase( query.find_last_not_of( ' ' ) + 1 );
	if ( query != searchQuery ) {
		searchQuery = query;
		foundAt = 0;
	}
	const Rml::String key = query + "|" + std::to_string( foundAt ) + "|" + std::to_string( session().keymap_rev );
	if ( key == lastSearch ) {
		return;
	}
	lastSearch = key;
	found = Matches( query );
	if ( found.size() > 12 ) {
		found.resize( 12 );
	}
	foundAt = std::max( 0, std::min( foundAt, int( found.size() ) - 1 ) );
	Rml::String rml;
	for ( size_t i = 0; i < found.size(); i++ ) {
		const oz::ed::Action *a = oz::ed::find_action( found[i] );
		const std::string n = std::to_string( i );
		rml += "<button class=\"srow" + Rml::String( int( i ) == foundAt ? " on" : "" ) + "\" id=\"ed-sr-" + n + "\" onclick=\"append ed panel search_pick " + n +
			"\"><span class=\"sl\">" + Enc( a->label ) + "</span><span class=\"sk\">" + Enc( session().keys_of( *a ) ) + "</span><span class=\"sh\">" +
			Enc( a->help ) + "</span></button>";
	}
	if ( found.empty() ) {
		rml = "<p>No action by that name: try another word</p>";
	}
	SetRml( doc, "ed-search-rows", rml );
	// the d-pad: from the field down the rows (the arrow keys move the highlight instead)
	NavUpDown( doc, "ed-search-text", "", found.empty() ? "" : "ed-sr-0" );
	for ( size_t i = 0; i < found.size(); i++ ) {
		NavUpDown( doc, "ed-sr-" + std::to_string( i ), i ? "ed-sr-" + std::to_string( i - 1 ) : "ed-search-text",
			i + 1 < found.size() ? "ed-sr-" + std::to_string( i + 1 ) : "" );
	}
}

void OpenSearch( Rml::ElementDocument *doc ) {
	searchOpen = true;
	Show( doc, "ed-search", true );
	SetInput( doc, "ed-search-text", "" );
	searchQuery.clear();
	foundAt = 0;
	lastSearch.clear();
	BuildSearch( doc );
	doc->UpdateDocument();	// shown before it can take the focus
	if ( Rml::Element *f = doc->GetElementById( "ed-search-text" ) ) {
		f->Focus();
	}
}

void CloseSearch( Rml::ElementDocument *doc ) {
	searchOpen = false;
	Show( doc, "ed-search", false );
	if ( TypingInside( doc->GetElementById( "ed-search" ) ) ) {
		TypingIn()->Blur();
	}
}

// the highlighted row (or the one clicked) done, the search closed first
void RunFound( Rml::ElementDocument *doc, int i ) {
	if ( i < 0 || i >= int( found.size() ) ) {
		return;
	}
	const std::string name = found[size_t( i )];
	CloseSearch( doc );
	session().notify = Notify;
	session().run( name, {} );
}

// The last compile's warnings: a row each, what it's about framed when picked (ed warning <i>)
void OpenWarnings( Rml::ElementDocument *doc ) {
	const std::vector<oz::ed::Session::CompileWarning> &ws = session().compile_warnings();
	Rml::String rml;
	for ( size_t i = 0; i < ws.size(); i++ ) {
		const std::string n = std::to_string( i );
		const std::string about = ws[i].ids.empty() ? "" : ws[i].ids.size() == 1 ? "1 object" : std::to_string( ws[i].ids.size() ) + " objects";
		rml += "<button class=\"srow wrow\" id=\"ed-wr-" + n + "\" data-hint=\"" + Enc( ws[i].text ) + "\" onclick=\"append ed panel warning_pick " + n +
			"\"><span class=\"sl\">" + Enc( ws[i].text ) + "</span><span class=\"sk\">" + Enc( about + ( ws[i].count > 1 ? " x" + std::to_string( ws[i].count ) : "" ) ) +
			"</span></button>";
	}
	SetRml( doc, "ed-warns-rows", rml );
	for ( size_t i = 0; i < ws.size(); i++ ) {
		NavUpDown( doc, "ed-wr-" + std::to_string( i ), i ? "ed-wr-" + std::to_string( i - 1 ) : "", i + 1 < ws.size() ? "ed-wr-" + std::to_string( i + 1 ) : "" );
	}
	warnsOpen = true;
	Show( doc, "ed-warns", true );
	doc->UpdateDocument();
	if ( Rml::Element *first = doc->GetElementById( "ed-wr-0" ) ) {
		first->Focus();
	}
}

void CloseWarnings( Rml::ElementDocument *doc ) {
	warnsOpen = false;
	Show( doc, "ed-warns", false );
}

void BuildKeymap( Rml::ElementDocument *doc ) {
	oz::ed::Session &s = session();
	const std::string filter = Lower( InputValue( doc, "ed-keymap-filter" ) );
	const Rml::String key = filter + "|" + std::to_string( s.keymap_rev ) + "|" + s.binding;
	if ( key == lastKeymap ) {
		return;
	}
	lastKeymap = key;
	Rml::String rml;
	std::vector<std::string> ids;
	for ( const oz::ed::Action &a : oz::ed::actions() ) {
		const std::string keys = s.keys_of( a );
		if ( !oz::ed::runs_alone( a ) ||
			( !filter.empty() && ( Lower( a.label ) + " " + Spaced( a.name ) + " " + Lower( keys ) ).find( filter ) == std::string::npos ) ) {
			continue;
		}
		const bool on = s.binding == a.name;
		ids.push_back( std::string( "ed-kb-" ) + a.name );
		rml += "<div class=\"krow" + Rml::String( on ? " on" : "" ) + "\" data-hint=\"" + Enc( a.help ) + "\"><span class=\"kl\">" + Enc( a.label ) +
			"</span><span class=\"kk\">" + ( on ? Rml::String( "Press the keys ..." ) : keys.empty() ? Rml::String( "<span class=\"none\">none</span>" ) : Enc( keys ) ) +
			"</span><button class=\"tb\" id=\"" + ids.back() + "\" onclick=\"append ed bind_start " + a.name + "\">SET</button></div>";
	}
	SetRml( doc, "ed-keymap-rows", rml );
	if ( Rml::Element *waiting = s.binding.empty() ? nullptr : doc->GetElementById( "ed-kb-" + s.binding ) ) {
		doc->UpdateDocument();	// laid out before it can scroll into view (SET from the console, the search)
		waiting->ScrollIntoView( Rml::ScrollIntoViewOptions( Rml::ScrollAlignment::Nearest ) );
	}
	const oz::ed::Action *b = s.binding.empty() ? nullptr : oz::ed::find_action( s.binding );
	SetText( doc, "ed-keymap-note", b ? "Press the keys for " + std::string( b->label ) + ", with Ctrl, Shift or Alt if you like: Esc keeps them as they were, Backspace leaves it none."
		: "SET an action, then press its keys. A key does one thing: given to another action, it leaves the one that had it.", lastKeymapNote );
	if ( Rml::Element *note = doc->GetElementById( "ed-keymap-note" ) ) {
		note->SetClass( "wait", b != nullptr );
	}
	// the d-pad: the filter, down the SET buttons, then the presets
	NavUpDown( doc, "ed-keymap-filter", "", ids.empty() ? "ed-keymap-oz" : ids.front() );
	for ( size_t i = 0; i < ids.size(); i++ ) {
		NavUpDown( doc, ids[i], i ? ids[i - 1] : "ed-keymap-filter", i + 1 < ids.size() ? ids[i + 1] : "ed-keymap-oz" );
	}
	for ( const char *id : { "ed-keymap-oz", "ed-keymap-radiant", "ed-keymap-done" } ) {
		NavUpDown( doc, id, ids.empty() ? "ed-keymap-filter" : ids.back(), "" );
	}
}

void OpenKeymap( Rml::ElementDocument *doc ) {
	if ( searchOpen ) {
		CloseSearch( doc );
	}
	keymapOpen = true;
	Show( doc, "ed-keymap", true );
	SetInput( doc, "ed-keymap-filter", "" );
	lastKeymap.clear();
	BuildKeymap( doc );
	doc->UpdateDocument();
	if ( Rml::Element *f = doc->GetElementById( "ed-keymap-filter" ) ) {
		f->Focus();
	}
}

void CloseKeymap( Rml::ElementDocument *doc ) {
	keymapOpen = false;
	session().binding.clear();
	Show( doc, "ed-keymap", false );
	if ( TypingInside( doc->GetElementById( "ed-keymap" ) ) ) {
		TypingIn()->Blur();
	}
}

// ---------------------------------------------------------------- Sky and sun

// The dialog over the view (Build > Sky and sun, the map's keys): id's skies as tiles, then the light.
// Everything in it runs ed sky ..., one undo step each on the map's own keys.
bool skyOpen;
Rml::String lastSky;

void BuildSky( Rml::ElementDocument *doc ) {
	oz::ed::Session &s = session();
	const oz::ed::Session::SkySun ss = s.sky_sun();
	char rgb[64];
	snprintf( rgb, sizeof rgb, "%g %g %g", ss.color[0], ss.color[1], ss.color[2] );
	const Rml::String key = ss.sky + "|" + ss.painted + "|" + std::to_string( ss.chosen ) + std::to_string( ss.own ) + "|" + rgb + "|" +
		Num( ss.brightness ) + " " + Num( ss.height ) + " " + Num( ss.direction ) + " " + Num( ss.sky_light ) + "|" + Num( ss.glow[0] ) + " " +
		Num( ss.glow[1] ) + " " + Num( ss.glow[2] ) + " " + Num( ss.ambient ) + " " + Num( ss.ambient_color[0] ) + " " + Num( ss.ambient_color[1] ) + " " +
		Num( ss.ambient_color[2] );
	if ( key == lastSky ) {
		return;
	}
	lastSky = key;

	// the skies: id's own (q3map2 sees only id's paks) sky shaders, not the bare cloud images in the same
	// folder (a wall of clouds); the map's own sky among them if it's a pack's
	std::vector<std::string> skies;
	oz::ed::ShaderLib &lib = oz::ed::shaders_lib();
	for ( const oz::ed::Material &m : lib.materials() ) {
		if ( m.stock && m.script && m.name.compare( 0, 6, "skies/" ) == 0 && lib.look( m.name ) == oz::ed::Look::Sky ) {
			skies.push_back( m.name );
		}
	}
	std::sort( skies.begin(), skies.end() );
	if ( std::find( skies.begin(), skies.end(), ss.sky ) == skies.end() ) {
		skies.insert( skies.begin(), ss.sky );
	}
	const std::string def = oz::ed::Session::DEFAULT_SKY;
	if ( std::find( skies.begin(), skies.end(), def ) != skies.end() ) {
		skies.erase( std::find( skies.begin(), skies.end(), def ) );
		skies.insert( skies.begin(), def );
	}
	Rml::String tiles;
	for ( size_t i = 0; i < skies.size(); i++ ) {
		const std::string &n = skies[i];
		Rml::String style;
		const Rml::String sw = SwatchRml( n, 5.4, style, true );
		const bool on = n == ss.sky;
		const Rml::String badge = n == def ? "DEFAULT" : n == ss.painted ? "PAINTED" : "";
		tiles += "<div class=\"mat" + Rml::String( on ? " on" : "" ) + "\" id=\"ed-sky-t-" + std::to_string( i ) + "\" data-hint=\"" + Enc( n ) +
			": the sky around the map\" onclick=\"append ed sky sky " + Enc( n ) + "\"><div class=\"sw\"" + ( style.empty() ? "" : " style=\"" + style + "\"" ) +
			">" + sw + "</div>" + ( badge.empty() ? "" : "<span class=\"badge\">" + badge + "</span>" ) + "<span class=\"mn\">" + Enc( n.substr( 6 ) ) + "</span></div>";
	}
	SetRml( doc, "ed-sky-tiles", tiles );
	const std::string why = ss.chosen ? "picked here" : !ss.painted.empty() && ss.sky == ss.painted ? "painted on the map's brushes" : "the default";
	SetRml( doc, "ed-sky-now", Enc( "SKY: " + Rml::StringUtilities::ToUpper( ss.sky.substr( 6 ) ) + " (" + why + ")" ) );

	// the light
	Rml::String rml = "<div class=\"srow\"><span class=\"sl\">Light from</span>"
		"<button class=\"chip" + Rml::String( ss.own ? "" : " on" ) + "\" id=\"ed-sky-editor\" data-hint=\"The sun and sky light below light the map\" "
		"onclick=\"append ed sky light_from editor\">THESE SETTINGS</button>"
		"<button class=\"chip" + Rml::String( ss.own ? " on" : "" ) + "\" id=\"ed-sky-own\" data-hint=\"The sky's own script lights the map, as id's maps with it are lit\" "
		"onclick=\"append ed sky light_from own\">THE SKY'S OWN</button>"
		"<span class=\"sh\">" + Rml::String( ss.own ? "The sky's own sun and glow light the map; the settings below wait." : "The sun and the sky's glow below light the map." ) +
		"</span></div>";
	// a color: the palette's dots, a swatch of what it is, the numbers to type
	auto colorRow = [&]( const char *field, const char *label, const double c[3], const char *help ) {
		char v[64];
		snprintf( v, sizeof v, "%g %g %g", c[0], c[1], c[2] );
		rml += "<div class=\"srow col\"><span class=\"sl\">" + Rml::String( label ) + "</span>";
		for ( const auto &p : PALETTE ) {
			const bool on = Rml::String( p.rgb ) == v;
			rml += "<button class=\"dot" + Rml::String( on ? " on" : "" ) + "\" style=\"background-color: " + HexColor( p.rgb ) + ";\" data-hint=\"" + Enc( p.name ) +
				" (" + p.rgb + ")\" onclick=\"append ed sky " + field + " " + p.rgb + "\"></button>";
		}
		rml += "<input type=\"text\" class=\"text rgb\" id=\"ed-sky-" + Rml::String( field ) + "\" value=\"" + Rml::String( v ) +
			"\" data-apply=\"sky_typed\" data-sky-field=\"" + field + "\" data-hint=\"Red, green and blue, 0 to 1 each: Enter sets it\"/>"
			"<span class=\"sh\">" + Rml::String( help ) + "</span></div>";
	};
	colorRow( "color", "Sun color", ss.color, ss.glow_set ? "The sun's light" : "The sun's light, and the sky's glow unless that has its own" );
	colorRow( "glow_color", "Sky glow color", ss.glow, ss.glow_set ? "The sky's own glow" : "The sun's for now: a color here sets it apart" );
	const struct {
		const char *field, *label, *help;
		double value, step;
	} nums[] = {
		{ "brightness", "Sun brightness", "How strong the sun is (100: about id's outdoor maps)", ss.brightness, 10 },
		{ "height", "Sun height", "Degrees over the horizon: 90 straight down, low for long shadows", ss.height, 5 },
		{ "direction", "Sun direction", "Degrees, as a light's angle: 0 from +X (east), 90 from +Y", ss.direction, 15 },
		{ "sky_light", "Sky light", "How much the sky itself glows onto the map (0: the sun alone)", ss.sky_light, 10 },
		{ "ambient", "Ambient light", "Light everywhere, before any lamp or the sun: lifts the darkest corners (0: none, 5 to 20 is subtle)", ss.ambient, 5 },
	};
	for ( const auto &n : nums ) {
		const Rml::String f = n.field;
		rml += "<div class=\"srow num\"><span class=\"sl\">" + Rml::String( n.label ) + "</span>"
			"<button class=\"tb\" id=\"ed-sky-" + f + "-less\" onclick=\"append ed sky " + f + " " + Num( n.value - n.step ) + "\">-</button>"
			"<input type=\"text\" class=\"text\" id=\"ed-sky-" + f + "\" value=\"" + Num( n.value ) + "\" data-apply=\"sky_typed\" data-sky-field=\"" + f +
			"\" data-hint=\"" + Enc( n.help ) + ": Enter sets it\"/>"
			"<button class=\"tb\" id=\"ed-sky-" + f + "-more\" onclick=\"append ed sky " + f + " " + Num( n.value + n.step ) + "\">+</button>"
			"<span class=\"sh\">" + Enc( n.help ) + "</span></div>";
	}
	colorRow( "ambient_color", "Ambient color", ss.ambient_color, "The ambient light's tint (it lights with or without the sky's settings)" );
	SetRml( doc, "ed-sky-light", rml );
	if ( Rml::Element *e = doc->GetElementById( "ed-sky-light" ) ) {
		e->SetClass( "own", ss.own );
	}
}

void OpenSky( Rml::ElementDocument *doc ) {
	skyOpen = true;
	session().sky_open = true;
	lastSky.clear();
	Show( doc, "ed-sky", true );
	BuildSky( doc );
	doc->UpdateDocument();
	if ( Rml::Element *b = doc->GetElementById( "ed-sky-done" ) ) {
		b->Focus();
	}
}

void CloseSky( Rml::ElementDocument *doc ) {
	skyOpen = false;
	session().sky_open = false;
	Show( doc, "ed-sky", false );
	if ( TypingInside( doc->GetElementById( "ed-sky" ) ) ) {
		TypingIn()->Blur();
	}
}

// The KEYS help's keys as the player has them: each kbd's data-keys names its action (its first keys)
void ShowKeyHelp( Rml::ElementDocument *doc ) {
	const Rml::String key = std::to_string( session().keymap_rev );
	if ( key == lastKeyHelp ) {
		return;
	}
	lastKeyHelp = key;
	Rml::ElementList kbds;
	doc->GetElementsByTagName( kbds, "kbd" );
	for ( Rml::Element *k : kbds ) {
		const oz::ed::Action *a = oz::ed::find_action( k->GetAttribute<Rml::String>( "data-keys", "" ) );
		if ( a ) {
			const std::string keys = session().keys_of( *a );
			k->SetInnerRML( Enc( keys.empty() ? "-" : keys.substr( 0, keys.find( ", " ) ) ) );
		}
	}
}

// The files a pick list offers, once a page opening (the game's files don't change under the editor)
const std::vector<std::string> &PickFiles( const std::string &type ) {
	static std::map<std::string, std::vector<std::string>> cache;
	auto it = cache.find( type );
	if ( it != cache.end() ) {
		return it->second;
	}
	std::vector<std::string> &out = cache[type];
	const char *dir = type == "music" ? "music" : type == "model" ? "models" : "sound";
	for ( const char *ext : type == "model" ? std::vector<const char *>{ "md3" } : std::vector<const char *>{ "wav", "ogg" } ) {
		int n = 0;
		char **files = OzE_ListTree( dir, ext, &n );
		for ( int i = 0; i < n; i++ ) {
			out.push_back( files[i] );
		}
		OzE_FreeFileList( files );
	}
	std::sort( out.begin(), out.end() );
	out.erase( std::unique( out.begin(), out.end() ), out.end() );
	return out;
}

// The maps Open map offers: the .map files in the editor's maps folder, by name
// The maps opened last (recent.txt beside the maps folder), newest first: a .map anywhere, by its full path
std::string RecentFile() {
	return std::string( OzEd_HomePath() ) + "/oz/editor/recent.txt";
}

std::vector<std::string> RecentMaps() {
	std::vector<std::string> out;
	char *text = nullptr;
	const int len = OzEd_ReadOSFile( RecentFile().c_str(), &text );
	const std::string all = len > 0 ? std::string( text, size_t( len ) ) : std::string();
	OzEd_Free( text );
	for ( size_t at = 0; at < all.size(); ) {
		size_t eol = all.find( '\n', at );
		std::string line = all.substr( at, eol == std::string::npos ? std::string::npos : eol - at );
		at = eol == std::string::npos ? all.size() : eol + 1;
		while ( !line.empty() && ( line.back() == '\r' || line.back() == ' ' ) ) {
			line.pop_back();
		}
		if ( !line.empty() && OzEd_FileExists( line.c_str() ) && std::find( out.begin(), out.end(), line ) == out.end() && out.size() < 8 ) {
			out.push_back( line );
		}
	}
	return out;
}

void NoteRecent( const std::string &path ) {
	if ( path.empty() ) {
		return;
	}
	std::vector<std::string> list = RecentMaps();
	list.erase( std::remove( list.begin(), list.end(), path ), list.end() );
	list.insert( list.begin(), path );
	std::string text;
	for ( size_t i = 0; i < list.size() && i < 8; i++ ) {
		text += list[i] + "\n";
	}
	OzEd_WriteOSFile( RecentFile().c_str(), text.data(), int( text.size() ) );
}

// The Open list: the recent maps first (by name in the maps folder, else by full path), then the folder's
std::vector<std::string> MyMaps() {
	std::vector<std::string> out;
	const std::string dir = session().maps_dir() + "/";
	for ( const std::string &r : RecentMaps() ) {
		const bool here = r.compare( 0, dir.size(), dir ) == 0 && r.size() > dir.size() + 4 && r.find( '/', dir.size() ) == std::string::npos;
		out.push_back( here ? r.substr( dir.size(), r.size() - dir.size() - 4 ) : r );
	}
	const size_t recent = out.size();
	int n = 0;
	char **files = OzEd_ListOSFiles( session().maps_dir().c_str(), "map", &n );
	for ( int i = 0; i < n; i++ ) {
		const std::string f = files[i];
		if ( f.size() > 4 ) {
			out.push_back( f.substr( 0, f.size() - 4 ) );
		}
	}
	OzEd_FreeList( files );
	std::sort( out.begin() + long( recent ), out.end() );
	// a recent one isn't listed twice
	for ( size_t i = recent; i < out.size(); ) {
		if ( std::find( out.begin(), out.begin() + long( recent ), out[i] ) != out.begin() + long( recent ) ) {
			out.erase( out.begin() + long( i ) );
		} else {
			i++;
		}
	}
	return out;
}

// The maps Open a game map offers: every maps/*.bsp in the game's files but the editor's play-tests (ed_*)
const std::vector<std::string> &GameMaps() {
	static std::vector<std::string> out;
	static bool made;
	if ( made ) {
		return out;
	}
	made = true;
	int n = 0;
	char **files = OzE_ListTree( "maps", "bsp", &n );
	for ( int i = 0; i < n; i++ ) {
		std::string m = files[i];
		m = m.compare( 0, 5, "maps/" ) == 0 ? m.substr( 5 ) : m;
		m = m.size() > 4 ? m.substr( 0, m.size() - 4 ) : m;
		if ( !m.empty() && Lower( m ).compare( 0, 3, "ed_" ) != 0 ) {
			out.push_back( m );
		}
	}
	OzE_FreeFileList( files );
	std::sort( out.begin(), out.end() );
	out.erase( std::unique( out.begin(), out.end() ), out.end() );
	return out;
}

void BuildPick( Rml::ElementDocument *doc ) {
	std::string query = Lower( InputValue( doc, "ed-pick-text" ) );
	if ( query != pickQuery ) {
		pickQuery = query;
		pickAt = 0;
	}
	const Rml::String key = query + "|" + std::to_string( pickAt ) + "|" + pickKey + "|" + pickType + "|" + std::to_string( session().doc.revision );
	if ( key == lastPick ) {
		return;
	}
	lastPick = key;
	// what's offered: the game's files, or the map's names
	std::vector<std::string> all;
	if ( pickType == "target" ) {
		std::set<std::string> names;
		for ( oz::ed::Id id : session().doc.ids_of( oz::ed::Kind::Entity ) ) {
			if ( const std::string *n = session().doc.get( id )->entity.find( "targetname" ) ) {
				names.insert( *n );
			}
		}
		all.assign( names.begin(), names.end() );
	} else if ( pickType == "open" ) {
		all = MyMaps();
	} else if ( pickType == "import" ) {
		all = GameMaps();
	} else {
		all = PickFiles( pickType );
	}
	std::vector<std::string> words;
	for ( size_t at = 0; at < query.size(); ) {
		size_t sp = query.find( ' ', at );
		sp = sp == std::string::npos ? query.size() : sp;
		if ( sp > at ) {
			words.push_back( query.substr( at, sp - at ) );
		}
		at = sp + 1;
	}
	pickFound.clear();
	size_t matches = 0;
	for ( const std::string &f : all ) {
		const std::string lf = Lower( f );
		bool ok = true;
		for ( const std::string &w : words ) {
			ok = ok && lf.find( w ) != std::string::npos;
		}
		if ( ok && ++matches <= 60 ) {
			pickFound.push_back( f );
		}
	}
	pickAt = std::max( 0, std::min( pickAt, int( pickFound.size() ) - 1 ) );
	Rml::String rml;
	for ( size_t i = 0; i < pickFound.size(); i++ ) {
		const std::string n = std::to_string( i );
		rml += "<button class=\"srow" + Rml::String( int( i ) == pickAt ? " on" : "" ) + "\" id=\"ed-pr-" + n + "\" onclick=\"append ed panel pick_row " + n +
			"\"><span class=\"sh\">" + Enc( pickFound[i] ) + "</span></button>";
	}
	if ( matches > pickFound.size() ) {
		rml += "<p>" + std::to_string( matches - pickFound.size() ) + " more: type more of the name</p>";
	} else if ( pickFound.empty() ) {
		rml = !all.empty() ? Rml::String( "<p>Nothing by that name</p>" )
			: pickType == "open" ? Rml::String( "<p>No maps saved yet: File, Save as gives the map a name</p>" ) : Rml::String( "<p>Nothing to pick from</p>" );
	}
	SetRml( doc, "ed-pick-rows", rml );
	NavUpDown( doc, "ed-pick-text", "", pickFound.empty() ? "" : "ed-pr-0" );
	for ( size_t i = 0; i < pickFound.size(); i++ ) {
		NavUpDown( doc, "ed-pr-" + std::to_string( i ), i ? "ed-pr-" + std::to_string( i - 1 ) : "ed-pick-text",
			i + 1 < pickFound.size() ? "ed-pr-" + std::to_string( i + 1 ) : "" );
	}
}

void OpenPick( Rml::ElementDocument *doc, const std::string &k, const std::string &type ) {
	pickOpen = true;
	pickKey = k, pickType = type;
	pickQuery.clear();
	pickAt = 0;
	lastPick.clear();
	// a key's value, or a map to open: yours, or one of the game's made editable again
	SetRml( doc, "ed-pick-title", Enc( type == "open" ? "OPEN A MAP" : type == "import" ? "OPEN A GAME MAP" : "PICK " + Rml::StringUtilities::ToUpper( k ) ) );
	SetRml( doc, "ed-pick-note", Enc( type == "open" ? "Your maps (oz/editor/maps in your Oz Arena folder): type part of a name, Enter opens it."
		: type == "import" ? "A map from the game's files (map packs too), its brushes, curves and entities made editable again by q3map2."
		: "" ) );
	Show( doc, "ed-pick-note", type == "open" || type == "import" );
	Show( doc, "ed-pick", true );
	SetInput( doc, "ed-pick-text", "" );
	BuildPick( doc );
	doc->UpdateDocument();
	if ( Rml::Element *f = doc->GetElementById( "ed-pick-text" ) ) {
		f->Focus();
	}
}

void ClosePick( Rml::ElementDocument *doc ) {
	pickOpen = false;
	Show( doc, "ed-pick", false );
	if ( TypingInside( doc->GetElementById( "ed-pick" ) ) ) {
		TypingIn()->Blur();
	}
}

void RunPick( Rml::ElementDocument *doc, int i, bool entered = false ) {
	// Open with a path typed and Enter (a .map anywhere: Radiant's, TrenchBroom's): that file, unless a row
	// is that very name (a recent map's full path)
	const std::string typed = InputValue( doc, "ed-pick-text" );
	const bool aRow = std::find( pickFound.begin(), pickFound.end(), typed ) != pickFound.end();
	if ( entered && !aRow && pickType == "open" && ( typed.find( '/' ) != std::string::npos || typed.find( '\\' ) != std::string::npos || typed.find( ':' ) != std::string::npos ) ) {
		std::string file = typed;
		file.erase( std::remove( file.begin(), file.end(), '"' ), file.end() );
		ClosePick( doc );
		session().notify = Notify;
		if ( !OzEd_FileExists( file.c_str() ) ) {
			Notify( "There's no file " + file, true );
			return;
		}
		session().run( "open", { file } );
		return;
	}
	if ( i < 0 || i >= int( pickFound.size() ) ) {
		return;
	}
	const std::string v = pickFound[size_t( i )], k = pickKey, type = pickType;
	ClosePick( doc );
	session().notify = Notify;
	if ( type == "open" ) {
		// a name in the maps folder, or a full path (a recent map from elsewhere)
		const bool full = v.find( '/' ) != std::string::npos || v.find( '\\' ) != std::string::npos || v.find( ':' ) != std::string::npos;
		session().run( "open", { full ? v : session().maps_dir() + "/" + v + ".map" } );
	} else if ( type == "import" ) {
		session().run( "import", { v } );
	} else {
		session().run( "key_set", { k, v } );
	}
}

// ---------------------------------------------------------------- the UV editor

// Its face: the first the mapping acts on (the faces tool's, else the selected brushes' first)
bool UvFace( oz::ed::FaceRef &ref ) {
	const std::vector<oz::ed::FaceRef> t = session().target_faces();
	if ( t.empty() ) {
		return false;
	}
	ref = t[0];
	return true;
}

void BuildUv( Rml::ElementDocument *doc ) {
	oz::ed::Session &s = session();
	oz::ed::FaceRef ref;
	if ( !UvFace( ref ) ) {
		SetRml( doc, "ed-uv-note", "No face: select a brush, or pick a face with the faces tool (T)" );
		SetRml( doc, "ed-uv-tex", "" );
		SetRml( doc, "ed-uv-lines", "" );
		lastUv.clear();
		return;
	}
	const Rml::String key = std::to_string( ref.first ) + "|" + std::to_string( ref.second ) + "|" + std::to_string( s.doc.revision ) + "|" +
		Num( uvScale ) + "|" + Num( uvDragX ) + "|" + Num( uvDragY ) + "|" + std::to_string( uvFit );
	if ( key == lastUv ) {
		return;
	}
	lastUv = key;
	const oz::ed::Object *o = s.doc.get( ref.first );
	const oz::ed::Face &f = o->brush.faces[size_t( ref.second )];
	const std::vector<oz::ed::Winding> &ws = s.doc.windings( ref.first );
	if ( size_t( ref.second ) >= ws.size() || ws[size_t( ref.second )].size() < 3 ) {
		return;
	}
	// the face's corners in texture space (repeats: s along, t down)
	std::vector<std::pair<double, double>> uv;
	double u0 = 1e30, u1 = -1e30, v0 = 1e30, v1 = -1e30;
	for ( const oz::ed::Vec3 &p : ws[size_t( ref.second )] ) {
		const double u = f.st.s.dot( p ) + f.st.s0, v = f.st.t.dot( p ) + f.st.t0;
		uv.emplace_back( u, v );
		u0 = std::min( u0, u ), u1 = std::max( u1, u ), v0 = std::min( v0, v ), v1 = std::max( v1, v );
	}
	if ( uvFit ) {
		uvScale = std::clamp( 0.7 * UV_VIEW / std::max( { u1 - u0, v1 - v0, 0.25 } ), 8.0, 4000.0 );
		uvFit = false;
	}
	const double cu = ( u0 + u1 ) / 2, cv = ( v0 + v1 ) / 2;
	auto sx = [&]( double u ) { return UV_VIEW / 2 + ( u - cu ) * uvScale; };
	auto sy = [&]( double v ) { return UV_VIEW / 2 + ( v - cv ) * uvScale; };
	// the texture's tiles under it (moved by the drag under way); the first repeat framed
	const std::string name = oz::ed::shader_name( f.shader );
	oz::ed::ShaderLib &lib = oz::ed::shaders_lib();
	Rml::String tiles;
	const double half = UV_VIEW / 2 / uvScale;
	const int i0 = int( std::floor( cu - half - uvDragX / uvScale ) ) - 1, i1 = int( std::ceil( cu + half - uvDragX / uvScale ) );
	const int j0 = int( std::floor( cv - half - uvDragY / uvScale ) ) - 1, j1 = int( std::ceil( cv + half - uvDragY / uvScale ) );
	char st[200];
	if ( lib.found( name ) && ( i1 - i0 + 1 ) * ( j1 - j0 + 1 ) <= 400 ) {
		const Rml::String src = Enc( "/@thumb/" + lib.image( name ) );
		for ( int j = j0; j <= j1; j++ ) {
			for ( int i = i0; i <= i1; i++ ) {
				snprintf( st, sizeof st, "left: %.1fdp; top: %.1fdp; width: %.1fdp; height: %.1fdp;", sx( i ) + uvDragX, sy( j ) + uvDragY, uvScale, uvScale );
				tiles += "<img class=\"uvt" + Rml::String( i == 0 && j == 0 ? " home" : "" ) + "\" src=\"" + src + "\" style=\"" + st + "\"/>";
			}
		}
	}
	SetRml( doc, "ed-uv-tex", tiles );
	// the face's outline over it: its edges as turned lines, its corners as squares
	Rml::String lines;
	for ( size_t k = 0; k < uv.size(); k++ ) {
		const auto &a = uv[k], &b = uv[( k + 1 ) % uv.size()];
		const double ax = sx( a.first ), ay = sy( a.second ), bx = sx( b.first ), by = sy( b.second );
		const double len = std::hypot( bx - ax, by - ay ), deg = std::atan2( by - ay, bx - ax ) * 180 / 3.14159265358979323846;
		snprintf( st, sizeof st, "left: %.1fdp; top: %.1fdp; width: %.1fdp; transform: rotate(%.2fdeg);", ax, ay, len, deg );
		lines += "<div class=\"uvl\" style=\"" + Rml::String( st ) + "\"></div>";
		snprintf( st, sizeof st, "left: %.1fdp; top: %.1fdp;", ax - 3, ay - 3 );
		lines += "<div class=\"uvp\" style=\"" + Rml::String( st ) + "\"></div>";
	}
	SetRml( doc, "ed-uv-lines", lines );
	int tw = 64, th = 64;
	s.doc.tex_dims( f.shader, tw, th );
	SetRml( doc, "ed-uv-note", Enc( name + " (" + std::to_string( tw ) + " x " + std::to_string( th ) + "), face " + std::to_string( ref.second ) + " of brush #" +
		std::to_string( ref.first ) + ": drag the texture, the wheel zooms" ) );
}

// a drag let go: the texture moved with it, the drag's dp in texels
void UvApply( double dx, double dy ) {
	oz::ed::FaceRef ref;
	if ( !UvFace( ref ) || ( dx == 0 && dy == 0 ) ) {
		return;
	}
	oz::ed::Session &s = session();
	int tw = 64, th = 64;
	s.doc.tex_dims( s.doc.get( ref.first )->brush.faces[size_t( ref.second )].shader, tw, th );
	s.notify = Notify;
	s.run( "tex_shift", { Num( std::round( -dx / uvScale * tw ) ), Num( std::round( -dy / uvScale * th ) ) } );
}

// The view's drags and wheel: a drag shows as it goes and moves the texture when let go
class UvListener : public Rml::EventListener {
public:
	void ProcessEvent( Rml::Event &e ) override {
		const float ratio = std::max( 0.01f, GetContext()->GetDensityIndependentPixelRatio() );
		const float x = e.GetParameter<float>( "mouse_x", 0 ) / ratio, y = e.GetParameter<float>( "mouse_y", 0 ) / ratio;
		switch ( e.GetId() ) {
		case Rml::EventId::Dragstart:
			fromX = x, fromY = y;
			break;
		case Rml::EventId::Drag:
			uvDragX = x - fromX, uvDragY = y - fromY;
			break;
		case Rml::EventId::Dragend: {
			const double dx = uvDragX, dy = uvDragY;
			uvDragX = uvDragY = 0;
			UvApply( dx, dy );
			break;
		}
		case Rml::EventId::Mousescroll:
			uvScale = std::clamp( uvScale * ( e.GetParameter<float>( "wheel_delta_y", 0 ) < 0 ? 1.25 : 0.8 ), 8.0, 4000.0 );
			e.StopPropagation();
			break;
		default:
			break;
		}
	}
private:
	float fromX = 0, fromY = 0;
};
UvListener uvListener;

void OpenUv( Rml::ElementDocument *doc ) {
	uvOpen = true;
	uvFit = true;
	uvDragX = uvDragY = 0;
	lastUv.clear();
	Show( doc, "ed-uv", true );
	BuildUv( doc );
}

void CloseUv( Rml::ElementDocument *doc ) {
	uvOpen = false;
	Show( doc, "ed-uv", false );
}

// ---------------------------------------------------------------- the menus

// Each menu's rows: an action of oz::ed::actions() (its label, keys and help), "-" a line, "#..." a heading
const struct {
	const char *name;
	std::vector<const char *> rows;
} MENUS[] = {
	{ "file", { "new", "open", "import", "-", "save", "save_as", "-", "leave" } },
	{ "edit", { "undo", "redo", "-", "cut", "copy", "paste", "duplicate", "delete", "-", "texture_lock", "grid_smaller", "grid_bigger", "-", "keymap" } },
	{ "select", { "select", "select_all", "select_none", "-", "hide", "hide_others", "show_all", "-", "lock", "unlock_all", "-", "group", "ungroup",
		"-", "show_objects" } },
	{ "view", { "views", "view_next", "-", "frame_selection", "frame_all", "-", "#SHOW", "filter_tools", "filter_triggers", "filter_sky", "filter_entities",
		"filter_curves", "-", "preview", "-", "#PANEL", "show_inspect", "show_materials", "show_entities", "show_objects", "show_history" } },
	{ "brush", { "create", "clip", "faces", "-", "collision", "detail", "-", "hollow", "subtract", "merge", "-", "rotate_left", "rotate_right", "rotate_left_15", "rotate_right_15",
		"-", "flip_x", "flip_y", "flip_z" } },
	{ "curve", { "#FROM THE SELECTED BRUSHES", "curve_cylinder", "curve_cone", "curve_arch", "curve_bevel", "curve_plane", "-", "#THE SELECTED CURVES",
		"curve_more_columns", "curve_fewer_columns", "curve_more_rows", "curve_fewer_rows", "curve_invert", "curve_caps", "curve_thicken" } },
	{ "texture", { "show_materials", "pick_material", "paint", "-", "uv", "tex_fit", "tex_left", "tex_right", "tex_top", "tex_bottom", "tex_center",
		"-", "tex_flip_s", "tex_flip_t", "tex_rotate_left", "tex_rotate_right", "tex_scale_up", "tex_scale_down", "tex_reset" } },
	{ "entity", { "show_entities", "light", "jump_pad", "push_land", "teleporter", "-", "#MOVERS", "door", "door_pair", "lift", "door_button",
		"train_path", "mover_play", "-", "to_world", "-", "#DEFRAG", "defrag_start", "defrag_checkpoint", "defrag_finish",
		"defrag_speed", "defrag_shooter", "defrag_check", "play_defrag" } },
	{ "build", { "play", "play_here", "play_defrag", "-", "sky_sun", "-", "compile", "compile_stop", "preview", "-", "check_map", "frame_leak", "warnings" } },
	{ "help", { "guide", "keymap", "search" } },
};

int MenuIndex( const std::string &name ) {
	for ( int i = 0; i < int( sizeof( MENUS ) / sizeof( MENUS[0] ) ); i++ ) {
		if ( name == MENUS[i].name ) {
			return i;
		}
	}
	return -1;
}

// A row's mark: what's on now (a tool in use, the texture lock, four views, a kind shown, the open tab)
bool MenuChecked( const std::string &a ) {
	const oz::ed::Session &s = session();
	static const char *filters[5] = { "filter_tools", "filter_triggers", "filter_sky", "filter_entities", "filter_curves" };
	for ( int i = 0; i < 5; i++ ) {
		if ( a == filters[i] ) {
			return !( s.filters & ( 1u << i ) );
		}
	}
	if ( a.compare( 0, 5, "show_" ) == 0 && a != "show_all" ) {
		return panel == a.substr( 5 );
	}
	return ( a == "texture_lock" && s.texture_lock ) || ( a == "views" && s.layout == 4 ) || ( a == "preview" && s.previewing() ) ||
		( a == "select" && s.tool == oz::ed::Tool::Select ) || ( a == "create" && s.tool == oz::ed::Tool::Create ) ||
		( a == "clip" && s.tool == oz::ed::Tool::Clip ) || ( a == "faces" && s.tool == oz::ed::Tool::Faces );
}

// Greyed: an action that has nothing to work on now (it still runs, and says why it did nothing)
bool MenuEnabled( const std::string &a ) {
	oz::ed::Session &s = session();
	const bool sel = !s.sel.empty();
	if ( a.compare( 0, 6, "curve_" ) == 0 ) {
		return sel;
	}
	if ( a.compare( 0, 4, "tex_" ) == 0 || a == "uv" || a == "paint" ) {
		return !s.target_faces().empty() || !s.target_patches().empty();
	}
	static const std::set<std::string> needSel = { "delete", "duplicate", "copy", "cut", "rotate_left", "rotate_right", "rotate_left_15",
		"rotate_right_15", "flip_x", "flip_y", "flip_z", "subtract", "hollow", "merge", "hide", "hide_others", "lock", "group", "ungroup",
		"frame_selection", "to_world" };
	if ( needSel.count( a ) ) {
		return sel || ( a == "delete" && !s.corners.empty() );
	}
	return a == "undo" ? !s.doc.undo_label().empty() : a == "redo" ? !s.doc.redo_label().empty() : a == "compile_stop" ? s.compiling()
		: a == "frame_leak" ? s.leaking() : a == "warnings" ? !s.compile_warnings().empty() : a == "show_all" ? !s.hidden.empty()
		: a == "unlock_all" ? !s.locked.empty() : true;
}

// The open menu's rows, made again when what they show changes
void BuildMenu( Rml::ElementDocument *doc ) {
	const int m = MenuIndex( menuOpen );
	if ( m < 0 ) {
		return;
	}
	oz::ed::Session &s = session();
	const Rml::String key = menuOpen + "|" + std::to_string( menuAt ) + "|" + std::to_string( s.sel_rev ) + "|" + std::to_string( s.doc.revision ) + "|" +
		std::to_string( (int)s.tool ) + std::to_string( s.texture_lock ) + std::to_string( s.layout ) + std::to_string( s.filters ) +
		std::to_string( s.previewing() ) + std::to_string( s.compiling() ) + std::to_string( s.leaking() ) + "|" + panel + "|" +
		std::to_string( s.keymap_rev ) + "|" + std::to_string( s.compile_warnings().size() ) + std::to_string( s.hidden.size() ) +
		std::to_string( s.locked.size() ) + std::to_string( s.show_rev );
	if ( key == lastMenu ) {
		return;
	}
	lastMenu = key;
	menuRows.clear();
	Rml::String rml;
	for ( const char *row : MENUS[m].rows ) {
		const std::string r = row;
		if ( r == "-" ) {
			rml += "<div class=\"msep\"></div>";
			continue;
		}
		if ( r[0] == '#' ) {
			rml += "<div class=\"mhead\">" + Enc( r.substr( 1 ) ) + "</div>";
			continue;
		}
		const oz::ed::Action *a = oz::ed::find_action( r );
		if ( !a ) {
			continue;
		}
		const int i = int( menuRows.size() );
		menuRows.push_back( r );
		const std::string keys = s.keys_of( *a );
		rml += "<button class=\"mi" + Rml::String( i == menuAt ? " on" : "" ) + ( MenuEnabled( r ) ? "" : " off" ) + "\" id=\"ed-mi-" + std::to_string( i ) +
			"\" data-action=\"" + r + "\" onclick=\"append ed panel menu_run " + r + "\"><span class=\"mc" + ( MenuChecked( r ) ? " chk" : "" ) +
			"\"></span><span class=\"ml\">" + Enc( a->label ) + "</span><span class=\"mk\">" + Enc( keys.substr( 0, keys.find( ", " ) ) ) + "</span></button>";
	}
	SetRml( doc, "ed-menu", rml );
}

void CloseMenu( Rml::ElementDocument *doc ) {
	if ( menuOpen.empty() ) {
		return;
	}
	if ( Rml::Element *t = doc->GetElementById( "mt-" + menuOpen ) ) {
		t->SetClass( "on", false );
	}
	menuOpen.clear();
	menuRows.clear();
	lastMenu.clear();
	Show( doc, "ed-menu", false );
}

// A menu opened under its title; at: the row highlighted for the keys (an action, "" none)
void OpenMenu( Rml::ElementDocument *doc, const std::string &name, const std::string &at = "" ) {
	const int m = MenuIndex( name );
	if ( m < 0 ) {
		return;
	}
	CloseMenu( doc );
	if ( Rml::Element *f = TypingIn() ) {
		f->Blur();
	}
	menuOpen = name;
	menuAt = -1;
	int i = 0;
	for ( const char *r : MENUS[m].rows ) {
		if ( r[0] != '-' && r[0] != '#' ) {
			menuAt = at == r ? i : menuAt;
			i++;
		}
	}
	Rml::Element *title = doc->GetElementById( "mt-" + name );
	Rml::Element *menu = doc->GetElementById( "ed-menu" );
	if ( !title || !menu ) {
		return;
	}
	title->SetClass( "on", true );
	menu->SetProperty( "left", Rml::CreateString( "%dpx", (int)title->GetAbsoluteOffset( Rml::BoxArea::Border ).x ) );
	Show( doc, "ed-menu", true );
	BuildMenu( doc );
}

// The highlighted row up or down (the first or last from none), or the menu left or right
void StepMenu( Rml::ElementDocument *doc, int rows, int menus ) {
	if ( menus ) {
		const int n = int( sizeof( MENUS ) / sizeof( MENUS[0] ) );
		OpenMenu( doc, MENUS[( MenuIndex( menuOpen ) + menus + n ) % n].name );
		menuAt = 0;
		return;
	}
	const int n = int( menuRows.size() );
	if ( n ) {
		menuAt = menuAt < 0 ? ( rows > 0 ? 0 : n - 1 ) : ( menuAt + rows + n ) % n;
	}
}

void RunMenuRow( Rml::ElementDocument *doc, const std::string &action ) {
	CloseMenu( doc );
	session().notify = Notify;
	session().run( action, {} );
}

// The pointer on the menus: a row or a title
bool InsideMenus() {
	for ( Rml::Element *e = GetContext()->GetHoverElement(); e; e = e->GetParentNode() ) {
		if ( e->GetId() == "ed-menu" || e->GetId() == "ed-menubar" ) {
			return true;
		}
	}
	return false;
}

// While a menu is open: the pointer over another title opens that one, over a row highlights it
void TrackMenu( Rml::ElementDocument *doc ) {
	for ( Rml::Element *e = GetContext()->GetHoverElement(); e; e = e->GetParentNode() ) {
		const Rml::String &id = e->GetId();
		if ( id.compare( 0, 3, "mt-" ) == 0 && id.substr( 3 ) != menuOpen ) {
			OpenMenu( doc, id.substr( 3 ) );
			return;
		}
		if ( id.compare( 0, 6, "ed-mi-" ) == 0 ) {
			menuAt = atoi( id.c_str() + 6 );
			break;
		}
	}
	BuildMenu( doc );
}

// ---------------------------------------------------------------- Save as, the getting started card

void CloseSaveAs( Rml::ElementDocument *doc ) {
	saveAsOpen = false;
	session().save_as_cancelled();  // closed without saving: nothing waits on it any more
	Show( doc, "ed-saveas", false );
	if ( TypingInside( doc->GetElementById( "ed-saveas" ) ) ) {
		TypingIn()->Blur();
	}
}

void OpenSaveAs( Rml::ElementDocument *doc ) {
	saveAsOpen = true;
	lastSaveAsNote.clear();
	Show( doc, "ed-saveas", true );
	SetInput( doc, "ed-saveas-name", session().name == "untitled" ? "" : session().name );
	doc->UpdateDocument();
	if ( Rml::Element *f = doc->GetElementById( "ed-saveas-name" ) ) {
		f->Focus();
	}
}

// what the name typed saves as: where, and a map already there that it replaces
void SaveAsNote( Rml::ElementDocument *doc ) {
	const std::string typed = InputValue( doc, "ed-saveas-name" );
	// the name as Save As keeps it (letters, digits, - _ and .), so the note checks the file it will write
	std::string n = typed;
	if ( n.size() > 4 && n.compare( n.size() - 4, 4, ".map" ) == 0 ) {
		n.resize( n.size() - 4 );
	}
	n.erase( std::remove_if( n.begin(), n.end(), []( unsigned char c ) { return !isalnum( c ) && c != '_' && c != '-' && c != '.'; } ), n.end() );
	const std::string file = session().maps_dir() + "/" + n + ".map";
	const bool there = !n.empty() && file != session().path && OzEd_FileExists( file.c_str() );
	const bool cut = !n.empty() && n != typed && n + ".map" != typed;
	SetText( doc, "ed-saveas-note", there ? n + ".map is there already: Save replaces it."
		: cut ? "Saves as " + n + ".map: a name keeps letters, digits, - and _ only."
		: "Saved in your maps (oz/editor/maps in your Oz Arena folder). Letters, digits, - and _.", lastSaveAsNote );
}

void RunSaveAs( Rml::ElementDocument *doc ) {
	const std::string n = InputValue( doc, "ed-saveas-name" );
	if ( n.empty() ) {
		Notify( "Type a name for the map first", true );
		return;
	}
	saveAsOpen = false;
	Show( doc, "ed-saveas", false );
	if ( TypingInside( doc->GetElementById( "ed-saveas" ) ) ) {
		TypingIn()->Blur();
	}
	session().notify = Notify;
	session().run( "save_as", { n } );
}

std::string GuideSeenFile() {
	return std::string( OzEd_HomePath() ) + "/oz/editor/guide_seen.txt";
}

void OpenGuide( Rml::ElementDocument *doc ) {
	guideOpen = true;
	CloseMenu( doc );
	Show( doc, "ed-guide", true );
	doc->UpdateDocument();
	if ( Rml::Element *b = doc->GetElementById( "ed-guide-ok" ) ) {
		b->Focus();
	}
}

// closed: it doesn't show by itself again (Help > Getting started, F1)
void CloseGuide( Rml::ElementDocument *doc ) {
	guideOpen = false;
	Show( doc, "ed-guide", false );
	const std::string seen = GuideSeenFile();
	if ( !OzEd_FileExists( seen.c_str() ) ) {
		OzEd_MakeDirs( seen.c_str() );
		OzEd_WriteOSFile( seen.c_str(), "seen\n", 5 );
	}
}

// ---------------------------------------------------------------- tips

// A button under the pointer a moment: its tip beside it (the tool strip's to the right, the rest below, or
// above near the bottom): a data-hint's text, or the action's name, its keys and what it does
void UpdateTip( Rml::ElementDocument *doc, int width, int height ) {
	Rml::Element *tip = doc->GetElementById( "ed-tip" );
	if ( !tip ) {
		return;
	}
	Rml::Element *target = nullptr;
	bool strip = false;
	// none over a menu, a list or a question: those say what they do themselves
	const bool covered = !menuOpen.empty() || searchOpen || pickOpen || saveAsOpen || guideOpen || keymapOpen || skyOpen || warnsOpen || !session().asking.empty();
	if ( !covered && !session().looking() && !session().dragging() ) {
		for ( Rml::Element *e = GetContext()->GetHoverElement(); e && e != doc; e = e->GetParentNode() ) {
			const Rml::String &id = e->GetId();
			if ( id == "viewport" || id == "ed-menu" || id == "ed-tip" ) {
				break;
			}
			if ( !target && e->GetTagName() == "button" && ( e->HasAttribute( "data-action" ) || e->HasAttribute( "data-hint" ) ) ) {
				target = e;
			}
			strip |= target && id == "ed-strip";
		}
	}
	const int now = OzE_Milliseconds();
	if ( target != tipFor ) {
		tipFor = target;
		tipSince = now;
		tipDone = false;
		if ( tipShown ) {
			tipShown = false;
			tip->SetClass( "hidden", true );
		}
		return;
	}
	if ( !target || tipShown || tipDone || now - tipSince < 450 ) {
		return;
	}
	Rml::String title, help;
	const Rml::String hint = target->GetAttribute<Rml::String>( "data-hint", "" );
	const oz::ed::Action *a = oz::ed::find_action( target->GetAttribute<Rml::String>( "data-action", "" ) );
	if ( !hint.empty() ) {
		title = Enc( hint );
	} else if ( a ) {
		const std::string keys = session().keys_of( *a );
		title = Enc( a->label ) + ( keys.empty() ? Rml::String() : "<span>" + Enc( keys ) + "</span>" );
		help = Enc( a->help );
	} else {
		return;
	}
	SetRml( doc, "ed-tip-title", title );
	SetRml( doc, "ed-tip-help", help );
	if ( Rml::Element *h = doc->GetElementById( "ed-tip-help" ) ) {
		h->SetClass( "none", help.empty() );
	}
	tip->SetClass( "hidden", false );
	tipShown = true;
	doc->UpdateDocument();	// laid out, to be placed by its size
	const float gap = 6 * std::max( 0.01f, GetContext()->GetDensityIndependentPixelRatio() );
	const Rml::Vector2f p = target->GetAbsoluteOffset( Rml::BoxArea::Border ), size = target->GetBox().GetSize( Rml::BoxArea::Border );
	const Rml::Vector2f ts = tip->GetBox().GetSize( Rml::BoxArea::Border );
	float x = strip ? p.x + size.x + gap : p.x, y = strip ? p.y : p.y + size.y + gap;
	if ( y + ts.y > height - gap ) {
		y = p.y - ts.y - gap;
	}
	x = std::max( gap, std::min( x, width - ts.x - gap ) );
	tip->SetProperty( "left", Rml::CreateString( "%dpx", (int)x ) );
	tip->SetProperty( "top", Rml::CreateString( "%dpx", (int)y ) );
}

void HideTip( Rml::ElementDocument *doc ) {
	tipDone = true;
	if ( tipShown ) {
		tipShown = false;
		Show( doc, "ed-tip", false );
	}
}

// ---------------------------------------------------------------- the page's own actions

void ShowPanel( Rml::ElementDocument *doc ) {
	if ( panel == shownPanel ) {
		return;
	}
	shownPanel = panel;
	static const char *names[5] = { "inspect", "materials", "entities", "objects", "history" };
	for ( const char *n : names ) {
		Show( doc, ( Rml::String( "panel-" ) + n ).c_str(), panel == n );
		if ( Rml::Element *t = doc->GetElementById( Rml::String( "tab-" ) + n ) ) {
			t->SetClass( "on", panel == n );
		}
	}
}

// The panel's typed values and its own controls: ed move_typed, rotate_typed <axis>, size_typed,
// material_typed, tex_typed, fit_typed, key_typed, key_name_typed, key_add, search_run, saveas_run (field:
// the text field whose Enter applied it), and ed panel <inspect | materials | objects | history | fit |
// key_add | key_remove_at <i> | mats <view> | mpage <+-1> | opage <+-1> | expand <id> | search_pick <row> |
// search_text <text> | keymap_close | menu <name> | menu_run <action> | menu_close | saveas_run |
// saveas_text <name> | saveas_close | guide_close | guide_keys>. Test hook: ed press <key> [mods], a key as the page gets it,
// with modifiers as if held (1 Shift, 2 Ctrl, 4 Alt).
bool Typed( const std::string &what, const std::vector<std::string> &args, Rml::Element *field = nullptr ) {
	Rml::ElementDocument *doc = EditorDoc();
	if ( !doc ) {
		return false;
	}
	oz::ed::Session &s = session();
	auto fieldKey = [&]() -> std::string {
		const int i = field ? field->GetAttribute<int>( "data-key-index", -1 ) : -1;
		return i >= 0 && i < int( shownKeys.size() ) ? shownKeys[size_t( i )] : "";
	};
	auto value = [&]() {
		std::string v = field ? InputValue( doc, field->GetId().c_str() ) : "";
		v.erase( std::remove( v.begin(), v.end(), '"' ), v.end() );	// a .map value can't hold one
		return v;
	};
	if ( what == "move_typed" ) {
		return s.run( "move", { InputValue( doc, "ed-mx" ), InputValue( doc, "ed-my" ), InputValue( doc, "ed-mz" ) } );
	}
	if ( what == "rotate_typed" ) {
		return s.run( "rotate", { InputValue( doc, "ed-deg" ), args.empty() ? "z" : args[0] } );
	}
	if ( what == "size_typed" ) {
		return s.run( "size", { InputValue( doc, "ed-sw" ), InputValue( doc, "ed-sd" ), InputValue( doc, "ed-sh" ) } );
	}
	if ( what == "material_typed" ) {
		return s.run( "material", { InputValue( doc, "ed-material" ) } );
	}
	if ( what == "tex_typed" ) {
		const std::string v = value();
		return v.empty() || s.run( "tex_set", { field->GetAttribute<Rml::String>( "data-field", "" ), v } );
	}
	if ( what == "fit_typed" ) {
		return s.run( "tex_fit", { InputValue( doc, "ed-fit-s" ), InputValue( doc, "ed-fit-t" ) } );
	}
	if ( what == "sky_typed" ) {
		const std::string f = field ? field->GetAttribute<Rml::String>( "data-sky-field", "" ) : "";
		lastSky.clear();	// shown again as it is, the value refused or not
		return f.empty() || s.run( "sky", { f, value() } );
	}
	if ( what == "key_typed" ) {
		const std::string k = fieldKey();
		return k.empty() || s.run( "key_set", { k, value() } );
	}
	if ( what == "key_name_typed" ) {
		const std::string k = fieldKey(), v = value();
		return k.empty() || v == k || s.run( "key_rename", { k, v } );
	}
	if ( what == "prefab_typed" ) {
		const std::string n = InputValue( doc, "ed-prefab-name" );
		if ( s.prefab_save( n ) ) {
			SetInput( doc, "ed-prefab-name", "" );
		}
		return true;
	}
	if ( what == "search_run" ) {
		RunFound( doc, foundAt );
		return true;
	}
	if ( what == "pick_run" ) {
		RunPick( doc, pickAt, true );
		return true;
	}
	if ( what == "saveas_run" ) {
		RunSaveAs( doc );
		return true;
	}
	if ( what == "press" && !args.empty() ) {
		const int k = OzE_KeyNum( args[0].c_str() );
		if ( k < 0 ) {
			Printf( "ed press: no key %s\n", args[0].c_str() );
			return true;
		}
		s.force_mods( args.size() > 1 ? atoi( args[1].c_str() ) : 0 );
		if ( !Editor_Key( k, true ) ) {
			Printf( "ed press: %s went to the page\n", args[0].c_str() );
		}
		Editor_Key( k, false );
		s.force_mods( -1 );
		return true;
	}
	if ( what == "key_add" ) {
		std::string k = InputValue( doc, "ed-newkey" ), v = InputValue( doc, "ed-newval" );
		k.erase( std::remove( k.begin(), k.end(), '"' ), k.end() );
		v.erase( std::remove( v.begin(), v.end(), '"' ), v.end() );
		if ( k.empty() ) {
			s.run( "key_set", {} );	// says what it needs
			return true;
		}
		s.run( "key_set", { k, v } );
		SetInput( doc, "ed-newkey", "" );
		SetInput( doc, "ed-newval", "" );
		return true;
	}
	if ( what != "panel" || args.empty() ) {
		return false;
	}
	const std::string &a = args[0];
	if ( a == "inspect" || a == "materials" || a == "entities" || a == "objects" || a == "history" ) {
		panel = a;
	} else if ( a == "fit" ) {
		Typed( "fit_typed", {} );
	} else if ( a == "key_add" ) {
		Typed( "key_add", {} );
	} else if ( a == "key_remove_at" && args.size() > 1 ) {
		const int i = atoi( args[1].c_str() );
		if ( i >= 0 && i < int( shownKeys.size() ) ) {
			s.run( "key_remove", { shownKeys[size_t( i )] } );
		}
	} else if ( a == "mats" && args.size() > 1 ) {
		matView = args[1];
		for ( size_t i = 2; i < args.size(); i++ ) {
			matView += " " + args[i];
		}
		matPage = 0;
	} else if ( a == "mpage" && args.size() > 1 ) {
		matPage = std::max( 0, matPage + atoi( args[1].c_str() ) );
	} else if ( a == "opage" && args.size() > 1 ) {
		objPage = std::max( 0, objPage + atoi( args[1].c_str() ) );
	} else if ( a == "expand" && args.size() > 1 ) {
		const int id = atoi( args[1].c_str() );
		if ( !expanded.erase( id ) ) {
			expanded.insert( id );
		}
	} else if ( a == "search_pick" && args.size() > 1 ) {
		RunFound( doc, atoi( args[1].c_str() ) );
	} else if ( a == "search_text" ) {
		std::string text;
		for ( size_t i = 1; i < args.size(); i++ ) {
			text += ( i > 1 ? " " : "" ) + args[i];
		}
		SetInput( doc, "ed-search-text", text );
	} else if ( a == "prefab_save" ) {
		Typed( "prefab_typed", {} );
	} else if ( ( a == "swatch" || a == "angle" ) && args.size() > 2 ) {
		// a palette color, a compass direction (or the camera's way, to 15 degrees)
		std::string v = args[2];
		if ( a == "swatch" ) {
			const size_t n = size_t( atoi( args[2].c_str() ) );
			v = n < sizeof( PALETTE ) / sizeof( PALETTE[0] ) ? PALETTE[n].rgb : "1 1 1";
		} else if ( v == "view" ) {
			const double yaw = std::fmod( std::fmod( std::round( s.cam.yaw / 15 ) * 15, 360.0 ) + 360.0, 360.0 );
			v = Num( yaw );
		}
		s.run( "key_set", { args[1], v } );
	} else if ( a == "fav" ) {
		ToggleFavorite( args.size() > 1 ? args[1] : s.shader );
	} else if ( a == "uv_close" ) {
		CloseUv( doc );
	} else if ( a == "uv_fit" ) {
		uvFit = true;
	} else if ( a == "uv_drag" && args.size() > 2 ) {
		// the tests' drag: repeats across and down, let go as a drag in the view is
		UvApply( atof( args[1].c_str() ) * uvScale, atof( args[2].c_str() ) * uvScale );
	} else if ( a == "uv_zoom" && args.size() > 1 ) {
		uvScale = std::clamp( uvScale * atof( args[1].c_str() ), 8.0, 4000.0 );
	} else if ( a == "advanced" ) {
		advancedOpen = !advancedOpen;
		lastInspect.clear();
	} else if ( a == "colors" && args.size() > 1 ) {
		colorGridKey = colorGridKey == args[1] ? "" : args[1];
		lastInspect.clear();	// the key rows made again with the grid, or without
	} else if ( a == "rgb" && args.size() > 4 ) {
		s.run( "key_set", { args[1], args[2] + " " + args[3] + " " + args[4] } );
	} else if ( a == "light_slide" && args.size() > 1 ) {
		// a light's slider moved: its number shows now, the key is set once it's let go (LightSlideTick)
		const std::string what = args[1];
		const Rml::String id = "ed-ls-" + what;
		const std::vector<oz::ed::Id> ents = s.key_entities();
		const oz::ed::Object *o = ents.empty() ? nullptr : s.doc.get( ents[0] );
		if ( !o || !doc->GetElementById( id ) ) {
			return true;
		}
		const double v = args.size() > 2 ? atof( args[2].c_str() ) : atof( InputValue( doc, id.c_str() ).c_str() );
		SetRml( doc, ( id + "-n" ).c_str(), LightSlideText( what, v, oz::ed::light_numbers( o->entity ) ) );
		lightSlide.what = what;
		lightSlide.value = v;
		lightSlide.at = OzE_Milliseconds();
	} else if ( a == "step" && args.size() > 2 ) {
		// a number key a step up or down, from what it is (or the game's default)
		const std::vector<oz::ed::Id> ents = s.key_entities();
		const oz::ed::Object *o = ents.empty() ? nullptr : s.doc.get( ents[0] );
		const oz::ed::KeyDef *kd = o ? oz::ed::key_def( o->entity.classname(), args[1] ) : nullptr;
		const std::string now = o ? o->entity.get( args[1], kd ? kd->def : "0" ) : "0";
		const double v = std::max( 0.0, atof( now.c_str() ) + atof( args[2].c_str() ) );
		s.run( "key_set", { args[1], Num( v ) } );
	} else if ( a == "bool" && args.size() > 1 ) {
		// on is "1"; off takes the key out (the game reads a missing one as off)
		const std::vector<oz::ed::Id> ents = s.key_entities();
		const oz::ed::Object *o = ents.empty() ? nullptr : s.doc.get( ents[0] );
		if ( o && o->entity.get( args[1] ) == "1" ) {
			s.run( "key_remove", { args[1] } );
		} else {
			s.run( "key_set", { args[1], "1" } );
		}
	} else if ( a == "listen" && args.size() > 1 ) {
		const std::vector<oz::ed::Id> ents = s.key_entities();
		const oz::ed::Object *o = ents.empty() ? nullptr : s.doc.get( ents[0] );
		std::string v = o ? o->entity.get( args[1] ) : "";
		v = v.substr( 0, v.find( ' ' ) );	// music: its intro
		if ( v.empty() || v[0] == '*' ) {
			Notify( v.empty() ? "No sound set yet: Pick one" : "A player's own sound: it plays as theirs in the game", false );
		} else {
			OzE_InsertText( ( "play " + v + "\n" ).c_str() );
			Notify( "Playing " + v, false );
		}
	} else if ( a == "pick" && args.size() > 2 ) {
		OpenPick( doc, args[1], args[2] );
	} else if ( a == "pick_row" && args.size() > 1 ) {
		RunPick( doc, atoi( args[1].c_str() ) );
	} else if ( a == "pick_text" ) {
		std::string text;
		for ( size_t i = 1; i < args.size(); i++ ) {
			text += ( i > 1 ? " " : "" ) + args[i];
		}
		SetInput( doc, "ed-pick-text", text );
	} else if ( a == "keymap_close" ) {
		CloseKeymap( doc );
	} else if ( a == "sky_close" ) {
		CloseSky( doc );
	} else if ( a == "menu" && args.size() > 1 ) {
		// a menu's title clicked: it opens, or closes when it's the one open
		if ( menuOpen == args[1] ) {
			CloseMenu( doc );
		} else {
			OpenMenu( doc, args[1] );
		}
	} else if ( a == "menu_run" && args.size() > 1 ) {
		RunMenuRow( doc, args[1] );
	} else if ( a == "menu_close" ) {
		CloseMenu( doc );
	} else if ( a == "saveas_run" ) {
		RunSaveAs( doc );
	} else if ( a == "saveas_text" ) {
		SetInput( doc, "ed-saveas-name", args.size() > 1 ? args[1] : "" );
	} else if ( a == "saveas_close" ) {
		CloseSaveAs( doc );
	} else if ( a == "guide_close" ) {
		CloseGuide( doc );
	} else if ( a == "guide_keys" ) {
		CloseGuide( doc );
		OpenKeymap( doc );
	} else if ( a == "warning_pick" && args.size() > 1 ) {
		CloseWarnings( doc );
		s.run( "warning", { args[1] } );
	} else {
		return false;
	}
	return true;
}

void Ed_f() {
	if ( OzE_Argc() < 2 ) {
		Printf( "ed <action> [args]: the map editor's actions:\n" );
		for ( const oz::ed::Action &a : oz::ed::actions() ) {
			Printf( "  %-16s %-14s %s\n", a.name, session().keys_of( a ).c_str(), a.label );
		}
		return;
	}
	std::vector<std::string> args;
	for ( int i = 2; i < OzE_Argc(); i++ ) {
		args.push_back( OzE_Argv( i ) );
	}
	session().notify = Notify;
	const std::string what = OzE_Argv( 1 );
	if ( !Typed( what, args ) && !session().run( what, args ) ) {
		Printf( "ed: no action %s (ed lists them)\n", what.c_str() );
	}
}

void Open_f() {
	std::string err;
	if ( OzE_Argc() < 2 ) {
		Printf( "ed_open <file.map>: opens a .map file in the map editor\n" );
	} else if ( !session().open( OzE_ArgsFrom( 1 ), err ) ) {
		Report( err );
	}
}

void Import_f() {
	std::string err;
	if ( OzE_Argc() < 2 ) {
		Printf( "ed_import <map>: a map in the game's files, decompiled by q3map2, in the map editor\n" );
	} else if ( !session().import_bsp( OzE_Argv( 1 ), err, true ) ) {
		Report( err );
	}
}

// ed_save [<file.map>]: the console's, never asks (a map never saved goes to the maps folder by its name)
void Save_f() {
	oz::ed::Session &s = session();
	s.run( "save", { OzE_Argc() > 1 ? std::string( OzE_ArgsFrom( 1 ) ) : !s.path.empty() ? s.path : s.maps_dir() + "/" + s.name + ".map" } );
}

void New_f() {
	session().new_map();
}

void Camera_f() {
	if ( OzE_Argc() < 4 ) {
		const oz::ed::Camera &c = session().cam;
		Printf( "ed_camera %.0f %.0f %.0f %.1f %.1f\n", c.origin.x, c.origin.y, c.origin.z, c.yaw, c.pitch );
		return;
	}
	oz::ed::Camera &c = session().cam;
	c.origin = oz::ed::Vec3( atof( OzE_Argv( 1 ) ), atof( OzE_Argv( 2 ) ), atof( OzE_Argv( 3 ) ) );
	if ( OzE_Argc() > 5 ) {
		c.yaw = atof( OzE_Argv( 4 ) );
		c.pitch = atof( OzE_Argv( 5 ) );
	}
}

// ed_stats: the map, the view, the selection; the first face's mapping and the first entity's keys
// the inspector shows (for the tests)
void Stats_f() {
	oz::ed::Session &s = session();
	std::string picked;
	for ( oz::ed::Id id : s.sel ) {
		picked += " " + std::to_string( id );
	}
	const oz::ed::Bounds b = s.selection_bounds();
	char box[160] = "";
	if ( !b.empty() ) {
		snprintf( box, sizeof box, "; bounds %g %g %g %g %g %g", b.lo.x, b.lo.y, b.lo.z, b.hi.x, b.hi.y, b.hi.z );
	}
	Printf( "ed_stats: %s; %s; %s; selected:%s%s; material %s; undo %d redo %d; compile %s\n", s.name.c_str(), s.status().c_str(), s.render_stats().c_str(),
		picked.c_str(), box, s.shader.c_str(), (int)s.doc.undo_size(), (int)s.doc.redo_size(), s.compile_report().empty() ? "none" : s.compile_report().c_str() );
	if ( !s.asking.empty() ) {
		Printf( "ed_stats asking: %s\n", s.asking.c_str() );
	}
	Printf( "ed_stats panes: %d %s %s\n", s.layout, oz::ed::pane_name( s.solo ), oz::ed::pane_name( s.active_pane() ) );
	std::string kept;
	if ( s.recovery_offer( kept ) ) {
		Printf( "ed_stats recovery: %s\n", kept.c_str() );
	}
	// what's open over the view, and the player's own keys
	if ( searchOpen ) {
		std::string rows;
		for ( const std::string &f : found ) {
			rows += " " + f;
		}
		Printf( "ed_stats page: search \"%s\"%s; at %d\n", searchQuery.c_str(), rows.c_str(), foundAt );
	}
	if ( keymapOpen ) {
		Printf( "ed_stats page: keys%s\n", s.binding.empty() ? "" : ( " binding " + s.binding ).c_str() );
	}
	if ( warnsOpen ) {
		Printf( "ed_stats page: warnings\n" );
	}
	if ( skyOpen ) {
		Printf( "ed_stats page: sky and sun\n" );
	}
	if ( pickOpen ) {
		Printf( "ed_stats page: pick %s %s \"%s\" %d; at %d\n", pickKey.c_str(), pickType.c_str(), pickQuery.c_str(), (int)pickFound.size(), pickAt );
	}
	if ( uvOpen ) {
		Printf( "ed_stats page: uv %g\n", uvScale );
	}
	if ( !menuOpen.empty() ) {
		Printf( "ed_stats page: menu %s %d rows; at %s\n", menuOpen.c_str(), (int)menuRows.size(),
			menuAt >= 0 && menuAt < int( menuRows.size() ) ? menuRows[size_t( menuAt )].c_str() : "none" );
	}
	if ( saveAsOpen ) {
		Printf( "ed_stats page: save as\n" );
	}
	if ( guideOpen ) {
		Printf( "ed_stats page: guide\n" );
	}
	if ( const std::vector<oz::ed::Id> pe = s.key_entities(); !pe.empty() ) {
		double speed, up, across, secs;
		if ( s.push_flight( pe[0], speed, up, across, secs ) ) {
			Printf( "ed_stats push: %.0f %.0f %.0f %.1f\n", speed, up, across, secs );
		}
	}
	const std::string course = s.course_summary();
	if ( course != "no course" ) {
		Printf( "ed_stats course: %s\n", course.c_str() );
	}
	for ( size_t i = 0; i < s.compile_warnings().size(); i++ ) {
		const oz::ed::Session::CompileWarning &w = s.compile_warnings()[i];
		std::string ids;
		for ( oz::ed::Id id : w.ids ) {
			ids += " " + std::to_string( id );
		}
		Printf( "ed_stats warnings: %d; %s;%s\n", (int)s.compile_warnings().size(), w.text.c_str(), ids.c_str() );
	}
	std::string own;
	for ( const oz::ed::Action &a : oz::ed::actions() ) {
		const std::string keys = s.keys_of( a );
		if ( keys != a.keys ) {
			own += ( own.empty() ? "" : "; " ) + std::string( a.name ) + "=" + keys;
		}
	}
	if ( !own.empty() ) {
		Printf( "ed_stats keymap: %s\n", own.c_str() );
	}
	const std::vector<oz::ed::FaceRef> faces = s.target_faces();
	if ( !faces.empty() ) {
		const oz::ed::Object *o = s.doc.get( faces[0].first );
		const oz::ed::Face &f = o->brush.faces[size_t( faces[0].second )];
		oz::ed::TexFields tf;
		const std::vector<oz::ed::Winding> &ws = s.doc.windings( faces[0].first );
		const bool ok = oz::ed::tex_fields( s.doc, f, size_t( faces[0].second ) < ws.size() ? ws[size_t( faces[0].second )] : oz::ed::Winding{},
			oz::ed::origin_for( s.doc, *o ), tf );
		Printf( "ed_stats face: %d %d %s %s shift %g %g rotate %g scale %g %g\n", faces[0].first, faces[0].second,
			oz::ed::shader_name( f.shader ).c_str(), ok ? "" : "(skewed)", tf.v[0], tf.v[1], tf.v[2], tf.v[3], tf.v[4] );
	}
	// the first selected curve: its size, which way it faces at its middle, its texture's range
	for ( oz::ed::Id id : s.sel ) {
		const oz::ed::Object *o = s.doc.get( id );
		if ( !o || o->kind != oz::ed::Kind::Patch || o->patch.ctrl.empty() ) {
			continue;
		}
		const oz::ed::Patch &pt = o->patch;
		const oz::ed::Vec3 n = oz::ed::curve_facing( pt );
		const double len = n.length() > 0 ? n.length() : 1;
		double lo[2] = { 1e30, 1e30 }, hi[2] = { -1e30, -1e30 };
		for ( size_t i = 0; i + 4 < pt.ctrl.size(); i += 5 ) {
			for ( int k = 0; k < 2; k++ ) {
				lo[k] = std::min( lo[k], pt.ctrl[i + 3 + k] ), hi[k] = std::max( hi[k], pt.ctrl[i + 3 + k] );
			}
		}
		Printf( "ed_stats curve: %d %dx%d %s; facing %.2f %.2f %.2f; st %g %g %g %g\n", id, pt.width, pt.height, oz::ed::shader_name( pt.shader ).c_str(),
			n.x / len, n.y / len, n.z / len, lo[0], hi[0], lo[1], hi[1] );
		break;
	}
	const std::vector<oz::ed::Id> ents = s.key_entities();
	if ( const oz::ed::Object *e = ents.empty() ? nullptr : s.doc.get( ents[0] ) ) {
		std::string keys;
		for ( const auto &kv : e->entity.keys ) {
			keys += " \"" + kv.first + "\" \"" + kv.second + "\"";
		}
		Printf( "ed_stats keys: %d%s\n", ents[0], keys.c_str() );
	}
}

// ed_classes [text]: the entity classes the editor knows (id's, CPMA's, Defrag's, the mods' .def
// files'), or those whose name has text, with their flags
void Classes_f() {
	session().load_defs();
	const std::string filter = OzE_Argc() > 1 ? Lower( OzE_Argv( 1 ) ) : "";
	int n = 0;
	for ( const oz::ed::EntityDef &d : oz::ed::entity_defs() ) {
		if ( !filter.empty() && Lower( d.name ).find( filter ) == std::string::npos ) {
			continue;
		}
		std::string flags;
		for ( const oz::ed::FlagDef &f : d.flags ) {
			flags += " [" + std::to_string( f.bit ) + " " + f.label + "]";
		}
		Printf( "  %s%s (%s)%s\n", d.name.c_str(), d.mod.empty() ? "" : ( " " + d.mod ).c_str(), d.brush ? "brushes" : "point", flags.c_str() );
		n++;
	}
	Printf( "ed_classes: %d of %d\n", n, (int)oz::ed::entity_defs().size() );
}

// ed_materials [text]: the texture browser's materials by folder, id's and the map packs', or those
// whose name has text
void Materials_f() {
	const std::vector<oz::ed::Material> &all = oz::ed::shaders_lib().materials();
	const std::string filter = OzE_Argc() > 1 ? Lower( OzE_Argv( 1 ) ) : "";
	std::map<std::string, int> folders[2];
	int stock = 0;
	for ( const oz::ed::Material &m : all ) {
		stock += m.stock;
		if ( filter.empty() ) {
			folders[m.stock ? 0 : 1][m.folder]++;
		} else if ( Lower( m.name ).find( filter ) != std::string::npos ) {
			Printf( "  %s%s  %s\n", m.name.c_str(), m.stock ? "" : " (map pack)", m.image.c_str() );
		}
	}
	Printf( "ed_materials: %d, %d of them id's and the tools\n", (int)all.size(), stock );
	for ( int k = 0; k < 2 && filter.empty(); k++ ) {
		std::string line = k ? "  map packs':" : "  id's:";
		for ( const auto &kv : folders[k] ) {
			line += " " + kv.first + " " + std::to_string( kv.second );
		}
		Printf( "%s\n", line.c_str() );
	}
}

void Leave_f() {
	leaving = true;
}

// ed_world [maps/<map>.bsp]: the lit preview's world, or none (the client's systems start again)
void World_f() {
	OzEd_LoadWorld( OzE_Argc() > 1 ? OzE_Argv( 1 ) : "" );
}

// ed_back: from a play-test (the pause page's Back to the map editor) to the editor, the map as it was
void Back_f() {
	OzE_InsertText( "disconnect\neditor\n" );	// before anything queued after it (a test script)
}

void Undo_f() {
	session().run( "undo", {} );
}

void Redo_f() {
	session().run( "redo", {} );
}

void Home_f() {
	session().frame_all();
}

// The hint for what the pointer is on: a control's own (data-hint), or its action's name, keys and
// what it does (data-action), else the view's.
Rml::String HoverHint() {
	for ( Rml::Element *e = GetContext()->GetHoverElement(); e; e = e->GetParentNode() ) {
		const Rml::String hint = e->GetAttribute<Rml::String>( "data-hint", "" );
		if ( !hint.empty() ) {
			return hint;
		}
		const Rml::String name = e->GetAttribute<Rml::String>( "data-action", "" );
		if ( name.empty() ) {
			continue;
		}
		if ( const oz::ed::Action *a = oz::ed::find_action( name ) ) {
			Rml::String text = a->label;
			const std::string keys = session().keys_of( *a );
			if ( !keys.empty() ) {
				text += " (" + keys + ")";
			}
			return text + ": " + a->help;
		}
	}
	return session().hint();
}

}  // namespace

namespace OzUI {

bool EditorShortcut( int key );

void Editor_Commands() {
	OzE_CvarCreate( "ed_q3map2", "", 0, "the q3map2.exe the map editor runs (empty: tools/q3map2.exe beside the game)",
		nullptr, nullptr, 0 );
	OzE_AddCommand( "editor", Editor_f );
	OzE_AddCommand( "ed", Ed_f );
	OzE_AddCommand( "ed_open", Open_f );
	OzE_AddCommand( "ed_import", Import_f );
	OzE_AddCommand( "ed_save", Save_f );
	OzE_AddCommand( "ed_new", New_f );
	OzE_AddCommand( "ed_camera", Camera_f );
	OzE_AddCommand( "ed_stats", Stats_f );
	OzE_AddCommand( "ed_leave", Leave_f );
	OzE_AddCommand( "ed_back", Back_f );
	OzE_AddCommand( "ed_world", World_f );
	OzE_AddCommand( "ed_materials", Materials_f );
	OzE_AddCommand( "ed_classes", Classes_f );
	OzE_AddCommand( "ed_undo", Undo_f );
	OzE_AddCommand( "ed_redo", Redo_f );
	OzE_AddCommand( "ed_home", Home_f );
}

bool Editor_Key( int key, bool down ) {
	Rml::ElementDocument *doc = EditorDoc();
	if ( !doc ) {
		return false;
	}
	if ( session().looking() || session().dragging() ) {
		session().key( key, down, true );
		return true;
	}
	if ( down && ( key == K_MOUSE1 || key == K_MOUSE2 || key == K_MOUSE3 ) ) {
		HideTip( doc );	// a click: its tip has done its work
	}
	if ( !session().asking.empty() ) {
		// the question is the page's: its buttons take Enter and the d-pad; Escape is Cancel
		if ( down && key == K_ESCAPE ) {
			session().answer( "cancel" );
			return true;
		}
		return key == K_MOUSE2 || key == K_BACKSPACE;
	}
	if ( keymapOpen ) {
		// the Keys dialog is the page's (its filter, its buttons); while an action waits for its keys,
		// the keys pressed are its (the left button still clicks)
		if ( !session().binding.empty() && key != K_MOUSE1 ) {
			session().key( key, down, false );
			return true;
		}
		if ( down && ( key == K_ESCAPE || key == K_PAD0_B ) ) {
			CloseKeymap( doc );
			return true;
		}
		if ( key == K_ESCAPE || key == K_PAD0_B || key == K_MOUSE2 ) {
			return true;
		}
		return !TypingIn() && ( key == K_BACKSPACE || key == 'q' || key == 'e' || key == 'f' || key == K_PAD0_LEFTSHOULDER || key == K_PAD0_RIGHTSHOULDER );
	}
	if ( skyOpen ) {
		// Sky and sun is the page's: Enter sets a typed value, Escape leaves a field, then closes it
		Rml::Element *field = TypingIn();
		if ( down && field && ( key == K_ENTER || key == K_KP_ENTER ) ) {
			const Rml::String apply = field->GetAttribute<Rml::String>( "data-apply", "" );
			if ( !apply.empty() ) {
				session().notify = Notify;
				Typed( apply, {}, field );
			}
			field->Blur();
			return true;
		}
		if ( down && ( key == K_ESCAPE || key == K_PAD0_B ) ) {
			if ( field ) {
				field->Blur();
			} else {
				CloseSky( doc );
			}
			return true;
		}
		// the left button: the dialog's own controls, else nothing (the menus and panels around wait)
		if ( key == K_MOUSE1 ) {
			for ( Rml::Element *e = doc->GetContext()->GetHoverElement(); e; e = e->GetParentNode() ) {
				if ( e->IsClassSet( "box" ) ) {
					return false;
				}
			}
			return true;
		}
		// the view beside the dialog: the right button looks around (the sun's arrow), the wheel moves
		const bool viewMouse = key == K_MOUSE2 || key == K_MOUSE3 || key == K_MWHEELUP || key == K_MWHEELDOWN;
		if ( viewMouse && ( OverView( doc ) || session().looking() || session().dragging() ) ) {
			return session().key( key, down, OverView( doc ) );
		}
		if ( key == K_ESCAPE || key == K_PAD0_B || key == K_MOUSE2 ) {
			return true;
		}
		return !field && ( key == K_BACKSPACE || key == 'q' || key == 'e' || key == 'f' || key == K_PAD0_LEFTSHOULDER || key == K_PAD0_RIGHTSHOULDER );
	}
	if ( warnsOpen && down && key == K_ESCAPE ) {
		CloseWarnings( doc );
		return true;
	}
	if ( guideOpen ) {
		// the card's: its buttons take clicks; Escape, Enter and F1 close it, and the pad's A and B
		if ( down && ( key == K_ESCAPE || key == K_ENTER || key == K_KP_ENTER || key == K_F1 || key == K_PAD0_A || key == K_PAD0_B ) ) {
			CloseGuide( doc );
			return true;
		}
		return key != K_MOUSE1;
	}
	if ( !menuOpen.empty() ) {
		// a menu's: the arrows and Enter work it, Escape or a click elsewhere closes it; any other key
		// closes it and does what it does
		const bool mouse = key == K_MOUSE1 || key == K_MOUSE2 || key == K_MOUSE3 || key == K_MWHEELUP || key == K_MWHEELDOWN;
		if ( mouse && InsideMenus() ) {
			return false;	// the page's: a row or a title clicked
		}
		if ( mouse || !down ) {
			if ( mouse && down ) {
				CloseMenu( doc );
			}
			return true;
		}
		if ( key == K_ESCAPE ) {
			CloseMenu( doc );
			return true;
		}
		if ( key == K_UPARROW || key == K_DOWNARROW || key == K_LEFTARROW || key == K_RIGHTARROW ) {
			StepMenu( doc, key == K_DOWNARROW ? 1 : key == K_UPARROW ? -1 : 0, key == K_RIGHTARROW ? 1 : key == K_LEFTARROW ? -1 : 0 );
			BuildMenu( doc );
			return true;
		}
		if ( key == K_ENTER || key == K_KP_ENTER ) {
			if ( menuAt >= 0 && menuAt < int( menuRows.size() ) ) {
				RunMenuRow( doc, menuRows[size_t( menuAt )] );
			}
			return true;
		}
		CloseMenu( doc );
	}
	if ( saveAsOpen && !TypingIn() ) {
		// the question's: Escape cancels; its buttons take clicks, Enter and the d-pad
		if ( down && key == K_ESCAPE ) {
			CloseSaveAs( doc );
			return true;
		}
		return !( key == K_MOUSE1 || key == K_ENTER || key == K_KP_ENTER || key == K_LEFTARROW || key == K_RIGHTARROW || key == K_UPARROW ||
			key == K_DOWNARROW );
	}
	const bool over = OverView( doc );
	if ( Rml::Element *field = TypingIn() ) {
		if ( ( key == K_MOUSE1 || key == K_MOUSE2 || key == K_MOUSE3 ) && over && down ) {
			field->Blur();	// a click in the view leaves the field
		} else if ( down && ( key == K_UPARROW || key == K_DOWNARROW ) && ( field->GetId() == "ed-search-text" || field->GetId() == "ed-pick-text" ) ) {
			// the search's or the pick list's highlight up and down its rows; the field keeps the focus
			int &at = field->GetId() == "ed-search-text" ? foundAt : pickAt;
			const int rows = int( field->GetId() == "ed-search-text" ? found.size() : pickFound.size() );
			at = std::max( 0, std::min( at + ( key == K_DOWNARROW ? 1 : -1 ), rows - 1 ) );
			return true;
		} else if ( down && ( key == K_ENTER || key == K_KP_ENTER ) ) {
			// Enter applies the field's row (its data-apply action) and leaves it
			const Rml::String apply = field->GetAttribute<Rml::String>( "data-apply", "" );
			if ( !apply.empty() ) {
				session().notify = Notify;
				Typed( apply, {}, field );
				field->Blur();
			}
			return true;
		} else if ( down && key == K_ESCAPE ) {
			field->Blur();
			if ( searchOpen ) {
				CloseSearch( doc );
			}
			if ( pickOpen ) {
				ClosePick( doc );
			}
			if ( saveAsOpen ) {
				CloseSaveAs( doc );
			}
			return true;
		} else if ( down && !saveAsOpen && !searchOpen && !pickOpen && EditorShortcut( key ) ) {
			// Ctrl+S, Ctrl+Z, F5 ... still work after typing in a field: it lets go of the keyboard, the
			// editor takes the key
			field->Blur();
			return session().key( key, down, over );
		} else {
			return false;
		}
	}
	if ( down && key == K_F10 ) {
		OpenMenu( doc, "file" );	// the menus from the keyboard (the arrows and Enter work them)
		return true;
	}
	if ( uvOpen && down && ( key == K_ESCAPE || ( key == 'u' && !OzE_KeyDown( K_CTRL ) && !OzE_KeyDown( K_ALT ) ) ) ) {
		CloseUv( doc );	// Escape closes the UV editor, U toggles it
		return true;
	}
	if ( session().key( key, down, over ) ) {
		return true;
	}
	// what the pages take as Back or a tab switch never leaves the editor: it has its own Main Menu
	return key == K_MOUSE2 || key == K_ESCAPE || key == K_BACKSPACE || key == 'q' || key == 'e' || key == 'f' ||
		key == K_PAD0_B || key == K_PAD0_LEFTSHOULDER || key == K_PAD0_RIGHTSHOULDER;
}

// A key the editor takes even from a text field: the F keys, and Ctrl with a key no text field uses
// (Ctrl+A, C, V, X and the arrows, Backspace, Delete, Home and End edit the text)
bool EditorShortcut( int key ) {
	if ( key >= K_F1 && key <= K_F12 ) {
		return key != K_F10;
	}
	if ( !OzE_KeyDown( K_CTRL ) ) {
		return false;
	}
	switch ( key ) {
	case 'a': case 'c': case 'v': case 'x': case K_LEFTARROW: case K_RIGHTARROW: case K_UPARROW: case K_DOWNARROW:
	case K_BACKSPACE: case K_DEL: case K_HOME: case K_END: case K_SHIFT: case K_CTRL: case K_ALT:
		return false;
	default:
		return key < K_MOUSE1 || key > K_MOUSE5;
	}
}

void Editor_Mouse( int x, int y ) {
	if ( EditorDoc() ) {
		session().pointer( x, y );
	}
}

bool Editor_Draw( int width, int height ) {
	if ( leaving ) {
		leaving = false;
		if ( IsPageOpen( "editor" ) ) {
			CloseAllPages();	// CL_Frame opens the main menu again
			return false;
		}
	}
	Rml::ElementDocument *doc = IsPageOpen( "editor" ) && OzE_ClientState() == 0 ? EditorDoc() : nullptr;
	if ( !doc ) {
		shownPanel.clear();	// the page is made again when it opens: everything shows anew
		lastInspect.clear();
		lastKeysRml.clear();
		lastMats.clear();
		lastObjects.clear();
		lastEntities.clear();
		lastHistory.clear();
		lastChain.clear();
		lastStatus.clear();
		lastName.clear();
		lastHint.clear();
		lastSizeTag.clear();
		lastTools.clear();
		lastKeyHelp.clear();
		lastPanes.clear();
		lastFilters = ~0u;
		lastSel = ~0ull;
		searchOpen = keymapOpen = warnsOpen = pickOpen = uvOpen = skyOpen = false;	// hidden in the page as made
		session().sky_open = false;
		saveAsOpen = guideOpen = tipShown = false;
		menuOpen.clear();
		lastMenu.clear();
		tipFor = nullptr;
		return false;
	}
	oz::ed::Session &s = session();
	s.notify = Notify;
	Rml::Element *view = doc->GetElementById( "viewport" );
	if ( !view ) {
		return true;
	}
	const Rml::Vector2f pos = view->GetAbsoluteOffset( Rml::BoxArea::Border );
	const Rml::Vector2f size = view->GetBox().GetSize( Rml::BoxArea::Border );
	s.frame( (int)pos.x, (int)pos.y, (int)size.x, (int)size.y );
	const bool noting = !note.empty() && OzE_Milliseconds() < noteUntil;
	if ( !s.renderer_ok() ) {
		SetText( doc, "ed-hint", "The map editor draws with the Oz renderer: Settings, Video, Renderer.", lastHint );
	} else {
		SetText( doc, "ed-hint", noting ? note : HoverHint(), lastHint );
	}
	if ( Rml::Element *hint = doc->GetElementById( "ed-hint" ) ) {
		hint->SetClass( "note", noting && !noteError );
		hint->SetClass( "err", noting && noteError );
	}
	SetText( doc, "ed-status", s.status(), lastStatus );
	if ( Rml::Element *tag = doc->GetElementById( "ed-size-tag" ) ) {
		// the size by the pointer, kept inside the view (to the pointer's left or above it at an edge)
		std::string text;
		double tx = 0, ty = 0;
		const bool shown = s.renderer_ok() && s.size_tag( text, tx, ty );
		tag->SetClass( "hidden", !shown );
		if ( shown ) {
			SetText( doc, "ed-size-tag", text, lastSizeTag );
			const Rml::Vector2f ts = tag->GetBox().GetSize( Rml::BoxArea::Border );
			if ( tx + ts.x > pos.x + size.x ) {
				tx -= ts.x + 36;
			}
			if ( ty + ts.y > pos.y + size.y ) {
				ty -= ts.y + 28;
			}
			tag->SetProperty( "left", Rml::CreateString( "%dpx", (int)tx ) );
			tag->SetProperty( "top", Rml::CreateString( "%dpx", (int)ty ) );
		}
	}
	SetText( doc, "ed-name", s.name + ( s.doc.dirty ? " *" : "" ), lastName );

	// the tool strip shows the tool in use (Light, Jump pad and Teleport the place tool's own), the
	// action bar the texture lock
	const Rml::String tools = Rml::CreateString( "%d %d %s", (int)s.tool, s.texture_lock ? 1 : 0, s.place_class.c_str() );
	if ( tools != lastTools ) {
		lastTools = tools;
		const bool place = s.tool == oz::ed::Tool::Place;
		const std::string &pc = s.place_class;
		const bool own = pc == "light" || pc == "jump_pad" || pc == "teleporter";
		static const char *ids[] = { "tool-select", "tool-create", "tool-clip", "tool-faces", "tool-place" };
		for ( int i = 0; i < 5; i++ ) {
			if ( Rml::Element *e = doc->GetElementById( ids[i] ) ) {
				e->SetClass( "on", (int)s.tool == i && !( place && own ) );
			}
		}
		const char *placers[3][2] = { { "tool-light", "light" }, { "tool-jump", "jump_pad" }, { "tool-tele", "teleporter" } };
		for ( const auto &p : placers ) {
			if ( Rml::Element *e = doc->GetElementById( p[0] ) ) {
				e->SetClass( "on", place && pc == p[1] );
			}
		}
		if ( Rml::Element *e = doc->GetElementById( "tool-lock" ) ) {
			e->SetClass( "on", s.texture_lock );
			e->SetInnerRML( s.texture_lock ? "TEXTURE LOCK ON" : "TEXTURE LOCK OFF" );
		}
	}
	// the panes' names over them (the 2D views', all four's in four), the one the pointer works in brighter
	const Rml::String panes = Rml::CreateString( "%d %d %d", s.layout, (int)s.solo, (int)s.active_pane() );
	if ( panes != lastPanes ) {
		lastPanes = panes;
		for ( int i = 0; i < 4; i++ ) {
			if ( Rml::Element *e = doc->GetElementById( Rml::CreateString( "pl-%d", i ) ) ) {
				e->SetClass( "hidden", !( s.layout == 4 || ( (int)s.solo == i && i != 0 ) ) );
				e->SetClass( "solo", s.layout != 4 );
				for ( int q = 0; q < 4; q++ ) {
					e->SetClass( Rml::CreateString( "q%d", q ), s.layout == 4 && q == i );
				}
				e->SetClass( "on", s.layout == 4 && (int)s.active_pane() == i );
			}
		}
		if ( Rml::Element *e = doc->GetElementById( "ed-views" ) ) {
			e->SetClass( "on", s.layout == 4 );
		}
	}
	// the Objects panel's filter chips: on while that kind shows
	if ( s.filters != lastFilters ) {
		lastFilters = s.filters;
		static const char *chips[5] = { "ed-f-tools", "ed-f-triggers", "ed-f-sky", "ed-f-entities", "ed-f-curves" };
		for ( int i = 0; i < 5; i++ ) {
			if ( Rml::Element *e = doc->GetElementById( chips[i] ) ) {
				e->SetClass( "on", !( s.filters & ( 1u << i ) ) );
			}
		}
	}
	// the question before unsaved changes go, its Save focused as it shows
	const bool ask = !s.asking.empty();
	if ( Rml::Element *e = doc->GetElementById( "ed-ask" ) ) {
		if ( e->IsClassSet( "hidden" ) == ask ) {
			e->SetClass( "hidden", !ask );
			if ( ask ) {
				if ( Rml::Element *b = doc->GetElementById( "ed-ask-save" ) ) {
					b->Focus();
				}
			}
		}
	}
	// the autosave's offer, while it stands
	std::string kept;
	const bool offer = s.recovery_offer( kept );
	Show( doc, "ed-recover", offer );
	if ( offer ) {
		// set when it changes: set each frame, after the frame's layout, it would never be laid out
		static Rml::String lastKept;
		SetText( doc, "ed-recover-text", "A map you hadn't saved was kept: " + kept + ".", lastKept );
	}
	// a compile under way: Stop in Compile's place; a leak: Show the leak
	Show( doc, "ed-compile", !s.compiling() );
	Show( doc, "ed-stop", s.compiling() );
	Show( doc, "ed-leak", s.leaking() );
	Show( doc, "ed-warnings", !s.compile_warnings().empty() && !s.compiling() );
	static Rml::String lastWarnings;
	SetText( doc, "ed-warnings", s.compile_warnings().size() == 1 ? "1 WARNING" : std::to_string( s.compile_warnings().size() ) + " WARNINGS", lastWarnings );
	if ( Rml::Element *e = doc->GetElementById( "ed-preview" ) ) {
		e->SetClass( "on", s.previewing() );
	}
	// the Size fields show the selection's size as it changes
	const uint64_t selKey = s.sel_rev * 1000003ull + s.doc.revision;
	if ( selKey != lastSel && !TypingIn() ) {
		lastSel = selKey;
		const oz::ed::Bounds b = s.selection_bounds();
		const oz::ed::Vec3 sz = b.empty() ? oz::ed::Vec3() : b.size();
		SetInput( doc, "ed-sw", Num( sz.x ) );
		SetInput( doc, "ed-sd", Num( sz.y ) );
		SetInput( doc, "ed-sh", Num( sz.z ) );
	}

	// the material in use, the recent ones first
	if ( Lower( s.shader ) != lastShader ) {
		lastShader = Lower( s.shader );
		recent.erase( std::remove( recent.begin(), recent.end(), lastShader ), recent.end() );
		recent.insert( recent.begin(), lastShader );
		if ( recent.size() > 24 ) {
			recent.resize( 24 );
		}
		SetRml( doc, "ed-cur-name", Enc( s.shader ) );
		SetSwatch( doc, "ed-cur-sw", s.shader, 3.8 );
	}

	// the search (F3) and the Keys dialog as the editor asks for them; the search goes when the focus
	// leaves it (a click in the view, a panel's field)
	if ( s.want_search ) {
		s.want_search = false;
		if ( !keymapOpen ) {
			OpenSearch( doc );
		}
	}
	if ( s.want_keymap ) {
		s.want_keymap = false;
		OpenKeymap( doc );
	}
	if ( s.want_sky ) {
		s.want_sky = false;
		OpenSky( doc );
	}
	if ( s.want_uv ) {
		s.want_uv = false;
		OpenUv( doc );
	}
	// the page's own actions: the main menu, the guide, Save as, the lists to open from, a menu, a tab
	if ( !s.want_page.empty() ) {
		const std::string w = s.want_page;
		s.want_page.clear();
		if ( w == "leave" ) {
			leaving = true;
		} else if ( w == "guide" ) {
			OpenGuide( doc );
		} else if ( w == "save_as" ) {
			OpenSaveAs( doc );
		} else if ( w == "open" || w == "import" ) {
			if ( searchOpen ) {
				CloseSearch( doc );
			}
			OpenPick( doc, "", w );
		} else if ( w.compare( 0, 5, "menu " ) == 0 ) {
			OpenMenu( doc, w.substr( 5 ), w == "menu file" ? "leave" : "" );	// Escape's: Enter leaves, Escape again closes it
		} else if ( w.compare( 0, 6, "panel " ) == 0 ) {
			panel = w.substr( 6 );
		}
	}
	if ( !guideChecked ) {
		guideChecked = true;
		if ( !OzEd_FileExists( GuideSeenFile().c_str() ) ) {
			OpenGuide( doc );	// the first time the editor opens
		}
	}
	if ( !menuOpen.empty() ) {
		TrackMenu( doc );
	}
	if ( saveAsOpen ) {
		SaveAsNote( doc );
	}
	// the map's file, opened or saved: the first of the recent maps
	static std::string notedPath;
	if ( s.path != notedPath ) {
		notedPath = s.path;
		NoteRecent( s.path );
	}
	UpdateTip( doc, width, height );
	if ( uvOpen ) {
		BuildUv( doc );
	}
	if ( s.want_warnings ) {
		s.want_warnings = false;
		if ( searchOpen ) {
			CloseSearch( doc );
		}
		OpenWarnings( doc );
	}
	if ( warnsOpen ) {
		bool inside = false;
		for ( Rml::Element *f = GetContext()->GetFocusElement(); f; f = f->GetParentNode() ) {
			inside = inside || f->GetId() == "ed-warns";
		}
		if ( !inside ) {
			CloseWarnings( doc );
		}
	}
	if ( searchOpen ) {
		bool inside = false;
		for ( Rml::Element *f = GetContext()->GetFocusElement(); f; f = f->GetParentNode() ) {
			inside = inside || f->GetId() == "ed-search";
		}
		if ( inside ) {
			BuildSearch( doc );
		} else {
			CloseSearch( doc );
		}
	}
	if ( keymapOpen ) {
		BuildKeymap( doc );
	}
	if ( skyOpen && !TypingInside( doc->GetElementById( "ed-sky" ) ) ) {
		BuildSky( doc );	// as undo and redo change it too; not under a field being typed in
		LoadThumbs( doc, "ed-sky-tiles" );
	}
	if ( pickOpen ) {
		bool inside = false;
		for ( Rml::Element *f = GetContext()->GetFocusElement(); f; f = f->GetParentNode() ) {
			inside = inside || f->GetId() == "ed-pick";
		}
		if ( inside ) {
			BuildPick( doc );
		} else {
			ClosePick( doc );
		}
	}
	ShowKeyHelp( doc );

	LightSlideTick();

	// the panel
	ShowPanel( doc );
	if ( panel == "inspect" ) {
		BuildInspector( doc );
	} else if ( panel == "materials" ) {
		BuildMaterials( doc );
		LoadThumbs( doc );
	} else if ( panel == "entities" ) {
		BuildEntities( doc );
	} else if ( panel == "objects" ) {
		BuildObjects( doc );
	} else {
		BuildHistory( doc );
	}
	const Rml::String chain = panel + "|" + lastInspect + "|" + lastMats + "|" + lastEntities + "|" + lastObjects + "|" + lastHistory;
	if ( chain != lastChain ) {
		lastChain = chain;
		ChainPanel( doc );
	}
	return true;
}

void Editor_Opened( Rml::ElementDocument *doc ) {
	// made anew: nothing of the last time it was open is shown
	shownPanel.clear();
	lastInspect.clear();
	lastKeysRml.clear();
	lastMats.clear();
	lastObjects.clear();
	lastEntities.clear();
	lastHistory.clear();
	lastKeyHelp.clear();
	lastPanes.clear();
	lastFilters = ~0u;
	searchOpen = keymapOpen = warnsOpen = pickOpen = uvOpen = skyOpen = false;
	session().sky_open = false;
	saveAsOpen = guideOpen = tipShown = false;
	menuOpen.clear();
	lastMenu.clear();
	tipFor = nullptr;
	session().binding.clear();
	doc->AddEventListener( Rml::EventId::Change, &slideListener );
	if ( Rml::Element *v = doc->GetElementById( "ed-uv-view" ) ) {
		for ( Rml::EventId id : { Rml::EventId::Dragstart, Rml::EventId::Drag, Rml::EventId::Dragend, Rml::EventId::Mousescroll } ) {
			v->AddEventListener( id, &uvListener );
		}
	}
	ShowPanel( doc );
	if ( panel == "inspect" ) {
		BuildInspector( doc );
	} else if ( panel == "materials" ) {
		BuildMaterials( doc );
	} else if ( panel == "entities" ) {
		BuildEntities( doc );
	} else if ( panel == "objects" ) {
		BuildObjects( doc );
	} else {
		BuildHistory( doc );
	}
	lastChain = panel + "|" + lastInspect + "|" + lastMats + "|" + lastEntities + "|" + lastObjects + "|" + lastHistory;
	ChainPanel( doc );
}

}  // namespace OzUI

extern "C" void OzUI_EditorLook( int dx, int dy ) {
	if ( OzEd_Looking() ) {	// any page sees mouse motion as it opens over a match: only the editor's look takes it
		session().look( dx, dy );
	}
}
