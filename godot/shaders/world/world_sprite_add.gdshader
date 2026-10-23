shader_type spatial;
// Additive camera-facing sprite (autosprite flares, flames). See world_sprite.gdshaderinc.
render_mode unshaded, blend_add, cull_disabled, depth_draw_never, shadows_disabled;
#include "res://shaders/world/world_surface.gdshaderinc"

const float BLACK_FLOOR = 0.0045;   // linear, about 18/255 sRGB
#include "res://shaders/world/world_sprite.gdshaderinc"

void fragment() {
	Surface s = world_surface(UV, vec2(0.0));
	// The PBR remaster lifted pure black to a faint grain (about 10/255); additively that draws the
	// quad's square edges, which Quake 3's art kept at 0. Anything that dark adds nothing.
	vec3 c = max(s.albedo.rgb + s.emission - vec3(BLACK_FLOOR), vec3(0.0)) / (1.0 - BLACK_FLOOR);
	// flares (autosprite) are round glows: fade to nothing before the quad's edge, so its square
	// never shows, whatever grain the art has near its border
	if (sprite_mode == 1) {
		c *= 1.0 - smoothstep(0.3, 0.5, length(UV - vec2(0.5)));
	}
	ALBEDO = c * max(emission_energy, 1.0);
	ALPHA = 1.0;
}
