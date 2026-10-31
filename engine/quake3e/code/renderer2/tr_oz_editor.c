/*
===========================================================================
Oz: the map editor's meshes and view (ENGINE_PLAN.md P13, engine/EDITOR.md)

The table in renderercommon/tr_oz_editor.h. A mesh is a model of its own type (MOD_OZEDMESH), so it
draws as an RT_MODEL refEntity through the usual entity path: placed by the entity, culled by its
bounds, lit by R_SetupEntityLighting (in an RDF_OZ_EDITOR scene the editor's view light, below).
Each range of its indexes is a surface (SF_OZ_EDMESH) drawn from the mesh's own vertex array, as
RB_SurfaceVaoMdvMesh draws an md3's.

Model slots live until the renderer shuts down (R_AllocModel has no free), so a freed mesh keeps its
slot for the next CreateMesh, and its buffers go back to GL.
===========================================================================
*/
#include "tr_local.h"

#define OZ_ED_MAX_MESHES	768

typedef struct srfOzEdRange_s {
	surfaceType_t		surfaceType;	// SF_OZ_EDMESH
	struct ozEdMesh_s	*mesh;
	shader_t			*shader;
	int					firstIndex;
	int					numIndexes;
} srfOzEdRange_t;

typedef struct ozEdMesh_s {
	model_t				*model;
	qboolean			used;
	qboolean			lines;
	vao_t				vao;			// its own GL objects (none until the first upload)
	int					numVerts;
	int					numRanges, maxRanges;
	srfOzEdRange_t		*ranges;
	vec3_t				bounds[2];
} ozEdMesh_t;

static struct {
	ozEdMesh_t		meshes[OZ_ED_MAX_MESHES];
	ozEdMesh_t		*byModel[MAX_MOD_KNOWN];
	vec3_t			ambient, directed, dir;
} ozEd;


static ozEdMesh_t *MeshForHandle( int handle ) {
	if ( handle <= 0 || handle >= MAX_MOD_KNOWN ) {
		return NULL;
	}
	return ozEd.byModel[handle];
}


static int OzEd_RegisterShader( const char *name ) {
	shader_t *sh;

	if ( !name || !name[0] ) {
		return 0;
	}
	sh = R_OzEdLook( name );
	if ( !sh ) {
		sh = R_FindShader( name, LIGHTMAP_OZ_EDITOR, qtrue );
	}
	return sh->defaultShader ? 0 : sh->index;
}


static void OzEd_ShaderImageSize( int handle, int *width, int *height ) {
	const shader_t *sh = R_GetShaderByHandle( handle );
	int i;

	*width = *height = 0;
	for ( i = 0; i < MAX_SHADER_STAGES; i++ ) {
		const shaderStage_t *st = sh->stages[i];
		if ( !st || !st->active ) {
			break;
		}
		if ( st->bundle[0].image[0] && !st->bundle[0].isLightmap ) {
			*width = st->bundle[0].image[0]->uploadWidth;
			*height = st->bundle[0].image[0]->uploadHeight;
			return;
		}
	}
}


static int OzEd_CreateMesh( int lines ) {
	ozEdMesh_t *m = NULL;
	int i;

	for ( i = 0; i < OZ_ED_MAX_MESHES; i++ ) {
		if ( !ozEd.meshes[i].used && ozEd.meshes[i].model ) {
			m = &ozEd.meshes[i];	// a freed one: its model slot comes back
			break;
		}
	}
	for ( i = 0; !m && i < OZ_ED_MAX_MESHES; i++ ) {
		if ( !ozEd.meshes[i].used && !ozEd.meshes[i].model ) {
			model_t *mod = R_AllocModel();
			if ( !mod ) {
				return 0;
			}
			mod->type = MOD_OZEDMESH;
			Com_sprintf( mod->name, sizeof( mod->name ), "*ozed%d", i );
			m = &ozEd.meshes[i];
			m->model = mod;
			ozEd.byModel[mod->index] = m;
		}
	}
	if ( !m ) {
		ri.Printf( PRINT_WARNING, "map editor: out of meshes (%d)\n", OZ_ED_MAX_MESHES );
		return 0;
	}
	m->used = qtrue;
	m->lines = lines ? qtrue : qfalse;
	m->numVerts = 0;
	m->numRanges = 0;
	ClearBounds( m->bounds[0], m->bounds[1] );
	return m->model->index;
}


static void ReleaseBuffers( ozEdMesh_t *m ) {
	if ( glState.currentVao == &m->vao ) {
		glState.currentVao = NULL;
		if ( glRefConfig.vertexArrayObject ) {
			qglBindVertexArray( 0 );
		}
	}
	if ( m->vao.vertexesVBO ) {
		qglDeleteBuffers( 1, &m->vao.vertexesVBO );
	}
	if ( m->vao.indexesIBO ) {
		qglDeleteBuffers( 1, &m->vao.indexesIBO );
	}
	if ( m->vao.vao ) {
		qglDeleteVertexArrays( 1, &m->vao.vao );
	}
	Com_Memset( &m->vao, 0, sizeof( m->vao ) );
}


static void OzEd_FreeMesh( int handle ) {
	ozEdMesh_t *m = MeshForHandle( handle );

	if ( !m || !m->used ) {
		return;
	}
	R_IssuePendingRenderCommands();	// nothing queued may still draw from its buffers
	ReleaseBuffers( m );
	if ( m->ranges ) {
		ri.Free( m->ranges );
	}
	m->ranges = NULL;
	m->numRanges = m->maxRanges = 0;
	m->numVerts = 0;
	m->used = qfalse;
}


// The attributes of R_CreateVao2's interleaved vertex (srfVert_t's, in its order).
static void SetAttribs( vao_t *vao ) {
	static const struct { int index, count, type, normalized, size; } layout[] = {
		{ ATTR_INDEX_POSITION, 3, GL_FLOAT, GL_FALSE, 12 },
		{ ATTR_INDEX_NORMAL, 4, GL_SHORT, GL_TRUE, 8 },
		{ ATTR_INDEX_TANGENT, 4, GL_SHORT, GL_TRUE, 8 },
		{ ATTR_INDEX_TEXCOORD, 2, GL_FLOAT, GL_FALSE, 8 },
		{ ATTR_INDEX_LIGHTCOORD, 2, GL_FLOAT, GL_FALSE, 8 },
		{ ATTR_INDEX_COLOR, 4, GL_UNSIGNED_SHORT, GL_TRUE, 8 },
		{ ATTR_INDEX_LIGHTDIRECTION, 4, GL_SHORT, GL_TRUE, 8 },
	};
	int i, offset = 0;

	for ( i = 0; i < (int)ARRAY_LEN( layout ); i++ ) {
		vaoAttrib_t *a = &vao->attribs[layout[i].index];
		a->enabled = 1;
		a->count = layout[i].count;
		a->type = layout[i].type;
		a->normalized = layout[i].normalized;
		a->offset = offset;
		offset += layout[i].size;
	}
	for ( i = 0; i < (int)ARRAY_LEN( layout ); i++ ) {
		vao->attribs[layout[i].index].stride = offset;
	}
}

#define OZ_ED_VERTEX_SIZE	60	// SetAttribs' stride


static void OzEd_UploadMesh( int handle, const ozEdVertex_t *verts, int numVerts, const unsigned short *indexes,
		int numIndexes, const ozEdRange_t *ranges, int numRanges ) {
	ozEdMesh_t *m = MeshForHandle( handle );
	byte *data, *p;
	int i;

	if ( !m || !m->used ) {
		return;
	}
	if ( numVerts > 65536 ) {
		ri.Printf( PRINT_WARNING, "map editor: a mesh of %d vertexes (65536 at most)\n", numVerts );
		numVerts = 0;
	}
	R_IssuePendingRenderCommands();	// what's queued draws from the old buffers

	// ranges
	if ( numRanges > m->maxRanges ) {
		if ( m->ranges ) {
			ri.Free( m->ranges );
		}
		m->maxRanges = numRanges + 8;
		m->ranges = ri.Malloc( m->maxRanges * sizeof( *m->ranges ) );
	}
	m->numRanges = 0;
	for ( i = 0; i < numRanges && numVerts > 0; i++ ) {
		srfOzEdRange_t *r = &m->ranges[m->numRanges];
		if ( ranges[i].numIndexes <= 0 || ranges[i].firstIndex < 0 || ranges[i].firstIndex + ranges[i].numIndexes > numIndexes ) {
			continue;
		}
		r->surfaceType = SF_OZ_EDMESH;
		r->mesh = m;
		r->shader = R_GetShaderByHandle( ranges[i].shader );
		r->firstIndex = ranges[i].firstIndex;
		r->numIndexes = ranges[i].numIndexes;
		m->numRanges++;
	}

	m->numVerts = numVerts;
	ClearBounds( m->bounds[0], m->bounds[1] );
	if ( numVerts <= 0 || numIndexes <= 0 ) {
		m->numRanges = 0;
		return;
	}

	// vertexes, interleaved as R_CreateVao2 lays them out
	data = ri.Hunk_AllocateTempMemory( numVerts * OZ_ED_VERTEX_SIZE );
	p = data;
	for ( i = 0; i < numVerts; i++ ) {
		const ozEdVertex_t *v = &verts[i];
		vec3_t n;
		vec4_t t, c;
		int16_t packed[4];
		uint16_t color[4];
		float lm[2] = { 0, 0 };

		AddPointToBounds( v->xyz, m->bounds[0], m->bounds[1] );
		Com_Memcpy( p, v->xyz, 12 ); p += 12;
		VectorCopy( v->normal, n );
		R_VaoPackNormal( packed, n ); Com_Memcpy( p, packed, 8 ); p += 8;
		Vector4Copy( v->tangent, t );
		R_VaoPackTangent( packed, t ); Com_Memcpy( p, packed, 8 ); p += 8;
		Com_Memcpy( p, v->st, 8 ); p += 8;
		Com_Memcpy( p, lm, 8 ); p += 8;
		Vector4Set( c, v->color[0] / 255.0f, v->color[1] / 255.0f, v->color[2] / 255.0f, v->color[3] / 255.0f );
		R_VaoPackColor( color, c ); Com_Memcpy( p, color, 8 ); p += 8;
		R_VaoPackNormal( packed, n ); Com_Memcpy( p, packed, 8 ); p += 8;	// light direction: along the normal
	}

	if ( !m->vao.vertexesVBO ) {
		Q_strncpyz( m->vao.name, m->model->name, sizeof( m->vao.name ) );
		SetAttribs( &m->vao );
		if ( glRefConfig.vertexArrayObject ) {
			qglGenVertexArrays( 1, &m->vao.vao );
		}
		qglGenBuffers( 1, &m->vao.vertexesVBO );
		qglGenBuffers( 1, &m->vao.indexesIBO );
	}
	if ( glRefConfig.vertexArrayObject ) {
		qglBindVertexArray( m->vao.vao );
	}
	m->vao.vertexesSize = numVerts * OZ_ED_VERTEX_SIZE;
	qglBindBuffer( GL_ARRAY_BUFFER, m->vao.vertexesVBO );
	qglBufferData( GL_ARRAY_BUFFER, m->vao.vertexesSize, data, GL_DYNAMIC_DRAW );
	m->vao.indexesSize = numIndexes * sizeof( glIndex_t );
	qglBindBuffer( GL_ELEMENT_ARRAY_BUFFER, m->vao.indexesIBO );
	qglBufferData( GL_ELEMENT_ARRAY_BUFFER, m->vao.indexesSize, indexes, GL_DYNAMIC_DRAW );
	Vao_SetVertexPointers( &m->vao );

	// nothing of the mesh stays bound: rend2 draws a batch's first 2D pic from whatever vertex array
	// is bound when its shader matches the last surface's (the editor's background fill drew from
	// these), so the next draw binds its own (no R_BindNullVao: its glGetError waits for the GPU)
	if ( glRefConfig.vertexArrayObject ) {
		qglBindVertexArray( 0 );
	} else {
		qglBindBuffer( GL_ARRAY_BUFFER, 0 );
		qglBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
	}
	glState.currentVao = NULL;
	tess.shader = NULL;

	ri.Hunk_FreeTempMemory( data );
}


static void OzEd_SetViewLight( const float ambient[3], const float directed[3], const float dir[3] ) {
	VectorCopy( ambient, ozEd.ambient );
	VectorCopy( directed, ozEd.directed );
	VectorNormalize2( dir, ozEd.dir );
}


/*
** the entity path (tr_main.c R_AddEntitySurfaces, tr_light.c, tr_model.c)
*/

void R_AddOzEdMeshSurfaces( trRefEntity_t *ent ) {
	ozEdMesh_t *m = MeshForHandle( tr.currentModel->index );
	int i;

	if ( !m || !m->used || m->numRanges == 0 ) {
		return;
	}
	if ( R_CullLocalBox( m->bounds ) == CULL_OUT ) {
		return;
	}
	if ( !m->lines ) {
		R_SetupEntityLighting( &tr.refdef, ent );
	}
	for ( i = 0; i < m->numRanges; i++ ) {
		shader_t *sh = ent->e.customShader ? R_GetShaderByHandle( ent->e.customShader ) : m->ranges[i].shader;
		R_AddDrawSurf( (surfaceType_t *)&m->ranges[i], sh, 0, qfalse, qfalse, 0 );
	}
}


void RB_SurfaceOzEdRange( void *surface ) {
	const srfOzEdRange_t *r = surface;

	if ( !r->mesh || !r->mesh->vao.vertexesVBO ) {
		return;
	}
	RB_EndSurface();
	RB_BeginSurface( tess.shader, tess.fogNum, tess.cubemapIndex );

	R_BindVao( (vao_t *)&r->mesh->vao );
	tess.useInternalVao = qfalse;
	tess.numIndexes = r->numIndexes;
	tess.firstIndex = r->firstIndex;
	tess.numVertexes = r->mesh->numVerts;
	tess.ozLines = r->mesh->lines;

	RB_EndSurface();
	tess.ozLines = qfalse;
}


qboolean R_OzEdMeshBounds( const model_t *model, vec3_t mins, vec3_t maxs ) {
	const ozEdMesh_t *m;

	if ( !model || model->type != MOD_OZEDMESH ) {
		return qfalse;
	}
	m = MeshForHandle( model->index );
	if ( m && m->used && m->numVerts ) {
		VectorCopy( m->bounds[0], mins );
		VectorCopy( m->bounds[1], maxs );
	} else {
		VectorClear( mins );
		VectorClear( maxs );
	}
	return qtrue;
}


void R_OzEdViewLight( trRefEntity_t *ent ) {
	VectorScale( ozEd.ambient, tr.identityLight, ent->ambientLight );
	VectorScale( ozEd.directed, tr.identityLight, ent->directedLight );
	VectorCopy( ozEd.dir, ent->lightDir );
}


static const ozEdRender_t ozEdRender = {
	OZ_ED_RENDER_VERSION,
	OzEd_RegisterShader,
	OzEd_ShaderImageSize,
	OzEd_CreateMesh,
	OzEd_UploadMesh,
	OzEd_FreeMesh,
	OzEd_SetViewLight,
};

const ozEdRender_t *R_OzEd_GetRender( void ) {
	if ( !glRefConfig.framebufferObject ) {
		return NULL;
	}
	if ( VectorLength( ozEd.dir ) == 0.0f ) {
		// a sun high to one side, as the editor starts
		const vec3_t ambient = { 90, 90, 96 }, directed = { 150, 146, 138 }, dir = { 0.4f, 0.25f, 0.88f };
		OzEd_SetViewLight( ambient, directed, dir );
	}
	return &ozEdRender;
}


void R_OzEd_Shutdown( void ) {
	int i;

	for ( i = 0; i < OZ_ED_MAX_MESHES; i++ ) {
		ozEdMesh_t *m = &ozEd.meshes[i];
		if ( m->vao.vertexesVBO || m->vao.vao ) {
			ReleaseBuffers( m );
		}
		if ( m->ranges ) {
			ri.Free( m->ranges );
		}
	}
	// the model slots go with the renderer (R_ModelInit starts the list again)
	Com_Memset( ozEd.meshes, 0, sizeof( ozEd.meshes ) );
	Com_Memset( ozEd.byModel, 0, sizeof( ozEd.byModel ) );
}
