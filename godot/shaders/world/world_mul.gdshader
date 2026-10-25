shader_type spatial;
// Multiplying id Tech 3 world surface: decals drawn with blendFunc filter (GL_DST_COLOR GL_ZERO) and
// no $lightmap stage (painted shadows under jump pads and weapon spots), or with GL_ZERO
// GL_ONE_MINUS_SRC_COLOR (grunge and dirt: darkens by the inverted image). The texture is the
// original image (make_godot_world.py), not the PBR albedo, which moves painted shading into AO.
render_mode unshaded, blend_mul, cull_disabled, depth_draw_never, shadows_disabled, fog_disabled;
#include "res://shaders/world/world_surface.gdshaderinc"

uniform bool invert = false;   // GL_ZERO GL_ONE_MINUS_SRC_COLOR

void vertex() {
	VERTEX = q3_deform(VERTEX, NORMAL, UV, (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz);
}

void fragment() {
	vec2 env = env_uv(NORMAL, VIEW, INV_VIEW_MATRIX);
	Surface s = world_surface(UV, env);
	ALBEDO = invert ? vec3(1.0) - s.albedo.rgb : s.albedo.rgb;
}
