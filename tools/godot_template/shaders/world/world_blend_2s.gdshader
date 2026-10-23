shader_type spatial;
// Alpha-blended id Tech 3 world surface (blendFunc blend: glass, grates, decals).
render_mode blend_mix, depth_draw_opaque, cull_disabled;
#include "res://shaders/world/world_surface.gdshaderinc"

void fragment() {
	// both sides take the front's light, as Quake 3's one lightmap texel does (the baked
	// lightmap is directional: the flipped normal of a back face would read it dark)
	if (!FRONT_FACING) {
		NORMAL = -NORMAL;
	}
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
