shader_type spatial;
// fx_puff.gdshader, added instead of blended: glowing trails (the remaster's rocket fire, plasma and
// BFG trails). Same instance data: INSTANCE_CUSTOM = (alpha, radius in meters, rotation), tint as color.
render_mode unshaded, blend_add, cull_disabled, depth_draw_never, shadows_disabled, fog_disabled;

uniform sampler2D tex : source_color, filter_linear_mipmap, repeat_disable;
uniform float soft_distance = 0.3;
uniform float intensity = 1.0;
uniform sampler2D depth_tex : hint_depth_texture, repeat_disable, filter_nearest;

varying float alpha;
varying vec3 tint;

void vertex() {
	float c = cos(INSTANCE_CUSTOM.b), s = sin(INSTANCE_CUSTOM.b);
	VERTEX.xy = mat2(vec2(c, s), vec2(-s, c)) * VERTEX.xy * INSTANCE_CUSTOM.g * 2.0;
	MODELVIEW_MATRIX = VIEW_MATRIX * mat4(INV_VIEW_MATRIX[0], INV_VIEW_MATRIX[1], INV_VIEW_MATRIX[2], MODEL_MATRIX[3]);
	alpha = INSTANCE_CUSTOM.r;
	tint = COLOR.rgb;
}

void fragment() {
	vec4 c = texture(tex, UV);
	vec4 view = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, texture(depth_tex, SCREEN_UV).r, 1.0);
	float soft = clamp((VERTEX.z - view.z / view.w) / soft_distance, 0.0, 1.0);
	// faded near the eye: the shooter's own trail starts at the gun and would glare over the view
	float near = smoothstep(0.5, 3.0, -VERTEX.z);
	ALBEDO = c.rgb * c.a * tint * alpha * soft * near * intensity;
}
