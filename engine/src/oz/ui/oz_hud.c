/*
===========================================================================
Oz Arena: the HUD (ENGINE_PLAN.md P6)

A port of the Godot build's in-game HUD (scenes/gameplay/hud: q3_hud.gd, hud_style.gd and the
hud_*.gd widgets) and its scoreboard and results (scoreboard.gd, results_screen.gd,
q3_match_stats.gd). Layout, sizes and colors are the Godot build's, in 1080p units (u). Settings
come from the cvars the native Settings page sets (oz_hud_*, oz_crosshair_*, cg_lagometer,
cg_drawFPS, con_notifyTime, cg_teamChatsOnly).

It runs in the engine, not in a cgame (Ozmium, 2026-10-29: every server gets the same HUD, Oz's
own game, baseq3, CPMA, Defrag and other mods, pure servers included). It reads what any cgame
reads: the snapshot's playerState and entities, the configstrings, the events and the server
commands (baseq3's protocol 68 numbering, which those mods share). While it draws, the running
cgame's own 2D is dropped (cl_cgame.c), except while a mod's own menu has the keys; the cgame keeps
running underneath, so its sounds, countdowns and announcer play as before. It draws through the
HUD canvas (oz_ui_hud.cpp), which every renderer offers; where it can't start, and with cl_ozHud 0,
the cgame's own HUD draws.

It is also the stats recorder (ENGINE_PLAN.md P8b step 5): its match state, written out at the
intermission once the final scoreboard is in (RecordMatch, to oz_stats.c), and the career's counts
the Godot build never recorded (frags by weapon, pickups, deaths by cause, the rail's run of hits).
===========================================================================
*/
#include "../../../quake3e/code/client/client.h"
#include "../../../quake3e/code/game/bg_public.h"
#include "../../../quake3e/code/game/g_public.h"	// oz_goto: a player's place on the local server
#include "oz_emoji.h"
#include "oz_hud.h"
#include "oz_hud_items.h"
#include "oz_progress.h"
#include "oz_ui_engine.h"
#ifdef USE_OZ_ONLINE
#include "../online/oz_online.h"
#include "../online/oz_stats.h"
#endif

#define MOD_COUNT	32		// the means of death the match record counts by (bg_public.h meansOfDeath_t, MOD_GRAPPLE last)

#define numHudItems		ARRAY_LEN( hudItems )

#define FONT_NUM		0		// Barlow SemiBold, tabular figures
#define FONT_MEDIUM		1		// Barlow Medium
#define FONT_LABEL		2		// Quake 3's own font (traced from pak0)

#define ALIGN_LEFT		OZ_HUD_ALIGN_LEFT
#define ALIGN_CENTER	OZ_HUD_ALIGN_CENTER
#define ALIGN_RIGHT		OZ_HUD_ALIGN_RIGHT
#define TEXT_NOCODES	OZ_HUD_TEXT_NOCODES
#define TEXT_MEASURE	OZ_HUD_TEXT_MEASURE
#define TEXT_STEPPED	OZ_HUD_TEXT_STEPPED
#define TEXT_WARM		OZ_HUD_TEXT_WARM

// ================================================================ the match, as a cgame would see it

typedef struct {
	int		client, score, ping, time, scoreFlags, powerUps, accuracy;
	int		impressiveCount, excellentCount, guantletCount, defendCount, assistCount, captures, perfect, team;
} ozScore_t;	// cg_local.h's ozScore_t, from the "scores" command

typedef struct {
	qboolean	infoValid;
	int			team, botSkill;
	char		modelName[MAX_QPATH], skinName[MAX_QPATH];
} ozClientInfo_t;

static struct {
	int				time;				// the client's game time (cg.time)
	int				frametime;
	clSnapshot_t	*snap;
	int				weaponSelect;
	qboolean		showScores, scoreBoardShowing, demoPlayback;
	int				numScores;
	ozScore_t		scores[MAX_CLIENTS];
	int				scoresRequestTime;
	int				scoresParsed;		// scoreboards parsed so far (the recorder waits for one after the intermission)
	qboolean		scoresFull;			// the last was baseq3's: accuracy, medals and time (not CPMA's)
	int				warmup;
	char			centerPrint[1024];
	int				centerPrintTime;
	int				crosshairClientNum, crosshairClientTime;
	int				itemPickup, itemPickupTime, itemPickupBlendTime;
	int				lowAmmoWarning;
	char			killerName[MAX_QPATH];
	refdef_t		refdef;				// the last world view cgame rendered
	qboolean		refdefValid;
	struct { vec3_t velocity; } predictedPlayerState;
} cg;

static struct {
	int				gametype, timelimit, fraglimit, capturelimit, levelStartTime;
	int				scores1, scores2, redflag, blueflag;
	int				voteTime, voteYes, voteNo;
	char			voteString[MAX_STRING_CHARS];
	ozClientInfo_t	clientinfo[MAX_CLIENTS];
	struct { int w, h; } glconfig;
} cgs;

static const char *CS( int index ) {
	if ( index < 0 || index >= MAX_CONFIGSTRINGS || !cl.gameState.stringOffsets[index] ) {
		return "";
	}
	return cl.gameState.stringData + cl.gameState.stringOffsets[index];
}

// whether the snapshot is your own play (not following someone, not spectating): what the match record counts
static qboolean OwnPlay( void ) {
	return cg.snap && !( cg.snap->ps.pm_flags & PMF_FOLLOW ) && cg.snap->ps.persistant[PERS_TEAM] != TEAM_SPECTATOR;
}

static const ozHudItem_t *FindItemForPowerup( int pw ) {
	int	i;

	for ( i = 1; i < (int)numHudItems; i++ ) {
		if ( ( hudItems[i].type == IT_POWERUP || hudItems[i].type == IT_TEAM ) && hudItems[i].tag == pw ) {
			return &hudItems[i];
		}
	}
	return NULL;
}

static const entityState_t *SnapEntity( int i ) {
	return &cl.parseEntities[( cl.snap.parseEntitiesNum + i ) & ( MAX_PARSE_ENTITIES - 1 )];
}

// where an entity is in the current snapshot (no interpolation: close enough for a damage number)
static qboolean EntityOrigin( int number, vec3_t out ) {
	int	i;

	for ( i = 0; i < cl.snap.numEntities; i++ ) {
		const entityState_t *es = SnapEntity( i );
		if ( es->number == number ) {
			VectorCopy( es->pos.trBase, out );
			return qtrue;
		}
	}
	if ( number == cl.snap.ps.clientNum ) {
		VectorCopy( cl.snap.ps.origin, out );
		return qtrue;
	}
	return qfalse;
}

#define MAX_HUD_VERTS	3000
#define MAX_HUD_INDEXES	( MAX_HUD_VERTS * 5 )	// a strip's points share their corners: 4 vertices, 18 indexes a segment

static ozHudVertex_t	hverts[MAX_HUD_VERTS];
static int		numHVerts;
static int		hindexes[MAX_HUD_INDEXES];
static int		numHIndexes;
static int		hvertImage;
static float	hudAlpha = 1.0f;		// Settings > HUD > Opacity, over everything but the crosshair
// where the triangles go instead of the HUD canvas (a crosshair preview's, OzHud_CrosshairPreview)
static void		( *hudSink )( const ozHudVertex_t *verts, int numVerts, int image, void *ctx );
static void		*hudSinkCtx;

static void Flush( void ) {
	if ( numHIndexes ) {
		if ( hudSink ) {
			// the previews' sink takes a triangle list
			static ozHudVertex_t	tris[MAX_HUD_INDEXES];
			int						i;
			for ( i = 0; i < numHIndexes; i++ ) {
				tris[i] = hverts[hindexes[i]];
			}
			hudSink( tris, numHIndexes, hvertImage, hudSinkCtx );
		} else {
			OzUI_HudMesh( hverts, numHVerts, hindexes, numHIndexes, hvertImage );
		}
	}
	numHVerts = 0;
	numHIndexes = 0;
}

static void UseImageIndexed( int image, int verts, int indexes ) {
	if ( image != hvertImage || numHVerts + verts > MAX_HUD_VERTS || numHIndexes + indexes > MAX_HUD_INDEXES ) {
		Flush();
		hvertImage = image;
	}
}

// room for verts vertices of triangles, each its own (Vert indexes them in order)
static void UseImage( int image, int verts ) {
	UseImageIndexed( image, verts, verts );
}

static byte ToByte( float v ) {
	if ( v <= 0.0f ) {
		return 0;
	}
	if ( v >= 1.0f ) {
		return 255;
	}
	return (byte)( v * 255.0f + 0.5f );
}

// a corner shared by indexes (Strip's), not drawn by itself
static void Corner( float x, float y, float s, float t, const float *c, float a ) {
	ozHudVertex_t	*v = &hverts[numHVerts++];

	v->xy[0] = x;
	v->xy[1] = y;
	v->st[0] = s;
	v->st[1] = t;
	v->color[0] = ToByte( c[0] );
	v->color[1] = ToByte( c[1] );
	v->color[2] = ToByte( c[2] );
	v->color[3] = ToByte( c[3] * a * hudAlpha );
}

// a triangle list's next vertex
static void Vert( float x, float y, float s, float t, const float *c, float a ) {
	hindexes[numHIndexes++] = numHVerts;
	Corner( x, y, s, t, c, a );
}

static void Tri( const float *a, const float *ca, const float *b, const float *cb, const float *c, const float *cc, float fa, float fb, float fc ) {
	UseImage( 0, 3 );
	Vert( a[0], a[1], 0, 0, ca, fa );
	Vert( b[0], b[1], 0, 0, cb, fb );
	Vert( c[0], c[1], 0, 0, cc, fc );
}

static void Normalize2( float *v ) {
	float	l = (float)sqrt( v[0] * v[0] + v[1] * v[1] );

	if ( l > 0.00001f ) {
		v[0] /= l;
		v[1] /= l;
	}
}

#define FEATHER		1.0f	// pixels of soft edge, as Godot's antialiased lines and polygons

// a convex polygon (x, y pairs, either winding) with a soft edge
#define MAX_POLY	96
static void Poly( const float *pts, int n, const float *color ) {
	float	out[MAX_POLY][2];
	float	area = 0.0f;
	int		i;

	if ( n < 3 || n > MAX_POLY || color[3] <= 0.0f ) {
		return;
	}
	for ( i = 0; i < n; i++ ) {
		const float *p = &pts[i * 2];
		const float *q = &pts[( ( i + 1 ) % n ) * 2];
		area += p[0] * q[1] - q[0] * p[1];
	}
	for ( i = 0; i < n; i++ ) {
		const float *p0 = &pts[( ( i + n - 1 ) % n ) * 2];
		const float *p1 = &pts[i * 2];
		const float *p2 = &pts[( ( i + 1 ) % n ) * 2];
		float	n1[2], n2[2], m[2], d;
		n1[0] = p1[1] - p0[1]; n1[1] = p0[0] - p1[0];
		n2[0] = p2[1] - p1[1]; n2[1] = p1[0] - p2[0];
		if ( area > 0.0f ) {
			n1[0] = -n1[0]; n1[1] = -n1[1];
			n2[0] = -n2[0]; n2[1] = -n2[1];
		}
		Normalize2( n1 );
		Normalize2( n2 );
		m[0] = n1[0] + n2[0];
		m[1] = n1[1] + n2[1];
		Normalize2( m );
		d = m[0] * n1[0] + m[1] * n1[1];
		if ( d < 0.3f ) {
			d = 0.3f;
		}
		out[i][0] = p1[0] + m[0] * FEATHER / d;
		out[i][1] = p1[1] + m[1] * FEATHER / d;
	}
	for ( i = 1; i < n - 1; i++ ) {
		Tri( &pts[0], color, &pts[i * 2], color, &pts[( i + 1 ) * 2], color, 1, 1, 1 );
	}
	for ( i = 0; i < n; i++ ) {
		const int j = ( i + 1 ) % n;
		Tri( &pts[i * 2], color, &pts[j * 2], color, out[j], color, 1, 1, 0 );
		Tri( &pts[i * 2], color, out[j], color, out[i], color, 1, 0, 0 );
	}
}

// a quad with a color at each corner (gradients), no soft edge
static void Quad4( const float *a, const float *b, const float *c, const float *d, const float *ca, const float *cb, const float *cc, const float *cd ) {
	Tri( a, ca, b, cb, c, cc, 1, 1, 1 );
	Tri( a, ca, c, cc, d, cd, 1, 1, 1 );
}

static void Rect( float x, float y, float w, float h, const float *color ) {
	float	a[2], b[2], c[2], d[2];

	if ( w <= 0 || h <= 0 || color[3] <= 0 ) {
		return;
	}
	a[0] = x; a[1] = y;
	b[0] = x + w; b[1] = y;
	c[0] = x + w; c[1] = y + h;
	d[0] = x; d[1] = y + h;
	Quad4( a, b, c, d, color, color, color, color );
}

// a stroke along points (x, y pairs) of the given width, with soft sides; closed joins the ends
#define MAX_STRIP	130
static void Strip( const float *pts, int n, float width, const float *color, qboolean closed ) {
	float	in[MAX_STRIP][2][2], out[MAX_STRIP][2][2];
	float	hw = width * 0.5f;
	int		i, segs, base;

	if ( n < 2 || n > MAX_STRIP || color[3] <= 0.0f ) {
		return;
	}
	if ( hw < 0.5f ) {
		// thinner than a pixel: fainter instead
		hw = 0.5f;
	}
	for ( i = 0; i < n; i++ ) {
		const float	*p = &pts[i * 2];
		float		d[2], nrm[2];
		int			a = i - 1, b = i + 1;
		if ( closed ) {
			a = ( i + n - 1 ) % n;
			b = ( i + 1 ) % n;
		}
		if ( a < 0 ) {
			a = i;
		}
		if ( b >= n ) {
			b = i;
		}
		d[0] = pts[b * 2] - pts[a * 2];
		d[1] = pts[b * 2 + 1] - pts[a * 2 + 1];
		Normalize2( d );
		nrm[0] = -d[1];
		nrm[1] = d[0];
		in[i][0][0] = p[0] + nrm[0] * hw;	in[i][0][1] = p[1] + nrm[1] * hw;
		in[i][1][0] = p[0] - nrm[0] * hw;	in[i][1][1] = p[1] - nrm[1] * hw;
		out[i][0][0] = p[0] + nrm[0] * ( hw + FEATHER );	out[i][0][1] = p[1] + nrm[1] * ( hw + FEATHER );
		out[i][1][0] = p[0] - nrm[0] * ( hw + FEATHER );	out[i][1][1] = p[1] - nrm[1] * ( hw + FEATHER );
	}
	segs = closed ? n : n - 1;
	// each point's four corners once (outer, inner, inner, outer), and each segment's six triangles over them:
	// the core, then the feather on either side, as Tri drew them a vertex each
	UseImageIndexed( 0, n * 4, segs * 18 );
	base = numHVerts;
	for ( i = 0; i < n; i++ ) {
		Corner( out[i][0][0], out[i][0][1], 0, 0, color, 0 );
		Corner( in[i][0][0], in[i][0][1], 0, 0, color, 1 );
		Corner( in[i][1][0], in[i][1][1], 0, 0, color, 1 );
		Corner( out[i][1][0], out[i][1][1], 0, 0, color, 0 );
	}
	for ( i = 0; i < segs; i++ ) {
		const int a = base + i * 4, b = base + ( ( i + 1 ) % n ) * 4;
		const int tris[18] = { a + 1, b + 1, b + 2,  a + 1, b + 2, a + 2,  a, b, b + 1,  a, b + 1, a + 1,
							   a + 2, b + 2, b + 3,  a + 2, b + 3, a + 3 };
		Com_Memcpy( hindexes + numHIndexes, tris, sizeof( tris ) );
		numHIndexes += 18;
	}
}

static void Line( float x0, float y0, float x1, float y1, float width, const float *color ) {
	float	p[4];

	p[0] = x0; p[1] = y0; p[2] = x1; p[3] = y1;
	Strip( p, 2, width, color, qfalse );
}

// screen angles: 0 is right, growing clockwise (y down), as Godot's draw_arc
static void StrokeArc( float cx, float cy, float r, float a0, float a1, int segs, const float *color, float width ) {
	float	p[MAX_STRIP * 2];
	int		i;

	if ( segs < 1 ) {
		segs = 1;
	}
	if ( segs > MAX_STRIP - 1 ) {
		segs = MAX_STRIP - 1;
	}
	for ( i = 0; i <= segs; i++ ) {
		const float a = a0 + ( a1 - a0 ) * i / segs;
		p[i * 2] = cx + (float)cos( a ) * r;
		p[i * 2 + 1] = cy + (float)sin( a ) * r;
	}
	Strip( p, segs + 1, width, color, qfalse );
}

static void Disc( float cx, float cy, float r, const float *color ) {
	float	p[48 * 2];
	int		i;

	for ( i = 0; i < 48; i++ ) {
		p[i * 2] = cx + (float)cos( i * M_PI * 2 / 48 ) * r;
		p[i * 2 + 1] = cy + (float)sin( i * M_PI * 2 / 48 ) * r;
	}
	Poly( p, 48, color );
}

static void Image( int image, float x, float y, float w, float h, const float *color ) {
	if ( !image || w <= 0 || h <= 0 || color[3] <= 0 ) {
		return;
	}
	UseImage( image, 6 );
	Vert( x, y, 0, 0, color, 1 );
	Vert( x + w, y, 1, 0, color, 1 );
	Vert( x + w, y + h, 1, 1, color, 1 );
	Vert( x, y, 0, 0, color, 1 );
	Vert( x + w, y + h, 1, 1, color, 1 );
	Vert( x, y + h, 0, 1, color, 1 );
	UseImage( 0, 0 );
}

static int ImageNamed( const char *name ) {
	return name && name[0] ? OzUI_HudImage( name ) : 0;
}

// text: y is the baseline, x its left end (or middle, or right end); returns the width. A text whose size
// animates (a pop) passes its steady size and the pop as scale, so it keeps its face (oz_ui_hud.cpp)
static float TextScaled( int font, float x, float y, const char *s, float size, float scale, const float *color, int flags,
		float outline, float maxWidth ) {
	float	rgba[4];

	if ( !s || !s[0] ) {
		return 0;
	}
	if ( !( flags & TEXT_MEASURE ) ) {
		if ( color[3] <= 0.0f ) {
			return 0;
		}
		Flush();
	}
	rgba[0] = color[0];
	rgba[1] = color[1];
	rgba[2] = color[2];
	rgba[3] = color[3] * hudAlpha;
	return OzUI_HudText( s, x, y, font, (float)(int)size, scale, flags, rgba, outline, maxWidth );
}

static float TextFlags( int font, float x, float y, const char *s, float size, const float *color, int flags, float outline, float maxWidth ) {
	return TextScaled( font, x, y, s, size, 1.0f, color, flags, outline, maxWidth );
}

// hud_style.gd text(): no color codes, a soft dark outline
static float Text( int font, float x, float y, const char *s, float size, const float *color, int align ) {
	return TextFlags( font, x, y, s, size, color, align | TEXT_NOCODES, 0.35f, 0 );
}

// whether a text shows black letters: a color code that picks black (id's ColorIndex keeps 3 bits: ^0,
// ^8, ^p, ^x ...) with something but spaces after it
static qboolean HasBlack( const char *s ) {
	qboolean black = qfalse;

	for ( ; *s; s++ ) {
		if ( s[0] == '^' && s[1] && s[1] != '^' ) {
			black = ( ( s[1] - '0' ) & 7 ) == 0;
			s++;
		} else if ( black && *s != ' ' ) {
			return qtrue;
		}
	}
	return qfalse;
}

// a text with black letters (a player's name, mostly) would vanish on the HUD's dark panels and the
// night skies: a soft grey panel goes behind it (Ozmium, 2026-10-31); y is the text's baseline
static void BlackBacking( int font, float x, float y, const char *s, float size, float scale, const float *color, int align,
		float maxWidth ) {
	if ( s && color[3] > 0.0f && HasBlack( s ) ) {
		const float	w = TextScaled( font, 0, 0, s, size, scale, color, TEXT_MEASURE, 0, maxWidth );
		const float	padX = size * scale * 0.22f, top = size * scale * 0.86f, bottom = size * scale * 0.26f;
		const float	left = ( align & 3 ) == ALIGN_CENTER ? x - w * 0.5f : ( align & 3 ) == ALIGN_RIGHT ? x - w : x;
		vec4_t		grey = { 0.62f, 0.62f, 0.62f, 0.5f };

		grey[3] *= color[3];
		Rect( left - padX, y - top, w + 2 * padX, top + bottom, grey );
	}
}

// hud_style.gd ctext(): Quake 3's color codes
static float CTextScaled( int font, float x, float y, const char *s, float size, float scale, const float *color, int align,
		float maxWidth ) {
	BlackBacking( font, x, y, s, size, scale, color, align, maxWidth );
	return TextScaled( font, x, y, s, size, scale, color, align, 0.35f, maxWidth );
}

static float CText( int font, float x, float y, const char *s, float size, const float *color, int align, float maxWidth ) {
	return CTextScaled( font, x, y, s, size, 1.0f, color, align, maxWidth );
}

static float TextWidth( int font, const char *s, float size ) {
	static const vec4_t white = { 1, 1, 1, 1 };
	return TextFlags( font, 0, 0, s, size, white, TEXT_MEASURE | TEXT_NOCODES, 0, 0 );
}

static float CTextWidth( int font, const char *s, float size ) {
	static const vec4_t white = { 1, 1, 1, 1 };
	return TextFlags( font, 0, 0, s, size, white, TEXT_MEASURE, 0, 0 );
}

// ================================================================ the style (hud_style.gd)

static const vec4_t INK = { 0.902f, 0.867f, 0.824f, 1 };		// #e6ddd2
static const vec4_t INK_HOT = { 1.0f, 0.953f, 0.902f, 1 };		// #fff3e6
static const vec4_t DIM = { 0.553f, 0.518f, 0.486f, 1 };		// #8d847c
static const vec4_t FAINT = { 0.365f, 0.333f, 0.310f, 1 };		// #5d554f
static const vec4_t RED = { 0.859f, 0.141f, 0.063f, 1 };		// #db2410
static const vec4_t GLOW = { 1.0f, 0.290f, 0.110f, 1 };		// #ff4a1c
static const vec4_t AMBER = { 1.0f, 0.678f, 0.2f, 1 };			// #ffad33
static const vec4_t GLASS = { 0.04f, 0.03f, 0.027f, 0.62f };
static const vec4_t GLASS_EDGE = { 1.0f, 0.92f, 0.85f, 0.08f };
static const vec4_t MEGA = { 0.624f, 0.847f, 1.0f, 1 };		// #9fd8ff
static const vec4_t HUD_TEAM_RED = { 1.0f, 0.28f, 0.2f, 1 };
static const vec4_t HUD_TEAM_BLUE = { 0.32f, 0.55f, 1.0f, 1 };
static const vec4_t WHITE = { 1, 1, 1, 1 };
static const vec4_t BLACK = { 0, 0, 0, 1 };
#define SKEW	0.26f		// panel slant: horizontal shift per unit of height

static float	u = 1.0f;		// pixels per HUD unit: 1080p = 1, times Settings' HUD scale

static void Col( float *out, const float *c, float a ) {
	out[0] = c[0];
	out[1] = c[1];
	out[2] = c[2];
	out[3] = c[3] * a;
}

static void Mix( float *out, const float *a, const float *b, float t ) {
	int	i;

	if ( t < 0 ) {
		t = 0;
	} else if ( t > 1 ) {
		t = 1;
	}
	for ( i = 0; i < 4; i++ ) {
		out[i] = a[i] + ( b[i] - a[i] ) * t;
	}
}

static float Clamp01( float v ) {
	return v < 0 ? 0 : v > 1 ? 1 : v;
}

static float MoveToward( float v, float to, float step ) {
	if ( v < to ) {
		return v + step > to ? to : v + step;
	}
	return v - step < to ? to : v - step;
}

// a slanted bar segment from f0 to f1 (fractions of the width) in the box
static void Bar( float x, float y, float w, float h, float f0, float f1, const float *color ) {
	float	s = h * SKEW, x0, x1, p[8];

	if ( f1 <= f0 ) {
		return;
	}
	x0 = x + f0 * ( w - s );
	x1 = x + f1 * ( w - s );
	p[0] = x0 + s; p[1] = y;
	p[2] = x1 + s; p[3] = y;
	p[4] = x1; p[5] = y + h;
	p[6] = x0; p[7] = y + h;
	Poly( p, 4, color );
}

// a slanted panel (parallelogram leaning right), with an accent strip along its bottom
static void Panel( float x, float y, float w, float h, const float *color, const float *accent, float accentW ) {
	float	s = h * SKEW, p[8];

	p[0] = x + s; p[1] = y;
	p[2] = x + w; p[3] = y;
	p[4] = x + w - s; p[5] = y + h;
	p[6] = x; p[7] = y + h;
	Poly( p, 4, color );
	Strip( p, 4, 1.0f, GLASS_EDGE, qtrue );
	if ( accent && accentW > 0 ) {
		const float a = accentW * SKEW;
		Bar( x, y + h - accentW, w - s + a, accentW, 0.0f, 0.35f, accent );
	}
}

static void CrossIcon( float x, float y, float w, float h, const float *color ) {
	const float t = w * 0.3f;
	Rect( x + w * 0.5f - t * 0.5f, y, t, h, color );
	Rect( x, y + h * 0.5f - t * 0.5f, w, t, color );
}

static void ShieldIcon( float x, float y, float w, float h, const float *color ) {
	static const float shape[12] = { 0.5f, 0, 1, 0.16f, 0.94f, 0.58f, 0.5f, 1, 0.06f, 0.58f, 0, 0.16f };
	static const vec4_t inner = { 0, 0, 0, 0.35f };
	float	p[12], q[12];
	int		i;

	for ( i = 0; i < 6; i++ ) {
		p[i * 2] = x + shape[i * 2] * w;
		p[i * 2 + 1] = y + shape[i * 2 + 1] * h;
		q[i * 2] = x + w * 0.5f + ( p[i * 2] - x - w * 0.5f ) * 0.55f;
		q[i * 2 + 1] = y + h * 0.5f + ( p[i * 2 + 1] - y - h * 0.5f ) * 0.55f;
	}
	Poly( p, 6, color );
	Poly( q, 6, inner );
}

// CG_PlaceString's words, uppercase: 1ST, TIED FOR 2ND ...
static const char *Place( int rank ) {
	const int	r = ( rank & ~RANK_TIED_FLAG ) + 1;
	const char	*s;

	if ( r == 1 ) {
		s = "1ST";
	} else if ( r == 2 ) {
		s = "2ND";
	} else if ( r == 3 ) {
		s = "3RD";
	} else if ( r >= 11 && r <= 13 ) {
		s = va( "%dTH", r );
	} else if ( r % 10 == 1 ) {
		s = va( "%dST", r );
	} else if ( r % 10 == 2 ) {
		s = va( "%dND", r );
	} else if ( r % 10 == 3 ) {
		s = va( "%dRD", r );
	} else {
		s = va( "%dTH", r );
	}
	return ( rank & RANK_TIED_FLAG ) ? va( "TIED FOR %s", s ) : s;
}

static void Upper( char *s ) {
	for ( ; *s; s++ ) {
		if ( *s >= 'a' && *s <= 'z' ) {
			*s -= 'a' - 'A';
		}
	}
}

// ================================================================ settings

static cvar_t	*oz_hud;
static cvar_t	*oz_hud_preset, *oz_hud_scale, *oz_hud_opacity, *oz_hud_show_speedometer, *oz_hud_speedometer;
static cvar_t	*oz_hud_item_timers, *oz_hud_damage_numbers, *oz_hud_kill_feed, *oz_hud_ammo_bar, *oz_hud_chat_lines;
static cvar_t	*oz_hud_chat_emoji;
static cvar_t	*oz_crosshair_shape, *oz_crosshair_thickness, *oz_crosshair_gap, *oz_crosshair_dot, *oz_crosshair_color;
static cvar_t	*oz_crosshair_outline, *oz_crosshair_hit_color, *oz_crosshair_per_weapon;
static cvar_t	*oz_crosshair_health, *oz_crosshair_opacity, *oz_crosshair_outline_width, *oz_crosshair_outline_opacity;
static cvar_t	*oz_crosshair_hit_flash, *oz_crosshair_hit_time, *oz_crosshair_hit_marker;
static cvar_t	*oz_crosshair_weapon[WP_NUM_WEAPONS];
static cvar_t	*oz_accessibility_subtitles, *oz_accessibility_subtitle_size;
static cvar_t	*oz_accessibility_reduced_flashing;	// no pulses or blinks, softer hit flashes
static cvar_t	*con_notifyTime;
// the competitive options the engine can give every mod (P6 item 5)
static cvar_t	*oz_audio_hit_sound, *oz_gameplay_draw_gun, *oz_gameplay_gun_offset, *oz_gameplay_bright_skins;
static cvar_t	*oz_gameplay_enemy_color, *oz_gameplay_team_color, *oz_mouse_zoom_sensitivity, *oz_controls_toggle_zoom;
// the game code's own cvars the HUD follows (a mod registers its own under the same names)
static cvar_t	*cg_crosshairSize, *cg_drawRewards, *cg_centertime, *cg_drawCrosshairNames;
static cvar_t	*cg_teamChatsOnly, *cg_lagometer, *cg_drawFPS, *cg_draw2D;

static const char *crosshairWeapons[WP_NUM_WEAPONS] = { "", "gauntlet", "machinegun", "shotgun", "grenadel", "rocketl",
	"lightning", "railgun", "plasma", "bfg", "" };

typedef struct {
	cvar_t		**cvar;
	const char	*name;
	const char	*value;
	int			flags;
} hudCvar_t;

static const hudCvar_t hudCvars[] = {
	{ &oz_hud, "cl_ozHud", "1", CVAR_ARCHIVE },
	{ &oz_hud_preset, "oz_hud_preset", "modern", CVAR_ARCHIVE },
	{ &oz_hud_scale, "oz_hud_scale", "1.0", CVAR_ARCHIVE },
	{ &oz_hud_opacity, "oz_hud_opacity", "1.0", CVAR_ARCHIVE },
	{ &oz_hud_show_speedometer, "oz_hud_show_speedometer", "1", CVAR_ARCHIVE },
	{ &oz_hud_speedometer, "oz_hud_speedometer", "arc", CVAR_ARCHIVE },
	{ &oz_hud_item_timers, "oz_hud_item_timers", "0", CVAR_ARCHIVE },
	{ &oz_hud_damage_numbers, "oz_hud_damage_numbers", "1", CVAR_ARCHIVE },
	{ &oz_hud_kill_feed, "oz_hud_kill_feed", "1", CVAR_ARCHIVE },
	{ &oz_hud_ammo_bar, "oz_hud_ammo_bar", "1", CVAR_ARCHIVE },
	{ &oz_hud_chat_lines, "oz_hud_chat_lines", "6", CVAR_ARCHIVE },
	{ &oz_hud_chat_emoji, "oz_hud_chat_emoji", "1", CVAR_ARCHIVE },
	{ &oz_crosshair_shape, "oz_crosshair_shape", "c", CVAR_ARCHIVE },
	{ &oz_crosshair_thickness, "oz_crosshair_thickness", "2", CVAR_ARCHIVE },
	{ &oz_crosshair_gap, "oz_crosshair_gap", "4", CVAR_ARCHIVE },
	{ &oz_crosshair_dot, "oz_crosshair_dot", "0", CVAR_ARCHIVE },
	{ &oz_crosshair_color, "oz_crosshair_color", "#ffffff", CVAR_ARCHIVE },
	{ &oz_crosshair_outline, "oz_crosshair_outline", "1", CVAR_ARCHIVE },
	{ &oz_crosshair_hit_color, "oz_crosshair_hit_color", "#ff3326", CVAR_ARCHIVE },
	{ &oz_crosshair_per_weapon, "oz_crosshair_per_weapon", "0", CVAR_ARCHIVE },
	{ &oz_crosshair_health, "oz_crosshair_health", "0", CVAR_ARCHIVE },
	{ &oz_crosshair_opacity, "oz_crosshair_opacity", "1", CVAR_ARCHIVE },
	{ &oz_crosshair_outline_width, "oz_crosshair_outline_width", "1", CVAR_ARCHIVE },
	{ &oz_crosshair_outline_opacity, "oz_crosshair_outline_opacity", "0.6", CVAR_ARCHIVE },
	{ &oz_crosshair_hit_flash, "oz_crosshair_hit_flash", "1", CVAR_ARCHIVE },
	{ &oz_crosshair_hit_time, "oz_crosshair_hit_time", "220", CVAR_ARCHIVE },
	{ &oz_crosshair_hit_marker, "oz_crosshair_hit_marker", "1", CVAR_ARCHIVE },
	{ &oz_accessibility_subtitles, "oz_accessibility_subtitles", "0", CVAR_ARCHIVE },
	{ &oz_accessibility_reduced_flashing, "oz_accessibility_reduced_flashing", "0", CVAR_ARCHIVE },
	{ &oz_accessibility_subtitle_size, "oz_accessibility_subtitle_size", "medium", CVAR_ARCHIVE },
	{ &con_notifyTime, "con_notifytime", "3", 0 },
	{ &oz_audio_hit_sound, "oz_audio_hit_sound", "classic", CVAR_ARCHIVE },
	{ &oz_gameplay_draw_gun, "oz_gameplay_draw_gun", "right", CVAR_ARCHIVE },
	{ &oz_gameplay_gun_offset, "oz_gameplay_gun_offset", "0", CVAR_ARCHIVE },
	{ &oz_gameplay_bright_skins, "oz_gameplay_bright_skins", "0", CVAR_ARCHIVE },
	{ &oz_gameplay_enemy_color, "oz_gameplay_enemy_color", "#4cff33", CVAR_ARCHIVE },
	{ &oz_gameplay_team_color, "oz_gameplay_team_color", "#ffffff", CVAR_ARCHIVE },
	{ &oz_mouse_zoom_sensitivity, "oz_mouse_zoom_sensitivity", "1.0", CVAR_ARCHIVE },
	{ &oz_controls_toggle_zoom, "oz_controls_toggle_zoom", "0", CVAR_ARCHIVE },
	{ &cg_crosshairSize, "cg_crosshairSize", "24", CVAR_ARCHIVE },
	{ &cg_drawRewards, "cg_drawRewards", "1", CVAR_ARCHIVE },
	{ &cg_centertime, "cg_centertime", "3", CVAR_CHEAT },
	{ &cg_drawCrosshairNames, "cg_drawCrosshairNames", "1", CVAR_ARCHIVE },
	{ &cg_teamChatsOnly, "cg_teamChatsOnly", "0", CVAR_ARCHIVE },
	{ &cg_lagometer, "cg_lagometer", "1", CVAR_ARCHIVE },
	{ &cg_drawFPS, "cg_drawFPS", "0", CVAR_ARCHIVE },
	{ &cg_draw2D, "cg_draw2D", "1", CVAR_ARCHIVE },
};

static void RegisterCvars( void ) {
	int	i;

	for ( i = 0; i < ARRAY_LEN( hudCvars ); i++ ) {
		*hudCvars[i].cvar = Cvar_Get( hudCvars[i].name, hudCvars[i].value, hudCvars[i].flags );
	}
	Cvar_SetDescription( oz_hud, "Oz Arena's HUD, on every server and mod (0: the game code's own HUD)." );
	for ( i = 1; i < WP_NUM_WEAPONS; i++ ) {
		if ( crosshairWeapons[i][0] ) {
			oz_crosshair_weapon[i] = Cvar_Get( va( "oz_crosshair_weapon_%s", crosshairWeapons[i] ), "default", CVAR_ARCHIVE );
		}
	}
}

static int HexDigit( char c ) {
	if ( c >= '0' && c <= '9' ) {
		return c - '0';
	}
	if ( c >= 'a' && c <= 'f' ) {
		return c - 'a' + 10;
	}
	if ( c >= 'A' && c <= 'F' ) {
		return c - 'A' + 10;
	}
	return 0;
}

// "#rrggbb" or "#rrggbbaa"
static void ParseColor( const char *s, float *out ) {
	int	i, n;

	out[0] = out[1] = out[2] = out[3] = 1.0f;
	if ( *s == '#' ) {
		s++;
	}
	n = strlen( s ) >= 8 ? 4 : 3;
	if ( strlen( s ) < 6 ) {
		return;
	}
	for ( i = 0; i < n; i++ ) {
		out[i] = ( HexDigit( s[i * 2] ) * 16 + HexDigit( s[i * 2 + 1] ) ) / 255.0f;
	}
}

// ================================================================ state

#define NAMES	11
static const char *weaponIcons[NAMES] = { "", "icons/iconw_gauntlet", "icons/iconw_machinegun", "icons/iconw_shotgun",
	"icons/iconw_grenade", "icons/iconw_rocket", "icons/iconw_lightning", "icons/iconw_railgun", "icons/iconw_plasma",
	"icons/iconw_bfg", "icons/iconw_grapple" };
static const char *weaponNames[NAMES] = { "", "GAUNTLET", "MACHINEGUN", "SHOTGUN", "GRENADE LAUNCHER", "ROCKET LAUNCHER",
	"LIGHTNING GUN", "RAILGUN", "PLASMA GUN", "BFG10K", "GRAPPLING HOOK" };
static const int lowAmmo[NAMES] = { 0, 0, 20, 3, 3, 3, 30, 3, 20, 3, 0 };	// about two seconds of fire

static struct {
	qboolean	init;
	int			weapon[NAMES];
	int			noammo, crosshairs[10];
	int			medals[6];
	int			flags[2][3];
} img;

typedef struct {
	char	a[MAX_QPATH], v[MAX_QPATH];
	int		mod, at, vt;
	qboolean am, vm;
	float	t;
} feedRow_t;

#define FEED_ROWS	6
#define FEED_LIFE	6.0f
#define MAX_HITS	8
#define LAG_SAMPLES	128
#define HISTORY_RATE	60.0f		// samples a second in the speed's and the lagometer's graphs, at any frame rate
#define MAX_NOTIFY	5
#define MAX_CHAT	100
#define MAX_NUMBERS	16

typedef struct {
	char	text[MAX_SAY_TEXT + 64];
	int		at;				// cg.time
} chatLine_t;

typedef struct {
	int		target;
	int		total;
	float	since;			// seconds since the last hit
	float	pop;
	vec3_t	pos, vel;
	qboolean on;
} damageNumber_t;

static struct {
	float		dt;					// this frame's seconds
	float		t;					// seconds, for pulses
	int			preset;				// 0 modern, 1 competitive, 2 minimal, 3 classic
	// vitals
	int			health, armor;
	float		ghostH, ghostA;
	// ammo
	int			weapon, lastAmmo;
	float		ammoFlash;
	// weapon bar
	int			barOwned, barWeapon;
	float		barShown, raise[NAMES];
	// speedometer
	float		speed, peak, peakHold, gaining;
	float		history[120];		// the last two seconds, at HISTORY_RATE
	int			numHistory;
	float		historyT;			// seconds since the last sample
	float		jump, jumpDelta, jumpT;
	// feed
	feedRow_t	feed[FEED_ROWS];
	int			numFeed;
	// damage
	struct { float angle, s, t; qboolean all; } hits[MAX_HITS];
	int			numHits;
	float		flash;
	int			damageEvent;
	float		attackerT;			// seconds since the last hit taken, < 0 none
	// crosshair
	float		hitT, pulseT;
	int			hits0;
	// lagometer
	float		lagFrames[LAG_SAMPLES], lagSnaps[LAG_SAMPLES];	// at HISTORY_RATE: the longest frame since the last
	int			numLag, lastSnapTime;							// sample, and the ping
	float		lagT, lagWorst;
	// notify
	struct { char text[256]; float t; } notify[MAX_NOTIFY];
	int			numNotify;
	// chat
	chatLine_t	chat[MAX_CHAT];
	int			numChat;
	// damage numbers
	damageNumber_t numbers[MAX_NUMBERS];
	// match stats (q3_match_stats.gd)
	int			shots[NAMES], statHits[NAMES], damage[NAMES], hitAt[NAMES];
	float		distance;
	vec3_t		lastOrigin;
	qboolean	lastValid;
	int			lastFlags;
	// top speed (the career's best.speed, P12): the counted one, and the fastest with pushes
	float		topSpeed, pushedTop;
	qboolean	pushed;
	int			speedFlags, speedDamage;
	int			kills[MAX_CLIENTS], deaths[MAX_CLIENTS];
	// the match record (P8b step 5): your frags and deaths by means of death, your pickups by item, and the
	// rail's run of hits (each rail shot a hit when your PERS_HITS goes up in the snapshot it was fired in,
	// on any game: the server damages in the same frame)
	int			fragsMod[MOD_COUNT], deathsMod[MOD_COUNT];
	int			pickups[64];
	int			railShots, railHits, railRun, railBest, railPersHits;
	// hits on a game without Oz's dmg command (id's, the mods): PERS_HITS rising in a snapshot, on the weapon
	// in hand (or the last one fired, for a missile landing after a switch)
	qboolean	dmgFromServer;		// Oz's game sends dmg: its hits are counted there
	int			lastFired;
	// death
	qboolean	dead;
	int			deathTime;
	// scores
	int			scoresAsked;
	int			fps, fpsFrames, fpsTime;
	// item timers: the major items seen on the map (by entity), and when each is back (game time, 0 up)
	int			timerItems[16], timerEnts[16], timerBack[16];
	int			numTimers;
	qboolean	timerFromServer;	// Oz's game says when (its "itemtimer" command): events aren't needed
	// death: who did it and how (the death card)
	int			killer, killerMod;
	// the reward on show (cg_draw.c's stack, kept here from the playerState's award counts)
	int			rewardStack, rewardTime, rewardMedal[10], rewardCount[10];
	int			awards[6];
	// the snapshot events were last read from
	int			eventSnap, eventSequence, externalEvent;
	int			entityEvent[MAX_GENTITIES];		// the last event bits seen on each entity
	int			entitySeen[MAX_GENTITIES];		// the snapshot each was last in
	// captions (Accessibility > Subtitles)
	struct { char text[64]; float t; } captions[3];
	int			numCaptions;
	int			warmed;			// WarmTexts' next
} hud;

// HUD > Item timers: the armors, the Mega Health and the powerups, with g_items.c's respawn times
static int MajorRespawn( int itemNum ) {
	const ozHudItem_t	*it;

	if ( itemNum <= 0 || itemNum >= numHudItems ) {
		return 0;
	}
	it = &hudItems[itemNum];
	if ( it->type == IT_ARMOR && it->quantity >= 50 ) {
		return 25;
	}
	if ( it->type == IT_HEALTH && it->quantity >= 100 ) {
		return 35;
	}
	if ( it->type == IT_POWERUP ) {
		return 120;
	}
	return 0;
}

// a major item's row, by its entity; ent -1 (a pickup event, which doesn't say which one) takes the
// first row of that item that is up
static int TimerSlot( int itemNum, int ent ) {
	int	i;

	if ( !MajorRespawn( itemNum ) ) {
		return -1;
	}
	for ( i = 0; i < hud.numTimers; i++ ) {
		if ( hud.timerItems[i] == itemNum && ( ent < 0 ? !hud.timerBack[i] : hud.timerEnts[i] == ent ) ) {
			return i;
		}
	}
	if ( ent < 0 || hud.numTimers == ARRAY_LEN( hud.timerItems ) ) {
		return -1;
	}
	hud.timerItems[hud.numTimers] = itemNum;
	hud.timerEnts[hud.numTimers] = ent;
	hud.timerBack[hud.numTimers] = 0;
	return hud.numTimers++;
}

// ================================================================ events

static const int modWeapon[] = { 0, WP_SHOTGUN, WP_GAUNTLET, WP_MACHINEGUN, WP_GRENADE_LAUNCHER, WP_GRENADE_LAUNCHER,
	WP_ROCKET_LAUNCHER, WP_ROCKET_LAUNCHER, WP_PLASMAGUN, WP_PLASMAGUN, WP_RAILGUN, WP_LIGHTNING, WP_BFG, WP_BFG };

static int ModWeapon( int mod ) {
	if ( mod >= 0 && mod < ARRAY_LEN( modWeapon ) ) {
		return modWeapon[mod];
	}
	if ( mod == MOD_GRAPPLE ) {
		return WP_GRAPPLING_HOOK;
	}
	return 0;
}

static const char *ModTag( int mod ) {
	switch ( mod ) {
	case MOD_UNKNOWN: return "DIED";
	case MOD_WATER: return "DROWNED";
	case MOD_SLIME: return "SLIME";
	case MOD_LAVA: return "LAVA";
	case MOD_CRUSH: return "CRUSHED";
	case MOD_TELEFRAG: return "TELEFRAG";
	case MOD_FALLING: return "FELL";
	case MOD_SUICIDE: return "SUICIDE";
	case MOD_TARGET_LASER: return "LASER";
	default: return "KILLED";
	}
}

// cg_event.c CG_Obituary's words for a death nobody caused
static const char *ObituaryLine( int mod ) {
	switch ( mod ) {
	case MOD_SUICIDE: return "suicides";
	case MOD_FALLING: return "cratered";
	case MOD_CRUSH: return "was squished";
	case MOD_WATER: return "sank like a rock";
	case MOD_SLIME: return "melted";
	case MOD_LAVA: return "does a back flip into the lava";
	case MOD_TARGET_LASER: return "saw the light";
	case MOD_TRIGGER_HURT: return "was in the wrong place";
	default: return "died";
	}
}

static void Card( float x, float y, float w, float h, int client, qboolean highlight, float alpha );
static void LoadImages( void );
static void LoadCards( void );

static void PlayerName( int client, char *out, int size ) {
	const char *info;

	out[0] = 0;
	if ( client < 0 || client >= MAX_CLIENTS ) {
		return;
	}
	info = CS( CS_PLAYERS + client );
	Q_strncpyz( out, Info_ValueForKey( info, "n" ), size );
}

static int PlayerTeam( int client ) {
	if ( cgs.gametype < GT_TEAM || client < 0 || client >= MAX_CLIENTS ) {
		return 0;
	}
	return cgs.clientinfo[client].team == TEAM_RED ? 1 : cgs.clientinfo[client].team == TEAM_BLUE ? 2 : 0;
}

static void Obituary( const entityState_t *es ) {
	const int	target = es->otherEntityNum;
	int			attacker = es->otherEntityNum2;
	const int	mod = es->eventParm;
	feedRow_t	*row;
	int			i;

	if ( target < 0 || target >= MAX_CLIENTS ) {
		return;
	}
	if ( attacker < 0 || attacker >= MAX_CLIENTS || attacker == target ) {
		attacker = -1;
	}
	hud.deaths[target]++;
	if ( attacker >= 0 ) {
		hud.kills[attacker]++;
	}
	if ( OwnPlay() && mod >= 0 && mod < MOD_COUNT ) {
		if ( target == cg.snap->ps.clientNum ) {
			hud.deathsMod[mod]++;
		} else if ( attacker == cg.snap->ps.clientNum ) {
			hud.fragsMod[mod]++;
		}
	}
	for ( i = FEED_ROWS - 1; i > 0; i-- ) {
		hud.feed[i] = hud.feed[i - 1];
	}
	if ( hud.numFeed < FEED_ROWS ) {
		hud.numFeed++;
	}
	row = &hud.feed[0];
	memset( row, 0, sizeof( *row ) );
	if ( attacker >= 0 ) {
		PlayerName( attacker, row->a, sizeof( row->a ) );
	}
	PlayerName( target, row->v, sizeof( row->v ) );
	row->mod = mod;
	row->am = attacker >= 0 && attacker == cg.snap->ps.clientNum;
	row->vm = target == cg.snap->ps.clientNum;
	row->at = PlayerTeam( attacker );
	row->vt = PlayerTeam( target );
	if ( row->vm ) {
		// the death card's killer, and cg.killerName
		hud.killer = attacker;
		hud.killerMod = mod;
		Q_strncpyz( cg.killerName, row->a, sizeof( cg.killerName ) );
	}
	if ( row->am ) {
		// cg_event.c CG_Obituary: "You fragged ..." and, outside team modes, your place
		if ( cgs.gametype < GT_TEAM ) {
			char place[64];
			Q_strncpyz( place, Place( cg.snap->ps.persistant[PERS_RANK] ), sizeof( place ) );
			Q_strlwr( place + 1 );
			Com_sprintf( cg.centerPrint, sizeof( cg.centerPrint ), "You fragged %s\n%s place with %i", row->v, place,
				cg.snap->ps.persistant[PERS_SCORE] );
		} else {
			Com_sprintf( cg.centerPrint, sizeof( cg.centerPrint ), "You fragged %s", row->v );
		}
		cg.centerPrintTime = cg.time;
	}
}

static void EntityEvent( const entityState_t *es, int event, qboolean mine ) {

	switch ( event ) {
	case EV_OBITUARY:
		if ( cg.snap ) {
			Obituary( es );
		}
		break;
	case EV_JUMP:
		if ( mine ) {
			// the takeoff speed against the previous takeoff in this chain
			const float ups = (float)sqrt( cg.predictedPlayerState.velocity[0] * cg.predictedPlayerState.velocity[0] +
				cg.predictedPlayerState.velocity[1] * cg.predictedPlayerState.velocity[1] );
			hud.jumpDelta = hud.jump >= 0 && hud.jumpT < 1.5f ? ups - hud.jump : 0;
			hud.jump = ups;
			hud.jumpT = 0;
		}
		break;
	case EV_JUMP_PAD:
		if ( mine ) {
			hud.pushed = qtrue;		// the pad's throw isn't the player's own speed (TopSpeed)
		}
		break;
	case EV_FIRE_WEAPON:
		// g_weapon.c's accuracy_shots: the gauntlet and the grapple don't count
		if ( mine && es->weapon > WP_NONE && es->weapon < NAMES ) {
			hud.shots[es->weapon]++;
			hud.lastFired = es->weapon;
		}
		break;
	case EV_ITEM_PICKUP:
	case EV_GLOBAL_ITEM_PICKUP: {
		// a major item taken where you could see or hear it: its respawn counts down from now (a powerup's own
		// pickup comes with its global one, which counts it)
		const qboolean powerup = event == EV_ITEM_PICKUP && es->eventParm > 0 && es->eventParm < (int)numHudItems
			&& hudItems[es->eventParm].type == IT_POWERUP;
		const int slot = hud.timerFromServer || powerup ? -1 : TimerSlot( es->eventParm, -1 );
		if ( slot >= 0 ) {
			hud.timerBack[slot] = cg.time + MajorRespawn( es->eventParm ) * 1000;
		}
		if ( mine && event == EV_ITEM_PICKUP && es->eventParm > 0 && es->eventParm < (int)numHudItems ) {
			// cg_event.c CG_ItemPickup: the name for 3 s, and the crosshair's pulse
			cg.itemPickup = es->eventParm;
			cg.itemPickupTime = cg.time;
			cg.itemPickupBlendTime = cg.time;
			if ( OwnPlay() && es->eventParm < (int)ARRAY_LEN( hud.pickups ) ) {
				hud.pickups[es->eventParm]++;
			}
		}
		break;
	}
	default:
		break;
	}
}

static void AddNotify( const char *text ) {
	int	i;

	if ( hud.numNotify == MAX_NOTIFY ) {
		for ( i = 1; i < MAX_NOTIFY; i++ ) {
			hud.notify[i - 1] = hud.notify[i];
		}
		hud.numNotify--;
	}
	Q_strncpyz( hud.notify[hud.numNotify].text, text, sizeof( hud.notify[0].text ) );
	hud.notify[hud.numNotify].t = 0;
	hud.numNotify++;
}

static void AddChat( const char *text ) {
	char	buf[MAX_SAY_TEXT + 64];
	char	*d = buf;
	int		i;

	// the game's name markers (EC 0x19) out, as Q3HudChat strips them
	for ( ; *text && d < buf + sizeof( buf ) - 1; text++ ) {
		if ( *text != 0x19 ) {
			*d++ = *text;
		}
	}
	*d = 0;
	if ( hud.numChat == MAX_CHAT ) {
		for ( i = 1; i < MAX_CHAT; i++ ) {
			hud.chat[i - 1] = hud.chat[i];
		}
		hud.numChat--;
	}
	Q_strncpyz( hud.chat[hud.numChat].text, buf, sizeof( hud.chat[0].text ) );
	hud.chat[hud.numChat].at = cg.time;
	hud.numChat++;
}

static void DamageNumber( int target, int amount ) {
	damageNumber_t	*n = NULL;
	vec3_t			origin;
	int				i;

	if ( !oz_hud_damage_numbers->integer || target < 0 || target >= MAX_CLIENTS ) {
		return;
	}
	if ( !EntityOrigin( target, origin ) ) {
		return;
	}
	for ( i = 0; i < MAX_NUMBERS; i++ ) {
		if ( hud.numbers[i].on && hud.numbers[i].target == target && hud.numbers[i].since < 1.0f ) {
			n = &hud.numbers[i];
			break;
		}
	}
	if ( !n ) {
		for ( i = 0; i < MAX_NUMBERS && hud.numbers[i].on; i++ ) {
		}
		if ( i == MAX_NUMBERS ) {
			i = 0;
		}
		n = &hud.numbers[i];
		memset( n, 0, sizeof( *n ) );
		n->on = qtrue;
		n->target = target;
	}
	n->total += amount;
	// relaunched off the head with the running total (damage_number.gd), in id units (32 a meter)
	VectorCopy( origin, n->pos );
	n->pos[2] += 40;
	n->vel[0] = crandom() * 1.2f * 32;
	n->vel[1] = crandom() * 1.2f * 32;
	n->vel[2] = 3.2f * 32;
	n->since = 0;
	n->pop = 0;
}

// cg_servercmds.c CG_ParseScores: baseq3's "scores" (a mod with its own format just leaves the list
// as it was). A command holds about 25 players: Oz's game sends the rest first, as "scoresmore <first>
// <count> ..." (game patch 0028), kept here until "scores" comes; id's game leaves them out (the board
// lists them from the configstrings)
static struct {
	ozScore_t	s[MAX_CLIENTS];
	int			from, to;		// the places kept, [from, to)
} moreScores;

static void ParseScoreAt( int arg, ozScore_t *s ) {
	s->client = atoi( Cmd_Argv( arg ) );
	s->score = atoi( Cmd_Argv( arg + 1 ) );
	s->ping = atoi( Cmd_Argv( arg + 2 ) );
	s->time = atoi( Cmd_Argv( arg + 3 ) );
	s->scoreFlags = atoi( Cmd_Argv( arg + 4 ) );
	s->powerUps = atoi( Cmd_Argv( arg + 5 ) );
	s->accuracy = atoi( Cmd_Argv( arg + 6 ) );
	s->impressiveCount = atoi( Cmd_Argv( arg + 7 ) );
	s->excellentCount = atoi( Cmd_Argv( arg + 8 ) );
	s->guantletCount = atoi( Cmd_Argv( arg + 9 ) );
	s->defendCount = atoi( Cmd_Argv( arg + 10 ) );
	s->assistCount = atoi( Cmd_Argv( arg + 11 ) );
	s->perfect = atoi( Cmd_Argv( arg + 12 ) );
	s->captures = atoi( Cmd_Argv( arg + 13 ) );
	if ( s->client < 0 || s->client >= MAX_CLIENTS ) {
		s->client = 0;
	}
	s->team = cgs.clientinfo[s->client].team;
}

static void ParseMoreScores( void ) {
	const int	first = atoi( Cmd_Argv( 1 ) );
	int			n = atoi( Cmd_Argv( 2 ) ), i;

	if ( first <= 0 || first >= MAX_CLIENTS || n < 0 || Cmd_Argc() < 3 + n * 14 ) {
		return;
	}
	if ( first != moreScores.to || !moreScores.to ) {
		moreScores.from = moreScores.to = first;	// the first part of a new list
	}
	if ( first + n > MAX_CLIENTS ) {
		n = MAX_CLIENTS - first;
	}
	for ( i = 0; i < n; i++ ) {
		ParseScoreAt( i * 14 + 3, &moreScores.s[first + i] );
	}
	moreScores.to = first + n;
}

static void ParseScores( void ) {
	int	i, n = atoi( Cmd_Argv( 1 ) );

	if ( n < 0 || n > MAX_CLIENTS || Cmd_Argc() < 4 + n * 14 ) {
		return;
	}
	for ( i = 0; i < n; i++ ) {
		ParseScoreAt( i * 14 + 4, &cg.scores[i] );
	}
	if ( moreScores.from == n ) {
		for ( ; i < moreScores.to; i++ ) {
			cg.scores[i] = moreScores.s[i];
		}
	}
	moreScores.from = moreScores.to = 0;
	cg.numScores = i;
	cg.scoresParsed++;
	cg.scoresFull = qtrue;
}

/*
CPMA's board, read from what its server sends (not its code), checked against CPMA's own board:
  xscores <n> then n times: <client> <score> <letters> <minutes> <ping> <damage given> <damage taken>
  dmscores <duelist> <duelist> then <spectator> <ping>... -1 (a duel: the scores are the configstrings')
*/
static void ParseCpmaScores( void ) {
	int	i, n = atoi( Cmd_Argv( 1 ) );

	if ( n < 0 || n > MAX_CLIENTS || Cmd_Argc() < 2 + n * 7 ) {
		return;
	}
	memset( cg.scores, 0, sizeof( cg.scores[0] ) * n );
	cg.numScores = n;
	cg.scoresParsed++;
	cg.scoresFull = qfalse;
	for ( i = 0; i < n; i++ ) {
		ozScore_t *s = &cg.scores[i];
		s->client = atoi( Cmd_Argv( i * 7 + 2 ) );
		s->score = atoi( Cmd_Argv( i * 7 + 3 ) );
		s->time = atoi( Cmd_Argv( i * 7 + 5 ) );
		s->ping = atoi( Cmd_Argv( i * 7 + 6 ) );
		if ( s->client < 0 || s->client >= MAX_CLIENTS ) {
			s->client = 0;
		}
		s->team = cgs.clientinfo[s->client].team;
	}
}

static void ParseCpmaDuel( void ) {
	const int	self = cl.snap.ps.clientNum, own = cl.snap.ps.persistant[PERS_SCORE];
	int			i, n = 0, a;

	for ( i = 1; i < Cmd_Argc() && n < MAX_CLIENTS; i++ ) {
		ozScore_t *s;
		a = atoi( Cmd_Argv( i ) );
		if ( a < 0 || a >= MAX_CLIENTS ) {
			break;
		}
		s = &cg.scores[n++];
		memset( s, 0, sizeof( *s ) );
		s->client = a;
		s->team = cgs.clientinfo[a].team;
		if ( i <= 2 ) {
			// the duelists: your own score from your state, the other's is the configstrings' other one
			// (CS_SCORES1 is the leader's); watching, the first listed gets the first
			if ( a == self ) {
				s->score = own;
			} else if ( self == atoi( Cmd_Argv( 3 - i ) ) ) {
				s->score = cgs.scores1 == own ? cgs.scores2 : cgs.scores1;
			} else {
				s->score = i == 1 ? cgs.scores1 : cgs.scores2;
			}
			if ( s->score == SCORE_NOT_PRESENT ) {
				s->score = 0;
			}
		} else {
			s->ping = atoi( Cmd_Argv( ++i ) );
		}
	}
	cg.numScores = n;
	cg.scoresParsed++;
	cg.scoresFull = qfalse;
}

static qboolean hudInit;

// ================================================================ Defrag's run timer

// Defrag's cgame draws its timer from a value only it reads. What the HUD can see is the run's state
// in stats[12]: 0 with no run, 6 from the start line, 12 past the finish line, 0 again at the next
// spawn (Defrag 1.91.31 on engine/tests/compat/maps/oz_defrag.bsp: the 6 and the 12 arrive the
// printed time apart). The HUD times the run from those snapshots, and at the finish shows the
// time Defrag prints: "Time performed by <name> : m:ss:mmm".
#define DF_STAT_RUN		12
#define DF_RUN_DONE		8		// the finish's bit

static struct {
	int		client;		// whose run (the followed player's, spectating)
	int		state;		// stats[DF_STAT_RUN] at the last snapshot
	int		start;		// server time of the snapshot that started the run; 0 with no run seen
	int		time;		// the run's time in ms once it's over, else -1
} df = { -1, 0, 0, -1 };

static qboolean Defrag( void ) {
	return !Q_stricmp( FS_GetCurrentGameDir(), "defrag" );
}

static void DefragUpdate( const playerState_t *ps ) {
	const int s = ps->stats[DF_STAT_RUN];

	if ( ps->clientNum != df.client ) {
		// someone else's run, already under way: its start wasn't seen
		df.client = ps->clientNum;
		df.state = s;
		df.start = 0;
		df.time = -1;
		return;
	}
	if ( s == df.state ) {
		return;
	}
	if ( !s ) {
		df.start = 0;
		df.time = -1;
	} else if ( !df.state ) {
		df.start = cl.snap.serverTime;
		df.time = -1;
	} else if ( ( s & DF_RUN_DONE ) && df.start && df.time < 0 ) {
		df.time = cl.snap.serverTime - df.start;
		// for a test to check against Defrag's printed time
		Com_Printf( "ozhud defrag: run %d ms by the snapshots\n", df.time );
	}
	df.state = s;
}

// Defrag's own time for the run, when the print is about the player the HUD shows
static void DefragPrint( const char *text ) {
	static const char	lead[] = "Time performed by ";
	char				line[256], name[64], *p, *sep;
	int					parts[4], n = 0, ms, i, scale;

	Q_strncpyz( line, text, sizeof( line ) );
	Q_CleanStr( line );
	if ( Q_strncmp( line, lead, sizeof( lead ) - 1 ) || !df.start ) {
		return;
	}
	p = line + sizeof( lead ) - 1;
	sep = strstr( p, " : " );
	if ( !sep ) {
		return;
	}
	*sep = 0;
	PlayerName( df.client, name, sizeof( name ) );
	Q_CleanStr( name );
	if ( Q_stricmp( p, name ) ) {
		return;
	}
	// m:ss:mmm (h:mm:ss:mmm on long runs): the last part is the milliseconds
	p = sep + 3;
	while ( n < 4 && *p >= '0' && *p <= '9' ) {
		parts[n++] = atoi( p );
		while ( *p >= '0' && *p <= '9' ) {
			p++;
		}
		if ( *p != ':' ) {
			break;
		}
		p++;
	}
	if ( n < 2 ) {
		return;
	}
	ms = parts[n - 1];
	for ( i = n - 2, scale = 1000; i >= 0; i--, scale *= 60 ) {
		ms += parts[i] * scale;
	}
	df.time = ms;
	Com_Printf( "ozhud defrag: run %d ms as Defrag printed it\n", df.time );
}

int OzHud_ServerCommand( const char *text ) {
	const char	*cmd;

	if ( !hudInit ) {
		return 0;
	}
	Cmd_TokenizeString( text );
	cmd = Cmd_Argv( 0 );
	if ( !strcmp( cmd, "cp" ) ) {
		Q_strncpyz( cg.centerPrint, Cmd_Argv( 1 ), sizeof( cg.centerPrint ) );
		cg.centerPrintTime = cg.time;
		return 0;
	}
	if ( !strcmp( cmd, "scores" ) ) {
		ParseScores();
		return 0;
	}
	if ( !strcmp( cmd, "scoresmore" ) ) {
		ParseMoreScores();
		return 0;
	}
	if ( !strcmp( cmd, "xscores" ) ) {
		ParseCpmaScores();
		return 0;
	}
	if ( !strcmp( cmd, "dmscores" ) ) {
		ParseCpmaDuel();
		return 0;
	}
	if ( !strcmp( cmd, "itemtimer" ) ) {
		// Oz's game (patch 0020): "itemtimer <entity> <item> <when it's back>", for every major item taken
		const int slot = TimerSlot( atoi( Cmd_Argv( 2 ) ), atoi( Cmd_Argv( 1 ) ) );
		hud.timerFromServer = qtrue;
		if ( slot >= 0 ) {
			hud.timerBack[slot] = atoi( Cmd_Argv( 3 ) );
		}
		return 1;
	}

	if ( !strcmp( cmd, "dmg" ) ) {
		// "dmg <target> <amount> <armor> <means> <time>" (q3core patch 0002): damage numbers and the
		// scoreboard's per-weapon damage and hits (hits once per weapon per server frame)
		const int	target = atoi( Cmd_Argv( 1 ) );
		const int	amount = atoi( Cmd_Argv( 2 ) );
		const int	means = Cmd_Argc() > 4 ? atoi( Cmd_Argv( 4 ) ) : -1;
		const int	time = Cmd_Argc() > 5 ? atoi( Cmd_Argv( 5 ) ) : -1;
		const int	w = means >= 0 ? ModWeapon( means ) : cg.snap ? cg.snap->ps.weapon : 0;
		hud.dmgFromServer = qtrue;
		if ( cg.snap && target == cg.snap->ps.clientNum ) {
			return 0;
		}
		DamageNumber( target, amount );
		if ( w > 0 && w < NAMES ) {
			hud.damage[w] += amount;
			if ( time < 0 || hud.hitAt[w] != time ) {
				hud.hitAt[w] = time;
				hud.statHits[w]++;
			}
		}
		return 0;
	}
	if ( !strcmp( cmd, "print" ) ) {
		char	line[256];
		char	*nl;
		Q_strncpyz( line, Cmd_Argv( 1 ), sizeof( line ) );
		nl = strchr( line, '\n' );
		if ( nl ) {
			*nl = 0;
		}
		if ( line[0] ) {
			AddNotify( line );
			if ( Defrag() ) {
				DefragPrint( line );
			}
		}
		return 0;
	}
	if ( !strcmp( cmd, "chat" ) ) {
		if ( !cg_teamChatsOnly->integer ) {
			AddChat( Cmd_Argv( 1 ) );
		}
		return 0;
	}
	if ( !strcmp( cmd, "tchat" ) ) {
		AddChat( Cmd_Argv( 1 ) );
		return 0;
	}
	if ( !strcmp( cmd, "map_restart" ) ) {
		memset( hud.feed, 0, sizeof( hud.feed ) );
		hud.numFeed = 0;
		memset( hud.kills, 0, sizeof( hud.kills ) );
		memset( hud.deaths, 0, sizeof( hud.deaths ) );
		memset( hud.shots, 0, sizeof( hud.shots ) );
		memset( hud.statHits, 0, sizeof( hud.statHits ) );
		memset( hud.damage, 0, sizeof( hud.damage ) );
		memset( hud.timerBack, 0, sizeof( hud.timerBack ) );
		memset( hud.fragsMod, 0, sizeof( hud.fragsMod ) );
		memset( hud.deathsMod, 0, sizeof( hud.deathsMod ) );
		memset( hud.pickups, 0, sizeof( hud.pickups ) );
		hud.railShots = hud.railHits = hud.railRun = hud.railBest = 0;
		hud.distance = 0;
		hud.lastValid = qfalse;
		hud.topSpeed = hud.pushedTop = 0;
		hud.pushed = qtrue;
		hud.rewardStack = 0;
	}
	return 0;
}

// ================================================================ setup

// oz_aimat [client]: turns the view to a player, or the nearest one in the snapshot (engine/tests/
// hud_test.py lands hits with it). Only on your own server with cheats on, as setviewpos
static void AimAt_f( void ) {
	int			target = atoi( Cmd_Argv( 1 ) );
	vec3_t		origin, eye, dir, angles;
	int			i;

	if ( !com_sv_running->integer || !Cvar_VariableIntegerValue( "sv_cheats" ) || cls.state != CA_ACTIVE ) {
		Com_Printf( "oz_aimat needs a local server with cheats on\n" );
		return;
	}
	if ( Cmd_Argc() < 2 ) {
		float best = 1e9f;
		target = -1;
		for ( i = 0; i < cl.snap.numEntities; i++ ) {
			const entityState_t *es = SnapEntity( i );
			vec3_t d;
			if ( es->eType == ET_PLAYER && es->number != cl.snap.ps.clientNum && !( es->eFlags & EF_DEAD ) ) {
				VectorSubtract( es->pos.trBase, cl.snap.ps.origin, d );
				if ( VectorLength( d ) < best ) {
					best = VectorLength( d );
					target = es->number;
				}
			}
		}
	}
	if ( target < 0 || !EntityOrigin( target, origin ) ) {
		Com_Printf( "oz_aimat <client in view>\n" );
		return;
	}
	VectorCopy( cl.snap.ps.origin, eye );
	eye[2] += cl.snap.ps.viewheight;
	VectorSubtract( origin, eye, dir );
	vectoangles( dir, angles );
	for ( i = 0; i < 3; i++ ) {
		cl.viewangles[i] = angles[i] - SHORT2ANGLE( cl.snap.ps.delta_angles[i] );
	}
}

sharedEntity_t *SV_GentityNum( int num );

// oz_goto <client>: puts you where that player stands, through the game's setviewpos, which telefrags
// them: a frag that needs no line of sight (engine/tests/progress_test.py plays the campaign's arenas
// with it). Only on your own server with cheats on
static void GoTo_f( void ) {
	int			target = Cmd_Argc() > 1 ? atoi( Cmd_Argv( 1 ) ) : -1;
	const float	*o;

	if ( !com_sv_running->integer || !Cvar_VariableIntegerValue( "sv_cheats" ) || cls.state != CA_ACTIVE ) {
		Com_Printf( "oz_goto [client]: a local server with cheats on\n" );
		return;
	}
	if ( target < 0 ) {	// the first other player in the game
		for ( target = 0; target < MAX_CLIENTS; target++ ) {
			if ( target != cl.snap.ps.clientNum && SV_GentityNum( target )->r.linked ) {
				break;
			}
		}
	}
	if ( target < 0 || target >= MAX_CLIENTS || target == cl.snap.ps.clientNum || !SV_GentityNum( target )->r.linked ) {
		Com_Printf( "oz_goto: client %i isn't in the game\n", target );
		return;
	}
	o = SV_GentityNum( target )->r.currentOrigin;
	Cbuf_InsertText( va( "setviewpos %i %i %i 0\n", (int)o[0], (int)o[1], (int)o[2] ) );
}

// ozents [all]: the snapshot's player entities (all: every entity) as the server sent them, and the
// playerState: how a server's mode marks its players (A51's Freeze Tag), read from what it sends
static void Ents_f( void ) {
	const qboolean	all = !Q_stricmp( Cmd_Argv( 1 ), "all" );
	const playerState_t	*ps = &cl.snap.ps;
	int				i;

	if ( !cl.snap.valid ) {
		Com_Printf( "ozents: no snapshot\n" );
		return;
	}
	Com_Printf( "ozents: snap %d, ps client %d pm_type %d pm_flags 0x%x eFlags 0x%x health %d weapon %d"
		" powerups 0x%x stats %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d pers %d %d %d %d %d %d %d %d\n",
		cl.snap.serverTime, ps->clientNum, ps->pm_type, ps->pm_flags, ps->eFlags, ps->stats[STAT_HEALTH], ps->weapon,
		( ps->powerups[0] ? 1 : 0 ) | ( ps->powerups[1] ? 2 : 0 ) | ( ps->powerups[2] ? 4 : 0 ) | ( ps->powerups[3] ? 8 : 0 )
		| ( ps->powerups[4] ? 16 : 0 ) | ( ps->powerups[5] ? 32 : 0 ) | ( ps->powerups[6] ? 64 : 0 ) | ( ps->powerups[7] ? 128 : 0 ),
		ps->stats[0], ps->stats[1], ps->stats[2], ps->stats[3], ps->stats[4], ps->stats[5], ps->stats[6], ps->stats[7],
		ps->stats[8], ps->stats[9], ps->stats[10], ps->stats[11], ps->stats[12], ps->stats[13], ps->stats[14], ps->stats[15],
		ps->persistant[0], ps->persistant[1], ps->persistant[2], ps->persistant[3], ps->persistant[4], ps->persistant[5],
		ps->persistant[6], ps->persistant[7] );
	for ( i = 0; i < cl.snap.numEntities; i++ ) {
		const entityState_t *es = &cl.parseEntities[( cl.snap.parseEntitiesNum + i ) & ( MAX_PARSE_ENTITIES - 1 )];
		if ( !all && es->eType != ET_PLAYER ) {
			continue;
		}
		Com_Printf( "ozents: %d type %d flags 0x%x powerups 0x%x legs %d torso %d weapon %d model %d %d client %d"
			" other %d %d generic %d frame %d solid 0x%x time %d %d event 0x%x %d tr %d at %d %d %d\n",
			es->number, es->eType, es->eFlags, es->powerups, es->legsAnim & ~ANIM_TOGGLEBIT, es->torsoAnim & ~ANIM_TOGGLEBIT,
			es->weapon, es->modelindex, es->modelindex2, es->clientNum, es->otherEntityNum, es->otherEntityNum2,
			es->generic1, es->frame, es->solid, es->time, es->time2, es->event, es->eventParm, es->pos.trType,
			(int)es->pos.trBase[0], (int)es->pos.trBase[1], (int)es->pos.trBase[2] );
	}
}

static void Type_f( void );
static void Emoji_f( void );
static void Board_f( void );
static void BoardPoint_f( void );
static void BoardMouseReset( void );

void OzHud_Init( void ) {
	if ( hudInit ) {
		return;
	}
	hudInit = qtrue;
	RegisterCvars();
	OzHud_ModsInit();
	BoardMouseReset();
	Cmd_AddCommand( "oz_aimat", AimAt_f );
	Cmd_AddCommand( "oz_aimdrawn", OzView_AimDrawn_f );
	Cmd_AddCommand( "oz_patrol", OzView_Patrol_f );
	Cmd_AddCommand( "oz_goto", GoTo_f );
	Cmd_AddCommand( "oz_type", Type_f );
	Cmd_AddCommand( "oz_emoji", Emoji_f );
	Cmd_AddCommand( "ozboard", Board_f );
	Cmd_AddCommand( "ozents", Ents_f );
	Cmd_AddCommand( "ozboard_point", BoardPoint_f );
}

// a new map or server: nothing of the last one carries over
void OzHud_Reset( void ) {
	const qboolean showScores = cg.showScores;	// a Tab held through a vid_restart stays held

	memset( &hud, 0, sizeof( hud ) );
	memset( &cg, 0, sizeof( cg ) );
	memset( &img, 0, sizeof( img ) );
	memset( &moreScores, 0, sizeof( moreScores ) );
	BoardMouseReset();
	cg.showScores = showScores;
	hud.jump = -1;
	hud.pushed = qtrue;
	hud.attackerT = -1;
	hud.killer = -1;
	hud.killerMod = -1;
	hud.eventSnap = -1;
	df.client = -1;
	df.state = 0;
	df.start = 0;
	df.time = -1;
}

// not off while paused: the game code's menu pauses a local match (cl_paused), and its HUD and board
// would show through under the menu. A mod's own menu (KEYCATCH_CGAME) gets the screen back, but not
// while a demo plays: CPMA holds the keys for its demo controls the whole time
int OzHud_Active( void ) {
	return hudInit && oz_hud->integer && cgvm && cls.state == CA_ACTIVE && cl.snap.valid
		&& ( clc.demoplaying || !( Key_GetCatcher() & KEYCATCH_CGAME ) ) && OzUI_HudAvailable();
}

int OzHud_HideCgame2D( void ) {
	return OzHud_Active();
}

// the keys the game code asks for (CG_KEY_SETCATCHER) and doesn't get, so the HUD stays up: the
// scoreboard key's (A51's cgame takes the mouse for its board, which hid the Oz HUD and its board and
// kept -scores from Oz), and every time under a server's client mod (Ozmium, 2026-11-03: on A51's
// servers the Oz HUD stays, whatever the mod does). CPMA's in-match menus keep theirs (decision 21)
int OzHud_KeepsKeys( void ) {
	return hudInit && oz_hud->integer && !clc.demoplaying && OzUI_HudAvailable()
		&& ( cg.showScores || cls.cgameClientMod );
}

void OzHud_RenderScene( const refdef_t *fd ) {
	if ( !( fd->rdflags & RDF_NOWORLDMODEL ) ) {
		cg.refdef = *fd;
		cg.refdefValid = qtrue;
	}
}

// what a cgame keeps in cgs: the server's settings and the players, from the configstrings
static void ReadConfigstrings( void ) {
	const char	*info = CS( CS_SERVERINFO );
	const char	*fs;
	int			i;

	cgs.gametype = atoi( Info_ValueForKey( info, "g_gametype" ) );
	cgs.timelimit = atoi( Info_ValueForKey( info, "timelimit" ) );
	cgs.fraglimit = atoi( Info_ValueForKey( info, "fraglimit" ) );
	cgs.capturelimit = atoi( Info_ValueForKey( info, "capturelimit" ) );
	cgs.levelStartTime = atoi( CS( CS_LEVEL_START_TIME ) );
	cgs.scores1 = CS( CS_SCORES1 )[0] ? atoi( CS( CS_SCORES1 ) ) : SCORE_NOT_PRESENT;
	cgs.scores2 = CS( CS_SCORES2 )[0] ? atoi( CS( CS_SCORES2 ) ) : SCORE_NOT_PRESENT;
	fs = CS( CS_FLAGSTATUS );
	cgs.redflag = fs[0] ? fs[0] - '0' : -1;
	cgs.blueflag = fs[0] && fs[1] ? fs[1] - '0' : -1;
	cgs.voteTime = atoi( CS( CS_VOTE_TIME ) );
	cgs.voteYes = atoi( CS( CS_VOTE_YES ) );
	cgs.voteNo = atoi( CS( CS_VOTE_NO ) );
	Q_strncpyz( cgs.voteString, CS( CS_VOTE_STRING ), sizeof( cgs.voteString ) );
	cg.warmup = atoi( CS( CS_WARMUP ) );
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		ozClientInfo_t	*ci = &cgs.clientinfo[i];
		const char		*p = CS( CS_PLAYERS + i );
		char			model[MAX_QPATH], *slash;
		ci->infoValid = p[0] != 0;
		if ( !ci->infoValid ) {
			continue;
		}
		ci->team = atoi( Info_ValueForKey( p, "t" ) );
		ci->botSkill = atoi( Info_ValueForKey( p, "skill" ) );
		Q_strncpyz( model, Info_ValueForKey( p, cgs.gametype >= GT_TEAM ? "team_model" : "model" ), sizeof( model ) );
		if ( !model[0] ) {
			Q_strncpyz( model, Info_ValueForKey( p, "model" ), sizeof( model ) );
		}
		slash = strchr( model, '/' );
		if ( slash ) {
			*slash = 0;
			Q_strncpyz( ci->skinName, slash + 1, sizeof( ci->skinName ) );
		} else {
			Q_strncpyz( ci->skinName, "default", sizeof( ci->skinName ) );
		}
		Q_strncpyz( ci->modelName, model[0] ? model : "sarge", sizeof( ci->modelName ) );
	}
}

// cg_event.c's event checks, on each new snapshot: the entities' events (temporary event entities
// once, an entity's own event when its bits change) and the playerState's own (CG_CheckPlayerstateEvents)
static void ReadEvents( void ) {
	const playerState_t	*ps = &cl.snap.ps;
	int					i, railShots, shotsBefore[NAMES];

	if ( cl.snap.messageNum == hud.eventSnap ) {
		return;
	}
	if ( hud.eventSnap < 0 ) {
		// the first snapshot: what is already on show isn't news
		hud.eventSequence = ps->eventSequence;
		hud.externalEvent = ps->externalEvent;
		hud.railPersHits = ps->persistant[PERS_HITS];
	}
	railShots = hud.shots[WP_RAILGUN];
	memcpy( shotsBefore, hud.shots, sizeof( shotsBefore ) );
	for ( i = 0; i < cl.snap.numEntities; i++ ) {
		const entityState_t	*es = SnapEntity( i );
		const int			n = es->number & ( MAX_GENTITIES - 1 );
		const qboolean		wasIn = hud.eventSnap >= 0 && hud.entitySeen[n] == hud.eventSnap;
		if ( es->eType > ET_EVENTS ) {
			if ( !wasIn ) {
				EntityEvent( es, ( es->eType - ET_EVENTS ) & ~EV_EVENT_BITS, qfalse );
			}
		} else if ( es->event != hud.entityEvent[n] || !wasIn ) {
			const int old = hud.entityEvent[n];
			hud.entityEvent[n] = es->event;
			if ( ( es->event & ~EV_EVENT_BITS ) && ( wasIn ? es->event != old : hud.eventSnap >= 0 ) ) {
				EntityEvent( es, es->event & ~EV_EVENT_BITS, es->number == ps->clientNum );
			}
		}
		hud.entitySeen[n] = cl.snap.messageNum;
	}
	if ( ps->eventSequence - hud.eventSequence > MAX_PS_EVENTS ) {
		hud.eventSequence = ps->eventSequence - MAX_PS_EVENTS;
	}
	for ( i = hud.eventSequence; i < ps->eventSequence; i++ ) {
		entityState_t	es;
		memset( &es, 0, sizeof( es ) );
		es.number = ps->clientNum;
		es.weapon = ps->weapon;
		es.eventParm = ps->eventParms[i & ( MAX_PS_EVENTS - 1 )];
		EntityEvent( &es, ps->events[i & ( MAX_PS_EVENTS - 1 )] & ~EV_EVENT_BITS, qtrue );
	}
	hud.eventSequence = ps->eventSequence;
	// what the game added outside the movement (G_AddEvent: a powerup's pickup, and every pickup of a player
	// with cg_predictItems 0) is the playerState's externalEvent, its toggle bits telling one from the last
	if ( ps->externalEvent && ps->externalEvent != hud.externalEvent ) {
		entityState_t	es;
		memset( &es, 0, sizeof( es ) );
		es.number = ps->clientNum;
		es.weapon = ps->weapon;
		es.eventParm = ps->externalEventParm;
		EntityEvent( &es, ps->externalEvent & ~EV_EVENT_BITS, qtrue );
	}
	hud.externalEvent = ps->externalEvent;
	hud.eventSnap = cl.snap.messageNum;
	// a rail fired in this snapshot hit when PERS_HITS went up with it (one hit a shot, however many it went through)
	if ( hud.shots[WP_RAILGUN] > railShots && OwnPlay() ) {
		hud.railShots++;
		if ( ps->persistant[PERS_HITS] > hud.railPersHits && ps->weapon == WP_RAILGUN ) {
			hud.railHits++;
			hud.railRun++;
			hud.railBest = MAX( hud.railBest, hud.railRun );
		} else {
			hud.railRun = 0;
		}
	}
	// a game without dmg (id's, the mods' on community servers): G_Damage counts PERS_HITS once a damage
	// on a live enemy, so a shotgun's pellets or a rail through two count once a shot fired in this snapshot
	// (hitscan damages in the frame it fires), and no weapon gets more hits than shots
	if ( !hud.dmgFromServer && OwnPlay() && ps->persistant[PERS_HITS] > hud.railPersHits ) {
		int	w = ps->weapon, add = ps->persistant[PERS_HITS] - hud.railPersHits;
		if ( ( w <= WP_NONE || w >= NAMES || ( w != WP_GAUNTLET && hud.shots[w] == shotsBefore[w] ) ) && hud.lastFired > WP_NONE ) {
			w = hud.lastFired;
		}
		if ( w > WP_NONE && w < NAMES ) {
			if ( w == WP_SHOTGUN || w == WP_RAILGUN ) {
				add = MIN( add, MAX( hud.shots[w] - shotsBefore[w], 1 ) );
			}
			hud.statHits[w] += add;
			if ( w != WP_GAUNTLET && hud.statHits[w] > hud.shots[w] ) {
				hud.statHits[w] = hud.shots[w];
			}
		}
	}
	hud.railPersHits = ps->persistant[PERS_HITS];
}

static void LoadImages( void ) {
	static const char *medals[6] = { "menu/medals/medal_impressive", "menu/medals/medal_excellent", "menu/medals/medal_gauntlet",
		"menu/medals/medal_defend", "menu/medals/medal_assist", "menu/medals/medal_capture" };
	int	i;

	if ( img.init ) {
		return;
	}
	img.init = qtrue;
	for ( i = 1; i < NAMES; i++ ) {
		img.weapon[i] = ImageNamed( weaponIcons[i] );
	}
	img.noammo = ImageNamed( "icons/noammo" );
	for ( i = 0; i < 10; i++ ) {
		img.crosshairs[i] = ImageNamed( va( "gfx/2d/crosshair%c", 'a' + i ) );
	}
	for ( i = 0; i < 6; i++ ) {
		img.medals[i] = ImageNamed( medals[i] );
	}
	for ( i = 0; i < 3; i++ ) {
		img.flags[0][i] = ImageNamed( va( "icons/iconf_red%d", i + 1 ) );
		img.flags[1][i] = ImageNamed( va( "icons/iconf_blu%d", i + 1 ) );
	}
	// and the rest a match shows, so they're read ahead (oz_ui_hud.cpp Hud_Render) rather than at their first
	// show: the items' icons (a pickup) and the level shot (the board's header)
	for ( i = 1; i < (int)numHudItems; i++ ) {
		ImageNamed( hudItems[i].icon );
	}
	ImageNamed( va( "levelshots/%s", Info_ValueForKey( CS( CS_SERVERINFO ), "mapname" ) ) );
}

static int ItemIcon( int itemNum ) {
	if ( itemNum <= 0 || itemNum >= numHudItems ) {
		return 0;
	}
	return ImageNamed( hudItems[itemNum].icon );
}

// ================================================================ vitals (hud_vitals.gd)

static qboolean compact;

static void VitalBlock( float x, float y, float w, float h, qboolean isHealth, int value, const float *color, float ghost, qboolean low ) {
	const float	pad = 14 * u;
	const float	isz = ( compact ? 30.0f : 34.0f ) * u;
	const float	ix = x + pad + 6 * u, iy = y + h * ( compact ? 0.22f : 0.2f );
	const qboolean dim = value <= 0 && !isHealth;
	const float	size = (float)(int)( ( compact ? 46.0f : 56.0f ) * u );
	float		bx, by, c[4];
	char		num[16];

	if ( !compact ) {
		Panel( x, y, w, h, GLASS, isHealth ? RED : AMBER, 3 * u );
	}
	if ( isHealth ) {
		CrossIcon( ix, iy, isz, isz, dim ? FAINT : color );
	} else {
		ShieldIcon( ix, iy, isz, isz, dim ? FAINT : color );
	}
	bx = ix + isz + 12 * u;
	by = iy + isz * 0.5f + size * 0.36f;
	Com_sprintf( num, sizeof( num ), "%d", value > 0 ? value : 0 );
	if ( low ) {
		// Quake 3 flashes low health; a soft red glow behind the number
		const float a = oz_accessibility_reduced_flashing->integer ? 0.5f : 0.5f + 0.5f * (float)sin( hud.t * M_PI * 4 );
		const float gw = TextWidth( FONT_NUM, num, size );
		int i;
		for ( i = 0; i < 3; i++ ) {
			const float r = size * ( 0.5f - i * 0.15f );
			Col( c, RED, ( 0.05f + i * 0.025f ) * ( 0.6f + 0.4f * a ) );
			Disc( bx + gw * 0.5f, by - size * 0.36f, r + gw * 0.3f, c );
		}
	}
	Text( FONT_NUM, bx, by, num, size, dim ? FAINT : color, ALIGN_LEFT );
	if ( compact ) {
		return;
	}
	{
		// the bar: 10 segments per 100, the second hundred over the first in a cooler tone. Its ends run
		// parallel to the panel's slanted sides, `pad` in from them at the bar's bottom
		const float	bh = 9 * u;
		const float	bottom = h - 13 * u;
		const float	lean = h * SKEW;
		const float	left = x + lean * ( 1.0f - bottom / h ) + pad;
		const float	right = x + w - lean * bottom / h - pad;
		const float	barX = left, barY = y + bottom - bh, barW = right - left + bh * SKEW;
		const float	base = Clamp01( value / 100.0f );
		const float	over = Clamp01( ( value - 100 ) / 100.0f );
		const float	g = Clamp01( ghost / 100.0f );
		const float	s = bh * SKEW;
		static const vec4_t track = { 1, 1, 1, 0.07f };
		static const vec4_t gap = { 0, 0, 0, 0.55f };
		int			i;
		Bar( barX, barY, barW, bh, 0, 1, track );
		if ( g > base ) {
			Col( c, RED, 0.75f );
			Bar( barX, barY, barW, bh, base, g, c );
		}
		if ( value <= 100 ) {
			Col( c, color, 0.92f );
		} else {
			Col( c, INK_HOT, 0.9f );
		}
		Bar( barX, barY, barW, bh, 0, base, c );
		if ( over > 0 ) {
			const float th = 4 * u;
			vec4_t light;
			Mix( light, AMBER, WHITE, 0.3f );
			Bar( barX + s, barY - th, barW - s + th * SKEW, th, 0, over, isHealth ? MEGA : light );
		}
		for ( i = 1; i < 10; i++ ) {
			const float fx = barX + ( i / 10.0f ) * ( barW - s );
			Line( fx + s + 4 * u * SKEW, barY - 4 * u, fx, barY + bh, 2 * u, gap );
		}
	}
}

static void DrawVitals( float x, float y ) {
	const float	bw = ( compact ? 190.0f : 250.0f ) * u, bh = ( compact ? 66.0f : 92.0f ) * u;
	const qboolean low = hud.health <= 25 && hud.health > 0;
	const float	pulse = oz_accessibility_reduced_flashing->integer ? 0.0f : 0.5f + 0.5f * (float)sin( hud.t * M_PI * 4 );
	float		hc[4];
	const float	*ac = hud.armor > 0 && hud.armor < 50 ? RED : AMBER;

	if ( hud.health > 100 ) {
		Col( hc, MEGA, 1 );
	} else if ( low ) {
		Mix( hc, RED, GLOW, pulse );
	} else {
		Col( hc, INK_HOT, 1 );
	}
	VitalBlock( x, y, bw, bh, qtrue, hud.health, hc, hud.ghostH, low );
	VitalBlock( x + bw + 14 * u, y, bw, bh, qfalse, hud.armor, ac, hud.ghostA, qfalse );
}

// ================================================================ ammo (hud_ammo.gd)

static void DrawAmmo( float x, float y, float w, const playerState_t *ps ) {
	const float	rw = ( compact ? 160.0f : 200.0f ) * u, rh = ( compact ? 66.0f : 92.0f ) * u;
	const int	weapon = ps->weapon;
	const int	ammo = weapon > 0 && weapon < MAX_WEAPONS ? ps->ammo[weapon] : 0;
	const qboolean reloading = ps->weaponstate == WEAPON_FIRING && ps->weaponTime > 100;
	float		rx, isz, ix, iy, size, c[4];
	qboolean	infinite, low;
	const int	hold = ps->stats[STAT_HOLDABLE_ITEM];

	if ( weapon <= 0 || weapon >= NAMES ) {
		return;
	}
	rx = x + w - rw;
	if ( !compact ) {
		Panel( rx, y, rw, rh, GLASS, RED, 3 * u );
	}
	isz = ( compact ? 34.0f : 44.0f ) * u;
	ix = rx + rw * 0.12f;
	iy = y + rh * 0.18f;
	Image( img.weapon[weapon], ix, iy, isz, isz, WHITE );
	infinite = ammo < 0 || weapon == WP_GAUNTLET;
	low = !infinite && ammo <= lowAmmo[weapon];
	Col( c, reloading ? FAINT : low ? RED : INK_HOT, 1 );
	Mix( c, c, WHITE, hud.ammoFlash * 0.5f );
	size = (float)(int)( ( compact ? 46.0f : 56.0f ) * u );
	Text( FONT_NUM, rx + rw - 22 * u, iy + isz * 0.5f + size * 0.36f, infinite ? "-" : va( "%d", ammo ), size, c, ALIGN_RIGHT );
	if ( !compact ) {
		Col( c, DIM, 0.9f );
		Text( FONT_LABEL, rx + rw - 22 * u, y + rh - 10 * u, weaponNames[weapon], 15 * u, c, ALIGN_RIGHT );
	}
	if ( hold > 0 ) {
		const float hx = rx - 64 * u, hy = y + rh * 0.2f, hs = 50 * u;
		Panel( hx - 6 * u, hy - 6 * u, hs + 12 * u, hs + 12 * u, GLASS, NULL, 0 );
		Image( ItemIcon( hold ), hx, hy, hs, hs, WHITE );
	}
}

// ================================================================ weapon bar (hud_weapon_bar.gd)

static void DrawWeaponBar( float x, float y, float w, float h, const playerState_t *ps, qboolean fadeOut ) {
	const float	cw = 58 * u, ch = 64 * u, gap = 6 * u;
	float		alpha = 1.0f, total, cx, c[4];
	int			list[NAMES], n = 0, i;

	if ( fadeOut ) {
		alpha = Clamp01( ( 1.4f - hud.barShown ) / 0.3f );
		if ( alpha <= 0 ) {
			return;
		}
	}
	for ( i = 1; i < 10; i++ ) {
		if ( hud.barOwned & ( 1 << i ) ) {
			list[n++] = i;
		}
	}
	if ( !n ) {
		return;
	}
	total = n * cw + ( n - 1 ) * gap;
	cx = x + ( w - total ) * 0.5f;
	for ( i = 0; i < n; i++ ) {
		const int	wp = list[i];
		const float	k = hud.raise[wp];
		const float	ry = y + h - ch - k * 8 * u;
		const qboolean held = wp == hud.barWeapon;
		const qboolean empty = wp > 1 && ps->ammo[wp] == 0;
		const float	ia = alpha * ( empty ? 0.35f : held ? 1.0f : 0.7f );
		float		ic[4];
		Col( c, GLASS, alpha * ( held ? 1.0f : 0.75f ) );
		Panel( cx, ry, cw, ch, c, NULL, 0 );
		Col( ic, WHITE, ia );
		Image( img.weapon[wp], cx + cw * 0.5f - 16 * u, ry + 7 * u, 32 * u, 32 * u, ic );
		if ( empty ) {
			Col( ic, WHITE, alpha * 0.8f );
			Image( img.noammo, cx + cw * 0.5f - 16 * u, ry + 7 * u, 32 * u, 32 * u, ic );
		}
		if ( wp != WP_GAUNTLET && ps->ammo[wp] >= 0 ) {
			Col( c, held ? INK_HOT : DIM, alpha );
			Text( FONT_MEDIUM, cx + cw * 0.5f, ry + ch - 8 * u, va( "%d", ps->ammo[wp] ), 17 * u, c, ALIGN_CENTER );
		}
		if ( held ) {
			Col( c, RED, alpha );
			Bar( cx + 4 * u, ry + ch + 3 * u, cw - 8 * u, 3 * u, 0, 1, c );
		}
		cx += cw + gap;
	}
}

// ================================================================ speedometer (hud_speedometer.gd)

#define MAX_UPS		1000.0f
#define RUN_UPS		320.0f
#define FAST_UPS	600.0f
#define SPEED_A0	( (float)M_PI + 0.42f )
#define SPEED_A1	( (float)M_PI * 2 - 0.42f )

static void Heat( float ups, float *out ) {
	if ( ups <= RUN_UPS ) {
		Col( out, INK_HOT, 1 );
	} else if ( ups <= FAST_UPS ) {
		Mix( out, INK_HOT, AMBER, ( ups - RUN_UPS ) / ( FAST_UPS - RUN_UPS ) );
	} else {
		Mix( out, AMBER, GLOW, ( ups - FAST_UPS ) / 300.0f );
	}
}

static float SpeedAngle( float ups ) {
	return SPEED_A0 + ( SPEED_A1 - SPEED_A0 ) * Clamp01( ups / MAX_UPS );
}

static void DrawSpeedArc( float x, float y, float w, float h ) {
	const float	cx = x + w * 0.5f, cy = y + h - 30 * u;
	const float	r = 92 * u, th = 7 * u;
	float		c[4], d[2], a;
	int			v, i;
	static const vec4_t band = { 0.03f, 0.025f, 0.02f, 0.55f };
	static const vec4_t track = { 1, 1, 1, 0.13f };

	// backdrop: a glass band under the arc
	StrokeArc( cx, cy, ( r - th * 2 + r + th * 2.6f ) * 0.5f, SPEED_A0, SPEED_A1, 40, band, th * 4.6f );
	StrokeArc( cx, cy, r + th * 2.6f, SPEED_A0, SPEED_A1, 48, GLASS_EDGE, 1 );
	// track and ticks
	StrokeArc( cx, cy, r, SPEED_A0, SPEED_A1, 64, track, th );
	for ( v = 0; v <= (int)MAX_UPS; v += 100 ) {
		const qboolean major = v % 500 == 0;
		a = SpeedAngle( (float)v );
		d[0] = (float)cos( a );
		d[1] = (float)sin( a );
		Col( c, INK, major ? 0.75f : 0.4f );
		Line( cx + d[0] * ( r + th * 0.75f ), cy + d[1] * ( r + th * 0.75f ), cx + d[0] * ( r + th * ( major ? 2.1f : 1.45f ) ),
			cy + d[1] * ( r + th * ( major ? 2.1f : 1.45f ) ), 1.5f * u, c );
	}
	// the run speed mark
	a = SpeedAngle( RUN_UPS );
	d[0] = (float)cos( a );
	d[1] = (float)sin( a );
	Col( c, RED, 0.9f );
	Line( cx + d[0] * ( r - th * 0.9f ), cy + d[1] * ( r - th * 0.9f ), cx + d[0] * ( r + th * 1.9f ), cy + d[1] * ( r + th * 1.9f ), 2.5f * u, c );
	Col( c, INK, 0.8f );
	Text( FONT_MEDIUM, cx + d[0] * ( r + th * 4 ), cy + d[1] * ( r + th * 4 ) + 5 * u, "320", 12 * u, c, ALIGN_CENTER );
	// the fill, heating with speed
	{
		const int	steps = 48;
		const float	f = Clamp01( hud.speed / MAX_UPS );
		for ( i = 0; i < steps; i++ ) {
			const float t0 = (float)i / steps;
			float t1 = (float)( i + 1 ) / steps;
			if ( t0 >= f ) {
				break;
			}
			if ( t1 > f ) {
				t1 = f;
			}
			Heat( t1 * MAX_UPS, c );
			if ( hud.gaining > 0.01f && t1 * MAX_UPS > RUN_UPS ) {
				float g[4];
				Col( g, c, 0.16f * hud.gaining );
				StrokeArc( cx, cy, r, SPEED_A0 + ( SPEED_A1 - SPEED_A0 ) * t0, SPEED_A0 + ( SPEED_A1 - SPEED_A0 ) * t1, 4, g, th * 3.2f );
			}
			StrokeArc( cx, cy, r, SPEED_A0 + ( SPEED_A1 - SPEED_A0 ) * t0, SPEED_A0 + ( SPEED_A1 - SPEED_A0 ) * t1, 4, c, th );
		}
	}
	// the tip
	a = SpeedAngle( hud.speed );
	d[0] = (float)cos( a );
	d[1] = (float)sin( a );
	Line( cx + d[0] * ( r - th * 1.1f ), cy + d[1] * ( r - th * 1.1f ), cx + d[0] * ( r + th * 1.1f ), cy + d[1] * ( r + th * 1.1f ), 3 * u, WHITE );
	// peak hold
	if ( hud.peak > hud.speed + 8 ) {
		float	p[6], tip[2], side[2];
		a = SpeedAngle( hud.peak );
		d[0] = (float)cos( a );
		d[1] = (float)sin( a );
		tip[0] = cx + d[0] * ( r + th * 0.9f );
		tip[1] = cy + d[1] * ( r + th * 0.9f );
		side[0] = -d[1] * 5 * u;
		side[1] = d[0] * 5 * u;
		p[0] = tip[0]; p[1] = tip[1];
		p[2] = tip[0] + d[0] * 9 * u + side[0]; p[3] = tip[1] + d[1] * 9 * u + side[1];
		p[4] = tip[0] + d[0] * 9 * u - side[0]; p[5] = tip[1] + d[1] * 9 * u - side[1];
		Heat( hud.peak, c );
		c[3] = 0.9f;
		Poly( p, 3, c );
	}
	// the last two seconds, faint, behind the digits
	if ( hud.numHistory > 2 ) {
		float	pts[120 * 2];
		const float hw = r * 1.2f, hh = r * 0.4f;
		const float bx = cx - hw * 0.5f, by = cy - r * 0.62f + hh;
		for ( i = 0; i < hud.numHistory; i++ ) {
			pts[i * 2] = bx + hw * i / 119.0f;
			pts[i * 2 + 1] = by - hh * Clamp01( hud.history[i] / MAX_UPS );
		}
		Heat( hud.speed, c );
		c[3] = 0.22f;
		Strip( pts, hud.numHistory, 1.5f * u, c, qfalse );
	}
	// digits
	Heat( hud.speed, c );
	Text( FONT_NUM, cx, cy - r * 0.18f, va( "%d", (int)( hud.speed + 0.5f ) ), 40 * u, c, ALIGN_CENTER );
	Col( c, INK, 0.6f );
	Text( FONT_LABEL, cx, cy - r * 0.18f + 17 * u, "UPS", 12 * u, c, ALIGN_CENTER );
	// the strafe-jump readout
	if ( hud.jump >= 0 ) {
		const float alpha = Clamp01( 1.0f - ( hud.jumpT - 2.5f ) / 0.8f );
		if ( alpha > 0 ) {
			const float jy = cy + 22 * u;
			Col( c, DIM, alpha );
			Text( FONT_LABEL, cx - 12 * u, jy, "JUMP", 12 * u, c, ALIGN_RIGHT );
			Heat( hud.jump, c );
			c[3] = alpha;
			Text( FONT_MEDIUM, cx - 64 * u, jy, va( "%d", (int)( hud.jump + 0.5f ) ), 17 * u, c, ALIGN_RIGHT );
			if ( fabs( hud.jumpDelta ) >= 1 ) {
				const qboolean gain = hud.jumpDelta > 0;
				Col( c, gain ? MEGA : RED, alpha );
				Text( FONT_MEDIUM, cx + 12 * u, jy, gain ? va( "+%d", (int)( hud.jumpDelta + 0.5f ) ) : va( "%d", (int)( hud.jumpDelta - 0.5f ) ),
					17 * u, c, ALIGN_LEFT );
			}
		}
	}
}

static void DrawSpeedCompact( float x, float y, float w, float h ) {
	const float	cx = x + w * 0.5f;
	const float	bx = cx - 60 * u, by = y + h - 10 * u, bw = 120 * u, bh = 4 * u;
	const float	rx = bx + bw * RUN_UPS / MAX_UPS;
	static const vec4_t track = { 1, 1, 1, 0.1f };
	float		c[4];

	Heat( hud.speed, c );
	Text( FONT_NUM, cx, y + h - 18 * u, va( "%d", (int)( hud.speed + 0.5f ) ), 30 * u, c, ALIGN_CENTER );
	Rect( bx, by, bw, bh, track );
	Rect( bx, by, bw * Clamp01( hud.speed / MAX_UPS ), bh, c );
	Line( rx, by - 3 * u, rx, by + bh + 3 * u, 2 * u, RED );
}

static void UpdateSpeed( float target ) {
	const float before = hud.speed;
	int			i;

	// eased toward the speed by the same share of the way each second at any frame rate (a share of the way
	// each frame went faster at low frame rates), and gaining while it climbs 30 ups a second or more (it was
	// 0.5 ups a frame, which a frame at 1000 fps never climbs)
	hud.speed += ( target - hud.speed ) * ( 1.0f - expf( -18.0f * hud.dt ) );
	hud.gaining = Clamp01( hud.gaining + ( hud.speed - before > 30.0f * hud.dt && hud.dt > 0 && hud.speed > RUN_UPS ? 0.6f : -1.2f ) * hud.dt * 4 );
	if ( hud.speed >= hud.peak ) {
		hud.peak = hud.speed;
		hud.peakHold = 1.2f;
	} else {
		hud.peakHold -= hud.dt;
		if ( hud.peakHold <= 0 ) {
			hud.peak = hud.peak - 450 * hud.dt > hud.speed ? hud.peak - 450 * hud.dt : hud.speed;
		}
	}
	hud.historyT += hud.dt;
	while ( hud.historyT >= 1.0f / HISTORY_RATE ) {
		hud.historyT -= 1.0f / HISTORY_RATE;
		if ( hud.numHistory == 120 ) {
			for ( i = 1; i < 120; i++ ) {
				hud.history[i - 1] = hud.history[i];
			}
			hud.numHistory--;
		}
		hud.history[hud.numHistory++] = hud.speed;
	}
	hud.jumpT += hud.dt;
}

// ================================================================ kill feed (hud_feed.gd)

static const float *NameColor( qboolean me, int team ) {
	if ( me ) {
		return AMBER;
	}
	if ( team == 1 ) {
		return HUD_TEAM_RED;
	}
	if ( team == 2 ) {
		return HUD_TEAM_BLUE;
	}
	return INK;
}

static void DrawFeed( float fx, float fy, float fw ) {
	const float	h = 34 * u, fs = 19 * u, iconW = 30 * u, gap = 10 * u;
	float		y = fy, c[4];
	int			i;

	for ( i = 0; i < hud.numFeed; i++ ) {
		feedRow_t	*row = &hud.feed[i];
		const float	t = row->t;
		const float	alpha = Clamp01( ( FEED_LIFE - t ) / 0.6f ) * Clamp01( t / 0.12f );
		const float	slide = ( 1.0f - Clamp01( t / 0.18f ) ) * 40 * u;
		const int	w = ModWeapon( row->mod );
		const char	*tag = w ? NULL : ModTag( row->mod );
		const float	midW = tag ? TextWidth( FONT_LABEL, tag, 13 * u ) + 6 * u : iconW;
		const float	aw = row->a[0] ? CTextWidth( FONT_MEDIUM, row->a, fs ) : 0;
		const float	vw = CTextWidth( FONT_MEDIUM, row->v, fs );
		const float	rw = aw + ( row->a[0] ? gap : 0 ) + midW + gap + vw + 36 * u;
		const float	rx = fx + fw - rw + slide;
		const qboolean involved = row->am || row->vm;
		const float	base = y + h * 0.5f + fs * 0.36f;
		float		x;
		if ( t >= FEED_LIFE ) {
			continue;
		}
		Col( c, GLASS, alpha * ( involved ? 1.0f : 0.8f ) );
		Panel( rx, y, rw, h, c, NULL, 0 );
		if ( involved ) {
			Col( c, row->am ? AMBER : RED, 0.8f * alpha );
			Bar( rx, y + h - 3 * u, rw, 3 * u, 0, 1, c );
		}
		x = rx + 18 * u;
		if ( row->a[0] ) {
			Col( c, NameColor( row->am, row->at ), alpha );
			CText( FONT_MEDIUM, x, base, row->a, fs, c, ALIGN_LEFT, 0 );
			x += aw + gap;
		}
		if ( !tag ) {
			Col( c, WHITE, alpha );
			Image( img.weapon[w], x, y + ( h - iconW ) * 0.5f, iconW, iconW, c );
		} else {
			Col( c, RED, alpha );
			Text( FONT_LABEL, x, y + h * 0.5f + 5 * u, tag, 13 * u, c, ALIGN_LEFT );
		}
		x += midW + gap;
		Col( c, NameColor( row->vm, row->vt ), alpha );
		CText( FONT_MEDIUM, x, base, row->v, fs, c, ALIGN_LEFT, 0 );
		y += h + 5 * u;
	}
}

// ================================================================ damage (hud_damage.gd)

#define DAMAGE_LIFE	1.1f

static void DamageFeedback( const playerState_t *ps ) {
	const int	count = ps->damageCount;
	const float	strength = count / 50.0f < 0.25f ? 0.25f : count / 50.0f > 1 ? 1 : count / 50.0f;
	int			i;

	hud.attackerT = 0;
	if ( hud.numHits == MAX_HITS ) {
		for ( i = 1; i < MAX_HITS; i++ ) {
			hud.hits[i - 1] = hud.hits[i];
		}
		hud.numHits--;
	}
	hud.hits[hud.numHits].s = strength;
	hud.hits[hud.numHits].t = 0;
	hud.hits[hud.numHits].all = ps->damageYaw == 255 && ps->damagePitch == 255;
	if ( !hud.hits[hud.numHits].all ) {
		// the direction the damage came from, against the view (CG_DamageFeedback)
		vec3_t	angles, dir, vf, vr;
		float	front, left;
		angles[PITCH] = ps->damagePitch / 255.0f * 360;
		angles[YAW] = ps->damageYaw / 255.0f * 360;
		angles[ROLL] = 0;
		AngleVectors( angles, dir, NULL, NULL );
		VectorNegate( dir, dir );
		AngleVectors( ps->viewangles, vf, vr, NULL );
		front = DotProduct( dir, vf );
		left = -DotProduct( dir, vr );
		hud.hits[hud.numHits].angle = (float)atan2( -left, front );
	}
	hud.numHits++;
	if ( strength > hud.flash ) {
		hud.flash = strength;
	}
}

static void DrawDamage( float W, float H, qboolean lowHealth ) {
	const float	k = H / 1080.0f;
	const float	cx = W * 0.5f, cy = H * 0.5f, r = 130 * k;
	const qboolean calm = oz_accessibility_reduced_flashing->integer != 0;
	float		edge = hud.flash * ( calm ? 0.2f : 0.45f ), c[4];
	int			i;

	if ( lowHealth ) {
		const float p = calm ? 0.12f : 0.12f + 0.08f * (float)sin( hud.t * M_PI * 2 * 1.2f );
		if ( p > edge ) {
			edge = p;
		}
	}
	if ( edge > 0.005f ) {
		const float band = H * 0.16f;
		float	col[4], clr[4];
		float	a[2], b[2], cc[2], d[2];
		Col( col, RED, edge );
		Col( clr, RED, 0 );
		a[0] = 0; a[1] = 0; b[0] = W; b[1] = 0; cc[0] = W; cc[1] = band; d[0] = 0; d[1] = band;
		Quad4( a, b, cc, d, col, col, clr, clr );
		a[0] = 0; a[1] = H - band; b[0] = W; b[1] = H - band; cc[0] = W; cc[1] = H; d[0] = 0; d[1] = H;
		Quad4( a, b, cc, d, clr, clr, col, col );
		a[0] = 0; a[1] = 0; b[0] = band; b[1] = 0; cc[0] = band; cc[1] = H; d[0] = 0; d[1] = H;
		Quad4( a, b, cc, d, col, clr, clr, col );
		a[0] = W - band; a[1] = 0; b[0] = W; b[1] = 0; cc[0] = W; cc[1] = H; d[0] = W - band; d[1] = H;
		Quad4( a, b, cc, d, clr, col, col, clr );
	}
	for ( i = 0; i < hud.numHits; i++ ) {
		float	a = Clamp01( 1.0f - hud.hits[i].t / DAMAGE_LIFE );
		a = a * a * hud.hits[i].s;
		if ( hud.hits[i].all ) {
			Col( c, RED, a * 0.6f );
			StrokeArc( cx, cy, r, 0, (float)M_PI * 2, 64, c, 6 * k );
		} else {
			const float mid = hud.hits[i].angle - (float)M_PI * 0.5f;
			const float spread = 0.42f;
			Col( c, GLOW, a );
			StrokeArc( cx, cy, r, mid - spread, mid + spread, 20, c, 9 * k );
			Col( c, RED, a * 0.7f );
			StrokeArc( cx, cy, r + 10 * k, mid - spread * 0.6f, mid + spread * 0.6f, 14, c, 4 * k );
		}
	}
}

// ================================================================ crosshair (hud_crosshair.gd)

#define CROSS_PULSE_TIME	0.2f

// the crosshair editor's weapon tab, shown in the match behind it while the editor says so each frame
static int		crossWeapon, crossWeaponTime;

static void HealthColor( int h, float *c ) {
	c[0] = c[1] = c[2] = c[3] = 1;
	if ( h <= 0 ) {
		c[0] = c[1] = c[2] = 0;
		return;
	}
	c[2] = h >= 100 ? 1.0f : h < 66 ? 0.0f : ( h - 66 ) / 33.0f;
	c[1] = h > 60 ? 1.0f : h < 30 ? 0.0f : ( h - 30 ) / 30.0f;
}

// the hit flash's length (Settings: Flash length, in ms)
static float CrossHitTime( void ) {
	const float t = oz_crosshair_hit_time->value / 1000.0f;
	return t > 0.05f ? t : 0.05f;
}

// a weapon's crosshair: its own with per-weapon crosshairs on, else the main one
static const char *CrosshairShape( int weapon ) {
	if ( oz_crosshair_per_weapon->integer && weapon > 0 && weapon < WP_NUM_WEAPONS && crosshairWeapons[weapon][0] ) {
		const char *wsh = oz_crosshair_weapon[weapon]->string;
		if ( wsh[0] && Q_stricmp( wsh, "default" ) ) {
			return wsh;
		}
	}
	return oz_crosshair_shape->string;
}

// the center dot (Center dot, or the dot shape): at least two 1080p pixels across, as thick as the lines
static void CrossDot( float cx, float cy, float k, const char *shape, float t, float ow, const float *color, const float *outline ) {
	float d = t > 2 * k ? t : 2 * k;
	if ( !Q_stricmp( shape, "dot" ) ) {
		d *= 1.6f;
	}
	if ( oz_crosshair_outline->integer ) {
		Disc( cx, cy, d + ow, outline );
	}
	Disc( cx, cy, d, color );
}

/*
The crosshair centered at (cx, cy), k screen pixels to a 1080p pixel, sz across (screen pixels); health
(armor counted) for Color by health, hitT seconds since a hit. The HUD's and the editors' previews
(OzHud_CrosshairPreview), so a preview is what the match shows.
*/
#define HIT_MARKER_TIME	0.3f	// the hit marker's shortest fade (a short Flash length still shows it)
#define HIT_MARKER_POP	0.08f	// it starts a little wider and settles in this long

/*
The hit marker: four short strokes on the diagonals around the crosshair, in the hit color, each time
you hurt an enemy (PERS_HITS going up, as the hit flash). It stands outside the crosshair (sz across)
and shows with no crosshair too.
*/
static void HitMarker( float cx, float cy, float k, float sz, float hitT ) {
	const float	life = CrossHitTime() > HIT_MARKER_TIME ? CrossHitTime() : HIT_MARKER_TIME;
	const float	d = 0.70710678f;
	float		hit[4], outline[4], a, r0, r1, w, ow;
	int			i, pass;

	if ( !oz_crosshair_hit_marker->integer || hitT < 0 || hitT >= life ) {
		return;
	}
	a = 1.0f - hitT / life;
	a = a * ( 2.0f - a );	// eases out: full a while, then gone
	r0 = sz * 0.5f + 4 * k;
	if ( r0 < 9 * k ) {
		r0 = 9 * k;
	}
	if ( hitT < HIT_MARKER_POP ) {
		r0 += 5 * k * ( 1.0f - hitT / HIT_MARKER_POP );
	}
	r1 = r0 + 9 * k;
	w = 2.5f * k > 1.5f ? 2.5f * k : 1.5f;
	ow = k > 1 ? k : 1;
	ParseColor( oz_crosshair_hit_color->string, hit );
	hit[3] *= a;
	Col( outline, BLACK, 0.6f * a );
	for ( pass = 0; pass < 2; pass++ ) {
		for ( i = 0; i < 4; i++ ) {
			const float sx = ( i & 1 ) ? d : -d, sy = ( i & 2 ) ? d : -d;
			const float e = pass ? 0 : ow;
			Line( cx + sx * ( r0 - e ), cy + sy * ( r0 - e ), cx + sx * ( r1 + e ), cy + sy * ( r1 + e ),
				pass ? w : w + 2 * ow, pass ? hit : outline );
		}
	}
}

static void CrosshairAt( float cx, float cy, float k, const char *shape, float sz, int health, float hitT ) {
	float		color[4], hit[4], outline[4], t, gap, arm, ow, oa;
	const float	saved = hudAlpha;

	hudAlpha = 1.0f;
	HitMarker( cx, cy, k, sz, hitT );
	if ( !shape[0] || !Q_stricmp( shape, "none" ) ) {
		hudAlpha = saved;
		return;
	}
	ParseColor( oz_crosshair_color->string, color );
	if ( oz_crosshair_health->integer ) {
		HealthColor( health, color );
	}
	if ( oz_crosshair_hit_flash->integer && hitT < CrossHitTime() ) {
		ParseColor( oz_crosshair_hit_color->string, hit );
		Mix( color, color, hit, 1.0f - hitT / CrossHitTime() );
	}
	color[3] *= Com_Clamp( 0.0f, 1.0f, oz_crosshair_opacity->value );
	// the outline: its width each side (at least a screen pixel) and its darkness
	ow = oz_crosshair_outline_width->value * k;
	if ( ow < 1 ) {
		ow = 1;
	}
	oa = Com_Clamp( 0.0f, 1.0f, oz_crosshair_outline_opacity->value ) * color[3];
	t = oz_crosshair_thickness->value * k;
	if ( t < 1 ) {
		t = 1;
	}
	if ( !shape[1] && shape[0] >= 'a' && shape[0] <= 'j' ) {
		const int n = shape[0] - 'a';
		const int image = img.crosshairs[n] ? img.crosshairs[n] : ImageNamed( va( "gfx/2d/crosshair%c", 'a' + n ) );
		const float x = cx - sz * 0.5f, y = cy - sz * 0.5f;
		if ( oz_crosshair_outline->integer ) {
			// the image moved out a pixel each way (Godot's); a wider outline is rings of it, the
			// diagonals too, each ring lighter so the overlaps add up to the outline's darkness
			static const float dirs[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
				{ 0.7071f, 0.7071f }, { -0.7071f, 0.7071f }, { 0.7071f, -0.7071f }, { -0.7071f, -0.7071f } };
			const int rings = (int)ceil( ow - 0.01f ), nd = ow > 1.01f ? 8 : 4;
			int r, d;
			Col( outline, BLACK, nd == 4 ? oa : 1.0f - (float)pow( 1.0f - oa, 1.0f / ( 2 * rings ) ) );
			for ( r = 1; r <= rings; r++ ) {
				const float o = r < rings ? (float)r : ow;
				for ( d = 0; d < nd; d++ ) {
					Image( image, x + dirs[d][0] * o, y + dirs[d][1] * o, sz, sz, outline );
				}
			}
		}
		Image( image, x, y, sz, sz, color );
		// Center dot over a classic image too (thickness sizes it)
		if ( oz_crosshair_dot->integer ) {
			Col( outline, BLACK, oa );
			CrossDot( cx, cy, k, shape, t, ow, color, outline );
		}
		hudAlpha = saved;
		return;
	}
	gap = oz_crosshair_gap->value * k;
	arm = sz * 0.5f;
	Col( outline, BLACK, oa );
	if ( !Q_stricmp( shape, "cross" ) || !Q_stricmp( shape, "t" ) ) {
		const qboolean full = !Q_stricmp( shape, "cross" );
		int pass;
		for ( pass = oz_crosshair_outline->integer ? 0 : 1; pass < 2; pass++ ) {
			const float *c = pass ? color : outline;
			const float lw = pass ? t : t + 2 * ow;
			const float e = pass ? 0 : ow;
			Rect( cx + gap - e, cy - lw * 0.5f, arm - gap + 2 * e, lw, c );
			Rect( cx - arm - e, cy - lw * 0.5f, arm - gap + 2 * e, lw, c );
			Rect( cx - lw * 0.5f, cy + gap - e, lw, arm - gap + 2 * e, c );
			if ( full ) {
				Rect( cx - lw * 0.5f, cy - arm - e, lw, arm - gap + 2 * e, c );
			}
		}
	}
	if ( !Q_stricmp( shape, "circle" ) ) {
		if ( oz_crosshair_outline->integer ) {
			StrokeArc( cx, cy, arm, 0, (float)M_PI * 2, 48, outline, t + 2 * ow );
		}
		StrokeArc( cx, cy, arm, 0, (float)M_PI * 2, 48, color, t );
	}
	if ( !Q_stricmp( shape, "dot" ) || oz_crosshair_dot->integer ) {
		CrossDot( cx, cy, k, shape, t, ow, color, outline );
	}
	hudAlpha = saved;
}

static void DrawCrosshair( float W, float H, const playerState_t *ps ) {
	const float	k = H / 1080.0f;
	const int	h = ps->stats[STAT_HEALTH];
	int			arm = ps->stats[STAT_ARMOR];
	float		sz = cg_crosshairSize->value * k;

	if ( hud.pulseT < CROSS_PULSE_TIME ) {
		sz *= 1.0f + hud.pulseT / CROSS_PULSE_TIME;
	}
	// CG_ColorForHealth counts the armor that would still protect
	if ( arm > (int)( h * 0.66f / 0.34f ) ) {
		arm = (int)( h * 0.66f / 0.34f );
	}
	CrosshairAt( W * 0.5f, H * 0.5f, k, CrosshairShape( crossWeapon && cls.realtime - crossWeaponTime < 250 ? crossWeapon : cg.weaponSelect ),
		sz, h > 0 ? h + arm : 0, hud.hitT );
}

// the crosshair editor, each frame while it's open over a match: its weapon tab (0: the weapon in hand)
void OzHud_CrosshairWeapon( int weapon ) {
	crossWeapon = weapon > 0 && weapon < WP_NUM_WEAPONS ? weapon : 0;
	crossWeaponTime = cls.realtime;
}

void OzHud_CrosshairTestHit( void ) {
	hud.hitT = 0;
}

// the crosshair as the HUD draws it, its triangles handed to sink instead (the editors' previews)
void OzHud_CrosshairPreview( float cx, float cy, float k, const char *shape, int weapon, float size, float hitT,
	void ( *sink )( const ozHudVertex_t *verts, int numVerts, int image, void *ctx ), void *ctx ) {
	Flush();
	hudSink = sink;
	hudSinkCtx = ctx;
	CrosshairAt( cx, cy, k, shape ? shape : CrosshairShape( weapon ), ( size > 0 ? size : cg_crosshairSize->value ) * k, 100, hitT );
	Flush();
	hudSink = NULL;
	hudSinkCtx = NULL;
}

// ================================================================ the match status (hud_status.gd)

static void ScoreBox( float x, float y, float w, float h, const char *label, int score, const float *color, qboolean mine ) {
	float	c[4];

	if ( mine ) {
		c[0] = color[0] * 0.25f;
		c[1] = color[1] * 0.25f;
		c[2] = color[2] * 0.25f;
		c[3] = 0.7f;
		Panel( x, y, w, h, c, NULL, 0 );
	} else {
		Panel( x, y, w, h, GLASS, NULL, 0 );
	}
	Col( c, color, 0.85f );
	Bar( x, y + h - 3 * u, w, 3 * u, 0, 1, c );
	Col( c, color, 0.95f );
	CText( FONT_LABEL, x + 22 * u, y + h * 0.5f + 5 * u, label, 13 * u, c, ALIGN_LEFT, w - 62 * u );
	Text( FONT_NUM, x + w - 18 * u, y + h * 0.5f + 10 * u, va( "%d", score ), 28 * u, INK_HOT, ALIGN_RIGHT );
}

static void FlagIcon( float bx, float by, float bw, float bh, int team, int state ) {
	if ( state < 0 || state > 2 ) {
		return;
	}
	Image( img.flags[team][state], bx + bw * 0.5f - 16 * u, by + bh + 4 * u, 32 * u, 32 * u, WHITE );
}

// the other score beside yours: the leader, or the runner-up when you lead (from the scores)
static int OtherPlayer( int me ) {
	int	i;

	for ( i = 0; i < cg.numScores; i++ ) {
		if ( cg.scores[i].client != me && cgs.clientinfo[cg.scores[i].client].infoValid
			&& cgs.clientinfo[cg.scores[i].client].team != TEAM_SPECTATOR ) {
			return cg.scores[i].client;
		}
	}
	return -1;
}

static int ScoreOf( int client ) {
	int	i;

	for ( i = 0; i < cg.numScores; i++ ) {
		if ( cg.scores[i].client == client ) {
			return cg.scores[i].score;
		}
	}
	return 0;
}

static void DrawStatus( float W, const playerState_t *ps ) {
	const float	y0 = 18 * u, cx = W * 0.5f;
	const float	clockX = cx - 62 * u, clockW = 124 * u, clockH = 46 * u;
	const float	bw = 172 * u;
	const float	lx = clockX - bw - 8 * u, rx = clockX + clockW + 8 * u, by = y0 + 4 * u, bh = 38 * u;
	int			elapsed = cg.time - cgs.levelStartTime, ms, secs;
	qboolean	warn = qfalse;
	char		clock[32], label[64];
	float		c[4];

	if ( elapsed < 0 ) {
		elapsed = 0;
	}
	ms = elapsed;
	if ( cgs.timelimit > 0 ) {
		ms = cgs.timelimit * 60000 - elapsed;
		if ( ms < 0 ) {
			ms = 0;
		}
		warn = ms < 60000;
	}
	secs = ms / 1000;
	Com_sprintf( clock, sizeof( clock ), "%d:%02d", secs / 60, secs % 60 );
	if ( cg.warmup ) {
		Q_strncpyz( clock, cg.warmup < 0 ? "WARMUP" : "0:00", sizeof( clock ) );
		warn = qfalse;
	}
	Panel( clockX, y0, clockW, clockH, GLASS, NULL, 0 );
	Text( FONT_NUM, cx, y0 + clockH * 0.5f + 11 * u, clock, 31 * u, warn ? RED : INK_HOT, ALIGN_CENTER );
	if ( cgs.gametype == GT_CTF && cgs.capturelimit > 0 ) {
		Col( c, DIM, 0.9f );
		Text( FONT_LABEL, cx, y0 + clockH + 16 * u, va( "FIRST TO %d", cgs.capturelimit ), 12 * u, c, ALIGN_CENTER );
	} else if ( cgs.gametype != GT_CTF && cgs.fraglimit > 0 ) {
		Col( c, DIM, 0.9f );
		Text( FONT_LABEL, cx, y0 + clockH + 16 * u, va( "FRAG LIMIT %d", cgs.fraglimit ), 12 * u, c, ALIGN_CENTER );
	}
	if ( cgs.gametype >= GT_TEAM ) {
		const int team = ps->persistant[PERS_TEAM];
		ScoreBox( lx, by, bw, bh, "RED", cgs.scores1, HUD_TEAM_RED, team == TEAM_RED );
		ScoreBox( rx, by, bw, bh, "BLUE", cgs.scores2, HUD_TEAM_BLUE, team == TEAM_BLUE );
		if ( cgs.gametype == GT_CTF ) {
			FlagIcon( lx, by, bw, bh, 0, cgs.redflag );
			FlagIcon( rx, by, bw, bh, 1, cgs.blueflag );
		}
	} else if ( ps->persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
		// a free spectator: the leader and the runner-up
		int i;
		for ( i = 0; i < cg.numScores && i < 2; i++ ) {
			PlayerName( cg.scores[i].client, label, sizeof( label ) );
			Upper( label );
			ScoreBox( i ? rx : lx, by, bw, bh, label, cg.scores[i].score, INK, qfalse );
		}
	} else {
		const int other = OtherPlayer( ps->clientNum );
		Q_strncpyz( label, Place( ps->persistant[PERS_RANK] ), sizeof( label ) );
		ScoreBox( lx, by, bw, bh, label, ps->persistant[PERS_SCORE], AMBER, qtrue );
		if ( other >= 0 ) {
			int score = ScoreOf( other );
			// the live scores: CS_SCORES1 is the leader's, CS_SCORES2 the runner-up's
			if ( ( ps->persistant[PERS_RANK] & ~RANK_TIED_FLAG ) == 0 ) {
				score = cgs.scores2 != SCORE_NOT_PRESENT ? cgs.scores2 : score;
			} else {
				score = cgs.scores1 != SCORE_NOT_PRESENT ? cgs.scores1 : score;
			}
			PlayerName( other, label, sizeof( label ) );
			Upper( label );
			ScoreBox( rx, by, bw, bh, label, score, INK, qfalse );
		}
	}
}

// Defrag's run timer under the match clock: white while running, amber once finished, until the
// next spawn. A death ends the run (Defrag starts it again from the start line), so it goes then
static void DrawDefragTimer( float W, const playerState_t *ps ) {
	const float	w = 210 * u, h = 44 * u, x = ( W - w ) * 0.5f, y = 94 * u;
	const qboolean	done = df.time >= 0;
	int			ms;
	char		t[32];

	if ( !df.start || ( !done && ps->pm_type == PM_DEAD ) ) {
		return;
	}
	ms = done ? df.time : cg.time - df.start;
	if ( ms < 0 ) {
		ms = 0;
	}
	Com_sprintf( t, sizeof( t ), "%d:%02d.%03d", ms / 60000, ms / 1000 % 60, ms % 1000 );
	Panel( x, y, w, h, GLASS, done ? AMBER : NULL, 3 * u );
	Text( FONT_NUM, W * 0.5f, y + h * 0.5f + 10 * u, t, 28 * u, done ? AMBER : INK_HOT, ALIGN_CENTER );
}

// ================================================================ chat (hud_chat.gd)

#define CHAT_OPEN_LINES		10
#define MAX_WRAPPED			24
#define CHAT_TEXT			( ( MAX_SAY_TEXT + 64 ) * 2 )	// a line with its smileys as emoji (2 bytes to 4)
#define MAX_SUGGESTIONS		8

// codes and smileys show as emoji (oz_emoji.c), where the setting and the UI's fonts allow
static qboolean ChatEmoji( void ) {
	return oz_hud_chat_emoji->integer && OzUI_EmojiFont() ? qtrue : qfalse;
}

// one line cut to the width at word ends; every piece after the first starts with the color in
// force where it was cut
static int Wrap( const char *text, float fs, float width, char out[][CHAT_TEXT], int max ) {
	char		line[CHAT_TEXT], word[CHAT_TEXT], test[CHAT_TEXT * 2];
	char		color[3];
	const char	*s = text, *p;
	int			n = 0, len;

	line[0] = 0;
	color[0] = 0;
	while ( *s && n < max ) {
		const char *w;
		while ( *s == ' ' ) {
			s++;
		}
		w = s;
		while ( *s && *s != ' ' ) {
			s++;
		}
		len = s - w;
		if ( !len ) {
			break;
		}
		if ( len >= (int)sizeof( word ) ) {
			len = sizeof( word ) - 1;
		}
		memcpy( word, w, len );
		word[len] = 0;
		Com_sprintf( test, sizeof( test ), line[0] ? "%s %s" : "%s%s", line, word );
		if ( line[0] && CTextWidth( FONT_MEDIUM, test, fs ) > width ) {
			// the color in force at the end of the line carries on
			for ( p = line; *p; p++ ) {
				if ( p[0] == '^' && p[1] && p[1] != '^' ) {
					color[0] = '^';
					color[1] = p[1];
					color[2] = 0;
				}
			}
			Q_strncpyz( out[n++], line, sizeof( out[0] ) );
			Com_sprintf( line, sizeof( line ), "%s%s", color, word );
		} else {
			Q_strncpyz( line, test, sizeof( line ) );
		}
	}
	if ( line[0] && n < max ) {
		Q_strncpyz( out[n++], line, sizeof( out[0] ) );
	}
	return n;
}

static void ChatLine( float x, float y, const char *text, float fs, float alpha ) {
	float	c[4];

	Col( c, INK, alpha );
	BlackBacking( FONT_MEDIUM, x, y, text, fs, 1.0f, c, ALIGN_LEFT, 0 );
	TextFlags( FONT_MEDIUM, x, y, text, fs, c, ALIGN_LEFT, 0.7f, 0 );
}

static void DrawChat( float x, float y, float w, float h ) {
	static char	wrapped[MAX_WRAPPED][CHAT_TEXT];
	static char	shown[CHAT_OPEN_LINES + 12][CHAT_TEXT];
	float		shownAlpha[CHAT_OPEN_LINES + 12];
	const float	fs = 19 * u, lh = 25 * u, pad = 14 * u;
	const float	wrapW = w - pad * 2;
	char		field[MAX_SAY_TEXT + 64], line[CHAT_TEXT];
	int			mode = 0, cursor = 0, numShown = 0, maxLines, i, j;
	const float	life = con_notifyTime->value * 1000;
	const qboolean emoji = ChatEmoji();
	float		inputH, bottom, ly, c[4];
	qboolean	typing;

	field[0] = 0;
	{
		const int r = CL_OzChatField( field, sizeof( field ) );
		mode = r & 15;
		cursor = r >> 4;
	}
	typing = mode != 0;
	maxLines = typing ? CHAT_OPEN_LINES : oz_hud_chat_lines->integer;
	if ( maxLines > CHAT_OPEN_LINES + 2 ) {
		maxLines = CHAT_OPEN_LINES + 2;
	}
	// the newest lines, wrapped, bottom up
	for ( i = hud.numChat - 1; i >= 0 && numShown < maxLines; i-- ) {
		const float age = (float)( cg.time - hud.chat[i].at );
		const char *text = hud.chat[i].text;
		float alpha = 1.0f;
		int n;
		if ( !typing ) {
			if ( age >= life ) {
				break;
			}
			alpha = Clamp01( ( life - age ) / 600.0f );
		}
		if ( emoji ) {
			OzEmoji_Expand( text, line, sizeof( line ), -1, NULL );
			text = line;
		}
		n = Wrap( text, fs, wrapW, wrapped, MAX_WRAPPED );
		for ( j = n - 1; j >= 0 && numShown < maxLines; j-- ) {
			Q_strncpyz( shown[numShown], wrapped[j], sizeof( shown[0] ) );
			shownAlpha[numShown++] = alpha;
		}
	}
	inputH = typing ? 36 * u : 0;
	bottom = y + h - inputH;
	if ( typing ) {
		const qboolean team = mode == 2;
		static const vec4_t teamColor = { 0, 1, 1, 1 };
		const char *label = team ? "SAY TEAM:" : mode == 3 ? "TELL:" : "SAY:";
		const float lfs = 14 * u;
		const qboolean preview = strchr( field, '^' ) != NULL;
		int suggested[MAX_SUGGESTIONS], numSuggested = 0, caret = cursor, start;
		const char *names[MAX_SUGGESTIONS];
		const char *typed = field;
		float lx, before, bodyH;
		char head[CHAT_TEXT];
		// a code being typed (":fi"): the emoji it may be go above the line, and Tab takes the first
		start = emoji ? OzEmoji_Typing( field, cursor ) : -1;
		if ( start >= 0 ) {
			numSuggested = OzEmoji_Suggest( field + start, cursor - start, suggested, names, MAX_SUGGESTIONS );
		}
		bodyH = ( numShown + ( preview ? 1 : 0 ) + ( numSuggested ? 1 : 0 ) ) * lh;
		Col( c, GLASS, 0.72f );
		Rect( x, bottom - bodyH - pad * 0.6f, w, bodyH + pad * 0.6f + inputH, c );
		Col( c, team ? teamColor : RED, 0.85f );
		Rect( x, bottom - bodyH - pad * 0.6f, 3 * u, bodyH + pad * 0.6f + inputH, c );
		Col( c, BLACK, 0.35f );
		Rect( x, y + h - inputH, w, inputH, c );
		Col( c, team ? teamColor : AMBER, 0.95f );
		Text( FONT_LABEL, x + pad, y + h - inputH * 0.5f + lfs * 0.38f, label, lfs, c, ALIGN_LEFT );
		lx = x + pad + TextWidth( FONT_LABEL, label, lfs ) + 10 * u;
		// what is typed, as typed (color codes shown, emoji as emoji but the one the caret is in), and the caret
		if ( emoji ) {
			OzEmoji_Expand( field, line, sizeof( line ), cursor, &caret );
			typed = line;
		}
		if ( !typed[0] && emoji ) {
			Col( c, DIM, 0.8f );
			TextFlags( FONT_MEDIUM, lx + 6 * u, y + h - inputH * 0.5f + fs * 0.36f, ":name: for emoji, Tab completes", fs, c,
				TEXT_NOCODES, 0, 0 );
		}
		TextFlags( FONT_MEDIUM, lx, y + h - inputH * 0.5f + fs * 0.36f, typed, fs, INK_HOT, TEXT_NOCODES, 0, 0 );
		Q_strncpyz( head, typed, sizeof( head ) );
		if ( caret >= 0 && caret < (int)sizeof( head ) ) {
			head[caret] = 0;
		}
		before = TextWidth( FONT_MEDIUM, head, fs );
		if ( (int)( cg.time / 500 ) & 1 ) {
			Rect( lx + before + 1, y + h - inputH * 0.5f - fs * 0.5f, 2 * u, fs, AMBER );
		}
		// a message with color codes: how it will look, just above the input
		if ( preview ) {
			char colored[CHAT_TEXT];
			Com_sprintf( colored, sizeof( colored ), "%s%s", team ? "^5" : "^2", field );
			if ( emoji ) {
				OzEmoji_Expand( colored, line, sizeof( line ), -1, NULL );
				Q_strncpyz( colored, line, sizeof( colored ) );
			}
			ChatLine( x + pad, bottom - 7 * u, colored, fs, 0.9f );
			bottom -= lh;
		}
		if ( numSuggested ) {
			float sx = x + pad;
			Col( c, BLACK, 0.3f );
			Rect( x + 3 * u, bottom - lh, w - 3 * u, lh, c );
			for ( i = 0; i < numSuggested; i++ ) {
				char item[OZ_EMOJI_MAX_NAME + 8];
				float iw;
				const int n = OzEmoji_Utf8( suggested[i], item );
				Com_sprintf( item + n, sizeof( item ) - n, " %s", names[i] );
				iw = TextWidth( FONT_MEDIUM, item, fs );
				if ( i && sx + iw > x + w - pad ) {
					break;
				}
				if ( !i ) {
					Col( c, AMBER, 0.22f );
					Rect( sx - 6 * u, bottom - lh + 2 * u, iw + 12 * u, lh - 4 * u, c );
				}
				TextFlags( FONT_MEDIUM, sx, bottom - 7 * u, item, fs, i ? INK : INK_HOT, TEXT_NOCODES, 0.7f, i ? 0 : x + w - pad - sx );
				sx += iw + 24 * u;
			}
			bottom -= lh;
		}
	}
	ly = bottom - 7 * u;
	for ( i = 0; i < numShown; i++ ) {
		ChatLine( x + pad, ly, shown[i], fs, shownAlpha[i] );
		ly -= lh;
	}
}

// ================================================================ messages (q3_hud.gd _draw_messages)

#define REWARD_TIME		3000
static const int awardPers[6] = { PERS_IMPRESSIVE_COUNT, PERS_EXCELLENT_COUNT, PERS_GAUNTLET_FRAG_COUNT, PERS_DEFEND_COUNT,
	PERS_ASSIST_COUNT, PERS_CAPTURES };

// cg_playerstate.c CG_CheckLocalSounds' rewards, from the playerState's award counts (the game code
// plays their sounds; this shows the medals): one on show, the rest queued
static void UpdateRewards( const playerState_t *ps, qboolean quiet ) {
	int	i;

	for ( i = 0; i < 6; i++ ) {
		const int n = ps->persistant[awardPers[i]];
		if ( n != hud.awards[i] && !quiet && n > hud.awards[i] && hud.rewardStack < ARRAY_LEN( hud.rewardMedal ) ) {
			hud.rewardMedal[hud.rewardStack] = i;
			hud.rewardCount[hud.rewardStack] = n;
			if ( !hud.rewardStack ) {
				hud.rewardTime = cg.time;
			}
			hud.rewardStack++;
		}
		hud.awards[i] = n;
	}
	if ( hud.rewardStack && cg.time - hud.rewardTime >= REWARD_TIME ) {
		for ( i = 1; i < hud.rewardStack; i++ ) {
			hud.rewardMedal[i - 1] = hud.rewardMedal[i];
			hud.rewardCount[i - 1] = hud.rewardCount[i];
		}
		hud.rewardStack--;
		hud.rewardTime = cg.time;
	}
}

// the reward on show, as the Godot build's medal (a pop, the count beside it)
static void DrawReward( float W, float H ) {
	float	t, a, pop, sz, x, y, c[4];

	if ( !cg_drawRewards->integer || !hud.rewardStack ) {
		return;
	}
	t = ( cg.time - hud.rewardTime ) / 1000.0f;
	a = Clamp01( ( REWARD_TIME / 1000.0f - t ) / 0.4f );
	pop = 1.0f + ( 1.0f - Clamp01( t / 0.2f ) ) * 0.6f;
	sz = 72 * u * pop;
	x = W * 0.5f - sz * 0.5f;
	y = H * 0.13f;
	Col( c, WHITE, a );
	Image( img.medals[hud.rewardMedal[0]], x, y, sz, sz, c );
	if ( hud.rewardCount[0] > 1 ) {
		Col( c, AMBER, a );
		Text( FONT_NUM, x + sz + 8 * u, y + sz - 8 * u, va( "x%d", hud.rewardCount[0] ), 26 * u, c, ALIGN_LEFT );
	}
}

// the Godot build's warmup lines (the game code still plays the countdown)
static void DrawWarmup( float W, float H ) {
	float	c[4];

	if ( !cg.warmup ) {
		return;
	}
	if ( cg.warmup < 0 ) {
		const char *ready = Info_ValueForKey( CS( CS_SERVERINFO ), "g_readyUp" );
		Text( FONT_LABEL, W * 0.5f, H * 0.2f, "WARMUP", 40 * u, INK_HOT, ALIGN_CENTER );
		Text( FONT_LABEL, W * 0.5f, H * 0.2f + 34 * u, atoi( ready ) ? "PRESS F3 WHEN YOU'RE READY" : "WAITING FOR PLAYERS", 18 * u, DIM, ALIGN_CENTER );
		return;
	}
	{
		const int sec = ( cg.warmup - cg.time ) / 1000;
		if ( cg.warmup - cg.time < 0 ) {
			return;
		}
		Col( c, INK_HOT, 1 );
		Text( FONT_LABEL, W * 0.5f, H * 0.2f, va( "STARTS IN %d", sec + 1 ), 40 * u, c, ALIGN_CENTER );
	}
}

static void DrawCenter( float W, float H ) {
	char		buf[1024], *line, *nl;
	float		t, a, pop, size, y;
	qboolean	big;
	int			i = 0;

	if ( !cg.centerPrintTime || cg.scoreBoardShowing ) {
		return;
	}
	t = ( cg.time - cg.centerPrintTime ) / 1000.0f;
	if ( t < 0 || t >= cg_centertime->value ) {
		return;
	}
	a = Clamp01( ( cg_centertime->value - t ) / 0.5f );
	big = !Q_stricmp( cg.centerPrint, "FIGHT!" );
	size = ( big ? 64.0f : 30.0f ) * u;
	pop = 1.0f + ( 1.0f - Clamp01( t / 0.15f ) ) * 0.25f;
	Q_strncpyz( buf, cg.centerPrint, sizeof( buf ) );
	y = H * 0.3f;
	for ( line = buf; line; line = nl, i++ ) {
		float c[4];
		nl = strchr( line, '\n' );
		if ( nl ) {
			*nl++ = 0;
		}
		Col( c, i == 0 ? INK_HOT : DIM, a );
		CTextScaled( FONT_LABEL, W * 0.5f, y + i * size * 1.2f, line, size * ( i == 0 ? 1.0f : 0.7f ), pop, c, ALIGN_CENTER, 0 );
	}
}

static void DrawMessages( float W, float H, float vitX, float vitY, float ammoX, float ammoY, float ammoW,
		float feedX, float feedY, float feedW, float feedH, const playerState_t *ps ) {
	float	y, c[4];
	int		i;

	// server prints, top left
	y = 40 * u;
	for ( i = 0; i < hud.numNotify; i++ ) {
		Col( c, INK, Clamp01( ( 6.0f - hud.notify[i].t ) / 0.5f ) );
		CText( FONT_MEDIUM, 34 * u, y, hud.notify[i].text, 18 * u, c, ALIGN_LEFT, 0 );
		y += 24 * u;
	}
	DrawCenter( W, H );
	// cg_draw.c CG_DrawCrosshairNames: the player under the crosshair, at half strength
	if ( cg_drawCrosshairNames->integer && !hud.dead && ps->persistant[PERS_TEAM] != TEAM_SPECTATOR ) {
		const int age = cg.time - cg.crosshairClientTime;
		if ( age >= 0 && age < 1000 && cg.crosshairClientNum >= 0 && cg.crosshairClientNum < MAX_CLIENTS ) {
			char name[MAX_QPATH];
			const float xa = 0.5f * ( age < 800 ? 1.0f : ( 1000 - age ) / 200.0f );
			PlayerName( cg.crosshairClientNum, name, sizeof( name ) );
			Col( c, INK_HOT, xa );
			CText( FONT_LABEL, W * 0.5f, H * ( 170.0f / 480.0f ), name, 22 * u, c, ALIGN_CENTER, 0 );
		}
	}
	// cg_draw.c CG_DrawAttacker: who last hurt you, for 10 s, under the kill feed
	if ( hud.attackerT >= 0 && !hud.dead ) {
		const int att = ps->persistant[PERS_ATTACKER];
		if ( hud.attackerT > 10 ) {
			hud.attackerT = -1;
		} else if ( att >= 0 && att < MAX_CLIENTS && att != ps->clientNum ) {
			char name[MAX_QPATH];
			PlayerName( att, name, sizeof( name ) );
			if ( name[0] ) {
				const float ay = feedY + feedH + 16 * u;
				Text( FONT_LABEL, feedX + feedW, ay, "ATTACKER", 12 * u, DIM, ALIGN_RIGHT );
				CText( FONT_LABEL, feedX + feedW, ay + 24 * u, name, 20 * u, INK_HOT, ALIGN_RIGHT, 0 );
			}
		}
	}
	// a vote on (cg_draw.c CG_DrawVote)
	if ( cgs.voteTime ) {
		int left = ( VOTE_TIME - ( cg.time - cgs.voteTime ) ) / 1000;
		if ( left < 0 ) {
			left = 0;
		}
		CText( FONT_LABEL, 16 * u, 170 * u, va( "VOTE (%d): %s   YES %d   NO %d", left, cgs.voteString, cgs.voteYes, cgs.voteNo ), 20 * u, AMBER, ALIGN_LEFT, 0 );
		Text( FONT_LABEL, 16 * u, 170 * u + 24 * u, "F1 YES    F2 NO", 14 * u, DIM, ALIGN_LEFT );
	}
	DrawWarmup( W, H );
	DrawReward( W, H );
	// the pickup, above the vitals
	if ( cg.itemPickup > 0 && cg.itemPickup < numHudItems ) {
		const float t = ( cg.time - cg.itemPickupTime ) / 1000.0f;
		if ( t >= 0 && t < 3.0f ) {
			const float a = Clamp01( ( 3.0f - t ) / 0.5f );
			const float px = vitX + 8 * u, py = vitY - 46 * u;
			Col( c, WHITE, a );
			Image( ItemIcon( cg.itemPickup ), px, py, 36 * u, 36 * u, c );
			Col( c, INK_HOT, a );
			Text( FONT_MEDIUM, px + 46 * u, py + 26 * u, hudItems[cg.itemPickup].name, 21 * u, c, ALIGN_LEFT );
		}
	}
	// powerups with their seconds left, above the ammo
	{
		float py = ammoY - 56 * u;
		int pw;
		for ( pw = PW_QUAD; pw <= PW_BLUEFLAG; pw++ ) {
			ozHudItem_t *it;
			float left;
			if ( !ps->powerups[pw] ) {
				continue;
			}
			left = ( ps->powerups[pw] - cg.time ) / 1000.0f;
			if ( pw < PW_REDFLAG && left <= 0 ) {
				continue;
			}
			it = FindItemForPowerup( pw );
			if ( !it ) {
				continue;
			}
			{
				const float x = ammoX + ammoW - 110 * u;
				Image( ImageNamed( it->icon ), x, py, 44 * u, 44 * u, WHITE );
				if ( pw < PW_REDFLAG ) {
					const int s = (int)ceil( left );
					Text( FONT_NUM, x + 54 * u, py + 34 * u, va( "%d", s ), 32 * u, s <= 5 ? RED : INK_HOT, ALIGN_LEFT );
				}
			}
			py -= 52 * u;
		}
	}
	// CG_DrawAmmoWarning
	if ( cg.lowAmmoWarning && !hud.dead && ps->persistant[PERS_TEAM] != TEAM_SPECTATOR ) {
		Col( c, RED, 0.85f );
		Text( FONT_LABEL, W * 0.5f, H * 0.62f, cg.lowAmmoWarning == 2 ? "OUT OF AMMO" : "LOW AMMO WARNING", 20 * u, c, ALIGN_CENTER );
	}
	// dead (q3_hud.gd's death card): KILLED BY over the killer's card and their weapon, or id's own
	// obituary line for a death with no killer; then how to come back (g_active.c's respawnTime: 1.7 s)
	if ( hud.dead ) {
		const float wait = ( hud.deathTime + 1700 - cg.time ) / 1000.0f;
		const float dy = H * 0.6f;
		if ( hud.killer >= 0 && hud.killer < MAX_CLIENTS && cgs.clientinfo[hud.killer].infoValid ) {
			const int w = ModWeapon( hud.killerMod );
			Text( FONT_LABEL, W * 0.5f, dy, "KILLED BY", 18 * u, DIM, ALIGN_CENTER );
			Card( ( W - 520 * u ) * 0.5f, dy + 14 * u, 520 * u, 80 * u, hud.killer, qfalse, 1 );
			if ( w > 0 && w < NAMES ) {
				Text( FONT_LABEL, W * 0.5f, dy + 122 * u, weaponNames[w], 16 * u, DIM, ALIGN_CENTER );
			}
		} else if ( hud.killerMod >= 0 ) {
			char name[MAX_QPATH];
			PlayerName( ps->clientNum, name, sizeof( name ) );
			CText( FONT_MEDIUM, W * 0.5f, dy + 50 * u, va( "%s^7 %s", name, ObituaryLine( hud.killerMod ) ), 28 * u, INK_HOT, ALIGN_CENTER, 0 );
		}
		Text( FONT_LABEL, W * 0.5f, dy + 156 * u, wait > 0 ? va( "RESPAWN IN %.1f", wait ) : "FIRE TO RESPAWN", 16 * u, INK, ALIGN_CENTER );
	}
	// a spectator: what they watch
	if ( ps->pm_flags & PMF_FOLLOW ) {
		char name[MAX_QPATH];
		PlayerName( ps->clientNum, name, sizeof( name ) );
		Text( FONT_LABEL, W * 0.5f, 124 * u, "FOLLOWING", 16 * u, DIM, ALIGN_CENTER );
		CText( FONT_MEDIUM, W * 0.5f, 152 * u, name, 26 * u, INK_HOT, ALIGN_CENTER, 0 );
	} else if ( ps->persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
		Text( FONT_LABEL, W * 0.5f, 150 * u, "SPECTATING", 30 * u, INK_HOT, ALIGN_CENTER );
	}
}

// ================================================================ lagometer and fps

// down the left edge, each major item's icon with the seconds until it's back, dimmed while it's up
// (q3_hud.gd _draw_item_timers); the map's items join the list as their entities come into view
static void DrawItemTimers( float H ) {
	float	y, c[4];
	int		i;

	for ( i = 0; i < cl.snap.numEntities; i++ ) {
		const entityState_t *es = SnapEntity( i );
		if ( es->eType == ET_ITEM ) {
			TimerSlot( es->modelindex, es->number );
		}
	}
	if ( !hud.numTimers ) {
		return;
	}
	y = H * 0.5f - hud.numTimers * 26 * u;
	for ( i = 0; i < hud.numTimers; i++ ) {
		const float	left = hud.timerBack[i] ? ( hud.timerBack[i] - cg.time ) / 1000.0f : -1;
		if ( left <= 0 ) {
			hud.timerBack[i] = 0;
		}
		Col( c, WHITE, left > 0 ? 1.0f : 0.4f );
		Image( ItemIcon( hud.timerItems[i] ), 34 * u, y, 40 * u, 40 * u, c );
		if ( left > 0 ) {
			Text( FONT_NUM, 84 * u, y + 31 * u, va( "%d", (int)ceil( left ) ), 28 * u, left <= 10 ? AMBER : INK_HOT, ALIGN_LEFT );
		}
		y += 52 * u;
	}
}

// ================================================================ subtitles (q3_hud.gd _caption)

// Accessibility > Subtitles: what the announcer says, by sound file
static const struct { const char *file, *text; } captionLines[] = {
	{ "impressive", "Impressive!" }, { "excellent", "Excellent!" }, { "humiliation", "Humiliation!" },
	{ "defense", "Defense!" }, { "assist", "Assist!" }, { "denied", "Denied!" }, { "voc_holyshit", "Holy shit!" },
	{ "takenlead", "You have taken the lead." }, { "tiedlead", "You are tied for the lead." }, { "lostlead", "You have lost the lead." },
	{ "sudden_death", "Sudden death!" }, { "1_minute", "One minute remaining." }, { "5_minute", "Five minutes remaining." },
	{ "1_frag", "One frag left." }, { "2_frags", "Two frags left." }, { "3_frags", "Three frags left." },
	{ "prepare", "Prepare to fight!" }, { "three", "Three" }, { "two", "Two" }, { "one", "One" }, { "fight", "Fight!" },
	{ "flagcapture_yourteam", "Your team captured the flag!" }, { "flagcapture_opponent", "The enemy captured your flag!" },
	{ "youwin", "You win!" }, { "voc_you_flag", "You have the flag!" }, { "voc_team_flag", "Your team has the enemy flag!" },
	{ "voc_enemy_flag", "The enemy has your flag!" }, { "voc_red_returned", "Red flag returned." },
	{ "voc_blue_returned", "Blue flag returned." }, { "voc_red_scores", "Red scores!" }, { "voc_blue_scores", "Blue scores!" },
	{ "redleads", "Red leads." }, { "blueleads", "Blue leads." }, { "teamstied", "Teams are tied." },
	{ "red_leads", "Red leads." }, { "blue_leads", "Blue leads." }, { "teams_tied", "Teams are tied." },
};

#define MAX_NOTED_SOUNDS	4096
#define CAPTION_TIME		2.5f
static short	soundCaption[MAX_NOTED_SOUNDS];		// a sound handle's line + 1, 0 for none
static char		soundWinner[MAX_NOTED_SOUNDS / 8];	// "<name>_wins" handles, one bit each
static char		soundHit[MAX_NOTED_SOUNDS / 8];		// the hit beeps' handles
static char		winnerNames[64][MAX_QPATH];
static int		numWinnerNames;
static short	soundWinnerName[MAX_NOTED_SOUNDS];

void OzHud_NoteSound( int sfx, const char *name ) {
	char	base[MAX_QPATH];
	int		i, len;

	// handle 0 is id's hit beep, the mixer's default sound, which every missing sound gets too
	if ( sfx <= 0 || sfx >= MAX_NOTED_SOUNDS || !name ) {
		return;
	}
	COM_StripExtension( COM_SkipPath( (char *)name ), base, sizeof( base ) );
	soundCaption[sfx] = 0;
	soundWinner[sfx >> 3] &= ~( 1 << ( sfx & 7 ) );
	soundHit[sfx >> 3] &= ~( 1 << ( sfx & 7 ) );
	// the hit beeps: id's (hit, hit_teammate) and CPMA's by damage (hit25 ...)
	if ( !Q_stricmpn( base, "hit", 3 ) ) {
		for ( i = 3; base[i] >= '0' && base[i] <= '9'; i++ ) {
		}
		if ( !base[i] || !Q_stricmp( base + 3, "_teammate" ) ) {
			soundHit[sfx >> 3] |= 1 << ( sfx & 7 );
		}
	}
	for ( i = 0; i < ARRAY_LEN( captionLines ); i++ ) {
		if ( !Q_stricmp( base, captionLines[i].file ) ) {
			soundCaption[sfx] = i + 1;
			return;
		}
	}
	len = strlen( base );
	if ( len > 5 && !Q_stricmp( base + len - 5, "_wins" ) && numWinnerNames < ARRAY_LEN( winnerNames ) ) {
		base[len - 5] = 0;
		base[0] = toupper( base[0] );
		Q_strncpyz( winnerNames[numWinnerNames], va( "%s wins!", base ), sizeof( winnerNames[0] ) );
		soundWinnerName[sfx] = numWinnerNames++;
		soundWinner[sfx >> 3] |= 1 << ( sfx & 7 );
	}
}

int OzHud_LocalSound( int sfx ) {
	const char	*text = NULL;
	int			i;

	if ( !hudInit || sfx < 0 || sfx >= MAX_NOTED_SOUNDS ) {
		return 1;
	}
	// Audio > Hit sound: off drops the hit beeps (pitch by damage is the game code's, P3); id's beep is
	// handle 0 (OzHud_NoteSound)
	if ( !sfx || soundHit[sfx >> 3] & ( 1 << ( sfx & 7 ) ) ) {
		return Q_stricmp( oz_audio_hit_sound->string, "off" ) != 0;
	}
	if ( !oz_accessibility_subtitles->integer ) {
		return 1;
	}
	if ( soundCaption[sfx] ) {
		text = captionLines[soundCaption[sfx] - 1].text;
	} else if ( soundWinner[sfx >> 3] & ( 1 << ( sfx & 7 ) ) ) {
		text = winnerNames[soundWinnerName[sfx]];
	}
	if ( !text ) {
		return 1;
	}
	if ( hud.numCaptions == ARRAY_LEN( hud.captions ) ) {
		for ( i = 1; i < hud.numCaptions; i++ ) {
			hud.captions[i - 1] = hud.captions[i];
		}
		hud.numCaptions--;
	}
	Q_strncpyz( hud.captions[hud.numCaptions].text, text, sizeof( hud.captions[0].text ) );
	hud.captions[hud.numCaptions].t = 0;
	hud.numCaptions++;
	return 1;
}

// ================================================================ competitive options (P6 item 5)

#define MAX_NOTED_MODELS	1024
static char		modelIsPlayer[MAX_NOTED_MODELS / 8];	// models/players/... handles, one bit each
static qboolean	zoomToggled;

void OzHud_NoteModel( int model, const char *name ) {
	OzView_NoteModel( model, name );	// each part's model and animations, for the forced enemy model
	if ( model <= 0 || model >= MAX_NOTED_MODELS || !name ) {
		return;
	}
	if ( !Q_stricmpn( name, "models/players/", 15 ) ) {
		modelIsPlayer[model >> 3] |= 1 << ( model & 7 );
	} else {
		modelIsPlayer[model >> 3] &= ~( 1 << ( model & 7 ) );
	}
}

// the client whose body this is: the player in the snapshot nearest the part's lighting origin (the
// game code lights every part of a player from where the player stands)
static int BodyOwner( const refEntity_t *ent ) {
	float	best = 64 * 64;
	int		i, owner = -1;

	for ( i = 0; i < cl.snap.numEntities; i++ ) {
		const entityState_t	*es = SnapEntity( i );
		vec3_t				d;
		float				dist;
		if ( es->eType != ET_PLAYER || es->clientNum < 0 || es->clientNum >= MAX_CLIENTS ) {
			continue;
		}
		VectorSubtract( es->pos.trBase, ent->lightingOrigin, d );
		dist = DotProduct( d, d );
		if ( dist < best ) {
			best = dist;
			owner = es->clientNum;
		}
	}
	return owner;
}

// the enemy whose body this part is (not you, not a teammate), with their team, or -1 (oz_hud_view.c)
int OzHud_EnemyBody( const refEntity_t *ent, int *team ) {
	int	owner;

	if ( ent->reType != RT_MODEL || ent->hModel <= 0 || ent->hModel >= MAX_NOTED_MODELS
		|| !( modelIsPlayer[ent->hModel >> 3] & ( 1 << ( ent->hModel & 7 ) ) ) || ( ent->renderfx & RF_THIRD_PERSON ) ) {
		return -1;
	}
	owner = BodyOwner( ent );
	if ( owner < 0 || owner == cl.snap.ps.clientNum
		|| ( cgs.gametype >= GT_TEAM && cgs.clientinfo[owner].team == cl.snap.ps.persistant[PERS_TEAM] ) ) {
		return -1;
	}
	*team = cgs.clientinfo[owner].team;
	return owner;
}

// A51's Freeze Tag: a team game whose server says clientversion (A51's servers all do) and sets
// configstring 873 to 1, which none of A51's other modes does (read from what its servers send:
// ozents, configstrings; 2026-11-03)
static qboolean A51FreezeTag( void ) {
	const char *info = CS( CS_SERVERINFO );

	return cls.state == CA_ACTIVE && atoi( Info_ValueForKey( info, "g_gametype" ) ) >= GT_TEAM
		&& Info_ValueForKey( info, "clientversion" )[0] && !strcmp( CS( 873 ), "1" );
}

// A51's Freeze Tag freezes a player as a body in a death pose (EF_DEAD, the body queue's entity once the
// player is back in), where they stay until thawed. Fighters' ragdolls fell and burned away within 10 s, so
// frozen teammates vanished: such a body keeps the game code's pose (RF_OZ_FROZEN) and gets a shell of ice
static void FrozenBody( refEntity_t *ent ) {
	static qhandle_t	ice;
	static int			iceGeneration = -1;
	float				best = 64 * 64;
	const entityState_t	*body = NULL;
	refEntity_t			shell;
	int					i;

	if ( ent->reType != RT_MODEL || ent->hModel <= 0 || ent->hModel >= MAX_NOTED_MODELS
		|| !( modelIsPlayer[ent->hModel >> 3] & ( 1 << ( ent->hModel & 7 ) ) ) || !A51FreezeTag() ) {
		return;
	}
	for ( i = 0; i < cl.snap.numEntities; i++ ) {
		const entityState_t	*es = SnapEntity( i );
		vec3_t				d;
		float				dist;
		if ( es->eType != ET_PLAYER ) {
			continue;
		}
		VectorSubtract( es->pos.trBase, ent->lightingOrigin, d );
		dist = DotProduct( d, d );
		if ( dist < best ) {
			best = dist;
			body = es;
		}
	}
	if ( !body || !( body->eFlags & EF_DEAD ) ) {
		return;
	}
	ent->renderfx |= RF_OZ_FROZEN;
	if ( iceGeneration != OzE_RendererGeneration() ) {
		iceGeneration = OzE_RendererGeneration();
		ice = re.RegisterShader( "oz_frozen" );
	}
	shell = *ent;
	shell.customShader = ice;
	shell.customSkin = 0;
	shell.shader.rgba[0] = 70;
	shell.shader.rgba[1] = 130;
	shell.shader.rgba[2] = 190;
	shell.shader.rgba[3] = 255;
	re.AddRefEntityToScene( &shell, qfalse );
}

int OzHud_RefEntity( refEntity_t *ent ) {
	FrozenBody( ent );
	if ( !OzHud_Active() ) {
		return 1;
	}
	// Gameplay > Gun position, side offset and field of view: the gun is placed when the scene renders,
	// against that scene's view (oz_hud_view.c). Not id's damage blood blob, a first-person sprite drawn
	// without the gun's depth hack (CG_DamageBlendBlob): for half a second after every hit it came first,
	// so the marks and effects drawn after it counted as the gun's, filled its quads, and the lightning
	// beam's layers were drawn where the cgame put them, off the gun as it shows
	if ( ( ent->renderfx & RF_FIRST_PERSON ) && ( ent->reType != RT_SPRITE || ( ent->renderfx & RF_DEPTHHACK ) ) ) {
		return OzView_Gun( ent );
	}
	// That blood blob smeared over the view after every hit hid what you were fighting (Ozmium,
	// 2026-11-03): it's dropped under every cgame. The HUD's red edge still shows the hit
	if ( ( ent->renderfx & RF_FIRST_PERSON ) && ent->reType == RT_SPRITE ) {
		return 0;
	}
	// Gameplay > Force enemy model, and what hangs on a forced body (oz_hud_view.c)
	if ( !OzView_Entity( ent ) ) {
		return 0;
	}
	// Gameplay > Bright skins: enemies' bodies flat in the enemy color (each renderer's internal oz_brightskin)
	if ( oz_gameplay_bright_skins->integer && ent->reType == RT_MODEL && ent->hModel > 0 && ent->hModel < MAX_NOTED_MODELS
		&& ( modelIsPlayer[ent->hModel >> 3] & ( 1 << ( ent->hModel & 7 ) ) ) && !( ent->renderfx & RF_THIRD_PERSON ) ) {
		const int		owner = BodyOwner( ent );
		const qboolean	mate = cgs.gametype >= GT_TEAM && owner >= 0 && cgs.clientinfo[owner].team == cl.snap.ps.persistant[PERS_TEAM];
		if ( owner >= 0 && owner != cl.snap.ps.clientNum ) {
			static qhandle_t	bright;
			static int			brightGeneration = -1;
			float				c[4];
			if ( brightGeneration != OzE_RendererGeneration() ) {
				brightGeneration = OzE_RendererGeneration();
				bright = re.RegisterShader( "oz_brightskin" );
			}
			ParseColor( mate ? oz_gameplay_team_color->string : oz_gameplay_enemy_color->string, c );
			ent->customShader = bright;
			ent->customSkin = 0;
			ent->shader.rgba[0] = ToByte( c[0] );
			ent->shader.rgba[1] = ToByte( c[1] );
			ent->shader.rgba[2] = ToByte( c[2] );
			ent->shader.rgba[3] = 255;
		}
	}
	return 1;
}

// Mouse > Zoom sensitivity: while the game code scales the mouse down (it's zoomed), the setting
// scales it further
float OzHud_ZoomSensitivity( float cgameScale ) {
	if ( !hudInit || cgameScale >= 1.0f || oz_mouse_zoom_sensitivity->value <= 0 ) {
		return 1.0f;
	}
	return oz_mouse_zoom_sensitivity->value;
}

// Controls > Toggle zoom: a press of the zoom key zooms in, the next zooms out; releases do nothing
int OzHud_ZoomCommand( void ) {
	const char *cmd = Cmd_Argv( 0 );

	if ( !hudInit || !oz_controls_toggle_zoom->integer || ( Q_stricmp( cmd, "+zoom" ) && Q_stricmp( cmd, "-zoom" ) ) ) {
		return 1;
	}
	if ( cmd[0] == '-' ) {
		return 0;
	}
	if ( zoomToggled ) {
		Cmd_TokenizeString( "-zoom" );
	}
	zoomToggled = !zoomToggled;
	return 1;
}

// the announcer's recent lines, bottom middle, stacked above `top`
static void DrawCaptions( float W, float top ) {
	const char	*sz = oz_accessibility_subtitle_size->string;
	const float	size = (float)(int)( ( !Q_stricmp( sz, "low" ) ? 22.0f : !Q_stricmp( sz, "high" ) ? 36.0f : 28.0f ) * u );
	float		y = top - 16 * u - size * 0.35f - ( hud.numCaptions - 1 ) * size * 1.4f;
	int			i;

	for ( i = 0; i < hud.numCaptions; i++ ) {
		const float	a = Clamp01( ( CAPTION_TIME - hud.captions[i].t ) / 0.4f );
		const float	w = TextWidth( FONT_MEDIUM, hud.captions[i].text, size );
		float		c[4];
		Col( c, BLACK, 0.6f * a );
		Rect( W * 0.5f - w * 0.5f - 12 * u, y - size * 1.05f, w + 24 * u, size * 1.4f, c );
		Col( c, INK_HOT, a );
		Text( FONT_MEDIUM, W * 0.5f, y, hud.captions[i].text, size, c, ALIGN_CENTER );
		y += size * 1.4f;
	}
}

static void DrawLagometer( float W, float H ) {
	const float	bw = 192 * u, bh = 96 * u;
	const float	bx = W - 34 * u - bw, by = H * 0.5f - 110 * u;
	const float	mid = by + bh * 0.5f, bar = bw / LAG_SAMPLES, half = bh * 0.5f;
	static const vec4_t back = { 0, 0, 0, 0.45f };
	static const vec4_t under = { 0.3f, 0.5f, 1.0f, 1 };
	static const vec4_t over = { 1.0f, 0.85f, 0.2f, 1 };
	static const vec4_t good = { 0.2f, 0.9f, 0.3f, 1 };
	static const vec4_t bad = { 1.0f, 0.2f, 0.15f, 1 };
	static const vec4_t line = { 1, 1, 1, 0.15f };
	int			i;

	Rect( bx, by, bw, bh, back );
	for ( i = 0; i < hud.numLag; i++ ) {
		const float x = bx + ( LAG_SAMPLES - hud.numLag + i ) * bar;
		const float f = hud.lagFrames[i], p = hud.lagSnaps[i];
		float h = ( f / 50.0f > 1 ? 1 : f / 50.0f ) * half;
		float ph = ( p / 300.0f > 1 ? 1 : p / 300.0f ) * half;
		if ( ph < 1 ) {
			ph = 1;
		}
		Rect( x, mid - h, bar > 1 ? bar : 1, h, f <= 1000.0f / 60.0f + 0.5f ? under : over );
		Rect( x, by + bh - ph, bar > 1 ? bar : 1, ph, p < 300 ? good : bad );
	}
	Rect( bx, mid - ( u > 1 ? u : 1 ) * 0.5f, bw, u > 1 ? u : 1, line );
	Text( FONT_MEDIUM, bx + bw, by + bh + 20 * u, va( "PING %d   FRAME %.1f MS", cg.snap->ping,
		hud.numLag ? hud.lagFrames[hud.numLag - 1] : 0.0f ), 14 * u, DIM, ALIGN_RIGHT );
}

// ================================================================ damage numbers (damage_number.gd)

static qboolean WorldToScreen( const vec3_t point, float W, float H, float *x, float *y ) {
	vec3_t	d;
	float	z, px, py, tx, ty;

	VectorSubtract( point, cg.refdef.vieworg, d );
	z = DotProduct( d, cg.refdef.viewaxis[0] );
	if ( z < 1 ) {
		return qfalse;
	}
	px = DotProduct( d, cg.refdef.viewaxis[1] );
	py = DotProduct( d, cg.refdef.viewaxis[2] );
	tx = (float)tan( DEG2RAD( cg.refdef.fov_x ) * 0.5f );
	ty = (float)tan( DEG2RAD( cg.refdef.fov_y ) * 0.5f );
	*x = cg.refdef.x + cg.refdef.width * 0.5f - px / z / tx * cg.refdef.width * 0.5f;
	*y = cg.refdef.y + cg.refdef.height * 0.5f - py / z / ty * cg.refdef.height * 0.5f;
	return *x > -100 && *x < W + 100 && *y > -100 && *y < H + 100;
}

static void DrawDamageNumbers( float W, float H ) {
	int	i;

	for ( i = 0; i < MAX_NUMBERS; i++ ) {
		damageNumber_t	*n = &hud.numbers[i];
		float			x, y, heat, size, pop, alpha, c[4];
		static const vec4_t yellow = { 1.0f, 0.85f, 0.2f, 1 };
		static const vec4_t orange = { 1.0f, 0.45f, 0.1f, 1 };
		static const vec4_t red = { 1.0f, 0.12f, 0.08f, 1 };
		if ( !n->on ) {
			continue;
		}
		n->since += hud.dt;
		n->pop += hud.dt;
		VectorMA( n->pos, hud.dt, n->vel, n->pos );		// its arc as it falls, the same at any frame rate
		n->pos[2] -= 0.5f * 9.0f * 32 * hud.dt * hud.dt;
		n->vel[2] -= 9.0f * 32 * hud.dt;
		alpha = n->since > 1.0f ? Clamp01( 1.0f - ( n->since - 1.0f ) / 0.35f ) : 1.0f;
		if ( alpha <= 0 ) {
			n->on = qfalse;
			continue;
		}
		if ( !WorldToScreen( n->pos, W, H, &x, &y ) ) {
			continue;
		}
		heat = Clamp01( n->total / 150.0f );
		Mix( c, WHITE, yellow, heat * 3 );
		Mix( c, c, orange, heat * 2 - 0.5f );
		Mix( c, c, red, heat * 2 - 1 );
		c[3] = alpha;
		size = ( 40 + ( n->total < 200 ? n->total : 200 ) * 0.18f ) * 0.9f * u;
		// 1.45x on a hit, back to 1 in 0.18 s with a little overshoot (TRANS_BACK)
		pop = 1.0f;
		if ( n->pop < 0.18f ) {
			const float k = n->pop / 0.18f - 1.0f;
			pop = 1.0f + 0.45f * ( 1.0f - ( 1.0f + 2.70158f * k * k * k + 1.70158f * k * k ) );
		}
		TextScaled( FONT_NUM, x, y, va( "%d", n->total ), size, pop, c, ALIGN_CENTER | TEXT_NOCODES | TEXT_STEPPED, 0.85f, 0 );
	}
}

// ================================================================ per frame

/*
The match's top speed, for the career's movement achievements (P12: Strafe Jumper at 600 ups, Speed
Demon at 800, Light Speed at 1000): the horizontal speed the player made by their own movement, alive,
playing and not following anyone. A push (a jump pad, a teleporter, a hit's knockback, a respawn)
isn't counted until the player is back on the ground at running speed, so a pad's throw carried on by
bunny hops isn't either; nor is a server's own run speed above the game's (g_speed; haste's 1.3 is the
game's). pushedTop is the fastest with pushes, for the log.
*/
static void TopSpeed( const playerState_t *ps, float ups ) {
	const float run = ps->speed > 0 ? (float)ps->speed : RUN_UPS;

	if ( ps->pm_type != PM_NORMAL || ( ps->pm_flags & ( PMF_FOLLOW | PMF_TIME_KNOCKBACK ) ) || ps->persistant[PERS_TEAM] == TEAM_SPECTATOR
		|| ( ( ps->eFlags ^ hud.speedFlags ) & EF_TELEPORT_BIT ) || ps->damageEvent != hud.speedDamage || run > RUN_UPS * 1.3f + 0.5f ) {
		hud.pushed = qtrue;
	} else if ( hud.pushed && ps->groundEntityNum != ENTITYNUM_NONE && ups <= run + 1 ) {
		hud.pushed = qfalse;
	}
	hud.speedFlags = ps->eFlags;
	hud.speedDamage = ps->damageEvent;
	if ( ps->pm_type == PM_NORMAL && ups > hud.pushedTop ) {
		hud.pushedTop = ups;
	}
	if ( !hud.pushed && ups > hud.topSpeed ) {
		hud.topSpeed = ups;
	}
}

static void Update( const playerState_t *ps ) {
	int		i;
	float	vel;

	hud.dt = cg.frametime / 1000.0f;
	if ( hud.dt < 0 || hud.dt > 0.2f ) {
		hud.dt = 0;
	}
	hud.t += hud.dt;
	// vitals
	if ( ps->stats[STAT_HEALTH] > hud.health ) {
		hud.ghostH = (float)ps->stats[STAT_HEALTH];
	}
	if ( ps->stats[STAT_ARMOR] > hud.armor ) {
		hud.ghostA = (float)ps->stats[STAT_ARMOR];
	}
	hud.health = ps->stats[STAT_HEALTH];
	hud.armor = ps->stats[STAT_ARMOR];
	hud.ghostH = MoveToward( hud.ghostH, (float)hud.health, hud.dt * 160 );
	hud.ghostA = MoveToward( hud.ghostA, (float)hud.armor, hud.dt * 160 );
	// ammo blinks on every shot
	if ( ps->weapon == hud.weapon && ps->weapon > 0 && ps->weapon < MAX_WEAPONS && ps->ammo[ps->weapon] < hud.lastAmmo
		&& !oz_accessibility_reduced_flashing->integer ) {
		hud.ammoFlash = 1;
	}
	hud.weapon = ps->weapon;
	hud.lastAmmo = ps->weapon > 0 && ps->weapon < MAX_WEAPONS ? ps->ammo[ps->weapon] : 0;
	hud.ammoFlash = MoveToward( hud.ammoFlash, 0, hud.dt * 8 );
	// the weapon bar
	if ( cg.weaponSelect != hud.barWeapon || ps->stats[STAT_WEAPONS] != hud.barOwned ) {
		hud.barShown = 0;
	}
	hud.barOwned = ps->stats[STAT_WEAPONS];
	hud.barWeapon = cg.weaponSelect;
	hud.barShown += hud.dt;
	for ( i = 1; i < 10; i++ ) {
		hud.raise[i] = MoveToward( hud.raise[i], i == hud.barWeapon ? 1.0f : 0.0f, hud.dt * 8 );
	}
	// speed
	vel = (float)sqrt( cg.predictedPlayerState.velocity[0] * cg.predictedPlayerState.velocity[0] +
		cg.predictedPlayerState.velocity[1] * cg.predictedPlayerState.velocity[1] );
	UpdateSpeed( ps->pm_type == PM_DEAD ? 0 : vel );
	TopSpeed( ps, vel );
	// feed, hits, notify
	for ( i = 0; i < hud.numFeed; i++ ) {
		hud.feed[i].t += hud.dt;
	}
	while ( hud.numFeed && hud.feed[hud.numFeed - 1].t >= FEED_LIFE ) {
		hud.numFeed--;
	}
	for ( i = 0; i < hud.numHits; i++ ) {
		hud.hits[i].t += hud.dt;
	}
	while ( hud.numHits && hud.hits[0].t >= DAMAGE_LIFE ) {
		for ( i = 1; i < hud.numHits; i++ ) {
			hud.hits[i - 1] = hud.hits[i];
		}
		hud.numHits--;
	}
	hud.flash = MoveToward( hud.flash, 0, hud.dt * 2.2f );
	for ( i = 0; i < hud.numNotify; i++ ) {
		hud.notify[i].t += hud.dt;
	}
	while ( hud.numNotify && hud.notify[0].t >= 6.0f ) {
		for ( i = 1; i < hud.numNotify; i++ ) {
			hud.notify[i - 1] = hud.notify[i];
		}
		hud.numNotify--;
	}
	if ( hud.attackerT >= 0 ) {
		hud.attackerT += hud.dt;
	}
	// damage taken (CG_DamageFeedback's condition), hits landed (the crosshair's hit color)
	if ( ps->damageEvent != hud.damageEvent ) {
		hud.damageEvent = ps->damageEvent;
		if ( ps->damageCount > 0 ) {
			DamageFeedback( ps );
		}
	}
	if ( ps->persistant[PERS_HITS] > hud.hits0 ) {
		hud.hitT = 0;
	}
	hud.hits0 = ps->persistant[PERS_HITS];
	hud.hitT += hud.dt;
	hud.pulseT = ( cg.time - cg.itemPickupBlendTime ) / 1000.0f;
	if ( hud.pulseT < 0 ) {
		hud.pulseT = 99;
	}
	// lagometer: the frames' time and the ping, HISTORY_RATE samples a second (a sample a frame covered a
	// tenth of a second at 1000 fps and four at 30); a sample keeps the longest frame since the last
	hud.lagWorst = MAX( hud.lagWorst, (float)cg.frametime );
	hud.lagT += hud.dt;
	while ( hud.lagT >= 1.0f / HISTORY_RATE ) {
		hud.lagT -= 1.0f / HISTORY_RATE;
		if ( hud.numLag == LAG_SAMPLES ) {
			for ( i = 1; i < LAG_SAMPLES; i++ ) {
				hud.lagFrames[i - 1] = hud.lagFrames[i];
				hud.lagSnaps[i - 1] = hud.lagSnaps[i];
			}
			hud.numLag--;
		}
		hud.lagFrames[hud.numLag] = hud.lagWorst;
		hud.lagSnaps[hud.numLag] = (float)cg.snap->ping;
		hud.numLag++;
		hud.lagWorst = 0;
	}
	// death
	if ( ps->pm_type == PM_DEAD && !hud.dead ) {
		hud.deathTime = cg.time;
	}
	hud.dead = ps->pm_type == PM_DEAD;
	// distance covered, alive, without teleports and respawns
	if ( ps->pm_type == PM_NORMAL && hud.lastValid && !( ( ps->eFlags ^ hud.lastFlags ) & EF_TELEPORT_BIT ) ) {
		const float step = Distance( ps->origin, hud.lastOrigin );
		if ( step < 128 ) {
			hud.distance += step;
		}
	}
	hud.lastValid = ps->pm_type == PM_NORMAL;
	VectorCopy( ps->origin, hud.lastOrigin );
	hud.lastFlags = ps->eFlags;
	// the others' names and scores beside yours: the scores every 2.5 s (the scoreboard asks
	// every 2 s on its own while it's up)
	if ( cg.time - hud.scoresAsked > 2500 || cg.time < hud.scoresAsked ) {
		hud.scoresAsked = cg.time;
		cg.scoresRequestTime = cg.time;	// so +scores keeps the list it has (CG_ScoresDown empties it otherwise)
		if ( !cg.demoPlayback ) {
			CL_AddReliableCommand( "score", qfalse );
		}
	}
	// cg_playerstate.c CG_CheckAmmo: about five seconds of fire left is low, none is empty
	{
		const int	weapons = ps->stats[STAT_WEAPONS];
		int			total = 0, w;
		cg.lowAmmoWarning = 0;
		for ( w = WP_MACHINEGUN; w < WP_NUM_WEAPONS; w++ ) {
			if ( !( weapons & ( 1 << w ) ) ) {
				continue;
			}
			if ( ps->ammo[w] < 0 ) {
				total = 5000;
				break;
			}
			total += ps->ammo[w] * ( w == WP_ROCKET_LAUNCHER || w == WP_GRENADE_LAUNCHER || w == WP_RAILGUN || w == WP_SHOTGUN ? 1000 : 200 );
			if ( total >= 5000 ) {
				break;
			}
		}
		if ( total < 5000 && ps->stats[STAT_WEAPONS] & ~( 1 << WP_GAUNTLET ) ) {
			cg.lowAmmoWarning = total == 0 ? 2 : 1;
		}
	}
	UpdateRewards( ps, ps->pm_type == PM_INTERMISSION || ps->persistant[PERS_TEAM] == TEAM_SPECTATOR );
	// captions age out
	for ( i = 0; i < hud.numCaptions; i++ ) {
		hud.captions[i].t += hud.dt;
	}
	while ( hud.numCaptions && hud.captions[0].t >= CAPTION_TIME ) {
		for ( i = 1; i < hud.numCaptions; i++ ) {
			hud.captions[i - 1] = hud.captions[i];
		}
		hud.numCaptions--;
	}
	// fps
	hud.fpsFrames++;
	if ( Sys_Milliseconds() - hud.fpsTime >= 500 ) {
		hud.fps = hud.fpsFrames * 1000 / ( Sys_Milliseconds() - hud.fpsTime > 0 ? Sys_Milliseconds() - hud.fpsTime : 1 );
		hud.fpsFrames = 0;
		hud.fpsTime = Sys_Milliseconds();
	}
}

static void DrawScoreboard( float W, float H, const playerState_t *ps );
static void DrawResults( float W, float H, const playerState_t *ps );
static void AwardXP( const playerState_t *ps );
static void RecordMatch( const playerState_t *ps );
static int resultsStart;
#define CAMPAIGN_RESULTS	12.0f	// seconds of a campaign arena's results before the campaign page
static int boardShown;		// frames since the board was last drawn, counting down

// cg_draw.c CG_ScanForCrosshairEntity: the player in front of the crosshair, if the world doesn't hide
// them (a ray against the players' boxes in the snapshot, then the world between)
static void ScanForCrosshairEntity( const playerState_t *ps ) {
	vec3_t	start, end, forward, d;
	float	best = 8192;
	int		i, hit = -1;

	if ( !cg.refdefValid ) {
		return;
	}
	VectorCopy( cg.refdef.vieworg, start );
	VectorCopy( cg.refdef.viewaxis[0], forward );
	for ( i = 0; i < cl.snap.numEntities; i++ ) {
		const entityState_t	*es = SnapEntity( i );
		float				along, side;
		vec3_t				c;
		if ( es->eType != ET_PLAYER || es->number >= MAX_CLIENTS || es->number == ps->clientNum || ( es->eFlags & EF_DEAD )
			|| ( es->powerups & ( 1 << PW_INVIS ) ) ) {
			continue;
		}
		VectorCopy( es->pos.trBase, c );
		c[2] += 8;
		VectorSubtract( c, start, d );
		along = DotProduct( d, forward );
		if ( along <= 0 || along >= best ) {
			continue;
		}
		VectorMA( d, -along, forward, d );
		side = VectorLength( d );
		if ( side < 22 ) {		// about a player's half width and height, as the ray passes
			best = along;
			hit = es->number;
		}
	}
	if ( hit < 0 ) {
		return;
	}
	{
		trace_t	tr;
		VectorMA( start, best, forward, end );
		CM_BoxTrace( &tr, start, end, vec3_origin, vec3_origin, 0, CONTENTS_SOLID | CONTENTS_BODY, qfalse );
		if ( tr.fraction < 0.98f ) {
			return;
		}
	}
	cg.crosshairClientNum = hit;
	cg.crosshairClientTime = cg.time;
}

/*
The texts a kill, a death and the match's end first show, at the sizes they'll take: each one's face and
outline made ahead, one a frame from a map's first HUD frames (oz_ui_hud.cpp draws its atlases off the
screen a frame later). A face takes 2 to 3 ms and its atlases 1 to 2 the first time, and the first kill of a
run made two of them in its frame.
*/
static void WarmTexts( float H ) {
	static const float	white[4] = { 1, 1, 1, 1 };
	const float			card = 80 * u;		// the death card's height
	const struct { int font; float size, outline; int flags; } texts[] = {
		{ FONT_LABEL, 30 * u, 0.35f, 0 }, { FONT_LABEL, 30 * u * 0.7f, 0.35f, 0 },		// the center print's lines
		{ FONT_NUM, 36 * u, 0.85f, TEXT_STEPPED }, { FONT_NUM, 44 * u, 0.85f, TEXT_STEPPED },	// damage numbers,
		{ FONT_NUM, 52 * u, 0.85f, TEXT_STEPPED }, { FONT_NUM, 60 * u, 0.85f, TEXT_STEPPED },	// their steps
		{ FONT_NUM, 68.4f * u, 0.85f, TEXT_STEPPED },
		{ FONT_MEDIUM, 19 * u, 0.35f, 0 }, { FONT_LABEL, 13 * u, 0.35f, 0 },			// the feed
		{ FONT_NUM, 26 * u, 0.35f, 0 },													// a medal's count
		{ FONT_LABEL, 18 * u, 0.35f, 0 }, { FONT_LABEL, 16 * u, 0.35f, 0 },			// the death card
		{ FONT_LABEL, card * 0.32f, 0.35f, 0 }, { FONT_LABEL, card * 0.2f, 0, 0 },
		{ FONT_LABEL, card * 0.16f, 0.35f, 0 }, { FONT_LABEL, card * 0.34f, 0.35f, 0 },
		{ FONT_MEDIUM, 28 * u, 0.35f, 0 },
		{ FONT_LABEL, 64 * u, 0.35f, 0 },												// FIGHT!
		{ FONT_LABEL, H * 0.11f, 0.35f, 0 }, { FONT_MEDIUM, H * 0.028f, 0.35f, 0 },	// the results
	};

	if ( hud.warmed >= 0 && hud.warmed < (int)ARRAY_LEN( texts ) ) {
		OzUI_HudText( "0", 0, 0, texts[hud.warmed].font, (float)(int)texts[hud.warmed].size, 1.0f,
			texts[hud.warmed].flags | TEXT_WARM, white, texts[hud.warmed].outline, 0 );
		hud.warmed++;
	}
}

void OzHud_Draw( void ) {
	const playerState_t	*ps;
	float				W, H;
	const char			*p;
	float				m, vitW, vitH, ammW, ammH, barX, barY, barW, barH, speedW, speedH, speedX, speedY;
	float				vitX, vitY, ammoX, ammoY, feedX, feedY, feedW, feedH, chatW, chatH, chatBottom, barTop;
	qboolean			barOn, classic, speedOn, arc, playing, spectator;
	int64_t				prof;	// frametime's details

	if ( !OzHud_Active() ) {
		return;
	}
	// what a cgame knows, from the client
	cg.snap = &cl.snap;
	ps = &cl.snap.ps;
	cg.time = cl.serverTime;
	cg.frametime = cls.frametime;
	cg.weaponSelect = cl.cmds[cl.cmdNumber & CMD_MASK].weapon;
	cg.demoPlayback = clc.demoplaying;
	VectorCopy( ps->velocity, cg.predictedPlayerState.velocity );
	cgs.glconfig.w = cls.glconfig.vidWidth;
	cgs.glconfig.h = cls.glconfig.vidHeight;
	W = (float)cgs.glconfig.w;
	H = (float)cgs.glconfig.h;
	prof = Com_ProfStart();
	ReadConfigstrings();
	ReadEvents();
	ScanForCrosshairEntity( ps );
	LoadImages();
	LoadCards();
	Update( ps );
	RecordMatch( ps );
	Com_ProfDetail( "hud update", prof );
	if ( !cg_draw2D->integer ) {
		return;		// the screen is the world's, as id's cgame leaves it; the HUD keeps following the match
	}
	p = oz_hud_preset->string;
	hud.preset = !Q_stricmp( p, "competitive" ) ? 1 : !Q_stricmp( p, "minimal" ) ? 2 : !Q_stricmp( p, "classic" ) ? 3 : 0;
	classic = hud.preset == 3;
	u = H / 1080.0f * ( oz_hud_scale->value > 0 ? oz_hud_scale->value : 1.0f ) * ( hud.preset == 1 ? 0.88f : 1.0f );
	hudAlpha = oz_hud_opacity->value > 0 ? ( oz_hud_opacity->value < 1 ? oz_hud_opacity->value : 1.0f ) : 1.0f;
	WarmTexts( H );
	compact = hud.preset != 0;
	spectator = ps->persistant[PERS_TEAM] == TEAM_SPECTATOR;
	playing = !spectator && ps->pm_type != PM_INTERMISSION;

	// the layout (q3_hud.gd layout())
	m = 34 * u;
	arc = !Q_stricmp( oz_hud_speedometer->string, "arc" ) && hud.preset != 1 && hud.preset != 3;
	speedOn = oz_hud_show_speedometer->integer != 0;
	barOn = oz_hud_ammo_bar->integer && hud.preset != 2;
	feedW = 560 * u;
	feedH = 260 * u;
	feedX = W - feedW - m;
	feedY = m;
	vitW = compact ? 400 * u : 530 * u;
	vitH = compact ? 66 * u : 92 * u;
	ammW = compact ? 260 * u : 330 * u;
	ammH = vitH;
	barW = W * 0.5f;
	barH = 84 * u;
	barX = W * 0.25f;
	barY = H - barH - 14 * u;
	barTop = barOn ? barY : H - m * 0.5f;
	speedW = arc ? 300 * u : 200 * u;
	speedH = arc ? 170 * u : 56 * u;
	if ( classic ) {
		const float rowW = vitW + ammW + 20 * u;
		const float x0 = ( W - rowW ) * 0.5f;
		ammoX = x0;
		ammoY = H - vitH - m * 0.6f;
		vitX = x0 + ammW + 20 * u;
		vitY = ammoY;
		barY = ammoY - barH - 10 * u;
		speedX = ( W - speedW ) * 0.5f;
		speedY = barY - speedH;
	} else {
		vitX = m;
		vitY = H - vitH - m;
		ammoX = W - ammW - m;
		ammoY = H - ammH - m;
		speedX = ( W - speedW ) * 0.5f;
		speedY = barTop - speedH - 4 * u;
	}
	chatBottom = classic ? H - m : vitY - 58 * u;
	chatW = 720 * u < W * 0.42f ? 720 * u : W * 0.42f;
	chatH = 14 * 25 * u + 50 * u;

	// the board replaces the match's own lines while it's up, and at the intermission
	cg.scoreBoardShowing = cg.showScores || ps->pm_type == PM_INTERMISSION;

	DrawDamage( W, H, playing && hud.health > 0 && hud.health <= 25 );
	if ( playing && !hud.dead && !cg.scoreBoardShowing ) {
		DrawCrosshair( W, H, ps );
	}
	DrawDamageNumbers( W, H );
	if ( ps->pm_type != PM_INTERMISSION ) {
		DrawStatus( W, ps );
	}
	if ( Defrag() ) {
		DefragUpdate( ps );
		if ( ps->pm_type != PM_INTERMISSION && !cg.scoreBoardShowing ) {
			DrawDefragTimer( W, ps );
		}
	}
	if ( oz_hud_kill_feed->integer && ps->pm_type != PM_INTERMISSION ) {
		DrawFeed( feedX, feedY, feedW );
	}
	if ( playing ) {
		const float saved = hudAlpha;
		if ( hud.dead ) {
			hudAlpha *= 0.45f;
		}
		if ( speedOn && !cg.scoreBoardShowing ) {
			if ( arc ) {
				DrawSpeedArc( speedX, speedY, speedW, speedH );
			} else {
				DrawSpeedCompact( speedX, speedY, speedW, speedH );
			}
		}
		if ( barOn || classic ) {
			DrawWeaponBar( barX, barY, barW, barH, ps, classic );
		}
		DrawVitals( vitX, vitY );
		DrawAmmo( ammoX, ammoY, ammW, ps );
		hudAlpha = saved;
	}
	DrawChat( m, chatBottom - chatH, chatW, chatH );
	if ( !cg.scoreBoardShowing ) {
		DrawMessages( W, H, vitX, vitY, ammoX, ammoY, ammW, feedX, feedY, feedW, feedH, ps );
	}
	// item timers are for spectators only (Ozmium, 2026-10-29): a player who knows when items come
	// back has an edge the game doesn't give (a followed player's view counts as spectating)
	if ( oz_hud_item_timers->integer && ( spectator || ( ps->pm_flags & PMF_FOLLOW ) ) && !cg.scoreBoardShowing ) {
		DrawItemTimers( H );
	}
	if ( cg_lagometer->integer && ps->pm_type != PM_INTERMISSION ) {
		DrawLagometer( W, H );
	}
	if ( cg_drawFPS->integer ) {
		Text( FONT_MEDIUM, W - 34 * u, H * 0.5f - 140 * u, va( "%d FPS", hud.fps ), 16 * u, DIM, ALIGN_RIGHT );
	}
	if ( oz_accessibility_subtitles->integer && hud.numCaptions ) {
		// above whatever is highest at the bottom middle, or above the death text
		float top = classic ? ( vitY < ammoY ? vitY : ammoY ) : H - 34 * u;
		if ( playing && ( barOn || classic ) && barY < top ) {
			top = barY;
		}
		if ( playing && speedOn && speedY < top ) {
			top = speedY;
		}
		if ( hud.dead && H * 0.6f - 30 * u < top ) {
			top = H * 0.6f - 30 * u;
		}
		DrawCaptions( W, top );
	}
	if ( ps->pm_type == PM_INTERMISSION ) {
		AwardXP( ps );	// with the scoreboard up too
	}
	if ( ps->pm_type == PM_INTERMISSION && !cg.showScores ) {
		DrawResults( W, H, ps );
	} else if ( cg.scoreBoardShowing ) {
		DrawScoreboard( W, H, ps );
	}
	if ( ps->pm_type != PM_INTERMISSION ) {
		resultsStart = 0;
	}
	if ( boardShown > 0 ) {
		boardShown--;
	}
	Flush();
}

// ================================================================ the scoreboard (scoreboard.gd)

static const vec4_t PLACE_COLORS[3] = { { 1.0f, 0.788f, 0.302f, 1 }, { 0.839f, 0.867f, 0.902f, 1 }, { 0.816f, 0.541f, 0.306f, 1 } };
static const char *statLabels[NAMES] = { "", "Gauntlet", "Machinegun", "Shotgun", "Grenade L.", "Rocket L.", "Lightning", "Railgun",
	"Plasma Gun", "BFG10K", "Grapple" };
// the weapons as the match record and the hub's careers name them
static const char *const weaponKeys[] = { "", "gauntlet", "machinegun", "shotgun", "grenade", "rocket", "lightning", "railgun", "plasma",
	"bfg", "grapple" };
static const char *medalNames[6] = { "IMPRESSIVE", "EXCELLENT", "HUMILIATION", "DEFEND", "ASSIST", "CAPTURE" };
static const int medalPers[6] = { PERS_IMPRESSIVE_COUNT, PERS_EXCELLENT_COUNT, PERS_GAUNTLET_FRAG_COUNT, PERS_DEFEND_COUNT,
	PERS_ASSIST_COUNT, PERS_CAPTURES };

static const char *GametypeName( void ) {
	switch ( cgs.gametype ) {
	case GT_TOURNAMENT: return "DUEL";
	case GT_SINGLE_PLAYER: return "SINGLE PLAYER";
	case GT_TEAM: {
		// A51's Clan Arena has no mark of its own but its name, "*A51* Clan Arena" or "A51|CA" (as the
		// Server Browser reads it)
		char name[MAX_INFO_STRING + 2], *c;
		Com_sprintf( name, sizeof( name ), " %s ", Info_ValueForKey( CS( CS_SERVERINFO ), "sv_hostname" ) );
		Q_CleanStr( name );
		for ( c = name; *c; c++ ) {
			*c = isalnum( (unsigned char)*c ) ? tolower( (unsigned char)*c ) : ' ';
		}
		if ( A51FreezeTag() ) {
			return "FREEZE TAG";
		}
		if ( Info_ValueForKey( CS( CS_SERVERINFO ), "clientversion" )[0] && ( strstr( name, " clan arena " ) || strstr( name, " ca " ) ) ) {
			return "CLAN ARENA";
		}
		return "TEAM DEATHMATCH";
	}
	case GT_CTF: return "CAPTURE THE FLAG";
	default: return "FREE FOR ALL";
	}
}

static void Outline( float x, float y, float w, float h, float width, const float *color ) {
	float	sq[8];

	sq[0] = x; sq[1] = y;
	sq[2] = x + w; sq[3] = y;
	sq[4] = x + w; sq[5] = y + h;
	sq[6] = x; sq[7] = y + h;
	Strip( sq, 4, width, color, qtrue );
}

// ---- what a card shows: the career's card for the local player (oz_progress.cpp, enforced against its
// unlocks), a card picked from its name for a bot (q3_profile.gd bot_card); looked up again only when the
// career changes or the bot does

typedef struct {
	char	banner[MAX_QPATH];		// an image, "" for the dark void
	int		bannerWide;				// 4:1 card art, else a 4:3 level shot
	char	emblem[MAX_QPATH];
	char	title[64];
	float	accent[4];
	float	frame[4];				// the frame's color
	int		level;					// 0: not shown
} cardData_t;

static struct {
	int			version;			// the career's, when looked up
	cardData_t	card;
} localCard;

static struct {
	char		key[MAX_QPATH + 16];	// name, model and skill, when looked up
	cardData_t	card;
} botCards[MAX_CLIENTS];

#ifdef USE_OZ_ONLINE
// another Oz player's card, as their game sent it to the hub (the roster, oz_online_engine.c)
static struct {
	char		id[24];
	int			level;
	char		parts[1024];			// the card's parts (JSON), when looked up
	cardData_t	card;
} seatCards[MAX_CLIENTS];
#endif

static void FromLook( const ozCardLook_t *look, cardData_t *cd ) {
	Q_strncpyz( cd->banner, look->banner, sizeof( cd->banner ) );
	cd->bannerWide = look->bannerWide;
	if ( look->emblem[0] ) {
		Q_strncpyz( cd->emblem, look->emblem, sizeof( cd->emblem ) );
	}
	if ( look->title[0] ) {
		Q_strncpyz( cd->title, look->title, sizeof( cd->title ) );
	}
	ParseColor( look->accent[0] ? look->accent : "#ff2020", cd->accent );
	ParseColor( look->frame[0] ? look->frame : look->accent, cd->frame );
	cd->level = look->level;
}

static void CardFor( int client, cardData_t *cd ) {
	const ozClientInfo_t	*ci = &cgs.clientinfo[client];
	char					name[MAX_QPATH], key[MAX_QPATH + 16];
	ozCardLook_t			look;

	memset( cd, 0, sizeof( *cd ) );
	ParseColor( "#ff2020", cd->accent );	// accent_red, the default card's
	ParseColor( "#ff2020", cd->frame );
	Com_sprintf( cd->emblem, sizeof( cd->emblem ), "models/players/%s/icon_%s", ci->modelName, ci->skinName );
	Q_strncpyz( cd->title, "Fresh Meat", sizeof( cd->title ) );
	if ( client == clc.clientNum ) {
		if ( localCard.version != OzProgress_CareerVersion() ) {
			localCard.version = OzProgress_CareerVersion();
			localCard.card = *cd;
			OzProgress_LocalCard( &look );
			FromLook( &look, &localCard.card );
		}
		*cd = localCard.card;
		return;
	}
	if ( ci->botSkill > 0 ) {
		PlayerName( client, name, sizeof( name ) );
		Q_CleanStr( name );
		Com_sprintf( key, sizeof( key ), "%s/%s/%d", name, ci->modelName, ci->botSkill );
		if ( strcmp( botCards[client].key, key ) ) {
			Q_strncpyz( botCards[client].key, key, sizeof( botCards[client].key ) );
			botCards[client].card = *cd;
			OzProgress_BotCard( name, ci->modelName, ci->botSkill, &look );
			FromLook( &look, &botCards[client].card );
		}
		*cd = botCards[client].card;
		return;
	}
#ifdef USE_OZ_ONLINE
	{
		ozSeat_t seat;
		PlayerName( client, name, sizeof( name ) );
		if ( OzOnline_Seated( client, name, &seat ) ) {
			if ( strcmp( seatCards[client].id, seat.id ) || seatCards[client].level != seat.level || strcmp( seatCards[client].parts, seat.card ) ) {
				Q_strncpyz( seatCards[client].id, seat.id, sizeof( seatCards[client].id ) );
				seatCards[client].level = seat.level;
				Q_strncpyz( seatCards[client].parts, seat.card, sizeof( seatCards[client].parts ) );
				seatCards[client].card = *cd;
				OzProgress_CardLook( seat.card, seat.level, &look );
				FromLook( &look, &seatCards[client].card );
			}
			*cd = seatCards[client].card;
		}
	}
#endif
}

// a player's card images, named as they join (one player a frame) so they're read ahead rather than at the
// card's first show: the killer's card at your death, the board
static void LoadCards( void ) {
	static int	next;
	cardData_t	cd;
	int			i;

	for ( i = 0; i < MAX_CLIENTS; i++, next = ( next + 1 ) % MAX_CLIENTS ) {
		if ( cgs.clientinfo[next].infoValid ) {
			CardFor( next, &cd );
			ImageNamed( cd.banner );
			ImageNamed( cd.emblem );
			next = ( next + 1 ) % MAX_CLIENTS;
			return;
		}
	}
}

// a player's card (q3_card_view.gd): the banner darkened toward the text, the emblem in its square,
// the accent's frame, the name in Quake 3's font, the title, the level
static void Card( float x, float y, float w, float h, int client, qboolean highlight, float alpha ) {
	static const vec4_t base = { 0.04f, 0.035f, 0.035f, 1 };
	static const vec4_t banner = { 0.75f, 0.75f, 0.75f, 1 };
	cardData_t			cd;
	float				c[4], c2[4], a[2], b[2], cc[2], d[2], e, textW, badgeW;
	char				name[MAX_QPATH];
	int					image;

	CardFor( client, &cd );
	Col( c, base, alpha );
	Rect( x, y, w, h, c );
	image = ImageNamed( cd.banner );
	if ( image ) {
		// the banner cropped to the card's shape, around its middle: 4:1 card art or a 4:3 level shot
		const float band = w > 0 ? ( h / w ) / ( cd.bannerWide ? 0.25f : 0.75f ) : 0.3f;
		const float t0 = ( 1 - band ) * 0.5f;
		Col( c, banner, alpha );
		UseImage( image, 6 );
		Vert( x, y, 0, t0, c, 1 );
		Vert( x + w, y, 1, t0, c, 1 );
		Vert( x + w, y + h, 1, t0 + band, c, 1 );
		Vert( x, y, 0, t0, c, 1 );
		Vert( x + w, y + h, 1, t0 + band, c, 1 );
		Vert( x, y + h, 0, t0 + band, c, 1 );
		UseImage( 0, 0 );
	}
	Col( c, BLACK, 0.15f * alpha );
	Col( c2, BLACK, 0.8f * alpha );
	a[0] = x; a[1] = y; b[0] = x + w; b[1] = y; cc[0] = x + w; cc[1] = y + h; d[0] = x; d[1] = y + h;
	Quad4( a, b, cc, d, c, c2, c2, c );
	e = h - 12;
	Col( c, BLACK, 0.6f * alpha );
	Rect( x + 6, y + 6, e, e, c );
	Col( c, WHITE, alpha );
	Image( ImageNamed( cd.emblem ), x + 6, y + 6, e, e, c );
	Col( c, cd.accent, alpha );
	Outline( x + 6, y + 6, e, e, 2, c );
	Col( c, cd.frame, alpha );	// the card's frame
	c[0] *= 0.8f;
	c[1] *= 0.8f;
	c[2] *= 0.8f;
	Outline( x, y, w, h, 2, c );
	if ( highlight ) {
		Col( c, WHITE, 0.9f * alpha );
		Outline( x + 1, y + 1, w - 2, h - 2, 2, c );
	}
	badgeW = h * 0.75f > 50 ? h * 0.75f : 50;
	textW = w - ( 6 + e + 14 ) - badgeW - 20;
	PlayerName( client, name, sizeof( name ) );
	Upper( name );
	Col( c, WHITE, alpha );
	CText( FONT_LABEL, x + 6 + e + 14, y + h * 0.46f, name, h * 0.32f, c, ALIGN_LEFT, textW );
	if ( cd.title[0] ) {
		Mix( c, cd.accent, WHITE, 0.35f );
		c[3] = alpha;
		TextFlags( FONT_LABEL, x + 6 + e + 14, y + h * 0.8f, cd.title, h * 0.2f, c, ALIGN_LEFT | TEXT_NOCODES, 0, textW );
	}
	if ( cd.level > 0 ) {
		Col( c, WHITE, 0.6f * alpha );
		Text( FONT_LABEL, x + w - badgeW * 0.5f - 10, y + h * 0.42f, "LVL", h * 0.16f, c, ALIGN_CENTER );
		Col( c, WHITE, alpha );
		Text( FONT_LABEL, x + w - badgeW * 0.5f - 10, y + h * 0.82f, va( "%d", cd.level ), h * 0.34f, c, ALIGN_CENTER );
	}
}

static void PingBars( float x, float y, int ping ) {
	static const vec4_t green = { 0.373f, 0.878f, 0.541f, 1 };
	static const vec4_t off = { 1, 1, 1, 0.15f };
	const int	lit = ping >= 999 ? 0 : ping < 50 ? 4 : ping < 100 ? 3 : ping < 180 ? 2 : 1;
	const float	*col = lit >= 3 ? green : lit == 2 ? AMBER : RED;
	int			i;

	for ( i = 0; i < 4; i++ ) {
		const float h = ( 6.0f + i * 4.0f ) * u;
		Rect( x + i * 7 * u, y - h, 5 * u, h, i < lit ? col : off );
	}
}

static void Section( float x, float y, float w, const char *title ) {
	const float	tw = TextWidth( FONT_LABEL, title, 15 * u );
	float		c[4];

	Text( FONT_LABEL, x, y, title, 15 * u, RED, ALIGN_LEFT );
	Col( c, RED, 0.35f );
	Rect( x + tw + 10 * u, y - 5 * u, w - tw - 10 * u, 1, c );
}

#define STATS_ROW	34.0f

static int StatWeapons( int *list ) {
	int	w, n = 0;

	for ( w = 1; w < NAMES; w++ ) {
		if ( hud.shots[w] || hud.damage[w] ) {
			list[n++] = w;
		}
	}
	return n;
}

static float StatsHeight( void ) {
	int	list[NAMES];
	int	n = StatWeapons( list );

	return ( 176.0f + 48.0f + ( n > 1 ? n : 1 ) * STATS_ROW + 70.0f + 52.0f + 112.0f ) * u;
}

// your match: the four big numbers, each weapon's damage and accuracy, distance, medals
static void DrawStats( float x0, float y0, float w0, const playerState_t *ps ) {
	static const vec4_t box = { 1, 1, 1, 0.035f };
	static const vec4_t track = { 1, 1, 1, 0.08f };
	static const char *bigLabels[4] = { "SCORE", "DEATHS", "DAMAGE", "ACCURACY" };
	const float	x = x0 + 24 * u, w = w0 - 44 * u;
	float		y = y0 + 40 * u, c[4];
	int			list[NAMES], n, i, shots = 0, hits = 0, damage = 0;
	char		big[4][32];

	Rect( x0, y0, w0, StatsHeight(), box );
	Col( c, AMBER, 0.8f );
	Rect( x0, y0, 4 * u, StatsHeight(), c );
	Text( FONT_LABEL, x, y, "YOUR MATCH", 22 * u, INK_HOT, ALIGN_LEFT );
	n = StatWeapons( list );
	for ( i = 0; i < n; i++ ) {
		const int wp = list[i];
		damage += hud.damage[wp];
		if ( wp != WP_GAUNTLET && wp != WP_GRAPPLING_HOOK ) {
			shots += hud.shots[wp];
			hits += hud.statHits[wp] < hud.shots[wp] ? hud.statHits[wp] : hud.shots[wp];
		}
	}
	Com_sprintf( big[0], sizeof( big[0] ), "%d", ps->persistant[PERS_SCORE] );
	Com_sprintf( big[1], sizeof( big[1] ), "%d", ps->persistant[PERS_KILLED] );
	Com_sprintf( big[2], sizeof( big[2] ), "%d", damage );
	if ( shots > 0 ) {
		Com_sprintf( big[3], sizeof( big[3] ), "%d%%", (int)( hits * 100.0f / shots + 0.5f ) );
	} else {
		Q_strncpyz( big[3], "-", sizeof( big[3] ) );
	}
	y += 26 * u;
	for ( i = 0; i < 4; i++ ) {
		const float bx = x + w * i / 4.0f;
		Text( FONT_NUM, bx, y + 52 * u, big[i], 40 * u, INK_HOT, ALIGN_LEFT );
		Col( c, DIM, 0.9f );
		Text( FONT_LABEL, bx + 2 * u, y + 76 * u, bigLabels[i], 13 * u, c, ALIGN_LEFT );
	}
	y += 120 * u;
	Section( x, y, w, "WEAPONS" );
	y += 22 * u;
	Col( c, DIM, 0.8f );
	Text( FONT_LABEL, x + w * 0.52f, y + 4 * u, "DAMAGE", 12 * u, c, ALIGN_CENTER );
	Text( FONT_LABEL, x + w * 0.72f, y + 4 * u, "HITS / SHOTS", 12 * u, c, ALIGN_CENTER );
	Text( FONT_LABEL, x + w * 0.93f, y + 4 * u, "ACCURACY", 12 * u, c, ALIGN_CENTER );
	y += 14 * u;
	if ( !n ) {
		Col( c, DIM, 0.9f );
		Text( FONT_MEDIUM, x, y + 24 * u, "No shots yet", 19 * u, c, ALIGN_LEFT );
		y += STATS_ROW * u;
	}
	for ( i = 0; i < n; i++ ) {
		const int	wp = list[i];
		const float	ry = y + STATS_ROW * u;
		const qboolean counted = wp != WP_GAUNTLET && wp != WP_GRAPPLING_HOOK && hud.shots[wp] > 0;
		const float	a = counted ? Clamp01( (float)hud.statHits[wp] / hud.shots[wp] ) : -1;
		Image( img.weapon[wp], x, ry - 24 * u, 26 * u, 26 * u, WHITE );
		Text( FONT_MEDIUM, x + 36 * u, ry - 4 * u, statLabels[wp], 19 * u, INK, ALIGN_LEFT );
		Text( FONT_MEDIUM, x + w * 0.52f, ry - 4 * u, va( "%d", hud.damage[wp] ), 20 * u, INK_HOT, ALIGN_CENTER );
		Col( c, INK, 0.8f );
		Text( FONT_MEDIUM, x + w * 0.72f, ry - 4 * u, a >= 0 ? va( "%d / %d", hud.statHits[wp], hud.shots[wp] ) : va( "%d", hud.statHits[wp] ),
			18 * u, c, ALIGN_CENTER );
		if ( a >= 0 ) {
			Bar( x + w * 0.85f, ry - 15 * u, w * 0.15f, 6 * u, 0, 1, track );
			Bar( x + w * 0.85f, ry - 15 * u, w * 0.15f, 6 * u, 0, a, a >= 0.5f ? AMBER : INK );
			Text( FONT_MEDIUM, x + w * 0.93f, ry - 18 * u, va( "%d%%", (int)( a * 100 + 0.5f ) ), 15 * u, INK_HOT, ALIGN_CENTER );
		}
		y += STATS_ROW * u;
	}
	y += 28 * u;
	Section( x, y, w, "DISTANCE" );
	{
		const float meters = hud.distance / 32.0f;
		Text( FONT_NUM, x, y + 44 * u, meters >= 1000 ? va( "%.2f km", meters / 1000 ) : va( "%d m", (int)( meters + 0.5f ) ), 30 * u, INK_HOT, ALIGN_LEFT );
	}
	y += 74 * u;
	Section( x, y, w, "MEDALS" );
	for ( i = 0; i < 6; i++ ) {
		const int	count = ps->persistant[medalPers[i]];
		const float	mx = x + w * i / 6.0f;
		Col( c, WHITE, count > 0 ? 1.0f : 0.22f );
		Image( img.medals[i], mx, y + 18 * u, 46 * u, 46 * u, c );
		if ( count > 0 ) {
			Col( c, INK_HOT, 1 );
		} else {
			Col( c, DIM, 0.6f );
		}
		Text( FONT_MEDIUM, mx + 23 * u, y + 86 * u, va( "%d", count ), 18 * u, c, ALIGN_CENTER );
		Col( c, DIM, count > 0 ? 0.85f : 0.5f );
		Text( FONT_LABEL, mx + 23 * u, y + 104 * u, medalNames[i], 10 * u, c, ALIGN_CENTER );
	}
}

// ---------------------------------------------------------------- the board's mouse

/*
While Tab holds the board up, the right mouse button frees the mouse for it and again takes it back
(scoreboard.gd set_interactive; holding it while clicking works too): a cursor, rows light up under it,
and a click on one opens that player's card in the left column, where your match shows (a click on the
same row closes it). Letting go of Tab ends it. The card is theirs large, this match's numbers and
their career: yours from your profile, a bot's skill, another Oz player's from the hub, with Add friend
(the hub's roster of the Oz players seated in the match, oz_online_engine.c).
*/
typedef struct {
	float	x, y, w, h;
	int		client;
} boardRect_t;

static struct {
	qboolean	on;				// the board has the mouse
	float		x, y;			// the cursor, in pixels
	int			hover;			// the client under it, -1
	int			selected;		// whose card is open, -1
	int			drawnAt;		// cls.framecount the board last drew
	int			numRows;
	boardRect_t	rows[MAX_CLIENTS];	// where the last board drawn put each player
	boardRect_t	button;			// the card's Add friend, w 0 with none
	char		buttonFor[24];	// the hub id it adds
	int			addedAt;		// cls.realtime of the last Add friend, for the hub's answer
	char		layout[96];		// the layout last printed, and the card's state (tests)
	char		cardState[96];
} boardMouse;

static void BoardMouseReset( void ) {
	memset( &boardMouse, 0, sizeof( boardMouse ) );
	boardMouse.hover = boardMouse.selected = -1;
}

static void BoardMouseOff( void ) {
	if ( boardMouse.on ) {
		boardMouse.on = qfalse;
		OzE_StartLocalSound( "sound/misc/menu3.wav" );
		Com_Printf( "ozhud board: mouse off\n" );
	}
	boardMouse.hover = boardMouse.selected = -1;
}

// the board is up and was drawn, and nothing else has the keys (the console, a menu, the chat line)
static qboolean BoardUp( void ) {
	return cg.showScores && OzHud_Active() && !Key_GetCatcher() && cls.framecount - boardMouse.drawnAt <= 2;
}

static qboolean Inside( const boardRect_t *r, float x, float y ) {
	return r->w > 0 && x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

static void BoardClick( void ) {
	int i;

	if ( Inside( &boardMouse.button, boardMouse.x, boardMouse.y ) ) {
		Com_Printf( "ozhud board: add friend %s\n", boardMouse.buttonFor );
#ifdef USE_OZ_ONLINE
		OzOnline_AddFriend( boardMouse.buttonFor );
#endif
		boardMouse.addedAt = cls.realtime ? cls.realtime : 1;
		OzE_StartLocalSound( "sound/misc/menu2.wav" );
		return;
	}
	for ( i = 0; i < boardMouse.numRows; i++ ) {
		if ( Inside( &boardMouse.rows[i], boardMouse.x, boardMouse.y ) ) {
			const int client = boardMouse.rows[i].client;
			boardMouse.selected = boardMouse.selected == client ? -1 : client;
			boardMouse.cardState[0] = 0;
			OzE_StartLocalSound( boardMouse.selected >= 0 ? "sound/misc/menu1.wav" : "sound/misc/menu3.wav" );
			Com_Printf( boardMouse.selected >= 0 ? "ozhud board: card %d\n" : "ozhud board: card %d closed\n", client );
			return;
		}
	}
}

// CL_MouseEvent: while the board has the mouse it moves the cursor, not the view (1)
int OzHud_BoardMouse( int dx, int dy ) {
	const float scale = cls.glconfig.vidHeight / 1080.0f;	// a mouse count a 1080p pixel, as the desktop's

	if ( !boardMouse.on ) {
		return 0;
	}
	if ( !BoardUp() ) {
		BoardMouseOff();
		return 0;
	}
	boardMouse.x += dx * scale;
	boardMouse.y += dy * scale;
	boardMouse.x = boardMouse.x < 0 ? 0 : boardMouse.x > cls.glconfig.vidWidth - 1 ? cls.glconfig.vidWidth - 1 : boardMouse.x;
	boardMouse.y = boardMouse.y < 0 ? 0 : boardMouse.y > cls.glconfig.vidHeight - 1 ? cls.glconfig.vidHeight - 1 : boardMouse.y;
	return 1;
}

// CL_KeyDownEvent, a key about to run its binding: right click under Tab gives the board the mouse or
// takes it back, and while the board has it, the mouse's buttons are its clicks (1: the key is the board's)
int OzHud_BoardKey( int key ) {
	if ( !BoardUp() ) {
		if ( boardMouse.on ) {
			BoardMouseOff();
		}
		return 0;
	}
	if ( key == K_MOUSE2 ) {
		if ( boardMouse.on ) {
			BoardMouseOff();
		} else {
			boardMouse.on = qtrue;
			boardMouse.x = cls.glconfig.vidWidth * 0.5f;
			boardMouse.y = cls.glconfig.vidHeight * 0.5f;
			boardMouse.hover = -1;
			OzE_StartLocalSound( "sound/misc/menu1.wav" );
			Com_Printf( "ozhud board: mouse on\n" );
		}
		return 1;
	}
	if ( !boardMouse.on ) {
		return 0;
	}
	if ( key == K_MOUSE1 ) {
		BoardClick();
		return 1;
	}
	return ( key >= K_MOUSE3 && key <= K_MOUSE5 ) || key == K_MWHEELUP || key == K_MWHEELDOWN;
}

// the arrow, its point at the cursor
static void BoardCursor( void ) {
	const float x = boardMouse.x, y = boardMouse.y, k = u * 1.15f;
	const float	p[7][2] = { { x, y }, { x, y + 16 * k }, { x + 4 * k, y + 12 * k }, { x + 7 * k, y + 18 * k }, { x + 9 * k, y + 17 * k },
		{ x + 6 * k, y + 11 * k }, { x + 11 * k, y + 11 * k } };
	float		outline[14];
	int			i;

	Tri( p[0], WHITE, p[1], WHITE, p[2], WHITE, 1, 1, 1 );
	Tri( p[0], WHITE, p[2], WHITE, p[5], WHITE, 1, 1, 1 );
	Tri( p[0], WHITE, p[5], WHITE, p[6], WHITE, 1, 1, 1 );
	Tri( p[2], WHITE, p[3], WHITE, p[4], WHITE, 1, 1, 1 );
	Tri( p[2], WHITE, p[4], WHITE, p[5], WHITE, 1, 1, 1 );
	for ( i = 0; i < 7; i++ ) {
		outline[i * 2] = p[i][0];
		outline[i * 2 + 1] = p[i][1];
	}
	Strip( outline, 7, 1.5f * u, BLACK, qtrue );
}

// ozboard: the board's mouse, card and rows (tests)
static void Board_f( void ) {
	int i;

	Com_Printf( "ozboard: mouse %s at %d %d, hover %d, card %d, %d rows, button %s\n", boardMouse.on ? "on" : "off", (int)boardMouse.x,
		(int)boardMouse.y, boardMouse.hover, boardMouse.selected, boardMouse.numRows, boardMouse.button.w > 0 ? boardMouse.buttonFor : "none" );
	for ( i = 0; i < boardMouse.numRows; i++ ) {
		const boardRect_t	*r = &boardMouse.rows[i];
		char				name[MAX_QPATH];
		PlayerName( r->client, name, sizeof( name ) );
		Q_CleanStr( name );
		Com_Printf( "ozboard: row %d at %d %d %d %d: %s\n", r->client, (int)r->x, (int)r->y, (int)r->w, (int)r->h, name );
	}
}

// ozboard_point <client> | button: the cursor to the middle of that player's row or the card's button,
// through the board's own mouse (tests, which run without a mouse)
static void BoardPoint_f( void ) {
	const char	*what = Cmd_Argv( 1 );
	const int	client = atoi( what );
	int			i;

	if ( !Q_stricmp( what, "button" ) ) {
		if ( boardMouse.button.w > 0 ) {
			OzHud_BoardMouse( (int)( ( boardMouse.button.x + boardMouse.button.w * 0.5f - boardMouse.x ) * 1080.0f / cls.glconfig.vidHeight ),
				(int)( ( boardMouse.button.y + boardMouse.button.h * 0.5f - boardMouse.y ) * 1080.0f / cls.glconfig.vidHeight ) );
			Com_Printf( "ozboard: pointing at the button (%s)\n", boardMouse.buttonFor );
			return;
		}
		Com_Printf( "ozboard: no button\n" );
		return;
	}
	for ( i = 0; i < boardMouse.numRows; i++ ) {
		const boardRect_t *r = &boardMouse.rows[i];
		if ( r->client == client ) {
			OzHud_BoardMouse( (int)( ( r->x + r->w * 0.3f - boardMouse.x ) * 1080.0f / cls.glconfig.vidHeight ),
				(int)( ( r->y + r->h * 0.5f - boardMouse.y ) * 1080.0f / cls.glconfig.vidHeight ) );
			Com_Printf( "ozboard: pointing at client %d (%d %d)\n", client, (int)boardMouse.x, (int)boardMouse.y );
			return;
		}
	}
	Com_Printf( "ozboard: client %d has no row\n", client );
}

// ---------------------------------------------------------------- a player's card (scoreboard.gd _player_panel)

typedef struct {
	char	label[48];
	char	value[96];		// a section's title has none
	int		section;
} panelLine_t;

#define PANEL_LINES		24
#define PANEL_CARD		120.0f		// the card's height, 1080p units
#define PANEL_BUTTON	42.0f

static const char *const botSkills[5] = { "I Can Win", "Bring It On", "Hurt Me Plenty", "Hardcore", "Nightmare!" };
static const char *const medalWords[6] = { "Impressive", "Excellent", "Humiliation", "Defend", "Assist", "Capture" };

static void AddLine( panelLine_t *l, int *n, const char *label, const char *value ) {
	if ( *n >= PANEL_LINES ) {
		return;
	}
	Q_strncpyz( l[*n].label, label, sizeof( l[0].label ) );
	Q_strncpyz( l[*n].value, value ? value : "", sizeof( l[0].value ) );
	l[*n].section = value == NULL;
	( *n )++;
}

static const ozScore_t *ScoreRow( int client ) {
	int i;

	for ( i = 0; i < cg.numScores; i++ ) {
		if ( cg.scores[i].client == client ) {
			return &cg.scores[i];
		}
	}
	return NULL;
}

static const char *Hours( int seconds ) {
	return va( "%dh %02dm", seconds / 3600, seconds / 60 % 60 );
}

static const char *Ratio( int frags, int deaths ) {
	return va( "%.2f", frags / (float)( deaths > 1 ? deaths : 1 ) );
}

// what the card says under it, and what kind of player it is for the log
typedef struct {
	panelLine_t	lines[PANEL_LINES];
	int			numLines;
	qboolean	seated;			// another Oz player, with a button
#ifdef USE_OZ_ONLINE
	ozSeat_t	seat;
#endif
	char		state[96];
} panel_t;

static void PanelOf( int client, const playerState_t *ps, panel_t *p ) {
	const ozClientInfo_t	*ci = &cgs.clientinfo[client];
	const ozScore_t			*s = ScoreRow( client );
	const qboolean			me = client == ps->clientNum;
	const int				frags = hud.kills[client];
	const int				deaths = me ? ps->persistant[PERS_KILLED] : hud.deaths[client];
	panelLine_t				*l = p->lines;
	int						*n = &p->numLines, i;
	char					model[MAX_QPATH], medals[96];

	memset( p, 0, sizeof( *p ) );
	AddLine( l, n, "THIS MATCH", NULL );
	AddLine( l, n, "Score", s ? va( "%d", s->score ) : "-" );
	AddLine( l, n, "Frags / deaths", va( "%d / %d", frags, deaths ) );
	AddLine( l, n, "Ratio", Ratio( frags, deaths ) );
	if ( me ) {
		int wp, shots = 0, hits = 0;
		for ( wp = 1; wp < NAMES; wp++ ) {
			if ( wp != WP_GAUNTLET && wp != WP_GRAPPLING_HOOK ) {
				shots += hud.shots[wp];
				hits += hud.statHits[wp] < hud.shots[wp] ? hud.statHits[wp] : hud.shots[wp];
			}
		}
		AddLine( l, n, "Accuracy", shots ? va( "%d%%", (int)( hits * 100.0f / shots + 0.5f ) ) : "-" );
	} else if ( s && cg.scoresFull ) {
		AddLine( l, n, "Accuracy", va( "%d%%", s->accuracy ) );
	}
	if ( s && cg.scoresFull ) {
		const int counts[6] = { s->impressiveCount, s->excellentCount, s->guantletCount, s->defendCount, s->assistCount, s->captures };
		medals[0] = 0;
		for ( i = 0; i < 6; i++ ) {
			if ( counts[i] > 0 ) {
				Q_strcat( medals, sizeof( medals ), va( "%s%s %d", medals[0] ? ", " : "", medalWords[i], counts[i] ) );
			}
		}
		AddLine( l, n, "Medals", medals[0] ? medals : "-" );
	}
	if ( ci->botSkill <= 0 ) {
		AddLine( l, n, "Ping", !s || s->ping >= 999 ? "-" : va( "%d ms", s->ping ) );
	}
	if ( s && s->time > 0 ) {
		AddLine( l, n, "Playing for", va( "%d min", s->time ) );
	}
	Q_strncpyz( model, ci->modelName, sizeof( model ) );
	model[0] = toupper( (unsigned char)model[0] );
	AddLine( l, n, "Character", model );
	if ( ci->botSkill > 0 ) {
		const int skill = ci->botSkill < 1 ? 1 : ci->botSkill > 5 ? 5 : ci->botSkill;
		AddLine( l, n, "BOT", NULL );
		AddLine( l, n, "Skill", va( "%d  %s", skill, botSkills[skill - 1] ) );
		Q_strncpyz( p->state, "bot", sizeof( p->state ) );
		return;
	}
	if ( me ) {
		int into, next, matches, wins, f, d, seconds;
		const int level = OzProgress_LevelFor( OzProgress_TotalXP(), &into, &next );
		OzProgress_LocalCareer( &matches, &wins, &f, &d, &seconds );
		AddLine( l, n, "CAREER", NULL );
		AddLine( l, n, "Level", va( "%d   %d / %d XP", level, into, next ) );
		AddLine( l, n, "Matches / wins", va( "%d / %d", matches, wins ) );
		AddLine( l, n, "Frags / deaths", va( "%d / %d   (%s)", f, d, Ratio( f, d ) ) );
		AddLine( l, n, "Time played", Hours( seconds ) );
		Q_strncpyz( p->state, "you", sizeof( p->state ) );
		return;
	}
	AddLine( l, n, "CAREER", NULL );
#ifdef USE_OZ_ONLINE
	{
		ozCareer_t	career;
		char		name[MAX_QPATH];
		PlayerName( client, name, sizeof( name ) );
		if ( !OzOnline_Online() ) {
			AddLine( l, n, "Offline: online services are off", "" );
			Q_strncpyz( p->state, "offline", sizeof( p->state ) );
		} else if ( !OzOnline_Seated( client, name, &p->seat ) ) {
			AddLine( l, n, "No online profile", "" );
			Q_strncpyz( p->state, "no online profile", sizeof( p->state ) );
		} else {
			static const char *const relations[4] = { "none", "friend", "sent", "asked" };
			p->seated = qtrue;
			if ( !OzOnline_Career( p->seat.id, &career ) ) {
				AddLine( l, n, "Asking the hub", "" );
			} else {
				char	labels[3][32], values[3][24];
				int		shown;
				AddLine( l, n, "Matches / wins", va( "%d / %d", career.matches, career.wins ) );
				AddLine( l, n, "Frags / deaths", va( "%d / %d   (%s)", career.frags, career.deaths, Ratio( career.frags, career.deaths ) ) );
				AddLine( l, n, "Time played", Hours( career.seconds ) );
				for ( i = 1; i < NAMES && Q_stricmp( weaponKeys[i], career.weapon ); i++ ) {
				}
				if ( career.weapon[0] && i < NAMES ) {
					AddLine( l, n, "Best weapon", career.weaponShots > 0 && i != WP_GAUNTLET
						? va( "%s   %d frags, %d%%", statLabels[i], career.weaponFrags, career.weaponHits * 100 / career.weaponShots )
						: va( "%s   %d frags", statLabels[i], career.weaponFrags ) );
				}
				shown = OzProgress_Showcase( p->seat.card, p->seat.stats, labels, values, 3 );
				for ( i = 0; i < shown; i++ ) {
					AddLine( l, n, labels[i], values[i] );
				}
			}
			Com_sprintf( p->state, sizeof( p->state ), "oz %s, %s, %s", p->seat.id, relations[OzOnline_Relation( p->seat.id ) & 3],
				*n > 0 && !strcmp( l[*n - 1].label, "Asking the hub" ) ? "career asked" : "career in" );
		}
	}
#else
	AddLine( l, n, "No online profile", "" );
	Q_strncpyz( p->state, "no online profile", sizeof( p->state ) );
#endif
}

static float PanelHeight( const panel_t *p ) {
	float	h = ( 32 + PANEL_CARD + 30 ) * u;
	int		i;

	if ( p->seated ) {
		h += ( PANEL_BUTTON + 34 ) * u;
	}
	for ( i = 0; i < p->numLines; i++ ) {
		h += ( p->lines[i].section ? 40 : 30 ) * u;
	}
	return h + 16 * u;
}

static void DrawPlayerPanel( float x, float y, float w, int client, const playerState_t *ps ) {
	static const vec4_t box = { 0.03f, 0.025f, 0.022f, 0.92f };
	static const vec4_t off = { 1, 1, 1, 0.06f };
	static panel_t	p;
	const float		pad = 26 * u;
	float			ly, c[4];
	int				i;

	PanelOf( client, ps, &p );
	if ( strcmp( p.state, boardMouse.cardState ) ) {
		Q_strncpyz( boardMouse.cardState, p.state, sizeof( boardMouse.cardState ) );
		Com_Printf( "ozhud board: card %d shows %s\n", client, p.state );
	}
	Rect( x, y, w, PanelHeight( &p ), box );
	Outline( x, y, w, PanelHeight( &p ), 1, GLASS_EDGE );
	Rect( x, y, w, 3 * u, RED );
	Card( x + pad, y + 30 * u, w - pad * 2, PANEL_CARD * u, client, client == ps->clientNum, 1 );
	ly = y + ( 30 + PANEL_CARD + 22 ) * u;
#ifdef USE_OZ_ONLINE
	if ( p.seated ) {
		const int		rel = OzOnline_Relation( p.seat.id );
		const qboolean	active = rel == OZ_REL_NONE || rel == OZ_REL_ASKED;
		const char		*label = rel == OZ_REL_FRIEND ? "FRIENDS" : rel == OZ_REL_SENT ? "FRIEND REQUEST SENT"
			: rel == OZ_REL_ASKED ? "ACCEPT FRIEND REQUEST" : "ADD FRIEND";
		const boardRect_t	r = { x + pad, ly, w - pad * 2, PANEL_BUTTON * u, -1 };
		char			text[256];
		const int		age = OzOnline_Notice( text, sizeof( text ) );
		if ( active ) {
			Col( c, RED, boardMouse.on && Inside( &r, boardMouse.x, boardMouse.y ) ? 1.0f : 0.75f );
			boardMouse.button = r;
			Q_strncpyz( boardMouse.buttonFor, p.seat.id, sizeof( boardMouse.buttonFor ) );
		} else {
			Col( c, off, 1 );
		}
		Rect( r.x, r.y, r.w, r.h, c );
		Outline( r.x, r.y, r.w, r.h, 1, GLASS_EDGE );
		Text( FONT_LABEL, r.x + r.w * 0.5f, r.y + r.h * 0.66f, label, 17 * u, active ? INK_HOT : DIM, ALIGN_CENTER );
		// the hub's answer to Add friend, a moment
		if ( boardMouse.addedAt && age >= 0 && age < 6000 && cls.realtime - boardMouse.addedAt < 8000 ) {
			CText( FONT_MEDIUM, r.x, r.y + r.h + 22 * u, text, 15 * u, INK, ALIGN_LEFT, r.w );
		}
		ly += ( PANEL_BUTTON + 34 ) * u;
	}
#endif
	for ( i = 0; i < p.numLines; i++ ) {
		const panelLine_t *l = &p.lines[i];
		if ( l->section ) {
			const float tw = TextWidth( FONT_LABEL, l->label, 16 * u );
			ly += 10 * u;
			Text( FONT_LABEL, x + pad, ly, l->label, 16 * u, RED, ALIGN_LEFT );
			Col( c, RED, 0.35f );
			Rect( x + pad + tw + 12 * u, ly - 6 * u, w - pad * 2 - tw - 12 * u, 1, c );
			ly += 30 * u;
			continue;
		}
		Col( c, INK, 0.7f );
		Text( FONT_MEDIUM, x + pad, ly, l->label, 19 * u, c, ALIGN_LEFT );
		Text( FONT_NUM, x + w - pad, ly, l->value, 20 * u, INK_HOT, ALIGN_RIGHT );
		ly += 30 * u;
	}
}

// ---------------------------------------------------------------- the rows

// a player's row: place, their icon and name, the flag they carry, score, frags and deaths, ping
static void BoardRow( float x, float y, float w, float h, int client, const ozScore_t *s, int place ) {
	const ozClientInfo_t	*ci = &cgs.clientinfo[client];
	const qboolean			me = client == cg.snap->ps.clientNum;
	const qboolean			hot = boardMouse.on && ( client == boardMouse.hover || client == boardMouse.selected );
	const float				fs = h * 0.56f < 26 * u ? h * 0.56f : 26 * u;
	const float				cy = y + h * 0.5f, base = cy + fs * 0.36f, px = x + w * 0.94f;
	const float				icon = h - 4 * u;
	const float				nameX = x + 42 * u + ( icon >= 14 * u ? icon + 8 * u : 0 );
	const int				deaths = me ? cg.snap->ps.persistant[PERS_KILLED] : hud.deaths[client];
	float					c[4];
	char					name[MAX_QPATH];

	c[0] = c[1] = c[2] = 1;
	c[3] = hot ? 0.13f : place % 2 == 0 ? 0.07f : 0.035f;
	if ( me && !hot ) {
		Col( c, AMBER, 0.13f );
	}
	Panel( x, y, w, h, c, NULL, 0 );
	if ( me || hot ) {
		Rect( x, y, 3 * u, h, me ? AMBER : RED );
	}
	if ( place < 3 ) {
		Col( c, PLACE_COLORS[place], 1 );
	} else {
		Col( c, DIM, 0.9f );
	}
	Text( FONT_NUM, x + 22 * u, base, va( "%d", place + 1 ), fs, c, ALIGN_CENTER );
	if ( icon >= 14 * u ) {
		Image( ImageNamed( va( "models/players/%s/icon_%s", ci->modelName, ci->skinName ) ), x + 42 * u, y + 2 * u, icon, icon, WHITE );
	}
	PlayerName( client, name, sizeof( name ) );
	CText( FONT_MEDIUM, nameX, base, name, fs, INK_HOT, ALIGN_LEFT, x + w * 0.6f - nameX );
	// cg_scoreboard.c: the flag beside a CTF flag carrier
	if ( s && ( s->powerUps & ( ( 1 << PW_REDFLAG ) | ( 1 << PW_BLUEFLAG ) ) ) ) {
		Image( img.flags[( s->powerUps & ( 1 << PW_REDFLAG ) ) ? 0 : 1][1], x + w * 0.62f - h * 0.4f, cy - h * 0.4f, h * 0.8f, h * 0.8f, WHITE );
	}
	Text( FONT_NUM, x + w * 0.71f, base, s ? va( "%d", s->score ) : "-", fs, INK_HOT, ALIGN_CENTER );
	Col( c, INK, 0.85f );
	Text( FONT_MEDIUM, x + w * 0.83f, base, va( "%d / %d", hud.kills[client], deaths ), fs * 0.9f, c, ALIGN_CENTER );
	if ( ci->botSkill > 0 ) {
		Text( FONT_LABEL, px, base, "BOT", fs * 0.7f, DIM, ALIGN_CENTER );
	} else if ( h >= 34 * u && w >= 470 * u ) {		// room for the bars beside the number
		PingBars( px - 34 * u, cy + 9 * u, s ? s->ping : 999 );
		Text( FONT_MEDIUM, px + 22 * u, base, !s || s->ping >= 999 ? "-" : va( "%d", s->ping ), fs * 0.9f, c, ALIGN_CENTER );
	} else {
		Text( FONT_MEDIUM, px, base, !s || s->ping >= 999 ? "-" : va( "%d", s->ping ), fs * 0.9f, c, ALIGN_CENTER );
	}
}

// the header: the level shot, darkened toward the text, the mode, the map, the clock; a crowded board's
// is shorter, its text smaller
static void BoardHeader( float px, float py, float pw, float headH, const char *mode ) {
	static const vec4_t shot = { 1, 1, 1, 1 };
	const float	k = headH / ( 132 * u );
	float		a[2], b[2], cc[2], d[2], c1[4], c2[4], c3[4], c4[4], c[4];
	char		title[MAX_QPATH], mapName[MAX_QPATH];
	int			levelshot, elapsed, ms, secs;

	Q_strncpyz( mapName, Info_ValueForKey( CS( CS_SERVERINFO ), "mapname" ), sizeof( mapName ) );
	levelshot = ImageNamed( va( "levelshots/%s", mapName ) );
	if ( levelshot ) {
		// the shot is 4:3; a band of it across the panel's width
		const float	band = pw > 0 ? headH / pw / 0.75f : 0.3f;
		const float	t0 = ( 1 - band ) * 0.45f;
		UseImage( levelshot, 6 );
		Vert( px, py, 0, t0, shot, 1 );
		Vert( px + pw, py, 1, t0, shot, 1 );
		Vert( px + pw, py + headH, 1, t0 + band, shot, 1 );
		Vert( px, py, 0, t0, shot, 1 );
		Vert( px + pw, py + headH, 1, t0 + band, shot, 1 );
		Vert( px, py + headH, 0, t0 + band, shot, 1 );
		UseImage( 0, 0 );
	}
	Col( c1, BLACK, 0.82f );
	Col( c2, BLACK, 0.12f );
	Col( c3, BLACK, 0.45f );
	Col( c4, BLACK, 0.9f );
	a[0] = px; a[1] = py; b[0] = px + pw; b[1] = py; cc[0] = px + pw; cc[1] = py + headH; d[0] = px; d[1] = py + headH;
	Quad4( a, b, cc, d, c1, c2, c3, c4 );
	Rect( px, py, pw, 3 * u, RED );
	Text( FONT_LABEL, px + 32 * u, py + 62 * u * k, mode, 44 * u * k, INK_HOT, ALIGN_LEFT );
	Q_strncpyz( title, CS( CS_MESSAGE ), sizeof( title ) );
	Upper( title );
	Col( c, AMBER, 0.95f );
	CText( FONT_LABEL, px + 34 * u, py + 100 * u * k, title, 20 * u * ( k > 0.8f ? k : 0.8f ), c, ALIGN_LEFT, 0 );
	elapsed = cg.time - cgs.levelStartTime;
	if ( elapsed < 0 ) {
		elapsed = 0;
	}
	ms = cgs.timelimit > 0 ? cgs.timelimit * 60000 - elapsed : elapsed;
	secs = ( ms > 0 ? ms : 0 ) / 1000;
	Text( FONT_NUM, px + pw - 32 * u, py + 70 * u * k, va( "%d:%02d", secs / 60, secs % 60 ), 46 * u * k, INK_HOT, ALIGN_RIGHT );
	Col( c, DIM, 0.95f );
	if ( cgs.gametype == GT_CTF && cgs.capturelimit > 0 ) {
		Text( FONT_LABEL, px + pw - 34 * u, py + 102 * u * k, va( "FIRST TO %d", cgs.capturelimit ), 15 * u * ( k > 0.87f ? k : 0.87f ), c, ALIGN_RIGHT );
	} else if ( cgs.fraglimit > 0 ) {
		Text( FONT_LABEL, px + pw - 34 * u, py + 102 * u * k, va( "FRAG LIMIT %d", cgs.fraglimit ), 15 * u * ( k > 0.87f ? k : 0.87f ), c, ALIGN_RIGHT );
	}
}

/*
The pause menu's team choices (oz_ui_menu.cpp PauseFrame): your team is your configstring's "t",
which id's game, Oz's and CPMA's all send, so it holds on any server, pure ones too; the snapshot
would show the followed player's team. In a duel id's game keeps every spectator in a line to play
(AddTournamentQueue, 1.32's spectatorTime) and its scores list the waiting ones longest-waiting first
(SortRanks), as CPMA's dmscores lists them after the two duelists: your place is how many spectators
the last scores put ahead of you. Each duel's end puts its loser at the line's back and the next in
line in against the winner.
*/
int OzE_TeamInfo( ozTeamInfo_t *t ) {
	const char	*info;
	int			i, me;

	memset( t, 0, sizeof( *t ) );
	if ( cls.state != CA_ACTIVE || clc.demoplaying ) {
		return 0;
	}
	info = CS( CS_SERVERINFO );
	me = clc.clientNum;
	t->gametype = atoi( Info_ValueForKey( info, "g_gametype" ) );
	t->fraglimit = atoi( Info_ValueForKey( info, "fraglimit" ) );
	t->timelimit = atoi( Info_ValueForKey( info, "timelimit" ) );
	t->capturelimit = atoi( Info_ValueForKey( info, "capturelimit" ) );
	t->redScore = atoi( CS( CS_SCORES1 ) );
	t->blueScore = atoi( CS( CS_SCORES2 ) );
	t->team = TEAM_SPECTATOR;
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const char	*p = CS( CS_PLAYERS + i );
		int			team;
		if ( !p[0] ) {
			continue;
		}
		team = atoi( Info_ValueForKey( p, "t" ) );
		if ( team >= TEAM_FREE && team <= TEAM_SPECTATOR ) {
			t->count[team]++;
		}
		if ( i == me ) {
			t->team = team;
		}
	}
	t->following = t->team == TEAM_SPECTATOR && ( cl.snap.ps.pm_flags & PMF_FOLLOW );
	t->intermission = cl.snap.ps.pm_type == PM_INTERMISSION;
	if ( t->gametype == GT_TOURNAMENT && t->team == TEAM_SPECTATOR ) {
		int ahead = 0;
		for ( i = 0; i < cg.numScores; i++ ) {
			const int c = cg.scores[i].client;
			if ( c == me ) {
				t->line = ahead + 1;
				break;
			}
			if ( c >= 0 && c < MAX_CLIENTS && CS( CS_PLAYERS + c )[0] && atoi( Info_ValueForKey( CS( CS_PLAYERS + c ), "t" ) ) == TEAM_SPECTATOR ) {
				ahead++;
			}
		}
	}
	return 1;
}

// the players watching, as the server lists them (its configstrings: a mod's scores may leave them out),
// names with their color codes; 0 when no one is
static int Spectators( char *out, int size ) {
	char	name[MAX_QPATH];
	int		i, n = 0;

	out[0] = 0;
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( !cgs.clientinfo[i].infoValid || cgs.clientinfo[i].team != TEAM_SPECTATOR ) {
			continue;
		}
		PlayerName( i, name, sizeof( name ) );
		Q_strcat( out, size, va( "%s%s", n++ ? "^7,  " : "", name[0] ? name : "?" ) );
	}
	return n;
}

typedef struct {
	int				client;
	const ozScore_t	*s;		// NULL: a player the game's scores left out
} boardEntry_t;

// everyone on the board, in the scores' order (best first), then any the scores left out, from the
// configstrings (id's game sends about 25 players, Oz's all 64; game patch 0028)
static int BoardEntries( boardEntry_t *out ) {
	qboolean	listed[MAX_CLIENTS];
	int			i, n = 0;

	memset( listed, 0, sizeof( listed ) );
	for ( i = 0; i < cg.numScores; i++ ) {
		const int c = cg.scores[i].client;
		if ( c < 0 || c >= MAX_CLIENTS || listed[c] || !cgs.clientinfo[c].infoValid ) {
			continue;
		}
		listed[c] = qtrue;
		out[n].client = c;
		out[n++].s = &cg.scores[i];
	}
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( !listed[i] && cgs.clientinfo[i].infoValid ) {
			out[n].client = i;
			out[n++].s = NULL;
		}
	}
	return n;
}

// the column a player's row goes in: red or blue in team games, else the one; -1 for a spectator
static int BoardColumn( int client, qboolean teams ) {
	const int team = cgs.clientinfo[client].team;

	if ( teams ) {
		return team == TEAM_RED ? 0 : team == TEAM_BLUE ? 1 : -1;
	}
	return team != TEAM_SPECTATOR ? 0 : -1;
}

// a row and the gap under it, 1080p units, tallest first: a board takes the tallest its rows fit
#define BOARD_STEPS	9
static const float boardPitch[BOARD_STEPS] = { 52, 44, 38, 33, 29, 25, 23, 21, 19 };

static float BoardPitch( int rows, float room ) {
	int i;

	for ( i = 0; i < BOARD_STEPS; i++ ) {
		if ( rows * boardPitch[i] * u <= room ) {
			return boardPitch[i] * u;
		}
	}
	return 0;
}

/*
The board: up to 64 players, every one on it. Compact rows (no cards: a player's card opens with a
click, see the board's mouse), as tall as they fit; a free for all with many players splits into two
or three columns side by side, a team game keeps a column a team. A crowded board's header is shorter.
*/
static void DrawScoreboard( float W, float H, const playerState_t *ps ) {
	static const vec4_t dimmer = { 0, 0, 0, 0.45f };
	static const vec4_t panelColor = { 0.03f, 0.025f, 0.022f, 0.86f };
	static boardEntry_t	entries[MAX_CLIENTS];
	const qboolean	teams = cgs.gametype >= GT_TEAM;
	const int		cols = teams ? 2 : 1;
	const int		numEntries = BoardEntries( entries );
	const qboolean	mine = ps->persistant[PERS_TEAM] != TEAM_SPECTATOR;
	const float		specH = 40 * u, gap = 24 * u;
	char			specs[MAX_STRING_CHARS];
	const int		numSpecs = Spectators( specs, sizeof( specs ) );
	int				count[2] = { 0, 0 }, maxRows, sub = 1, rowsPer, col, i, drawn = 0;
	qboolean		crowded;
	float			pw, px, py, headH, leftX, leftY, leftW, listX, listW, cw, sw, top, room, pitch, rh, bottom, ph, specY, c[4];
	static panel_t	measure;

	if ( !boardShown && cg.numScores > 0 ) {
		// what the board shows, in the console, for the HUD test to check against the server's log
		int wp, shots = 0, hits = 0, damage = 0;
		for ( wp = 1; wp < NAMES; wp++ ) {
			damage += hud.damage[wp];
			if ( wp != WP_GAUNTLET && wp != WP_GRAPPLING_HOOK ) {
				shots += hud.shots[wp];
				hits += hud.statHits[wp];
			}
		}
		for ( i = 0; i < cg.numScores && cg.scores[i].client != cg.snap->ps.clientNum; i++ ) {
		}
		Com_Printf( "ozhud board: shots %d hits %d damage %d server accuracy %d at %d\n", shots, hits, damage,
			i < cg.numScores ? cg.scores[i].accuracy : -1, cg.time - cgs.levelStartTime );
		for ( i = 0; i < MAX_CLIENTS; i++ ) {
			if ( cgs.clientinfo[i].infoValid ) {
				Com_Printf( "ozhud board: client %d kills %d deaths %d\n", i, hud.kills[i], hud.deaths[i] );
			}
		}
		for ( i = 0; i < cg.numScores; i++ ) {
			Com_Printf( "ozhud board: row %d score %d ping %d\n", cg.scores[i].client, cg.scores[i].score, cg.scores[i].ping );
		}
	}
	if ( cg.numScores > 0 ) {
		boardShown = 2;
	}
	if ( cg.time - cg.scoresRequestTime > 2000 && !cg.demoPlayback && ps->pm_type != PM_INTERMISSION ) {
		cg.scoresRequestTime = cg.time;
		CL_AddReliableCommand( "score", qfalse );
	}
#ifdef USE_OZ_ONLINE
	OzOnline_WantRoster();		// who's an Oz player here, for their cards
#endif
	boardMouse.drawnAt = cls.framecount;
	if ( boardMouse.selected >= 0 && !cgs.clientinfo[boardMouse.selected].infoValid ) {
		boardMouse.selected = -1;	// they left
	}
	if ( boardMouse.on ) {
		int hover = -1;
		for ( i = 0; i < boardMouse.numRows; i++ ) {
			if ( Inside( &boardMouse.rows[i], boardMouse.x, boardMouse.y ) ) {
				hover = boardMouse.rows[i].client;
			}
		}
		if ( hover >= 0 && hover != boardMouse.hover ) {
			OzE_StartLocalSound( "sound/misc/menu2.wav" );
		}
		boardMouse.hover = hover;
	}

	// the layout
	for ( i = 0; i < numEntries; i++ ) {
		col = BoardColumn( entries[i].client, teams );
		if ( col >= 0 ) {
			count[col]++;
		}
	}
	maxRows = count[0] > count[1] ? count[0] : count[1];
	crowded = maxRows > ( teams ? 14 : 24 );
	pw = W * 0.92f < 1780 * u ? W * 0.92f : 1780 * u;
	px = ( W - pw ) * 0.5f;
	py = ( crowded ? 36 : 70 ) * u;
	headH = ( crowded ? 92 : 132 ) * u;
	leftX = px + 24 * u;
	leftY = py + headH + 24 * u;
	leftW = pw * 0.33f;
	listX = leftX + leftW + 28 * u;
	listW = px + pw - 24 * u - listX;
	cw = ( listW - gap * ( cols - 1 ) ) / cols;
	top = py + headH + 38 * u + ( teams ? 64 * u : 0 );
	room = H - 50 * u - ( numSpecs ? specH + 4 * u : 0 ) - top;
	pitch = BoardPitch( maxRows, room );
	if ( !teams ) {
		// side by side when that makes the rows taller, each column wide enough for a name
		int k;
		for ( k = 2; k <= 3 && pitch < 33 * u; k++ ) {
			const float p = BoardPitch( ( maxRows + k - 1 ) / k, room );
			if ( ( cw - gap * ( k - 1 ) ) / k >= 330 * u && p > pitch ) {
				sub = k;
				pitch = p;
			}
		}
	} else if ( !pitch && ( cw - gap ) / 2 >= 240 * u ) {
		sub = 2;
		pitch = BoardPitch( ( maxRows + 1 ) / 2, room );
	}
	if ( !pitch ) {
		pitch = boardPitch[BOARD_STEPS - 1] * u;	// the last don't fit: cut
	}
	rowsPer = maxRows > 0 ? ( maxRows + sub - 1 ) / sub : 1;
	rh = pitch - ( pitch >= 33 * u ? 4 : 2 ) * u;
	sw = ( cw - gap * ( sub - 1 ) ) / sub;

	// as tall as the longest column, the spectators' line, or the left column
	bottom = top + rowsPer * pitch;
	specY = bottom + 4 * u;
	if ( numSpecs ) {
		bottom = specY + specH;
	}
	if ( boardMouse.selected >= 0 ) {
		PanelOf( boardMouse.selected, ps, &measure );
		if ( leftY + PanelHeight( &measure ) > bottom ) {
			bottom = leftY + PanelHeight( &measure );
		}
	} else if ( mine && leftY + StatsHeight() > bottom ) {
		bottom = leftY + StatsHeight();
	}
	ph = bottom - py + 70 * u;
	if ( ph > H - py - 10 * u ) {
		ph = H - py - 10 * u;
	}
	Rect( 0, 0, W, H, dimmer );
	Rect( px, py, pw, ph, panelColor );
	Outline( px, py, pw, ph, 1, GLASS_EDGE );
	BoardHeader( px, py, pw, headH, ps->pm_type == PM_INTERMISSION ? "RESULTS" : GametypeName() );
	boardMouse.button.w = 0;
	if ( boardMouse.selected >= 0 ) {
		DrawPlayerPanel( leftX, leftY, leftW, boardMouse.selected, ps );
	} else if ( mine ) {
		DrawStats( leftX, leftY, leftW, ps );
	}
	boardMouse.numRows = 0;
	for ( col = 0; col < cols; col++ ) {
		const float	x0 = listX + col * ( cw + gap );
		float		y = py + headH + 30 * u;
		int			place = 0, k;
		if ( teams ) {
			const float	*tc = col ? HUD_TEAM_BLUE : HUD_TEAM_RED;
			float		dark[4], light[4];
			dark[0] = tc[0] * 0.3f;
			dark[1] = tc[1] * 0.3f;
			dark[2] = tc[2] * 0.3f;
			dark[3] = 0.85f;
			Panel( x0, y - 8 * u, cw, 54 * u, dark, NULL, 0 );
			Bar( x0, y - 8 * u + 50 * u, cw, 4 * u, 0, 1, tc );
			Mix( light, tc, WHITE, 0.2f );
			Text( FONT_LABEL, x0 + 26 * u, y - 8 * u + 36 * u, col ? "BLUE TEAM" : "RED TEAM", 24 * u, light, ALIGN_LEFT );
			Text( FONT_LABEL, x0 + 26 * u + TextWidth( FONT_LABEL, col ? "BLUE TEAM" : "RED TEAM", 24 * u ) + 14 * u, y - 8 * u + 36 * u,
				va( "%d PLAYERS", count[col] ), 13 * u, DIM, ALIGN_LEFT );
			Text( FONT_NUM, x0 + cw - 24 * u, y - 8 * u + 42 * u, va( "%d", col ? cgs.scores2 : cgs.scores1 ), 44 * u, INK_HOT, ALIGN_RIGHT );
			y += 64 * u;
		}
		Col( c, DIM, 0.8f );
		for ( k = 0; k < sub; k++ ) {
			const float x = x0 + k * ( sw + gap );
			Text( FONT_LABEL, x + 22 * u, y + 4 * u, "#", 13 * u, c, ALIGN_CENTER );
			Text( FONT_LABEL, x + 42 * u, y + 4 * u, "PLAYER", 13 * u, c, ALIGN_LEFT );
			Text( FONT_LABEL, x + sw * 0.71f, y + 4 * u, "SCORE", 13 * u, c, ALIGN_CENTER );
			Text( FONT_LABEL, x + sw * 0.83f, y + 4 * u, "K / D", 13 * u, c, ALIGN_CENTER );
			Text( FONT_LABEL, x + sw * 0.94f, y + 4 * u, "PING", 13 * u, c, ALIGN_CENTER );
		}
		for ( i = 0; i < numEntries; i++ ) {
			const int	client = entries[i].client;
			float		x, ry;
			if ( BoardColumn( client, teams ) != col ) {
				continue;
			}
			k = place / rowsPer;
			x = x0 + k * ( sw + gap );
			ry = top + ( place % rowsPer ) * pitch;
			if ( k < sub && ry + rh <= py + ph - 40 * u ) {
				boardRect_t *r = &boardMouse.rows[boardMouse.numRows++];
				BoardRow( x, ry, sw, rh, client, entries[i].s, place );
				r->x = x;
				r->y = ry;
				r->w = sw;
				r->h = rh;
				r->client = client;
				drawn++;
			}
			place++;
		}
	}
	// who's watching, under the players (Ozmium, 2026-10-31: the board left spectators out)
	if ( numSpecs && specY + specH <= py + ph - 40 * u ) {
		const float label = Text( FONT_LABEL, listX + 8 * u, specY + 26 * u, va( "SPECTATORS %d", numSpecs ), 13 * u, DIM, ALIGN_LEFT );
		CText( FONT_MEDIUM, listX + 8 * u + label + 22 * u, specY + 27 * u, specs, 18 * u, INK, ALIGN_LEFT,
			listW - label - 38 * u );
	}
	Col( c, DIM, 0.9f );
	Text( FONT_LABEL, px + pw * 0.5f, py + ph - 26 * u, boardMouse.on ? ( ps->pm_type == PM_INTERMISSION
		? "CLICK A PLAYER FOR THEIR CARD     RIGHT CLICK  BACK" : "CLICK A PLAYER FOR THEIR CARD     RIGHT CLICK  BACK TO PLAY" )
		: ps->pm_type == PM_INTERMISSION ? "FIRE  CONTINUE     RIGHT CLICK  PLAYER CARDS" : "RIGHT CLICK  PLAYER CARDS     RELEASE TAB TO PLAY",
		14 * u, c, ALIGN_CENTER );
	Col( c, RED, 0.5f );
	Rect( px, py + ph - 3 * u, pw, 3 * u, c );
	{
		char	layout[sizeof( boardMouse.layout )];
		int		scored = 0;
		for ( i = 0; i < numEntries; i++ ) {
			scored += entries[i].s && BoardColumn( entries[i].client, teams ) >= 0;
		}
		Com_sprintf( layout, sizeof( layout ), "%d players (%d scored), %d rows drawn, %d column%s of %d, rows %d units",
			count[0] + count[1], scored, drawn, cols * sub, cols * sub > 1 ? "s" : "", rowsPer, (int)( pitch / u + 0.5f ) );
		if ( strcmp( layout, boardMouse.layout ) ) {
			Q_strncpyz( boardMouse.layout, layout, sizeof( boardMouse.layout ) );
			Com_Printf( "ozhud board: layout %s\n", layout );
		}
	}
	if ( boardMouse.on ) {
		BoardCursor();
	}
}

// ================================================================ the results (results_screen.gd)

// XP for a match, given once at the intermission by the career (oz_progress.cpp: q3_profile.gd
// award_match, the campaign's own lines too), from the end of the match as this client sees it

static struct {
	int			awardedFor;				// the level start time it was given for
	ozAward_t	a;
} xp;

// ---------------------------------------------------------------- the record's names

// a frag's or a death's cause, by weapon, or what killed you without one (deaths_by's names for those)
static const char *CauseKey( int mod ) {
	const int w = ModWeapon( mod );
	if ( w > 0 && w < ARRAY_LEN( weaponKeys ) ) {
		return weaponKeys[w];
	}
	switch ( mod ) {
	case MOD_WATER: return "drown";
	case MOD_SLIME: return "slime";
	case MOD_LAVA: return "lava";
	case MOD_CRUSH: return "crush";
	case MOD_TELEFRAG: return "telefrag";
	case MOD_FALLING: return "fall";
	case MOD_SUICIDE: return "suicide";
	case MOD_TARGET_LASER: return "laser";
	case MOD_TRIGGER_HURT: return "hurt";
	default: return "other";
	}
}

// an item as the career's pickups.<item> names it (catalog.json), or NULL for one it doesn't count
static const char *PickupKey( int item ) {
	const ozHudItem_t *it;
	if ( item <= 0 || item >= (int)numHudItems ) {
		return NULL;
	}
	it = &hudItems[item];
	if ( it->type == IT_POWERUP ) {
		switch ( it->tag ) {
		case PW_QUAD: return "quad";
		case PW_BATTLESUIT: return "battlesuit";
		case PW_HASTE: return "haste";
		case PW_INVIS: return "invis";
		case PW_REGEN: return "regen";
		case PW_FLIGHT: return "flight";
		default: return NULL;
		}
	}
	if ( it->type == IT_HEALTH && it->quantity >= 100 ) {
		return "mega";
	}
	if ( it->type == IT_ARMOR && it->quantity >= 50 ) {
		return it->quantity >= 100 ? "armor_red" : "armor_yellow";
	}
	if ( it->type == IT_HOLDABLE ) {
		return it->tag == HI_MEDKIT ? "medkit" : it->tag == HI_TELEPORTER ? "teleporter" : NULL;
	}
	return NULL;
}

static void CountAdd( ozCounts_t *c, const char *key, int n ) {
	int i;
	if ( n <= 0 || !key ) {
		return;
	}
	for ( i = 0; i < c->num; i++ ) {
		if ( !strcmp( c->key[i], key ) ) {
			c->n[i] += n;
			return;
		}
	}
	if ( c->num < OZ_MATCH_COUNTS ) {
		Q_strncpyz( c->key[c->num], key, sizeof( c->key[0] ) );
		c->n[c->num++] = n;
	}
}

// the career's counts from the match (P8b step 5): frags by the nine weapons, pickups, deaths by the catalog's
// six causes, the rail's run and accuracy
static void CareerCounts( ozMatchEnd_t *m ) {
	int i;
	for ( i = 0; i < MOD_COUNT; i++ ) {
		const char	*k = CauseKey( i );
		const int	w = ModWeapon( i );
		if ( w >= WP_GAUNTLET && w <= WP_BFG ) {
			CountAdd( &m->fragsWeapon, k, hud.fragsMod[i] );
		}
		if ( !strcmp( k, "lava" ) || !strcmp( k, "slime" ) || !strcmp( k, "fall" ) || !strcmp( k, "drown" ) || !strcmp( k, "telefrag" )
			|| !strcmp( k, "crush" ) ) {
			CountAdd( &m->deathsBy, k, hud.deathsMod[i] );
		}
	}
	for ( i = 0; i < (int)ARRAY_LEN( hud.pickups ); i++ ) {
		CountAdd( &m->pickups, PickupKey( i ), hud.pickups[i] );
	}
	m->railStreak = hud.railBest;
	m->railShots = hud.railShots;
	m->railHits = hud.railHits;
}

static void AwardXP( const playerState_t *ps ) {
	ozMatchEnd_t	m;
	char			map[MAX_QPATH];
	int				i;

	if ( xp.awardedFor == cgs.levelStartTime + 1 || ps->persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
		return;
	}
	memset( &m, 0, sizeof( m ) );
	m.place = -1;
	for ( i = 0; i < cg.numScores && m.numScores < OZ_MATCH_SCORES; i++ ) {	// the standings, best first
		if ( cg.scores[i].client < 0 || cg.scores[i].client >= MAX_CLIENTS || cg.scores[i].team == TEAM_SPECTATOR ) {
			continue;
		}
		if ( cg.scores[i].client == ps->clientNum ) {
			m.place = m.numScores;
		}
		m.scores[m.numScores++] = cg.scores[i].score;
	}
	if ( m.place < 0 ) {
		return;		// the intermission's scores haven't come yet
	}
	Q_strncpyz( map, Info_ValueForKey( CS( CS_SERVERINFO ), "mapname" ), sizeof( map ) );
	m.map = map;
	m.gametype = cgs.gametype;
	m.team = ps->persistant[PERS_TEAM];
	m.teamScores[0] = cgs.scores1;
	m.teamScores[1] = cgs.scores2;
	m.impressive = ps->persistant[PERS_IMPRESSIVE_COUNT];
	m.excellent = ps->persistant[PERS_EXCELLENT_COUNT];
	m.gauntlet = ps->persistant[PERS_GAUNTLET_FRAG_COUNT];
	m.defend = ps->persistant[PERS_DEFEND_COUNT];
	m.assist = ps->persistant[PERS_ASSIST_COUNT];
	m.captures = ps->persistant[PERS_CAPTURES];
	m.killed = ps->persistant[PERS_KILLED];
	m.model = cgs.clientinfo[ps->clientNum].modelName;
	m.skin = cgs.clientinfo[ps->clientNum].skinName;
	m.seconds = ( cg.snap ? cg.snap->serverTime - cgs.levelStartTime : 0 ) / 1000;
	m.topSpeed = (int)hud.topSpeed;
	CareerCounts( &m );
	if ( OzProgress_MatchEnd( &m, cg.demoPlayback, &xp.a ) ) {
		xp.awardedFor = cgs.levelStartTime + 1;
		Com_Printf( "ozhud: top speed %d ups (%d with pushes)\n", m.topSpeed, (int)hud.pushedTop );
	}
}

/*
The match record (ENGINE_PLAN.md P8b step 5), once a match is over: the server, mode and map, the final
scoreboard, and your frags and deaths by weapon, shots, hits, pickups and medals, handed to oz_stats.c,
which keeps it until the hub has it. Any scoreboard parsed after the intermission began is the final one
(an answer to an older request comes in order, before it), so one is asked for then. Not for demos, nor
while spectating or following someone.
*/
static struct {
	int		recordedFor;		// the level start time it was recorded for, + 1
	int		intermissionAt;		// cls.realtime the intermission was first seen, 0 outside one
	int		scoresAt;			// cg.scoresParsed then
} record;

static void RecordMatch( const playerState_t *ps ) {
#ifdef USE_OZ_ONLINE
	static ozMatchRecord_t	r;
	char					map[MAX_QPATH];
	int						i, w;

	if ( ps->pm_type != PM_INTERMISSION ) {
		record.intermissionAt = 0;
		return;
	}
	if ( record.recordedFor == cgs.levelStartTime + 1 || cg.demoPlayback || !OwnPlay() ) {
		return;
	}
	if ( !record.intermissionAt ) {
		record.intermissionAt = cls.realtime ? cls.realtime : 1;
		record.scoresAt = cg.scoresParsed;
		CL_AddReliableCommand( "score", qfalse );
		return;
	}
	if ( cg.scoresParsed == record.scoresAt ) {
		return;
	}
	memset( &r, 0, sizeof( r ) );
	Q_strncpyz( map, Info_ValueForKey( CS( CS_SERVERINFO ), "mapname" ), sizeof( map ) );
	r.map = map;
	r.gametype = cgs.gametype;
	r.start = cgs.levelStartTime;
	r.seconds = ( cl.snap.serverTime - cgs.levelStartTime ) / 1000;
	r.me = ps->clientNum;
	r.place = -1;
	for ( i = 0; i < cg.numScores && r.numRows < OZ_RECORD_ROWS - 2; i++ ) {	// the standings, best first
		const ozScore_t	*s = &cg.scores[i];
		ozRecordRow_t	*row = &r.rows[r.numRows];
		if ( s->client < 0 || s->client >= MAX_CLIENTS || s->team == TEAM_SPECTATOR || !cgs.clientinfo[s->client].infoValid ) {
			continue;
		}
		row->client = s->client;
		row->score = s->score;
		row->team = s->team;
		row->bot = cgs.clientinfo[s->client].botSkill > 0;
		PlayerName( s->client, row->name, sizeof( row->name ) );
		if ( s->client == ps->clientNum ) {
			r.place = r.numRows;
			r.score = s->score;
		}
		r.numRows++;
	}
	if ( r.place < 0 ) {
		return;		// not on the board: nothing to record
	}
	if ( cgs.gametype >= GT_TEAM && cgs.scores1 != SCORE_NOT_PRESENT && cgs.scores2 != SCORE_NOT_PRESENT ) {
		// the team scores as rows of their own, so the players' reports agree on them too
		r.rows[r.numRows].client = -1;
		r.rows[r.numRows].score = cgs.scores1;
		r.rows[r.numRows++].team = TEAM_RED;
		r.rows[r.numRows].client = -2;
		r.rows[r.numRows].score = cgs.scores2;
		r.rows[r.numRows++].team = TEAM_BLUE;
		r.won = ps->persistant[PERS_TEAM] == TEAM_RED ? cgs.scores1 > cgs.scores2 : ps->persistant[PERS_TEAM] == TEAM_BLUE && cgs.scores2 > cgs.scores1;
	} else {
		r.won = r.place == 0 && r.numRows > 1 && r.rows[0].score > r.rows[1].score;
	}
	r.frags = hud.kills[ps->clientNum];
	r.deaths = hud.deaths[ps->clientNum];
	r.xp = xp.awardedFor == cgs.levelStartTime + 1 ? xp.a.gain : 0;
	r.railStreak = hud.railBest;
	r.distance = (int)hud.distance;
	r.speed = (int)hud.topSpeed;
	for ( i = 0; i < MOD_COUNT; i++ ) {
		CountAdd( &r.fragsWeapon, CauseKey( i ), hud.fragsMod[i] );
		CountAdd( &r.deathsWeapon, CauseKey( i ), hud.deathsMod[i] );
	}
	for ( w = WP_GAUNTLET; w < NAMES && w < (int)ARRAY_LEN( weaponKeys ); w++ ) {
		CountAdd( &r.shots, weaponKeys[w], hud.shots[w] );
		// hits: Oz's game's dmg command (any damage, a corpse's too), other games' PERS_HITS; the rail's, any game's, by PERS_HITS: live enemies
		// only, as id's accuracy counts them, and as the career's rail accuracy takes them
		CountAdd( &r.hits, weaponKeys[w], w == WP_RAILGUN ? hud.railHits : hud.statHits[w] );
		CountAdd( &r.damage, weaponKeys[w], hud.damage[w] );
	}
	for ( i = 0; i < (int)ARRAY_LEN( hud.pickups ); i++ ) {
		CountAdd( &r.pickups, PickupKey( i ), hud.pickups[i] );
	}
	CountAdd( &r.medals, "impressive", ps->persistant[PERS_IMPRESSIVE_COUNT] );
	CountAdd( &r.medals, "excellent", ps->persistant[PERS_EXCELLENT_COUNT] );
	CountAdd( &r.medals, "gauntlet", ps->persistant[PERS_GAUNTLET_FRAG_COUNT] );
	CountAdd( &r.medals, "defend", ps->persistant[PERS_DEFEND_COUNT] );
	CountAdd( &r.medals, "assist", ps->persistant[PERS_ASSIST_COUNT] );
	CountAdd( &r.medals, "captures", ps->persistant[PERS_CAPTURES] );
	OzStats_Record( &r );
	record.recordedFor = cgs.levelStartTime + 1;
#else
	(void)ps;
#endif
}

// the XP lines one after another, then the bar filling, with a flash for every level crossed
static void DrawXP( float W, float H, float t ) {
	const float	x = W * 0.715f, w = W * 0.26f, barY = H * 0.78f, barH = H * 0.025f;
	const float	fill = Clamp01( ( t - 1.6f - xp.a.numLines * 0.25f ) / 1.5f );
	const int	now = xp.a.before + (int)( xp.a.gain * fill );
	int			into, next, level, i;
	float		y = H * 0.42f, c[4], flash;
	static const vec4_t back = { 1, 1, 1, 0.08f };
	static const vec4_t bar = { 1.0f, 0.7f, 0.15f, 1 };
	static const vec4_t gain = { 1, 0.85f, 0.4f, 1 };

	if ( xp.awardedFor != cgs.levelStartTime + 1 ) {
		return;		// a campaign match waits a moment for the game's postgame
	}
	for ( i = 0; i < xp.a.numLines; i++ ) {
		const float a = Clamp01( ( t - 1.4f - i * 0.25f ) / 0.25f );
		Col( c, WHITE, 0.85f * a );
		Text( FONT_MEDIUM, x, y, xp.a.lines[i], H * 0.024f, c, ALIGN_LEFT );
		Col( c, gain, a );
		Text( FONT_NUM, x + w, y, va( "+%d XP", xp.a.amounts[i] ), H * 0.024f, c, ALIGN_RIGHT );
		y += H * 0.034f;
	}
	level = OzProgress_LevelFor( now, &into, &next );
	flash = level > OzProgress_LevelFor( xp.a.before, NULL, NULL ) && !oz_accessibility_reduced_flashing->integer
		? Clamp01( 1.0f - ( t - 1.6f - xp.a.numLines * 0.25f - 1.5f ) / 0.8f ) : 0;
	Mix( c, WHITE, bar, flash );
	Text( FONT_LABEL, x, barY - H * 0.02f, va( "LEVEL %d", level ), H * 0.05f, c, ALIGN_LEFT );
	Rect( x, barY, w, barH, back );
	Rect( x, barY, w * Clamp01( (float)into / next ), barH, bar );
	if ( flash > 0 ) {
		Col( c, WHITE, 0.6f * flash );
		Rect( x, barY, w, barH, c );
	}
	Col( c, WHITE, 0.75f );
	Text( FONT_MEDIUM, x, barY + barH + H * 0.03f, va( "%d / %d XP", into, next ), H * 0.022f, c, ALIGN_LEFT );
	// the card parts the match unlocked
	for ( i = 0; i < xp.a.numUnlocked; i++ ) {
		const float a = Clamp01( ( t - 3.4f - xp.a.numLines * 0.25f - i * 0.3f ) / 0.3f );
		Col( c, gain, a );
		Text( FONT_MEDIUM, x, barY + barH + H * ( 0.07f + 0.03f * i ), va( "UNLOCKED  %s", xp.a.unlocked[i] ), H * 0.02f, c, ALIGN_LEFT );
	}
}

// the end of the match: VICTORY or DEFEAT, the standings as cards sliding in, the medals you earned
// and the XP. No podium of its own: the game puts the top three on id's podium in the map (g_arenas.c
// SpawnModelsOnVictoryPads), which the Godot build had to recreate
static void DrawResults( float W, float H, const playerState_t *ps ) {
	static const vec4_t gold = { 1.0f, 0.82f, 0.3f, 1 };
	static const vec4_t loss = { 0.85f, 0.12f, 0.08f, 1 };
	static const vec4_t shade = { 0.02f, 0.015f, 0.015f, 0.7f };
	static const vec4_t clear = { 0.02f, 0.015f, 0.015f, 0 };
	static const vec4_t medalText = { 1, 0.85f, 0.4f, 1 };
	const float	t = resultsStart ? ( cg.time - resultsStart ) / 1000.0f : 0;
	const qboolean spectator = ps->persistant[PERS_TEAM] == TEAM_SPECTATOR;
	qboolean	won;
	float		a[2], b[2], cc[2], d[2], c[4], y, pop;
	const char	*title, *sub;
	int			i, n = 0, mx;

	if ( !resultsStart ) {
		resultsStart = cg.time;
	}
	if ( cgs.gametype >= GT_TEAM ) {
		const int mine = ps->persistant[PERS_TEAM] == TEAM_BLUE ? cgs.scores2 : cgs.scores1;
		const int theirs = ps->persistant[PERS_TEAM] == TEAM_BLUE ? cgs.scores1 : cgs.scores2;
		won = mine > theirs;
		sub = cgs.scores1 == cgs.scores2 ? va( "Teams tied at %d", cgs.scores1 ) : cgs.scores1 > cgs.scores2 ?
			va( "Red wins %d to %d", cgs.scores1, cgs.scores2 ) : va( "Blue wins %d to %d", cgs.scores2, cgs.scores1 );
	} else {
		// ui_sppostgame.c: first place wins, ties included
		won = ( ps->persistant[PERS_RANK] & ~RANK_TIED_FLAG ) == 0;
		char place[64];
		Q_strncpyz( place, Place( ps->persistant[PERS_RANK] ), sizeof( place ) );
		Q_strlwr( place + 1 );
		sub = va( "%s place with %d", place, ps->persistant[PERS_SCORE] );
	}
	title = spectator ? "MATCH OVER" : won ? "VICTORY" : "DEFEAT";
	// the map shows through, darker at the sides behind the standings and the medals
	Col( c, shade, Clamp01( t / 0.6f ) );
	a[0] = 0; a[1] = 0; b[0] = W * 0.36f; b[1] = 0; cc[0] = W * 0.36f; cc[1] = H; d[0] = 0; d[1] = H;
	Quad4( a, b, cc, d, c, clear, clear, c );
	a[0] = W * 0.64f; b[0] = W; cc[0] = W; d[0] = W * 0.64f;
	Quad4( a, b, cc, d, clear, c, c, clear );
	// the title pops in
	pop = 1.0f + 0.4f * ( 1.0f - Clamp01( t / 0.45f ) );
	Col( c, spectator ? INK_HOT : won ? gold : loss, Clamp01( t / 0.3f ) );
	TextScaled( FONT_LABEL, W * 0.5f, H * 0.04f + H * 0.1f, title, H * 0.11f, pop, c, ALIGN_CENTER | TEXT_NOCODES, 0.35f, 0 );
	if ( !spectator ) {
		Col( c, WHITE, 0.75f * Clamp01( t / 0.3f ) );
		Text( FONT_MEDIUM, W * 0.5f, H * 0.2f, sub, H * 0.028f, c, ALIGN_CENTER );
	}
	// the standings
	y = H * 0.25f;
	for ( i = 0; i < cg.numScores && y < H * 0.85f; i++ ) {
		const ozScore_t	*s = &cg.scores[i];
		const float		ra = Clamp01( ( t - 0.35f - n * 0.12f ) / 0.25f );
		const float		ch = H * 0.075f, cw = W * 0.19f;
		if ( s->client < 0 || s->client >= MAX_CLIENTS || s->team == TEAM_SPECTATOR ) {
			continue;
		}
		Col( c, WHITE, 0.8f * ra );
		Text( FONT_NUM, W * 0.03f, y + ch * 0.5f + H * 0.014f, va( "%d", n + 1 ), H * 0.04f, c, ALIGN_LEFT );
		Card( W * 0.03f + H * 0.05f + 14, y, cw, ch, s->client, s->client == ps->clientNum, ra );
		Col( c, WHITE, ra );
		Text( FONT_NUM, W * 0.03f + H * 0.05f + 28 + cw + H * 0.07f, y + ch * 0.5f + H * 0.016f, va( "%d", s->score ), H * 0.045f, c, ALIGN_RIGHT );
		y += ch + H * 0.01f;
		n++;
	}
	// the medals you earned
	if ( !spectator ) {
		mx = 0;
		for ( i = 0; i < 6; i++ ) {
			const int count = ps->persistant[medalPers[i]];
			const float k = Clamp01( ( t - 0.9f - mx * 0.15f ) / 0.3f );
			const float sz = H * 0.08f * k;
			const float x = W * 0.715f + mx * ( H * 0.08f + 18 );
			if ( count <= 0 ) {
				continue;
			}
			Image( img.medals[i], x + ( H * 0.08f - sz ) * 0.5f, H * 0.25f + ( H * 0.08f - sz ) * 0.5f, sz, sz, WHITE );
			Col( c, medalText, k );
			Text( FONT_MEDIUM, x + H * 0.04f, H * 0.25f + H * 0.08f + H * 0.025f, va( "%c%s x%d", medalNames[i][0],
				Q_strlwr( va( "%s", medalNames[i] + 1 ) ), count ), H * 0.02f, c, ALIGN_CENTER );
			mx++;
		}
		DrawXP( W, H, t );
	}
	Col( c, WHITE, 0.5f );
	if ( xp.a.campaign && xp.awardedFor == cgs.levelStartTime + 1 ) {
		// the single player game never leaves its intermission: back to the campaign once the XP has shown
		const float left = CAMPAIGN_RESULTS - t;
		Text( FONT_LABEL, W * 0.5f, H * 0.94f, va( "TAB  SCOREBOARD     BACK TO THE CAMPAIGN IN %d", (int)( left > 0 ? left + 0.99f : 0 ) ), H * 0.022f, c, ALIGN_CENTER );
		if ( left <= 0 && !cg.demoPlayback ) {
			xp.a.campaign = 0;
			OzProgress_CampaignContinue();
		}
		return;
	}
	Text( FONT_LABEL, W * 0.5f, H * 0.94f, "TAB  SCOREBOARD", H * 0.022f, c, ALIGN_CENTER );
}

// +scores / -scores, which the game code takes: the board shows while it's held, and letting go takes
// the board's mouse back
void OzHud_ScoresKey( int down ) {
	cg.showScores = down ? qtrue : qfalse;
	if ( OzHud_KeepsKeys() ) {	// keys the game code took before
		Key_SetCatcher( Key_GetCatcher() & ~KEYCATCH_CGAME );
	}
	if ( !down ) {
		BoardMouseOff();
	}
}

// an emoji's code put in the chat line at the cursor, ":name:"
static void ChatInsertEmoji( char *line, int size, int *cursor, int codepoint ) {
	const char	*name = OzEmoji_Code( codepoint );
	const int	len = strlen( line );
	char		code[OZ_EMOJI_MAX_NAME + 3];
	int			n;

	if ( !name ) {
		return;
	}
	Com_sprintf( code, sizeof( code ), ":%s:", name );
	n = strlen( code );
	if ( len + n > size - 2 ) {	// as Field_CharEvent, room for a leading slash
		return;
	}
	memmove( line + *cursor + n, line + *cursor, len + 1 - *cursor );
	memcpy( line + *cursor, code, n );
	*cursor += n;
}

// a character for the chat line (cl_keys.c): an emoji goes in as its code, and the pieces of emoji
// sequences (skin tones, joiners, flag letters) are dropped; Backspace takes a whole code or smiley
// while they show as emoji. 0 leaves the character to the line.
int OzHud_ChatChar( char *line, int size, int *cursor, int ch ) {
	const int len = strlen( line );

	if ( *cursor < 0 || *cursor > len ) {
		return 0;
	}
	if ( ch == 'h' - 'a' + 1 ) {
		const int n = ChatEmoji() ? OzEmoji_Before( line, *cursor ) : 0;
		if ( n < 2 ) {
			return 0;
		}
		memmove( line + *cursor - n, line + *cursor, len + 1 - *cursor );
		*cursor -= n;
		return 1;
	}
	if ( ch < 0x80 || !( OzEmoji_Part( ch ) || OzEmoji_Code( ch ) ) ) {
		return 0;
	}
	ChatInsertEmoji( line, size, cursor, ch );
	return 1;
}

// a paste into the chat line (SDL's clipboard is UTF-8): emoji go in as their codes, other characters
// past ASCII are dropped (the network would make them dots)
void OzHud_ChatPaste( char *line, int size, int *cursor, const char *text ) {
	while ( *text ) {
		int c;
		text += OzEmoji_Decode( text, &c );
		if ( c >= 0x80 ) {
			OzHud_ChatChar( line, size, cursor, c );
		} else if ( c >= ' ' ) {
			const int len = strlen( line );
			if ( *cursor < 0 || *cursor > len || len > size - 3 ) {
				return;
			}
			memmove( line + *cursor + 1, line + *cursor, len + 1 - *cursor );
			line[( *cursor )++] = (char)c;
		}
	}
}

// Tab in the chat line (hud_chat.gd _complete): the word before the cursor becomes the first player
// whose name, without its color codes, starts with it; a code being typed (":fi") becomes the first
// emoji the HUD suggests for it
void OzHud_CompleteName( char *line, int size, int *cursor ) {
	char	word[MAX_QPATH], name[MAX_QPATH], rest[MAX_EDIT_LINE], out[MAX_EDIT_LINE];
	int		start, len, i;

	if ( *cursor < 0 || *cursor > (int)strlen( line ) ) {
		return;
	}
	start = ChatEmoji() ? OzEmoji_Typing( line, *cursor ) : -1;
	if ( start >= 0 ) {
		int codepoint;
		const char *found;
		if ( OzEmoji_Suggest( line + start, *cursor - start, &codepoint, &found, 1 ) ) {
			// the name typed so far, and its colon, give way to the emoji's code
			memmove( line + start - 1, line + *cursor, strlen( line + *cursor ) + 1 );
			*cursor = start - 1;
			ChatInsertEmoji( line, size, cursor, codepoint );
		}
		return;
	}
	start = *cursor;
	while ( start > 0 && line[start - 1] != ' ' ) {
		start--;
	}
	len = *cursor - start;
	if ( len <= 0 || len >= (int)sizeof( word ) ) {
		return;
	}
	memcpy( word, line + start, len );
	word[len] = 0;
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const char *info = CS( CS_PLAYERS + i );
		if ( !info[0] ) {
			continue;
		}
		Q_strncpyz( name, Info_ValueForKey( info, "n" ), sizeof( name ) );
		Q_CleanStr( name );
		if ( Q_stricmpn( name, word, len ) ) {
			continue;
		}
		Q_strncpyz( rest, line + *cursor, sizeof( rest ) );
		memcpy( out, line, start );
		out[start] = 0;
		Q_strcat( out, sizeof( out ), name );
		*cursor = strlen( out );
		Q_strcat( out, sizeof( out ), rest );
		Q_strncpyz( line, out, size );
		if ( *cursor > (int)strlen( line ) ) {
			*cursor = strlen( line );
		}
		return;
	}
}

// oz_type <text> (tests): the text as the keyboard would type it (UTF-8), to whatever takes characters;
// \t is Tab, \b Backspace, \n Enter
static void Type_f( void ) {
	const char *s = Cmd_Argv( 1 );

	while ( *s ) {
		int c;
		if ( s[0] == '\\' && ( s[1] == 't' || s[1] == 'n' ) ) {
			const int key = s[1] == 't' ? K_TAB : K_ENTER;
			CL_KeyEvent( key, qtrue, Sys_Milliseconds() );
			CL_KeyEvent( key, qfalse, Sys_Milliseconds() );
			s += 2;
			continue;
		}
		if ( s[0] == '\\' && s[1] == 'b' ) {
			CL_CharEvent( 'h' - 'a' + 1 );
			s += 2;
			continue;
		}
		s += OzEmoji_Decode( s, &c );
		CL_CharEvent( c );
	}
}

// oz_emoji "<text>": the text as the chat shows it, and the emoji a code being typed at its end may be
static void Emoji_f( void ) {
	const char	*text = Cmd_Argv( 1 );
	char		shown[CHAT_TEXT];
	int			suggested[MAX_SUGGESTIONS], n = 0, i;
	const char	*names[MAX_SUGGESTIONS];
	const int	start = OzEmoji_Typing( text, strlen( text ) );

	OzEmoji_Expand( text, shown, sizeof( shown ), -1, NULL );
	Com_Printf( "oz_emoji: \"%s\"\n", shown );
	if ( start >= 0 ) {
		n = OzEmoji_Suggest( text + start, strlen( text ) - start, suggested, names, MAX_SUGGESTIONS );
	}
	for ( i = 0; i < n; i++ ) {
		char u8[5];
		u8[OzEmoji_Utf8( suggested[i], u8 )] = 0;
		Com_Printf( "oz_emoji: suggests %s :%s: (sent as :%s:)\n", u8, names[i], OzEmoji_Code( suggested[i] ) );
	}
}

