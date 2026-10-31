#version 450

#define BLUR_SIZE 7
#define BLUR_NUM_WEIGHTS 4

layout(push_constant) uniform Push {
	vec2 uvOffset;
	vec2 uvScale;
	vec2 texCoordMin;
	vec2 texCoordMax;
	vec2 texelOffset;
	vec2 pad;
	vec4 weights;
} pc;

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 fragTexCoord[BLUR_SIZE];

layout(location = 0) out vec4 finalColor;

void main() {
	vec4 color = vec4(0.0, 0.0, 0.0, 0.0);
	for (int i = 0; i < BLUR_SIZE; i++) {
		vec2 inRegion = step(pc.texCoordMin, fragTexCoord[i]) * step(fragTexCoord[i], pc.texCoordMax);
		color += texture(tex, fragTexCoord[i]) * inRegion.x * inRegion.y * pc.weights[abs(i - BLUR_NUM_WEIGHTS + 1)];
	}
	finalColor = color;
}
