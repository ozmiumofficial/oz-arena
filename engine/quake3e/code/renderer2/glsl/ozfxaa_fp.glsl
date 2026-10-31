// Oz: FXAA over the finished 3D view (Graphics > Anti-aliasing: FXAA; RB_OzFinish), Timothy Lottes's
// original FXAA: along an edge found from the luma of the four diagonal neighbors, a blend of two or
// four samples, the wider kept only while it stays within the neighborhood's luma.
uniform sampler2D u_DiffuseMap;
uniform vec4      u_Color;   // xy: one texel

varying vec2      var_Tex1;

#define FXAA_SPAN_MAX   8.0
#define FXAA_REDUCE_MUL (1.0 / 8.0)
#define FXAA_REDUCE_MIN (1.0 / 128.0)

void main()
{
	vec2 px = u_Color.xy;
	vec3 rgbNW = texture2D(u_DiffuseMap, var_Tex1 + vec2(-1.0, -1.0) * px).rgb;
	vec3 rgbNE = texture2D(u_DiffuseMap, var_Tex1 + vec2( 1.0, -1.0) * px).rgb;
	vec3 rgbSW = texture2D(u_DiffuseMap, var_Tex1 + vec2(-1.0,  1.0) * px).rgb;
	vec3 rgbSE = texture2D(u_DiffuseMap, var_Tex1 + vec2( 1.0,  1.0) * px).rgb;
	vec3 rgbM  = texture2D(u_DiffuseMap, var_Tex1).rgb;

	const vec3 luma = vec3(0.299, 0.587, 0.114);
	float lumaNW = dot(rgbNW, luma);
	float lumaNE = dot(rgbNE, luma);
	float lumaSW = dot(rgbSW, luma);
	float lumaSE = dot(rgbSE, luma);
	float lumaM  = dot(rgbM,  luma);
	float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
	float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

	vec2 dir = vec2(-((lumaNW + lumaNE) - (lumaSW + lumaSE)), (lumaNW + lumaSW) - (lumaNE + lumaSE));
	float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
	float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
	dir = clamp(dir * rcpDirMin, vec2(-FXAA_SPAN_MAX), vec2(FXAA_SPAN_MAX)) * px;

	vec3 rgbA = 0.5 * (texture2D(u_DiffuseMap, var_Tex1 + dir * (1.0 / 3.0 - 0.5)).rgb +
	                   texture2D(u_DiffuseMap, var_Tex1 + dir * (2.0 / 3.0 - 0.5)).rgb);
	vec3 rgbB = rgbA * 0.5 + 0.25 * (texture2D(u_DiffuseMap, var_Tex1 + dir * -0.5).rgb +
	                                 texture2D(u_DiffuseMap, var_Tex1 + dir *  0.5).rgb);
	float lumaB = dot(rgbB, luma);
	gl_FragColor = vec4((lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB, 1.0);
}
