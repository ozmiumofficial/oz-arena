shader_type spatial;
// The armor shell: a glowing rim over a player's body for a moment whenever armor takes part of a
// hit (the "armorhit" server command, tools/q3core/patches/0002). Color: the armor color, red when
// armor is low; Q3Client tweens `flash` from 1 to 0. Drawn as a material_overlay, pushed out along
// the normals so it sits just outside the body.
render_mode unshaded, blend_add, depth_draw_never, cull_back, shadows_disabled, fog_disabled;

uniform vec4 color : source_color = vec4(1.0, 0.8, 0.3, 1.0);
uniform float flash = 0.0;
uniform float grow = 0.03;   // meters

void vertex() {
	VERTEX += NORMAL * grow;
}

void fragment() {
	float rim = pow(1.0 - clamp(dot(NORMAL, VIEW), 0.0, 1.0), 2.0);
	float bands = 0.75 + 0.25 * sin(TIME * 18.0 + UV.y * 40.0);
	ALBEDO = color.rgb * (0.25 + rim * 1.6) * bands * flash * 2.0;
}
