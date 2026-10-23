shader_type spatial;
// Alpha-blended id Tech 3 world surface (blendFunc blend: glass, grates, decals).
render_mode blend_mix, depth_draw_opaque, cull_back;
#include "res://shaders/world/world_surface.gdshaderinc"

void fragment() {
	vec2 env = env_uv(NORMAL, VIEW, INV_VIEW_MATRIX);
	Surface s = world_surface(env_map ? env : UV, env);
	ALBEDO = s.albedo.rgb;
	ALPHA = s.albedo.a;
	ROUGHNESS = s.orm.g;
	METALLIC = s.orm.b;
	NORMAL_MAP = s.normal;
	NORMAL_MAP_DEPTH = normal_strength;
	EMISSION = s.emission;
}
