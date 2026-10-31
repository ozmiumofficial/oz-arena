#version 450

// opacity stands in for the GL3 backend's constant blend color

layout(push_constant) uniform Push {
	vec2 uvOffset;
	vec2 uvScale;
	float opacity;
} pc;

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 finalColor;

void main() {
	finalColor = texture(tex, fragTexCoord) * pc.opacity;
}
