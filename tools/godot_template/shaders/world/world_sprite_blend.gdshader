shader_type spatial;
// Alpha-blended or alpha-tested camera-facing sprite (autosprite2 wires, banners). See world_sprite.gdshaderinc.
render_mode cull_disabled, depth_draw_opaque;
#include "res://shaders/world/world_surface.gdshaderinc"
#include "res://shaders/world/world_sprite.gdshaderinc"

void fragment() {
	Surface s = world_surface(UV, vec2(0.0));
	ALBEDO = s.albedo.rgb;
	AO = s.orm.r;
	ROUGHNESS = s.orm.g;
	METALLIC = s.orm.b;
	EMISSION = s.emission;
	ALPHA = s.albedo.a;
	if (alpha_cut > 0.0) {
		ALPHA_SCISSOR_THRESHOLD = alpha_cut;
	}
}
