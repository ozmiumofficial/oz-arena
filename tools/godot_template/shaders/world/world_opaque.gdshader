shader_type spatial;
// Opaque id Tech 3 world surface (alphaFunc via alpha_cut). See world_surface.gdshaderinc.
render_mode cull_back;
#include "res://shaders/world/world_surface.gdshaderinc"

void fragment() {
	vec2 env = env_uv(NORMAL, VIEW, INV_VIEW_MATRIX);
	Surface s = world_surface(env_map ? env : UV, env);
	ALBEDO = s.albedo.rgb;
	AO = s.orm.r;
	AO_LIGHT_AFFECT = 0.4;
	ROUGHNESS = s.orm.g;
	METALLIC = s.orm.b;
	NORMAL_MAP = s.normal;
	NORMAL_MAP_DEPTH = normal_strength;
	EMISSION = s.emission;
	if (alpha_cut > 0.0) {
		ALPHA = s.albedo.a;
		ALPHA_SCISSOR_THRESHOLD = alpha_cut;
	}
}
