/*
===========================================================================
Copyright (C) 2026 Oz Arena contributors

This file is part of Oz Arena, built on the Quake III Arena source code.

Oz Arena is free software; you can redistribute it and/or modify it under the terms of the GNU General
Public License as published by the Free Software Foundation; either version 2 of the License, or (at your
option) any later version.
===========================================================================
*/
// oz_physics.cpp -- the fighters' ragdolls on Jolt Physics (ENGINE_PLAN.md P2), as the Godot build's ran:
// capsules joined by cones and hinges, colliding with the map's solids, the movers and each other. Past the
// Godot build, each joint has muscle tone (after Lincoln Margison's "Improving ragdolls forever"): a motor
// pulls it weakly toward the neutral posture, and the motor's strength grows over the last stretch before
// each limit, so a limb slows into its limit rather than stopping dead.

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>

#include <cstdarg>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

#include "oz_physics.h"

using namespace JPH;

namespace {

namespace Layers {
	constexpr ObjectLayer SOLID = 0, MOVER = 1, RAGDOLL = 2, COUNT = 3;
}

const BroadPhaseLayer STILL( 0 ), MOVING( 1 );

class BroadPhaseLayers final : public BroadPhaseLayerInterface {
public:
	uint GetNumBroadPhaseLayers() const override { return 2; }
	BroadPhaseLayer GetBroadPhaseLayer( ObjectLayer layer ) const override { return layer == Layers::SOLID ? STILL : MOVING; }
#if defined( JPH_EXTERNAL_PROFILE ) || defined( JPH_PROFILE_ENABLED )
	const char *GetBroadPhaseLayerName( BroadPhaseLayer layer ) const override { return layer == STILL ? "still" : "moving"; }
#endif
};

// ragdolls hit everything; solids and movers only ragdolls
class ObjectVsBroadPhase final : public ObjectVsBroadPhaseLayerFilter {
public:
	bool ShouldCollide( ObjectLayer layer, BroadPhaseLayer bp ) const override { return layer == Layers::RAGDOLL || bp == MOVING; }
};

class ObjectPairs final : public ObjectLayerPairFilter {
public:
	bool ShouldCollide( ObjectLayer a, ObjectLayer b ) const override { return a == Layers::RAGDOLL || b == Layers::RAGDOLL; }
};

struct Joint {
	TwoBodyConstraint	*constraint = nullptr;
	ozJointKind_t		kind = OZ_JOINT_NONE;
	float				swing = 0, twistMin = 0, twistMax = 0, weak = 0, strong = 0, zone = 0;
};

struct Ragdoll {
	bool				used = false, carried = false;
	float				tone = 1.0f;		// how much of the muscle tone is left (it fades once the body lies)
	int					numBodies = 0;
	BodyID				bodies[OZ_PHYS_BODIES];
	Joint				joints[OZ_PHYS_BODIES];
	Ref<GroupFilterTable> filter;
};

struct World {
	TempAllocatorImpl		*temp = nullptr;
	JobSystemSingleThreaded	*jobs = nullptr;
	PhysicsSystem			*system = nullptr;
	BroadPhaseLayers		bpLayers;
	ObjectVsBroadPhase		objectVsBp;
	ObjectPairs				objectPairs;
	std::unordered_map<int, BodyID> solids, movers;
	Ragdoll					ragdolls[OZ_PHYS_RAGDOLLS];
	int						added = 0;		// solids since the broad phase was last optimized
};

World *world;
void ( *printText )( const char *text );

void TraceText( const char *fmt, ... ) {
	char text[1024];
	va_list args;

	va_start( args, fmt );
	vsnprintf( text, sizeof( text ), fmt, args );
	va_end( args );
	if ( printText ) {
		printText( text );
	}
}

// a mover touching a ragdoll's body carries it
class Contacts final : public ContactListener {
public:
	void OnContactAdded( const Body &a, const Body &b, const ContactManifold &, ContactSettings & ) override {
		const Body *part = a.GetObjectLayer() == Layers::RAGDOLL ? &a : &b, *other = part == &a ? &b : &a;
		if ( part->GetObjectLayer() == Layers::RAGDOLL && other->GetObjectLayer() == Layers::MOVER ) {
			const uint64 r = part->GetUserData();
			if ( r >= 1 && r <= OZ_PHYS_RAGDOLLS ) {
				world->ragdolls[r - 1].carried = true;
			}
		}
	}
} contacts;

Vec3 V( const float *v ) {
	return Vec3( v[0], v[1], v[2] );
}

Quat Q( const float *q ) {
	return Quat( q[0], q[1], q[2], q[3] ).Normalized();
}

void RemoveBody( BodyID id ) {
	BodyInterface &bi = world->system->GetBodyInterfaceNoLock();
	if ( !id.IsInvalid() ) {
		bi.RemoveBody( id );
		bi.DestroyBody( id );
	}
}

// how far into the zone before a limit an angle is: 0 where the zone starts (or before), 1 at the limit
float Depth( float angle, float lo, float hi, float zone ) {
	float d = 0.0f;
	if ( zone <= 0.0f ) {
		return 0.0f;
	}
	if ( angle > hi - zone ) {
		d = ( angle - ( hi - zone ) ) / zone;
	} else if ( angle < lo + zone ) {
		d = ( lo + zone - angle ) / zone;
	}
	return d < 0.0f ? 0.0f : d > 1.0f ? 1.0f : d;
}

// the muscle tone each step: a joint's motor is weak through its range and stiffens toward each limit; as
// the tone fades, every motor weakens with it, and once it's gone they're off (the joints swing freely)
void Tone( Ragdoll &r ) {
	for ( int i = 0; i < r.numBodies; i++ ) {
		Joint &j = r.joints[i];
		float s;

		if ( !j.constraint || j.strong <= 0.0f ) {
			continue;
		}
		if ( r.tone <= 0.0f ) {
			if ( j.kind == OZ_JOINT_CONE ) {
				static_cast<SwingTwistConstraint *>( j.constraint )->SetSwingMotorState( EMotorState::Off );
				static_cast<SwingTwistConstraint *>( j.constraint )->SetTwistMotorState( EMotorState::Off );
			} else {
				static_cast<HingeConstraint *>( j.constraint )->SetMotorState( EMotorState::Off );
			}
			j.strong = 0.0f;
			continue;
		}
		if ( j.kind == OZ_JOINT_CONE ) {
			SwingTwistConstraint *c = static_cast<SwingTwistConstraint *>( j.constraint );
			const Quat q = c->GetRotationInConstraintSpace();
			const Quat twist = q.GetTwist( Vec3::sAxisX() );
			const Quat swing = q * twist.Conjugated();
			const float swingAngle = 2.0f * ACos( std::min( 1.0f, std::fabs( swing.GetW() ) ) );
			const float twistAngle = twist.GetRotationAngle( Vec3::sAxisX() );
			s = std::max( Depth( swingAngle, -j.swing, j.swing, std::min( j.zone, j.swing * 0.5f ) ),
					 Depth( twistAngle, j.twistMin, j.twistMax, std::min( j.zone, ( j.twistMax - j.twistMin ) * 0.25f ) ) );
			const float torque = ( j.weak + ( j.strong - j.weak ) * s * s ) * r.tone;
			c->GetSwingMotorSettings().SetTorqueLimit( torque );
			c->GetTwistMotorSettings().SetTorqueLimit( torque );
		} else if ( j.kind == OZ_JOINT_HINGE ) {
			HingeConstraint *c = static_cast<HingeConstraint *>( j.constraint );
			s = Depth( c->GetCurrentAngle(), j.twistMin, j.twistMax, std::min( j.zone, ( j.twistMax - j.twistMin ) * 0.25f ) );
			c->GetMotorSettings().SetTorqueLimit( ( j.weak + ( j.strong - j.weak ) * s * s ) * r.tone );
		}
	}
}

} // namespace

extern "C" {

void OzPhys_Init( void ( *print )( const char *text ) ) {
	if ( world ) {
		return;
	}
	printText = print;
	RegisterDefaultAllocator();
	Trace = TraceText;
	Factory::sInstance = new Factory();
	RegisterTypes();
	world = new World();
	world->temp = new TempAllocatorImpl( 16 * 1024 * 1024 );
	world->jobs = new JobSystemSingleThreaded( cMaxPhysicsJobs );
	world->system = new PhysicsSystem();
	world->system->Init( 65536, 0, 65536, 16384, world->bpLayers, world->objectVsBp, world->objectPairs );
	world->system->SetContactListener( &contacts );
	// more passes than Jolt's defaults (10 and 2): a body's light hands and feet under its heavy trunk sank
	// into the floor and its joints parted with them
	PhysicsSettings settings = world->system->GetPhysicsSettings();
	settings.mNumVelocitySteps = 32;
	settings.mNumPositionSteps = 16;
	// a body lying still sleeps (it costs nothing then): Jolt's own threshold (0.03 m/s, each part's points kept
	// within half a unit for half a second) is finer than a body's resting jitter, so bodies crept along the
	// floor a few units a second and stayed awake until they burned, 0.2 ms a step each. At 0.2 m/s a part
	// stays within 3.2 units for half a second; a falling, rolling or carried body moves far more.
	settings.mPointVelocitySleepThreshold = 0.2f;
	world->system->SetPhysicsSettings( settings );
}

void OzPhys_Clear( void ) {
	if ( !world ) {
		return;
	}
	for ( int i = 0; i < OZ_PHYS_RAGDOLLS; i++ ) {
		OzPhys_RemoveRagdoll( i );
	}
	for ( auto &s : world->solids ) {
		RemoveBody( s.second );
	}
	for ( auto &m : world->movers ) {
		RemoveBody( m.second );
	}
	world->solids.clear();
	world->movers.clear();
	world->added = 0;
}

void OzPhys_Shutdown( void ) {
	if ( !world ) {
		return;
	}
	OzPhys_Clear();
	delete world->system;
	delete world->jobs;
	delete world->temp;
	delete world;
	world = nullptr;
	UnregisterTypes();
	delete Factory::sInstance;
	Factory::sInstance = nullptr;
}

int OzPhys_HasSolid( int id ) {
	return world && world->solids.count( id );
}

void OzPhys_AddSolid( int id, const float *points, int numPoints ) {
	Array<Vec3> p;
	BodyID body;

	if ( !world || world->solids.count( id ) ) {
		return;
	}
	for ( int i = 0; i < numPoints; i++ ) {
		p.push_back( V( points + 3 * i ) );
	}
	ConvexHullShapeSettings hull( p, 0.01f );
	ShapeSettings::ShapeResult shape = hull.Create();
	if ( !shape.HasError() ) {
		BodyCreationSettings settings( shape.Get(), RVec3::sZero(), Quat::sIdentity(), EMotionType::Static, Layers::SOLID );
		settings.mFriction = 1.0f;
		body = world->system->GetBodyInterfaceNoLock().CreateAndAddBody( settings, EActivation::DontActivate );
		world->added++;
	}
	world->solids[id] = body;		// a flat one, which makes no hull, stays known (as nothing)
}

int OzPhys_HasMover( int index ) {
	return world && world->movers.count( index );
}

void OzPhys_AddMover( int index, int numHulls, const int *numPoints, const float *points ) {
	StaticCompoundShapeSettings compound;
	BodyID body;
	int parts = 0;

	if ( !world || world->movers.count( index ) ) {
		return;
	}
	for ( int h = 0; h < numHulls; points += 3 * numPoints[h], h++ ) {
		Array<Vec3> p;
		for ( int i = 0; i < numPoints[h]; i++ ) {
			p.push_back( V( points + 3 * i ) );
		}
		Ref<ConvexHullShapeSettings> hull = new ConvexHullShapeSettings( p, 0.01f );
		if ( !hull->Create().HasError() ) {
			compound.AddShape( Vec3::sZero(), Quat::sIdentity(), hull );
			parts++;
		}
	}
	if ( parts ) {
		ShapeSettings::ShapeResult shape = compound.Create();
		if ( !shape.HasError() ) {
			BodyCreationSettings settings( shape.Get(), RVec3::sZero(), Quat::sIdentity(), EMotionType::Kinematic, Layers::MOVER );
			settings.mFriction = 1.0f;
			body = world->system->GetBodyInterfaceNoLock().CreateAndAddBody( settings, EActivation::Activate );
		}
	}
	world->movers[index] = body;
}

void OzPhys_MoveMover( int index, const float position[3], const float rotation[4], float dt ) {
	BodyID body;

	if ( !world || !world->movers.count( index ) || ( body = world->movers[index] ).IsInvalid() ) {
		return;
	}
	world->system->GetBodyInterfaceNoLock().MoveKinematic( body, RVec3( V( position ) ), Q( rotation ), dt );
}

int OzPhys_AddRagdoll( const ozPhysBody_t *bodies, int numBodies, const unsigned char *apart, int tone ) {
	int r;

	if ( !world || numBodies < 1 || numBodies > OZ_PHYS_BODIES ) {
		return -1;
	}
	BodyInterface &bi = world->system->GetBodyInterfaceNoLock();
	for ( r = 0; r < OZ_PHYS_RAGDOLLS && world->ragdolls[r].used; r++ ) {
	}
	if ( r == OZ_PHYS_RAGDOLLS ) {
		return -1;
	}
	Ragdoll &rd = world->ragdolls[r];
	rd = Ragdoll();
	rd.used = true;
	rd.numBodies = numBodies;
	rd.filter = new GroupFilterTable( numBodies );
	for ( int a = 0; a < numBodies; a++ ) {
		for ( int b = a + 1; b < numBodies; b++ ) {
			if ( apart[a * numBodies + b] || apart[b * numBodies + a] ) {
				rd.filter->DisableCollision( a, b );
			}
		}
	}
	for ( int i = 0; i < numBodies; i++ ) {
		const ozPhysBody_t &b = bodies[i];
		BodyCreationSettings settings( new CapsuleShape( std::max( b.halfHeight, 0.001f ), b.radius ), RVec3( V( b.position ) ),
									   Q( b.rotation ), EMotionType::Dynamic, Layers::RAGDOLL );
		settings.mLinearVelocity = V( b.velocity );
		settings.mMotionQuality = EMotionQuality::LinearCast;		// thin limbs at speed step through edges otherwise
		settings.mFriction = 1.0f;
		settings.mRestitution = 0.0f;
		settings.mLinearDamping = 0.05f;
		settings.mAngularDamping = 1.5f;		// the Godot build's 0.6 let bodies roll on, capsules like wheels: a stumble
												// rolled 100 units and more, and a body crept across a moving lift
		settings.mOverrideMassProperties = EOverrideMassProperties::MassAndInertiaProvided;
		settings.mMassPropertiesOverride.mMass = b.mass;
		settings.mMassPropertiesOverride.mInertia = Mat44::sScale( Vec3::sReplicate( b.inertia ) );
		settings.mCollisionGroup = CollisionGroup( rd.filter, 0, i );
		settings.mUserData = r + 1;
		rd.bodies[i] = bi.CreateAndAddBody( settings, EActivation::Activate );
		if ( rd.bodies[i].IsInvalid() ) {
			rd.numBodies = i;
			OzPhys_RemoveRagdoll( r );
			return -1;
		}
	}
	for ( int i = 0; i < numBodies; i++ ) {
		const ozPhysBody_t &b = bodies[i];
		Joint &j = rd.joints[i];
		TwoBodyConstraint *c = nullptr;

		if ( b.parent < 0 || b.parent >= i || b.kind == OZ_JOINT_NONE ) {
			continue;
		}
		const MotorSettings motor( 2.0f, 1.0f, 0.0f, b.weakTorque );
		if ( b.kind == OZ_JOINT_HINGE ) {
			HingeConstraintSettings s;
			s.mSpace = EConstraintSpace::LocalToBodyCOM;
			s.mPoint1 = RVec3( V( b.point1 ) );
			s.mHingeAxis1 = V( b.axis1 ).Normalized();
			s.mNormalAxis1 = V( b.normal1 ).Normalized();
			s.mPoint2 = RVec3( V( b.point2 ) );
			s.mHingeAxis2 = V( b.axis2 ).Normalized();
			s.mNormalAxis2 = V( b.normal2 ).Normalized();
			s.mLimitsMin = b.twistMin;
			s.mLimitsMax = b.twistMax;
			s.mMotorSettings = motor;
			HingeConstraint *h = static_cast<HingeConstraint *>( bi.CreateConstraint( &s, rd.bodies[b.parent], rd.bodies[i] ) );
			if ( h && tone ) {
				h->SetMotorState( EMotorState::Position );
				h->SetTargetAngle( b.neutral[0] );
			}
			c = h;
		} else {
			SwingTwistConstraintSettings s;
			s.mSpace = EConstraintSpace::LocalToBodyCOM;
			s.mPosition1 = RVec3( V( b.point1 ) );
			s.mTwistAxis1 = V( b.axis1 ).Normalized();
			s.mPlaneAxis1 = V( b.normal1 ).Normalized();
			s.mPosition2 = RVec3( V( b.point2 ) );
			s.mTwistAxis2 = V( b.axis2 ).Normalized();
			s.mPlaneAxis2 = V( b.normal2 ).Normalized();
			s.mNormalHalfConeAngle = s.mPlaneHalfConeAngle = b.swing;
			s.mTwistMinAngle = b.twistMin;
			s.mTwistMaxAngle = b.twistMax;
			s.mSwingMotorSettings = s.mTwistMotorSettings = motor;
			SwingTwistConstraint *st = static_cast<SwingTwistConstraint *>( bi.CreateConstraint( &s, rd.bodies[b.parent], rd.bodies[i] ) );
			if ( st && tone ) {
				st->SetSwingMotorState( EMotorState::Position );
				st->SetTwistMotorState( EMotorState::Position );
				st->SetTargetOrientationCS( Q( b.neutral ) );
			}
			c = st;
		}
		if ( c ) {
			world->system->AddConstraint( c );
			j.constraint = c;
			j.kind = b.kind;
			j.swing = b.swing;
			j.twistMin = b.twistMin;
			j.twistMax = b.twistMax;
			j.weak = b.weakTorque;
			j.strong = tone ? b.strongTorque : 0.0f;
			j.zone = b.zone;
		}
	}
	return r;
}

void OzPhys_RemoveRagdoll( int r ) {
	if ( !world || r < 0 || r >= OZ_PHYS_RAGDOLLS || !world->ragdolls[r].used ) {
		return;
	}
	Ragdoll &rd = world->ragdolls[r];
	for ( int i = 0; i < OZ_PHYS_BODIES; i++ ) {
		if ( rd.joints[i].constraint ) {
			world->system->RemoveConstraint( rd.joints[i].constraint );
			rd.joints[i].constraint = nullptr;
		}
	}
	for ( int i = 0; i < rd.numBodies; i++ ) {
		RemoveBody( rd.bodies[i] );
	}
	rd = Ragdoll();
}

void OzPhys_ReadRagdoll( int r, float ( *position )[3], float ( *rotation )[4] ) {
	if ( !world || r < 0 || r >= OZ_PHYS_RAGDOLLS || !world->ragdolls[r].used ) {
		return;
	}
	const Ragdoll &rd = world->ragdolls[r];
	BodyInterface &bi = world->system->GetBodyInterfaceNoLock();
	for ( int i = 0; i < rd.numBodies; i++ ) {
		RVec3 p;
		Quat q;
		bi.GetPositionAndRotation( rd.bodies[i], p, q );
		position[i][0] = (float)p.GetX(); position[i][1] = (float)p.GetY(); position[i][2] = (float)p.GetZ();
		rotation[i][0] = q.GetX(); rotation[i][1] = q.GetY(); rotation[i][2] = q.GetZ(); rotation[i][3] = q.GetW();
	}
}

int OzPhys_RagdollAsleep( int r ) {
	if ( !world || r < 0 || r >= OZ_PHYS_RAGDOLLS || !world->ragdolls[r].used ) {
		return 1;
	}
	const Ragdoll &rd = world->ragdolls[r];
	BodyInterface &bi = world->system->GetBodyInterfaceNoLock();
	for ( int i = 0; i < rd.numBodies; i++ ) {
		if ( bi.IsActive( rd.bodies[i] ) ) {
			return 0;
		}
	}
	return 1;
}

void OzPhys_ShiftRagdoll( int r, const float delta[3] ) {
	if ( !world || r < 0 || r >= OZ_PHYS_RAGDOLLS || !world->ragdolls[r].used ) {
		return;
	}
	const Ragdoll &rd = world->ragdolls[r];
	BodyInterface &bi = world->system->GetBodyInterfaceNoLock();
	for ( int i = 0; i < rd.numBodies; i++ ) {
		bi.SetPosition( rd.bodies[i], bi.GetPosition( rd.bodies[i] ) + V( delta ), EActivation::Activate );
	}
}

void OzPhys_ToneRagdoll( int r, float tone ) {
	if ( world && r >= 0 && r < OZ_PHYS_RAGDOLLS && world->ragdolls[r].used ) {
		world->ragdolls[r].tone = tone < 0.0f ? 0.0f : tone > 1.0f ? 1.0f : tone;
	}
}

void OzPhys_WakeRagdoll( int r ) {
	if ( !world || r < 0 || r >= OZ_PHYS_RAGDOLLS || !world->ragdolls[r].used ) {
		return;
	}
	const Ragdoll &rd = world->ragdolls[r];
	world->system->GetBodyInterfaceNoLock().ActivateBodies( rd.bodies, rd.numBodies );
}

int OzPhys_RagdollCarried( int r ) {
	return world && r >= 0 && r < OZ_PHYS_RAGDOLLS && world->ragdolls[r].used && world->ragdolls[r].carried;
}

int OzPhys_Optimize( void ) {
	if ( !world || world->added < 256 ) {
		return 0;
	}
	world->system->OptimizeBroadPhase();
	world->added = 0;
	return 1;
}

void OzPhys_Step( float dt, float gravity ) {
	if ( !world ) {
		return;
	}
	world->system->SetGravity( Vec3( 0.0f, 0.0f, -gravity ) );
	for ( int r = 0; r < OZ_PHYS_RAGDOLLS; r++ ) {
		if ( world->ragdolls[r].used ) {
			Tone( world->ragdolls[r] );
		}
	}
	world->system->Update( dt, 1, world->temp, world->jobs );
}

void OzPhys_Counts( int *bodies, int *awake, int *solids ) {
	*bodies = world ? (int)world->system->GetNumBodies() : 0;
	*awake = world ? (int)world->system->GetNumActiveBodies( EBodyType::RigidBody ) : 0;
	*solids = world ? (int)world->solids.size() : 0;
}

} // extern "C"
