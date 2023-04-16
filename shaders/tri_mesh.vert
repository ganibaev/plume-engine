#version 460

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vColor;
layout (location = 3) in vec2 vTexCoord;
layout (location = 4) in uint vMatID;

layout (location = 0) out vec3 outColor;
layout (location = 1) out vec2 texCoord;
layout (location = 2) flat out uint matID;
layout (location = 3) out float lightIntensity;

layout (set = 0, binding = 0) uniform CameraBuffer
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

struct ObjectData
{
	mat4 model;
};

// object matrices 
layout (std140, set = 1, binding = 0) readonly buffer ObjectBuffer
{
	ObjectData objects[];
} objectBuffer;

layout (push_constant) uniform constants
{
	vec4 data;
	mat4 renderMatrix;
} PushConstants;

void main()
{
	mat4 modelMatrix = objectBuffer.objects[gl_BaseInstance].model;

	// no non-uniform scaling
	vec3 normalWorldSpace = normalize(modelMatrix * vec4(vNormal, 0.0)).xyz;
	lightIntensity = max(dot(normalWorldSpace, camSceneData.sunlightDirection.xyz), 0) + camSceneData.ambientLight;

	mat4 transformMatrix = (camSceneData.viewproj * modelMatrix);
	gl_Position = transformMatrix * vec4(vPosition, 1.0f);

	outColor = vColor;
	texCoord = vTexCoord;
	matID = vMatID;
}