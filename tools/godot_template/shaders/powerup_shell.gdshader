shader_type spatial;
// Powerup shells (quad, battle suit, regen, invisibility) as an overlay material.
// id Tech 3 draws them with deformVertexes wave (push along the normal), tcGen environment
// (UV from the view-space normal), tcMod rotate + scroll, blendfunc add.
render_mode unshaded, blend_add, cull_back, depth_draw_never, shadows_disabled;

uniform sampler2D tex : source_color, filter_linear_mipmap, repeat_enable;
uniform float grow = 0.09;          // meters (3 units for bodies, 0.5 for weapons)
uniform float rotate_deg = 30.0;    // degrees per second
uniform vec2 scroll = vec2(1.0, 0.1);
uniform float intensity = 1.0;

void vertex() {
	VERTEX += NORMAL * grow;
}

void fragment() {
	vec2 uv = NORMAL.xy * 0.5 + 0.5;
	float a = radians(rotate_deg) * TIME;
	uv = mat2(vec2(cos(a), sin(a)), vec2(-sin(a), cos(a))) * (uv - 0.5) + 0.5 + scroll * TIME;
	ALBEDO = texture(tex, uv).rgb * intensity;
	ALPHA = 1.0;
}
