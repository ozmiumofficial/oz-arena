shader_type spatial;
// Scrolling additive layers: rail core, lightning bolt, teleport effect.
// Mirrors the shader stages: tcMod scale / scroll per layer, rgbGen wave sin brightness.
render_mode unshaded, blend_add, cull_disabled, depth_draw_never, shadows_disabled, fog_disabled;

uniform sampler2D tex : source_color, filter_linear_mipmap, repeat_enable;
uniform vec4 color : source_color = vec4(1.0);
uniform float intensity = 1.0;
uniform float progress = 0.0;      // 0..1 over the effect's life, fades out linearly
uniform vec2 scale_a = vec2(1.0);
uniform vec2 scroll_a = vec2(0.0);
uniform float wave_a = 0.0;        // rgbGen wave sin frequency (Hz), 0 = steady
uniform float amp_a = 0.5;         // rgbGen wave sin amplitude around 1, clamped to 0..1 like the renderer
uniform float layer_b = 0.0;       // 1 enables the second layer (lightning)
uniform vec2 scale_b = vec2(1.0);
uniform vec2 scroll_b = vec2(0.0);
uniform float wave_b = 0.0;
uniform float amp_b = 0.8;
uniform vec2 repeat = vec2(1.0);   // texture repeats along the geometry (beam length)

void fragment() {
	vec2 uv = UV * repeat;
	float wa = wave_a > 0.0 ? clamp(1.0 + amp_a * sin(TIME * 6.2831 * wave_a), 0.0, 1.0) : 1.0;
	vec3 c = texture(tex, uv * scale_a + TIME * scroll_a).rgb * wa;
	if (layer_b > 0.5) {
		float wb = wave_b > 0.0 ? clamp(1.0 + amp_b * sin(TIME * 6.2831 * wave_b), 0.0, 1.0) : 1.0;
		c += texture(tex, uv * scale_b + TIME * scroll_b).rgb * wb;
	}
	ALBEDO = c * color.rgb * intensity;
	ALPHA = clamp(1.0 - progress, 0.0, 1.0);
}
