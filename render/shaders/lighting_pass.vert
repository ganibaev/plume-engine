#version 460

layout (location = 0) out vec2 outTexCoords;

void main()
{
	outTexCoords = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(outTexCoords * 2.0 - 1.0, 0.0, 1.0);
}