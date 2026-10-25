shader_type spatial;
// The remaster's glowing beams (rail halo, Quake Live rail, lightning sheath): quads along local +X
// (QuadMesh, size = length x width in meters), crossed about that axis like id's beams (Q3FX.glow_beam),
// with a hot core and a soft halo across the width. No texture: the falloff is computed.
render_mode unshaded, blend_add, cull_disabled, depth_draw_never, shadows_disabled, fog_disabled;

uniform vec4 color : source_color = vec4(1.0);
uniform float intensity = 1.0;
uniform float progress = 0.0;     // 0..1 over the effect's life (Q3FxTimed)
uniform float core = 0.35;        // share of the width the white-hot core fills
uniform float core_gain = 1.2;    // brightness of the core
uniform float halo_gain = 0.6;    // brightness of the halo
uniform float spread = 0.0;       // the halo widens by this many times its width over the life
uniform float fade_pow = 1.5;     // how fast it fades with progress
uniform float flicker = 0.0;      // lightning: brightness ripples running along the beam
uniform float length_m = 1.0;     // the beam's length (meters), for the ripples and the end taper
uniform float near_fade = 1.2;    // meters from the eye over which it fades in (the shooter's own beam)

varying float along;

void vertex() {
	VERTEX.y *= 1.0 + spread * progress;
	along = UV.x * length_m;
}

void fragment() {
	// 0 on the axis, 1 at the edge. Clamped: MSAA samples the edges a hair outside the quad, and
	// pow() of a negative is NaN, which bloom smeared into great white bars along the beam.
	float d = clamp(abs(UV.y * 2.0 - 1.0), 0.0, 1.0);
	float k = core / (1.0 + spread * progress);
	float c = exp(-(d * d) / max(k * k, 1e-4) * 2.5);
	float h = pow(1.0 - d, 2.2);
	float f = 1.0;
	if (flicker > 0.0) {
		f = 1.0 - flicker + flicker * (0.5 + 0.5 * sin(along * 9.0 - TIME * 47.0) * sin(along * 3.1 + TIME * 23.0));
	}
	// a short taper at the ends instead of a hard cut
	float ends = smoothstep(0.0, 0.06, along) * smoothstep(0.0, 0.06, length_m - along);
	float fade = pow(clamp(1.0 - progress, 0.0, 1.0), fade_pow) * smoothstep(0.1, near_fade, -VERTEX.z);
	// a quad seen nearly edge-on would show as a hard bright line: its share fades out
	float facing = smoothstep(0.1, 0.5, abs(dot(normalize(NORMAL), normalize(VIEW))));
	vec3 hot = mix(color.rgb, vec3(1.0), 0.65);
	ALBEDO = (hot * c * core_gain + color.rgb * h * halo_gain) * intensity * f * ends * fade * facing / 1.5;
}
