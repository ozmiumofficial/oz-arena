shader_type spatial;
// Additive id Tech 3 world surface (blendFunc add: flames, beams, energy fields). Unshaded.
render_mode unshaded, blend_add, cull_disabled, depth_draw_never, shadows_disabled;
#include "res://shaders/world/world_surface.gdshaderinc"

// as a next pass under an alphaFunc paint layer (glass_frame's glass): only where the paint is cut
uniform sampler2D hole_tex : hint_default_white, filter_linear_mipmap, repeat_enable;
uniform bool use_holes = false;

const float BLACK_FLOOR = 0.0045;   // linear, about 18/255 sRGB

void fragment() {
	if (use_holes && texture(hole_tex, UV).a >= 0.5) {
		discard;
	}
	vec2 env = env_uv(NORMAL, VIEW, INV_VIEW_MATRIX);
	Surface s = world_surface(env_map ? env : UV, env);
	// The PBR remaster lifted pure black to a faint grain (about 10/255); additively that draws the
	// quad's square edges, which Quake 3's art kept at 0. Anything that dark adds nothing.
	vec3 c = max(s.albedo.rgb + s.emission - vec3(BLACK_FLOOR), vec3(0.0)) / (1.0 - BLACK_FLOOR);
	ALBEDO = c * max(emission_energy, 1.0);
	ALPHA = 1.0;
}
