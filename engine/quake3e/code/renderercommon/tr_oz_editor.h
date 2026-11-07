/*
===========================================================================
Oz: the map editor's render API (ENGINE_PLAN.md P13, engine/EDITOR.md)

The editor (engine/src/oz/editor) draws the map it edits through this table, which a renderer hands
out from refexport_t.GetEditorRender. The oz renderer (rend2, renderer2/tr_oz_editor.c) has it; the
others leave GetEditorRender NULL, and the editor asks for the oz renderer.

- A mesh is geometry the editor builds and rebuilds as the map changes: vertices, 16-bit indexes and
  ranges of them, each drawn with one shader. Its handle is a model handle, so it draws as an RT_MODEL
  refEntity (origin, axis, customShader and shaderRGBA as for any model) in any scene.
- A mesh of lines draws GL_LINES (the grid, edges, handles) with the vertex colors, unlit.
- Shaders from RegisterShader draw as the world draws them, with their lightmap stages lit by the
  scene's light instead: the view light (SetViewLight) and dynamic lights. RegisterShader with a name
  of one of the editor's own looks ("$oz_ed_*", below) gives that look.
- A scene with RDF_OZ_EDITOR (and RDF_NOWORLDMODEL, as there's no world) is the editor's 3D view: it
  keeps the world view's tone map and bloom and draws out to OZ_ED_FAR.
- With RDF_OZ_ORTHO too it's one of the editor's 2D views: orthographic, fov_x and fov_y the width
  and height it sees in map units, from the view's origin forward out to 2 * OZ_ED_FAR.
- Meshes live until freed or until the renderer shuts down; the editor builds them again at the next
  renderer start (a vid_restart, a map load).

Plain C with no engine types, so the client's C++ can include it.
===========================================================================
*/
#ifndef TR_OZ_EDITOR_H
#define TR_OZ_EDITOR_H

#define OZ_ED_RENDER_VERSION	1
#define OZ_ED_FAR				32768.0f	// an RDF_OZ_EDITOR scene's far clip
#define RDF_OZ_EDITOR			0x0400		// the editor's 3D view (with RDF_NOWORLDMODEL)
#define RDF_OZ_ORTHO			0x0800		// with RDF_OZ_EDITOR: a 2D view (fov_x, fov_y: its size in map units)

// The editor's own looks, by name: unlit, the vertex colors over the shader's texture or white.
//   $oz_ed_lines     lines, depth tested                   $oz_ed_lines_xray  lines, drawn through everything
//   $oz_ed_flat      solid triangles in the vertex colors  $oz_ed_tint        triangles blended over (selection)
//   $oz_ed_ghost     see-through triangles (tool brushes, the create box)
//   $oz_ed_sky:<image>  the image, unlit and repeating, drawn first behind everything (the World tab's sky)

typedef struct {
	float			xyz[3];
	float			st[2];
	float			normal[3];
	float			tangent[4];	// along s; w: the bitangent's sign (along t)
	unsigned char	color[4];
} ozEdVertex_t;

typedef struct {
	int				shader;		// a handle from RegisterShader
	int				firstIndex;
	int				numIndexes;
} ozEdRange_t;

typedef struct ozEdRender_s {
	int		version;			// OZ_ED_RENDER_VERSION

	// A shader as the world draws it, lit by the scene's light; or one of the editor's looks.
	int		(*RegisterShader)( const char *name );
	// The size of a shader's first image in texels (0 0 when it has none), as drawn.
	void	(*ShaderImageSize)( int shader, int *width, int *height );

	int		(*CreateMesh)( int lines );	// a model handle; 0 when out of slots
	// Replaces the mesh's geometry (any size; 65536 vertexes at most).
	void	(*UploadMesh)( int mesh, const ozEdVertex_t *verts, int numVerts, const unsigned short *indexes,
				int numIndexes, const ozEdRange_t *ranges, int numRanges );
	void	(*FreeMesh)( int mesh );

	// The light an RDF_OZ_EDITOR scene's models get: ambient and directed in 0..255, and the
	// direction the directed light comes from (toward it).
	void	(*SetViewLight)( const float ambient[3], const float directed[3], const float dir[3] );
} ozEdRender_t;

#endif
