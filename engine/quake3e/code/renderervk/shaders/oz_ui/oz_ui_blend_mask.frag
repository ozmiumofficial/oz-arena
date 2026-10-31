#version 450

layout(set = 0, binding = 0) uniform sampler2D tex;
layout(set = 1, binding = 0) uniform sampler2D texMask;

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 finalColor;

void main() {
	vec4 texColor = texture(tex, fragTexCoord);
	float maskAlpha = texture(texMask, fragTexCoord).a;
	finalColor = texColor * maskAlpha;
}
