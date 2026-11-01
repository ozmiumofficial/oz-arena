/*
===========================================================================
Oz Arena: single player and progression (ENGINE_PLAN.md P12). GPL-2.0.

The Godot build's career, cards and campaign, ported line by line so the same match grants the same
XP and achievements (engine/tests/data/progress_vectors.json, made from that code):
  q3_cards.gd		the card catalog (ui/cards/catalog.json, the Godot build's library/cards/cards.json)
					and its rules: achievements computed from career stats, one unlock rule per part,
					levels, ranks, the showcase stats
  q3_profile.gd		the career: XP, stats, the card, award_match
  campaign.gd		the original game's campaign over the arenas.txt "single" arenas: Training, tiers of
					four, the Final; progress, record_result and its XP
  gameplay.gd		_show_results: a match's end turned into award_match's report (MatchEnd)

What the Godot build never recorded is (2026-10-30 and -31), so its achievements and parts can be
earned: award_match writes the career's campaign record (each arena's best skill won), the top speed
the HUD measured in the match (best.speed), and what the stats recorder (P8b step 5) reads from the
HUD's match state: frags by weapon, pickups, deaths by cause, the rail's longest run of hits and best
accuracy. make_progress_vectors.py makes its vectors from the Godot code with the same lines added.

Files, in the user folder whichever mod runs (the online identity's rule): <fs_homepath>/profile/
profile.json and campaign.json, the Godot build's own formats. The first run from the player's own
user folder imports the Godot build's (%APPDATA%\Godot\app_userdata\Q3Remaster); a test's scratch
fs_homepath never does.

  oz_profile_import	1: a new profile is first imported from the Godot build's user folder
  ozprogress_vectors <file>	runs the test vectors in memory (the saved career isn't touched)
  ozprofile			prints the level, XP and achievements
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
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace OzUI {
namespace Prog {

const char *const SLOTS[NUM_SLOTS][2] = { { "banner", "banners" }, { "emblem", "emblems" }, { "frame", "frames" },
	{ "title", "titles" }, { "accent", "accents" }, { "taunt", "taunts" }, { "rail", "rails" } };

static cJSON *cat;										// the catalog
static std::map<std::string, std::map<std::string, cJSON *>> partIndex;
static std::map<std::string, cJSON *> achIndex;
static cJSON *prof;										// q3_profile.gd's _data
static cJSON *camp;										// campaign.gd's progress
static cJSON *lastResult;								// campaign.gd's last_result
static std::vector<Tier> tiers;
static bool memoryOnly;									// the vectors run: nothing reaches the disk
static int version = 1;
static std::string postgame;							// the game's last "postgame", until a match takes it
static std::string lastMovie;
static bool campaignResults;							// the results on screen are a campaign arena's
static int waitStart;									// when a campaign match's end first waited for its postgame

// ---------------------------------------------------------------- JSON helpers

cJSON *Get( const cJSON *o, const char *k ) {
	return o && cJSON_IsObject( o ) ? cJSON_GetObjectItemCaseSensitive( o, k ) : nullptr;
}

std::string Str( const cJSON *o, const char *k, const char *def ) {
	const cJSON *v = Get( o, k );
	return cJSON_IsString( v ) ? v->valuestring : def;
}

// Godot's float() / int() of a value: numbers and bools, anything else 0
double Num( const cJSON *v, double def ) {
	if ( cJSON_IsNumber( v ) ) {
		return v->valuedouble;
	}
	if ( cJSON_IsBool( v ) ) {
		return cJSON_IsTrue( v ) ? 1 : 0;
	}
	return def;
}

static long long Int( const cJSON *v, long long def = 0 ) {
	return v ? (long long)std::trunc( Num( v, (double)def ) ) : def;
}

static void SetNum( cJSON *o, const char *k, double n ) {
	if ( Get( o, k ) ) {
		cJSON_ReplaceItemInObjectCaseSensitive( o, k, cJSON_CreateNumber( n ) );
	} else {
		cJSON_AddNumberToObject( o, k, n );
	}
}

static void SetItem( cJSON *o, const char *k, cJSON *item ) {
	if ( Get( o, k ) ) {
		cJSON_ReplaceItemInObjectCaseSensitive( o, k, item );
	} else {
		cJSON_AddItemToObject( o, k, item );
	}
}

static cJSON *ObjectIn( cJSON *o, const char *k ) {
	cJSON *v = Get( o, k );
	if ( !cJSON_IsObject( v ) ) {
		v = cJSON_CreateObject();
		SetItem( o, k, v );
	}
	return v;
}

static bool Contains( const cJSON *arr, const std::string &s ) {
	const cJSON *e;
	cJSON_ArrayForEach( e, arr ) {
		if ( cJSON_IsString( e ) && s == e->valuestring ) {
			return true;
		}
	}
	return false;
}

static cJSON *Line( const std::string &label, long long xp ) {
	cJSON *l = cJSON_CreateArray();
	cJSON_AddItemToArray( l, cJSON_CreateString( label.c_str() ) );
	cJSON_AddItemToArray( l, cJSON_CreateNumber( (double)xp ) );
	return l;
}

static std::string Fmt( const char *fmt, ... ) {
	char buf[256];
	va_list ap;
	va_start( ap, fmt );
	vsnprintf( buf, sizeof( buf ), fmt, ap );
	va_end( ap );
	return buf;
}

// ---------------------------------------------------------------- the catalog (q3_cards.gd)

cJSON *Catalog() {
	if ( !cat ) {
		void *buf = nullptr;
		const int len = OzE_ReadFile( "ui/cards/catalog.json", &buf );
		cat = len > 0 ? cJSON_ParseWithLength( (const char *)buf, len ) : nullptr;
		if ( buf ) {
			OzE_FreeFile( buf );
		}
		if ( !cat ) {
			Printf( "^3profile: ui/cards/catalog.json is missing or isn't JSON\n" );
			cat = cJSON_CreateObject();
		}
		cJSON *kind;
		cJSON_ArrayForEach( kind, Get( cat, "parts" ) ) {
			cJSON *p;
			cJSON_ArrayForEach( p, kind ) {
				partIndex[kind->string][Str( p, "id" )] = p;
			}
		}
		cJSON *a;
		cJSON_ArrayForEach( a, Get( cat, "achievements" ) ) {
			achIndex[Str( a, "id" )] = a;
		}
	}
	return cat;
}

cJSON *Parts( const char *kind ) {
	return Get( Get( Catalog(), "parts" ), kind );
}

cJSON *Part( const char *kind, const std::string &id ) {
	Catalog();
	auto k = partIndex.find( kind );
	if ( k == partIndex.end() ) {
		return nullptr;
	}
	auto p = k->second.find( id );
	return p == k->second.end() ? nullptr : p->second;
}

cJSON *Achievements() {
	return Get( Catalog(), "achievements" );
}

cJSON *Achievement( const std::string &id ) {
	Catalog();
	auto a = achIndex.find( id );
	return a == achIndex.end() ? nullptr : a->second;
}

static const char *SlotKind( const std::string &slot ) {
	for ( auto &s : SLOTS ) {
		if ( slot == s[0] ) {
			return s[1];
		}
	}
	return nullptr;
}

cJSON *DefaultCard() {
	cJSON *d = Get( Catalog(), "default_card" );
	return d ? cJSON_Duplicate( d, 1 ) : cJSON_CreateObject();
}

static cJSON *NewProfile() {
	cJSON *p = cJSON_CreateObject();
	cJSON_AddNumberToObject( p, "level", 1 );
	cJSON_AddNumberToObject( p, "xp", 0 );
	cJSON_AddObjectToObject( p, "stats" );
	cJSON_AddObjectToObject( p, "campaign" );
	return p;
}

// a career stat by dotted key; a missing stat is 0
double Stat( const cJSON *stats, const std::string &key ) {
	const cJSON *v = stats;
	std::stringstream ss( key );
	std::string k;
	while ( std::getline( ss, k, '.' ) ) {
		if ( !cJSON_IsObject( v ) || !( v = Get( v, k.c_str() ) ) ) {
			return 0;
		}
	}
	return cJSON_IsNumber( v ) ? v->valuedouble : 0;
}

static const cJSON *DictStat( const cJSON *stats, const std::string &key ) {
	const cJSON *v = stats;
	std::stringstream ss( key );
	std::string k;
	while ( std::getline( ss, k, '.' ) ) {
		if ( !cJSON_IsObject( v ) || !( v = Get( v, k.c_str() ) ) ) {
			return nullptr;
		}
	}
	return cJSON_IsObject( v ) ? v : nullptr;
}

static int CampaignCount( const cJSON *profile, const cJSON *arenas, int skill ) {
	const cJSON *won = Get( profile, "campaign" ), *m;
	int n = 0;
	cJSON_ArrayForEach( m, arenas ) {
		n += cJSON_IsString( m ) && Int( Get( won, m->valuestring ) ) >= skill;
	}
	return n;
}

// how far along an achievement is (compare with its "target")
double ProgressOf( const cJSON *a, const cJSON *profile ) {
	const cJSON *p = Get( a, "progress" ), *stats = Get( profile, "stats" );
	if ( Get( p, "stat" ) ) {
		return Stat( stats, Str( p, "stat" ) );
	}
	if ( Get( p, "set" ) ) {
		const cJSON *d = DictStat( stats, Str( p, "set" ) ), *k;
		const double need = Num( Get( p, "min" ), 1 );
		int n = 0;
		cJSON_ArrayForEach( k, Get( p, "of" ) ) {
			n += cJSON_IsString( k ) && Num( Get( d, k->valuestring ), 0 ) >= need;
		}
		return n;
	}
	if ( Get( p, "max" ) ) {
		double best = 0;
		const cJSON *v;
		cJSON_ArrayForEach( v, DictStat( stats, Str( p, "max" ) ) ) {
			best = std::max( best, Num( v, 0 ) );
		}
		return best;
	}
	if ( Get( p, "campaign" ) ) {
		return CampaignCount( profile, Get( p, "campaign" ), (int)Int( Get( p, "skill" ), 1 ) );
	}
	return 0;
}

static bool IsAchieved( const cJSON *a, const cJSON *profile ) {
	return ProgressOf( a, profile ) >= Num( Get( a, "target" ), 1 );
}

// collections: the keys still needed
std::vector<std::string> Missing( const cJSON *a, const cJSON *profile ) {
	const cJSON *p = Get( a, "progress" ), *k;
	std::vector<std::string> out;
	if ( Get( p, "set" ) ) {
		const cJSON *d = DictStat( Get( profile, "stats" ), Str( p, "set" ) );
		cJSON_ArrayForEach( k, Get( p, "of" ) ) {
			if ( cJSON_IsString( k ) && Num( Get( d, k->valuestring ), 0 ) < Num( Get( p, "min" ), 1 ) ) {
				out.push_back( k->valuestring );
			}
		}
	} else if ( Get( p, "campaign" ) ) {
		const cJSON *won = Get( profile, "campaign" );
		cJSON_ArrayForEach( k, Get( p, "campaign" ) ) {
			if ( cJSON_IsString( k ) && Int( Get( won, k->valuestring ) ) < Int( Get( p, "skill" ), 1 ) ) {
				out.push_back( k->valuestring );
			}
		}
	}
	return out;
}

std::set<std::string> Achieved( const cJSON *profile ) {
	std::set<std::string> out;
	const cJSON *a;
	cJSON_ArrayForEach( a, Achievements() ) {
		if ( IsAchieved( a, profile ) ) {
			out.insert( Str( a, "id" ) );
		}
	}
	return out;
}

bool IsUnlocked( const cJSON *rule, const cJSON *profile, const std::set<std::string> *done ) {
	if ( cJSON_IsTrue( Get( rule, "free" ) ) ) {
		return true;
	}
	if ( Get( rule, "level" ) ) {
		return Int( Get( profile, "level" ), 1 ) >= Int( Get( rule, "level" ) );
	}
	if ( Get( rule, "achievement" ) ) {
		if ( done ) {
			return done->count( Str( rule, "achievement" ) ) > 0;
		}
		return Achieved( profile ).count( Str( rule, "achievement" ) ) > 0;
	}
	if ( Get( rule, "stat" ) ) {
		return Stat( Get( profile, "stats" ), Str( rule, "stat" ) ) >= Num( Get( rule, "min" ), 1 );
	}
	if ( Get( rule, "campaign" ) ) {
		return CampaignCount( profile, Get( rule, "campaign" ), (int)Int( Get( rule, "skill" ), 1 ) ) >= cJSON_GetArraySize( Get( rule, "campaign" ) );
	}
	if ( Get( rule, "any" ) ) {
		const cJSON *r;
		cJSON_ArrayForEach( r, Get( rule, "any" ) ) {
			if ( IsUnlocked( r, profile, done ) ) {
				return true;
			}
		}
	}
	return false;
}

// what unlocks a part, for the locker: "LEVEL 40", "ACHIEVEMENT: RAIL GOD", or the part's own text
std::string UnlockText( const cJSON *p ) {
	const cJSON *rule = Get( p, "unlock" );
	std::string s;
	if ( cJSON_IsTrue( Get( rule, "free" ) ) ) {
		return "FREE";
	}
	if ( Get( rule, "level" ) ) {
		return Fmt( "LEVEL %d", (int)Int( Get( rule, "level" ) ) );
	}
	if ( Get( rule, "achievement" ) ) {
		const std::string id = Str( rule, "achievement" );
		s = "ACHIEVEMENT: " + Str( Achievement( id ), "name", id.c_str() );
	} else {
		s = Str( p, "desc" );
	}
	for ( char &c : s ) {
		c = (char)toupper( (unsigned char)c );
	}
	return s;
}

static cJSON *KnownStats( const cJSON *ids, bool achieved, const std::set<std::string> *done ) {
	cJSON *out = cJSON_CreateArray();
	const cJSON *s;
	cJSON_ArrayForEach( s, ids ) {
		if ( cJSON_GetArraySize( out ) >= SHOWCASE || !cJSON_IsString( s ) ) {
			continue;
		}
		bool ok = false;
		if ( achieved ) {
			ok = done ? done->count( s->valuestring ) > 0 : Achievement( s->valuestring ) != nullptr;
		} else {
			const cJSON *e;
			cJSON_ArrayForEach( e, Get( Catalog(), "card_stats" ) ) {
				ok |= Str( e, "id" ) == s->valuestring;
			}
		}
		if ( ok ) {
			cJSON_AddItemToArray( out, cJSON_CreateString( s->valuestring ) );
		}
	}
	return out;
}

// the card as anyone else may see it: every part the profile hasn't earned goes back to the default
cJSON *EnforceUnlocks( const cJSON *card, const cJSON *profile ) {
	cJSON *def = DefaultCard();
	const std::set<std::string> done = Achieved( profile );
	cJSON *out = cJSON_Duplicate( def, 1 );
	for ( auto &s : SLOTS ) {
		const cJSON *p = Part( s[1], Str( card, s[0] ) );
		if ( p && IsUnlocked( Get( p, "unlock" ), profile, &done ) ) {
			SetItem( out, s[0], cJSON_CreateString( Str( p, "id" ).c_str() ) );
		}
	}
	cJSON *stats = KnownStats( Get( card, "stats" ), false, nullptr );
	if ( cJSON_GetArraySize( stats ) ) {
		SetItem( out, "stats", stats );
	} else {
		cJSON_Delete( stats );
		SetItem( out, "stats", cJSON_Duplicate( Get( def, "stats" ), 1 ) );
	}
	SetItem( out, "badges", KnownStats( Get( card, "badges" ), true, &done ) );
	cJSON_Delete( def );
	return out;
}

// a card from someone else's game: every part this catalog doesn't know goes back to the default
cJSON *KnownParts( const cJSON *card ) {
	cJSON *out = DefaultCard();
	for ( auto &s : SLOTS ) {
		if ( Part( s[1], Str( card, s[0] ) ) ) {
			SetItem( out, s[0], cJSON_CreateString( Str( card, s[0] ).c_str() ) );
		}
	}
	SetItem( out, "stats", KnownStats( Get( card, "stats" ), false, nullptr ) );
	SetItem( out, "badges", KnownStats( Get( card, "badges" ), true, nullptr ) );
	return out;
}

static long long XpToNext( long long level ) {
	const cJSON *l = Get( Catalog(), "levels" );
	return Int( Get( l, "xp_base" ), 1000 ) + Int( Get( l, "xp_step" ), 25 ) * ( level - 1 );
}

int MaxLevel() {
	return (int)Int( Get( Get( Catalog(), "levels" ), "max" ), 100 );
}

LevelInfo LevelFor( long long total ) {
	const long long top = MaxLevel();
	long long level = 1, left = total;
	while ( level < top && left >= XpToNext( level ) ) {
		left -= XpToNext( level );
		level++;
	}
	return { (int)level, left, XpToNext( level ) };
}

// a ranked rating as the ladder shows it: {id, name, division, label, next, from}
cJSON *Rank( long long rating ) {
	const cJSON *list = Get( Catalog(), "ranked_tiers" );
	cJSON *out = cJSON_CreateObject();
	const int n = cJSON_GetArraySize( list );
	if ( !n ) {
		return out;
	}
	const long long step = Int( Get( Get( Catalog(), "ranked" ), "division" ), 100 );
	int i = 0;
	for ( int t = 0; t < n; t++ ) {
		if ( rating >= Int( Get( cJSON_GetArrayItem( list, t ), "min" ) ) ) {
			i = t;
		}
	}
	const cJSON *tier = cJSON_GetArrayItem( list, i );
	const std::string name = Str( tier, "name" );
	cJSON_AddStringToObject( out, "id", Str( tier, "id" ).c_str() );
	cJSON_AddStringToObject( out, "name", name.c_str() );
	if ( i == n - 1 ) {
		cJSON_AddNumberToObject( out, "division", 0 );
		cJSON_AddStringToObject( out, "label", name.c_str() );
		cJSON_AddNumberToObject( out, "next", -1 );
		cJSON_AddNumberToObject( out, "from", (double)Int( Get( tier, "min" ) ) );
		return out;
	}
	const long long top = Int( Get( cJSON_GetArrayItem( list, i + 1 ), "min" ) );
	const long long div = std::min( 3LL, std::max( 1LL, (long long)std::ceil( (double)( top - rating ) / step ) ) );
	const long long next = top - ( div - 1 ) * step;
	static const char *roman[3] = { "I", "II", "III" };
	cJSON_AddNumberToObject( out, "division", (double)div );
	cJSON_AddStringToObject( out, "label", ( name + " " + roman[div - 1] ).c_str() );
	cJSON_AddNumberToObject( out, "next", (double)next );
	cJSON_AddNumberToObject( out, "from", (double)( div < 3 ? std::max( Int( Get( tier, "min" ) ), next - step ) : Int( Get( tier, "min" ) ) ) );
	return out;
}

// a showcase stat as the card prints it
std::string StatValue( const std::string &id, const cJSON *profile ) {
	const cJSON *s = nullptr, *e;
	cJSON_ArrayForEach( e, Get( Catalog(), "card_stats" ) ) {
		if ( Str( e, "id" ) == id ) {
			s = e;
		}
	}
	const std::string how = Str( s, "value" );
	const cJSON *stats = Get( profile, "stats" );
	if ( how == "=kd" ) {
		return Fmt( "%.2f", Stat( stats, "frags" ) / std::max( Stat( stats, "deaths" ), 1.0 ) );
	}
	if ( how == "=arenas_won" ) {
		int n = 0;
		const cJSON *v;
		cJSON_ArrayForEach( v, DictStat( stats, "wins_map" ) ) {
			n += Num( v, 0 ) >= 1.0;
		}
		return Fmt( "%d", n );
	}
	if ( how == "=achievements" ) {
		return Fmt( "%d/%d", (int)Achieved( profile ).size(), cJSON_GetArraySize( Achievements() ) );
	}
	if ( how == "=hours" ) {
		return Fmt( "%lld", (long long)( Stat( stats, "playtime" ) / 3600.0 ) );
	}
	if ( how == "best.speed" ) {
		return Fmt( "%lld UPS", (long long)Stat( stats, how ) );
	}
	if ( how == "best.rail_accuracy" ) {
		return Fmt( "%lld%%", (long long)Stat( stats, how ) );
	}
	if ( how.empty() ) {
		return "-";
	}
	return Fmt( "%lld", (long long)Stat( stats, how ) );
}

// ---------------------------------------------------------------- the career (q3_profile.gd)

std::filesystem::path Folder() {
	return std::filesystem::u8path( OzE_CvarString( "fs_homepath" ) ) / "profile";
}

cJSON *ReadJson( const std::filesystem::path &p ) {
	std::ifstream f( p, std::ios::binary );
	if ( !f ) {
		return nullptr;
	}
	std::stringstream ss;
	ss << f.rdbuf();
	return cJSON_Parse( ss.str().c_str() );
}

void WriteJson( const std::filesystem::path &p, const cJSON *j ) {
	char *text = cJSON_Print( j );
	std::error_code ec;
	std::filesystem::create_directories( p.parent_path(), ec );
	std::ofstream f( p, std::ios::binary | std::ios::trunc );
	if ( f && text ) {
		f << text;
	}
	if ( !f ) {
		Printf( "^3profile: couldn't write %s\n", p.u8string().c_str() );
	}
	cJSON_free( text );
}

// the first run from the player's own user folder takes the Godot build's career and campaign
static void Import() {
	const char *appdata = getenv( "APPDATA" );
	if ( !atoi( OzE_CvarString( "oz_profile_import" ) ) || !OzE_IsPlayerHome() || !appdata || !appdata[0] ) {
		return;
	}
	const std::filesystem::path from = std::filesystem::u8path( appdata ) / "Godot" / "app_userdata" / "Q3Remaster";
	std::error_code ec;
	if ( !std::filesystem::exists( from / "profile.json", ec ) ) {
		return;
	}
	std::filesystem::create_directories( Folder(), ec );
	for ( const char *name : { "profile.json", "campaign.json" } ) {
		if ( std::filesystem::exists( from / name, ec ) ) {
			std::filesystem::copy_file( from / name, Folder() / name, std::filesystem::copy_options::skip_existing, ec );
		}
	}
	Printf( "profile: imported the Godot build's career from %s\n", from.u8string().c_str() );
}

static void AddIn( cJSON *st, const char *k, const std::string &sub, long long n );

// profiles written before 2026-10-23 kept medals flat; the default rail was blue until then
static void Migrate( cJSON *d ) {
	cJSON *st = ObjectIn( d, "stats" );
	static const char *old[][2] = { { "impressives", "impressive" }, { "excellents", "excellent" }, { "gauntlets", "gauntlet" },
		{ "defends", "defend" }, { "assists", "assist" }, { "capturess", "capture" }, { "perfects", "perfect" }, { "accuracys", "accuracy" } };
	for ( auto &o : old ) {
		if ( Get( st, o[0] ) ) {
			AddIn( st, "medal", o[1], Int( Get( st, o[0] ) ) );
			cJSON_DeleteItemFromObjectCaseSensitive( st, o[0] );
		}
	}
	cJSON_DeleteItemFromObjectCaseSensitive( st, "fragss" );
	if ( Int( Get( d, "card_v" ), 1 ) < 2 ) {
		cJSON *card = Get( d, "card" );
		if ( Str( card, "rail" ) == "rail_blue" ) {
			SetItem( card, "rail", cJSON_CreateString( "rail_red" ) );
		}
		SetNum( d, "card_v", 2 );
	}
}

cJSON *Profile() {
	if ( !prof ) {
		if ( !memoryOnly ) {
			std::error_code ec;
			if ( !std::filesystem::exists( Folder() / "profile.json", ec ) ) {
				Import();
			}
			prof = ReadJson( Folder() / "profile.json" );
		}
		if ( !cJSON_IsObject( prof ) ) {
			cJSON_Delete( prof );
			prof = NewProfile();
		}
		SetNum( prof, "xp", (double)Int( Get( prof, "xp" ) ) );
		if ( !Get( prof, "card" ) ) {
			cJSON_AddItemToObject( prof, "card", DefaultCard() );
		}
		Migrate( prof );
		if ( !memoryOnly ) {
			// Oz: careers from before the campaign record was written (the Godot build's included) take
			// the arenas their campaign has won
			cJSON *c = ReadJson( Folder() / "campaign.json" ), *won = ObjectIn( prof, "campaign" );
			const cJSON *w;
			cJSON_ArrayForEach( w, Get( c, "won" ) ) {
				if ( Int( w ) > Int( Get( won, w->string ) ) ) {
					SetNum( won, w->string, (double)Int( w ) );
				}
			}
			cJSON_Delete( c );
		}
	}
	return prof;
}

void Save() {
	version++;
	if ( !memoryOnly ) {
		WriteJson( Folder() / "profile.json", Profile() );
		Changed();
	}
}

long long TotalXp() {
	return Int( Get( Profile(), "xp" ) );
}

// the card to show for the local player, every part they haven't earned put back to default
cJSON *ShownCard() {
	return EnforceUnlocks( Get( Profile(), "card" ), Profile() );
}

static void Add( cJSON *st, const char *k, long long n ) {
	SetNum( st, k, (double)( Int( Get( st, k ) ) + n ) );
}

static void AddIn( cJSON *st, const char *k, const std::string &sub, long long n ) {
	cJSON *d = ObjectIn( st, k );
	SetNum( d, sub.c_str(), (double)( Int( Get( d, sub.c_str() ) ) + n ) );
}

// every part the career has earned, "kind:id", in the catalog's order
std::vector<std::string> UnlockedIds( const cJSON *profile ) {
	const std::set<std::string> done = Achieved( profile );
	std::vector<std::string> out;
	for ( auto &s : SLOTS ) {
		const cJSON *p;
		cJSON_ArrayForEach( p, Parts( s[1] ) ) {
			if ( IsUnlocked( Get( p, "unlock" ), profile, &done ) ) {
				out.push_back( std::string( s[1] ) + ":" + Str( p, "id" ) );
			}
		}
	}
	return out;
}

// Godot's String.capitalize() of a one-word id
static std::string Capitalize( std::string s ) {
	if ( !s.empty() ) {
		s[0] = (char)toupper( (unsigned char)s[0] );
	}
	return s;
}

// one match into the career: q3_profile.gd award_match, line by line
cJSON *AwardMatch( const cJSON *r ) {
	cJSON *d = Profile();
	const std::vector<std::string> had = UnlockedIds( d );
	cJSON *lines = cJSON_CreateArray();
	cJSON_AddItemToArray( lines, Line( "Match complete", 100 ) );
	const long long frags = Int( Get( r, "frags" ) );
	if ( frags > 0 ) {
		cJSON_AddItemToArray( lines, Line( Fmt( "%lld frags", frags ), frags * 10 ) );
	}
	const bool won = cJSON_IsTrue( Get( r, "won" ) );
	if ( won ) {
		const long long skill = Int( Get( r, "campaign_skill" ) );
		cJSON_AddItemToArray( lines, Line( "Victory" + ( skill > 0 ? Fmt( " (skill %lld)", skill ) : std::string() ),
			(long long)( 150 * ( skill > 0 ? std::max( (double)skill, 3.0 ) / 3.0 : 1.0 ) ) ) );
	}
	const cJSON *m;
	cJSON_ArrayForEach( m, Get( r, "medals" ) ) {
		const long long n = Int( m );
		if ( n <= 0 ) {
			continue;
		}
		if ( !strcmp( m->string, "frags" ) ) {
			cJSON_AddItemToArray( lines, Line( Fmt( "Frags medal (%lld)", n ), 25 ) );
		} else {
			cJSON_AddItemToArray( lines, Line( Fmt( "%s x%lld", Capitalize( m->string ).c_str(), n ), n * 25 ) );
		}
	}
	const bool perfect = cJSON_IsTrue( Get( r, "perfect" ) );
	if ( perfect ) {
		cJSON_AddItemToArray( lines, Line( "Perfect", 100 ) );
	}
	const cJSON *x;
	cJSON_ArrayForEach( x, Get( r, "extra" ) ) {
		cJSON_AddItemToArray( lines, cJSON_Duplicate( x, 1 ) );
	}
	long long gain = 0;
	cJSON_ArrayForEach( x, lines ) {
		gain += Int( cJSON_GetArrayItem( x, 1 ) );
	}
	const long long before = TotalXp();
	SetNum( d, "xp", (double)( before + gain ) );
	cJSON *st = ObjectIn( d, "stats" );
	std::string mode = Str( r, "mode" );
	if ( mode == "clan_arena" ) {
		mode = "ca";
	} else if ( mode == "freeze_tag" ) {
		mode = "ft";
	}
	const std::string model = Str( r, "model" );
	Add( st, "matches", 1 );
	Add( st, "frags", frags );
	Add( st, "deaths", Int( Get( r, "deaths" ) ) );
	Add( st, "playtime", Int( Get( r, "seconds" ) ) );
	if ( !model.empty() ) {
		AddIn( st, "matches_model", model, 1 );
		AddIn( st, "matches_skin", model + "/" + Str( r, "skin", "default" ), 1 );
	}
	if ( won ) {
		Add( st, "wins", 1 );
		AddIn( st, "wins_map", Str( r, "map" ), 1 );
		if ( !mode.empty() ) {
			AddIn( st, "wins_mode", mode, 1 );
		}
		if ( mode == "duel" ) {
			Add( st, "duel_wins", 1 );
		}
		// Oz: the career's campaign record, each arena's best skill won, which the campaign's
		// achievements and parts read. The Godot build never wrote it, so they couldn't be earned.
		const long long skill = Int( Get( r, "campaign_skill" ) );
		if ( skill > 0 ) {
			cJSON *won = ObjectIn( d, "campaign" );
			const std::string map = Str( r, "map" );
			SetNum( won, map.c_str(), (double)std::max( Int( Get( won, map.c_str() ) ), skill ) );
		}
	}
	cJSON_ArrayForEach( m, Get( r, "medals" ) ) {
		if ( strcmp( m->string, "frags" ) ) {
			AddIn( st, "medal", !strcmp( m->string, "captures" ) ? "capture" : m->string, Int( m ) );
		}
	}
	if ( perfect ) {
		AddIn( st, "medal", "perfect", 1 );
	}
	// Oz: the top speed the player made by their own movement (the HUD's), which the movement
	// achievements read; the Godot build left it to its race mode, which Oz doesn't have
	const long long speed = Int( Get( r, "speed" ) );
	if ( speed > Stat( st, "best.speed" ) ) {
		SetNum( ObjectIn( st, "best" ), "speed", (double)speed );
	}
	// Oz: what the Godot build's career keys but never recorded (the stats recorder, P8b step 5): frags by weapon,
	// pickups, deaths by cause, the rail's longest run of hits, and its best accuracy in a match of 10 shots or more
	for ( const char *k : { "frags_weapon", "pickups", "deaths_by" } ) {
		const cJSON *x;
		cJSON_ArrayForEach( x, Get( r, k ) ) {
			AddIn( st, k, x->string, Int( x ) );
		}
	}
	const long long railStreak = Int( Get( r, "rail_streak" ) ), railShots = Int( Get( r, "rail_shots" ) );
	if ( railStreak > Stat( st, "best.rail_streak" ) ) {
		SetNum( ObjectIn( st, "best" ), "rail_streak", (double)railStreak );
	}
	if ( railShots >= 10 ) {
		const long long acc = Int( Get( r, "rail_hits" ) ) * 100 / railShots;
		if ( acc > Stat( st, "best.rail_accuracy" ) ) {
			SetNum( ObjectIn( st, "best" ), "rail_accuracy", (double)acc );
		}
	}
	SetNum( d, "level", LevelFor( Int( Get( d, "xp" ) ) ).level );
	cJSON *fresh = cJSON_CreateArray();
	cJSON *news = Get( d, "new" );
	if ( !cJSON_IsArray( news ) ) {
		news = cJSON_CreateArray();
		SetItem( d, "new", news );
	}
	for ( const std::string &key : UnlockedIds( d ) ) {
		if ( std::find( had.begin(), had.end(), key ) == had.end() ) {
			const std::string kind = key.substr( 0, key.find( ':' ) ), id = key.substr( key.find( ':' ) + 1 );
			cJSON *u = cJSON_CreateObject();
			cJSON_AddStringToObject( u, "kind", kind.c_str() );
			cJSON_AddStringToObject( u, "id", id.c_str() );
			cJSON_AddStringToObject( u, "name", Str( Part( kind.c_str(), id ), "name", id.c_str() ).c_str() );
			cJSON_AddItemToArray( fresh, u );
			if ( !Contains( news, key ) ) {
				cJSON_AddItemToArray( news, cJSON_CreateString( key.c_str() ) );
			}
		}
	}
	Save();
	cJSON *out = cJSON_CreateObject();
	cJSON_AddItemToObject( out, "lines", lines );
	cJSON_AddNumberToObject( out, "gain", (double)gain );
	cJSON_AddNumberToObject( out, "before", (double)before );
	cJSON_AddNumberToObject( out, "after", (double)( before + gain ) );
	cJSON_AddItemToObject( out, "unlocked", fresh );
	return out;
}

// ranked as the card shows it; the rating is only ever the ranked backend's
cJSON *Ranked() {
	const cJSON *st = Get( Profile(), "stats" );
	const cJSON *cfg = Get( Catalog(), "ranked" );
	const long long matches = (long long)Stat( st, "ranked_matches" );
	const long long rating = Get( st, "ranked_rating" ) ? (long long)Stat( st, "ranked_rating" ) : Int( Get( cfg, "start" ), 1000 );
	const long long placements = Int( Get( cfg, "placements" ), 10 );
	cJSON *out = cJSON_CreateObject();
	cJSON_AddBoolToObject( out, "open", LevelFor( TotalXp() ).level >= RANKED_LEVEL );
	cJSON_AddNumberToObject( out, "matches", (double)matches );
	cJSON_AddBoolToObject( out, "placed", matches >= placements );
	cJSON_AddNumberToObject( out, "rating", (double)rating );
	cJSON_AddItemToObject( out, "rank", Rank( rating ) );
	return out;
}

// puts a part in a card slot; refuses an unknown slot or part, or one not yet earned
bool SetCard( const std::string &slot, const std::string &id ) {
	const char *kind = SlotKind( slot );
	const cJSON *p = kind ? Part( kind, id ) : nullptr;
	if ( !p || !IsUnlocked( Get( p, "unlock" ), Profile(), nullptr ) ) {
		return false;
	}
	SetItem( Get( Profile(), "card" ), slot.c_str(), cJSON_CreateString( id.c_str() ) );
	Save();
	return true;
}

// the card's showcase stats or badges, in order; unknown stats and badges not yet earned are refused
bool SetShowcase( const std::string &kind, const std::vector<std::string> &ids ) {
	if ( (int)ids.size() > SHOWCASE || ( kind != "stats" && kind != "badges" ) ) {
		return false;
	}
	const std::set<std::string> done = Achieved( Profile() );
	cJSON *arr = cJSON_CreateArray();
	for ( const std::string &i : ids ) {
		bool ok = kind == "badges" ? done.count( i ) > 0 : false;
		if ( kind == "stats" ) {
			const cJSON *e;
			cJSON_ArrayForEach( e, Get( Catalog(), "card_stats" ) ) {
				ok |= Str( e, "id" ) == i;
			}
		}
		if ( !ok ) {
			cJSON_Delete( arr );
			return false;
		}
		cJSON_AddItemToArray( arr, cJSON_CreateString( i.c_str() ) );
	}
	SetItem( Get( Profile(), "card" ), kind.c_str(), arr );
	Save();
	return true;
}

// Godot's String.hash(): djb2 over the characters
static unsigned GodotHash( const std::string &s ) {
	unsigned h = 5381;
	for ( unsigned char c : s ) {
		h = ( h << 5 ) + h + c;
	}
	return h;
}

// a card for a bot: its own head, a banner and title picked from its name, a level from its skill
cJSON *BotCard( const std::string &name, const std::string &model, int skill ) {
	cJSON *c = DefaultCard();
	const long long h = GodotHash( name );
	const std::string emblem = "head_" + model;
	if ( Part( "emblems", emblem ) ) {
		SetItem( c, "emblem", cJSON_CreateString( emblem.c_str() ) );
	}
	const cJSON *banners = Parts( "banners" ), *titles = Parts( "titles" );
	if ( cJSON_GetArraySize( banners ) ) {
		SetItem( c, "banner", cJSON_CreateString( Str( cJSON_GetArrayItem( banners, (int)( h % cJSON_GetArraySize( banners ) ) ), "id" ).c_str() ) );
	}
	if ( cJSON_GetArraySize( titles ) ) {
		SetItem( c, "title", cJSON_CreateString( Str( cJSON_GetArrayItem( titles, (int)( ( h / 7 ) % cJSON_GetArraySize( titles ) ) ), "id" ).c_str() ) );
	}
	SetNum( c, "level", (double)std::min( 100LL, std::max( 1LL, skill * 12 + h % 9 ) ) );
	return c;
}

// ---------------------------------------------------------------- the campaign (campaign.gd)

static std::filesystem::path CampaignPath() {
	return Folder() / "campaign.json";
}

static cJSON *NewCampaign( int skill ) {
	cJSON *c = cJSON_CreateObject();
	cJSON_AddNumberToObject( c, "skill", skill );
	cJSON_AddObjectToObject( c, "won" );
	cJSON_AddObjectToObject( c, "awards" );
	return c;
}

cJSON *Campaign() {
	if ( !camp ) {
		camp = NewCampaign( 3 );
		cJSON *d = memoryOnly ? nullptr : ( Profile(), ReadJson( CampaignPath() ) );	// the profile first: it imports both
		const cJSON *e;
		cJSON_ArrayForEach( e, d ) {
			SetItem( camp, e->string, cJSON_Duplicate( e, 1 ) );
		}
		cJSON_Delete( d );
		ObjectIn( camp, "won" );
		ObjectIn( camp, "awards" );
	}
	return camp;
}

void SaveCampaign() {
	version++;
	if ( !memoryOnly ) {
		WriteJson( CampaignPath(), Campaign() );
		Changed();
	}
}

void ResetCampaign() {
	cJSON *c = NewCampaign( (int)Int( Get( Campaign(), "skill" ), 3 ) );
	cJSON_Delete( camp );
	camp = c;
	SaveCampaign();
}

int CampaignSkill() {
	return (int)Int( Get( Campaign(), "skill" ), 3 );
}

void SetCampaignSkill( int skill ) {
	if ( skill >= 1 && skill <= 5 && skill != CampaignSkill() ) {
		SetNum( Campaign(), "skill", skill );
		SaveCampaign();
	}
}

// the "single" arenas in file order: Training first, the Final last, the rest tiers of four
void SetArenas( const cJSON *maps ) {
	std::vector<const cJSON *> training, rest, final;
	const cJSON *m;
	tiers.clear();
	cJSON_ArrayForEach( m, maps ) {
		if ( !Contains( Get( m, "types" ), "single" ) ) {
			continue;
		}
		std::string special = Str( m, "special" );
		for ( char &c : special ) {
			c = (char)tolower( (unsigned char)c );
		}
		if ( special == "training" ) {
			training.push_back( m );
		} else if ( special == "final" ) {
			final.push_back( m );
		} else if ( special.empty() ) {
			rest.push_back( m );
		}
	}
	auto arena = [&]( const cJSON *m ) {
		Arena a;
		a.map = Str( m, "map" );
		a.name = Str( m, "name", a.map.c_str() );
		a.fraglimit = (int)Int( Get( m, "fraglimit" ) );
		const cJSON *b;
		cJSON_ArrayForEach( b, Get( m, "bots" ) ) {
			a.bots.push_back( cJSON_IsString( b ) ? b->valuestring : "" );
		}
		return a;
	};
	if ( !training.empty() ) {
		tiers.push_back( { "Training", {} } );
		for ( auto *t : training ) {
			tiers.back().arenas.push_back( arena( t ) );
		}
	}
	for ( size_t i = 0; i < rest.size(); i += 4 ) {
		tiers.push_back( { Fmt( "Tier %d", (int)( i / 4 + 1 ) ), {} } );
		for ( size_t k = i; k < i + 4 && k < rest.size(); k++ ) {
			tiers.back().arenas.push_back( arena( rest[k] ) );
		}
	}
	if ( !final.empty() ) {
		tiers.push_back( { "Final", {} } );
		for ( auto *t : final ) {
			tiers.back().arenas.push_back( arena( t ) );
		}
	}
}

const std::vector<Tier> &Tiers() {
	return tiers;
}

int WonAt( const std::string &map ) {
	return (int)Int( Get( Get( Campaign(), "won" ), map.c_str() ) );
}

bool TierComplete( int i ) {
	for ( const Arena &a : tiers[i].arenas ) {
		if ( !Get( Get( Campaign(), "won" ), a.map.c_str() ) ) {
			return false;
		}
	}
	return true;
}

bool TierOpen( int i ) {
	for ( int t = 0; t < i; t++ ) {
		if ( !TierComplete( t ) ) {
			return false;
		}
	}
	return true;
}

int ArenaTier( const std::string &map ) {
	for ( size_t i = 0; i < tiers.size(); i++ ) {
		for ( const Arena &a : tiers[i].arenas ) {
			if ( a.map == map ) {
				return (int)i;
			}
		}
	}
	return -1;
}

// a campaign match ended: the movie the win unlocks ("tier1" ... "tier7", "end"), or ""
static std::string RecordResult( const std::string &map, int skill, bool won, const cJSON *awards ) {
	const int tier = ArenaTier( map );
	const bool wasComplete = tier >= 0 && TierComplete( tier );
	const long long before = Int( Get( Get( Campaign(), "won" ), map.c_str() ) );
	cJSON_Delete( lastResult );
	lastResult = cJSON_CreateObject();
	cJSON_AddBoolToObject( lastResult, "first_win", won && before == 0 );
	cJSON_AddBoolToObject( lastResult, "skill_up", won && before > 0 && skill > before );
	cJSON_AddNumberToObject( lastResult, "skill", skill );
	cJSON_AddStringToObject( lastResult, "tier", "" );
	cJSON_AddBoolToObject( lastResult, "campaign", false );
	if ( won ) {
		SetNum( Get( Campaign(), "won" ), map.c_str(), (double)std::max( before, (long long)skill ) );
	}
	const cJSON *k;
	cJSON_ArrayForEach( k, awards ) {
		cJSON *aw = Get( Campaign(), "awards" );
		SetNum( aw, k->string, (double)( Int( Get( aw, k->string ) ) + Int( k ) ) );
	}
	SaveCampaign();
	if ( tier < 0 || wasComplete || !TierComplete( tier ) ) {
		return "";
	}
	SetItem( lastResult, "tier", cJSON_CreateString( tiers[tier].name.c_str() ) );
	SetItem( lastResult, "campaign", cJSON_CreateBool( tier == (int)tiers.size() - 1 ) );
	return tier == (int)tiers.size() - 1 ? "end" : Fmt( "tier%d", tier + 1 );
}

// the campaign XP lines for the last result
static cJSON *XpLines() {
	cJSON *out = cJSON_CreateArray();
	const cJSON *r = lastResult;
	if ( cJSON_IsTrue( Get( r, "first_win" ) ) ) {
		cJSON_AddItemToArray( out, Line( "Arena cleared", (long long)( 250 * std::max( Num( Get( r, "skill" ), 3 ), 1.0 ) / 3.0 ) ) );
	}
	if ( cJSON_IsTrue( Get( r, "skill_up" ) ) ) {
		cJSON_AddItemToArray( out, Line( "Cleared at a harder skill", 100 ) );
	}
	const std::string tier = Str( r, "tier" );
	if ( !tier.empty() && tier != "Training" && !cJSON_IsTrue( Get( r, "campaign" ) ) ) {
		cJSON_AddItemToArray( out, Line( tier + " complete", 1000 ) );
	}
	if ( cJSON_IsTrue( Get( r, "campaign" ) ) ) {
		cJSON_AddItemToArray( out, Line( "Campaign complete", 5000 ) );
	}
	return out;
}

// ---------------------------------------------------------------- a match's end (gameplay.gd _show_results)

#define RANK_TIED	0x4000

/*
`m`: {map, mode, gametype, campaign, scores (the standings, best first), place, team, team_scores,
pers: {impressive, excellent, gauntlet, defend, assist, captures, killed}, post (the postgame's
arguments, or ""), model, skin, seconds, speed (the top speed, ups), and what the stats recorder adds:
frags_weapon, pickups and deaths_by ({name: count}), rail {streak, shots, hits}}. Returns award_match's
result with the movie and the win.
*/
cJSON *MatchEnd( const cJSON *m ) {
	const bool campaign = cJSON_IsTrue( Get( m, "campaign" ) );
	std::vector<std::string> post;
	{
		std::stringstream ss( Str( m, "post" ) );
		std::string w;
		while ( ss >> w ) {
			post.push_back( w );
		}
	}
	auto P = [&]( size_t i ) { return i < post.size() ? atoll( post[i].c_str() ) : 0LL; };
	const cJSON *rows = Get( m, "scores" ), *p = Get( m, "pers" );
	const int numRows = cJSON_GetArraySize( rows );
	auto row = [&]( int i ) { return Int( cJSON_GetArrayItem( rows, i ) ); };
	int place = (int)Int( Get( m, "place" ) );
	bool tied = numRows > 1 && row( 0 ) == row( 1 );
	bool won = place == 0 && !tied && numRows > 1;
	if ( !campaign && Int( Get( m, "gametype" ) ) >= 3 ) {	// GT_TEAM: the team's score decides
		const cJSON *ts = Get( m, "team_scores" );
		const long long red = Int( cJSON_GetArrayItem( ts, 0 ) ), blue = Int( cJSON_GetArrayItem( ts, 1 ) );
		const long long team = Int( Get( m, "team" ) );
		tied = red == blue;
		won = !tied && ( ( team == 1 && red > blue ) || ( team == 2 && blue > red ) );
	}
	cJSON *medals = cJSON_CreateObject();
	for ( const char *k : { "impressive", "excellent", "gauntlet", "defend", "assist", "captures" } ) {
		cJSON_AddNumberToObject( medals, k, (double)Int( Get( p, k ) ) );
	}
	bool perfect = won && Int( Get( p, "killed" ) ) == 0;
	cJSON *awards = cJSON_CreateObject();
	if ( post.size() >= 8 ) {
		// clients you accuracy impressive excellent gauntlet score perfect, then client rank score each
		const long long you = P( 1 );
		long long rank = 8;
		for ( size_t i = 8; i + 2 < post.size(); i += 3 ) {
			if ( P( i ) == you ) {
				rank = ( P( i + 1 ) & ~RANK_TIED ) + 1;
			}
		}
		won = rank == 1;
		place = rank < 8 ? (int)std::max( rank - 1, 0LL ) : place;
		perfect = P( 7 ) != 0;
		const long long fragsBefore = Int( Get( Get( Campaign(), "awards" ), "frags" ) );
		const long long frags = std::max( P( 6 ), 0LL );
		cJSON_AddNumberToObject( awards, "accuracy", P( 2 ) >= 50 ? 1 : 0 );
		cJSON_AddNumberToObject( awards, "impressive", (double)P( 3 ) );
		cJSON_AddNumberToObject( awards, "excellent", (double)P( 4 ) );
		cJSON_AddNumberToObject( awards, "gauntlet", (double)P( 5 ) );
		cJSON_AddNumberToObject( awards, "frags", (double)frags );
		cJSON_AddNumberToObject( awards, "perfect", perfect ? 1 : 0 );
		cJSON_Delete( medals );
		medals = cJSON_CreateObject();
		cJSON_AddNumberToObject( medals, "impressive", (double)P( 3 ) );
		cJSON_AddNumberToObject( medals, "excellent", (double)P( 4 ) );
		cJSON_AddNumberToObject( medals, "gauntlet", (double)P( 5 ) );
		if ( P( 2 ) >= 50 ) {
			cJSON_AddNumberToObject( medals, "accuracy", 1 );
		}
		if ( ( fragsBefore + frags ) / 100 > fragsBefore / 100 ) {
			cJSON_AddNumberToObject( medals, "frags", (double)( ( fragsBefore + frags ) / 100 * 100 ) );
		}
	}
	std::string movie;
	cJSON *extra = cJSON_CreateArray();
	const int skill = CampaignSkill();
	if ( campaign ) {
		if ( !cJSON_GetArraySize( awards ) ) {
			cJSON_Delete( awards );
			awards = cJSON_Duplicate( medals, 1 );
			if ( perfect ) {
				cJSON_AddNumberToObject( awards, "perfect", 1 );
			}
		}
		movie = RecordResult( Str( m, "map" ), skill, won, awards );
		cJSON_Delete( extra );
		extra = XpLines();
	}
	cJSON_Delete( awards );
	cJSON *r = cJSON_CreateObject();
	cJSON_AddStringToObject( r, "map", Str( m, "map" ).c_str() );
	cJSON_AddStringToObject( r, "mode", Str( m, "mode" ).c_str() );
	cJSON_AddBoolToObject( r, "won", won );
	cJSON_AddNumberToObject( r, "frags", (double)std::max( place < numRows ? row( place ) : 0, 0LL ) );
	cJSON_AddNumberToObject( r, "deaths", (double)Int( Get( p, "killed" ) ) );
	cJSON_AddItemToObject( r, "medals", medals );
	cJSON_AddBoolToObject( r, "perfect", perfect );
	cJSON_AddNumberToObject( r, "campaign_skill", campaign ? skill : 0 );
	cJSON_AddItemToObject( r, "extra", extra );
	cJSON_AddStringToObject( r, "model", Str( m, "model" ).c_str() );
	cJSON_AddStringToObject( r, "skin", Str( m, "skin" ).c_str() );
	cJSON_AddNumberToObject( r, "seconds", (double)Int( Get( m, "seconds" ) ) );
	cJSON_AddNumberToObject( r, "speed", (double)Int( Get( m, "speed" ) ) );
	for ( const char *k : { "frags_weapon", "pickups", "deaths_by" } ) {
		cJSON_AddItemToObject( r, k, cJSON_IsObject( Get( m, k ) ) ? cJSON_Duplicate( Get( m, k ), 1 ) : cJSON_CreateObject() );
	}
	const cJSON *rail = Get( m, "rail" );
	cJSON_AddNumberToObject( r, "rail_streak", (double)Int( Get( rail, "streak" ) ) );
	cJSON_AddNumberToObject( r, "rail_shots", (double)Int( Get( rail, "shots" ) ) );
	cJSON_AddNumberToObject( r, "rail_hits", (double)Int( Get( rail, "hits" ) ) );
	cJSON *xp = AwardMatch( r );
	cJSON_Delete( r );
	cJSON_AddStringToObject( xp, "movie", movie.c_str() );
	cJSON_AddBoolToObject( xp, "won", won );
	return xp;
}

static const char *ModeName( int gametype, const std::string &map ) {
	switch ( gametype ) {
	case 1: return "duel";
	case 2: return !map.compare( 0, 9, "q3tourney" ) ? "duel" : "ffa";	// campaign.gd setup_for
	case 3: return "tdm";
	case 4: return "ctf";
	default: return "ffa";
	}
}

// ---------------------------------------------------------------- the test vectors

static bool Same( const cJSON *a, const cJSON *b, const std::string &path, std::string &why ) {
	if ( cJSON_IsNumber( a ) || cJSON_IsNumber( b ) ) {
		if ( !cJSON_IsNumber( a ) || !cJSON_IsNumber( b ) || std::fabs( a->valuedouble - b->valuedouble ) > 1e-6 ) {
			why = path + ": " + ( a ? cJSON_PrintUnformatted( a ) : "nothing" ) + " for " + ( b ? cJSON_PrintUnformatted( b ) : "nothing" );
			return false;
		}
		return true;
	}
	if ( cJSON_IsString( a ) || cJSON_IsString( b ) ) {
		if ( !cJSON_IsString( a ) || !cJSON_IsString( b ) || strcmp( a->valuestring, b->valuestring ) ) {
			why = path + ": " + ( a ? cJSON_PrintUnformatted( a ) : "nothing" ) + " for " + ( b ? cJSON_PrintUnformatted( b ) : "nothing" );
			return false;
		}
		return true;
	}
	if ( cJSON_IsBool( a ) || cJSON_IsBool( b ) ) {
		if ( !cJSON_IsBool( a ) || !cJSON_IsBool( b ) || cJSON_IsTrue( a ) != cJSON_IsTrue( b ) ) {
			why = path + ": a different truth";
			return false;
		}
		return true;
	}
	if ( cJSON_IsArray( b ) ) {
		if ( !cJSON_IsArray( a ) || cJSON_GetArraySize( a ) != cJSON_GetArraySize( b ) ) {
			why = path + Fmt( ": %d entries for %d", cJSON_GetArraySize( a ), cJSON_GetArraySize( b ) ) + " " + ( a ? cJSON_PrintUnformatted( a ) : "" );
			return false;
		}
		for ( int i = 0; i < cJSON_GetArraySize( b ); i++ ) {
			if ( !Same( cJSON_GetArrayItem( a, i ), cJSON_GetArrayItem( b, i ), path + Fmt( "[%d]", i ), why ) ) {
				return false;
			}
		}
		return true;
	}
	if ( cJSON_IsObject( b ) ) {
		if ( !cJSON_IsObject( a ) || cJSON_GetArraySize( a ) != cJSON_GetArraySize( b ) ) {
			why = path + ": not the same keys";
			return false;
		}
		const cJSON *e;
		cJSON_ArrayForEach( e, b ) {
			if ( !Same( Get( a, e->string ), e, path + "." + e->string, why ) ) {
				return false;
			}
		}
		return true;
	}
	return true;
}

static cJSON *Sorted( const std::set<std::string> &s ) {
	cJSON *a = cJSON_CreateArray();
	for ( const std::string &x : s ) {
		cJSON_AddItemToArray( a, cJSON_CreateString( x.c_str() ) );
	}
	return a;
}

static cJSON *EvalProfile( const cJSON *profile, const cJSON *cards ) {
	cJSON *out = cJSON_CreateObject();
	cJSON_AddItemToObject( out, "achieved", Sorted( Achieved( profile ) ) );
	cJSON *progress = cJSON_AddObjectToObject( out, "progress" ), *missing = cJSON_AddObjectToObject( out, "missing" );
	const cJSON *a;
	cJSON_ArrayForEach( a, Achievements() ) {
		cJSON_AddNumberToObject( progress, Str( a, "id" ).c_str(), ProgressOf( a, profile ) );
		const std::vector<std::string> ms = Missing( a, profile );
		if ( !ms.empty() ) {
			cJSON *l = cJSON_AddArrayToObject( missing, Str( a, "id" ).c_str() );
			for ( const std::string &x : ms ) {
				cJSON_AddItemToArray( l, cJSON_CreateString( x.c_str() ) );
			}
		}
	}
	cJSON *unlocked = cJSON_AddArrayToObject( out, "unlocked" );
	for ( const std::string &k : UnlockedIds( profile ) ) {
		cJSON_AddItemToArray( unlocked, cJSON_CreateString( k.c_str() ) );
	}
	cJSON *sv = cJSON_AddObjectToObject( out, "stat_values" );
	cJSON_ArrayForEach( a, Get( Catalog(), "card_stats" ) ) {
		cJSON_AddStringToObject( sv, Str( a, "id" ).c_str(), StatValue( Str( a, "id" ), profile ).c_str() );
	}
	cJSON *enf = cJSON_AddArrayToObject( out, "enforce" ), *known = cJSON_AddArrayToObject( out, "known" );
	cJSON_ArrayForEach( a, cards ) {
		cJSON_AddItemToArray( enf, EnforceUnlocks( a, profile ) );
		cJSON_AddItemToArray( known, KnownParts( a ) );
	}
	return out;
}

// runs the vectors in memory: the saved career and campaign stay as they are
static void Vectors_f() {
	std::ifstream f( std::filesystem::u8path( OzE_Argv( 1 ) ), std::ios::binary );
	std::stringstream ss;
	ss << f.rdbuf();
	cJSON *v = cJSON_Parse( ss.str().c_str() );
	if ( !v ) {
		Printf( "usage: ozprogress_vectors <progress_vectors.json>\n" );
		return;
	}
	cJSON *savedProf = prof, *savedCamp = camp, *savedLast = lastResult;
	const std::vector<Tier> savedTiers = tiers;
	const int savedVersion = version;
	prof = camp = lastResult = nullptr;
	memoryOnly = true;
	SetArenas( Get( v, "maps" ) );
	int passed = 0, total = 0;
	const cJSON *c, *expectedCases = Get( v, "expected" );
	int ci = 0;
	cJSON_ArrayForEach( c, Get( v, "cases" ) ) {
		const cJSON *exp = cJSON_GetArrayItem( expectedCases, ci++ );
		cJSON_Delete( prof );
		cJSON_Delete( camp );
		prof = camp = nullptr;
		const cJSON *s;
		int si = 0;
		cJSON_ArrayForEach( s, Get( c, "steps" ) ) {
			const cJSON *want = cJSON_GetArrayItem( Get( exp, "steps" ), si );
			cJSON *got = nullptr;
			if ( Get( s, "skill" ) ) {
				SetNum( Campaign(), "skill", (double)Int( Get( s, "skill" ) ) );
				got = cJSON_CreateObject();
			} else if ( Get( s, "match" ) ) {
				got = MatchEnd( Get( s, "match" ) );
				cJSON_AddNumberToObject( got, "level", LevelFor( TotalXp() ).level );
				cJSON_AddItemToObject( got, "achieved", Sorted( Achieved( Profile() ) ) );
				cJSON_AddItemToObject( got, "campaign_won", cJSON_Duplicate( Get( Campaign(), "won" ), 1 ) );
				cJSON_AddItemToObject( got, "campaign_awards", cJSON_Duplicate( Get( Campaign(), "awards" ), 1 ) );
			} else if ( Get( s, "eval" ) ) {
				got = EvalProfile( Get( s, "eval" ), Get( s, "cards" ) );
			}
			std::string why;
			total++;
			if ( Same( got, want, Fmt( "%s step %d", Str( c, "name" ).c_str(), si ), why ) ) {
				passed++;
			} else {
				Printf( "^1vectors: %s\n", why.c_str() );
			}
			cJSON_Delete( got );
			si++;
		}
		auto list = [&]( const char *key, auto make ) {
			const cJSON *x;
			int i = 0;
			cJSON_ArrayForEach( x, Get( c, key ) ) {
				cJSON *got = make( x );
				std::string why;
				total++;
				if ( Same( got, cJSON_GetArrayItem( Get( exp, key ), i ), Fmt( "%s %s %d", Str( c, "name" ).c_str(), key, i ), why ) ) {
					passed++;
				} else {
					Printf( "^1vectors: %s\n", why.c_str() );
				}
				cJSON_Delete( got );
				i++;
			}
		};
		list( "levels", []( const cJSON *x ) {
			const LevelInfo l = LevelFor( Int( x ) );
			cJSON *o = cJSON_CreateObject();
			cJSON_AddNumberToObject( o, "level", l.level );
			cJSON_AddNumberToObject( o, "xp", (double)l.into );
			cJSON_AddNumberToObject( o, "next", (double)l.next );
			return o;
		} );
		list( "ranks", []( const cJSON *x ) { return Rank( Int( x ) ); } );
		list( "bots", []( const cJSON *x ) {
			return BotCard( cJSON_GetStringValue( cJSON_GetArrayItem( x, 0 ) ), cJSON_GetStringValue( cJSON_GetArrayItem( x, 1 ) ),
				(int)Int( cJSON_GetArrayItem( x, 2 ) ) );
		} );
	}
	cJSON_Delete( prof );
	cJSON_Delete( camp );
	cJSON_Delete( lastResult );
	cJSON_Delete( v );
	prof = savedProf;
	camp = savedCamp;
	lastResult = savedLast;
	tiers = savedTiers;
	version = savedVersion;
	memoryOnly = false;
	Changed();
	Printf( "vectors: %d of %d match the Godot build\n", passed, total );
}

static void Profile_f() {
	const LevelInfo l = LevelFor( TotalXp() );
	const std::set<std::string> done = Achieved( Profile() );
	Printf( "profile: level %d, %lld XP (%lld / %lld into the level), %d of %d achievements\n", l.level, TotalXp(), l.into, l.next,
		(int)done.size(), cJSON_GetArraySize( Achievements() ) );
	std::string ids;
	for ( const std::string &d : done ) {
		ids += " " + d;
	}
	Printf( "  achieved:%s\n", ids.c_str() );
	Printf( "  campaign: %d arenas won, skill %d\n", cJSON_GetArraySize( Get( Campaign(), "won" ) ), CampaignSkill() );
}

void Commands() {
	OzE_CvarCreate( "oz_profile_import", "1", 0, "1: a new career is first imported from the Godot build's user folder.", nullptr, nullptr, 1 );
	OzE_AddCommand( "ozprogress_vectors", Vectors_f );
	OzE_AddCommand( "ozprofile", Profile_f );
}

void CampaignStart( const std::string &map ) {
	const int tier = ArenaTier( map );
	if ( tier < 0 || !TierOpen( tier ) ) {
		PlaySound( Sound::Buzz );
		Printf( "campaign: %s %s\n", map.c_str(), tier < 0 ? "isn't a campaign arena" : "is locked" );
		return;
	}
	postgame.clear();
	OzE_CvarSet( "oz_campaign_map", map.c_str() );
	CloseAllPages();
	// ui_spLevel.c's start: spmap is the game's single player (g_gametype 2, no warmup, 8 clients), which
	// spawns the arena's bots at g_spSkill with its limits; tests play with cheats (oz_campaign_devmap 1)
	OzE_InsertText( Fmt( "set g_spSkill %d; %s %s\n", CampaignSkill(), atoi( OzE_CvarString( "oz_campaign_devmap" ) ) ? "spdevmap" : "spmap",
		map.c_str() ).c_str() );
}

} // namespace Prog
} // namespace OzUI

using namespace OzUI;
using namespace OzUI::Prog;

// ---------------------------------------------------------------- the C side

static std::string pendingPage;

static void Copy( char *dst, const char *src, size_t size ) {
	snprintf( dst, size, "%s", src ? src : "" );
}

namespace OzUI {
namespace Prog {
const char *TakePendingPage() {
	static std::string page;
	page = pendingPage;
	pendingPage.clear();
	return page.c_str();
}
}
}

static void Look( const cJSON *card, int level, ozCardLook_t *out ) {
	memset( out, 0, sizeof( *out ) );
	CardArt art = ArtOf( card );
	Copy( out->banner, art.banner.c_str(), sizeof( out->banner ) );
	out->bannerWide = art.bannerWide;
	Copy( out->emblem, art.emblem.c_str(), sizeof( out->emblem ) );
	Copy( out->title, Str( Part( "titles", Str( card, "title" ) ), "name" ).c_str(), sizeof( out->title ) );
	Copy( out->accent, art.accent.c_str(), sizeof( out->accent ) );
	Copy( out->frame, art.frameColor.c_str(), sizeof( out->frame ) );
	out->level = level;
}

extern "C" {

int OzProgress_LevelFor( int xp, int *into, int *next ) {
	const LevelInfo l = LevelFor( xp < 0 ? 0 : xp );
	if ( into ) {
		*into = (int)l.into;
	}
	if ( next ) {
		*next = (int)l.next;
	}
	return l.level;
}

int OzProgress_TotalXP( void ) {
	return (int)TotalXp();
}

void OzProgress_LocalCard( ozCardLook_t *out ) {
	cJSON *card = ShownCard();
	Look( card, LevelFor( TotalXp() ).level, out );
	cJSON_Delete( card );
}

void OzProgress_BotCard( const char *name, const char *model, int skill, ozCardLook_t *out ) {
	cJSON *card = BotCard( name, model, skill );
	Look( card, (int)Int( Get( card, "level" ) ), out );
	cJSON_Delete( card );
}

void OzProgress_CardLook( const char *cardJson, int level, ozCardLook_t *out ) {
	cJSON *card = cJSON_Parse( cardJson ? cardJson : "" );
	Look( card, level, out );
	cJSON_Delete( card );
}

int OzProgress_Showcase( const char *cardJson, const char *statsJson, char labels[][32], char values[][24], int max ) {
	cJSON *card = cJSON_Parse( cardJson ? cardJson : "" ), *stats = cJSON_Parse( statsJson ? statsJson : "" );
	const cJSON *id, *e;
	int n = 0;
	cJSON_ArrayForEach( id, Get( card, "stats" ) ) {
		if ( n >= max || !cJSON_IsString( id ) ) {
			continue;
		}
		cJSON_ArrayForEach( e, Get( Catalog(), "card_stats" ) ) {
			if ( Str( e, "id" ) == id->valuestring ) {
				Copy( labels[n], Str( e, "label" ).c_str(), 32 );
				Copy( values[n], Str( stats, id->valuestring, "-" ).c_str(), 24 );
				n++;
				break;
			}
		}
	}
	cJSON_Delete( card );
	cJSON_Delete( stats );
	return n;
}

void OzProgress_LocalCareer( int *matches, int *wins, int *frags, int *deaths, int *seconds ) {
	const cJSON *stats = Get( Profile(), "stats" );
	*matches = (int)Stat( stats, "matches" );
	*wins = (int)Stat( stats, "wins" );
	*frags = (int)Stat( stats, "frags" );
	*deaths = (int)Stat( stats, "deaths" );
	*seconds = (int)Stat( stats, "playtime" );
}

int OzProgress_CampaignMatch( void ) {
	char map[64];
	int gametype = 0, seconds, warmup, score, local;
	return OzE_MatchInfo( map, sizeof( map ), &gametype, &seconds, &warmup, &score, &local ) && local && gametype == 2
		&& !StrICmp( map, OzE_CvarString( "oz_campaign_map" ) );
}

void OzProgress_Postgame( const char *args ) {
	postgame = args ? args : "";
}

void OzProgress_MatchStarted( void ) {
	waitStart = 0;
	campaignResults = false;
	lastMovie.clear();
}

int OzProgress_MatchEnd( const ozMatchEnd_t *e, int demo, ozAward_t *out ) {
	const bool campaign = !demo && OzProgress_CampaignMatch();
	// the game sends its postgame in the frame the intermission starts; it may come a little after (up to
	// a second: counted in frames it was 60 ms at 1000 fps)
	if ( campaign && postgame.empty() ) {
		if ( !waitStart ) {
			waitStart = OzE_Milliseconds() | 1;
		}
		if ( OzE_Milliseconds() - waitStart < 1000 ) {
			return 0;
		}
	}
	cJSON *m = cJSON_CreateObject();
	cJSON_AddStringToObject( m, "map", e->map );
	cJSON_AddStringToObject( m, "mode", ModeName( e->gametype, e->map ) );
	cJSON_AddNumberToObject( m, "gametype", e->gametype );
	cJSON_AddBoolToObject( m, "campaign", campaign );
	cJSON *scores = cJSON_AddArrayToObject( m, "scores" );
	for ( int i = 0; i < e->numScores && i < OZ_MATCH_SCORES; i++ ) {
		cJSON_AddItemToArray( scores, cJSON_CreateNumber( e->scores[i] ) );
	}
	cJSON_AddNumberToObject( m, "place", e->place );
	cJSON_AddNumberToObject( m, "team", e->team );
	cJSON *ts = cJSON_AddArrayToObject( m, "team_scores" );
	cJSON_AddItemToArray( ts, cJSON_CreateNumber( e->teamScores[0] ) );
	cJSON_AddItemToArray( ts, cJSON_CreateNumber( e->teamScores[1] ) );
	cJSON *p = cJSON_AddObjectToObject( m, "pers" );
	cJSON_AddNumberToObject( p, "impressive", e->impressive );
	cJSON_AddNumberToObject( p, "excellent", e->excellent );
	cJSON_AddNumberToObject( p, "gauntlet", e->gauntlet );
	cJSON_AddNumberToObject( p, "defend", e->defend );
	cJSON_AddNumberToObject( p, "assist", e->assist );
	cJSON_AddNumberToObject( p, "captures", e->captures );
	cJSON_AddNumberToObject( p, "killed", e->killed );
	cJSON_AddStringToObject( m, "post", campaign ? postgame.c_str() : "" );
	cJSON_AddStringToObject( m, "model", e->model );
	cJSON_AddStringToObject( m, "skin", e->skin && e->skin[0] ? e->skin : "default" );
	cJSON_AddNumberToObject( m, "seconds", e->seconds );
	cJSON_AddNumberToObject( m, "speed", e->topSpeed );
	for ( const std::pair<const char *, const ozCounts_t *> c : { std::make_pair( "frags_weapon", &e->fragsWeapon ),
			std::make_pair( "pickups", &e->pickups ), std::make_pair( "deaths_by", &e->deathsBy ) } ) {
		cJSON *o = cJSON_AddObjectToObject( m, c.first );
		for ( int i = 0; i < c.second->num && i < OZ_MATCH_COUNTS; i++ ) {
			if ( c.second->n[i] > 0 ) {
				cJSON_AddNumberToObject( o, c.second->key[i], c.second->n[i] );
			}
		}
	}
	cJSON *rail = cJSON_AddObjectToObject( m, "rail" );
	cJSON_AddNumberToObject( rail, "streak", e->railStreak );
	cJSON_AddNumberToObject( rail, "shots", e->railShots );
	cJSON_AddNumberToObject( rail, "hits", e->railHits );
	postgame.clear();

	// a demo shows the XP the match gave without giving it again
	cJSON *saved = demo ? cJSON_Duplicate( Profile(), 1 ) : nullptr, *savedCamp = demo ? cJSON_Duplicate( Campaign(), 1 ) : nullptr;
	const bool wasMemory = memoryOnly;
	memoryOnly = memoryOnly || demo;
	cJSON *xp = MatchEnd( m );
	memoryOnly = wasMemory;
	if ( demo ) {
		cJSON_Delete( prof );
		cJSON_Delete( camp );
		prof = saved;
		camp = savedCamp;
	}

	memset( out, 0, sizeof( *out ) );
	const cJSON *l;
	cJSON_ArrayForEach( l, Get( xp, "lines" ) ) {
		if ( out->numLines < OZ_XP_LINES ) {
			Copy( out->lines[out->numLines], cJSON_GetStringValue( cJSON_GetArrayItem( l, 0 ) ), sizeof( out->lines[0] ) );
			out->amounts[out->numLines++] = (int)Int( cJSON_GetArrayItem( l, 1 ) );
		}
	}
	out->before = (int)Int( Get( xp, "before" ) );
	out->gain = (int)Int( Get( xp, "gain" ) );
	cJSON_ArrayForEach( l, Get( xp, "unlocked" ) ) {
		if ( out->numUnlocked < 4 ) {
			std::string kind = Str( l, "kind" );
			kind = Capitalize( kind.substr( 0, kind.size() - 1 ) );
			Copy( out->unlocked[out->numUnlocked++], ( kind + ": " + Str( l, "name" ) ).c_str(), sizeof( out->unlocked[0] ) );
		}
	}
	out->campaign = campaign;
	campaignResults = campaign;
	lastMovie = Str( xp, "movie" );
	OzE_Print( Fmt( "ozprogress: %s %s on %s, +%d XP (%d to %d)%s%s\n", campaign ? "campaign" : "match", cJSON_IsTrue( Get( xp, "won" ) ) ? "won" : "lost",
		e->map, out->gain, out->before, out->before + out->gain, lastMovie.empty() ? "" : ", movie ", lastMovie.c_str() ).c_str() );
	// the match's events and what they gave, for engine/tests/progress_test.py to replay through the Godot build's code
	for ( const cJSON *j : { (const cJSON *)m, (const cJSON *)xp } ) {
		char *text = cJSON_PrintUnformatted( j );
		OzE_Print( ( std::string( j == m ? "ozprogress events " : "ozprogress award " ) + ( text ? text : "{}" ) + "\n" ).c_str() );
		cJSON_free( text );
	}
	cJSON_Delete( xp );
	cJSON_Delete( m );
	return 1;
}

void OzProgress_CampaignContinue( void ) {
	if ( !campaignResults ) {
		return;
	}
	campaignResults = false;
	pendingPage = "campaign";
	OzE_CvarSet( "oz_campaign_map", "" );
	std::string cmd = "disconnect\n";
	if ( !lastMovie.empty() && atoi( OzE_CvarString( "oz_gameplay_tier_movies" ) ) ) {
		cmd += "cinematic " + lastMovie + ".RoQ\n";
	}
	lastMovie.clear();
	OzE_InsertText( cmd.c_str() );	// ahead of anything queued (a test script's waits)
}

const char *OzProgress_CareerJson( void ) {
	static std::string text;
	cJSON *m = cJSON_CreateObject();
	cJSON *shown = ShownCard();
	cJSON_AddStringToObject( m, "type", "career" );
	cJSON_AddNumberToObject( m, "xp", (double)TotalXp() );
	cJSON *stats = cJSON_CreateObject();
	const cJSON *id;
	cJSON_ArrayForEach( id, Get( shown, "stats" ) ) {
		if ( cJSON_IsString( id ) ) {
			cJSON_AddStringToObject( stats, id->valuestring, StatValue( id->valuestring, Profile() ).c_str() );
		}
	}
	cJSON_AddItemToObject( m, "card", shown );
	cJSON_AddItemToObject( m, "stats", stats );
	char *s = cJSON_PrintUnformatted( m );
	text = s ? s : "";
	cJSON_free( s );
	cJSON_Delete( m );
	return text.c_str();
}

int OzProgress_CareerVersion( void ) {
	return version;
}

} // extern "C"
