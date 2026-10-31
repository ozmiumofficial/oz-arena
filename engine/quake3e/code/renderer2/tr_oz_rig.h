/*
===========================================================================
Copyright (C) 2026 Oz Arena contributors

This file is part of Oz Arena, built on the Quake III Arena source code.

Oz Arena is free software; you can redistribute it and/or modify it under the terms of the GNU General
Public License as published by the Free Software Foundation; either version 2 of the License, or (at your
option) any later version.
===========================================================================
*/
// tr_oz_rig.h -- the remastered fighters' rig and ragdoll (tr_oz_rig.c, tr_oz_ragdoll.c), ENGINE_PLAN.md P2

#ifndef TR_OZ_RIG_H
#define TR_OZ_RIG_H

#include "tr_local.h"

#define OZ_RAGDOLL_BODIES		32

// a ragdoll body: a capsule riding on its bone, joined to its parent's (tools/make_fighters.py, after the
// Godot build's plan); positions in id units, standing (the bind pose)
typedef struct {
	int		joint;			// the bone it moves
	int		parent;			// the body it hangs from, or -1 (the pelvis)
	int		kind;			// its joint: 0 none, 1 a cone (swing and twist), 2 a hinge
	vec3_t	center, axis;	// the capsule, its axis pointing away from its joint
	float	radius, height, mass, reach;	// id units, kg; reach: its farthest joint from its middle
	vec3_t	at;				// the joint
	float	swing, twist, bent;	// degrees: a cone's swing and twist; a hinge's bend standing
	qboolean hip;			// a cone centered on the thigh hanging 30 degrees forward, not on the stance
	float	lean;			// degrees the neutral posture turns it forward
	float	chain;			// derived: the joints from the pelvis to its own, end to end (units)
	// derived (tr_oz_rig.c): the capsule's frame (its y along the axis) and the bone's frame in it, and the
	// joint for the physics (oz_physics.h): in the parent's and this body's space, meters and radians
	float	place[12], toBone[12];
	float	point1[3], axis1[3], normal1[3], point2[3], axis2[3], normal2[3];
	float	swingLimit, twistMin, twistMax, neutral[4], weakTorque, strongTorque;
} ozBody_t;

// a fighter rig, from the description in its IQM's comment (tools/make_fighters.py)
typedef struct ozRig_s {
	char		name[MAX_QPATH];	// the IQM's
	iqmData_t	*data;
	int			upperFirst;		// the "upper" frames start here; the "lower" ones at 0
	int			deathFrames;	// lower frames below this are deaths (BOTH_DEATH*, BOTH_DEAD*)
	int			deadFrame[3];	// BOTH_DEAD1-3: a corpse's frame
	char		*meshPart;		// 'l', 'u' or 'h' for each surface
	char		*jointPart;		// and for each joint
	int			tagTorso, tagHead, tagWeapon;
	int			numSpine;
	int			spine[8];		// the torso's joints up to the chest: the cgame's torso turn spreads along them

	// the ragdoll's bodies, the pairs that never collide, and each joint's body (or -1)
	int			numBodies;
	ozBody_t	bodies[OZ_RAGDOLL_BODIES];
	byte		apart[OZ_RAGDOLL_BODIES][OZ_RAGDOLL_BODIES];
	int			jointBody[IQM_MAX_JOINTS];
	float		span;			// the farthest a body's end can reach from the pelvis, its joints straight
} ozRig_t;

// 3x4 row-major matrices, as tr_model_iqm.c keeps them
void R_OzMat34Multiply( const float *a, const float *b, float *out );
void R_OzMat34InvertRigid( const float *m, float *out );

void R_OzRagdollBeginScene( void );
void R_OzRagdollNewMap( void );
void R_OzRagdollShutdown( void );
const float *R_OzRagdollFighter( const ozRig_t *rig, const trRefEntity_t *legs, const float *world, float *dissolve );

#endif
