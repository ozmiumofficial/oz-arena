#version 450

// a full-screen pass, four vertexes of a strip from gl_VertexIndex; uvOffset and uvScale stand in for
// the GL3 backend's per-call quads (renderer2/tr_oz_ui.c uiVertPassthrough)

layout(push_constant) uniform Push {
	vec2 uvOffset;
	vec2 uvScale;
} pc;

layout(location = 0) out vec2 fragTexCoord;

out gl_PerVertex {
	vec4 gl_Position;
};

void main() {
	vec2 uv = vec2(gl_VertexIndex & 1, gl_VertexIndex >> 1);
	fragTexCoord = uv * pc.uvScale + pc.uvOffset;
	gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
