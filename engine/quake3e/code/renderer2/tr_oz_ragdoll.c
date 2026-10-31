/*
===========================================================================
Copyright (C) 2026 Oz Arena contributors

This file is part of Oz Arena, built on the Quake III Arena source code.

Oz Arena is free software; you can redistribute it and/or modify it under the terms of the GNU General
Public License as published by the Free Software Foundation; either version 2 of the License, or (at your
option) any later version.
===========================================================================
*/
// tr_oz_ragdoll.c -- the fighters' ragdolls, under any cgame (ENGINE_PLAN.md P2)
//
// The renderer follows each fighter from frame to frame by where its legs are. When the legs start a death
// (a BOTH_DEATH or BOTH_DEAD frame), a ragdoll takes the skeleton over, as the Godot build's did: rigid
// capsules on the bones that carry the skin, joined by cones and hinges (tools/make_fighters.py's plan),
// simulated by Jolt Physics (engine/src/oz/physics) at 120 Hz against the map's solids around the body
// (cm_oz.c), the scene's movers (a lift carries a body lying on it) and the other bodies; a body's parts
// collide with each other too. Each part starts with the speed its bone had in the last frames alive,
// against the body, plus the speed of the server's corpse, which carries the run, the jump and the killing
// hit's knockback (it is in the snapshot, ri.OzBodyVelocity; the cgame only eases the drawn body toward it).
// Past the Godot build, the joints have muscle tone through the fall (r_ozRagdollTone, after Lincoln
// Margison's "Improving ragdolls forever"): a weak pull toward the neutral posture that stiffens before each
// limit, fading once the body lies. It is client-side and cosmetic: the server's corpse goes on as before.
//
// The burn: when the server sinks the corpse (it does before removing it) or after r_ozCorpseBurn seconds
// (10), the body burns away over 1.6 seconds (the entity's ozDissolve). r_ozRagdollTest 1 (a cheat) drops
// every fighter into its ragdoll, for tests.

#include "tr_oz_rig.h"
#ifdef USE_OZ_PHYSICS
#include "oz_physics.h"

#define OZ_RAGDOLL_TRACKS		64
#define OZ_RAGDOLL_STEP			( 1000.0f / 120.0f )	// ms: 120 Hz, as the Godot build's physics
#define OZ_RAGDOLL_MAX_STEPS	30			// a slow frame catches up at most 250 ms
#define OZ_RAGDOLL_MATCH		64.0f		// legs move less than this between two frames
#define OZ_RAGDOLL_FORGET		1000		// ms unseen before a track is dropped
#define OZ_RAGDOLL_SPEED		900.0f		// the fastest a part starts, units/s
#define OZ_RAGDOLL_MOVERS		128			// movers in one scene
#define OZ_RAGDOLL_SINK_RATE	40.0f		// the server sinks a corpse slower than this, units/s
#define OZ_RAGDOLL_SINK_SPAN	200			// ms its height is watched over (it sinks a unit each 100 ms)
#define OZ_RAGDOLL_CRUSH		16			// ms a mover has a body in it before it's crushed (three frames at 125 fps)
#define OZ_RAGDOLL_BURN_TIME	1600		// ms
#define OZ_RAGDOLL_MASK			( CONTENTS_SOLID | CONTENTS_PLAYERCLIP )
#define OZ_RAGDOLL_METER		32.0f		// id units: the physics works in meters
#define OZ_RAGDOLL_NEAR			192.0f		// the map's solids this near a body are in the physics...
#define OZ_RAGDOLL_REGATHER		64.0f		// ...gathered again when it is this far from where they last were
#define OZ_RAGDOLL_GAP			4.0f		// units a joint parts before the body counts as pulled apart
#define OZ_RAGDOLL_FLUNG		16.0f		// a part this much farther from the pelvis than the rig reaches flew off
#define OZ_RAGDOLL_HULL			64			// corners of one solid at most
#define OZ_RAGDOLL_ZONE			20.0f		// degrees before each limit the muscle tone stiffens over
#define OZ_RAGDOLL_LIMP			1500		// ms after the fall the muscle tone starts fading, and how long it takes:
											// lying, a body goes limp (with its motors pressing its limbs to the floor,
											// it crept across a moving platform)

typedef struct {
	const ozRig_t *rig;
	qboolean	active;
	int			seen;			// refdef time last seen
	int			scene;			// the scene that last updated it
	vec3_t		anchor;			// the legs entity's origin when last seen
	qboolean	ragdoll;

	// alive: the bodies' middles and the legs in the last two frames, for the speed a body starts with
	vec3_t		last[OZ_RAGDOLL_BODIES], before[OZ_RAGDOLL_BODIES];
	vec3_t		lastAnchor, beforeAnchor;
	int			lastTime, beforeTime;

	// the ragdoll
	int			phys;			// its ragdoll in the physics, or -1
	float		place[OZ_RAGDOLL_BODIES][12];	// each body's frame now (3x4, units)
	float		startJoints[IQM_MAX_JOINTS * 12];		// the skeleton when it fell
	float		joints[IQM_MAX_JOINTS * 12];			// and now
	int			startTime, burnStart;
	float		lyingZ, sinkZ;	// the corpse's height lying still, and at sinkTime
	int			sinkTime;		// when its fall or sinking was last measured
	vec3_t		gathered;		// where the map's solids around it were last gathered
	float		point1[OZ_RAGDOLL_BODIES][3];	// each joint's point in its parent's space, as it was made (meters)
	float		gap;			// the most a joint parted, units
	int			gapAt;			// ms after the fall that was
	int			gapBody;		// and the body whose joint it was
	qboolean	flung;			// a part flew off
	float		startSpeed;		// the body's speed when it fell
	vec3_t		startAt;		// and where
	int			solidAt;		// a body in a wall when last drawn, or -1
	int			crushSince;		// since when a mover has had a body in it (0: none has)
	qboolean	crushed;
} ozTrack_t;

// a mover in the scene: an inline model ("*N") the cgame placed (it smooths their movement)
typedef struct {
	int				index;
	clipHandle_t	clip;
	vec3_t			origin, angles;
	float			rotation[4];
	vec3_t			mins, maxs;		// its bounds in the world, rotation allowed for
} ozMover_t;

static ozTrack_t	tracks[OZ_RAGDOLL_TRACKS];
static int			sceneCount, lastSceneTime;
static float		physTime;		// ms the physics has stepped to
static qboolean		physUp;
static ozMover_t	movers[OZ_RAGDOLL_MOVERS], wasMovers[OZ_RAGDOLL_MOVERS];
static int			numMovers, numWasMovers;

static struct {
	int		started, ended, inSolid, exploded, active, problems, carried;
	float	stretch;
	char	last[4][256];		// the latest ragdolls that ended in a wall or pulled apart, for ozragdolls
	char	lastStart[192];		// the latest ragdoll's start: where, how fast, and the hit's push
} stats;

static void JointOrigin( const float *joints, int j, vec3_t out ) {
	VectorSet( out, joints[12 * j + 3], joints[12 * j + 7], joints[12 * j + 11] );
}

// a 3x4 frame's rotation as a quaternion (x y z w), and back with a translation
static void MatrixQuat( const float *m, float *q ) {
	const float tr = m[0] + m[5] + m[10];
	float s;

	if ( tr > 0.0f ) {
		s = sqrt( tr + 1.0f ) * 2.0f;
		q[3] = 0.25f * s; q[0] = ( m[9] - m[6] ) / s; q[1] = ( m[2] - m[8] ) / s; q[2] = ( m[4] - m[1] ) / s;
	} else if ( m[0] > m[5] && m[0] > m[10] ) {
		s = sqrt( 1.0f + m[0] - m[5] - m[10] ) * 2.0f;
		q[3] = ( m[9] - m[6] ) / s; q[0] = 0.25f * s; q[1] = ( m[1] + m[4] ) / s; q[2] = ( m[2] + m[8] ) / s;
	} else if ( m[5] > m[10] ) {
		s = sqrt( 1.0f + m[5] - m[0] - m[10] ) * 2.0f;
		q[3] = ( m[2] - m[8] ) / s; q[0] = ( m[1] + m[4] ) / s; q[1] = 0.25f * s; q[2] = ( m[6] + m[9] ) / s;
	} else {
		s = sqrt( 1.0f + m[10] - m[0] - m[5] ) * 2.0f;
		q[3] = ( m[4] - m[1] ) / s; q[0] = ( m[2] + m[8] ) / s; q[1] = ( m[6] + m[9] ) / s; q[2] = 0.25f * s;
	}
}

static void QuatMatrix( const float *q, const vec3_t t, float *m ) {
	const float x = q[0], y = q[1], z = q[2], w = q[3];

	m[0] = 1.0f - 2.0f * ( y * y + z * z ); m[1] = 2.0f * ( x * y - z * w ); m[2] = 2.0f * ( x * z + y * w ); m[3] = t[0];
	m[4] = 2.0f * ( x * y + z * w ); m[5] = 1.0f - 2.0f * ( x * x + z * z ); m[6] = 2.0f * ( y * z - x * w ); m[7] = t[1];
	m[8] = 2.0f * ( x * z - y * w ); m[9] = 2.0f * ( y * z + x * w ); m[10] = 1.0f - 2.0f * ( x * x + y * y ); m[11] = t[2];
}

// a frame's rotation applied to a direction, and its inverse
static void Rotate( const float *m, const float *v, float *out ) {
	out[0] = m[0] * v[0] + m[1] * v[1] + m[2] * v[2];
	out[1] = m[4] * v[0] + m[5] * v[1] + m[6] * v[2];
	out[2] = m[8] * v[0] + m[9] * v[1] + m[10] * v[2];
}

static void Unrotate( const float *m, const float *v, float *out ) {
	out[0] = m[0] * v[0] + m[4] * v[1] + m[8] * v[2];
	out[1] = m[1] * v[0] + m[5] * v[1] + m[9] * v[2];
	out[2] = m[2] * v[0] + m[6] * v[1] + m[10] * v[2];
}

// a point of a body (meters, in its space) in the world (units)
static void BodyPoint( const float *place, const float *local, vec3_t out ) {
	vec3_t p;
	VectorScale( local, OZ_RAGDOLL_METER, p );
	Rotate( place, p, out );
	out[0] += place[3]; out[1] += place[7]; out[2] += place[11];
}

// the ends of a body's capsule's straight part, where it is now
static void Segment( const ozBody_t *b, const float *place, vec3_t a, vec3_t z ) {
	const float h = MAX( b->height * 0.5f - b->radius, 0.0f );
	VectorSet( a, place[3] - place[1] * h, place[7] - place[5] * h, place[11] - place[9] * h );
	VectorSet( z, place[3] + place[1] * h, place[7] + place[5] * h, place[11] + place[9] * h );
}

// the closest two segments come (Ericson's ClosestPtSegmentSegment)
static float SegmentsApart( const vec3_t p1, const vec3_t q1, const vec3_t p2, const vec3_t q2 ) {
	vec3_t d1, d2, r, c1, c2;
	float a, e, f, s = 0.0f, t = 0.0f;

	VectorSubtract( q1, p1, d1 );
	VectorSubtract( q2, p2, d2 );
	VectorSubtract( p1, p2, r );
	a = DotProduct( d1, d1 );
	e = DotProduct( d2, d2 );
	f = DotProduct( d2, r );
	if ( a > 1e-6f || e > 1e-6f ) {
		if ( a <= 1e-6f ) {
			t = f / e;
		} else {
			const float c = DotProduct( d1, r );
			if ( e <= 1e-6f ) {
				s = -c / a;
			} else {
				const float b = DotProduct( d1, d2 ), den = a * e - b * b;
				s = den > 1e-6f ? ( b * f - c * e ) / den : 0.0f;
				s = s < 0.0f ? 0.0f : s > 1.0f ? 1.0f : s;
				t = ( b * s + f ) / e;
				if ( t < 0.0f ) {
					t = 0.0f;
					s = -c / a;
				} else if ( t > 1.0f ) {
					t = 1.0f;
					s = ( b - c ) / a;
				}
			}
			s = s < 0.0f ? 0.0f : s > 1.0f ? 1.0f : s;
		}
		t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
	}
	VectorMA( p1, s, d1, c1 );
	VectorMA( p2, t, d2, c2 );
	return Distance( c1, c2 );
}

/*
=================
The map's solids, for the physics
=================
*/

// a convex solid's corners, from its planes (out of it): where three planes cross inside all the others, in
// meters
static int HullPoints( const float *planes, int numPlanes, float *points ) {
	int i, j, k, m, n = 0;

	for ( i = 0; i < numPlanes; i++ ) {
		for ( j = i + 1; j < numPlanes; j++ ) {
			for ( k = j + 1; k < numPlanes && n < OZ_RAGDOLL_HULL; k++ ) {
				const float *a = planes + 4 * i, *b = planes + 4 * j, *c = planes + 4 * k;
				vec3_t bc, ca, ab, p;
				float det;

				CrossProduct( b, c, bc );
				det = DotProduct( a, bc );
				if ( fabs( det ) < 1e-5f ) {
					continue;
				}
				CrossProduct( c, a, ca );
				CrossProduct( a, b, ab );
				for ( m = 0; m < 3; m++ ) {
					p[m] = ( a[3] * bc[m] + b[3] * ca[m] + c[3] * ab[m] ) / det;
				}
				for ( m = 0; m < numPlanes && DotProduct( planes + 4 * m, p ) - planes[4 * m + 3] < 0.05f; m++ ) {
				}
				if ( m < numPlanes ) {
					continue;
				}
				VectorScale( p, 1.0f / OZ_RAGDOLL_METER, p );
				for ( m = 0; m < n && DistanceSquared( points + 3 * m, p ) > 1e-6f; m++ ) {
				}
				if ( m == n ) {
					VectorCopy( p, points + 3 * n );		// a macro: n counts up after it
					n++;
				}
			}
		}
	}
	return n;
}

static void AddSolid( void *ctx, int id, const float *planes, int numPlanes ) {
	float points[OZ_RAGDOLL_HULL * 3];

	if ( !OzPhys_HasSolid( id ) ) {
		OzPhys_AddSolid( id, points, HullPoints( planes, numPlanes, points ) );
	}
}

// the map's solids around a body, once it has moved on from where they last were
static void Gather( ozTrack_t *t, qboolean now ) {
	vec3_t at, mins, maxs;
	int i;

	VectorSet( at, t->place[0][3], t->place[0][7], t->place[0][11] );
	if ( !now && Distance( at, t->gathered ) < OZ_RAGDOLL_REGATHER ) {
		return;
	}
	VectorCopy( at, t->gathered );
	for ( i = 0; i < 3; i++ ) {
		mins[i] = at[i] - OZ_RAGDOLL_NEAR;
		maxs[i] = at[i] + OZ_RAGDOLL_NEAR;
	}
	ri.CM_OzSolids( 0, mins, maxs, OZ_RAGDOLL_MASK, AddSolid, NULL );
}

typedef struct {
	int		numHulls, numPoints;
	int		counts[512];
	float	points[512 * OZ_RAGDOLL_HULL * 3];
} ozHulls_t;

static void AddMoverHull( void *ctx, int id, const float *planes, int numPlanes ) {
	ozHulls_t *h = ctx;

	if ( h->numHulls < (int)ARRAY_LEN( h->counts ) ) {
		h->counts[h->numHulls] = HullPoints( planes, numPlanes, h->points + 3 * h->numPoints );
		h->numPoints += h->counts[h->numHulls++];
	}
}

// the angles an axis was made from (AnglesToAxis's inverse; Q3's axis[1] points left)
static void AxisAngles( const vec3_t axis[3], vec3_t angles ) {
	angles[PITCH] = RAD2DEG( asin( -axis[0][2] < -1.0f ? -1.0f : -axis[0][2] > 1.0f ? 1.0f : -axis[0][2] ) );
	angles[YAW] = RAD2DEG( atan2( axis[0][1], axis[0][0] ) );
	angles[ROLL] = RAD2DEG( atan2( axis[1][2], axis[2][2] ) );
}

/*
=================
FindMovers

The scene's movers: the cgame's entities drawn with an inline model, and where it put them. The physics gets
each one's solids the first time it is seen, as one body it moves to where the scene draws it.
=================
*/
static void FindMovers( void ) {
	static ozHulls_t hulls;
	int i, j, count = ri.CM_NumInlineModels ? ri.CM_NumInlineModels() : 0;

	numMovers = 0;
	for ( i = 0; i < tr.refdef.num_entities && numMovers < OZ_RAGDOLL_MOVERS; i++ ) {
		const trRefEntity_t *ent = &tr.refdef.entities[i];
		const model_t *mod;
		ozMover_t *m;
		float frame[12], radius;
		int index;

		if ( ent->e.reType != RT_MODEL ) {
			continue;
		}
		mod = R_GetModelByHandle( ent->e.hModel );
		if ( mod->type != MOD_BRUSH || !mod->bmodel || mod->name[0] != '*' ) {
			continue;
		}
		index = atoi( mod->name + 1 );
		if ( index <= 0 || index >= count ) {
			continue;
		}
		m = &movers[numMovers++];
		m->index = index;
		m->clip = ri.CM_InlineModel( index );
		VectorCopy( ent->e.origin, m->origin );
		for ( j = 0; j < 3; j++ ) {
			frame[4 * j + 0] = ent->e.axis[0][j];
			frame[4 * j + 1] = ent->e.axis[1][j];
			frame[4 * j + 2] = ent->e.axis[2][j];
		}
		MatrixQuat( frame, m->rotation );
		if ( ent->e.axis[0][0] == 1.0f && ent->e.axis[1][1] == 1.0f && ent->e.axis[2][2] == 1.0f ) {
			VectorClear( m->angles );
			VectorAdd( m->origin, mod->bmodel->bounds[0], m->mins );
			VectorAdd( m->origin, mod->bmodel->bounds[1], m->maxs );
		} else {
			AxisAngles( (const vec3_t *)ent->e.axis, m->angles );
			radius = RadiusFromBounds( mod->bmodel->bounds[0], mod->bmodel->bounds[1] );
			for ( j = 0; j < 3; j++ ) {
				m->mins[j] = m->origin[j] - radius;
				m->maxs[j] = m->origin[j] + radius;
			}
		}
		if ( physUp && !OzPhys_HasMover( index ) ) {
			hulls.numHulls = hulls.numPoints = 0;
			ri.CM_OzSolids( m->clip, NULL, NULL, OZ_RAGDOLL_MASK, AddMoverHull, &hulls );
			OzPhys_AddMover( index, hulls.numHulls, hulls.counts, hulls.points );
		}
	}
}

static int MoverContents( const vec3_t p ) {
	int i, c = 0;

	for ( i = 0; i < numMovers; i++ ) {
		const ozMover_t *m = &movers[i];
		if ( p[0] >= m->mins[0] && p[0] <= m->maxs[0] && p[1] >= m->mins[1] && p[1] <= m->maxs[1] &&
			 p[2] >= m->mins[2] && p[2] <= m->maxs[2] ) {
			c |= ri.CM_TransformedPointContents( p, m->clip, m->origin, m->angles );
		}
	}
	return c;
}

static void PhysPrint( const char *text ) {
	ri.Printf( PRINT_DEVELOPER, "physics: %s\n", text );
}

static void Problem( const ozTrack_t *t, const char *what ) {
	Com_sprintf( stats.last[stats.problems++ % 4], sizeof( stats.last[0] ), "%s: %s; fell at (%.0f %.0f %.0f) at %.0f "
				 "units/s, worst %d ms after%s", t->rig->name, what, t->startAt[0], t->startAt[1], t->startAt[2],
				 t->startSpeed, t->gapAt, OzPhys_RagdollCarried( t->phys ) ? ", on a mover" : "" );
}

// a ragdoll pulled apart: a joint parted more than OZ_RAGDOLL_GAP units, or a part flew off (farther from the
// pelvis than the rig reaches with its joints straight, and OZ_RAGDOLL_FLUNG units)
static qboolean PulledApart( const ozTrack_t *t ) {
	return t->gap > OZ_RAGDOLL_GAP || t->flung;
}

// a ragdoll ends: its stats, and its bodies out of the physics
static void EndRagdoll( ozTrack_t *t ) {
	char what[96];

	if ( t->ragdoll ) {
		stats.ended++;
		// as it was last drawn: since then a mover may have gone through where it lay
		if ( t->solidAt >= 0 ) {
			stats.inSolid++;
			Com_sprintf( what, sizeof( what ), "body %d (radius %.1f) ended in the world at (%.0f %.0f %.0f)", t->solidAt,
						 t->rig->bodies[t->solidAt].radius, t->place[t->solidAt][3], t->place[t->solidAt][7],
						 t->place[t->solidAt][11] );
			Problem( t, what );
		}
		if ( PulledApart( t ) ) {
			stats.exploded++;
			Com_sprintf( what, sizeof( what ), t->flung ? "a part flew off (a joint parted %.1f units, body %d's)" :
						 "pulled apart (a joint parted %.1f units, body %d's)", t->gap, t->gapBody );
			Problem( t, what );
		}
		stats.stretch = MAX( stats.stretch, 1.0f + t->gap / ( 2.0f * OZ_RAGDOLL_GAP ) );
		stats.carried += OzPhys_RagdollCarried( t->phys );
	}
	OzPhys_RemoveRagdoll( t->phys );
	t->phys = -1;
	t->ragdoll = qfalse;
}

static void EndTrack( ozTrack_t *t ) {
	EndRagdoll( t );
	t->active = qfalse;
}

static void Reset( void ) {
	int i;

	for ( i = 0; i < OZ_RAGDOLL_TRACKS; i++ ) {
		tracks[i].active = qfalse;
		tracks[i].phys = -1;
	}
	numMovers = numWasMovers = 0;
	if ( physUp ) {
		OzPhys_Clear();
	}
}

void R_OzRagdollNewMap( void ) {
	Reset();
}

void R_OzRagdollShutdown( void ) {
	Reset();
	if ( physUp ) {
		OzPhys_Shutdown();
		physUp = qfalse;
	}
}

// a body lying on a mover sleeps while the mover stands still; once it moves, the body wakes (else a lift
// going down left it hanging in the air)
static void WakeNearMovers( const ozTrack_t *t ) {
	int i, k;

	for ( i = 0; i < numMovers; i++ ) {
		const ozMover_t *m = &movers[i];
		for ( k = 0; k < numWasMovers && wasMovers[k].index != m->index; k++ ) {
		}
		if ( k == numWasMovers || ( VectorCompare( wasMovers[k].origin, m->origin ) &&
			 DotProduct4( wasMovers[k].rotation, m->rotation ) > 0.99999f ) ) {
			continue;
		}
		if ( t->place[0][3] > m->mins[0] - 64.0f && t->place[0][3] < m->maxs[0] + 64.0f &&
			 t->place[0][7] > m->mins[1] - 64.0f && t->place[0][7] < m->maxs[1] + 64.0f &&
			 t->place[0][11] > m->mins[2] - 64.0f && t->place[0][11] < m->maxs[2] + 64.0f ) {
			OzPhys_WakeRagdoll( t->phys );
			return;
		}
	}
}

// the physics steps up to the scene's time, the movers going from where the last scene had them to this one's
static void Step( int now ) {
	float gravity = ri.Cvar_VariableIntegerValue( "g_gravity" ) / OZ_RAGDOLL_METER;
	int i, s, steps, ragdolls = 0;

	for ( i = 0; i < OZ_RAGDOLL_TRACKS; i++ ) {
		if ( tracks[i].active && tracks[i].phys >= 0 ) {
			Gather( &tracks[i], qfalse );
			WakeNearMovers( &tracks[i] );
			ragdolls++;
		}
	}
	if ( !ragdolls || now < physTime ) {
		physTime = now;
	} else {
		steps = (int)( ( now - physTime ) / OZ_RAGDOLL_STEP );
		if ( steps > OZ_RAGDOLL_MAX_STEPS ) {
			physTime = now - OZ_RAGDOLL_MAX_STEPS * OZ_RAGDOLL_STEP;
			steps = OZ_RAGDOLL_MAX_STEPS;
		}
		if ( gravity <= 0.0f ) {
			gravity = 800.0f / OZ_RAGDOLL_METER;
		}
		for ( s = 1; s <= steps; s++ ) {
			const float f = (float)s / steps;
			for ( i = 0; i < numMovers; i++ ) {
				const ozMover_t *m = &movers[i], *was = m;
				vec3_t at;
				float q[4], d;
				int k;
				for ( k = 0; k < numWasMovers; k++ ) {
					if ( wasMovers[k].index == m->index ) {
						was = &wasMovers[k];
					}
				}
				d = DotProduct4( was->rotation, m->rotation ) < 0.0f ? -1.0f : 1.0f;
				for ( k = 0; k < 4; k++ ) {
					q[k] = was->rotation[k] * d * ( 1.0f - f ) + m->rotation[k] * f;
				}
				for ( k = 0; k < 3; k++ ) {
					at[k] = ( was->origin[k] + ( m->origin[k] - was->origin[k] ) * f ) / OZ_RAGDOLL_METER;
				}
				OzPhys_MoveMover( m->index, at, q, OZ_RAGDOLL_STEP * 0.001f );
			}
			OzPhys_Step( OZ_RAGDOLL_STEP * 0.001f, gravity );
		}
		physTime += steps * OZ_RAGDOLL_STEP;
	}
	Com_Memcpy( wasMovers, movers, sizeof( movers[0] ) * numMovers );
	numWasMovers = numMovers;
}

void R_OzRagdollBeginScene( void ) {
	int i;

	sceneCount++;
	if ( !physUp ) {
		OzPhys_Init( PhysPrint );
		physUp = qtrue;
	}
	if ( tr.refdef.time < lastSceneTime - 1000 ) {		// time went back: another map, a demo seek
		Reset();
	}
	lastSceneTime = tr.refdef.time;
	FindMovers();
	for ( i = 0; i < OZ_RAGDOLL_TRACKS; i++ ) {
		if ( tracks[i].active && tr.refdef.time - tracks[i].seen > OZ_RAGDOLL_FORGET ) {
			EndTrack( &tracks[i] );
		}
	}
	Step( tr.refdef.time );
}

void R_OzRagdollStats_f( void ) {
	int i;

	stats.active = 0;
	for ( i = 0; i < OZ_RAGDOLL_TRACKS; i++ ) {
		stats.active += tracks[i].active && tracks[i].ragdoll;
	}
	ri.Printf( PRINT_ALL, "ragdolls: %d started, %d ended, %d active, %d ended inside a wall, %d pulled apart, "
			   "most stretch %.2fx, %d carried by movers\n", stats.started, stats.ended, stats.active, stats.inSolid,
			   stats.exploded, stats.stretch, stats.carried );
	for ( i = 0; i < 4 && i < stats.problems; i++ ) {
		ri.Printf( PRINT_ALL, "  %s\n", stats.last[i] );
	}
	if ( stats.lastStart[0] ) {
		ri.Printf( PRINT_ALL, "  last start: %s\n", stats.lastStart );
	}
	for ( i = 0; i < OZ_RAGDOLL_TRACKS; i++ ) {
		const ozTrack_t *t = &tracks[i];
		if ( t->active && t->ragdoll ) {
			ri.Printf( PRINT_ALL, "  ragdoll %s: hips at (%.0f %.0f %.0f)%s\n", t->rig->name, t->place[0][3],
					   t->place[0][7], t->place[0][11], OzPhys_RagdollCarried( t->phys ) ? ", carried" : "" );
		}
	}
	for ( i = 0; i < numMovers; i++ ) {
		ri.Printf( PRINT_ALL, "  mover *%d at (%.0f %.0f %.0f)\n", movers[i].index, movers[i].origin[0], movers[i].origin[1],
				   movers[i].origin[2] );
	}
}

// the track of the fighter whose legs are at origin: alive fighters match alive tracks; a dead one its
// ragdoll, or the alive track it falls from
static ozTrack_t *FindTrack( const ozRig_t *rig, const vec3_t origin, qboolean dead ) {
	ozTrack_t *best = NULL, *free = NULL;
	float bestDist = OZ_RAGDOLL_MATCH * OZ_RAGDOLL_MATCH;
	int i, oldest = 0;

	for ( i = 0; i < OZ_RAGDOLL_TRACKS; i++ ) {
		ozTrack_t *t = &tracks[i];
		float d;

		if ( !t->active ) {
			if ( !free ) free = t;
			continue;
		}
		if ( t->seen < tracks[oldest].seen ) {
			oldest = i;
		}
		if ( t->rig != rig || ( t->ragdoll && !dead ) ) {
			continue;
		}
		d = DistanceSquared( t->anchor, origin );
		if ( t->ragdoll ) {
			d *= 0.25f;		// a corpse stays its own
		}
		if ( d < bestDist ) {
			best = t;
			bestDist = d;
		}
	}
	if ( best ) {
		return best;
	}
	if ( !free ) {
		free = &tracks[oldest];
		EndTrack( free );
	}
	Com_Memset( free, 0, sizeof( *free ) );
	free->active = qtrue;
	free->rig = rig;
	free->phys = -1;
	free->solidAt = -1;
	free->seen = tr.refdef.time;
	VectorCopy( origin, free->anchor );
	return free;
}

// each body's frame as the skeleton poses it (a capsule rides on its bone)
static void Places( const ozRig_t *rig, const float *world, float (*place)[12] ) {
	float skin[12];
	int i;

	for ( i = 0; i < rig->numBodies; i++ ) {
		const ozBody_t *b = &rig->bodies[i];
		R_OzMat34Multiply( world + 12 * b->joint, rig->data->invBindJoints + 12 * b->joint, skin );
		R_OzMat34Multiply( skin, b->place, place[i] );
	}
}

// whether a body's capsules (at their ends and middles, as boxes) are clear of the world, moved by shift
static qboolean Clear( const ozRig_t *rig, float (*place)[12], const vec3_t shift ) {
	int i, k;

	for ( i = 0; i < rig->numBodies; i++ ) {
		const float r = rig->bodies[i].radius * 0.7f;
		vec3_t a, b, at, mins, maxs;
		trace_t tr;

		Segment( &rig->bodies[i], place[i], a, b );
		VectorSet( mins, -r, -r, -r );
		VectorSet( maxs, r, r, r );
		for ( k = 0; k < 3; k++ ) {
			VectorAdd( a, b, at );
			VectorScale( at, 0.5f, at );
			if ( k != 1 ) {
				VectorCopy( k ? b : a, at );
			}
			VectorAdd( at, shift, at );
			ri.CM_BoxTrace( &tr, at, at, mins, maxs, 0, OZ_RAGDOLL_MASK, qfalse );
			if ( tr.startsolid ) {
				return qfalse;
			}
		}
	}
	return qtrue;
}

/*
=================
StartJoint

A joint as the dying pose has it, which the solver would otherwise yank into place on the first steps (the
rigs' bones stretch in the animations, so a dying pose rarely keeps a joint's two halves where they met
standing; yanked, thin parts went through the floor): its point where the part's bone puts it now, in the
parent's space too; a cone the pose already swings past its limit (a gun arm hanging straight off the spine)
made in that pose, as the Godot build did; a hinge's axis on the parent's as it lies now, and a bend past its
limits taken as the nearest it allows.
=================
*/
static void StartJoint( const ozBody_t *b, const float *up, const float *own, ozPhysBody_t *d ) {
	vec3_t at, rel, h1, n1, n2, w, x;

	BodyPoint( own, b->point2, at );
	VectorSet( rel, at[0] - up[3], at[1] - up[7], at[2] - up[11] );
	Unrotate( up, rel, d->point1 );
	VectorScale( d->point1, 1.0f / OZ_RAGDOLL_METER, d->point1 );
	Rotate( up, b->axis1, h1 );
	Rotate( up, b->normal1, n1 );
	if ( b->kind == 1 ) {
		Rotate( own, b->axis2, w );
		if ( acos( Com_Clamp( -1.0f, 1.0f, DotProduct( h1, w ) ) ) > b->swingLimit ) {
			Unrotate( own, h1, d->axis2 );
			Unrotate( own, n1, d->normal2 );
		}
	} else if ( b->kind == 2 ) {
		float angle, c, s;
		int k;

		Rotate( own, b->normal2, n2 );
		VectorMA( n2, -DotProduct( n2, h1 ), h1, n2 );
		VectorNormalize( n2 );
		CrossProduct( n1, n2, x );
		angle = atan2( DotProduct( h1, x ), DotProduct( n1, n2 ) );
		angle = Com_Clamp( b->twistMin, b->twistMax, angle );
		// the parent's normal turned by that angle about the hinge axis: where the part's normal lies
		CrossProduct( h1, n1, x );
		c = cos( angle );
		s = sin( angle );
		for ( k = 0; k < 3; k++ ) {
			w[k] = n1[k] * c + x[k] * s;
		}
		Unrotate( own, h1, d->axis2 );
		Unrotate( own, w, d->normal2 );
	}
}

// cl_ozProbe (engine/tests/frame_rate_test.py): a body starts to burn away, and why
static void NoteBurn( const ozTrack_t *t, int now, const char *why ) {
	if ( ri.Cvar_VariableIntegerValue( "cl_ozProbe" ) ) {
		ri.Printf( PRINT_ALL, "ozburn %d %s\n", now - t->startTime, why );
	}
}

static void Start( ozTrack_t *t, const trRefEntity_t *legs, const float *world, int now ) {
	const ozRig_t *rig = t->rig;
	float dt = ( t->lastTime - t->beforeTime ) * 0.001f, speed;
	qboolean moving = t->beforeTime && dt > 0.0f && dt < 0.2f && now - t->lastTime < 200;
	ozPhysBody_t desc[OZ_RAGDOLL_BODIES];
	unsigned char apart[OZ_RAGDOLL_BODIES * OZ_RAGDOLL_BODIES];
	vec3_t drawn, body, push, mean, shift = { 0.0f, 0.0f, 0.0f };
	int i, k;

	// the body as drawn moved with the legs; the server's corpse moves with the knockback too (where the
	// snapshot has a speed for it: a mod may send none, and then the drawn movement stays)
	VectorClear( drawn );
	if ( moving ) {
		VectorSubtract( t->lastAnchor, t->beforeAnchor, drawn );
		VectorScale( drawn, 1.0f / dt, drawn );
	}
	if ( !ri.OzBodyVelocity || !ri.OzBodyVelocity( legs->e.origin, body ) ) {
		VectorCopy( drawn, body );
	}
	VectorSubtract( body, drawn, push );
	t->startSpeed = VectorLength( body );
	VectorCopy( legs->e.origin, t->startAt );
	Places( rig, world, t->place );

	// a body first seen already lying (it fell out of sight, or in the eyes of the one you followed): its
	// pose dips into the floor, or into the wall it died against (id's dead models clip through them), so it
	// starts the least way clear of them, up to 24 units up or aside
	if ( !moving && !Clear( rig, t->place, shift ) ) {
		static const float ways[9][3] = { { 0, 0, 1 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 },
										  { 0.7071f, 0.7071f, 0 }, { 0.7071f, -0.7071f, 0 }, { -0.7071f, 0.7071f, 0 },
										  { -0.7071f, -0.7071f, 0 } };
		float d;
		for ( d = 2.0f; d <= 24.0f; d += 2.0f ) {
			for ( k = 0; k < 9; k++ ) {
				VectorScale( ways[k], d, shift );
				if ( Clear( rig, t->place, shift ) ) {
					break;
				}
			}
			if ( k < 9 ) {
				break;
			}
		}
		if ( d > 24.0f ) {
			VectorClear( shift );
		}
		for ( i = 0; i < rig->numBodies; i++ ) {
			t->place[i][3] += shift[0];
			t->place[i][7] += shift[1];
			t->place[i][11] += shift[2];
		}
	}
	// a part the dying pose put into a wall (an arm through it, a lying corpse's limb in the floor) starts
	// where the line from its parent's middle meets the wall (parents come first, so that one is clear)
	for ( i = 1; i < rig->numBodies; i++ ) {
		const ozBody_t *b = &rig->bodies[i];
		const float r = b->radius * 0.7f;
		vec3_t from, to, mins, maxs;
		trace_t tr;

		VectorSet( from, t->place[b->parent][3], t->place[b->parent][7], t->place[b->parent][11] );
		VectorSet( to, t->place[i][3], t->place[i][7], t->place[i][11] );
		if ( !( ri.CM_PointContents( to, 0 ) & OZ_RAGDOLL_MASK ) ) {
			continue;
		}
		VectorSet( mins, -r, -r, -r );
		VectorSet( maxs, r, r, r );
		ri.CM_BoxTrace( &tr, from, to, mins, maxs, 0, OZ_RAGDOLL_MASK, qfalse );
		if ( !tr.startsolid ) {
			t->place[i][3] += tr.endpos[0] - to[0];
			t->place[i][7] += tr.endpos[1] - to[1];
			t->place[i][11] += tr.endpos[2] - to[2];
		}
	}
	// each part keeps the speed its bone had against the body alive (between the last two frames before the
	// death pose, which itself would read as a jump), on top of the body's
	VectorClear( mean );
	for ( i = 0; i < rig->numBodies; i++ ) {
		const ozBody_t *b = &rig->bodies[i];
		ozPhysBody_t *d = &desc[i];
		vec3_t vel;
		float length;

		VectorClear( vel );
		if ( moving ) {
			VectorSubtract( t->last[i], t->before[i], vel );
			VectorScale( vel, 1.0f / dt, vel );
			VectorSubtract( vel, drawn, vel );
		}
		VectorAdd( vel, body, vel );
		speed = VectorLength( vel );
		if ( speed > OZ_RAGDOLL_SPEED ) {
			VectorScale( vel, OZ_RAGDOLL_SPEED / speed, vel );
		}
		VectorMA( mean, 1.0f / rig->numBodies, vel, mean );
		Com_Memset( d, 0, sizeof( *d ) );
		VectorSet( d->position, t->place[i][3] / OZ_RAGDOLL_METER, t->place[i][7] / OZ_RAGDOLL_METER,
				   t->place[i][11] / OZ_RAGDOLL_METER );
		MatrixQuat( t->place[i], d->rotation );
		VectorScale( vel, 1.0f / OZ_RAGDOLL_METER, d->velocity );
		d->radius = b->radius / OZ_RAGDOLL_METER;
		d->halfHeight = MAX( b->height * 0.5f - b->radius, 0.0f ) / OZ_RAGDOLL_METER;
		d->mass = b->mass;
		// a thin capsule has next to no inertia about its length, and a small one with joints far off next to
		// none at all: the joints spin them up. Every part gets the inertia of a rod reaching its farthest joint.
		length = MAX( b->height, 2.0f * b->reach ) / OZ_RAGDOLL_METER;
		d->inertia = b->mass * ( length * length / 12.0f + d->radius * d->radius * 0.25f );
		d->parent = b->parent;
		d->kind = b->kind;
		VectorCopy( b->point1, d->point1 );
		VectorCopy( b->axis1, d->axis1 );
		VectorCopy( b->normal1, d->normal1 );
		VectorCopy( b->point2, d->point2 );
		VectorCopy( b->axis2, d->axis2 );
		VectorCopy( b->normal2, d->normal2 );
		d->swing = b->swingLimit;
		d->twistMin = b->twistMin;
		d->twistMax = b->twistMax;
		Vector4Copy( b->neutral, d->neutral );
		d->weakTorque = b->weakTorque;
		d->strongTorque = b->strongTorque;
		d->zone = DEG2RAD( OZ_RAGDOLL_ZONE );
		if ( b->parent >= 0 ) {
			StartJoint( b, t->place[b->parent], t->place[i], d );
			VectorCopy( d->point1, t->point1[i] );
		}
	}
	// parts that touch standing never collide, nor those that overlap in the pose the body dies in (an arm
	// across the chest, a hand in the hip), which the solver would fling apart on the first step
	for ( i = 0; i < rig->numBodies; i++ ) {
		for ( k = 0; k < rig->numBodies; k++ ) {
			vec3_t a0, a1, b0, b1;
			apart[i * rig->numBodies + k] = rig->apart[i][k];
			if ( k > i && !rig->apart[i][k] ) {
				Segment( &rig->bodies[i], t->place[i], a0, a1 );
				Segment( &rig->bodies[k], t->place[k], b0, b1 );
				apart[i * rig->numBodies + k] = SegmentsApart( a0, a1, b0, b1 ) < rig->bodies[i].radius + rig->bodies[k].radius;
			}
		}
	}
	Com_sprintf( stats.lastStart, sizeof( stats.lastStart ), "%s at (%.0f %.0f %.0f), moving (%.0f %.0f %.0f), "
				 "body (%.0f %.0f %.0f), push (%.0f %.0f %.0f)", rig->name, legs->e.origin[0], legs->e.origin[1],
				 legs->e.origin[2], mean[0], mean[1], mean[2], body[0], body[1], body[2], push[0], push[1], push[2] );
	Gather( t, qtrue );
	t->phys = OzPhys_AddRagdoll( desc, rig->numBodies, apart, r_ozRagdollTone->integer );
	Com_Memcpy( t->startJoints, world, rig->data->num_joints * 12 * sizeof( float ) );
	Com_Memcpy( t->joints, world, rig->data->num_joints * 12 * sizeof( float ) );
	t->ragdoll = qtrue;
	t->startTime = now;
	t->burnStart = 0;
	t->gap = 0.0f;
	t->flung = qfalse;
	t->lyingZ = t->sinkZ = -1e9f;
	t->sinkTime = 0;
	stats.started++;
}

/*
=================
Pose

The skeleton from the bodies: each body's bone where its capsule lies; the other joints (bones carrying too
little skin for a body, tags, an unskinned root) carried along by the joint above them as the dying pose had
them, or by the pelvis.
=================
*/
static void Pose( ozTrack_t *t ) {
	const ozRig_t *rig = t->rig;
	const iqmData_t *data = rig->data;
	float pos[OZ_RAGDOLL_BODIES][3], rot[OZ_RAGDOLL_BODIES][4];
	int i, j;

	if ( t->phys >= 0 ) {
		OzPhys_ReadRagdoll( t->phys, pos, rot );
		for ( i = 0; i < rig->numBodies; i++ ) {
			vec3_t at;
			VectorScale( pos[i], OZ_RAGDOLL_METER, at );
			QuatMatrix( rot[i], at, t->place[i] );
		}
	}
	for ( i = 0; i < rig->numBodies; i++ ) {
		R_OzMat34Multiply( t->place[i], rig->bodies[i].toBone, t->joints + 12 * rig->bodies[i].joint );
	}
	for ( j = 0; j < data->num_joints; j++ ) {
		int p = data->jointParents[j];
		float inv[12], rel[12];
		if ( rig->jointBody[j] >= 0 ) {
			continue;
		}
		if ( p < 0 ) {
			p = rig->bodies[0].joint;
		}
		R_OzMat34InvertRigid( t->startJoints + 12 * p, inv );
		R_OzMat34Multiply( inv, t->startJoints + 12 * j, rel );
		R_OzMat34Multiply( t->joints + 12 * p, rel, t->joints + 12 * j );
	}
}

// how the body lies now: a part in a wall or a mover, a joint parted, a part flown off
static void Check( ozTrack_t *t, int now ) {
	const ozRig_t *rig = t->rig;
	vec3_t hips;
	float torn = 0.0f;
	int i;

	VectorSet( hips, t->place[0][3], t->place[0][7], t->place[0][11] );
	t->solidAt = -1;
	for ( i = 0; i < rig->numBodies; i++ ) {
		const ozBody_t *b = &rig->bodies[i];
		vec3_t mid, a1, a2;

		VectorSet( mid, t->place[i][3], t->place[i][7], t->place[i][11] );
		if ( ( ri.CM_PointContents( mid, 0 ) & CONTENTS_SOLID ) && t->phys >= 0 ) {
			// a part pressed into the world past its middle (a light hand under the trunk, a body first seen
			// lying against a wall): the whole body moves the way that part comes back out, along the line from
			// its parent's middle (the pelvis: from the server's corpse, which is never in a wall) to where the
			// world starts. Moving only the part parted its joint.
			trace_t tr;
			vec3_t from, delta;
			int k;
			if ( b->parent >= 0 ) {
				VectorSet( from, t->place[b->parent][3], t->place[b->parent][7], t->place[b->parent][11] );
			} else {
				VectorCopy( t->anchor, from );
			}
			ri.CM_BoxTrace( &tr, from, mid, vec3_origin, vec3_origin, 0, OZ_RAGDOLL_MASK, qfalse );
			if ( !tr.startsolid ) {
				VectorSubtract( tr.endpos, mid, delta );
				for ( k = 0; k < rig->numBodies; k++ ) {
					t->place[k][3] += delta[0];
					t->place[k][7] += delta[1];
					t->place[k][11] += delta[2];
				}
				VectorAdd( hips, delta, hips );
				VectorAdd( mid, delta, mid );
				VectorScale( delta, 1.0f / OZ_RAGDOLL_METER, delta );
				OzPhys_ShiftRagdoll( t->phys, delta );
			}
		}
		if ( t->solidAt < 0 && ( ri.CM_PointContents( mid, 0 ) & CONTENTS_SOLID ) ) {
			t->solidAt = i;		// in a wall (in a mover is a crush, below)
		}
		if ( now - t->startTime < 200 || ( t->burnStart && now - t->burnStart >= OZ_RAGDOLL_BURN_TIME ) ) {
			continue;
		}
		if ( Distance( mid, hips ) > rig->span + OZ_RAGDOLL_FLUNG ) {
			t->flung = qtrue;
		}
		if ( b->parent >= 0 ) {
			BodyPoint( t->place[b->parent], t->point1[i], a1 );
			BodyPoint( t->place[i], b->point2, a2 );
			torn = MAX( torn, Distance( a1, a2 ) );
			if ( Distance( a1, a2 ) > t->gap && !( Distance( a1, a2 ) > 2.0f * OZ_RAGDOLL_GAP && OzPhys_RagdollCarried( t->phys ) ) ) {
				t->gap = Distance( a1, a2 );
				t->gapAt = now - t->startTime;
				t->gapBody = i;
			}
		}
	}
	// a mover squeezing the body against the world (a part in it three updates running, or a joint torn open
	// on a body a mover has carried) crushes it: it's gone at once, as the game gibs a crushed corpse
	if ( torn > 2.0f * OZ_RAGDOLL_GAP && OzPhys_RagdollCarried( t->phys ) ) {
		t->crushed = qtrue;
	}
	for ( i = 0; numMovers && i < rig->numBodies && !t->crushed; i++ ) {
		vec3_t mid;
		VectorSet( mid, t->place[i][3], t->place[i][7], t->place[i][11] );
		if ( MoverContents( mid ) & CONTENTS_SOLID ) {
			break;
		}
	}
	if ( !t->crushed && numMovers && i < rig->numBodies ) {
		if ( !t->crushSince ) {
			t->crushSince = now;
		}
		t->crushed = now - t->crushSince >= OZ_RAGDOLL_CRUSH;
	} else if ( !t->crushed ) {
		t->crushSince = 0;
	}
	if ( t->crushed && ( !t->burnStart || now - t->burnStart < OZ_RAGDOLL_BURN_TIME ) ) {
		t->burnStart = now - OZ_RAGDOLL_BURN_TIME;
		NoteBurn( t, now, "crushed" );
	}
	if ( t->burnStart && now - t->burnStart >= OZ_RAGDOLL_BURN_TIME ) {
		t->solidAt = -1;		// burned away: nothing left to see in the wall
	}
}

static float Burn( const ozTrack_t *t, int now ) {
	float f;

	if ( !t->burnStart ) {
		return 0.0f;
	}
	f = (float)( now - t->burnStart ) / OZ_RAGDOLL_BURN_TIME;
	return f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
}

/*
=================
R_OzRagdollFighter

A fighter's legs and its skeleton as the animation poses it, in the world. Returns the skeleton to draw
instead (its ragdoll's) and how far it has burned, or NULL while it lives (or for a rig without bodies).
=================
*/
const float *R_OzRagdollFighter( const ozRig_t *rig, const trRefEntity_t *legs, const float *world, float *dissolve ) {
	int frame = legs->e.frame, now = tr.refdef.time, i;
	qboolean dead = ( frame >= 0 && frame < rig->deathFrames ) || r_ozRagdollTest->integer;
	ozTrack_t *t;

	*dissolve = 0.0f;
	if ( !rig->numBodies || !physUp ) {
		return NULL;
	}
	t = FindTrack( rig, legs->e.origin, dead );
	if ( t->scene == sceneCount ) {
		// a second entity of the same fighter (a powerup shell): the same skeleton
		*dissolve = Burn( t, now );
		return t->ragdoll ? t->joints : NULL;
	}
	t->scene = sceneCount;
	t->seen = now;
	VectorCopy( legs->e.origin, t->anchor );

	if ( !dead ) {
		float place[OZ_RAGDOLL_BODIES][12];
		EndRagdoll( t );		// risen (r_ozRagdollTest off again)
		if ( now != t->lastTime ) {
			Com_Memcpy( t->before, t->last, sizeof( t->last ) );
			VectorCopy( t->lastAnchor, t->beforeAnchor );
			t->beforeTime = t->lastTime;
		}
		VectorCopy( legs->e.origin, t->lastAnchor );
		Places( rig, world, place );
		for ( i = 0; i < rig->numBodies; i++ ) {
			VectorSet( t->last[i], place[i][3], place[i][7], place[i][11] );
		}
		t->lastTime = now;
		return NULL;
	}
	if ( !t->ragdoll ) {
		Start( t, legs, world, now );
	}

	// the server sinks a corpse into the floor before removing it: slowly, where a falling one drops fast.
	// Its height is compared over OZ_RAGDOLL_SINK_SPAN, not a frame: the server moves it a unit at a time,
	// which in one frame looked like a fall at any frame rate over 30
	if ( frame == rig->deadFrame[0] || frame == rig->deadFrame[1] || frame == rig->deadFrame[2] ) {
		float z = legs->e.origin[2];
		if ( t->lyingZ < -1e8f || z > t->lyingZ ) {
			t->lyingZ = t->sinkZ = z;
			t->sinkTime = now;
		} else if ( now - t->sinkTime >= OZ_RAGDOLL_SINK_SPAN ) {
			if ( t->sinkZ - z > OZ_RAGDOLL_SINK_RATE * ( now - t->sinkTime ) * 0.001f ) {
				t->lyingZ = z;		// falling
			} else if ( z < t->lyingZ - 0.5f && !t->burnStart ) {
				t->burnStart = now;
				NoteBurn( t, now, "sinking" );
			}
			t->sinkZ = z;
			t->sinkTime = now;
		}
	}
	if ( !t->burnStart && r_ozCorpseBurn->value > 0.0f && now - t->startTime > r_ozCorpseBurn->value * 1000.0f ) {
		t->burnStart = now;
		NoteBurn( t, now, "timer" );
	}
	OzPhys_ToneRagdoll( t->phys, 2.0f - (float)( now - t->startTime ) / OZ_RAGDOLL_LIMP );
	Pose( t );
	Check( t, now );
	if ( t->burnStart && now - t->burnStart >= OZ_RAGDOLL_BURN_TIME && t->phys >= 0 ) {
		stats.carried += OzPhys_RagdollCarried( t->phys );
		OzPhys_RemoveRagdoll( t->phys );		// burned away: it lies as it was, unseen, until the track ends
		t->phys = -1;
	}
	*dissolve = Burn( t, now );
	return t->joints;
}

#else	// no physics: fighters keep their death animation

void R_OzRagdollBeginScene( void ) {
}

void R_OzRagdollNewMap( void ) {
}

void R_OzRagdollShutdown( void ) {
}

void R_OzRagdollStats_f( void ) {
	ri.Printf( PRINT_ALL, "ragdolls: this renderer was built without physics\n" );
}

const float *R_OzRagdollFighter( const ozRig_t *rig, const trRefEntity_t *legs, const float *world, float *dissolve ) {
	*dissolve = 0.0f;
	return NULL;
}

#endif
