shader_type spatial;
// Heat shimmer (the remaster's; Quake 3 had none): a camera-facing disc that bends the picture
// behind it with rising noise, strongest at its middle. Behind explosions and rocket exhausts.
// progress 0..1 (Q3FxTimed) swells it in fast and fades it out; a missile's is steady.
// It draws the opaque scene's copy, so Q3FX.heat gives it a render priority below every other
// transparent; a nearer object under the bent sample (the gun, a player) isn't smeared in.
render_mode unshaded, blend_mix, cull_disabled, depth_draw_never, shadows_disabled, fog_disabled;

uniform sampler2D screen_tex : hint_screen_texture, filter_linear_mipmap, repeat_disable;
uniform sampler2D depth_tex : hint_depth_texture, repeat_disable, filter_nearest;
uniform float progress = 0.0;
uniform bool steady = false;       // no life fade (a missile's exhaust)
uniform float strength = 0.03;     // largest bend in screen heights, for a disc 1 m away
uniform float rise = 1.6;          // how fast the ripples climb (noise cells per second)
uniform float cells = 5.0;         // noise cells across the disc
uniform float soft_distance = 0.5; // meters over which it fades out where it meets a surface

float hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float vnoise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

void vertex() {
	MODELVIEW_MATRIX = VIEW_MATRIX * mat4(INV_VIEW_MATRIX[0], INV_VIEW_MATRIX[1], INV_VIEW_MATRIX[2], MODEL_MATRIX[3]);
}

void fragment() {
	float r = length(UV - 0.5) * 2.0;
	float mask = 1.0 - smoothstep(0.25, 1.0, r);
	float life = steady ? 1.0 : smoothstep(0.0, 0.08, progress) * (1.0 - smoothstep(0.3, 1.0, progress));
	vec4 view = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, texture(depth_tex, SCREEN_UV).r, 1.0);
	float soft = clamp((VERTEX.z - view.z / view.w) / soft_distance, 0.0, 1.0);
	vec2 p = UV * cells + vec2(0.0, TIME * rise);
	vec2 n = vec2(vnoise(p), vnoise(p + vec2(17.3, 5.1))) - 0.5;
	float k = mask * life * soft;
	vec2 uv = SCREEN_UV + n * strength * k / max(-VERTEX.z, 0.5);
	vec4 bent = INV_PROJECTION_MATRIX * vec4(uv * 2.0 - 1.0, texture(depth_tex, uv).r, 1.0);
	if (bent.z / bent.w > VERTEX.z) {
		uv = SCREEN_UV;   // the bent sample is in front of the disc
	}
	ALBEDO = texture(screen_tex, uv).rgb;
	ALPHA = k;
}
