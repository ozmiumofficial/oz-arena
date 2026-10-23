shader_type spatial;
// Additive id Tech 3 world surface followed by a $lightmap filter stage (textures/base_wall/glass01:
// a reflection, then the lightmap multiplying the frame): drawn lit, so the baked light scales the
// layer the way that filter pass did, and a dark room's glass shows only a faint reflection.
render_mode blend_add, cull_disabled, depth_draw_never, shadows_disabled, specular_disabled;
#include "res://shaders/world/world_surface.gdshaderinc"

// as a next pass under an alphaFunc paint layer (glass_frame's glass): only where the paint is cut
uniform sampler2D hole_tex : hint_default_white, filter_linear_mipmap, repeat_enable;
uniform bool use_holes = false;

void fragment() {
	if (use_holes && texture(hole_tex, UV).a >= 0.5) {
		discard;
	}
	// both sides take the front's light, as Quake 3's one lightmap texel does (the baked
	// lightmap is directional: the flipped normal of a back face would read it dark)
	if (!FRONT_FACING) {
		NORMAL = -NORMAL;
	}
	vec2 env = env_uv(NORMAL, VIEW, INV_VIEW_MATRIX);
	Surface s = world_surface(env_map ? env : UV, env);
	ALBEDO = s.albedo.rgb;
	ROUGHNESS = 1.0;
	METALLIC = 0.0;
}
