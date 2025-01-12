#version 460

layout (set = 0, binding = 0) uniform samplerCube skyboxSampler;
layout (location = 0) in vec3 inUVW;

layout (location = 0) out vec4 outFragColor;

void main()
{
	outFragColor = texture(skyboxSampler, inUVW);
}