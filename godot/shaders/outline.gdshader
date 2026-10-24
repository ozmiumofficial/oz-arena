shader_type spatial;
// A player's outline in the replay viewer (Q3ReplayViewer): the body drawn again, grown along its
// normals with its front faces culled, so only a rim shows around it (an inverted hull). Used as
// the body's material_overlay.
render_mode unshaded, cull_front, shadows_disabled;

uniform vec4 color : source_color = vec4(1.0, 0.68, 0.2, 1.0);
uniform float width = 0.018;

void vertex() {
	VERTEX += NORMAL * width;
}

void fragment() {
	ALBEDO = color.rgb;
}
