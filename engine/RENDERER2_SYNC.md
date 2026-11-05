# rend2 sync: ioquake3's renderergl2 in Quake3e's renderer2

The `oz` renderer module (`OzArena_oz_x86_64.dll`, `cl_renderer oz`) is ioquake3's current OpenGL 2 renderer ("rend2"), kept in Quake3e's folder `engine/quake3e/code/renderer2` so later Quake3e merges still line up. This file lists every place it differs from ioquake3's `code/renderergl2`, so ioquake3's later changes cherry-pick cleanly (ENGINE_PLAN.md P0, "The blend's two ioquake3 pieces").

## Sync points

| | Commit | Date |
|---|---|---|
| Quake3e's copy last followed ioquake3 at | `b8be5a8bd` ("Fix line not removed in previous commit") | 2021-07-06 |
| Synced to | `87a99d18` (ioquake3 `main`) | 2026-10-12 |

The base was found by measuring every ioquake3 commit that touched `code/renderergl2` against Quake3e's copy: `b8be5a8bd` was the closest (2,403 differing lines; the 2016 import point was 6,800). The 50 ioquake3 commits since then were three-way merged (`git merge-file`, base `b8be5a8bd`, ours Quake3e 2026-10-14, theirs `87a99d18`): 23 conflicts in 11 files, resolved as below.

`engine/thirdparty.json` "upstream.ioq3.commit" records the synced commit; `engine/tools/upstream_report.py` lists ioquake3's renderer commits since then.

## How to sync again

1. `git fetch ioq3 main`, then `python engine/tools/upstream_report.py` to see the new `code/renderergl2` commits.
2. For each one: `git show <commit> -- code/renderergl2 | git apply -p3 --directory=engine/quake3e/code/renderer2 -3` (or cherry-pick by hand), keeping the adaptations below.
3. Build (`python engine/build_engine.py --skip-qvms`), run q3dm17 on `cl_renderer oz`, and update the sync point here and in `thirdparty.json`.

## Adaptations (keep these on every sync)

**Quake3e's platform layer, not SDL's.** ioquake3 loads GL through SDL and supports OpenGL ES and Emscripten; Quake3e has its own window and GL loading (`ri.GL_GetProcAddress`, `ri.GLimp_*`).
- `tr_extensions.c`: no `SDL.h`; `SDL_GL_GetProcAddress` is defined as `ri.GL_GetProcAddress`; extensions are checked by Quake3e's `GLimp_HaveExtension` against the extension string. The GL version, vendor and renderer strings and `GLimp_InitGamma` come from Quake3e's block. ioquake3's OpenGL ES extension block is left out.
- `tr_extensions.c` sets up `GL_EXT_texture_filter_anisotropic` (marked `Oz:`). ioquake3 did it in its `sdl_glimp.c`, and Quake3e's renderers each do it themselves, so after the merge nothing set `textureFilterAnisotropic`: every texture drew without anisotropic filtering, whatever `r_ext_max_anisotropy` or Settings' Anisotropic filtering said, and floors and walls seen at an angle blurred toward the distance. As in ioquake3, `maxAnisotropy` keeps the driver's maximum and `R_CreateImage2` clamps `r_ext_max_anisotropy` to it. The look test's q3dm17 spots (2026-10-30) got about twice the fine detail back, as id's look has it; 8x and 16x drew nearly the same pictures, so the default stays 8 (Quake3e's).
- Desktop GL only: `qglesMajorVersion` stays declared and is always 0, so ioquake3's GLES branches elsewhere compile but never run (keeping them makes later merges smaller). `glRefConfig.vaoCacheGlIndexType` is always `GL_UNSIGNED_INT`, `occlusionQueryTarget` always `GL_SAMPLES_PASSED`.
- `tr_init.c`: `r_swapInterval` belongs to Quake3e's client (no Emscripten default here). Video capture keeps ioquake3's RGBA-to-RGB conversion but encodes JPEG through the client (`ri.CL_SaveJPGToBuffer`, not ioquake3's `RE_SaveJPGToBuffer`).
- Quake3e's own `tr_common.h`, `qgl.h`, `glext.h` stay (ioquake3's live in `renderercommon`). `tr_common.h` defines `Q_ALIGN` as Quake3e's `QALIGN` and declares `R_LoadPVR`.
- `tr_image_pvr.c` is ioquake3's `renderercommon/tr_image_pvr.c`, copied into `renderer2` (Quake3e's shared `renderercommon` has no PVR loader, and the other renderers don't need one).

**Quake3e features ioquake3 doesn't have, kept.**
- `tr_flares.c`: no flares in hyperspace (Quake3e 2021-02), before ioquake3's `r_flares` check.
- `tr_local.h`: `RE_FinishBloom`, `RE_ThrottleBackend`, `RE_CanMinimize`, `RE_GetConfig`, `RE_VertexLighting` (Quake3e's renderer interface), beside ioquake3's `R_ConvertTextureFormat`.
- `tr_image.c`: Quake3e's `namelen` kept with ioquake3's `dataType`.
- `tr_init.c`: Quake3e's cvar descriptions (`Cvar_SetDescription`).
- `const` where Quake3e added it: `FBO_Blit`/`FBO_BlitFromTexture` color, `R_Radix` source (ioquake3's rename of its first argument to `offset` is taken).

**Taken from ioquake3 over Quake3e.**
- `RB_RenderDrawSurfList` is not static (ioquake3 calls it from outside `tr_backend.c`).
- `DrawNormals` (`r_shownormals`): ioquake3's implementation (Quake3e's was a stub).
- `tr_shader.c`: the `sh` to `pShader` rename, and the `sunShadowScale` parse fix.
- `R_DrawElements` takes `firstIndex` as an `int` (marked `Oz:`). Quake3e's copy narrowed it to `glIndex_t`, which is 16 bits here. That copy has no VAO cache, but with ioquake3's, offsets into the cache's index buffer pass 65,535, wrapped, and drew another batch's triangles in place of the surface: the garbage `r_vaoCache 1` drew after the P0 merge (q3dm1's white floors and missing walls, and on q3dm17 floors swapped for other geometry). Not an upstream bug: ioquake3 has `int`, and Quake3e's copy never used the cache.
- New GLSL: `greyscale_fp.glsl`, `greyscale_vp.glsl`.

**Fixes to Quake3e's copy** (worth sending upstream): `tr_shader.c` used `TK_GENEGIC`, a typo for `TK_GENERIC`; it never compiled because Quake3e's CMake doesn't build `renderer2`.

**Fixes to both upstreams** (worth sending to ioquake3 and Quake3e; marked `Oz:`):
- `GLSL_InitUniforms` zeroes each program's uniform cache (`uniformBuffer`), as GL zeroes a newly linked program's uniforms. Both allocate it with `ri.Malloc`, which is `Z_TagMalloc` and not zero-filled, and `GLSL_SetUniform*` skip any value the cache already holds. After a renderer restart that keeps the context (`RE_Shutdown(REF_KEEP_CONTEXT)`, as connecting to a server does), the zone can hand a program the last program's freed cache, so the sampler setup at init skipped units it thought were set: the lightall programs kept `u_SpecularMap` and `u_CubeMap` on unit 0, the diffuse map. With normal or specular mapping on, the world then drew flat white, or black with blown-out streaks with `r_mergeLightmaps 0`, while models and the HUD were fine. Whether the stale values lined up depended on the zone's layout, which even the length of `fs_homepath` shifts. On 2026-10-29 `engine/tests/pure_server_test.py` run from `%TEMP%` broke in all 11 runs before the fix whose settings sampled those maps (the 12th, with normal, specular and deluxe mapping off, drew correctly), and drew correctly in 7 of 7 after it.

## Oz changes (P1, the world look; keep on every sync)

Marked `Oz:` in the code. The world library and its scripts come from `tools/make_pk3s.py`.
- `tr_shader.c`: `scripts/*.ozmtr` are read after every other script (loaded first, so the temp
  buffers still free in order), so they override id's. Only this renderer reads them.
- `tr_image.c`: a `.dds` beside an image asked for without an extension (an implicit shader's
  image and its `_n` / `_s` maps) wins even with `r_ext_compressed_textures 0`.
- `glsl/lightall_fp.glsl`: ambient occlusion from the specular map's blue channel (`r_pbr 1`),
  on the baked light and the cubemap reflection.
- `glsl/tonemap_fp.glsl`, `r_ozToneMap 1` (default): display values unchanged up to 0.8, then a
  soft shoulder; rend2's filmic curve (`r_ozToneMap 0`) has a toe and a black point that crushed
  the maps' shadows.
- `RB_OzBloom` (`tr_postprocess.c`, `glsl/ozbloom_fp.glsl`, `tr.ozBloomShader`): bright pass over
  `r_ozBloomThreshold`, three rounds of `RB_BlurAxis`'s taps, added back before the tone map
  (`r_ozBloom`, `r_ozBloomIntensity`). Each axis is one pass (`glsl/ozblur_fp.glsl`,
  `tr.ozBlurShader`) rather than `RB_BlurAxis`'s five blended blits.
- Defaults: `r_pbr 1`, `r_glossType 2` (roughness), `r_cubeMapping 1`, `r_autoExposure 0`.
- `tr_main.c` `R_RadixSort`: a first pass on `cubemapIndex`, so the surfaces of each cubemap are
  drawn together within a sort key. `RB_RenderDrawSurfList` starts a batch at every cubemap change
  and the sort key has no cubemap bits, so a map's cubemaps interleaved: q3dm17's slowest spot ran
  230 fps with 28 cubemaps, 350 with this. Worth sending to ioquake3.

- `r_vaoCache 1` by default (ioquake3's default is 0): world surfaces stay in a GPU buffer between
  frames instead of being copied into the tess buffer every frame. Its garbage after the P0 merge
  was the 16-bit `firstIndex` (`R_DrawElements`, above). In the look test (35 maps, 1920x1080) map
  averages went from 352-842 fps to 669-1002 and the slowest spot from 302 to 621; 25 maps now
  average at Quake3e's 1000 fps cap.
  - `RB_SurfaceVaoCached` ORs a surface's dlight and pshadow bits in after a mid-batch flush, not
    before. ioquake3 gives them to the flushed batch, then `RB_BeginSurface` clears them, so that
    surface loses its dynamic lights for the frame. Worth sending to ioquake3.
  - With the cache, curved surfaces draw at full detail at any distance: the cached path returns
    before `RB_SurfaceGrid`'s LOD, so `r_lodCurveError` only applies with the cache off.
  - The cvar is archived, so a config written before keeps `r_vaoCache 0`.
  - `engine/tests/vao_cache_test.py` checks it at the look test's spots, cache off and on within one
    run. 35 of 35 maps match, and the unfixed build fails all 10 spots of q3dm1 and q3dm17. In the
    same runs, map averages went from 388-840 fps to 692-1003 (q3dm17 585 to 914, q3tourney6 388
    to 714).

## Oz changes (P4, the native UI; keep on every sync)

- `tr_oz_ui.c` (P4): the native UI's renderer, `renderercommon/tr_oz_ui.h`'s calls. A file of its own; `tr_init.c` only hands it out (`re.GetUIRender = R_UI_GetRender`) and frees it (`R_UI_Shutdown()` at the top of `RE_Shutdown`). It draws with raw GL but through rend2's caches (`GL_UseProgram`, `GL_BindMultiTexture`, `GL_BindFramebuffer`, `GL_State`, `R_BindNullVao`), so a sync that changes those helpers needs a look at it.
- Found with it, in ioquake3's code (an upstream candidate): `RB_StretchPic` begins a surface only when the shader changes, but `RB_EndSurface` clears `tess.useInternalVao`. The first 2D draw of a command batch that shares the last surface's shader skips `RB_UpdateTessVao` and draws from whatever VAO is bound, with stale data. Stock rend2 only gets away with it because the tess VAO is still bound; `tr_oz_ui.c` sets `tess.shader = NULL` after the UI. That missed the UI making its buffers between frames (`UI_MakeVao` unbinds rend2's VAO): the first frame after a `vid_restart` on the main menu drew the showcase's background fill from VAO 0 and crashed in the driver. Since 2026-10-31 `RB_StretchPic` also begins a surface when `tess.useInternalVao` is clear (marked `Oz:`), so the pic always goes through the tess VAO.

## Oz changes (P2, the fighters; keep on every sync)

The rigs come from `tools/make_fighters.py` (`oz_rig.iqm` beside id's player md3s, its comment block describing the parts, the spine and the ragdoll).
- `tr_oz_rig.c`, `tr_oz_rig.h` (new): the fighters under any cgame.
  - `R_RegisterMD3` calls `R_OzRigRegister`: a `lower`, `upper` or `head.md3` with `oz_rig.iqm` in its folder gets `model_t.ozRig` and `ozRigPart` (only with `r_ozRigs 1`, latched). With `r_ozModernFighters 1` (latched; Settings > Experimental > Modern player models) an `oz_rig_modern.iqm` beside it comes first: the modern fighters of `pak_oz_modern_fighters.pk3` (`tools/fighter_swap`).
  - `RE_RenderScene` calls `R_OzRigPrepareScene` after `RE_BeginScene`. It matches each torso to the legs whose `tag_torso` it sits on and each head to its torso's `tag_head`, poses one skeleton per fighter (spreading the torso turn along the spine), and gives every part entity `ozBones` (skinning matrices relative to that entity), `ozDissolve` and `ozCenter`.
  - `R_AddEntitySurfaces` draws a `MOD_MESH` entity with `ozBones` through `R_AddOzRigSurfaces`: that part's IQM meshes, shaders from its skin by surface name, culled and fogged at `ozCenter`.
  - `R_LerpTag` answers `tag_torso`, `tag_head` and `tag_weapon` from the rig (`R_OzRigLerpTag`); any other tag still comes from the md3.
  - `RB_IQMSurfaceAnim` and `RB_IQMSurfaceAnimVao` take `backEnd.currentEntity->ozBones` when set instead of computing the pose. `R_IQMJointMats` exposes `ComputeJointMats`; `R_LoadIQM` keeps the comment block (`iqmData_t.comment`).
  - The matrices live in a per-frame pool, reset in `R_InitNextFrame` (`R_OzRigNextFrame`); `RE_AddRefEntityToScene` clears the entity's fields.
- `tr_oz_ragdoll.c` (new): Verlet ragdolls for fighters whose legs play a death, driven from `R_OzRigPrepareScene` in scenes with a world; needs the new refimport `CM_BoxTrace` and `CM_PointContents` (`renderercommon/tr_public.h`, set in `cl_main.c`). `ozragdolls` prints its counts; `r_ozCorpseBurn`, `r_ozRagdollTest` (a cheat).
- The corpse burn: `glsl/lightall_vp.glsl` and `glsl/generic_vp.glsl` pass `var_OzPosition` (world space); `glsl/lightall_fp.glsl` and `glsl/generic_fp.glsl` (a skin's scripted shaders draw there) discard where two octaves of value noise fall under `u_OzDissolve` (new uniform `UNIFORM_OZDISSOLVE`, set per stage in `RB_IterateStagesGeneric`) and add an overbright orange edge. `RB_RenderDrawSurfList` leaves a burning entity out of the depth prepass.

## Oz changes (P3, effects and cinematics; keep on every sync)

- `tr_backend.c` `RE_StretchRaw`: no power-of-two check. The upscaled cinematics are 1280x960 (`client/cl_cin_ogv.c`) and OpenGL 3 takes frames of any size; the client still gives the opengl and vulkan renderers power-of-two frames.
- Stage keywords `ozGlow <scale>`, `ozMono <share>` and `ozSoft <units>` (`tr_shader.c` ParseStage, `shaderStage_t`), set as `u_OzGlow`, `u_OzMono` and `u_OzSoft` in `RB_IterateStagesGeneric` and applied in `generic_fp.glsl`; `ozMono` greys the image (its brightest channel) so `rgbGen entity` or `vertex` colors it (Graphics > Lightning color, Plasma color); `ozSoft` samples `tr.hdrDepthImage` (bound to `TB_SHADOWMAP`), which `tr_image.c` now always creates.
- `tr_local.h` `MAX_POLYS` 8192 and `MAX_POLYVERTS` 32768 (id's 600 and 3000; Quake3e's renderers' values): the remade effects' sparks, beams and burn lines are polys. `tr_scene.c` counts what each frame asked for; `ozpolys` prints the most polys and verts a frame asked for since the last call and the polys dropped over the limits.

## Oz changes (P13, the map editor; keep on every sync)

The map editor's 3D view (engine/EDITOR.md) draws through `renderercommon/tr_oz_editor.h`, which `tr_init.c` hands out (`re.GetEditorRender = R_OzEd_GetRender`) and frees (`R_OzEd_Shutdown()` in `RE_Shutdown`). The opengl and vulkan renderers leave it NULL.
- `tr_oz_editor.c` (new): meshes the editor builds and rebuilds as the map changes. A mesh is a model of its own type, `MOD_OZEDMESH` (`tr_local.h`), drawn as an RT_MODEL entity: `R_AddEntitySurfaces` calls `R_AddOzEdMeshSurfaces`, which culls by its bounds, lights it with `R_SetupEntityLighting` and adds one `SF_OZ_EDMESH` surface (`rb_surfaceTable`, `tr_surface.c`) per range of its indexes. `RB_SurfaceOzEdRange` binds the mesh's own vertex array (R_CreateVao2's layout) and draws the range, as `RB_SurfaceVaoMdvMesh` draws an md3's. Model slots can't be freed (`R_AllocModel`), so a freed mesh's slot is reused; its GL buffers are deleted. `R_ModelBounds` answers for it (`R_OzEdMeshBounds`).
- Line meshes: `tess.ozLines` makes `R_DrawElements` (`tr_shade.c`) draw `GL_LINES`.
- `LIGHTMAP_OZ_EDITOR` (-5): a world shader on an editor mesh. `R_FindShaderEx` accepts it; an implicit shader takes it as `LIGHTMAP_NONE` (`CGEN_LIGHTING_DIFFUSE`); in `CollapseStagesToGLSL` a diffuse stage paired with a lightmap stage collapses with the light vector instead (the scene's light, as on a model), so remastered materials keep their normal and specular maps.
- `R_OzEdLook` (`tr_shader.c`): the editor's own looks, made the first time they're asked for (unlit, in the vertex colors), each with its sort: `$oz_ed_flat` (opaque), `$oz_ed_tint` (the selection's, on faces already drawn: blended, polygon offset, `SS_DECAL`), `$oz_ed_ghost` (two-sided) and `$oz_ed_tool` (one-sided, the tool brushes' and entity boxes') blended at `SS_BLEND0`, `$oz_ed_lines` and `$oz_ed_lines_xray` (no depth test) after the game's blends, and `$oz_ed_handles` (no depth test, `SS_NEAREST`) over everything.
- `OzEd_UploadMesh` leaves nothing bound (`glState.currentVao = NULL`, `tess.shader = NULL`). rend2 draws a batch's first 2D pic from whatever vertex array is bound when its shader matches the last surface's (the P4 note above): the editor uploads its overlays as it draws, and its background fill drew from the last mesh uploaded, leaving the view's void unfilled; the bloom then added up there over the frames to white.
- A triangle faces the camera when it's clockwise seen from its front (`GL_Cull` culls `GL_FRONT` for `CT_FRONT_SIDED`, as in id's renderers). The editor's meshes are wound that way.
- `R_OzThumbnail` (`tr_image.c`, `tr_common.h`), the texture browser's thumbnails through the UI's `LoadThumbnail` (`tr_oz_ui.c`, `tr_oz_ui.h`, `OZ_UI_RENDER_VERSION` 2; the other renderers leave it NULL): `R_LoadImage`'s data, a .dds's mip level that fits (its sRGB formats uploaded as plain, as the UI draws), else boxed down; alpha drawn opaque.
- `R_SetupEntityLighting` (`tr_light.c`): an editor scene without a world (`RDF_OZ_EDITOR` with `RDF_NOWORLDMODEL`) lights its models by the editor's view light; with one (the lit preview, a compiled map loaded as the world) by the map's light grid, as the game does.
- `RDF_OZ_ORTHO` (`tr_oz_editor.h`, with `RDF_OZ_EDITOR`): the editor's 2D views. `RE_RenderScene` (`tr_scene.c`) sets `VPF_ORTHOGRAPHIC`; `R_RenderView` (`tr_main.c`) then calls `R_SetupProjectionOrtho` (the sun shadows' own) with `fovX` and `fovY` as the map units seen across and up, from the view's origin out to `2 * OZ_ED_FAR`, in place of `R_SetupProjection`, and `R_GenerateDrawSurfs` leaves out `R_SetupProjectionZ`, which would put a perspective depth back. Several editor scenes a frame (the four panes) each get their own post-process box.
- `RDF_OZ_EDITOR` (`tr_oz_editor.h`, with `RDF_NOWORLDMODEL`): the editor's view keeps the world view's post-process (`tr_scene.c`: tone map and bloom) and draws out to `OZ_ED_FAR` (`tr_main.c` `R_SetFarClip`); `R_SetupEntityLighting` (`tr_light.c`) gives its models the editor's view light (`R_OzEdViewLight`, set through `SetViewLight`), without the 32 minimum other entities get.

## Build

`engine/quake3e/CMakeLists.txt` builds the module from `code/renderer2/*.c` (not `stringify.c`), `code/renderercommon` and the three shared files, and embeds each `glsl/*.glsl` as a C string with `cmake_modules/stringify_shader.cmake` (ioquake3's script; Quake3e's `stringify.c` needs POSIX `libgen.h`, which MSVC lacks).

## Checked (2026-10-28)

`cl_renderer oz` on an RTX 3070 Ti, OpenGL 4.6: 115 GLSL shaders compile, q3dm17 draws the same scene from the same spawn as the `opengl` renderer.
