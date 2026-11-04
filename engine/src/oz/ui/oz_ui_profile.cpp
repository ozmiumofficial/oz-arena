/*
===========================================================================
Oz Arena: the Profile and Campaign pages' data (ENGINE_PLAN.md P12). GPL-2.0.

The career, cards and campaign (oz_progress.cpp) put into the menu model the pages bind, in the shapes
P5's stand-in data had (engine/tools/menu/gen_cards_data.py made them from the Godot build's demo):
  profile			the Profile page: the card, every slot's parts with what's earned, the showcase,
					the achievements, the career
  campaign			the Campaign page's choices, medals and counts; campaign_tiers its tiers
  leaderboards		each row's card (oz_ui_social.cpp calls CardView)

A part's look is its own art from zz-oz-cards.pk3 (the Godot build's library/cards, packed when the
engine is staged: tools/make_cards_pk3.py), else the player's paks (level shots, icons), else the
gradients the stand-in data drew.

Menu actions (oz_ui_menu.cpp passes them on):
  profileset <slot> <id>			a part on the card (refused while locked)
  showcase <stats|badges> <id>		a showcase stat or badge on or off
  campaign <map>					plays a campaign arena at the page's skill
  campaignreset						starts the campaign over (the career keeps its XP)
===========================================================================
*/
#include "oz_ui_internal.h"
#include "oz_progress.h"
#include "cJSON.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <vector>

namespace OzUI {
cJSON *Menu_Node( const char *path );	// oz_ui_menu.cpp
void Menu_Dirty( const char *path );
}

namespace OzUI {
namespace Prog {

namespace {

const char *const NONE = "none";
const char *const FALLBACK_ICON = "/menu/medals/medal_frags";
const char *const SMOKE = "radial-gradient(ellipse at 70% 60%, #2a1512 0%, #070605 70%)";
const char *const PLATE = "repeating-linear-gradient(135deg, #1a1412 0dp, #1a1412 7dp, #14100e 7dp, #14100e 14dp)";
const char *const BEVEL = "linear-gradient(180deg, #ffffff2e 0%, #ffffff00 6%, #00000000 80%, #000000a6 100%), "
	"linear-gradient(160deg, #00000026 0%, #00000073 100%)";

const std::map<std::string, std::string> TIER = { { "bronze", "#c27a45" }, { "silver", "#c3ccd6" }, { "gold", "#ffc93c" },
	{ "platinum", "#4fe0c8" }, { "diamond", "#6ab8ff" }, { "godlike", "#ff3d22" } };
const std::map<std::string, std::string> SKY = { { "sky_hellsky", "#7a1a0a" }, { "sky_hellsky2bright", "#a8401a" }, { "sky_kc_dm10sky", "#5a3a6a" },
	{ "sky_kcbasesky_arena1_sky", "#3a4a6a" }, { "sky_killsky", "#6a2a1a" }, { "sky_nightsky_xian_dm1", "#1a2a5a" }, { "sky_skybox", "#4a5a7a" } };
const std::map<std::string, std::string> SKYTEX = { { "sky_hellsky", "textures/skies/intelredclouds" }, { "sky_hellsky2bright", "textures/skies/intelredclouds" },
	{ "sky_kc_dm10sky", "textures/skies/inteldimclouds" }, { "sky_kcbasesky_arena1_sky", "textures/skies/pjbasesky" },
	{ "sky_killsky", "textures/skies/killsky_1" }, { "sky_nightsky_xian_dm1", "textures/skies/bluedimclouds" }, { "sky_skybox", "textures/skies/topclouds" } };
const char *const MATC[][2] = { { "lava", "#ff6a1a" }, { "slime", "#6acf2a" }, { "blue", "#3a6aff" }, { "red", "#c81a0a" }, { "gold", "#ffc83a" },
	{ "electric", "#5ab4ff" }, { "flesh", "#8a2a2a" } };
const std::map<std::string, std::string> CAMPAIGN_FRAME = { { "frame_training", "#6b625b" }, { "frame_tier1", "#7a5a3a" }, { "frame_tier2", "#8a6a3a" },
	{ "frame_tier3", "#9a7a3a" }, { "frame_tier4", "#a8562a" }, { "frame_tier5", "#b8402a" }, { "frame_tier6", "#c02a1a" }, { "frame_final", "#e0b040" },
	{ "frame_nightmare", "#d01010" } };
const std::map<std::string, std::string> SHELL = { { "frame_quad", "#4a6bff" }, { "frame_battlesuit", "#ffcc33" }, { "frame_regen", "#ff3030" },
	{ "frame_haste", "#ff9a30" }, { "frame_invis", "#b8d8ff" } };

std::string F( const char *fmt, ... ) {
	char buf[1024];
	va_list ap;
	va_start( ap, fmt );
	vsnprintf( buf, sizeof( buf ), fmt, ap );
	va_end( ap );
	return buf;
}

std::string Thousands( long long n ) {
	std::string s = std::to_string( n < 0 ? -n : n ), out;
	for ( size_t i = 0; i < s.size(); i++ ) {
		if ( i && ( s.size() - i ) % 3 == 0 ) {
			out += ',';
		}
		out += s[i];
	}
	return n < 0 ? "-" + out : out;
}

std::string Pct( double f ) {
	return F( "%.1f%%", std::min( 1.0, std::max( 0.0, f ) ) * 100 );
}

void Rgb( const std::string &hex, int c[3] ) {
	const unsigned v = (unsigned)strtoul( hex.c_str() + ( hex[0] == '#' ), nullptr, 16 );
	const bool rgba = hex.size() > 7;
	c[0] = ( v >> ( rgba ? 24 : 16 ) ) & 255;
	c[1] = ( v >> ( rgba ? 16 : 8 ) ) & 255;
	c[2] = ( v >> ( rgba ? 8 : 0 ) ) & 255;
}

std::string Lighten( const std::string &hex, double t = 0.3 ) {
	int c[3];
	Rgb( hex, c );
	return F( "#%02x%02x%02x", (int)( c[0] + ( 255 - c[0] ) * t ), (int)( c[1] + ( 255 - c[1] ) * t ), (int)( c[2] + ( 255 - c[2] ) * t ) );
}

std::string Darken( const std::string &hex, double t = 0.55 ) {
	int c[3];
	Rgb( hex, c );
	return F( "#%02x%02x%02x", (int)( c[0] * ( 1 - t ) ), (int)( c[1] * ( 1 - t ) ), (int)( c[2] * ( 1 - t ) ) );
}

// a pak image without its extension, or "" when the paks don't have it (cached: the paks don't change under a menu)
std::map<std::string, std::string> imgCache;
std::string Img( const std::string &path ) {
	if ( path.empty() ) {
		return "";
	}
	auto c = imgCache.find( path );
	if ( c != imgCache.end() ) {
		return c->second;
	}
	std::string out;
	for ( const char *ext : { ".tga", ".jpg", ".png" } ) {
		if ( OzE_FileExists( ( path + ext ).c_str() ) ) {
			out = "/" + path;
			break;
		}
	}
	return imgCache[path] = out;
}

// a part's art in zz-oz-cards.pk3 ("cards/banners/x.jpg" is ui/cards/banners/x.jpg there), without its extension
std::string Art( const cJSON *p ) {
	const std::string art = Str( p, "art" );
	if ( art.empty() ) {
		return "";
	}
	auto c = imgCache.find( "@" + art );
	if ( c != imgCache.end() ) {
		return c->second;
	}
	const size_t dot = art.rfind( '.' );
	const std::string out = OzE_FileExists( ( "ui/" + art ).c_str() ) ? "/ui/" + art.substr( 0, dot ) : "";
	return imgCache["@" + art] = out;
}

std::string Accent( const std::string &id ) {
	const std::string v = Str( Part( "accents", id ), "value" );
	return v.empty() ? "#ff2020" : v;
}

std::string MatColor( const cJSON *p ) {
	const std::string n = Str( p, "id" ) + " " + Str( p, "material" );
	for ( auto &m : MATC ) {
		if ( n.find( m[0] ) != std::string::npos ) {
			return m[1];
		}
	}
	return "#6a5a4a";
}

// the stat banners' career: the favorite arena, the best weapon, the top medal (q3_profile.gd _banner_ctx)
struct BannerCtx { std::string favoriteMap, bestWeapon, topMedal; };

std::string Best( const cJSON *m ) {
	std::string top;
	double n = 0;
	const cJSON *e;
	cJSON_ArrayForEach( e, m ) {
		if ( Num( e ) > n ) {
			n = Num( e );
			top = e->string;
		}
	}
	return top;
}

BannerCtx CtxOf( const cJSON *profile ) {
	const cJSON *st = Get( profile, "stats" );
	return { Best( Get( st, "wins_map" ) ), Best( Get( st, "frags_weapon" ) ), Best( Get( st, "medal" ) ) };
}

std::string EmblemArt( const std::string &eid );

// (bg, fg) decorators for a banner part: bg fills the 4:1 box, fg (or none) lies over it
std::pair<std::string, std::string> Banner( const std::string &pid, const BannerCtx *ctx ) {
	const cJSON *p = Part( "banners", pid );
	if ( !p || pid == "void" ) {
		return { SMOKE, NONE };
	}
	const std::string kind = Str( p, "kind" );
	if ( kind == "stat" ) {
		if ( pid == "favorite_arena" ) {
			const std::string m = ctx && !ctx->favoriteMap.empty() ? ctx->favoriteMap : "q3dm17";
			const std::string art = Art( Part( "banners", "map_" + m ) );
			const std::string shot = !art.empty() ? art : Img( "levelshots/" + m );
			return { shot.empty() ? PLATE : "image(" + shot + " cover)", NONE };
		}
		if ( pid == "weapon_mastery" ) {
			const std::string icon = EmblemArt( "iconw_" + ( ctx && !ctx->bestWeapon.empty() ? ctx->bestWeapon : "rocket" ) );
			return { PLATE, icon.empty() ? NONE : "image(" + icon + " contain left)" };
		}
		if ( pid == "trophy_wall" ) {
			const std::string icon = EmblemArt( "medal_" + ( ctx && !ctx->topMedal.empty() ? ctx->topMedal : "excellent" ) );
			return { PLATE, icon.empty() ? NONE : "image(" + icon + " contain left)" };
		}
		if ( pid == "rank_crest" ) {
			const std::string c = TIER.at( "platinum" );
			return { F( "linear-gradient(110deg, #0a0807 30%%, %s55 55%%, #0a0807 56%%, #0a0807 62%%, %s33 63%%, #0a0807 80%%)", c.c_str(), c.c_str() ), NONE };
		}
		if ( pid == "campaign_map" ) {
			return { PLATE, "repeating-linear-gradient(90deg, #ffad33cc 0dp, #db2410cc 5dp, #00000000 5dp, #00000000 9dp)" };
		}
		return { "linear-gradient(120deg, #1a1408 0%, #6a4a12 50%, #140f06 100%)",
			"repeating-linear-gradient(120deg, #00000000 0dp, #00000000 16dp, #ffffff0a 16dp, #ffffff0a 17dp)" };
	}
	if ( kind == "prestige" ) {
		static const std::map<std::string, std::string> over = {
			{ "quad_damage", "radial-gradient(ellipse at 50% 50%, #466effb3 0%, #283cff40 60%, #00000000 100%)" },
			{ "lava_bath", "linear-gradient(180deg, #00000000 0%, #ff6e1480 45%, #ff3c00d9 70%, #3a0800 100%)" },
			{ "longest_yard", "linear-gradient(90deg, #00000000 0%, #3060ff80 60%, #ffffffb3 100%)" },
			{ "gibbed", "radial-gradient(ellipse at 50% 90%, #8a0a04cc 0%, #00000000 60%)" },
			{ "nightmare", "radial-gradient(ellipse at 50% 40%, #00000000 30%, #b400008c 100%)" } };
		const std::string art = Art( p );
		auto o = over.find( pid );
		return { !art.empty() ? "image(" + art + " cover)" : Banner( Str( p, "base", "void" ), ctx ).first, o == over.end() ? NONE : o->second };
	}
	const std::string art = Art( p );
	if ( !art.empty() ) {
		return { "image(" + art + " cover)", NONE };
	}
	if ( !pid.compare( 0, 4, "map_" ) ) {
		const std::string shot = Img( "levelshots/" + pid.substr( 4 ) );
		return { shot.empty() ? PLATE : "image(" + shot + " cover)", NONE };
	}
	if ( !pid.compare( 0, 4, "sky_" ) ) {
		auto t = SKYTEX.find( pid );
		const std::string tex = t == SKYTEX.end() ? "" : Img( t->second );
		auto s = SKY.find( pid );
		const std::string c = s == SKY.end() ? "#4a3a3a" : s->second;
		if ( !tex.empty() ) {
			return { "image(" + tex + " cover)", F( "linear-gradient(90deg, %s99 0%%, %s33 100%%)", c.c_str(), c.c_str() ) };
		}
		return { F( "linear-gradient(120deg, %s 0%%, %s 50%%, %s 100%%)", Darken( c, 0.7 ).c_str(), c.c_str(), Darken( c, 0.8 ).c_str() ), NONE };
	}
	if ( !pid.compare( 0, 5, "char_" ) ) {
		const std::string icon = Img( "models/players/" + pid.substr( 5 ) + "/icon_default" );
		return { "linear-gradient(90deg, #070605 0%, #2a1512 60%, #3a1a12 100%)", icon.empty() ? NONE : "image(" + icon + " contain right)" };
	}
	if ( !pid.compare( 0, 4, "mat_" ) ) {
		const std::string c = MatColor( p );
		return { F( "linear-gradient(120deg, %s 0%%, %s 55%%, %s 100%%)", Darken( c, 0.75 ).c_str(), Darken( c, 0.35 ).c_str(), Darken( c, 0.85 ).c_str() ), NONE };
	}
	return { SMOKE, NONE };
}

// the frame's main color: its tier's, its powerup's, its material's, else the accent's
std::string FrameColor( const std::string &fid, const std::string &accent ) {
	const cJSON *f = Part( "frames", fid );
	const std::string style = Str( f, "style" );
	if ( !f || style == "plain" ) {
		return accent;
	}
	if ( style == "material" ) {
		const std::string tint = Str( f, "tint" );
		return tint.empty() ? MatColor( f ) : tint;
	}
	if ( style == "shell" ) {
		auto s = SHELL.find( fid );
		return s == SHELL.end() ? accent : s->second;
	}
	if ( style == "ranked" ) {
		auto t = TIER.find( fid.substr( fid.find( "frame_ranked_" ) == 0 ? 13 : 0 ) );
		return t == TIER.end() ? accent : t->second;
	}
	auto c = CAMPAIGN_FRAME.find( fid );
	return c == CAMPAIGN_FRAME.end() ? accent : c->second;
}

// (decorator, padding) for a frame part around a card of that accent
std::pair<std::string, std::string> Frame( const std::string &fid, const std::string &accent ) {
	const cJSON *f = Part( "frames", fid );
	const std::string style = Str( f, "style" );
	if ( !f || style == "plain" ) {
		return { F( "linear-gradient(160deg, %saa 0%%, #2a2320 40%%, #141110 100%%)", accent.c_str() ), "0.14em" };
	}
	const std::string art = Art( f );
	const std::string c = FrameColor( fid, accent );
	if ( !art.empty() ) {	// the art under a bevel: lit top edge, shaded foot
		return { std::string( BEVEL ) + ", image(" + art + " cover)", style == "material" || style == "shell" ? "0.42em" : "0.36em" };
	}
	if ( style == "material" ) {
		return { F( "linear-gradient(160deg, %s 0%%, %s 30%%, %s 60%%, %s 100%%)", Darken( c, 0.2 ).c_str(), Darken( c, 0.7 ).c_str(),
			Darken( c, 0.5 ).c_str(), Darken( c, 0.8 ).c_str() ), "0.42em" };
	}
	if ( style == "shell" ) {
		return { F( "linear-gradient(160deg, %s 0%%, %s 50%%, %s 100%%)", Lighten( c, 0.2 ).c_str(), Darken( c, 0.5 ).c_str(), c.c_str() ), "0.42em" };
	}
	return { F( "linear-gradient(160deg, %s 0%%, #1a1412 45%%, %s88 100%%)", c.c_str(), c.c_str() ), "0.36em" };
}

std::string EmblemArt( const std::string &eid ) {
	const std::string art = Art( Part( "emblems", eid ) );
	if ( !art.empty() ) {
		return art;
	}
	if ( !eid.compare( 0, 5, "head_" ) ) {
		const std::string rest = eid.substr( 5 );
		const size_t us = rest.find( '_' );
		return Img( "models/players/" + rest.substr( 0, us ) + "/icon_" + ( us == std::string::npos ? "default" : rest.substr( us + 1 ) ) );
	}
	if ( !eid.compare( 0, 6, "medal_" ) ) {
		return Img( "menu/medals/" + eid );
	}
	return Img( "icons/" + eid );
}

std::pair<std::string, std::string> RailDeco( const std::string &v ) {
	if ( v == "spectrum" ) {
		return { "linear-gradient(90deg, #ff3030 0%, #ffb020 17%, #f0ff30 33%, #30ff60 50%, #30c0ff 67%, #6040ff 83%, #ff30d0 100%)", "#ffffff" };
	}
	if ( v == "team" ) {
		return { "linear-gradient(90deg, #ff2a2a 0%, #ff2a2a 50%, #2a5aff 50%, #2a5aff 100%)", "#ffffff" };
	}
	return { F( "linear-gradient(90deg, %s 0%%, %s 100%%)", v.c_str(), v.c_str() ), v };
}

std::string MedalColor( const std::string &medal ) {
	return Str( Get( Catalog(), "medal_colors" ), medal.c_str(), "#ffffff" );
}

std::string AchIcon( const cJSON *a ) {
	const std::string icon = Str( a, "icon" );
	return !icon.compare( 0, 7, "emblem:" ) ? EmblemArt( icon.substr( 7 ) ) : "";
}

std::string AchGroup( const std::string &i ) {
	auto starts = [&]( const char *p ) { return !i.compare( 0, strlen( p ), p ); };
	auto in = [&]( std::initializer_list<const char *> l ) { for ( const char *x : l ) if ( i == x ) return true; return false; };
	if ( starts( "medal_" ) && i != "medal_collector" ) return "Medals";
	if ( starts( "weapon_" ) || i == "arsenal" ) return "Weapons";
	if ( starts( "powerup_" ) || in( { "every_powerup", "quad_god" } ) ) return "Powerups";
	if ( in( { "perfect", "untouchable", "rail_streak", "rail_accuracy", "air_rocket", "telefragger", "trifecta" } ) ) return "Feats";
	if ( starts( "speed_" ) || in( { "rocket_jumper", "plasma_climber" } ) ) return "Movement";
	if ( in( { "lava_swimmer", "lava_bath", "cratered", "sank", "telefragged" } ) ) return "Accidents";
	if ( in( { "duelist", "duel_100", "flag_returns", "clutch", "thaws", "instagib" } ) ) return "Modes";
	for ( const char *p : { "matches_", "wins_", "frags_" } ) {
		if ( starts( p ) && i.size() > strlen( p ) && std::all_of( i.begin() + strlen( p ), i.end(), ::isdigit ) ) return "Career";
	}
	if ( starts( "campaign_" ) ) return "Campaign";
	if ( in( { "tourist", "home_turf", "roster", "medal_collector" } ) ) return "Collections";
	if ( starts( "ranked_" ) ) return "Ranked";
	return "Other";
}
const char *const ACH_GROUPS[] = { "Medals", "Weapons", "Powerups", "Feats", "Movement", "Accidents", "Modes", "Career", "Campaign",
	"Collections", "Ranked", "Other" };

std::string Sentence( std::string s ) {
	for ( size_t i = 1; i < s.size(); i++ ) {
		s[i] = (char)tolower( (unsigned char)s[i] );
	}
	return s;
}

void Add( cJSON *o, const char *k, const std::string &v ) {
	cJSON_AddStringToObject( o, k, v.c_str() );
}

std::map<std::string, std::string> MapNames() {
	std::map<std::string, std::string> out;
	const cJSON *m;
	cJSON_ArrayForEach( m, Menu_Node( "maps" ) ) {
		out[Str( m, "map" )] = Str( m, "name" );
	}
	return out;
}

// the page's state keys survive a refill
void Keep( cJSON *to, const cJSON *from, std::initializer_list<const char *> keys ) {
	for ( const char *k : keys ) {
		const cJSON *v = Get( from, k );
		if ( v ) {
			cJSON_DeleteItemFromObjectCaseSensitive( to, k );
			cJSON_AddItemToObject( to, k, cJSON_Duplicate( v, 1 ) );
		}
	}
}

void ReplaceNode( const char *path, cJSON *src ) {
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
		cJSON_AddItemToArray( dst, cJSON_DetachItemViaPointer( src, c ) );	// keeps the item's key
	}
	cJSON_Delete( src );
	Menu_Dirty( path );
}

bool filling;
cJSON *matchRows;		// the Matches tab's rows from the hub's last match_history, null before one
cJSON *sharedCareer;	// the hub's last word on the online career (career, or match_history's)

std::string MatchesNote();
cJSON *SharedView( const cJSON *c );

} // namespace

// ---------------------------------------------------------------- a card

CardArt ArtOf( const cJSON *card ) {
	CardArt a;
	const std::string bid = Str( card, "banner" );
	const cJSON *bp = Part( "banners", bid );
	std::string art = Art( bp );
	if ( art.empty() && Str( bp, "kind" ) == "prestige" ) {
		art = Art( Part( "banners", Str( bp, "base" ) ) );
	}
	if ( !art.empty() ) {
		a.banner = art.substr( 1 );
		a.bannerWide = 1;
	} else if ( !bid.compare( 0, 4, "map_" ) ) {
		a.banner = "levelshots/" + bid.substr( 4 );
		a.bannerWide = 0;
	} else {
		a.bannerWide = 0;
	}
	const std::string emblem = EmblemArt( Str( card, "emblem" ) );
	a.emblem = emblem.empty() ? "" : emblem.substr( 1 );
	a.accent = Accent( Str( card, "accent" ) );
	a.frameColor = FrameColor( Str( card, "frame" ), a.accent );
	return a;
}

cJSON *CardView( const cJSON *card, int level, const cJSON *statValues, const char *ident ) {
	const std::string accent = Accent( Str( card, "accent" ) );
	const BannerCtx ctx = CtxOf( Profile() );
	const auto frame = Frame( Str( card, "frame" ), accent );
	const auto banner = Banner( Str( card, "banner" ), ident && !strcmp( ident, "LOCAL" ) ? &ctx : nullptr );
	const std::string emblem = EmblemArt( Str( card, "emblem" ) );
	cJSON *o = cJSON_CreateObject();
	cJSON_AddNumberToObject( o, "level", level );
	Add( o, "accent", accent );
	Add( o, "title_color", Lighten( accent, 0.3 ) );
	Add( o, "title", Str( Part( "titles", Str( card, "title" ) ), "name" ) );
	Add( o, "frame", frame.first );
	Add( o, "pad", frame.second );
	Add( o, "banner_bg", banner.first );
	Add( o, "banner_fg", banner.second );
	Add( o, "emblem", emblem.empty() ? "/models/players/sarge/icon_default" : emblem );
	Add( o, "trim", F( "linear-gradient(90deg, %s 0%%, %s55 60%%, %s00 100%%)", accent.c_str(), accent.c_str(), accent.c_str() ) );
	Add( o, "glow", accent + "66" );
	cJSON *stats = cJSON_AddArrayToObject( o, "stats" ), *badges = cJSON_AddArrayToObject( o, "badges" );
	for ( int i = 0; i < SHOWCASE; i++ ) {
		const cJSON *sid = cJSON_GetArrayItem( Get( card, "stats" ), i );
		std::string label;
		const cJSON *e;
		cJSON_ArrayForEach( e, Get( Catalog(), "card_stats" ) ) {
			if ( cJSON_IsString( sid ) && Str( e, "id" ) == sid->valuestring ) {
				label = Str( e, "label" );
			}
		}
		cJSON *s = cJSON_CreateObject();
		Add( s, "v", cJSON_IsString( sid ) ? Str( statValues, sid->valuestring, "-" ) : "" );
		Add( s, "l", label );
		cJSON_AddItemToArray( stats, s );
	}
	for ( int i = 0; i < SHOWCASE; i++ ) {
		const cJSON *bid = cJSON_GetArrayItem( Get( card, "badges" ), i );
		const cJSON *a = cJSON_IsString( bid ) ? Achievement( bid->valuestring ) : nullptr;
		const std::string icon = a ? AchIcon( a ) : "";
		cJSON *b = cJSON_CreateObject();
		cJSON_AddBoolToObject( b, "on", a != nullptr );
		Add( b, "icon", icon.empty() ? FALLBACK_ICON : icon );
		Add( b, "color", a ? MedalColor( Str( a, "medal" ) ) : "#ffffff14" );
		cJSON_AddItemToArray( badges, b );
	}
	Add( o, "id", ident ? ident : "" );
	Add( o, "xp_pct", "0%" );
	Add( o, "xp_text", "" );
	Add( o, "xp_bar", F( "linear-gradient(90deg, %saa 0%%, %s 100%%)", accent.c_str(), accent.c_str() ) );
	cJSON_AddBoolToObject( o, "ranked", false );
	Add( o, "rank", "" );
	Add( o, "rank_color", "#8d847c" );
	Add( o, "mmr", "" );
	cJSON_AddArrayToObject( o, "pips" );
	Add( o, "rank_deco", "linear-gradient(165deg, #2a2320 0%, #1a1614 100%)" );
	Add( o, "rank_mark", "#8d847c" );
	return o;
}

// ---------------------------------------------------------------- the Profile page

// the grids' images load a few a frame after the page shows ("/@tile/", oz_ui_render.cpp): the banner grid's
// 98 took 0.24 s in Profile's first frame. A tile shows a stand-in of no fixed size, so a banner's art goes
// in as fill: the cards pak's banners are 4:1, as their box, so it draws what cover did.
static std::string GridBanner( const std::string &deco ) {
	static const std::string from = "image(/ui/cards/banners/", cover = " cover)";
	if ( deco.compare( 0, from.size(), from ) || deco.size() < from.size() + cover.size()
		|| deco.compare( deco.size() - cover.size(), cover.size(), cover ) ) {
		return deco;
	}
	return "image(/@tile" + deco.substr( 6, deco.size() - 6 - cover.size() ) + " fill)";
}

static cJSON *Grid( const char *slot, const char *kind, const cJSON *profile, const std::set<std::string> &done ) {
	std::vector<const cJSON *> parts;
	const cJSON *p;
	cJSON_ArrayForEach( p, Parts( kind ) ) {
		parts.push_back( p );
	}
	auto open = [&]( const cJSON *x ) { return IsUnlocked( Get( x, "unlock" ), profile, &done ); };
	const std::string k = kind;
	if ( k == "titles" || k == "accents" || k == "rails" ) {
		std::stable_sort( parts.begin(), parts.end(), [&]( const cJSON *a, const cJSON *b ) { return open( a ) && !open( b ); } );
	}
	cJSON *groups = cJSON_CreateArray();
	for ( const cJSON *x : parts ) {
		const std::string id = Str( x, "id" ), style = Str( x, "style" ), pk = Str( x, "kind" ), group = Str( x, "group" );
		const cJSON *rule = Get( x, "unlock" );
		std::string g;
		if ( k == "banners" ) {
			g = pk == "stat" ? "Career" : pk == "prestige" ? "Prestige" : !id.compare( 0, 4, "map_" ) ? "Arenas" : !id.compare( 0, 4, "sky_" ) ? "Skies"
				: !id.compare( 0, 5, "char_" ) ? "Characters" : !id.compare( 0, 4, "mat_" ) ? "Materials" : "Default";
		} else if ( k == "emblems" ) {
			g = group == "heads" ? "Characters" : group == "skins" ? "Skins" : group == "weapons" ? "Weapons" : group == "items" ? "Items"
				: group == "medals" ? "Medals" : "Other";
		} else if ( k == "frames" ) {
			g = style == "plain" ? "Default" : style == "material" ? "Level track" : style == "shell" ? "Powerups" : style == "campaign" ? "Campaign"
				: style == "ranked" ? "Ranked" : "Other";
		} else if ( k == "taunts" ) {
			g = "Characters";
		} else {
			g = cJSON_IsTrue( Get( rule, "free" ) ) ? "Free" : Get( rule, "level" ) ? "Level track" : "Earned";
		}
		cJSON *hit = nullptr, *e;
		cJSON_ArrayForEach( e, groups ) {
			if ( Str( e, "name" ) == g ) {
				hit = e;
			}
		}
		const bool isOpen = open( x );
		std::string how = cJSON_IsTrue( Get( rule, "free" ) ) ? "" : UnlockText( x );
		std::string art;
		cJSON *item = cJSON_CreateObject();
		Add( item, "id", id );
		Add( item, "name", Str( x, "name" ) );
		cJSON_AddBoolToObject( item, "open", isOpen );
		Add( item, "level", Get( rule, "level" ) && !isOpen ? F( "L%d", (int)Num( Get( rule, "level" ) ) ) : "" );
		Add( item, "info", isOpen ? Str( x, "desc", "Unlocked" ) : how );
		Add( item, "toast", Str( x, "name" ) + ": " + Sentence( how ) );
		if ( k == "banners" ) {
			const auto b = Banner( id, nullptr );
			Add( item, "bg", GridBanner( b.first ) );
			Add( item, "fg", b.second );
		} else if ( k == "emblems" || k == "taunts" ) {
			art = EmblemArt( k == "taunts" ? "head_" + id.substr( 6 ) : id );
			Add( item, "art", art.empty() ? art : "/@tile" + art );	// an img sized by its box
		} else if ( k == "frames" ) {
			const auto f = Frame( id, "#db2410" );
			Add( item, "deco", f.first );
			Add( item, "pad", f.second );
		} else if ( k == "titles" ) {
			Add( item, "sub", cJSON_IsTrue( Get( rule, "free" ) ) ? "Free" : Str( x, "desc", Sentence( how ).c_str() ) );
		} else if ( k == "accents" ) {
			Add( item, "value", Str( x, "value" ) );
		} else if ( k == "rails" ) {
			const auto r = RailDeco( Str( x, "value", "#0050ff" ) );
			Add( item, "beam", r.first );
			Add( item, "spiral", F( "repeating-linear-gradient(110deg, #00000000 0dp, #00000000 6dp, %saa 6dp, %saa 8dp)", r.second.c_str(), r.second.c_str() ) );
		}
		if ( ( k == "emblems" || k == "taunts" ) && art.empty() ) {
			cJSON_Delete( item );	// nothing in the player's paks to draw
			continue;
		}
		if ( !hit ) {
			hit = cJSON_CreateObject();
			Add( hit, "name", g );
			cJSON_AddNumberToObject( hit, "open", 0 );
			cJSON_AddNumberToObject( hit, "total", 0 );
			cJSON_AddArrayToObject( hit, "items" );
			cJSON_AddItemToArray( groups, hit );
		}
		cJSON_AddItemToArray( Get( hit, "items" ), item );
		cJSON_SetNumberValue( Get( hit, "total" ), Num( Get( hit, "total" ) ) + 1 );
		cJSON_SetNumberValue( Get( hit, "open" ), Num( Get( hit, "open" ) ) + isOpen );
	}
	(void)slot;
	return groups;
}

// ---------------------------------------------------------------- the Character tab: skin and player color

// the model cvar's character and skin ("sarge/red"; none is the default), as the cgame reads it
std::pair<std::string, std::string> PlayerModel() {
	const std::string m = OzE_CvarString( "model" );
	const size_t slash = m.find( '/' );
	return { m.substr( 0, slash ), slash == std::string::npos || slash + 1 == m.size() ? "default" : m.substr( slash + 1 ) };
}

// a character's skins as the original game's model menu finds them (ui_playermodel.c): an icon_<skin>
// beside its models. The default first, then the team skins, then its own.
std::vector<std::string> SkinsOf( const std::string &model ) {
	std::vector<std::string> found, out;
	int n = 0;
	char **files = model.empty() ? nullptr : OzE_ListFiles( ( "models/players/" + model ).c_str(), ".tga", &n );
	for ( int i = 0; i < n; i++ ) {
		std::string f = files[i];
		for ( char &c : f ) {
			c = (char)tolower( (unsigned char)c );
		}
		if ( f.size() > 9 && !f.compare( 0, 5, "icon_" ) && std::find( found.begin(), found.end(), f.substr( 5, f.size() - 9 ) ) == found.end() ) {
			found.push_back( f.substr( 5, f.size() - 9 ) );
		}
	}
	OzE_FreeFileList( files );
	for ( const char *s : { "default", "red", "blue" } ) {
		if ( std::find( found.begin(), found.end(), s ) != found.end() ) {
			out.push_back( s );
		}
	}
	for ( const std::string &s : found ) {
		if ( s != "default" && s != "red" && s != "blue" ) {
			out.push_back( s );
		}
	}
	return out;
}

static cJSON *SkinList( const std::string &model ) {
	cJSON *out = cJSON_CreateArray();
	for ( const std::string &s : SkinsOf( model ) ) {
		std::string name = s == "red" || s == "blue" ? s + " team" : s;
		name[0] = (char)toupper( (unsigned char)name[0] );
		cJSON *e = cJSON_CreateObject();
		Add( e, "id", s );
		Add( e, "name", name );
		Add( e, "icon", "/models/players/" + model + "/icon_" + s );
		cJSON_AddItemToArray( out, e );
	}
	return out;
}

// the player's color (profile/team_color) where the menus read it: the showcase's rim light
static void AccentFromCvar() {
	const char *accent = OzE_CvarString( "oz_profile_team_color" );
	if ( accent[0] == '#' && strcmp( accent, Menu_Get( "player.accent" ) ) ) {
		Menu_Set( "player.accent", accent );
	}
}

void FillCharacter() {
	const std::pair<std::string, std::string> pm = PlayerModel();
	if ( Menu_Node( "profile.skins" ) ) {
		ReplaceNode( "profile.skins", SkinList( pm.first ) );
	}
	Menu_Set( "profile.skin", pm.second.c_str() );
	AccentFromCvar();
}

// the character and skin the player wears: the model and headmodel cvars, as the original game's model
// menu sets them, and profile/skin (oz_profile_skin) with them
void SetModel( const std::string &model, const std::string &skin ) {
	const std::string v = skin == "default" ? model : model + "/" + skin;
	OzE_CvarSet( "model", v.c_str() );
	OzE_CvarSet( "headmodel", v.c_str() );
	Settings_Set( "profile/skin", skin.c_str() );
	Menu_Set( "player.model", model.c_str() );
	Menu_Set( "player.icon", ( "/models/players/" + model + "/icon_" + skin ).c_str() );
	FillCharacter();
}

// q3_profile.gd rail_color1: the rail as the userinfo's color1, the nearest of the original game's
// seven rail colors (4 red, 2 green, 1 blue); white under the team and rainbow rails. The white free
// rail with a dark player color (which SetAccent gives it) is that color as "#rrggbb": Oz's cgame
// draws a dark rail as a dark beam in a white glow (cg_oz_fx.c CG_OzRailTrail); id's takes it as white.
void RailColor1( const std::string &rail ) {
	static const char *const seven[][2] = { { "rail_blue", "1" }, { "rail_green", "2" }, { "rail_cyan", "3" }, { "rail_red", "4" },
		{ "rail_magenta", "5" }, { "rail_yellow", "6" }, { "rail_white", "7" } };
	std::string n;
	for ( auto &s : seven ) {
		if ( rail == s[0] ) {
			n = s[1];
		}
	}
	const std::string v = Str( Part( "rails", rail ), "value" );
	if ( n.empty() && v.size() == 7 && v[0] == '#' ) {
		const unsigned c = (unsigned)strtoul( v.c_str() + 1, nullptr, 16 );
		const int k = ( ( c >> 16 & 255 ) > 127 ? 4 : 0 ) | ( ( c >> 8 & 255 ) > 127 ? 2 : 0 ) | ( ( c & 255 ) > 127 ? 1 : 0 );
		n = std::to_string( k ? k : 7 );
	}
	if ( rail == "rail_white" ) {
		const char *accent = OzE_CvarString( "oz_profile_team_color" );
		const unsigned c = accent && accent[0] == '#' && strlen( accent ) == 7 ? (unsigned)strtoul( accent + 1, nullptr, 16 ) : 0xffffff;
		if ( ( c >> 16 & 255 ) < 90 && ( c >> 8 & 255 ) < 90 && ( c & 255 ) < 90 ) {	// cg_oz_fx.c OZ_DARK
			n = accent;
		}
	}
	OzE_CvarSet( "color1", n.empty() ? "7" : n.c_str() );
}

/*
The player's color (profile/team_color, the Character tab): the showcase's rim light, and the rail
trail. A card wearing one of the seven free rails takes the one nearest the color (rail_color1's rule),
which goes in color1; an earned rail on the card stays, as the player picked it on purpose.
*/
void SetAccent( const std::string &hex ) {
	Settings_Set( "profile/team_color", hex.c_str() );
	Menu_Set( "player.accent", hex.c_str() );
	static const char *const freeRails[8] = { "rail_white", "rail_blue", "rail_green", "rail_cyan", "rail_red", "rail_magenta", "rail_yellow", "rail_white" };
	const std::string now = Str( Get( Profile(), "card" ), "rail" );
	if ( std::find( std::begin( freeRails ), std::end( freeRails ), now ) == std::end( freeRails ) && Part( "rails", now ) ) {
		return;
	}
	const unsigned c = (unsigned)strtoul( hex.c_str() + 1, nullptr, 16 );
	const char *rail = freeRails[( ( c >> 16 & 255 ) > 127 ? 4 : 0 ) | ( ( c >> 8 & 255 ) > 127 ? 2 : 0 ) | ( ( c & 255 ) > 127 ? 1 : 0 )];
	if ( now == rail || SetCard( "rail", rail ) ) {
		RailColor1( rail );
	}
}

void Profile_Fill() {
	cJSON *old = Menu_Node( "profile" );
	if ( !old || filling ) {
		return;
	}
	filling = true;
	cJSON *d = Profile(), *stats = Get( d, "stats" );
	const std::set<std::string> done = Achieved( d );
	const LevelInfo lv = LevelFor( TotalXp() );
	const std::map<std::string, std::string> mapNames = MapNames();
	cJSON *card = Get( d, "card" ), *shown = ShownCard();
	cJSON *o = cJSON_CreateObject();

	Add( o, "_note", "The Profile page's data, filled by oz_ui_profile.cpp from the career (P12)." );
	Add( o, "tab", "card" );
	Add( o, "slot", "banner" );
	cJSON_AddBoolToObject( o, "locked", true );
	cJSON_AddBoolToObject( o, "hover_on", false );
	Add( o, "hover_name", "" );
	Add( o, "hover_info", "" );
	cJSON_AddBoolToObject( o, "hover_locked", false );
	cJSON_AddBoolToObject( o, "name_dirty", false );
	Keep( o, old, { "tab", "slot", "locked", "hover_on", "hover_name", "hover_info", "hover_locked", "name_dirty" } );
	// the Character tab: the character's skins and the one worn, the player's color
	const std::pair<std::string, std::string> pm = PlayerModel();
	cJSON_AddItemToObject( o, "skins", SkinList( pm.first ) );
	Add( o, "skin", pm.second );
	AccentFromCvar();
	cJSON_AddNumberToObject( o, "level", lv.level );
	// the Matches tab: the hub's last answers (MatchHistory, SharedCareer)
	cJSON_AddItemToObject( o, "matches", matchRows ? cJSON_Duplicate( matchRows, 1 ) : cJSON_CreateArray() );
	cJSON_AddItemToObject( o, "shared", SharedView( sharedCareer ) );
	Add( o, "matches_note", MatchesNote() );

	// the achievements
	cJSON *achList = cJSON_CreateArray();
	std::map<std::string, std::pair<int, int>> medalTotals;
	std::set<std::string> groupsSeen;
	const cJSON *a;
	cJSON_ArrayForEach( a, Achievements() ) {
		const std::string id = Str( a, "id" ), medal = Str( a, "medal" );
		const bool isDone = done.count( id ) > 0;
		const bool secret = cJSON_IsTrue( Get( a, "hidden" ) ) && !isDone;
		const double prog = ProgressOf( a, d ), target = std::max( 1.0, Num( Get( a, "target" ), 1 ) );
		std::string pays;
		for ( auto &s : SLOTS ) {
			const cJSON *p;
			cJSON_ArrayForEach( p, Parts( s[1] ) ) {
				if ( Str( Get( p, "unlock" ), "achievement" ) == id ) {
					std::string kind = s[1];
					pays += ( pays.empty() ? "" : ", " ) + Str( p, "name" ) + " " + kind.substr( 0, kind.size() - 1 );
				}
			}
		}
		std::string missing;
		const std::vector<std::string> ms = Missing( a, d );
		if ( !isDone && !secret && !ms.empty() && ms.size() <= 8 ) {
			for ( const std::string &m : ms ) {
				auto n = mapNames.find( m );
				missing += ( missing.empty() ? "Still to go: " : ", " ) + ( n == mapNames.end() ? m : n->second );
			}
		}
		const std::string icon = AchIcon( a ), group = AchGroup( id );
		groupsSeen.insert( group );
		medalTotals[medal].first += isDone;
		medalTotals[medal].second++;
		cJSON *e = cJSON_CreateObject();
		Add( e, "id", id );
		Add( e, "group", group );
		Add( e, "state", isDone ? "Earned" : prog > 0 ? "In progress" : "Not started" );
		cJSON_AddBoolToObject( e, "done", isDone );
		cJSON_AddBoolToObject( e, "secret", secret );
		Add( e, "name", secret ? "Hidden achievement" : Str( a, "name" ) );
		Add( e, "desc", secret ? "Keep playing to find out." : Str( a, "desc" ) );
		Add( e, "medal", medal );
		Add( e, "color", MedalColor( medal ) );
		Add( e, "icon", icon.empty() ? FALLBACK_ICON : icon );
		cJSON_AddBoolToObject( e, "has_icon", !icon.empty() && !secret );
		cJSON_AddBoolToObject( e, "bar", !isDone && !secret && target > 1 );
		Add( e, "pct", Pct( prog / target ) );
		Add( e, "progress", Thousands( (long long)std::min( prog, target ) ) + " / " + Thousands( (long long)target ) );
		Add( e, "missing", missing );
		Add( e, "pays", !pays.empty() && !secret ? "Pays: " + pays : "" );
		cJSON_AddItemToArray( achList, e );
	}
	const int achTotal = cJSON_GetArraySize( achList );
	cJSON_AddNumberToObject( o, "ach_done", (double)done.size() );
	cJSON_AddNumberToObject( o, "ach_total", achTotal );

	// the card: the parts in each slot, then the card others see
	cJSON *cur = cJSON_AddObjectToObject( o, "cur" );
	for ( auto &s : SLOTS ) {
		Add( cur, s[0], Str( card, s[0] ) );
	}
	cJSON *values = cJSON_CreateObject();
	const cJSON *cs;
	cJSON_ArrayForEach( cs, Get( Catalog(), "card_stats" ) ) {
		Add( values, Str( cs, "id" ).c_str(), StatValue( Str( cs, "id" ), d ) );
	}
	cJSON *view = CardView( shown, lv.level, values, "LOCAL" );
	cJSON_ReplaceItemInObjectCaseSensitive( view, "xp_pct", cJSON_CreateString( Pct( (double)lv.into / std::max( 1LL, lv.next ) ).c_str() ) );
	cJSON_ReplaceItemInObjectCaseSensitive( view, "xp_text", cJSON_CreateString( ( Thousands( lv.into ) + " / " + Thousands( lv.next ) + " XP" ).c_str() ) );
	cJSON *ranked = Ranked();
	if ( cJSON_IsTrue( Get( ranked, "placed" ) ) ) {
		const cJSON *r = Get( ranked, "rank" );
		auto t = TIER.find( Str( r, "id" ) );
		const std::string col = t == TIER.end() ? "#8d847c" : t->second;
		std::string label = Str( r, "label" );
		for ( char &c : label ) {
			c = (char)toupper( (unsigned char)c );
		}
		cJSON_ReplaceItemInObjectCaseSensitive( view, "ranked", cJSON_CreateTrue() );
		cJSON_ReplaceItemInObjectCaseSensitive( view, "rank", cJSON_CreateString( label.c_str() ) );
		cJSON_ReplaceItemInObjectCaseSensitive( view, "rank_color", cJSON_CreateString( col.c_str() ) );
		cJSON_ReplaceItemInObjectCaseSensitive( view, "mmr", cJSON_CreateString( ( Thousands( (long long)Num( Get( ranked, "rating" ) ) ) + " MMR" ).c_str() ) );
		cJSON *pips = Get( view, "pips" );
		const int div = (int)Num( Get( r, "division" ) );
		for ( int i = 0; div && i < 4 - div; i++ ) {
			cJSON *pip = cJSON_CreateObject();
			Add( pip, "c", col );
			cJSON_AddItemToArray( pips, pip );
		}
		cJSON_ReplaceItemInObjectCaseSensitive( view, "rank_deco", cJSON_CreateString( F( "linear-gradient(165deg, %s 0%%, %s 45%%, %s 100%%)",
			Lighten( col, 0.45 ).c_str(), col.c_str(), Darken( col, 0.55 ).c_str() ).c_str() ) );
		cJSON_ReplaceItemInObjectCaseSensitive( view, "rank_mark", cJSON_CreateString( Lighten( col, 0.65 ).c_str() ) );
	}
	cJSON_Delete( ranked );
	cJSON_AddItemToObject( o, "card", view );

	static const char *const tips[][3] = {
		{ "banner", "Banner", "Wide art across the top of your card. Arena banners come from winning there, characters from beating them in the campaign." },
		{ "emblem", "Emblem", "The portrait on your card. Every default head is free; skins, weapons, items and medals are earned." },
		{ "frame", "Frame", "The border around your card. The level track pays one every 10 levels; powerup frames pulse." },
		{ "title", "Title", "The line under your name." },
		{ "accent", "Accent", "Your card's trim, glow and XP bar. The original game's seven colors are free." },
		{ "rail", "Rail", "Your rail trail's color in matches. Forced rail colors and competitive settings override it for whoever turns them on." },
		{ "taunt", "Taunt", "The voice line when your card appears on the duel versus screen. Selecting one plays it." },
		{ "stats", "Stats", "Three career stats shown on your card, in the order you pick them." },
		{ "badges", "Badges", "Up to three achievement medals shown on your card." } };
	cJSON *grid = cJSON_AddObjectToObject( o, "grid" ), *slots = cJSON_AddArrayToObject( o, "slots" );
	for ( auto &t : tips ) {
		cJSON *s = cJSON_CreateObject();
		Add( s, "id", t[0] );
		Add( s, "label", t[1] );
		const char *kind = nullptr;
		for ( auto &sl : SLOTS ) {
			if ( !strcmp( sl[0], t[0] ) ) {
				kind = sl[1];
			}
		}
		if ( kind ) {
			cJSON *g = Grid( t[0], kind, d, done );
			int on = 0, total = 0;
			const cJSON *e;
			cJSON_ArrayForEach( e, g ) {
				on += (int)Num( Get( e, "open" ) );
				total += (int)Num( Get( e, "total" ) );
			}
			cJSON_AddItemToObject( grid, t[0], g );
			Add( s, "count", F( "%d/%d", on, total ) );
		} else {
			Add( s, "count", F( "%d/3", cJSON_GetArraySize( Get( card, t[0] ) ) ) );
		}
		Add( s, "tip", t[2] );
		cJSON_AddItemToArray( slots, s );
	}

	// the showcase's choices
	cJSON *statsPick = cJSON_AddArrayToObject( o, "stats" );
	cJSON_ArrayForEach( cs, Get( Catalog(), "card_stats" ) ) {
		int slot = 0, i = 0;
		const cJSON *x;
		cJSON_ArrayForEach( x, Get( card, "stats" ) ) {
			i++;
			if ( cJSON_IsString( x ) && Str( cs, "id" ) == x->valuestring ) {
				slot = i;
			}
		}
		cJSON *e = cJSON_CreateObject();
		Add( e, "id", Str( cs, "id" ) );
		Add( e, "label", Str( cs, "label" ) );
		Add( e, "value", Str( values, Str( cs, "id" ).c_str(), "-" ) );
		cJSON_AddNumberToObject( e, "slot", slot );
		Add( e, "info", slot ? F( "Slot %d on your card", slot ) : "Select to show it on your card" );
		cJSON_AddItemToArray( statsPick, e );
	}
	cJSON_Delete( values );
	cJSON *badgesPick = cJSON_AddArrayToObject( o, "badges" );
	const cJSON *e;
	cJSON_ArrayForEach( e, achList ) {
		if ( cJSON_IsTrue( Get( e, "secret" ) ) ) {
			continue;
		}
		int slot = 0, i = 0;
		const cJSON *x;
		cJSON_ArrayForEach( x, Get( card, "badges" ) ) {
			i++;
			if ( cJSON_IsString( x ) && Str( e, "id" ) == x->valuestring ) {
				slot = i;
			}
		}
		cJSON *b = cJSON_CreateObject();
		Add( b, "id", Str( e, "id" ) );
		Add( b, "name", Str( e, "name" ) );
		cJSON_AddBoolToObject( b, "open", cJSON_IsTrue( Get( e, "done" ) ) );
		cJSON_AddNumberToObject( b, "slot", slot );
		Add( b, "icon", Str( e, "icon" ) );
		Add( b, "color", Str( e, "color" ) );
		Add( b, "info", slot ? F( "Badge %d on your card", slot ) : Str( e, "desc" ) );
		Add( b, "toast", Str( e, "name" ) + ": earn this achievement to wear its medal." );
		cJSON_AddItemToArray( badgesPick, b );
	}

	cJSON *colors = cJSON_AddArrayToObject( o, "colors" );
	for ( const char *c : { "#db2410", "#ff6a00", "#ffcc00", "#7dff3a", "#22e0c8", "#2f8cff", "#5a3bff", "#d23cff", "#ff4f9a", "#ffffff", "#a0a0a0", "#000000" } ) {
		cJSON_AddItemToArray( colors, cJSON_CreateString( c ) );
	}
	cJSON *codes = cJSON_AddArrayToObject( o, "codes" );
	static const char *const codeColors[] = { "#5a5a5a", "#ff0000", "#00ff00", "#ffff00", "#4a6bff", "#00ffff", "#ff00ff", "#ffffff" };
	for ( int i = 0; i < 8; i++ ) {
		cJSON *c = cJSON_CreateObject();
		Add( c, "code", F( "^%d", i ) );
		Add( c, "color", codeColors[i] );
		cJSON_AddItemToArray( codes, c );
	}

	cJSON *ach = cJSON_AddObjectToObject( o, "ach" );
	Add( ach, "group", "All" );
	Add( ach, "show", "All" );
	Keep( ach, Get( old, "ach" ), { "group", "show" } );
	cJSON *groups = cJSON_AddArrayToObject( ach, "groups" );
	cJSON_AddItemToArray( groups, cJSON_CreateString( "All" ) );
	for ( const char *g : ACH_GROUPS ) {
		if ( groupsSeen.count( g ) ) {
			cJSON_AddItemToArray( groups, cJSON_CreateString( g ) );
		}
	}
	cJSON_AddItemToObject( ach, "list", achList );
	cJSON_AddNumberToObject( ach, "done", (double)done.size() );
	cJSON_AddNumberToObject( ach, "total", achTotal );
	Add( ach, "pct", Pct( (double)done.size() / std::max( 1, achTotal ) ) );
	cJSON *medals = cJSON_AddArrayToObject( ach, "medals" );
	for ( const char *m : { "bronze", "silver", "gold" } ) {
		cJSON *x = cJSON_CreateObject();
		Add( x, "name", m );
		cJSON_AddNumberToObject( x, "n", medalTotals[m].first );
		cJSON_AddNumberToObject( x, "of", medalTotals[m].second );
		Add( x, "color", MedalColor( m ) );
		cJSON_AddItemToArray( medals, x );
	}

	// the career
	cJSON *c = cJSON_AddObjectToObject( o, "career" );
	const long long matches = (long long)Stat( stats, "matches" ), wins = (long long)Stat( stats, "wins" );
	const long long frags = (long long)Stat( stats, "frags" ), deaths = (long long)Stat( stats, "deaths" );
	const double playtime = Stat( stats, "playtime" ), hours = playtime / 3600;
	const int maxLevel = MaxLevel();
	cJSON_AddNumberToObject( c, "level", lv.level );
	cJSON_AddNumberToObject( c, "max", maxLevel );
	Add( c, "total", Thousands( TotalXp() ) );
	Add( c, "xp", Thousands( lv.into ) + " / " + Thousands( lv.next ) );
	Add( c, "to_next", Thousands( lv.next - lv.into ) + F( " to level %d", std::min( maxLevel, lv.level + 1 ) ) );
	Add( c, "pct", Pct( (double)lv.into / std::max( 1LL, lv.next ) ) );
	struct Next { long long level; int kind; const char *k; const cJSON *p; };
	std::vector<Next> nxt;
	int ki = 0;
	cJSON *kinds = Get( Catalog(), "parts" ), *kind;
	cJSON_ArrayForEach( kind, kinds ) {
		const cJSON *p;
		cJSON_ArrayForEach( p, kind ) {
			const long long l = (long long)Num( Get( Get( p, "unlock" ), "level" ) );
			if ( l > lv.level ) {
				nxt.push_back( { l, ki, kind->string, p } );
			}
		}
		ki++;
	}
	std::stable_sort( nxt.begin(), nxt.end(), []( const Next &a, const Next &b ) { return a.level != b.level ? a.level < b.level : a.kind < b.kind; } );
	cJSON *next = cJSON_AddArrayToObject( c, "next" );
	for ( size_t i = 0; i < nxt.size() && i < 6; i++ ) {
		std::string k = nxt[i].k, upper = k.substr( 0, k.size() - 1 );
		for ( char &ch : upper ) {
			ch = (char)toupper( (unsigned char)ch );
		}
		cJSON *x = cJSON_CreateObject();
		Add( x, "level", F( "L%lld", nxt[i].level ) );
		Add( x, "name", Str( nxt[i].p, "name" ) );
		Add( x, "kind", upper );
		std::string swatch = "text", deco = NONE, color = "#8d847c";
		const std::string id = Str( nxt[i].p, "id" );
		if ( k == "banners" ) {
			swatch = "banner";
			deco = Banner( id, nullptr ).first;
		} else if ( k == "frames" ) {
			swatch = "frame";
			deco = Frame( id, "#db2410" ).first;
		} else if ( k == "accents" ) {
			swatch = "dot";
			color = Str( nxt[i].p, "value" );
		} else if ( k == "rails" ) {
			swatch = "rail";
			deco = RailDeco( Str( nxt[i].p, "value" ) ).first;
		}
		Add( x, "swatch", swatch );
		Add( x, "deco", deco );
		Add( x, "color", color );
		cJSON_AddItemToArray( next, x );
	}
	std::string fav = "-";
	{
		const std::string m = Best( Get( stats, "matches_model" ) );
		if ( !m.empty() ) {
			fav = F( "%s (%d)", m.c_str(), (int)Stat( stats, "matches_model." + m ) );
		}
	}
	cJSON *rows = cJSON_AddArrayToObject( c, "rows" );
	const std::pair<const char *, std::string> kv[] = {
		{ "Matches", Thousands( matches ) }, { "Wins", Thousands( wins ) },
		{ "Win rate", matches ? F( "%d%%", (int)std::lround( (double)wins / matches * 100 ) ) : "-" },
		{ "Frags", Thousands( frags ) }, { "Deaths", Thousands( deaths ) }, { "K/D", F( "%.2f", (double)frags / std::max( 1LL, deaths ) ) },
		{ "Time played", hours >= 1 ? F( "%.1f h", hours ) : F( "%d min", (int)std::lround( playtime / 60 ) ) }, { "Favorite character", fav } };
	for ( auto &r : kv ) {
		cJSON *x = cJSON_CreateObject();
		Add( x, "k", r.first );
		Add( x, "v", r.second );
		cJSON_AddItemToArray( rows, x );
	}
	static const char *const weapons[] = { "gauntlet", "machinegun", "shotgun", "grenade", "rocket", "lightning", "railgun", "plasma", "bfg" };
	double top = 1;
	for ( const char *w : weapons ) {
		top = std::max( top, Stat( stats, std::string( "frags_weapon." ) + w ) );
	}
	cJSON *weps = cJSON_AddArrayToObject( c, "weapons" );
	for ( const char *w : weapons ) {
		const double n = Stat( stats, std::string( "frags_weapon." ) + w );
		std::string name = w;
		name[0] = (char)toupper( (unsigned char)name[0] );
		cJSON *x = cJSON_CreateObject();
		Add( x, "name", !strcmp( w, "bfg" ) ? "BFG" : name );
		Add( x, "icon", std::string( "/icons/iconw_" ) + w );
		Add( x, "n", Thousands( (long long)n ) );
		Add( x, "pct", Pct( n / top ) );
		cJSON_AddItemToArray( weps, x );
	}
	cJSON *meds = cJSON_AddArrayToObject( c, "medals" );
	for ( const char *m : { "excellent", "impressive", "gauntlet", "capture", "assist", "defend", "perfect" } ) {
		const double n = Stat( stats, std::string( "medal." ) + m );
		std::string label = !strcmp( m, "gauntlet" ) ? "HUMILIATION" : m;
		for ( char &ch : label ) {
			ch = (char)toupper( (unsigned char)ch );
		}
		cJSON *x = cJSON_CreateObject();
		Add( x, "label", label );
		Add( x, "icon", std::string( "/menu/medals/medal_" ) + ( !strcmp( m, "perfect" ) ? "victory" : m ) );
		Add( x, "n", Thousands( (long long)n ) );
		cJSON_AddBoolToObject( x, "zero", n == 0 );
		cJSON_AddItemToArray( meds, x );
	}
	cJSON *modes = cJSON_AddArrayToObject( c, "modes" );
	static const char *const modeNames[][2] = { { "ffa", "Free For All" }, { "duel", "Tournament" }, { "tdm", "Team DM" }, { "ctf", "CTF" },
		{ "ca", "Clan Arena" }, { "ft", "Freeze Tag" }, { "instagib", "Instagib" } };
	for ( auto &m : modeNames ) {
		cJSON *x = cJSON_CreateObject();
		Add( x, "name", m[1] );
		Add( x, "n", Thousands( (long long)Stat( stats, std::string( "wins_mode." ) + m[0] ) ) );
		cJSON_AddItemToArray( modes, x );
	}

	cJSON_Delete( shown );
	ReplaceNode( "profile", o );
	filling = false;
}

// ---------------------------------------------------------------- the Matches tab (the stats recorder, P8b)

namespace {

const char *ModeLabel( const std::string &mode ) {
	static const char *const names[][2] = { { "ffa", "Free For All" }, { "duel", "Tournament" }, { "tdm", "Team DM" }, { "ctf", "CTF" },
		{ "single", "Single Player" }, { "ca", "Clan Arena" }, { "ft", "Freeze Tag" } };
	for ( auto &n : names ) {
		if ( mode == n[0] ) {
			return n[1];
		}
	}
	return mode.c_str()[0] ? "Other mode" : "Match";
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

std::string Ordinal( int n ) {
	const char *suffix = n % 100 >= 11 && n % 100 <= 13 ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
	return F( "%d%s", n, suffix );
}

std::string MatchesNote() {
	return matchRows ? "No matches yet. Play one to the end, on any server, and it shows here."
		: "Your finished matches show here when you're online. Matches played offline wait on this PC and go up then.";
}

// the online career (the hub's career, or match_history's) in the tab's shape
cJSON *SharedView( const cJSON *c ) {
	const long long xp = (long long)Num( Get( c, "xp" ) ), matches = (long long)Num( Get( c, "matches" ) ), wins = (long long)Num( Get( c, "wins" ) );
	cJSON *o = cJSON_CreateObject();
	cJSON_AddNumberToObject( o, "level", c ? Num( Get( c, "level" ), 1 ) : 1 );
	Add( o, "xp", Thousands( xp ) );
	cJSON *rows = cJSON_AddArrayToObject( o, "rows" );
	const std::pair<const char *, std::string> kv[] = { { "Counted matches", Thousands( matches ) }, { "Wins", Thousands( wins ) },
		{ "Frags", Thousands( (long long)Num( Get( c, "frags" ) ) ) },
		{ "Win rate", matches ? F( "%d%%", (int)std::lround( (double)wins / matches * 100 ) ) : "-" } };
	for ( auto &r : kv ) {
		cJSON *x = cJSON_CreateObject();
		Add( x, "k", r.first );
		Add( x, "v", r.second );
		cJSON_AddItemToArray( rows, x );
	}
	cJSON *modes = cJSON_AddArrayToObject( o, "modes" );
	const cJSON *m;
	cJSON_ArrayForEach( m, Get( c, "modes" ) ) {
		cJSON *x = cJSON_CreateObject();
		Add( x, "name", ModeLabel( Str( m, "mode" ) ) + ( Str( m, "mod" ) != "baseq3" ? " (" + Str( m, "mod" ) + ")" : std::string() ) );
		Add( x, "n", Thousands( (long long)Num( Get( m, "matches" ) ) ) );
		cJSON_AddItemToArray( modes, x );
	}
	return o;
}

}

void MatchHistory( const cJSON *m ) {
	const std::map<std::string, std::string> names = MapNames();
	static const std::map<std::string, std::pair<const char *, const char *>> trust = {
		{ "corroborated", { "CORROBORATED", "#7fd36a" } }, { "verified", { "VERIFIED", "#4aa8ff" } }, { "disputed", { "DISPUTED", "#e0a030" } },
		{ "self", { "SELF-REPORTED", "#8d847c" } } };
	cJSON *rows = cJSON_CreateArray();
	const cJSON *x;
	cJSON_ArrayForEach( x, Get( m, "matches" ) ) {
		const time_t at = (time_t)( Num( Get( x, "at" ) ) / 1000 );
		char when[32] = "";
		const struct tm *t = localtime( &at );
		if ( t ) {
			strftime( when, sizeof( when ), "%b %d, %H:%M", t );
		}
		const std::string map = Str( x, "map" ), server = Str( x, "server" ), host = Plain( Str( x, "hostname" ) );
		auto tr = trust.find( Str( x, "trust" ) );
		if ( tr == trust.end() ) {
			tr = trust.find( "self" );
		}
		const bool counted = cJSON_IsTrue( Get( x, "counted" ) );
		cJSON *o = cJSON_CreateObject();
		Add( o, "when", when );
		Add( o, "map", names.count( map ) ? names.at( map ) : map );
		Add( o, "mode", ModeLabel( Str( x, "mode" ) ) );
		Add( o, "server", server == "local" ? "Your own game" : !server.compare( 0, 4, "ice:" ) ? "Peer-to-peer" : host.empty() ? server : host );
		Add( o, "place", F( "%s of %d", Ordinal( (int)Num( Get( x, "place" ) ) ).c_str(), (int)Num( Get( x, "players" ) ) ) );
		Add( o, "kd", F( "%d / %d", (int)Num( Get( x, "frags" ) ), (int)Num( Get( x, "deaths" ) ) ) );
		Add( o, "xp", counted ? F( "+%d online XP", (int)Num( Get( x, "shared_xp" ) ) ) : Num( Get( x, "xp" ) ) > 0 ? F( "+%d XP", (int)Num( Get( x, "xp" ) ) ) : "" );
		Add( o, "trust_id", Str( x, "trust", "self" ) );
		Add( o, "trust", tr->second.first );
		Add( o, "color", tr->second.second );
		cJSON_AddItemToArray( rows, o );
	}
	cJSON_Delete( matchRows );
	matchRows = rows;
	if ( cJSON_IsObject( Get( m, "career" ) ) ) {
		cJSON_Delete( sharedCareer );
		sharedCareer = cJSON_Duplicate( Get( m, "career" ), 1 );
	}
	if ( Menu_Node( "profile.matches" ) ) {
		ReplaceNode( "profile.matches", cJSON_Duplicate( rows, 1 ) );
		ReplaceNode( "profile.shared", SharedView( sharedCareer ) );
		Menu_Set( "profile.matches_note", MatchesNote().c_str() );
	}
}

void SharedCareer( const cJSON *m ) {
	cJSON_Delete( sharedCareer );
	sharedCareer = cJSON_Duplicate( m, 1 );
	if ( Menu_Node( "profile.shared" ) ) {
		ReplaceNode( "profile.shared", SharedView( sharedCareer ) );
	}
}

// ---------------------------------------------------------------- the Campaign page

void Campaign_Fill() {
	if ( !Menu_Node( "campaign" ) || !Menu_Node( "campaign_tiers" ) ) {
		return;
	}
	std::map<std::string, int> mapIndex;
	int i = 0;
	const cJSON *m;
	cJSON_ArrayForEach( m, Menu_Node( "maps" ) ) {
		mapIndex[Str( m, "map" )] = i++;
	}
	const std::vector<Tier> &tiers = Tiers();
	cJSON *list = cJSON_CreateArray();
	int won = 0, total = 0, current = -1;
	std::string options;
	for ( size_t t = 0; t < tiers.size(); t++ ) {
		cJSON *e = cJSON_CreateObject();
		Add( e, "name", tiers[t].name );
		cJSON_AddBoolToObject( e, "open", TierOpen( (int)t ) );
		cJSON_AddBoolToObject( e, "complete", TierComplete( (int)t ) );
		cJSON *arenas = cJSON_AddArrayToObject( e, "arenas" );
		for ( const Arena &a : tiers[t].arenas ) {
			cJSON *x = cJSON_CreateObject();
			Add( x, "map", a.map );
			auto mi = mapIndex.find( a.map );
			cJSON_AddNumberToObject( x, "mi", mi == mapIndex.end() ? 9999 : mi->second );
			cJSON_AddNumberToObject( x, "won", WonAt( a.map ) );
			cJSON_AddItemToArray( arenas, x );
			won += WonAt( a.map ) > 0;
			total++;
		}
		cJSON_AddItemToArray( list, e );
		if ( current < 0 && TierOpen( (int)t ) && !TierComplete( (int)t ) ) {
			current = (int)t;
		}
		options += ( t ? "|" : "" ) + std::to_string( t );
	}
	if ( current < 0 ) {
		current = tiers.empty() ? 0 : (int)tiers.size() - 1;
	}
	ReplaceNode( "campaign_tiers", list );

	cJSON *c = Menu_Node( "campaign" );
	auto set = [&]( const char *k, cJSON *v ) {
		cJSON_DeleteItemFromObjectCaseSensitive( c, k );
		cJSON_AddItemToObject( c, k, v );
	};
	set( "tier", cJSON_CreateNumber( current ) );
	set( "skill", cJSON_CreateNumber( CampaignSkill() ) );
	set( "won", cJSON_CreateNumber( won ) );
	set( "total", cJSON_CreateNumber( std::max( total, 1 ) ) );
	set( "tier_options", cJSON_CreateString( options.c_str() ) );
	cJSON *medals = cJSON_CreateArray();
	static const char *const awards[][3] = { { "accuracy", "Accuracy", "medal_accuracy" }, { "impressive", "Impressive", "medal_impressive" },
		{ "excellent", "Excellent", "medal_excellent" }, { "gauntlet", "Humiliation", "medal_gauntlet" }, { "frags", "Frags", "medal_frags" },
		{ "perfect", "Perfect", "medal_victory" } };
	for ( auto &aw : awards ) {
		cJSON *x = cJSON_CreateObject();
		Add( x, "id", aw[0] );
		Add( x, "label", aw[1] );
		Add( x, "img", std::string( "/menu/medals/" ) + aw[2] );
		cJSON_AddNumberToObject( x, "count", Num( Get( Get( Campaign(), "awards" ), aw[0] ) ) );
		cJSON_AddItemToArray( medals, x );
	}
	set( "medals", medals );
	Menu_Dirty( "campaign" );
}

void Changed() {
	// the top bar's player chip: the card's title, the level, the rank once placed
	cJSON *shown = ShownCard(), *ranked = Ranked();
	Menu_Set( "player.title", Str( Part( "titles", Str( shown, "title" ) ), "name" ).c_str() );
	Menu_Set( "player.level", std::to_string( LevelFor( TotalXp() ).level ).c_str() );
	Menu_Set( "player.rank", cJSON_IsTrue( Get( ranked, "placed" ) ) ? Str( Get( ranked, "rank" ), "label" ).c_str() : "Unranked" );
	cJSON_Delete( shown );
	cJSON_Delete( ranked );
	Profile_Fill();
	Campaign_Fill();
}

} // namespace Prog

// the menus' actions for these pages (Menu_Run); false: not one of them
bool Profile_Action( const Rml::String &verb, const Rml::String &arg ) {
	const size_t sp = arg.find( ' ' );
	const std::string a = arg.substr( 0, sp ), b = sp == Rml::String::npos ? "" : arg.substr( sp + 1 );
	if ( verb == "profileset" ) {
		if ( !Prog::SetCard( a, b ) ) {
			PlaySound( Sound::Buzz );
			return true;
		}
		PlaySound( Sound::Move );
		if ( a == "taunt" ) {
			const std::string sound = Prog::Str( Prog::Part( "taunts", b ), "sound" );
			if ( !sound.empty() ) {
				OzE_StartLocalSound( sound.c_str() );
			}
		} else if ( a == "rail" ) {
			Prog::RailColor1( b );
		}
		return true;
	}
	if ( verb == "profilematches" ) {	// the Matches tab opens: the history and online career from the hub
		OzE_ExecuteNow( "hub match_history" );
		return true;
	}
	if ( verb == "profilecharacter" ) {	// the Character tab opens: its skins and color as the cvars have them now
		Prog::FillCharacter();
		return true;
	}
	if ( verb == "profilemodel" || verb == "profileskin" ) {
		const std::pair<std::string, std::string> now = Prog::PlayerModel();
		const std::string model = verb == "profilemodel" ? a : now.first, skin = verb == "profileskin" ? a : now.second;
		const std::vector<std::string> skins = Prog::SkinsOf( model );
		if ( skins.empty() || ( verb == "profileskin" && std::find( skins.begin(), skins.end(), skin ) == skins.end() ) ) {
			PlaySound( Sound::Buzz );
			return true;
		}
		// another character keeps the skin when it has one of that name, as the Godot build's set_skin
		Prog::SetModel( model, std::find( skins.begin(), skins.end(), skin ) != skins.end() ? skin : "default" );
		PlaySound( Sound::Move );
		return true;
	}
	if ( verb == "profileaccent" ) {
		if ( a.size() != 7 || a[0] != '#' ) {
			PlaySound( Sound::Buzz );
			return true;
		}
		Prog::SetAccent( a );
		PlaySound( Sound::Move );
		return true;
	}
	if ( verb == "showcase" ) {
		const cJSON *card = Prog::Get( Prog::Profile(), "card" ), *x;
		std::vector<std::string> ids;
		bool had = false;
		cJSON_ArrayForEach( x, Prog::Get( card, a.c_str() ) ) {
			if ( cJSON_IsString( x ) ) {
				if ( b == x->valuestring ) {
					had = true;
				} else {
					ids.push_back( x->valuestring );
				}
			}
		}
		if ( !had ) {
			if ( (int)ids.size() >= Prog::SHOWCASE ) {
				PlaySound( Sound::Buzz );
				Menu_Toast( "Three at most: take one off first.", "warn" );
				return true;
			}
			ids.push_back( b );
		}
		PlaySound( Prog::SetShowcase( a, ids ) ? Sound::Move : Sound::Buzz );
		return true;
	}
	if ( verb == "campaign" ) {
		Prog::SetCampaignSkill( atoi( Menu_Get( "campaign.skill" ) ) );
		Prog::CampaignStart( a );
		return true;
	}
	if ( verb == "campaignreset" ) {
		Prog::ResetCampaign();
		PlaySound( Sound::Move );
		Menu_Toast( "The campaign starts over. Your career keeps its XP and unlocks.", "info" );
		return true;
	}
	return false;
}

} // namespace OzUI
