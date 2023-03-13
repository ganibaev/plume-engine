#version 460

layout (location = 0) in vec3 inColor;

layout (location = 0) out vec4 outFragColor;

layout (set = 0, binding = 0) uniform CamSceneData
{
	mat4 view;
	mat4 proj;
	mat4 viewproj;
	vec4 fogColor; // w for exponent
	vec4 fogDistances; // x -- min, y -- max
	vec4 ambientColor;
	vec4 sunlightDirection; // w for sun power
	vec4 sunlightColor;
} camSceneData;

void main()
{
	// return red
	outFragColor = vec4(inColor + camSceneData.ambientColor.xyz, 1.0f);
}