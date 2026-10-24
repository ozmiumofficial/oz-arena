// The map editor's ground grid (Q3EditorGrid): thin lines every cell, brighter every 8 cells,
// the X and Y axes of id space in red and green, fading out with distance.
shader_type spatial;
render_mode unshaded, blend_mix, cull_disabled, depth_draw_never, shadows_disabled;

uniform float cell = 0.5;           // meters between lines
uniform vec4 minor_color : source_color = vec4(0.55, 0.52, 0.5, 0.18);
uniform vec4 major_color : source_color = vec4(0.75, 0.7, 0.65, 0.4);
uniform float fade = 60.0;          // meters to fade over

varying vec3 world;

void vertex() {
	world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

float lines(vec2 p, float size) {
	vec2 g = abs(fract(p / size - 0.5) - 0.5) / fwidth(p / size);
	return 1.0 - min(min(g.x, g.y), 1.0);
}

void fragment() {
	vec2 p = world.xz;
	float minor = lines(p, cell);
	float major = lines(p, cell * 8.0);
	vec4 c = mix(minor_color * minor, major_color, major);
	// id X runs along Godot z, so the line at Godot x = 0 is the X axis (red); id Y is green
	vec2 axis = abs(p) / fwidth(p);
	if (axis.x < 1.0) c = vec4(0.95, 0.25, 0.2, 0.7);
	if (axis.y < 1.0) c = vec4(0.25, 0.85, 0.3, 0.7);
	float d = length(world - CAMERA_POSITION_WORLD);
	c.a *= clamp(1.0 - d / (fade + cell * 40.0), 0.0, 1.0);
	ALBEDO = c.rgb;
	ALPHA = c.a;
}
