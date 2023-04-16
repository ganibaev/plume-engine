#version 460

layout (location = 0) in vec3 inColor;
layout (location = 3) in float lightIntensity;

layout (location = 0) out vec4 outFragColor;

layout (set = 0, binding = 0) uniform CamSceneData
{
	mat4 view;
	mat4 proj;
	mat4 viewproj;
	vec4 fogColor; // w for exponent
	vec4 fogDistances; // x -- min, y -- max
	vec3 ambientColor;
	float ambientLight;
	vec4 sunlightDirection; // w for sun power
	vec4 sunlightColor;
} camSceneData;

void main()
{
	outFragColor = vec4(lightIntensity * (inColor + camSceneData.ambientColor), 1.0f);
}