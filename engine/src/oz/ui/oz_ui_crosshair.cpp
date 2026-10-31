/*
Oz: the crosshair editor (ui/pages/crosshair.rml, the web menu's CrosshairEditor.tsx) and <crosshair>, the
crosshair as the HUD draws it (oz_hud.c OzHud_CrosshairPreview). The editor's shape tiles and preview and
Settings' Crosshair tab show what the match shows, the preview at its size in the match.

The editor is one page, from the pause menu and from Settings. Over a match (crosshair.live) its panel
sits left and the match's own crosshair, in the screen's middle, is the preview: a weapon tab shows that
weapon's there, Test hit flashes it. From the main menu it previews over a level shot. Its controls write
the crosshair settings (settings.json's crosshair/*) as they change, and its values follow the settings
every frame while it's open, so the color picker, a pasted code and the console show at once.
*/
#include "oz_ui_internal.h"
#include "oz_ui.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace OzUI {

namespace {

// the weapon tabs (crosshair/weapon_<key>), by weapon number as the game numbers them
const char *const weaponKeys[] = { "", "gauntlet", "machinegun", "shotgun", "grenadel", "rocketl", "lightning", "railgun",
	"plasma", "bfg" };
const int numWeapons = sizeof( weaponKeys ) / sizeof( weaponKeys[0] );

// the settings the page shows, as crosshair.<name>
const char *const shownValues[] = { "shape", "size", "thickness", "gap", "dot", "color", "opacity", "health_color", "outline",
	"outline_width", "outline_opacity", "hit_flash", "hit_color", "hit_time", "per_weapon" };

// what the HUD draws the crosshair from: a <crosshair> makes its geometry again when one changes
const char *const drawnCvars[] = { "oz_crosshair_shape", "cg_crosshairSize", "oz_crosshair_thickness", "oz_crosshair_gap",
	"oz_crosshair_dot", "oz_crosshair_color", "oz_crosshair_opacity", "oz_crosshair_health", "oz_crosshair_outline",
	"oz_crosshair_outline_width", "oz_crosshair_outline_opacity", "oz_crosshair_hit_flash", "oz_crosshair_hit_color",
	"oz_crosshair_hit_time", "oz_crosshair_per_weapon" };

Rml::String drawnKey;			// the cvars above and the weapons' own crosshairs, this frame
int testHit = -100000;			// Test hit's time (OzE_Milliseconds): the previews flash the hit color

int WeaponNumber( const Rml::String &key ) {
	for ( int i = 1; i < numWeapons; i++ ) {
		if ( key == weaponKeys[i] ) {
			return i;
		}
	}
	return 0;
}

Rml::String WeaponSetting( int weapon ) {
	return Rml::String( "crosshair/weapon_" ) + weaponKeys[weapon];
}

bool IsTrue( const char *value ) {
	return !strcmp( value, "true" );
}

void Put( const Rml::String &path, const char *value ) {
	if ( strcmp( Menu_Get( path.c_str() ), value ) ) {
		Menu_Set( path.c_str(), value );
	}
}

// the settings -> the page's values, and what follows from them
void Sync() {
	for ( const char *name : shownValues ) {
		const char *v = Settings_Value( ( Rml::String( "crosshair/" ) + name ).c_str() );
		if ( *v ) {
			Put( Rml::String( "crosshair." ) + name, v );
		}
	}
	const bool perWeapon = IsTrue( Settings_Value( "crosshair/per_weapon" ) );
	const Rml::String main = Settings_Value( "crosshair/shape" );
	// a tab's dot: that weapon has its own
	for ( int i = 0;; i++ ) {
		const int w = WeaponNumber( Menu_Get( Rml::CreateString( "crosshair.weapons.%d.id", i ).c_str() ) );
		if ( !w ) {
			break;
		}
		const bool own = perWeapon && strcmp( Settings_Value( WeaponSetting( w ).c_str() ), "default" );
		Put( Rml::CreateString( "crosshair.weapons.%d.own", i ), own ? "true" : "false" );
	}
	const int w = WeaponNumber( Menu_Get( "crosshair.weapon" ) );
	const Rml::String own = w ? Settings_Value( WeaponSetting( w ).c_str() ) : "default";
	// the tile picked: the main crosshair on All, the weapon's own (or "default") on its tab
	Put( "crosshair.pick", w ? own.c_str() : main.c_str() );
	// what the match shows for the tab, so the controls that don't apply to it dim
	const Rml::String showing = w && perWeapon && own != "default" ? own : main;
	const bool classic = showing.size() == 1 && showing[0] >= 'a' && showing[0] <= 'j';
	Put( "crosshair.vector", !classic && showing != "none" ? "true" : "false" );
	Put( "crosshair.lines", showing == "cross" || showing == "t" ? "true" : "false" );
}

/*
<crosshair [shape="c"] [weapon="railgun"] [fit="34"]/>: the crosshair as the HUD draws it, centered in the
element; the main one, or the weapon's (with per-weapon crosshairs on), or the shape given. Without fit,
at its size in the match (a 1080p pixel is the window's height / 1080); with fit, in dp (the page's
scale) and no bigger than fit: the shape tiles.
*/
struct Piece {
	Rml::Geometry geometry;
	Rml::Texture texture;
};

struct Building {
	Rml::RenderManager *manager;
	std::vector<Piece> *pieces;
	float alpha;
};

void Sink( const ozHudVertex_t *verts, int numVerts, int image, void *ctx ) {
	Building *b = (Building *)ctx;
	Rml::Mesh mesh;
	mesh.vertices.reserve( numVerts );
	mesh.indices.reserve( numVerts );
	for ( int i = 0; i < numVerts; i++ ) {
		Rml::Vertex v;
		v.position = Rml::Vector2f( verts[i].xy[0], verts[i].xy[1] );
		v.tex_coord = Rml::Vector2f( verts[i].st[0], verts[i].st[1] );
		v.colour = Rml::Colourb( verts[i].color[0], verts[i].color[1], verts[i].color[2],
			(Rml::byte)( verts[i].color[3] * b->alpha + 0.5f ) ).ToPremultiplied();
		mesh.vertices.push_back( v );
		mesh.indices.push_back( i );
	}
	b->pieces->push_back( Piece{ b->manager->MakeGeometry( std::move( mesh ) ), Hud_ImageTexture( image ) } );
}

class ElementCrosshair : public Rml::Element {
public:
	explicit ElementCrosshair( const Rml::String &tag ) : Rml::Element( tag ) {}

protected:
	void OnRender() override {
		Rml::RenderManager *m = GetRenderManager();
		Rml::Context *c = GetContext();
		if ( !m || !c || !OzUI_HudAvailable() ) {
			return;
		}
		const float fit = GetAttribute<float>( "fit", 0.0f );
		const float k = fit > 0 ? c->GetDensityIndependentPixelRatio() : c->GetDimensions().y / 1080.0f;
		const float size = fit > 0 ? std::fmin( (float)atof( OzE_CvarString( "cg_crosshairSize" ) ), fit ) : 0.0f;
		const Rml::String shape = GetAttribute<Rml::String>( "shape", "" );
		const int weapon = WeaponNumber( GetAttribute<Rml::String>( "weapon", "" ) );
		const float alpha = GetComputedValues().opacity();
		const float hitT = ( OzE_Milliseconds() - testHit ) / 1000.0f;
		const bool flashing = hitT < 1.1f;	// Flash length goes to a second
		const Rml::String key = Rml::CreateString( "%g %g %g %s %d %s", k, size, alpha, shape.c_str(), weapon, drawnKey.c_str() );

		if ( key != lastKey || flashing || wasFlashing ) {
			lastKey = key;
			pieces.clear();
			Building b = { m, &pieces, alpha };
			OzHud_CrosshairPreview( 0, 0, k, shape.empty() ? nullptr : shape.c_str(), weapon, size, hitT, Sink, &b );
		}
		wasFlashing = flashing;
		// the middle, on a whole pixel as the HUD's is (the window's width and height are even)
		const Rml::Vector2f at = GetAbsoluteOffset( Rml::BoxArea::Content ) + GetBox().GetSize( Rml::BoxArea::Content ) * 0.5f;
		const Rml::Vector2f middle( std::round( at.x ), std::round( at.y ) );
		for ( const Piece &p : pieces ) {
			p.geometry.Render( middle, p.texture );
		}
	}

private:
	Rml::String lastKey;
	bool wasFlashing = false;
	std::vector<Piece> pieces;
};

Rml::ElementInstancerGeneric<ElementCrosshair> crosshairInstancer;

} // namespace

void Crosshair_Init() {
	Rml::Factory::RegisterElementInstancer( "crosshair", &crosshairInstancer );
}

void Crosshair_Opened() {
	Menu_Set( "crosshair.weapon", "all" );
	Menu_Set( "crosshair.weapon_name", "" );
	Menu_Set( "crosshair.live", IsPageOpen( "pause" ) ? "true" : "false" );	// over a match
	Sync();
}

void Crosshair_Frame() {
	drawnKey.clear();
	for ( const char *name : drawnCvars ) {
		drawnKey += OzE_CvarString( name );
		drawnKey += '|';
	}
	for ( int i = 1; i < numWeapons; i++ ) {
		drawnKey += OzE_CvarString( ( Rml::String( "oz_crosshair_weapon_" ) + weaponKeys[i] ).c_str() );
		drawnKey += '|';
	}
	if ( !IsPageOpen( "crosshair" ) ) {
		return;
	}
	Sync();
	if ( IsTrue( Menu_Get( "crosshair.live" ) ) ) {
		OzHud_CrosshairWeapon( WeaponNumber( Menu_Get( "crosshair.weapon" ) ) );
	}
}

bool Crosshair_Action( const Rml::String &verb, const Rml::String &arg ) {
	if ( verb == "xhweapon" ) {			// xhweapon <weapon | all>: a tab
		const int w = WeaponNumber( arg );
		Rml::String name;
		for ( int i = 0; w; i++ ) {
			const char *id = Menu_Get( Rml::CreateString( "crosshair.weapons.%d.id", i ).c_str() );
			if ( !*id || arg == id ) {
				name = Menu_Get( Rml::CreateString( "crosshair.weapons.%d.name", i ).c_str() );
				break;
			}
		}
		Menu_Set( "crosshair.weapon", w ? arg.c_str() : "all" );
		Menu_Set( "crosshair.weapon_name", name.c_str() );
		Sync();
	} else if ( verb == "xhshape" ) {	// xhshape <shape | default>: the tab's; a weapon's own turns per-weapon crosshairs on
		const int w = WeaponNumber( Menu_Get( "crosshair.weapon" ) );
		if ( !w ) {
			if ( arg != "default" ) {
				Settings_Set( "crosshair/shape", arg.c_str() );
			}
		} else {
			Settings_Set( WeaponSetting( w ).c_str(), arg.c_str() );
			if ( arg != "default" && !IsTrue( Settings_Value( "crosshair/per_weapon" ) ) ) {
				Settings_Set( "crosshair/per_weapon", "true" );
				Menu_Toast( "Per-weapon crosshairs are on.", "info" );
			}
		}
		Sync();
	} else if ( verb == "xhhit" ) {		// Test hit: the previews and the match's crosshair flash the hit color
		testHit = OzE_Milliseconds();
		OzHud_CrosshairTestHit();
	} else if ( verb == "xhreset" ) {	// every crosshair setting to its default
		Settings_Reset( "crosshair/" );
		Sync();
		Menu_Toast( "Crosshair reset.", "info" );
	} else {
		return false;
	}
	return true;
}

} // namespace OzUI
