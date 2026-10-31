#version 450

// RmlUi's linear, radial and conic gradients; the parameters in a uniform buffer (too many for push constants)

#define LINEAR 0
#define RADIAL 1
#define CONIC 2
#define REPEATING_LINEAR 3
#define REPEATING_RADIAL 4
#define REPEATING_CONIC 5
#define PI 3.14159265
#define MAX_NUM_STOPS 16

layout(set = 2, binding = 0) uniform Gradient {
	int func;
	int numStops;
	vec2 p;
	vec2 v;
	vec4 stopColors[MAX_NUM_STOPS];
	vec4 stopPositions[MAX_NUM_STOPS / 4];	// four to a vec4
} g;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 finalColor;

float stopPosition(int i) {
	return g.stopPositions[i / 4][i % 4];
}

vec4 mixStopColors(float t) {
	vec4 color = g.stopColors[0];
	for (int i = 1; i < g.numStops; i++)
		color = mix(color, g.stopColors[i], smoothstep(stopPosition(i - 1), stopPosition(i), t));
	return color;
}

void main() {
	float t = 0.0;
	if (g.func == LINEAR || g.func == REPEATING_LINEAR) {
		float distSquare = dot(g.v, g.v);
		vec2 V = fragTexCoord - g.p;
		t = dot(g.v, V) / distSquare;
	} else if (g.func == RADIAL || g.func == REPEATING_RADIAL) {
		vec2 V = fragTexCoord - g.p;
		t = length(g.v * V);
	} else if (g.func == CONIC || g.func == REPEATING_CONIC) {
		mat2 R = mat2(g.v.x, -g.v.y, g.v.y, g.v.x);
		vec2 V = R * (fragTexCoord - g.p);
		t = 0.5 + atan(-V.x, V.y) / (2.0 * PI);
	}
	if (g.func == REPEATING_LINEAR || g.func == REPEATING_RADIAL || g.func == REPEATING_CONIC) {
		float t0 = stopPosition(0);
		float t1 = stopPosition(g.numStops - 1);
		t = t0 + mod(t - t0, t1 - t0);
	}
	finalColor = fragColor * mixStopColors(t);
}
