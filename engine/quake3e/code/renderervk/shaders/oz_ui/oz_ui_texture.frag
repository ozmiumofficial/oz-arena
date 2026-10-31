#version 450

// the engine's images aren't premultiplied (straight 1), RmlUi's own textures are

layout(push_constant) uniform Push {
	mat4 transform;
	vec2 translate;
	float straight;
} pc;

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 finalColor;

void main() {
	vec4 texColor = texture(tex, fragTexCoord);
	texColor.rgb *= mix(1.0, texColor.a, pc.straight);
	finalColor = fragColor * texColor;
}
