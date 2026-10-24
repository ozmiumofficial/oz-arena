shader_type spatial;
// Q3Sparks' streaks: a quad stretched along the spark's motion (x) and across it (y), the
// instance color its heat (alpha fades it as it cools). Additive and bright enough to bloom.
render_mode unshaded, blend_add, cull_disabled, depth_draw_never, shadows_disabled, fog_disabled;

uniform float brightness = 3.0;

varying vec4 heat;

void vertex() {
	heat = COLOR;
}

void fragment() {
	float across = 1.0 - abs(UV.y * 2.0 - 1.0);
	// UV.x 1 is the head (where the spark is now): bright there, tapering along the tail
	float along = smoothstep(0.0, 0.7, UV.x) * (1.0 - smoothstep(0.9, 1.0, UV.x));
	float k = across * across * mix(0.35, 1.0, along) * heat.a;
	ALBEDO = heat.rgb * brightness * k;
	ALPHA = 1.0;
}
