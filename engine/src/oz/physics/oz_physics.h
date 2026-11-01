/*
===========================================================================
Copyright (C) 2026 Oz Arena contributors

This file is part of Oz Arena, built on the Quake III Arena source code.

Oz Arena is free software; you can redistribute it and/or modify it under the terms of the GNU General
Public License as published by the Free Software Foundation; either version 2 of the License, or (at your
option) any later version.
===========================================================================
*/
// oz_physics.h -- the fighters' ragdolls on Jolt Physics (ENGINE_PLAN.md P2), for the oz renderer
// (tr_oz_ragdoll.c). Everything is in meters (32 id units) and world axes; quaternions are x y z w.

#ifndef OZ_PHYSICS_H
#define OZ_PHYSICS_H

#ifdef __cplusplus
extern "C" {
#endif

#define OZ_PHYS_BODIES		32		// bodies in one ragdoll
#define OZ_PHYS_RAGDOLLS	64

typedef enum { OZ_JOINT_NONE, OZ_JOINT_CONE, OZ_JOINT_HINGE } ozJointKind_t;

typedef struct {
	float	position[3], rotation[4];	// the capsule's middle; its axis is the body's y
	float	velocity[3];
	float	radius, halfHeight;			// the capsule (halfHeight: its straight part's)
	float	mass, inertia;				// kg, and kg m^2 about every axis (a rod's: thin capsules spin up otherwise)
	int		parent;						// the body it hangs from, or -1
	ozJointKind_t kind;
	float	point1[3], axis1[3], normal1[3];	// the joint in the parent's space: where, its twist (a cone) or hinge
	float	point2[3], axis2[3], normal2[3];	// axis, and a normal to it; and in this body's
	float	swing, twistMin, twistMax;	// a cone's half angle and twist range, or a hinge's range (radians)
	// the muscle tone: a motor toward the neutral posture, its torque limit growing from weak to strong over
	// the last zone (radians) before each limit
	float	neutral[4];					// a cone's target turn in constraint space; a hinge's angle is neutral[0]
	float	weakTorque, strongTorque, zone;	// N m, N m, radians
} ozPhysBody_t;

void	OzPhys_Init( void (*print)( const char *text ) );
void	OzPhys_Shutdown( void );
void	OzPhys_Clear( void );				// another map: drops the solids, movers and ragdolls

// the world: static convex solids (by their corner points), and movers (an inline model's solids, moved
// kinematically to where the scene draws them)
int		OzPhys_HasSolid( int id );
void	OzPhys_AddSolid( int id, const float *points, int numPoints );
int		OzPhys_HasMover( int index );
void	OzPhys_AddMover( int index, int numHulls, const int *numPoints, const float *points );
void	OzPhys_MoveMover( int index, const float position[3], const float rotation[4], float dt );

// ragdolls: -1 if it couldn't be made. apart[a * numBodies + b] keeps two bodies from colliding.
int		OzPhys_AddRagdoll( const ozPhysBody_t *bodies, int numBodies, const unsigned char *apart, int tone );
void	OzPhys_RemoveRagdoll( int ragdoll );
void	OzPhys_ReadRagdoll( int ragdoll, float (*position)[3], float (*rotation)[4] );
int		OzPhys_RagdollAsleep( int ragdoll );
int		OzPhys_RagdollCarried( int ragdoll );	// a mover has touched it
void	OzPhys_WakeRagdoll( int ragdoll );
void	OzPhys_ToneRagdoll( int ragdoll, float tone );		// how much of its muscle tone is left, 0 to 1
void	OzPhys_ShiftRagdoll( int ragdoll, const float delta[3] );	// every body moved by delta
int		OzPhys_Optimize( void );			// the broad phase rebuilt after many solids were added: 1 if it was
void	OzPhys_Step( float dt, float gravity );
void	OzPhys_Counts( int *bodies, int *awake, int *solids );	// for ozragdolls

#ifdef __cplusplus
}
#endif

#endif
