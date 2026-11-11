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

The live light's pages are lightmaps in tr.lightmaps after the world's (a thumbnail's compile, when one
is loaded), so a shader found with a page's index (RegisterLitShader) is drawn as a map's lit surface:
the same collapse into lightall, the same overbright shift of the texels.
===========================================================================
*/
#include "tr_local.h"

#define OZ_ED_MAX_MESHES	768
#define OZ_ED_MAX_LIGHTMAPS	32
#define OZ_ED_PROBE_REACH	96.0f

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
	// the live light: its pages, after the world's lightmaps in lmArray (tr.lightmaps while it has pages)
	image_t			*pages[OZ_ED_MAX_LIGHTMAPS];
	int				numPages;
	int				worldLightmaps;
	image_t			**lmArray;
	ozEdLightProbe_t	*probes;
	int				numProbes, maxProbes;
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


static int OzEd_DefineShader( const char *name, const char *text ) {
	const shader_t *sh = R_OzDefineShader( name, text );
	return sh ? sh->index : 0;
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
		const float *lm = v->lm;	// the live light's page, for a lit shader's range

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
** the live light's pages and probes
*/

// The pages are still the renderer's lightmaps: no world was loaded over them since they were made.
static qboolean PagesValid( void ) {
	return ozEd.numPages > 0 && ozEd.lmArray && tr.lightmaps == ozEd.lmArray ? qtrue : qfalse;
}


static int OzEd_CreateLightmap( int width, int height ) {
	image_t **arr, *img;
	byte *black;
	int i;

	if ( width <= 0 || height <= 0 || width > glConfig.maxTextureSize || height > glConfig.maxTextureSize ) {
		return -1;
	}
	if ( ozEd.numPages && !PagesValid() ) {
		ozEd.numPages = 0;	// a world came in over them (its own lightmaps): start again after it
		ozEd.lmArray = NULL;
	}
	if ( ozEd.numPages >= OZ_ED_MAX_LIGHTMAPS ) {
		return -1;
	}
	if ( !ozEd.numPages ) {
		ozEd.worldLightmaps = tr.lightmaps ? tr.numLightmaps : 0;
	}
	R_IssuePendingRenderCommands();
	black = ri.Hunk_AllocateTempMemory( width * height * 4 );
	Com_Memset( black, 0, width * height * 4 );
	for ( i = 0; i < width * height; i++ ) {
		black[i * 4 + 3] = 255;
	}
	img = R_CreateImage( va( "*ozEdLightmap%d", ozEd.numPages ), black, width, height, IMGTYPE_COLORALPHA,
		IMGFLAG_NOLIGHTSCALE | IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, GL_RGBA8 );
	ri.Hunk_FreeTempMemory( black );
	if ( !img ) {
		return -1;
	}
	ozEd.pages[ozEd.numPages] = img;

	// the world's lightmaps, then the pages
	arr = ri.Malloc( ( ozEd.worldLightmaps + ozEd.numPages + 1 ) * sizeof( *arr ) );
	for ( i = 0; i < ozEd.worldLightmaps; i++ ) {
		arr[i] = tr.lightmaps[i];
	}
	for ( i = 0; i <= ozEd.numPages; i++ ) {
		arr[ozEd.worldLightmaps + i] = ozEd.pages[i];
	}
	if ( ozEd.lmArray ) {
		ri.Free( ozEd.lmArray );
	}
	ozEd.lmArray = arr;
	tr.lightmaps = arr;
	tr.numLightmaps = ozEd.worldLightmaps + ozEd.numPages + 1;
	return ozEd.numPages++;
}


// a map's lightmap texel as the renderer shifts it (tr_bsp.c R_ColorShiftLightingBytes): by the map's
// overbright bits less the screen's, normalized by color past 255
static void ShiftTexel( const byte in[3], byte out[4] ) {
	const int shift = r_mapOverBrightBits->integer - tr.overbrightBits;
	int r = shift >= 0 ? in[0] << shift : in[0] >> -shift;
	int g = shift >= 0 ? in[1] << shift : in[1] >> -shift;
	int b = shift >= 0 ? in[2] << shift : in[2] >> -shift;

	if ( ( r | g | b ) > 255 ) {
		int max = r > g ? r : g;
		max = max > b ? max : b;
		r = r * 255 / max;
		g = g * 255 / max;
		b = b * 255 / max;
	}
	out[0] = r;
	out[1] = g;
	out[2] = b;
	out[3] = 255;
}


static void OzEd_UploadLightmap( int lightmap, int x, int y, int width, int height, const unsigned char *rgb ) {
	image_t *img;
	byte *data;
	int i;

	if ( !PagesValid() || lightmap < 0 || lightmap >= ozEd.numPages || !rgb || width <= 0 || height <= 0 ) {
		return;
	}
	img = ozEd.pages[lightmap];
	if ( x < 0 || y < 0 || x + width > img->width || y + height > img->height ) {
		return;
	}
	R_IssuePendingRenderCommands();	// what's queued draws with the texels as they were
	data = ri.Hunk_AllocateTempMemory( width * height * 4 );
	for ( i = 0; i < width * height; i++ ) {
		ShiftTexel( rgb + i * 3, data + i * 4 );
	}
	R_UpdateSubImage( img, data, x, y, width, height, GL_RGBA8 );
	ri.Hunk_FreeTempMemory( data );
}


static int OzEd_RegisterLitShader( const char *name, int lightmap ) {
	shader_t *sh;

	if ( !name || !name[0] || !PagesValid() || lightmap < 0 || lightmap >= ozEd.numPages ) {
		return 0;
	}
	sh = R_FindShader( name, ozEd.worldLightmaps + lightmap, qtrue );
	return sh->defaultShader ? 0 : sh->index;
}


static void OzEd_SetLightProbes( const ozEdLightProbe_t *probes, int count ) {
	if ( count <= 0 || !probes ) {
		ozEd.numProbes = 0;
		return;
	}
	if ( count > ozEd.maxProbes ) {
		if ( ozEd.probes ) {
			ri.Free( ozEd.probes );
		}
		ozEd.maxProbes = count + 64;
		ozEd.probes = ri.Malloc( ozEd.maxProbes * sizeof( *ozEd.probes ) );
	}
	Com_Memcpy( ozEd.probes, probes, count * sizeof( *probes ) );
	ozEd.numProbes = count;
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
	const ozEdLightProbe_t *best = NULL;

	// a model (not the editor's own meshes) where the live light reaches: the nearest probe's light
	if ( ozEd.numProbes > 0 && ent->e.reType == RT_MODEL ) {
		const model_t *mod = R_GetModelByHandle( ent->e.hModel );
		if ( mod && mod->type != MOD_OZEDMESH ) {
			const float *at = ( ent->e.renderfx & RF_LIGHTING_ORIGIN ) ? ent->e.lightingOrigin : ent->e.origin;
			float bestD = OZ_ED_PROBE_REACH * OZ_ED_PROBE_REACH;
			int i;
			for ( i = 0; i < ozEd.numProbes; i++ ) {
				vec3_t d;
				float dd;
				VectorSubtract( at, ozEd.probes[i].origin, d );
				dd = DotProduct( d, d );
				if ( dd < bestD ) {
					bestD = dd;
					best = &ozEd.probes[i];
				}
			}
		}
	}
	if ( best ) {
		VectorScale( best->ambient, tr.identityLight, ent->ambientLight );
		VectorScale( best->directed, tr.identityLight, ent->directedLight );
		VectorNormalize2( best->dir, ent->lightDir );
		if ( VectorLength( ent->lightDir ) == 0.0f ) {
			VectorCopy( ozEd.dir, ent->lightDir );
		}
		return;
	}
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
	OzEd_DefineShader,
	OzEd_CreateLightmap,
	OzEd_UploadLightmap,
	OzEd_RegisterLitShader,
	OzEd_SetLightProbes,
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

	// the live light's pages go with the images (R_DeleteTextures); the list naming them with the world's
	if ( ozEd.lmArray ) {
		if ( tr.lightmaps == ozEd.lmArray ) {
			tr.lightmaps = NULL;
			tr.numLightmaps = 0;
		}
		ri.Free( ozEd.lmArray );
	}
	ozEd.lmArray = NULL;
	ozEd.numPages = 0;
	ozEd.worldLightmaps = 0;
	Com_Memset( ozEd.pages, 0, sizeof( ozEd.pages ) );
	if ( ozEd.probes ) {
		ri.Free( ozEd.probes );
	}
	ozEd.probes = NULL;
	ozEd.numProbes = ozEd.maxProbes = 0;
}
