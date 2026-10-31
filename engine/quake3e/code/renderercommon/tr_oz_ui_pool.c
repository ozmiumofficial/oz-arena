/*
===========================================================================
Oz: the native UI's geometry pool (renderercommon/tr_oz_ui_pool.h)
===========================================================================
*/
#include "../qcommon/q_shared.h"
#include "../renderercommon/tr_public.h"
#include "tr_oz_ui_pool.h"


/*
================
Ranges: first fit in a sorted free list
================
*/
static void UIRanges_Insert( uiRanges_t *r, int at, int offset, int size ) {
	if ( r->numFree == r->maxFree ) {
		const int newMax = r->maxFree ? r->maxFree * 2 : 32;
		uiRange_t *grown = ri.Malloc( newMax * sizeof( uiRange_t ) );
		if ( r->free ) {
			Com_Memcpy( grown, r->free, r->numFree * sizeof( uiRange_t ) );
			ri.Free( r->free );
		}
		r->free = grown;
		r->maxFree = newMax;
	}
	memmove( &r->free[at + 1], &r->free[at], ( r->numFree - at ) * sizeof( uiRange_t ) );
	r->free[at].offset = offset;
	r->free[at].size = size;
	r->numFree++;
}


static void UIRanges_Remove( uiRanges_t *r, int at ) {
	memmove( &r->free[at], &r->free[at + 1], ( r->numFree - at - 1 ) * sizeof( uiRange_t ) );
	r->numFree--;
}


static int UIRanges_Alloc( uiRanges_t *r, int size ) {
	int i, offset;

	for ( i = 0; i < r->numFree; i++ ) {
		if ( r->free[i].size >= size ) {
			offset = r->free[i].offset;
			r->free[i].offset += size;
			r->free[i].size -= size;
			if ( !r->free[i].size ) {
				UIRanges_Remove( r, i );
			}
			return offset;
		}
	}
	return -1;
}


static void UIRanges_Free( uiRanges_t *r, int offset, int size ) {
	int i;

	for ( i = 0; i < r->numFree && r->free[i].offset < offset; i++ ) {
	}
	// merge with the range before, the one after, or both
	if ( i > 0 && r->free[i - 1].offset + r->free[i - 1].size == offset ) {
		r->free[i - 1].size += size;
		if ( i < r->numFree && offset + size == r->free[i].offset ) {
			r->free[i - 1].size += r->free[i].size;
			UIRanges_Remove( r, i );
		}
		return;
	}
	if ( i < r->numFree && offset + size == r->free[i].offset ) {
		r->free[i].offset = offset;
		r->free[i].size += size;
		return;
	}
	UIRanges_Insert( r, i, offset, size );
}


static void UIRanges_Reset( uiRanges_t *r, int capacity ) {
	r->numFree = 0;
	UIRanges_Insert( r, 0, 0, capacity );
}


static void UIRanges_Clear( uiRanges_t *r ) {
	if ( r->free ) {
		ri.Free( r->free );
	}
	Com_Memset( r, 0, sizeof( *r ) );
}


/*
================
Chunks
================
*/
static qboolean UIPool_Oversize( const uiPoolChunk_t *c ) {
	return c->numVerts > UI_POOL_CHUNK_VERTS || c->numIndexes > UI_POOL_CHUNK_INDEXES;
}


static void UIPool_FreeChunk( uiPool_t *p, int i ) {
	uiPoolChunk_t *c = &p->chunks[i];

	p->freeChunk( i );
	UIRanges_Clear( &c->verts );
	UIRanges_Clear( &c->indexes );
	Com_Memset( c, 0, sizeof( *c ) );
}


// a chunk with room for both, made if none has it
static int UIPool_FindChunk( uiPool_t *p, int numVerts, int numIndexes, int *firstVert, int *firstIndex ) {
	uiPoolChunk_t *c;
	int i, free = -1;

	for ( i = 0; i < UI_POOL_MAX_CHUNKS; i++ ) {
		c = &p->chunks[i];
		if ( !c->used ) {
			if ( free < 0 ) {
				free = i;
			}
			continue;
		}
		if ( UIPool_Oversize( c ) ) {
			continue;	// its one geometry's own
		}
		*firstVert = UIRanges_Alloc( &c->verts, numVerts );
		if ( *firstVert < 0 ) {
			continue;
		}
		*firstIndex = UIRanges_Alloc( &c->indexes, numIndexes );
		if ( *firstIndex < 0 ) {
			UIRanges_Free( &c->verts, *firstVert, numVerts );
			continue;
		}
		return i;
	}

	if ( free < 0 ) {
		ri.Printf( PRINT_WARNING, "UI: geometry pool full (%i chunks)\n", UI_POOL_MAX_CHUNKS );
		return -1;
	}
	c = &p->chunks[free];
	c->numVerts = MAX( numVerts, UI_POOL_CHUNK_VERTS );
	c->numIndexes = MAX( numIndexes, UI_POOL_CHUNK_INDEXES );
	if ( UIPool_Oversize( c ) ) {
		c->numVerts = numVerts;		// exactly this one's size
		c->numIndexes = numIndexes;
	}
	if ( !p->makeChunk( free, c->numVerts, c->numIndexes ) ) {
		Com_Memset( c, 0, sizeof( *c ) );
		return -1;
	}
	c->used = 1;
	UIRanges_Reset( &c->verts, c->numVerts );
	UIRanges_Reset( &c->indexes, c->numIndexes );
	*firstVert = UIRanges_Alloc( &c->verts, numVerts );
	*firstIndex = UIRanges_Alloc( &c->indexes, numIndexes );
	return free;
}


/*
================
Geometry
================
*/
int UIPool_Compile( uiPool_t *p, const ozUiVertex_t *verts, int numVerts, const int *indexes, int numIndexes ) {
	uiPoolGeometry_t *g;
	int chunk, firstVert, firstIndex, h;

	if ( numVerts <= 0 || numIndexes <= 0 ) {
		return 0;
	}
	chunk = UIPool_FindChunk( p, numVerts, numIndexes, &firstVert, &firstIndex );
	if ( chunk < 0 ) {
		return 0;
	}

	if ( !p->firstFree ) {
		// the handle table grows; handle h is geometry[h - 1], firstFree 0 is none
		const int newMax = p->maxGeometry ? p->maxGeometry * 2 : 1024;
		uiPoolGeometry_t *grown = ri.Malloc( newMax * sizeof( uiPoolGeometry_t ) );
		if ( p->geometry ) {
			Com_Memcpy( grown, p->geometry, p->maxGeometry * sizeof( uiPoolGeometry_t ) );
			ri.Free( p->geometry );
		}
		for ( h = p->maxGeometry + 1; h <= newMax; h++ ) {
			grown[h - 1].chunk = -1;
			grown[h - 1].nextFree = h < newMax ? h + 1 : 0;
		}
		p->firstFree = p->maxGeometry + 1;
		p->geometry = grown;
		p->maxGeometry = newMax;
	}
	h = p->firstFree;
	g = &p->geometry[h - 1];
	p->firstFree = g->nextFree;

	g->chunk = chunk;
	g->firstVert = firstVert;
	g->numVerts = numVerts;
	g->firstIndex = firstIndex;
	g->numIndexes = numIndexes;
	g->nextFree = 0;
	p->chunks[chunk].geometries++;

	p->upload( chunk, firstVert, verts, numVerts, firstIndex, indexes, numIndexes );
	return h;
}


const uiPoolGeometry_t *UIPool_Get( const uiPool_t *p, int handle ) {
	if ( handle <= 0 || handle > p->maxGeometry || p->geometry[handle - 1].chunk < 0 ) {
		return NULL;
	}
	return &p->geometry[handle - 1];
}


// the handle is free at once (RmlUi is done with it), its ranges after UI_POOL_DELAY frames
void UIPool_Release( uiPool_t *p, int handle ) {
	const int slot = p->frame % UI_POOL_DELAY;
	uiPoolGeometry_t *g;

	if ( !UIPool_Get( p, handle ) ) {
		return;
	}
	g = &p->geometry[handle - 1];
	if ( p->numReleased[slot] == p->maxReleased[slot] ) {
		const int newMax = p->maxReleased[slot] ? p->maxReleased[slot] * 2 : 256;
		int *grown = ri.Malloc( newMax * sizeof( int ) * 5 );
		if ( p->released[slot] ) {
			Com_Memcpy( grown, p->released[slot], p->numReleased[slot] * sizeof( int ) * 5 );
			ri.Free( p->released[slot] );
		}
		p->released[slot] = grown;
		p->maxReleased[slot] = newMax;
	}
	{
		int *r = &p->released[slot][p->numReleased[slot]++ * 5];
		r[0] = g->chunk;
		r[1] = g->firstVert;
		r[2] = g->numVerts;
		r[3] = g->firstIndex;
		r[4] = g->numIndexes;
	}
	g->chunk = -1;
	g->nextFree = p->firstFree;
	p->firstFree = handle;
}


void UIPool_Frame( uiPool_t *p ) {
	int slot, i;

	p->frame++;
	slot = p->frame % UI_POOL_DELAY;
	for ( i = 0; i < p->numReleased[slot]; i++ ) {
		const int *r = &p->released[slot][i * 5];
		uiPoolChunk_t *c = &p->chunks[r[0]];

		if ( !c->used ) {
			continue;
		}
		if ( --c->geometries == 0 && UIPool_Oversize( c ) ) {
			UIPool_FreeChunk( p, r[0] );
			continue;
		}
		UIRanges_Free( &c->verts, r[1], r[2] );
		UIRanges_Free( &c->indexes, r[3], r[4] );
	}
	p->numReleased[slot] = 0;
}


void UIPool_Shutdown( uiPool_t *p ) {
	int i;

	for ( i = 0; i < UI_POOL_MAX_CHUNKS; i++ ) {
		if ( p->chunks[i].used ) {
			UIPool_FreeChunk( p, i );
		}
	}
	for ( i = 0; i < UI_POOL_DELAY; i++ ) {
		if ( p->released[i] ) {
			ri.Free( p->released[i] );
		}
		p->released[i] = NULL;
		p->numReleased[i] = p->maxReleased[i] = 0;
	}
	if ( p->geometry ) {
		ri.Free( p->geometry );
	}
	p->geometry = NULL;
	p->maxGeometry = p->firstFree = 0;
	p->frame = 0;
}


/*
================
Blur
================
*/
void UI_SigmaToParameters( float desired, int *passLevel, float *sigma ) {
	const int maxPasses = 10;
	const float maxSinglePassSigma = 3.0f;
	int level = 0;
	int v = (int)( desired * ( 2.0f / maxSinglePassSigma ) );

	while ( v > 1 ) {	// log2
		v >>= 1;
		level++;
	}
	*passLevel = (int)Com_Clamp( 0, maxPasses, level );
	*sigma = Com_Clamp( 0.0f, maxSinglePassSigma, desired / (float)( 1 << *passLevel ) );
}


void UI_BlurWeights( float sigma, float weights[UI_BLUR_WEIGHTS] ) {
	float normalization = 0.0f;
	int i;

	for ( i = 0; i < UI_BLUR_WEIGHTS; i++ ) {
		if ( fabs( sigma ) < 0.1f ) {
			weights[i] = ( i == 0 ) ? 1.0f : 0.0f;
		} else {
			weights[i] = expf( -(float)( i * i ) / ( 2.0f * sigma * sigma ) ) / ( sqrtf( 2.0f * M_PI ) * sigma );
		}
		normalization += ( i == 0 ? 1.0f : 2.0f ) * weights[i];
	}
	for ( i = 0; i < UI_BLUR_WEIGHTS; i++ ) {
		weights[i] /= normalization;
	}
}
