/*
===========================================================================
Oz Arena: Defrag's race maps, for Skirmish under Defrag (its mod picker). GPL-2.0.

A race map is a BSP with Defrag's timer entities: a target_startTimer and a target_stopTimer, between
which Defrag's own game times a run (the map editor's course tools place the same ones). They're
looked for where a switch to Defrag loads maps from: the Maps folder (fs_mapspath), and the baseq3
and defrag folders of the user folder, the base path and the Steam install. Skirmish runs under Oz's
game, which doesn't load the defrag folders, so the paks are read here, outside the file system:
each map's header, then its entities, shaders and models. q3map writes the entities near a BSP's end,
so a map in a pak is read through (an install's baseq3 holds about 1.2 GB of maps): a slice a frame
(OzRaces_Step), and what was learned is kept in <fs_homepath>/oz/race_maps.json by each pak's path,
size and time, so only new or changed paks are read again.

What a map tells of its course, for Skirmish's estimate of how hard it is (oz_ui_skirmish.cpp): its
checkpoints, the course's length (start, checkpoints nearest first, finish), the weapons lying on it
(rocket, plasma, grenade and BFG jumps), its teleports and hurt triggers, lava or slime, slick floors.
Its name, author and style come from Defrag's map info (scripts/<map>.defi) in any pak looked at,
Defrag's own included.
===========================================================================
*/
#include "../../../quake3e/code/client/client.h"
#include "../../../quake3e/code/qcommon/unzip.h"
#include "cJSON.h"
#include "oz_races.h"

#define CACHE_VERSION	1
#define MAX_SOURCES		4096
#define MAX_PAK_MAPS	512
#define MAX_RACES		2048
#define MAX_ENTS		4096
#define MAX_PLACES		64
#define CHUNK			65536
#define BSP_LUMPS		17
#define BSP_HEADER		( 8 + BSP_LUMPS * 8 )
#define MAX_LUMP		( 32 * 1024 * 1024 )

enum { L_ENTITIES = 0, L_SHADERS = 1, L_MODELS = 7 };
static const int wanted[3] = { L_ENTITIES, L_SHADERS, L_MODELS };

typedef struct {
	char		path[MAX_OSPATH];
	fileOffset_t size;
	fileTime_t	mtime;
	qboolean	pak;		// else a loose BSP
	qboolean	onPath;		// in a folder Oz's game loads (the Maps folder, baseq3): its level shots show
	qboolean	stale;		// new or changed since the last look: read it
} source_t;

static source_t	sources[MAX_SOURCES];
static int		numSources, nextSource;
static ozRacesState_t state;
static int		generation;
static int		toRead, numRead;
static int		workMsec;	// this look's time in OzRaces_Step
static double	bytesRead;	// this look's map bytes unpacked or read

static cJSON	*cache;		// race_maps.json: { "version", "sources": { <path>: { size, mtime, maps, defi, shots } } }
static cJSON	*fresh;		// this look's sources, the same shape

static ozRace_t	races[MAX_RACES];
static int		numRaces;

// the pak being read: its maps, then each map a chunk at a time
static struct {
	unzFile		z;
	cJSON		*entry;
	unsigned long maps[MAX_PAK_MAPS];	// the BSPs' places in the zip's directory
	int			numMaps, nextMap;
	qboolean	open;		// a BSP is open, partly read
	char		name[MAX_QPATH];
	int			offset, end;
	byte		header[BSP_HEADER];
	int			ofs[BSP_LUMPS], len[BSP_LUMPS];
	byte		*lump[3];	// wanted[]'s
} pak;

static byte chunk[CHUNK];


/*
==================
The course: what a race map's lumps tell
==================
*/
typedef struct {
	char		classname[48], targetname[64], target[64];
	int			model;		// a brush model's number ("*3"), else -1
	vec3_t		origin;
	qboolean	hasOrigin;
} ent_t;

static ent_t	ents[MAX_ENTS];
static int		numEnts;
static char		wsMessage[128], wsAuthor[64];

static void ParseEntities( const char *p, const char *end ) {
	char key[64], value[256];

	numEnts = 0;
	wsMessage[0] = wsAuthor[0] = '\0';
	while ( p < end && numEnts < MAX_ENTS ) {
		ent_t *e;
		while ( p < end && *p != '{' ) p++;
		if ( p >= end ) {
			break;
		}
		p++;
		e = &ents[numEnts];
		memset( e, 0, sizeof( *e ) );
		e->model = -1;
		for ( ;; ) {
			char *out;
			int n, k;
			// a quoted key, then a quoted value
			for ( k = 0; k < 2; k++ ) {
				while ( p < end && *p != '"' && *p != '}' ) p++;
				if ( p >= end || *p == '}' ) {
					break;
				}
				p++;
				out = k ? value : key;
				n = 0;
				while ( p < end && *p != '"' ) {
					if ( n < ( k ? (int)sizeof( value ) : (int)sizeof( key ) ) - 1 ) {
						out[n++] = *p;
					}
					p++;
				}
				out[n] = '\0';
				if ( p < end ) p++;
			}
			if ( k < 2 ) {
				break;
			}
			if ( !Q_stricmp( key, "classname" ) ) {
				Q_strncpyz( e->classname, value, sizeof( e->classname ) );
			} else if ( !Q_stricmp( key, "targetname" ) ) {
				Q_strncpyz( e->targetname, value, sizeof( e->targetname ) );
			} else if ( !Q_stricmp( key, "target" ) ) {
				Q_strncpyz( e->target, value, sizeof( e->target ) );
			} else if ( !Q_stricmp( key, "model" ) && value[0] == '*' ) {
				e->model = atoi( value + 1 );
			} else if ( !Q_stricmp( key, "origin" ) ) {
				e->hasOrigin = sscanf( value, "%f %f %f", &e->origin[0], &e->origin[1], &e->origin[2] ) == 3;
			} else if ( numEnts == 0 && !Q_stricmp( key, "message" ) ) {
				Q_strncpyz( wsMessage, value, sizeof( wsMessage ) );
			} else if ( numEnts == 0 && ( !Q_stricmp( key, "author" ) || !Q_stricmp( key, "_author" ) ) ) {
				Q_strncpyz( wsAuthor, value, sizeof( wsAuthor ) );
			}
		}
		while ( p < end && *p != '}' ) p++;
		if ( p < end ) p++;
		numEnts++;
	}
}

static int CountClass( const char *cls ) {
	int i, n = 0;
	for ( i = 0; i < numEnts; i++ ) {
		n += !Q_stricmp( ents[i].classname, cls );
	}
	return n;
}

/*
Where a timer entity is: the middle of each trigger that fires it (its brush model's box), else its own
origin
*/
static int Places( const char *cls, const byte *models, int numModels, vec3_t *out ) {
	int i, j, n = 0;

	for ( i = 0; i < numEnts && n < MAX_PLACES; i++ ) {
		const ent_t *t = &ents[i];
		if ( Q_stricmpn( t->classname, "trigger_", 8 ) || !t->target[0] || t->model < 0 || t->model >= numModels ) {
			continue;
		}
		for ( j = 0; j < numEnts; j++ ) {
			if ( !Q_stricmp( ents[j].classname, cls ) && !Q_stricmp( ents[j].targetname, t->target ) ) {
				float b[6];
				memcpy( b, models + t->model * 40, sizeof( b ) );	// dmodel_t: mins, maxs, then the surfaces' and brushes' ranges
				VectorSet( out[n], ( b[0] + b[3] ) * 0.5f, ( b[1] + b[4] ) * 0.5f, ( b[2] + b[5] ) * 0.5f );
				n++;
				break;
			}
		}
	}
	for ( i = 0; i < numEnts && !n; i++ ) {
		if ( !Q_stricmp( ents[i].classname, cls ) && ents[i].hasOrigin ) {
			VectorCopy( ents[i].origin, out[0] );
			n = 1;
		}
	}
	return n;
}

// a race map's course as JSON, NULL when the map has no start and finish
static cJSON *Course( const char *map ) {
	static vec3_t starts[MAX_PLACES], checks[MAX_PLACES], stops[MAX_PLACES];
	const byte *models = pak.lump[2];
	const int numModels = pak.len[L_MODELS] / 40;
	const byte *sh = pak.lump[1];
	int numStarts, numChecks, numStops, i, weapons = 0, lava = 0, slick = 0;
	qboolean used[MAX_PLACES];
	float length = 0.0f;
	vec3_t at;
	cJSON *c;

	if ( !CountClass( "target_startTimer" ) || !CountClass( "target_stopTimer" ) ) {
		return NULL;
	}
	numStarts = Places( "target_startTimer", models, numModels, starts );
	numChecks = Places( "target_checkpoint", models, numModels, checks );
	numStops = Places( "target_stopTimer", models, numModels, stops );
	if ( numStarts && numStops ) {
		// start, then the nearest checkpoint not passed yet, ... then the nearest finish
		VectorCopy( starts[0], at );
		memset( used, 0, sizeof( used ) );
		for ( ;; ) {
			int best = -1;
			float d = 0.0f;
			for ( i = 0; i < numChecks; i++ ) {
				const float di = Distance( at, checks[i] );
				if ( !used[i] && ( best < 0 || di < d ) ) {
					best = i;
					d = di;
				}
			}
			if ( best < 0 ) {
				break;
			}
			used[best] = qtrue;
			length += d;
			VectorCopy( checks[best], at );
		}
		{
			float d = Distance( at, stops[0] );
			for ( i = 1; i < numStops; i++ ) {
				d = MIN( d, Distance( at, stops[i] ) );
			}
			length += d;
		}
	}
	weapons |= CountClass( "weapon_rocketlauncher" ) ? OZ_RACE_ROCKET : 0;
	weapons |= CountClass( "weapon_plasmagun" ) ? OZ_RACE_PLASMA : 0;
	weapons |= CountClass( "weapon_grenadelauncher" ) ? OZ_RACE_GRENADE : 0;
	weapons |= CountClass( "weapon_bfg" ) ? OZ_RACE_BFG : 0;
	for ( i = 0; sh && i < pak.len[L_SHADERS] / 72; i++ ) {	// dshader_t: the name (64), surfaceFlags, contentFlags
		int flags[2];
		memcpy( flags, sh + i * 72 + 64, sizeof( flags ) );
		slick |= ( flags[0] & SURF_SLICK ) != 0;
		lava |= ( flags[1] & ( CONTENTS_LAVA | CONTENTS_SLIME ) ) != 0;
	}
	c = cJSON_CreateObject();
	cJSON_AddStringToObject( c, "map", map );
	cJSON_AddStringToObject( c, "message", wsMessage );
	cJSON_AddStringToObject( c, "author", wsAuthor );
	cJSON_AddNumberToObject( c, "checkpoints", numChecks );
	cJSON_AddNumberToObject( c, "length", (int)length );
	cJSON_AddNumberToObject( c, "weapons", weapons );
	cJSON_AddNumberToObject( c, "teleports", CountClass( "trigger_teleport" ) );
	cJSON_AddNumberToObject( c, "hurts", CountClass( "trigger_hurt" ) );
	cJSON_AddNumberToObject( c, "lava", lava );
	cJSON_AddNumberToObject( c, "slick", slick );
	return c;
}

/*
Defrag's map info (scripts/<map>.defi): blocks of a key and its value a line, as
	{
	map		"coldrun"
	longname	"Cold Run"
	style		"run"
	author		"thrasher"
	}
*/
static void ParseDefi( const char *p, cJSON *out ) {
	cJSON *cur = NULL;

	while ( *p ) {
		char key[32], value[256];
		int n = 0;
		while ( *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' ) p++;
		if ( !*p ) {
			break;
		}
		if ( *p == '{' || *p == '}' ) {
			if ( *p == '}' && cur ) {
				if ( cJSON_GetStringValue( cJSON_GetObjectItem( cur, "map" ) ) ) {
					cJSON_AddItemToArray( out, cur );
				} else {
					cJSON_Delete( cur );
				}
				cur = NULL;
			} else if ( *p == '{' && !cur ) {
				cur = cJSON_CreateObject();
			}
			p++;
			continue;
		}
		while ( *p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != '}' ) {
			if ( n < (int)sizeof( key ) - 1 ) key[n++] = *p;
			p++;
		}
		key[n] = '\0';
		while ( *p == ' ' || *p == '\t' ) p++;
		n = 0;
		if ( *p == '"' ) {
			p++;
			while ( *p && *p != '"' && *p != '\n' ) {
				if ( n < (int)sizeof( value ) - 1 ) value[n++] = *p;
				p++;
			}
			if ( *p == '"' ) p++;
		} else {
			while ( *p && *p != '\r' && *p != '\n' && *p != '}' ) {
				if ( n < (int)sizeof( value ) - 1 ) value[n++] = *p;
				p++;
			}
		}
		value[n] = '\0';
		if ( cur && ( !Q_stricmp( key, "map" ) || !Q_stricmp( key, "longname" ) || !Q_stricmp( key, "style" ) || !Q_stricmp( key, "author" ) ) ) {
			Q_strlwr( key );
			cJSON_DeleteItemFromObject( cur, key );
			cJSON_AddStringToObject( cur, key, value );
		}
	}
	cJSON_Delete( cur );	// an unclosed block
}


/*
==================
Reading a pak: its directory, then each map a chunk at a time
==================
*/
static cJSON *NewEntry( const source_t *s ) {
	cJSON *e = cJSON_CreateObject();
	cJSON_AddNumberToObject( e, "size", (double)s->size );
	cJSON_AddNumberToObject( e, "mtime", (double)s->mtime );
	cJSON_AddItemToObject( e, "maps", cJSON_CreateArray() );
	cJSON_AddItemToObject( e, "defi", cJSON_CreateArray() );
	cJSON_AddItemToObject( e, "shots", cJSON_CreateArray() );
	return e;
}

static void FreeLumps( void ) {
	int i;
	for ( i = 0; i < 3; i++ ) {
		if ( pak.lump[i] ) {
			Z_Free( pak.lump[i] );
			pak.lump[i] = NULL;
		}
	}
}

static void ClosePak( void ) {
	if ( pak.open ) {
		unzCloseCurrentFile( pak.z );
		pak.open = qfalse;
	}
	if ( pak.z ) {
		unzClose( pak.z );
		pak.z = NULL;
	}
	FreeLumps();
	if ( pak.entry ) {
		cJSON_Delete( pak.entry );
		pak.entry = NULL;
	}
}

// "maps/x.bsp" -> "x" (only straight under the folder, any case)
static qboolean Stem( const char *path, const char *folder, const char *ext, char *out, int size ) {
	const size_t f = strlen( folder ), n = strlen( path ), e = strlen( ext );
	if ( n <= f + e || Q_stricmpn( path, folder, f ) || Q_stricmp( path + n - e, ext ) || strchr( path + f, '/' ) ) {
		return qfalse;
	}
	Q_strncpyz( out, path + f, MIN( size, (int)( n - f - e + 1 ) ) );
	return qtrue;
}

static qboolean OpenPak( const source_t *s ) {
	int r;

	memset( &pak, 0, sizeof( pak ) );
	pak.entry = NewEntry( s );
	pak.z = unzOpen( s->path );
	if ( !pak.z ) {
		return qfalse;
	}
	for ( r = unzGoToFirstFile( pak.z ); r == UNZ_OK; r = unzGoToNextFile( pak.z ) ) {
		unz_file_info info;
		char name[MAX_OSPATH], stem[MAX_QPATH], *c;
		unzGetCurrentFileInfo( pak.z, &info, name, sizeof( name ), NULL, 0, NULL, 0 );
		for ( c = name; *c; c++ ) {
			*c = *c == '\\' ? '/' : *c;
		}
		if ( Stem( name, "maps/", ".bsp", stem, sizeof( stem ) ) ) {
			if ( pak.numMaps < MAX_PAK_MAPS ) {
				unzGetCurrentFileInfoPosition( pak.z, &pak.maps[pak.numMaps++] );
			}
		} else if ( Stem( name, "levelshots/", ".jpg", stem, sizeof( stem ) ) || Stem( name, "levelshots/", ".tga", stem, sizeof( stem ) ) ||
				Stem( name, "levelshots/", ".png", stem, sizeof( stem ) ) ) {
			cJSON_AddItemToArray( cJSON_GetObjectItem( pak.entry, "shots" ), cJSON_CreateString( Q_strlwr( stem ) ) );
		} else if ( Stem( name, "scripts/", ".defi", stem, sizeof( stem ) ) && info.uncompressed_size < 65536 && unzOpenCurrentFile( pak.z ) == UNZ_OK ) {
			char *text = Z_Malloc( info.uncompressed_size + 1 );
			const int n = unzReadCurrentFile( pak.z, text, info.uncompressed_size );
			unzCloseCurrentFile( pak.z );
			text[n > 0 ? n : 0] = '\0';
			ParseDefi( text, cJSON_GetObjectItem( pak.entry, "defi" ) );
			Z_Free( text );
		}
	}
	return qtrue;
}

// the next map's header: which lumps to keep, how far to read; qfalse when it isn't a map to read
static qboolean OpenMap( void ) {
	unz_file_info info;
	char name[MAX_OSPATH];
	int i;

	unzSetCurrentFileInfoPosition( pak.z, pak.maps[pak.nextMap] );
	if ( unzGetCurrentFileInfo( pak.z, &info, name, sizeof( name ), NULL, 0, NULL, 0 ) != UNZ_OK || unzOpenCurrentFile( pak.z ) != UNZ_OK ) {
		return qfalse;
	}
	pak.open = qtrue;
	COM_StripExtension( COM_SkipPath( name ), pak.name, sizeof( pak.name ) );
	if ( unzReadCurrentFile( pak.z, pak.header, BSP_HEADER ) != BSP_HEADER || memcmp( pak.header, "IBSP", 4 ) ) {
		return qfalse;
	}
	pak.end = BSP_HEADER;
	for ( i = 0; i < BSP_LUMPS; i++ ) {
		memcpy( &pak.ofs[i], pak.header + 8 + i * 8, 4 );
		memcpy( &pak.len[i], pak.header + 12 + i * 8, 4 );
	}
	for ( i = 0; i < 3; i++ ) {
		const int l = wanted[i];
		if ( pak.ofs[l] < BSP_HEADER || pak.len[l] < 0 || pak.len[l] > MAX_LUMP || pak.ofs[l] + pak.len[l] > (int)info.uncompressed_size ) {
			return qfalse;
		}
		pak.lump[i] = Z_Malloc( pak.len[l] + 1 );	// the entities end in a '\0' too
		pak.end = MAX( pak.end, pak.ofs[l] + pak.len[l] );
	}
	pak.offset = BSP_HEADER;
	return qtrue;
}

static void CloseMap( void ) {
	if ( pak.open ) {
		unzCloseCurrentFile( pak.z );
		pak.open = qfalse;
	}
	FreeLumps();
	pak.nextMap++;
}

// a chunk of the open map, into the lumps it covers
static qboolean ReadChunk( void ) {
	const int n = unzReadCurrentFile( pak.z, chunk, MIN( CHUNK, pak.end - pak.offset ) );
	int i;

	if ( n <= 0 ) {
		return qfalse;
	}
	for ( i = 0; i < 3; i++ ) {
		const int l = wanted[i];
		const int from = MAX( pak.offset, pak.ofs[l] ), to = MIN( pak.offset + n, pak.ofs[l] + pak.len[l] );
		if ( to > from ) {
			memcpy( pak.lump[i] + ( from - pak.ofs[l] ), chunk + ( from - pak.offset ), to - from );
		}
	}
	pak.offset += n;
	bytesRead += n;
	return qtrue;
}

static void MapRead( void ) {
	cJSON *c;
	pak.lump[0][pak.len[L_ENTITIES]] = '\0';
	ParseEntities( (const char *)pak.lump[0], (const char *)pak.lump[0] + pak.len[L_ENTITIES] );
	if ( ( c = Course( pak.name ) ) != NULL ) {
		cJSON_AddItemToArray( cJSON_GetObjectItem( pak.entry, "maps" ), c );
	}
}

// a slice of a pak: qtrue once it's all read (its entry is then in fresh)
static qboolean PakStep( const source_t *s, int until ) {
	if ( !pak.entry ) {
		if ( !OpenPak( s ) ) {
			pak.numMaps = 0;	// not a zip: nothing in it
		}
	}
	while ( pak.nextMap < pak.numMaps && Sys_Milliseconds() < until ) {
		if ( !pak.open && !OpenMap() ) {
			CloseMap();
			continue;
		}
		if ( pak.offset >= pak.end ) {
			MapRead();
			CloseMap();
		} else if ( !ReadChunk() ) {
			CloseMap();
		}
	}
	if ( pak.nextMap < pak.numMaps ) {
		return qfalse;
	}
	cJSON_AddItemToObject( cJSON_GetObjectItem( fresh, "sources" ), s->path, pak.entry );
	pak.entry = NULL;
	ClosePak();
	return qtrue;
}

// a loose BSP: its header and lumps straight from the file
static void LooseRead( const source_t *s ) {
	FILE *f = Sys_FOpen( s->path, "rb" );
	int i;

	memset( &pak, 0, sizeof( pak ) );
	pak.entry = NewEntry( s );
	COM_StripExtension( COM_SkipPath( (char *)s->path ), pak.name, sizeof( pak.name ) );
	if ( f && fread( pak.header, 1, BSP_HEADER, f ) == BSP_HEADER && !memcmp( pak.header, "IBSP", 4 ) ) {
		qboolean ok = qtrue;
		for ( i = 0; i < BSP_LUMPS; i++ ) {
			memcpy( &pak.ofs[i], pak.header + 8 + i * 8, 4 );
			memcpy( &pak.len[i], pak.header + 12 + i * 8, 4 );
		}
		for ( i = 0; i < 3 && ok; i++ ) {
			const int l = wanted[i];
			ok = pak.ofs[l] >= BSP_HEADER && pak.len[l] >= 0 && pak.len[l] <= MAX_LUMP && pak.ofs[l] + pak.len[l] <= s->size;
			if ( ok ) {
				pak.lump[i] = Z_Malloc( pak.len[l] + 1 );
				ok = fseek( f, pak.ofs[l], SEEK_SET ) == 0 && (int)fread( pak.lump[i], 1, pak.len[l], f ) == pak.len[l];
				bytesRead += pak.len[l];
			}
		}
		if ( ok ) {
			MapRead();
		}
	}
	if ( f ) {
		fclose( f );
	}
	FreeLumps();
	cJSON_AddItemToObject( cJSON_GetObjectItem( fresh, "sources" ), s->path, pak.entry );
	pak.entry = NULL;
}


/*
==================
The folders, the cache and the list
==================
*/
static const char *CachePath( void ) {
	return FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), "oz", "race_maps.json" );
}

static void LoadCache( void ) {
	FILE *f = Sys_FOpen( CachePath(), "rb" );
	long len;
	char *text;

	cJSON_Delete( cache );
	cache = NULL;
	if ( f ) {
		fseek( f, 0, SEEK_END );
		len = ftell( f );
		fseek( f, 0, SEEK_SET );
		text = Z_Malloc( len + 1 );
		text[fread( text, 1, len, f )] = '\0';
		fclose( f );
		cache = cJSON_Parse( text );
		Z_Free( text );
	}
	if ( !cJSON_IsObject( cache ) || cJSON_GetNumberValue( cJSON_GetObjectItem( cache, "version" ) ) != CACHE_VERSION ||
			!cJSON_IsObject( cJSON_GetObjectItem( cache, "sources" ) ) ) {
		cJSON_Delete( cache );
		cache = cJSON_CreateObject();
		cJSON_AddNumberToObject( cache, "version", CACHE_VERSION );
		cJSON_AddItemToObject( cache, "sources", cJSON_CreateObject() );
	}
}

static void SaveCache( void ) {
	const char *path = CachePath();
	char *text = cJSON_PrintUnformatted( fresh );
	FILE *f;

	FS_CreatePath( path );
	if ( text && ( f = Sys_FOpen( path, "wb" ) ) != NULL ) {
		fwrite( text, 1, strlen( text ), f );
		fclose( f );
	}
	cJSON_free( text );
}

static void AddSource( const char *dir, const char *name, qboolean isPak, qboolean onPath ) {
	source_t *s;
	fileTime_t ctime;
	int i;

	if ( numSources >= MAX_SOURCES ) {
		return;
	}
	s = &sources[numSources];
	Com_sprintf( s->path, sizeof( s->path ), "%s%c%s", dir, PATH_SEP, name );
	for ( i = 0; i < numSources; i++ ) {
		if ( !Q_stricmp( sources[i].path, s->path ) ) {
			return;	// a folder that's two of the roots
		}
	}
	if ( !Sys_GetFileStats( s->path, &s->size, &s->mtime, &ctime ) ) {
		return;
	}
	s->pak = isPak;
	s->onPath = onPath;
	numSources++;
}

static void AddFolder( const char *dir, qboolean onPath ) {
	char maps[MAX_OSPATH];
	char **list;
	int i, n;

	if ( !dir[0] ) {
		return;
	}
	list = Sys_ListFiles( dir, ".pk3", NULL, &n, qfalse );
	for ( i = 0; i < n; i++ ) {
		AddSource( dir, list[i], qtrue, onPath );
	}
	Sys_FreeFileList( list );
	Com_sprintf( maps, sizeof( maps ), "%s%cmaps", dir, PATH_SEP );
	list = Sys_ListFiles( maps, ".bsp", NULL, &n, qfalse );
	for ( i = 0; i < n; i++ ) {
		AddSource( maps, list[i], qfalse, onPath );
	}
	Sys_FreeFileList( list );
}

// every pak and loose map where Defrag would load maps; the ones the cache knows as they are need no reading
static void List( void ) {
	const char *roots[] = { Cvar_VariableString( "fs_homepath" ), Cvar_VariableString( "fs_basepath" ), Cvar_VariableString( "fs_steampath" ) };
	cJSON *known;
	int i;

	if ( !cache ) {
		LoadCache();
	}
	known = cJSON_GetObjectItem( cache, "sources" );
	numSources = nextSource = toRead = numRead = 0;
	AddFolder( FS_GetMapsPath(), qtrue );
	for ( i = 0; i < ARRAY_LEN( roots ); i++ ) {
		if ( roots[i][0] ) {
			AddFolder( va( "%s%c%s", roots[i], PATH_SEP, BASEGAME ), qtrue );
			AddFolder( va( "%s%cdefrag", roots[i], PATH_SEP ), qfalse );
		}
	}
	cJSON_Delete( fresh );
	fresh = cJSON_CreateObject();
	cJSON_AddNumberToObject( fresh, "version", CACHE_VERSION );
	cJSON_AddItemToObject( fresh, "sources", cJSON_CreateObject() );
	for ( i = 0; i < numSources; i++ ) {
		source_t *s = &sources[i];
		cJSON *e = cJSON_GetObjectItemCaseSensitive( known, s->path );
		s->stale = !e || cJSON_GetNumberValue( cJSON_GetObjectItem( e, "size" ) ) != (double)s->size ||
			cJSON_GetNumberValue( cJSON_GetObjectItem( e, "mtime" ) ) != (double)s->mtime;
		if ( s->stale ) {
			toRead++;
		} else {
			cJSON_AddItemToObject( cJSON_GetObjectItem( fresh, "sources" ), s->path, cJSON_DetachItemViaPointer( known, e ) );
		}
	}
	state = OZR_LOOKING;
}

// the list from fresh: each map once (the first folder's), named from any map info
static void Rebuild( void ) {
	cJSON *all = cJSON_GetObjectItem( fresh, "sources" );
	int i;

	numRaces = 0;
	for ( i = 0; i < numSources; i++ ) {
		const source_t *s = &sources[i];
		cJSON *e = cJSON_GetObjectItemCaseSensitive( all, s->path ), *m;
		for ( m = e ? cJSON_GetObjectItem( e, "maps" )->child : NULL; m && numRaces < MAX_RACES; m = m->next ) {
			const char *map = cJSON_GetStringValue( cJSON_GetObjectItem( m, "map" ) );
			ozRace_t *r = &races[numRaces];
			cJSON *shot, *src, *d;
			int j;
			if ( !map ) {
				continue;
			}
			for ( j = 0; j < numRaces && Q_stricmp( races[j].map, map ); j++ );
			if ( j < numRaces ) {
				continue;
			}
			memset( r, 0, sizeof( *r ) );
			Q_strncpyz( r->map, map, sizeof( r->map ) );
			Q_strncpyz( r->author, cJSON_GetStringValue( cJSON_GetObjectItem( m, "author" ) ), sizeof( r->author ) );
			Q_strncpyz( r->where, s->path, sizeof( r->where ) );
			r->checkpoints = (int)cJSON_GetNumberValue( cJSON_GetObjectItem( m, "checkpoints" ) );
			r->length = (int)cJSON_GetNumberValue( cJSON_GetObjectItem( m, "length" ) );
			r->weapons = (int)cJSON_GetNumberValue( cJSON_GetObjectItem( m, "weapons" ) );
			r->teleports = (int)cJSON_GetNumberValue( cJSON_GetObjectItem( m, "teleports" ) );
			r->hurts = (int)cJSON_GetNumberValue( cJSON_GetObjectItem( m, "hurts" ) );
			r->lava = (int)cJSON_GetNumberValue( cJSON_GetObjectItem( m, "lava" ) );
			r->slick = (int)cJSON_GetNumberValue( cJSON_GetObjectItem( m, "slick" ) );
			for ( shot = s->onPath ? cJSON_GetObjectItem( e, "shots" )->child : NULL; shot; shot = shot->next ) {
				if ( !Q_stricmp( cJSON_GetStringValue( shot ), map ) ) {
					Com_sprintf( r->shot, sizeof( r->shot ), "/levelshots/%s", map );
				}
			}
			// Defrag's map info, from whichever pak has it
			for ( src = all->child; src; src = src->next ) {
				for ( d = cJSON_GetObjectItem( src, "defi" )->child; d; d = d->next ) {
					const char *v;
					if ( Q_stricmp( cJSON_GetStringValue( cJSON_GetObjectItem( d, "map" ) ), map ) ) {
						continue;
					}
					if ( ( v = cJSON_GetStringValue( cJSON_GetObjectItem( d, "longname" ) ) ) != NULL && Q_stricmp( v, map ) ) {
						Q_strncpyz( r->name, v, sizeof( r->name ) );
					}
					if ( ( v = cJSON_GetStringValue( cJSON_GetObjectItem( d, "author" ) ) ) != NULL && *v ) {
						Q_strncpyz( r->author, v, sizeof( r->author ) );
					}
					if ( ( v = cJSON_GetStringValue( cJSON_GetObjectItem( d, "style" ) ) ) != NULL ) {
						Q_strncpyz( r->style, v, sizeof( r->style ) );
					}
				}
			}
			numRaces++;
		}
	}
	generation++;
}


/*
==================
OzRaces_Step
==================
*/
int OzRaces_Step( int msec ) {
	const int start = Sys_Milliseconds(), until = start + msec;

	if ( state == OZR_IDLE ) {
		workMsec = 0;
		bytesRead = 0;
		List();
		Rebuild();
	}
	while ( state == OZR_LOOKING && Sys_Milliseconds() < until ) {
		const source_t *s;
		if ( nextSource >= numSources ) {
			if ( toRead || cJSON_GetArraySize( cJSON_GetObjectItem( cache, "sources" ) ) ) {
				SaveCache();	// something was read, or a pak went away
			}
			cJSON_Delete( cache );
			cache = fresh;
			fresh = cJSON_Duplicate( cache, qtrue );	// Rebuild reads fresh
			state = OZR_DONE;
			workMsec += Sys_Milliseconds() - start;
			Com_Printf( "oz_races: %d race maps in %d paks and maps (%d read: %.0f MB of maps in %d ms, a slice a frame)\n",
				numRaces, numSources, toRead, bytesRead / ( 1024.0 * 1024.0 ), workMsec );
			return 0;
		}
		s = &sources[nextSource];
		if ( !s->stale ) {
			nextSource++;
			continue;
		}
		if ( s->pak ) {
			if ( !PakStep( s, until ) ) {
				break;
			}
		} else {
			LooseRead( s );
		}
		nextSource++;
		numRead++;
		Rebuild();
	}
	workMsec += Sys_Milliseconds() - start;
	return state == OZR_LOOKING;
}

void OzRaces_Again( void ) {
	if ( state == OZR_LOOKING ) {
		// what this look has so far goes back into the cache (the pak half read is read again)
		cJSON *from = cJSON_GetObjectItem( fresh, "sources" ), *to = cJSON_GetObjectItem( cache, "sources" ), *e;
		ClosePak();
		while ( from && ( e = from->child ) != NULL ) {
			cJSON_DetachItemViaPointer( from, e );
			cJSON_DeleteItemFromObjectCaseSensitive( to, e->string );
			cJSON_AddItemToObject( to, e->string, e );
		}
		cJSON_Delete( fresh );
		fresh = NULL;
	} else if ( fresh ) {
		cJSON_Delete( cache );
		cache = fresh;
		fresh = NULL;
	}
	state = OZR_IDLE;
}

ozRacesState_t OzRaces_State( int *done, int *total ) {
	if ( done ) {
		*done = numRead;
	}
	if ( total ) {
		*total = toRead;
	}
	return state;
}

int OzRaces_Generation( void ) {
	return generation;
}

int OzRaces_Count( void ) {
	return numRaces;
}

const ozRace_t *OzRaces_Get( int index ) {
	return index >= 0 && index < numRaces ? &races[index] : NULL;
}
