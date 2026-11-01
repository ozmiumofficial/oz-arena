/*
===========================================================================
Oz Arena: the competitive options that change the 3D view (ENGINE_PLAN.md P2, P6)

Like the HUD (oz_hud.c), these work under every cgame, from the entities it adds to the scene:

- Gameplay > Gun position, Gun side offset, Gun field of view and Widescreen gun placement: the gun's
  parts (RF_FIRST_PERSON) are kept until the scene renders, then placed against that scene's own view:
  moved sideways, mirrored for the left (a reflection across the view's forward-up plane, which the oz
  renderer draws with its culling flipped), or put in the middle. Gun field of view itself is the
  renderer's (tr_backend.c gives the gun its own projection).
- Gameplay > Force enemy model: an enemy's lower, upper and head models are swapped for the chosen
  character's, each entity on its own (the cgame shares model handles between players), with its frames
  moved to the same animation in the new model's animation.cfg. The torso, the head, the gun and the
  flag are hung again on the new model's tags, so a rig's parts still meet (tr_oz_rig.c).
- The ragdolls' push: a body's speed in the snapshot, which holds the killing hit's knockback
  (OzHud_BodyVelocity, a refimport for renderer2/tr_oz_ragdoll.c).

View bob and Damage view kick are Oz's own cgame's (game/patches/0021): the engine can't see the bob,
which the cgame works out from its own prediction.
===========================================================================
*/
#include "../../../quake3e/code/client/client.h"
#include "../../../quake3e/code/game/bg_public.h"
#include "oz_hud.h"
#include "oz_ui_engine.h"

#define MAX_GUN_PARTS		16
#define MAX_GUN_QUADS		64		// a glowing lightning beam's layers: up to 7 pieces each (cg_oz_fx.c BEAM_PIECES + 3)
#define MAX_GUN_LINES		4
#define MAX_NOTED_MODELS	1024
#define MAX_ANIM_CFGS		64
#define NUM_ANIMS			( LEGS_TURN + 1 )	// the lines of a baseq3 animation.cfg

enum { PART_NONE, PART_LOWER, PART_UPPER, PART_HEAD };

static cvar_t	*oz_gameplay_draw_gun, *oz_gameplay_gun_offset, *oz_gameplay_gun_fov, *oz_gameplay_gun_widescreen;
static cvar_t	*oz_gameplay_force_model, *cg_fov;

static void InitCvars( void ) {
	if ( oz_gameplay_draw_gun ) {
		return;
	}
	oz_gameplay_draw_gun = Cvar_Get( "oz_gameplay_draw_gun", "right", CVAR_ARCHIVE );
	oz_gameplay_gun_offset = Cvar_Get( "oz_gameplay_gun_offset", "0", CVAR_ARCHIVE );
	oz_gameplay_gun_fov = Cvar_Get( "oz_gameplay_gun_fov", "0", CVAR_ARCHIVE );
	oz_gameplay_gun_widescreen = Cvar_Get( "oz_gameplay_gun_widescreen", "1", CVAR_ARCHIVE );
	oz_gameplay_force_model = Cvar_Get( "oz_gameplay_force_model", "off", CVAR_ARCHIVE );
	Cvar_Get( "oz_gameplay_view_bob", "1", CVAR_ARCHIVE );		// Oz's cgame reads these two
	Cvar_Get( "oz_gameplay_damage_kick", "0", CVAR_ARCHIVE );
	cg_fov = Cvar_Get( "cg_fov", "90", CVAR_ARCHIVE );
}

// ================================================================ the gun

static refEntity_t	gunParts[MAX_GUN_PARTS];
static qboolean		gunBeam[MAX_GUN_PARTS];		// a lightning beam from the gun's flash: only its start moves
static int			numGunParts;
static polyVert_t	gunQuads[MAX_GUN_QUADS][4];	// quads added after the gun (PlaceLayers)
static qhandle_t	gunQuadShaders[MAX_GUN_QUADS];
static int			numGunQuads;

int OzView_Gun( const refEntity_t *ent ) {
	InitCvars();
	if ( Q_stricmp( oz_gameplay_draw_gun->string, "hidden" ) && numGunParts < MAX_GUN_PARTS ) {
		gunBeam[numGunParts] = qfalse;
		gunParts[numGunParts++] = *ent;
	}
	return 0;
}

// the quads the cgame adds once the gun is in: a glowing beam's layers among them (0 keeps them for PlaceLayers)
int OzView_Polys( qhandle_t shader, int numVerts, const polyVert_t *verts, int numPolys ) {
	int i;

	if ( !numGunParts || numVerts != 4 || numPolys < 1 || numGunQuads + numPolys > MAX_GUN_QUADS ) {
		return 1;
	}
	for ( i = 0; i < numPolys; i++, numGunQuads++ ) {
		gunQuadShaders[numGunQuads] = shader;
		Com_Memcpy( gunQuads[numGunQuads], verts + 4 * i, sizeof( gunQuads[0] ) );
	}
	return 0;
}

// a point across the plane through o with normal n
static void ReflectPoint( vec3_t p, const vec3_t o, const vec3_t n ) {
	vec3_t d;

	VectorSubtract( p, o, d );
	VectorMA( p, -2.0f * DotProduct( d, n ), n, p );
}

/*
=================
GunScale

With Gun field of view the oz renderer draws the gun in a projection of its own (tr_backend.c
RB_OzGunProjection) and the world, a beam from the gun's flash included, in the view's: a point of the
gun shows where the world's point at the same depth, its offsets across the view times this, shows.
1 when the gun shares the view's projection.
=================
*/
static float GunScale( const refdef_t *fd ) {
	float ty;

	if ( oz_gameplay_gun_fov->value <= 0.0f || fd->fov_y <= 0.0f || Q_stricmp( Cvar_VariableString( "cl_renderer" ), "oz" ) ) {
		return 1.0f;
	}
	ty = tan( DEG2RAD( MIN( oz_gameplay_gun_fov->value, 170.0f ) ) * 0.5f ) * 0.75f;	// as RB_OzGunProjection
	return ty > 0.0f ? tan( DEG2RAD( fd->fov_y ) * 0.5f ) / ty : 1.0f;
}

// the world's point that shows where a point of the gun shows (GunScale)
static void OnScreen( const refdef_t *fd, float scale, const vec3_t p, vec3_t out ) {
	vec3_t d;
	int i;

	VectorSubtract( p, fd->vieworg, d );
	VectorMA( fd->vieworg, DotProduct( d, fd->viewaxis[0] ), fd->viewaxis[0], out );
	for ( i = 1; i < 3; i++ ) {
		VectorMA( out, DotProduct( d, fd->viewaxis[i] ) * scale, fd->viewaxis[i], out );
	}
}

/*
=================
PlaceLayers

A glowing lightning beam's layers (Oz's cgame, cg_oz_fx.c OzBeamStrip) come after the gun as quads: pieces
of a strip from the gun's flash to where the beam stops, turned to the view (v0 and v1 across its near end,
v3 and v2 across its far end, the same half-width across both). As with the bolt, the beam's start moves
with the gun and its end stays: each piece is laid again along the moved line, turned to the view from
there. Left where the cgame put them, the halo and the core ran from the gun's old place, a second beam
beside the bolt. `from` holds the parts' places before they moved, `to` where a beam from each starts now
(where it shows, with the gun in a projection of its own: GunScale).
=================
*/
static void PlaceLayers( const refdef_t *fd, const vec3_t *from, const vec3_t *to ) {
	struct { int part; vec3_t dir; float len; } lines[MAX_GUN_LINES];
	int		line[MAX_GUN_QUADS], numLines = 0, i, j, k;
	float	at[MAX_GUN_QUADS][2], half[MAX_GUN_QUADS], s, len, w, scale;
	vec3_t	m0, m1, r, r1, d, dir, view, side, end, start;

	for ( i = 0; i < numGunQuads; i++ ) {
		const polyVert_t *v = gunQuads[i];

		line[i] = -1;
		VectorAdd( v[0].xyz, v[1].xyz, m0 ); VectorScale( m0, 0.5f, m0 );
		VectorAdd( v[3].xyz, v[2].xyz, m1 ); VectorScale( m1, 0.5f, m1 );
		VectorSubtract( v[0].xyz, v[1].xyz, r ); VectorScale( r, 0.5f, r );
		VectorSubtract( v[3].xyz, v[2].xyz, r1 ); VectorScale( r1, 0.5f, r1 );
		w = VectorLength( r );
		if ( w < 0.01f || Distance( r, r1 ) > 0.05f ) {
			continue;
		}
		for ( j = 0; j < numGunParts && line[i] < 0; j++ ) {
			if ( gunBeam[j] || VectorCompare( from[j], to[j] ) ) {
				continue;		// the bolt, or a part a beam leaves from where the cgame put it
			}
			VectorSubtract( m1, from[j], d );
			len = VectorNormalize2( d, dir );
			VectorSubtract( m0, from[j], d );
			s = DotProduct( d, dir );
			VectorMA( d, -s, dir, d );
			VectorSubtract( from[j], fd->vieworg, view );
			if ( len < 1.0f || s < -0.1f || s > len || VectorLength( d ) > 0.1f + len * 0.0001f ||
				 VectorNormalize( view ) == 0.0f || fabs( DotProduct( r, dir ) ) > 0.02f * w + 0.05f ||
				 fabs( DotProduct( r, view ) ) > 0.02f * w + 0.05f ) {
				continue;
			}
			for ( k = 0; k < numLines && !( lines[k].part == j && DotProduct( lines[k].dir, dir ) > 0.9999f ); k++ ) {
			}
			if ( k == numLines ) {
				if ( numLines == MAX_GUN_LINES ) {
					break;
				}
				lines[k].part = j;
				VectorCopy( dir, lines[k].dir );
				lines[k].len = 0.0f;
				numLines++;
			}
			lines[k].len = MAX( lines[k].len, len );
			line[i] = k;
			at[i][0] = s;
			at[i][1] = len;
			CrossProduct( dir, view, side );
			VectorNormalize( side );
			half[i] = DotProduct( r, side );	// the cgame's side is cross( dir, the start's offset from the eye )
		}
	}

	for ( i = 0; i < numGunQuads; i++ ) {
		polyVert_t *v = gunQuads[i];

		if ( ( k = line[i] ) >= 0 ) {
			VectorCopy( to[lines[k].part], start );
			VectorMA( from[lines[k].part], lines[k].len, lines[k].dir, end );
			VectorSubtract( end, start, d );
			len = VectorNormalize2( d, dir );
			VectorSubtract( start, fd->vieworg, view );
			CrossProduct( dir, view, side );
			if ( len > 0.0f && VectorNormalize( side ) > 0.0f ) {
				scale = len / lines[k].len;
				VectorMA( start, at[i][0] * scale, dir, m0 );
				VectorMA( start, at[i][1] * scale, dir, m1 );
				VectorScale( side, half[i], r );
				VectorAdd( m0, r, v[0].xyz );
				VectorSubtract( m0, r, v[1].xyz );
				VectorSubtract( m1, r, v[2].xyz );
				VectorAdd( m1, r, v[3].xyz );
			}
		}
		re.AddPolyToScene( gunQuadShaders[i], 4, v, 1 );
	}
	numGunQuads = 0;
}

/*
=================
PlaceGun

The gun's parts against the view they're drawn in (the cgame placed them against the same one). Sideways,
the gun's first part (the gun itself, on the hand's tag_weapon) sets where it is: the right keeps that,
pushed out toward the edge by the offset; the left mirrors it; the center moves it to the middle. With a
field of view of its own the gun is the same size on every screen, so on a wide one it would sit nearer
the middle than on 4:3: Widescreen gun placement moves it out by the screen's width over 4:3's, and the
drop baseq3's cgame gives the gun at a wide cg_fov (CG_AddViewWeapon's fovOffset) is taken back.
=================
*/
static void PlaceGun( const refdef_t *fd ) {
	const char		*side = oz_gameplay_draw_gun->string;
	const qboolean	left = !Q_stricmp( side, "left" ), center = !Q_stricmp( side, "center" );
	const qboolean	ownFov = oz_gameplay_gun_fov->value > 0.0f;
	const float		wide = ownFov && oz_gameplay_gun_widescreen->integer && fd->height > 0 ? fd->width * 0.75f / fd->height : 1.0f;
	const float		*o = fd->vieworg, *lateral = fd->viewaxis[1];	// it points left
	const float		scale = GunScale( fd );
	float			y0, target, shift, lift = 0.0f;
	vec3_t			d, from[MAX_GUN_PARTS], to[MAX_GUN_PARTS];
	int				i, j;

	VectorSubtract( gunParts[0].origin, o, d );
	y0 = DotProduct( d, lateral );
	target = center ? 0.0f : ( y0 * wide - oz_gameplay_gun_offset->value ) * ( left ? -1.0f : 1.0f );
	shift = target - ( left ? -y0 : y0 );
	if ( ownFov && cg_fov->value > 90.0f && !Q_stricmp( FS_GetCurrentGameDir(), "oz" ) ) {
		lift = 0.2f * ( cg_fov->integer - 90 );
	}

	for ( i = 0; i < numGunParts; i++ ) {
		refEntity_t *e = &gunParts[i];

		VectorCopy( e->origin, from[i] );
		if ( left ) {
			ReflectPoint( e->origin, o, lateral );
			if ( !gunBeam[i] ) {
				ReflectPoint( e->oldorigin, o, lateral );
				if ( e->renderfx & RF_LIGHTING_ORIGIN ) {
					ReflectPoint( e->lightingOrigin, o, lateral );
				}
				for ( j = 0; j < 3; j++ ) {
					VectorMA( e->axis[j], -2.0f * DotProduct( e->axis[j], lateral ), lateral, e->axis[j] );
				}
			}
		}
		VectorMA( e->origin, shift, lateral, e->origin );
		VectorMA( e->origin, lift, fd->viewaxis[2], e->origin );
		OnScreen( fd, scale, e->origin, to[i] );	// a beam from the part starts where it shows
		if ( gunBeam[i] ) {
			VectorCopy( to[i], e->origin );		// the bolt: from where the flash shows
		} else {
			VectorMA( e->oldorigin, shift, lateral, e->oldorigin );
			VectorMA( e->oldorigin, lift, fd->viewaxis[2], e->oldorigin );
			if ( e->renderfx & RF_LIGHTING_ORIGIN ) {
				VectorMA( e->lightingOrigin, shift, lateral, e->lightingOrigin );
			}
		}
		re.AddRefEntityToScene( e, qfalse );
	}
	PlaceLayers( fd, from, to );
}

// ================================================================ the forced enemy model

typedef struct {
	char		folder[MAX_QPATH];
	qboolean	valid;
	int			first[NUM_ANIMS], num[NUM_ANIMS];
} ozAnimCfg_t;

static ozAnimCfg_t	animCfgs[MAX_ANIM_CFGS];
static int			numAnimCfgs;
static byte			notedPart[MAX_NOTED_MODELS], notedCfg[MAX_NOTED_MODELS];
static byte			notedWeapon[MAX_NOTED_MODELS];		// a gun's model (models/weapons*)

// a model folder's animation.cfg as CG_ParseAnimationFile reads it: legs-only frames don't count the
// torso-only ones (lower.md3 hasn't got them)
static int AnimCfg( const char *folder ) {
	ozAnimCfg_t	*c;
	char		*buf;
	const char	*text, *prev, *token;
	int			i, skip = 0;

	for ( i = 0; i < numAnimCfgs; i++ ) {
		if ( !Q_stricmp( animCfgs[i].folder, folder ) ) {
			return i;
		}
	}
	if ( numAnimCfgs == MAX_ANIM_CFGS ) {
		return -1;
	}
	c = &animCfgs[numAnimCfgs];
	Com_Memset( c, 0, sizeof( *c ) );
	Q_strncpyz( c->folder, folder, sizeof( c->folder ) );
	if ( FS_ReadFile( va( "models/players/%s/animation.cfg", folder ), (void **)&buf ) > 0 ) {
		text = buf;
		while ( 1 ) {
			prev = text;
			token = COM_Parse( &text );
			if ( !token[0] ) {
				break;
			}
			if ( !Q_stricmp( token, "footsteps" ) || !Q_stricmp( token, "sex" ) ) {
				COM_Parse( &text );
			} else if ( !Q_stricmp( token, "headoffset" ) ) {
				COM_Parse( &text );
				COM_Parse( &text );
				COM_Parse( &text );
			} else if ( token[0] >= '0' && token[0] <= '9' ) {
				text = prev;
				break;
			}
		}
		for ( i = 0; i < NUM_ANIMS; i++ ) {
			token = COM_Parse( &text );
			if ( !token[0] ) {
				break;
			}
			c->first[i] = atoi( token );
			if ( i == LEGS_WALKCR ) {
				skip = c->first[LEGS_WALKCR] - c->first[TORSO_GESTURE];
			}
			if ( i >= LEGS_WALKCR ) {
				c->first[i] -= skip;
			}
			c->num[i] = atoi( COM_Parse( &text ) );
			COM_Parse( &text );		// loop frames
			COM_Parse( &text );		// fps
		}
		c->valid = i == NUM_ANIMS;
		FS_FreeFile( buf );
	}
	return numAnimCfgs++;
}

void OzView_NoteModel( int model, const char *name ) {
	char		folder[MAX_QPATH];
	const char	*slash;
	int			part = PART_NONE, cfg;

	if ( model <= 0 || model >= MAX_NOTED_MODELS ) {
		return;
	}
	notedPart[model] = PART_NONE;
	notedWeapon[model] = name && !Q_stricmpn( name, "models/weapons", 14 );
	if ( !name || Q_stricmpn( name, "models/players/", 15 ) ) {
		return;
	}
	slash = strchr( name + 15, '/' );
	if ( !slash || strchr( slash + 1, '/' ) || slash - ( name + 15 ) >= (int)sizeof( folder ) ) {
		return;
	}
	part = !Q_stricmp( slash + 1, "lower.md3" ) ? PART_LOWER : !Q_stricmp( slash + 1, "upper.md3" ) ? PART_UPPER :
		   !Q_stricmp( slash + 1, "head.md3" ) ? PART_HEAD : PART_NONE;
	if ( part == PART_NONE ) {
		return;
	}
	Q_strncpyz( folder, name + 15, slash - ( name + 15 ) + 1 );
	cfg = AnimCfg( folder );
	if ( cfg >= 0 ) {
		notedPart[model] = part;
		notedCfg[model] = cfg;
	}
}

// the frame of the same animation, as far through it, in another model's animation.cfg
static int RemapFrame( const ozAnimCfg_t *from, const ozAnimCfg_t *to, int part, int frame ) {
	int i;

	for ( i = 0; i < NUM_ANIMS; i++ ) {
		const int n = MAX( from->num[i], 1 );
		if ( ( part == PART_UPPER && i >= LEGS_WALKCR ) || ( part == PART_LOWER && i >= TORSO_GESTURE && i < LEGS_WALKCR ) ) {
			continue;
		}
		if ( frame >= from->first[i] && frame < from->first[i] + n ) {
			return to->first[i] + ( to->num[i] > 1 ? ( frame - from->first[i] ) * to->num[i] / n : 0 );
		}
	}
	return frame;
}

static struct {
	char		folder[MAX_QPATH];		// "" while off
	int			generation, cfg;
	qhandle_t	model[4], skin[4][3];	// by part; skins default, red, blue
} forced;

static qboolean Forcing( void ) {
	const char	*want = oz_gameplay_force_model->string;
	static const char *parts[4] = { "", "lower", "upper", "head" }, *skins[3] = { "default", "red", "blue" };
	int			p, s;

	if ( !want[0] || !Q_stricmp( want, "off" ) || strchr( want, '/' ) || strchr( want, '.' ) ) {
		forced.folder[0] = '\0';
		return qfalse;
	}
	if ( !Q_stricmp( want, forced.folder ) && forced.generation == OzE_RendererGeneration() ) {
		return forced.model[PART_LOWER] != 0;
	}
	Q_strncpyz( forced.folder, want, sizeof( forced.folder ) );
	forced.generation = OzE_RendererGeneration();
	forced.cfg = AnimCfg( want );
	for ( p = PART_LOWER; p <= PART_HEAD; p++ ) {
		char model[MAX_QPATH];
		Com_sprintf( model, sizeof( model ), "models/players/%s/%s.md3", want, parts[p] );
		forced.model[p] = re.RegisterModel( model );
		OzHud_NoteModel( forced.model[p], model );	// a player's body too (bright skins come after the swap)
		for ( s = 0; s < 3; s++ ) {
			const char *skin = va( "models/players/%s/%s_%s.skin", want, parts[p], skins[s] );
			forced.skin[p][s] = FS_ReadFile( skin, NULL ) > 0 ? re.RegisterSkin( skin ) : 0;
		}
	}
	if ( !forced.model[PART_LOWER] || !forced.model[PART_UPPER] || !forced.model[PART_HEAD] || forced.cfg < 0 ||
		 !animCfgs[forced.cfg].valid ) {
		Com_Printf( S_COLOR_YELLOW "Force enemy model: no character %s\n", want );
		forced.model[PART_LOWER] = 0;
	}
	return forced.model[PART_LOWER] != 0;
}

// an enemy's body as the cgame drew it this scene, and where the parts went
typedef struct {
	int				scene;
	vec3_t			lightingOrigin;		// every part, and what hangs on them, has the body's
	refEntity_t		legs, torso;		// as the cgame added them
	refEntity_t		legsNew;			// and the legs as drawn
	vec3_t			torsoOrigin, torsoAxis[3];	// where the torso went
	int				torsoFrame, torsoOldFrame;	// in the new model
	qboolean		hasTorso;
	struct {
		vec3_t		oldOrigin, oldAxis[3], newOrigin, newAxis[3];
	}				tags[2];			// tag_weapon and tag_flag of the torso: where they were and are
	int				anchor;				// the tag the last thing hung on the torso was on, or -1
} ozForcedBody_t;

static const char	*attachTags[2] = { "tag_weapon", "tag_flag" };

static ozForcedBody_t	bodies[MAX_CLIENTS];
static int				scene = 1;

static void Tag( orientation_t *tag, qhandle_t model, int oldframe, int frame, float backlerp, const char *name ) {
	if ( !re.LerpTag( tag, model, oldframe, frame, 1.0f - backlerp, name ) ) {
		VectorClear( tag->origin );
		AxisClear( tag->axis );
	}
}

static void Transpose( vec3_t m[3], vec3_t out[3] ) {
	int i, j;

	for ( i = 0; i < 3; i++ ) {
		for ( j = 0; j < 3; j++ ) {
			out[i][j] = m[j][i];
		}
	}
}

/*
=================
Rehang

A child the cgame hung on its parent's tag (CG_PositionRotatedEntityOnTag: axis = turn x tag x parent):
the same turn, on the parent's new tag, wherever the parent went.
=================
*/
static void Rehang( refEntity_t *child, vec3_t parentAxis[3], const vec3_t parentNewOrigin, vec3_t parentNewAxis[3],
					orientation_t *tagOld, orientation_t *tagNew ) {
	vec3_t	pt[3], tt[3], a[3], b[3], turn[3];
	int		i;

	Transpose( parentAxis, pt );
	Transpose( tagOld->axis, tt );
	MatrixMultiply( child->axis, pt, a );
	MatrixMultiply( a, tt, turn );
	MatrixMultiply( turn, tagNew->axis, b );
	MatrixMultiply( b, parentNewAxis, child->axis );
	VectorCopy( parentNewOrigin, child->origin );
	for ( i = 0; i < 3; i++ ) {
		VectorMA( child->origin, tagNew->origin[i], parentNewAxis[i], child->origin );
	}
	VectorCopy( child->origin, child->oldorigin );
}

// where a tag of a model at a place is
static void TagPlace( const vec3_t origin, vec3_t axis[3], orientation_t *tag, vec3_t outOrigin, vec3_t outAxis[3] ) {
	int i;

	VectorCopy( origin, outOrigin );
	for ( i = 0; i < 3; i++ ) {
		VectorMA( outOrigin, tag->origin[i], axis[i], outOrigin );
	}
	MatrixMultiply( tag->axis, axis, outAxis );
}

// a rigid move of an entity from one frame to another (what hangs on the gun, or on the flag pole)
static void Carry( refEntity_t *e, const vec3_t fromOrigin, vec3_t fromAxis[3], const vec3_t toOrigin, vec3_t toAxis[3] ) {
	vec3_t	d, local, axis[3];
	int		i, j;

	VectorSubtract( e->origin, fromOrigin, d );
	for ( i = 0; i < 3; i++ ) {
		local[i] = DotProduct( d, fromAxis[i] );
	}
	VectorCopy( toOrigin, e->origin );
	for ( i = 0; i < 3; i++ ) {
		VectorMA( e->origin, local[i], toAxis[i], e->origin );
	}
	VectorCopy( e->origin, e->oldorigin );
	for ( j = 0; j < 3; j++ ) {
		VectorClear( axis[j] );
		for ( i = 0; i < 3; i++ ) {
			VectorMA( axis[j], DotProduct( e->axis[j], fromAxis[i] ), toAxis[i], axis[j] );
		}
	}
	for ( j = 0; j < 3; j++ ) {
		VectorCopy( axis[j], e->axis[j] );
	}
}

// a body part of an enemy: the forced model's, its frames moved to the same animation, hung on its tags
static void ForcePart( refEntity_t *ent, int owner, int team ) {
	ozForcedBody_t		*b = &bodies[owner];
	const int			part = notedPart[ent->hModel];
	const ozAnimCfg_t	*from = &animCfgs[notedCfg[ent->hModel]], *to = &animCfgs[forced.cfg];
	const int			skin = team == TEAM_RED ? 1 : team == TEAM_BLUE ? 2 : 0;
	refEntity_t			orig = *ent;
	const qboolean		remap = from->valid && Q_stricmp( from->folder, forced.folder );
	orientation_t		tagOld, tagNew;
	int					i;

	if ( b->scene != scene ) {
		Com_Memset( b, 0, sizeof( *b ) );
		b->scene = scene;
	}
	VectorCopy( ent->lightingOrigin, b->lightingOrigin );
	ent->hModel = forced.model[part];
	ent->customSkin = forced.skin[part][skin] ? forced.skin[part][skin] : forced.skin[part][0];
	if ( part != PART_HEAD && remap ) {
		ent->frame = RemapFrame( from, to, part, orig.frame );
		ent->oldframe = RemapFrame( from, to, part, orig.oldframe );
	}

	if ( part == PART_LOWER ) {
		b->legs = orig;
		b->legsNew = *ent;
		b->hasTorso = qfalse;
	} else if ( part == PART_UPPER && b->legs.hModel ) {
		vec3_t legsAxis[3], torsoAxis[3];
		Tag( &tagOld, b->legs.hModel, b->legs.oldframe, b->legs.frame, b->legs.backlerp, "tag_torso" );
		Tag( &tagNew, b->legsNew.hModel, b->legsNew.oldframe, b->legsNew.frame, b->legs.backlerp, "tag_torso" );
		AxisCopy( b->legs.axis, legsAxis );
		Rehang( ent, legsAxis, b->legs.origin, legsAxis, &tagOld, &tagNew );
		b->torso = orig;
		VectorCopy( ent->origin, b->torsoOrigin );
		AxisCopy( ent->axis, b->torsoAxis );
		b->torsoFrame = ent->frame;
		b->torsoOldFrame = ent->oldframe;
		b->hasTorso = qtrue;
		b->anchor = -1;
		AxisCopy( orig.axis, torsoAxis );
		for ( i = 0; i < 2; i++ ) {
			Tag( &tagOld, orig.hModel, orig.oldframe, orig.frame, orig.backlerp, attachTags[i] );
			Tag( &tagNew, ent->hModel, ent->oldframe, ent->frame, ent->backlerp, attachTags[i] );
			TagPlace( orig.origin, torsoAxis, &tagOld, b->tags[i].oldOrigin, b->tags[i].oldAxis );
			TagPlace( ent->origin, ent->axis, &tagNew, b->tags[i].newOrigin, b->tags[i].newAxis );
		}
	} else if ( part == PART_HEAD && b->hasTorso ) {
		vec3_t torsoAxis[3];
		Tag( &tagOld, b->torso.hModel, b->torso.oldframe, b->torso.frame, b->torso.backlerp, "tag_head" );
		Tag( &tagNew, forced.model[PART_UPPER], b->torsoOldFrame, b->torsoFrame, b->torso.backlerp, "tag_head" );
		AxisCopy( b->torso.axis, torsoAxis );
		Rehang( ent, torsoAxis, b->torsoOrigin, b->torsoAxis, &tagOld, &tagNew );
	}
}

// what the cgame hung on an enemy's torso: the gun on tag_weapon, a flag pole on tag_flag, and what hangs on
// those; each moves with the tag it is on, or with the last thing that was
static void ForceAttached( refEntity_t *ent, ozForcedBody_t *b ) {
	vec3_t	torsoAxis[3];
	int		i;

	for ( i = 0; i < 2; i++ ) {
		if ( Distance( b->tags[i].oldOrigin, ent->origin ) < 0.5f ) {
			b->anchor = i;
			break;
		}
	}
	if ( b->anchor >= 0 ) {
		Carry( ent, b->tags[b->anchor].oldOrigin, b->tags[b->anchor].oldAxis, b->tags[b->anchor].newOrigin,
			   b->tags[b->anchor].newAxis );
	} else {
		AxisCopy( b->torso.axis, torsoAxis );
		Carry( ent, b->torso.origin, torsoAxis, b->torsoOrigin, b->torsoAxis );
	}
}

/*
=================
OzView_Entity

Every other entity the cgame adds: an enemy's body part takes the forced model, and what hangs on a forced
body moves with it. 0 keeps it for the scene's render (a lightning beam from the gun), or drops it: under
r_ozRagdollTest (a cheat) the fighters dropped into their ragdolls are alive, and the cgame would draw each
one's gun where its living hand is, floating over the body (a dead player has none: the game drops it).
=================
*/
int OzView_Entity( refEntity_t *ent ) {
	int i, owner, team = 0;

	if ( ent->reType == RT_MODEL && ent->hModel > 0 && ent->hModel < MAX_NOTED_MODELS && notedWeapon[ent->hModel] &&
		 ( ent->renderfx & RF_LIGHTING_ORIGIN ) && !( ent->renderfx & RF_FIRST_PERSON ) &&
		 Cvar_VariableIntegerValue( "r_ozRagdollTest" ) ) {
		return 0;
	}
	if ( ent->reType == RT_LIGHTNING ) {
		for ( i = 0; i < numGunParts; i++ ) {
			if ( !gunBeam[i] && VectorCompare( gunParts[i].origin, ent->origin ) && numGunParts < MAX_GUN_PARTS ) {
				gunBeam[numGunParts] = qtrue;
				gunParts[numGunParts++] = *ent;
				return 0;
			}
		}
		return 1;
	}
	InitCvars();
	if ( ent->reType != RT_MODEL || !Forcing() ) {
		return 1;
	}
	if ( ent->hModel > 0 && ent->hModel < MAX_NOTED_MODELS && notedPart[ent->hModel] ) {
		owner = OzHud_EnemyBody( ent, &team );
		if ( owner >= 0 && owner < MAX_CLIENTS ) {
			ForcePart( ent, owner, team );
		}
		return 1;		// or a body we leave alone (yours, a teammate's)
	}
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		ozForcedBody_t *b = &bodies[i];
		if ( b->scene == scene && b->hasTorso && VectorCompare( b->lightingOrigin, ent->lightingOrigin ) ) {
			ForceAttached( ent, b );
			break;
		}
	}
	return 1;
}

// ================================================================ the scene

void OzView_ClearScene( void ) {
	numGunParts = 0;
	numGunQuads = 0;
	scene++;
}

cvar_t *cl_ozProbe;

void OzView_RenderScene( const refdef_t *fd ) {
	if ( numGunParts && !( fd->rdflags & RDF_NOWORLDMODEL ) ) {
		InitCvars();
		PlaceGun( fd );
		numGunParts = 0;
	}
	if ( cl_ozProbe && cl_ozProbe->integer > 1 && !( fd->rdflags & RDF_NOWORLDMODEL ) ) {
		Com_Printf( "ozview %d %.3f %.3f %.3f %.3f %d %.3f %.3f\n", fd->time, fd->vieworg[0], fd->vieworg[1], fd->vieworg[2],
			RAD2DEG( -asin( fd->viewaxis[0][2] ) ), cl.cmds[cl.cmdNumber & CMD_MASK].serverTime, Sys_Microseconds() / 1000.0,
			RAD2DEG( atan2( fd->viewaxis[1][2], fd->viewaxis[2][2] ) ) );
	}
}

// ================================================================ the frame-rate probe

/*
=================
OzView_ProbeSnapshot

cl_ozProbe 1 (engine/tests/frame_rate_test.py): each new snapshot's player as the server moved it, a line
"ozprobe <server time> <command time> <x> <y> <z> <xy speed> <z speed> <ground> <weapon> <ammo> <events> <hits>
<damage event>", the events the ones new since the last snapshot. 2 also prints each world view's place, a line
"ozview <time> <x> <y> <z> <pitch> <latest command's time> <real ms> <roll>".
=================
*/
void OzView_ProbeSnapshot( void ) {
	static int	sequence = -1;
	const playerState_t *ps = &cl.snap.ps;
	char		events[64];
	int			i;

	if ( !cl_ozProbe ) {
		cl_ozProbe = Cvar_Get( "cl_ozProbe", "0", CVAR_TEMP );
	}
	if ( !cl_ozProbe->integer || !cl.snap.valid ) {
		sequence = -1;
		return;
	}
	events[0] = '\0';
	if ( sequence >= 0 && ps->eventSequence > sequence ) {
		for ( i = MAX( sequence, ps->eventSequence - MAX_PS_EVENTS ); i < ps->eventSequence; i++ ) {
			Q_strcat( events, sizeof( events ), va( "%s%d", events[0] ? "," : "", ps->events[i & ( MAX_PS_EVENTS - 1 )] & ~EV_EVENT_BITS ) );
		}
	}
	sequence = ps->eventSequence;
	Com_Printf( "ozprobe %d %d %.3f %.3f %.3f %.2f %.2f %d %d %d %s %d %d\n", cl.snap.serverTime, ps->commandTime,
		ps->origin[0], ps->origin[1], ps->origin[2], sqrt( ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1] ),
		ps->velocity[2], ps->groundEntityNum != ENTITYNUM_NONE, ps->weapon, ps->weapon > 0 && ps->weapon < MAX_WEAPONS ? ps->ammo[ps->weapon] : 0,
		events[0] ? events : "-", ps->persistant[PERS_HITS], ps->damageEvent );
}

// ================================================================ the ragdolls' push

/*
=================
OzHud_BodyVelocity

The speed of the snapshot's player (or corpse) nearest a point, within 128 units: baseq3's protocol sends a
player's velocity in pos.trDelta, so a body just killed moves with the hit's knockback. Players' speeds count
once a snapshot of this map has shown one moving (a mod may send none).
=================
*/
qboolean OzHud_BodyVelocity( const vec3_t origin, vec3_t velocity ) {
	static int			serverId = -1, scanned = -1;
	static qboolean		speedsSent;
	const entityState_t	*found = NULL;
	float				best = 128.0f * 128.0f, d;		// a body knocked away is drawn behind the snapshot
	int					i;

	if ( cls.state != CA_ACTIVE || !cl.snap.valid ) {
		return qfalse;
	}
	if ( serverId != cl.serverId ) {
		serverId = cl.serverId;
		speedsSent = qfalse;
		scanned = -1;
	}
	for ( i = 0; !speedsSent && scanned != cl.snap.serverTime && i < cl.snap.numEntities; i++ ) {
		const entityState_t *es = &cl.parseEntities[( cl.snap.parseEntitiesNum + i ) & ( MAX_PARSE_ENTITIES - 1 )];
		// TR_INTERPOLATE, or TR_LINEAR_STOP under g_smoothClients
		speedsSent = es->eType == ET_PLAYER && es->pos.trType != TR_STATIONARY && !VectorCompare( es->pos.trDelta, vec3_origin );
	}
	scanned = cl.snap.serverTime;
	for ( i = 0; i < cl.snap.numEntities; i++ ) {
		const entityState_t *es = &cl.parseEntities[( cl.snap.parseEntitiesNum + i ) & ( MAX_PARSE_ENTITIES - 1 )];
		if ( es->eType != ET_PLAYER ) {
			continue;
		}
		d = DistanceSquared( es->pos.trBase, origin );
		if ( d < best ) {
			best = d;
			found = es;
		}
	}
	// the player the snapshot is from (you, or whoever you follow) isn't among its entities
	if ( cl.snap.ps.pm_type != PM_SPECTATOR && DistanceSquared( cl.snap.ps.origin, origin ) < best ) {
		VectorCopy( cl.snap.ps.velocity, velocity );
		return qtrue;
	}
	if ( !found || !speedsSent ) {
		return qfalse;
	}
	if ( found->pos.trType == TR_STATIONARY ) {
		VectorClear( velocity );
	} else {
		VectorCopy( found->pos.trDelta, velocity );
	}
	return qtrue;
}
