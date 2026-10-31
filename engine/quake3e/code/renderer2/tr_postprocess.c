/*
===========================================================================
Copyright (C) 2011 Andrei Drexler, Richard Allen, James Canete

This file is part of Reaction source code.

Reaction source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Reaction source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Reaction source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include "tr_local.h"

void RB_ToneMap(FBO_t *hdrFbo, ivec4_t hdrBox, FBO_t *ldrFbo, ivec4_t ldrBox, int autoExposure)
{
	ivec4_t srcBox, dstBox;
	vec4_t color;
	static int lastFrameCount = 0;

	if (autoExposure)
	{
		if (lastFrameCount == 0 || tr.frameCount < lastFrameCount || tr.frameCount - lastFrameCount > 5)
		{
			// determine average log luminance
			FBO_t *srcFbo, *dstFbo, *tmp;
			int size = 256;

			lastFrameCount = tr.frameCount;

			VectorSet4(dstBox, 0, 0, size, size);

			FBO_Blit(hdrFbo, hdrBox, NULL, tr.textureScratchFbo[0], dstBox, &tr.calclevels4xShader[0], NULL, 0);

			srcFbo = tr.textureScratchFbo[0];
			dstFbo = tr.textureScratchFbo[1];

			// downscale to 1x1 texture
			while (size > 1)
			{
				VectorSet4(srcBox, 0, 0, size, size);
				//size >>= 2;
				size >>= 1;
				VectorSet4(dstBox, 0, 0, size, size);

				if (size == 1)
					dstFbo = tr.targetLevelsFbo;

				//FBO_Blit(targetFbo, srcBox, NULL, tr.textureScratchFbo[nextScratch], dstBox, &tr.calclevels4xShader[1], NULL, 0);
				FBO_FastBlit(srcFbo, srcBox, dstFbo, dstBox, GL_COLOR_BUFFER_BIT, GL_LINEAR);

				tmp = srcFbo;
				srcFbo = dstFbo;
				dstFbo = tmp;
			}
		}

		// blend with old log luminance for gradual change
		VectorSet4(srcBox, 0, 0, 0, 0);

		color[0] = 
		color[1] =
		color[2] = 1.0f;
		if (glRefConfig.textureFloat)
			color[3] = 0.03f;
		else
			color[3] = 0.1f;

		FBO_Blit(tr.targetLevelsFbo, srcBox, NULL, tr.calcLevelsFbo, NULL,  NULL, color, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
	}

	// tonemap
	color[0] =
	color[1] =
	color[2] = pow(2, r_cameraExposure->value - autoExposure); //exp2(r_cameraExposure->value);
	color[3] = 1.0f;

	if (autoExposure)
		GL_BindToTMU(tr.calcLevelsImage,  TB_LEVELSMAP);
	else
		GL_BindToTMU(tr.fixedLevelsImage, TB_LEVELSMAP);

	FBO_Blit(hdrFbo, hdrBox, NULL, ldrFbo, ldrBox, &tr.tonemapShader, color, 0);
}

/*
=============
RB_BokehBlur


Blurs a part of one framebuffer to another.

Framebuffers can be identical. 
=============
*/
void RB_BokehBlur(FBO_t *src, ivec4_t srcBox, FBO_t *dst, ivec4_t dstBox, float blur)
{
//	ivec4_t srcBox, dstBox;
	vec4_t color;
	
	blur *= 10.0f;

	if (blur < 0.004f)
		return;

	if (glRefConfig.framebufferObject)
	{
		// bokeh blur
		if (blur > 0.0f)
		{
			ivec4_t quarterBox;

			quarterBox[0] = 0;
			quarterBox[1] = tr.quarterFbo[0]->height;
			quarterBox[2] = tr.quarterFbo[0]->width;
			quarterBox[3] = -tr.quarterFbo[0]->height;

			// create a quarter texture
			//FBO_Blit(NULL, NULL, NULL, tr.quarterFbo[0], NULL, NULL, NULL, 0);
			FBO_FastBlit(src, srcBox, tr.quarterFbo[0], quarterBox, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		}

#ifndef HQ_BLUR
		if (blur > 1.0f)
		{
			// create a 1/16th texture
			//FBO_Blit(tr.quarterFbo[0], NULL, NULL, tr.textureScratchFbo[0], NULL, NULL, NULL, 0);
			FBO_FastBlit(tr.quarterFbo[0], NULL, tr.textureScratchFbo[0], NULL, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		}
#endif

		if (blur > 0.0f && blur <= 1.0f)
		{
			// Crossfade original with quarter texture
			VectorSet4(color, 1, 1, 1, blur);

			FBO_Blit(tr.quarterFbo[0], NULL, NULL, dst, dstBox, NULL, color, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
		}
#ifndef HQ_BLUR
		// ok blur, but can see some pixelization
		else if (blur > 1.0f && blur <= 2.0f)
		{
			// crossfade quarter texture with 1/16th texture
			FBO_Blit(tr.quarterFbo[0], NULL, NULL, dst, dstBox, NULL, NULL, 0);

			VectorSet4(color, 1, 1, 1, blur - 1.0f);

			FBO_Blit(tr.textureScratchFbo[0], NULL, NULL, dst, dstBox, NULL, color, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
		}
		else if (blur > 2.0f)
		{
			// blur 1/16th texture then replace
			int i;

			for (i = 0; i < 2; i++)
			{
				vec2_t blurTexScale;
				float subblur;

				subblur = ((blur - 2.0f) / 2.0f) / 3.0f * (float)(i + 1);

				blurTexScale[0] =
				blurTexScale[1] = subblur;

				color[0] =
				color[1] =
				color[2] = 0.5f;
				color[3] = 1.0f;

				if (i != 0)
					FBO_Blit(tr.textureScratchFbo[0], NULL, blurTexScale, tr.textureScratchFbo[1], NULL, &tr.bokehShader, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
				else
					FBO_Blit(tr.textureScratchFbo[0], NULL, blurTexScale, tr.textureScratchFbo[1], NULL, &tr.bokehShader, color, 0);
			}

			FBO_Blit(tr.textureScratchFbo[1], NULL, NULL, dst, dstBox, NULL, NULL, 0);
		}
#else // higher quality blur, but slower
		else if (blur > 1.0f)
		{
			// blur quarter texture then replace
			int i;

			src = tr.quarterFbo[0];
			dst = tr.quarterFbo[1];

			VectorSet4(color, 0.5f, 0.5f, 0.5f, 1);

			for (i = 0; i < 2; i++)
			{
				vec2_t blurTexScale;
				float subblur;

				subblur = (blur - 1.0f) / 2.0f * (float)(i + 1);

				blurTexScale[0] =
				blurTexScale[1] = subblur;

				color[0] =
				color[1] =
				color[2] = 1.0f;
				if (i != 0)
					color[3] = 1.0f;
				else
					color[3] = 0.5f;

				FBO_Blit(tr.quarterFbo[0], NULL, blurTexScale, tr.quarterFbo[1], NULL, &tr.bokehShader, color, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
			}

			FBO_Blit(tr.quarterFbo[1], NULL, NULL, dst, dstBox, NULL, NULL, 0);
		}
#endif
	}
}


static void RB_RadialBlur(FBO_t *srcFbo, FBO_t *dstFbo, int passes, float stretch, float x, float y, float w, float h, float xcenter, float ycenter, float alpha)
{
	ivec4_t srcBox, dstBox;
	int srcWidth, srcHeight;
	vec4_t color;
	const float inc = 1.f / passes;
	const float mul = powf(stretch, inc);
	float scale;

	alpha *= inc;
	VectorSet4(color, alpha, alpha, alpha, 1.0f);

	srcWidth  = srcFbo ? srcFbo->width  : glConfig.vidWidth;
	srcHeight = srcFbo ? srcFbo->height : glConfig.vidHeight;

	VectorSet4(srcBox, 0, 0, srcWidth, srcHeight);

	VectorSet4(dstBox, x, y, w, h);
	FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, 0);

	--passes;
	scale = mul;
	while (passes > 0)
	{
		float iscale = 1.f / scale;
		float s0 = xcenter * (1.f - iscale);
		float t0 = (1.0f - ycenter) * (1.f - iscale);

		srcBox[0] = s0 * srcWidth;
		srcBox[1] = t0 * srcHeight;
		srcBox[2] = iscale * srcWidth;
		srcBox[3] = iscale * srcHeight;
			
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );

		scale *= mul;
		--passes;
	}
}


static qboolean RB_UpdateSunFlareVis(void)
{
	GLuint sampleCount = 0;
	if (!glRefConfig.occlusionQuery)
		return qtrue;

	tr.sunFlareQueryIndex ^= 1;
	if (!tr.sunFlareQueryActive[tr.sunFlareQueryIndex])
		return qtrue;

	/* debug code */
	if (0)
	{
		int iter;
		for (iter=0 ; ; ++iter)
		{
			GLint available = 0;
			qglGetQueryObjectiv(tr.sunFlareQuery[tr.sunFlareQueryIndex], GL_QUERY_RESULT_AVAILABLE, &available);
			if (available)
				break;
		}

		ri.Printf(PRINT_DEVELOPER, "Waited %d iterations\n", iter);
	}
	
	// Note: On desktop OpenGL this is a sample count (glRefConfig.occlusionQueryTarget == GL_SAMPLES_PASSED)
	// but on OpenGL ES this is a boolean (glRefConfig.occlusionQueryTarget == GL_ANY_SAMPLES_PASSED)
	qglGetQueryObjectuiv(tr.sunFlareQuery[tr.sunFlareQueryIndex], GL_QUERY_RESULT, &sampleCount);
	return sampleCount > 0;
}

void RB_SunRays(FBO_t *srcFbo, ivec4_t srcBox, FBO_t *dstFbo, ivec4_t dstBox)
{
	vec4_t color;
	float dot;
	const float cutoff = 0.25f;
	qboolean colorize = qtrue;

//	float w, h, w2, h2;
	mat4_t mvp;
	vec4_t pos, hpos;

	dot = DotProduct(tr.sunDirection, backEnd.viewParms.or.axis[0]);
	if (dot < cutoff)
		return;

	if (!RB_UpdateSunFlareVis())
		return;

	// From RB_DrawSun()
	{
		float dist;
		mat4_t trans, model;

		Mat4Translation( backEnd.viewParms.or.origin, trans );
		Mat4Multiply( backEnd.viewParms.world.modelMatrix, trans, model );
		Mat4Multiply(backEnd.viewParms.projectionMatrix, model, mvp);

		dist = backEnd.viewParms.zFar / 1.75;		// div sqrt(3)

		VectorScale( tr.sunDirection, dist, pos );
	}

	// project sun point
	//Mat4Multiply(backEnd.viewParms.projectionMatrix, backEnd.viewParms.world.modelMatrix, mvp);
	Mat4Transform(mvp, pos, hpos);

	// transform to UV coords
	hpos[3] = 0.5f / hpos[3];

	pos[0] = 0.5f + hpos[0] * hpos[3];
	pos[1] = 0.5f + hpos[1] * hpos[3];

	// initialize quarter buffers
	{
		float mul = 1.f;
		ivec4_t rayBox, quarterBox;
		int srcWidth  = srcFbo ? srcFbo->width  : glConfig.vidWidth;
		int srcHeight = srcFbo ? srcFbo->height : glConfig.vidHeight;

		VectorSet4(color, mul, mul, mul, 1);

		rayBox[0] = srcBox[0] * tr.sunRaysFbo->width  / srcWidth;
		rayBox[1] = srcBox[1] * tr.sunRaysFbo->height / srcHeight;
		rayBox[2] = srcBox[2] * tr.sunRaysFbo->width  / srcWidth;
		rayBox[3] = srcBox[3] * tr.sunRaysFbo->height / srcHeight;

		quarterBox[0] = 0;
		quarterBox[1] = tr.quarterFbo[0]->height;
		quarterBox[2] = tr.quarterFbo[0]->width;
		quarterBox[3] = -tr.quarterFbo[0]->height;

		// first, downsample the framebuffer
		if (colorize)
		{
			FBO_FastBlit(srcFbo, srcBox, tr.quarterFbo[0], quarterBox, GL_COLOR_BUFFER_BIT, GL_LINEAR);
			FBO_Blit(tr.sunRaysFbo, rayBox, NULL, tr.quarterFbo[0], quarterBox, NULL, color, GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO);
		}
		else
		{
			FBO_FastBlit(tr.sunRaysFbo, rayBox, tr.quarterFbo[0], quarterBox, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		}
	}

	// radial blur passes, ping-ponging between the two quarter-size buffers
	{
		const float stretch_add = 2.f/3.f;
		float stretch = 1.f + stretch_add;
		int i;
		for (i=0; i<2; ++i)
		{
			RB_RadialBlur(tr.quarterFbo[i&1], tr.quarterFbo[(~i) & 1], 5, stretch, 0.f, 0.f, tr.quarterFbo[0]->width, tr.quarterFbo[0]->height, pos[0], pos[1], 1.125f);
			stretch += stretch_add;
		}
	}
	
	// add result back on top of the main buffer
	{
		float mul = 1.f;

		VectorSet4(color, mul, mul, mul, 1);

		FBO_Blit(tr.quarterFbo[0], NULL, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
	}
}

static void RB_BlurAxis(FBO_t *srcFbo, FBO_t *dstFbo, float strength, qboolean horizontal)
{
	float dx, dy;
	float xmul, ymul;
	float weights[3] = {
		0.227027027f,
		0.316216216f,
		0.070270270f,
	};
	float offsets[3] = {
		0.f,
		1.3846153846f,
		3.2307692308f,
	};

	xmul = horizontal;
	ymul = 1.f - xmul;

	xmul *= strength;
	ymul *= strength;

	{
		ivec4_t srcBox, dstBox;
		vec4_t color;

		VectorSet4(color, weights[0], weights[0], weights[0], 1.0f);
		VectorSet4(srcBox, 0, 0, srcFbo->width, srcFbo->height);
		VectorSet4(dstBox, 0, 0, dstFbo->width, dstFbo->height);
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, 0);

		VectorSet4(color, weights[1], weights[1], weights[1], 1.0f);
		dx = offsets[1] * xmul;
		dy = offsets[1] * ymul;
		VectorSet4(srcBox, dx, dy, srcFbo->width, srcFbo->height);
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
		VectorSet4(srcBox, -dx, -dy, srcFbo->width, srcFbo->height);
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);

		VectorSet4(color, weights[2], weights[2], weights[2], 1.0f);
		dx = offsets[2] * xmul;
		dy = offsets[2] * ymul;
		VectorSet4(srcBox, dx, dy, srcFbo->width, srcFbo->height);
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
		VectorSet4(srcBox, -dx, -dy, srcFbo->width, srcFbo->height);
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
	}
}

static void RB_HBlur(FBO_t *srcFbo, FBO_t *dstFbo, float strength)
{
	RB_BlurAxis(srcFbo, dstFbo, strength, qtrue);
}

static void RB_VBlur(FBO_t *srcFbo, FBO_t *dstFbo, float strength)
{
	RB_BlurAxis(srcFbo, dstFbo, strength, qfalse);
}

/*
=============
RB_OzBloom

Oz (ENGINE_PLAN.md P1): bloom from the existing blur. What's brighter than r_ozBloomThreshold
goes to the half-size buffer (halved, so overbright fits its 8 bits), is blurred three times
with a growing radius, and is added back onto the HDR scene before the tone map.
=============
*/
void RB_OzBloom(FBO_t *srcFbo, ivec4_t srcBox)
{
	vec4_t color;
	float strength;

	if (!tr.quarterFbo[0] || !tr.quarterFbo[1] || r_ozBloomIntensity->value <= 0.0f)
		return;

	VectorSet4(color, 0.5f, 0.5f, 0.5f, r_ozBloomThreshold->value);
	FBO_Blit(srcFbo, srcBox, NULL, tr.quarterFbo[0], NULL, &tr.ozBloomShader, color, 0);

	for (strength = 1.0f; strength <= 4.0f; strength *= 2.0f)
	{
		RB_HBlur(tr.quarterFbo[0], tr.quarterFbo[1], strength);
		RB_VBlur(tr.quarterFbo[1], tr.quarterFbo[0], strength);
	}

	color[0] = color[1] = color[2] = 2.0f * r_ozBloomIntensity->value;
	color[3] = 1.0f;
	FBO_Blit(tr.quarterFbo[0], NULL, NULL, srcFbo, srcBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
}

void RB_GaussianBlur(FBO_t *srcFbo, FBO_t *dstFbo, float blur)
{
	//float mul = 1.f;
	float factor = Com_Clamp(0.f, 1.f, blur);

	if (factor <= 0.f)
		return;

	{
		ivec4_t srcBox, dstBox;
		vec4_t color;

		VectorSet4(color, 1, 1, 1, 1);

		// first, downsample the framebuffer
		FBO_FastBlit(srcFbo, NULL, tr.quarterFbo[0], NULL, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		FBO_FastBlit(tr.quarterFbo[0], NULL, tr.textureScratchFbo[0], NULL, GL_COLOR_BUFFER_BIT, GL_LINEAR);

		// set the alpha channel
		qglColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
		FBO_BlitFromTexture(tr.whiteImage, NULL, NULL, tr.textureScratchFbo[0], NULL, NULL, color, GLS_DEPTHTEST_DISABLE);
		qglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

		// blur the tiny buffer horizontally and vertically
		RB_HBlur(tr.textureScratchFbo[0], tr.textureScratchFbo[1], factor);
		RB_VBlur(tr.textureScratchFbo[1], tr.textureScratchFbo[0], factor);

		// finally, merge back to framebuffer
		VectorSet4(srcBox, 0, 0, tr.textureScratchFbo[0]->width, tr.textureScratchFbo[0]->height);
		VectorSet4(dstBox, 0, 0, glConfig.vidWidth,              glConfig.vidHeight);
		color[3] = factor;
		FBO_Blit(tr.textureScratchFbo[0], srcBox, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
	}
}

/*
=============
R_OzRenderScale

Oz: Video > Render scale (r_ozRenderScale): a world view is drawn at this share of its size into the
lower left of its place, and RB_OzFinish scales it up after the tone map. Only with the HDR post
process, which has the buffers for it.
=============
*/
float R_OzRenderScale(void)
{
	if (!glRefConfig.framebufferObject || !tr.renderFbo || !tr.screenScratchFbo || !r_hdr->integer || !r_postProcess->integer)
		return 1.0f;
	return Com_Clamp(0.5f, 1.0f, r_ozRenderScale->value);
}

/*
=============
RB_OzUpscale

The view drawn at box, scaled up to fullBox in the same buffer, through screenScratchFbo. The texels
just past box are last frame's, so its last column and row are copied out over them first. AMD FSR 1
(r_ozUpscaler 1): EASU, the edge-adaptive upscale, then RCAS sharpening, with FsrEasuConOffset's and
FsrRcasCon's constants (ffx_fsr1.h) passed as float bits; else a bilinear blit.
=============
*/
static void RB_OzUpscale(FBO_t *fbo, ivec4_t box, ivec4_t full)
{
	ivec4_t edge, past;

	VectorSet4(edge, box[0] + box[2] - 1, box[1], 1, box[3]);
	VectorSet4(past, box[0] + box[2], box[1], 2, box[3]);
	FBO_FastBlit(fbo, edge, fbo, past, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	VectorSet4(edge, box[0], box[1] + box[3] - 1, box[2] + 2, 1);
	VectorSet4(past, box[0], box[1] + box[3], box[2] + 2, 2);
	FBO_FastBlit(fbo, edge, fbo, past, GL_COLOR_BUFFER_BIT, GL_NEAREST);

	if (r_ozUpscaler->integer && tr.ozFsrReady)
	{
		const float iw = box[2], ih = box[3], ow = full[2], oh = full[3];
		const float tw = fbo->width, th = fbo->height;
		vec4_t con0, con1, con2, con3, corner;

		VectorSet4(con0, iw / ow, ih / oh, 0.5f * iw / ow - 0.5f + box[0], 0.5f * ih / oh - 0.5f + box[1]);
		VectorSet4(con1, 1.0f / tw, 1.0f / th, 1.0f / tw, -1.0f / th);
		VectorSet4(con2, -1.0f / tw, 2.0f / th, 1.0f / tw, 2.0f / th);
		VectorSet4(con3, 0.0f, 4.0f / th, 0.0f, 0.0f);
		VectorSet4(corner, full[0], full[1], 0.0f, 0.0f);
		GLSL_SetUniformVec4(&tr.ozFsrEasuShader, UNIFORM_OZFSR0, con0);
		GLSL_SetUniformVec4(&tr.ozFsrEasuShader, UNIFORM_OZFSR1, con1);
		GLSL_SetUniformVec4(&tr.ozFsrEasuShader, UNIFORM_OZFSR2, con2);
		GLSL_SetUniformVec4(&tr.ozFsrEasuShader, UNIFORM_OZFSR3, con3);
		FBO_Blit(fbo, NULL, NULL, tr.screenScratchFbo, full, &tr.ozFsrEasuShader, corner, 0);

		// AMD's default sharpness, 0.2 stops below the most
		VectorSet4(con0, exp2f(-0.2f), 0.0f, 0.0f, 0.0f);
		GLSL_SetUniformVec4(&tr.ozFsrRcasShader, UNIFORM_OZFSR0, con0);
		FBO_Blit(tr.screenScratchFbo, NULL, NULL, fbo, full, &tr.ozFsrRcasShader, NULL, 0);
		return;
	}
	FBO_FastBlit(fbo, box, tr.screenScratchFbo, full, GL_COLOR_BUFFER_BIT, GL_LINEAR);
	FBO_FastBlit(tr.screenScratchFbo, full, fbo, full, GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

/*
=============
RB_OzFinish

Oz: after the tone map, on the finished view in box: contrast and the colorblind filter (Video >
Contrast, Accessibility > Colorblind), FXAA (Graphics > Anti-aliasing), then the render scale's
upscale to fullBox. Each pass goes through screenScratchFbo and back.
=============
*/
void RB_OzFinish(FBO_t *fbo, ivec4_t box, ivec4_t fullBox)
{
	const float contrast = Com_Clamp(0.5f, 1.5f, r_ozContrast->value);
	const int colorblind = r_ozColorblind->integer;
	vec4_t color;

	if (!fbo || !tr.screenScratchFbo)
		return;

	if (contrast != 1.0f || (colorblind >= 1 && colorblind <= 3))
	{
		VectorSet4(color, contrast, (colorblind >= 1 && colorblind <= 3) ? colorblind : 0, 0.0f, 0.0f);
		FBO_Blit(fbo, box, NULL, tr.screenScratchFbo, box, &tr.ozGradeShader, color, 0);
		FBO_FastBlit(tr.screenScratchFbo, box, fbo, box, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	}

	if (r_ozFxaa->integer)
	{
		VectorSet4(color, 1.0f / fbo->width, 1.0f / fbo->height, 0.0f, 0.0f);
		FBO_Blit(fbo, box, NULL, tr.screenScratchFbo, box, &tr.ozFxaaShader, color, 0);
		FBO_FastBlit(tr.screenScratchFbo, box, fbo, box, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	}

	if (box[2] != fullBox[2] || box[3] != fullBox[3])
		RB_OzUpscale(fbo, box, fullBox);
}
