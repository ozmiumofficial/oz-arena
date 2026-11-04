/*
===========================================================================
Oz: a player's Quake III config, imported (q3import)

Oz's own game never runs the install's baseq3/q3config.cfg or autoexec.cfg (common.c
Com_RunsGameConfig): run whole, a config from 1999 brought half-size textures (r_picmip), an 85 fps
cap and id's binds over Oz's defaults. This reads them instead and takes what is the player's own:

  - the binds that differ from id's own (default.cfg in the install's pak0), and the script cvars
    they reach through vstr; Escape stays Oz's
  - the settings Settings shows (mouse, field of view, crosshair, HUD, gameplay, sound, name and
    character) whose value isn't id's 1.32 default: a value still at id's default was never the
    player's choice, so Oz's stays

Graphics, the frame rate, the window, the network and everything else stay as Oz has them.

The config is the first of baseq3/q3config.cfg (and the autoexec.cfg beside it) in the home, base
and Steam paths (the install), then ioquake3's user folder. The main menu offers the import once, on
Oz's first run (no oz/q3config.cfg when the game started); Settings' footer offers it any time.

  oz_q3import		auto: the first run offers it from the player's own user folder; 1: from any (tests); 0: never
  q3import [check]	imports; check prints what would come over and what is left out, changing nothing
===========================================================================
*/
#include "../../../quake3e/code/client/client.h"
#include "../../../quake3e/code/qcommon/unzip.h"
#include "oz_ui_engine.h"

#define MAX_ARGS		64
#define MAX_SETS		2048
#define MAX_BINDS		512
#define MAX_IMPORTS		512
#define MAX_KEPT		32

typedef enum { G_BINDS, G_MOUSE, G_VIEW, G_CROSSHAIR, G_HUD, G_EFFECTS, G_GAMEPLAY, G_SOUND, G_PLAYER, G_COUNT } group_t;
static const char *const groupNames[G_COUNT] = { "Key binds", "Mouse", "View", "Crosshair", "HUD", "Effects", "Gameplay",
	"Sound", "Player" };

typedef struct {
	const char	*q3;			// the old config's cvar
	const char	*idDefault;		// id's own default (1.32): a value still at it was never the player's
	const char	*target;		// the cvar Settings shows it with
	group_t		group;
	const char	*label;
	// NULL: a number kept in [min, max]; qfalse: nothing to take
	qboolean	(*convert)( const char *in, char *out, int outSize, char *shown, int shownSize );
	float		min, max;
} importCvar_t;

typedef enum { IT_BIND, IT_SCRIPT, IT_CVAR, IT_MODEL } itemKind_t;

typedef struct {
	itemKind_t	kind;
	group_t		group;
	int			key;
	char		name[MAX_QPATH];
	char		value[MAX_STRING_CHARS];
	char		shown[128];		// in the dialog's line for its group
} importItem_t;

typedef struct {
	const char	*name, *value;
} importSet_t;

static struct {
	char			folder[MAX_OSPATH];	// where the config is
	char			*text;				// the parsed tokens (Z_Malloc)
	importSet_t		sets[MAX_SETS];		// set, seta and bare cvar lines, the last of each name
	int				numSets;
	const char		*binds[MAX_KEYS];	// the config's binds by key ("" unbound)
	const char		*idBinds[MAX_KEYS];	// id's default.cfg's
	qboolean		haveIdBinds;
	char			*idText;
	importItem_t	items[MAX_IMPORTS];
	int				numItems;
	int				sameAsId;			// binds left as Oz has them because they were id's own
	char			kept[MAX_STRING_CHARS];	// what stays Oz's, for the dialog
	char			others[MAX_STRING_CHARS];	// the commands that aren't settings (exec, echo...), for check
} imp;

static cvar_t	*oz_q3import;
static qboolean	firstRun;		// no oz/q3config.cfg when the game started
static qboolean	offered;

/*
================
Reading the configs, as the engine would run them: commands end at a new line or a semicolon outside
quotes, // starts a comment. Tokens are copied out, each with its terminator, into one buffer.
================
*/
static char *ParseCommand( const char *p, char **pool, char **argv, int *argc ) {
	*argc = 0;
	for ( ;; ) {
		while ( *p == ' ' || *p == '\t' || *p == '\r' ) {
			p++;
		}
		if ( !*p ) {
			return (char *)p;
		}
		if ( *p == '\n' || *p == ';' ) {
			return (char *)p + 1;
		}
		if ( p[0] == '/' && p[1] == '/' ) {
			while ( *p && *p != '\n' ) {
				p++;
			}
			continue;
		}
		char *t = *pool;
		if ( *p == '"' ) {
			for ( p++; *p && *p != '"' && *p != '\n'; p++ ) {
				*(*pool)++ = *p;
			}
			if ( *p == '"' ) {
				p++;
			}
		} else {
			for ( ; *p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != ';' && *p != '"' && !( p[0] == '/' && p[1] == '/' ); p++ ) {
				*(*pool)++ = *p;
			}
		}
		*(*pool)++ = '\0';
		if ( *argc < MAX_ARGS ) {
			argv[(*argc)++] = t;
		}
	}
}

// the arguments from first on, joined by spaces, into the pool (bind x weapon 5)
static const char *Join( char **pool, char **argv, int first, int argc ) {
	char *t = *pool;
	int i;
	for ( i = first; i < argc; i++ ) {
		const size_t n = strlen( argv[i] );
		if ( i > first ) {
			*(*pool)++ = ' ';
		}
		memcpy( *pool, argv[i], n );
		*pool += n;
	}
	*(*pool)++ = '\0';
	return t;
}

static const importCvar_t *FindCvar( const char *name );

static const char *SetValue( const char *name ) {
	int i;
	for ( i = 0; i < imp.numSets; i++ ) {
		if ( !Q_stricmp( imp.sets[i].name, name ) ) {
			return imp.sets[i].value;
		}
	}
	return NULL;
}

static void AddSet( const char *name, const char *value ) {
	int i;
	for ( i = 0; i < imp.numSets; i++ ) {
		if ( !Q_stricmp( imp.sets[i].name, name ) ) {
			imp.sets[i].value = value;
			return;
		}
	}
	if ( imp.numSets < MAX_SETS ) {
		imp.sets[imp.numSets].name = name;
		imp.sets[imp.numSets++].value = value;
	}
}

static void Note( char *list, int size, const char *what ) {
	if ( !Q_stristr( list, va( "%s,", what ) ) && strlen( list ) + strlen( what ) + 3 < (size_t)size ) {
		Q_strcat( list, size, va( "%s, ", what ) );
	}
}

// the commands of one file: binds into binds, set lines into sets (or, with sets NULL, binds only)
static void ParseFile( const char *text, char **pool, const char **binds, qboolean sets ) {
	char *argv[MAX_ARGS];
	int argc, k;
	const char *p = text;

	while ( *p ) {
		p = ParseCommand( p, pool, argv, &argc );
		if ( !argc ) {
			continue;
		}
		if ( !Q_stricmp( argv[0], "bind" ) && argc >= 3 ) {
			if ( ( k = Key_StringToKeynum( argv[1] ) ) >= 0 && k < MAX_KEYS ) {
				binds[k] = Join( pool, argv, 2, argc );
			}
		} else if ( !Q_stricmp( argv[0], "unbind" ) && argc >= 2 ) {
			if ( ( k = Key_StringToKeynum( argv[1] ) ) >= 0 && k < MAX_KEYS ) {
				binds[k] = "";
			}
		} else if ( !Q_stricmp( argv[0], "unbindall" ) ) {
			for ( k = 0; k < MAX_KEYS; k++ ) {
				binds[k] = binds[k] ? "" : NULL;
			}
		} else if ( !sets ) {
			continue;
		} else if ( ( !Q_stricmp( argv[0], "seta" ) || !Q_stricmp( argv[0], "set" ) || !Q_stricmp( argv[0], "sets" ) ||
			!Q_stricmp( argv[0], "setu" ) ) && argc >= 3 ) {
			AddSet( argv[1], Join( pool, argv, 2, argc ) );
		} else if ( argc == 2 && ( FindCvar( argv[0] ) || Cvar_Flags( argv[0] ) != CVAR_NONEXISTENT ) ) {
			AddSet( argv[0], argv[1] );		// a bare "sensitivity 3", as autoexecs often have
		} else {
			Note( imp.others, sizeof( imp.others ), argv[0] );
		}
	}
}

static char *ReadOSFile( const char *path, int *length ) {
	FILE *f = Sys_FOpen( path, "rb" );
	char *text;
	long n;

	if ( !f ) {
		return NULL;
	}
	fseek( f, 0, SEEK_END );
	n = ftell( f );
	fseek( f, 0, SEEK_SET );
	if ( n < 0 || n > 4 * 1024 * 1024 ) {
		fclose( f );
		return NULL;
	}
	text = Z_Malloc( n + 1 );
	n = (long)fread( text, 1, n, f );
	text[n] = '\0';
	fclose( f );
	*length = (int)n;
	return text;
}

// id's default.cfg, from pak0 in the config's folder or the install's: its binds aren't the player's
static void ReadIdBinds( void ) {
	static const char *const bases[] = { "fs_homepath", "fs_basepath", "fs_steampath" };
	char folder[MAX_OSPATH];
	char *pool;
	int i;

	for ( i = 0; i < 4 && !imp.haveIdBinds; i++ ) {
		unz_file_info info;
		unzFile z;
		if ( i == 0 ) {
			Q_strncpyz( folder, imp.folder, sizeof( folder ) );
		} else {
			Com_sprintf( folder, sizeof( folder ), "%s/baseq3", Cvar_VariableString( bases[i - 1] ) );
		}
		if ( ( z = unzOpen( va( "%s/pak0.pk3", folder ) ) ) == NULL ) {
			continue;
		}
		if ( unzLocateFile( z, "default.cfg", 2 ) == UNZ_OK && unzGetCurrentFileInfo( z, &info, NULL, 0, NULL, 0, NULL, 0 ) == UNZ_OK &&
			info.uncompressed_size < 65536 && unzOpenCurrentFile( z ) == UNZ_OK ) {
			char *text = Z_Malloc( info.uncompressed_size + 1 );
			const int n = unzReadCurrentFile( z, text, info.uncompressed_size );
			unzCloseCurrentFile( z );
			if ( n > 0 ) {
				text[n] = '\0';
				imp.idText = pool = Z_Malloc( n * 2 + 64 );
				ParseFile( text, &pool, imp.idBinds, qfalse );
				imp.haveIdBinds = qtrue;
			}
			Z_Free( text );
		}
		unzClose( z );
	}
}

/*
================
The settings that come over, and how. Ranges are Settings' (engine/ui/settings.json).
================
*/
static qboolean Bool( const char *in, char *out, int outSize, char *shown, int shownSize ) {
	Q_strncpyz( out, atof( in ) ? "1" : "0", outSize );
	Q_strncpyz( shown, atof( in ) ? "on" : "off", shownSize );
	return qtrue;
}

static qboolean Percent( const char *in, char *out, int outSize, char *shown, int shownSize ) {
	const float v = Com_Clamp( 0, 1, atof( in ) );
	Com_sprintf( out, outSize, "%g", v );
	Com_sprintf( shown, shownSize, "%d%%", (int)( v * 100 + 0.5f ) );
	return qtrue;
}

static qboolean Pitch( const char *in, char *out, int outSize, char *shown, int shownSize ) {
	const float v = atof( in );
	if ( !v ) {
		return qfalse;
	}
	Com_sprintf( out, outSize, "%g", v );
	if ( fabs( fabs( v ) - 0.022f ) < 0.0001f ) {
		Q_strncpyz( shown, v < 0 ? "inverted" : "not inverted", shownSize );
	} else {
		Com_sprintf( shown, shownSize, "pitch %g%s", v, v < 0 ? " (inverted)" : "" );
	}
	return qtrue;
}

// cg_drawCrosshair: 0 none, else id's crosshair a to j (cg_draw.c: the number mod 10)
static qboolean CrosshairShape( const char *in, char *out, int outSize, char *shown, int shownSize ) {
	const int n = atoi( in );
	if ( n <= 0 ) {
		Q_strncpyz( out, "none", outSize );
		Q_strncpyz( shown, "none", shownSize );
	} else {
		Com_sprintf( out, outSize, "%c", 'a' + n % 10 );
		Com_sprintf( shown, shownSize, "Classic %c", 'A' + n % 10 );
	}
	return qtrue;
}

static qboolean DrawGun( const char *in, char *out, int outSize, char *shown, int shownSize ) {
	if ( atoi( in ) ) {
		return qfalse;	// drawn: Oz's own place for it stays
	}
	Q_strncpyz( out, "hidden", outSize );
	Q_strncpyz( shown, "gun hidden", shownSize );
	return qtrue;
}

static qboolean Autoswitch( const char *in, char *out, int outSize, char *shown, int shownSize ) {
	if ( atoi( in ) ) {
		return qfalse;
	}
	Q_strncpyz( out, "never", outSize );
	Q_strncpyz( shown, "no weapon autoswitch", shownSize );
	return qtrue;
}

static qboolean Character( const char *model, const char *skin ) {
	return FS_ReadFile( va( "models/players/%s/icon_%s.tga", model, skin ), NULL ) > 0;
}

// cg_forceModel 1 drew everyone as your own character: Oz's forced model is that character
static qboolean ForceModel( const char *in, char *out, int outSize, char *shown, int shownSize ) {
	const char *model = SetValue( "model" ) ? SetValue( "model" ) : Cvar_VariableString( "model" );
	char m[MAX_QPATH], *s;

	if ( !atoi( in ) ) {
		return qfalse;
	}
	Q_strncpyz( m, model, sizeof( m ) );
	if ( ( s = strchr( m, '/' ) ) != NULL ) {
		*s = '\0';
	}
	if ( !m[0] || !Character( m, "default" ) ) {
		return qfalse;
	}
	Q_strncpyz( out, m, outSize );
	Com_sprintf( shown, shownSize, "everyone as %s", m );
	return qtrue;
}

static const importCvar_t cvars[] = {
	{ "sensitivity", "5", "sensitivity", G_MOUSE, "sensitivity", NULL, 0.1f, 30 },
	{ "m_pitch", "0.022", "m_pitch", G_MOUSE, "pitch", Pitch },
	{ "m_yaw", "0.022", "m_yaw", G_MOUSE, "yaw", NULL, 0.001f, 1 },
	{ "cl_mouseAccel", "0", "cl_mouseAccel", G_MOUSE, "acceleration", NULL, 0, 1 },
	{ "cg_fov", "90", "cg_fov", G_VIEW, "field of view", NULL, 60, 130 },
	{ "cg_zoomfov", "22.5", "cg_zoomfov", G_VIEW, "zoom field of view", NULL, 1, 160 },
	{ "cg_drawGun", "1", "oz_gameplay_draw_gun", G_VIEW, "gun", DrawGun },
	{ "cg_drawCrosshair", "4", "oz_crosshair_shape", G_CROSSHAIR, "shape", CrosshairShape },
	{ "cg_crosshairSize", "24", "cg_crosshairSize", G_CROSSHAIR, "size", NULL, 8, 96 },
	{ "cg_crosshairHealth", "1", "oz_crosshair_health", G_CROSSHAIR, "health color", Bool },
	{ "cg_drawFPS", "0", "cg_drawFPS", G_HUD, "frame counter", Bool },
	{ "cg_lagometer", "1", "cg_lagometer", G_HUD, "net graph", Bool },
	{ "con_notifytime", "3", "con_notifytime", G_HUD, "chat time", NULL, 3, 30 },
	{ "cg_teamChatsOnly", "0", "cg_teamChatsOnly", G_HUD, "team chats only", Bool },
	{ "cg_marks", "1", "cg_marks", G_EFFECTS, "decals", Bool },
	{ "cg_gibs", "1", "cg_gibs", G_EFFECTS, "gibs", Bool },
	{ "cg_autoswitch", "1", "oz_gameplay_autoswitch", G_GAMEPLAY, "autoswitch", Autoswitch },
	{ "cg_forceModel", "0", "oz_gameplay_force_model", G_GAMEPLAY, "forced model", ForceModel },
	{ "s_volume", "0.8", "s_volume", G_SOUND, "volume", Percent },
	{ "s_musicvolume", "0.25", "s_musicVolume", G_SOUND, "music", Percent },
	{ "name", "UnnamedPlayer", "name", G_PLAYER, "name", NULL },
	{ "model", "sarge", "model", G_PLAYER, "character", NULL },	// IT_MODEL: with its skin
};

// left as Oz has them; the dialog names those the config had otherwise
static const struct { const char *q3, *label; } keep[] = {
	{ "r_picmip", "texture quality" }, { "com_maxfps", "frame rate cap" }, { "r_mode", "window size" },
	{ "r_fullscreen", "full screen" }, { "r_gamma", "brightness" }, { "r_textureMode", "texture filtering" },
	{ "r_vertexLight", "lighting" }, { "r_overBrightBits", "lighting" }, { "r_mapOverBrightBits", "lighting" },
	{ "r_intensity", "lighting" }, { "r_lodbias", "model detail" }, { "r_subdivisions", "curve detail" },
	{ "r_colorbits", "color depth" }, { "r_texturebits", "color depth" }, { "r_dynamiclight", "dynamic lights" },
	{ "r_fastsky", "sky" }, { "r_ext_compressed_textures", "texture compression" }, { "cg_shadows", "shadows" },
	{ "cg_simpleItems", "items" }, { "rate", "network" }, { "snaps", "network" }, { "cl_maxpackets", "network" },
	{ "cl_timeNudge", "network" }, { "com_hunkMegs", "memory" }, { "com_zoneMegs", "memory" },
	{ "cl_renderer", "renderer" }, { "r_swapInterval", "vsync" },
};

static const importCvar_t *FindCvar( const char *name ) {
	int i;
	for ( i = 0; i < ARRAY_LEN( cvars ); i++ ) {
		if ( !Q_stricmp( cvars[i].q3, name ) ) {
			return &cvars[i];
		}
	}
	return NULL;
}

static qboolean SameValue( const char *a, const char *b ) {
	char *end;
	const double x = strtod( a, &end );
	if ( a[0] && !*end ) {
		const double y = strtod( b, &end );
		if ( b[0] && !*end ) {
			return fabs( x - y ) < 1e-6;
		}
	}
	return !Q_stricmp( a, b );
}

static char *Trim( char *s ) {
	char *e;
	while ( *s == ' ' || *s == '\t' ) {
		s++;
	}
	for ( e = s + strlen( s ); e > s && ( e[-1] == ' ' || e[-1] == '\t' || e[-1] == ';' ); e-- ) {
	}
	*e = '\0';
	return s;
}

// a bind's command, as compared: no spaces at the ends, any case
static qboolean SameCommand( const char *a, const char *b ) {
	char x[MAX_STRING_CHARS], y[MAX_STRING_CHARS];
	Q_strncpyz( x, a, sizeof( x ) );
	Q_strncpyz( y, b, sizeof( y ) );
	return !Q_stricmp( Trim( x ), Trim( y ) );
}

static importItem_t *AddItem( itemKind_t kind, group_t group, const char *name, const char *value, const char *shown ) {
	importItem_t *it;
	if ( imp.numItems >= MAX_IMPORTS ) {
		return NULL;
	}
	it = &imp.items[imp.numItems++];
	it->kind = kind;
	it->group = group;
	it->key = -1;
	Q_strncpyz( it->name, name, sizeof( it->name ) );
	Q_strncpyz( it->value, value, sizeof( it->value ) );
	Q_strncpyz( it->shown, shown, sizeof( it->shown ) );
	return it;
}

// the scripts a bind runs: every name after vstr, set or seta, and theirs in turn
static void AddScripts( const char *text, int depth ) {
	char *argv[MAX_ARGS], *pool, *buf;
	const char *p = text;
	int argc, i;

	if ( depth > 8 ) {
		return;
	}
	buf = pool = Z_Malloc( strlen( text ) * 2 + 64 );
	while ( *p ) {
		p = ParseCommand( p, &pool, argv, &argc );
		for ( i = 0; i + 1 < argc; i++ ) {
			const char *value;
			int n;
			if ( Q_stricmp( argv[i], "vstr" ) && Q_stricmp( argv[i], "set" ) && Q_stricmp( argv[i], "seta" ) ) {
				continue;
			}
			// the player's own cvars only: an engine's or a game's is a setting, not a script
			if ( ( value = SetValue( argv[i + 1] ) ) == NULL || FindCvar( argv[i + 1] ) ||
				( Cvar_Flags( argv[i + 1] ) != CVAR_NONEXISTENT && !( Cvar_Flags( argv[i + 1] ) & CVAR_USER_CREATED ) ) ) {
				continue;
			}
			for ( n = 0; n < imp.numItems; n++ ) {
				if ( imp.items[n].kind == IT_SCRIPT && !Q_stricmp( imp.items[n].name, argv[i + 1] ) ) {
					break;
				}
			}
			if ( n == imp.numItems && AddItem( IT_SCRIPT, G_BINDS, argv[i + 1], value, "" ) ) {
				AddScripts( value, depth + 1 );
			}
		}
	}
	Z_Free( buf );
}

static const char *Folder( int i, char *buf, int size ) {
	const char *bases[] = { "fs_homepath", "fs_basepath", "fs_steampath" };
	if ( i < 3 ) {
		const char *base = Cvar_VariableString( bases[i] );
		if ( !base[0] ) {
			return NULL;
		}
		Com_sprintf( buf, size, "%s/baseq3", base );
		return buf;
	}
#ifdef _WIN32
	if ( i == 3 && getenv( "APPDATA" ) ) {
		Com_sprintf( buf, size, "%s/Quake3/baseq3", getenv( "APPDATA" ) );	// ioquake3's user folder
		return buf;
	}
#endif
	return NULL;
}

static void Clear( void ) {
	if ( imp.text ) {
		Z_Free( imp.text );
	}
	if ( imp.idText ) {
		Z_Free( imp.idText );
	}
	Com_Memset( &imp, 0, sizeof( imp ) );
}

/*
================
OzQ3Import_Plan

Reads the config and works out what comes over. The number of things to import; 0 without a config
or with nothing in it that isn't id's or Oz's already.
================
*/
int OzQ3Import_Plan( void ) {
	char *files[2] = { NULL, NULL }, *pool, folder[MAX_OSPATH];
	int lengths[2] = { 0, 0 }, i, k;
	const char *names[2] = { Q3CONFIG_CFG, "autoexec.cfg" };

	Clear();
	for ( i = 0; i < 5 && !files[0] && !files[1]; i++ ) {
		if ( !Folder( i, folder, sizeof( folder ) ) ) {
			continue;
		}
		for ( k = 0; k < 2; k++ ) {
			files[k] = ReadOSFile( va( "%s/%s", folder, names[k] ), &lengths[k] );
		}
		Q_strncpyz( imp.folder, folder, sizeof( imp.folder ) );
	}
	if ( !files[0] && !files[1] ) {
		imp.folder[0] = '\0';
		return 0;
	}
	imp.text = pool = Z_Malloc( ( lengths[0] + lengths[1] ) * 2 + 64 );
	for ( k = 0; k < 2; k++ ) {
		if ( files[k] ) {
			ParseFile( files[k], &pool, imp.binds, qtrue );	// autoexec.cfg after, as it runs
			Z_Free( files[k] );
		}
	}
	ReadIdBinds();

	// binds: the player's own, over Oz's
	for ( k = 0; k < MAX_KEYS; k++ ) {
		const char *cmd = imp.binds[k], *now;
		if ( !cmd || !cmd[0] || k == K_ESCAPE ) {
			continue;
		}
		if ( imp.haveIdBinds && imp.idBinds[k] && SameCommand( imp.idBinds[k], cmd ) ) {
			imp.sameAsId++;
			continue;
		}
		now = Key_GetBinding( k );
		if ( now && SameCommand( now, cmd ) ) {
			continue;
		}
		if ( imp.numItems < MAX_IMPORTS ) {
			importItem_t *it = AddItem( IT_BIND, G_BINDS, Key_KeynumToString( k ), cmd, "" );
			it->key = k;
		}
	}
	for ( i = 0, k = imp.numItems; i < k; i++ ) {
		AddScripts( imp.items[i].value, 0 );
	}

	// settings: those that aren't id's default
	for ( i = 0; i < ARRAY_LEN( cvars ); i++ ) {
		const importCvar_t *c = &cvars[i];
		const char *v = SetValue( c->q3 );
		char out[MAX_CVAR_VALUE_STRING], shown[128];
		if ( !v || SameValue( v, c->idDefault ) ) {
			continue;
		}
		if ( c->convert ) {
			if ( !c->convert( v, out, sizeof( out ), shown, sizeof( shown ) ) ) {
				continue;
			}
		} else if ( !Q_stricmp( c->q3, "model" ) ) {
			char m[MAX_QPATH], *s;
			Q_strncpyz( m, v, sizeof( m ) );
			if ( ( s = strchr( m, '/' ) ) != NULL ) {
				*s++ = '\0';
			}
			if ( !Character( m, "default" ) ) {
				continue;	// a character Oz doesn't have
			}
			if ( s && ( !s[0] || !Q_stricmp( s, "default" ) || !Character( m, s ) ) ) {
				s = NULL;
			}
			Com_sprintf( out, sizeof( out ), s ? "%s/%s" : "%s", m, s );
			Com_sprintf( shown, sizeof( shown ), s ? "%s (%s)" : "%s", m, s );
			if ( !SameValue( out, Cvar_VariableString( "model" ) ) ) {
				AddItem( IT_MODEL, c->group, c->target, out, shown );
			}
			continue;
		} else if ( c->min < c->max ) {
			Com_sprintf( out, sizeof( out ), "%g", Com_Clamp( c->min, c->max, atof( v ) ) );
			Com_sprintf( shown, sizeof( shown ), "%s %s", c->label, out );
		} else {
			char clean[MAX_CVAR_VALUE_STRING];
			Q_strncpyz( out, v, sizeof( out ) );
			Q_strncpyz( clean, v, sizeof( clean ) );
			Com_sprintf( shown, sizeof( shown ), "%s %s", c->label, Q_CleanStr( clean ) );	// the name without its colors
		}
		if ( c->convert && c->convert != Pitch && c->convert != CrosshairShape && c->convert != DrawGun &&
			c->convert != Autoswitch && c->convert != ForceModel ) {
			Q_strncpyz( shown, va( "%s %s", c->label, shown ), sizeof( shown ) );	// "frame counter on"
		}
		if ( !SameValue( out, Cvar_VariableString( c->target ) ) ) {
			AddItem( IT_CVAR, c->group, c->target, out, shown );
		}
	}

	// what stays Oz's
	for ( i = 0; i < ARRAY_LEN( keep ); i++ ) {
		const char *v = SetValue( keep[i].q3 );
		if ( v && Cvar_Flags( keep[i].q3 ) != CVAR_NONEXISTENT && !SameValue( v, Cvar_VariableString( keep[i].q3 ) ) ) {
			Note( imp.kept, sizeof( imp.kept ), keep[i].label );
		}
	}
	for ( i = 0; i < 2; i++ ) {
		char *list = i ? imp.others : imp.kept;
		k = (int)strlen( list );
		if ( k >= 2 ) {
			list[k - 2] = '\0';		// the last ", "
		}
	}
	return imp.numItems;
}

const char *OzQ3Import_From( void ) {
	return imp.folder;
}

const char *OzQ3Import_Kept( void ) {
	return imp.kept;
}

/*
================
OzQ3Import_Summary

The dialog's lines, a group each: "Mouse", "sensitivity 3.5, inverted". 0 past the last.
================
*/
int OzQ3Import_Summary( int line, char *name, int nameSize, char *text, int textSize ) {
	int g, i, n = 0;

	for ( g = 0; g < G_COUNT; g++ ) {
		int binds = 0, scripts = 0;
		text[0] = '\0';
		for ( i = 0; i < imp.numItems; i++ ) {
			const importItem_t *it = &imp.items[i];
			if ( it->group != g ) {
				continue;
			}
			if ( it->kind == IT_BIND ) {
				binds++;
			} else if ( it->kind == IT_SCRIPT ) {
				scripts++;
			} else {
				if ( text[0] ) {
					Q_strcat( text, textSize, ", " );
				}
				Q_strcat( text, textSize, it->shown );
			}
		}
		if ( binds ) {
			Com_sprintf( text, textSize, binds == 1 ? "1 key of yours" : "%d keys of yours", binds );
			if ( scripts ) {
				Q_strcat( text, textSize, va( scripts == 1 ? ", with the script it runs" : ", with the %d scripts they run", scripts ) );
			}
		}
		if ( text[0] && n++ == line ) {
			Q_strncpyz( name, groupNames[g], nameSize );
			return 1;
		}
	}
	return 0;
}

/*
================
OzQ3Import_Apply

Imports what the plan holds; the number of binds and settings that came over.
================
*/
int OzQ3Import_Apply( void ) {
	int i;

	for ( i = 0; i < imp.numItems; i++ ) {
		const importItem_t *it = &imp.items[i];
		switch ( it->kind ) {
		case IT_BIND:
			Key_SetBinding( it->key, it->value );
			break;
		case IT_SCRIPT:
			Cvar_Get( it->name, "", CVAR_ARCHIVE );
			Cvar_Set( it->name, it->value );
			break;
		case IT_MODEL: {
			// as the Profile page sets a character (oz_ui_profile.cpp SetModel)
			const char *skin = strchr( it->value, '/' );
			Cvar_Set( "model", it->value );
			Cvar_Set( "headmodel", it->value );
			Cvar_Set( "oz_profile_skin", skin ? skin + 1 : "default" );
			break;
		}
		case IT_CVAR:
			if ( Cvar_Flags( it->name ) == CVAR_NONEXISTENT ) {
				Cvar_Get( it->name, it->value, CVAR_ARCHIVE );	// a cgame's, made before the cgame is
			}
			Cvar_Set( it->name, it->value );
			break;
		}
	}
	Com_Printf( "q3import: %d binds and settings from %s\n", imp.numItems, imp.folder );
	return imp.numItems;
}

// the main menu's offer, once: Oz's first run, a config with something of the player's in it
int OzQ3Import_Offer( void ) {
	if ( offered || !firstRun ) {
		return 0;
	}
	offered = qtrue;
	if ( !Q_stricmp( oz_q3import->string, "0" ) || ( Q_stricmp( oz_q3import->string, "1" ) && !OzE_IsPlayerHome() ) ) {
		return 0;
	}
	return OzQ3Import_Plan() > 0;
}

static void Q3Import_f( void ) {
	const qboolean check = !Q_stricmp( Cmd_Argv( 1 ), "check" );
	char name[64], text[MAX_STRING_CHARS];
	int i;

	if ( !OzQ3Import_Plan() ) {
		Com_Printf( imp.folder[0] ? "q3import: nothing of yours in %s that Oz doesn't have already\n" :
			"q3import: no Quake III config found\n", imp.folder );
		return;
	}
	if ( !check ) {
		OzQ3Import_Apply();
		return;
	}
	Com_Printf( "q3import: from %s (id's binds %s)\n", imp.folder, imp.haveIdBinds ? "read from pak0" : "not found: every bind counts" );
	for ( i = 0; i < imp.numItems; i++ ) {
		const importItem_t *it = &imp.items[i];
		Com_Printf( "  %s %s \"%s\"\n", it->kind == IT_BIND ? "bind" : it->kind == IT_SCRIPT ? "script" : "set",
			it->name, it->value );
	}
	for ( i = 0; OzQ3Import_Summary( i, name, sizeof( name ), text, sizeof( text ) ); i++ ) {
		Com_Printf( "  summary: %s: %s\n", name, text );
	}
	Com_Printf( "  id's binds left as Oz has them: %d\n", imp.sameAsId );
	Com_Printf( "  kept as Oz has them: %s\n", imp.kept[0] ? imp.kept : "(nothing different)" );
	if ( imp.others[0] ) {
		Com_Printf( "  other commands left out: %s\n", imp.others );
	}
}

// at CL_Init (OzUI_Init), before the first frame writes oz/q3config.cfg
void OzQ3Import_Init( void ) {
	static qboolean started;
	if ( started ) {
		return;
	}
	started = qtrue;
	firstRun = !Q_stricmp( FS_GetCurrentGameDir(), "oz" ) && !FS_SV_FileExists( "oz/" Q3CONFIG_CFG );
	oz_q3import = Cvar_Get( "oz_q3import", "auto", 0 );
	Cvar_SetDescription( oz_q3import, "auto: Oz's first run offers to import the Quake III config from the player's own user folder; 1: from any; 0: never." );
	Cmd_AddCommand( "q3import", Q3Import_f );
}
