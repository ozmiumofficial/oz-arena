shader_type spatial;
// A mirror or portal surface (Q3Portal): the view through it, then its Quake 3 shader's stages
// (r_overBrightBits 1 taken into account: identityLighting stages show at their own value, the
// rgbGen wave add at twice it). The view stays linear HDR, so its highlights keep their glow.
//   look 0  textures/common/mirror2: mirror1 (premultiplied, clear), then sfx/mirror (1 - src color)
//   look 1  textures/sfx/bugmirror: mirror1, then mirrorkc added
//   look 2  textures/sfx/portal_sfx: portal_sfx3 blended, portal_sfx1 multiplied (tcMod rotate 360),
//           portal_sfx added (rgbGen wave inversesawtooth 0 .5 .2 .5), portalfog by distance
//           (alphaGen portal); deformVertexes wave 100 sin 0 2 0 .5
render_mode unshaded, cull_back, depth_draw_opaque;

uniform sampler2D view_tex : filter_linear, repeat_disable;   // linear HDR (Q3Portal's SubViewport)
uniform bool has_view = false;
uniform int look = 0;
uniform vec3 s_origin;          // the surface frame: the image maps onto it by position
uniform vec3 s_u;
uniform vec3 s_v;
uniform vec4 window;            // rectangle center (u, v) and size, meters
uniform float flip_u = 1.0;
uniform float portal_range = 8.0;
uniform sampler2D stage0 : filter_linear_mipmap, repeat_enable;
uniform sampler2D stage1 : filter_linear_mipmap, repeat_enable;
uniform sampler2D stage2 : filter_linear_mipmap, repeat_enable;
uniform sampler2D stage3 : filter_linear_mipmap, repeat_enable;

varying vec3 world_pos;

vec3 to_linear(vec3 c) {
	return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

void vertex() {
	if (look == 2) {
		// deformVertexes wave 100 sin 0 2 0 .5: 2 units along the normal, spread by position
		vec3 p = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz * 32.0;
		VERTEX += NORMAL * 2.0 * sin(TAU * ((p.x + p.y + p.z) / 100.0 + TIME * 0.5)) / 32.0;
	}
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	vec3 rel = world_pos - s_origin;
	vec2 uv = vec2(0.5 + flip_u * (dot(rel, s_u) - window.x) / window.z,
		0.5 - (dot(rel, s_v) - window.y) / window.w);
	vec3 view = has_view ? texture(view_tex, clamp(uv, 0.0, 1.0)).rgb : vec3(0.0);
	vec3 c = view;
	if (look == 0) {
		vec4 t0 = texture(stage0, UV);
		c = to_linear(t0.rgb) + c * (1.0 - t0.a);
		c *= to_linear(1.0 - texture(stage1, UV).rgb);
	} else if (look == 1) {
		vec4 t0 = texture(stage0, UV);
		c = to_linear(t0.rgb) + c * (1.0 - t0.a);
		c += to_linear(texture(stage1, UV).rgb);
	} else {
		vec4 t0 = texture(stage0, UV);
		c = mix(c, to_linear(t0.rgb), t0.a);
		// tcMod rotate 360 (RB_CalcRotateTexCoords)
		float a = radians(-360.0 * TIME);
		vec2 r = vec2(UV.x * cos(a) - UV.y * sin(a) + 0.5 - 0.5 * cos(a) + 0.5 * sin(a),
			UV.x * sin(a) + UV.y * cos(a) + 0.5 - 0.5 * sin(a) - 0.5 * cos(a));
		c *= to_linear(texture(stage1, r).rgb);
		c += 2.0 * 0.5 * (1.0 - fract(0.2 + TIME * 0.5)) * to_linear(texture(stage2, UV).rgb);
		// alphaGen portal: fogged by distance, fully at portalRange
		float fog = clamp(distance(world_pos, CAMERA_POSITION_WORLD) / portal_range, 0.0, 1.0);
		c = mix(c, to_linear(texture(stage3, UV).rgb), fog);
	}
	ALBEDO = c;
}
