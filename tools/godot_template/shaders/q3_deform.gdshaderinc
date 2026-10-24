// id Tech 3 deformVertexes as vertex motion (tr_shade_calc.c RB_DeformTessGeometry), for the world
// shaders and the model stage shaders: banners and flags wave, lamps swing, spines bulge.
// Up to two deforms, in shader order. Each is
//   deformN_a = (kind, spread or width, base or height, amplitude or speed)
//   deformN_b = (phase, frequency, waveform 1 sin .. 5 inversesawtooth, 0)
//   deformN_move = the move vector in id axes (x forward, y left, z up)
// kind 1 wave: along the normal by a waveform whose phase shifts with x + y + z / div
// kind 2 move: the whole surface along the vector, by a waveform
// kind 3 bulge: along the normal by sin(s * width + time * speed) * height
// Positions and amounts are id units: one unit is 1/32 m, and Godot's axes are id's (y, z, x).
// deformVertexes normal only shimmers the lighting and isn't drawn.

uniform vec4 deform1_a = vec4(0.0);
uniform vec4 deform1_b = vec4(0.0);
uniform vec3 deform1_move = vec3(0.0);
uniform vec4 deform2_a = vec4(0.0);
uniform vec4 deform2_b = vec4(0.0);
uniform vec3 deform2_move = vec3(0.0);

// EvalWaveForm without the clamp the color waves have: deforms swing both ways
float q3_deform_wave(float func, float base, float amp, float phase, float freq) {
	float t = fract(phase + TIME * freq);
	float f = 0.0;
	if (func < 1.5) f = sin(t * TAU);
	else if (func < 2.5) f = 1.0 - 4.0 * abs(fract(t + 0.25) - 0.5);   // 0, 1, 0, -1 as id's table
	else if (func < 3.5) f = t < 0.5 ? 1.0 : -1.0;
	else if (func < 4.5) f = t;
	else f = 1.0 - t;
	return base + amp * f;
}

vec3 q3_deform_one(vec3 v, vec3 n, vec2 uv, vec3 world, vec4 a, vec4 b, vec3 move) {
	if (a.x < 0.5) {
		return v;
	}
	if (a.x < 1.5) {
		float off = (world.x + world.y + world.z) * 32.0 * a.y;
		return v + n * q3_deform_wave(b.z, a.z, a.w, b.x + off, b.y) / 32.0;
	}
	if (a.x < 2.5) {
		return v + vec3(move.y, move.z, move.x) * q3_deform_wave(b.z, a.z, a.w, b.x, b.y) / 32.0;
	}
	return v + n * sin(uv.x * a.y + TIME * a.w) * a.z / 32.0;
}

// VERTEX in model space; world is the position the wave's phase follows: world space for map
// surfaces (id's space there), model space for models
vec3 q3_deform(vec3 v, vec3 n, vec2 uv, vec3 world) {
	v = q3_deform_one(v, n, uv, world, deform1_a, deform1_b, deform1_move);
	return q3_deform_one(v, n, uv, world, deform2_a, deform2_b, deform2_move);
}
