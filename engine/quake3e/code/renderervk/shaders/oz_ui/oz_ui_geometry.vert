#version 450

// Oz: the native UI's geometry (RmlUi's vertexes), as the oz renderer's uiVertMain (renderer2/tr_oz_ui.c)

layout(push_constant) uniform Push {
	mat4 transform;
	vec2 translate;
	float straight;
} pc;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor0;
layout(location = 2) in vec2 inTexCoord0;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;

out gl_PerVertex {
	vec4 gl_Position;
};

void main() {
	fragTexCoord = inTexCoord0;
	fragColor = inColor0;
	gl_Position = pc.transform * vec4(inPosition + pc.translate, 0.0, 1.0);
	gl_Position.z = 0.5 * (gl_Position.z + gl_Position.w);	// GL's clip depth, -w to w, to Vulkan's 0 to w
}
