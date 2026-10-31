/*
===========================================================================
Copyright (C) 2026 Oz Arena contributors

This file is part of Oz Arena, built on the Quake III Arena source code.

Oz Arena is free software; you can redistribute it and/or modify it under the terms of the GNU General
Public License as published by the Free Software Foundation; either version 2 of the License, or (at your
option) any later version.
===========================================================================
*/
// cm_oz.c -- the collision map's solids for the fighters' ragdoll physics (ENGINE_PLAN.md P2)

#include "cm_local.h"
#include "cm_patch.h"

#define OZ_SOLIDS_LIST		4096
#define OZ_SOLID_PLANES		128
#define OZ_PATCH_THICKNESS	4.0f		// a curved surface's facets are this thick behind it

static qboolean BoxesTouch( const vec3_t mins, const vec3_t maxs, const vec3_t bounds[2] ) {
	return !mins || ( bounds[0][0] <= maxs[0] && bounds[1][0] >= mins[0] && bounds[0][1] <= maxs[1] &&
					  bounds[1][1] >= mins[1] && bounds[0][2] <= maxs[2] && bounds[1][2] >= mins[2] );
}

static void Brush( const cbrush_t *b, int id, int mask, void (*solid)( void *ctx, int id, const float *planes, int numPlanes ), void *ctx ) {
	float planes[OZ_SOLID_PLANES * 4];
	int i, n = 0;

	if ( !( b->contents & mask ) ) {
		return;
	}
	for ( i = 0; i < b->numsides && n < OZ_SOLID_PLANES; i++, n++ ) {
		VectorCopy( b->sides[i].plane->normal, planes + 4 * n );
		planes[4 * n + 3] = b->sides[i].plane->dist;
	}
	solid( ctx, id, planes, n );
}

/*
==================
CM_OzSolids

The solid brushes and curved surfaces' facets touching a box (mins NULL: all of them), each as its planes
(normal and distance, out of the solid), from the world (model 0) or an inline model (in its own space,
brushes only). An id names each for the caller's cache: a brush's number, or -(the surface's number * 4096
+ its facet + 1). A facet is a slab OZ_PATCH_THICKNESS units deep behind the surface.
==================
*/
void CM_OzSolids( clipHandle_t model, const vec3_t mins, const vec3_t maxs, int mask,
				  void (*solid)( void *ctx, int id, const float *planes, int numPlanes ), void *ctx ) {
	static cbrush_t *brushes[OZ_SOLIDS_LIST];
	static int leafs[OZ_SOLIDS_LIST];
	int i, j, k, f, n, last;

	if ( !cm.numNodes ) {
		return;
	}
	if ( model ) {
		const cmodel_t *cmod = CM_ClipHandleToModel( model );
		for ( i = 0; i < cmod->leaf.numLeafBrushes; i++ ) {
			const int b = cm.leafbrushes[cmod->leaf.firstLeafBrush + i];
			if ( BoxesTouch( mins, maxs, (const vec3_t *)cm.brushes[b].bounds ) ) {
				Brush( &cm.brushes[b], b, mask, solid, ctx );
			}
		}
		return;
	}
	n = CM_BoxBrushes( mins, maxs, brushes, OZ_SOLIDS_LIST );
	for ( i = 0; i < n; i++ ) {
		Brush( brushes[i], brushes[i] - cm.brushes, mask, solid, ctx );
	}
	// curved surfaces: each facet's surface plane, its borders and a back
	n = CM_BoxLeafnums( mins, maxs, leafs, OZ_SOLIDS_LIST, &last );
	cm.checkcount++;
	for ( i = 0; i < n; i++ ) {
		const cLeaf_t *leaf = &cm.leafs[leafs[i]];
		for ( k = 0; k < leaf->numLeafSurfaces; k++ ) {
			const int s = cm.leafsurfaces[leaf->firstLeafSurface + k];
			cPatch_t *patch = cm.surfaces[s];
			const patchCollide_t *pc;

			if ( !patch || patch->checkcount == cm.checkcount || !( patch->contents & mask ) || !patch->pc ) {
				continue;
			}
			patch->checkcount = cm.checkcount;
			pc = patch->pc;
			if ( !BoxesTouch( mins, maxs, (const vec3_t *)pc->bounds ) ) {
				continue;
			}
			for ( f = 0; f < pc->numFacets && f < 4096; f++ ) {
				const facet_t *facet = &pc->facets[f];
				const float *sp = pc->planes[facet->surfacePlane].plane;
				float planes[( 2 + 4 + 6 + 16 ) * 4];
				int np = 0;

				Vector4Copy( sp, planes );
				VectorNegate( sp, planes + 4 );
				planes[7] = -( sp[3] - OZ_PATCH_THICKNESS );
				np = 2;
				for ( j = 0; j < facet->numBorders; j++, np++ ) {
					const float *bp = pc->planes[facet->borderPlanes[j]].plane;
					if ( facet->borderInward[j] ) {
						VectorNegate( bp, planes + 4 * np );
						planes[4 * np + 3] = -bp[3];
					} else {
						Vector4Copy( bp, planes + 4 * np );
					}
				}
				solid( ctx, -( s * 4096 + f + 1 ), planes, np );
			}
		}
	}
}
