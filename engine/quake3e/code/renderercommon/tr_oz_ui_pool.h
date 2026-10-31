/*
===========================================================================
Oz: the native UI's geometry pool (ENGINE_PLAN.md P4, renderercommon/tr_oz_ui.h)

The HUD compiles and releases many small geometries every frame. The renderers (oz, opengl and
vulkan) keep them in a few large buffers ("chunks", vertexes and indexes apart) and hand out ranges of
them, so a compile is a copy into a mapped or sub-uploaded range, not a buffer made and deleted.

A released range comes back UI_POOL_DELAY frames later (UIPool_Frame), so a frame still on the GPU
never reads data written over it. A geometry larger than a chunk gets a chunk of its own, freed
when it comes back.

The renderer does the buffers: it makes a chunk, copies into it and frees it through the
callbacks; the pool does the bookkeeping.
===========================================================================
*/
#ifndef TR_OZ_UI_POOL_H
#define TR_OZ_UI_POOL_H

#include "tr_oz_ui.h"

#define UI_POOL_DELAY			3		// frames before a released range is reused
#define UI_POOL_MAX_CHUNKS		64
#define UI_POOL_CHUNK_VERTS		65536
#define UI_POOL_CHUNK_INDEXES	( UI_POOL_CHUNK_VERTS * 3 )

typedef struct {
	int		offset, size;
} uiRange_t;

typedef struct {
	uiRange_t	*free;		// sorted by offset, neighbors merged
	int			numFree, maxFree;
} uiRanges_t;

typedef struct {
	int		chunk;			// -1: a free handle
	int		firstVert, numVerts;
	int		firstIndex, numIndexes;
	int		nextFree;
} uiPoolGeometry_t;

typedef struct {
	int		used;
	int		numVerts, numIndexes;	// capacity
	int		geometries;				// live or waiting to come back
	uiRanges_t	verts, indexes;
} uiPoolChunk_t;

typedef struct {
	// the renderer's buffers; a chunk's index is the renderer's too
	int		(*makeChunk)( int chunk, int numVerts, int numIndexes );		// 0 on failure
	void	(*freeChunk)( int chunk );
	void	(*upload)( int chunk, int firstVert, const ozUiVertex_t *verts, int numVerts, int firstIndex, const int *indexes, int numIndexes );

	uiPoolChunk_t		chunks[UI_POOL_MAX_CHUNKS];
	uiPoolGeometry_t	*geometry;
	int					maxGeometry, firstFree;
	int					*released[UI_POOL_DELAY];	// geometry handles whose ranges wait
	int					numReleased[UI_POOL_DELAY], maxReleased[UI_POOL_DELAY];
	int					frame;
} uiPool_t;

int		UIPool_Compile( uiPool_t *p, const ozUiVertex_t *verts, int numVerts, const int *indexes, int numIndexes );	// a handle > 0, 0 on failure
void	UIPool_Release( uiPool_t *p, int handle );
const uiPoolGeometry_t *UIPool_Get( const uiPool_t *p, int handle );	// NULL for none
void	UIPool_Frame( uiPool_t *p );		// a new frame: ranges released UI_POOL_DELAY frames ago come back
void	UIPool_Shutdown( uiPool_t *p );	// frees every chunk (the renderer's objects too) and the tables

// RmlUi's blur, shared by the backends: downscale passes for a large sigma, a 7-tap Gaussian
#define UI_BLUR_SIZE		7
#define UI_BLUR_WEIGHTS		( ( UI_BLUR_SIZE + 1 ) / 2 )

void	UI_SigmaToParameters( float desired, int *passLevel, float *sigma );
void	UI_BlurWeights( float sigma, float weights[UI_BLUR_WEIGHTS] );

#endif
