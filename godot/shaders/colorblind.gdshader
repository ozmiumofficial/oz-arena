shader_type canvas_item;
// Settings > Accessibility > Colorblind mode: a daltonize filter over everything Godot draws
// (the 3D and the HUD). The screen is simulated as the chosen color deficiency sees it in LMS
// cone space; the colors lost to it are shifted into the channels that eye still tells apart.

uniform sampler2D screen : hint_screen_texture, filter_nearest;
uniform int mode = 0;   // 1 protanopia, 2 deuteranopia, 3 tritanopia

void fragment() {
	vec3 c = texture(screen, SCREEN_UV).rgb;
	float l = 17.8824 * c.r + 43.5161 * c.g + 4.11935 * c.b;
	float m = 3.45565 * c.r + 27.1554 * c.g + 3.86714 * c.b;
	float s = 0.0299566 * c.r + 0.184309 * c.g + 1.46709 * c.b;
	if (mode == 1) {
		l = 2.02344 * m - 2.52581 * s;
	} else if (mode == 2) {
		m = 0.494207 * l + 1.24827 * s;
	} else if (mode == 3) {
		s = -0.395913 * l + 0.801109 * m;
	}
	vec3 sim = vec3(
		0.0809444479 * l - 0.130504409 * m + 0.116721066 * s,
		-0.0102485335 * l + 0.0540193266 * m - 0.113614708 * s,
		-0.000365296938 * l - 0.00412161469 * m + 0.693511405 * s);
	vec3 err = c - sim;
	COLOR = vec4(clamp(c + vec3(0.0, 0.7 * err.r + err.g, 0.7 * err.r + err.b), 0.0, 1.0), 1.0);
}
