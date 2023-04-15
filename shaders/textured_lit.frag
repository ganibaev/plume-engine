#version 460
#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout (location = 0) in vec3 inColor;
layout (location = 1) in vec2 texCoord;
layout (location = 2) flat in uint matID;

layout (location = 0) out vec4 outFragColor;

layout (push_constant) uniform constants
{
	vec4 data;
	mat4 renderMatrix;
} PushConstants;

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

layout (set = 2, binding = 0) uniform sampler2D tex[];

void main()
{
	vec3 color = texture(tex[matID], texCoord).xyz;
	outFragColor = vec4(color, 1.0f);
}