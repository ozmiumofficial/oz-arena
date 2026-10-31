#version 450

layout(push_constant) uniform Push {
	vec2 uvOffset;
	vec2 uvScale;
	mat4 colorMatrix;
} pc;

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 finalColor;

void main() {
	vec4 texColor = texture(tex, fragTexCoord);
	vec3 transformedColor = vec3(pc.colorMatrix * texColor);
	finalColor = vec4(transformedColor, texColor.a);
}
