// Remastered id Tech 3 world surface (tools/make_godot_world.py writes one material per shader).
// Base layer: PBR albedo / normal (OpenGL) / ORM (R AO, G rough, B metal), moved by the base
// stage's tcMods; animMap flipbooks as a horizontal atlas; up to two additive glow layers with
// their own tcMod motion and rgbGen wave; self_illum for surface lights without a glow stage;
// optional stochastic hex tiling so large faces show no visible repeat.

#include "res://shaders/q3_deform.gdshaderinc"

uniform sampler2D albedo_tex : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D normal_tex : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D orm_tex : hint_default_white, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D emission_tex : source_color, hint_default_black, filter_linear_mipmap, repeat_enable;
uniform float normal_strength = 1.0;
uniform vec4 tint : source_color = vec4(1.0);   // the map editor's per-face tint (white: none)
uniform float alpha_cut = 0.0;            // > 0: alpha scissor threshold (alphaFunc)
uniform bool stochastic = false;          // hex tiling against visible repeats (isotropic textures)
uniform float stochastic_scale = 1.0;     // lattice cells per texture repeat

uniform vec2 base_scale = vec2(1.0);
uniform float base_rotate = 0.0;          // degrees per second (tcMod rotate)
uniform vec2 base_scroll = vec2(0.0);     // repeats per second (tcMod scroll)
uniform vec4 base_turb = vec4(0.0);       // base, amplitude, phase, frequency (tcMod turb)
uniform float frames = 1.0;               // animMap frame count (albedo/emission atlases)
uniform float fps = 0.0;

uniform sampler2D glow1_tex : source_color, hint_default_black, filter_linear_mipmap, repeat_enable;
uniform vec2 glow1_scale = vec2(1.0);
uniform vec3 glow1_motion = vec3(0.0);    // scroll s, scroll t, rotate deg/s
uniform vec4 glow1_wave = vec4(0.0, 1.0, 0.0, 0.0);   // func, base, amp, freq (func 0 = steady)
uniform float glow1_phase = 0.0;
uniform float glow1_frames = 1.0;
uniform float glow1_fps = 0.0;
uniform sampler2D glow2_tex : source_color, hint_default_black, filter_linear_mipmap, repeat_enable;
uniform vec2 glow2_scale = vec2(1.0);
uniform vec3 glow2_motion = vec3(0.0);
uniform vec4 glow2_wave = vec4(0.0, 1.0, 0.0, 0.0);
uniform float glow2_phase = 0.0;
uniform float emission_energy = 1.0;      // from q3map_surfacelight
uniform float self_illum = 0.0;           // bright texels glow (light panels with no glow stage)
uniform bool env_map = false;             // the base stage is tcGen environment (glass, chrome)
// an opaque stage under a blended paint layer, showing where the paint is clear: a reflection
// (console: chrome) or a moving texture (diamond2c_ow: energy flowing under the grate)
uniform sampler2D under_tex : source_color, hint_default_black, filter_linear_mipmap, repeat_enable;
uniform bool has_under = false;
uniform bool under_env = true;            // tcGen environment, else the surface's own coordinates
uniform vec2 under_scale = vec2(1.0);
uniform vec3 under_motion = vec3(0.0);    // scroll s, scroll t, rotate deg/s
uniform vec4 under_turb = vec4(0.0);
// the glow stages are drawn before the blended paint (teslacoil: arcs inside the cable frame):
// they show at full strength where the paint is clear, and not through it
uniform bool glow_under = false;
uniform bool glow_lit = false;   // glow stages drawn before the $lightmap filter: lit, not glowing (chrome_metal's detail)

// tcGen environment (tr_shade_calc.c RB_CalcEnvironmentTexCoords): the view reflected off the
// surface, its id y and z (Godot x and y) as texture coordinates, so the image slides as you move.
vec2 env_uv(vec3 normal, vec3 view, mat4 inv_view) {
	vec3 r = (inv_view * vec4(2.0 * normal * dot(normal, view) - view, 0.0)).xyz;
	return vec2(0.5 + r.x * 0.5, 0.5 - r.y * 0.5);
}

float q3_wave(vec4 w, float phase) {
	// id Tech 3 waveforms: 1 sin, 2 triangle, 3 square, 4 sawtooth, 5 inversesawtooth
	if (w.x < 0.5) return 1.0;
	float t = fract(phase + TIME * w.w);
	float f = 0.0;
	if (w.x < 1.5) f = sin(t * TAU);
	else if (w.x < 2.5) f = 1.0 - 4.0 * abs(fract(t + 0.25) - 0.5);   // 0, 1, 0, -1 as id's table
	else if (w.x < 3.5) f = t < 0.5 ? 1.0 : -1.0;
	else if (w.x < 4.5) f = t;
	else f = 1.0 - t;
	return max(w.y + w.z * f, 0.0);
}

vec2 tc_motion(vec2 uv, vec2 scale, float rotate_deg, vec2 scroll, vec4 turb) {
	uv *= scale;
	if (rotate_deg != 0.0) {
		float a = radians(rotate_deg) * TIME;
		uv = mat2(vec2(cos(a), sin(a)), vec2(-sin(a), cos(a))) * (uv - 0.5) + 0.5;
	}
	uv += scroll * TIME;
	if (turb.y != 0.0) {
		uv += turb.y * sin((uv.yx * 0.125 + turb.z + TIME * turb.w) * TAU);
	}
	return uv;
}

vec2 flip_uv(vec2 uv, float n, float rate) {
	if (n < 1.5) return uv;
	float f = floor(mod(TIME * rate, n));
	return vec2((fract(uv.x) + f) / n, uv.y);
}

vec2 hash2(vec2 p) {
	return fract(sin(vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)))) * 43758.5453);
}

// Hex-lattice tiling: three randomly offset copies blended by triangle weights.
// Offsets only (no rotation), so tangent-space normals stay valid.
void hex_weights(vec2 uv, out vec2 o1, out vec2 o2, out vec2 o3, out vec3 w) {
	vec2 skew = mat2(vec2(1.0, 0.0), vec2(-0.57735027, 1.15470054)) * uv * 3.4641016 * stochastic_scale;
	vec2 b = floor(skew);
	vec3 t = vec3(fract(skew), 0.0);
	t.z = 1.0 - t.x - t.y;
	vec2 v1, v2, v3;
	if (t.z > 0.0) {
		w = vec3(t.z, t.y, t.x); v1 = b; v2 = b + vec2(0.0, 1.0); v3 = b + vec2(1.0, 0.0);
	} else {
		w = vec3(-t.z, 1.0 - t.y, 1.0 - t.x); v1 = b + vec2(1.0); v2 = b + vec2(1.0, 0.0); v3 = b + vec2(0.0, 1.0);
	}
	w = pow(w, vec3(4.0));
	w /= (w.x + w.y + w.z);
	o1 = hash2(v1); o2 = hash2(v2); o3 = hash2(v3);
}

vec4 tile_sample(sampler2D tex, vec2 uv, vec2 o1, vec2 o2, vec2 o3, vec3 w, vec2 dx, vec2 dy) {
	if (!stochastic) return texture(tex, uv);
	return textureGrad(tex, uv + o1, dx, dy) * w.x + textureGrad(tex, uv + o2, dx, dy) * w.y
		+ textureGrad(tex, uv + o3, dx, dy) * w.z;
}

vec3 glow_layers(vec2 uv0) {
	vec3 e = vec3(0.0);
	vec2 g1 = tc_motion(uv0, glow1_scale, glow1_motion.z, glow1_motion.xy, vec4(0.0));
	e += texture(glow1_tex, flip_uv(g1, glow1_frames, glow1_fps)).rgb * q3_wave(glow1_wave, glow1_phase);
	vec2 g2 = tc_motion(uv0, glow2_scale, glow2_motion.z, glow2_motion.xy, vec4(0.0));
	e += texture(glow2_tex, g2).rgb * q3_wave(glow2_wave, glow2_phase);
	return e;
}

struct Surface {
	vec4 albedo;
	vec3 orm;
	vec3 normal;
	vec3 emission;
	vec3 emission_base;   // emission without the glow layers
	vec3 glow;            // the glow layers at full weight (the additive shaders add them as they are)
};

// uv0: the base coordinates; env: tcGen environment coordinates (for has_under)
Surface world_surface(vec2 uv0, vec2 env) {
	Surface s;
	vec2 uv = flip_uv(tc_motion(uv0, base_scale, base_rotate, base_scroll, base_turb), frames, fps);
	vec2 o1, o2, o3; vec3 w;
	hex_weights(uv, o1, o2, o3, w);
	vec2 dx = dFdx(uv), dy = dFdy(uv);
	s.albedo = tile_sample(albedo_tex, uv, o1, o2, o3, w, dx, dy);
	s.orm = tile_sample(orm_tex, uv, o1, o2, o3, w, dx, dy).rgb;
	s.normal = tile_sample(normal_tex, uv, o1, o2, o3, w, dx, dy).rgb;
	float paint = s.albedo.a;
	if (has_under) {
		vec2 u = under_env ? env * under_scale : tc_motion(uv0, under_scale, under_motion.z, under_motion.xy, under_turb);
		s.albedo = vec4(mix(texture(under_tex, u).rgb, s.albedo.rgb, paint), 1.0);
	} else if (glow_under) {
		s.albedo = vec4(s.albedo.rgb * paint, 1.0);
	}
	if (glow_lit) {
		s.albedo.rgb += glow_layers(uv0);   // lit like the base (s.glow stays zero)
	}
	s.albedo.rgb *= tint.rgb;
	float lum = dot(s.albedo.rgb, vec3(0.2126, 0.7152, 0.0722));
	s.emission_base = (texture(emission_tex, uv).rgb + s.albedo.rgb * smoothstep(0.45, 0.9, lum) * self_illum) * emission_energy;
	s.glow = glow_lit ? vec3(0.0) : glow_layers(uv0);
	s.emission = s.emission_base + (glow_under ? s.glow * (1.0 - paint) : s.glow * emission_energy);
	return s;
}
