shader_type spatial;
// id Tech 3 liquids (surfaceparm water / slime / lava) as one modern shader:
// two counter-moving samples of the remastered texture (the shader's tcMod scroll/turb),
// deformVertexes wave as real vertex motion, screen-space refraction for water,
// emissive flow for slime and lava. kind: 0 water, 1 slime, 2 lava.
render_mode blend_mix, cull_disabled, depth_draw_always;

uniform sampler2D albedo_tex : source_color, filter_linear_mipmap, repeat_enable;
uniform sampler2D normal_tex : hint_normal, filter_linear_mipmap, repeat_enable;
uniform sampler2D screen_tex : hint_screen_texture, filter_linear_mipmap;
uniform int kind = 0;
uniform vec2 scroll = vec2(0.02, 0.01);
uniform vec4 turb = vec4(0.0, 0.1, 0.0, 0.1);
uniform float opacity = 0.6;
uniform float emission_energy = 0.0;
uniform float wave_amp = 0.0;        // meters (deformVertexes wave amplitude)
uniform float wave_freq = 0.0;       // Hz
uniform float wave_spread = 0.0;     // per meter
uniform float roughness = 0.35;      // water stays a little glossy so it reads as water
uniform float refraction = 0.03;

void vertex() {
	if (wave_amp > 0.0) {
		vec3 wp = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
		VERTEX.y += wave_amp * sin(TIME * wave_freq * TAU + (wp.x + wp.z) * wave_spread);
	}
}

void fragment() {
	vec2 t = vec2(turb.y * sin(TIME * turb.w * TAU + UV.y * 3.0), turb.y * cos(TIME * turb.w * TAU + UV.x * 3.0));
	vec2 uv1 = UV + scroll * TIME + t;
	vec2 uv2 = UV * 0.7 - scroll.yx * TIME * 0.8 - t;
	vec3 c = mix(texture(albedo_tex, uv1).rgb, texture(albedo_tex, uv2).rgb, 0.5);
	vec3 n = normalize(texture(normal_tex, uv1).rgb * 2.0 - 1.0 + texture(normal_tex, uv2).rgb * 2.0 - 1.0) * 0.5 + 0.5;
	NORMAL_MAP = n;
	ROUGHNESS = roughness;
	METALLIC = 0.0;
	if (kind == 0) {
		vec3 behind = texture(screen_tex, SCREEN_UV + (n.xy - 0.5) * refraction).rgb;
		ALBEDO = mix(behind, c, 0.45);
		ALPHA = opacity;
	} else {
		ALBEDO = c;
		ALPHA = kind == 2 ? 1.0 : opacity;
		float pulse = 0.85 + 0.15 * sin(TIME * 1.3 + UV.x * 5.0);
		EMISSION = c * emission_energy * pulse;
	}
}
