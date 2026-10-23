shader_type sky;
// id Tech 3 cloud skies (skyParms - <cloudheight> -): the shader's stages projected onto a
// dome at cloud height and scrolled with their tcMod scroll/scale. Layer 2 is added when the
// stage was additive, blended otherwise.
uniform sampler2D layer1 : source_color, filter_linear_mipmap, repeat_enable;
uniform vec2 scale1 = vec2(1.0);
uniform vec2 scroll1 = vec2(0.0);
uniform sampler2D layer2 : source_color, hint_default_black, filter_linear_mipmap, repeat_enable;
uniform vec2 scale2 = vec2(1.0);
uniform vec2 scroll2 = vec2(0.0);
uniform bool layer2_add = true;
uniform float layer2_mix = 0.5;
uniform float cloud_height = 512.0;     // id units (curvature of the dome)
uniform vec3 horizon_color : source_color = vec3(0.1, 0.09, 0.1);
uniform float energy = 1.0;

void sky() {
	vec3 d = EYEDIR;
	float h = max(d.y, 0.03);
	float bend = 512.0 / max(cloud_height, 64.0);
	vec2 p = d.xz / (h + 0.25 * bend) * 0.25;
	vec3 c = texture(layer1, p * scale1 + TIME * scroll1).rgb;
	vec3 c2 = texture(layer2, p * scale2 + TIME * scroll2).rgb;
	c = layer2_add ? c + c2 : mix(c, c2, layer2_mix);
	COLOR = mix(horizon_color, c, smoothstep(-0.02, 0.12, d.y)) * energy;
}
