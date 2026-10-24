// A sky face in the map editor: the map's sky seen through the brush, by view direction, with the
// same mappings as shaders/world/sky_clouds.gdshader (mode 0) and sky_panorama.gdshader (mode 1).
// In the game these faces are holes onto the sky; the editor has no sky behind them to show.
shader_type spatial;
render_mode unshaded, cull_back;

uniform int mode = 0;
uniform sampler2D layer1 : source_color, filter_linear_mipmap, repeat_enable;
uniform vec2 scale1 = vec2(1.0);
uniform vec2 scroll1 = vec2(0.0);
uniform sampler2D layer2 : source_color, hint_default_black, filter_linear_mipmap, repeat_enable;
uniform vec2 scale2 = vec2(1.0);
uniform vec2 scroll2 = vec2(0.0);
uniform bool layer2_add = true;
uniform float layer2_mix = 0.5;
uniform float cloud_height = 512.0;
uniform vec3 horizon_color : source_color = vec3(0.1, 0.09, 0.1);
uniform sampler2D panorama : source_color, filter_linear, repeat_disable;
uniform float energy = 1.0;

varying vec3 world;

void vertex() {
	world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	vec3 d = normalize(world - CAMERA_POSITION_WORLD);
	vec3 c;
	if (mode == 1) {
		vec2 uv = vec2(atan(d.x, d.z) / TAU + 0.5, acos(clamp(d.y, -1.0, 1.0)) / PI);
		c = texture(panorama, uv).rgb;
	} else {
		float h = max(d.y, 0.03);
		float bend = 512.0 / max(cloud_height, 64.0);
		vec2 p = d.xz / (h + 0.25 * bend) * 0.25;
		c = texture(layer1, p * scale1 + TIME * scroll1).rgb;
		vec3 c2 = texture(layer2, p * scale2 + TIME * scroll2).rgb;
		c = layer2_add ? c + c2 : mix(c, c2, layer2_mix);
		c = mix(horizon_color, c, smoothstep(-0.02, 0.12, d.y));
	}
	ALBEDO = c * energy;
}
