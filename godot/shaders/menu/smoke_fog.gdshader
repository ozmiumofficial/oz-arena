shader_type fog;
// Drifting smoke for the menu showcase: two octaves of scrolling 3D noise, thinned toward the
// volume's edges. `burst` thickens it for the fade between subjects.

uniform sampler3D noise_tex : repeat_enable;
uniform float density : hint_range(0.0, 4.0) = 0.3;
uniform float burst : hint_range(0.0, 3.0) = 0.0;
uniform float scale = 0.16;
uniform vec3 drift = vec3(0.035, 0.05, 0.02);
uniform vec3 albedo : source_color = vec3(0.4, 0.38, 0.37);
uniform vec3 emission : source_color = vec3(0.0);

void fog() {
	vec3 p = WORLD_POSITION * scale + drift * TIME;
	float a = texture(noise_tex, p).r;
	float b = texture(noise_tex, p * 2.7 + vec3(0.0, TIME * 0.03, 0.0)).r;
	float smoke = smoothstep(0.42, 0.85, a * 0.72 + b * 0.38);
	vec3 c = abs(UVW * 2.0 - 1.0);
	float edge = 1.0 - smoothstep(0.55, 1.0, max(max(c.x, c.y * 0.9), c.z));
	DENSITY = (smoke * density + burst * (0.35 + smoke)) * edge;
	ALBEDO = albedo;
	EMISSION = emission;
}
