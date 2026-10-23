// Shared by fx_sprite_add / fx_sprite_mix: a camera-facing flipbook quad.
// The texture is a horizontal strip of `frames` frames (tools/make_fx.py); progress 0..1
// runs through them with a cross-fade, like id Tech 3's animmap, and fades out at the end.
uniform sampler2D tex : source_color, filter_linear_mipmap, repeat_disable;
uniform int frames = 1;
uniform float progress = 0.0;
uniform vec4 tint : source_color = vec4(1.0);
uniform float intensity = 1.0;
uniform float fade_start = 0.55;
uniform float size = 1.0;
uniform float spin = 0.0;
uniform float spin_rate = 0.0;     // radians per second (tcMod rotate)
uniform float grow_from = 1.0;     // size at progress 0 (CG_AddSpriteExplosion grows 30 -> 72 units)
uniform bool billboard = true;
uniform float uv_stretch = 1.0;    // tcMod stretch: the texture drawn at this fraction of its size, centered
uniform float uv_rotate = 0.0;     // tcMod rotate, radians per second about the texture's center

void vertex() {
	float a = spin + TIME * spin_rate;
	float c = cos(a), s = sin(a);
	VERTEX.xy = mat2(vec2(c, s), vec2(-s, c)) * VERTEX.xy * size * mix(grow_from, 1.0, progress);
	if (billboard) {
		MODELVIEW_MATRIX = VIEW_MATRIX * mat4(INV_VIEW_MATRIX[0], INV_VIEW_MATRIX[1], INV_VIEW_MATRIX[2], MODEL_MATRIX[3]);
	}
}

vec4 flipbook(vec2 uv) {
	vec2 c = uv - 0.5;
	float r = TIME * uv_rotate;
	c = mat2(vec2(cos(r), sin(r)), vec2(-sin(r), cos(r))) * c;
	uv = clamp(c / uv_stretch + 0.5, 0.0, 1.0);
	float f = clamp(progress, 0.0, 0.9999) * float(frames);
	float i0 = floor(f);
	float i1 = min(i0 + 1.0, float(frames) - 1.0);
	vec4 c0 = texture(tex, vec2((uv.x + i0) / float(frames), uv.y));
	vec4 c1 = texture(tex, vec2((uv.x + i1) / float(frames), uv.y));
	return mix(c0, c1, f - i0);
}

float fade_out() {
	return 1.0 - smoothstep(fade_start, 1.0, progress);
}
