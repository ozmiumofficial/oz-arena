#version 450

layout(push_constant) uniform Push {
	vec2 uvOffset;
	vec2 uvScale;
	vec2 texCoordMin;
	vec2 texCoordMax;
	vec4 color;
} pc;

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 finalColor;

void main() {
	vec2 inRegion = step(pc.texCoordMin, fragTexCoord) * step(fragTexCoord, pc.texCoordMax);
	finalColor = texture(tex, fragTexCoord).a * inRegion.x * inRegion.y * pc.color;
}
