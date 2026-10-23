shader_type spatial;
// A fragged body burning away (Q3Character.disintegrate): the character's own PBR material
// (albedo, OpenGL normal map, roughness and metal from the ORM texture's G and B, emission,
// cut-out alpha) eaten by 3D noise in world space as `dissolve` runs 0 -> 1, with a glowing
// ember edge where it burns.
render_mode cull_disabled, depth_draw_opaque;

uniform sampler2D albedo_tex : source_color, filter_linear_mipmap, repeat_enable;
uniform vec4 albedo_color : source_color = vec4(1.0);
uniform sampler2D normal_tex : hint_normal, filter_linear_mipmap, repeat_enable;
uniform bool use_normal = false;
uniform sampler2D orm_tex : hint_default_white, filter_linear_mipmap, repeat_enable;
uniform bool use_orm = false;
uniform float roughness = 0.7;
uniform float metallic = 0.0;
uniform sampler2D emission_tex : source_color, hint_default_black, filter_linear_mipmap, repeat_enable;
uniform vec3 emission_color : source_color = vec3(0.0);
uniform float emission_energy = 1.0;
uniform float alpha_scissor = -1.0;      // cut-out surfaces (feathers, wings); < 0: opaque
uniform float dissolve = 0.0;            // 0 whole, 1 gone
uniform vec3 edge_color : source_color = vec3(1.0, 0.42, 0.08);
uniform float edge_width = 0.05;
uniform float noise_scale = 7.0;         // per meter

varying vec3 world_pos;

float hash(vec3 p) {
	p = fract(p * 0.3183099 + 0.1);
	p *= 17.0;
	return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float noise(vec3 x) {
	vec3 i = floor(x);
	vec3 f = fract(x);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(mix(hash(i + vec3(0, 0, 0)), hash(i + vec3(1, 0, 0)), f.x),
			mix(hash(i + vec3(0, 1, 0)), hash(i + vec3(1, 1, 0)), f.x), f.y),
		mix(mix(hash(i + vec3(0, 0, 1)), hash(i + vec3(1, 0, 1)), f.x),
			mix(hash(i + vec3(0, 1, 1)), hash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

void vertex() {
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	vec4 a = texture(albedo_tex, UV) * albedo_color;
	if (alpha_scissor >= 0.0 && a.a < alpha_scissor) {
		discard;
	}
	// two octaves: big burning patches with ragged edges
	float n = noise(world_pos * noise_scale) * 0.65 + noise(world_pos * noise_scale * 3.1) * 0.35;
	float cut = dissolve * (1.0 + edge_width) - edge_width;
	if (n < cut) {
		discard;
	}
	ALBEDO = a.rgb;
	if (use_normal) {
		NORMAL_MAP = texture(normal_tex, UV).rgb;
	}
	vec4 orm = use_orm ? texture(orm_tex, UV) : vec4(1.0);
	ROUGHNESS = use_orm ? orm.g * roughness : roughness;
	METALLIC = use_orm ? orm.b * metallic : metallic;
	vec3 glow = texture(emission_tex, UV).rgb * emission_color * emission_energy;
	// the burning rim, hottest at the cut
	float rim = 1.0 - smoothstep(cut, cut + edge_width, n);
	if (dissolve > 0.0) {
		rim = rim * rim;
		glow += edge_color * rim * 2.2;
		ALBEDO = mix(ALBEDO, vec3(0.05, 0.03, 0.02), rim);   // charred at the burning edge
	}
	EMISSION = glow;
}
