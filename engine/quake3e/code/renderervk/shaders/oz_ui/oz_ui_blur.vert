#version 450

// RmlUi's separable 7-tap Gaussian: the taps' coordinates (renderer2/tr_oz_ui.c uiVertBlur)

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

layout(location = 0) out vec2 fragTexCoord[BLUR_SIZE];

out gl_PerVertex {
	vec4 gl_Position;
};

void main() {
	vec2 uv = vec2(gl_VertexIndex & 1, gl_VertexIndex >> 1);
	for (int i = 0; i < BLUR_SIZE; i++)
		fragTexCoord[i] = uv - float(i - BLUR_NUM_WEIGHTS + 1) * pc.texelOffset;
	gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
