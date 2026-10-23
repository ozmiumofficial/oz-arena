shader_type spatial;
// cgame's local-entity sprites (smoke puffs, shotgun smoke, bubbles), many per draw: a MultiMesh
// whose instances carry INSTANCE_CUSTOM = (alpha, radius in meters, rotation in radians) and their
// tint as the instance color. id Tech 3 "blendfunc blend" with rgbGen / alphaGen entity.
render_mode unshaded, blend_mix, cull_disabled, depth_draw_never, shadows_disabled;

uniform sampler2D tex : source_color, filter_linear_mipmap, repeat_disable;

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
	ALBEDO = c.rgb * tint;
	ALPHA = c.a * alpha;
}
