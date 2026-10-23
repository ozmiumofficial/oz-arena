shader_type spatial;
// Alpha-blended flipbook (blood, rail rings, smoke): id Tech 3 "blendfunc blend".
render_mode unshaded, blend_mix, cull_disabled, depth_draw_never, shadows_disabled;
#include "res://shaders/fx_sprite.gdshaderinc"

void fragment() {
	vec4 c = flipbook(UV);
	ALBEDO = c.rgb * tint.rgb * intensity;
	ALPHA = c.a * tint.a * fade_out();
}
