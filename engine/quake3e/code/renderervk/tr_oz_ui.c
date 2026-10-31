/*
===========================================================================
Oz: the native UI's renderer on Quake3e's vulkan renderer (ENGINE_PLAN.md P4, renderercommon/tr_oz_ui.h)

The oz renderer's port of RmlUi 6.3's GL3 backend (renderer2/tr_oz_ui.c: Backends/RmlUi_Renderer_GL3.cpp,
MIT, Copyright (c) 2019-2023 The RmlUi Team and contributors) in Vulkan, with the same programs
(shaders/oz_ui, compiled to SPIR-V by engine/build_engine.py), layers, filters and clip masks.

It keeps GL's conventions so the port follows the GL code line for line: the projection flips y as
GL's does, so a layer's rows run up from the bottom as an FBO's do, and scissors, viewports, blits
and texture coordinates are the GL code's numbers. A thin layer of GL-like state (the bound target
and textures, scissor, blending, stencil) opens and closes render passes and moves images between
layouts as the draws need them.

The UI's own work, every layer and filter pass, goes into a command buffer of its own, submitted at
EndFrame ahead of the frame's. Only the last step, the base layer composited onto the frame, is a
render command (RC_OZ_UI) in the frame's main render pass, after the 2D queued before it and below
what comes after (the console). Quake3e's render passes stay as they are.

Geometry lives in the shared pool's chunks (renderercommon/tr_oz_ui_pool.c): host-visible buffers
mapped once, so a compile is a copy. Three command buffers rotate with fences; what is released
(geometry ranges, textures, staging buffers) is freed once the fence of the last frame that could
have used it has passed.
===========================================================================
*/
#include "tr_local.h"
#include "vk.h"
#include "../renderercommon/tr_oz_ui_pool.h"

#include "shaders/spirv/oz_ui_shader_data.c"

#define UI_MAX_LAYERS		8
#define UI_FRAMES			UI_POOL_DELAY		// command buffers in flight
#define UI_COLOR_FORMAT		VK_FORMAT_R8G8B8A8_UNORM
#define UI_GRADIENT_RING	( 256 * 1024 )		// a frame's gradient parameters
#define UI_MAX_SETS			4096

#define UI_CHECK( call ) { \
	const VkResult res = call; \
	if ( res < 0 ) { \
		ri.Error( ERR_FATAL, "UI: %s returned %i", #call, res ); \
	} \
}

// the Vulkan calls made here, loaded through vk.c (vk_instance_proc, vk_device_proc)
#define UI_INSTANCE_PROCS \
	UI_PROC( vkGetPhysicalDeviceFormatProperties ) \
	UI_PROC( vkGetPhysicalDeviceMemoryProperties ) \
	UI_PROC( vkGetPhysicalDeviceProperties )

#define UI_DEVICE_PROCS \
	UI_PROC( vkAllocateCommandBuffers ) \
	UI_PROC( vkAllocateDescriptorSets ) \
	UI_PROC( vkAllocateMemory ) \
	UI_PROC( vkBeginCommandBuffer ) \
	UI_PROC( vkBindBufferMemory ) \
	UI_PROC( vkBindImageMemory ) \
	UI_PROC( vkCmdBeginRenderPass ) \
	UI_PROC( vkCmdBindDescriptorSets ) \
	UI_PROC( vkCmdBindIndexBuffer ) \
	UI_PROC( vkCmdBindPipeline ) \
	UI_PROC( vkCmdBindVertexBuffers ) \
	UI_PROC( vkCmdBlitImage ) \
	UI_PROC( vkCmdClearAttachments ) \
	UI_PROC( vkCmdCopyBufferToImage ) \
	UI_PROC( vkCmdCopyImage ) \
	UI_PROC( vkCmdDraw ) \
	UI_PROC( vkCmdDrawIndexed ) \
	UI_PROC( vkCmdEndRenderPass ) \
	UI_PROC( vkCmdPipelineBarrier ) \
	UI_PROC( vkCmdPushConstants ) \
	UI_PROC( vkCmdSetScissor ) \
	UI_PROC( vkCmdSetStencilReference ) \
	UI_PROC( vkCmdSetViewport ) \
	UI_PROC( vkCreateBuffer ) \
	UI_PROC( vkCreateCommandPool ) \
	UI_PROC( vkCreateDescriptorPool ) \
	UI_PROC( vkCreateDescriptorSetLayout ) \
	UI_PROC( vkCreateFence ) \
	UI_PROC( vkCreateFramebuffer ) \
	UI_PROC( vkCreateGraphicsPipelines ) \
	UI_PROC( vkCreateImage ) \
	UI_PROC( vkCreateImageView ) \
	UI_PROC( vkCreatePipelineLayout ) \
	UI_PROC( vkCreateRenderPass ) \
	UI_PROC( vkCreateSampler ) \
	UI_PROC( vkCreateShaderModule ) \
	UI_PROC( vkDestroyBuffer ) \
	UI_PROC( vkDestroyCommandPool ) \
	UI_PROC( vkDestroyDescriptorPool ) \
	UI_PROC( vkDestroyDescriptorSetLayout ) \
	UI_PROC( vkDestroyFence ) \
	UI_PROC( vkDestroyFramebuffer ) \
	UI_PROC( vkDestroyImage ) \
	UI_PROC( vkDestroyImageView ) \
	UI_PROC( vkDestroyPipeline ) \
	UI_PROC( vkDestroyPipelineLayout ) \
	UI_PROC( vkDestroyRenderPass ) \
	UI_PROC( vkDestroySampler ) \
	UI_PROC( vkDestroyShaderModule ) \
	UI_PROC( vkDeviceWaitIdle ) \
	UI_PROC( vkEndCommandBuffer ) \
	UI_PROC( vkFreeDescriptorSets ) \
	UI_PROC( vkFreeMemory ) \
	UI_PROC( vkGetBufferMemoryRequirements ) \
	UI_PROC( vkGetImageMemoryRequirements ) \
	UI_PROC( vkMapMemory ) \
	UI_PROC( vkQueueSubmit ) \
	UI_PROC( vkResetCommandBuffer ) \
	UI_PROC( vkResetFences ) \
	UI_PROC( vkUpdateDescriptorSets ) \
	UI_PROC( vkWaitForFences )

#define UI_PROC( name ) static PFN_##name q##name;
UI_INSTANCE_PROCS
UI_DEVICE_PROCS
#undef UI_PROC

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
	UM_GEOMETRY_VERT, UM_COLOR_FRAG, UM_TEXTURE_FRAG, UM_GRADIENT_FRAG, UM_PASS_VERT, UM_PASSTHROUGH_FRAG,
	UM_COLOR_MATRIX_FRAG, UM_BLEND_MASK_FRAG, UM_BLUR_VERT, UM_BLUR_FRAG, UM_DROP_SHADOW_FRAG,
	UM_COUNT
} uiModule_t;

static const struct {
	const unsigned char	*code;
	int					size;
} uiModuleCode[UM_COUNT] = {
	{ oz_ui_geometry_vert_spv, sizeof( oz_ui_geometry_vert_spv ) },
	{ oz_ui_color_frag_spv, sizeof( oz_ui_color_frag_spv ) },
	{ oz_ui_texture_frag_spv, sizeof( oz_ui_texture_frag_spv ) },
	{ oz_ui_gradient_frag_spv, sizeof( oz_ui_gradient_frag_spv ) },
	{ oz_ui_pass_vert_spv, sizeof( oz_ui_pass_vert_spv ) },
	{ oz_ui_passthrough_frag_spv, sizeof( oz_ui_passthrough_frag_spv ) },
	{ oz_ui_color_matrix_frag_spv, sizeof( oz_ui_color_matrix_frag_spv ) },
	{ oz_ui_blend_mask_frag_spv, sizeof( oz_ui_blend_mask_frag_spv ) },
	{ oz_ui_blur_vert_spv, sizeof( oz_ui_blur_vert_spv ) },
	{ oz_ui_blur_frag_spv, sizeof( oz_ui_blur_frag_spv ) },
	{ oz_ui_drop_shadow_frag_spv, sizeof( oz_ui_drop_shadow_frag_spv ) }
};

static const struct {
	uiModule_t	vert, frag;
	qboolean	geometry;	// RmlUi's vertexes, else a full-screen pass from gl_VertexIndex
	int			textures;	// sampled units
} uiProgramDefs[UP_COUNT] = {
	{ UM_GEOMETRY_VERT, UM_COLOR_FRAG, qtrue, 0 },
	{ UM_GEOMETRY_VERT, UM_TEXTURE_FRAG, qtrue, 1 },
	{ UM_GEOMETRY_VERT, UM_GRADIENT_FRAG, qtrue, 0 },
	{ UM_PASS_VERT, UM_PASSTHROUGH_FRAG, qfalse, 1 },
	{ UM_PASS_VERT, UM_COLOR_MATRIX_FRAG, qfalse, 1 },
	{ UM_PASS_VERT, UM_BLEND_MASK_FRAG, qfalse, 2 },
	{ UM_BLUR_VERT, UM_BLUR_FRAG, qfalse, 1 },
	{ UM_PASS_VERT, UM_DROP_SHADOW_FRAG, qfalse, 1 }
};

// the stencil as GL's state has it: off (or ALWAYS and KEEP), EQUAL and KEEP, or a clip mask written
typedef enum { US_OFF, US_TEST, US_REPLACE, US_INCR, US_COUNT } uiStencil_t;
typedef enum { UK_LAYER, UK_POST, UK_COUNT } uiPassKind_t;	// render passes: with the depth-stencil buffer or without

// push constants, as the shaders declare them
typedef struct {
	float	transform[16];
	float	translate[2];
	float	straight;
} uiPushGeometry_t;

typedef struct {
	float	uvOffset[2], uvScale[2];
	float	opacity;
} uiPushPass_t;

typedef struct {
	float	uvOffset[2], uvScale[2];
	float	matrix[16];
} uiPushMatrix_t;

typedef struct {
	float	uvOffset[2], uvScale[2], texCoordMin[2], texCoordMax[2];
	float	color[4];
} uiPushShadow_t;

typedef struct {
	float	uvOffset[2], uvScale[2], texCoordMin[2], texCoordMax[2], texelOffset[2], pad[2];
	float	weights[4];
} uiPushBlur_t;

typedef struct {	// std140, as oz_ui_gradient.frag's Gradient
	int		func, numStops;
	float	p[2], v[2], pad[2];
	float	stopColors[OZ_UI_MAX_STOPS][4];
	float	stopPositions[OZ_UI_MAX_STOPS];
} uiGradientBlock_t;

typedef struct {
	VkImage			image;
	VkDeviceMemory	memory;
	VkImageView		view;
	VkDescriptorSet	descriptor;
	VkImageLayout	layout;		// after the commands recorded so far
	int				width, height;
} uiImage_t;

typedef struct {
	uiImage_t		img;
	VkFramebuffer	framebuffer;
	qboolean		stencil;	// a layer, with the shared depth-stencil buffer
} uiTarget_t;

typedef struct {
	uiImage_t		img;			// ours; an engine image's is its own
	VkDescriptorSet	descriptor;		// the one drawn with
	VkBuffer		staging;		// pixels not copied in yet
	VkDeviceMemory	stagingMemory;
	qboolean		used, owned, straight;
	int				nextFree;
} uiTexture_t;

typedef struct {
	VkBuffer		buffer[2];		// vertexes, indexes
	VkDeviceMemory	memory[2];
	byte			*mapped[2];
} uiChunk_t;

typedef struct {	// released, waiting for the GPU
	uiImage_t		img;
	VkBuffer		buffer;
	VkDeviceMemory	bufferMemory;
} uiGarbage_t;

typedef struct {
	int			x0, y0, x1, y1;	// RmlUi's coordinates: from the top left
	qboolean	valid;
} uiRect_t;

typedef struct {
	VkBuffer		buffer;
	VkDeviceMemory	memory;
	byte			*mapped;
	VkDescriptorSet	descriptor;
} uiRing_t;

static struct {
	qboolean		initialized, failed;

	VkPhysicalDeviceMemoryProperties memoryProps;
	VkDeviceSize	uboAlignment;
	VkFormat		depthFormat;

	VkShaderModule	modules[UM_COUNT];
	VkRenderPass	passes[UK_COUNT];
	VkDescriptorSetLayout uboLayout;
	VkPipelineLayout pipelineLayout;
	VkPipeline		pipelines[UP_COUNT][UK_COUNT][2][US_COUNT];	// [program][kind][blend][stencil], made on first use
	VkPipeline		composite[RENDER_PASS_COUNT];				// onto the frame
	VkSampler		samplerClamp;		// layers: transparent black outside
	VkSampler		samplerRepeat;		// RmlUi's textures, GL_REPEAT
	VkDescriptorPool descriptorPool;

	VkCommandPool	commandPool;
	VkCommandBuffer	cmds[UI_FRAMES];
	VkFence			fences[UI_FRAMES];
	VkCommandBuffer	cmd;				// recording, between BeginFrame and EndFrame
	int				slot;
	uiRing_t		rings[UI_FRAMES];
	uint32_t		ringUsed;

	uiPool_t		pool;
	uiChunk_t		chunks[UI_POOL_MAX_CHUNKS];
	uiTexture_t		*textures;
	int				maxTextures, firstFreeTexture;
	int				pendingUploads;		// made outside a frame, copied in at the next
	uiGarbage_t		*garbage[UI_FRAMES];
	int				numGarbage[UI_FRAMES], maxGarbage[UI_FRAMES];

	int				width, height;		// the layers, the UI's size
	uiImage_t		depthStencil;		// shared by the layers
	uiTarget_t		layers[UI_MAX_LAYERS];
	int				numLayers;			// made
	int				layerTop;			// in use
	uiTarget_t		post[3];			// postprocess primary, secondary, tertiary
	uiTarget_t		mask;

	// GL's state, as the port sets it
	const uiTarget_t *target;			// the bound framebuffer
	const uiTarget_t *passTarget;		// whose render pass is open; NULL for none
	VkPipeline		boundPipeline;
	int				boundChunk;
	VkDescriptorSet	units[2];			// the bound textures
	uiImage_t		*unitImages[2];		// the same, when it's a target of ours
	int				program;
	int				viewport[4];		// glViewport
	int				scissorBox[4];		// glScissor
	uiRect_t		scissor;
	qboolean		blend;				// premultiplied, or none
	qboolean		stencilTest;
	qboolean		stencilEqual;		// EQUAL, else ALWAYS
	int				stencilOp;			// US_OFF (KEEP), US_REPLACE or US_INCR
	int				stencilRef;
	qboolean		colorWrite;
	struct {
		float		uvOffset[2], uvScale[2], texCoordMin[2], texCoordMax[2], texelOffset[2];
		float		opacity, weights[UI_BLUR_WEIGHTS], color[4], matrix[16];
		qboolean	straight;
	} u;								// the programs' uniforms

	float			projection[16], transform[16];
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
Memory, buffers and images
================
*/
static uint32_t UI_MemoryType( uint32_t bits, VkMemoryPropertyFlags props, VkMemoryPropertyFlags preferred ) {
	uint32_t i;

	for ( i = 0; i < ui.memoryProps.memoryTypeCount; i++ ) {
		if ( ( bits & ( 1 << i ) ) && ( ui.memoryProps.memoryTypes[i].propertyFlags & ( props | preferred ) ) == ( props | preferred ) ) {
			return i;
		}
	}
	for ( i = 0; i < ui.memoryProps.memoryTypeCount; i++ ) {
		if ( ( bits & ( 1 << i ) ) && ( ui.memoryProps.memoryTypes[i].propertyFlags & props ) == props ) {
			return i;
		}
	}
	ri.Error( ERR_FATAL, "UI: no Vulkan memory type 0x%x", props );
	return 0;
}


// host-visible and mapped; device-local too when the GPU offers that (its BAR) and it's asked for
static void *UI_MakeBuffer( VkDeviceSize size, VkBufferUsageFlags usage, qboolean deviceLocal, VkBuffer *buffer, VkDeviceMemory *memory ) {
	VkBufferCreateInfo desc;
	VkMemoryRequirements reqs;
	VkMemoryAllocateInfo alloc;
	void *mapped;

	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	desc.size = size;
	desc.usage = usage;
	desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	UI_CHECK( qvkCreateBuffer( vk.device, &desc, NULL, buffer ) );

	qvkGetBufferMemoryRequirements( vk.device, *buffer, &reqs );
	Com_Memset( &alloc, 0, sizeof( alloc ) );
	alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc.allocationSize = reqs.size;
	alloc.memoryTypeIndex = UI_MemoryType( reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		deviceLocal ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : 0 );
	UI_CHECK( qvkAllocateMemory( vk.device, &alloc, NULL, memory ) );
	UI_CHECK( qvkBindBufferMemory( vk.device, *buffer, *memory, 0 ) );
	UI_CHECK( qvkMapMemory( vk.device, *memory, 0, VK_WHOLE_SIZE, 0, &mapped ) );
	return mapped;
}


static void UI_FreeBuffer( VkBuffer *buffer, VkDeviceMemory *memory ) {
	if ( *buffer ) qvkDestroyBuffer( vk.device, *buffer, NULL );
	if ( *memory ) qvkFreeMemory( vk.device, *memory, NULL );	// unmaps
	*buffer = VK_NULL_HANDLE;
	*memory = VK_NULL_HANDLE;
}


static VkDescriptorSet UI_AllocSet( VkDescriptorSetLayout layout ) {
	VkDescriptorSetAllocateInfo alloc;
	VkDescriptorSet set;

	alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	alloc.pNext = NULL;
	alloc.descriptorPool = ui.descriptorPool;
	alloc.descriptorSetCount = 1;
	alloc.pSetLayouts = &layout;
	if ( qvkAllocateDescriptorSets( vk.device, &alloc, &set ) != VK_SUCCESS ) {
		ri.Printf( PRINT_WARNING, "UI: out of descriptor sets (%i)\n", UI_MAX_SETS );
		return VK_NULL_HANDLE;
	}
	return set;
}


static void UI_WriteImageSet( VkDescriptorSet set, VkImageView view, VkSampler sampler ) {
	VkDescriptorImageInfo info;
	VkWriteDescriptorSet desc;

	info.sampler = sampler;
	info.imageView = view;
	info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	desc.dstSet = set;
	desc.descriptorCount = 1;
	desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	desc.pImageInfo = &info;
	qvkUpdateDescriptorSets( vk.device, 1, &desc, 0, NULL );
}


// an image of ours with a view; color ones get a descriptor set with the sampler
static void UI_MakeImage( uiImage_t *img, int width, int height, VkFormat format, VkImageUsageFlags usage, VkSampler sampler ) {
	const qboolean depth = ( usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT ) != 0;
	VkImageCreateInfo desc;
	VkImageViewCreateInfo view;
	VkMemoryRequirements reqs;
	VkMemoryAllocateInfo alloc;

	Com_Memset( img, 0, sizeof( *img ) );
	img->width = width;
	img->height = height;
	img->layout = VK_IMAGE_LAYOUT_UNDEFINED;

	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	desc.imageType = VK_IMAGE_TYPE_2D;
	desc.format = format;
	desc.extent.width = width;
	desc.extent.height = height;
	desc.extent.depth = 1;
	desc.mipLevels = 1;
	desc.arrayLayers = 1;
	desc.samples = VK_SAMPLE_COUNT_1_BIT;
	desc.tiling = VK_IMAGE_TILING_OPTIMAL;
	desc.usage = usage;
	desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	UI_CHECK( qvkCreateImage( vk.device, &desc, NULL, &img->image ) );

	qvkGetImageMemoryRequirements( vk.device, img->image, &reqs );
	Com_Memset( &alloc, 0, sizeof( alloc ) );
	alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc.allocationSize = reqs.size;
	alloc.memoryTypeIndex = UI_MemoryType( reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0 );
	UI_CHECK( qvkAllocateMemory( vk.device, &alloc, NULL, &img->memory ) );
	UI_CHECK( qvkBindImageMemory( vk.device, img->image, img->memory, 0 ) );

	Com_Memset( &view, 0, sizeof( view ) );
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.image = img->image;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = format;
	view.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	view.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	view.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	view.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	view.subresourceRange.aspectMask = depth ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.levelCount = 1;
	view.subresourceRange.layerCount = 1;
	UI_CHECK( qvkCreateImageView( vk.device, &view, NULL, &img->view ) );

	if ( sampler ) {
		img->descriptor = UI_AllocSet( vk.set_layout_sampler );
		if ( img->descriptor ) {
			UI_WriteImageSet( img->descriptor, img->view, sampler );
		}
	}
}


static void UI_FreeImage( uiImage_t *img ) {
	if ( img->descriptor ) qvkFreeDescriptorSets( vk.device, ui.descriptorPool, 1, &img->descriptor );
	if ( img->view ) qvkDestroyImageView( vk.device, img->view, NULL );
	if ( img->image ) qvkDestroyImage( vk.device, img->image, NULL );
	if ( img->memory ) qvkFreeMemory( vk.device, img->memory, NULL );
	Com_Memset( img, 0, sizeof( *img ) );
}


// freed when the GPU is done with this frame's commands (UI_FRAMES frames on)
static void UI_Discard( const uiImage_t *img, VkBuffer buffer, VkDeviceMemory bufferMemory ) {
	const int slot = ui.pool.frame % UI_FRAMES;
	uiGarbage_t *g;

	if ( ui.numGarbage[slot] == ui.maxGarbage[slot] ) {
		const int newMax = ui.maxGarbage[slot] ? ui.maxGarbage[slot] * 2 : 64;
		uiGarbage_t *grown = ri.Malloc( newMax * sizeof( uiGarbage_t ) );
		if ( ui.garbage[slot] ) {
			Com_Memcpy( grown, ui.garbage[slot], ui.numGarbage[slot] * sizeof( uiGarbage_t ) );
			ri.Free( ui.garbage[slot] );
		}
		ui.garbage[slot] = grown;
		ui.maxGarbage[slot] = newMax;
	}
	g = &ui.garbage[slot][ui.numGarbage[slot]++];
	Com_Memset( g, 0, sizeof( *g ) );
	if ( img ) {
		g->img = *img;
	}
	g->buffer = buffer;
	g->bufferMemory = bufferMemory;
}


static void UI_FreeGarbage( int slot ) {
	int i;

	for ( i = 0; i < ui.numGarbage[slot]; i++ ) {
		uiGarbage_t *g = &ui.garbage[slot][i];
		UI_FreeImage( &g->img );
		UI_FreeBuffer( &g->buffer, &g->bufferMemory );
	}
	ui.numGarbage[slot] = 0;
}


/*
================
Recording: barriers and render passes

Every boundary between passes and copies is a full barrier (the UI's passes are few and small):
whatever came before, attachments, copies and reads, is done before what comes after starts, and
the images named move to their new layouts.
================
*/
static VkAccessFlags UI_LayoutAccess( VkImageLayout layout ) {
	switch ( layout ) {
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: return VK_ACCESS_SHADER_READ_BIT;
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: return VK_ACCESS_TRANSFER_READ_BIT;
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: return VK_ACCESS_TRANSFER_WRITE_BIT;
	default: return 0;
	}
}


#define UI_STAGES	( VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT \
					| VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT )
#define UI_ACCESS	( VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT \
					| VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT \
					| VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT )


static void UI_EndPass( void ) {
	if ( ui.passTarget ) {
		qvkCmdEndRenderPass( ui.cmd );
		ui.passTarget = NULL;
	}
}


// a barrier moving up to three images to new layouts (NULL for none); ends the open pass
static void UI_Barrier( uiImage_t *a, VkImageLayout la, uiImage_t *b, VkImageLayout lb, uiImage_t *c, VkImageLayout lc ) {
	uiImage_t *imgs[3];
	VkImageLayout layouts[3];
	VkImageMemoryBarrier barriers[3];
	VkMemoryBarrier memory;
	int i, n = 0;

	UI_EndPass();
	imgs[0] = a; imgs[1] = b; imgs[2] = c;
	layouts[0] = la; layouts[1] = lb; layouts[2] = lc;
	for ( i = 0; i < 3; i++ ) {
		VkImageMemoryBarrier *bar = &barriers[n];
		if ( !imgs[i] || imgs[i]->layout == layouts[i] ) {
			continue;
		}
		Com_Memset( bar, 0, sizeof( *bar ) );
		bar->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		bar->srcAccessMask = UI_LayoutAccess( imgs[i]->layout );
		bar->dstAccessMask = UI_LayoutAccess( layouts[i] );
		bar->oldLayout = imgs[i]->layout;
		bar->newLayout = layouts[i];
		bar->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bar->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bar->image = imgs[i]->image;
		bar->subresourceRange.aspectMask = layouts[i] == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
			? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
		bar->subresourceRange.levelCount = 1;
		bar->subresourceRange.layerCount = 1;
		imgs[i]->layout = layouts[i];
		n++;
	}
	memory.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memory.pNext = NULL;
	memory.srcAccessMask = UI_ACCESS;
	memory.dstAccessMask = UI_ACCESS;
	qvkCmdPipelineBarrier( ui.cmd, UI_STAGES, UI_STAGES, 0, 1, &memory, 0, NULL, n, barriers );
}


// the bound target's render pass, open, with the first textures bound readable
static void UI_BeginPass( int textures ) {
	const uiTarget_t *t = ui.target;
	VkRenderPassBeginInfo begin;
	int i;

	for ( i = 0; i < textures; i++ ) {
		if ( ui.unitImages[i] && ui.unitImages[i]->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ) {
			UI_Barrier( ui.unitImages[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, NULL, 0, NULL, 0 );
		}
	}
	if ( ui.passTarget == t ) {
		return;
	}
	UI_Barrier( (uiImage_t *)&t->img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		t->stencil ? &ui.depthStencil : NULL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, NULL, 0 );

	Com_Memset( &begin, 0, sizeof( begin ) );
	begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	begin.renderPass = ui.passes[t->stencil ? UK_LAYER : UK_POST];
	begin.framebuffer = t->framebuffer;
	begin.renderArea.extent.width = t->img.width;
	begin.renderArea.extent.height = t->img.height;
	qvkCmdBeginRenderPass( ui.cmd, &begin, VK_SUBPASS_CONTENTS_INLINE );
	ui.passTarget = t;
	ui.boundPipeline = VK_NULL_HANDLE;
}


/*
================
Pipelines
================
*/
static VkShaderModule UI_Module( uiModule_t m ) {
	VkShaderModuleCreateInfo desc;
	uint32_t *code;

	if ( ui.modules[m] ) {
		return ui.modules[m];
	}
	code = ri.Malloc( uiModuleCode[m].size );	// aligned for the uint32_t words
	Com_Memcpy( code, uiModuleCode[m].code, uiModuleCode[m].size );
	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	desc.codeSize = uiModuleCode[m].size;
	desc.pCode = code;
	UI_CHECK( qvkCreateShaderModule( vk.device, &desc, NULL, &ui.modules[m] ) );
	ri.Free( code );
	return ui.modules[m];
}


static VkPipeline UI_MakePipeline( uiProgram_t p, VkRenderPass pass, VkSampleCountFlagBits samples, qboolean depthStencil, qboolean blend, uiStencil_t stencil ) {
	static const VkDynamicState dynamics[3] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_STENCIL_REFERENCE };
	VkPipelineShaderStageCreateInfo stages[2];
	VkVertexInputBindingDescription binding;
	VkVertexInputAttributeDescription attribs[3];
	VkPipelineVertexInputStateCreateInfo input;
	VkPipelineInputAssemblyStateCreateInfo assembly;
	VkPipelineViewportStateCreateInfo viewport;
	VkPipelineRasterizationStateCreateInfo raster;
	VkPipelineMultisampleStateCreateInfo multisample;
	VkPipelineDepthStencilStateCreateInfo ds;
	VkPipelineColorBlendAttachmentState attachment;
	VkPipelineColorBlendStateCreateInfo blending;
	VkPipelineDynamicStateCreateInfo dynamic;
	VkGraphicsPipelineCreateInfo desc;
	VkPipeline pipeline;

	Com_Memset( stages, 0, sizeof( stages ) );
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = UI_Module( uiProgramDefs[p].vert );
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = UI_Module( uiProgramDefs[p].frag );
	stages[1].pName = "main";

	// RmlUi's vertexes: ozUiVertex_t
	binding.binding = 0;
	binding.stride = sizeof( ozUiVertex_t );
	binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	attribs[0].location = 0; attribs[0].binding = 0; attribs[0].format = VK_FORMAT_R32G32_SFLOAT; attribs[0].offset = offsetof( ozUiVertex_t, pos );
	attribs[1].location = 1; attribs[1].binding = 0; attribs[1].format = VK_FORMAT_R8G8B8A8_UNORM; attribs[1].offset = offsetof( ozUiVertex_t, color );
	attribs[2].location = 2; attribs[2].binding = 0; attribs[2].format = VK_FORMAT_R32G32_SFLOAT; attribs[2].offset = offsetof( ozUiVertex_t, st );
	Com_Memset( &input, 0, sizeof( input ) );
	input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	if ( uiProgramDefs[p].geometry ) {
		input.vertexBindingDescriptionCount = 1;
		input.pVertexBindingDescriptions = &binding;
		input.vertexAttributeDescriptionCount = 3;
		input.pVertexAttributeDescriptions = attribs;
	}

	Com_Memset( &assembly, 0, sizeof( assembly ) );
	assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assembly.topology = uiProgramDefs[p].geometry ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

	Com_Memset( &viewport, 0, sizeof( viewport ) );
	viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport.viewportCount = 1;
	viewport.scissorCount = 1;

	Com_Memset( &raster, 0, sizeof( raster ) );
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = VK_CULL_MODE_NONE;
	raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	raster.lineWidth = 1.0f;

	Com_Memset( &multisample, 0, sizeof( multisample ) );
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = samples;
	multisample.minSampleShading = 1.0f;

	Com_Memset( &ds, 0, sizeof( ds ) );
	ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;
	ds.maxDepthBounds = 1.0f;
	if ( stencil != US_OFF ) {
		ds.stencilTestEnable = VK_TRUE;
		ds.front.failOp = VK_STENCIL_OP_KEEP;
		ds.front.depthFailOp = VK_STENCIL_OP_KEEP;
		ds.front.passOp = stencil == US_REPLACE ? VK_STENCIL_OP_REPLACE : stencil == US_INCR ? VK_STENCIL_OP_INCREMENT_AND_CLAMP : VK_STENCIL_OP_KEEP;
		ds.front.compareOp = stencil == US_TEST ? VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_ALWAYS;
		ds.front.compareMask = 0xff;
		ds.front.writeMask = 0xff;
		ds.back = ds.front;
	}

	Com_Memset( &attachment, 0, sizeof( attachment ) );
	attachment.blendEnable = blend ? VK_TRUE : VK_FALSE;
	attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	attachment.colorBlendOp = VK_BLEND_OP_ADD;
	attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	attachment.alphaBlendOp = VK_BLEND_OP_ADD;
	attachment.colorWriteMask = ( stencil == US_REPLACE || stencil == US_INCR ) ? 0	// writing a clip mask
		: VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	Com_Memset( &blending, 0, sizeof( blending ) );
	blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blending.attachmentCount = 1;
	blending.pAttachments = &attachment;

	Com_Memset( &dynamic, 0, sizeof( dynamic ) );
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = stencil != US_OFF ? 3 : 2;
	dynamic.pDynamicStates = dynamics;

	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	desc.stageCount = 2;
	desc.pStages = stages;
	desc.pVertexInputState = &input;
	desc.pInputAssemblyState = &assembly;
	desc.pViewportState = &viewport;
	desc.pRasterizationState = &raster;
	desc.pMultisampleState = &multisample;
	desc.pDepthStencilState = depthStencil ? &ds : NULL;
	desc.pColorBlendState = &blending;
	desc.pDynamicState = &dynamic;
	desc.layout = ui.pipelineLayout;
	desc.renderPass = pass;
	desc.subpass = 0;
	desc.basePipelineIndex = -1;
	UI_CHECK( qvkCreateGraphicsPipelines( vk.device, VK_NULL_HANDLE, 1, &desc, NULL, &pipeline ) );
	return pipeline;
}


// the pipeline for a program in the open pass, with GL's blending and stencil state
static VkPipeline UI_Pipeline( uiProgram_t p ) {
	const uiPassKind_t kind = ui.passTarget->stencil ? UK_LAYER : UK_POST;
	uiStencil_t stencil = US_OFF;
	VkPipeline *pipeline;

	if ( kind == UK_LAYER && ui.stencilTest ) {
		if ( !ui.colorWrite ) {
			stencil = ui.stencilOp == US_INCR ? US_INCR : US_REPLACE;
		} else if ( ui.stencilEqual ) {
			stencil = US_TEST;
		}
	}
	pipeline = &ui.pipelines[p][kind][ui.blend ? 1 : 0][stencil];
	if ( !*pipeline ) {
		*pipeline = UI_MakePipeline( p, ui.passes[kind], VK_SAMPLE_COUNT_1_BIT, kind == UK_LAYER, ui.blend, stencil );
	}
	return *pipeline;
}


/*
================
UI_Draw

A draw with the program in use into the bound target, as GL draws with its state: the pass opened,
the pipeline, viewport, scissor, stencil reference and textures set, the uniforms pushed.
================
*/
static void UI_Draw( const void *push, int pushSize, const uiPoolGeometry_t *g, VkDescriptorSet ubo, uint32_t uboOffset ) {
	const uiProgram_t p = ui.program;
	VkPipeline pipeline;
	VkViewport viewport;
	VkRect2D scissor;
	int i;

	if ( !ui.colorWrite && !ui.target->stencil ) {
		return;	// a clip mask with no stencil buffer: nothing to write
	}
	UI_BeginPass( uiProgramDefs[p].textures );
	pipeline = UI_Pipeline( p );
	if ( pipeline != ui.boundPipeline ) {
		qvkCmdBindPipeline( ui.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
		ui.boundPipeline = pipeline;
	}

	viewport.x = ui.viewport[0];
	viewport.y = ui.viewport[1];
	viewport.width = ui.viewport[2];
	viewport.height = ui.viewport[3];
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	qvkCmdSetViewport( ui.cmd, 0, 1, &viewport );
	scissor.offset.x = MAX( 0, ui.scissorBox[0] );
	scissor.offset.y = MAX( 0, ui.scissorBox[1] );
	scissor.extent.width = MAX( 0, ui.scissorBox[2] );
	scissor.extent.height = MAX( 0, ui.scissorBox[3] );
	qvkCmdSetScissor( ui.cmd, 0, 1, &scissor );
	if ( ui.passTarget->stencil && ui.stencilTest && ( ui.stencilEqual || !ui.colorWrite ) ) {
		qvkCmdSetStencilReference( ui.cmd, VK_STENCIL_FACE_FRONT_AND_BACK, ui.stencilRef );
	}

	for ( i = 0; i < uiProgramDefs[p].textures; i++ ) {
		if ( ui.units[i] ) {
			qvkCmdBindDescriptorSets( ui.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ui.pipelineLayout, i, 1, &ui.units[i], 0, NULL );
		}
	}
	if ( ubo ) {
		qvkCmdBindDescriptorSets( ui.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ui.pipelineLayout, 2, 1, &ubo, 1, &uboOffset );
	}
	qvkCmdPushConstants( ui.cmd, ui.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, pushSize, push );

	if ( g ) {
		const uiChunk_t *c = &ui.chunks[g->chunk];
		if ( ui.boundChunk != g->chunk ) {
			const VkDeviceSize zero = 0;
			qvkCmdBindVertexBuffers( ui.cmd, 0, 1, &c->buffer[0], &zero );
			qvkCmdBindIndexBuffer( ui.cmd, c->buffer[1], 0, VK_INDEX_TYPE_UINT32 );
			ui.boundChunk = g->chunk;
		}
		qvkCmdDrawIndexed( ui.cmd, g->numIndexes, 1, g->firstIndex, g->firstVert, 0 );
	} else {
		qvkCmdDraw( ui.cmd, 4, 1, 0, 0 );
	}
}


/*
================
GL's calls, as the port makes them
================
*/
static void UI_UseProgram( uiProgram_t p ) {
	ui.program = p;
}


static void UI_BindTarget( const uiTarget_t *t ) {
	ui.target = t;
}


// unit 0 or 1: one of our targets' images, or a texture's descriptor set
static void UI_BindImage( int unit, uiImage_t *img ) {
	ui.units[unit] = img->descriptor;
	ui.unitImages[unit] = img;
}


static void UI_BindSet( int unit, VkDescriptorSet set ) {
	ui.units[unit] = set;
	ui.unitImages[unit] = NULL;
}


static void UI_Viewport( int x, int y, int w, int h ) {
	ui.viewport[0] = x;
	ui.viewport[1] = y;
	ui.viewport[2] = w;
	ui.viewport[3] = h;
}


static void UI_ScissorBox( int x, int y, int w, int h ) {
	ui.scissorBox[0] = x;
	ui.scissorBox[1] = y;
	ui.scissorBox[2] = w;
	ui.scissorBox[3] = h;
}


// glClear within the scissor box, as GL's is
static void UI_Clear( qboolean color, qboolean stencil, int stencilValue ) {
	const uiTarget_t *t = ui.target;
	VkClearAttachment clears[2];
	VkClearRect rect;
	int n = 0, x0, y0, x1, y1;

	x0 = Com_Clamp( 0, t->img.width, ui.scissorBox[0] );
	y0 = Com_Clamp( 0, t->img.height, ui.scissorBox[1] );
	x1 = Com_Clamp( 0, t->img.width, ui.scissorBox[0] + ui.scissorBox[2] );
	y1 = Com_Clamp( 0, t->img.height, ui.scissorBox[1] + ui.scissorBox[3] );
	if ( x1 <= x0 || y1 <= y0 ) {
		return;
	}
	if ( color ) {
		Com_Memset( &clears[n], 0, sizeof( clears[n] ) );
		clears[n].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		n++;
	}
	if ( stencil && t->stencil ) {
		Com_Memset( &clears[n], 0, sizeof( clears[n] ) );
		clears[n].aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
		clears[n].clearValue.depthStencil.stencil = stencilValue;
		n++;
	}
	if ( !n ) {
		return;
	}
	UI_BeginPass( 0 );
	rect.rect.offset.x = x0;
	rect.rect.offset.y = y0;
	rect.rect.extent.width = x1 - x0;
	rect.rect.extent.height = y1 - y0;
	rect.baseArrayLayer = 0;
	rect.layerCount = 1;
	qvkCmdClearAttachments( ui.cmd, n, clears, 1, &rect );
}


// glBlitFramebuffer: GL's coordinates, the destination inside the scissor box (as the port's are)
static void UI_Blit( uiImage_t *src, int sx0, int sy0, int sx1, int sy1, uiImage_t *dst, int dx0, int dy0, int dx1, int dy1, VkFilter filter ) {
	VkImageBlit blit;

	if ( sx0 == sx1 || sy0 == sy1 || dx0 == dx1 || dy0 == dy1 ) {
		return;
	}
	UI_Barrier( src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, NULL, 0 );
	Com_Memset( &blit, 0, sizeof( blit ) );
	blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blit.srcSubresource.layerCount = 1;
	blit.srcOffsets[0].x = sx0; blit.srcOffsets[0].y = sy0;
	blit.srcOffsets[1].x = sx1; blit.srcOffsets[1].y = sy1; blit.srcOffsets[1].z = 1;
	blit.dstSubresource = blit.srcSubresource;
	blit.dstOffsets[0].x = dx0; blit.dstOffsets[0].y = dy0;
	blit.dstOffsets[1].x = dx1; blit.dstOffsets[1].y = dy1; blit.dstOffsets[1].z = 1;
	qvkCmdBlitImage( ui.cmd, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, filter );
}


/*
================
The pool's chunks: host-visible, mapped once
================
*/
static int UI_MakeChunk( int chunk, int numVerts, int numIndexes ) {
	uiChunk_t *c = &ui.chunks[chunk];

	c->mapped[0] = UI_MakeBuffer( numVerts * sizeof( ozUiVertex_t ), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, qtrue, &c->buffer[0], &c->memory[0] );
	c->mapped[1] = UI_MakeBuffer( numIndexes * sizeof( int ), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, qtrue, &c->buffer[1], &c->memory[1] );
	return 1;
}


// only when the GPU is done with it (UIPool_Frame after the fence, or a shutdown after the idle wait)
static void UI_FreeChunk( int chunk ) {
	uiChunk_t *c = &ui.chunks[chunk];

	UI_FreeBuffer( &c->buffer[0], &c->memory[0] );
	UI_FreeBuffer( &c->buffer[1], &c->memory[1] );
	Com_Memset( c, 0, sizeof( *c ) );
	if ( ui.boundChunk == chunk ) {
		ui.boundChunk = -1;
	}
}


static void UI_UploadChunk( int chunk, int firstVert, const ozUiVertex_t *verts, int numVerts, int firstIndex, const int *indexes, int numIndexes ) {
	const uiChunk_t *c = &ui.chunks[chunk];

	Com_Memcpy( c->mapped[0] + firstVert * sizeof( ozUiVertex_t ), verts, numVerts * sizeof( ozUiVertex_t ) );
	Com_Memcpy( c->mapped[1] + firstIndex * sizeof( int ), indexes, numIndexes * sizeof( int ) );
}


/*
================
UI_Init

Everything but the targets, on first use.
================
*/
static VkRenderPass UI_MakeRenderPass( qboolean depthStencil ) {
	VkAttachmentDescription attachments[2];
	VkAttachmentReference colorRef, dsRef;
	VkSubpassDescription subpass;
	VkRenderPassCreateInfo desc;
	VkRenderPass pass;

	Com_Memset( attachments, 0, sizeof( attachments ) );
	attachments[0].format = UI_COLOR_FORMAT;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachments[1].format = ui.depthFormat;
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	colorRef.attachment = 0;
	colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	dsRef.attachment = 1;
	dsRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	Com_Memset( &subpass, 0, sizeof( subpass ) );
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;
	subpass.pDepthStencilAttachment = depthStencil ? &dsRef : NULL;

	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	desc.attachmentCount = depthStencil ? 2 : 1;
	desc.pAttachments = attachments;
	desc.subpassCount = 1;
	desc.pSubpasses = &subpass;
	UI_CHECK( qvkCreateRenderPass( vk.device, &desc, NULL, &pass ) );
	return pass;
}


static VkSampler UI_MakeSampler( VkSamplerAddressMode mode ) {
	VkSamplerCreateInfo desc;
	VkSampler sampler;

	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	desc.magFilter = VK_FILTER_LINEAR;
	desc.minFilter = VK_FILTER_LINEAR;
	desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	desc.addressModeU = mode;
	desc.addressModeV = mode;
	desc.addressModeW = mode;
	desc.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	desc.maxLod = 0.25f;
	UI_CHECK( qvkCreateSampler( vk.device, &desc, NULL, &sampler ) );
	return sampler;
}


static qboolean UI_Init( void ) {
	const VkFormat depthFormats[2] = { VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT };
	const char *missing = NULL;
	VkPhysicalDeviceProperties props;
	VkFormatProperties formatProps;
	VkDescriptorPoolSize sizes[2];
	VkDescriptorPoolCreateInfo poolDesc;
	VkDescriptorSetLayoutBinding binding;
	VkDescriptorSetLayoutCreateInfo layoutDesc;
	VkDescriptorSetLayout setLayouts[3];
	VkPushConstantRange push;
	VkPipelineLayoutCreateInfo pipelineLayoutDesc;
	VkCommandPoolCreateInfo poolInfo;
	VkCommandBufferAllocateInfo cmdAlloc;
	VkFenceCreateInfo fenceDesc;
	VkDescriptorBufferInfo bufferInfo;
	VkWriteDescriptorSet write;
	int i;

	if ( ui.initialized ) {
		return qtrue;
	}
	if ( ui.failed || !vk.device ) {
		return qfalse;
	}
	ui.failed = qtrue;	// until it all works

#define UI_PROC( name ) q##name = (PFN_##name)vk_instance_proc( #name ); if ( !q##name ) missing = #name;
	UI_INSTANCE_PROCS
#undef UI_PROC
#define UI_PROC( name ) q##name = (PFN_##name)vk_device_proc( #name ); if ( !q##name ) missing = #name;
	UI_DEVICE_PROCS
#undef UI_PROC
	if ( missing ) {
		ri.Printf( PRINT_WARNING, "UI: no %s\n", missing );
		return qfalse;
	}

	qvkGetPhysicalDeviceMemoryProperties( vk.physical_device, &ui.memoryProps );
	qvkGetPhysicalDeviceProperties( vk.physical_device, &props );
	ui.uboAlignment = MAX( props.limits.minUniformBufferOffsetAlignment, 16 );
	ui.depthFormat = VK_FORMAT_UNDEFINED;
	for ( i = 0; i < 2 && !ui.depthFormat; i++ ) {
		qvkGetPhysicalDeviceFormatProperties( vk.physical_device, depthFormats[i], &formatProps );
		if ( formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT ) {
			ui.depthFormat = depthFormats[i];
		}
	}
	if ( !ui.depthFormat ) {
		ri.Printf( PRINT_WARNING, "UI: no depth-stencil format for clip masks\n" );
		return qfalse;
	}

	ui.passes[UK_LAYER] = UI_MakeRenderPass( qtrue );
	ui.passes[UK_POST] = UI_MakeRenderPass( qfalse );
	ui.samplerClamp = UI_MakeSampler( VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER );
	ui.samplerRepeat = UI_MakeSampler( VK_SAMPLER_ADDRESS_MODE_REPEAT );

	// descriptors: textures and targets, the gradients' ring
	sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	sizes[0].descriptorCount = UI_MAX_SETS;
	sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	sizes[1].descriptorCount = UI_FRAMES;
	Com_Memset( &poolDesc, 0, sizeof( poolDesc ) );
	poolDesc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolDesc.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	poolDesc.maxSets = UI_MAX_SETS + UI_FRAMES;
	poolDesc.poolSizeCount = 2;
	poolDesc.pPoolSizes = sizes;
	UI_CHECK( qvkCreateDescriptorPool( vk.device, &poolDesc, NULL, &ui.descriptorPool ) );

	Com_Memset( &binding, 0, sizeof( binding ) );
	binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	Com_Memset( &layoutDesc, 0, sizeof( layoutDesc ) );
	layoutDesc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutDesc.bindingCount = 1;
	layoutDesc.pBindings = &binding;
	UI_CHECK( qvkCreateDescriptorSetLayout( vk.device, &layoutDesc, NULL, &ui.uboLayout ) );

	// set 0 the texture, 1 the mask (Quake3e's sampler layout, so its images' sets bind as they are), 2 the gradient
	setLayouts[0] = vk.set_layout_sampler;
	setLayouts[1] = vk.set_layout_sampler;
	setLayouts[2] = ui.uboLayout;
	push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	push.offset = 0;
	push.size = 128;
	Com_Memset( &pipelineLayoutDesc, 0, sizeof( pipelineLayoutDesc ) );
	pipelineLayoutDesc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutDesc.setLayoutCount = 3;
	pipelineLayoutDesc.pSetLayouts = setLayouts;
	pipelineLayoutDesc.pushConstantRangeCount = 1;
	pipelineLayoutDesc.pPushConstantRanges = &push;
	UI_CHECK( qvkCreatePipelineLayout( vk.device, &pipelineLayoutDesc, NULL, &ui.pipelineLayout ) );

	// the command buffers, their fences (made signaled: the first wait passes) and gradient rings
	Com_Memset( &poolInfo, 0, sizeof( poolInfo ) );
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = vk.queue_family_index;
	UI_CHECK( qvkCreateCommandPool( vk.device, &poolInfo, NULL, &ui.commandPool ) );
	Com_Memset( &cmdAlloc, 0, sizeof( cmdAlloc ) );
	cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmdAlloc.commandPool = ui.commandPool;
	cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmdAlloc.commandBufferCount = UI_FRAMES;
	UI_CHECK( qvkAllocateCommandBuffers( vk.device, &cmdAlloc, ui.cmds ) );
	Com_Memset( &fenceDesc, 0, sizeof( fenceDesc ) );
	fenceDesc.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceDesc.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	for ( i = 0; i < UI_FRAMES; i++ ) {
		uiRing_t *r = &ui.rings[i];
		UI_CHECK( qvkCreateFence( vk.device, &fenceDesc, NULL, &ui.fences[i] ) );
		r->mapped = UI_MakeBuffer( UI_GRADIENT_RING, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, qfalse, &r->buffer, &r->memory );
		r->descriptor = UI_AllocSet( ui.uboLayout );
		bufferInfo.buffer = r->buffer;
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof( uiGradientBlock_t );
		Com_Memset( &write, 0, sizeof( write ) );
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = r->descriptor;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		write.pBufferInfo = &bufferInfo;
		qvkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
	}

	ui.pool.makeChunk = UI_MakeChunk;
	ui.pool.freeChunk = UI_FreeChunk;
	ui.pool.upload = UI_UploadChunk;
	ui.boundChunk = -1;
	ui.program = -1;

	ui.failed = qfalse;
	ui.initialized = qtrue;
	ri.Printf( PRINT_ALL, "UI renderer: Vulkan, %i programs, %s clip masks\n", UP_COUNT,
		ui.depthFormat == VK_FORMAT_D24_UNORM_S8_UINT ? "D24S8" : "D32S8" );
	return qtrue;
}


static void UI_SubmitGeometry( const uiPoolGeometry_t *g, float x, float y ) {
	uiPushGeometry_t push;

	Com_Memcpy( push.transform, ui.transform, sizeof( push.transform ) );
	push.translate[0] = x;
	push.translate[1] = y;
	push.straight = ui.u.straight ? 1.0f : 0.0f;
	UI_Draw( &push, sizeof( push ), g, VK_NULL_HANDLE, 0 );
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
static uiTexture_t *UI_AddTexture( int *handle ) {
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
	Com_Memset( t, 0, sizeof( *t ) );
	t->used = qtrue;
	*handle = h;
	return t;
}


// a texture's pixels into its image, recorded in the frame's commands
static void UI_Upload( uiTexture_t *t ) {
	VkBufferImageCopy copy;

	UI_Barrier( &t->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, NULL, 0, NULL, 0 );
	Com_Memset( &copy, 0, sizeof( copy ) );
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1;
	copy.imageExtent.width = t->img.width;
	copy.imageExtent.height = t->img.height;
	copy.imageExtent.depth = 1;
	qvkCmdCopyBufferToImage( ui.cmd, t->staging, t->img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy );
	UI_Barrier( &t->img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, NULL, 0, NULL, 0 );
	UI_Discard( NULL, t->staging, t->stagingMemory );
	t->staging = VK_NULL_HANDLE;
	t->stagingMemory = VK_NULL_HANDLE;
}


/*
================
UI_LoadTexture

Through the engine's image loader (tga, png, jpg, dds, ... from the pk3s), kept in the renderer's
image list, so a page and the game share one copy, drawn with the image's own descriptor set.
No mipmaps, picmip or overbright scaling, and no rounding to a power of two.
================
*/
static int UI_LoadTexture( const char *name, int *width, int *height ) {
	image_t *image;
	uiTexture_t *t;
	int h;

	if ( !tr.registered || !UI_Init() ) {
		return 0;
	}
	image = R_FindImageFile( name, IMGFLAG_NO_COMPRESSION | IMGFLAG_NOLIGHTSCALE | IMGFLAG_NOSCALE );
	if ( !image || !image->descriptor ) {
		return 0;
	}
	*width = image->width;
	*height = image->height;
	t = UI_AddTexture( &h );
	t->descriptor = image->descriptor;
	t->straight = qtrue;
	return h;
}


// premultiplied RGBA8, copied in at the next of the UI's command buffers (now, inside a frame)
static int UI_CreateTexture( const byte *rgba, int width, int height ) {
	uiTexture_t *t;
	int h;

	if ( !tr.registered || !UI_Init() || width <= 0 || height <= 0 ) {
		return 0;
	}
	t = UI_AddTexture( &h );
	UI_MakeImage( &t->img, width, height, UI_COLOR_FORMAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, ui.samplerRepeat );
	t->descriptor = t->img.descriptor;
	t->owned = qtrue;
	Com_Memcpy( UI_MakeBuffer( width * height * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, qfalse, &t->staging, &t->stagingMemory ), rgba, width * height * 4 );
	if ( ui.inFrame ) {
		UI_Upload( t );
	} else {
		ui.pendingUploads++;
	}
	return h;
}


static void UI_ReleaseTexture( int handle ) {
	uiTexture_t *t = UI_Texture( handle );
	int i;

	if ( !t ) {
		return;
	}
	if ( t->owned ) {
		for ( i = 0; i < 2; i++ ) {
			if ( ui.units[i] == t->descriptor ) {
				UI_BindSet( i, VK_NULL_HANDLE );
			}
		}
		UI_Discard( &t->img, t->staging, t->stagingMemory );
	}
	Com_Memset( t, 0, sizeof( *t ) );
	t->nextFree = ui.firstFreeTexture;
	ui.firstFreeTexture = handle;
}


/*
================
Targets: the layers, the postprocess buffers and the blend mask
================
*/
static void UI_MakeTarget( uiTarget_t *t, qboolean stencil ) {
	VkFramebufferCreateInfo desc;
	VkImageView views[2];

	if ( stencil && !ui.depthStencil.image ) {
		UI_MakeImage( &ui.depthStencil, ui.width, ui.height, ui.depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_NULL_HANDLE );
	}
	UI_MakeImage( &t->img, ui.width, ui.height, UI_COLOR_FORMAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
		| VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, ui.samplerClamp );	// border: transparent black
	t->stencil = stencil;

	views[0] = t->img.view;
	views[1] = ui.depthStencil.view;
	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	desc.renderPass = ui.passes[stencil ? UK_LAYER : UK_POST];
	desc.attachmentCount = stencil ? 2 : 1;
	desc.pAttachments = views;
	desc.width = ui.width;
	desc.height = ui.height;
	desc.layers = 1;
	UI_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL, &t->framebuffer ) );
}


static void UI_FreeTarget( uiTarget_t *t ) {
	if ( t->framebuffer ) qvkDestroyFramebuffer( vk.device, t->framebuffer, NULL );
	UI_FreeImage( &t->img );
	Com_Memset( t, 0, sizeof( *t ) );
}


// with the GPU idle
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
	UI_FreeImage( &ui.depthStencil );
}


static uiTarget_t *UI_Post( int i ) {
	if ( !ui.post[i].framebuffer ) {
		UI_MakeTarget( &ui.post[i], qfalse );
	}
	return &ui.post[i];
}


// GL binds by name, so what was bound stays bound: the pointers to either follow the swap
static void UI_SwapPost( void ) {
	const uiTarget_t t = ui.post[0];
	int i;

#define UI_SWAPPED( p, a, b ) ( (p) == (a) ? (b) : (p) == (b) ? (a) : (p) )
	ui.post[0] = ui.post[1];
	ui.post[1] = t;
	ui.target = UI_SWAPPED( ui.target, &ui.post[0], &ui.post[1] );
	ui.passTarget = UI_SWAPPED( ui.passTarget, &ui.post[0], &ui.post[1] );
	for ( i = 0; i < 2; i++ ) {
		ui.unitImages[i] = UI_SWAPPED( ui.unitImages[i], &ui.post[0].img, &ui.post[1].img );
	}
#undef UI_SWAPPED
}


/*
================
Scissor

ui.scissor is in RmlUi's coordinates (from the top left); GL's come from the bottom left, and so do
a layer's rows here. "No scissor" is the whole target.
================
*/
static void UI_SetScissorRect( uiRect_t r ) {
	if ( r.valid ) {
		const int x = Com_Clamp( 0, ui.width, r.x0 );
		const int y = Com_Clamp( 0, ui.height, ui.height - r.y1 );
		UI_ScissorBox( x, y, r.x1 - r.x0, r.y1 - r.y0 );
	} else {
		UI_ScissorBox( 0, 0, ui.width, ui.height );
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
	ui.u.uvOffset[0] = uvOffsetX;
	ui.u.uvOffset[1] = uvOffsetY;
	ui.u.uvScale[0] = uvScaleX;
	ui.u.uvScale[1] = uvScaleY;

	switch ( ui.program ) {
	case UP_COLOR_MATRIX: {
		uiPushMatrix_t push;
		Com_Memcpy( push.uvOffset, ui.u.uvOffset, sizeof( float ) * 4 );
		Com_Memcpy( push.matrix, ui.u.matrix, sizeof( push.matrix ) );
		UI_Draw( &push, sizeof( push ), NULL, VK_NULL_HANDLE, 0 );
		break;
	}
	case UP_DROP_SHADOW: {
		uiPushShadow_t push;
		Com_Memcpy( push.uvOffset, ui.u.uvOffset, sizeof( float ) * 4 );
		Com_Memcpy( push.texCoordMin, ui.u.texCoordMin, sizeof( float ) * 4 );
		Com_Memcpy( push.color, ui.u.color, sizeof( push.color ) );
		UI_Draw( &push, sizeof( push ), NULL, VK_NULL_HANDLE, 0 );
		break;
	}
	case UP_BLUR: {
		uiPushBlur_t push;
		Com_Memcpy( push.uvOffset, ui.u.uvOffset, sizeof( float ) * 4 );
		Com_Memcpy( push.texCoordMin, ui.u.texCoordMin, sizeof( float ) * 4 );
		Com_Memcpy( push.texelOffset, ui.u.texelOffset, sizeof( push.texelOffset ) );
		push.pad[0] = push.pad[1] = 0.0f;
		Com_Memcpy( push.weights, ui.u.weights, sizeof( push.weights ) );
		UI_Draw( &push, sizeof( push ), NULL, VK_NULL_HANDLE, 0 );
		break;
	}
	default: {	// passthrough, blend mask
		uiPushPass_t push;
		Com_Memcpy( push.uvOffset, ui.u.uvOffset, sizeof( float ) * 4 );
		push.opacity = ui.u.opacity;
		UI_Draw( &push, sizeof( push ), NULL, VK_NULL_HANDLE, 0 );
		break;
	}
	}
}


/*
================
Frames
================
*/
static void UI_BeginFrame( int width, int height ) {
	VkCommandBufferBeginInfo begin;
	int i;

	if ( !tr.registered || !UI_Init() || width <= 0 || height <= 0 ) {
		return;
	}
	if ( ui.inFrame ) {
		// the last frame never ended (an error in between): its commands never ran
		vk_wait_idle();
		for ( i = 0; i < ui.numLayers; i++ ) ui.layers[i].img.layout = VK_IMAGE_LAYOUT_UNDEFINED;
		for ( i = 0; i < 3; i++ ) ui.post[i].img.layout = VK_IMAGE_LAYOUT_UNDEFINED;
		ui.mask.img.layout = VK_IMAGE_LAYOUT_UNDEFINED;
		ui.depthStencil.layout = VK_IMAGE_LAYOUT_UNDEFINED;
		ui.passTarget = NULL;
	}

	// this frame's command buffer, once the GPU is done with its last use; what was released
	// UI_FRAMES frames ago goes with it
	ui.slot = ( ui.pool.frame + 1 ) % UI_FRAMES;
	UI_CHECK( qvkWaitForFences( vk.device, 1, &ui.fences[ui.slot], VK_TRUE, 5 * 1000000000ULL ) );
	UIPool_Frame( &ui.pool );
	UI_FreeGarbage( ui.slot );
	ui.ringUsed = 0;

	if ( width != ui.width || height != ui.height ) {
		vk_wait_idle();
		UI_FreeTargets();
		ui.width = width;
		ui.height = height;
	}

	ui.cmd = ui.cmds[ui.slot];
	UI_CHECK( qvkResetCommandBuffer( ui.cmd, 0 ) );
	Com_Memset( &begin, 0, sizeof( begin ) );
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	UI_CHECK( qvkBeginCommandBuffer( ui.cmd, &begin ) );
	ui.passTarget = NULL;
	ui.boundPipeline = VK_NULL_HANDLE;
	ui.boundChunk = -1;

	// textures made since the last frame
	for ( i = 0; i < ui.maxTextures && ui.pendingUploads; i++ ) {
		if ( ui.textures[i].used && ui.textures[i].staging ) {
			UI_Upload( &ui.textures[i] );
			ui.pendingUploads--;
		}
	}
	ui.pendingUploads = 0;

	ui.inFrame = qtrue;
	ui.program = -1;
	ui.layerTop = 0;
	UI_BindSet( 0, VK_NULL_HANDLE );
	UI_BindSet( 1, VK_NULL_HANDLE );

	UI_Viewport( 0, 0, ui.width, ui.height );
	ui.scissor.valid = qfalse;
	UI_SetScissorRect( ui.scissor );

	ui.blend = qtrue;
	ui.colorWrite = qtrue;
	ui.stencilTest = qtrue;
	ui.stencilEqual = qfalse;
	ui.stencilOp = US_OFF;
	ui.stencilRef = 1;
	ui.u.opacity = 1.0f;

	UI_Ortho( 0, ui.width, ui.height, 0, -10000, 10000, ui.projection );
	Com_Memcpy( ui.transform, ui.projection, sizeof( ui.transform ) );

	// the base layer
	if ( !ui.numLayers ) {
		UI_MakeTarget( &ui.layers[0], qtrue );
		ui.numLayers = 1;
	}
	UI_BindTarget( &ui.layers[0] );
	ui.layerTop = 1;
	UI_Clear( qtrue, qtrue, 0 );
}


typedef struct {
	int		commandId;	// RC_OZ_UI
} uiCompositeCommand_t;


static void UI_EndFrame( void ) {
	uiCompositeCommand_t *cmd;
	VkSubmitInfo submit;

	if ( !ui.inFrame ) {
		return;
	}
	ui.inFrame = qfalse;

	// the base layer, readable for the composite in the frame's commands, submitted after these
	UI_Barrier( &ui.layers[0].img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, NULL, 0, NULL, 0 );
	UI_CHECK( qvkEndCommandBuffer( ui.cmd ) );
	UI_CHECK( qvkResetFences( vk.device, 1, &ui.fences[ui.slot] ) );
	Com_Memset( &submit, 0, sizeof( submit ) );
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &ui.cmd;
	UI_CHECK( qvkQueueSubmit( vk.queue, 1, &submit, ui.fences[ui.slot] ) );
	ui.cmd = VK_NULL_HANDLE;
	ui.program = -1;
	ui.layerTop = 0;

	// the composite: in the frame's render pass, after the 2D queued so far
	cmd = R_GetCommandBuffer( sizeof( *cmd ) );
	if ( cmd ) {
		cmd->commandId = RC_OZ_UI;
	}
}


/*
================
RB_UI_Composite

RC_OZ_UI: the base layer onto the frame with premultiplied blending, flipped from GL's rows to
the frame's. The UI is 2D: the world blooms first, as at RB_StretchPic. Then the state the frame's
draws keep track of is handed back: the pipeline, viewport, scissor, descriptor sets and push
constants are set again by the next draw.
================
*/
const void *RB_UI_Composite( const void *data ) {
	const uiCompositeCommand_t *cmd = (const uiCompositeCommand_t *)data;
	const int pass = vk.renderPassIndex;
	uiPushPass_t push;
	VkViewport viewport;
	VkRect2D scissor;
	uint32_t i, start, end;

	RB_EndSurface();
	if ( r_bloom->integer ) {
		vk_bloom();
	}
	if ( !ui.initialized || !ui.layers[0].img.descriptor || ( pass != RENDER_PASS_MAIN && pass != RENDER_PASS_POST_BLOOM ) ) {
		return (const void *)( cmd + 1 );
	}
	if ( !ui.composite[pass] ) {
		ui.composite[pass] = UI_MakePipeline( UP_PASSTHROUGH, pass == RENDER_PASS_MAIN ? vk.render_pass.main : vk.render_pass.post_bloom,
			(VkSampleCountFlagBits)vk_main_samples(), qtrue, qtrue, US_OFF );
	}

	qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ui.composite[pass] );
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = vk.renderWidth;
	viewport.height = vk.renderHeight;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	qvkCmdSetViewport( vk.cmd->command_buffer, 0, 1, &viewport );
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = vk.renderWidth;
	scissor.extent.height = vk.renderHeight;
	qvkCmdSetScissor( vk.cmd->command_buffer, 0, 1, &scissor );
	qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ui.pipelineLayout, 0, 1, &ui.layers[0].img.descriptor, 0, NULL );
	push.uvOffset[0] = 0.0f;
	push.uvOffset[1] = 1.0f;
	push.uvScale[0] = 1.0f;
	push.uvScale[1] = -1.0f;
	push.opacity = 1.0f;
	qvkCmdPushConstants( vk.cmd->command_buffer, ui.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
	qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );

	vk.cmd->last_pipeline = VK_NULL_HANDLE;
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;
	Com_Memset( &vk.cmd->scissor_rect, 0, sizeof( vk.cmd->scissor_rect ) );
	start = ~0U;
	end = 0;
	for ( i = 0; i < VK_DESC_COUNT; i++ ) {
		if ( vk.cmd->descriptor_set.current[i] != VK_NULL_HANDLE ) {
			start = MIN( start, i );
			end = MAX( end, i );
		}
	}
	vk.cmd->descriptor_set.start = start;
	vk.cmd->descriptor_set.end = end;
	backEnd.projection2D = qfalse;	// RB_SetGL2D pushes the 2D transform again

	return (const void *)( cmd + 1 );
}


/*
================
Geometry
================
*/
static void UI_RenderGeometry( int geometry, float x, float y, int texture ) {
	const uiPoolGeometry_t *g = UIPool_Get( &ui.pool, geometry );
	const uiTexture_t *t = UI_Texture( texture );

	if ( !ui.inFrame || !g ) {
		return;
	}
	if ( t && t->descriptor ) {
		UI_UseProgram( UP_TEXTURE );
		ui.u.straight = t->straight;
		UI_BindSet( 0, t->descriptor );
	} else {
		UI_UseProgram( UP_COLOR );
	}
	UI_SubmitGeometry( g, x, y );
}


static void UI_RenderGradient( const ozUiGradient_t *gradient, int geometry, float x, float y ) {
	const uiPoolGeometry_t *g = UIPool_Get( &ui.pool, geometry );
	const int numStops = Com_Clamp( 1, OZ_UI_MAX_STOPS, gradient->numStops );
	const uint32_t size = PAD( sizeof( uiGradientBlock_t ), ui.uboAlignment );
	uiRing_t *ring = &ui.rings[ui.slot];
	uiGradientBlock_t *block;
	uiPushGeometry_t push;
	uint32_t offset;

	if ( !ui.inFrame || !g ) {
		return;
	}
	if ( ui.ringUsed + size > UI_GRADIENT_RING ) {
		ri.Printf( PRINT_DEVELOPER, "UI: more than %i gradients in a frame\n", UI_GRADIENT_RING / size );
		return;
	}
	offset = ui.ringUsed;
	ui.ringUsed += size;
	block = (uiGradientBlock_t *)( ring->mapped + offset );
	block->func = gradient->func;
	block->numStops = numStops;
	block->p[0] = gradient->p[0];
	block->p[1] = gradient->p[1];
	block->v[0] = gradient->v[0];
	block->v[1] = gradient->v[1];
	Com_Memcpy( block->stopColors, gradient->stopColors, sizeof( float ) * 4 * numStops );
	Com_Memcpy( block->stopPositions, gradient->stopPositions, sizeof( float ) * numStops );

	UI_UseProgram( UP_GRADIENT );
	Com_Memcpy( push.transform, ui.transform, sizeof( push.transform ) );
	push.translate[0] = x;
	push.translate[1] = y;
	push.straight = 0.0f;
	UI_Draw( &push, sizeof( push ), g, ring->descriptor, offset );
}


static void UI_SetTransform( const float *matrix ) {
	if ( matrix ) {
		UI_MatMul( ui.projection, matrix, ui.transform );
	} else {
		Com_Memcpy( ui.transform, ui.projection, sizeof( ui.transform ) );
	}
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
	ui.stencilTest = enable ? qtrue : qfalse;
}


static void UI_RenderToClipMask( int op, int geometry, float x, float y ) {
	int writeValue = 1, testValue = 1;

	if ( !ui.inFrame ) {
		return;
	}
	switch ( op ) {
	case OZ_UI_CLIP_SET:
		UI_Clear( qfalse, qtrue, 0 );
		ui.stencilOp = US_REPLACE;
		break;
	case OZ_UI_CLIP_SET_INVERSE:
		UI_Clear( qfalse, qtrue, 1 );
		ui.stencilOp = US_REPLACE;
		writeValue = 0;
		break;
	case OZ_UI_CLIP_INTERSECT:
		ui.stencilOp = US_INCR;
		testValue = ui.stencilRef + 1;
		break;
	}

	ui.colorWrite = qfalse;
	ui.stencilEqual = qfalse;
	ui.stencilRef = writeValue;
	if ( ui.stencilTest ) {	// GL writes no stencil with the test off
		UI_RenderGeometry( geometry, x, y, 0 );
	}

	ui.colorWrite = qtrue;
	ui.stencilOp = US_OFF;
	ui.stencilEqual = qtrue;
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
	ui.u.texCoordMin[0] = ( regionGL.x0 + 0.5f ) / width;
	ui.u.texCoordMin[1] = ( regionGL.y0 + 0.5f ) / height;
	ui.u.texCoordMax[0] = ( regionGL.x1 - 0.5f ) / width;
	ui.u.texCoordMax[1] = ( regionGL.y1 - 0.5f ) / height;
}


// the scissor from GL's coordinates
static void UI_SetScissorGL( uiRect_t r ) {
	UI_SetScissorRect( UI_Flipped( r ) );
}


static void UI_Blur( float sigma, uiTarget_t *sourceDest, uiTarget_t *temp, uiRect_t windowGL ) {
	const uiRect_t originalScissor = ui.scissor;
	uiRect_t scissor = windowGL;
	int passLevel, i, srcMinX, srcMinY, srcMaxX, srcMaxY, tgtMinX, tgtMinY, tgtMaxX, tgtMaxY;
	float uvScaleX, uvScaleY;

	UI_SigmaToParameters( sigma, &passLevel, &sigma );

	UI_UseProgram( UP_PASSTHROUGH );
	UI_SetScissorGL( scissor );

	// downscale by halves with bilinear filtering
	UI_Viewport( 0, 0, sourceDest->img.width / 2, sourceDest->img.height / 2 );
	uvScaleX = ( sourceDest->img.width % 2 == 1 ) ? ( 1.0f - 1.0f / sourceDest->img.width ) : 1.0f;
	uvScaleY = ( sourceDest->img.height % 2 == 1 ) ? ( 1.0f - 1.0f / sourceDest->img.height ) : 1.0f;
	for ( i = 0; i < passLevel; i++ ) {
		const qboolean fromSource = ( i % 2 == 0 );
		scissor.x0 = ( scissor.x0 + 1 ) / 2;
		scissor.y0 = ( scissor.y0 + 1 ) / 2;
		scissor.x1 = MAX( scissor.x1 / 2, scissor.x0 );
		scissor.y1 = MAX( scissor.y1 / 2, scissor.y0 );
		UI_BindImage( 0, fromSource ? &sourceDest->img : &temp->img );
		UI_BindTarget( fromSource ? temp : sourceDest );
		UI_SetScissorGL( scissor );
		UI_DrawQuad( 0, 0, uvScaleX, uvScaleY );
	}
	UI_Viewport( 0, 0, sourceDest->img.width, sourceDest->img.height );

	// the image must end up in temp
	if ( passLevel % 2 == 0 ) {
		UI_BindImage( 0, &sourceDest->img );
		UI_BindTarget( temp );
		UI_DrawQuad( 0, 0, 1, 1 );
	}

	UI_UseProgram( UP_BLUR );
	UI_BlurWeights( sigma, ui.u.weights );
	UI_SetTexCoordLimits( scissor, sourceDest->img.width, sourceDest->img.height );

	// vertical
	UI_BindImage( 0, &temp->img );
	UI_BindTarget( sourceDest );
	ui.u.texelOffset[0] = 0.0f;
	ui.u.texelOffset[1] = 1.0f / temp->img.height;
	UI_DrawQuad( 0, 0, 1, 1 );

	// horizontal, with a 1-pixel transparent border around the region against bleeding when upscaling
	UI_BindImage( 0, &sourceDest->img );
	UI_BindTarget( temp );
	{
		uiRect_t padded = scissor;
		padded.x0--; padded.y0--; padded.x1++; padded.y1++;
		UI_SetScissorGL( padded );
		UI_Clear( qtrue, qfalse, 0 );
	}
	UI_SetScissorGL( scissor );
	ui.u.texelOffset[0] = 1.0f / sourceDest->img.width;
	ui.u.texelOffset[1] = 0.0f;
	UI_DrawQuad( 0, 0, 1, 1 );

	// upscale into the region, then again by an exact power of two for stable placement
	UI_SetScissorGL( windowGL );
	srcMinX = scissor.x0; srcMinY = scissor.y0; srcMaxX = scissor.x1; srcMaxY = scissor.y1;
	UI_Blit( &temp->img, srcMinX, srcMinY, srcMaxX, srcMaxY, &sourceDest->img, windowGL.x0, windowGL.y0, windowGL.x1, windowGL.y1, VK_FILTER_LINEAR );
	tgtMinX = srcMinX << passLevel; tgtMinY = srcMinY << passLevel;
	tgtMaxX = srcMaxX << passLevel; tgtMaxY = srcMaxY << passLevel;
	if ( tgtMinX != windowGL.x0 || tgtMinY != windowGL.y0 || tgtMaxX != windowGL.x1 || tgtMaxY != windowGL.y1 ) {
		UI_Blit( &temp->img, srcMinX, srcMinY, srcMaxX, srcMaxY, &sourceDest->img, tgtMinX, tgtMinY, tgtMaxX, tgtMaxY, VK_FILTER_LINEAR );
	}

	UI_SetScissorRect( originalScissor );
}


/*
================
Layers and filters
================
*/

// the layer into the primary postprocess buffer, within the scissor box (glBlitFramebuffer's is)
static void UI_BlitLayerToPost( int layer ) {
	uiImage_t *source = &ui.layers[layer].img;
	uiImage_t *dest = &UI_Post( 0 )->img;
	VkImageCopy copy;
	int x0, y0, x1, y1;

	x0 = Com_Clamp( 0, source->width, ui.scissorBox[0] );
	y0 = Com_Clamp( 0, source->height, ui.scissorBox[1] );
	x1 = Com_Clamp( 0, source->width, ui.scissorBox[0] + ui.scissorBox[2] );
	y1 = Com_Clamp( 0, source->height, ui.scissorBox[1] + ui.scissorBox[3] );
	if ( x1 <= x0 || y1 <= y0 ) {
		return;
	}
	UI_Barrier( source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dest, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, NULL, 0 );
	Com_Memset( &copy, 0, sizeof( copy ) );
	copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.srcSubresource.layerCount = 1;
	copy.dstSubresource = copy.srcSubresource;
	copy.srcOffset.x = copy.dstOffset.x = x0;
	copy.srcOffset.y = copy.dstOffset.y = y0;
	copy.extent.width = x1 - x0;
	copy.extent.height = y1 - y0;
	copy.extent.depth = 1;
	qvkCmdCopyImage( ui.cmd, source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dest->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy );
}


static void UI_RenderFilters( const ozUiFilter_t *filters, int numFilters ) {
	int i;

	for ( i = 0; i < numFilters; i++ ) {
		const ozUiFilter_t *f = &filters[i];

		switch ( f->type ) {
		case OZ_UI_FILTER_OPACITY:
			ui.blend = qfalse;
			UI_UseProgram( UP_PASSTHROUGH );
			ui.u.opacity = f->value;
			UI_BindImage( 0, &UI_Post( 0 )->img );
			UI_BindTarget( UI_Post( 1 ) );
			UI_DrawQuad( 0, 0, 1, 1 );
			ui.u.opacity = 1.0f;
			UI_SwapPost();
			ui.blend = qtrue;
			break;

		case OZ_UI_FILTER_BLUR:
			ui.blend = qfalse;
			UI_Blur( f->value, UI_Post( 0 ), UI_Post( 1 ), UI_ScissorGL() );
			ui.blend = qtrue;
			break;

		case OZ_UI_FILTER_DROP_SHADOW: {
			const uiRect_t windowGL = UI_ScissorGL();
			uiTarget_t *primary = UI_Post( 0 ), *secondary = UI_Post( 1 );

			UI_UseProgram( UP_DROP_SHADOW );
			ui.blend = qfalse;
			Com_Memcpy( ui.u.color, f->color, sizeof( ui.u.color ) );
			UI_BindImage( 0, &primary->img );
			UI_BindTarget( secondary );
			UI_SetTexCoordLimits( windowGL, primary->img.width, primary->img.height );
			UI_DrawQuad( -f->offset[0] / ui.width, f->offset[1] / ui.height, 1, 1 );
			if ( f->value >= 0.5f ) {
				UI_Blur( f->value, secondary, UI_Post( 2 ), windowGL );
			}
			UI_UseProgram( UP_PASSTHROUGH );
			UI_BindImage( 0, &primary->img );
			UI_BindTarget( secondary );
			ui.blend = qtrue;
			UI_DrawQuad( 0, 0, 1, 1 );
			UI_SwapPost();
			break;
		}

		case OZ_UI_FILTER_COLOR_MATRIX:
			UI_UseProgram( UP_COLOR_MATRIX );
			ui.blend = qfalse;
			Com_Memcpy( ui.u.matrix, f->matrix, sizeof( ui.u.matrix ) );
			UI_BindImage( 0, &UI_Post( 0 )->img );
			UI_BindTarget( UI_Post( 1 ) );
			UI_DrawQuad( 0, 0, 1, 1 );
			UI_SwapPost();
			ui.blend = qtrue;
			break;

		case OZ_UI_FILTER_MASK_IMAGE:
			if ( !ui.mask.framebuffer ) {
				break;
			}
			UI_UseProgram( UP_BLEND_MASK );
			ui.blend = qfalse;
			UI_BindImage( 0, &UI_Post( 0 )->img );
			UI_BindImage( 1, &ui.mask.img );
			UI_BindTarget( UI_Post( 1 ) );
			UI_DrawQuad( 0, 0, 1, 1 );
			UI_BindSet( 1, VK_NULL_HANDLE );
			UI_SwapPost();
			ui.blend = qtrue;
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
	UI_Clear( qtrue, qfalse, 0 );
	return ui.layerTop++;
}


static void UI_CompositeLayers( int source, int destination, int replace, const ozUiFilter_t *filters, int numFilters ) {
	if ( !ui.inFrame || source < 0 || source >= ui.layerTop || destination < 0 || destination >= ui.layerTop ) {
		return;
	}

	UI_BlitLayerToPost( source );
	UI_RenderFilters( filters, numFilters );

	UI_BindTarget( &ui.layers[destination] );
	UI_BindImage( 0, &UI_Post( 0 )->img );
	UI_UseProgram( UP_PASSTHROUGH );
	ui.blend = replace ? qfalse : qtrue;
	UI_DrawQuad( 0, 0, 1, 1 );
	ui.blend = qtrue;

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


// the scissor region of the top layer, flipped from GL's rows to a texture's, as a new texture
static int UI_SaveLayerAsTexture( void ) {
	const uiRect_t bounds = ui.scissor;
	uiImage_t *source = &ui.layers[ui.layerTop - 1].img;
	uiTexture_t *t;
	int w, h, handle;

	if ( !ui.inFrame || !bounds.valid || bounds.x1 <= bounds.x0 || bounds.y1 <= bounds.y0 ) {
		return 0;
	}
	w = bounds.x1 - bounds.x0;
	h = bounds.y1 - bounds.y0;
	t = UI_AddTexture( &handle );
	UI_MakeImage( &t->img, w, h, UI_COLOR_FORMAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, ui.samplerRepeat );
	t->descriptor = t->img.descriptor;
	t->owned = qtrue;

	UI_Blit( source, bounds.x0, source->height - bounds.y1, bounds.x1, source->height - bounds.y0, &t->img, 0, h, w, 0, VK_FILTER_NEAREST );
	UI_Barrier( &t->img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, NULL, 0, NULL, 0 );
	return handle;
}


static void UI_SaveLayerAsMask( void ) {
	if ( !ui.inFrame ) {
		return;
	}
	UI_BlitLayerToPost( ui.layerTop - 1 );
	if ( !ui.mask.framebuffer ) {
		UI_MakeTarget( &ui.mask, qfalse );
	}
	UI_BindTarget( &ui.mask );
	UI_BindImage( 0, &UI_Post( 0 )->img );
	UI_UseProgram( UP_PASSTHROUGH );
	ui.blend = qfalse;
	UI_DrawQuad( 0, 0, 1, 1 );
	ui.blend = qtrue;
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

refexport_t.GetUIRender.
================
*/
const ozUiRender_t *R_UI_GetRender( void ) {
	if ( !tr.registered || !vk.active || ui.failed ) {
		return NULL;
	}
	return &uiRender;
}


/*
================
R_UI_Shutdown

From RE_Shutdown, while the device still exists: once the GPU is idle, every object made here goes.
The client releases its handles before this; any left are dropped with the rest.
================
*/
void R_UI_Shutdown( void ) {
	int i, j, k, l;

	if ( ui.initialized ) {
		qvkDeviceWaitIdle( vk.device );
		UIPool_Shutdown( &ui.pool );
		for ( i = 0; i < ui.maxTextures; i++ ) {
			if ( ui.textures[i].used && ui.textures[i].owned ) {
				UI_FreeImage( &ui.textures[i].img );
				UI_FreeBuffer( &ui.textures[i].staging, &ui.textures[i].stagingMemory );
			}
		}
		for ( i = 0; i < UI_FRAMES; i++ ) {
			UI_FreeGarbage( i );
			UI_FreeBuffer( &ui.rings[i].buffer, &ui.rings[i].memory );
			qvkDestroyFence( vk.device, ui.fences[i], NULL );
		}
		UI_FreeTargets();
		for ( i = 0; i < UP_COUNT; i++ ) for ( j = 0; j < UK_COUNT; j++ ) for ( k = 0; k < 2; k++ ) for ( l = 0; l < US_COUNT; l++ ) {
			if ( ui.pipelines[i][j][k][l] ) {
				qvkDestroyPipeline( vk.device, ui.pipelines[i][j][k][l], NULL );
			}
		}
		for ( i = 0; i < RENDER_PASS_COUNT; i++ ) {
			if ( ui.composite[i] ) {
				qvkDestroyPipeline( vk.device, ui.composite[i], NULL );
			}
		}
		for ( i = 0; i < UM_COUNT; i++ ) {
			if ( ui.modules[i] ) {
				qvkDestroyShaderModule( vk.device, ui.modules[i], NULL );
			}
		}
		qvkDestroyCommandPool( vk.device, ui.commandPool, NULL );
		qvkDestroyPipelineLayout( vk.device, ui.pipelineLayout, NULL );
		qvkDestroyDescriptorSetLayout( vk.device, ui.uboLayout, NULL );
		qvkDestroyDescriptorPool( vk.device, ui.descriptorPool, NULL );	// and its sets
		qvkDestroySampler( vk.device, ui.samplerClamp, NULL );
		qvkDestroySampler( vk.device, ui.samplerRepeat, NULL );
		for ( i = 0; i < UK_COUNT; i++ ) {
			qvkDestroyRenderPass( vk.device, ui.passes[i], NULL );
		}
	}
	for ( i = 0; i < UI_FRAMES; i++ ) {
		if ( ui.garbage[i] ) {
			ri.Free( ui.garbage[i] );
		}
	}
	if ( ui.textures ) {
		ri.Free( ui.textures );
	}
	Com_Memset( &ui, 0, sizeof( ui ) );
}
