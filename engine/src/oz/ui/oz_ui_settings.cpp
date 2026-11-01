/*
Oz: the settings schema and the generated Settings page (ENGINE_PLAN.md P4 step 4).

ui/settings.json is the Godot build's schema.gd, exported once by
engine/tools/export_settings_schema.py: 178 entries (130 settings and 48 key binds) in ten tabs
plus the profile. At startup the engine creates the entries' cvars (archived) and generates the
Settings page from them: tabs, groups, a control per entry, search, the description panel, the
crosshair and cm/360 previews, reset (R) and share codes in the Godot build's format.

The page's data model ("settings") is bound to the rows below; the cvars stay the truth, read
back every frame while the page shows, so a change from the console shows at once. The Video tab's
changes wait for Apply (Ozmium, 2026-11-01: stepping through window sizes restarted the window at
each one): the row shows the value waiting, the cvar keeps the old one until then.
*/
#include "oz_ui_internal.h"

#include "cJSON.h"

#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

namespace OzUI {

bool (*keyHook)( int key ) = nullptr;	// set while a bind slot waits for a key (oz_ui.cpp asks it first)

struct Option {
	Rml::String value, label, cvarValue;
};

struct Entry {
	Rml::String key, tab, label, type, tip, group, cvar, unit, preview, command, def;	// def: the cvar's default
	float min = 0, max = 0, step = 0, scale = 1;
	bool hasRange = false, create = false, restart = false, wired = true;
	int maxLen = 0;
	std::vector<Option> options;
	Rml::String boolValues[2];		// cvar values for false and true
	Rml::String binds[3];			// bind: the default keys
	int groupIndex = -1;
	Rml::String id;					// the control's element id
	Rml::String dynamic;			// options filled from the engine when the page opens: monitors, output_devices, characters
};

// the data model's view of an entry
struct Row {
	Rml::String text, str, b0, b1, b2;
	float num = 0;
	bool on = false, shown = false, modified = false, pending = false;	// pending: waiting for Apply
};

struct Group {
	Rml::String tab, name, title;
	bool shown = false;
};

static std::vector<Entry> entries;
static std::vector<Rml::String> tabIds, tabLabels;
static std::vector<Row> rows;
static std::vector<Group> groups;
static std::vector<Rml::String> lastCvar;		// what each row was made from
static Rml::String tab, search, tipLabel, tipText, tipNote, cm360;
static Rml::DataModelHandle model;
static Rml::ElementDocument *page;
static int captureRow = -1, captureSlot, bindFrames, tipRow = -1;

// Graphics > Preset: each setting's cvar value for low, medium, high, ultra and classic (settings.json "presets")
static const char *const presetNames[] = { "low", "medium", "high", "ultra", "classic" };
static std::vector<std::pair<int, std::vector<Rml::String>>> presets;	// (entry, values)
// Video's window settings, kept 15 s only if the player says so (display_confirm.rml)
static int displayDeadline;					// OzE_Milliseconds; 0 with nothing to confirm
static Rml::String displayOld[3];
static const char *const displayKeys[3] = { "video/display_mode", "video/resolution", "video/monitor" };
// the Video tab's changes, waiting for Apply: entry -> its cvar's new value
static std::map<int, Rml::String> staged;
static bool applyShown;				// the footer's Apply and Discard: on the Video tab, or with changes waiting
static int pendingCount;
static Rml::String applyNote;

static void ShowTip( int i );
static void Refresh( int i );
static void Apply( int i, const Rml::String &value );
static void UpdateApply();
static void PickerFrame();

static int Find( const char *key ) {
	for ( size_t i = 0; i < entries.size(); i++ ) {
		if ( entries[i].key == key ) {
			return (int)i;
		}
	}
	return -1;
}

static int FindById( const Rml::String &id, int *slot ) {
	*slot = -1;
	for ( size_t i = 0; i < entries.size(); i++ ) {
		const Rml::String &e = entries[i].id;
		if ( id == e ) {
			return (int)i;
		}
		if ( entries[i].type == "bind" && id.size() == e.size() + 2 && !id.compare( 0, e.size(), e ) && id[e.size()] == '_' ) {
			*slot = id.back() - '0';
			return (int)i;
		}
		if ( entries[i].type == "color" && id.size() == e.size() + 5 && !id.compare( 0, e.size(), e ) && !id.compare( e.size(), 5, "_pick" ) ) {
			return (int)i;	// its swatch
		}
	}
	return -1;
}

static Rml::String Fmt( const char *fmt, ... ) {
	char text[512];
	va_list ap;

	va_start( ap, fmt );
	vsnprintf( text, sizeof( text ), fmt, ap );
	va_end( ap );
	return text;
}

static bool SameNumber( const char *a, const char *b ) {
	char *ea, *eb;
	const double x = strtod( a, &ea ), y = strtod( b, &eb );
	return *a && *b && !*ea && !*eb && fabs( x - y ) < 1e-6;
}

static bool SameValue( const Rml::String &a, const char *b ) {
	return a == b || SameNumber( a.c_str(), b );
}

/*
================
Settings_Init: ui/settings.json -> entries, and their cvars
================
*/
static Rml::String JsonString( cJSON *o, const char *name ) {
	cJSON *v = cJSON_GetObjectItemCaseSensitive( o, name );
	if ( cJSON_IsString( v ) ) {
		return v->valuestring;
	}
	if ( cJSON_IsNumber( v ) ) {
		return Fmt( "%g", v->valuedouble );
	}
	if ( cJSON_IsBool( v ) ) {
		return cJSON_IsTrue( v ) ? "true" : "false";
	}
	return "";
}

static float JsonNumber( cJSON *o, const char *name, float def ) {
	cJSON *v = cJSON_GetObjectItemCaseSensitive( o, name );
	return cJSON_IsNumber( v ) ? (float)v->valuedouble : def;
}

static bool JsonBool( cJSON *o, const char *name, bool def ) {
	cJSON *v = cJSON_GetObjectItemCaseSensitive( o, name );
	return cJSON_IsBool( v ) ? cJSON_IsTrue( v ) != 0 : def;
}

void Settings_Init() {
	void *buffer = nullptr;
	const int len = OzE_ReadFile( "ui/settings.json", &buffer );

	entries.clear();
	tabIds.clear();
	tabLabels.clear();
	groups.clear();
	if ( len < 0 ) {
		Printf( "^3UI: no ui/settings.json (oz/zz-oz-ui.pk3)\n" );
		return;
	}
	cJSON *root = cJSON_ParseWithLength( (const char *)buffer, len );
	OzE_FreeFile( buffer );
	if ( !root ) {
		Printf( "^1UI: ui/settings.json doesn't parse\n" );
		return;
	}

	cJSON *t;
	cJSON_ArrayForEach( t, cJSON_GetObjectItemCaseSensitive( root, "tabs" ) ) {
		tabIds.push_back( cJSON_GetArrayItem( t, 0 )->valuestring );
		tabLabels.push_back( cJSON_GetArrayItem( t, 1 )->valuestring );
	}

	cJSON *s;
	cJSON_ArrayForEach( s, cJSON_GetObjectItemCaseSensitive( root, "settings" ) ) {
		Entry e;
		e.key = JsonString( s, "key" );
		e.tab = JsonString( s, "tab" );
		e.label = JsonString( s, "label" );
		e.type = JsonString( s, "type" );
		e.tip = JsonString( s, "tip" );
		e.group = JsonString( s, "group" );
		e.cvar = JsonString( s, "cvar" );
		e.def = JsonString( s, "cvar_default" );
		e.unit = JsonString( s, "unit" );
		e.preview = JsonString( s, "preview" );
		e.command = JsonString( s, "command" );
		e.dynamic = JsonString( s, "dynamic" );
		e.create = JsonBool( s, "create", false );
		e.restart = JsonBool( s, "restart", false );
		e.wired = JsonBool( s, "wired", true );
		e.scale = JsonNumber( s, "scale", 1.0f );
		e.hasRange = cJSON_GetObjectItemCaseSensitive( s, "min" ) && cJSON_GetObjectItemCaseSensitive( s, "step" );
		e.min = JsonNumber( s, "min", 0 );
		e.max = JsonNumber( s, "max", 0 );
		e.step = JsonNumber( s, "step", 1 );
		e.maxLen = (int)JsonNumber( s, "max", 0 );
		e.id = e.key;
		for ( char &c : e.id ) {
			if ( c == '/' ) {
				c = '_';
			}
		}
		cJSON *values = cJSON_GetObjectItemCaseSensitive( s, "values" );
		cJSON *o;
		cJSON_ArrayForEach( o, cJSON_GetObjectItemCaseSensitive( s, "options" ) ) {
			Option opt;
			cJSON *v = cJSON_GetArrayItem( o, 0 );
			opt.value = cJSON_IsString( v ) ? Rml::String( v->valuestring ) : Fmt( "%g", v->valuedouble );
			opt.label = cJSON_GetArrayItem( o, 1 )->valuestring;
			cJSON *cv = cJSON_GetObjectItemCaseSensitive( values, opt.value.c_str() );
			opt.cvarValue = cJSON_IsString( cv ) ? Rml::String( cv->valuestring ) : opt.value;
			e.options.push_back( opt );
		}
		e.boolValues[0] = "0";
		e.boolValues[1] = "1";
		if ( e.type == "bool" && values ) {
			e.boolValues[0] = JsonString( values, "false" );
			e.boolValues[1] = JsonString( values, "true" );
		}
		int b = 0;
		cJSON_ArrayForEach( o, cJSON_GetObjectItemCaseSensitive( s, "binds" ) ) {
			if ( b < 3 ) {
				e.binds[b++] = o->valuestring;
			}
		}

		// groups: a heading wherever the group changes within a tab
		if ( groups.empty() || groups.back().tab != e.tab || groups.back().name != e.group ) {
			Group g;
			g.tab = e.tab;
			g.name = e.group;
			groups.push_back( g );
		}
		e.groupIndex = (int)groups.size() - 1;

		// the cvar: created archived with its range and description, or only bound when the engine owns it
		if ( !e.cvar.empty() && e.create ) {
			const bool numeric = ( e.type == "int" || e.type == "float" ) && e.hasRange;
			const Rml::String lo = Fmt( "%g", e.min * e.scale ), hi = Fmt( "%g", e.max * e.scale );
			OzE_CvarCreate( e.cvar.c_str(), e.def.c_str(), e.restart, e.tip.c_str(), numeric ? lo.c_str() : nullptr,
				numeric ? hi.c_str() : nullptr, e.type == "int" && e.scale == 1.0f );
		}
		entries.push_back( e );
	}

	// the presets' values, as the cvars hold them
	presets.clear();
	cJSON *p;
	cJSON_ArrayForEach( p, cJSON_GetObjectItemCaseSensitive( root, "presets" ) ) {
		const int i = Find( p->string );
		if ( i < 0 ) {
			continue;
		}
		const Entry &e = entries[i];
		std::vector<Rml::String> values;
		cJSON *v;
		cJSON_ArrayForEach( v, p ) {
			Rml::String value = cJSON_IsString( v ) ? Rml::String( v->valuestring ) :
				cJSON_IsBool( v ) ? Rml::String( cJSON_IsTrue( v ) ? "true" : "false" ) : Fmt( "%g", v->valuedouble );
			if ( e.type == "bool" ) {
				value = e.boolValues[value == "true" ? 1 : 0];
			} else {
				for ( const Option &o : e.options ) {
					if ( o.value == value ) {
						value = o.cvarValue;
						break;
					}
				}
			}
			values.push_back( value );
		}
		if ( values.size() == sizeof( presetNames ) / sizeof( presetNames[0] ) ) {
			presets.push_back( { i, values } );
		}
	}
	cJSON_Delete( root );

	rows.assign( entries.size(), Row() );
	lastCvar.assign( entries.size(), "\x01" );
	if ( !tabIds.empty() ) {
		tab = tabIds[0];
	}
}

int Settings_Loaded() {
	return (int)entries.size();
}

/*
================
Values: an entry's cvar <-> what its control shows
================
*/
static int OptionIndex( const Entry &e, const char *cvarValue ) {
	for ( size_t i = 0; i < e.options.size(); i++ ) {
		if ( SameValue( e.options[i].cvarValue, cvarValue ) ) {
			return (int)i;
		}
	}
	return -1;
}

static int Decimals( float step ) {
	int d = 0;
	while ( d < 4 && fabs( step * powf( 10.0f, (float)d ) - roundf( step * powf( 10.0f, (float)d ) ) ) > 1e-4f ) {
		d++;
	}
	return d;
}

static Rml::String KeyLabel( int key ) {
	if ( key < 0 ) {
		return "-";
	}
	Rml::String name = OzE_KeyName( key );
	if ( name.size() == 1 ) {
		for ( char &c : name ) {
			c = (char)toupper( (unsigned char)c );
		}
	}
	return name;
}

static bool IsPadKey( int key ) {
	return ( key >= K_PAD0_A && key < MAX_KEYS ) || ( key >= K_JOY1 && key <= K_JOY32 ) || ( key >= K_AUX1 && key <= K_AUX16 );
}

// the keys bound to a command: up to two keyboard/mouse ones and one pad one
static void BoundKeys( const Rml::String &command, int keys[3] ) {
	keys[0] = keys[1] = keys[2] = -1;
	for ( int k = 0; k < MAX_KEYS; k++ ) {
		const char *b = OzE_Binding( k );
		if ( !b || !b[0] || StrICmp( b, command.c_str() ) ) {
			continue;
		}
		if ( IsPadKey( k ) ) {
			if ( keys[2] < 0 ) {
				keys[2] = k;
			}
		} else if ( keys[0] < 0 ) {
			keys[0] = k;
		} else if ( keys[1] < 0 ) {
			keys[1] = k;
		}
	}
}

static Rml::String BindsSignature( const Entry &e ) {
	int keys[3];
	BoundKeys( e.command, keys );
	return Fmt( "%d %d %d", keys[0], keys[1], keys[2] );
}

static Rml::String CurrentCvar( const Entry &e ) {
	if ( e.type == "bind" ) {
		return BindsSignature( e );
	}
	return OzE_CvarExists( e.cvar.c_str() ) ? Rml::String( OzE_CvarString( e.cvar.c_str() ) ) : e.def;
}

// what the page shows: the value waiting for Apply, else the cvar's
static Rml::String ShownCvar( size_t i ) {
	const auto s = staged.find( (int)i );
	return s != staged.end() ? s->second : CurrentCvar( entries[i] );
}

// the row from the cvar's current value
static void MakeRow( size_t i, const Rml::String &cv ) {
	const Entry &e = entries[i];
	Row &r = rows[i];

	r.pending = staged.count( (int)i ) != 0;

	if ( e.type == "bind" ) {
		int keys[3], defaults = 0;
		BoundKeys( e.command, keys );
		r.b0 = KeyLabel( keys[0] );
		r.b1 = KeyLabel( keys[1] );
		r.b2 = KeyLabel( keys[2] );
		if ( captureRow == (int)i ) {
			( captureSlot == 0 ? r.b0 : captureSlot == 1 ? r.b1 : r.b2 ) = "Press a key";
		}
		for ( int s = 0; s < 3; s++ ) {
			defaults += ( keys[s] < 0 && e.binds[s].empty() ) || ( keys[s] >= 0 && !StrICmp( OzE_KeyName( keys[s] ), e.binds[s].c_str() ) );
		}
		r.modified = defaults != 3;
		return;
	}

	r.modified = !SameValue( e.def, cv.c_str() );
	if ( e.type == "bool" ) {
		r.on = SameValue( e.boolValues[1], cv.c_str() ) || ( e.boolValues[1] == "1" && atof( cv.c_str() ) != 0 );
		r.text = r.on ? "On" : "Off";
	} else if ( e.type == "int" || e.type == "float" ) {
		r.num = (float)atof( cv.c_str() ) / e.scale;
		r.text = Fmt( "%.*f%s%s", e.type == "int" ? 0 : Decimals( e.step ), r.num, e.unit.empty() ? "" : " ", e.unit.c_str() );
	} else if ( e.type == "enum" ) {
		const int o = OptionIndex( e, cv.c_str() );
		r.text = o >= 0 ? e.options[o].label : cv;
	} else {
		r.str = cv;
		r.text = cv;
	}
}

static void SetCvar( const Entry &e, const Rml::String &value ) {
	if ( !e.cvar.empty() && strcmp( OzE_CvarString( e.cvar.c_str() ), value.c_str() ) ) {
		OzE_CvarSet( e.cvar.c_str(), value.c_str() );
	}
}

/*
================
The view: tabs, search and the previews
================
*/
static void UpdateShown() {
	Rml::StringList words;
	Rml::String q;

	for ( char c : search ) {
		q += (char)tolower( (unsigned char)c );
	}
	Rml::StringUtilities::ExpandString( words, q, ' ' );
	for ( Group &g : groups ) {
		g.shown = false;
	}
	for ( size_t i = 0; i < entries.size(); i++ ) {
		const Entry &e = entries[i];
		bool shown;
		if ( e.tab == "profile" || !e.wired ) {
			shown = false;	// the Profile page's, or a later phase's (settings.json "wired": kept, shown once it's built)
		} else if ( words.empty() ) {
			shown = e.tab == tab;
		} else {
			Rml::String hay = e.label + " " + e.tip + " " + e.group + " " + e.key;
			for ( char &c : hay ) {
				c = (char)tolower( (unsigned char)c );
			}
			shown = true;
			for ( const Rml::String &w : words ) {
				shown = shown && ( w.empty() || hay.find( w ) != Rml::String::npos );
			}
		}
		rows[i].shown = shown;
		if ( shown ) {
			groups[e.groupIndex].shown = true;
		}
	}
	for ( Group &g : groups ) {
		size_t t = 0;
		while ( t < tabIds.size() && tabIds[t] != g.tab ) {
			t++;
		}
		g.title = words.empty() || t == tabIds.size() ? g.name : tabLabels[t] + " / " + g.name;
	}
	// set here, where the focus moves to a tab or the search: Apply never takes away the button it's on
	applyShown = !staged.empty() || ( words.empty() && tab == "video" );
	UpdateApply();
	if ( model ) {
		model.DirtyVariable( "rows" );
		model.DirtyVariable( "groups" );
		model.DirtyVariable( "tab" );
	}
	// the description panel shows the first setting until one is focused
	if ( tipRow < 0 || !rows[tipRow].shown ) {
		for ( size_t i = 0; i < entries.size(); i++ ) {
			if ( rows[i].shown ) {
				ShowTip( (int)i );
				break;
			}
		}
	}
}

static void UpdatePreviews() {
	// cm/360 with m_yaw 0.022, the classic degrees per count
	const float sens = (float)atof( OzE_CvarString( "sensitivity" ) ), dpi = (float)atof( OzE_CvarString( "oz_mouse_dpi" ) );
	cm360 = sens > 0 && dpi > 0 ? Fmt( "%.1f cm per full turn at %.0f DPI", 360.0f / ( sens * 0.022f ) / dpi * 2.54f, dpi ) : "";
	// (the crosshair's preview is a <crosshair> element: the HUD draws it from the cvars, oz_ui_crosshair.cpp)
	if ( model ) {
		model.DirtyVariable( "cm360" );
	}
}

static void ShowTip( int i ) {
	if ( i < 0 ) {
		return;
	}
	tipRow = i;
	const Entry &e = entries[i];
	tipLabel = e.label;
	tipText = e.tip;
	tipNote.clear();
	if ( e.restart ) {
		tipNote += "Takes effect after a restart. ";
	}
	if ( !e.wired ) {
		tipNote += "Not in the engine yet: kept, and used once its part is built. ";
	}
	if ( !e.cvar.empty() ) {
		tipNote += "Console: " + e.cvar;
	} else if ( !e.command.empty() ) {
		tipNote += "Console: bind <key> " + e.command;
	}
	if ( model ) {
		model.DirtyVariable( "tip_label" );
		model.DirtyVariable( "tip_text" );
		model.DirtyVariable( "tip_note" );
	}
}

/*
================
The options the engine knows only when the page opens (settings.json "dynamic"): the monitors, the
sound devices, the characters (the folders under models/players with an icon, as the menus list them)
================
*/
static void FillDynamic() {
	for ( size_t i = 0; i < entries.size(); i++ ) {
		Entry &e = entries[i];
		if ( e.dynamic.empty() ) {
			continue;
		}
		std::vector<Option> o;
		if ( e.dynamic == "monitors" ) {
			const int n = OzE_Monitors();
			for ( int m = 0; m < n; m++ ) {
				o.push_back( { Fmt( "%d", m ), Fmt( "Monitor %d", m + 1 ), Fmt( "%d", m ) } );
			}
		} else if ( e.dynamic == "output_devices" ) {
			o.push_back( { "Default", "Default", "Default" } );
			char list[4096];
			Rml::StringList names;
			OzE_AudioDevices( list, sizeof( list ) );
			Rml::StringUtilities::ExpandString( names, list, '\n' );
			for ( const Rml::String &n : names ) {
				if ( !n.empty() ) {
					o.push_back( { n, n, n } );
				}
			}
		} else if ( e.dynamic == "characters" || e.dynamic == "characters_only" ) {
			if ( e.dynamic == "characters" ) {
				o.push_back( { "off", "Off", e.options.empty() ? Rml::String( "off" ) : e.options[0].cvarValue } );
			}
			int n = 0;
			char **dirs = OzE_ListFiles( "models/players", "/", &n );
			for ( int d = 0; d < n; d++ ) {
				Rml::String dir = dirs[d];
				if ( !dir.empty() && dir.back() == '/' ) {
					dir.pop_back();		// a pak's directory entry, as q3_ui's model list strips it
				}
				if ( dir.empty() || dir[0] == '.' || !OzE_FileExists( ( "models/players/" + dir + "/icon_default.tga" ).c_str() ) ) {
					continue;
				}
				Rml::String label = dir;
				label[0] = (char)toupper( (unsigned char)label[0] );
				o.push_back( { dir, label, dir } );
			}
			OzE_FreeFileList( dirs );
		} else {
			continue;	// skins: the Profile page's Character tab (oz_ui_profile.cpp SkinsOf)
		}
		if ( !o.empty() ) {
			e.options = o;
			Refresh( (int)i );
		}
	}
}

void Settings_Frame() {
	// opening: the engine's lists; closing: a renderer setting that waits for a restart gets it
	static bool wasShown;
	const bool shown = page && page->IsVisible();
	if ( shown && !wasShown ) {
		FillDynamic();
	}
	if ( !shown && wasShown && OzE_SettingsRestart() ) {
		OzE_InsertText( "vid_restart\n" );
	}
	wasShown = shown;
	if ( !staged.empty() && !IsPageOpen( "settings" ) ) {
		Settings_Discard();		// closed some other way than Back (which asks): ui_close, a map load
	}
	PickerFrame();
	if ( displayDeadline ) {
		const int left = displayDeadline - OzE_Milliseconds();
		if ( left > 0 ) {
			Menu_Set( "display.seconds", Fmt( "%d", ( left + 999 ) / 1000 ).c_str() );
		} else {
			Settings_DisplayRevert();
			if ( IsPageOpen( "display_confirm" ) ) {
				Menu_Run( "back", nullptr );
			}
		}
	}
	if ( !page || !page->IsVisible() || entries.empty() ) {
		return;
	}
	bool dirty = false;
	const bool binds = ( ++bindFrames % 15 ) == 0 || captureRow >= 0;
	for ( size_t i = 0; i < entries.size(); i++ ) {
		if ( entries[i].type == "bind" && !binds && lastCvar[i] != "\x01" ) {
			continue;
		}
		const Rml::String cv = ShownCvar( i );
		if ( cv != lastCvar[i] ) {
			lastCvar[i] = cv;
			MakeRow( i, cv );
			dirty = true;
		}
	}
	if ( dirty && model ) {
		model.DirtyVariable( "rows" );
		UpdatePreviews();
	}
}

static void Refresh( int i ) {
	lastCvar[i] = "\x01";	// remade next frame
	MakeRow( i, ShownCvar( i ) );
	if ( model ) {
		model.DirtyVariable( "rows" );
	}
}

/*
================
Changes, as the page makes them
================
*/
/*
================
Graphics > Preset sets the settings in its column; changing one of them makes it Custom
================
*/
static void ApplyPreset( const Rml::String &name ) {
	for ( int c = 0; c < (int)( sizeof( presetNames ) / sizeof( presetNames[0] ) ); c++ ) {
		if ( name == presetNames[c] ) {
			for ( const auto &p : presets ) {
				SetCvar( entries[p.first], p.second[c] );
				Refresh( p.first );
			}
			return;
		}
	}
}

static void PresetToCustom( int i ) {
	const int preset = Find( "graphics/preset" );
	if ( preset < 0 ) {
		return;
	}
	for ( const auto &p : presets ) {
		if ( p.first == i ) {
			SetCvar( entries[preset], "custom" );
			Refresh( preset );
			return;
		}
	}
}

/*
================
Video's window settings: the window changes as they're applied (oz_settings_apply.c restarts the
renderer), and goes back unless the player keeps it within 15 s (display_confirm.rml)
================
*/
static bool IsDisplayKey( const Rml::String &key ) {
	for ( const char *k : displayKeys ) {
		if ( key == k ) {
			return true;
		}
	}
	return false;
}

void Settings_DisplayKeep() {
	displayDeadline = 0;
}

void Settings_DisplayRevert() {
	if ( !displayDeadline ) {
		return;
	}
	displayDeadline = 0;
	for ( int k = 0; k < 3; k++ ) {
		const int i = Find( displayKeys[k] );
		if ( i >= 0 ) {
			SetCvar( entries[i], displayOld[k] );
			Refresh( i );
		}
	}
}

int Settings_DisplayPending() {
	return displayDeadline != 0;
}

/*
================
A color setting's picker (color_picker.rml): its swatches and R, G, B sliders in the menu model's
"picker"; OK sets the setting
================
*/
static int pickerRow = -1;

static void PickerShow( unsigned r, unsigned g, unsigned b ) {
	Menu_Set( "picker.r", Fmt( "%u", r & 255 ).c_str() );
	Menu_Set( "picker.g", Fmt( "%u", g & 255 ).c_str() );
	Menu_Set( "picker.b", Fmt( "%u", b & 255 ).c_str() );
	Menu_Set( "picker.hex", Fmt( "#%02x%02x%02x", r & 255, g & 255, b & 255 ).c_str() );
}

void Settings_PickColor( const char *key ) {
	unsigned r = 255, g = 255, b = 255;
	pickerRow = Find( key );
	if ( pickerRow < 0 || entries[pickerRow].type != "color" ) {
		pickerRow = -1;
		return;
	}
	sscanf( OzE_CvarString( entries[pickerRow].cvar.c_str() ), "#%02x%02x%02x", &r, &g, &b );
	Menu_Set( "picker.label", entries[pickerRow].label.c_str() );
	PickerShow( r, g, b );
	OpenPageByName( "color_picker" );
}

void Settings_PickerSet( const char *hex ) {
	unsigned r, g, b;
	if ( hex && hex[0] ) {
		if ( sscanf( hex, "#%02x%02x%02x", &r, &g, &b ) == 3 ) {
			PickerShow( r, g, b );		// a swatch: the sliders follow
		}
		return;
	}
	if ( pickerRow >= 0 ) {				// OK
		Apply( pickerRow, Menu_Get( "picker.hex" ) );
	}
}

// the sliders move the color (the menu model holds them as numbers)
static void PickerFrame() {
	if ( pickerRow < 0 || !IsPageOpen( "color_picker" ) ) {
		return;
	}
	const Rml::String hex = Fmt( "#%02x%02x%02x", atoi( Menu_Get( "picker.r" ) ) & 255, atoi( Menu_Get( "picker.g" ) ) & 255,
		atoi( Menu_Get( "picker.b" ) ) & 255 );
	if ( hex != Menu_Get( "picker.hex" ) ) {
		Menu_Set( "picker.hex", hex.c_str() );
	}
}

// a Video setting changed on the Settings page waits for Apply; elsewhere (another page, the
// crosshair editor's paste, the console with the page closed) it applies at once
static bool Waits( const Entry &e ) {
	return e.tab == "video" && IsPageOpen( "settings" );
}

// the cvar's value for what the page set: a number clamped to its range; "" for a malformed color
static Rml::String CvarValue( const Entry &e, const Rml::String &value ) {
	if ( e.type == "int" || e.type == "float" ) {
		float v = (float)atof( value.c_str() );
		if ( e.hasRange ) {
			v = Rml::Math::Clamp( v, e.min, e.max );
		}
		return e.type == "int" && e.scale == 1.0f ? Fmt( "%d", (int)roundf( v ) ) : Fmt( "%g", v * e.scale );
	}
	if ( e.type == "color" && !( value.size() == 7 && value[0] == '#' && strspn( value.c_str() + 1, "0123456789abcdefABCDEF" ) == 6 ) ) {
		return "";
	}
	return value;
}

// the cvar set now, and what follows from it: the preset, the window's 15 s question
static void SetNow( int i, const Rml::String &value ) {
	const Entry &e = entries[i];
	const bool display = IsDisplayKey( e.key );

	if ( display && !displayDeadline ) {
		for ( int k = 0; k < 3; k++ ) {
			const int d = Find( displayKeys[k] );
			displayOld[k] = d >= 0 ? Rml::String( OzE_CvarString( entries[d].cvar.c_str() ) ) : Rml::String();
		}
	}
	Rml::String before = e.cvar.empty() ? Rml::String() : Rml::String( OzE_CvarString( e.cvar.c_str() ) );

	SetCvar( e, value );
	Refresh( i );

	if ( e.cvar.empty() || before == OzE_CvarString( e.cvar.c_str() ) ) {
		return;
	}
	if ( e.key == "graphics/preset" ) {
		ApplyPreset( Rml::String( OzE_CvarString( e.cvar.c_str() ) ) );
	} else {
		PresetToCustom( i );
	}
	if ( display ) {
		displayDeadline = OzE_Milliseconds() + 15000;
		Menu_Set( "display.seconds", "15" );
		if ( !IsPageOpen( "display_confirm" ) ) {
			OpenPageByName( "display_confirm" );
		}
	}
}

static void Apply( int i, const Rml::String &value ) {
	const Entry &e = entries[i];
	const Rml::String v = CvarValue( e, value );

	if ( e.type == "color" && v.empty() ) {
		Refresh( i );	// not a color: the field shows the setting again
		return;
	}
	if ( !Waits( e ) ) {
		SetNow( i, v );
		return;
	}
	if ( SameValue( v, CurrentCvar( e ).c_str() ) ) {
		staged.erase( i );	// back to what it is: nothing to apply
	} else {
		staged[i] = v;
	}
	applyShown = applyShown || !staged.empty();
	Refresh( i );
	UpdateApply();
}

/*
================
The Video tab's Apply: its changes wait until Apply sets them all at once (so the window restarts
once, and asks to be kept as before). Discard drops them, and so does leaving Settings any way but
Back, which asks first (apply_confirm.rml).
================
*/
static void UpdateApply() {
	pendingCount = (int)staged.size();
	applyNote = !applyShown ? "Changes apply and save as you make them." : pendingCount == 0 ? "Video changes wait for Apply." :
		pendingCount == 1 ? "1 change waits for Apply." : Fmt( "%d changes wait for Apply.", pendingCount );
	if ( model ) {
		model.DirtyVariable( "apply_shown" );
		model.DirtyVariable( "pending" );
		model.DirtyVariable( "apply_note" );
	}
}

void Settings_ApplyChanges() {
	std::map<int, Rml::String> now;
	now.swap( staged );		// each row shows its cvar again
	for ( const auto &s : now ) {
		SetNow( s.first, s.second );
	}
	UpdateApply();
}

void Settings_Discard() {
	std::map<int, Rml::String> was;
	was.swap( staged );
	for ( const auto &s : was ) {
		Refresh( s.first );
	}
	UpdateApply();
}

int Settings_Unapplied() {
	return (int)staged.size();
}

// apply_confirm.rml: the changes applied or not, and Settings closed under it
void Settings_Leave( bool apply ) {
	std::map<int, Rml::String> now;
	now.swap( staged );		// so Back closes Settings instead of asking again
	GoBack();				// the question
	GoBack();				// Settings
	staged.swap( now );
	if ( apply ) {
		Settings_ApplyChanges();	// the window's question opens over the page below
	} else {
		Settings_Discard();
	}
}

static void Toggle( int i ) {
	const Entry &e = entries[i];
	Apply( i, rows[i].on ? e.boolValues[0] : e.boolValues[1] );
}

static void Cycle( int i, int dir ) {
	const Entry &e = entries[i];
	if ( e.options.empty() ) {
		return;
	}
	const int n = (int)e.options.size();
	const int o = OptionIndex( e, ShownCvar( i ).c_str() );
	Apply( i, e.options[( ( o < 0 ? 0 : o + dir ) % n + n ) % n].cvarValue );	// SpinControl_Key wraps
}

static void ResetEntry( int i ) {
	const Entry &e = entries[i];
	if ( e.type == "bind" ) {
		int keys[3];
		BoundKeys( e.command, keys );
		for ( int k : keys ) {
			if ( k >= 0 ) {
				OzE_SetBinding( k, "" );
			}
		}
		for ( const Rml::String &b : e.binds ) {
			const int k = b.empty() ? -1 : OzE_KeyNum( b.c_str() );
			if ( k >= 0 ) {
				OzE_SetBinding( k, e.command.c_str() );
			}
		}
	} else if ( Waits( e ) ) {
		Apply( i, e.def );	// waits for Apply as any other change there
		return;
	} else {
		SetCvar( e, e.def );
	}
	Refresh( i );
}

// a bind slot waits for the next key; Escape cancels
static bool CaptureKey( int key ) {
	const int i = captureRow;
	keyHook = nullptr;
	captureRow = -1;
	if ( key != K_ESCAPE && key != K_PAD0_B && i >= 0 ) {
		const Entry &e = entries[i];
		int keys[3];
		BoundKeys( e.command, keys );
		if ( keys[captureSlot] >= 0 ) {
			OzE_SetBinding( keys[captureSlot], "" );
		}
		OzE_SetBinding( key, e.command.c_str() );
		PlaySound( Sound::Move );
	} else {
		PlaySound( Sound::Out );
	}
	if ( i >= 0 ) {
		Refresh( i );
	}
	return true;
}

static void StartCapture( int i, int slot ) {
	captureRow = i;
	captureSlot = slot;
	keyHook = CaptureKey;
	Refresh( i );
}

bool Settings_TabCount( int *count ) {
	*count = (int)tabIds.size();
	return !tabIds.empty();
}

void Settings_SetTab( int index ) {
	if ( index < 0 || index >= (int)tabIds.size() ) {
		return;
	}
	tab = tabIds[index];
	search.clear();
	if ( model ) {
		model.DirtyVariable( "search" );
	}
	UpdateShown();
	if ( page ) {
		if ( Rml::Element *list = page->GetElementById( "list" ) ) {
			list->SetScrollTop( 0 );
		}
	}
}

bool Settings_Tab( int direction ) {
	if ( tabIds.empty() ) {
		return false;
	}
	size_t t = 0;
	while ( t < tabIds.size() && tabIds[t] != tab ) {
		t++;
	}
	const int n = (int)tabIds.size();
	Settings_SetTab( ( ( (int)t + direction ) % n + n ) % n );
	if ( page ) {
		FocusQuietly( page->GetElementById( "tab_" + tab ) );
	}
	return true;
}

/*
================
Share codes: the Godot build's format, base64 of JSON {key: var_to_str(value)} for every
non-default setting (binds included, in the Godot build's key names), the profile left out
================
*/
static const char *const b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static Rml::String Base64( const Rml::String &in ) {
	Rml::String out;
	for ( size_t i = 0; i < in.size(); i += 3 ) {
		const unsigned v = ( (unsigned char)in[i] << 16 ) | ( i + 1 < in.size() ? (unsigned char)in[i + 1] << 8 : 0 ) | ( i + 2 < in.size() ? (unsigned char)in[i + 2] : 0 );
		out += b64[( v >> 18 ) & 63];
		out += b64[( v >> 12 ) & 63];
		out += i + 1 < in.size() ? b64[( v >> 6 ) & 63] : '=';
		out += i + 2 < in.size() ? b64[v & 63] : '=';
	}
	return out;
}

static Rml::String Unbase64( const Rml::String &in ) {
	Rml::String out;
	unsigned v = 0;
	int bits = 0;
	for ( char c : in ) {
		const char *p = strchr( b64, c );
		if ( !p || !c ) {
			continue;
		}
		v = ( v << 6 ) | (unsigned)( p - b64 );
		bits += 6;
		if ( bits >= 8 ) {
			bits -= 8;
			out += (char)( ( v >> bits ) & 255 );
		}
	}
	return out;
}

// the Godot build's names for keys (engine/tools/export_settings_schema.py's tables, reversed)
static const char *const godotKeys[][2] = {
	{ "UPARROW", "key:Up" }, { "DOWNARROW", "key:Down" }, { "LEFTARROW", "key:Left" }, { "RIGHTARROW", "key:Right" },
	{ "SPACE", "key:Space" }, { "CTRL", "key:Ctrl" }, { "SHIFT", "key:Shift" }, { "ENTER", "key:Enter" }, { "TAB", "key:Tab" },
	{ "PAUSE", "key:Pause" }, { "-", "key:Minus" }, { "=", "key:Equal" }, { ",", "key:Comma" }, { ".", "key:Period" },
	{ "[", "key:BracketLeft" }, { "]", "key:BracketRight" }, { "HOME", "key:Home" }, { "END", "key:End" },
	{ "MOUSE1", "mouse:1" }, { "MOUSE2", "mouse:2" }, { "MOUSE3", "mouse:3" }, { "MWHEELUP", "mouse:4" }, { "MWHEELDOWN", "mouse:5" },
	{ "PAD0_A", "joy:0" }, { "PAD0_B", "joy:1" }, { "PAD0_X", "joy:2" }, { "PAD0_Y", "joy:3" }, { "PAD0_BACK", "joy:4" },
	{ "PAD0_GUIDE", "joy:5" }, { "PAD0_START", "joy:6" }, { "PAD0_LEFTSTICK_CLICK", "joy:7" }, { "PAD0_RIGHTSTICK_CLICK", "joy:8" },
	{ "PAD0_LEFTSHOULDER", "joy:9" }, { "PAD0_RIGHTSHOULDER", "joy:10" }, { "PAD0_DPAD_UP", "joy:11" }, { "PAD0_DPAD_DOWN", "joy:12" },
	{ "PAD0_DPAD_LEFT", "joy:13" }, { "PAD0_DPAD_RIGHT", "joy:14" }, { "PAD0_LEFTTRIGGER", "joyaxis:4:1" }, { "PAD0_RIGHTTRIGGER", "joyaxis:5:1" },
};

static Rml::String ToGodotKey( int key ) {
	if ( key < 0 ) {
		return "";
	}
	const Rml::String name = OzE_KeyName( key );
	for ( const auto &k : godotKeys ) {
		if ( !StrICmp( k[0], name.c_str() ) ) {
			return k[1];
		}
	}
	Rml::String up = name;
	for ( char &c : up ) {
		c = (char)toupper( (unsigned char)c );
	}
	return "key:" + up;
}

static int FromGodotKey( const char *s ) {
	if ( !s[0] ) {
		return -1;
	}
	for ( const auto &k : godotKeys ) {
		if ( !strcmp( k[1], s ) ) {
			return OzE_KeyNum( k[0] );
		}
	}
	if ( !strncmp( s, "key:", 4 ) ) {
		Rml::String low = s + 4;
		if ( low.size() == 1 ) {
			low[0] = (char)tolower( (unsigned char)low[0] );
		}
		return OzE_KeyNum( low.c_str() );
	}
	return -1;
}

// Godot's var_to_str for floats: 1.0, 0.15, 90.0
static Rml::String GodotFloat( double v ) {
	Rml::String s = Fmt( "%.9g", v );
	if ( s.find_first_of( ".e" ) == Rml::String::npos ) {
		s += ".0";
	}
	return s;
}

static Rml::String GodotQuote( const Rml::String &s ) {
	Rml::String q = "\"";
	for ( char c : s ) {
		if ( c == '"' || c == '\\' ) {
			q += '\\';
		}
		q += c;
	}
	return q + "\"";
}

// an entry's current value as the Godot build's var_to_str wrote it
static Rml::String GodotValue( const Entry &e ) {
	const Rml::String cv = CurrentCvar( e );
	if ( e.type == "bind" ) {
		int keys[3];
		BoundKeys( e.command, keys );
		return "[" + GodotQuote( ToGodotKey( keys[0] ) ) + ", " + GodotQuote( ToGodotKey( keys[1] ) ) + ", " + GodotQuote( ToGodotKey( keys[2] ) ) + "]";
	}
	if ( e.type == "bool" ) {
		return SameValue( e.boolValues[1], cv.c_str() ) || ( e.boolValues[1] == "1" && atof( cv.c_str() ) != 0 ) ? "true" : "false";
	}
	if ( e.type == "int" ) {
		return Fmt( "%d", (int)roundf( (float)atof( cv.c_str() ) / e.scale ) );
	}
	if ( e.type == "float" ) {
		return GodotFloat( atof( cv.c_str() ) / e.scale );
	}
	if ( e.type == "enum" ) {
		const int o = OptionIndex( e, cv.c_str() );
		const Rml::String v = o >= 0 ? e.options[o].value : cv;
		char *end;
		strtod( v.c_str(), &end );
		return !v.empty() && !*end ? v : GodotQuote( v );	// anisotropy's options are numbers
	}
	if ( e.type == "color" && cv.size() == 7 ) {
		const unsigned rgb = (unsigned)strtoul( cv.c_str() + 1, nullptr, 16 );
		return Fmt( "Color(%g, %g, %g, 1)", ( ( rgb >> 16 ) & 255 ) / 255.0, ( ( rgb >> 8 ) & 255 ) / 255.0, ( rgb & 255 ) / 255.0 );
	}
	return GodotQuote( cv );
}

static Rml::String ExportCode( const char *prefix ) {
	cJSON *o = cJSON_CreateObject();
	for ( size_t i = 0; i < entries.size(); i++ ) {
		const Entry &e = entries[i];
		MakeRow( i, CurrentCvar( e ) );
		const bool modified = rows[i].modified;
		if ( staged.count( (int)i ) ) {
			MakeRow( i, ShownCvar( i ) );	// the code has the setting as it is; the row shows what waits for Apply
		}
		if ( e.key.compare( 0, strlen( prefix ), prefix ) || e.tab == "profile" || !modified ) {
			continue;
		}
		cJSON_AddStringToObject( o, e.key.c_str(), GodotValue( e ).c_str() );
	}
	char *json = cJSON_PrintUnformatted( o );
	const Rml::String code = Base64( json );
	cJSON_free( json );
	cJSON_Delete( o );
	return code;
}

// one value as var_to_str wrote it -> the cvar (or the binds)
static bool ImportValue( int i, const Rml::String &v ) {
	const Entry &e = entries[i];
	if ( e.type == "bind" ) {
		cJSON *a = cJSON_Parse( v.c_str() );
		if ( !cJSON_IsArray( a ) ) {
			cJSON_Delete( a );
			return false;
		}
		int keys[3];
		BoundKeys( e.command, keys );
		for ( int k : keys ) {
			if ( k >= 0 ) {
				OzE_SetBinding( k, "" );
			}
		}
		cJSON *k;
		cJSON_ArrayForEach( k, a ) {
			const int key = cJSON_IsString( k ) ? FromGodotKey( k->valuestring ) : -1;
			if ( key >= 0 ) {
				OzE_SetBinding( key, e.command.c_str() );
			}
		}
		cJSON_Delete( a );
		return true;
	}
	if ( e.type == "bool" ) {
		Apply( i, e.boolValues[v == "true" ? 1 : 0] );
	} else if ( e.type == "color" && !v.compare( 0, 6, "Color(" ) ) {
		float c[4] = { 1, 1, 1, 1 };
		sscanf( v.c_str() + 6, "%f , %f , %f , %f", &c[0], &c[1], &c[2], &c[3] );
		Apply( i, Fmt( "#%02x%02x%02x", (int)roundf( Rml::Math::Clamp( c[0], 0.f, 1.f ) * 255 ),
			(int)roundf( Rml::Math::Clamp( c[1], 0.f, 1.f ) * 255 ), (int)roundf( Rml::Math::Clamp( c[2], 0.f, 1.f ) * 255 ) ) );
	} else if ( e.type == "int" || e.type == "float" ) {
		Apply( i, v );
	} else {
		Rml::String s = v;
		if ( s.size() >= 2 && s.front() == '"' && s.back() == '"' ) {
			cJSON *j = cJSON_Parse( s.c_str() );
			s = cJSON_IsString( j ) ? j->valuestring : s.substr( 1, s.size() - 2 );
			cJSON_Delete( j );
		}
		if ( e.type == "enum" ) {
			for ( const Option &o : e.options ) {
				if ( SameValue( o.value, s.c_str() ) ) {
					s = o.cvarValue;
					break;
				}
			}
		}
		Apply( i, s );
	}
	return true;
}

static int ImportCode( const Rml::String &code ) {
	Rml::String trimmed;
	for ( char c : code ) {
		if ( !isspace( (unsigned char)c ) ) {
			trimmed += c;
		}
	}
	cJSON *o = cJSON_Parse( Unbase64( trimmed ).c_str() );
	if ( !cJSON_IsObject( o ) ) {
		cJSON_Delete( o );
		return -1;
	}
	int n = 0;
	cJSON *item;
	cJSON_ArrayForEach( item, o ) {
		const int i = Find( item->string );
		if ( i >= 0 && entries[i].tab != "profile" && cJSON_IsString( item ) && ImportValue( i, item->valuestring ) ) {
			n++;
		}
	}
	cJSON_Delete( o );
	return n;
}

/*
================
The page
================
*/
static Rml::String Escape( const Rml::String &s ) {
	Rml::String out;
	for ( char c : s ) {
		switch ( c ) {
		case '<': out += "&lt;"; break;
		case '>': out += "&gt;"; break;
		case '&': out += "&amp;"; break;
		case '"': out += "&quot;"; break;
		default: out += c;
		}
	}
	return out;
}

static Rml::String Control( size_t i ) {
	const Entry &e = entries[i];
	const Rml::String r = Fmt( "rows[%d]", (int)i ), id = e.id;

	if ( e.type == "bool" ) {
		return "<div class=\"toggle\" id=\"" + id + "\" data-class-on=\"" + r + ".on\"><span class=\"track\"><span class=\"knob\"/></span>"
			"<span class=\"state\">{{" + r + ".text}}</span></div>";
	}
	if ( e.type == "int" || e.type == "float" ) {
		return Fmt( "<input type=\"range\" class=\"slider\" id=\"%s\" min=\"%g\" max=\"%g\" step=\"%g\" data-value=\"%s.num\"/>", id.c_str(), e.min, e.max, e.step, r.c_str() ) +
			"<span class=\"value q3\">{{" + r + ".text}}</span>";
	}
	if ( e.type == "enum" ) {
		return "<div class=\"spinner\" id=\"" + id + "\"><span class=\"arrow prev\">&lt;</span><span class=\"v\">{{" + r + ".text}}</span>"
			"<span class=\"arrow next\">&gt;</span></div>";
	}
	if ( e.type == "bind" ) {
		return "<div class=\"binds\"><button class=\"bind\" id=\"" + id + "_0\">{{" + r + ".b0}}</button><button class=\"bind\" id=\"" + id +
			"_1\">{{" + r + ".b1}}</button><button class=\"bind pad\" id=\"" + id + "_2\">{{" + r + ".b2}}</button></div>";
	}
	if ( e.type == "color" ) {
		// the swatch opens the picker (color_picker.rml) and is what the keys and a pad reach; the field
		// takes a #rrggbb typed, by the mouse (in a text field the arrows move the caret)
		return "<button class=\"swatch\" id=\"" + id + "_pick\" data-style-background-color=\"" + r + ".str\" onclick=\"pickcolor " + e.key +
			"\"/><input type=\"text\" class=\"text color mouseonly\" id=\"" + id + "\" maxlength=\"7\" data-value=\"" + r + ".str\"/>";
	}
	return Fmt( "<input type=\"text\" class=\"text\" id=\"%s\" maxlength=\"%d\" data-value=\"%s.str\"/>", id.c_str(), e.maxLen > 0 ? e.maxLen : 64, r.c_str() );
}

// laid out as the web menu's SettingsPage in its PagePanel (P5): the panel right of the menu column,
// the tabs and the search over the list, the focused setting's description beside it
Rml::String Settings_PageRml() {
	Rml::String rml =
		"<rml>\n<head>\n<title>Settings</title>\n"
		"<link type=\"text/rcss\" href=\"../rcss/menu.rcss\"/>\n<link type=\"text/rcss\" href=\"../rcss/settings.rcss\"/>\n"
		"</head>\n<body class=\"settings\" data-model=\"settings\">\n"
		"<div class=\"panel\">\n<div class=\"corners\"><div class=\"tl\"/><div class=\"tr\"/><div class=\"bl\"/><div class=\"br\"/></div>"
		"<div class=\"topline\"/>\n"
		"<div class=\"phead\"><div><banner text=\"Settings\"/></div>"
		"<div class=\"backbtn\" id=\"back\"><kbd>Esc</kbd><span>Back</span></div></div>\n"
		"<div class=\"bar\"><div class=\"tabs\" id=\"tabs\">";
	for ( size_t t = 0; t < tabIds.size(); t++ ) {
		rml += "<button class=\"tab\" id=\"tab_" + tabIds[t] + "\" data-class-active=\"tab == '" + tabIds[t] + "' &amp;&amp; search == ''\"" +
			( t == 0 ? " autofocus" : "" ) + "><span>" + Escape( tabLabels[t] ) + "</span><span class=\"under\"/></button>";
	}
	rml += "</div>\n<input type=\"text\" class=\"text\" id=\"search\" maxlength=\"40\" data-value=\"search\" placeholder=\"Search settings\"/></div>\n"
		"<div class=\"body\">\n<div class=\"list\" id=\"list\">\n";
	int lastGroup = -1;
	for ( size_t i = 0; i < entries.size(); i++ ) {
		const Entry &e = entries[i];
		if ( e.tab == "profile" ) {
			continue;
		}
		if ( e.groupIndex != lastGroup ) {
			lastGroup = e.groupIndex;
			rml += Fmt( "<div class=\"group\" data-if=\"groups[%d].shown\"><span class=\"t\">{{groups[%d].title}}</span><span class=\"rule\"/></div>\n",
				e.groupIndex, e.groupIndex );
		}
		// a Video row's change waits for Apply (Waits)
		const bool waits = e.tab == "video";
		rml += Fmt( "<div class=\"row\" id=\"row_%s\" data-if=\"rows[%d].shown\" data-class-modified=\"rows[%d].modified\"", e.id.c_str(), (int)i, (int)i ) +
			( waits ? Fmt( " data-class-pending=\"rows[%d].pending\"", (int)i ) : Rml::String() ) + ">";
		rml += "<span class=\"label\"><span>" + Escape( e.label ) + "</span>" + ( e.restart ? "<span class=\"badge\">restart</span>" : "" ) +
			( e.wired ? "" : "<span class=\"badge dimb\">soon</span>" ) +
			( waits ? Fmt( "<span class=\"badge\" data-if=\"rows[%d].pending\">not applied</span>", (int)i ) : Rml::String() ) + "</span>";
		rml += "<div class=\"control " + e.type + "\">" + Control( i ) + "</div></div>\n";
	}
	rml += "</div>\n<div class=\"info\">\n<div class=\"info-label q3\">{{tip_label}}</div>\n<div class=\"info-text\">{{tip_text}}</div>\n"
		"<div class=\"info-note\">{{tip_note}}</div>\n"
		"<div class=\"preview crosshair\" data-if=\"tab == 'crosshair' &amp;&amp; search == ''\"><div class=\"bg\"/><div class=\"veil\"/><crosshair/></div>\n"
		"<div class=\"preview cm360\" data-if=\"tab == 'mouse' &amp;&amp; search == ''\">{{cm360}}</div>\n</div>\n</div>\n"
		"<div class=\"footer pfoot\" id=\"footer\"><button class=\"btn small\" id=\"reset_tab\">Reset tab</button>"
		"<button class=\"btn small\" id=\"copy_code\">Copy config code</button>"
		"<button class=\"btn small\" id=\"paste_code\">Paste code</button>"
		"<button class=\"btn small primary\" id=\"xh_editor\" data-if=\"tab == 'crosshair' &amp;&amp; search == ''\" onclick=\"page crosshair\">Crosshair editor</button>"
		"<span class=\"note\">{{apply_note}}</span>"
		"<button class=\"btn small\" id=\"settings_discard\" data-if=\"apply_shown\" data-class-refused=\"pending == 0\">Discard</button>"
		"<button class=\"btn small\" id=\"settings_apply\" data-if=\"apply_shown\" data-class-primary=\"pending &gt; 0\" data-class-refused=\"pending == 0\">Apply</button></div>\n"
		"</div>\n</body>\n</rml>\n";
	return rml;
}

void Settings_BindModel( Rml::Context *context ) {
	Rml::DataModelConstructor c = context->CreateDataModel( "settings" );
	if ( !c ) {
		return;
	}
	if ( auto s = c.RegisterStruct<Row>() ) {
		s.RegisterMember( "text", &Row::text );
		s.RegisterMember( "str", &Row::str );
		s.RegisterMember( "b0", &Row::b0 );
		s.RegisterMember( "b1", &Row::b1 );
		s.RegisterMember( "b2", &Row::b2 );
		s.RegisterMember( "num", &Row::num );
		s.RegisterMember( "on", &Row::on );
		s.RegisterMember( "shown", &Row::shown );
		s.RegisterMember( "modified", &Row::modified );
		s.RegisterMember( "pending", &Row::pending );
	}
	if ( auto g = c.RegisterStruct<Group>() ) {
		g.RegisterMember( "title", &Group::title );
		g.RegisterMember( "shown", &Group::shown );
	}
	c.RegisterArray<std::vector<Row>>();
	c.RegisterArray<std::vector<Group>>();
	c.Bind( "rows", &rows );
	c.Bind( "groups", &groups );
	c.Bind( "tab", &tab );
	c.Bind( "search", &search );
	c.Bind( "tip_label", &tipLabel );
	c.Bind( "tip_text", &tipText );
	c.Bind( "tip_note", &tipNote );
	c.Bind( "cm360", &cm360 );
	c.Bind( "apply_shown", &applyShown );
	c.Bind( "pending", &pendingCount );
	c.Bind( "apply_note", &applyNote );
	model = c.GetModelHandle();

	for ( size_t i = 0; i < entries.size(); i++ ) {
		MakeRow( i, ShownCvar( i ) );
	}
	UpdateShown();
	UpdatePreviews();
}

// the first and last control of the rows that show, for the list's edges
// the control of row i the keys reach: a bind's first slot, a color's swatch
static Rml::Element *RowControl( size_t i ) {
	return page->GetElementById( entries[i].type == "bind" ? entries[i].id + "_0" : entries[i].type == "color" ? entries[i].id + "_pick" : entries[i].id );
}

static Rml::Element *EdgeControl( bool last ) {
	for ( size_t n = 0; n < entries.size(); n++ ) {
		const size_t i = last ? entries.size() - 1 - n : n;
		if ( rows[i].shown ) {
			return RowControl( i );
		}
	}
	return nullptr;
}

// the next row that shows, up (-1) or down (1) from row i; -1 past the list's edge
static int NextShownRow( int i, int dir ) {
	for ( int j = i + dir; j >= 0 && j < (int)entries.size(); j += dir ) {
		if ( rows[j].shown ) {
			return j;
		}
	}
	return -1;
}

static bool InList( Rml::Element *e ) {
	for ( ; e; e = e->GetParentNode() ) {
		if ( e->GetId() == "list" ) {
			return true;
		}
	}
	return false;
}

static bool InFooter( Rml::Element *e ) {
	return e && e->GetParentNode() && e->GetParentNode()->GetId() == "footer";
}

static bool Takes( Rml::Element *e, const char *cls ) {
	return e && e->IsClassSet( cls );
}

/*
================
SettingsListener: the page's controls (capture phase, after the generic one in oz_ui.cpp)
================
*/
static Rml::ObserverPtr<Rml::Element> pendingFocus;	// where a key's focus goes instead (a color's swatch)

void Settings_AfterKey() {
	if ( pendingFocus ) {
		pendingFocus->Focus( true );
		pendingFocus.reset();
	}
}

class SettingsListener : public Rml::EventListener {
	bool keyNav = false;	// the last focus move came from the keys or a pad, not the mouse

public:
	void ProcessEvent( Rml::Event &event ) override {
		Rml::Element *target = event.GetTargetElement();
		if ( !target ) {
			return;
		}
		int slot;
		const int i = FindById( target->GetId(), &slot );

		switch ( event.GetId() ) {
		case Rml::EventId::Mousedown:
			keyNav = false;
			break;

		case Rml::EventId::Focus: {
			// the keys reach a color's swatch (it opens the picker): in its text field they'd move the
			// caret. Moved once the key is done (Settings_AfterKey): a focus set inside this event doesn't hold.
			if ( keyNav && i >= 0 && entries[i].type == "color" && target->GetTagName() == "input" ) {
				if ( Rml::Element *swatch = page->GetElementById( target->GetId() + "_pick" ) ) {
					pendingFocus = swatch->GetObserverPtr();
				}
			}
			if ( i >= 0 ) {
				ShowTip( i );
			}
			// the focused control's row lights up (the web's focus-within)
			static Rml::ObserverPtr<Rml::Element> lit;
			if ( lit ) {
				lit->SetClass( "current", false );
			}
			Rml::Element *row = target;
			while ( row && !row->IsClassSet( "row" ) ) {
				row = row->GetParentNode();
			}
			if ( row ) {
				row->SetClass( "current", true );
				lit = row->GetObserverPtr();
			}
			break;
		}

		case Rml::EventId::Change:
			if ( target->GetId() == "search" ) {
				search = event.GetParameter<Rml::String>( "value", "" );
				UpdateShown();
			} else if ( i >= 0 && ( entries[i].type == "int" || entries[i].type == "float" ) ) {
				Apply( i, Fmt( "%g", event.GetParameter<float>( "value", 0.0f ) ) );
			} else if ( i >= 0 && ( entries[i].type == "string" || entries[i].type == "color" ) ) {
				Apply( i, event.GetParameter<Rml::String>( "value", "" ) );
			}
			break;

		case Rml::EventId::Click: {
			int ci = i;
			if ( ci < 0 && target->GetParentNode() ) {	// a spinner's arrow or value, a toggle's knob
				for ( Rml::Element *p = target->GetParentNode(); p && ci < 0; p = p->GetParentNode() ) {
					ci = FindById( p->GetId(), &slot );
				}
			}
			// a click on a tab's label lands on the label: its button has the id
			Rml::Element *button = target;
			while ( button->GetId().empty() && button->GetParentNode() ) {
				button = button->GetParentNode();
			}
			const Rml::String &id = button->GetId();
			if ( !id.compare( 0, 4, "tab_" ) ) {
				for ( size_t t = 0; t < tabIds.size(); t++ ) {
					if ( id == "tab_" + tabIds[t] ) {
						Settings_SetTab( (int)t );
					}
				}
			} else if ( id == "reset_tab" ) {
				for ( size_t e = 0; e < entries.size(); e++ ) {
					if ( rows[e].shown ) {
						ResetEntry( (int)e );
					}
				}
			} else if ( id == "copy_code" ) {
				const Rml::String code = ExportCode( tab == "crosshair" ? "crosshair/" : "" );
				Rml::GetSystemInterface()->SetClipboardText( code );
				Printf( "Config code: %s\n", code.c_str() );
			} else if ( id == "paste_code" ) {
				Rml::String clip;
				Rml::GetSystemInterface()->GetClipboardText( clip );
				const int n = ImportCode( clip );
				if ( n < 0 ) {
					PlaySound( Sound::Buzz );
				}
				Printf( n < 0 ? "That isn't a config code\n" : "Applied %d settings from the code\n", n );
			} else if ( id == "settings_apply" || id == "settings_discard" ) {
				if ( staged.empty() ) {
					PlaySound( Sound::Buzz );
				} else if ( id == "settings_apply" ) {
					Settings_ApplyChanges();
				} else {
					Settings_Discard();
				}
			} else if ( id == "back" ) {
				OzE_ExecuteNow( "ui_back\n" );
			} else if ( ci >= 0 ) {
				const Entry &e = entries[ci];
				if ( e.type == "bool" ) {
					Toggle( ci );
				} else if ( e.type == "enum" ) {
					Cycle( ci, target->IsClassSet( "prev" ) ? -1 : 1 );
				} else if ( e.type == "bind" && slot >= 0 ) {
					StartCapture( ci, slot );
				}
			}
			break;
		}

		case Rml::EventId::Keydown: {
			const int key = event.GetParameter<int>( "key_identifier", 0 );
			Rml::Element *to = nullptr;

			keyNav = true;

			if ( ( key == Rml::Input::KI_LEFT || key == Rml::Input::KI_RIGHT ) && i >= 0 ) {
				if ( entries[i].type == "bool" ) {
					Toggle( i );
					event.StopPropagation();
				} else if ( entries[i].type == "enum" ) {
					Cycle( i, key == Rml::Input::KI_LEFT ? -1 : 1 );
					event.StopPropagation();
				}
				break;
			}
			if ( key == Rml::Input::KI_R && i >= 0 && !Takes( target, "color" ) && target->GetTagName() != "input" ) {
				if ( rows[i].modified ) {
					ResetEntry( i );
					PlaySound( Sound::Move );
				} else {
					PlaySound( Sound::Buzz );
				}
				event.StopPropagation();
				break;
			}

			// row by row, but between key binds (each of their three slots goes to the one below): the
			// controls don't line up (a color's swatch, a toggle right of the sliders), so RmlUi's
			// nearest-in-that-direction skips rows (a toggle's down went past two sliders to the next toggle)
			if ( ( key == Rml::Input::KI_UP || key == Rml::Input::KI_DOWN ) && i >= 0 && InList( target ) ) {
				const int j = NextShownRow( i, key == Rml::Input::KI_UP ? -1 : 1 );
				if ( j >= 0 && ( entries[i].type != "bind" || entries[j].type != "bind" ) ) {
					to = RowControl( j );
				}
			}

			// the regions' edges: tabs <-> the list <-> the footer (RmlUi's nav doesn't cross a scroll container)
			const bool tabButton = !target->GetId().compare( 0, 4, "tab_" );
			if ( to ) {
			} else if ( key == Rml::Input::KI_DOWN && ( tabButton || target->GetId() == "search" ) ) {
				to = tabButton ? EdgeControl( false ) : page->GetElementById( "tab_" + tab );
				if ( tabButton && !to ) {
					to = page->GetElementById( "reset_tab" );
				}
			} else if ( key == Rml::Input::KI_UP && tabButton ) {
				to = page->GetElementById( "search" );
			} else if ( key == Rml::Input::KI_UP && InList( target ) ) {
				Rml::Element *first = EdgeControl( false );
				if ( first && ( target == first || target->GetParentNode() == first->GetParentNode() ) ) {
					to = page->GetElementById( search.empty() ? "tab_" + tab : Rml::String( "search" ) );
				}
			} else if ( key == Rml::Input::KI_DOWN && InList( target ) ) {
				Rml::Element *last = EdgeControl( true );
				if ( last && ( target == last || target->GetParentNode() == last->GetParentNode() ) ) {
					to = page->GetElementById( staged.empty() ? "reset_tab" : "settings_apply" );
				}
			} else if ( key == Rml::Input::KI_UP && InFooter( target ) ) {
				to = EdgeControl( true );
				if ( !to ) {
					to = page->GetElementById( "tab_" + tab );
				}
			}
			if ( to ) {
				to->Focus();
				to->ScrollIntoView( Rml::ScrollIntoViewOptions( Rml::ScrollAlignment::Nearest ) );
				event.StopPropagation();
			}
			break;
		}
		default:
			break;
		}
	}
};

static SettingsListener settingsListener;

void Settings_Attach( Rml::ElementDocument *doc ) {
	page = doc;
	doc->AddEventListener( Rml::EventId::Focus, &settingsListener, true );
	doc->AddEventListener( Rml::EventId::Change, &settingsListener, true );
	doc->AddEventListener( Rml::EventId::Click, &settingsListener, true );
	doc->AddEventListener( Rml::EventId::Keydown, &settingsListener, true );
	doc->AddEventListener( Rml::EventId::Mousedown, &settingsListener, true );
}

/*
================
Commands: the test hooks and share codes
================
*/
// ui_settings_set <key> <value>: as the page sets it (the value as its control holds it: 110, true, high)
bool Settings_Set( const char *key, const char *value ) {
	const int i = Find( key );
	if ( i < 0 ) {
		return false;
	}
	const Entry &e = entries[i];
	Rml::String v = value;
	if ( e.type == "bool" ) {
		v = e.boolValues[( !strcmp( value, "true" ) || !strcmp( value, "1" ) ) ? 1 : 0];
	} else if ( e.type == "enum" ) {
		for ( const Option &o : e.options ) {
			if ( SameValue( o.value, value ) ) {
				v = o.cvarValue;
			}
		}
	}
	Apply( i, v );
	return true;
}

// the value as Settings_Set takes it (true, 24, d), not as the page shows it
const char *Settings_Value( const char *key ) {
	static Rml::String value;
	const int i = Find( key );
	if ( i < 0 ) {
		return "";
	}
	const Entry &e = entries[i];
	value = ShownCvar( i );
	if ( e.type == "bool" ) {
		value = SameValue( e.boolValues[1], value.c_str() ) ? "true" : "false";
	} else if ( ( e.type == "int" || e.type == "float" ) && e.scale != 1.0f ) {	// 60 (%) for 0.6
		value = e.type == "int" ? Fmt( "%d", (int)roundf( (float)atof( value.c_str() ) / e.scale ) ) : Fmt( "%g", atof( value.c_str() ) / e.scale );
	} else if ( e.type == "enum" ) {
		for ( const Option &o : e.options ) {
			if ( SameValue( o.cvarValue, value.c_str() ) ) {
				value = o.value;
				break;
			}
		}
	}
	return value.c_str();
}

const char *Settings_Get( const char *key ) {
	static Rml::String value;
	if ( !strcmp( key, "@tab" ) ) {
		return tab.c_str();
	}
	const int i = Find( key );
	if ( i < 0 ) {
		return "";
	}
	MakeRow( i, ShownCvar( i ) );
	value = entries[i].type == "bind" ? rows[i].b0 + " " + rows[i].b1 + " " + rows[i].b2 : rows[i].text;
	return value.c_str();
}

static void Set_f() {
	if ( OzE_Argc() < 3 ) {
		Printf( "ui_settings_set <key> <value>\n" );
		return;
	}
	if ( !Settings_Set( OzE_Argv( 1 ), OzE_ArgsFrom( 2 ) ) ) {
		Printf( "ui_settings_set: no setting %s\n", OzE_Argv( 1 ) );
		return;
	}
	Printf( "ui_settings_set: %s = %s\n", OzE_Argv( 1 ), Settings_Get( OzE_Argv( 1 ) ) );
}

static void Get_f() {
	if ( OzE_Argc() < 2 || Find( OzE_Argv( 1 ) ) < 0 ) {
		Printf( "ui_settings_get <key>\n" );
		return;
	}
	const Entry &e = entries[Find( OzE_Argv( 1 ) )];
	Printf( "ui_settings_get: %s = %s (%s \"%s\")\n", e.key.c_str(), Settings_Get( e.key.c_str() ), e.cvar.empty() ? e.command.c_str() : e.cvar.c_str(),
		e.cvar.empty() ? "" : OzE_CvarString( e.cvar.c_str() ) );
	if ( !e.options.empty() ) {
		Rml::String list;
		for ( const Option &o : e.options ) {
			list += ( list.empty() ? "" : " | " ) + o.label;
		}
		Printf( "  %d options: %s\n", (int)e.options.size(), list.c_str() );
	}
}

static void List_f() {
	for ( const Entry &e : entries ) {
		Printf( "%-32s %-6s %-28s %s\n", e.key.c_str(), e.type.c_str(), e.cvar.empty() ? e.command.c_str() : e.cvar.c_str(), e.wired ? "" : "(not wired yet)" );
	}
	Printf( "%d settings\n", (int)entries.size() );
}

// the share code of the settings under prefix ("crosshair/" or all), to the clipboard; and one from it
void Settings_CopyCode( const char *prefix ) {
	Rml::GetSystemInterface()->SetClipboardText( ExportCode( prefix ) );
}

int Settings_PasteCode() {
	Rml::String clip;
	Rml::GetSystemInterface()->GetClipboardText( clip );
	return ImportCode( clip );
}

// every setting under prefix back to its default (the crosshair editor's Reset)
void Settings_Reset( const char *prefix ) {
	for ( size_t i = 0; i < entries.size(); i++ ) {
		if ( !entries[i].key.compare( 0, strlen( prefix ), prefix ) ) {
			ResetEntry( (int)i );
		}
	}
}

static void Code_f() {
	Printf( "%s\n", ExportCode( OzE_Argc() > 1 ? OzE_Argv( 1 ) : "" ).c_str() );
}

static void Import_f() {
	if ( OzE_Argc() < 2 ) {
		Printf( "ui_settings_import <code>\n" );
		return;
	}
	const int n = ImportCode( OzE_ArgsFrom( 1 ) );
	Printf( n < 0 ? "ui_settings_import: not a config code\n" : "ui_settings_import: %d settings\n", n );
}

static void Tab_f() {
	if ( OzE_Argc() > 1 ) {
		for ( size_t t = 0; t < tabIds.size(); t++ ) {
			if ( tabIds[t] == OzE_Argv( 1 ) || Fmt( "%d", (int)t ) == OzE_Argv( 1 ) ) {
				Settings_SetTab( (int)t );
				if ( GetContext() ) {
					GetContext()->Update();
				}
				return;
			}
		}
	}
	Printf( OzE_Argc() > 1 ? "ui_tab <name or number>\n" : "ui_tab: %s\n", search.empty() ? tab.c_str() : "search" );
}

void Settings_Commands() {
	OzE_AddCommand( "ui_settings_set", Set_f );
	OzE_AddCommand( "ui_settings_get", Get_f );
	OzE_AddCommand( "ui_settings_list", List_f );
	OzE_AddCommand( "ui_settings_code", Code_f );
	OzE_AddCommand( "ui_settings_import", Import_f );
	OzE_AddCommand( "ui_tab", Tab_f );
}

} // namespace OzUI
