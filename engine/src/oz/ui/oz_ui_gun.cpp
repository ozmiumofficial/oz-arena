/*
Oz: the gun editor (ui/pages/gun.rml): Gameplay's Gun settings on a page of their own, from the pause menu
and from Settings' Gameplay tab. The engine places the gun from them under every cgame (oz_hud_view.c
PlaceGun), as the scene renders, so over a match (gun.live) the match's own gun is the preview and moves
as the controls change; the panel sits on the side away from it. From the main menu there's no gun to
show. The controls write the settings as they change (the presets through Settings' preset tables), and
the page's values follow the settings every frame while it's open, so a preset, Reset and the console
show at once.
*/
#include "oz_ui_internal.h"
#include "oz_ui.h"

#include <cstring>

namespace OzUI {

namespace {

// gun.<name> <- the setting
const char *const shownValues[][2] = {
	{ "preset", "gameplay/gun_preset" }, { "position", "gameplay/draw_gun" }, { "forward", "gameplay/gun_forward" },
	{ "side", "gameplay/gun_offset" }, { "height", "gameplay/gun_height" }, { "pitch", "gameplay/gun_pitch" },
	{ "yaw", "gameplay/gun_yaw" }, { "roll", "gameplay/gun_roll" }, { "fov", "gameplay/gun_fov" },
	{ "widescreen", "gameplay/gun_widescreen" } };

void Sync() {
	for ( const auto &v : shownValues ) {
		const char *value = Settings_Value( v[1] );
		const Rml::String path = Rml::String( "gun." ) + v[0];
		if ( *value && strcmp( Menu_Get( path.c_str() ), value ) ) {
			Menu_Set( path.c_str(), value );
		}
	}
}

} // namespace

void Gun_Opened() {
	Menu_Set( "gun.live", IsPageOpen( "pause" ) ? "true" : "false" );	// over a match
	Sync();
}

void Gun_Frame() {
	if ( IsPageOpen( "gun" ) ) {
		Sync();
	}
}

bool Gun_Action( const Rml::String &verb, const Rml::String & ) {
	if ( verb == "gunreset" ) {		// every Gun setting to its default: Classic
		for ( const auto &v : shownValues ) {
			Settings_Reset( v[1] );
		}
		Sync();
		Menu_Toast( "Gun reset.", "info" );
		return true;
	}
	return false;
}

} // namespace OzUI
