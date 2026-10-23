shader_type spatial;
// Additive flipbook (explosions, plasma, sparks): id Tech 3 "blendfunc add".
render_mode unshaded, blend_add, cull_disabled, depth_draw_never, shadows_disabled;
#include "res://shaders/fx_sprite.gdshaderinc"

void fragment() {
	ALBEDO = flipbook(UV).rgb * tint.rgb * intensity;
	ALPHA = fade_out();
}
