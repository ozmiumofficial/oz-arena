shader_type sky;
// id Tech 3 skybox (skyParms farbox env/<name>_rt/bk/lf/ft/up/dn) resampled by
// tools/make_godot_world.py into one equirectangular panorama with this exact mapping:
// u = atan(dir.x, dir.z) / TAU + 0.5, v = acos(dir.y) / PI.
uniform sampler2D panorama : source_color, filter_linear, repeat_disable;
uniform float energy = 1.0;

void sky() {
	vec3 d = EYEDIR;
	vec2 uv = vec2(atan(d.x, d.z) / TAU + 0.5, acos(clamp(d.y, -1.0, 1.0)) / PI);
	COLOR = texture(panorama, uv).rgb * energy;
}
