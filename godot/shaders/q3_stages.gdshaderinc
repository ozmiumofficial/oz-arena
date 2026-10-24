// Quake 3's multi-stage model shaders (tools/make_prop_shaders.py -> prop_shaders.json):
// up to three stages drawn in order, each with its own blend, tcGen environment, tcMod
// scale/rotate/scroll/turb and an rgbGen wave. Stages the engine lights (rgbGen lightingDiffuse)
// go to ALBEDO and take the scene's lighting; the rest are full bright like rgbGen identity and
// go to EMISSION. Variants differ only in render mode (opaque/additive, culling).

#include "res://shaders/q3_deform.gdshaderinc"

uniform sampler2D tex0 : source_color, filter_linear_mipmap, repeat_enable;
uniform sampler2D tex1 : source_color, filter_linear_mipmap, repeat_enable;
uniform sampler2D tex2 : source_color, filter_linear_mipmap, repeat_enable;
// mode: 0 off, 1 replace, 2 add, 3 alpha blend, 4 filter (multiply), 5 alpha-test replace
uniform ivec3 mode = ivec3(1, 0, 0);
uniform ivec3 env = ivec3(0);          // 1 = tcGen environment
uniform ivec3 lit = ivec3(0);          // 1 = lit by the scene (rgbGen lightingDiffuse)
uniform vec2 scale0 = vec2(1.0);
uniform vec2 scale1 = vec2(1.0);
uniform vec2 scale2 = vec2(1.0);
uniform vec3 rotate = vec3(0.0);       // degrees per second, around the texture center
uniform vec2 scroll0 = vec2(0.0);
uniform vec2 scroll1 = vec2(0.0);
uniform vec2 scroll2 = vec2(0.0);
uniform vec2 turb0 = vec2(0.0);        // amplitude, frequency
uniform vec2 turb1 = vec2(0.0);
uniform vec2 turb2 = vec2(0.0);
uniform vec4 wave0 = vec4(1.0, 0.0, 0.0, 0.0);   // base, amplitude, phase, frequency
uniform vec4 wave1 = vec4(1.0, 0.0, 0.0, 0.0);
uniform vec4 wave2 = vec4(1.0, 0.0, 0.0, 0.0);
uniform ivec3 wave_func = ivec3(0);    // 0 none, 1 sin, 2 square, 3 triangle, 4 sawtooth, 5 inverse sawtooth
uniform float roughness = 0.65;        // lit stages stay matte
// Quake 3 doubled the screen with r_overBrightBits 1, so full-bright (rgbGen identity) stages
// looked about twice as bright as their images; additive layers get less so they don't blow out
uniform float overbright = 2.0;
uniform float overbright_add = 1.3;

float wave(int f, vec4 w) {
	if (f == 0) return 1.0;
	float x = fract(w.z + TIME * w.w);
	float v = f == 1 ? sin(x * TAU) : f == 2 ? (x < 0.5 ? 1.0 : -1.0) : f == 3 ? (x < 0.5 ? 4.0 * x - 1.0 : 3.0 - 4.0 * x)
		: f == 4 ? x : 1.0 - x;
	return clamp(w.x + w.y * v, 0.0, 2.0);
}

vec2 stage_uv(vec2 uv, vec2 env_uv, int e, vec2 sc, float rot, vec2 scr, vec2 tb) {
	vec2 st = e != 0 ? env_uv : uv;
	if (tb.x != 0.0) st += tb.x * sin((st.yx + TIME * tb.y) * TAU);   // tcMod turb
	st *= sc;                                                          // tcMod scale
	float a = radians(-rot * TIME);                                    // tcMod rotate
	st = mat2(vec2(cos(a), sin(a)), vec2(-sin(a), cos(a))) * (st - 0.5) + 0.5;
	return st + scr * TIME;                                            // tcMod scroll
}

void apply_stage(int m, int lit_flag, vec4 s, inout vec3 lc, inout vec3 uc) {
	bool l = lit_flag != 0;
	vec3 sl = l ? s.rgb : vec3(0.0);
	vec3 su = l ? vec3(0.0) : s.rgb;
	if (m == 1 || (m == 5 && s.a >= 0.5)) { lc = sl; uc = su; }
	else if (m == 2) { lc += sl; uc += su; }
	else if (m == 3) { lc = mix(lc, sl, s.a); uc = mix(uc, su, s.a); }
	else if (m == 4) { lc *= s.rgb; uc *= s.rgb; }
}

// deformVertexes (q3_deform.gdshaderinc): the CTF flags wave, the armor's energy shell swells
void vertex() {
	// the phase follows the model-space position, as id's tess.xyz for a model: a carried flag
	// mustn't flutter faster because it moves
	VERTEX = q3_deform(VERTEX, NORMAL, UV, VERTEX);
}


void fragment() {
	// tcGen environment: the view direction reflected about the normal, in world space (tr_shade_calc.c)
	vec3 n = normalize((INV_VIEW_MATRIX * vec4(NORMAL, 0.0)).xyz);
	vec3 v = normalize((INV_VIEW_MATRIX * vec4(VIEW, 0.0)).xyz);
	vec3 r = 2.0 * dot(n, v) * n - v;
	vec2 env_uv = vec2(0.5 + r.x * 0.5, 0.5 - r.y * 0.5);
	vec3 lc = vec3(0.0);
	vec3 uc = vec3(0.0);
	if (mode.x != 0) apply_stage(mode.x, lit.x, texture(tex0, stage_uv(UV, env_uv, env.x, scale0, rotate.x, scroll0, turb0)) * wave(wave_func.x, wave0), lc, uc);
	if (mode.y != 0) apply_stage(mode.y, lit.y, texture(tex1, stage_uv(UV, env_uv, env.y, scale1, rotate.y, scroll1, turb1)) * wave(wave_func.y, wave1), lc, uc);
	if (mode.z != 0) apply_stage(mode.z, lit.z, texture(tex2, stage_uv(UV, env_uv, env.z, scale2, rotate.z, scroll2, turb2)) * wave(wave_func.z, wave2), lc, uc);
#ifdef Q3_ADDITIVE
	ALBEDO = lc + uc * overbright_add;
#else
	ALBEDO = lc;
	EMISSION = uc * overbright;
	ROUGHNESS = roughness;
	METALLIC = 0.0;
#endif
}
