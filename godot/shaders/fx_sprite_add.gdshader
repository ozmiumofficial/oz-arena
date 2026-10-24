shader_type spatial;
// Additive flipbook (explosions, plasma, sparks): id Tech 3 "blendfunc add".
render_mode unshaded, blend_add, cull_disabled, depth_draw_never, shadows_disabled, fog_disabled;
#include "res://shaders/fx_sprite.gdshaderinc"

void fragment() {
	// models lying on a wall (bullet and rail flashes) aren't billboards and stay hard
	float soft = billboard ? soft_fade(SCREEN_UV, VERTEX.z, INV_PROJECTION_MATRIX) : 1.0;
	ALBEDO = flipbook(UV).rgb * tint.rgb * intensity * soft;
	ALPHA = fade_out();
}
