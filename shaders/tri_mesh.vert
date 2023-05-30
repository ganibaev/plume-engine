#version 460

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vColor;
layout (location = 3) in vec2 vTexCoord;
layout (location = 4) in uint vMatID;

layout (location = 0) out vec3 outColor;
layout (location = 1) out vec2 texCoord;
layout (location = 2) flat out uint matID;
layout (location = 3) out vec3 fragPosWorld;
layout (location = 4) out vec3 fragNormalWorld;

layout (set = 0, binding = 0) uniform CameraBuffer
{
	mat4 view;
	mat4 invView;
	mat4 proj;
	mat4 viewproj;
	vec4 fogColor; // w for exponent
	vec4 fogDistances; // x -- min, y -- max
	vec4 ambientLight;
	vec4 sunlightDirection; // w for sun power
	vec4 sunlightColor;
	vec4 pointLightPosition;
	vec4 pointLightColor;
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

	// normal transform, no non-uniform scaling
	fragNormalWorld = normalize(modelMatrix * vec4(vNormal, 0.0)).xyz;
	// lightIntensity = max(dot(normalWorldSpace, camSceneData.sunlightDirection.xyz), 0) + camSceneData.ambientLight.a;

	vec4 vPositionWorld = modelMatrix * vec4(vPosition, 1.0);
	fragPosWorld = vPositionWorld.xyz;

	// mat4 transformMatrix = (camSceneData.viewproj * modelMatrix);
	gl_Position = camSceneData.viewproj * vPositionWorld;

	outColor = vColor;
	texCoord = vTexCoord;
	matID = vMatID;
}