/*
Oz: the native UI's C++ parts, shared between oz_ui.cpp (RmlUi, pages, input, sounds, test
hooks), oz_ui_render.cpp (RmlUi's render interface on the renderer's UI calls) and
oz_ui_settings.cpp (ui/settings.json, its cvars and the generated Settings page).
*/
#pragma once

#include <RmlUi/Core.h>

#include <filesystem>
#include <set>
#include <string>
#include <map>
#include <vector>

struct cJSON;

extern "C" {
#include "oz_ui_engine.h"
#include "../../../quake3e/code/client/keycodes.h"
}

namespace OzUI {

// ---- oz_ui.cpp

// ui_qmenu.c's four: menu1 in, menu2 move, menu3 out, menu4 buzz
enum class Sound { In, Move, Out, Buzz };
void PlaySound( Sound sound );

// programmatic focus is silent, as q3_ui's cursor placement is
void FocusQuietly( Rml::Element *element );

Rml::Context *GetContext();
void Printf( const char *fmt, ... );

inline int StrICmp( const char *a, const char *b ) {
	for ( ;; a++, b++ ) {
		const int d = tolower( (unsigned char)*a ) - tolower( (unsigned char)*b );
		if ( d || !*a ) {
			return d;
		}
	}
}

// asked first with every key press while set (a bind slot waiting for its key); true takes the key
extern bool (*keyHook)( int key );

// the page stack, for the menus' actions
bool OpenPageByName( const char *name );
void GoBack();
void CloseAllPages();
bool IsPageOpen( const char *name );
const char *TopPageName();					// "" with no page open
Rml::ElementDocument *TopDocument();

// ---- oz_ui_hud.cpp (P6: the HUD canvas)
bool Hud_Pending();							// cgame drew something this frame
void Hud_Render( int width, int height );	// inside the UI's frame, before the pages; then dropped
void Hud_Clear();
void Hud_RendererShutdown();				// the renderer's textures go (each map load): read them ahead again
void Hud_Shutdown();
Rml::Texture Hud_ImageTexture( int image );	// an OzUI_HudImage handle's texture

// ---- oz_ui_render.cpp

class RenderInterface : public Rml::RenderInterface {
public:
	// the renderer's UI calls for this frame; NULL while it is down or has none
	const ozUiRender_t *r = nullptr;

	void Acquire();				// the renderer's UI calls, before RmlUi lays out (images load then)
	bool BeginFrame( int width, int height );
	void LoadTiles( bool all );	// the "/@tile/" images waiting: a few a frame, or all at once (a capture)
	void EndFrame();
	void RendererShutdown();	// release everything RmlUi holds of the renderer's

	Rml::CompiledGeometryHandle CompileGeometry( Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices ) override;
	void RenderGeometry( Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture ) override;
	void ReleaseGeometry( Rml::CompiledGeometryHandle geometry ) override;
	Rml::TextureHandle LoadTexture( Rml::Vector2i &dimensions, const Rml::String &source ) override;
	Rml::TextureHandle GenerateTexture( Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions ) override;
	void ReleaseTexture( Rml::TextureHandle texture ) override;
	void EnableScissorRegion( bool enable ) override;
	void SetScissorRegion( Rml::Rectanglei region ) override;
	void EnableClipMask( bool enable ) override;
	void RenderToClipMask( Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation ) override;
	void SetTransform( const Rml::Matrix4f *transform ) override;
	Rml::LayerHandle PushLayer() override;
	void CompositeLayers( Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blend_mode,
		Rml::Span<const Rml::CompiledFilterHandle> filters ) override;
	void PopLayer() override;
	Rml::TextureHandle SaveLayerAsTexture() override;
	Rml::CompiledFilterHandle SaveLayerAsMaskImage() override;
	Rml::CompiledFilterHandle CompileFilter( const Rml::String &name, const Rml::Dictionary &parameters ) override;
	void ReleaseFilter( Rml::CompiledFilterHandle filter ) override;
	Rml::CompiledShaderHandle CompileShader( const Rml::String &name, const Rml::Dictionary &parameters ) override;
	void RenderShader( Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
		Rml::TextureHandle texture ) override;
	void ReleaseShader( Rml::CompiledShaderHandle shader ) override;

private:
	unsigned generation = 1;	// in each handle's top bits: a handle from before a renderer restart is ignored
	struct Tile { int handle; Rml::String path; };
	std::vector<Tile> tiles;	// loaded by LoadTiles into the stand-in handle given out
	bool tilesNow = false;		// a capture waits: tiles load as asked
	int scissor[4] = {};
	bool scissorOn = false;

	uintptr_t Wrap( int handle ) const;
	int Unwrap( uintptr_t handle ) const;
};

RenderInterface &Renderer();

// ---- oz_ui_settings.cpp

void Settings_Init();									// ui/settings.json -> entries and their cvars
int Settings_Loaded();									// how many entries
Rml::String Settings_PageRml();							// the generated Settings page
void Settings_Attach( Rml::ElementDocument *doc );		// its data model is bound before loading
void Settings_BindModel( Rml::Context *context );
void Settings_Frame();									// cvars -> the page, while it shows
void Settings_AfterKey();								// after a key the context took: a focus the page moves elsewhere

// oz_ui_font.cpp: the display font traced from the player's pak0, as TrueType (empty without it)
const std::vector<unsigned char> &Font_MakeDisplay( Rml::String &how );
const std::vector<unsigned char> &Font_Emoji( Rml::String &how );	// Windows' Segoe UI Emoji (empty without it)
bool Settings_Tab( int direction );						// shoulder buttons; false if no tabs
bool Settings_TabCount( int *count );
void Settings_SetTab( int index );
bool Settings_Set( const char *key, const char *value );	// as the page sets it
const char *Settings_Get( const char *key );				// as the page shows it
const char *Settings_Value( const char *key );			// as Settings_Set takes it
void Settings_CopyCode( const char *prefix );			// a share code to the clipboard
int Settings_PasteCode();								// from the clipboard: how many settings, -1 if it isn't a code
void Settings_Reset( const char *prefix );				// every setting under prefix to its default
void Settings_Commands();
void Settings_DisplayKeep();							// display_confirm.rml: keep the new window settings
void Settings_DisplayRevert();							// or put the old ones back (Escape, or 15 s)
int Settings_DisplayPending();
void Settings_ApplyChanges();							// the Video tab's changes, waiting for Apply, set
void Settings_Discard();								// or dropped
int Settings_Unapplied();								// how many wait
void Settings_Leave( bool apply );						// apply_confirm.rml: apply or drop them, and close Settings
void Settings_PickColor( const char *key );				// opens color_picker.rml for a color setting
void Settings_PickerSet( const char *hex );				// color_picker.rml: the chosen color

// ---- oz_ui_menu.cpp: the menus' data model, actions and the main menu's logic (P5)

void Menu_Init();										// ui/data/menu.json, the action instancer
void Menu_BindModel( Rml::Context *context );			// the "menu" data model, before the pages load
void Menu_Shutdown();
void Menu_Commands();
void Menu_Attach( const char *name, Rml::ElementDocument *doc );
void Menu_Opened( const char *name );					// a page opened (the player's name, the pause menu's state)
void Menu_Covered( const char *top );					// the page now on top, for the main menu's shading
bool Menu_Back( const char *page );						// true: the page handled going back
bool Menu_Side( Rml::Element *target, int dir );
bool Menu_Tab( Rml::ElementDocument *doc, int dir );	// Q and E: false if the page has no tabs
void Menu_OpenSub( const char *name );
void Menu_Social();
void Menu_SplashDone();
void Menu_Q3Import( bool apply );						// q3import.rml shown, or the import done (oz_q3import.c)
void Menu_Frame();
void Menu_Accent( float rgb[3] );
void Menu_Toast( const char *text, const char *tone );
void Menu_Run( const Rml::String &commands, Rml::Element *element );
const char *Menu_Get( const char *path );
bool Menu_Set( const char *path, const char *value );

// ---- oz_ui_crosshair.cpp: the crosshair editor (crosshair.rml) and the <crosshair> element the HUD draws
void Crosshair_Init();						// the element's instancer
void Crosshair_Opened();					// the editor opened: its values, live over a match or not
void Crosshair_Frame();						// its values follow the settings while it's open
bool Crosshair_Action( const Rml::String &verb, const Rml::String &arg );	// xhweapon, xhshape, xhhit, xhreset

// ---- oz_ui_gun.cpp: the gun editor (gun.rml)
void Gun_Opened();							// its values, live over a match or not
void Gun_Frame();							// its values follow the settings while it's open
bool Gun_Action( const Rml::String &verb, const Rml::String &arg );	// gunreset

// ---- oz_ui_social.cpp (P7, P8): the online pages' parts
bool Social_Action( const Rml::String &verb, const Rml::String &arg, Rml::Element *element );	// browse, join, joinask, askcancel, unjoin, quick...
void Social_Opened( const char *page );
void Social_Frame();						// the Server Browser's refresh, the connection screen
Rml::String Social_FollowText( const char *kind, const char *key, const char *password );	// a party leader's hosted match ("host")
							// or the community server they join ("server", its address): the hub's party_follow, else ""

// ---- oz_ui_servers.cpp (P8b): the Server Browser's community servers
bool Servers_Action( const Rml::String &verb, const Rml::String &arg, Rml::Element *element );	// servers, serverjoin, serverinfo, sbsort...
void Servers_Opened( const char *page );
void Servers_Frame();						// the rows follow the sweep and the filters; the server card its status
bool Servers_Back();						// the server card was open: Back closed it
void Servers_MatchesChanged();				// the hub's peer-to-peer rows came again
void Servers_JoinedMatch( const char *host, const char *name );	// a peer-to-peer match joined: the history's
std::string Servers_Name( const char *address );	// a server row's name, "" when it isn't one
void Servers_FollowServer( const char *address, const char *password );	// the party leader joined a community server: join it too (net_follow)

// ---- oz_ui_skirmish.cpp: Skirmish's mod picker, search and sorts, and Defrag's race maps
bool Skirmish_Action( const Rml::String &verb, const Rml::String &arg );	// skimport, race, skmods
void Skirmish_Opened( const char *page );
void Skirmish_Commands();					// ui_skirmish, for tests
void Skirmish_SetMod( const char *mod );	// the tab the page opens on next (the Mod Library's Play)

// the Mod Library (oz_ui_mods.cpp, PH step 2)
bool Mods_Action( const Rml::String &verb, const Rml::String &arg );	// modlib_import, modlib_go, modlib_play, modlib_remove...
void Mods_Opened( const char *page );
bool Mods_Back();							// the import card or the remove question was open: Back closed it
void Mods_Commands();						// oz_modlib, for tests
void Skirmish_Frame();						// the race maps looked for; the cards follow the mod, the search and the sort

// ---- oz_progress.cpp (P12): the career, cards and campaign, ported from the Godot build
namespace Prog {
enum { NUM_SLOTS = 7, SHOWCASE = 3 };
extern const char *const SLOTS[NUM_SLOTS][2];	// card slot, part kind
struct LevelInfo { int level; long long into, next; };
struct Arena { std::string map, name; int fraglimit; std::vector<std::string> bots; };
struct Tier { std::string name; std::vector<Arena> arenas; };

cJSON *Get( const cJSON *o, const char *k );
std::string Str( const cJSON *o, const char *k, const char *def = "" );
double Num( const cJSON *v, double def = 0 );

cJSON *Catalog();
cJSON *Parts( const char *kind );
cJSON *Part( const char *kind, const std::string &id );
cJSON *Achievements();
cJSON *Achievement( const std::string &id );
cJSON *DefaultCard();
double Stat( const cJSON *stats, const std::string &key );
double ProgressOf( const cJSON *a, const cJSON *profile );
std::vector<std::string> Missing( const cJSON *a, const cJSON *profile );
std::set<std::string> Achieved( const cJSON *profile );
bool IsUnlocked( const cJSON *rule, const cJSON *profile, const std::set<std::string> *done );
std::string UnlockText( const cJSON *part );
cJSON *EnforceUnlocks( const cJSON *card, const cJSON *profile );	// new
cJSON *KnownParts( const cJSON *card );								// new
LevelInfo LevelFor( long long totalXp );
int MaxLevel();
cJSON *Rank( long long rating );									// new
std::string StatValue( const std::string &id, const cJSON *profile );

cJSON *Profile();								// the career (q3_profile.gd's data)
void Save();
std::filesystem::path Folder();					// <fs_homepath>/profile: the career's, the same under every mod
cJSON *ReadJson( const std::filesystem::path &p );	// nullptr when missing or not JSON
void WriteJson( const std::filesystem::path &p, const cJSON *j );
long long TotalXp();
cJSON *ShownCard();								// new: the card others see
cJSON *Ranked();								// new
std::vector<std::string> UnlockedIds( const cJSON *profile );
bool SetCard( const std::string &slot, const std::string &id );
bool SetShowcase( const std::string &kind, const std::vector<std::string> &ids );
cJSON *BotCard( const std::string &name, const std::string &model, int skill );	// new

cJSON *Campaign();								// the campaign's progress {skill, won, awards}
void SetArenas( const cJSON *maps );			// the menus' maps (arenas.txt order) -> the tiers
const std::vector<Tier> &Tiers();
bool TierOpen( int tier );
bool TierComplete( int tier );
int WonAt( const std::string &map );			// the best skill an arena was won at, 0 if never
int CampaignSkill();
void SetCampaignSkill( int skill );
void ResetCampaign();
void CampaignStart( const std::string &map );
const char *TakePendingPage();					// the page to open over the main menu after a campaign match
void Commands();

// oz_ui_profile.cpp: the pages' side
struct CardArt { std::string banner, emblem, accent, frameColor; int bannerWide; };
CardArt ArtOf( const cJSON *card );				// the HUD's images and colors for a card
void Changed();									// the career or the campaign changed: the pages follow
cJSON *CardView( const cJSON *card, int level, const cJSON *statValues, const char *ident );	// new: a card in the pages' shape
void Profile_Fill();
void Campaign_Fill();
void MatchHistory( const cJSON *m );			// the hub's match_history: the Matches tab (P8b)
void SharedCareer( const cJSON *m );			// the hub's career: the online career beside it
}
bool Profile_Action( const Rml::String &verb, const Rml::String &arg );	// oz_ui_profile.cpp: profileset, showcase, campaign, campaignreset

// ---- oz_ui_editor.cpp (P13): the map editor's page
void Editor_Commands();
bool Editor_Key( int key, bool down );		// asked first with every key while the editor's page is on top
void Editor_Mouse( int x, int y );			// the pointer, window pixels
bool Editor_Draw( int width, int height );	// its 3D view, before the page layer; false when it isn't open
void Editor_Opened( Rml::ElementDocument *doc );	// its page shown: the panel made and chained for the d-pad

// ---- oz_ui_test (in oz_ui.cpp): pages for the focus walk
struct FocusWalkResult {
	Rml::String page;
	int reached = 0, total = 0;
	Rml::StringList missed;
};

// "/@thumb/<image>" sources the renderer couldn't load this run (oz_ui_render.cpp), by image path: the
// map editor's browser leaves those materials out. ThumbFailedRev() goes up with each new one.
bool ThumbFailed( const std::string &image );

// The info blocks of the game's scripts: first (scripts/arenas.txt, scripts/bots.txt), then every scripts/*<ext>
// (oz_ui_menu.cpp), each block's keys in lowercase
void ReadInfoFiles( const char *first, const char *ext, std::vector<std::map<Rml::String, Rml::String>> &out );
int ThumbFailedRev();

} // namespace OzUI
