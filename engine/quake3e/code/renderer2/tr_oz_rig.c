/*
===========================================================================
Copyright (C) 2026 Oz Arena contributors

This file is part of Oz Arena, built on the Quake III Arena source code.

Oz Arena is free software; you can redistribute it and/or modify it under the terms of the GNU General
Public License as published by the Free Software Foundation; either version 2 of the License, or (at your
option) any later version.
===========================================================================
*/
// tr_oz_rig.c -- the remastered fighters under any cgame (ENGINE_PLAN.md P2)
//
// A cgame draws a player as three md3s, lower, upper and head, each a refEntity hung on a tag of the one
// before. When tools/make_fighters.py's oz_rig.iqm sits beside them (in the remastered pak, so never on a
// pure server), the md3s still load, but each part draws its share of the rig: one skeleton, posed from all
// three entities of the player.
//
// - The rig's "lower" frames are lower.md3's frames and its "upper" frames upper.md3's, sampled from the
//   clip each frame belongs to, so the cgame's frame numbers pick the same pose.
// - Legs joints follow the legs entity. Torso joints hang from tag_torso the way the md3 torso does, so the
//   cgame's torso turn lands there; the head joints follow the head entity.
// - LerpTag answers tag_torso, tag_head and tag_weapon from the rig's tag joints (fitted to id's tags), so
//   the gun and anything else the cgame hangs on a tag follows the drawn rig.
// - Skins work as for the md3: the rig's meshes carry the md3 surfaces' names.
// - When the legs play a death, the ragdoll takes the skeleton over (tr_oz_ragdoll.c).

#include "tr_oz_rig.h"

#define OZ_RIG_POOL			(512 * 1024)	// floats of matrices in a frame
#define OZ_RIG_SCENE_PARTS	256				// fighter parts in one scene
#define OZ_RIG_LINK			0.5f			// how near a part must be to its parent's tag, in units
#define OZ_RIG_CULL_RADIUS	64.0f			// a part's reach from its origin
#define OZ_RIG_FOG_RADIUS	32.0f

typedef struct {
	trRefEntity_t	*ent;
	const ozRig_t	*rig;
	int				part;
	float			*joints;	// every joint in the part's model space, at the entity's frame
	float			world[12];	// the entity's placement
	vec3_t			childAt;	// where the next part hangs: tag_torso of legs, tag_head of a torso
	int				lower, upper, head;		// the fighter's other parts in this scene, or -1
	int				src[3];		// the part each of the three joint groups follows
	float			*skeleton;	// the fighter's joints in the world, if this part owns them
	float			dissolve;
} ozPart_t;

static float	pool[OZ_RIG_POOL];
static int		poolUsed;
static struct {
	int		parts, torsos, heads, loneTorsos, loneHeads;	// in world scenes, since the last ozrigs
} rigStats;

// ozrigs: how many fighter parts the world scenes drew since the last time, and how many torsos and heads
// found no part to hang on (a cgame, or the engine's forced enemy model, put them off their parent's tag)
void R_OzRigStats_f( void ) {
	ri.Printf( PRINT_ALL, "rig parts: %d, %d torsos (%d not on legs), %d heads (%d not on a torso)\n", rigStats.parts,
			   rigStats.torsos, rigStats.loneTorsos, rigStats.heads, rigStats.loneHeads );
	Com_Memset( &rigStats, 0, sizeof( rigStats ) );
}

static float *PoolAlloc( int floats ) {
	float *p;

	if ( poolUsed + floats > OZ_RIG_POOL ) {
		return NULL;
	}
	p = pool + poolUsed;
	poolUsed += floats;
	return p;
}

void R_OzRigNextFrame( void ) {
	poolUsed = 0;
}

void R_OzMat34Multiply( const float *a, const float *b, float *out ) {
	int r;

	for ( r = 0; r < 3; r++ ) {
		const float *row = a + 4 * r;
		out[4 * r + 0] = row[0] * b[0] + row[1] * b[4] + row[2] * b[8];
		out[4 * r + 1] = row[0] * b[1] + row[1] * b[5] + row[2] * b[9];
		out[4 * r + 2] = row[0] * b[2] + row[1] * b[6] + row[2] * b[10];
		out[4 * r + 3] = row[0] * b[3] + row[1] * b[7] + row[2] * b[11] + row[3];
	}
}

// the inverse of a rotation (with a uniform scale) and a translation
void R_OzMat34InvertRigid( const float *m, float *out ) {
	float s2 = m[0] * m[0] + m[4] * m[4] + m[8] * m[8];
	float k = s2 > 0.0f ? 1.0f / s2 : 1.0f;
	int r;

	for ( r = 0; r < 3; r++ ) {
		out[4 * r + 0] = m[0 + r] * k;
		out[4 * r + 1] = m[4 + r] * k;
		out[4 * r + 2] = m[8 + r] * k;
	}
	for ( r = 0; r < 3; r++ ) {
		out[4 * r + 3] = -( out[4 * r] * m[3] + out[4 * r + 1] * m[7] + out[4 * r + 2] * m[11] );
	}
}

static void EntityMatrix( const refEntity_t *e, float *m ) {
	int r;

	for ( r = 0; r < 3; r++ ) {
		m[4 * r + 0] = e->axis[0][r];
		m[4 * r + 1] = e->axis[1][r];
		m[4 * r + 2] = e->axis[2][r];
		m[4 * r + 3] = e->origin[r];
	}
}

static int ClampFrame( int frame, int count ) {
	return frame < 0 ? 0 : frame >= count ? count - 1 : frame;
}

static int JointNamed( const iqmData_t *data, const char *name ) {
	const char *s = data->jointNames;
	int i;

	for ( i = 0; i < data->num_joints; i++ ) {
		if ( !Q_stricmp( s, name ) ) {
			return i;
		}
		s += strlen( s ) + 1;
	}
	return -1;
}

static int ParseInt( const char **text ) {
	return atoi( COM_ParseExt( text, qfalse ) );
}

static float ParseFloat( const char **text ) {
	return atof( COM_ParseExt( text, qfalse ) );
}

#define OZ_METER		32.0f		// id units
#define OZ_GRAVITY		25.0f		// m/s^2: id's 800 units/s^2
#define OZ_TONE_WEAK	0.3f		// the muscle tone's pull through a joint's range, against what holds its limb up:
									// never enough to lift it
#define OZ_TONE_STRONG	3.0f		// and at its limits
#define OZ_KNEE			50.0f		// degrees the neutral posture bends a knee

// a direction, or a point, in a frame's own space (3x4 row-major, rigid)
static void ToLocal( const float *m, const vec3_t v, float *out ) {
	int c;
	for ( c = 0; c < 3; c++ ) {
		out[c] = m[c] * v[0] + m[4 + c] * v[1] + m[8 + c] * v[2];
	}
}

static void PointToLocal( const float *m, const vec3_t p, float *out ) {
	vec3_t d;
	VectorSet( d, p[0] - m[3], p[1] - m[7], p[2] - m[11] );
	ToLocal( m, d, out );
	VectorScale( out, 1.0f / OZ_METER, out );
}

// v turned by an angle about a unit axis (Rodrigues)
static void Turn( const vec3_t v, const vec3_t k, float angle, vec3_t out ) {
	vec3_t kv;
	float c = cos( angle ), s = sin( angle ), d = DotProduct( k, v ) * ( 1.0f - c );
	int i;
	CrossProduct( k, v, kv );
	for ( i = 0; i < 3; i++ ) {
		out[i] = v[i] * c + kv[i] * s + k[i] * d;
	}
}

/*
=================
RagdollBodies

What the physics needs of each body (tr_oz_ragdoll.c, oz_physics.h), worked out once from the rig standing:
the capsule's frame (its y along its axis) and the bone's frame in it; the joint in each of its two bodies
(a cone's twist axis along the body's capsule, a hip's turned on the parent's side to the thigh hanging 30
degrees forward, so its cone centers there; a hinge's axis across the body, to the left, so the lower part
bends back as it turns positive); the limits; the neutral posture's turn; and the muscle tone's torques,
against what holds up the limb beyond the joint (its weight at its middle, under id's gravity).
=================
*/
static void RagdollBodies( ozRig_t *rig ) {
	const float *bind = rig->data->bindJoints;
	const vec3_t forward = { 1.0f, 0.0f, 0.0f }, left = { 0.0f, 1.0f, 0.0f };
	float inv[12];
	int i, k, x;

	for ( i = 0; i < rig->data->num_joints; i++ ) {
		rig->jointBody[i] = -1;
	}
	for ( i = 0; i < rig->numBodies; i++ ) {
		ozBody_t *b = &rig->bodies[i];
		vec3_t ax, ay, az, ref;

		rig->jointBody[b->joint] = i;
		// how far its far end can reach from the pelvis, every joint between straight
		b->chain = b->parent < 0 ? 0.0f : rig->bodies[b->parent].chain +
			Distance( b->at, b->parent ? rig->bodies[b->parent].at : rig->bodies[0].center );
		rig->span = MAX( rig->span, b->chain + Distance( b->at, b->center ) + b->height * 0.5f );
		VectorCopy( b->axis, ay );
		VectorNormalize( ay );
		VectorSet( ref, 0.0f, 0.0f, 1.0f );
		if ( fabs( ay[2] ) > 0.9f ) {
			VectorSet( ref, 1.0f, 0.0f, 0.0f );
		}
		CrossProduct( ay, ref, ax );
		VectorNormalize( ax );
		CrossProduct( ax, ay, az );
		for ( k = 0; k < 3; k++ ) {
			b->place[4 * k + 0] = ax[k];
			b->place[4 * k + 1] = ay[k];
			b->place[4 * k + 2] = az[k];
			b->place[4 * k + 3] = b->center[k];
		}
		R_OzMat34InvertRigid( b->place, inv );
		R_OzMat34Multiply( inv, bind + 12 * b->joint, b->toBone );
	}
	for ( i = 0; i < rig->numBodies; i++ ) {
		ozBody_t *b = &rig->bodies[i];
		const ozBody_t *u;
		vec3_t t, pl, t1, pl1, n1, h, lean, com;
		float mass = 0.0f, hold;

		if ( b->parent < 0 ) {
			continue;
		}
		u = &rig->bodies[b->parent];
		VectorNormalize2( b->axis, t );
		PointToLocal( u->place, b->at, b->point1 );
		PointToLocal( b->place, b->at, b->point2 );
		if ( b->kind == 2 ) {
			VectorMA( left, -DotProduct( left, t ), t, h );
			VectorNormalize( h );
			ToLocal( u->place, h, b->axis1 );
			ToLocal( u->place, t, b->normal1 );
			ToLocal( b->place, h, b->axis2 );
			ToLocal( b->place, t, b->normal2 );
			b->twistMin = DEG2RAD( -b->bent );
			b->twistMax = DEG2RAD( 140.0f - b->bent );
			b->neutral[0] = DEG2RAD( OZ_KNEE - b->bent );
			b->neutral[0] = b->neutral[0] < b->twistMin ? b->twistMin : b->neutral[0] > b->twistMax ? b->twistMax : b->neutral[0];
		} else {
			// the plane axis: forward made square to the twist axis (up, for one pointing forward)
			VectorCopy( fabs( t[0] ) > 0.9f ? vec3_origin : forward, pl );
			if ( fabs( t[0] ) > 0.9f ) {
				pl[2] = 1.0f;
			}
			VectorMA( pl, -DotProduct( pl, t ), t, pl );
			VectorNormalize( pl );
			VectorCopy( t, t1 );
			VectorCopy( pl, pl1 );
			if ( b->hip ) {
				vec3_t hang = { 0.5f, 0.0f, -0.8660254f }, k;
				float angle = acos( DotProduct( t, hang ) > 1.0f ? 1.0f : DotProduct( t, hang ) );
				CrossProduct( t, hang, k );
				if ( VectorNormalize( k ) > 1e-4f ) {
					Turn( t, k, angle, t1 );
					Turn( pl, k, angle, pl1 );
				}
			}
			ToLocal( u->place, t1, b->axis1 );
			ToLocal( u->place, pl1, b->normal1 );
			ToLocal( b->place, t, b->axis2 );
			ToLocal( b->place, pl, b->normal2 );
			b->swingLimit = DEG2RAD( b->swing );
			b->twistMin = DEG2RAD( -b->twist );
			b->twistMax = DEG2RAD( b->twist );
			// the neutral posture: the part turned forward by its lean, in the joint's own space (twist axis, plane
			// axis, their normal)
			Vector4Set( b->neutral, 0.0f, 0.0f, 0.0f, 1.0f );
			CrossProduct( t1, forward, lean );
			if ( b->lean != 0.0f && VectorNormalize( lean ) > 1e-4f ) {
				float s = sin( DEG2RAD( b->lean ) * 0.5f );
				CrossProduct( t1, pl1, n1 );
				Vector4Set( b->neutral, DotProduct( lean, t1 ) * s, DotProduct( lean, pl1 ) * s, DotProduct( lean, n1 ) * s,
							cos( DEG2RAD( b->lean ) * 0.5f ) );
			}
		}
		// the tone's torques: the weight of everything hanging from this joint, at its middle
		VectorClear( com );
		for ( k = 0; k < rig->numBodies; k++ ) {
			for ( x = k; x >= 0 && x != i; x = rig->bodies[x].parent ) {
			}
			if ( x == i ) {
				mass += rig->bodies[k].mass;
				VectorMA( com, rig->bodies[k].mass, rig->bodies[k].center, com );
			}
		}
		VectorScale( com, 1.0f / mass, com );
		hold = mass * OZ_GRAVITY * Distance( com, b->at ) / OZ_METER;
		b->weakTorque = MAX( OZ_TONE_WEAK * hold, 0.2f );
		b->strongTorque = MAX( OZ_TONE_STRONG * hold, 1.0f );
	}
}

/*
=================
RigFor

The rig an IQM's comment describes, or NULL for any other IQM. The comment (tools/make_fighters.py):
"oz_rig 1", "upper <frame>", "deaths <frames>", "dead <frame> <frame> <frame>", "meshes <part letters>",
"joints <part letters>", "bodies <n> (<joint> <parent> <kind> <center> <axis> <radius> <height> <mass> <reach>
<joint point> <swing> <twist> <bent> <hip> <lean>)...", "apart <n> (<body> <body>)...". (Older rigs' "particles"
and "links", the first ragdoll's, are skipped.)
=================
*/
static ozRig_t *RigFor( model_t *iqm ) {
	iqmData_t *data = iqm->modelData;
	const char *text, *tok;
	char meshes[MAX_TOKEN_CHARS], joints[MAX_TOKEN_CHARS];
	ozRig_t *rig;
	int i, n, ok = 1;

	if ( data->ozRig || !data->comment ) {
		return data->ozRig;
	}
	text = data->comment;
	tok = COM_ParseExt( &text, qtrue );
	if ( strcmp( tok, "oz_rig" ) || ParseInt( &text ) != 1 ) {
		return NULL;
	}
	rig = ri.Hunk_Alloc( sizeof( *rig ) + data->num_surfaces + data->num_joints + 2, h_low );
	rig->data = data;
	Q_strncpyz( rig->name, iqm->name, sizeof( rig->name ) );
	rig->meshPart = (char *)( rig + 1 );
	rig->jointPart = rig->meshPart + data->num_surfaces + 1;
	meshes[0] = joints[0] = '\0';
	while ( ok ) {
		tok = COM_ParseExt( &text, qtrue );
		if ( !tok[0] ) {
			break;
		}
		if ( !strcmp( tok, "upper" ) ) {
			rig->upperFirst = ParseInt( &text );
		} else if ( !strcmp( tok, "deaths" ) ) {
			rig->deathFrames = ParseInt( &text );
		} else if ( !strcmp( tok, "dead" ) ) {
			for ( i = 0; i < 3; i++ ) {
				rig->deadFrame[i] = ParseInt( &text );
			}
		} else if ( !strcmp( tok, "meshes" ) ) {
			Q_strncpyz( meshes, COM_ParseExt( &text, qfalse ), sizeof( meshes ) );
		} else if ( !strcmp( tok, "joints" ) ) {
			Q_strncpyz( joints, COM_ParseExt( &text, qfalse ), sizeof( joints ) );
		} else if ( !strcmp( tok, "spine" ) ) {
			n = ParseInt( &text );
			ok = n >= 0 && n <= (int)ARRAY_LEN( rig->spine );
			for ( i = 0; ok && i < n; i++ ) {
				rig->spine[i] = ParseInt( &text );
				ok = rig->spine[i] >= 0 && rig->spine[i] < data->num_joints;
			}
			rig->numSpine = ok ? n : 0;
		} else if ( !strcmp( tok, "bodies" ) ) {
			n = ParseInt( &text );
			ok = n > 0 && n <= OZ_RAGDOLL_BODIES;
			for ( i = 0; ok && i < n; i++ ) {
				ozBody_t *b = &rig->bodies[i];
				int k;
				b->joint = ParseInt( &text );
				b->parent = ParseInt( &text );
				b->kind = ParseInt( &text );
				for ( k = 0; k < 3; k++ ) {
					b->center[k] = ParseFloat( &text );
				}
				for ( k = 0; k < 3; k++ ) {
					b->axis[k] = ParseFloat( &text );
				}
				b->radius = ParseFloat( &text );
				b->height = ParseFloat( &text );
				b->mass = ParseFloat( &text );
				b->reach = ParseFloat( &text );
				for ( k = 0; k < 3; k++ ) {
					b->at[k] = ParseFloat( &text );
				}
				b->swing = ParseFloat( &text );
				b->twist = ParseFloat( &text );
				b->bent = ParseFloat( &text );
				b->hip = ParseInt( &text ) != 0;
				b->lean = ParseFloat( &text );
				ok = b->joint >= 0 && b->joint < data->num_joints && b->parent < i && ( b->parent >= 0 || i == 0 ) &&
					b->kind >= 0 && b->kind <= 2 && b->radius > 0.0f && b->mass > 0.0f;
			}
			rig->numBodies = ok ? n : 0;
		} else if ( !strcmp( tok, "apart" ) ) {
			n = ParseInt( &text );
			for ( i = 0; ok && i < n; i++ ) {
				int a = ParseInt( &text ), b = ParseInt( &text );
				ok = a >= 0 && b >= 0 && a < rig->numBodies && b < rig->numBodies;
				if ( ok ) {
					rig->apart[a][b] = rig->apart[b][a] = 1;
				}
			}
		}
	}
	if ( !ok || rig->upperFirst <= 0 || rig->upperFirst >= data->num_frames ||
		 (int)strlen( meshes ) != data->num_surfaces || (int)strlen( joints ) != data->num_joints ) {
		ri.Printf( PRINT_WARNING, "%s: a fighter rig with a bad description\n", iqm->name );
		return NULL;
	}
	strcpy( rig->meshPart, meshes );
	strcpy( rig->jointPart, joints );
	rig->tagTorso = JointNamed( data, "tag_torso" );
	rig->tagHead = JointNamed( data, "tag_head" );
	rig->tagWeapon = JointNamed( data, "tag_weapon" );
	if ( rig->tagTorso < 0 || rig->tagHead < 0 || rig->tagWeapon < 0 ) {
		ri.Printf( PRINT_WARNING, "%s: a fighter rig without its tag joints\n", iqm->name );
		return NULL;
	}

	RagdollBodies( rig );

	data->ozRig = rig;
	return rig;
}

/*
=================
R_OzRigRegister

A player part (<folder>/lower.md3, upper.md3 or head.md3) gets the rig in <folder>/oz_rig.iqm, if there is one.
With r_ozModernFighters (Settings, Experimental), <folder>/oz_rig_modern.iqm comes first: the upgraded fighters
(pak_oz_modern_fighters.pk3, tools/fighter_swap), a new model on its own skeleton in the fighter's frame slots.
=================
*/
static qboolean RigPath( const char *folder, int len, const char *rig, char *path ) {
	if ( len + strlen( rig ) >= MAX_QPATH ) {
		return qfalse;
	}
	Q_strncpyz( path, folder, len + 1 );
	Q_strcat( path, MAX_QPATH, rig );
	return ri.FS_ReadFile( path, NULL ) > 0;
}

void R_OzRigRegister( model_t *mod ) {
	char path[MAX_QPATH];
	const char *file;
	model_t *iqm;
	qhandle_t h;
	int part, len;

	mod->ozRig = NULL;
	file = strrchr( mod->name, '/' );
	if ( !file || !r_ozRigs->integer ) {
		return;
	}
	file++;
	if ( !Q_stricmp( file, "lower.md3" ) ) {
		part = OZ_RIG_LOWER;
	} else if ( !Q_stricmp( file, "upper.md3" ) ) {
		part = OZ_RIG_UPPER;
	} else if ( !Q_stricmp( file, "head.md3" ) ) {
		part = OZ_RIG_HEAD;
	} else {
		return;
	}
	len = (int)( file - mod->name );
	if ( !( r_ozModernFighters->integer && RigPath( mod->name, len, "oz_rig_modern.iqm", path ) ) &&
		 !RigPath( mod->name, len, "oz_rig.iqm", path ) ) {
		return;
	}
	h = RE_RegisterModel( path );
	iqm = R_GetModelByHandle( h );
	if ( !h || iqm->type != MOD_IQM ) {
		return;
	}
	mod->ozRig = RigFor( iqm );
	mod->ozRigPart = part;
}

// the transform from a part's model space to the world: the legs part's model space is the rig's; the torso
// hangs at tag_torso and the head at tag_head, as the md3s do
static void PartBase( const ozPart_t *p, const float *joints, float *out ) {
	float inv[12];

	if ( p->part == OZ_RIG_LOWER ) {
		Com_Memcpy( out, p->world, sizeof( p->world ) );
		return;
	}
	R_OzMat34InvertRigid( joints + 12 * ( p->part == OZ_RIG_UPPER ? p->rig->tagTorso : p->rig->tagHead ), inv );
	R_OzMat34Multiply( p->world, inv, out );
}

/*
=================
SpreadTorsoTurn

The cgame turns the torso against the legs at tag_torso (CG_PlayerAngles), as the md3 does: one kink at the
waist. Spread it along the spine instead: the spine's joints take part of the turn, a step more each, and
the chest the whole of it, so the arms, the gun and the head stay where the cgame puts them.
=================
*/
static void SpreadTorsoTurn( ozPart_t *legs, const ozPart_t *torso ) {
	const ozRig_t *rig = legs->rig;
	float tag[12], inv[12], base[12], turn[12], untwisted[12], part[12];
	float s, angle, a;
	vec3_t axis, at;
	int i, r;

	if ( rig->numSpine < 2 ) {
		return;
	}
	// the turn: from the torso where the legs' tag_torso puts it, to where the cgame put it
	R_OzMat34Multiply( legs->world, legs->joints + 12 * rig->tagTorso, tag );
	R_OzMat34InvertRigid( tag, inv );
	R_OzMat34Multiply( torso->world, inv, turn );
	VectorSet( at, tag[3], tag[7], tag[11] );

	// its axis and angle: the skew part is 2 sin(angle) along the axis, the trace 1 + 2 cos(angle)
	VectorSet( axis, turn[9] - turn[6], turn[2] - turn[8], turn[4] - turn[1] );
	s = VectorNormalize( axis );
	if ( s < 1e-5f ) {
		return;		// no turn
	}
	angle = atan2( 0.5f * s, 0.5f * ( turn[0] + turn[5] + turn[10] - 1.0f ) );

	// the untwisted torso: hung from the legs' tag_torso
	R_OzMat34InvertRigid( torso->joints + 12 * rig->tagTorso, inv );
	R_OzMat34Multiply( tag, inv, base );
	for ( i = 0; i < rig->numSpine - 1; i++ ) {
		int j = rig->spine[i];
		float f = (float)( i + 1 ) / rig->numSpine, c, sn, k;

		// part of the turn, about tag_torso
		a = angle * f;
		c = cos( a );
		sn = sin( a );
		k = 1.0f - c;
		part[0] = c + axis[0] * axis[0] * k;
		part[1] = axis[0] * axis[1] * k - axis[2] * sn;
		part[2] = axis[0] * axis[2] * k + axis[1] * sn;
		part[4] = axis[1] * axis[0] * k + axis[2] * sn;
		part[5] = c + axis[1] * axis[1] * k;
		part[6] = axis[1] * axis[2] * k - axis[0] * sn;
		part[8] = axis[2] * axis[0] * k - axis[1] * sn;
		part[9] = axis[2] * axis[1] * k + axis[0] * sn;
		part[10] = c + axis[2] * axis[2] * k;
		for ( r = 0; r < 3; r++ ) {
			part[4 * r + 3] = at[r] - ( part[4 * r] * at[0] + part[4 * r + 1] * at[1] + part[4 * r + 2] * at[2] )
							+ f * turn[4 * r + 3] + f * ( ( turn[4 * r] - 1.0f * ( r == 0 ) ) * at[0] +
							( turn[4 * r + 1] - 1.0f * ( r == 1 ) ) * at[1] + ( turn[4 * r + 2] - 1.0f * ( r == 2 ) ) * at[2] );
		}
		R_OzMat34Multiply( base, torso->joints + 12 * j, untwisted );
		R_OzMat34Multiply( part, untwisted, legs->skeleton + 12 * j );
	}
}

/*
=================
R_OzRigPrepareScene

Before a scene renders: find the fighters' parts among its entities, match each torso to the legs whose
tag_torso it sits on and each head to the torso whose tag_head it sits on, pose each fighter's skeleton in
the world (or take its ragdoll's), then give every part the skinning matrices relative to its own entity.
=================
*/
void R_OzRigPrepareScene( void ) {
	static ozPart_t parts[OZ_RIG_SCENE_PARTS];
	qboolean world = !( tr.refdef.rdflags & RDF_NOWORLDMODEL );
	int n, i, j, s;

	if ( !r_ozRigs->integer ) {
		return;
	}
	if ( world ) {
		R_OzRagdollBeginScene();
	}

	for ( n = 0, i = 0; i < tr.refdef.num_entities && n < OZ_RIG_SCENE_PARTS; i++ ) {
		trRefEntity_t *ent = &tr.refdef.entities[i];
		const model_t *mod;
		ozPart_t *p;
		int first, count, frame, oldframe;
		float tag[12];

		ent->ozDissolve = 0.0f;
		if ( ent->e.reType != RT_MODEL ) {
			continue;
		}
		mod = R_GetModelByHandle( ent->e.hModel );
		if ( !mod->ozRig ) {
			continue;
		}
		p = &parts[n];
		p->joints = PoolAlloc( mod->ozRig->data->num_joints * 12 );
		if ( !p->joints ) {
			break;
		}
		p->ent = ent;
		p->rig = mod->ozRig;
		p->part = mod->ozRigPart;
		p->lower = p->upper = p->head = -1;
		p->skeleton = NULL;
		p->dissolve = 0.0f;

		first = p->part == OZ_RIG_LOWER ? 0 : p->rig->upperFirst;
		count = p->part == OZ_RIG_LOWER ? p->rig->upperFirst : p->rig->data->num_frames - p->rig->upperFirst;
		frame = first + ( p->part == OZ_RIG_HEAD ? 0 : ClampFrame( ent->e.frame, count ) );
		oldframe = first + ( p->part == OZ_RIG_HEAD ? 0 : ClampFrame( ent->e.oldframe, count ) );
		R_IQMJointMats( p->rig->data, frame, oldframe, ent->e.backlerp, p->joints );
		EntityMatrix( &ent->e, p->world );

		if ( p->part != OZ_RIG_HEAD ) {
			float base[12];
			PartBase( p, p->joints, base );
			R_OzMat34Multiply( base, p->joints + 12 * ( p->part == OZ_RIG_LOWER ? p->rig->tagTorso : p->rig->tagHead ), tag );
			VectorSet( p->childAt, tag[3], tag[7], tag[11] );
		}
		n++;
	}

	// torsos onto legs, heads onto torsos (a powerup shell is a second entity at the same place: it links too)
	for ( i = 0; i < n; i++ ) {
		ozPart_t *child = &parts[i];
		int want = child->part == OZ_RIG_UPPER ? OZ_RIG_LOWER : child->part == OZ_RIG_HEAD ? OZ_RIG_UPPER : -1;

		if ( want < 0 ) {
			continue;
		}
		for ( j = 0; j < n; j++ ) {
			ozPart_t *parent = &parts[j];
			if ( parent->part != want || parent->rig != child->rig ||
				 fabs( parent->childAt[0] - child->ent->e.origin[0] ) > OZ_RIG_LINK ||
				 fabs( parent->childAt[1] - child->ent->e.origin[1] ) > OZ_RIG_LINK ||
				 fabs( parent->childAt[2] - child->ent->e.origin[2] ) > OZ_RIG_LINK ) {
				continue;
			}
			if ( want == OZ_RIG_LOWER ) {
				if ( child->lower < 0 ) child->lower = j;
				if ( parent->upper < 0 ) parent->upper = i;
			} else {
				if ( child->upper < 0 ) child->upper = j;
				if ( parent->head < 0 ) parent->head = i;
			}
		}
	}

	if ( world ) {
		for ( i = 0; i < n; i++ ) {
			rigStats.parts++;
			if ( parts[i].part == OZ_RIG_UPPER ) {
				rigStats.torsos++;
				rigStats.loneTorsos += parts[i].lower < 0;
			} else if ( parts[i].part == OZ_RIG_HEAD ) {
				rigStats.heads++;
				rigStats.loneHeads += parts[i].upper < 0;
			}
		}
	}

	// which part each joint group follows; a missing part's joints come from the nearest part there is
	for ( i = 0; i < n; i++ ) {
		ozPart_t *p = &parts[i];
		int *src = p->src;

		src[OZ_RIG_LOWER] = p->part == OZ_RIG_LOWER ? i : p->part == OZ_RIG_UPPER ? p->lower : -1;
		src[OZ_RIG_UPPER] = p->part == OZ_RIG_UPPER ? i : p->upper;
		src[OZ_RIG_HEAD] = p->part == OZ_RIG_HEAD ? i : p->part == OZ_RIG_UPPER ? p->head : -1;
		if ( p->part == OZ_RIG_LOWER && src[OZ_RIG_UPPER] >= 0 ) {
			src[OZ_RIG_HEAD] = parts[src[OZ_RIG_UPPER]].head;
		}
		if ( p->part == OZ_RIG_HEAD && src[OZ_RIG_UPPER] >= 0 ) {
			src[OZ_RIG_LOWER] = parts[src[OZ_RIG_UPPER]].lower;
		}
		if ( src[OZ_RIG_UPPER] < 0 ) src[OZ_RIG_UPPER] = src[OZ_RIG_LOWER] >= 0 ? src[OZ_RIG_LOWER] : src[OZ_RIG_HEAD];
		if ( src[OZ_RIG_LOWER] < 0 ) src[OZ_RIG_LOWER] = src[OZ_RIG_UPPER];
		if ( src[OZ_RIG_HEAD] < 0 ) src[OZ_RIG_HEAD] = src[OZ_RIG_UPPER];
	}

	// each fighter's skeleton in the world, posed once by its legs part (or by a part drawn without legs)
	for ( i = 0; i < n; i++ ) {
		ozPart_t *p = &parts[i];
		const iqmData_t *data = p->rig->data;
		const float *srcJoints[3];
		float base[3][12];

		if ( p->part != OZ_RIG_LOWER && parts[p->src[OZ_RIG_LOWER]].part == OZ_RIG_LOWER ) {
			continue;		// the legs pose it
		}
		p->skeleton = PoolAlloc( data->num_joints * 12 );
		if ( !p->skeleton ) {
			continue;
		}
		for ( s = 0; s < 3; s++ ) {
			const ozPart_t *sp = &parts[p->src[s]];
			// the head has no frames of its own: its joints are the torso's, hung from its own entity
			srcJoints[s] = ( sp->part == OZ_RIG_HEAD && sp->upper >= 0 ) ? parts[sp->upper].joints : sp->joints;
			PartBase( sp, srcJoints[s], base[s] );
		}
		for ( j = 0; j < data->num_joints; j++ ) {
			s = p->rig->jointPart[j] == 'h' ? OZ_RIG_HEAD : p->rig->jointPart[j] == 'u' ? OZ_RIG_UPPER : OZ_RIG_LOWER;
			R_OzMat34Multiply( base[s], srcJoints[s] + 12 * j, p->skeleton + 12 * j );
		}
		if ( p->part == OZ_RIG_LOWER && p->upper >= 0 ) {
			SpreadTorsoTurn( p, &parts[p->upper] );
		}
		if ( world && p->part == OZ_RIG_LOWER && p->rig->numBodies ) {
			const float *rag = R_OzRagdollFighter( p->rig, p->ent, p->skeleton, &p->dissolve );
			if ( rag ) {
				Com_Memcpy( p->skeleton, rag, data->num_joints * 12 * sizeof( float ) );
			}
		}
	}

	// every part: the skeleton relative to its own entity, times the inverse bind pose
	for ( i = 0; i < n; i++ ) {
		ozPart_t *p = &parts[i];
		const iqmData_t *data = p->rig->data;
		const ozPart_t *owner = p->skeleton ? p : &parts[p->src[OZ_RIG_LOWER]];
		float inv[12], m[12], *bones;

		if ( !owner->skeleton ) {
			continue;
		}
		bones = PoolAlloc( data->num_joints * 12 );
		if ( !bones ) {
			continue;
		}
		R_OzMat34InvertRigid( p->world, inv );
		for ( j = 0; j < data->num_joints; j++ ) {
			R_OzMat34Multiply( inv, owner->skeleton + 12 * j, m );
			R_OzMat34Multiply( m, data->invBindJoints + 12 * j, bones + 12 * j );
		}
		p->ent->ozBones = bones;
		p->ent->ozDissolve = owner->dissolve;
		j = p->rig->numBodies ? p->rig->bodies[0].joint : 0;
		VectorSet( p->ent->ozCenter, owner->skeleton[12 * j + 3], owner->skeleton[12 * j + 7], owner->skeleton[12 * j + 11] );
	}
}

static int FogNum( const vec3_t at ) {
	const fog_t *fog;
	int i, j;

	if ( tr.refdef.rdflags & RDF_NOWORLDMODEL ) {
		return 0;
	}
	for ( i = 1; i < tr.world->numfogs; i++ ) {
		fog = &tr.world->fogs[i];
		for ( j = 0; j < 3; j++ ) {
			if ( at[j] - OZ_RIG_FOG_RADIUS >= fog->bounds[1][j] ||
				 at[j] + OZ_RIG_FOG_RADIUS <= fog->bounds[0][j] ) {
				break;
			}
		}
		if ( j == 3 ) {
			return i;
		}
	}
	return 0;
}

/*
=================
R_AddOzRigSurfaces

A fighter part's share of the rig: the meshes of its part, with the shaders its skin gives them.
=================
*/
void R_AddOzRigSurfaces( trRefEntity_t *ent ) {
	const ozRig_t *rig = tr.currentModel->ozRig;
	iqmData_t *data = rig->data;
	srfIQModel_t *surface = data->surfaces;
	char part = "luh"[tr.currentModel->ozRigPart];
	qboolean personalModel;
	int i, j, fogNum, cubemapIndex;

	if ( ent->ozDissolve >= 1.0f ) {
		return;		// burned away
	}
	personalModel = ( ent->e.renderfx & RF_THIRD_PERSON ) && !( tr.viewParms.isPortal ||
					( tr.viewParms.flags & ( VPF_SHADOWMAP | VPF_DEPTHSHADOW ) ) );

	if ( R_CullPointAndRadius( ent->ozCenter, OZ_RIG_CULL_RADIUS ) == CULL_OUT ) {
		return;
	}
	if ( !personalModel || r_shadows->integer > 1 ) {
		R_SetupEntityLighting( &tr.refdef, ent );
	}
	fogNum = FogNum( ent->ozCenter );
	cubemapIndex = R_CubemapForPoint( ent->ozCenter );

	for ( i = 0; i < data->num_surfaces; i++, surface++ ) {
		shader_t *shader = surface->shader;
		void *drawSurf;

		if ( rig->meshPart[i] != part ) {
			continue;
		}
		if ( ent->e.customShader ) {
			shader = R_GetShaderByHandle( ent->e.customShader );
		} else if ( ent->e.customSkin > 0 && ent->e.customSkin < tr.numSkins ) {
			const skin_t *skin = R_GetSkinByHandle( ent->e.customSkin );
			for ( j = 0; j < skin->numSurfaces; j++ ) {
				if ( !strcmp( skin->surfaces[j].name, surface->name ) ) {
					shader = skin->surfaces[j].shader;
					break;
				}
			}
		}
		drawSurf = data->numVaoSurfaces ? (void *)&data->vaoSurfaces[i] : (void *)surface;

		if ( r_shadows->integer == 3 && fogNum == 0 && ( ent->e.renderfx & RF_SHADOW_PLANE ) &&
			 shader->sort == SS_OPAQUE ) {
			R_AddDrawSurf( drawSurf, tr.projectionShadowShader, 0, 0, 0, 0 );
		}
		if ( !personalModel ) {
			R_AddDrawSurf( drawSurf, shader, fogNum, 0, 0, cubemapIndex );
		}
	}
}

/*
=================
R_OzRigLerpTag

tag_torso of the legs, tag_head and tag_weapon of the torso, from the rig's tag joints; qfalse for any other
tag (the md3 answers those).
=================
*/
qboolean R_OzRigLerpTag( orientation_t *tag, const model_t *mod, int startFrame, int endFrame,
						 float frac, const char *tagName ) {
	const ozRig_t *rig = mod->ozRig;
	float joints[IQM_MAX_JOINTS * 12], inv[12], m[12];
	int first, count, t, r;

	if ( mod->ozRigPart == OZ_RIG_LOWER && !Q_stricmp( tagName, "tag_torso" ) ) {
		t = rig->tagTorso;
	} else if ( mod->ozRigPart == OZ_RIG_UPPER && !Q_stricmp( tagName, "tag_head" ) ) {
		t = rig->tagHead;
	} else if ( mod->ozRigPart == OZ_RIG_UPPER && !Q_stricmp( tagName, "tag_weapon" ) ) {
		t = rig->tagWeapon;
	} else {
		return qfalse;
	}
	first = mod->ozRigPart == OZ_RIG_LOWER ? 0 : rig->upperFirst;
	count = mod->ozRigPart == OZ_RIG_LOWER ? rig->upperFirst : rig->data->num_frames - rig->upperFirst;
	// asked as the scene poses the part (its frame, the one it comes from, how far back), so both share the pose
	// R_IQMJointMats keeps for the frame
	R_IQMJointMats( rig->data, first + ClampFrame( endFrame, count ), first + ClampFrame( startFrame, count ),
					1.0f - frac, joints );
	if ( mod->ozRigPart == OZ_RIG_UPPER ) {
		R_OzMat34InvertRigid( joints + 12 * rig->tagTorso, inv );
		R_OzMat34Multiply( inv, joints + 12 * t, m );
	} else {
		Com_Memcpy( m, joints + 12 * t, sizeof( m ) );
	}
	for ( r = 0; r < 3; r++ ) {
		tag->axis[0][r] = m[4 * r + 0];
		tag->axis[1][r] = m[4 * r + 1];
		tag->axis[2][r] = m[4 * r + 2];
		tag->origin[r] = m[4 * r + 3];
	}
	return qtrue;
}
