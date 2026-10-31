/*
===========================================================================
Oz: the native UI's render API (ENGINE_PLAN.md P4)

The client's RmlUi render interface (engine/src/oz/ui) draws through this table, which a renderer
hands out from refexport_t.GetUIRender. The oz renderer (rend2, renderer2/tr_oz_ui.c) has it; a
renderer without it leaves GetUIRender NULL.

- Geometry and textures are made at once and live until released or until the renderer shuts down
  (the client releases them first; RmlUi rebuilds them lazily on the next frame).
- Drawing happens between BeginFrame and EndFrame, which the client calls inside the renderer's own
  frame. BeginFrame flushes the 2D queued so far, so the UI lands above it and below what comes after
  (the console). The UI draws into its own layer, composited onto the frame at EndFrame.
- Colors are premultiplied, as RmlUi 6 makes them. Coordinates are pixels from the top left.

Plain C with no engine types, so the client's C++ can include it.
===========================================================================
*/
#ifndef TR_OZ_UI_H
#define TR_OZ_UI_H

#define OZ_UI_RENDER_VERSION	2
#define OZ_UI_MAX_STOPS			16

typedef struct {
	float			pos[2];
	unsigned char	color[4];	// premultiplied
	float			st[2];
} ozUiVertex_t;					// RmlUi's Vertex, byte for byte

typedef enum {
	OZ_UI_CLIP_SET,
	OZ_UI_CLIP_SET_INVERSE,
	OZ_UI_CLIP_INTERSECT
} ozUiClipOp_t;

typedef enum {
	OZ_UI_FILTER_OPACITY,		// value: the factor
	OZ_UI_FILTER_BLUR,			// value: sigma in pixels
	OZ_UI_FILTER_DROP_SHADOW,	// value: sigma; color, offset
	OZ_UI_FILTER_COLOR_MATRIX,	// matrix (brightness, contrast, grayscale, ...)
	OZ_UI_FILTER_MASK_IMAGE		// the layer last saved by SaveLayerAsMask
} ozUiFilterType_t;

typedef struct {
	int		type;				// ozUiFilterType_t
	float	value;
	float	color[4];			// premultiplied
	float	offset[2];
	float	matrix[16];			// column-major
} ozUiFilter_t;

typedef enum {
	OZ_UI_GRADIENT_LINEAR,
	OZ_UI_GRADIENT_RADIAL,
	OZ_UI_GRADIENT_CONIC,
	OZ_UI_GRADIENT_REPEATING_LINEAR,
	OZ_UI_GRADIENT_REPEATING_RADIAL,
	OZ_UI_GRADIENT_REPEATING_CONIC
} ozUiGradientFunc_t;

typedef struct {
	int		func;				// ozUiGradientFunc_t
	float	p[2];				// linear: start; radial and conic: center
	float	v[2];				// linear: start to end; radial: 1 / radius; conic: unit vector of the angle
	int		numStops;
	float	stopPositions[OZ_UI_MAX_STOPS];
	float	stopColors[OZ_UI_MAX_STOPS][4];	// premultiplied
} ozUiGradient_t;

typedef struct {
	int		version;			// OZ_UI_RENDER_VERSION

	// resources, any time while the renderer runs; handles are > 0, 0 is none
	int		(*CompileGeometry)( const ozUiVertex_t *verts, int numVerts, const int *indexes, int numIndexes );
	void	(*ReleaseGeometry)( int geometry );
	int		(*LoadTexture)( const char *name, int *width, int *height );	// through the engine's image loader
	int		(*CreateTexture)( const unsigned char *rgba, int width, int height );	// premultiplied RGBA8
	void	(*ReleaseTexture)( int texture );

	// drawing, between BeginFrame and EndFrame
	void	(*BeginFrame)( int width, int height );
	void	(*EndFrame)( void );
	void	(*RenderGeometry)( int geometry, float x, float y, int texture );
	void	(*RenderGradient)( const ozUiGradient_t *gradient, int geometry, float x, float y );
	void	(*SetScissor)( int enable, int x, int y, int width, int height );
	void	(*SetTransform)( const float *matrix );		// column-major 4x4, NULL for none
	void	(*EnableClipMask)( int enable );
	void	(*RenderToClipMask)( int op, int geometry, float x, float y );	// ozUiClipOp_t

	// layers, for RmlUi's filters, backdrop filters, box shadows and masks
	int		(*PushLayer)( void );
	void	(*CompositeLayers)( int source, int destination, int replace, const ozUiFilter_t *filters, int numFilters );
	void	(*PopLayer)( void );
	int		(*SaveLayerAsTexture)( void );		// the scissor region of the top layer, as a new texture
	void	(*SaveLayerAsMask)( void );			// for the next OZ_UI_FILTER_MASK_IMAGE

	// a small copy of an image, at most size texels across (the map editor's texture browser); NULL
	// where the renderer has none (the page then loads the image whole)
	int		(*LoadThumbnail)( const char *name, int size, int *width, int *height );
} ozUiRender_t;

#endif
