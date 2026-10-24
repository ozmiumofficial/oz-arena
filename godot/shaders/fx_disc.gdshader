shader_type spatial;
// Rail-trail discs drawn as one MultiMesh: each instance is a camera-facing sprite whose
// fade comes from its custom data (x = 0..1 life), rotating like railDisc's tcMod rotate -30.
render_mode unshaded, blend_add, cull_disabled, depth_draw_never, shadows_disabled, fog_disabled;

uniform sampler2D tex : source_color, filter_linear_mipmap, repeat_disable;
uniform vec4 color : source_color = vec4(1.0);
uniform float intensity = 1.0;
varying float life;

void vertex() {
	life = INSTANCE_CUSTOM.x;
	float a = radians(-30.0) * TIME + INSTANCE_CUSTOM.y;
	VERTEX.xy = mat2(vec2(cos(a), sin(a)), vec2(-sin(a), cos(a))) * VERTEX.xy;
	MODELVIEW_MATRIX = VIEW_MATRIX * mat4(INV_VIEW_MATRIX[0], INV_VIEW_MATRIX[1], INV_VIEW_MATRIX[2], MODEL_MATRIX[3]);
}

void fragment() {
	ALBEDO = texture(tex, UV).rgb * color.rgb * intensity;
	ALPHA = clamp(1.0 - life, 0.0, 1.0);
}
