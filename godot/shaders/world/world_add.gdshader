shader_type spatial;
// Additive id Tech 3 world surface (blendFunc add: flames, beams, energy fields). Unshaded.
// fog_disabled: volumetric fog would add the haze color over the whole surface (see world_sprite_add)
render_mode unshaded, blend_add, cull_disabled, depth_draw_never, shadows_disabled, fog_disabled;
#include "res://shaders/world/world_surface.gdshaderinc"

// deformVertexes (q3_deform.gdshaderinc): banners wave, lamps swing
void vertex() {
	VERTEX = q3_deform(VERTEX, NORMAL, UV, (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz);
}

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
	// every stage adds at the same weight, as in Quake 3: s.emission scales the glow layers by
	// emission_energy (0.3 for a weak surfacelight), which dimmed console03's green ring to 30%;
	// s.emission_base already carries shown_energy()
	vec3 c = max(s.albedo.rgb + s.emission_base + s.glow - vec3(BLACK_FLOOR), vec3(0.0)) / (1.0 - BLACK_FLOOR);
	ALBEDO = c * max(shown_energy(), 1.0);
	ALPHA = 1.0;
}
