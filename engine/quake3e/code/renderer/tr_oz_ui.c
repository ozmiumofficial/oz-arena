/*
===========================================================================
Oz: the native UI's renderer on Quake3e's opengl renderer (ENGINE_PLAN.md P4, renderercommon/tr_oz_ui.h)

The oz renderer's port of RmlUi 6.3's GL3 backend (renderer2/tr_oz_ui.c: Backends/RmlUi_Renderer_GL3.cpp,
MIT, Copyright (c) 2019-2023 The RmlUi Team and contributors) on this renderer's state. Its GLSL
programs, vertex array objects and the rest are loaded for the UI alone (qgl.h QGL_OZ_UI_PROCS,
GL 3.2 in the compatibility context the renderer gets). Textures go through GL_BindTexture,
blending and depth through GL_State, and the VBO binding is dropped first (VBO_UnBind), so the
renderer's caches stay true; the framebuffer bound when the UI began is bound again when it ends.

Geometry lives in the shared pool's chunks (renderercommon/tr_oz_ui_pool.c): a chunk is a VAO over
one vertex and one index buffer, and a compile is two sub-uploads.

The UI draws into layers of its own (sharing one depth-stencil buffer for clip masks); EndFrame
composites the base layer onto the framebuffer 2D draws to (the main FBO with r_fbo 1, the back
buffer without) with premultiplied blending.
===========================================================================
*/
#include "tr_local.h"
#include "../renderercommon/tr_oz_ui_pool.h"

extern void RB_SetGL2D( void );	// tr_backend.c, as tr_arb.c declares it

#define UI_MAX_LAYERS		8

typedef enum {
	UP_COLOR,
	UP_TEXTURE,
	UP_GRADIENT,
	UP_PASSTHROUGH,
	UP_COLOR_MATRIX,
	UP_BLEND_MASK,
	UP_BLUR,
	UP_DROP_SHADOW,
	UP_COUNT
} uiProgram_t;

typedef enum {
	UU_TRANSLATE, UU_TRANSFORM, UU_TEX, UU_TEX_MASK, UU_STRAIGHT, UU_COLOR, UU_COLOR_MATRIX,
	UU_TEXEL_OFFSET, UU_TEX_COORD_MIN, UU_TEX_COORD_MAX, UU_WEIGHTS, UU_FUNC, UU_P, UU_V,
	UU_STOP_COLORS, UU_STOP_POSITIONS, UU_NUM_STOPS, UU_UV_OFFSET, UU_UV_SCALE, UU_OPACITY,
	UU_COUNT
} uiUniform_t;

static const char *const uiUniformNames[UU_COUNT] = {
	"_translate", "_transform", "_tex", "_texMask", "_straight", "_color", "_color_matrix",
	"_texelOffset", "_texCoordMin", "_texCoordMax", "_weights", "_func", "_p", "_v",
	"_stop_colors", "_stop_positions", "_num_stops", "_uvOffset", "_uvScale", "_opacity"
};

// the shaders are the oz renderer's (RmlUi's GL3 ones); UI_CompileShader adds #version and the sizes

static const char uiVertMain[] =
	"uniform vec2 _translate;\n"
	"uniform mat4 _transform;\n"
	"in vec2 inPosition;\n"
	"in vec4 inColor0;\n"
	"in vec2 inTexCoord0;\n"
	"out vec2 fragTexCoord;\n"
	"out vec4 fragColor;\n"
	"void main() {\n"
	"	fragTexCoord = inTexCoord0;\n"
	"	fragColor = inColor0;\n"
	"	gl_Position = _transform * vec4(inPosition + _translate, 0.0, 1.0);\n"
	"}\n";

// the engine's images aren't premultiplied (_straight 1), RmlUi's own textures are
static const char uiFragTexture[] =
	"uniform sampler2D _tex;\n"
	"uniform float _straight;\n"
	"in vec2 fragTexCoord;\n"
	"in vec4 fragColor;\n"
	"out vec4 finalColor;\n"
	"void main() {\n"
	"	vec4 texColor = texture(_tex, fragTexCoord);\n"
	"	texColor.rgb *= mix(1.0, texColor.a, _straight);\n"
	"	finalColor = fragColor * texColor;\n"
	"}\n";

static const char uiFragColor[] =
	"in vec2 fragTexCoord;\n"
	"in vec4 fragColor;\n"
	"out vec4 finalColor;\n"
	"void main() {\n"
	"	finalColor = fragColor;\n"
	"}\n";

static const char uiFragGradient[] =
	"#define LINEAR 0\n"
	"#define RADIAL 1\n"
	"#define CONIC 2\n"
	"#define REPEATING_LINEAR 3\n"
	"#define REPEATING_RADIAL 4\n"
	"#define REPEATING_CONIC 5\n"
	"#define PI 3.14159265\n"
	"uniform int _func;\n"
	"uniform vec2 _p;\n"
	"uniform vec2 _v;\n"
	"uniform vec4 _stop_colors[MAX_NUM_STOPS];\n"
	"uniform float _stop_positions[MAX_NUM_STOPS];\n"
	"uniform int _num_stops;\n"
	"in vec2 fragTexCoord;\n"
	"in vec4 fragColor;\n"
	"out vec4 finalColor;\n"
	"vec4 mix_stop_colors(float t) {\n"
	"	vec4 color = _stop_colors[0];\n"
	"	for (int i = 1; i < _num_stops; i++)\n"
	"		color = mix(color, _stop_colors[i], smoothstep(_stop_positions[i-1], _stop_positions[i], t));\n"
	"	return color;\n"
	"}\n"
	"void main() {\n"
	"	float t = 0.0;\n"
	"	if (_func == LINEAR || _func == REPEATING_LINEAR) {\n"
	"		float dist_square = dot(_v, _v);\n"
	"		vec2 V = fragTexCoord - _p;\n"
	"		t = dot(_v, V) / dist_square;\n"
	"	} else if (_func == RADIAL || _func == REPEATING_RADIAL) {\n"
	"		vec2 V = fragTexCoord - _p;\n"
	"		t = length(_v * V);\n"
	"	} else if (_func == CONIC || _func == REPEATING_CONIC) {\n"
	"		mat2 R = mat2(_v.x, -_v.y, _v.y, _v.x);\n"
	"		vec2 V = R * (fragTexCoord - _p);\n"
	"		t = 0.5 + atan(-V.x, V.y) / (2.0 * PI);\n"
	"	}\n"
	"	if (_func == REPEATING_LINEAR || _func == REPEATING_RADIAL || _func == REPEATING_CONIC) {\n"
	"		float t0 = _stop_positions[0];\n"
	"		float t1 = _stop_positions[_num_stops - 1];\n"
	"		t = t0 + mod(t - t0, t1 - t0);\n"
	"	}\n"
	"	finalColor = fragColor * mix_stop_colors(t);\n"
	"}\n";

// full-screen passes; _uvOffset and _uvScale stand in for the GL3 backend's per-call quads
static const char uiVertPassthrough[] =
	"uniform vec2 _uvOffset;\n"
	"uniform vec2 _uvScale;\n"
	"in vec2 inPosition;\n"
	"in vec2 inTexCoord0;\n"
	"out vec2 fragTexCoord;\n"
	"void main() {\n"
	"	fragTexCoord = inTexCoord0 * _uvScale + _uvOffset;\n"
	"	gl_Position = vec4(inPosition, 0.0, 1.0);\n"
	"}\n";

// _opacity stands in for the GL3 backend's constant blend color
static const char uiFragPassthrough[] =
	"uniform sampler2D _tex;\n"
	"uniform float _opacity;\n"
	"in vec2 fragTexCoord;\n"
	"out vec4 finalColor;\n"
	"void main() {\n"
	"	finalColor = texture(_tex, fragTexCoord) * _opacity;\n"
	"}\n";

static const char uiFragColorMatrix[] =
	"uniform sampler2D _tex;\n"
	"uniform mat4 _color_matrix;\n"
	"in vec2 fragTexCoord;\n"
	"out vec4 finalColor;\n"
	"void main() {\n"
	"	vec4 texColor = texture(_tex, fragTexCoord);\n"
	"	vec3 transformedColor = vec3(_color_matrix * texColor);\n"
	"	finalColor = vec4(transformedColor, texColor.a);\n"
	"}\n";

static const char uiFragBlendMask[] =
	"uniform sampler2D _tex;\n"
	"uniform sampler2D _texMask;\n"
	"in vec2 fragTexCoord;\n"
	"out vec4 finalColor;\n"
	"void main() {\n"
	"	vec4 texColor = texture(_tex, fragTexCoord);\n"
	"	float maskAlpha = texture(_texMask, fragTexCoord).a;\n"
	"	finalColor = texColor * maskAlpha;\n"
	"}\n";

static const char uiVertBlur[] =
	"uniform vec2 _texelOffset;\n"
	"in vec2 inPosition;\n"
	"in vec2 inTexCoord0;\n"
	"out vec2 fragTexCoord[BLUR_SIZE];\n"
	"void main() {\n"
	"	for (int i = 0; i < BLUR_SIZE; i++)\n"
	"		fragTexCoord[i] = inTexCoord0 - float(i - BLUR_NUM_WEIGHTS + 1) * _texelOffset;\n"
	"	gl_Position = vec4(inPosition, 0.0, 1.0);\n"
	"}\n";

static const char uiFragBlur[] =
	"uniform sampler2D _tex;\n"
	"uniform float _weights[BLUR_NUM_WEIGHTS];\n"
	"uniform vec2 _texCoordMin;\n"
	"uniform vec2 _texCoordMax;\n"
	"in vec2 fragTexCoord[BLUR_SIZE];\n"
	"out vec4 finalColor;\n"
	"void main() {\n"
	"	vec4 color = vec4(0.0, 0.0, 0.0, 0.0);\n"
	"	for (int i = 0; i < BLUR_SIZE; i++) {\n"
	"		vec2 in_region = step(_texCoordMin, fragTexCoord[i]) * step(fragTexCoord[i], _texCoordMax);\n"
	"		color += texture(_tex, fragTexCoord[i]) * in_region.x * in_region.y * _weights[abs(i - BLUR_NUM_WEIGHTS + 1)];\n"
	"	}\n"
	"	finalColor = color;\n"
	"}\n";

static const char uiFragDropShadow[] =
	"uniform sampler2D _tex;\n"
	"uniform vec2 _texCoordMin;\n"
	"uniform vec2 _texCoordMax;\n"
	"uniform vec4 _color;\n"
	"in vec2 fragTexCoord;\n"
	"out vec4 finalColor;\n"
	"void main() {\n"
	"	vec2 in_region = step(_texCoordMin, fragTexCoord) * step(fragTexCoord, _texCoordMax);\n"
	"	finalColor = texture(_tex, fragTexCoord).a * in_region.x * in_region.y * _color;\n"
	"}\n";

static const struct {
	const char *name, *vert, *frag;
} uiProgramDefs[UP_COUNT] = {
	{ "color", uiVertMain, uiFragColor },
	{ "texture", uiVertMain, uiFragTexture },
	{ "gradient", uiVertMain, uiFragGradient },
	{ "passthrough", uiVertPassthrough, uiFragPassthrough },
	{ "color matrix", uiVertPassthrough, uiFragColorMatrix },
	{ "blend mask", uiVertPassthrough, uiFragBlendMask },
	{ "blur", uiVertBlur, uiFragBlur },
	{ "drop shadow", uiVertPassthrough, uiFragDropShadow }
};

typedef struct {
	GLuint		vao, vbo, ibo;
} uiChunk_t;

typedef struct {
	GLuint		texnum;
	qboolean	used;
	qboolean	owned;		// made here; an engine image belongs to the image list
	qboolean	straight;	// alpha not premultiplied (the engine's images)
	int			nextFree;
} uiTexture_t;

typedef struct {
	GLuint		fbo, color;
	int			width, height;
} uiTarget_t;

typedef struct {
	int			x0, y0, x1, y1;	// RmlUi's coordinates: from the top left
	qboolean	valid;
} uiRect_t;

static struct {
	qboolean		initialized, failed;

	GLuint			programs[UP_COUNT];
	GLint			uniforms[UP_COUNT][UU_COUNT];
	int				program;	// the one in use, -1 for none of ours
	qboolean		transformDirty[UP_COUNT];

	uiPool_t		pool;
	uiChunk_t		chunks[UI_POOL_MAX_CHUNKS];
	uiTexture_t		*textures;
	int				maxTextures, firstFreeTexture;

	GLuint			quadVao, quadVbo, quadIbo;

	int				width, height;		// the layers, the UI's size
	GLuint			depthStencil;		// shared by the layers
	uiTarget_t		layers[UI_MAX_LAYERS];
	int				numLayers;			// made
	int				layerTop;			// in use
	uiTarget_t		post[3];			// postprocess primary, secondary, tertiary
	uiTarget_t		mask;

	GLint			frameDraw, frameRead;	// the framebuffers bound when the UI began
	float			projection[16], transform[16];
	uiRect_t		scissor;
	int				stencilRef;
	qboolean		inFrame;
} ui;



/*
================
Matrices, column-major as GL's
================
*/
static void UI_Ortho( float left, float right, float bottom, float top, float zNear, float zFar, float *m ) {
	Com_Memset( m, 0, 16 * sizeof( float ) );
	m[0] = 2.0f / ( right - left );
	m[5] = 2.0f / ( top - bottom );
	m[10] = -2.0f / ( zFar - zNear );
	m[12] = -( right + left ) / ( right - left );
	m[13] = -( top + bottom ) / ( top - bottom );
	m[14] = -( zFar + zNear ) / ( zFar - zNear );
	m[15] = 1.0f;
}


static void UI_MatMul( const float *a, const float *b, float *out ) {
	int i, j;

	for ( i = 0; i < 4; i++ ) {
		for ( j = 0; j < 4; j++ ) {
			out[i * 4 + j] = a[0 * 4 + j] * b[i * 4 + 0] + a[1 * 4 + j] * b[i * 4 + 1] + a[2 * 4 + j] * b[i * 4 + 2] + a[3 * 4 + j] * b[i * 4 + 3];
		}
	}
}


/*
================
UI_CompileShader
================
*/
static GLuint UI_CompileShader( GLenum type, const char *src, const char *name ) {
	char header[256], log[2048];
	const char *srcs[2];
	GLuint shader;
	GLint ok;

	Com_sprintf( header, sizeof( header ), "#version 150\n#define MAX_NUM_STOPS %i\n#define BLUR_SIZE %i\n#define BLUR_NUM_WEIGHTS %i\n",
		OZ_UI_MAX_STOPS, UI_BLUR_SIZE, UI_BLUR_WEIGHTS );
	srcs[0] = header;
	srcs[1] = src;

	shader = qglCreateShader( type );
	qglShaderSource( shader, 2, srcs, NULL );
	qglCompileShader( shader );
	qglGetShaderiv( shader, GL_COMPILE_STATUS, &ok );
	if ( !ok ) {
		qglGetShaderInfoLog( shader, sizeof( log ), NULL, log );
		ri.Printf( PRINT_WARNING, "UI shader \"%s\" (%s) didn't compile:\n%s\n", name, type == GL_VERTEX_SHADER ? "vertex" : "fragment", log );
		qglDeleteShader( shader );
		return 0;
	}
	return shader;
}


/*
================
UI_LinkProgram
================
*/
static qboolean UI_LinkProgram( uiProgram_t p ) {
	GLuint vert, frag, prog;
	GLint ok;
	char log[2048];
	int i;

	vert = UI_CompileShader( GL_VERTEX_SHADER, uiProgramDefs[p].vert, uiProgramDefs[p].name );
	frag = UI_CompileShader( GL_FRAGMENT_SHADER, uiProgramDefs[p].frag, uiProgramDefs[p].name );
	if ( !vert || !frag ) {
		return qfalse;
	}

	prog = qglCreateProgram();
	qglAttachShader( prog, vert );
	qglAttachShader( prog, frag );
	qglBindAttribLocation( prog, 0, "inPosition" );
	qglBindAttribLocation( prog, 1, "inColor0" );
	qglBindAttribLocation( prog, 2, "inTexCoord0" );
	qglLinkProgram( prog );
	qglDetachShader( prog, vert );
	qglDetachShader( prog, frag );
	qglDeleteShader( vert );
	qglDeleteShader( frag );

	qglGetProgramiv( prog, GL_LINK_STATUS, &ok );
	if ( !ok ) {
		qglGetProgramInfoLog( prog, sizeof( log ), NULL, log );
		ri.Printf( PRINT_WARNING, "UI program \"%s\" didn't link:\n%s\n", uiProgramDefs[p].name, log );
		qglDeleteProgram( prog );
		return qfalse;
	}

	ui.programs[p] = prog;
	for ( i = 0; i < UU_COUNT; i++ ) {
		ui.uniforms[p][i] = qglGetUniformLocation( prog, uiUniformNames[i] );
	}

	// samplers and defaults
	qglUseProgram( prog );
	if ( ui.uniforms[p][UU_TEX] >= 0 ) qglUniform1i( ui.uniforms[p][UU_TEX], 0 );
	if ( ui.uniforms[p][UU_TEX_MASK] >= 0 ) qglUniform1i( ui.uniforms[p][UU_TEX_MASK], 1 );
	if ( ui.uniforms[p][UU_UV_SCALE] >= 0 ) qglUniform2f( ui.uniforms[p][UU_UV_SCALE], 1.0f, 1.0f );
	if ( ui.uniforms[p][UU_OPACITY] >= 0 ) qglUniform1f( ui.uniforms[p][UU_OPACITY], 1.0f );
	qglUseProgram( 0 );
	return qtrue;
}


/*
================
UI_MakeVao

A VAO with RmlUi's vertex layout over a vertex and an index buffer (data or NULL). It binds
GL_ARRAY_BUFFER, which the renderer's client-side arrays need at 0: VBO_UnBind has put it there
and it goes back there.
================
*/
static void UI_MakeVao( GLuint *vao, GLuint *vbo, GLuint *ibo, const void *verts, int vertBytes, const void *indexes, int indexBytes ) {
	qglGenVertexArrays( 1, vao );
	qglGenBuffers( 1, vbo );
	qglGenBuffers( 1, ibo );
	qglBindVertexArray( *vao );

	qglBindBuffer( GL_ARRAY_BUFFER, *vbo );
	qglBufferData( GL_ARRAY_BUFFER, vertBytes, verts, verts ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW );
	qglEnableVertexAttribArray( 0 );
	qglVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, sizeof( ozUiVertex_t ), (const GLvoid *)offsetof( ozUiVertex_t, pos ) );
	qglEnableVertexAttribArray( 1 );
	qglVertexAttribPointer( 1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof( ozUiVertex_t ), (const GLvoid *)offsetof( ozUiVertex_t, color ) );
	qglEnableVertexAttribArray( 2 );
	qglVertexAttribPointer( 2, 2, GL_FLOAT, GL_FALSE, sizeof( ozUiVertex_t ), (const GLvoid *)offsetof( ozUiVertex_t, st ) );

	qglBindBuffer( GL_ELEMENT_ARRAY_BUFFER, *ibo );	// the VAO's own binding
	qglBufferData( GL_ELEMENT_ARRAY_BUFFER, indexBytes, indexes, indexes ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW );

	qglBindVertexArray( 0 );
	qglBindBuffer( GL_ARRAY_BUFFER, 0 );
}


static void UI_FreeVao( GLuint *vao, GLuint *vbo, GLuint *ibo ) {
	if ( *vao ) qglDeleteVertexArrays( 1, vao );
	if ( *vbo ) qglDeleteBuffers( 1, vbo );
	if ( *ibo ) qglDeleteBuffers( 1, ibo );
	*vao = *vbo = *ibo = 0;
}


/*
================
The pool's chunks: a VAO each. Uploads go through GL_COPY_WRITE_BUFFER, which no VAO and none of
the renderer's draws use, so a compile outside the UI's frame disturbs nothing.
================
*/
static int UI_MakeChunk( int chunk, int numVerts, int numIndexes ) {
	uiChunk_t *c = &ui.chunks[chunk];

	UI_MakeVao( &c->vao, &c->vbo, &c->ibo, NULL, numVerts * sizeof( ozUiVertex_t ), NULL, numIndexes * sizeof( int ) );
	return c->vao != 0;
}


static void UI_FreeChunk( int chunk ) {
	uiChunk_t *c = &ui.chunks[chunk];

	qglBindVertexArray( 0 );
	UI_FreeVao( &c->vao, &c->vbo, &c->ibo );
}


static void UI_UploadChunk( int chunk, int firstVert, const ozUiVertex_t *verts, int numVerts, int firstIndex, const int *indexes, int numIndexes ) {
	const uiChunk_t *c = &ui.chunks[chunk];

	qglBindBuffer( GL_COPY_WRITE_BUFFER, c->vbo );
	qglBufferSubData( GL_COPY_WRITE_BUFFER, firstVert * sizeof( ozUiVertex_t ), numVerts * sizeof( ozUiVertex_t ), verts );
	qglBindBuffer( GL_COPY_WRITE_BUFFER, c->ibo );
	qglBufferSubData( GL_COPY_WRITE_BUFFER, firstIndex * sizeof( int ), numIndexes * sizeof( int ), indexes );
	qglBindBuffer( GL_COPY_WRITE_BUFFER, 0 );
}


/*
================
UI_Init

Programs, the pool and the full-screen quad, on first use.
================
*/
static qboolean UI_Init( void ) {
	static const ozUiVertex_t quad[4] = {
		{ { -1.0f, -1.0f }, { 255, 255, 255, 255 }, { 0.0f, 0.0f } },
		{ {  1.0f, -1.0f }, { 255, 255, 255, 255 }, { 1.0f, 0.0f } },
		{ {  1.0f,  1.0f }, { 255, 255, 255, 255 }, { 1.0f, 1.0f } },
		{ { -1.0f,  1.0f }, { 255, 255, 255, 255 }, { 0.0f, 1.0f } }
	};
	static const int quadIndexes[6] = { 0, 1, 2, 0, 2, 3 };
	int i;

	if ( ui.initialized ) {
		return qtrue;
	}
	if ( ui.failed ) {
		return qfalse;
	}
	ui.failed = qtrue;	// until it all works

	for ( i = 0; i < UP_COUNT; i++ ) {
		if ( !UI_LinkProgram( i ) ) {
			return qfalse;
		}
	}
	ui.program = -1;

#ifdef USE_VBO
	VBO_UnBind();
#endif
	UI_MakeVao( &ui.quadVao, &ui.quadVbo, &ui.quadIbo, quad, sizeof( quad ), quadIndexes, sizeof( quadIndexes ) );

	ui.pool.makeChunk = UI_MakeChunk;
	ui.pool.freeChunk = UI_FreeChunk;
	ui.pool.upload = UI_UploadChunk;

	ui.failed = qfalse;
	ui.initialized = qtrue;
	ri.Printf( PRINT_ALL, "UI renderer: GLSL 1.50, %i programs\n", UP_COUNT );
	return qtrue;
}


static void UI_UseProgram( uiProgram_t p ) {
	if ( ui.program != p ) {
		qglUseProgram( ui.programs[p] );
		ui.program = p;
	}
}


static GLint UI_Uniform( uiUniform_t u ) {
	return ui.program >= 0 ? ui.uniforms[ui.program][u] : -1;
}


static void UI_SubmitTransform( float x, float y ) {
	if ( ui.transformDirty[ui.program] ) {
		qglUniformMatrix4fv( UI_Uniform( UU_TRANSFORM ), 1, GL_FALSE, ui.transform );
		ui.transformDirty[ui.program] = qfalse;
	}
	qglUniform2f( UI_Uniform( UU_TRANSLATE ), x, y );
}


/*
================
Resources
================
*/
static uiTexture_t *UI_Texture( int handle ) {
	if ( handle <= 0 || handle > ui.maxTextures || !ui.textures[handle - 1].used ) {
		return NULL;
	}
	return &ui.textures[handle - 1];
}


static int UI_CompileGeometry( const ozUiVertex_t *verts, int numVerts, const int *indexes, int numIndexes ) {
	if ( !tr.registered || !UI_Init() ) {
		return 0;
	}
	return UIPool_Compile( &ui.pool, verts, numVerts, indexes, numIndexes );
}


static void UI_ReleaseGeometry( int handle ) {
	UIPool_Release( &ui.pool, handle );
}


// a texture handle; the table grows, and freed handles are reused first
static int UI_AddTexture( GLuint texnum, qboolean owned, qboolean straight ) {
	uiTexture_t *t;
	int h;

	if ( !ui.firstFreeTexture ) {
		const int newMax = ui.maxTextures ? ui.maxTextures * 2 : 256;
		uiTexture_t *grown = ri.Malloc( newMax * sizeof( uiTexture_t ) );
		Com_Memset( grown, 0, newMax * sizeof( uiTexture_t ) );
		if ( ui.textures ) {
			Com_Memcpy( grown, ui.textures, ui.maxTextures * sizeof( uiTexture_t ) );
			ri.Free( ui.textures );
		}
		for ( h = ui.maxTextures + 1; h <= newMax; h++ ) {
			grown[h - 1].nextFree = h < newMax ? h + 1 : 0;
		}
		ui.firstFreeTexture = ui.maxTextures + 1;
		ui.textures = grown;
		ui.maxTextures = newMax;
	}
	h = ui.firstFreeTexture;
	t = &ui.textures[h - 1];
	ui.firstFreeTexture = t->nextFree;

	t->texnum = texnum;
	t->used = qtrue;
	t->owned = owned;
	t->straight = straight;
	t->nextFree = 0;
	return h;
}


static void UI_BindTexture( GLuint tex ) {
	GL_BindTexture( 0, tex );
}


// the renderer's binding cache must not keep a deleted name: GL binds 0 in its place
static void UI_DeleteTexture( GLuint tex ) {
	int i;

	qglDeleteTextures( 1, &tex );
	for ( i = 0; i < MAX_TEXTURE_UNITS; i++ ) {
		if ( glState.currenttextures[i] == tex ) {
			glState.currenttextures[i] = 0;
		}
	}
}


// an empty or filled RGBA8 texture of ours
static GLuint UI_MakeTexture( const byte *rgba, int width, int height, GLint wrap ) {
	static const float transparent[4] = { 0, 0, 0, 0 };
	GLuint tex;

	qglGenTextures( 1, &tex );
	UI_BindTexture( tex );
	qglTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap );
	if ( wrap == GL_CLAMP_TO_BORDER ) {
		qglTexParameterfv( GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, transparent );
	}
	return tex;
}


/*
================
UI_LoadTexture

Through the engine's image loader (tga, png, jpg, dds, ... from the pk3s), kept in the renderer's
image list, so a page and the game share one copy. No mipmaps, picmip or overbright scaling, and
no rounding to a power of two (r_roundImagesDown would halve a 1920 by 1080 picture).
================
*/
static int UI_LoadTexture( const char *name, int *width, int *height ) {
	image_t *image;

	if ( !tr.registered || !UI_Init() ) {
		return 0;
	}
	image = R_FindImageFile( name, IMGFLAG_NO_COMPRESSION | IMGFLAG_NOLIGHTSCALE | IMGFLAG_NOSCALE );
	if ( !image ) {
		return 0;
	}
	*width = image->width;
	*height = image->height;
	return UI_AddTexture( image->texnum, qfalse, qtrue );
}


static int UI_CreateTexture( const byte *rgba, int width, int height ) {
	if ( !tr.registered || !UI_Init() || width <= 0 || height <= 0 ) {
		return 0;
	}
	return UI_AddTexture( UI_MakeTexture( rgba, width, height, GL_REPEAT ), qtrue, qfalse );
}


static void UI_ReleaseTexture( int handle ) {
	uiTexture_t *t = UI_Texture( handle );

	if ( !t ) {
		return;
	}
	if ( t->owned ) {
		UI_DeleteTexture( t->texnum );
	}
	t->texnum = 0;
	t->used = qfalse;
	t->owned = qfalse;
	t->nextFree = ui.firstFreeTexture;
	ui.firstFreeTexture = handle;
}


/*
================
Targets: the layers, the postprocess buffers and the blend mask
================
*/
static void UI_MakeTarget( uiTarget_t *t, qboolean stencil ) {
	GLenum status;

	t->width = ui.width;
	t->height = ui.height;
	t->color = UI_MakeTexture( NULL, t->width, t->height, GL_CLAMP_TO_BORDER );	// border: transparent black
	qglGenFramebuffers( 1, &t->fbo );
	qglBindFramebuffer( GL_FRAMEBUFFER, t->fbo );
	qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->color, 0 );
	if ( stencil ) {
		if ( !ui.depthStencil ) {
			qglGenRenderbuffers( 1, &ui.depthStencil );
			qglBindRenderbuffer( GL_RENDERBUFFER, ui.depthStencil );
			qglRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, ui.width, ui.height );
			qglBindRenderbuffer( GL_RENDERBUFFER, 0 );
		}
		qglFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, ui.depthStencil );
	}
	status = qglCheckFramebufferStatus( GL_FRAMEBUFFER );
	if ( status != GL_FRAMEBUFFER_COMPLETE ) {
		ri.Printf( PRINT_WARNING, "UI framebuffer %ix%i incomplete: 0x%x\n", t->width, t->height, status );
	}
}


static void UI_FreeTarget( uiTarget_t *t ) {
	if ( t->fbo ) qglDeleteFramebuffers( 1, &t->fbo );
	if ( t->color ) UI_DeleteTexture( t->color );
	Com_Memset( t, 0, sizeof( *t ) );
}


// outside a frame: the framebuffer binding is the renderer's, put back after
static void UI_FreeTargets( void ) {
	int i;

	for ( i = 0; i < ui.numLayers; i++ ) {
		UI_FreeTarget( &ui.layers[i] );
	}
	ui.numLayers = 0;
	for ( i = 0; i < 3; i++ ) {
		UI_FreeTarget( &ui.post[i] );
	}
	UI_FreeTarget( &ui.mask );
	if ( ui.depthStencil ) {
		qglDeleteRenderbuffers( 1, &ui.depthStencil );
		ui.depthStencil = 0;
	}
}


static const uiTarget_t *UI_Post( int i ) {
	if ( !ui.post[i].fbo ) {
		UI_MakeTarget( &ui.post[i], qfalse );
	}
	return &ui.post[i];
}


static void UI_SwapPost( void ) {
	const uiTarget_t t = ui.post[0];

	ui.post[0] = ui.post[1];
	ui.post[1] = t;
}


static void UI_BindTarget( const uiTarget_t *t ) {
	qglBindFramebuffer( GL_FRAMEBUFFER, t->fbo );
}


/*
================
Scissor

ui.scissor is in RmlUi's coordinates (from the top left); GL's come from the bottom left.
The renderer keeps the scissor test on, so "no scissor" is the whole target.
================
*/
static void UI_SetScissorRect( uiRect_t r ) {
	if ( r.valid ) {
		const int x = Com_Clamp( 0, ui.width, r.x0 );
		const int y = Com_Clamp( 0, ui.height, ui.height - r.y1 );
		qglScissor( x, y, r.x1 - r.x0, r.y1 - r.y0 );
	} else {
		qglScissor( 0, 0, ui.width, ui.height );
	}
	ui.scissor = r;
}


static uiRect_t UI_Flipped( uiRect_t r ) {
	uiRect_t f = r;

	f.y0 = ui.height - r.y1;
	f.y1 = ui.height - r.y0;
	return f;
}


// the current scissor in GL's coordinates, the whole layer when there is none
static uiRect_t UI_ScissorGL( void ) {
	uiRect_t r = { 0, 0, 0, 0, qtrue };

	if ( !ui.scissor.valid ) {
		r.x1 = ui.width;
		r.y1 = ui.height;
		return r;
	}
	return UI_Flipped( ui.scissor );
}


static void UI_SetScissor( int enable, int x, int y, int width, int height ) {
	uiRect_t r;

	r.x0 = x;
	r.y0 = y;
	r.x1 = x + width;
	r.y1 = y + height;
	r.valid = enable && width >= 0 && height >= 0;
	UI_SetScissorRect( r );
}


/*
================
UI_DrawQuad

The full-screen quad with the program in use.
================
*/
static void UI_DrawQuad( float uvOffsetX, float uvOffsetY, float uvScaleX, float uvScaleY ) {
	qglUniform2f( UI_Uniform( UU_UV_OFFSET ), uvOffsetX, uvOffsetY );
	qglUniform2f( UI_Uniform( UU_UV_SCALE ), uvScaleX, uvScaleY );
	qglBindVertexArray( ui.quadVao );
	qglDrawElements( GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL );
}


#define UI_BLEND_PREMULTIPLIED	( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA )
#define UI_BLEND_NONE			( GLS_DEPTHTEST_DISABLE )


/*
================
Frames
================
*/
static void UI_BeginFrame( int width, int height ) {
	if ( !tr.registered || !UI_Init() || width <= 0 || height <= 0 ) {
		return;
	}

	// what was queued so far lands below the UI, its last 2D batch too (RC_END_OF_LIST doesn't draw it)
	R_IssuePendingRenderCommands();
	RB_EndSurface();
#ifdef USE_VBO
	VBO_UnBind();
#endif
	RB_SetGL2D();		// the 2D view, as a stretch pic's: a portal's clip plane off among the rest
#ifdef USE_FBO
	R_BloomScreen();	// the UI is 2D: the world blooms first, as at the first RB_StretchPic
#endif
	GL_ProgramDisable();

	qglGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &ui.frameDraw );
	qglGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &ui.frameRead );

	if ( width != ui.width || height != ui.height ) {
		UI_FreeTargets();
		ui.width = width;
		ui.height = height;
	}
	UIPool_Frame( &ui.pool );

	ui.inFrame = qtrue;
	ui.program = -1;
	ui.layerTop = 0;

	qglViewport( 0, 0, ui.width, ui.height );
	ui.scissor.valid = qfalse;
	UI_SetScissorRect( ui.scissor );

	GL_State( UI_BLEND_PREMULTIPLIED );
	GL_Cull( CT_TWO_SIDED );
	qglColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	qglEnable( GL_STENCIL_TEST );
	qglStencilFunc( GL_ALWAYS, 1, ~0U );
	qglStencilMask( ~0U );
	qglStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	ui.stencilRef = 1;

	UI_Ortho( 0, ui.width, ui.height, 0, -10000, 10000, ui.projection );
	Com_Memcpy( ui.transform, ui.projection, sizeof( ui.transform ) );
	Com_Memset( ui.transformDirty, 1, sizeof( ui.transformDirty ) );

	// the base layer
	if ( !ui.numLayers ) {
		UI_MakeTarget( &ui.layers[0], qtrue );
		ui.numLayers = 1;
	}
	UI_BindTarget( &ui.layers[0] );
	ui.layerTop = 1;
	qglClearColor( 0, 0, 0, 0 );
	qglClearStencil( 0 );
	qglClear( GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
}


static void UI_EndFrame( void ) {
	if ( !ui.inFrame ) {
		return;
	}
	ui.inFrame = qfalse;

	// composite the base layer onto the framebuffer 2D draws to, in the 2D view (RB_SetGL2D)
	qglBindFramebuffer( GL_DRAW_FRAMEBUFFER, ui.frameDraw );
	qglBindFramebuffer( GL_READ_FRAMEBUFFER, ui.frameRead );

	qglDisable( GL_STENCIL_TEST );
	qglViewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
	qglScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
	GL_State( UI_BLEND_PREMULTIPLIED );
	UI_UseProgram( UP_PASSTHROUGH );
	UI_BindTexture( ui.layers[0].color );
	UI_DrawQuad( 0, 0, 1, 1 );

	// leave the state the renderer expects: no program, VAO or buffer of ours, unit 0 selected
	qglBindVertexArray( 0 );
	qglUseProgram( 0 );
	GL_SelectTexture( 0 );
	qglClearStencil( 0 );
	backEnd.projection2D = qfalse;	// the next 2D draw sets up its view again
	ui.program = -1;
	ui.layerTop = 0;
}


/*
================
Geometry
================
*/
static void UI_DrawGeometry( const uiPoolGeometry_t *g ) {
	qglBindVertexArray( ui.chunks[g->chunk].vao );
	qglDrawElementsBaseVertex( GL_TRIANGLES, g->numIndexes, GL_UNSIGNED_INT, (const GLvoid *)( (size_t)g->firstIndex * sizeof( int ) ), g->firstVert );
}


static void UI_RenderGeometry( int geometry, float x, float y, int texture ) {
	const uiPoolGeometry_t *g = UIPool_Get( &ui.pool, geometry );
	const uiTexture_t *t = UI_Texture( texture );

	if ( !ui.inFrame || !g ) {
		return;
	}
	if ( t ) {
		UI_UseProgram( UP_TEXTURE );
		qglUniform1f( UI_Uniform( UU_STRAIGHT ), t->straight ? 1.0f : 0.0f );
		UI_BindTexture( t->texnum );
	} else {
		UI_UseProgram( UP_COLOR );
	}
	UI_SubmitTransform( x, y );
	UI_DrawGeometry( g );
}


static void UI_RenderGradient( const ozUiGradient_t *gradient, int geometry, float x, float y ) {
	const uiPoolGeometry_t *g = UIPool_Get( &ui.pool, geometry );
	const int numStops = Com_Clamp( 1, OZ_UI_MAX_STOPS, gradient->numStops );

	if ( !ui.inFrame || !g ) {
		return;
	}
	UI_UseProgram( UP_GRADIENT );
	qglUniform1i( UI_Uniform( UU_FUNC ), gradient->func );
	qglUniform2f( UI_Uniform( UU_P ), gradient->p[0], gradient->p[1] );
	qglUniform2f( UI_Uniform( UU_V ), gradient->v[0], gradient->v[1] );
	qglUniform1i( UI_Uniform( UU_NUM_STOPS ), numStops );
	qglUniform1fv( UI_Uniform( UU_STOP_POSITIONS ), numStops, gradient->stopPositions );
	qglUniform4fv( UI_Uniform( UU_STOP_COLORS ), numStops, gradient->stopColors[0] );
	UI_SubmitTransform( x, y );
	UI_DrawGeometry( g );
}


static void UI_SetTransform( const float *matrix ) {
	if ( matrix ) {
		UI_MatMul( ui.projection, matrix, ui.transform );
	} else {
		Com_Memcpy( ui.transform, ui.projection, sizeof( ui.transform ) );
	}
	Com_Memset( ui.transformDirty, 1, sizeof( ui.transformDirty ) );
}


/*
================
Clip masks, in the layers' shared stencil buffer
================
*/
static void UI_EnableClipMask( int enable ) {
	if ( !ui.inFrame ) {
		return;
	}
	if ( enable ) {
		qglEnable( GL_STENCIL_TEST );
	} else {
		qglDisable( GL_STENCIL_TEST );
	}
}


static void UI_RenderToClipMask( int op, int geometry, float x, float y ) {
	GLint writeValue = 1, testValue = 1;

	if ( !ui.inFrame ) {
		return;
	}
	switch ( op ) {
	case OZ_UI_CLIP_SET:
		qglClear( GL_STENCIL_BUFFER_BIT );
		qglStencilOp( GL_KEEP, GL_KEEP, GL_REPLACE );
		break;
	case OZ_UI_CLIP_SET_INVERSE:
		qglClearStencil( 1 );
		qglClear( GL_STENCIL_BUFFER_BIT );
		qglClearStencil( 0 );
		qglStencilOp( GL_KEEP, GL_KEEP, GL_REPLACE );
		writeValue = 0;
		break;
	case OZ_UI_CLIP_INTERSECT:
		qglStencilOp( GL_KEEP, GL_KEEP, GL_INCR );
		testValue = ui.stencilRef + 1;
		break;
	}

	qglColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
	qglStencilFunc( GL_ALWAYS, writeValue, ~0U );
	UI_RenderGeometry( geometry, x, y, 0 );

	qglColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	qglStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	qglStencilFunc( GL_EQUAL, testValue, ~0U );
	ui.stencilRef = testValue;
}


/*
================
Blur

RmlUi's: downscale by halves for a large sigma, a separable 7-tap Gaussian, then upscale back
into the region. sourceDest and temp are full-size postprocess buffers, region is in GL's
coordinates.
================
*/
static void UI_SetTexCoordLimits( uiRect_t regionGL, int width, int height ) {
	qglUniform2f( UI_Uniform( UU_TEX_COORD_MIN ), ( regionGL.x0 + 0.5f ) / width, ( regionGL.y0 + 0.5f ) / height );
	qglUniform2f( UI_Uniform( UU_TEX_COORD_MAX ), ( regionGL.x1 - 0.5f ) / width, ( regionGL.y1 - 0.5f ) / height );
}


// the scissor from GL's coordinates
static void UI_SetScissorGL( uiRect_t r ) {
	UI_SetScissorRect( UI_Flipped( r ) );
}


static void UI_Blur( float sigma, const uiTarget_t *sourceDest, const uiTarget_t *temp, uiRect_t windowGL ) {
	const uiRect_t originalScissor = ui.scissor;
	uiRect_t scissor = windowGL;
	float weights[UI_BLUR_WEIGHTS];
	int passLevel, i, srcMinX, srcMinY, srcMaxX, srcMaxY, tgtMinX, tgtMinY, tgtMaxX, tgtMaxY;
	float uvScaleX, uvScaleY;

	UI_SigmaToParameters( sigma, &passLevel, &sigma );

	UI_UseProgram( UP_PASSTHROUGH );
	UI_SetScissorGL( scissor );

	// downscale by halves with bilinear filtering
	qglViewport( 0, 0, sourceDest->width / 2, sourceDest->height / 2 );
	uvScaleX = ( sourceDest->width % 2 == 1 ) ? ( 1.0f - 1.0f / sourceDest->width ) : 1.0f;
	uvScaleY = ( sourceDest->height % 2 == 1 ) ? ( 1.0f - 1.0f / sourceDest->height ) : 1.0f;
	for ( i = 0; i < passLevel; i++ ) {
		const qboolean fromSource = ( i % 2 == 0 );
		scissor.x0 = ( scissor.x0 + 1 ) / 2;
		scissor.y0 = ( scissor.y0 + 1 ) / 2;
		scissor.x1 = MAX( scissor.x1 / 2, scissor.x0 );
		scissor.y1 = MAX( scissor.y1 / 2, scissor.y0 );
		UI_BindTexture( fromSource ? sourceDest->color : temp->color );
		UI_BindTarget( fromSource ? temp : sourceDest );
		UI_SetScissorGL( scissor );
		UI_DrawQuad( 0, 0, uvScaleX, uvScaleY );
	}
	qglViewport( 0, 0, sourceDest->width, sourceDest->height );

	// the image must end up in temp
	if ( passLevel % 2 == 0 ) {
		UI_BindTexture( sourceDest->color );
		UI_BindTarget( temp );
		UI_DrawQuad( 0, 0, 1, 1 );
	}

	UI_UseProgram( UP_BLUR );
	UI_BlurWeights( sigma, weights );
	qglUniform1fv( UI_Uniform( UU_WEIGHTS ), UI_BLUR_WEIGHTS, weights );
	UI_SetTexCoordLimits( scissor, sourceDest->width, sourceDest->height );

	// vertical
	UI_BindTexture( temp->color );
	UI_BindTarget( sourceDest );
	qglUniform2f( UI_Uniform( UU_TEXEL_OFFSET ), 0.0f, 1.0f / temp->height );
	UI_DrawQuad( 0, 0, 1, 1 );

	// horizontal, with a 1-pixel transparent border around the region against bleeding when upscaling
	UI_BindTexture( sourceDest->color );
	UI_BindTarget( temp );
	{
		uiRect_t padded = scissor;
		padded.x0--; padded.y0--; padded.x1++; padded.y1++;
		UI_SetScissorGL( padded );
		qglClear( GL_COLOR_BUFFER_BIT );
	}
	UI_SetScissorGL( scissor );
	qglUniform2f( UI_Uniform( UU_TEXEL_OFFSET ), 1.0f / sourceDest->width, 0.0f );
	UI_DrawQuad( 0, 0, 1, 1 );

	// upscale into the region, then again by an exact power of two for stable placement
	UI_SetScissorGL( windowGL );
	qglBindFramebuffer( GL_READ_FRAMEBUFFER, temp->fbo );
	qglBindFramebuffer( GL_DRAW_FRAMEBUFFER, sourceDest->fbo );
	srcMinX = scissor.x0; srcMinY = scissor.y0; srcMaxX = scissor.x1; srcMaxY = scissor.y1;
	qglBlitFramebuffer( srcMinX, srcMinY, srcMaxX, srcMaxY, windowGL.x0, windowGL.y0, windowGL.x1, windowGL.y1, GL_COLOR_BUFFER_BIT, GL_LINEAR );
	tgtMinX = srcMinX << passLevel; tgtMinY = srcMinY << passLevel;
	tgtMaxX = srcMaxX << passLevel; tgtMaxY = srcMaxY << passLevel;
	if ( tgtMinX != windowGL.x0 || tgtMinY != windowGL.y0 || tgtMaxX != windowGL.x1 || tgtMaxY != windowGL.y1 ) {
		qglBlitFramebuffer( srcMinX, srcMinY, srcMaxX, srcMaxY, tgtMinX, tgtMinY, tgtMaxX, tgtMaxY, GL_COLOR_BUFFER_BIT, GL_LINEAR );
	}

	UI_SetScissorRect( originalScissor );
}


/*
================
Layers and filters
================
*/
static void UI_BlitLayerToPost( int layer ) {
	const uiTarget_t *source = &ui.layers[layer];
	const uiTarget_t *dest = UI_Post( 0 );

	qglBindFramebuffer( GL_READ_FRAMEBUFFER, source->fbo );
	qglBindFramebuffer( GL_DRAW_FRAMEBUFFER, dest->fbo );
	qglBlitFramebuffer( 0, 0, source->width, source->height, 0, 0, dest->width, dest->height, GL_COLOR_BUFFER_BIT, GL_NEAREST );
}


static void UI_RenderFilters( const ozUiFilter_t *filters, int numFilters ) {
	int i;

	for ( i = 0; i < numFilters; i++ ) {
		const ozUiFilter_t *f = &filters[i];

		switch ( f->type ) {
		case OZ_UI_FILTER_OPACITY:
			GL_State( UI_BLEND_NONE );
			UI_UseProgram( UP_PASSTHROUGH );
			qglUniform1f( UI_Uniform( UU_OPACITY ), f->value );
			UI_BindTexture( UI_Post( 0 )->color );
			UI_BindTarget( UI_Post( 1 ) );
			UI_DrawQuad( 0, 0, 1, 1 );
			qglUniform1f( UI_Uniform( UU_OPACITY ), 1.0f );
			UI_SwapPost();
			GL_State( UI_BLEND_PREMULTIPLIED );
			break;

		case OZ_UI_FILTER_BLUR:
			GL_State( UI_BLEND_NONE );
			UI_Blur( f->value, UI_Post( 0 ), UI_Post( 1 ), UI_ScissorGL() );
			GL_State( UI_BLEND_PREMULTIPLIED );
			break;

		case OZ_UI_FILTER_DROP_SHADOW: {
			const uiRect_t windowGL = UI_ScissorGL();
			const uiTarget_t *primary = UI_Post( 0 ), *secondary = UI_Post( 1 );

			UI_UseProgram( UP_DROP_SHADOW );
			GL_State( UI_BLEND_NONE );
			qglUniform4fv( UI_Uniform( UU_COLOR ), 1, f->color );
			UI_BindTexture( primary->color );
			UI_BindTarget( secondary );
			UI_SetTexCoordLimits( windowGL, primary->width, primary->height );
			UI_DrawQuad( -f->offset[0] / ui.width, f->offset[1] / ui.height, 1, 1 );
			if ( f->value >= 0.5f ) {
				UI_Blur( f->value, secondary, UI_Post( 2 ), windowGL );
			}
			UI_UseProgram( UP_PASSTHROUGH );
			UI_BindTexture( primary->color );
			GL_State( UI_BLEND_PREMULTIPLIED );
			UI_DrawQuad( 0, 0, 1, 1 );
			UI_SwapPost();
			break;
		}

		case OZ_UI_FILTER_COLOR_MATRIX:
			UI_UseProgram( UP_COLOR_MATRIX );
			GL_State( UI_BLEND_NONE );
			qglUniformMatrix4fv( UI_Uniform( UU_COLOR_MATRIX ), 1, GL_FALSE, f->matrix );
			UI_BindTexture( UI_Post( 0 )->color );
			UI_BindTarget( UI_Post( 1 ) );
			UI_DrawQuad( 0, 0, 1, 1 );
			UI_SwapPost();
			GL_State( UI_BLEND_PREMULTIPLIED );
			break;

		case OZ_UI_FILTER_MASK_IMAGE:
			if ( !ui.mask.fbo ) {
				break;
			}
			UI_UseProgram( UP_BLEND_MASK );
			GL_State( UI_BLEND_NONE );
			UI_BindTexture( UI_Post( 0 )->color );
			GL_BindTexture( 1, ui.mask.color );
			UI_BindTarget( UI_Post( 1 ) );
			UI_DrawQuad( 0, 0, 1, 1 );
			UI_SwapPost();
			GL_State( UI_BLEND_PREMULTIPLIED );
			break;
		}
	}
}


static int UI_PushLayer( void ) {
	uiTarget_t *t;

	if ( !ui.inFrame ) {
		return 0;
	}
	if ( ui.layerTop == UI_MAX_LAYERS ) {
		ri.Printf( PRINT_WARNING, "UI: more than %i layers\n", UI_MAX_LAYERS );
		return ui.layerTop - 1;
	}
	if ( ui.layerTop == ui.numLayers ) {
		UI_MakeTarget( &ui.layers[ui.numLayers++], qtrue );
	}
	t = &ui.layers[ui.layerTop];
	UI_BindTarget( t );
	qglClear( GL_COLOR_BUFFER_BIT );
	return ui.layerTop++;
}


static void UI_CompositeLayers( int source, int destination, int replace, const ozUiFilter_t *filters, int numFilters ) {
	if ( !ui.inFrame || source < 0 || source >= ui.layerTop || destination < 0 || destination >= ui.layerTop ) {
		return;
	}

	UI_BlitLayerToPost( source );
	UI_RenderFilters( filters, numFilters );

	UI_BindTarget( &ui.layers[destination] );
	UI_BindTexture( UI_Post( 0 )->color );
	UI_UseProgram( UP_PASSTHROUGH );
	GL_State( replace ? UI_BLEND_NONE : UI_BLEND_PREMULTIPLIED );
	UI_DrawQuad( 0, 0, 1, 1 );
	GL_State( UI_BLEND_PREMULTIPLIED );

	if ( destination != ui.layerTop - 1 ) {
		UI_BindTarget( &ui.layers[ui.layerTop - 1] );
	}
}


static void UI_PopLayer( void ) {
	if ( !ui.inFrame || ui.layerTop <= 1 ) {
		return;
	}
	ui.layerTop--;
	UI_BindTarget( &ui.layers[ui.layerTop - 1] );
}


static int UI_SaveLayerAsTexture( void ) {
	const uiRect_t bounds = ui.scissor;
	const uiRect_t none = { 0, 0, 0, 0, qfalse };
	const uiTarget_t *source, *dest;
	int w, h;
	GLuint tex;

	if ( !ui.inFrame || !bounds.valid || bounds.x1 <= bounds.x0 || bounds.y1 <= bounds.y0 ) {
		return 0;
	}
	w = bounds.x1 - bounds.x0;
	h = bounds.y1 - bounds.y0;
	tex = UI_MakeTexture( NULL, w, h, GL_REPEAT );

	UI_BlitLayerToPost( ui.layerTop - 1 );
	UI_SetScissorRect( none );

	// flipped vertically, as textures are, and moved to the origin
	source = UI_Post( 0 );
	dest = UI_Post( 1 );
	qglBindFramebuffer( GL_READ_FRAMEBUFFER, source->fbo );
	qglBindFramebuffer( GL_DRAW_FRAMEBUFFER, dest->fbo );
	qglBlitFramebuffer( bounds.x0, source->height - bounds.y1, bounds.x1, source->height - bounds.y0,
		0, h, w, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST );

	qglBindFramebuffer( GL_READ_FRAMEBUFFER, dest->fbo );
	UI_BindTexture( tex );
	qglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h );

	UI_SetScissorRect( bounds );
	UI_BindTarget( &ui.layers[ui.layerTop - 1] );
	return UI_AddTexture( tex, qtrue, qfalse );
}


static void UI_SaveLayerAsMask( void ) {
	if ( !ui.inFrame ) {
		return;
	}
	UI_BlitLayerToPost( ui.layerTop - 1 );
	if ( !ui.mask.fbo ) {
		UI_MakeTarget( &ui.mask, qfalse );
	}
	UI_BindTarget( &ui.mask );
	UI_BindTexture( UI_Post( 0 )->color );
	UI_UseProgram( UP_PASSTHROUGH );
	GL_State( UI_BLEND_NONE );
	UI_DrawQuad( 0, 0, 1, 1 );
	GL_State( UI_BLEND_PREMULTIPLIED );
	UI_BindTarget( &ui.layers[ui.layerTop - 1] );
}


static const ozUiRender_t uiRender = {
	OZ_UI_RENDER_VERSION,
	UI_CompileGeometry,
	UI_ReleaseGeometry,
	UI_LoadTexture,
	UI_CreateTexture,
	UI_ReleaseTexture,
	UI_BeginFrame,
	UI_EndFrame,
	UI_RenderGeometry,
	UI_RenderGradient,
	UI_SetScissor,
	UI_SetTransform,
	UI_EnableClipMask,
	UI_RenderToClipMask,
	UI_PushLayer,
	UI_CompositeLayers,
	UI_PopLayer,
	UI_SaveLayerAsTexture,
	UI_SaveLayerAsMask
};


/*
================
R_UI_GetRender

refexport_t.GetUIRender. The UI needs GL 3.2's programs, vertex arrays and framebuffers
(R_InitExtensions loads them as QGL_OZ_UI_PROCS and QGL_FBO_PROCS).
================
*/
const ozUiRender_t *R_UI_GetRender( void ) {
	if ( !tr.registered || !qglGenFramebuffers || !qglDrawElementsBaseVertex || gl_version < 32 || ui.failed ) {
		return NULL;
	}
	return &uiRender;
}


/*
================
R_UI_Shutdown

From RE_Shutdown, while the context still exists: every GL object made here goes.
The client releases its handles before this; any left are dropped with the rest.
================
*/
void R_UI_Shutdown( void ) {
	int i;

	if ( ui.initialized ) {
		qglBindVertexArray( 0 );
		UIPool_Shutdown( &ui.pool );
		for ( i = 0; i < ui.maxTextures; i++ ) {
			if ( ui.textures[i].used && ui.textures[i].owned ) {
				UI_DeleteTexture( ui.textures[i].texnum );
			}
		}
		UI_FreeTargets();
		qglUseProgram( 0 );
		for ( i = 0; i < UP_COUNT; i++ ) {
			if ( ui.programs[i] ) {
				qglDeleteProgram( ui.programs[i] );
			}
		}
		UI_FreeVao( &ui.quadVao, &ui.quadVbo, &ui.quadIbo );
	}
	if ( ui.textures ) {
		ri.Free( ui.textures );
	}
	Com_Memset( &ui, 0, sizeof( ui ) );
}
